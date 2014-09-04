/*
 *  MCube mc3230 acceleration sensor driver
 *
 *  Copyright (C) 2011 MCube Inc.,
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * *****************************************************************************/

#ifndef MC3230_H
#define MC3230_H

#include <linux/ioctl.h>

#define MC3230_REG_CHIP_ID 		0x18
#define MC3230_REG_X_OUT			0x0 //RO
#define MC3230_REG_Y_OUT			0x1 //RO
#define MC3230_REG_Z_OUT			0x2 //RO
#define MC3230_REG_STAT 			0x04
#define MC3230_REG_SLEEP_COUNTER 0x05
#define MC3230_REG_INTMOD			0x06
#define MC3230_REG_SYSMOD			0x07
#define MC3230_REG_RATE_SAMP 		0x08

#define MC32X0_XOUT_EX_L_REG				0x0d

#define MC3230_REG_RBM_DATA		0x0D
#define MC3230_REG_PRODUCT_CODE	0x3b





#define MCIO				0xA1

#define RBUFF_SIZE		12	/* Rx buffer size */
/* IOCTLs for MC3230 library */
#define MC_IOCTL_INIT                  _IO(MCIO, 0x01)
#define MC_IOCTL_RESET      	          _IO(MCIO, 0x04)
#define MC_IOCTL_CLOSE		           _IO(MCIO, 0x02)
#define MC_IOCTL_START		             _IO(MCIO, 0x03)
#define MC_IOCTL_GETDATA               _IOR(MCIO, 0x08, char[RBUFF_SIZE+1])

/* IOCTLs for APPs */
#define MC_IOCTL_APP_SET_RATE		_IOW(MCIO, 0x10, char)

#if 0
/*wake mode rate Samples/Second (0~2)*/
#define MC3230_RATE_128          0
#define MC3230_RATE_64          1
#define MC3230_RATE_32         2
#define MC3230_RATE_16         3
#define MC3230_RATE_8       	  4
#define MC3230_RATE_4         5
#define MC3230_RATE_2         6
#define MC3230_RATE_1         7
#endif

/*sniffr mode rate Samples/Second (3~4)*/
#define MC3230_SNIFFR_RATE_32		0
#define MC3230_SNIFFR_RATE_16		1
#define MC3230_SNIFFR_RATE_8		2
#define MC3230_SNIFFR_RATE_1		3
#define MC3230_SNIFFR_RATE_SHIFT	3

//#define ACTIVE_MASK				1
//#define FREAD_MASK				2


/*status*/
#define MC3230_OPEN           1
#define MC3230_CLOSE          0

#define MC3230_RANGE		1500000
#define MC3230_IIC_ADDR 	  0x4c 
#define MC3230_PRECISION       8 //8bit data
#define MC3230_BOUNDARY        (0x1 << (MC3230_PRECISION - 1))
#define MC3230_GRAVITY_STEP   MC3230_RANGE/MC3230_BOUNDARY //110 //2g full scale range


struct mc3230_axis {
	int x;
	int y;
	int z;
};

//#define  GSENSOR_DEV_PATH    "/dev/mma8452_daemon"



//add accel calibrate IO
typedef struct {
	unsigned short	x;		/**< X axis */
	unsigned short	y;		/**< Y axis */
	unsigned short	z;		/**< Z axis */
} GSENSOR_VECTOR3D;

typedef struct{
	int x;
	int y;
	int z;
}SENSOR_DATA;
//=========================================add by guanj============================
struct mc3230_platform_data {
	u16 	model;
	u16 	swap_xy;
	u16 	swap_xyz;
	signed char orientation[9];
	int 	(*get_pendown_state)(void);
	int 	(*init_platform_hw)(void);
	int 	(*mc3230_platform_sleep)(void);
	int 	(*mc3230_platform_wakeup)(void);
	void	(*exit_platform_hw)(void);
};

