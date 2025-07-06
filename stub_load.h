#ifndef _STUB_LOAD_H_
#define _STUB_LOAD_H_

#include <stddef.h>
#include <stdint.h>
#include "slip.h"

#define INVALID_PLL_VALUE  0xFF
//2 x 8K buffer, total 32K
//#define MEM_DATA_BLOCKS       1
//#define MEM_DATA_BLOCK_SIZE   (8 * 1024)
//#define MEM_DATA_BUF_SIZE     (MEM_DATA_BLOCKS * MEM_DATA_BLOCK_SIZE)

/* Full set of protocol commands */
typedef enum
{
    ESP_READ_VERSION = 0x01,
    ESP_FLASH_BEGIN = 0x02,
    ESP_FLASH_DATA = 0x03,
    ESP_FLASH_END = 0x04,

    /* Commands support */
    ESP_MEM_BEGIN = 0x05,
    ESP_MEM_END = 0x06,
    ESP_MEM_DATA = 0x07,
    ESP_SYNC = 0x08,
    ESP_WRITE_REG = 0x09,
    ESP_READ_REG = 0x0a,


    ESP_SPI_SET_PARAMS = 0x0b,
    ESP_PIN_READ = 0x0c, /* ??? */
    ESP_SPI_ATTACH = 0x0d,
    ESP_SPI_READ = 0x0e,
    ESP_SET_BAUD = 0x0f,
    ESP_FLASH_DEFLATED_BEGIN = 0x10,
    ESP_FLASH_DEFLATED_DATA = 0x11,
    ESP_FLASH_DEFLATED_END = 0x12,
    ESP_FLASH_VERIFY_MD5 = 0x13,

    /* Stub-only commands */
    ESP_ERASE_FLASH = 0xD0,
    ESP_ERASE_REGION = 0xD1,
    ESP_READ_FLASH = 0xD2,
    ESP_RUN_USER_CODE = 0xD3,

    EFUSE_CMD_START = 0x20,
    EFUSE_CMD_WRITE_DATA = 0x21,
    EFUSE_CMD_GEN_DATA = 0x22,
    EFUSE_CMD_END = 0x23,

    // start encrypt
    ENC_START  = 0x30,
    PLL_EN	= 0x31,
    FLASH_CONFIG = 0x32,

    ESP_SD_BEGIN = 0x40,
    ESP_SD_DATA = 0x41,
    ESP_SD_END = 0x42,

} esp_command;


/* Command request header */
typedef struct
__attribute__((packed))
{
    uint8_t zero;
    uint8_t op; /* maps to esp_command enum */
    uint16_t data_len;
    int32_t checksum;
    uint8_t data_buf[32]; /* actually variable length, determined by data_len */
} esp_command_req_t;

/* Command response header */
typedef struct
{
    uint8_t resp; /* should be '1' */
    uint8_t op_ret; /* Should match 'op' */
    uint16_t len_ret; /* Length of result data (can be ignored as SLIP framing helps) */
    uint32_t value; /* 32-bit response used by some commands */
} esp_command_response_t;


/* command response has some (optional) data after it, then 2 (or 4 on ESP32 ROM)
   bytes of status.
 */
typedef struct
__attribute__((packed))
{
    uint8_t error; /* non-zero = failed */
    uint8_t status; /* status of a failure */
} esp_command_data_status_t;

enum verify_status
{
    VER_OK = 0,

    VER_INIT,
    VER_START,
    VER_FAIL
};

/* Error codes */
typedef enum
{
    ESP_OK = 0,

    ESP_BAD_DATA_LEN = 0xC0,
    ESP_BAD_DATA_CHECKSUM = 0xC1,
    ESP_BAD_BLOCKSIZE = 0xC2,
    ESP_INVALID_COMMAND = 0xC3,
    ESP_FAILED_SPI_OP = 0xC4,
    ESP_FAILED_SPI_UNLOCK = 0xC5,
    ESP_NOT_IN_FLASH_MODE = 0xC6,
    ESP_INFLATE_ERROR = 0xC7,
    ESP_NOT_ENOUGH_DATA = 0xC8,
    ESP_TOO_MUCH_DATA = 0xC9,
    ESP_BAD_DATA_SEQ = 0xCA,
    ESP_BAD_DATA_READBACK = 0xCB,
    ESP_ERR_SD_PROBE = 0xCC,
    ESP_ERR_TIMEOUT = 0xCD,

    ESP_IMG_HDR_MARK_ERROR = 0xF0,
    ESP_IMG_HDR_RSAKEY_OFFSET_ERROR = 0xF1,
    ESP_IMG_HDR_IMGHASH_OFFSET_ERROR = 0xF2,
    ESP_IMG_HDR_AESKEY_OFFSET_ERROR = 0xF3,
    ESP_IMG_HDR_CMDSBLK_OFFSET_ERROR = 0xF4,
    ESP_IMG_HDR_RSA_KEY_ERROR = 0xF5,
    ESP_IMG_HDR_RSA_DECRYT_ERROR = 0xF6,
    ESP_IMG_HDR_RSA_SIG_ERROR = 0xF7,
    ESP_IMG_HASH_ERROR = 0xF8,
    ESP_IMG_UNKNOWN_ERROR = 0xFE,

    ESP_CMD_NOT_IMPLEMENTED = 0xFF,
} esp_command_error;

typedef enum
{
    COMM_TYPE_UART = 0, // UART
    COMM_TYPE_USB,      // USB
    COMM_TYPE_COUNT     // total of communication type
} comm_type;

typedef struct
{
	volatile uint8_t cpu_cfg_para;
	volatile uint8_t flash_clk_div;
	volatile uint8_t peri_pclk_div_reserved;
	volatile uint8_t aon_cfg_pclk_div;
	volatile uint8_t cmn_peri_pclk_div;
	volatile uint8_t ap_peri_pclk_div;
	volatile uint8_t hclk_div;
	volatile uint8_t pll_enable_flag;
} pll_clk_div_t;


//------------UART use only begin-----------------------------------
typedef struct
{
//    uint8_t *buf_a;
    //uint8_t buf_b[MAX_WRITE_BLOCK+64];
    uint8_t* reading_buf; /* Pointer to buf_a, or buf_b - which are we reading_buf? */
    uint32_t read; /* how many bytes have we read in the frame */
    slip_state_t state;
    esp_command_error error;
    esp_command_req_t* command; /* Pointer to buf_a or buf_b as latest command received */
} uart_buf_t;

typedef struct
{
    uint16_t read; /* how many bytes have we read in the frame */
    slip_state_t state;
} uart_buf_state_t;


void
ub_state_save();

void
ub_state_recovery();

void
ub_state_init();

void
pll_init(pll_clk_div_t *pll_clk_div);

esp_command_error
uart_receive_bytes(uint8_t* bytes, int32_t len);

//------------UART use only end-----------------------------------

int32_t
do_cmd(uint8_t* buf, int32_t* len, comm_type comm);

int32_t
post_do_cmd(uint8_t* buf, comm_type comm);

int8_t*
get_next_data_buf(int32_t* idx);

int8_t*
get_last_data_buf(int32_t* size);

void
read_data_buf_ready(int32_t idx);

void
set_resp_error(uint8_t* buf, uint8_t error, comm_type comm);

void
make_full_resp(uint8_t* buf, uint8_t command, uint8_t error);

int32_t flash_get_rdy_buf();

void flash_set_buf_free();
//just for static memory share.
//notice!! must not write when stub load is working
uint8_t*
get_sl_buffer();

int32_t sd_get_rdy_buf();
void sd_set_buf_free();

#endif // _STUB_LOAD_H_
