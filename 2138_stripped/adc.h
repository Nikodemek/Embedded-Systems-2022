/******************************************************************************
 *
 * Copyright:
 *    (C) 2000 - 2005 Embedded Artists AB
 *
 * Description:
 *    Christmas tree
 *
 *****************************************************************************/

#ifndef _ADC_H_
#define _ADC_H_

#include <general.h>

/******************************************************************************
 * Defines and typedefs
 *****************************************************************************/

#define AIN6 6
#define AIN7 7

#define ACCEL_X AIN6
#define ACCEL_Y AIN7

void
delayMs(tU16 delayInMs);

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
tU16 getAnalogueInput1(tU8 channel);

/*****************************************************************************
 *
 * Description:
 *
 ****************************************************************************/
void initAdc(void);

#endif
