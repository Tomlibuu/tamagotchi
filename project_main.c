// Made by Alex Heikkinen, Hannu Sirviö and Tommi Kyllönen

/* C Standard library */
#include <stdio.h>
#include <string.h>

/* XDCtools files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/i2c/I2CCC26XX.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/UART.h>

/* Board Header files */
#include "Board.h"
#include "buzzer.h"
#include "sensors/mpu9250.h"

/* Task */
#define STACKSIZE 2048
Char sensorTaskStack[STACKSIZE];
Char uartTaskStack[STACKSIZE];

// Definition of the state machine
enum state
{
    WAITING = 1, READ, EAT, PET, EXERCISE
};
enum state programState = WAITING;

// Global variables for MPU values
float ax, ay, az, gx, gy, gz;

// Clock handle
Clock_Handle clkHandle;
Clock_Params clkParams;

// Leds, buttons and buzzer RTOS variables
static PIN_Handle buttonHandle;
static PIN_Handle button2Handle;
static PIN_State buttonState;
static PIN_State button2State;
static PIN_Handle ledHandle;
static PIN_Handle led2Handle;
static PIN_State ledState;
static PIN_State led2State;
static PIN_Handle buzzerHandle;
static PIN_State buzzerState;

// UART read message
uint8_t readResult[30];
uint8_t test[30];
int8_t index = 0;

// MPU power pin global variables
static PIN_Handle hMpuPin;
static PIN_State MpuPinState;

// MPU power pin
static PIN_Config MpuPinConfig[] = {
Board_MPU_POWER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_HIGH | PIN_PUSHPULL
                                             | PIN_DRVSTR_MAX,
                                     PIN_TERMINATE };

// MPU uses its own I2C interface
static const I2CCC26XX_I2CPinCfg i2cMPUCfg = { .pinSDA = Board_I2C0_SDA1,
                                               .pinSCL = Board_I2C0_SCL1 };

// RTOS variables configurations
PIN_Config buttonConfig[] = {
Board_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
                              PIN_TERMINATE
        };

PIN_Config button2Config[] = {
Board_BUTTON1 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
                               PIN_TERMINATE
        };

PIN_Config buzzerConfig[] = {
Board_BUZZER | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW,
                              PIN_TERMINATE
        };


PIN_Config ledConfig[] = {
Board_LED1 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
                           PIN_TERMINATE
        };

PIN_Config led2Config[] = {
Board_LED0 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
                            PIN_TERMINATE
        };

void clkFxn(UArg arg0)
{
    buzzerClose();
    Clock_stop(clkHandle);
}

// Interrupt handler functions
void buttonFxn(PIN_Handle handle, PIN_Id pinId)
{

    // Blink red led of the device
    uint_t pinValue = PIN_getOutputValue( Board_LED1);
    pinValue = !pinValue;
    PIN_setOutputValue(ledHandle, Board_LED1, pinValue);
}

void button2Fxn(PIN_Handle handle, PIN_Id pinId)
{

    // Blink green led of the device
    uint_t pinValue = PIN_getOutputValue( Board_LED0);
    pinValue = !pinValue;
    PIN_setOutputValue(led2Handle, Board_LED0, pinValue);
}

// Set state to 'READ' when message got from UART
void setReadState(uint8_t *buffer, size_t len)
{
    test[index] = buffer[0];
    index++;
    programState = READ;
}

static void uartFxn(UART_Handle handle, void *rxBuf, size_t len)
{
    setReadState(rxBuf, len);

    // Käsittelijän viimeisenä asiana siirrytään odottamaan uutta keskeytystä..
    UART_read(handle, rxBuf, 1);
}

