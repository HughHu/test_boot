/* Standard includes. */
#include <stdio.h> /* For printf() */
#include <string.h>

#include "contiki.h"
#include "IOMuxManager.h"
#include "chip.h"
//#include "cmn_sysctrl_reg_venus.h"
//#include "ana_aon_regfile_reg_venus.h"
//#include "aon_efuse_ctrl_reg_venus.h"
#include "log_print.h"
//#include "Driver_UART.h"
#include "stub_load.h"
#include "main.h"
#include "platform.h"
#include "spiflash.h"
#include "list.h"
#include "queue.h"

PROCESS_NAME(uart_boot_process);
/*---------------------------------------------------------------------------*/
PROCESS(flash_prog_process, "flash program process");
PROCESS(erase_a_block_process, "erase process");
PROCESS(program_process, "program process");

/*---------------------------------------------------------------------------*/

//flash_prog_t flash_prog = {0, 0, {{(uint8_t *)AP_SRAM_BASE, 0, 0},
//		{(uint8_t *)AP_SRAM_BASE + LOAD_BLK_SIZE, 0, 0}}};
flash_prog_t flash_prog = {0, 0, 0, 0, (uint8_t *)AP_SRAM_BASE, 0, 0, {{0, 0}}};
extern FLASH_DEV flash_dev;

static int flash_chip_erase = 0; // 1: chip erase, 0: sector erase

void flash_prog_init()
{
    process_start(&flash_prog_process, NULL);
    process_start(&erase_a_block_process, NULL);
    process_start(&program_process, NULL);
}

//int flash_erase_a_sector(FLASH_DEV *dev, unsigned int FlashAddr)
PROCESS_THREAD(erase_a_block_process, ev, data)
{
    static unsigned int j, RetData;
    int ret;
    unsigned int result, timeout = flash_dev.timeout;
    static unsigned long FlashAddr;
    uint32_t remain_size = 0;

    PROCESS_BEGIN();
    while(1) {
    	PROCESS_WAIT_EVENT();
    	if(ev != PROCESS_EVENT_POLL) {
//    		put_str("ers->");
    		FlashAddr = *((unsigned long *)data);
    		if(FlashAddr & (SPIROM_SECTOR_SIZE - 1)) { //not align with sector
				ret = -1;
				goto END;
			}
    	}

		/*-- write enable --*/
		result = spirom_cmd_send(&flash_dev, SPIROM_CMD_WREN, 0x0, 0, NULL, &RetData);
		if(result != 0) {
			ret = -1;
			goto END;
		}
		for(j = 1; j < timeout; j++) {
			/*-- get enable status --*/
			result = spirom_cmd_send(&flash_dev, SPIROM_CMD_RDST, 0x0, 0, NULL, &RetData);
			if(result != 0) {
				ret = -1;
				goto END;
			}
			if(RetData & SPIROM_SR_BP_MASK) {
				ret = -1;
				goto END;
			} else if(RetData & SPIROM_SR_WEL_MASK) {
				break;
			}
			process_poll(&erase_a_block_process);
			PROCESS_YIELD();
		}
		if((RetData & SPIROM_SR_WEL_MASK) == 0) {
			ret = -1;
			goto END;
		}
		/*-- erase --*/
		remain_size = flash_prog.flash_offset + flash_prog.total_size - FlashAddr;

        
        if(flash_chip_erase == 1) {
            // chip erase
            result = spirom_cmd_send(&flash_dev, SPIROM_OP_CHIP_ERASE, 0x0, 0, NULL, &RetData);
            /*-- get erase status --*/
            for(j = 1; j < timeout; j++) {
                result = spirom_cmd_send(&flash_dev, SPIROM_CMD_RDST, 0x0, 0, NULL, &RetData);          
                volatile int delay = 10000;
                while(delay--)
                    ;
                if(result != 0) {
                    ret = -1;
                    goto END;
                }
                if((RetData & (SPIROM_SR_WIP_MASK | SPIROM_SR_WEL_MASK)) == 0) {
                    break;
                }
                process_poll(&erase_a_block_process);
                PROCESS_YIELD();
            }
            if((RetData & (SPIROM_SR_WIP_MASK | SPIROM_SR_WEL_MASK)) != 0) {
                ret = -1;
                goto END;
            }           
            flash_prog.erase_size += remain_size;
        } else {
            if(!(FlashAddr & SPIROM_BLK64_MASK) && remain_size >= SPIROM_BLK64_SIZE) {
                result = spirom_cmd_send(&flash_dev, SPIROM_CMD_ERASE_B64, FlashAddr, 0, NULL, &RetData);
                flash_prog.erase_size += SPIROM_BLK64_SIZE;
            } else if(!(FlashAddr & SPIROM_BLK32_MASK) && remain_size >= SPIROM_BLK32_SIZE) {
                result = spirom_cmd_send(&flash_dev, SPIROM_CMD_ERASE_B32, FlashAddr, 0, NULL, &RetData);
                flash_prog.erase_size += SPIROM_BLK32_SIZE;
            } else {
                result = spirom_cmd_send(&flash_dev, SPIROM_CMD_ERASE, FlashAddr, 0, NULL, &RetData);
                flash_prog.erase_size += SPIROM_SECTOR_SIZE;
            }
        }
        
		if(result != 0) {
			ret = -1;
			goto END;
		}

		/*-- get erase status --*/
		for(j = 1; j < timeout; j++) {
			result = spirom_cmd_send(&flash_dev, SPIROM_CMD_RDST, 0x0, 0, NULL, &RetData);
			if(result != 0) {
				ret = -1;
				goto END;
			}
			if((RetData & (SPIROM_SR_WIP_MASK | SPIROM_SR_WEL_MASK)) == 0) {
				break;
			}
			process_poll(&erase_a_block_process);
			PROCESS_YIELD();
		}
		if((RetData & (SPIROM_SR_WIP_MASK | SPIROM_SR_WEL_MASK)) != 0) {
			ret = -1;
			goto END;
		}
		ret = 0;
		END:
		process_post(&flash_prog_process, PROCESS_EVENT_CONTINUE, (void *)ret);
    }

    PROCESS_END();
}


