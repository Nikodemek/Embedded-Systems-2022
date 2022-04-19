/*************************************************************************************
 *
 * @Description:
 * Program przykładowy - odpowiednik "Hello World" dla systemów wbudowanych
 * Rekomendujemy wkopiowywanie do niniejszego projektu nowych funkcjonalności
 *
 *
 * UWAGA! Po zmianie rozszerzenia na cpp program automatycznie będzie używać
 * kompilatora g++. Oczywiście konieczne jest wprowadzenie odpowiednich zmian w
 * pliku "makefile"
 *
 *
 * Program przykładowy wykorzystuje Timer #0 i Timer #1 do "mrugania" diodami
 * Dioda P1.16 jest zapalona i gaszona, a czas pomiędzy tymi zdarzeniami
 * odmierzany jest przez Timer #0.
 * Program aktywnie oczekuje na upłynięcie odmierzanego czasu (1s)
 *
 * Druga z diod P1.17 jest gaszona i zapalana w takt przerwań generowanych
 * przez timer #1, z okresem 500 ms i wypełnieniem 20%.
 * Procedura obsługi przerwań zdefiniowana jest w innym pliku (irq/irq_handler.c)
 * Sama procedura MUSI być oznaczona dla kompilatora jako procedura obsługi 
 * przerwania odpowiedniego typu. W przykładzie jest to przerwanie wektoryzowane.
 * Odpowiednia deklaracja znajduje się w pliku (irq/irq_handler.h)
 * 
 * Prócz "mrugania" diodami program wypisuje na konsoli powitalny tekst.
 * 
 * @Authors: Michał Morawski,
 *           Daniel Arendt, 
 *           Przemysław Ignaciuk,
 *           Marcin Kwapisz
 *
 * @Change log:
 *           2016.12.01: Wersja oryginalna.
 *
 ******************************************************************************/

#include "general.h"
#include <lpc2xxx.h>
#include <printf_P.h>
#include <printf_init.h>
#include <consol.h>
#include <config.h>
#include "irq/irq_handler.h"
#include "timer.h"
#include "VIC.h"

#include "key.h"
#include "i2c.h"
#include "pca9532.h"



#include "Common_Def.h"
#include <stdio.h>

#define PROC_STACK_SIZE 1024
#define INIT_STACK_SIZE  400

static tU8 procStack[PROC_STACK_SIZE];
static tU8 initStack[INIT_STACK_SIZE];
static tU8 pid;

static void proc(void* arg);
static void initProc(void* arg);

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

/************************************************************************
 * @Description: opóźnienie wyrażone w liczbie sekund
 * @Parameter:
 *    [in] seconds: liczba sekund opĂłĹşnienia
 * @Returns: Nic
 * @Side effects:
 *    przeprogramowany Timer #0
 *************************************************************************/
static void sdelay (tU32 seconds)
{
	T0TCR = TIMER_RESET;                    //Zatrzymaj i zresetuj
    T0PR  = PERIPHERAL_CLOCK-1;             //jednostka w preskalerze
    T0MR0 = seconds;
    T0IR  = TIMER_ALL_INT;                  //Resetowanie flag przerwaĹ„
    T0MCR = MR0_S;                          //Licz do wartości w MR0 i zatrzymaj się
    T0TCR = TIMER_RUN;                      //Uruchom timer

    // sprawdź czy timer działa
    // nie ma wpisanego ogranicznika liczby pętli, ze względu na charakter procedury
    while (T0TCR & TIMER_RUN)
    {
    }
}

/************************************************************************
 * @Description: uruchomienie obsługi przerwań
 * @Parameter:
 *    [in] period    : okres generatora przerwań
 *    [in] duty_cycle: wypełnienie w %
 * @Returns: Nic
 * @Side effects:
 *    przeprogramowany timer #1
 *************************************************************************/
