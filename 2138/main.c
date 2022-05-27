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

#define PROC1_STACK_SIZE 1024
#define PROC2_STACK_SIZE 1024
#define ACC_X_CTRL_STACK_SIZE 1024
#define ACC_Y_CTRL_STACK_SIZE 1024
#define ACC_Y_CTRL_STACK_SIZE 1024
#define KEY_CTRL_STACK_SIZE 1024
#define INIT_STACK_SIZE  400

static tU8 proc1Stack[PROC1_STACK_SIZE];
static tU8 proc2Stack[PROC2_STACK_SIZE];
static tU8 accXCtrlStack[ACC_X_CTRL_STACK_SIZE];
static tU8 accYCtrlStack[ACC_Y_CTRL_STACK_SIZE];
static tU8 keyCtrlStack[ACC_Y_CTRL_STACK_SIZE];
static tU8 initStack[INIT_STACK_SIZE];
static tU8 pid1;
static tU8 pid2;
static tU8 pidAccXCtrl;
static tU8 pidAccYCtrl;
static tU8 pidKeyCtrl;

static tU8 lcdHeight = 130;
static tU8 lcdWidth = 130;
static tU16 refXValue, refYValue;

static struct Ball ball;

struct Ball {
    int xPos;
    int yPos;
    int radius;
    int speed;
};

enum Direction {
    Up,
    Down,
    Left,
    Right
};

extern void initKeyProc(void);

static void initProc(void* arg);
static void drawWelcome(void);
static void overdrawBall(int color);
static void moveBall(tU8 joyDirection);
static void playWithTheBall(void);
static void accControlProc(void);
static void initBall(void);

static tS8 direction = KEY_NOTHING;

volatile tU32 msClock;
volatile tU8 killProc1 = FALSE;
volatile tU8 rgbSpeed = 10;

/******************************************************************************
** Function name:		udelay
**
** Descriptions:		
**
** parameters:			delay length
** Returned value:		None
** 
******************************************************************************/
void udelay( unsigned int delayInUs )
{
  /*
   * setup timer #1 for delay
   */
  T1TCR = 0x02;          //stop and reset timer
  T1PR  = 0x00;          //set prescaler to zero
  T1MR0 = (((long)delayInUs-1) * (long)CORE_FREQ/1000) / 1000;
  T1IR  = 0xff;          //reset all interrrupt flags
  T1MCR = 0x04;          //stop timer on match
  T1TCR = 0x01;          //start timer
  
  //wait until delay time has elapsed
  while (T1TCR & 0x01)
    ;
}

/*****************************************************************************
 *
 * Description:
 *    The first function to execute 
 *
 ****************************************************************************/
int
main(void)
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
proc1(void* arg)
{
  tU8 pca9532Present = FALSE;
    
    osSleep(50);

    //check if connection with PCA9532
    pca9532Present = pca9532Init();
    tU8 pin = 0;
  
	for(;;)
	{
	    if (TRUE == pca9532Present)
	    {
            setPca9532Pin(pin, 0);
            udelay(400000);
            setPca9532Pin(pin, 1);
            pin = (pin + 1) % 16;
        }
    }
}


typedef enum _ButtonError_t
{
  BUTT_OK = 0, BUTT_TO_ERROR, BUTT_SHORT_ERROR
} ButtonError_t;

#define _BUT_MAX_SCAN_PER   200
#define _BUT_MIN_SCAN_PER   10

#define _PDIR_OFFSET        (((unsigned int)&IODIR0 - (unsigned int)&IOPIN0)/sizeof(unsigned int))
#define _PSET_OFFSET        (((unsigned int)&IOSET0 - (unsigned int)&IOPIN0)/sizeof(unsigned int))
#define _PCLR_OFFSET        (((unsigned int)&IOCLR0 - (unsigned int)&IOPIN0)/sizeof(unsigned int))
#define _PIN_OFFSET         (((unsigned int)&IOPIN0 - (unsigned int)&IOPIN0)/sizeof(unsigned int))

