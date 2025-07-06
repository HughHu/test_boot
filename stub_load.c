#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "chip.h"
#include "stub_load.h"
#include "main.h"
#include "spiflash.h"
#include "uart_burn_md5.h"
#include "contiki.h"
#include "efuse.h"
#include <stdlib.h>

#include "ClockManager.h"
#include "clock_config.h"
#include "secure.h"

extern flash_prog_t flash_prog;
extern uint32_t cur_baud_rate, nxt_baud_rate;
PROCESS_NAME(flash_prog_process);

extern sd_prog_t sd_prog;
int32_t s_sd_block_start = 0;
PROCESS_NAME(sd_prog_process);

uart_buf_t ub;
uart_buf_state_t ubs;

int32_t *s_mem_cpy_dat;
uint32_t s_mem_cpy_len;
//s_mem_i: index of blocks current writing
//s_mem_len: length of bytes already write in current block,
int32_t s_mem_i, s_mem_len;
//if get mem_data and mem_finish cmds before mem_begin, initial data can do these case
// s_mem_remaining records the remaining bytes that needs to be received;
int8_t* s_mem_offset = NULL;
int32_t s_mem_remaining = 0x01;
//int32_t s_flash_offset = -1;
int32_t s_mem_seq = 0;

void
ub_state_save()
{
    BOOT_LOG("ub_state_save, read=%d, state=%d\n", ub.read, ub.state);
    ubs.read = ub.read;
    ubs.state = ub.state;
}

void
ub_state_recovery()
{
    ub.read = ubs.read;
    ub.state = ubs.state;
    BOOT_LOG("ub_state_recovery, read=%d, state=%d\n", ub.read, ub.state);
}

void
ub_state_init()
{
    ub.reading_buf = (uint8_t*)DATA_RX_BUF;
    ub.read = 0;
    ub.state = 0;
    ub.command = NULL;
    ub.error = 0;
}

void
pll_init(pll_clk_div_t *pll_clk_div)
{
	if(!pll_clk_div || pll_clk_div->pll_enable_flag == 0) {
		return;
	}

	// enable SYSPLL
	IP_SYSNODEF->REG_SYSPLL_CFG0.bit.SYSPLL_ENABLE = 0x1;
	while(!IP_SYSNODEF->REG_SYSPLL_CFG0.bit.SYSPLL_LOCK);

	// set core clk, default 300M
	if(pll_clk_div->cpu_cfg_para != INVALID_PLL_VALUE) {
		CRM_InitCoreSrc(pll_clk_div->cpu_cfg_para);
	} else {
		// Init cpu run 300M
		CRM_InitCoreSrc(BOARD_BOOTCLOCKRUN_CRM_CORE_CFG_PARA);
	}

	// set flash clk, default 100M
	if(pll_clk_div->flash_clk_div != INVALID_PLL_VALUE) {
		CRM_InitFlashSrc(BOARD_BOOTCLOCKRUN_CRM_FLASH_CFG_PARA);

		// divider: 0 ~ 31
		HAL_CRM_SetFlashClkDiv(pll_clk_div->flash_clk_div);
		HAL_CRM_SetFlashClkSrc(BOARD_BOOTCLOCKRUN_FLASH_CLK_SRC);
	} else {
		CRM_InitFlashSrc(BOARD_BOOTCLOCKRUN_CRM_FLASH_CFG_PARA);

		// FLASH 100M
		HAL_CRM_SetFlashClkDiv(BOARD_BOOTCLOCKRUN_FLASH_CLK_M);
		HAL_CRM_SetFlashClkSrc(BOARD_BOOTCLOCKRUN_FLASH_CLK_SRC);
	}

	// Init CRM peri clock 100M Fixed
	CRM_InitPeriSrc(BOARD_BOOTCLOCKRUN_CRM_PERI_CFG_PARA);

	if(pll_clk_div->aon_cfg_pclk_div != INVALID_PLL_VALUE) {
		// AON_CFG_PCLK 300/aon_cfg_pclk
		HAL_CRM_SetAon_cfg_pclkClkDiv(BOARD_BOOTCLOCKRUN_AON_CFG_PCLK_CLK_N, pll_clk_div->aon_cfg_pclk_div);
	} else {
		// AON_CFG_PCLK 300/8=37.5M
		HAL_CRM_SetAon_cfg_pclkClkDiv(BOARD_BOOTCLOCKRUN_AON_CFG_PCLK_CLK_N, BOARD_BOOTCLOCKRUN_AON_CFG_PCLK_CLK_M);
	}

	if(pll_clk_div->cmn_peri_pclk_div != INVALID_PLL_VALUE) {
		// CMN_PERI_PCLK 300/cmn_peri_pclk_div
		HAL_CRM_SetCmn_peri_pclkClkDiv(BOARD_BOOTCLOCKRUN_CMN_PERI_PCLK_CLK_N, pll_clk_div->cmn_peri_pclk_div);
	} else {
		// CMN_PERI_PCLK 300/4=75M
		HAL_CRM_SetCmn_peri_pclkClkDiv(BOARD_BOOTCLOCKRUN_CMN_PERI_PCLK_CLK_N, BOARD_BOOTCLOCKRUN_CMN_PERI_PCLK_CLK_M);
	}

	if(pll_clk_div->ap_peri_pclk_div != INVALID_PLL_VALUE) {
		// AP_PERI_PCLK 300/ap_peri_pclk
		HAL_CRM_SetAp_peri_pclkClkDiv(BOARD_BOOTCLOCKRUN_AP_PERI_PCLK_CLK_N, pll_clk_div->ap_peri_pclk_div);
	} else {
		// AP_PERI_PCLK 300/4=75M
		HAL_CRM_SetAp_peri_pclkClkDiv(BOARD_BOOTCLOCKRUN_AP_PERI_PCLK_CLK_N, BOARD_BOOTCLOCKRUN_AP_PERI_PCLK_CLK_M);
	}

	if((pll_clk_div->hclk_div != INVALID_PLL_VALUE) && (pll_clk_div->hclk_div != 0)) {
		// HCLK = 300M / hclk_div
		HAL_CRM_SetHclkClkDiv(BOARD_BOOTCLOCKRUN_HCLK_CLK_N, pll_clk_div->hclk_div);
		HAL_CRM_SetHclkClkSrc(BOARD_BOOTCLOCKRUN_HCLK_CLK_SRC);
	} else {
		// HCLK = 300M
		HAL_CRM_SetHclkClkDiv(BOARD_BOOTCLOCKRUN_HCLK_CLK_N, BOARD_BOOTCLOCKRUN_HCLK_CLK_M);
		HAL_CRM_SetHclkClkSrc(BOARD_BOOTCLOCKRUN_HCLK_CLK_SRC);
	}
}

