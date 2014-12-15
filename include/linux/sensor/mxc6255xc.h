
/******************** (C) COPYRIGHT 2010 STMicroelectronics ********************
*
* File Name          : mxc622x.h
* Authors            : MH - C&I BU - Application Team
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
* THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
*
*******************************************************************************/


#ifndef	__MXC622X_H__
#define	__MXC622X_H__

#include	<linux/ioctl.h>	/* For IOCTL macros */
#include	<linux/input.h>

#ifndef DEBUG
#define DEBUG
#endif

#define	MXC622X_ACC_IOCTL_BASE 77
/** The following define the IOCTL command values via the ioctl macros */
#define	MXC622X_ACC_IOCTL_SET_DELAY		_IOW(MXC622X_ACC_IOCTL_BASE, 0, int)
#define	MXC622X_ACC_IOCTL_GET_DELAY		_IOR(MXC622X_ACC_IOCTL_BASE, 1, int)
#define	MXC622X_ACC_IOCTL_SET_ENABLE		_IOW(MXC622X_ACC_IOCTL_BASE, 2, int)
#define	MXC622X_ACC_IOCTL_GET_ENABLE		_IOR(MXC622X_ACC_IOCTL_BASE, 3, int)
#define	MXC622X_ACC_IOCTL_GET_COOR_XYZ       _IOW(MXC622X_ACC_IOCTL_BASE, 22, int)
#define	MXC622X_ACC_IOCTL_GET_CHIP_ID        _IOR(MXC622X_ACC_IOCTL_BASE, 255, char[32])

/************************************************/
/* 	Accelerometer defines section	 	*/
/************************************************/
#define MXC622X_ACC_DEV_NAME		"mxc6255xc"
#define MXC622X_ACC_INPUT_NAME		"mxc6255xc" 
#define MXC622X_ACC_I2C_ADDR     	0x15
#define MXC622X_ACC_I2C_NAME     	MXC622X_ACC_DEV_NAME

/* MXC622X register address */
#define MXC622X_REG_CTRL		0x04
#define MXC622X_REG_DATA		0x00

/* MXC622X control bit */
#define MXC622X_CTRL_PWRON		0x00	/* power on */
#define MXC622X_CTRL_PWRDN		0x80	/* power donw */

//#if defined(CONFIG_MACH_SP6810A)
//#define I2C_BUS_NUM_STATIC_ALLOC
//#define I2C_STATIC_BUS_NUM        ( 0)	// Need to be modified according to actual setting
//#endif

#ifdef	__KERNEL__
struct mxc622x_acc_platform_data {
	int poll_interval;
	int min_interval;
	int max_interval;
	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

};
#endif	/* __KERNEL__ */

#endif	/* __MXC622X_H__ */



