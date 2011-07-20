/*---------------------------------------------------------------------------------------------------------
 * kernel/include/linux/goodix_touch.h
 *
 * Copyright(c) 2010 Goodix Technology Corp. All rights reserved.      
 * Author: Eltonny
 * Date: 2010.11.11                                    
 *                                                                                                         
 *---------------------------------------------------------------------------------------------------------*/

#ifndef 	_LINUX_GOODIX_TOUCH_H
#define		_LINUX_GOODIX_TOUCH_H

#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>

#define GOODIX_I2C_NAME "goodix-ts"
#define GUITAR_GT80X
//触摸屏的分辨率
#define TOUCH_MAX_HEIGHT 	7680	
#define TOUCH_MAX_WIDTH	 	5120
//显示屏的分辨率，根据具体平台更改，与触摸屏映射坐标相关
#define SCREEN_MAX_HEIGHT	1024				
#define SCREEN_MAX_WIDTH	600

//#define SHUTDOWN_PORT 	RK29_PIN4_PD5			//SHUTDOWN管脚号
//#define INT_PORT  		RK29_PIN0_PA2			//Int IO port

#if 0
#ifdef INT_PORT
	#define TS_INT 		gpio_to_irq(INT_PORT)	//Interrupt Number,EINT18 as 119
	//#define  INT_CFG    	S3C_GPIO_SFN(3)			//IO configer,EINT type
#else
	#define TS_INT 	0
#endif
#endif

#define FLAG_UP 	0
#define FLAG_DOWN 	1

#define GOODIX_MULTI_TOUCH
#ifndef GOODIX_MULTI_TOUCH
	#define MAX_FINGER_NUM 1
#else
	#define MAX_FINGER_NUM 2					//最大支持手指数(<=5)
#endif
#undef GOODIX_TS_DEBUG

#define gt80xy_swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)

struct goodix_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	uint8_t use_irq;
	uint8_t use_shutdown;
	struct hrtimer timer;
	struct work_struct  work;
	char phys[32];
	int bad_data;
	int retry;

	struct early_suspend early_suspend;
	int (*power)(struct goodix_ts_data * ts, int on);
};

struct goodix_i2c_rmi_platform_data {
	uint32_t version;	/* Use this entry for panels with */
	//该结构体用于管理设备平台资源
	//预留，用于之后的功能扩展

	unsigned shutdown_pin;
	unsigned irq_pin;
};

#endif /* _LINUX_GOODIX_TOUCH_H */
