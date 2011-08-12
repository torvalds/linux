/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
  $
 */

#ifndef __MPU_H_
#define __MPU_H_

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#elif defined LINUX
#include <sys/ioctl.h>
#endif

#ifdef M_HW
#include "mpu6000.h"
#else
#include "mpu3050.h"
#endif

/* Number of axes on each sensor */
#define GYRO_NUM_AXES               (3)
#define ACCEL_NUM_AXES              (3)
#define COMPASS_NUM_AXES            (3)

#if defined __KERNEL__ || defined LINUX
#define MPU_IOCTL (0x81) /* Magic number for MPU Iocts */
/* IOCTL commands for /dev/mpu */
#define MPU_SET_MPU_CONFIG	_IOW(MPU_IOCTL, 0x00, struct mldl_cfg)
#define MPU_GET_MPU_CONFIG	_IOR(MPU_IOCTL, 0x00, struct mldl_cfg)

#define MPU_SET_PLATFORM_DATA	_IOW(MPU_IOCTL, 0x01, struct mldl_cfg)

#define MPU_READ		_IOR(MPU_IOCTL, 0x10, struct mpu_read_write)
#define MPU_WRITE		_IOW(MPU_IOCTL, 0x10, struct mpu_read_write)
#define MPU_READ_MEM		_IOR(MPU_IOCTL, 0x11, struct mpu_read_write)
#define MPU_WRITE_MEM		_IOW(MPU_IOCTL, 0x11, struct mpu_read_write)
#define MPU_READ_FIFO		_IOR(MPU_IOCTL, 0x12, struct mpu_read_write)
#define MPU_WRITE_FIFO		_IOW(MPU_IOCTL, 0x12, struct mpu_read_write)

#define MPU_READ_COMPASS	_IOR(MPU_IOCTL, 0x12, unsigned char)
#define MPU_READ_ACCEL		_IOR(MPU_IOCTL, 0x13, unsigned char)
#define MPU_READ_PRESSURE	_IOR(MPU_IOCTL, 0x14, unsigned char)

#define MPU_CONFIG_ACCEL	_IOW(MPU_IOCTL, 0x20, struct ext_slave_config)
#define MPU_CONFIG_COMPASS	_IOW(MPU_IOCTL, 0x21, struct ext_slave_config)
#define MPU_CONFIG_PRESSURE	_IOW(MPU_IOCTL, 0x22, struct ext_slave_config)

#define MPU_GET_CONFIG_ACCEL	_IOR(MPU_IOCTL, 0x20, struct ext_slave_config)
#define MPU_GET_CONFIG_COMPASS	_IOR(MPU_IOCTL, 0x21, struct ext_slave_config)
#define MPU_GET_CONFIG_PRESSURE	_IOR(MPU_IOCTL, 0x22, struct ext_slave_config)

#define MPU_SUSPEND		_IO(MPU_IOCTL, 0x30)
#define MPU_RESUME		_IO(MPU_IOCTL, 0x31)
/* Userspace PM Event response */
#define MPU_PM_EVENT_HANDLED	_IO(MPU_IOCTL, 0x32)

#endif
/* Structure for the following IOCTL's:
   MPU_READ
   MPU_WRITE
   MPU_READ_MEM
   MPU_WRITE_MEM
   MPU_READ_FIFO
   MPU_WRITE_FIFO
*/
struct mpu_read_write {
	/* Memory address or register address depending on ioctl */
	unsigned short address;
	unsigned short length;
	unsigned char *data;
};

enum mpuirq_data_type {
    MPUIRQ_DATA_TYPE_MPU_IRQ,
    MPUIRQ_DATA_TYPE_SLAVE_IRQ,
    MPUIRQ_DATA_TYPE_PM_EVENT,
    MPUIRQ_DATA_TYPE_NUM_TYPES,
};

/* User space PM event notification */
#define MPU_PM_EVENT_SUSPEND_PREPARE (3)
#define MPU_PM_EVENT_POST_SUSPEND    (4)

#define MAX_MPUIRQ_DATA_SIZE (32)

struct mpuirq_data {
	int interruptcount;
	unsigned long long irqtime;
	int data_type;
	int data_size;
	void *data;
};

enum ext_slave_config_key {
	MPU_SLAVE_CONFIG_ODR_SUSPEND,
	MPU_SLAVE_CONFIG_ODR_RESUME,
	MPU_SLAVE_CONFIG_FSR_SUSPEND,
	MPU_SLAVE_CONFIG_FSR_RESUME,
	MPU_SLAVE_CONFIG_MOT_THS,
	MPU_SLAVE_CONFIG_NMOT_THS,
	MPU_SLAVE_CONFIG_MOT_DUR,
	MPU_SLAVE_CONFIG_NMOT_DUR,
	MPU_SLAVE_CONFIG_IRQ_SUSPEND,
	MPU_SLAVE_CONFIG_IRQ_RESUME,
	MPU_SLAVE_WRITE_REGISTERS,
	MPU_SLAVE_READ_REGISTERS,
	MPU_SLAVE_CONFIG_NUM_CONFIG_KEYS,
};

