/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for mma8452 compass chip.
 */
#ifndef MMA8452_H
#define MMA8452_H

#include <linux/ioctl.h>

/* Default register settings */
#define RBUFF_SIZE		12	/* Rx buffer size */

#define MMA8452_REG_STATUS   	    	0x0 //RO
#define MMA8452_REG_X_OUT_MSB       	0x1 //RO
#define MMA8452_REG_X_OUT_LSB       	0x2 //RO
#define MMA8452_REG_Y_OUT_MSB       	0x3 //RO
#define MMA8452_REG_Y_OUT_LSB       	0x4 //RO
#define MMA8452_REG_Z_OUT_MSB       	0x5 //RO
#define MMA8452_REG_Z_OUT_LSB       	0x6 //RO
#define MMA8452_REG_F_SETUP		       	0x9 //RW

#define MMA8452_REG_SYSMOD				0xB //RO
#define MMA8452_REG_INTSRC	    		0xC //RO
#define MMA8452_REG_WHO_AM_I      		0xD //RO
#define MMA8452_REG_XYZ_DATA_CFG		0xE //RW
#define MMA8452_REG_HP_FILTER_CUTOFF	0xF //RW
#define MMA8452_REG_PL_STATUS			0x10 //RO
#define MMA8452_REG_PL_CFG				0x11 //RW
#define MMA8452_REG_PL_COUNT			0x12 //RW
#define MMA8452_REG_PL_BF_ZCOMP			0x13 //RW
#define MMA8452_REG_P_L_THS_REG			0x14 //RW
#define MMA8452_REG_FF_MT_CFG			0x15 //RW
#define MMA8452_REG_FF_MT_SRC			0x16 //RO
#define MMA8452_REG_FF_MT_THS			0x17 //RW
#define MMA8452_REG_FF_MT_COUNT			0x18 //RW
#define MMA8452_REG_TRANSIENT_CFG		0x1D //RW
#define MMA8452_REG_TRANSIENT_SRC		0x1E //RO
#define MMA8452_REG_TRANSIENT_THS		0x1F //RW
#define MMA8452_REG_TRANSIENT_COUNT		0x20 //RW
#define MMA8452_REG_PULSE_CFG			0x21 //RW
#define MMA8452_REG_PULSE_SRC			0x22 //RO
#define MMA8452_REG_PULSE_THSX			0x23 //RW
#define MMA8452_REG_PULSE_THSY			0x24 //RW
#define MMA8452_REG_PULSE_THSZ			0x25 //RW
#define MMA8452_REG_PULSE_TMLT			0x26 //RW
#define MMA8452_REG_PULSE_LTCY			0x27 //RW
#define MMA8452_REG_PULSE_WIND			0x28 //RW
#define MMA8452_REG_ASLP_COUNT			0x29 //RW
#define MMA8452_REG_CTRL_REG1			0x2A //RW
#define MMA8452_REG_CTRL_REG2			0x2B //RW
#define MMA8452_REG_CTRL_REG3			0x2C //RW
#define MMA8452_REG_CTRL_REG4			0x2D //RW
#define MMA8452_REG_CTRL_REG5			0x2E //RW
#define MMA8452_REG_OFF_X				0x2F //RW
#define MMA8452_REG_OFF_Y				0x30 //RW
#define MMA8452_REG_OFF_Z				0x31 //RW

#define MMAIO				0xA1

/* IOCTLs for MMA8452 library */
#define MMA_IOCTL_INIT                  _IO(MMAIO, 0x01)
#define MMA_IOCTL_RESET      	          _IO(MMAIO, 0x04)
#define MMA_IOCTL_CLOSE		           _IO(MMAIO, 0x02)
#define MMA_IOCTL_START		             _IO(MMAIO, 0x03)
#define MMA_IOCTL_GETDATA               _IOR(MMAIO, 0x08, char[RBUFF_SIZE+1])

/* IOCTLs for APPs */
#define MMA_IOCTL_APP_SET_RATE		_IOW(MMAIO, 0x10, char)


/*rate*/
#define MMA8452_RATE_800          0
#define MMA8452_RATE_400          1
#define MMA8452_RATE_200          2
#define MMA8452_RATE_100          3
#define MMA8452_RATE_50        	  4
#define MMA8452_RATE_12P5         5
#define MMA8452_RATE_6P25         6
#define MMA8452_RATE_1P56         7
#define MMA8452_RATE_SHIFT		  3


#define MMA8452_ASLP_RATE_50          0
#define MMA8452_ASLP_RATE_12P5        1
#define MMA8452_ASLP_RATE_6P25        2
#define MMA8452_ASLP_RATE_1P56        3
#define MMA8452_ASLP_RATE_SHIFT		  6

/*Auto-adapt mma845x series*/
/*Modified by Yick @ROCKCHIP 
  xieyi@rockchips.com*/
/*
  Range: unit(ug 1g=1 000 000 ug)
  		 option(2g,4g,8g)
		 G would be defined on android HAL
  Precision: bit wide of valid data
  Boundary: Max positive count
  Gravity_step: gravity value indicated by per count
 */
#define FREAD_MASK				0 /* enabled(1<<1) only if reading MSB 8bits*/
#define MMA845X_RANGE			2000000
/* mma8451 */
#define MMA8451_PRECISION       14
#define MMA8451_BOUNDARY        (0x1 << (MMA8451_PRECISION - 1))
#define MMA8451_GRAVITY_STEP    MMA845X_RANGE / MMA8451_BOUNDARY

/* mma8452 */
#define MMA8452_PRECISION       12
#define MMA8452_BOUNDARY        (0x1 << (MMA8452_PRECISION - 1))
#define MMA8452_GRAVITY_STEP    MMA845X_RANGE / MMA8452_BOUNDARY

/* mma8453 */
#define MMA8453_PRECISION       10
#define MMA8453_BOUNDARY        (0x1 << (MMA8453_PRECISION - 1))
#define MMA8453_GRAVITY_STEP    MMA845X_RANGE / MMA8453_BOUNDARY

/*End of precision adaption*/

#define MMA8452_TOTAL_TIME      10

#define ACTIVE_MASK				1

/*status*/
#define MMA8452_SUSPEND           2
#define MMA8452_OPEN           1
#define MMA8452_CLOSE          0

#define MMA8452_REG_LEN         11

struct mma8452_axis {
	int x;
	int y;
	int z;
};

#define  GSENSOR_DEV_PATH    "/dev/mma8452_daemon"

#endif