#define GSENSOR						   	0x85
//#define GSENSOR_IOCTL_INIT                  _IO(GSENSOR,  0x01)
#define GSENSOR_IOCTL_READ_CHIPINFO         _IOR(GSENSOR, 0x02, int)
#define GSENSOR_IOCTL_READ_SENSORDATA       _IOR(GSENSOR, 0x03, int)
#define GSENSOR_IOCTL_READ_OFFSET			_IOR(GSENSOR, 0x04, GSENSOR_VECTOR3D)
#define GSENSOR_IOCTL_READ_GAIN				_IOR(GSENSOR, 0x05, GSENSOR_VECTOR3D)
#define GSENSOR_IOCTL_READ_RAW_DATA			_IOR(GSENSOR, 0x06, int)
#define GSENSOR_IOCTL_SET_CALI				_IOW(GSENSOR, 0x06, SENSOR_DATA)
#define GSENSOR_IOCTL_GET_CALI				_IOW(GSENSOR, 0x07, SENSOR_DATA)
#define GSENSOR_IOCTL_CLR_CALI				_IO(GSENSOR, 0x08)
#define GSENSOR_MCUBE_IOCTL_READ_RBM_DATA		_IOR(GSENSOR, 0x09, SENSOR_DATA)
#define GSENSOR_MCUBE_IOCTL_SET_RBM_MODE		_IO(GSENSOR, 0x0a)
#define GSENSOR_MCUBE_IOCTL_CLEAR_RBM_MODE		_IO(GSENSOR, 0x0b)
#define GSENSOR_MCUBE_IOCTL_SET_CALI			_IOW(GSENSOR, 0x0c, SENSOR_DATA)
#define GSENSOR_MCUBE_IOCTL_REGISTER_MAP		_IO(GSENSOR, 0x0d)
#define GSENSOR_IOCTL_SET_CALI_MODE   			_IOW(GSENSOR, 0x0e,int)




/* IOCTLs for Msensor misc. device library */
#define MSENSOR						   0x83
#define MSENSOR_IOCTL_INIT					_IO(MSENSOR, 0x01)
#define MSENSOR_IOCTL_READ_CHIPINFO			_IOR(MSENSOR, 0x02, int)
#define MSENSOR_IOCTL_READ_SENSORDATA		_IOR(MSENSOR, 0x03, int)
#define MSENSOR_IOCTL_READ_POSTUREDATA		_IOR(MSENSOR, 0x04, int)
#define MSENSOR_IOCTL_READ_CALIDATA			_IOR(MSENSOR, 0x05, int)
#define MSENSOR_IOCTL_READ_CONTROL			_IOR(MSENSOR, 0x06, int)
#define MSENSOR_IOCTL_SET_CONTROL			_IOW(MSENSOR, 0x07, int)
#define MSENSOR_IOCTL_SET_MODE           	_IOW(MSENSOR, 0x08, int)
#define MSENSOR_IOCTL_SET_POSTURE        	_IOW(MSENSOR, 0x09, int)
#define MSENSOR_IOCTL_SET_CALIDATA     	  	_IOW(MSENSOR, 0x0a, int)
#define MSENSOR_IOCTL_SENSOR_ENABLE         _IOW(MSENSOR, 0x51, int)
#define MSENSOR_IOCTL_READ_FACTORY_SENSORDATA  _IOW(MSENSOR, 0x52, int)


#if 0
/* IOCTLs for AKM library */
#define ECS_IOCTL_WRITE                 _IOW(MSENSOR, 0x0b, char*)
#define ECS_IOCTL_READ                  _IOWR(MSENSOR, 0x0c, char*)
#define ECS_IOCTL_RESET      	        _IO(MSENSOR, 0x0d) /* NOT used in AK8975 */
#define ECS_IOCTL_SET_MODE              _IOW(MSENSOR, 0x0e, short)
#define ECS_IOCTL_GETDATA               _IOR(MSENSOR, 0x0f, char[SENSOR_DATA_SIZE])
#define ECS_IOCTL_SET_YPR               _IOW(MSENSOR, 0x10, short[12])
#define ECS_IOCTL_GET_OPEN_STATUS       _IOR(MSENSOR, 0x11, int)
#define ECS_IOCTL_GET_CLOSE_STATUS      _IOR(MSENSOR, 0x12, int)
#define ECS_IOCTL_GET_OSENSOR_STATUS	_IOR(MSENSOR, 0x13, int)
#define ECS_IOCTL_GET_DELAY             _IOR(MSENSOR, 0x14, short)
#define ECS_IOCTL_GET_PROJECT_NAME      _IOR(MSENSOR, 0x15, char[64])
#define ECS_IOCTL_GET_MATRIX            _IOR(MSENSOR, 0x16, short [4][3][3])
#define	ECS_IOCTL_GET_LAYOUT			_IOR(MSENSOR, 0x17, int[3])
#endif
#define ECS_IOCTL_GET_OUTBIT        	_IOR(MSENSOR, 0x23, char)
#define ECS_IOCTL_GET_ACCEL         	_IOR(MSENSOR, 0x24, short[3])
#define MMC31XX_IOC_RM					_IO(MSENSOR, 0x25)
#define MMC31XX_IOC_RRM					_IO(MSENSOR, 0x26)