/* For the MPU_SLAVE_CONFIG_IRQ_SUSPEND and MPU_SLAVE_CONFIG_IRQ_RESUME */
enum ext_slave_config_irq_type {
	MPU_SLAVE_IRQ_TYPE_NONE,
	MPU_SLAVE_IRQ_TYPE_MOTION,
	MPU_SLAVE_IRQ_TYPE_DATA_READY,
};

/* Structure for the following IOCTS's
 * MPU_CONFIG_ACCEL
 * MPU_CONFIG_COMPASS
 * MPU_CONFIG_PRESSURE
 * MPU_GET_CONFIG_ACCEL
 * MPU_GET_CONFIG_COMPASS
 * MPU_GET_CONFIG_PRESSURE
 */
struct ext_slave_config {
	int key;
	int len;
	int apply;
	void *data;
};

enum ext_slave_type {
	EXT_SLAVE_TYPE_GYROSCOPE,
	EXT_SLAVE_TYPE_ACCELEROMETER,
	EXT_SLAVE_TYPE_COMPASS,
	EXT_SLAVE_TYPE_PRESSURE,
	/*EXT_SLAVE_TYPE_TEMPERATURE */
};

enum ext_slave_id {
	ID_INVALID = 0,

	ACCEL_ID_LIS331,
	ACCEL_ID_LSM303,
	ACCEL_ID_LIS3DH,
	ACCEL_ID_KXSD9,
	ACCEL_ID_KXTF9,
	ACCEL_ID_BMA150,
	ACCEL_ID_BMA222,
	ACCEL_ID_ADI346,
	ACCEL_ID_MMA8450,
	ACCEL_ID_MMA845X,
	ACCEL_ID_MPU6000,

	COMPASS_ID_AKM,
	COMPASS_ID_AMI30X,
	COMPASS_ID_AMI306,
	COMPASS_ID_YAS529,
	COMPASS_ID_YAS530,
	COMPASS_ID_HMC5883,
	COMPASS_ID_LSM303,
	COMPASS_ID_MMC314X,
	COMPASS_ID_HSCDTD002B,
	COMPASS_ID_HSCDTD004A,

	PRESSURE_ID_BMA085,
};

enum ext_slave_endian {
	EXT_SLAVE_BIG_ENDIAN,
	EXT_SLAVE_LITTLE_ENDIAN,
	EXT_SLAVE_FS8_BIG_ENDIAN,
	EXT_SLAVE_FS16_BIG_ENDIAN,
};

enum ext_slave_bus {
	EXT_SLAVE_BUS_INVALID = -1,
	EXT_SLAVE_BUS_PRIMARY = 0,
	EXT_SLAVE_BUS_SECONDARY = 1
};


/**
 *  struct ext_slave_platform_data - Platform data for mpu3050 slave devices
 *
 *  @get_slave_descr: Function pointer to retrieve the struct ext_slave_descr
 *                    for this slave
 *  @irq: the irq number attached to the slave if any.
 *  @adapt_num: the I2C adapter number.
 *  @bus: the bus the slave is attached to: enum ext_slave_bus
 *  @address: the I2C slave address of the slave device.
 *  @orientation: the mounting matrix of the device relative to MPU.
 *  @irq_data: private data for the slave irq handler
 *  @private_data: additional data, user customizable.  Not touched by the MPU
 *                 driver.
 *
 * The orientation matricies are 3x3 rotation matricies
 * that are applied to the data to rotate from the mounting orientation to the
 * platform orientation.  The values must be one of 0, 1, or -1 and each row and
 * column should have exactly 1 non-zero value.
 */
struct ext_slave_platform_data {
	struct ext_slave_descr *(*get_slave_descr) (void);
	int irq;
	int adapt_num;
	int bus;
	unsigned char address;
	signed char orientation[9];
	void *irq_data;
	void *private_data;
};


struct tFixPntRange {
	long mantissa;
	long fraction;
};

/**
 *  struct ext_slave_descr - Description of the slave device for programming.
 *
 *  @suspend:	function pointer to put the device in suspended state
 *  @resume:	function pointer to put the device in running state
 *  @read:	function that reads the device data
 *  @init:	function used to preallocate memory used by the driver
 *  @exit:	function used to free memory allocated for the driver
 *  @config:	function used to configure the device
 *  @get_config:function used to get the device's configuration
 *
 *  @name:	text name of the device
 *  @type:	device type. enum ext_slave_type
 *  @id:	enum ext_slave_id
 *  @reg:	starting register address to retrieve data.
 *  @len:	length in bytes of the sensor data.  Should be 6.
 *  @endian:	byte order of the data. enum ext_slave_endian
 *  @range:	full scale range of the slave ouput: struct tFixPntRange
 *
 *  Defines the functions and information about the slave the mpu3050 needs to
 *  use the slave device.
 */
