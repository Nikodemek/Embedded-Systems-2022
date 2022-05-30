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

#include "pre_emptive_os/api/osapi.h"
#include "./lcd.h"
#include "./pca9532.h"
#include "./adc.h"
#include "./general.h"
#include "./ball_game.h"
#include "startup/framework.h"

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
    tS16 xPos;
    tS16 yPos;
    tU8 speed;
    tU8 height;
    tU8 width;
} Obstacle;

tU8 accXCtrlStack[ACC_X_CTRL_STACK_SIZE];
tU8 accYCtrlStack[ACC_Y_CTRL_STACK_SIZE];
tU8 obstaclesCtrlStack[OBSTACLES_CTRL_STACK_SIZE];
tU8 gameTimeStack[GAME_TIME_STACK_SIZE];

tU8 pidAccXCtrl;
tU8 pidAccYCtrl;
tU8 pidObstaclesCtrl;
tU8 pidGameTime;

const tU8 pixelsPerDiodRow = (tU8)(LCD_HEIGHT / 8);

volatile tU16 obstacleDelay = 200;
volatile tU8 diodsRow = 0;
volatile tBool isInProgress = FALSE;
volatile tU32 gameTime = 0;
volatile tBool pca9532Present = FALSE;

Ball ball;
Obstacle obstacles[MAX_OBSTACLES];


/*!
 *  @brief    A procedure for delaying the execution
 *            of a code for a given amount of time.
 *  @param delay 
 *            A delay value as an 16-bit unsigned integer.
 */
static void
sleep(tU16 delay)
{
    osSleep(delay / 6);
}

/*!
 *  @brief    A function for clamping a value between
 *            a given min and a given max.
 *  @param val 
 *            A value to clamp.
 *  @param min 
 *            Min value of the result.
 *  @param max
 *            Max value of the result.
 *  @returns  a 16-bit signed integer value clamped
 *            between min and max
 */
static tS16
clamp(tS16 val, tS16 min, tS16 max)
{
    if (val >= max) return max;
    if (val <= min) return min;
    return val;
}

/*!
 *  @brief    A function for generating a pseudo-random
 *            16-bit unsigned integer value.
 *  @param minInc 
 *            Min value (inclusive)
 *  @param maxInc
 *            Max value (inclusive)
 *  @returns  a pseudo-random 16-bit unsigned integer
 *            ranging from minInc to maxInc
 */
static tU16
random(tU16 minInc, tU16 maxInc)
{
    if (maxInc < minInc) return 0;
    return rand() % (maxInc - minInc + 1) + minInc;
}

/*!
 *  @brief    A function for calculating ball movement
 *            strength.
 *  @param absoluteValue
 *            An absolute value of the difference between
 *            reference acclelerometer value and current
 *            value in a certain axis.
 *  @returns  calculated strength of a move ranging from 0 to 8
 */
static tU16
calculateStrength(tU16 absoluteValue)
{
    if (absoluteValue < 30) return 0;
    return clamp(absoluteValue / 20, 0, 8);
}

/*!
 *  @brief    A function for calculating ball movement
 *            delay.
 *  @param strength
 *            A strength of a move, ranging from 0 to 8.
 *  @returns  calculated delay before the next ball move
 */
static tU16
calculateDelay(tU16 strength)
{
    if (strength == 0) return 40;
    return 136 - 16 * strength;     // linear
}

/*!
 *  @brief    A function for reading player's score.
 *  @returns  player's score as an 32-bit unsigned integer
 */
tU32
getScore(void)
{
    return gameTime / 10 * 10;
}

/*!
 *  @brief    A function resposible for checking
 *            if the collision between the ball and
 *            given obstacle has occured.
 *  @param obstacle
 *            A pointer to the obstacle object for which
 *            the collision check shall happen.
 *  @returns  true if collisions occured, false if it did not
 */
tBool
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

/*!
 *  @brief    A procedure used to randomize the
 *            properties of the new obstacle and move
 *            it on top of the game-area
 *  @param obstacle
 *            A pointer to the obstacle object
 *            to be randomized.
 */
void
randomizeObstacle(Obstacle *obstacle)
{
    tU8 newWidth = (tU8)random(20, 60);
    tU8 newHeight = (tU8)random(1, 5);
    tU8 newSpeed = (tU8)random(1, 5);
    tU16 newXPos = random(0, LCD_WIDTH - newWidth);
    tU16 newYPos = 0;

    obstacle->width = newWidth;
    obstacle->height = newHeight;
    obstacle->speed = newSpeed;
    obstacle->xPos = newXPos;
    obstacle->yPos = newYPos;
}

