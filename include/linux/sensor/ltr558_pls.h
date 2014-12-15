/* Lite-On LTR-558ALS Linux Driver 
 * 
 * Copyright (C) 2011 Lite-On Technology Corp (Singapore) 
 *  
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * 
 */ 
 
#ifndef _LTR558_H 
#define _LTR558_H 
 
#include <linux/earlysuspend.h>
//#include <mach/board.h>
 
 #define LTR558_ADDR 		0X23
//#define  LTR558_DEVICE 		"LTR558"
#define  LTR558_DEVICE 		"ltr_558als"
//#define  LTR558_INPUT_DEV	"ltr558_input_dev"
#define  LTR558_INPUT_DEV	LTR558_DEVICE
/* LTR-558 Registers */ 
#define LTR558_ALS_CONTR  0x80 
#define LTR558_PS_CONTR  0x81 
#define LTR558_PS_LED   0x82 
#define LTR558_PS_N_PULSES  0x83 
#define LTR558_PS_MEAS_RATE 0x84 
#define LTR558_ALS_MEAS_RATE 0x85 
#define LTR558_MANUFACTURER_ID 0x87 
 
#define LTR558_INTERRUPT  0x8F 
#define LTR558_PS_THRES_UP_0 0x90 
#define LTR558_PS_THRES_UP_1 0x91 
#define LTR558_PS_THRES_LOW_0 0x92 
#define LTR558_PS_THRES_LOW_1 0x93 
 
#define LTR558_ALS_THRES_UP_0 0x97 
#define LTR558_ALS_THRES_UP_1 0x98 
#define LTR558_ALS_THRES_LOW_0 0x99 
#define LTR558_ALS_THRES_LOW_1 0x9A 
 
#define LTR558_INTERRUPT_PERSIST 0x9E 
 
/* 558's Read Only Registers */ 
#define LTR558_ALS_DATA_CH1_0 0x88 
#define LTR558_ALS_DATA_CH1_1 0x89 
#define LTR558_ALS_DATA_CH0_0 0x8A 
#define LTR558_ALS_DATA_CH0_1 0x8B 
#define LTR558_ALS_PS_STATUS 0x8C 
#define LTR558_PS_DATA_0  0x8D 
#define LTR558_PS_DATA_1  0x8E 
 
 
/* Basic Operating Modes  */
#define MODE_ALS_ON_Range1  0x0B 
#define MODE_ALS_ON_Range2  0x03 
#define MODE_ALS_StdBy   0x00 
 
#define MODE_PS_ON_Gain1  0x03 
#define MODE_PS_ON_Gain2  0x07 
#define MODE_PS_ON_Gain4  0x0B 
#define MODE_PS_ON_Gain8  0x0C 
#define MODE_PS_StdBy   0x00 

#define PS_RANGE1  1 
#define PS_RANGE2 2 
#define PS_RANGE4  4 
#define PS_RANGE8 8 
 
#define ALS_RANGE1_320  1 
#define ALS_RANGE2_64K  2 
 


/*  
 * Magic Number 
 * ============ 
 * Refer to file ioctl-number.txt for allocation 
 */

#define LTR_IOCTL_MAGIC                         0x1C
#define LTR_IOCTL_GET_PFLAG                     _IOR(LTR_IOCTL_MAGIC, 1, int)
#define LTR_IOCTL_GET_LFLAG                     _IOR(LTR_IOCTL_MAGIC, 2, int)
#define LTR_IOCTL_SET_PFLAG                     _IOW(LTR_IOCTL_MAGIC, 3, int)
#define LTR_IOCTL_SET_LFLAG                     _IOW(LTR_IOCTL_MAGIC, 4, int)
#define LTR_IOCTL_GET_PS_DATA                   _IOW(LTR_IOCTL_MAGIC, 5, unsigned char)
#define LTR_IOCTL_GET_ALS_DATA                  _IOW(LTR_IOCTL_MAGIC, 6, unsigned char)

/* Power On response time in ms   */
#define PON_DELAY 600 
#define WAKEUP_DELAY 10 

/* Interrupt vector number to use when probing IRQ number. 
 * User changeable depending on sys interrupt. 
 * For IRQ numbers used, see /proc/interrupts. 
 */ 

#define LTR558_PLS_IRQ_PIN "ltr558_irq_pin"

#define DRIVER_VERSION "1.0" 
#define DEVICE_NAME "LTR_558ALS" 
#define LTR558_DBG 0
#define LTR558_DBG_LDJ 1
#if LTR558_DBG
#define LTR558_DEBUG(format, ...)	\
		printk(KERN_INFO "LTR558" format "\n", ## __VA_ARGS__)
#else
#define LTR558_DEBUG(format, ...)
#endif

#if LTR558_DBG_LDJ
#define LTR558_DEBUG_LIUDJ(format, ...)   printk(KERN_INFO "LTR558 " format "\n", ## __VA_ARGS__)
#else
#define LTR558_DEBUG_LIUDJ(format, ...)
#endif
struct ltr558_pls_platform_data {
	int irq_gpio_number;
};
 

typedef struct ltr558_pls_t { 
	 int irq;
	 struct input_dev *input;
	 struct i2c_client *client; 
	 struct mutex lock; 
//	 struct work_struct	work;
//	 struct workqueue_struct *ltr_work_queue;
//	 struct early_suspend ltr_early_suspend;
	 struct work_struct	work;
	 struct workqueue_struct *ltr_work_queue;
	 struct early_suspend ltr_early_suspend;

}ltr558_pls_struct; 


#endif