esp_command_error
uart_receive_bytes(uint8_t* bytes, int32_t len)
{
    int32_t i;
    int16_t r = 0;
    BOOT_LOG("uart_receive_bytes len is %d\n", len);
    for (i = 0; i < len; i++) {
        r = SLIP_recv_byte(bytes[i], (slip_state_t*)&ub.state);
        //BOOT_LOG("uart_receive_bytes %d byte is %02x, r is %d\n", i, (uint8_t)bytes[i], r);
        if (r >= 0) {
            ub.reading_buf[ub.read++] = (uint8_t) r;
            if (ub.read == MAX_WRITE_BLOCK) {
                /* shouldn't happen unless there are data errors */
                r = SLIP_FINISHED_FRAME;
            }
        }
        if (r == SLIP_FINISHED_FRAME) {
            /* end of frame, set 'command' */
            ub.read = 0;
            break;
        }
    }

    return r == SLIP_FINISHED_FRAME ? ESP_OK : ESP_NOT_ENOUGH_DATA;
}

esp_command_error
verify_data_len(esp_command_req_t* command, uint8_t len)
{
    return (command->data_len == len) ? ESP_OK : ESP_BAD_DATA_LEN;
}

int32_t
_mem_data_cpy(void* data, uint32_t length)
{
    int8_t* src = (int8_t*)data;

	memcpy((s_mem_offset + s_mem_len), src, length);
	s_mem_remaining -= length;
	s_mem_len += length;

    return length;
}

int32_t sd_prog_in_process()
{
    // empty return 0, other return 1
	return (sd_prog.ctrl_head == sd_prog.ctrl_tail ? 0 : 1);
}

int32_t sd_get_free_buf()
{
	int32_t idx = -1, tail;

	tail = (sd_prog.ctrl_tail + 1) % LOAD_BLK_NUM;
	if(tail != sd_prog.ctrl_head)  // buffer is not full
		idx = sd_prog.ctrl_tail;
	return idx;
}

int32_t sd_get_rdy_buf()
{
	int32_t idx = -1, head;
	if(sd_prog.ctrl_head != sd_prog.ctrl_tail) {
		idx = sd_prog.ctrl_head;
	}
	return idx;
}

void sd_set_buf_rdy()
{
	int32_t tail, idx;

	tail = (sd_prog.ctrl_tail + 1) % LOAD_BLK_NUM;
	if(tail != sd_prog.ctrl_head) { // buffer is not full
		idx = sd_prog.data_ctrl[sd_prog.ctrl_tail].buf_idx;
		sd_prog.ctrl_tail = tail;
		sd_prog.data_ctrl[tail].buf_idx = (idx + 1) % (LOAD_BLK_NUM - 1); //update next buf_idx
	}
}

void sd_set_buf_size(uint32_t ctrl_idx, uint32_t size)
{
	if(ctrl_idx < LOAD_BLK_NUM && size <= LOAD_BLK_SIZE) {
		sd_prog.data_ctrl[ctrl_idx].size = size;
	}
}

