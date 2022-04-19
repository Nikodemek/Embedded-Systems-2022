/******************************************************************************
 *
 * Copyright:
 *    (C) 2006 Embedded Artists AB
 *
 * File:
 *    key.h
 *
 * Description:
 *    Expose public functions and defines related to joystick key.
 *
 *****************************************************************************/
#ifndef _KEY_H_
#define _KEY_H_

/******************************************************************************
 * Includes
 *****************************************************************************/
#include "../pre_emptive_os/api/general.h"


/******************************************************************************
 * Defines and typedefs
 *****************************************************************************/
#define KEY_NOTHING 0x00
#define KEY_UP      0x01
#define KEY_RIGHT   0x02
#define KEY_DOWN    0x04
#define KEY_LEFT    0x08
#define KEY_CENTER  0x10


tU8 checkKey(void);
tU8 checkKey2(void);

void initKeyProc(void);

#endif
