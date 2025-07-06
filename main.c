/* Standard includes. */
#include "contiki.h"
#include <stdio.h>
#include <string.h>
#include "IOMuxManager.h"
#include "chip.h"
#include "main.h"
#include "error.h"
#include "stub_load.h"
#include "spiflash.h"
#include "Driver_GPIO.h"

#include "lib_sdc.h"
#include "systick.h"

#include "ota_config.h"
#include "ota.h"

#include "efuse.h"
#include "ClockManager.h"
#include "clock_config.h"
#include "ftsdc021.h"
#include "secure.h"
#include "Driver_CRYPTO.h"


#define PIN_BOOT_OPT                 3        // GPIOA_03
#define PIN_BOOT_LED                 10       // GPIOA_10
#define PIN_BOOT_RX                  2        // GPIOA_02
#define PIN_BOOT_TX                  3        // GPIOA_03

// SDIO use the same pins as Flash
#define PIN_BOOT_SDIO_CLK            10       // GPIOB_10
#define PIN_BOOT_SDIO_CMD            15       // GPIOB_15
#define PIN_BOOT_SDIO_DAT0           11       // GPIOB_11
#define PIN_BOOT_SDIO_DAT1           12       // GPIOB_12
#define PIN_BOOT_SDIO_DAT2           13       // GPIOB_13
#define PIN_BOOT_SDIO_DAT3           14       // GPIOB_14

#define IOMUX_PIN_BOOT_OPT           CSK_IOMUX_FUNC_DEFAULT
#define IOMUX_PIN_BOOT_LED           CSK_IOMUX_FUNC_DEFAULT
#define IOMUX_PIN_BOOT_UART          CSK_IOMUX_FUNC_ALTER2
#define IOMUX_PIN_BOOT_SDIO          CSK_IOMUX_FUNC_ALTER1


volatile uint32_t rom_code_version = ROM_CODE_VERSION;


#define _DMA32    __attribute__((aligned(32)))
#define SD_BLK_SZ	(512)
#define SD_CARD_BUF 	(sizeof(SDCardInfo))

#define BOOT_HEADER_MASK			0xFC000000

static _DMA32 uint8_t  SourceBuf[SD_BLK_SZ] = {0};
static _DMA32 uint8_t  FTSDC021_SD_CARD_BUF[SD_CARD_BUF];

static void iomux_sel_sdc();



PROCESS(led_process, "led process");

typedef void (*func_entry) (void);
/*---------------------------------------------------------------------------*/

#define IMG_HDR_POS    320
#define HDR_SUM_POS    380
#define VEC_SUM_POS    382
int8_t header_verify(uint8_t *buf)
{
	int8_t ret = FAIL;
	uint16_t i, sum = 0;
	if(buf[IMG_HDR_POS] == 'H' && buf[IMG_HDR_POS + 1] == 'r') {
		for(i = IMG_HDR_POS; i < HDR_SUM_POS; i++) {
			sum += buf[i];
		}
		if(sum != (buf[HDR_SUM_POS] + (buf[HDR_SUM_POS + 1] << 8))) {
			goto END;
		}
		for(i = 0; i < IMG_HDR_POS; i++) {
			sum += buf[i];
		}
		if((sum + buf[HDR_SUM_POS] + buf[HDR_SUM_POS + 1])
				!= (buf[VEC_SUM_POS] + (buf[VEC_SUM_POS + 1] << 8))) {
			goto END;
		}
		ret = SUCCESS;
	}
END:
	return ret;
}


FLASH_DEV flash_dev = {
	.base_addr = CMN_FLASHC_BASE,
	.d_width = 4,
	.sclk_div = 0xff,  //divider is 1
	.run_mod = RUN_WITHOUT_INT,
	.timeout = 0x180000,
	.addr_bytes = 3,
	.addr_auto = 0,
	.interrupt_enable = NULL,
	.interrupt_disable = NULL,
	.dualflash_mode = 0,  // disable dual flash mode
};