void sd_set_buf_free()
{
    int32_t head, idx;
    if(sd_prog.ctrl_head != sd_prog.ctrl_tail) { // buffer is not empty
    	idx = sd_prog.ctrl_head;
    	sd_prog.data_ctrl[idx].size = 0;
    	sd_prog.ctrl_head = (idx + 1) % LOAD_BLK_NUM;
    }
}

int32_t _sd_mem_cpy(void *data, int length)
{
	int8_t* src = (int8_t*)data;
	int32_t remain, len = 0;

	int32_t idx = sd_get_free_buf();
	data_ctrl_t *pbuf_cb = &sd_prog.data_ctrl[idx];
	int32_t buf_idx = pbuf_cb->buf_idx;

	if(idx >= 0) {  //free buffer is ready
		remain = LOAD_BLK_SIZE - pbuf_cb->size;
		len = length > remain ? remain : length;
		memcpy((sd_prog.load_base + buf_idx * LOAD_BLK_SIZE + pbuf_cb->size), src, len);
		s_mem_remaining -= len;
		pbuf_cb->size += len;
		if(s_mem_remaining == 0 || pbuf_cb->size == LOAD_BLK_SIZE) {
            BOOT_LOG("dat-%d-%d->\n", idx, buf_idx);

			if(sd_prog_in_process() == 0) {
			    process_post(&sd_prog_process,  PROCESS_EVENT_BUF_RDY, (void *)idx);  // send event to sd program
			}
			sd_set_buf_rdy();
		}
	}

	return len;
}

int32_t sd_mem_cpy()
{
	int32_t len;

    len = _sd_mem_cpy(s_mem_cpy_dat, s_mem_cpy_len);
    if(len) {
		s_mem_cpy_dat = (int32_t *)((char *)s_mem_cpy_dat + len);
		s_mem_cpy_len -= len;
    }

    return len;
}

esp_command_error
handle_sd_begin(uint32_t size, uint32_t en_4bit, uint32_t config_io, uint32_t offset)
{
    if(sd_card_probe(en_4bit, (void *)config_io)) {
        BOOT_LOG("sd card probe failed\n");
        return ESP_ERR_SD_PROBE;
    }

    sd_prog_init();

    s_sd_block_start = offset;

    s_mem_i = 0;
    s_mem_len = 0;
    s_mem_offset = (int8_t *)AP_SRAM_BASE;
    s_mem_remaining = size;
    s_mem_seq = -1;

    sd_prog.cnt = 0;
    sd_prog.sd_offset = offset;
    sd_prog.total_size = size;
    sd_prog.erase_size = size; // faked erase size for sd card
    sd_prog.ctrl_head = 0;
    sd_prog.ctrl_tail = 0;
    for(int i = 0 ; i < LOAD_BLK_NUM; i++) {
    	sd_prog.data_ctrl[i].buf_idx = 0;
    	sd_prog.data_ctrl[i].size = 0;
    }

    BOOT_LOG("handle_sd_begin data size is %d\n", size);
    return ESP_OK;
}

esp_command_error
handle_sd_data(void* data, uint32_t seq_num, uint32_t length)
{
    if (s_mem_offset == NULL && length > 0) {
        return ESP_NOT_IN_FLASH_MODE;
    }
    if (length > s_mem_remaining) {
        return ESP_TOO_MUCH_DATA;
    }
    if (seq_num == s_mem_seq) {
        //get the same package again, it is fine, but do nothing
        return ESP_OK;
    }
    if (seq_num != s_mem_seq + 1) {
        return ESP_BAD_DATA_SEQ;
    }

    s_mem_seq = seq_num;
    s_mem_cpy_dat = data;
    s_mem_cpy_len = length;

    return ESP_OK;
}

esp_command_error
handle_sd_finish()
{
    esp_command_error res = s_mem_remaining > 0 ? ESP_NOT_ENOUGH_DATA : ESP_OK;
    BOOT_LOG("handle_sd_finish remain size is %d\n", s_mem_remaining);
    s_mem_remaining = 0x1;
//    s_mem_offset = NULL;
    return res;
}

// Among 32 bytes (8 uint32) of Data, theoretically 4 uint32 are used for MEM_BEGIN:
// Total Size, Number of Data Packets, Data Size in one Packet, Memory Offset.
// Actually here only 1st uint32 (Total Size) is used for MEM_BEGIN!
esp_command_error
handle_mem_begin(uint32_t size, uint32_t offset)
{
    s_mem_i = 0;
    s_mem_len = 0;
    s_mem_offset = (int8_t *)offset;
    s_mem_remaining = size;
    s_mem_seq = -1;

    BOOT_LOG("handle_mem_begin data size is %d\n", size);
    return ESP_OK;
}