struct ext_slave_descr {
	int (*init) (void *mlsl_handle,
		     struct ext_slave_descr *slave,
		     struct ext_slave_platform_data *pdata);
	int (*exit) (void *mlsl_handle,
		     struct ext_slave_descr *slave,
		     struct ext_slave_platform_data *pdata);
	int (*suspend) (void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata);
	int (*resume) (void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata);
	int (*read) (void *mlsl_handle,
		     struct ext_slave_descr *slave,
		     struct ext_slave_platform_data *pdata,
		     unsigned char *data);
	int (*config) (void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata,
		       struct ext_slave_config *config);
	int (*get_config) (void *mlsl_handle,
			   struct ext_slave_descr *slave,
			   struct ext_slave_platform_data *pdata,
			   struct ext_slave_config *config);

	char *name;
	unsigned char type;
	unsigned char id;
	unsigned char reg;
	unsigned int len;
	unsigned char endian;
	struct tFixPntRange range;
};

/**
 * struct mpu3050_platform_data - Platform data for the mpu3050 driver
 * @int_config:		Bits [7:3] of the int config register.
 * @orientation:	Orientation matrix of the gyroscope
 * @level_shifter:	0: VLogic, 1: VDD
 * @accel:		Accel platform data
 * @compass:		Compass platform data
 * @pressure:		Pressure platform data
 *
 * Contains platform specific information on how to configure the MPU3050 to
 * work on this platform.  The orientation matricies are 3x3 rotation matricies
 * that are applied to the data to rotate from the mounting orientation to the
 * platform orientation.  The values must be one of 0, 1, or -1 and each row and
 * column should have exactly 1 non-zero value.
 */
struct mpu3050_platform_data {
	unsigned char int_config;
	signed char orientation[MPU_NUM_AXES * MPU_NUM_AXES];
	unsigned char level_shifter;
	struct ext_slave_platform_data accel;
	struct ext_slave_platform_data compass;
	struct ext_slave_platform_data pressure;
};


/*
    Accelerometer
*/
#define get_accel_slave_descr NULL

#ifdef CONFIG_MPU_SENSORS_ADXL346	/* ADI accelerometer */
struct ext_slave_descr *adxl346_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr adxl346_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_BMA150	/* Bosch accelerometer */
struct ext_slave_descr *bma150_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr bma150_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_BMA222	/* Bosch 222 accelerometer */
struct ext_slave_descr *bma222_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr bma222_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_KXSD9	/* Kionix accelerometer */
struct ext_slave_descr *kxsd9_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr kxsd9_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_KXTF9	/* Kionix accelerometer */
struct ext_slave_descr *kxtf9_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr kxtf9_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_LIS331DLH	/* ST accelerometer */
struct ext_slave_descr *lis331dlh_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr lis331dlh_get_slave_descr
#endif


#ifdef CONFIG_MPU_SENSORS_LIS3DH	/* ST accelerometer */
struct ext_slave_descr *lis3dh_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr lis3dh_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_LSM303DLHA	/* ST accelerometer */
struct ext_slave_descr *lsm303dlha_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr lsm303dlha_get_slave_descr
#endif

/* MPU6000 Accel */
#if defined(CONFIG_MPU_SENSORS_MPU6000) || \
    defined(CONFIG_MPU_SENSORS_MPU6000_MODULE)
struct ext_slave_descr *mantis_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr mantis_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_MMA8450	/* Freescale accelerometer */
struct ext_slave_descr *mma8450_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr mma8450_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_MMA845X	/* Freescale accelerometer */
struct ext_slave_descr *mma845x_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr mma845x_get_slave_descr
#endif


/*
    Compass
*/
#define get_compass_slave_descr NULL

#ifdef CONFIG_MPU_SENSORS_AK8975	/* AKM compass */
struct ext_slave_descr *ak8975_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr ak8975_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_AMI30X	/* AICHI Steel  AMI304/305 compass */
struct ext_slave_descr *ami30x_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr ami30x_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_AMI306	/* AICHI Steel AMI306 compass */
struct ext_slave_descr *ami306_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr ami306_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_HMC5883	/* Honeywell compass */
struct ext_slave_descr *hmc5883_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr hmc5883_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_MMC314X	/* MEMSIC compass */
struct ext_slave_descr *mmc314x_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr mmc314x_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_LSM303DLHM	/* ST compass */
struct ext_slave_descr *lsm303dlhm_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr lsm303dlhm_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_YAS529	/* Yamaha compass */
struct ext_slave_descr *yas529_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr yas529_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_YAS530	/* Yamaha compass */
struct ext_slave_descr *yas530_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr yas530_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_HSCDTD002B	/* Alps HSCDTD002B compass */
struct ext_slave_descr *hscdtd002b_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr hscdtd002b_get_slave_descr
#endif

#ifdef CONFIG_MPU_SENSORS_HSCDTD004A	/* Alps HSCDTD004A compass */
struct ext_slave_descr *hscdtd004a_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr hscdtd004a_get_slave_descr
#endif
/*
    Pressure
*/
#define get_pressure_slave_descr NULL

#ifdef CONFIG_MPU_SENSORS_BMA085	/* BMA pressure */
struct ext_slave_descr *bma085_get_slave_descr(void);
#undef get_pressure_slave_descr
#define get_pressure_slave_descr bma085_get_slave_descr
#endif

#endif				/* __MPU_H_ */
