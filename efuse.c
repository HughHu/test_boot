#include "chip.h"
#include "efuse.h"

#define KEY_FILE_FLAG   "LSKEY"

#define MAX_EFUSE_FILE_SIZE  0x200

// 0 - Flash; 1 - SD; [2-15] - reserved
int efuse_boot_option()
{
	int ret = -1;
	char *buf = (char *)&(IP_EFUSE_CTRL->REG_AUTO_LOAD_18);
	if(buf[1] == 0x00)
		ret = 0;
	else if(buf[1] == 0x01)
		ret = 1;
	else
		ret = (buf[1] & 0x0F);

	return ret;
}

// 0 - AP enabled; 1 - AP disabled
int efuse_boot_ap_disable()
{
	int ret = -1;
	char *buf = (char *)&(IP_EFUSE_CTRL->REG_AUTO_LOAD_18);
	if((buf[0] & 0x0F) == 0x00)
		ret = 0;
	else
		ret = 1;

	return ret;
}

// secure boot mode: 0 - None, 1 - CRC32, 2 - SHA256, 3 - ECSDA256, 4 - RSA2048
int efuse_boot_secure_enable()
{
	int ret = -1;
	char *buf = (char *)&(IP_EFUSE_CTRL->REG_AUTO_LOAD_18);
    ret = (buf[0] & 0xF0)>>4;

	return ret;
}

int efuse_boot_config_read()
{
	int ret = -1;
	char *buf = (char *)&(IP_EFUSE_CTRL->REG_AUTO_LOAD_18);
	if(buf[2] == 0x00)
		ret = 0;
	else
		ret = buf[2];

	return ret;
}

int efuse_boot_ota_header_offset()
{
	int ret = -1;
	char *buf = (char *)&(IP_EFUSE_CTRL->REG_AUTO_LOAD_18);
	if(buf[3] == 0x00)
		ret = 0;
	else
		ret = buf[3];
	return ret;
}

int efuse_boot_debug_protect_enable()
{
	int ret = -1;
	uint32_t *buf = (uint32_t *)&(IP_EFUSE_CTRL->REG_AUTO_LOAD_19);
    if (*buf != 0)
        ret = 1; //enable protect
    else
        ret = 0;  //disable protect

    return ret;
}

void efuse_program_ctrl(char enable)
{
	if(enable) {  //enable program
		if(!IP_EFUSE_CTRL->REG_PROG_PROTECT.all) {
			IP_EFUSE_CTRL->REG_PROG_PROTECT.all = 0xcafeef02;
		}
	} else {  //disable program
		if(IP_EFUSE_CTRL->REG_PROG_PROTECT.all) {
			IP_EFUSE_CTRL->REG_PROG_PROTECT.all = 0xcafeef02;
		}
	}

}

int8_t efuse_write_bit(uint8_t addr, uint8_t bit)
{
	int8_t ret = -1;
	if(addr < 0x80 && bit < 0x20) {
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_ADDR = (bit << 7) | addr;
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_TYPE = 1;  //program mode
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_START = 1;
		while(IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_START);
		ret = 0;
	}

	return ret;
}

int8_t efuse_read_word(uint8_t addr, uint32_t *val)
{
	int8_t ret = -1;

	if(addr < 0x80) {
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_ADDR = addr;
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_TYPE = 0;  //read mode
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_MARGIN_RD = 0; //normal read
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_START = 1;
		while(IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_START);
		*val = IP_EFUSE_CTRL->REG_RD_DATA.all;
		ret = 0;
	}
	return ret;
}

int8_t efuse_write_word(uint8_t addr, uint32_t val)
{
	int8_t ret = 0, i;

	if(addr < 0x80) {
		for(i = 0; i < 32; i++) {
			if(val & (1UL << i)) {
				ret = efuse_write_bit(addr, i);
				if(ret)
					break;
			}
		}
	} else {
		ret = -1;
	}
	return ret;
}

int8_t efuse_read_word_mr(uint8_t addr, uint32_t *val)
{
	int8_t ret = -1;

	if(addr < 0x80) {
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_ADDR = addr;
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_TYPE = 0;  //read mode
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_MARGIN_RD = 1; //margin read
		IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_START = 1;
		while(IP_EFUSE_CTRL->REG_CMD_CTL.bit.EFU_CMD_START);
		*val = IP_EFUSE_CTRL->REG_RD_DATA.all;
		ret = 0;
	}
	return ret;
}