//int program_a_sector(FLASH_DEV *dev, unsigned int FlashAddr, unsigned char* start, unsigned int DataSize)
PROCESS_THREAD(program_process, ev, data)
{
	unsigned int result, timeout = flash_dev.timeout;
	int ret;
	static unsigned int j, RetData, flash_addr, remain_size, step_size;
	static unsigned char *data_buf;

	PROCESS_BEGIN();
	while(1) {
		PROCESS_WAIT_EVENT();
		if(ev != PROCESS_EVENT_POLL) {
//			put_str("wrt->");
			flash_addr = ((flash_ops_data *)data)->flash_addr;
			remain_size = ((flash_ops_data *)data)->size;
			data_buf = ((flash_ops_data *)data)->data;
			if((unsigned int)data_buf & 0x3) { //data address is not 4 bytes aligned
				ret = -1;
				goto END;
			}
		}

		do {
			step_size = SPIROM_PAGE_SIZE - (flash_addr & SPIROM_PAGE_MASK);
			step_size = (step_size > remain_size) ? remain_size : step_size;
			/*-- write enable --*/
			result = spirom_cmd_send(&flash_dev, SPIROM_CMD_WREN, 0x0, 0, NULL, &RetData);
			if(result != 0) {
				ret = -1;
				goto END;
			}
			for(j = 1; j < timeout; j++) {
				/*-- get enable status --*/
				result = spirom_cmd_send(&flash_dev, SPIROM_CMD_RDST, 0x0, 0, NULL, &RetData);
				if(result != 0) {
					ret = -1;
					goto END;
				}
				if(RetData & SPIROM_SR_BP_MASK) {
					ret = -1;
					goto END;
				} else if(RetData & SPIROM_SR_WEL_MASK) {
					break;
				}
				process_poll(&program_process);
				PROCESS_YIELD();
			}
			if((RetData & SPIROM_SR_WEL_MASK) == 0) {
				ret = -1;
				goto END;
			}

			result = spirom_cmd_send(&flash_dev, SPIROM_CMD_PROGRAM, flash_addr, step_size,
					(unsigned int *)data_buf, &RetData);
			if(result != 0) {
				ret = -1;
				goto END;
			}
//			for(j = 0; j < 80; j++) { // remove it after test
//				for(volatile int i = 0; i < 1000; i++) {}
//				process_poll(&program_process);
//				PROCESS_YIELD();
//			}
			/*-- ckeck completion --*/
			for(j = 1; j < timeout; j++) {
				result = spirom_cmd_send(&flash_dev, SPIROM_CMD_RDST, 0x0, 0, NULL, &RetData);
				if(result != 0) {
					ret = -1;
					goto END;
				}
				if((RetData & (SPIROM_SR_WIP_MASK | SPIROM_SR_WEL_MASK)) == 0) {
					break;
				}
				process_poll(&program_process);
				PROCESS_YIELD();
			}
			if((RetData & (SPIROM_SR_WIP_MASK | SPIROM_SR_WEL_MASK)) != 0) {
				ret = -1;
				goto END;
			}

			flash_addr += step_size;
			data_buf += step_size;
			remain_size -= step_size;
		}while(remain_size);

		ret = 0;
		END:  // return value
		process_post(&flash_prog_process, PROCESS_EVENT_CONTINUE, (void *)ret);
	}

	PROCESS_END();
}

