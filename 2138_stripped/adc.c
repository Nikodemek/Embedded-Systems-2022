/******************************************************************************
 *
 * Copyright:
 *    (C) 2000 - 2005 Embedded Artists AB
 *
 * Description:
 *    Christmas tree
 *
 *****************************************************************************/

#include <general.h>
#include <printf_P.h>
#include <lpc2xxx.h>
#include <config.h>
#include "adc.h"

/******************************************************************************
 * Defines and typedefs
 *****************************************************************************/
#define CRYSTAL_FREQUENCY FOSC
#define PLL_FACTOR        PLL_MUL
#define VPBDIV_FACTOR     PBSD

/*****************************************************************************
 *
 * Description:
 *    Delay execution by a specified number of milliseconds by using
 *    timer #1. A polled implementation.
 *
 * Params:
 *    [in] delayInMs - the number of milliseconds to delay.
 *
 ****************************************************************************/
void
delayMs(tU16 delayInMs)
{
  /*
   * setup timer #1 for delay
   */
  T1TCR = 0x02;          // stop and reset timer
  T1PR  = 0x00;          // set prescaler to zero
  T1MR0 = delayInMs * (CORE_FREQ / PBSD / 1000);
  T1IR  = 0xff;          // reset all interrrupt flags
  T1MCR = 0x04;          // stop timer on match
  T1TCR = 0x01;          // start timer

  //wait until delay time has elapsed
  while (T1TCR & 0x01);
}


/*****************************************************************************
 *
 * Description:
 *    Start a conversion of one selected analogue input and return
 *    10-bit result.
 *
 * Params:
 *    [in] channel - analogue input channel to convert.
 *
 * Return:
 *    10-bit conversion result
 *
 ****************************************************************************/
tU16
getAnalogueInput1(tU8 channel)
{
	//start conversion now (for selected channel)
	AD1CR = (AD1CR & 0xFFFFFF00u) | (1 << channel) | (1 << 24);
	
	//wait til done
	while((AD1DR & 0x80000000u) == 0);

	//get result and adjust to 10-bit integer
  return (AD1DR>>6) & 0x3FFu;
}

/*****************************************************************************
 *
 * Description:
 *
 ****************************************************************************/
void
initAdc(void)
{
	volatile tU32 integerResult;
  
  //Initialize ADC: AIN1.6 = P0.21
  PINSEL1 &= ~((1<<10)|(1<<11));
  PINSEL1 |=  (1<<11);

  //Initialize ADC: AIN1.7 = P0.22
  PINSEL1 &= ~((1<<12)|(1<<13));
  PINSEL1 |=  (1<<12);

  AD1CR = (1 << 0)                             |  //SEL = 1, dummy channel #0
         ((CRYSTAL_FREQUENCY *
           PLL_FACTOR /
           VPBDIV_FACTOR) / 4500000 - 1) << 8 |  //set clock division factor, so ADC clock is 4.5MHz
         (0 << 16)                            |  //BURST = 0, conversions are SW controlled
         (0 << 17)                            |  //CLKS  = 0, 11 clocks = 10-bit result
         (1 << 21)                            |  //PDN   = 1, ADC is active
         (1 << 24)                            |  //START = 1, start a conversion now
         (0 << 27);	                             //EDGE  = 0, not relevant when start=1

  //short delay and dummy read
  delayMs(10);
  integerResult = AD1DR;
}