bool flash_ota_header(ls_ota_header_t *boot_header, int sign_mode, int ota_header_offset)
{
    IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_TGT_SLV_SEL = 1;  // map address to flash

    if(boot_header->valid_flag != OTA_EXEC_VALID_FLAG)
    {
        // if secure boot enabled or the verify above failed, go to check boot-loader and executable
        IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_A = 1;  // enable region A encrypt

        boot_header = (ls_ota_header_t *)(CP_CIPHER_REGION_A + ota_header_offset * 0x10000);

        if(boot_header->valid_flag != OTA_EXEC_VALID_FLAG)
        {
            IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_A = 0;
        }
    }

    if(boot_header->valid_flag == OTA_EXEC_VALID_FLAG)
    {
        if(OTA_SIGN_NONE == sign_mode)
        {
            if(!ota_check_sum(boot_header))
                return false;
        }
        else if(sign_mode == OTA_SIGN_CRC32)
        {
            if(!ota_check_zone_crc(boot_header))
                return false;
        }
        else
        {
            extern void* CRYPTO0_Handler;
            secure_init();
            if(CSK_DRIVER_OK != CRYPTO_Verify_Flash_Signature(CRYPTO0_Handler, boot_header, sign_mode))
            {
                secure_shutdown();
                return false;
            }
            secure_shutdown();

            if((boot_header->entry & BOOT_HEADER_MASK) != AP_FLASH_BASE)
            {
                // Region A - 0x08000000
                if((boot_header->entry & BOOT_HEADER_MASK) == CP_CIPHER_REGION_A)
                {
                    IP_SYSCTRL->REG_CIPHER_CTRL0.bit.CIPHER_SLV1_BASE_ADDR = 0x3000 + ota_header_offset;
                }
                // Region B -0x10000000
                else if((boot_header->entry & BOOT_HEADER_MASK) == CP_CIPHER_REGION_B)
                {
                    IP_SYSCTRL->REG_CIPHER_CTRL2.bit.CIPHER_DEV_OFFSET_REGION_B = (ota_header_offset << 4);
                    IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_B = IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_A;
                }
                // Region C -0x18000000
                else if((boot_header->entry & BOOT_HEADER_MASK) == CP_CIPHER_REGION_C)
                {
                    IP_SYSCTRL->REG_CIPHER_CTRL2.bit.CIPHER_DEV_OFFSET_REGION_C = (ota_header_offset << 4);
                    IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_C = IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_A;
                }
                // Region D -0x1C000000
                else if((boot_header->entry & BOOT_HEADER_MASK) == CP_CIPHER_REGION_D)
                {
                    IP_SYSCTRL->REG_CIPHER_CTRL1.bit.CIPHER_DEV_OFFSET_REGION_D = (ota_header_offset << 4);
                    IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_D = IP_SYSCTRL->REG_CIPHER_CTRL3.bit.CIPHER_EN_REGION_A;
                }
            }
        }

        run_image((uint8_t *)boot_header->entry);

        return true;
    }
    return false;
}

bool flash_img_is_valid(uint8_t *buf)
{
	if(flash_init(&flash_dev, FLASH_SPI_IGNORE_QE | FLASH_SPI_RELEASE_DPD, 0) != 0)
	{
		return false;
	}
	memcpy(buf, (void *)AP_FLASH_BASE, VECTOR_BLK_SIZE);

	int sign_mode=efuse_boot_secure_enable();

    // if secure boot not enabled, go with the original method;
	if(OTA_SIGN_NONE == sign_mode)
	{
		if(header_verify(buf) == SUCCESS)
			return true;
	}

   	int ota_header_offset = efuse_boot_ota_header_offset();

   	ls_ota_header_t *boot_header;

    // check the OTA header offset defined in efuse
    if(ota_header_offset > 0) {
        boot_header = (ls_ota_header_t *)(AP_FLASH_BASE + ota_header_offset * 0x10000);
        flash_ota_header(boot_header, sign_mode, ota_header_offset);
    }

    // check the original boot header
    boot_header = (ls_ota_header_t *)(AP_FLASH_BASE);
    flash_ota_header(boot_header, sign_mode, ota_header_offset);

	return false;
}