int32_t erase_sectors(void *addr)
{
	uint32_t flash_addr = *((uint32_t *)addr);

	if(flash_prog.flash_offset + flash_prog.erase_size > flash_addr) {  // erased already
		return 0;
	} else {
		BOOT_LOG("era-%d-%d->\n", flash_addr, flash_prog.erase_size);
		process_post(&erase_a_block_process, PROCESS_EVENT_CONTINUE, addr);
		return 1;
	}
}

flash_ops_data flash_ops;

PROCESS_THREAD(flash_prog_process, ev, data)
{
	static int idx;
	static char event;

	PROCESS_BEGIN();

	while(1) {
		PROCESS_WAIT_EVENT();
		if(ev == PROCESS_EVENT_ERASE) {
            if((int)data == 0xCAFE000E) { // erase whole flash
                event = PROCESS_EVENT_PROG_OK;
                do {
                    flash_chip_erase = 1;
                    if(erase_sectors(&flash_ops.flash_addr)) {
                        PROCESS_WAIT_EVENT();
                        if((int)data) {
                            event = PROCESS_EVENT_PROG_ERR;
                            break;
                        } else {
                            flash_prog.erase_size = flash_prog.total_size;
                            flash_ops.flash_addr = 0;
                            flash_prog.cnt = 0;
                        }
                    }
                } while(0);
                process_post(&uart_boot_process,  event, NULL);
            } else {
                flash_ops.flash_addr = flash_prog.flash_offset;
                event = PROCESS_EVENT_PROG_OK;
                flash_chip_erase = 0;
                do {

                    if(erase_sectors(&flash_ops.flash_addr)) {
                        PROCESS_WAIT_EVENT();
                        if((int)data) {
                            event = PROCESS_EVENT_PROG_ERR;
                            break;
                        }
                        flash_ops.flash_addr = flash_prog.flash_offset + flash_prog.erase_size;
                        flash_prog.cnt = flash_prog.total_size - flash_prog.erase_size;
                    }
                } while(flash_prog.cnt > 0);
                process_post(&uart_boot_process,  event, NULL);
                continue;
            }
		}

		while(1) {
			idx = flash_get_rdy_buf();
			if(idx < 0)
				break;
			flash_ops.data = flash_prog.data_ctrl[idx].buf_idx * LOAD_BLK_SIZE + flash_prog.load_base;
			flash_ops.flash_addr = flash_prog.flash_offset + LOAD_BLK_SIZE * flash_prog.cnt;
			flash_ops.size = flash_prog.data_ctrl[idx].size;
			flash_ops.ctrl_idx = idx;

			//erase a sector
            flash_chip_erase = 0;
			if(erase_sectors(&flash_ops.flash_addr)) {
				while(1) {
					PROCESS_WAIT_EVENT();
					if(ev != PROCESS_EVENT_BUF_RDY) {
						break;
					}
				}
				if((int)data) {
					event = PROCESS_EVENT_PROG_ERR;
					goto END;
				}
			}

			//program a sector
			process_post(&program_process, PROCESS_EVENT_CONTINUE, (void *)&flash_ops);
			while(1) {
				PROCESS_WAIT_EVENT();
				if(ev != PROCESS_EVENT_BUF_RDY) {
					break;
				}
			}
			if((int)data)  {
				event = PROCESS_EVENT_PROG_ERR;
				goto END;
			}
			event = PROCESS_EVENT_BUF_FREE;
			// update information of flash_prog
			flash_prog.cnt++;

			END:
			flash_set_buf_free();
			// send event
			process_post(&uart_boot_process,  event, NULL);
			BOOT_LOG("out-%d->\n", flash_ops.ctrl_idx);
		}
	}

	PROCESS_END();
}
