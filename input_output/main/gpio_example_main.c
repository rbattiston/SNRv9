/* GPIO Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_log.h"

/**
 * Brief:
 * This test code shows how to configure gpio and how to use gpio interrupt.
 *
 * GPIO status:
 * GPIO18: output
 * GPIO19: output
 * GPIO4:  input, pulled up, interrupt from rising edge and falling edge
 * GPIO5:  input, pulled up, interrupt from rising edge.
 *
 * Note. These are the default GPIO pins to be used in the example. You can
 * change IO pins in menuconfig.
 *
 * Test:
 * Connect GPIO18 with GPIO4
 * Connect GPIO19 with GPIO5
 * Generate pulses on GPIO18/19, that triggers interrupt on GPIO4/5
 *
 */

#define CLOCK_595       22
#define LATCH_595       23
#define DATA_595        12
#define OE_595          13
#define LOAD_165        0
#define CLK_165         2
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<CLOCK_595) | (1ULL<<LATCH_595) | (1ULL<<DATA_595) | (1ULL<<OE_595))

#define DATA165     15

uint8_t Out_Value = 0;
uint8_t Value = 0;

uint8_t Read_74HC165(void)
{
    uint8_t i;
    uint8_t Temp = 0;
    gpio_set_level(LOAD_165, 0);
	gpio_set_level(LOAD_165, 1);
    for(i=0;i<8;i++)
    {
        Temp <<= 1;
        gpio_set_level(CLK_165, 0); 
        if(gpio_get_level(DATA165) == 0){Temp |= 0x01;}
        gpio_set_level(CLK_165, 1);      
    }  
    return Temp;
}

void Get_Input_Value(void)
{
   uint8_t Value1 = 0;
    uint8_t Value2 = 0;
    Value1 = Read_74HC165();
    vTaskDelay(20 / portTICK_PERIOD_MS);
    Value2 = Read_74HC165();
    if(Value1 == Value2)
    {
        Value = Value1;
    }
}

void Send_Bytes(uint8_t dat)   //Send 1 byte
{
    uint8_t i;
    for(i=8;i>=1;i--)
    {
        if(dat & 0x80){gpio_set_level(DATA_595, 1);}
        else {gpio_set_level(DATA_595, 0);}       //Sends data bit by bit from high to low.
        dat <<= 1;
		gpio_set_level(CLOCK_595, 0);
	    gpio_set_level(CLOCK_595, 1);
    }
}

void Send_74HC595(uint8_t out)
{
    Send_Bytes(out);
	gpio_set_level(LATCH_595, 0);
	gpio_set_level(LATCH_595, 1);
}

void app_main(void)
{
    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    gpio_reset_pin(DATA165);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(DATA165, GPIO_MODE_INPUT);

    gpio_reset_pin(LOAD_165);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(LOAD_165, GPIO_MODE_OUTPUT);

    gpio_reset_pin(CLK_165);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(CLK_165, GPIO_MODE_OUTPUT);

    gpio_set_level(OE_595, 1);
    Send_74HC595(0);
    gpio_set_level(OE_595, 0);

    while(1) {
        Get_Input_Value();
        Send_74HC595(Value);
    }
}