PROCESS_THREAD(led_process, ev, data)
{
	static struct etimer timer;
	static uint32_t val = 1, state = 0;
	static void* GPIOA_Handler;

	PROCESS_BEGIN();

	etimer_set(&timer, CLOCK_SECOND / 16);
	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_A, PIN_BOOT_LED, IOMUX_PIN_BOOT_LED);
	GPIOA_Handler = GPIOA();
	GPIO_Initialize(GPIOA_Handler, NULL, NULL);

	GPIO_Control(GPIOA_Handler, CSK_GPIO_MODE_PULL_NONE | \
								CSK_GPIO_DEBOUNCE_DISABLE, 1UL << PIN_BOOT_LED);

	GPIO_SetDir(GPIOA_Handler, 1UL << PIN_BOOT_LED, CSK_GPIO_DIR_OUTPUT);
	GPIO_PinWrite(GPIOA_Handler, 1UL << PIN_BOOT_LED, val);

	while(1) {
		/* Wait for the periodic timer to expire and then restart the timer. */
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
		if(!state) {
			// boot_opt pin is released?
			if(GPIO_PinRead(GPIOA_Handler, (1UL << PIN_BOOT_OPT))) {
				IOMuxManager_PinConfigure (CSK_IOMUX_PAD_A, PIN_BOOT_RX, IOMUX_PIN_BOOT_UART);
				IOMuxManager_PinConfigure (CSK_IOMUX_PAD_A, PIN_BOOT_TX, IOMUX_PIN_BOOT_UART);
				// secure boot
				if(!efuse_boot_debug_protect_enable()) {
					uart_init();
				}
				// slow led flashing
				etimer_set(&timer, CLOCK_SECOND / 8);
				state = 1;    // update state
			}
		} else {
			val = val ? 0 : 1;
			GPIO_PinWrite(GPIOA_Handler, 1UL << PIN_BOOT_LED, val);
		}
		etimer_reset(&timer);
	}

	PROCESS_END();
}

void run_image(uint8_t *addr)
{
    disable_GINT();
    // disable interrupts and systick exception
    disable_IRQ(IRQ_Timer_VECTOR);
    clear_IRQ(IRQ_Timer_VECTOR);

    disable_IRQ(IRQ_SDIOH_VECTOR);
    clear_IRQ(IRQ_SDIOH_VECTOR);

    disable_IRQ(IRQ_UART0_VECTOR);
    clear_IRQ(IRQ_UART0_VECTOR);

    disable_IRQ(IRQ_DMAC_VECTOR);
    clear_IRQ(IRQ_DMAC_VECTOR);

    disable_IRQ(IRQ_GPIOA_VECTOR);
    clear_IRQ(IRQ_GPIOA_VECTOR);

    // disable the relavant peripheral clock
    __HAL_CRM_SDIO_H_CLK_DISABLE();

    func_entry pFunEntry = (func_entry)(addr);
    pFunEntry();

    // enable the relavant perippheral clock
    __HAL_CRM_SDIO_H_CLK_ENABLE();

    enable_IRQ(IRQ_GPIOA_VECTOR);
    enable_IRQ(IRQ_DMAC_VECTOR);
    enable_IRQ(IRQ_UART0_VECTOR);
    enable_IRQ(IRQ_SDIOH_VECTOR);
    enable_IRQ(IRQ_Timer_VECTOR);

    enable_GINT();
}

#define IMG_VMA_OFFSET   (IMG_HDR_POS + 8)
#define IMG_SIZE_OFFSET  (IMG_HDR_POS + 4)
void boot_flash()
{
	uint32_t vma, size;

	vma = *(uint32_t *)(AP_FLASH_BASE + IMG_VMA_OFFSET);
	size = *(uint32_t *)(AP_FLASH_BASE + IMG_SIZE_OFFSET);
	if(vma != AP_FLASH_BASE) {  //need code copy
		memcpy((uint8_t *)vma, (uint8_t *)AP_FLASH_BASE, size);
	}
	run_image((uint8_t *)vma); //never return
}



