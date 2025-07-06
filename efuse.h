#ifndef _BOOT_EFUSE_H_
#define _BOOT_EFUSE_H_

#define EFUSE_PERM_READ_WRITE      0
#define EFUSE_PERM_DISABLE_READ    1
#define EFUSE_PERM_DISABLE_WRITE   2

struct efuse_item_info {
	uint16_t addr;
	uint16_t length;
	uint32_t perm;
	uint8_t  data[4];
};


int8_t efuse_write_bit(uint8_t addr, uint8_t bit);
int8_t efuse_read_word(uint8_t addr, uint32_t *val);
int8_t efuse_write_word(uint8_t addr, uint32_t val);
int efuse_file_valid();
int efuse_boot_option();
int efuse_boot_ap_disable();
int efuse_boot_secure_enable();
int efuse_boot_config_read();
int efuse_boot_debug_protect_enable();
int efuse_boot_ota_header_offset();
void efuse_program_ctrl(char enable);

#endif /* _BOOT_EFUSE_H_ */