esp_command_error
handle_flash_begin(uint32_t size, uint32_t offset)
{
    s_mem_i = 0;
    s_mem_len = 0;
    s_mem_offset = (int8_t *)AP_SRAM_BASE;
    s_mem_remaining = size;
    s_mem_seq = -1;

    flash_prog.cnt = 0;
    flash_prog.flash_offset = offset;
    flash_prog.total_size = size;
    flash_prog.erase_size = 0;
    flash_prog.ctrl_head = 0;
    flash_prog.ctrl_tail = 0;
    for(int i = 0 ; i < LOAD_BLK_NUM; i++) {
    	flash_prog.data_ctrl[i].buf_idx = 0;
    	flash_prog.data_ctrl[i].size = 0;
    }

    BOOT_LOG("handle_flash_begin data size is %d\n", size);
    return ESP_OK;
}



// Among 32 bytes (8 uint32) of Data, theoretically >=4 uint32 are used for MEM_DATA:
// Data Size, Sequence Number, 0, 0, and then Data
// Actually here the 2st uint32 (Sequence Number) and Data itself are used for MEM_BEGIN!
esp_command_error
handle_mem_data(void* data, uint32_t seq_num, uint32_t length)
{
    if (s_mem_offset == NULL && length > 0) {
        return ESP_NOT_IN_FLASH_MODE;
    }
    if (length > s_mem_remaining) {
        return ESP_TOO_MUCH_DATA;
    }
    if (seq_num == s_mem_seq) {
        //get the same package again, it is fine, but do nothing
        return ESP_OK;
    }
    if (seq_num != s_mem_seq + 1) {
        return ESP_BAD_DATA_SEQ;
    }
    s_mem_seq = seq_num;
    // save data and size for copying later
    _mem_data_cpy(data, length);

    return ESP_OK;
}

esp_command_error
handle_flash_data(void* data, uint32_t seq_num, uint32_t length)
{
    if (s_mem_offset == NULL && length > 0) {
        return ESP_NOT_IN_FLASH_MODE;
    }
    if (length > s_mem_remaining) {
        return ESP_TOO_MUCH_DATA;
    }
    if (seq_num == s_mem_seq) {
        //get the same package again, it is fine, but do nothing
    	s_mem_cpy_len = 0;
        return ESP_OK;
    }
    if (seq_num != s_mem_seq + 1) {
        return ESP_BAD_DATA_SEQ;
    }
    s_mem_seq = seq_num;

    s_mem_cpy_dat = data;
    s_mem_cpy_len = length;

    return ESP_OK;
}

int32_t flash_prog_in_process()
{
    // empty return 0, other return 1
	return (flash_prog.ctrl_head == flash_prog.ctrl_tail ? 0 : 1);
}

int32_t flash_get_free_buf()
{
	int32_t idx = -1, tail;

	tail = (flash_prog.ctrl_tail + 1) % LOAD_BLK_NUM;
	if(tail != flash_prog.ctrl_head)  // buffer is not full
		idx = flash_prog.ctrl_tail;
	return idx;
}

int32_t flash_get_rdy_buf()
{
	int32_t idx = -1, head;
	if(flash_prog.ctrl_head != flash_prog.ctrl_tail) {
		idx = flash_prog.ctrl_head;
	}
	return idx;
}

void flash_set_buf_rdy()
{
	int32_t tail, idx;

	tail = (flash_prog.ctrl_tail + 1) % LOAD_BLK_NUM;
	if(tail != flash_prog.ctrl_head) { // buffer is not full
		idx = flash_prog.data_ctrl[flash_prog.ctrl_tail].buf_idx;
		flash_prog.ctrl_tail = tail;
		flash_prog.data_ctrl[tail].buf_idx = (idx + 1) % LOAD_BLK_NUM; //update next buf_idx
	}
}

void flash_set_buf_size(uint32_t ctrl_idx, uint32_t size)
{
	if(ctrl_idx < LOAD_BLK_NUM && size <= LOAD_BLK_SIZE) {
		flash_prog.data_ctrl[ctrl_idx].size = size;
	}
}

void flash_set_buf_free()
{
    int32_t head, idx;
    if(flash_prog.ctrl_head != flash_prog.ctrl_tail) { // buffer is not empty
    	idx = flash_prog.ctrl_head;
    	flash_prog.data_ctrl[idx].size = 0;
    	flash_prog.ctrl_head = (idx + 1) % LOAD_BLK_NUM;
    }
}