// Define constants for the MBR structure
#define MBR_SIZE               (512)
#define PARTITION_ENTRY_SIZE   (16)
#define PARTITION_TABLE_OFFSET (0x1BE)

// Structure to represent a partition entry in the MBR
typedef struct {
    uint8_t status;         // Boot indicator (0x00 = inactive, 0x80 = active/bootable)
    uint8_t chs_start[3];   // Cylinder-head-sector address of the start
    uint8_t type;           // Partition type (see partition type codes)
    uint8_t chs_end[3];     // Cylinder-head-sector address of the end
    uint32_t lba_start;     // Logical Block Address (LBA) of the start
    uint32_t num_sectors;   // Number of sectors in the partition
} PartitionEntry;

// Function to read the MBR and parse partition entries
uint32_t read_partition_table(uint8_t *mbr_data) {
    PartitionEntry partition_entries[4];  // MBR contains 4 partition entries
    int i;

    if(mbr_data[0x1FE] != 0x55 || mbr_data[0x1FF] != 0xAA)
    	return 0;

    // Copy partition table entries from MBR data
    for (i = 0; i < 4; i++) {
        uint8_t *entry_ptr = mbr_data + PARTITION_TABLE_OFFSET + i * PARTITION_ENTRY_SIZE;
        partition_entries[i].status = entry_ptr[0];
        // Read CHS addresses (not used in most modern systems)
        partition_entries[i].type = entry_ptr[4];
        // Read LBA start address
        partition_entries[i].lba_start = entry_ptr[8] | (entry_ptr[9] << 8) | (entry_ptr[10] << 16) | (entry_ptr[11] << 24);
        // Read number of sectors
        partition_entries[i].num_sectors = entry_ptr[12] | (entry_ptr[13] << 8) | (entry_ptr[14] << 16) | (entry_ptr[15] << 24);
    }

    // Check for a bootable partition (status = 0x80 & type = 0x06)
    for (i = 0; i < 4; i++) {
        if (partition_entries[i].status == 0x80 && partition_entries[i].type == 0x06) {
            return partition_entries[i].lba_start;  // Exit after finding the first bootable partition
        }
    }

    return 0;
}

