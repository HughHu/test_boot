/* Standard includes. */
#include <string.h>
#include "contiki.h"
#include "IOMuxManager.h"
#include "ClockManager.h"
#include "arcs_ap.h"
#include "Driver_GPIO.h"
#include "Driver_UART.h"
#include "stub_load.h"
#include "main.h"
#include "spiflash.h"
#include "systick.h"

#define  DEFAULT_BAUD_RATE  115200

extern uint32_t s_mem_cpy_len;

PROCESS(uart_boot_process, "uart boot process");
void* UART_Handler = NULL;
uint32_t cur_baud_rate = DEFAULT_BAUD_RATE, nxt_baud_rate = DEFAULT_BAUD_RATE;
volatile int32_t usart_tx_event_complete = 0;
static int32_t uart_initialized = 0;
static uint32_t uart_time_out_acc = 0;
static volatile uint32_t uart_time_out_max = 100;

static int32_t UART_Send_Polling(void* handler, uint8_t *buf, uint32_t len)
{
    int i;
    for (i = 0; i < len; i++){
        IP_UART0->REG_RXTX_BUFFER.all = buf[i];
        while(!IP_UART0->REG_STATUS.bit.TX_FIFO_SPACE)
            ;
    }

    while(IP_UART0->REG_STATUS.bit.TX_FIFO_SPACE < 16)
        ;

    // delay 10us/bit * 9 for the last UART data out
    int n = 24 * 10 * 9;
    while(--n) {
        __NOP();
    }

    return 0;
}

int32_t UART_Rx_Timeout_Process()
{
    if(0 == uart_initialized)
        return -1;

    static uint32_t uart_rx_cnt_prev = 0;
    static int32_t uart_rx_event_timeout = CSK_UART_EVENT_RX_TIMEOUT;
    CSK_UART_STATUS stat;
    UART_GetStatus(UART_Handler, &stat);
    uint32_t cnt = UART_GetRxCount(UART_Handler);
    if(stat.rx_busy) {
        if(cnt > 0) {
            if(cnt == uart_rx_cnt_prev) {
                if(process_nevents() < (PROCESS_CONF_NUMEVENTS >> 1)) {
                    process_post(&uart_boot_process,  PROCESS_EVENT_UART_RXD, &uart_rx_event_timeout);
                }
            }
            uart_rx_cnt_prev = cnt;
        }
    } else {
        uart_rx_cnt_prev = 0;

        if(cnt > 0) {
			if(process_nevents() < (PROCESS_CONF_NUMEVENTS >> 1)) {
				process_post(&uart_boot_process,  PROCESS_EVENT_UART_RXD, &uart_rx_event_timeout);
			}
        }
    }
    return 0;
}

void UART_EventCallback(uint32_t event, void* workspace)
{
    static int32_t usart_rx_event_complete = 0;

    switch(event) {
    case CSK_UART_EVENT_SEND_COMPLETE:
        usart_tx_event_complete = event;
        break;
    case CSK_UART_EVENT_RX_TIMEOUT:
        usart_rx_event_complete = event;
        // ignore the rx timeout event if the queue is almost full
        if(process_nevents() < (PROCESS_CONF_NUMEVENTS >> 1))
            process_post(&uart_boot_process,  PROCESS_EVENT_UART_RXD, &usart_rx_event_complete);
        break;
    case CSK_UART_EVENT_RECEIVE_COMPLETE:
        usart_rx_event_complete = event;
        process_post(&uart_boot_process,  PROCESS_EVENT_UART_RXD, &usart_rx_event_complete);
        break;
    default:
        break;
    }
}

void uart_dev_init(uint32_t baud_rate)
{
    UART_PowerControl(UART_Handler, CSK_POWER_OFF);

    UART_Uninitialize(UART_Handler);

    UART_Initialize(UART_Handler, UART_EventCallback, NULL);

    UART_PowerControl(UART_Handler, CSK_POWER_FULL);

    UART_Control(UART_Handler, CSK_UART_MODE_ASYNCHRONOUS |
                        CSK_UART_DATA_BITS_8 |
                        CSK_UART_PARITY_NONE |
                        CSK_UART_STOP_BITS_1 |
                        CSK_UART_FLOW_CONTROL_NONE |
                        CSK_UART_Function_CONTROL_Dma |
                        CSK_UART_GPIO_CONTROL_DEFAULT, baud_rate);

    UART_Control(UART_Handler, CSK_UART_CONTROL_TX, 1);
    UART_Control(UART_Handler, CSK_UART_CONTROL_RX, 1);
}

void uart_init(void)
{
    UART_Handler = UART0();

    __HAL_CRM_UART0_CLK_ENABLE();
    __HAL_CRM_DMA_CLK_ENABLE();

    memset((uint8_t*)SLIP_RX_BUF, 0, MAX_WRITE_BLOCK);

    uart_dev_init(cur_baud_rate);
    uart_initialized = 1;

//    UART_Send(UART_Handler, "012345678901234567890123456789", 30);  // just for debug
    ub_state_init();
    process_start(&uart_boot_process, NULL);
    flash_prog_init();
}

