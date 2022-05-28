/******************************************************************************
 *
 * Copyright:
 *    Byczki(TM)
 *
 * File:
 *    ball_game.c
 *
 * Description:
 *    Implement functions resonsible for the game mechanics.
 *
 *****************************************************************************/

#include <stdlib.h>
#include "pre_emptive_os/api/osapi.h"
#include "lcd.h"
#include "pca9532.h"
#include "adc.h"
#include "general.h"
#include "ball_game.h"

#define ACC_X_CTRL_STACK_SIZE 512
#define ACC_Y_CTRL_STACK_SIZE 512
#define OBSTACLES_CTRL_STACK_SIZE 512
#define GAME_TIME_STACK_SIZE 128

#define MAX_OBSTACLES 6
#define MIN_INTERVAL 30

#define NOTHING 0x00
#define UP      0x01
#define RIGHT   0x02
#define DOWN    0x04
#define LEFT    0x08


typedef struct Ball
{
    tU16 xPos;
    tU16 yPos;
    tU8 speed;
    tU8 radius;
} Ball;

typedef struct Obstacle
{
    tU16 xPos;
    tU16 yPos;
    tU8 speed;
    tU8 height;
    tU8 width;
} Obstacle;

static tU8 accXCtrlStack[ACC_X_CTRL_STACK_SIZE];
static tU8 accYCtrlStack[ACC_Y_CTRL_STACK_SIZE];
static tU8 obstaclesCtrlStack[OBSTACLES_CTRL_STACK_SIZE];
static tU8 gameTimeStack[GAME_TIME_STACK_SIZE];

static tU8 pidAccXCtrl;
static tU8 pidAccYCtrl;
static tU8 pidObstaclesCtrl;
static tU8 pidGameTime;

static const tU8 pixelsPerDiodRow = (tU8) LCD_HEIGHT / 8;

static tU16 refXValue;
static tU16 refYValue;

static volatile tU8 obstacleDelay = 300;
static volatile tU8 diodsRow = 0;
static volatile tBool isInProgress = FALSE;
static volatile tU32 gameTime = 0;
static volatile tBool pca9532Present = FALSE;

static Ball ball;
static Obstacle obstacles[MAX_OBSTACLES];

static tU16
clamp(tU16 val, tU16 min, tU16 max)
{
    if (val >= max) return max;
    if (val <= min) return min;
    return val;
}

static tU16
random(tU16 minInc, tU16 maxInc)
{
    if (maxInc < minInc) return 0;
    return rand() % (maxInc - minInc + 1) + minInc;
}

static tU16
calculateStrength(tU16 absoluteValue)
{
    if (absoluteValue < 0) return 0;
    if (absoluteValue < 30) return 0;
    return clamp(absoluteValue / 20, 0, 8);
}

static tU16
calculateDelay(tU16 strength)
{
    if (strength <= 0) return 40;
    return 136 - 16 * strength;     // linear
}

static tU16
getScore()
{
    return gameTime * 10;
}

static tBool
isCollision(Obstacle *obstacle)
{
    tU8 ballRadius = ball.radius;

    tU16 ballYPos = ball.yPos;
    tU16 obstacleYPos = obstacle->yPos;
    tU8 obstacleHeight = obstacle->height;
    if (ballYPos > obstacleYPos + obstacleHeight || 
        ballYPos + ballRadius < obstacleYPos) return FALSE;

    tU16 ballXPos = ball.xPos;
    tU16 obstacleXPos = obstacle->xPos;
    tU8 obstacleWidth = obstacle->width;
    if (ballXPos > obstacleXPos + obstacleWidth || 
        ballXPos + ballRadius < obstacleXPos) return FALSE;

    return TRUE;
}

