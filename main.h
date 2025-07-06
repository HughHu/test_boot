/*
 * main.h
 *
 *  Created on: Apr 20, 2018
 *
 */

#ifndef INCLUDE_MAIN_H_
#define INCLUDE_MAIN_H_

#include "stub_load.h"
#include "log_print.h"

#define ROM_CODE_VERSION    0x05

//#define ROM_DBG // DO NOT DEFINE IN UPGRADE WHICH USE UART0 AS WHILE

#ifdef ROM_DBG
#include <sys/time.h>

#define BOOT_LOG(fmt, ...)    do{ \
	struct timeval tp; \
	gettimeofday(&tp, NULL); \
	uint32_t sec = (uint32_t)tp.tv_sec; \
	uint32_t us = (uint32_t)tp.tv_usec; \
	CLOG("[%d.%d]",sec,us); \
	CLOG("    "fmt, ##__VA_ARGS__);}while(0)

#else
#define BOOT_LOG
#endif


// TODO
#define AP_FREE_SRAM      0x20050000
// RX_BUF CAN NOT reach 0x200A4000 which is the start address of data ram
#define AP_RX_BUF_BASE    0x200A0000
#define AP_FLASH_BASE     0x30000000

/*4K buffer*/
#define BUF_SIZE_ADJ        128
#define MAX_WRITE_BLOCK     (1024 * 8 - BUF_SIZE_ADJ)
#define VECTOR_BLK_SIZE     384
#define LOAD_BLK_SIZE       (1024 * 4)
#define LOAD_BLK_NUM        16

// size 8k - BUF_SIZE_ADJ
#define SLIP_RX_BUF         (AP_RX_BUF_BASE)
// size 4k + BUF_SIZE_ADJ
#define DATA_RX_BUF         (SLIP_RX_BUF + MAX_WRITE_BLOCK)
// size 4k * n
#define AP_SRAM_BASE        (AP_FREE_SRAM)


enum threads
{
    THD_MISC = 0,
    THD_FLASH,
    THD_TFCARD,
    THD_UART,
    THD_USB,
    THD_VER_HDR,
    THD_VER_IMG,
    THD_CPY_IMG,
    THD_SIZE    //must keep this item at the end of the enum
};

/*
 	.ascii   "Hr"                                 //   byte(0~1):   header mark
	.byte    0x01, 0x00                           //   byte(2~3):   header version 1.0
	.long    0                                    //   byte(4~7):   image size
	.long    __Vectors                            //   byte(8~11):  image vma
	.byte    0x00, 0x00, 0x01                     //   byte(12~14): image version 0.0.1
	.byte    0x00                                 //   byte(15~15): image flags
	.ascii   "\0\0\0\0", "\0\0\0\0", "\0\0\0\0"   //   byte(16~27): image name
	.short   0x00                                 //   byte(28~29): patch_offset
	.short   0x00                                 //   byte(30~31): gpt_offset
	.space   (7 * 4)                              //   byte(32~59): reserved
	.short   0x0                                  //   byte(60~61): header check sum
	.short   0x0                                  //   byte(62~63): vector check sum
*/
typedef struct {
	uint8_t  vectors[192];
	uint8_t  hdr_mark[2];
	uint8_t  hdr_version[2];
	uint32_t img_size;
	uint32_t img_vma;
	uint8_t  img_version[3];
	uint8_t  img_flags;
	uint8_t  img_name[12];
	uint16_t patch_offset;
	uint16_t gpt_offset;
	uint32_t reserve[7];
	uint16_t hdr_cs;
	uint16_t vec_cs;
}img_header;

typedef struct rom_funcs {
	void (*patch_init)(void *param);
}rom_funcs;

typedef enum {
	BOOT_OPS_XTDBG,
	BOOT_OPS_USB,
	BOOT_OPS_UART,
	BOOT_OPS_FLASH,
}BOOT_OPS;

typedef enum {
    VERIFY_STATUS_UNKNOWN = 0xFF,
    VERIFY_STATUS_FAIL = 0,
    VERIFY_STATUS_OK = 1
} verify_status_t;

typedef enum {
	PROCESS_EVENT_UART_RXD = 0x10,
	PROCESS_EVENT_BUF_RDY,
	PROCESS_EVENT_BUF_FREE,
	PROCESS_EVENT_PROG_ERR,
	PROCESS_EVENT_ERASE,
	PROCESS_EVENT_PROG_OK
}PROCESS_EVENT_t;

//typedef struct {
//	unsigned char *data;
//	unsigned short size;
//	unsigned short rdy;
//}buf_cb_t;

typedef struct {
	uint16_t size;
	uint16_t buf_idx;
}data_ctrl_t;

typedef struct {
	uint32_t flash_offset;
	int32_t total_size;
	int32_t erase_size;
	int32_t cnt;
	uint8_t *load_base;
	uint8_t ctrl_head, ctrl_tail;
//	uint16_t load_sz[LOAD_BLK_NUM];
	data_ctrl_t data_ctrl[LOAD_BLK_NUM];
}flash_prog_t;

typedef struct {
	unsigned int flash_addr;
	unsigned char *data;
	unsigned short size;
	short ctrl_idx;
}flash_ops_data;

typedef struct {
	uint32_t sd_offset;
	int32_t total_size;
	int32_t erase_size;
	int32_t cnt;
	uint8_t *load_base;
	uint8_t ctrl_head, ctrl_tail;
	data_ctrl_t data_ctrl[LOAD_BLK_NUM];
}sd_prog_t;

typedef struct {
	unsigned int sd_addr;
	unsigned char *data;
	unsigned short size;
	short ctrl_idx;
}sd_ops_data;

extern void uart_init();
extern int8_t header_verify(uint8_t *buf);
extern void run_image(uint8_t *addr);


extern void do_bootops(void *param);
extern void boot_flash();
extern void system_init(int stage);

extern void flash_prog_init();
extern int32_t flash_prog_in_process();
extern int32_t flash_mem_cpy();

extern void sd_prog_init();
extern int32_t sd_prog_in_process();
extern int32_t sd_mem_cpy();
int sd_card_probe(int enable_4bit, void *config_io);

//#ifdef ROM_DBG
//extern void put_str(char *str);
//extern void put_hex(uint32_t value, uint32_t bits);
//#define put_u32(val)  put_hex(val, 32)
//#define put_u16(val)  put_hex(val, 16)
//#define put_u8(val)   put_hex(val, 8)
//#else
//#define put_str(str)
//#define put_u32(val)
//#define put_u16(val)
//#define put_u8(val)
//#endif


//#define VERIFY_OK(status)       ((status) == VERIFY_STATUS_OK)
//#define VERIFY_FAILED(status)   ((status) == VERIFY_STATUS_FAIL)
//
//extern struct pt threads_pt[THD_SIZE];
//extern char threads_sch[THD_SIZE];
//extern struct pt_sem data_rdy;
//extern NDS_DRIVER_USART Driver_USART1;
////extern bool verify_header_status, verify_image_status;
//extern verify_status_t verify_header_status, verify_image_status;
extern volatile uint32_t rom_code_version;
//
////param: index of threads
//#define IS_THREAD_STARTED(v)    ( threads_sch[v] == 1 )
//#define THREADS_START(v)        PT_INIT(&threads_pt[v]); threads_sch[v] = 1
//#define THREADS_STOP(v)         threads_sch[v] = 0
//#define PT_SEM_INIT_WAIT(pt, s) PT_SEM_INIT(s, 0); PT_SEM_WAIT(pt, s)
//
////secure configuration
//extern uint32_t efuse_sec_cfg; //00:disable; 01:permissive; 1x:enforcing
//#define IS_SEC_DISABLED     (efuse_sec_cfg == 0)
//#define IS_SEC_ENABLED      (efuse_sec_cfg > 0)
//#define IS_SEC_PERMISSIVE   (efuse_sec_cfg == 1)
//#define IS_SEC_ENFORCING    (efuse_sec_cfg >= 2)

#endif /* INCLUDE_MAIN_H_ */
