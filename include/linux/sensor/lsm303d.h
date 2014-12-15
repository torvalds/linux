/******************** (C) COPYRIGHT 2013 STMicroelectronics *******************
*
* File Name          : lsm303d.h
* Authors            : AMS - MSH Div - Application Team
*		     : Matteo Dameno (matteo.dameno@st.com)
*		     : Denis Ciocca (denis.ciocca@st.com)
* Version            : V.1.0.5
* Date               : 2013/Oct/23
*
*******************************************************************************
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
******************************************************************************/

#ifndef	__LSM303D_H__
#define	__LSM303D_H__

#define	LSM303D_DEV_NAME	"lsm303d"
#define	LSM303D_ACC_DEV_NAME	"lsm303d_acc"	/* Input file name */
#define	LSM303D_MAG_DEV_NAME	"lsm303d_mag"	/* Input file name */

#define LSM303D_SAD0L			(0x02)
#define LSM303D_SAD0H			(0x01)
#define LSM303D_I2C_SADROOT		(0x07)
#define LSM303D_I2C_SAD_L		((LSM303D_I2C_SADROOT<<2) | \
								LSM303D_SAD0L)
#define LSM303D_I2C_SAD_H		((LSM303D_I2C_SADROOT<<2) | \
								LSM303D_SAD0H)

/************************************************/
/* 	Output data			 	*/
/*************************************************
accelerometer: ug
magnetometer: ugauss
*************************************************/

/************************************************/
/* 	sysfs data			 	*/
/*************************************************
accelerometer:
	- pollrate->ms
	- fullscale->g
magnetometer:
	- pollrate->ms
	- fullscale->gauss
*************************************************/

/************************************************/
/* 	Accelerometer section defines	 	*/
/************************************************/

/* Accelerometer Sensor Full Scale */
#define	LSM303D_ACC_FS_MASK	(0x18)
#define LSM303D_ACC_FS_2G 	(0x00)	/* Full scale 2g */
#define LSM303D_ACC_FS_4G 	(0x08)	/* Full scale 4g */
#define LSM303D_ACC_FS_8G 	(0x10)	/* Full scale 8g */
#define LSM303D_ACC_FS_16G	(0x18)	/* Full scale 16g */

/* Accelerometer Anti-Aliasing Filter */
#define ANTI_ALIASING_773	(0X00)
#define ANTI_ALIASING_362	(0X40)
#define ANTI_ALIASING_194	(0X80)
#define ANTI_ALIASING_50	(0XC0)

/************************************************/
/* 	Magnetometer section defines	 	*/
/************************************************/

/* Magnetometer Sensor Full Scale */
#define LSM303D_MAG_FS_MASK	(0x60)
#define LSM303D_MAG_FS_2G	(0x00)	/* Full scale 2 gauss */
#define LSM303D_MAG_FS_4G	(0x20)	/* Full scale 4 gauss */
#define LSM303D_MAG_FS_8G	(0x40)	/* Full scale 8 gauss */
#define LSM303D_MAG_FS_12G	(0x60)	/* Full scale 12 gauss */


#ifdef	__KERNEL__

#define DEFAULT_INT1_GPIO		(-EINVAL)
#define DEFAULT_INT2_GPIO		(-EINVAL)

#define	LSM303D_ACC_MIN_POLL_PERIOD_MS	1
#define LSM303D_MAG_MIN_POLL_PERIOD_MS	5

struct lsm303d_acc_platform_data {
	
	unsigned int poll_interval;
	unsigned int min_interval;

	u8 fs_range;

	short rot_matrix[3][3];

	u8 aa_filter_bandwidth;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

	int gpio_int1;
	int gpio_int2;
};

struct lsm303d_mag_platform_data {

	unsigned int poll_interval;
	unsigned int min_interval;

	u8 fs_range;

	short rot_matrix[3][3];

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);
};

struct lsm303d_main_platform_data {
	
	struct lsm303d_acc_platform_data *pdata_acc;
	struct lsm303d_mag_platform_data *pdata_mag;
};

#endif	/* __KERNEL__ */
#endif	/* __LSM303D_H__ */