static void
randomizeObstacle(Obstacle *obstacle)
{
    tU8 newWidth = (tU8)random(20, 80);
    tU8 newHeight = (tU8)random(1, 5);
    tU8 newSpeed = (tU8)random(1, 6);
    tU16 newXPos = random(0, LCD_WIDTH - newWidth);
    tU16 newYPos = 0;

    obstacle->width = newWidth;
    obstacle->height = newHeight;
    obstacle->speed = newSpeed;
    obstacle->xPos = newXPos;
    obstacle->yPos = newYPos;
}

static void
fillObstacles(void)
{
    tU16 minY = LCD_HEIGHT;
    Obstacle *newObstacle;
    tU8 i;
    for (i = 0; i < MAX_OBSTACLES; i++)
    {
        Obstacle *obstacle = &obstacles[i];
        tU16 yPos = obstacle->yPos;
        if (yPos < LCD_HEIGHT)
        {
            if (yPos < minY) minY = yPos;
            continue;
        }
        newObstacle = obstacle;
    }
    if (minY < MIN_INTERVAL) return;

    randomizeObstacle(newObstacle);
}

static tBool
isAnyCollision()
{
    tU8 i;
    for (i = 0; i < MAX_OBSTACLES; i++)
    {
        Obstacle *obstacle = &obstacles[i];
        if (isCollision(obstacle)) return TRUE;
    }
    return FALSE;
}

static void
overdrawBall(tU8 color)
{
    lcdRect(ball.xPos, ball.yPos, ball.radius, ball.radius, color);
}

static void
overdrawObstacles(tU8 color)
{
    tU8 i;
    for (i = 0; i < MAX_OBSTACLES; i++)
    {
        Obstacle *obstacle = &obstacles[i];
        if (obstacle->yPos > LCD_HEIGHT) continue;

        lcdRect(obstacle->xPos, obstacle->yPos, obstacle->width, obstacle->height, color);
    }
}

static void
detectCollisions()
{
    if (isAnyCollision()) 
    {
        stopGame();
    }
}

static void
updateDiods()
{
    tU8 newRow = clamp(ball.yPos / pixelsPerDiodRow, 0, 7); 
    if (newRow == diodsRow) return;

    setPca9532Pin(diodsRow, 1);
    setPca9532Pin(15 - diodsRow, 1);
    setPca9532Pin(newRow, 0);
    setPca9532Pin(15 - newRow, 0);
    diodsRow = newRow;
}

static void
moveBall(tU8 dir)
{
    tBool moved = TRUE;
    switch (dir)
    {
    case UP:
        overdrawBall(BLACK);
        ball.yPos -= ball.speed;
        break;
    case DOWN:
        overdrawBall(BLACK);
        ball.yPos += ball.speed;
        break;
    case LEFT:
        overdrawBall(BLACK);
        ball.xPos -= ball.speed;
        break;
    case RIGHT:
        overdrawBall(BLACK);
        ball.xPos += ball.speed;
        break;
    default:
        moved = FALSE;
        break;
    }

    if (moved == FALSE) return;

    ball.yPos = clamp(ball.yPos, 0, LCD_HEIGHT);
    ball.xPos = clamp(ball.xPos, 0, LCD_WIDTH);

    overdrawBall(WHITE);
    detectCollisions();
}

static void
moveObstacles(void)
{
    overdrawObstacles(BLACK);
    tU8 i;
    for (i = 0; i < MAX_OBSTACLES; i++)
    {
        Obstacle *obstacle = &obstacles[i];
        if (obstacle->yPos > LCD_HEIGHT) continue;

        obstacle->yPos += obstacle->speed;
    }
    overdrawObstacles(WHITE);
    detectCollisions();
}

static void
moveBallAndWait(tU16 absoluteValue, tU8 dir, tBool update)
{
    tU16 strength = calculateStrength(absoluteValue);
    tU16 delay = calculateDelay(strength);

    if (strength > 0) moveBall(dir);
    if (update) updateDiods();

    osSleep(delay);
}

