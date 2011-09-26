/* include/linux/kxtf9.h - KXTF9 accelerometer driver
 *
 * Copyright (C) 2010 Kionix, Inc.
 * Written by Kuching Tan <kuchingtan@kionix.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __KXTF9_H__
#define __KXTF9_H__

#define KXTF9_I2C_ADDR	0x0F
/* CTRL_REG1 BITS */
#define RES_12BIT		0x40
#define KXTF9_G_2G 		0x00
#define KXTF9_G_4G 		0x08
#define KXTF9_G_8G 		0x10
#define SHIFT_ADJ_2G	4
#define SHIFT_ADJ_4G	3
#define SHIFT_ADJ_8G	2
#define TPE				0x01	/* tilt position function enable bit */
#define WUFE			0x02	/* wake-up function enable bit */
#define TDTE			0x04	/* tap/double-tap function enable bit */
/* CTRL_REG3 BITS */
#define SRST			0x80	/* software reset */
#define DCST			0x10	/* communication-test function */
#define OTP1_6			0x00	/* tilt ODR masks */
#define OTP6_3			0x20
#define OTP12_5			0x40
#define OTP50			0x60
#define OWUF25			0x00	/* wuf ODR masks */
#define OWUF50			0x01
#define OWUF100			0x02
#define OWUF200			0x03
#define OTDT50			0x00	/* tdt ODR masks */
#define OTDT100			0x04
#define OTDT200			0x08
#define OTDT400			0x0C
/* INT_CTRL_REG1 BITS */
#define KXTF9_IEN		0x20	/* interrupt enable */
#define KXTF9_IEA		0x10	/* interrupt polarity */
#define KXTF9_IEL		0x08	/* interrupt response */
#define IEU				0x04	/* alternate unlatched response */
/* DATA_CTRL_REG BITS */
#define ODR800F			0x06	/* lpf output ODR masks */
#define ODR400F			0x05
#define ODR200F			0x04
#define ODR100F			0x03
#define ODR50F			0x02
#define ODR25F			0x01

/* Device Meta Data */
#define DESC_DEV		"KXTF9 3-axis Accelerometer"	// Device Description
#define VERSION_DEV		"1.1.8"
#define VER_MAJOR_DEV	1
#define	VER_MINOR_DEV	1
#define VER_MAINT_DEV	8
#define	MAX_G_DEV		(8.0f)		// Maximum G Level
#define	MAX_SENS_DEV	(1024.0f)	// Maximum Sensitivity
#define PWR_DEV			(0.57f)		// Typical Current

/* Input Device Name */
#define INPUT_NAME_ACC	"kxtf9_accel"

/* Device name for kxtf9 misc. device */
#define NAME_DEV	"kxtf9"
#define DIR_DEV		"/dev/kxtf9"

/* IOCTLs for kxtf9 misc. device library */
#define KXTF9IO									0x94
#define KXTF9_IOCTL_GET_COUNT			_IOR(KXTF9IO, 0x01, int)
#define KXTF9_IOCTL_GET_MG				_IOR(KXTF9IO, 0x02, int)
#define KXTF9_IOCTL_ENABLE_OUTPUT		 _IO(KXTF9IO, 0x03)
#define KXTF9_IOCTL_DISABLE_OUTPUT		 _IO(KXTF9IO, 0x04)
#define KXTF9_IOCTL_GET_ENABLE			_IOR(KXTF9IO, 0x05, int)
#define KXTF9_IOCTL_RESET				 _IO(KXTF9IO, 0x06)
#define KXTF9_IOCTL_UPDATE_ODR			_IOW(KXTF9IO, 0x07, int)
#define KXTF9_IOCTL_ENABLE_DCST			 _IO(KXTF9IO, 0x08)
#define KXTF9_IOCTL_DISABLE_DCST		 _IO(KXTF9IO, 0x09)
#define KXTF9_IOCTL_GET_DCST_RESP		_IOR(KXTF9IO, 0x0A, int)


#ifdef __KERNEL__
struct kxtf9_platform_data {
	int poll_interval;
	int min_interval;

	u8 g_range;
	u8 shift_adj;
	u8 mul_fac;

	u8 axis_map_x;
	u8 axis_map_y;
	u8 axis_map_z;

	u8 negate_x;
	u8 negate_y;
	u8 negate_z;

	u8 data_odr_init;
	u8 ctrl_reg1_init;
	u8 int_ctrl_init;
	u8 tilt_timer_init;
	u8 engine_odr_init;
	u8 wuf_timer_init;
	u8 wuf_thresh_init;
	u8 tdt_timer_init;
	u8 tdt_h_thresh_init;
	u8 tdt_l_thresh_init;
	u8 tdt_tap_timer_init;
	u8 tdt_total_timer_init;
	u8 tdt_latency_timer_init;
	u8 tdt_window_timer_init;

	int (*init)(void);
	void (*exit)(void);
	int (*power_on)(void);
	int (*power_off)(void);

	int gpio;
};
#endif /* __KERNEL__ */

#endif  /* __KXTF9_H__ */

