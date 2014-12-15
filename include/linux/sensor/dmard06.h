/*
 * @file include/linux/dmard06.h
 * @brief DMARD06 g-sensor Linux device driver
 * @author Domintech Technology Co., Ltd (http://www.domintech.com.tw)
 * @version 1.2
 * @date 2011/11/14
 *
 * @section LICENSE
 *
 *  Copyright 2011 Domintech Technology Co., Ltd
 *
 * 	This software is licensed under the terms of the GNU General Public
 * 	License version 2, as published by the Free Software Foundation, and
 * 	may be copied, distributed, and modified under those terms.
 *
 * 	This program is distributed in the hope that it will be useful,
 * 	but WITHOUT ANY WARRANTY; without even the implied warranty of
 * 	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * 	GNU General Public License for more details.
 *
 *
 */
#ifndef DMARD05_H
#define DMARD05_H

#define DEVICE_I2C_NAME "dmard06"

#define DMT_DEBUG_DATA	0
//#define DMT_DEBUG_DATA 		0

//g-senor layout configuration, choose one of the following configuration
//#define CONFIG_GSEN_LAYOUT_PAT_1
//#define CONFIG_GSEN_LAYOUT_PAT_2
//#define CONFIG_GSEN_LAYOUT_PAT_3
//#define CONFIG_GSEN_LAYOUT_PAT_4
#define CONFIG_GSEN_LAYOUT_PAT_5
//#define CONFIG_GSEN_LAYOUT_PAT_6
//#define CONFIG_GSEN_LAYOUT_PAT_7
//#define CONFIG_GSEN_LAYOUT_PAT_8

#define CONFIG_GSEN_CALIBRATION_GRAVITY_ON_Z_NEGATIVE 1
#define CONFIG_GSEN_CALIBRATION_GRAVITY_ON_Z_POSITIVE 2
#define CONFIG_GSEN_CALIBRATION_GRAVITY_ON_Y_NEGATIVE 3
#define CONFIG_GSEN_CALIBRATION_GRAVITY_ON_Y_POSITIVE 4
#define CONFIG_GSEN_CALIBRATION_GRAVITY_ON_X_NEGATIVE 5
#define CONFIG_GSEN_CALIBRATION_GRAVITY_ON_X_POSITIVE 6

#define AVG_NUM 16
#define DEFAULT_SENSITIVITY 64

#define X_OUT 		0x41
#define SW_RESET 	0x53
#define WHO_AM_I 	0x0f
#define WHO_AM_I_VALUE 	0x06

#if DMT_DEBUG_DATA
#define IN_FUNC_MSG printk(KERN_INFO "@DMT@ In %s\n", __func__)
#define PRINT_X_Y_Z(x, y, z) printk(KERN_INFO "@DMT@ X/Y/Z axis: %04d , %04d , %04d\n", (x), (y), (z))
#define PRINT_OFFSET(x, y, z) printk(KERN_INFO "@offset@  X/Y/Z axis: %04d , %04d , %04d\n",offset.x,offset.y,offset.z);
#else
#define IN_FUNC_MSG
#define PRINT_X_Y_Z(x, y, z)
#define PRINT_OFFSET(x, y, z)
#endif

#define IOCTL_MAGIC  0x09
#define SENSOR_DATA_SIZE 3                           

#define SENSOR_RESET    		_IO(IOCTL_MAGIC, 0)
#define SENSOR_CALIBRATION   	_IOWR(IOCTL_MAGIC,  1, int[SENSOR_DATA_SIZE])
#define SENSOR_GET_OFFSET  		_IOR(IOCTL_MAGIC,  2, int[SENSOR_DATA_SIZE])
#define SENSOR_SET_OFFSET  		_IOWR(IOCTL_MAGIC,  3, int[SENSOR_DATA_SIZE])
#define SENSOR_READ_ACCEL_XYZ  	_IOR(IOCTL_MAGIC,  4, int[SENSOR_DATA_SIZE])

#define SENSOR_MAXNR 4


#define MMAIO				0xA1

/* IOCTLs for MMA7660 library */
#define MMA_IOCTL_INIT                  _IO(MMAIO, 0x01)
#define MMA_IOCTL_RESET      	          _IO(MMAIO, 0x04)
#define MMA_IOCTL_CLOSE		           _IO(MMAIO, 0x02)
#define MMA_IOCTL_START		             _IO(MMAIO, 0x03)
#define MMA_IOCTL_GETDATA               _IOR(MMAIO, 0x08, char[RBUFF_SIZE+1])

/* IOCTLs for APPs */
#define MMA_IOCTL_APP_SET_RATE		_IOW(MMAIO, 0x10, char)


#define DMARD06_RATE_SHIFT  3

#define DMARD06_REG_WHO_AM_I 0x0f
#define DMARD06_DEVID		0x06

/*status*/
#define DMARD06_OPEN		   1
#define DMARD06_CLOSE		   0

#define DMARD06_REG_NORMAL 0x44
#define DMARD06_REG_MODE 0x45
#define DMARD06_REG_FLITER 0x46
#define DMARD06_REG_INT 0x47
#define DMARD06_REG_NA 0x48
#define DMARD06_REG_EVENT 0x4a
#define DMARD06_REG_Threshold 0x4c
#define DMARD06_REG_Duration 0x4d

#define DMARD06_RATE_1P56 		3
#define DMARD06_RATE_6P25		2
#define DMARD06_RATE_12P5 		1
#define DMARD06_RATE_50		0

#define MMA8452_RATE_1P56  DMARD06_RATE_1P56
#define MMA8452_RATE_6P25  DMARD06_RATE_6P25
#define MMA8452_RATE_12P5  DMARD06_RATE_12P5
#define MMA8452_RATE_50      DMARD06_RATE_50



#define DMARD06_REG_INTSU   0x47

#define DMARD06_IIC_ADDR		0x38  
#define DMARD06_GRAVITY_STEP	32
#define DMARD06_PRECISION		7
#define DMARD06_BOUNDARY  (0x1 << (DMARD06_PRECISION - 1))

#define DMARD06_REG_X_OUT		0x41
#define DMARD06_REG_Y_OUT		0x42
#define DMARD06_REG_Z_OUT		0x43








#define RBUFF_SIZE 12

struct dmard06_data {
	char  status;
	char  curr_tate;
	struct input_dev *input_dev;
	struct i2c_client *client;
	struct work_struct work;
	struct delayed_work delaywork;	/*report second event*/
	unsigned int delay;
};

struct dmard06_axis {
	int x;
	int y;
	int z;
};


#endif