/* IOCTLs for MMC31XX device */
#define MMC31XX_IOC_TM					_IO(MSENSOR, 0x18)
#define MMC31XX_IOC_SET					_IO(MSENSOR, 0x19)
#define MMC31XX_IOC_RESET				_IO(MSENSOR, 0x1a)
#define MMC31XX_IOC_READ				_IOR(MSENSOR, 0x1b, int[3])
#define MMC31XX_IOC_READXYZ				_IOR(MSENSOR, 0x1c, int[3])

#define ECOMPASS_IOC_GET_DELAY			_IOR(MSENSOR, 0x1d, int)
#define ECOMPASS_IOC_GET_MFLAG			_IOR(MSENSOR, 0x1e, short)
#define	ECOMPASS_IOC_GET_OFLAG			_IOR(MSENSOR, 0x1f, short)
#define ECOMPASS_IOC_GET_OPEN_STATUS	_IOR(MSENSOR, 0x20, int)
#define ECOMPASS_IOC_SET_YPR			_IOW(MSENSOR, 0x21, int[12])
#define ECOMPASS_IOC_GET_LAYOUT			_IOR(MSENSOR, 0X22, int)




#define ALSPS							0X84
#define ALSPS_SET_PS_MODE					_IOW(ALSPS, 0x01, int)
#define ALSPS_GET_PS_MODE					_IOR(ALSPS, 0x02, int)
#define ALSPS_GET_PS_DATA					_IOR(ALSPS, 0x03, int)
#define ALSPS_GET_PS_RAW_DATA				_IOR(ALSPS, 0x04, int)
#define ALSPS_SET_ALS_MODE					_IOW(ALSPS, 0x05, int)
#define ALSPS_GET_ALS_MODE					_IOR(ALSPS, 0x06, int)
#define ALSPS_GET_ALS_DATA					_IOR(ALSPS, 0x07, int)
#define ALSPS_GET_ALS_RAW_DATA           	_IOR(ALSPS, 0x08, int)

#define GYROSCOPE							0X86
#define GYROSCOPE_IOCTL_INIT					_IO(GYROSCOPE, 0x01)
#define GYROSCOPE_IOCTL_SMT_DATA			_IOR(GYROSCOPE, 0x02, int)
#define GYROSCOPE_IOCTL_READ_SENSORDATA		_IOR(GYROSCOPE, 0x03, int)
#define GYROSCOPE_IOCTL_SET_CALI			_IOW(GYROSCOPE, 0x04, SENSOR_DATA)
#define GYROSCOPE_IOCTL_GET_CALI			_IOW(GYROSCOPE, 0x05, SENSOR_DATA)
#define GYROSCOPE_IOCTL_CLR_CALI			_IO(GYROSCOPE, 0x06)

#define BROMETER							0X87
#define BAROMETER_IOCTL_INIT				_IO(BROMETER, 0x01)
#define BAROMETER_GET_PRESS_DATA			_IOR(BROMETER, 0x02, int)
#define BAROMETER_GET_TEMP_DATA			    _IOR(BROMETER, 0x03, int)
#define BAROMETER_IOCTL_READ_CHIPINFO		_IOR(BROMETER, 0x04, int)

extern long mc3230_ioctl( struct file *file, unsigned int cmd,unsigned long arg,struct i2c_client *client);
#endif

