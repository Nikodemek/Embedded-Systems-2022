/******************************************************************************
 *
 * Copyright:
 *    (C) 2000 - 2007 Embedded Artists AB
 *
 * Description:
 *    Main program for LPC2148 Education Board test program
 *
 *****************************************************************************/

#include "pre_emptive_os/api/osapi.h"
#include "pre_emptive_os/api/general.h"
#include <printf_P.h>
#include <ea_init.h>
#include <lpc2xxx.h>
#include <consol.h>
#include "i2c.h"
#include "adc.h"
#include "lcd.h"
#include "pca9532.h"
#include "ea_97x60c.h"
#include "key.h"
#include "ball_game.h"

#define PROC1_STACK_SIZE 1024
#define KEY_CTRL_STACK_SIZE 1024
#define INIT_STACK_SIZE 400

static tU8 proc1Stack[PROC1_STACK_SIZE];
static tU8 initStack[INIT_STACK_SIZE];

static tU8 pid1;

extern void initKeyProc(void);
static void initProc(void *arg);
static void drawWelcome(void);

volatile tU32 msClock;

static volatile tBool pca9532Present = FALSE;

/******************************************************************************
** Function name:		udelay
**
** Descriptions:
**
** parameters:			delay length
** Returned value:		None
**
******************************************************************************/
void udelay(unsigned int delayInUs)
{
    /*
     * setup timer #1 for delay
     */
    T1TCR = 0x02; // stop and reset timer
    T1PR = 0x00;  // set prescaler to zero
    T1MR0 = (((long)delayInUs - 1) * (long)CORE_FREQ / 1000) / 1000;
    T1IR = 0xff;  // reset all interrrupt flags
    T1MCR = 0x04; // stop timer on match
    T1TCR = 0x01; // start timer

    // wait until delay time has elapsed
    while (T1TCR & 0x01);
}

/*****************************************************************************
 *
 * Description:
 *    The first function to execute
 *
 ****************************************************************************/
int main(void)
{
    tU8 error;
    tU8 pid;

    osInit();
    osCreateProcess(initProc, initStack, INIT_STACK_SIZE, &pid, 1, NULL, &error);
    osStartProcess(pid, &error);

    osStart();
    return 0;
}

/*****************************************************************************
 *
 * Description:
 *    A process entry function
 *
 * Params:
 *    [in] arg - This parameter is not used in this application.
 *
 ****************************************************************************/
static void
proc1(void *arg)
{
    osSleep(50);

    if (pca9532Present == FALSE) pca9532Present = pca9532Init();
    if (pca9532Present == FALSE) return;

    lcdInit();
    initAdc();
    drawWelcome();

    osSleep(169);
    initKeyProc();

    IODIR |= (1 << 13) | (1 << 14);
    IOCLR = (1 << 13) | (1 << 14);

    for (;;)
    {
        switch (checkKey())
        {
            case KEY_UP:
                startGame();
                break;
            case KEY_DOWN:
                stopGame();
                break;
            default:
                continue;
        }
    }
}

/*!
 *  @brief    A procedure for displaying a welcome
 *            message on the lcd screen.
 */
static void
drawWelcome(void)
{
    lcdColor(WHITE, BLACK);
    lcdClrscr();
    lcdGotoxy(20, 16);
    lcdPuts("Welcome to");
    lcdGotoxy(12, 30);
    lcdPuts("Ball The Game");
    lcdGotoxy(58, 64);
    lcdPuts(":)");
    lcdGotoxy(33, 98);
    lcdPuts("(C) 2022");
    lcdGotoxy(32, 112);
    lcdPuts("(X.D.0v)");
}

/*****************************************************************************
 *
 * Description:
 *    The entry function for the initialization process.
 *
 * Params:
 *    [in] arg - This parameter is not used in this application.
 *
 ****************************************************************************/
static void
initProc(void *arg)
{
    tU8 error;

    eaInit();  // initialize printf
    consolInit();
    i2cInit(); // initialize I2C

    osCreateProcess(proc1, proc1Stack, PROC1_STACK_SIZE, &pid1, 3, NULL, &error);
    osStartProcess(pid1, &error);

    osDeleteProcess();
}

/*****************************************************************************
 *
 * Description:
 *    The timer tick entry function that is called once every timer tick
 *    interrupt in the RTOS. Observe that any processing in this
 *    function must be kept as short as possible since this function
 *    execute in interrupt context.
 *
 * Params:
 *    [in] elapsedTime - The number of elapsed milliseconds since last call.
 *
 ****************************************************************************/
void appTick(tU32 elapsedTime)
{
    msClock += elapsedTime;
}