int32_t _flash_mem_cpy(void *data, int length)
{
	int8_t* src = (int8_t*)data;
	int32_t remain, len = 0;

	int32_t idx = flash_get_free_buf();
	data_ctrl_t *pbuf_cb = &flash_prog.data_ctrl[idx];
	int32_t buf_idx = pbuf_cb->buf_idx;

	if(idx >= 0) {  //free buffer is ready
		remain = LOAD_BLK_SIZE - pbuf_cb->size;
		len = length > remain ? remain : length;
		memcpy((flash_prog.load_base + buf_idx * LOAD_BLK_SIZE + pbuf_cb->size), src, len);
		s_mem_remaining -= len;
		pbuf_cb->size += len;
		if(s_mem_remaining == 0 || pbuf_cb->size == LOAD_BLK_SIZE) {
            BOOT_LOG("dat-%d-%d->\n", idx, buf_idx);

			if(flash_prog_in_process() == 0) {
			    process_post(&flash_prog_process,  PROCESS_EVENT_BUF_RDY, (void *)idx);  // send event to flash program
			}
			flash_set_buf_rdy();
		}
	}

	return len;
}

int32_t flash_mem_cpy()
{
	int32_t len;

    len = _flash_mem_cpy(s_mem_cpy_dat, s_mem_cpy_len);
    if(len) {
		s_mem_cpy_dat = (int32_t *)((char *)s_mem_cpy_dat + len);
		s_mem_cpy_len -= len;
    }

    return len;
}


// Among 32 bytes (8 uint32) of Data, theoretically 2 uint32 are used for MEM_END:
// Execute Flag, Entry point Address
// Actually none of them is used for MEM_END!
esp_command_error
handle_mem_finish()
{
    esp_command_error res = s_mem_remaining > 0 ? ESP_NOT_ENOUGH_DATA : ESP_OK;
    BOOT_LOG("handle_mem_finish remain size is %d\n", s_mem_remaining);
    s_mem_remaining = 0x1;
//    s_mem_offset = NULL;
    return res;
}

esp_command_error
handle_flash_finish()
{
    esp_command_error res = s_mem_remaining > 0 ? ESP_NOT_ENOUGH_DATA : ESP_OK;
    BOOT_LOG("handle_flash_finish remain size is %d\n", s_mem_remaining);
    s_mem_remaining = 0x1;
    s_mem_offset = NULL;
    return res;
}

esp_command_error
handle_flash_erase(uint32_t offset, uint32_t bytes)
{
    if(bytes == 0xCAFE000E) {
        // erase whole flash
        process_post(&flash_prog_process,  PROCESS_EVENT_ERASE, (void *)bytes);
    } else {
        flash_prog.flash_offset = offset;
        flash_prog.total_size = bytes;
        flash_prog.erase_size = 0;
        flash_prog.cnt = bytes;
        process_post(&flash_prog_process,  PROCESS_EVENT_ERASE, NULL);
    }
	return ESP_OK;
}

esp_command_error
handle_read_reg(uint32_t addr, uint32_t *value)
{
	if(addr & 0x3) {
		return ESP_INVALID_COMMAND;
	}
	*value = inw(addr);
	return ESP_OK;
}

esp_command_error
handle_write_reg(uint32_t addr, uint32_t value, uint32_t mask)
{
	uint32_t org;
	if(addr & 0x3) {
		return ESP_INVALID_COMMAND;
	}
	org = inw(addr);
	outw(addr, (org & (~mask)) | (value & mask));
	return ESP_OK;
}

uint8_t
calculate_checksum(uint8_t* buf, int length)
{
    int i;
    uint8_t res = 0xef;
    for (i = 0; i < length; i++) {
        res ^= buf[i];
    }
    return res;
}

#define CMD_HDR_LEN     ( sizeof(esp_command_req_t) - sizeof(((esp_command_req_t*)0)->data_buf) )
bool
check_cmd_buf(uint8_t* buf, int32_t buflen)
{
    esp_command_req_t* command;
    if (buf == NULL || buflen < CMD_HDR_LEN)
        return false;

    command = (esp_command_req_t*)buf;
    if (buflen < CMD_HDR_LEN + command->data_len)
        return false;

    return true;
}


#define __HAL_EFUSE_CLK_ENABLE()    \
do { \
	IP_AON_CTRL->REG_AON_CLK_CTRL.bit.AON_SEL_EFUSE_CLK = 0x1; \
    IP_AON_CTRL->REG_AON_CLK_CTRL.bit.ENA_EFUSE_CLK = 0x1; \
} while(0)

#define __HAL_EFUSE_CLK_DISABLE()    \
do { \
    IP_AON_CTRL->REG_AON_CLK_CTRL.bit.ENA_EFUSE_CLK = 0x0; \
} while(0)

#define __HAL_EFUSE_POWER_ENABLE()    \
do { \
    IP_AON_CTRL->REG_AON_TUNE2.bit.EN_PSW_EFUSE = 0x1; \
} while(0)

#define __HAL_EFUSE_POWER_DISABLE()    \
do { \
    IP_AON_CTRL->REG_AON_TUNE2.bit.EN_PSW_EFUSE = 0x0; \
} while(0)


