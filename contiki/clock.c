#include <stdint.h>
#include <stdbool.h>
#include "contiki.h"
#include "chip.h"
#include "clock.h"
#include "systick.h"

static volatile uint32_t systick_pre_second = 1000;
static volatile uint32_t systick_clk_freq = 1000000;

static volatile uint32_t tm_count;
static volatile uint32_t sys_tick_ms;
static volatile uint32_t timeout_threshhold = 1;


uint32_t SysTick_Value(void)
{
	return sys_tick_ms;
}

extern int32_t UART_Rx_Timeout_Process();
static uint32_t timeout_cnt = 0;
void SysTick_Handler(void)
{
	sys_tick_ms++;

	static uint32_t acc_tick = 0;
	acc_tick++;
	if(acc_tick >= (1000 / CLOCK_SECOND)) {
		acc_tick = 0;
		tm_count++;
		if(etimer_pending()) {
			etimer_request_poll();
		}
	}
	SysTick_Reload(systick_clk_freq / systick_pre_second);
	if((timeout_cnt % timeout_threshhold) == 0) {
		timeout_cnt = 0;
		UART_Rx_Timeout_Process();
	}
	timeout_cnt++;
}


/*---------------------------------------------------------------------------*/
void
clock_init(void)
{
	tm_count = 0;
	register_ISR(IRQ_Timer_VECTOR, SysTick_Handler, NULL);
	SysTick_Config(systick_clk_freq / systick_pre_second);
}
/*---------------------------------------------------------------------------*/
clock_time_t
clock_time(void)
{
  return (clock_time_t)(tm_count);
}
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
unsigned long
clock_seconds(void)
{
  return tm_count / CLOCK_SECOND;
}
/*---------------------------------------------------------------------------*/
void
clock_wait(clock_time_t i)
{
	uint32_t start = clock_time();
	while(clock_time() < (start + i));
}
/*---------------------------------------------------------------------------*/
void
clock_delay_usec(uint16_t dt)
{
#define CNT_PER_USEC     (systick_clk_freq / 1000 / 1000)
	uint32_t end, start, ticks;
	uint32_t cnt = dt * CNT_PER_USEC;

	ticks = cnt / (systick_clk_freq / CLOCK_SECOND);
	if(ticks) {
		clock_wait(ticks);
		cnt -= ticks * (systick_clk_freq / CLOCK_SECOND);
	}
	start = SysTimer_GetLoadValue();
    do {
        end = SysTimer_GetLoadValue();
    	if(end >= start) { //not reload
    		if((end - start) >= cnt) {
    			break;
    		}
    	} else { //reload
    		if(((~start) + 1 + end) >= cnt) {
    			break;
    		}
    	}
    }while(1);
}
/*---------------------------------------------------------------------------*/
/**
 * \brief Obsolete delay function but we implement it here since some code
 * still uses it
 */
void
clock_delay(unsigned int i)
{
  clock_delay_usec(i);
}
/*---------------------------------------------------------------------------*/
/**
 * @}
 * @}
 * @}
 */