static void init_irq (tU32 period, tU8 duty_cycle)
{
	//Zainicjuj VIC dla przerwań od Timera #1
    VICIntSelect &= ~TIMER_1_IRQ;           //Przerwanie od Timera #1 przypisane do IRQ (nie do FIQ)
    VICVectAddr5  = (tU32)IRQ_Test;         //adres procedury przerwania
    VICVectCntl5  = VIC_ENABLE_SLOT | TIMER_1_IRQ_NO;
    VICIntEnable  = TIMER_1_IRQ;            // Przypisanie i odblokowanie slotu w VIC od Timera #1
  
    T1TCR = TIMER_RESET;                    //Zatrzymaj i zresetuj
    T1PR  = 0;                              //Preskaler nieużywany
    T1MR0 = ((tU64)period)*((tU64)PERIPHERAL_CLOCK)/1000;
    T1MR1 = (tU64)T1MR0 * duty_cycle / 100; //Wypełnienie
    T1IR  = TIMER_ALL_INT;                  //Resetowanie flag przerwań
    T1MCR = MR0_I | MR1_I | MR0_R;          //Generuj okresowe przerwania dla MR0 i dodatkowo dla MR1
    T1TCR = TIMER_RUN;                      //Uruchom timer
}

void main(void)
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
proc(void* arg)
{
  tU8 pca9532Present = FALSE;
  
  osSleep(50);
  
  //check if connection with PCA9532
  pca9532Present = pca9532Init();
  
  if (TRUE == pca9532Present)
  {
    lcdInit();
    lcdColor(0xff,0x00);
    lcdClrscr();
    lcdGotoxy(16,66);
    lcdPuts("Welcome to");
    lcdGotoxy(20,80);
    lcdPuts("Ball The Game");
    lcdGotoxy(0,96);
    lcdPuts(":)");
    lcdGotoxy(8,112);
    lcdPuts("(C)2022 (0.1.0v))");
    
    osSleep(100);

    lcdClrscr();

    initKeyProc();

    for(;;) {
        tU8 keyPressed;
        keyPressed = checkKey();

        if(keyPressed != KEY_NOTHING) {
            if (keyPressed == KEY_CENTER)
            {
                playWithTheBall();
            }
        }

        lcdClrscr();
        move(&ball, Up);
        lcdRect(ball.xPos, ball.yPos, ball.radius, ball.radius, 0xFF);
    }

  }
}

static void
move(struct Ball *ball, enum Direction dir) {
    switch (dir)
    {
        case Up:
            ball->yPos = ball->yPos + ball->speed;
            break;
        case Down:
            ball->yPos = ball->yPos - ball->speed;
            break;
        case Left:
            ball->xPos = ball->xPos - ball->speed;
            break;
        case Right:
            ball->xPos = ball->xPos + ball->speed;
            break;
        default:
            break;
    }
}

static Direction
checkDirection(tU8 joyDirection) {
    switch (joyDirection)
    {
        case KEY_UP:
            return Up;
        case KEY_DOWN:
            return Down;
        case KEY_LEFT:
            return Left;
        case KEY_RIGHT:
            return Right;
        default:
            break;
    }
}

static tS8 direction = 0;

static void
playWithTheBall(void) {
    struct Ball ball;
    ball.xPos = 10;
    ball.yPos = 10;
    ball.speed = 5;
    ball.radius = 4;
    lcdRect(ball.xPos, ball.yPos, ball.radius, ball.radius, 0xFF);

    tU8 keypress;

    for(;;) {
        keypress = checkKey();
        if (keypress != KEY_NOTHING)
        {
            if (keypress == KEY_CENTER)
                break;
          
            if ((keypress == KEY_UP)    ||
                (keypress == KEY_RIGHT) ||
                (keypress == KEY_DOWN)  ||
                (keypress == KEY_LEFT))
            direction = keypress;
        }

        if (direction != 0)
        {
            struct Direction dir = checkDirection(direction)
            move(&ball, dir);
        }  
    }
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
  osCreateProcess(proc, procStack, PROC_STACK_SIZE, &pid, 3, NULL, &error);
  osStartProcess(pid, &error);

  osDeleteProcess();
}