/*!
 *  @brief    A procedure used to create an obstacle,
 *            when the are too few. Our application uses
 *            object-pooling method to reduce costs of
 *            instantiating new objects, so it looks for
 *            any out-of-in-game-area obstacle and if found
 *            it randomizes it's properties and moves it
 *            to the top.
 */
void
fillObstacles(void)
{
    tU16 minY = LCD_HEIGHT;
    tBool found = FALSE;
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
        found = TRUE;
    }
    if (minY < MIN_INTERVAL || found == FALSE) return;

    randomizeObstacle(newObstacle);
}

/*!
 *  @brief    A function checking if any ball-obstacle
 *            collision occured.
 *  @returns  true if such at least one occured,
 *            false if none
 */
tBool
isAnyCollision(void)
{
    tU8 i;
    for (i = 0; i < MAX_OBSTACLES; i++)
    {
        Obstacle *obstacle = &obstacles[i];
        if (isCollision(obstacle)) return TRUE;
    }
    return FALSE;
}

/*!
 *  @brief    A procedure for drawing the ball
 *            with the specified color.
 *  @param color
 *            A unsigned value 0-255 specifying a color.
 */
void
overdrawBall(tU8 color)
{
    lcdRect(ball.xPos, ball.yPos, ball.radius, ball.radius, color);
}

/*!
 *  @brief    A procedure for drawing over all the
 *            obstacles, that are in the in-game area, 
 *            with the specified color.
 *  @param color
 *            A unsigned value 0-255 specifying a color.
 */
void
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

/*!
 *  @brief    A procedure for detecting a possible
 *            collision and ending the game if such
 *            occured.
 */
void
detectCollisions(void)
{
    if (isAnyCollision())
    {
        stopGame();
    }
}

/*!
 *  @brief    A procedure for updating diods according
 *            to the ball's position.
 */
void
updateDiods(void)
{
    tU8 newRow = clamp(ball.yPos / pixelsPerDiodRow, 0, 7);
    if (newRow == diodsRow) return;

    setPca9532Pin(diodsRow, 1);
    setPca9532Pin(15 - diodsRow, 1);
    setPca9532Pin(newRow, 0);
    setPca9532Pin(15 - newRow, 0);
    diodsRow = newRow;
}

/*!
 *  @brief    A procedure for actually moving the
 *            ball in the specified direciont and 
 *            detecting possible collisions afterwards.
 */
void
moveBall(tU8 dir)
{
    tBool moved = TRUE;
    switch (dir)
    {
    case UP:
        overdrawBall(BLACK);
        ball.yPos = clamp(ball.yPos - ball.speed, 0, LCD_HEIGHT - ball.radius - 1);
        break;
    case DOWN:
        overdrawBall(BLACK);
        ball.yPos = clamp(ball.yPos + ball.speed, 0, LCD_HEIGHT - ball.radius - 1);
        break;
    case LEFT:
        overdrawBall(BLACK);
        ball.xPos = clamp(ball.xPos - ball.speed, 0, LCD_WIDTH - ball.radius - 1);
        break;
    case RIGHT:
        overdrawBall(BLACK);
        ball.xPos = clamp(ball.xPos + ball.speed, 0, LCD_WIDTH - ball.radius - 1);
        break;
    default:
        moved = FALSE;
        break;
    }

    if (moved == FALSE) return;

    overdrawBall(WHITE);
    detectCollisions();
}

/*!
 *  @brief    A procedure for actually moving all
 *            the obstacles, that are in the game area
 *            and detecting possible collisions afterwards.
 */
void
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

/*!
 *  @brief    A procedure for moving the ball in
 *            a certain direction, with a certain strength,
 *            optionally updating the diods and finally
 *            waiting some amount of time.
 *  @param absoluteValue 
 *            Absolute value of the move, is a base for
 *            calculating the strength of move and a delay.
 *  @param dir
 *            Direction in which the ball will move.
 *  @param update
 *            tBool indicating if diods update is neccessary.
 */
void
moveBallAndWait(tU16 absoluteValue, tU8 dir, tBool update)
{
    tU16 strength = calculateStrength(absoluteValue);
    tU16 delay = calculateDelay(strength);

    if (strength > 0) moveBall(dir);
    if (update) updateDiods();

    sleep(delay);
}