/* Task Functions */
Void uartTaskFxn(UArg arg0, UArg arg1)
{

    // UART-library settings
    UART_Handle uart;
    UART_Params uartParams;

    // Initialize serial communication
    UART_Params_init(&uartParams);
    uartParams.writeDataMode = UART_DATA_TEXT;
    uartParams.readDataMode = UART_DATA_TEXT;
    uartParams.readMode = UART_MODE_CALLBACK;
    uartParams.readCallback = &uartFxn;
    uartParams.readEcho = UART_ECHO_OFF;
    uartParams.baudRate = 9600;
    uartParams.dataLength = UART_LEN_8;
    uartParams.parityType = UART_PAR_NONE;
    uartParams.stopBits = UART_STOP_ONE;

    // Open connection to serial port in the constant Board_UART0
    uart = UART_open(Board_UART0, &uartParams);
    if (uart == NULL)
    {
        System_abort("Error opening the UART");
    }

    UART_read(uart, readResult, 1);

    while (1)
    {
        Clock_setTimeout(clkHandle, 500000 / Clock_tickPeriod);

        char sendmessage[100];
        // Send message to backend depending on current state
        if (programState == EXERCISE)
        {
            sprintf(sendmessage, "id:3301,EXERCISE:1\n\r\0");
            UART_write(uart, sendmessage, strlen(sendmessage) + 1);
            programState = WAITING;
        }
        else if (programState == PET)
        {
            sprintf(sendmessage, "id:3301,PET:1\n\r\0");
            UART_write(uart, sendmessage, strlen(sendmessage) + 1);
            programState = WAITING;
        }
        else if (programState == EAT)
        {
            sprintf(sendmessage, "id:3301,EAT:1\n\r\0");
            UART_write(uart, sendmessage, strlen(sendmessage) + 1);
            programState = WAITING;
        }
        else if (programState == READ)
        {

            char id[] = "3301";
            bool isSame = false;
            int8_t i = 0;

            // Check that read uart message start with our sensortag's id
            for (i = 0; i < 4; i++)
            {
                if (id[i] == test[i])
                {
                    isSame = true;
                }
                else
                {
                    isSame = false;
                    break;
                }
            }

            // Incoming uart messages have at 10th index significant char
            // Which tells what need is asked. If tamagotchi is hungry
            // the message's 10th index char is R, when needing petting
            // the significant index char is I and with exercise need
            // the char is S
            if (isSame == true && test[10] == 'R')
            {
                index = 0;
                int i = 0;

                for (i = 0; i < 30; i++)
                {
                    test[i] = 0;
                }

                buzzerOpen(buzzerHandle);
                buzzerSetFrequency(1000);
                Clock_start(clkHandle);
                while (Clock_isActive(clkHandle) == true)
                {
                    // Empty loop to keep execution in one task when clock running
                }

            }
            else if (isSame == true && test[10] == 'I')
            {
                index = 0;
                int i = 0;

                for (i = 0; i < 30; i++)
                {
                    test[i] = 0;
                }
                buzzerOpen(buzzerHandle);
                buzzerSetFrequency(4000);
                Clock_start(clkHandle);
                while (Clock_isActive(clkHandle) == true)
                {
                    // Empty loop to keep execution in one task when clock running
                }

            }
            else if (isSame == true && test[10] == 'S')
            {
                index = 0;
                int i = 0;

                for (i = 0; i < 30; i++)
                {
                    test[i] = 0;
                }
                buzzerOpen(buzzerHandle);
                buzzerSetFrequency(7000);
                Clock_start(clkHandle);
                while (Clock_isActive(clkHandle) == true)
                {
                    // Empty loop to keep execution in one task when clock running
                }

            }

            index = 0;

            for (i = 0; i < 30; i++)
            {
                test[i] = 0;
            }
            programState = WAITING;
        }

        Task_sleep(100000 / Clock_tickPeriod);
    }
}