#define _PORT1_BUT1_BIT     24
#define _PORT1_BUT2_BIT     25

//#define OS_DI() do {VICIntSelect &= ~0x10;} while(0)
//#define OS_EI() do {VICIntSelect |= 0x10; VICIntEnable = VICIntEnable | 0x10;} while(0)
#define OS_DI() m_os_dis_int()
#define OS_EI() m_os_ena_int()

typedef struct _ButtonCtrl_t
{
  volatile unsigned long * pButBaseReg;
           unsigned int ButBit;
} ButtonCtrl_t, *pButtonCtrl_t;

typedef struct _ButtonsPairCtrl_t
{
  ButtonCtrl_t Ba;
  ButtonCtrl_t Bb;
} ButtonsPairCtrl_t, *pButtonsPairCtrl_t;

const ButtonsPairCtrl_t ButtonsCtrl[1] =
{
  {
    .Ba=
    {
      .pButBaseReg = &IOPIN1,   // CAP_BUTT_1 = P1.24
      .ButBit = _PORT1_BUT1_BIT,
    },
    .Bb=
    {
      .pButBaseReg = &IOPIN1,   // CAP_BUTT_2 = P1.25
      .ButBit = _PORT1_BUT2_BIT,
    }
  }
};

tU8
readTouch(tU8 id, tU32 *pCount)
{
  volatile unsigned int To;
  unsigned int Count, Hold, MasterMask, SlaveMask;
  volatile unsigned int *pMasterReg, *pSlaveReg;
  tSR localSR;

  To = _BUT_MAX_SCAN_PER;

  if(id & 1)
  {
    pMasterReg = ButtonsCtrl[id>>1].Ba.pButBaseReg;
    MasterMask = 1UL << ButtonsCtrl[id>>1].Ba.ButBit;
    pSlaveReg  = ButtonsCtrl[id>>1].Bb.pButBaseReg;
    SlaveMask  = 1UL << ButtonsCtrl[id>>1].Bb.ButBit;
  }
  else
  {
    pMasterReg = ButtonsCtrl[id>>1].Bb.pButBaseReg;
    MasterMask = 1UL << ButtonsCtrl[id>>1].Bb.ButBit;
    pSlaveReg  = ButtonsCtrl[id>>1].Ba.pButBaseReg;
    SlaveMask  = 1UL << ButtonsCtrl[id>>1].Ba.ButBit;
  }
#if 0
  // Button scan algorithm
  // 1. Starting state Ba-o1 (Port Ba Output H), Bb-o1
  *(pSlaveReg + _PSET_OFFSET)  = SlaveMask;
  *(pSlaveReg + _PSET_OFFSET)  = MasterMask;
  *(pSlaveReg + _PDIR_OFFSET) |= SlaveMask;
  *(pSlaveReg + _PDIR_OFFSET) |= MasterMask;

  // 2. Set Ba i (input)
  *(pSlaveReg + _PDIR_OFFSET) &= ~SlaveMask;
//printf("\nS=%x, ", *(pSlaveReg + _PIN_OFFSET) & SlaveMask);

  // 3. Set Bb o0
  OS_DI();
  T1TCR = 1; // enable Timer
  *(pMasterReg + _PCLR_OFFSET) = MasterMask;

  // 4. wait and counting until Ba state get 0
  Count = T1TC;
  while(*(pSlaveReg + _PIN_OFFSET) & SlaveMask)
  {
    if(!To)
    {
      break;
    }
    --To;
  }
  Hold = T1TC - Count;
  OS_EI();
//printf("S=%x, hold = %d  (%d)  ", *(pSlaveReg + _PIN_OFFSET) & SlaveMask, Hold, To);
  To = _BUT_MAX_SCAN_PER;
#endif

  // 5. Ba o0
  *(pSlaveReg + _PCLR_OFFSET)  = SlaveMask;
  *(pSlaveReg + _PDIR_OFFSET) |= SlaveMask;

  // 6. Set Ba i
  *(pSlaveReg + _PDIR_OFFSET) &= ~SlaveMask;
//printf(", S=%d, ", IOPIN1 & (1UL<<25));

  // 7. Set Bb o1
  OS_DI();
  T1TCR = 1; // enable Timer
  Count = T1TC;
  *(pMasterReg + _PSET_OFFSET) = MasterMask;

  // 8. wait and counting until Ba state get 1
  while(!(*(pSlaveReg + _PIN_OFFSET) & SlaveMask))
  {
    if(!To)
    {
      break;
    }
    --To;
  }
//  Hold += T1TC - Count;
  Hold = T1TC - Count;
  OS_EI();
//printf(", S=%d; %d (%d) ", IOPIN1 & (1UL<<25), Hold, To);
//printf("; %d (%d) ", Hold, To);
  T1TCR = 0;  // disable Timer

  // 9. Set Ba o1
  *(pSlaveReg + _PSET_OFFSET)  = SlaveMask;
  *(pSlaveReg + _PDIR_OFFSET) |= SlaveMask;

//  if (id == 0)
// printf("\n");
//else
// printf("\n                          ");
  if(!To)
  {
//printf("BUTT_TO_ERROR");
    return(BUTT_TO_ERROR);
  }

  if(To == _BUT_MAX_SCAN_PER)
  {
//printf("BUTT_SHORT_ERROR");
    return(BUTT_SHORT_ERROR);
  }

  *pCount = Hold;
//printf("BUTT_OK (%d)", Hold);
  return(BUTT_OK);
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
proc2(void* arg)
{
  tU8 pca9532Present = FALSE;
  
  osSleep(50);
  
  //check if connection with PCA9532
  pca9532Present = pca9532Init();
  
  if (TRUE == pca9532Present)
  {
    lcdInit();
    initAdc();
    drawWelcome();

    osSleep(169);
    initKeyProc();

    initBall();

    for(;;) {
        tU8 keyPressed;
        keyPressed = checkKey();

        if(keyPressed != KEY_NOTHING) {
            if (keyPressed == KEY_UP)
            {
            	accControlProc();
            }
        }
    }
  }
}

static void
drawWelcome(void) {
    lcdColor(0xff,0x00);
    lcdClrscr();
    lcdGotoxy(20,16);
    lcdPuts("Welcome to");
    lcdGotoxy(12,30);
    lcdPuts("Ball The Game");
    lcdGotoxy(58,64);
    lcdPuts(":)");
    lcdGotoxy(33,98);
    lcdPuts("(C) 2022");
    lcdGotoxy(32,112);
    lcdPuts("(0.0.XDv)");
}

static void
overdrawBall(int color)
{
  lcdRect(ball.xPos, ball.yPos, ball.radius, ball.radius, color);
}

static void
moveBall(tU8 dir)
{
  overdrawBall(0x00);
  switch (dir)
  {
      case KEY_UP:
          ball.yPos = ball.yPos - ball.speed;
          break;
      case KEY_DOWN:
          ball.yPos = ball.yPos + ball.speed;
          break;
      case KEY_LEFT:
          ball.xPos = ball.xPos - ball.speed;
          break;
      case KEY_RIGHT:
          ball.xPos = ball.xPos + ball.speed;
          break;
      default:
          break;
  }
  ball.yPos = abs(ball.yPos % lcdHeight);
  ball.xPos = abs(ball.xPos % lcdWidth);

  overdrawBall(0xFF);
}

static void
accControlXProc()
{
  tU32 delay;
  int input, value, strength;
  tU8 dir;

  for(;;)
  {
	input = getAnalogueInput1(ACCEL_X);
	value = refXValue - input;

	if (value > 0)
	{
	  dir = KEY_UP;
	}
	else
	{
	  dir = KEY_DOWN;
	}

	if (abs(value) <= 30)
	{
		strength = 0;
	}
	else
	{
		strength = abs(value / 10);
	}

	if (strength == 0)
	{
	  dir = KEY_NOTHING;
	  delay = 10;
	}
	else
	{
	  if (strength > 5)
	  {
		strength = 5;
	  }
	  delay = 4 * strength * strength - 45 * strength + 141;
	}
	moveBall(dir);
	osSleep(delay);
  }
}

static void
accControlYProc()
{
  tU32 delay;
  int input, value, strength;
  tU8 dir;

  for(;;)
  {
	input = getAnalogueInput1(ACCEL_Y);
	value = refYValue - input;

	if (value > 0)
	{
	  dir = KEY_RIGHT;
	}
	else
	{
	  dir = KEY_LEFT;
	}

	if (abs(value) <= 30)
	{
		strength = 0;
	}
	else
	{
		strength = abs(value / 15);
	}

	if (strength == 0)
	{
	  dir = KEY_NOTHING;
	  delay = 10;
	}
	else
	{
	  if (strength > 5)
	  {
		strength = 5;
	  }
	  delay = 4 * strength * strength - 45 * strength + 141;
	}
	moveBall(dir);
	osSleep(delay);
  }
}

static void
accControlProc()
{
  IODIR |= (1 << 13) | (1 << 14);
  IOCLR = (1 << 13) | (1 << 14);

  refXValue = getAnalogueInput1(ACCEL_X);
  refYValue = getAnalogueInput1(ACCEL_Y);

  tU8 error;
  osCreateProcess(accControlXProc, accXCtrlStack, ACC_X_CTRL_STACK_SIZE, &pidAccXCtrl, 2, NULL, &error);
  osStartProcess(pidAccXCtrl, &error);
  osCreateProcess(accControlYProc, accYCtrlStack, ACC_Y_CTRL_STACK_SIZE, &pidAccYCtrl, 2, NULL, &error);
  osStartProcess(pidAccYCtrl, &error);
  osCreateProcess(playWithTheBall, keyCtrlStack, KEY_CTRL_STACK_SIZE, &pidKeyCtrl, 2, NULL, &error);
  osStartProcess(pidKeyCtrl, &error);
  for(;;);
}

static void
playWithTheBall(void) {
    tU8 keypress;
    for(;;) {
        keypress = checkKey();
        if (keypress != KEY_NOTHING)
        {
            if (keypress == KEY_CENTER) {
                direction = KEY_NOTHING;
                break;
            }
          
            if ((keypress == KEY_UP)    ||
                (keypress == KEY_RIGHT) ||
                (keypress == KEY_DOWN)  ||
                (keypress == KEY_LEFT))
            direction = keypress;
        }

        if (direction != KEY_NOTHING)
        {
            moveBall(direction);
            direction = KEY_NOTHING;
        }
        osSleep(20);
    }
}

static void
initBall(void)
{
  ball.xPos = 65;
  ball.yPos = 65;
  ball.speed = 5;
  ball.radius = 4;

  lcdClrscr();
  lcdColor(0x00, 0xff);
  lcdRect(ball.xPos, ball.yPos, ball.radius, ball.radius, 0xFF);
  lcdClrscr();
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
initProc(void* arg)
{
  tU8 error;

  eaInit();   //initialize printf
  i2cInit();  //initialize I2C
//  osCreateProcess(proc1, proc1Stack, PROC1_STACK_SIZE, &pid1, 3, NULL, &error);
//  osStartProcess(pid1, &error);
  osCreateProcess(proc2, proc2Stack, PROC2_STACK_SIZE, &pid2, 3, NULL, &error);
  osStartProcess(pid2, &error);

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
void
appTick(tU32 elapsedTime)
{
  msClock += elapsedTime;
}
