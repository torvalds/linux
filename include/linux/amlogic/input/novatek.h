/*
 * include/linux/novatek_ts.h
 *
 * Copyright (C) 2010 - 2011 Novatek, Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef 	_LINUX_NOVATEK_TOUCH_H 
#define		_LINUX_NOVATEK_TOUCH_H


//#define NT11002
#define NT11003

#define NOVATEK_TS_ADDR		(0x01)
#define NOVATEK_HW_ADDR		(0x7F)


//*************************TouchScreen Work Part*****************************

#define NOVATEK_TS_NAME		"nt1100x"
//#define	NOVATEK_TS_SCLK		200*1000

/*******************Step 1: define resolution *****************/
//define default resolution of the touchscreen
#define TP_MAX_WIDTH	1280
#define TP_MAX_HEIGHT	800

//define default resolution of LCM
#define LCD_MAX_WIDTH	1280
#define LCD_MAX_HEIGHT	800

//#define TOOL_PRESSURE		100
//#if 0
//#define INT_PORT  	    S3C64XX_GPN(15)						//Int IO port  S3C64XX_GPL(10)
//#ifdef INT_PORT
//	#define TS_INT 		gpio_to_irq(INT_PORT)			//Interrupt Number,EINT18(119)
//	#define INT_CFG    	S3C_GPIO_SFN(2)					//IO configer as EINT
//#else
//	#define TS_INT	0
//#endif	
//#else
///**********************Step 2: Setting Interrupt******************************/
//#define BABBAGE_NOVATEK_TS_RST1  (('k'-'a')*8+7)
//#define BABBAGE_NOVATEK_TS_INT1 (('j'-'a')*8+0)
//
//#define INT_PORT BABBAGE_NOVATEK_TS_INT1
//#define TS_INT gpio_to_irq(INT_PORT)
//
//#endif
/******************Step 3: Setting Reset option**************************/

//#define Novatek_HWRST_LowLeval()    (gpio_set_value(BABBAGE_NOVATEK_TS_RST1, 0))
//#define Nvoatek_HWRST_HighLeval()   (gpio_set_value(BABBAGE_NOVATEK_TS_RST1, 1))
//whether need send cfg?


#if defined(NT11002)

#define IIC_BYTENUM			4
#define MAX_FINGER_NUM		2

#elif defined(NT11003)

#define IIC_BYTENUM			6
#define MAX_FINGER_NUM		5

#endif


//#define  HAVE_TOUCH_KEY
#ifdef HAVE_TOUCH_KEY
#define MENU        21
#define HOME        22
#define BACK        23
#define VOLUMEDOWN  24
#define VOLUMEUP    25
#endif

//#define swap(x, y) do { typeof(x) z = x; x = y; y = z; } while (0)


//static const char *novatek_ts_name = "nt1103-ts";
//static struct workqueue_struct *novatek_wq;
//struct i2c_client * i2c_connect_client_novatek = NULL; 
//static struct proc_dir_entry *novatek_proc_entry;
//static struct kobject *novatek_debug_kobj;
//	
//#ifdef CONFIG_HAS_EARLYSUSPEND
//static void novatek_ts_early_suspend(struct early_suspend *h);
//static void novatek_ts_late_resume(struct early_suspend *h);
//#endif 

//*****************************End of Part I *********************************

//*************************Touchkey Surpport Part*****************************
//#define HAVE_TOUCH_KEY
#ifdef HAVE_TOUCH_KEY

const uint16_t touch_key_array[] =
{
	KEY_MENU,				//MENU
	KEY_HOME,				//HOME
	KEY_SEND				//CALL							  
};

#define MAX_KEY_NUM	 (sizeof(touch_key_array)/sizeof(touch_key_array[0]))

#endif
//*****************************End of Part II*********************************


#define TP_COORDINATE_XY_CHANGE
//#define TP_COORDINATE_X_REVERSE
//#define TP_COORDINATE_Y_REVERSE


#define NTP_APK_DRIVER_FUNC_SUPPORT
#define NVT_BOOTLOADER_FUNC_SUPPORT
//#define NTP_CHARGER_DETECT_SUPPORT


#endif /* _LINUX_NOVATEK_TOUCH_H */