void boot_sdcard()
{
#define SDCARD_IMAGE_OFFSET_SECTOR (64 * 2) // 64K
	uint32_t vma;
	int32_t size;
	uint32_t sector;

	// Read and parse the partition table
	gm_sdc_api_sdcard_sector_read(SD_0, 0, 1, SourceBuf);
	sector = read_partition_table(SourceBuf);

	// If no valid boot partition found, set to 64KB
	if(sector == 0)
		sector = SDCARD_IMAGE_OFFSET_SECTOR;

	if(gm_sdc_api_sdcard_sector_read(SD_0, sector, 1, SourceBuf))
		return;
	sector++;

	int sign_mode=efuse_boot_secure_enable();

	// if secure boot not enabled, go with the original method;
	if(OTA_SIGN_NONE == sign_mode)
	{
		if(header_verify(SourceBuf) == SUCCESS)
		{
			vma = *(uint32_t *)(&SourceBuf[IMG_VMA_OFFSET]);
			size = *(uint32_t *)(&SourceBuf[IMG_SIZE_OFFSET]);
			//need code copy
			uint8_t *pMem = (uint8_t *)vma;

			// the 1st block has been loaded ahead
			memcpy(pMem, SourceBuf, SD_BLK_SZ);
			pMem += SD_BLK_SZ;
			size -= SD_BLK_SZ;

			while(size > 0)
			{
				int retry = 5;
				while(gm_sdc_api_sdcard_sector_read(SD_0, sector, 1, SourceBuf))
				{
					if(retry == 0)
						return;
					retry--;
				}
				sector++;

				memcpy(pMem, SourceBuf, ((size > SD_BLK_SZ) ? SD_BLK_SZ : size));
				pMem += SD_BLK_SZ;
				size -= SD_BLK_SZ;
			}

			run_image((uint8_t *)vma); //never return
		}
	}

	// if secure boot enabled or the verify above failed, go to check boot-loader and executable
	ls_ota_header_t *boot_header;

	boot_header = (ls_ota_header_t*)SourceBuf;
	if(boot_header->valid_flag == OTA_EXEC_VALID_FLAG)
	{
		vma = boot_header->address;
		size = boot_header->size;

		if((vma + size) > 0x200A0000)
			return;

		//need code copy
		uint8_t *pMem = (uint8_t *)vma;
		// the 1st block has been loaded ahead
		memcpy(pMem, SourceBuf, SD_BLK_SZ);
		pMem += SD_BLK_SZ;
		size -= SD_BLK_SZ;

		while(size > 0)
		{
			int retry = 5;
			while(gm_sdc_api_sdcard_sector_read(SD_0, sector, 1, SourceBuf))
			{
				if(retry == 0)
					return;
				retry--;
			}
			sector++;

			memcpy(pMem, SourceBuf, ((size > SD_BLK_SZ) ? SD_BLK_SZ : size));
			pMem += SD_BLK_SZ;
			size -= SD_BLK_SZ;
		}

		boot_header = (ls_ota_header_t*)vma;

		if(OTA_SIGN_NONE == sign_mode)
		{
            if(!ota_check_sum(boot_header))
                return;
		}
		else if(sign_mode == OTA_SIGN_CRC32)
	    {
            if(!ota_check_zone_crc(boot_header))
                return;
	    }
		else
        {
            extern void* CRYPTO0_Handler;
            secure_init();
            if(CSK_DRIVER_OK != CRYPTO_Verify_Flash_Signature(CRYPTO0_Handler, boot_header, sign_mode))
            {
				secure_shutdown();
                return;
            }
            secure_shutdown();
        }

        run_image((uint8_t *)boot_header->entry); //never return
	}
	return;
}

void system_init(int stage)
{
	// set the ap peripheral clock: divider-1 
	HAL_CRM_SetAp_peri_pclkClkDiv(1, 1);
	clock_init();
	enable_GINT();
}

void contiki_init()
{
	process_init();
	process_start(&etimer_process, NULL);
	process_start(&led_process, NULL);
}

void upgrade()
{
	uint8_t r;
	flash_init(&flash_dev, 0, 0);
    contiki_init();

	while(1) {
		do {
		  r = process_run();
		} while(r > 0);
	}
}

int get_boot_opt()
{
	int ret;
	void* GPIOA_Handler = GPIOA();
	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_A, PIN_BOOT_OPT, IOMUX_PIN_BOOT_OPT);
	GPIO_Initialize(GPIOA_Handler, NULL, NULL);
	GPIO_SetDir(GPIOA_Handler, (1UL << PIN_BOOT_OPT), CSK_GPIO_DIR_INPUT);  //CSK_GPIO_DIR_INPUT
	ret = GPIO_PinRead(GPIOA_Handler, (1UL << PIN_BOOT_OPT));

	return ret;
}


#define WAKEUP_ACT_JUMP_RAM         (0xAA)
#define WAKEUP_ACT_JUMP_FLASH       (0xBB)
#define WAKEUP_ACT_JUMP_FLASH_INIT  (0xCC)

#define WAKEUP_CAUSE_GPIO           (0x3FF << 16)
#define WAKEUP_CAUSE_WF             (0x1 << 6)
#define WAKEUP_CAUSE_BT             (0x1 << 5)
#define WAKEUP_CAUSE_RTC            (0x1 << 4)
#define WAKEUP_CAUSE_KEY1           (0x1 << 3)
#define WAKEUP_CAUSE_KEY0           (0x1 << 2)
#define WAKEUP_CAUSE_IWDT           (0x1 << 1)
#define WAKEUP_CAUSE_TIMER          (0x1 << 0)
#define WAKEUP_CAUSE_ALL            (WAKEUP_CAUSE_GPIO | WAKEUP_CAUSE_WF | WAKEUP_CAUSE_BT | WAKEUP_CAUSE_RTC \
                                    | WAKEUP_CAUSE_KEY1 | WAKEUP_CAUSE_KEY0 | WAKEUP_CAUSE_IWDT | WAKEUP_CAUSE_TIMER)

