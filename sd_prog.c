/* Standard includes. */
#include <stdio.h> /* For printf() */
#include <string.h>

#include "contiki.h"
#include "IOMuxManager.h"
#include "chip.h"
#include "log_print.h"
#include "stub_load.h"
#include "main.h"
#include "list.h"
#include "queue.h"
#include "lib_sdc.h"

PROCESS_NAME(uart_boot_process);
/*---------------------------------------------------------------------------*/
PROCESS(sd_prog_process, "sd program process");


/*---------------------------------------------------------------------------*/


sd_prog_t sd_prog = {0, 0, 0, 0, (uint8_t *)AP_SRAM_BASE, 0, 0, {{0, 0}}};
sd_ops_data sd_ops;

void sd_prog_init()
{
    process_start(&sd_prog_process, NULL);
}


PROCESS_THREAD(sd_prog_process, ev, data)
{
	static int idx;
	static char event;

	PROCESS_BEGIN();
	while(1) {
		PROCESS_WAIT_EVENT();

		while(1) {
			idx = sd_get_rdy_buf();
			if(idx < 0)
				break;
			sd_ops.data = sd_prog.data_ctrl[idx].buf_idx * LOAD_BLK_SIZE + sd_prog.load_base;
			sd_ops.sd_addr = sd_prog.sd_offset + sd_prog.cnt;
			sd_ops.size = sd_prog.data_ctrl[idx].size;
			sd_ops.ctrl_idx = idx;

			//program blocks, only support 512 bytes block size
			if(ERR_SD_NO_ERROR == gm_sdc_api_sdcard_sector_write(SD_0, sd_ops.sd_addr, ((sd_ops.size + 512 - 1) >> 9), sd_ops.data)) {
				event = PROCESS_EVENT_BUF_FREE;
				// update information of sd_prog
				sd_prog.cnt += ((sd_ops.size + 512 - 1) >> 9);
			} else {
				BOOT_LOG("sd write failed\n");
				event = PROCESS_EVENT_PROG_ERR;
			}

			sd_set_buf_free();
			// send event
			process_post(&uart_boot_process,  event, NULL);
			BOOT_LOG("out-%d->\n", sd_ops.ctrl_idx);
		}
	}

	PROCESS_END();
}
