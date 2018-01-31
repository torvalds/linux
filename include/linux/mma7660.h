/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Definitions for mma7660 compass chip.
 */
#ifndef MMA7660_H
#define MMA7660_H

#include <linux/ioctl.h>




/* Default register settings */
#define RBUFF_SIZE		12	/* Rx buffer size */


#define MMA7660_REG_X_OUT       0x0
#define MMA7660_REG_Y_OUT       0x1
#define MMA7660_REG_Z_OUT       0x2
#define MMA7660_REG_TILT        0x3
#define MMA7660_REG_SRST        0x4
#define MMA7660_REG_SPCNT       0x5
#define MMA7660_REG_INTSU       0x6
#define MMA7660_REG_MODE        0x7
#define MMA7660_REG_SR          0x8
#define MMA7660_REG_PDET        0x9
#define MMA7660_REG_PD          0xa


#define MMAIO				0xA1

/* IOCTLs for MMA7660 library */
#define ECS_IOCTL_INIT                  _IO(MMAIO, 0x01)
#define ECS_IOCTL_RESET      	          _IO(MMAIO, 0x04)
#define ECS_IOCTL_CLOSE		           _IO(MMAIO, 0x02)
#define ECS_IOCTL_START		             _IO(MMAIO, 0x03)
#define ECS_IOCTL_GETDATA               _IOR(MMAIO, 0x08, char[RBUFF_SIZE+1])

/* IOCTLs for APPs */
#define ECS_IOCTL_APP_SET_RATE		_IOW(MMAIO, 0x10, char)


/*rate*/
#define MMA7660_RATE_1          1
#define MMA7660_RATE_2          2
#define MMA7660_RATE_4          4
#define MMA7660_RATE_8          8
#define MMA7660_RATE_16         16
#define MMA7660_RATE_32         32
#define MMA7660_RATE_64         64
#define MMA7660_RATE_120        128

/*status*/
#define MMA7660_OPEN           1
#define MMA7660_CLOSE          0



#define MMA7660_IIC_ADDR 	    0x98  
#define MMA7660_REG_LEN         11
#define MMA7660_RANGE						2000000
#define MMA7660_PRECISION       6
#define MMA7660_BOUNDARY        (0x1 << (MMA7660_PRECISION - 1))
#define MMA7660_GRAVITY_STEP    MMA7660_RANGE/MMA7660_BOUNDARY
#define MMA7660_TOTAL_TIME      10



struct mma7660_platform_data {
	int reset;
	int clk_on;
	int intr;
};

struct mma7660_data {
    char  status;
    char  curr_tate;
	struct input_dev *input_dev;
	struct i2c_client *client;
	struct work_struct work;
	struct delayed_work delaywork;	/*report second event*/
};

struct mma7660_axis {
	int x;
	int y;
	int z;
};

#define  GSENSOR_DEV_PATH    "/dev/mma7660_daemon"


#endif