__attribute__((naked))
void wakeup_process()
{
	uint32_t wakeup_action, wakeup_cause, wakeup_addr;
	wakeup_action = IP_AON_CTRL->REG_AON_DIG_RSVD0.all;
	if(wakeup_action == 0)	// no wakeup action
		 __asm__ volatile ("ret");

	wakeup_cause = IP_AON_CTRL->REG_WAKEUP_IRSR.all;

	if((wakeup_cause & WAKEUP_CAUSE_ALL) != 0) {
		wakeup_addr = IP_AON_CTRL->REG_AON_DIG_RSVD1.all;
		if((wakeup_action & 0x000000FF) == WAKEUP_ACT_JUMP_RAM) {
			func_entry pFunEntry = (func_entry)((void *)(wakeup_addr));
			pFunEntry();
			// should not reach this after
		}
		if((wakeup_action & 0x000000FF) == WAKEUP_ACT_JUMP_FLASH) {
			func_entry pFunEntry = (func_entry)((void *)(wakeup_addr));
			pFunEntry();
			// should not reach this after
		}
// 		we move the wakeup process to the the beginning of startup, can't use any nested function calls
// 		if the application needs to jump to flash with flash init, it should be done in the ram code
//		if((wakeup_action & 0x000000FF) == WAKEUP_ACT_JUMP_FLASH_INIT) {
//			// enable flash controller clock
//			__HAL_CRM_FLASH_CLK_ENABLE();
//			// do not enable QE in boot, only read it to choose single or quad mode
//			flash_init(&flash_dev, FLASH_SPI_IGNORE_QE | FLASH_SPI_RELEASE_DPD, 0);
//
//			func_entry pFunEntry = (func_entry)((void *)(wakeup_addr));
//			pFunEntry();
//			// should not reach this after
//		}
	}
    // go back to the startup routine
    __asm__ volatile ("ret");
}

int main( void )
{
	DisableDCache();

	bool valid;
	uint8_t *buf = (uint8_t *)SLIP_RX_BUF;
	int32_t boot_ops;

    system_init(0);
	boot_ops = get_boot_opt();
	if(!boot_ops) {
        // in secure boot, ignore boot opiton pin, go to check the image first
        if(!efuse_boot_debug_protect_enable()) {
    		// enter upgrade mode
    		upgrade();
    		return 0;
        }
	}


#ifdef ROM_DBG
	logInit(0, 115200);
#endif
	BOOT_LOG("arcs boot\n");

	// if ap disable(cp only), let ap run into wfi
	if(efuse_boot_ap_disable()) {
		BOOT_LOG("ap disabled\n");
		disable_IRQ(IRQ_Timer_VECTOR);
		clear_IRQ(IRQ_Timer_VECTOR);

		disable_GINT();

		while(1) {
			__WFI();
		}
	}

	// bit0: 0 - Flash; 1 - SD
	// bit1: always try to boot from Flash
	// bit2: always try to boot from SD card
	// bit3: enable SDIO 4bit mode
	boot_ops = efuse_boot_option();

	if((boot_ops & 0x01) == 0x01) {
		if(!sd_card_probe(boot_ops & 0x08, NULL)) {
			// boot from sd card
			boot_sdcard();
		}

		// if SD card boot failed, try flash
		if((boot_ops & 0x02) == 0x02) {
			if(flash_img_is_valid(buf)) {
				// boot from flash
				boot_flash();
			}
		}

	} else {
    	if(flash_img_is_valid(buf)) {
			// boot from flash
			boot_flash();
		}

		// if flash boot failed, try sd card
		if((boot_ops & 0x04) == 0x04) {
			if(!sd_card_probe(boot_ops & 0x08, NULL)) {
				// boot from sd card
				boot_sdcard();
			}
		}
	}

	// if not flash or sd card boot, run to upgrade
	upgrade();

	return 0;
}


