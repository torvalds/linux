
/******************** (C) COPYRIGHT 2011 STMicroelectronics ********************
*
* File Name	: lis3dh_acc_sysfs.h
* Authors	: MH - C&I BU - Application Team
*		: Matteo Dameno (matteo.dameno@st.com)
* Version	: V.1.0.12
* Date		: 2012/Feb/29
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
*******************************************************************************/
/*******************************************************************************
Version History.

 Revision 1.0.10: 2011/Aug/16

 Revision 1.0.11: 2012/Jan/09
  moved under input/misc
 Revision 1.0.12: 2012/Feb/29
  moved use_smbus inside status struct; modified:-update_fs_range;-set_range
  input format; allows gpio_intX to be passed as parameter at insmod time;
  renamed field g_range to fs_range in lis3dh_acc_platform_data;
  replaced defines SA0L and SA0H with LIS3DH_SAD0x
*******************************************************************************/

#ifndef	__LIS3DH_H__
#define	__LIS3DH_H__


#define	LIS3DH_ACC_DEV_NAME		"lis3dh_acc"

#define	LIS3DH_ACC_MIN_POLL_PERIOD_MS	1


#ifdef __KERNEL__

#define LIS3DH_SAD0L				(0x00)
#define LIS3DH_SAD0H				(0x01)
#define LIS3DH_ACC_I2C_SADROOT		(0x0C)

/* I2C address if acc SA0 pin to GND */
#define LIS3DH_ACC_I2C_SAD_L		((LIS3DH_ACC_I2C_SADROOT<<1)| \
						LIS3DH_SAD0L)

/* I2C address if acc SA0 pin to Vdd */
#define LIS3DH_ACC_I2C_SAD_H		((LIS3DH_ACC_I2C_SADROOT<<1)| \
						LIS3DH_SAD0H)

#define LIS3DH_ACC_DEFAULT_INT1_GPIO		(-EINVAL)
#define LIS3DH_ACC_DEFAULT_INT2_GPIO		(-EINVAL)

/* Accelerometer Sensor Full Scale */
#define	LIS3DH_ACC_FS_MASK		(0x30)
#define LIS3DH_ACC_G_2G		(0x00)
#define LIS3DH_ACC_G_4G		(0x10)
#define LIS3DH_ACC_G_8G		(0x20)
#define LIS3DH_ACC_G_16G		(0x30)

struct lis3dh_acc_platform_data {
	unsigned int poll_interval;
	unsigned int min_interval;

	u8 fs_range;

	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	u8 negate_x;
	u8 negate_y;
	u8 negate_z;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

	/* set gpio_int[1,2] either to the choosen gpio pin number or to -EINVAL
	 * if leaved unconnected
	 */
	int gpio_int1;
	int gpio_int2;
};

#endif	/* __KERNEL__ */

#endif	/* __LIS3DH_H__ */