esp_command_error
handle_efuse_cmd_write_data(void* data, uint32_t length)
{
    struct efuse_item_info* efuse_info = (struct efuse_item_info *)data;

	if((length + efuse_info->addr) > (512)) {
		return ESP_TOO_MUCH_DATA;
	}

    // word aligned required
    uint8_t addr = (efuse_info->addr >> 2);
    uint32_t *pdata = (uint32_t *)(efuse_info->data);
    uint32_t val_in, val_out;

	// use pclk for efuse
	// enable efuse
    __HAL_EFUSE_CLK_ENABLE();

	// enable power for efuse program
	__HAL_EFUSE_POWER_ENABLE();

	// disable redundancy mode
	IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_REDUNDANCY_ENA_B = 0x1;
	IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_REDUNDANCY_ROW_SEL = 0x0;

	// read once before write
	efuse_read_word(0, &val_out);

    // write the values
    for(int i = 0; i < efuse_info->length; i += 4) {
        val_in = *(pdata);
        val_out = 0;
        int ret;
    	efuse_program_ctrl(1);
        ret = efuse_write_word(addr, val_in);
    	efuse_program_ctrl(0);
        if(ret) {
            return ESP_BAD_DATA_READBACK;
        }
        pdata++;

        ret = efuse_read_word(addr, &val_out);
        if(val_in != val_out || ret) {
            return ESP_BAD_DATA_READBACK;
        }
        addr++;
    }


    addr = (efuse_info->addr >> 2);
	efuse_program_ctrl(1);
    if((efuse_info->perm & EFUSE_PERM_DISABLE_WRITE) == EFUSE_PERM_DISABLE_WRITE) {
        int bits = ((efuse_info->length - 1) >> 4) + 1;
        int pos = (addr >> 2);
        while(bits > 0) {
            efuse_write_bit(16, pos);
            bits--;
            pos++;
        }
    }
    if((efuse_info->perm & EFUSE_PERM_DISABLE_READ) == EFUSE_PERM_DISABLE_READ) {
        int bits = ((efuse_info->length - 1) >> 4) + 1;
        int pos = (addr >> 2);
        while(bits > 0) {
            efuse_write_bit(17, pos);
            bits--;
            pos++;
        }
    }   
	efuse_program_ctrl(0);

	// read once again to have right read access
	efuse_read_word(0, &val_out);

	// disable power for efuse program
	// __HAL_EFUSE_POWER_DISABLE();

	// disable efuse
	// __HAL_EFUSE_CLK_DISABLE();

    return ESP_OK;
}

esp_command_error
handle_efuse_cmd_gen_data(void* data, uint32_t length)
{
    // generate and write to efuse, then set the permission
    struct efuse_item_info* efuse_info = (struct efuse_item_info *)data;

	if((length + efuse_info->addr) > (512)) {
		return ESP_TOO_MUCH_DATA;
	}

	// generate random data
	srand(SysTimer_GetLoadValue());
	for(int i = 0; i < efuse_info->length; i++) {
		efuse_info->data[i] = (uint8_t)rand();
	}

    // word aligned required
    uint8_t addr = (efuse_info->addr >> 2);
    uint32_t *pdata = (uint32_t *)(efuse_info->data);
    uint32_t val_in, val_out;

	// use pclk for efuse
	// enable efuse
    __HAL_EFUSE_CLK_ENABLE();

	// enable power for efuse program
	__HAL_EFUSE_POWER_ENABLE();

	// disable redundancy mode
	IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_REDUNDANCY_ENA_B = 0x1;
	IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_REDUNDANCY_ROW_SEL = 0x0;

    // write the values
    for(int i = 0; i < efuse_info->length; i += 4) {
        val_in = *(pdata);
        val_out = 0;
        int ret;
        ret = efuse_write_word(addr, val_in);
        if(ret) {
            return ESP_BAD_DATA_READBACK;
        }
        pdata++;

        ret = efuse_read_word(addr, &val_out);
        if(val_in != val_out || ret) {
            return ESP_BAD_DATA_READBACK;
        }
        addr++;
    }


	efuse_program_ctrl(1);
    if((efuse_info->perm & EFUSE_PERM_DISABLE_WRITE) == EFUSE_PERM_DISABLE_WRITE) {
        int bits = ((efuse_info->length - 1) >> 4) + 1;
        int pos = (addr >> 2);
        while(bits > 0) {
            efuse_write_bit(16, pos);
            bits--;
            pos++;
        }
    }
    if((efuse_info->perm & EFUSE_PERM_DISABLE_READ) == EFUSE_PERM_DISABLE_READ) {
        int bits = ((efuse_info->length - 1) >> 4) + 1;
        int pos = (addr >> 2);
        while(bits > 0) {
            efuse_write_bit(17, pos);
            bits--;
            pos++;
        }
    }
	efuse_program_ctrl(0);

	// read once again to have right read access
	efuse_read_word(0, &val_out);

	// disable power for efuse program
	__HAL_EFUSE_POWER_DISABLE();

	// disable efuse
	// __HAL_EFUSE_CLK_DISABLE();

    return ESP_OK;
}