int32_t uart_wait_tx_rdy()
{
    CSK_UART_STATUS uart_status;

    while(usart_tx_event_complete == 0);
    do {
        UART_GetStatus(UART_Handler, &uart_status);
        if(uart_status.tx_busy == 0) break;
    } while(1);
    return 0;
}

PROCESS_THREAD(uart_boot_process, ev, data)
{
    static int32_t n = 0, cmd_id, rdy, error = 0;
    static uint32_t verify_header_status;
    static uint8_t *cmd = (uint8_t *)SLIP_RX_BUF;

    PROCESS_BEGIN();

    while(1) {
        if (! n) {
            BOOT_LOG("... UART Receive, %s - %d\n", __func__, __LINE__);
            uart_time_out_acc = 0;
            UART_Receive(UART_Handler, cmd, MAX_WRITE_BLOCK);
        }
        while(1) {
            PROCESS_WAIT_EVENT();
            if (ev == PROCESS_EVENT_UART_RXD) { // ignore other events
                BOOT_LOG("... UART RXD EVENT, %s - %d\n", __func__, __LINE__);
                break;
            } else if(ev == PROCESS_EVENT_PROG_ERR) {
                error = ESP_FAILED_SPI_OP;  // set error flag
                BOOT_LOG("... PROG ERR EVENT, %s - %d\n", __func__, __LINE__);
            }
        }
        n = UART_GetRxCount(UART_Handler);
        if(0 == n && *((uint32_t *)data) == CSK_UART_EVENT_RX_TIMEOUT) {
        	// it should be some timeout from last packet when 0
        	// ignore it to continue
        	BOOT_LOG("... UART RX timeout ignored\n");
            // set to none zero to avoid receive again
        	n = 1;
        	continue;
        }
        ub_state_save();
        if (uart_receive_bytes(cmd, n)) {
            BOOT_LOG("... UART RX TIMEOUT or REACH MAX, %s - %d\n", __func__, __LINE__);
            extern uart_buf_t ub;
            if(ub.read < 4) {
                BOOT_LOG("... UART RX - ub.read %d bytes\n", ub.read);
                goto UART_PROCESS_TIMEOUT;
            } 

            if(n >= MAX_WRITE_BLOCK) {
                BOOT_LOG("... UART RX - MAX_WRITE_BLOCK\n");
                error = ESP_TOO_MUCH_DATA; // set error flag
                goto UART_PROCESS_ERROR;
            }

            if(ub.reading_buf[0] != 0x00) {
                BOOT_LOG("... UART RX - direction error\n");
                error = ESP_INVALID_COMMAND; // set error flag
                goto UART_PROCESS_ERROR;
            }

            int len = ub.reading_buf[2] + (ub.reading_buf[3] << 8);
            BOOT_LOG("... UART RX - len = %d, ub.read = %d\n", len, ub.read);
            if(len >= MAX_WRITE_BLOCK || ub.read > (len + 24)) {
            	BOOT_LOG("... UART RX - too long\n");
                error = ESP_BAD_BLOCKSIZE; // set error flag
                goto UART_PROCESS_ERROR;
            }

UART_PROCESS_TIMEOUT:
            // waiting for more, should have a timeout set here
            uart_time_out_acc++;
            BOOT_LOG("... UART RX - time acc %d\n",uart_time_out_acc);
            if(uart_time_out_acc > uart_time_out_max) {
                BOOT_LOG("... UART RX - timeout\n");
                error = ESP_ERR_TIMEOUT; // set error flag
                uart_time_out_acc = 0;
                goto UART_PROCESS_ERROR;
            }

            // keep receiving data
            ub_state_recovery();
            continue;

UART_PROCESS_ERROR:
			BOOT_LOG("... UART PROCESS ERROR, send back resp with len %d\n",n);
			UART_Control(UART_Handler, CSK_UART_ABORT_RECEIVE, 1);

            if(ub.read < 2) {
                BOOT_LOG("... UART PROCESS ERROR- ub.read %d bytes\n", ub.read);
                cmd[2] = 0xFF; // set to unkown cmd if ub.read < 2
            }
			n = 0;
			ub.read = 0;
			ub.state = 0;

			cmd[0] = 0xC0; // start byte
			cmd[1] = 0x01; // direction byte
            // cmd[2] = cmd[2]; // keep the command byte
            cmd[3] = 0x02; // length
            cmd[4] = 0x00; // length
            cmd[5] = 0x00; // reserved
            cmd[6] = 0x00; // reserved
            cmd[7] = 0x00; // reserved
            cmd[8] = 0x00; // reserved
            cmd[9] = (uint8_t)error; // status
            cmd[10] = 0x01; // error
            cmd[11] = 0xC0; // end byte
			UART_Send_Polling(UART_Handler, cmd, 12);
            error = 0;  // clear error flag
			memset(cmd, 0, MAX_WRITE_BLOCK);
			continue;
        } else {
            BOOT_LOG("\nrx->\n");
            //get a complete package, deal it
            UART_Control(UART_Handler, CSK_UART_ABORT_RECEIVE, 1);

            cmd_id = do_cmd(cmd, &n, COMM_TYPE_UART);

            if(cmd_id == ESP_MEM_END) {
                extern int8_t* s_mem_offset;
                uint8_t * ap_base = (uint8_t *)s_mem_offset;
                s_mem_offset = NULL;
                verify_header_status = 0; //header_verify((uint8_t *)ap_base);
				usart_tx_event_complete = 0;
				BOOT_LOG("send back for baud rate change\n");
				UART_Send_Polling(UART_Handler, cmd, n);
//                    UART_Send(UART_Handler, cmd, n);
//                    uart_wait_tx_rdy();
				run_image((uint8_t *)ap_base);

				set_resp_error(cmd, verify_header_status, COMM_TYPE_UART); //ESP_CMD_NOT_IMPLEMENTED
            } else if(cmd_id == ESP_SET_BAUD) {
                if(cur_baud_rate != nxt_baud_rate) {
                    usart_tx_event_complete = 0;
//                    UART_Send(UART_Handler, cmd, n);
                    UART_Send_Polling(UART_Handler, cmd, n);
                    n = 0;
//                    uart_wait_tx_rdy();
                    uint32_t tick_curr = SysTick_Value();
                    // delay ~2ms, 12bytes transmit needs ~1ms
                    while((SysTick_Value() - tick_curr) < 2)
                        ;
                    // change baud rate
                    BOOT_LOG("change baud rate to %d\n", nxt_baud_rate);
                    cur_baud_rate = nxt_baud_rate;
                    uart_dev_init(cur_baud_rate);
                    memset(cmd, 0, MAX_WRITE_BLOCK);
                    continue;
                }
            } else if(cmd_id == ESP_FLASH_END) {
                do {
                    rdy = flash_prog_in_process();
                    BOOT_LOG("end- %d ->\n", rdy);
                    if(rdy) { // flash program in process
                        PROCESS_WAIT_EVENT();
                        if(ev == PROCESS_EVENT_PROG_ERR) {
                            error = ESP_FAILED_SPI_OP;  // set error flag
                        }
                        BOOT_LOG("flash program in process\n");
                    }
                } while(rdy);
            } else if(cmd_id == ESP_FLASH_DATA) {
                while(s_mem_cpy_len) {  // if the value is zero, do not need copy
                    int32_t len = flash_mem_cpy();
                    if(len == 0) {  // no free buffer to copy
                        BOOT_LOG("wait->\n");
                        PROCESS_WAIT_EVENT();
                        if(ev == PROCESS_EVENT_PROG_ERR) {
                            error = ESP_FAILED_SPI_OP;  // set error flag
                        }
                    }
                }
            } else if (cmd_id == ESP_ERASE_REGION) { // wait erase finish
                do {
                    PROCESS_WAIT_EVENT();
                    if(ev == PROCESS_EVENT_PROG_OK) {
                        break;
                    } else if(ev == PROCESS_EVENT_PROG_ERR) {
                        error = ESP_FAILED_SPI_OP;  // set error flag
                        break;
                    }
                } while(1);
            } else if(cmd_id == ESP_SD_DATA) {
                while(s_mem_cpy_len) {  // if the value is zero, do not need copy
                    int32_t len = sd_mem_cpy();
                    if(len == 0) {  // no free buffer to copy
                        BOOT_LOG("wait->\n");
                        PROCESS_WAIT_EVENT();
                        if(ev == PROCESS_EVENT_PROG_ERR) {
                            error = ESP_FAILED_SPI_OP;  // set error flag
                        }
                    }
                }
            } else if(cmd_id == ESP_SD_END) {
                do {
                    rdy = sd_prog_in_process();
                    BOOT_LOG("end- %d ->\n", rdy);
                    if(rdy) { // sd program in process
                        PROCESS_WAIT_EVENT();
                        if(ev == PROCESS_EVENT_PROG_ERR) {
                            error = ESP_FAILED_SPI_OP;  // set error flag
                        }
                        BOOT_LOG("sd program in process\n");
                    }
                } while(rdy);
            } 

            if(error) {
                BOOT_LOG("error- %d ->\n", error);
                set_resp_error(cmd, error, COMM_TYPE_UART);
                error = 0;  // clear error flag
            }
            //send a response
//            UART_Send(UART_Handler, cmd, n);
            BOOT_LOG("send back resp %d with len %d\n", cmd_id, n);
            UART_Send_Polling(UART_Handler, cmd, n);
            //reset n for next rx frame
            memset(cmd, 0, MAX_WRITE_BLOCK);
            n = 0;
        }
    }

    PROCESS_END();
}
