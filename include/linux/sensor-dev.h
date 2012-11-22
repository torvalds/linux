/* include/linux/sensor-dev.h - sensor header file
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: luowei <lw@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/miscdevice.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif


#define SENSOR_ON		1
#define SENSOR_OFF		0
#define SENSOR_UNKNOW_DATA	-1

enum sensor_type {
	SENSOR_TYPE_NULL,
	SENSOR_TYPE_ACCEL,
	SENSOR_TYPE_COMPASS,	
	SENSOR_TYPE_GYROSCOPE,	
	SENSOR_TYPE_LIGHT,	
	SENSOR_TYPE_PROXIMITY,
	SENSOR_TYPE_TEMPERATURE,	
	//SENSOR_TYPE_PRESSURE,
	SENSOR_NUM_TYPES
};

enum sensor_id {
	ID_INVALID = 0,
		
	ACCEL_ID_ALL,
	ACCEL_ID_LIS331,
	ACCEL_ID_LSM303DLX,
	ACCEL_ID_LIS3DH,
	ACCEL_ID_KXSD9,
	ACCEL_ID_KXTF9,
	ACCEL_ID_KXTIK,
	ACCEL_ID_BMA150,
	ACCEL_ID_BMA222,
	ACCEL_ID_BMA250,
	ACCEL_ID_ADXL34X,
	ACCEL_ID_MMA8450,
	ACCEL_ID_MMA845X,
	ACCEL_ID_MMA7660,
	ACCEL_ID_MPU6050,
	ACCEL_ID_MXC6225,

	COMPASS_ID_ALL,
	COMPASS_ID_AK8975,
	COMPASS_ID_AK8972,
	COMPASS_ID_AMI30X,
	COMPASS_ID_AMI306,
	COMPASS_ID_YAS529,
	COMPASS_ID_YAS530,
	COMPASS_ID_HMC5883,
	COMPASS_ID_LSM303DLH,
	COMPASS_ID_LSM303DLM,
	COMPASS_ID_MMC314X,
	COMPASS_ID_HSCDTD002B,
	COMPASS_ID_HSCDTD004A,

	GYRO_ID_ALL,
	GYRO_ID_L3G4200D,
	GYRO_ID_K3G,

	LIGHT_ID_ALL,
	LIGHT_ID_CM3217,
	LIGHT_ID_AL3006,
	LIGHT_ID_STK3171,
	LIGHT_ID_ISL29023,
	LIGHT_ID_AP321XX,
	
	PROXIMITY_ID_ALL,
	PROXIMITY_ID_AL3006,
	PROXIMITY_ID_STK3171,
	PROXIMITY_ID_AP321XX,
	TEMPERATURE_ID_ALL,

	PRESSURE_ID_ALL,
	PRESSURE_ID_BMA085,
	SENSOR_NUM_ID,
};


struct sensor_axis {
	int x;
	int y;
	int z;
};

struct sensor_operate {
	char *name;
	int type;
	int	id_i2c;
	int	range[2];
	int 	brightness[2];//backlight min_brightness max_brightness 
	int read_reg;
	int read_len;
	int id_reg;
	int id_data;
	int precision;
	int ctrl_reg;
	int ctrl_data;
	int int_ctrl_reg;
	int	int_status_reg;
	int trig;	//intterupt trigger
	int (*active)(struct i2c_client *client, int enable, int rate);	
	int (*init)(struct i2c_client *client);	
	int (*report)(struct i2c_client *client);
	int (*suspend)(struct i2c_client *client);
	int (*resume)(struct i2c_client *client);
	struct miscdevice *misc_dev;

};


/* Platform data for the sensor */
struct sensor_private_data {
	int type;
	struct i2c_client *client;	
	struct input_dev *input_dev;
	struct work_struct work;
	struct delayed_work delaywork;	/*report second event*/
	struct sensor_axis axis;
	char sensor_data[40];		//max support40 bytes data
	atomic_t data_ready;
	wait_queue_head_t data_ready_wq;		
	struct mutex data_mutex;
	struct mutex operation_mutex;	
	struct mutex sensor_mutex;
	struct mutex i2c_mutex;
	int status_cur;
	int start_count;
	int devid;
	struct i2c_device_id *i2c_id;
	struct sensor_platform_data *pdata;
	struct sensor_operate *ops; 
	struct file_operations fops;
	struct miscdevice miscdev;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct 	early_suspend early_suspend;
#endif
};


extern int sensor_register_slave(int type,struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void));


extern int sensor_unregister_slave(int type,struct i2c_client *client,
			struct sensor_platform_data *slave_pdata,
			struct sensor_operate *(*get_sensor_ops)(void));


#define GSENSOR_IO				0xA1
#define GBUFF_SIZE				12	/* Rx buffer size */

/* IOCTLs for MMA8452 library */
#define GSENSOR_IOCTL_INIT                  _IO(GSENSOR_IO, 0x01)
#define GSENSOR_IOCTL_RESET      	        _IO(GSENSOR_IO, 0x04)
#define GSENSOR_IOCTL_CLOSE		           	_IO(GSENSOR_IO, 0x02)
#define GSENSOR_IOCTL_START		            _IO(GSENSOR_IO, 0x03)
#define GSENSOR_IOCTL_GETDATA               _IOR(GSENSOR_IO, 0x08, char[GBUFF_SIZE+1])
/* IOCTLs for APPs */
#define GSENSOR_IOCTL_APP_SET_RATE			_IOW(GSENSOR_IO, 0x10, char)


#define LIGHTSENSOR_IOCTL_MAGIC 'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED		_IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *) 
#define LIGHTSENSOR_IOCTL_ENABLE			_IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *) 
#define LIGHTSENSOR_IOCTL_DISABLE			_IOW(LIGHTSENSOR_IOCTL_MAGIC, 3, int *)

#define PSENSOR_IOCTL_MAGIC 'c'
#define PSENSOR_IOCTL_GET_ENABLED 			_IOR(PSENSOR_IOCTL_MAGIC, 1, int *)
#define PSENSOR_IOCTL_ENABLE 				_IOW(PSENSOR_IOCTL_MAGIC, 2, int *)
#define PSENSOR_IOCTL_DISABLE       		_IOW(PSENSOR_IOCTL_MAGIC, 3, int *)




extern int sensor_rx_data(struct i2c_client *client, char *rxData, int length);
extern int sensor_tx_data(struct i2c_client *client, char *txData, int length);
extern int sensor_write_reg(struct i2c_client *client, int addr, int value);
extern int sensor_read_reg(struct i2c_client *client, int addr);
extern int sensor_tx_data_normal(struct i2c_client *client, char *buf, int num);
extern int sensor_rx_data_normal(struct i2c_client *client, char *buf, int num);
extern int sensor_write_reg_normal(struct i2c_client *client, char value);
extern int sensor_read_reg_normal(struct i2c_client *client);