static void
diodsShowOff(tU16 delay)
{
    if (pca9532Present == FALSE) pca9532Present = pca9532Init();
    if (pca9532Present == FALSE) return;

    tS8 pin;
    for (pin = -7; pin < 8; pin++)
    {
        tU8 leftPin = abs(pin);
        tU8 rightPin = 16 - leftPin;
        setPca9532Pin(leftPin, 0);
        setPca9532Pin(rightPin, 0);
        osSleep(delay);
        setPca9532Pin(leftPin, 1);
        setPca9532Pin(rightPin, 1);
    }
}

static void
gameTimeProc()
{
    tU8 hoodlum = 0;
    while (isInProgress)
    {
        gameTime++;
        if (hoodlum++ >= 50)
        {
            hoodlum = 0;
            obstacleDelay -= (obstacleDelay / 15);
        }
        osSleep(100);
    }

    osDeleteProcess();
}

static void
accXCtrlProc(void)
{
    while (isInProgress)
    {
        tU16 value = refXValue - getAnalogueInput1(ACCEL_X);
        tU16 absoluteValue = abs(value);

        if (value > 0) moveBallAndWait(absoluteValue, UP, FALSE);
        else moveBallAndWait(absoluteValue, DOWN, FALSE);
    }

    osDeleteProcess();
}

static void
accYCtrlProc(void)
{
    while (isInProgress)
    {
        tU16 value = refYValue - getAnalogueInput1(ACCEL_Y);
        tU16 absoluteValue = abs(value);

        if (value > 0) moveBallAndWait(absoluteValue, RIGHT, TRUE);
        else moveBallAndWait(absoluteValue, LEFT, TRUE);
        updateDiods();
    }

    osDeleteProcess();
}

static void 
obstaclesCtrlProc(void)
{
    while (isInProgress)
    {
        osSleep(obstacleDelay);
        moveObstacles();
        fillObstacles();
    }

    osDeleteProcess();
}

static void
initScene(void)
{
    lcdColor(BLACK, WHITE);
    lcdClrscr();

    tU16 heightMiddle = LCD_HEIGHT / 2; 
    tU16 widthMiddle = LCD_HEIGHT / 2; 
    ball.xPos = widthMiddle;
    ball.yPos = heightMiddle;
    ball.speed = 5;
    ball.radius = 4;

    tU8 i;
    for (i = 0; i < MAX_OBSTACLES; i++)
    {
        obstacles[i].yPos = LCD_HEIGHT + 1;
    }

    overdrawBall(WHITE);
}

static void
startGame()
{
    if (isInProgress) return;
    isInProgress = TRUE;

    tU8 error;
    time_t t;
    srand(time(&t));

    gameTime = 0;
    refXValue = getAnalogueInput1(ACCEL_X);
    refYValue = getAnalogueInput1(ACCEL_Y);
    pca9532Present = pca9532Init();
    initScene();
    diodsShowOff(40);

    osCreateProcess(accXCtrlProc, accXCtrlStack, ACC_X_CTRL_STACK_SIZE, &pidAccXCtrl, 2, NULL, &error);
    osStartProcess(pidAccXCtrl, &error);

    osCreateProcess(accYCtrlProc, accYCtrlStack, ACC_Y_CTRL_STACK_SIZE, &pidAccYCtrl, 2, NULL, &error);
    osStartProcess(pidAccYCtrl, &error);

    osCreateProcess(obstaclesCtrlProc, obstaclesCtrlStack, OBSTACLES_CTRL_STACK_SIZE, &pidObstaclesCtrl, 2, NULL, &error);
    osStartProcess(pidObstaclesCtrl, &error);

    osCreateProcess(gameTimeProc, gameTimeStack, GAME_TIME_STACK_SIZE, &pidGameTime, 2, NULL, &error);
    osStartProcess(pidGameTime, &error);

    while(isInProgress);
}

static void
stopGame()
{
    if (isInProgress == FALSE) return;
    isInProgress = FALSE;
    diodsShowOff(80);
}