Void sensorTaskFxn(UArg arg0, UArg arg1)
{

    I2C_Handle i2cMPU;
    I2C_Params i2cMPUParams;

    I2C_Params_init(&i2cMPUParams);
    i2cMPUParams.bitRate = I2C_400kHz;
    // Note the different configuration below
    i2cMPUParams.custom = (uintptr_t) &i2cMPUCfg;

    // MPU power on
    PIN_setOutputValue(hMpuPin, Board_MPU_POWER, Board_MPU_POWER_ON);

    // Wait 100ms for the MPU sensor to power up
    Task_sleep(100000 / Clock_tickPeriod);

    // MPU open i2c
    i2cMPU = I2C_open(Board_I2C, &i2cMPUParams);

    if (i2cMPU == NULL)
    {
        System_abort("Error Initializing I2CMPU\n");
    }

    Task_sleep(100000 / Clock_tickPeriod);
    mpu9250_setup(&i2cMPU);

    while (1)
    {
        Clock_setTimeout(clkHandle, 100000 / Clock_tickPeriod);
        if (programState == WAITING)
        {

            // MPU ask data
            mpu9250_get_data(&i2cMPU, &ax, &ay, &az, &gx, &gy, &gz);

            // Recognize actions from combination of led states and limit values
            if ((ay > 1.5 || ay < -1.5) && PIN_getOutputValue( Board_LED0) == 1
                    && PIN_getOutputValue( Board_LED1) == 0)
            {
                buzzerOpen(buzzerHandle);
                buzzerSetFrequency(6000);
                Clock_start(clkHandle);
                while (Clock_isActive(clkHandle) == true)
                {
                    // Empty loop to keep execution in one task when clock running
                }

                programState = EAT;
            }
            else if ((ax > 1 || ax < -1) && PIN_getOutputValue( Board_LED1) == 1
                    && PIN_getOutputValue( Board_LED0) == 0)
            {

                buzzerOpen(buzzerHandle);
                buzzerSetFrequency(6000);
                Clock_start(clkHandle);
                while (Clock_isActive(clkHandle) == true)
                {
                    // Empty loop to keep execution in one task when clock running
                }

                programState = PET;

            }

            else if ((az > 2 || az < -2) && PIN_getOutputValue( Board_LED1) == 1
                    && PIN_getOutputValue( Board_LED0) == 1)
            {
                buzzerOpen(buzzerHandle);
                buzzerSetFrequency(6000);
                Clock_start(clkHandle);
                while (Clock_isActive(clkHandle) == true)
                {
                    // Empty loop to keep execution in one task when clock running
                }

                programState = EXERCISE;
            }

        }

        Task_sleep(100000 / Clock_tickPeriod);

    }
}

Int main(void)
{

    // Task variables
    Task_Handle sensorTaskHandle;
    Task_Params sensorTaskParams;
    Task_Handle uartTaskHandle;
    Task_Params uartTaskParams;

    // Initialize board
    Board_initGeneral();

    // Initialize i2c bus
    Board_initI2C();

    // Initialize UART
    Board_initUART();

    // Open MPU power pin
    hMpuPin = PIN_open(&MpuPinState, MpuPinConfig);
    if (hMpuPin == NULL)
    {
        System_abort("Pin open failed!");
    }

    // Open the button, buzzer and led pins
    buttonHandle = PIN_open(&buttonState, buttonConfig);
    if (!buttonHandle)
    {
        System_abort("Error initializing button pins\n");
    }

    button2Handle = PIN_open(&button2State, button2Config);
    if (!button2Handle)
    {
        System_abort("Error initializing button pins\n");
    }

    ledHandle = PIN_open(&ledState, ledConfig);
    if (!ledHandle)
    {
        System_abort("Error initializing LED pins\n");
    }

    led2Handle = PIN_open(&led2State, led2Config);
    if (!led2Handle)
    {
        System_abort("Error initializing LED pins\n");
    }

    buzzerHandle = PIN_open(&buzzerState, buzzerConfig);
    if (!buzzerHandle)
    {
        System_abort("Error initializing buzzer pin\n");
    }

    // Set interrupt handler functions to buttons
    if (PIN_registerIntCb(buttonHandle, &buttonFxn) != 0)
    {
        System_abort("Error registering button callback function");
    }
    if (PIN_registerIntCb(button2Handle, &button2Fxn) != 0)
    {
        System_abort("Error registering button callback function");
    }

    // Initialize clock
    Clock_Params_init(&clkParams);
    clkParams.period = 0;
    clkParams.startFlag = FALSE;
    clkHandle = Clock_create((Clock_FuncPtr) clkFxn, 100000 / Clock_tickPeriod,
                             &clkParams, NULL);
    if (clkHandle == NULL)
    {
        System_abort("Clock create failed");
    }

    /* Task */
    Task_Params_init(&sensorTaskParams);
    sensorTaskParams.stackSize = STACKSIZE;
    sensorTaskParams.stack = &sensorTaskStack;
    sensorTaskParams.priority = 2;
    sensorTaskHandle = Task_create(sensorTaskFxn, &sensorTaskParams, NULL);
    if (sensorTaskHandle == NULL)
    {
        System_abort("Task create failed!");
    }

    Task_Params_init(&uartTaskParams);
    uartTaskParams.stackSize = STACKSIZE;
    uartTaskParams.stack = &uartTaskStack;
    uartTaskParams.priority = 2;
    uartTaskHandle = Task_create(uartTaskFxn, &uartTaskParams, NULL);
    if (uartTaskHandle == NULL)
    {
        System_abort("Task create failed!");
    }

    /* Sanity check */
    System_printf("Hello world!\n");
    System_flush();

    /* Start BIOS */
    BIOS_start();

    return (0);
}