static void iomux_sel_sdc()
{
 	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_CLK, IOMUX_PIN_BOOT_SDIO);  //sd_clk
 	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_CMD, IOMUX_PIN_BOOT_SDIO);  //sd_cmd
 	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_DAT0, IOMUX_PIN_BOOT_SDIO);  //sd_dat0
 	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_DAT1, IOMUX_PIN_BOOT_SDIO);  //sd_dat1
	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_DAT2, IOMUX_PIN_BOOT_SDIO);  //sd_dat2
	IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_DAT3, IOMUX_PIN_BOOT_SDIO);  //sd_dat3

 	HAL_CRM_SetSdio_hClkSrc(0); ////select xtal as the clock //0x1; //select syspll as the clock
 	__HAL_CRM_SDIO_H_CLK_ENABLE(); //enable clock

 	 IP_SDIOH->REG_CCR_TCR_SRR.bit.SD_CLK_EN = 0x1;
 	 IP_SDIOH->REG_HC1_PCR_BGCR.bit.SD_BUS_POW = 0x1; // Set SD power enable
 	 IP_SDIOH->REG_HC1_PCR_BGCR.bit.SD_BUS_VOL = 0x3; // Set SD power enable
 	 IP_SDIOH->REG_VR1.bit.LO_SD_RSTN = 0x1; // Release Reset signal
}

static void iomux_sel_sdc_dummy()
{
 	HAL_CRM_SetSdio_hClkSrc(0); ////select xtal as the clock //0x1; //select syspll as the clock
 	__HAL_CRM_SDIO_H_CLK_ENABLE(); //enable clock

 	 IP_SDIOH->REG_CCR_TCR_SRR.bit.SD_CLK_EN = 0x1;
 	 IP_SDIOH->REG_HC1_PCR_BGCR.bit.SD_BUS_POW = 0x1; // Set SD power enable
 	 IP_SDIOH->REG_HC1_PCR_BGCR.bit.SD_BUS_VOL = 0x3; // Set SD power enable
 	 IP_SDIOH->REG_VR1.bit.LO_SD_RSTN = 0x1; // Release Reset signal
}


int sd_card_probe(int enable_4bit, void *config_io)
{
    int ret;

    if(NULL == config_io) {
        gm_api_sdc_platform_init((SDC_OPTION_ENABLE | SDC_OPTION_CD_INVERT), 0,
                iomux_sel_sdc, (uint32_t) FTSDC021_SD_CARD_BUF);
    } else {
        gm_api_sdc_platform_init((SDC_OPTION_ENABLE | SDC_OPTION_CD_INVERT), 0,
                config_io, (uint32_t) FTSDC021_SD_CARD_BUF);
    }

// 	gm_sdc_api_action(SD_0, GM_SDC_ACTION_SET_ADMA_BUFER, FTSDC021_SD_ADMA_BUF, NULL);

 	ret = (int)gm_sdc_api_action(SD_0, GM_SDC_ACTION_CARD_DETECTION, NULL, NULL);
 	if (ret != 0) {
 		BOOT_LOG("%s (return %u): NO sdcard was found!! \r\n", __func__, ret);
 		return ret;
 	}

 	if (enable_4bit) {
//		IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_DAT2, IOMUX_PIN_BOOT_SDIO);  //sd_dat2
//		IOMuxManager_PinConfigure (CSK_IOMUX_PAD_B, PIN_BOOT_SDIO_DAT3, IOMUX_PIN_BOOT_SDIO);  //sd_dat3

		u32 bus_width = 4; // 1 for 1-bit SDIO, 4 for 4-bit SDIO
		ret = gm_sdc_api_action(SD_0, GM_SDC_ACTION_SET_BUS_WIDTH, &bus_width, NULL);
		BOOT_LOG("%s (return %u): set to 4 bit width!! \r\n", __func__, ret);
    }

 	return ret;
}
