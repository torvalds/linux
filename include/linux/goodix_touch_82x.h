/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 
 * Copyright (C) 2011 Goodix, Inc.
 * 
 * Author: Scott
 * Date: 2012.01.05
 */

#ifndef _LINUX_GOODIX_TOUCH_H
#define _LINUX_GOODIX_TOUCH_H

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>

#define fail    0
#define success 1

#define false   0
#define true    1

#define FLAG_UP 		0
#define FLAG_DOWN 	1

#define RELEASE_DATE "2012-02-07"

#define GOODIX_I2C_NAME	 "Goodix-TS-82X"
#define GOODIX_I2C_ADDR     0X5D

#if 1
#define GTDEBUG(fmt, arg...) printk("<--GT-DEBUG-->"fmt, ##arg)
#else
#define DEBUG(fmt, arg...)
#endif

#if 1
#define GTNOTICE(fmt, arg...) printk("<--GT-NOTICE-->"fmt, ##arg)
#else
#define NOTICE(fmt, arg...)
#endif

#if 1
#define GTWARNING(fmt, arg...) printk("<--GT-WARNING-->"fmt, ##arg)
#else
#define WARNING(fmt, arg...)
#endif

#if 1
#define GTDEBUG_MSG(fmt, arg...) printk("<--GT msg-->"fmt, ##arg)
#else
#define DEBUG_MSG(fmt, arg...)
#endif

#if 1
#define GTDEBUG_UPDATE(fmt, arg...) printk("<--GT update-->"fmt, ##arg)
#else
#define DEBUG_UPDATE(fmt, arg...)
#endif 

#if 0   //w++
#define GTDEBUG_COOR(fmt, arg...) printk(fmt, ##arg)
#define GTDEBUG_COORD
#else
#define GTDEBUG_COOR(fmt, arg...)
#define DEBUG_COOR(fmt, arg...)
#endif

#if 1
#define GTDEBUG_ARRAY(array, num)   do{\
                                   int i;\
                                   u8* a = array;\
                                   for (i = 0; i < (num); i++)\
                                   {\
                                       printk("%02x   ", (a)[i]);\
                                       if ((i + 1 ) %10 == 0)\
                                       {\
                                           printk("\n");\
                                       }\
                                   }\
                                   printk("\n");\
                                  }while(0)
#else
#define DEBUG_ARRAY(array, num) 
#endif 

#define ADDR_MAX_LENGTH     2
#define ADDR_LENGTH         ADDR_MAX_LENGTH

//#define CREATE_WR_NODE
//#define AUTO_UPDATE_GUITAR             //如果定义了则上电会自动判断是否需要升级

//--------------------------For user redefine-----------------------------//
//-------------------------GPIO REDEFINE START----------------------------//
#define GPIO_DIRECTION_INPUT(port)          gpio_direction_input(port)
#define GPIO_DIRECTION_OUTPUT(port, val)    gpio_direction_output(port, val)
#define GPIO_SET_VALUE(port, val)           gpio_set_value(port, val)
#define GPIO_FREE(port)                     gpio_free(port)
#define GPIO_REQUEST(port, name)            gpio_request(port, name)
#define GPIO_PULL_UPDOWN(port, val)      gpio_pull_updown(port,val)  // s3c_gpio_setpull(port, val)
#define GPIO_CFG_PIN(port, cfg)                 //s3c_gpio_cfgpin(port, cfg)
//-------------------------GPIO REDEFINE END------------------------------//


//*************************TouchScreen Work Part Start**************************

#define RESET_PORT          S3C64XX_GPF(3)          //RESET管脚号
#define INT_PORT            S3C64XX_GPL(10)         //Int IO port
#ifdef INT_PORT
    #define TS_INT          gpio_to_irq(INT_PORT)      //Interrupt Number,EINT18(119)
    #define INT_CFG         S3C_GPIO_SFN(3)            //IO configer as EINT
#else
    #define TS_INT          0
#endif


#define GT_IRQ_RISING       IRQ_TYPE_EDGE_RISING
#define GT_IRQ_FALLING      IRQ_TYPE_EDGE_FALLING
#define INT_TRIGGER         GT_IRQ_FALLING
#define POLL_TIME           10        //actual query spacing interval:POLL_TIME+6

#define GOODIX_MULTI_TOUCH
#ifdef GOODIX_MULTI_TOUCH
    #define MAX_FINGER_NUM    5
#else
    #define MAX_FINGER_NUM    1
#endif

#define gtswap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)

struct goodix_i2c_rmi_platform_data {
 /*   u8 bad_data;	
    u8 irq_is_disable;
    u16 addr;
    s32 use_reset;        //use RESET flag
    s32 use_irq;          //use EINT flag
    u32 version;
    s32 (*power)(struct goodix_ts_data * ts, s32 on);
    struct i2c_client *client;
    struct input_dev *input_dev;
    struct hrtimer timer;
    struct work_struct work;
    struct early_suspend early_suspend;
    s8 phys[32];
*/	
    uint32_t version;	/* Use this entry for panels with */
    unsigned gpio_shutdown;
    unsigned gpio_irq;
    unsigned gpio_reset;
    bool irq_edge; /* 0:rising edge, 1:falling edge */
    bool swap_xy;
    bool xpol;
    bool ypol;
    int xmax;
    int ymax;
    int config_info_len;
    u8 *config_info;
    int (*init_platform_hw)(void);

};
//*************************TouchScreen Work Part End****************************

#endif /* _LINUX_GOODIX_TOUCH_H */