/*!
 *  @brief    A procedure resonsible for a quick
 *            diod-down-and-up animation.
 *  @param delay
 *            Delay between switching to nex row
 *            of diods. Value inversly proportional
 *            to the speed of the animation.
 *            (common values between 20 and 100)
 */
void
diodsShowOff(tU16 delay)
{
    if (pca9532Present == FALSE) pca9532Present = pca9532Init();
    if (pca9532Present == FALSE) return;

    tS8 pin;
    for (pin = -7; pin < 8; pin++)
    {
        tU8 leftPin = abs(pin);
        tU8 rightPin = 15 - leftPin;
        setPca9532Pin(leftPin, 0);
        setPca9532Pin(rightPin, 0);
        sleep(delay);
        setPca9532Pin(leftPin, 1);
        setPca9532Pin(rightPin, 1);
    }
}

/*!
 *  @brief    A procedure responsible for measuring
 *            user's in-game time and periodically increasing
 *            the difficulty level by speeding-up the obstacles.
 *            Designed to be a separate process.
 */
void
gameTimeProc(void)
{
    tU8 hoodlum = 0;
    while (isInProgress)
    {
        gameTime++;
        if (hoodlum++ >= 50)
        {
            hoodlum = 0;
            obstacleDelay -= (obstacleDelay / 20);
        }
        sleep(10);
    }

    osDeleteProcess();
}

/*!
 *  @brief    A procedure responsible for reading
 *            the X-axis of accelerometer and moving the
 *            ball accordingly to the input value.
 *            Designed to be a separate process.
 */
void
accXCtrlProc(void)
{
    tS16 refXValue = getAnalogueInput1(ACCEL_X);
    while (isInProgress)
    {
        tS16 value = refXValue - getAnalogueInput1(ACCEL_X);
        tU16 absoluteValue = abs(value);

        if (value > 0) moveBallAndWait(absoluteValue, UP, FALSE);
        else moveBallAndWait(absoluteValue, DOWN, FALSE);
    }

    osDeleteProcess();
}

/*!
 *  @brief    A procedure responsible for reading
 *            the Y-axis of accelerometer and moving the
 *            ball accordingly to the input value.
 *            Designed to be a separate process. 
 */
void
accYCtrlProc(void)
{
    tS16 refYValue = getAnalogueInput1(ACCEL_Y);
    while (isInProgress)
    {
        tS16 value = refYValue - getAnalogueInput1(ACCEL_Y);
        tU16 absoluteValue = abs(value);

        if (value > 0) moveBallAndWait(absoluteValue, RIGHT, TRUE);
        else moveBallAndWait(absoluteValue, LEFT, TRUE);
        updateDiods();
    }

    osDeleteProcess();
}

/*!
 *  @brief    A procedure responsible for obstacles
 *            movement, respawning and speed.
 *            Designed to be a separate process. 
 */
void
obstaclesCtrlProc(void)
{
    sleep(500);

    while (isInProgress)
    {
        fillObstacles();
        moveObstacles();
        sleep(obstacleDelay);
    }

    osDeleteProcess();
}

/*!
 *  @brief    A procedure for initializing the scene
 *            and the game state
 */
void
initScene(void)
{
    lcdColor(BLACK, WHITE);
    lcdClrscr();

    gameTime = 0;
    obstacleDelay = 200;

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

/*!
 *  @brief    A procedure for displaying user's score
 *            at the end of the game.
 */
static void
displayScoreWindow(void)
{
    lcdRect(0, 45, 130, 40, WHITE);
    lcdGotoxy(45, 48);
    lcdPuts("SCORE");

    tU32 score = getScore();
    tU32 tempScore = score;
    tU8 counter = 0;
    while(tempScore > 0)
    {
        counter++;
        tempScore /= 10;
    }
    tU16 x = 65 - counter * 4;

    lcdGotoxy(x, 65);
    char buffer[10];
    sprintf(buffer, "%d", score);
    lcdPuts(buffer);
}


/*!
 *  @brief    A procedure for starting a new game,
 *            it creates all the processes needed for 
 *            movement controll, obstacles movement and 
 *            in-game-timer.
 */
void
startGame(void)
{
    if (isInProgress) return;
    isInProgress = TRUE;

    tU8 error;
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

    // while(isInProgress);
}

/*!
 *  @brief    A procedure for stopping the game,
 *            called on collisions or user's button click.
 */
void
stopGame(void)
{
    if (isInProgress == FALSE) return;
    isInProgress = FALSE;
    diodsShowOff(40);
    displayScoreWindow();
}