esp_command_error
handle_enc_start_data(void* data, uint32_t length, uint32_t *key_buf)
{
    int res = secure_init();
    secure_get_local_public_key(key_buf);
    secure_set_peer_public_key(data, key_buf+16);

    return ESP_OK;
}

int32_t
do_cmd(uint8_t* buf, int32_t* len, comm_type comm)
{
    bool is_valid_cmd = true;
    esp_command_req_t* command = (esp_command_req_t*)buf;  //default value

    if (comm == COMM_TYPE_UART) {
        //set value to ub.command, start to handle cmd
        ub.command = (esp_command_req_t*)ub.reading_buf;
        command = ub.command;
    }

    if(command->op != ENC_START && secure_decrypt_data(command)<0)
        return -1;

    /* provide easy access for 32-bit data words */
    uint32_t* data_words = (uint32_t*)command->data_buf;
    /* response header */
    esp_command_response_t resp = {
        .resp = 1,
        .op_ret = command->op,
        .len_ret = 2, /* esptool.py ignores this value, but other users of the stub may not */
        .value = 0,
    };
    //default value for error and status
    esp_command_error error = ESP_CMD_NOT_IMPLEMENTED;
    int32_t bytes = 0, status = 0, ret = -1;
    uint8_t cs;
    uint32_t data_ext[17];	// __attribute__((aligned(4)));
    uint8_t* dbuf = command->data_buf + 16;
    int32_t dlen = command->data_len - 16;

    /* First stage of command processing - before sending error/status */
    BOOT_LOG("cmd is 0x%02x, size is %d, checksum is 0x%x\n",
                command->op, command->data_len, command->checksum);
    switch (command->op) {
        case ESP_MEM_BEGIN:
            error = verify_data_len(command, 16) || handle_mem_begin(data_words[0], data_words[3]);
            BOOT_LOG("ESP_MEM_BEGIN error code is %d\n", error);
            break;
        case ESP_MEM_DATA:
			BOOT_LOG("ESP_MEM_DATA data_size=%d, seq_num=%d\n", data_words[0], data_words[1]);
			cs = calculate_checksum(dbuf, dlen);
			if (cs == (uint8_t)command->checksum) {
				error = handle_mem_data(dbuf, data_words[1], dlen);
			} else {
				error = ESP_BAD_DATA_CHECKSUM;
			}
			BOOT_LOG("ESP_MEM_DATA error code is %d\n", error);
            break;
        case ESP_MEM_END:
            error = verify_data_len(command, 8) || handle_mem_finish();
            BOOT_LOG("ESP_MEM_END error code is %d\n", error);
            break;
        case ESP_SYNC:
            error = verify_data_len(command, 36);
            BOOT_LOG("ESP_SYNC error code is %d\n", error);
            break;
        case ESP_READ_VERSION:
            error = verify_data_len(command, 0);
            status = rom_code_version;
            BOOT_LOG("ESP_READ_VERSION error code is %d, version is %d\n", error, rom_code_version);
            break;
        case ESP_FLASH_BEGIN:
        	error = verify_data_len(command, 16) || handle_flash_begin(data_words[0], data_words[3]);
        	BOOT_LOG("ESP_FLASH_BEGIN error code is %d\n", error);
        	break;
        case ESP_FLASH_DATA:
        	BOOT_LOG("ESP_FLASH_DATA data_size=%d, seq_num=%d\n", data_words[0], data_words[1]);
			cs = calculate_checksum(dbuf, dlen);
			if (cs == (uint8_t)command->checksum) {
				error = handle_flash_data(dbuf, data_words[1], dlen);
			} else {
				error = ESP_BAD_DATA_CHECKSUM;
			}
			BOOT_LOG("ESP_FLASH_DATA error code is %d\n", error);
        	break;
        case ESP_FLASH_END:
        	error = verify_data_len(command, 4) || handle_flash_finish();
        	BOOT_LOG("ESP_FLASH_END error code is %d\n", error);
        	break;
        case ESP_FLASH_VERIFY_MD5:
        	error = mbedtls_md5_ret((uint8_t*)data_words[0] + AP_FLASH_BASE, data_words[1], data_ext);
			if(error){
				error = ESP_IMG_UNKNOWN_ERROR;
			}
			bytes = 16;
			BOOT_LOG("ESP_FLASH_VERIFY_MD5 error code is %d\n", error);
        	break;
        case ESP_SET_BAUD:
        	if(data_words[1] != cur_baud_rate) {
        		error = ESP_INVALID_COMMAND;
        	} else {
        		nxt_baud_rate = data_words[0];
        		error = ESP_OK;
        	}
        	break;
        case ESP_ERASE_REGION:
        	//data_words[0]:flash offset, data_words[1]:erase bytes
        	error = handle_flash_erase(data_words[0], data_words[1]);
        	break;
        case ESP_READ_REG:
        	error = verify_data_len(command, 4) || handle_read_reg(data_words[0], &(resp.value));
        	if(error){
				error = ESP_IMG_UNKNOWN_ERROR;
			}
        	BOOT_LOG("ESP_READ_REG error code is %d\n", error);
        	break;
        case ESP_WRITE_REG:
        	error = handle_write_reg(data_words[0], data_words[1], data_words[2]);
        	break;
        case EFUSE_CMD_WRITE_DATA:
			BOOT_LOG("EFUSE_CMD_WRITE_DATA address=%d, length=%d\n", (data_words[6] & 0x0000FFFF), ((data_words[6] >> 16) & 0x0000FFFF));
			cs = calculate_checksum(dbuf, dlen);
			if (cs == (uint8_t)command->checksum) {
				error = handle_efuse_cmd_write_data(dbuf + 8, dlen - 8);
			} else {
				error = ESP_BAD_DATA_CHECKSUM;
			}
			BOOT_LOG("EFUSE_CMD_WRITE_DATA error code is %d\n", error);
            break;
        case EFUSE_CMD_GEN_DATA:
			BOOT_LOG("EFUSE_CMD_GEN_DATA address=%d, length=%d\n", (data_words[6] & 0x0000FFFF), ((data_words[6] >> 16) & 0x0000FFFF));
			cs = calculate_checksum(dbuf, dlen);
			if (cs == (uint8_t)command->checksum) {
				error = handle_efuse_cmd_gen_data(dbuf + 8, dlen - 8);
			} else {
				error = ESP_BAD_DATA_CHECKSUM;
			}
			BOOT_LOG("EFUSE_CMD_GEN_DATA error code is %d\n", error);
            break;
        case PLL_EN:
        	pll_clk_div_t *pClkDiv = (pll_clk_div_t *)command->data_buf;
        	pll_init(pClkDiv);
            error = ESP_OK;
        	break;
        case ESP_SD_BEGIN:
            error = verify_data_len(command, 16) || handle_sd_begin(data_words[0], data_words[1], data_words[2], data_words[3]);
            break;
        case ESP_SD_DATA:
            cs = calculate_checksum(dbuf, dlen);
            if (cs == (uint8_t)command->checksum) {
                error = handle_sd_data(dbuf, data_words[1], dlen);
            } else {
                error = ESP_BAD_DATA_CHECKSUM;
            }
            break;
        case ESP_SD_END:
            error = verify_data_len(command, 4) || handle_sd_finish();
            break;
        case FLASH_CONFIG:
            extern FLASH_DEV flash_dev;
            flash_dev.addr_bytes = (command->data_buf[0] != 4) ? 3 : 4;
            flash_dev.dualflash_mode = (command->data_buf[1] != 0) ? true : false;
            error = flash_init(&flash_dev, 0, 0);
        default:
            is_valid_cmd = false;
            break;
    }

	if(error != 0) {
		// if error occurs, status is supposed to set to 1
		status = 1;
	}

	if (comm == COMM_TYPE_UART) {
		//set error state for post-do-cmd
		ub.error = error;

		SLIP_init((char*)buf, (char*)NULL);
		/*respons package*/
		SLIP_send_frame_delimiter();
		SLIP_send_frame_data_buf(&resp, sizeof(esp_command_response_t));
		SLIP_send_frame_data(error);
		SLIP_send_frame_data(status);
		if(bytes) {
			SLIP_send_frame_data_buf(data_ext, bytes);
		}
		SLIP_send_frame_delimiter();

		*len = SLIP_get_tx_size();
	}

    if(is_valid_cmd && (error == ESP_OK)) {
    	ret = command->op;
    }

    //return command id
    return ret;
}

void
set_resp_error(uint8_t* buf, uint8_t error, comm_type comm)
{
    //FIXME, not think about slip, simply write fix location
    //buf[1 + sizeof(esp_command_response_t)] = error;

    uint32_t idx = sizeof(esp_command_response_t);
    if (comm == COMM_TYPE_UART)
        idx++; //for SLIP delimiter

    //BSD: first error, then status?
    buf[idx] = error;
    buf[idx + 1] = (error == ESP_OK ? 0 : 1); //status
}

//prerequisite: buffer size is no less than sizeof(esp_command_response_t)+2
void
make_full_resp(uint8_t* buf, uint8_t command, uint8_t error)
{
    esp_command_response_t* pres = (esp_command_response_t*)buf;
    pres->resp = 1;
    pres->op_ret = command;
    pres->len_ret = 2; // error + status
    pres->value = 0;
    buf[ sizeof(esp_command_response_t) ] = error; //error
    buf[ sizeof(esp_command_response_t) + 1 ] = (error == ESP_OK ? 0 : 1); //status
}
