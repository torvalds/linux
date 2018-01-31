/* SPDX-License-Identifier: GPL-2.0 */
#define KXTIK_DEVID		0x05	//chip id
#define KXTIK_RANGE	2000000

#define KXTIK_XOUT_HPF_L                (0x00)	/* 0000 0000 */
#define KXTIK_XOUT_HPF_H                (0x01)	/* 0000 0001 */
#define KXTIK_YOUT_HPF_L                (0x02)	/* 0000 0010 */
#define KXTIK_YOUT_HPF_H                (0x03)	/* 0000 0011 */
#define KXTIK_ZOUT_HPF_L                (0x04)	/* 0001 0100 */
#define KXTIK_ZOUT_HPF_H                (0x05)	/* 0001 0101 */
#define KXTIK_XOUT_L                    (0x06)	/* 0000 0110 */
#define KXTIK_XOUT_H                    (0x07)	/* 0000 0111 */
#define KXTIK_YOUT_L                    (0x08)	/* 0000 1000 */
#define KXTIK_YOUT_H                    (0x09)	/* 0000 1001 */
#define KXTIK_ZOUT_L                    (0x0A)	/* 0001 1010 */
#define KXTIK_ZOUT_H                    (0x0B)	/* 0001 1011 */
#define KXTIK_ST_RESP                   (0x0C)	/* 0000 1100 */
#define KXTIK_WHO_AM_I                  (0x0F)	/* 0000 1111 */
#define KXTIK_TILT_POS_CUR              (0x10)	/* 0001 0000 */
#define KXTIK_TILT_POS_PRE              (0x11)	/* 0001 0001 */
#define KXTIK_INT_SRC_REG1              (0x15)	/* 0001 0101 */
#define KXTIK_INT_SRC_REG2              (0x16)	/* 0001 0110 */
#define KXTIK_STATUS_REG                (0x18)	/* 0001 1000 */
#define KXTIK_INT_REL                   (0x1A)	/* 0001 1010 */
#define KXTIK_CTRL_REG1                 (0x1B)	/* 0001 1011 */
#define KXTIK_CTRL_REG2                 (0x1C)	/* 0001 1100 */
#define KXTIK_CTRL_REG3                 (0x1D)	/* 0001 1101 */
#define KXTIK_INT_CTRL_REG1             (0x1E)	/* 0001 1110 */
#define KXTIK_INT_CTRL_REG2             (0x1F)	/* 0001 1111 */
#define KXTIK_INT_CTRL_REG3             (0x20)	/* 0010 0000 */
#define KXTIK_DATA_CTRL_REG             (0x21)	/* 0010 0001 */
#define KXTIK_TILT_TIMER                (0x28)	/* 0010 1000 */
#define KXTIK_WUF_TIMER                 (0x29)	/* 0010 1001 */
#define KXTIK_TDT_TIMER                 (0x2B)	/* 0010 1011 */
#define KXTIK_TDT_H_THRESH              (0x2C)	/* 0010 1100 */
#define KXTIK_TDT_L_THRESH              (0x2D)	/* 0010 1101 */
#define KXTIK_TDT_TAP_TIMER             (0x2E)	/* 0010 1110 */
#define KXTIK_TDT_TOTAL_TIMER           (0x2F)	/* 0010 1111 */
#define KXTIK_TDT_LATENCY_TIMER         (0x30)	/* 0011 0000 */
#define KXTIK_TDT_WINDOW_TIMER          (0x31)	/* 0011 0001 */
#define KXTIK_WUF_THRESH                (0x5A)	/* 0101 1010 */
#define KXTIK_TILT_ANGLE                (0x5C)	/* 0101 1100 */
#define KXTIK_HYST_SET                  (0x5F)	/* 0101 1111 */

/* CONTROL REGISTER 1 BITS */
#define KXTIK_DISABLE			0x7F
#define KXTIK_ENABLE			(1 << 7)
/* INPUT_ABS CONSTANTS */
#define FUZZ			3
#define FLAT			3
/* RESUME STATE INDICES */
#define RES_DATA_CTRL		0
#define RES_CTRL_REG1		1
#define RES_INT_CTRL1		2
#define RESUME_ENTRIES		3

/* CTRL_REG1: set resolution, g-range, data ready enable */
/* Output resolution: 8-bit valid or 12-bit valid */
#define KXTIK_RES_8BIT		0
#define KXTIK_RES_12BIT		(1 << 6)
/* Output g-range: +/-2g, 4g, or 8g */
#define KXTIK_G_2G		0
#define KXTIK_G_4G		(1 << 3)
#define KXTIK_G_8G		(1 << 4)

/* DATA_CTRL_REG: controls the output data rate of the part */
#define KXTIK_ODR12_5F		0
#define KXTIK_ODR25F			1
#define KXTIK_ODR50F			2
#define KXTIK_ODR100F			3
#define KXTIK_ODR200F			4
#define KXTIK_ODR400F			5
#define KXTIK_ODR800F			6

/* kxtik */
#define KXTIK_PRECISION       12
#define KXTIK_BOUNDARY        (0x1 << (KXTIK_PRECISION - 1))
#define KXTIK_GRAVITY_STEP    KXTIK_RANGE / KXTIK_BOUNDARY
