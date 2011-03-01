/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.
 $
 */

#ifndef __MPU_H_
#define __MPU_H_

#ifdef __KERNEL__
#include <linux/types.h>
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

/* IOCTL commands for /dev/mpu */
#define MPU_SET_MPU_CONFIG          (0x00)
#define MPU_SET_INT_CONFIG          (0x01)
#define MPU_SET_EXT_SYNC            (0x02)
#define MPU_SET_FULL_SCALE          (0x03)
#define MPU_SET_LPF                 (0x04)
#define MPU_SET_CLK_SRC             (0x05)
#define MPU_SET_DIVIDER             (0x06)
#define MPU_SET_LEVEL_SHIFTER       (0x07)
#define MPU_SET_DMP_ENABLE          (0x08)
#define MPU_SET_FIFO_ENABLE         (0x09)
#define MPU_SET_DMP_CFG1            (0x0a)
#define MPU_SET_DMP_CFG2            (0x0b)
#define MPU_SET_OFFSET_TC           (0x0c)
#define MPU_SET_RAM                 (0x0d)

#define MPU_SET_PLATFORM_DATA       (0x0e)

#define MPU_GET_MPU_CONFIG          (0x80)
#define MPU_GET_INT_CONFIG          (0x81)
#define MPU_GET_EXT_SYNC            (0x82)
#define MPU_GET_FULL_SCALE          (0x83)
#define MPU_GET_LPF                 (0x84)
#define MPU_GET_CLK_SRC             (0x85)
#define MPU_GET_DIVIDER             (0x86)
#define MPU_GET_LEVEL_SHIFTER       (0x87)
#define MPU_GET_DMP_ENABLE          (0x88)
#define MPU_GET_FIFO_ENABLE         (0x89)
#define MPU_GET_DMP_CFG1            (0x8a)
#define MPU_GET_DMP_CFG2            (0x8b)
#define MPU_GET_OFFSET_TC           (0x8c)
#define MPU_GET_RAM                 (0x8d)

#define MPU_READ_REGISTER           (0x40)
#define MPU_WRITE_REGISTER          (0x41)
#define MPU_READ_MEMORY             (0x42)
#define MPU_WRITE_MEMORY            (0x43)

#define MPU_SUSPEND                 (0x44)
#define MPU_RESUME                  (0x45)
#define MPU_READ_COMPASS            (0x46)
#define MPU_READ_ACCEL              (0x47)
#define MPU_READ_PRESSURE           (0x48)

#define MPU_CONFIG_ACCEL            (0x20)
#define MPU_CONFIG_COMPASS          (0x21)
#define MPU_CONFIG_PRESSURE         (0x22)

/* Structure for the following IOCTL's:
   MPU_SET_RAM
   MPU_GET_RAM
   MPU_READ_REGISTER
   MPU_WRITE_REGISTER
   MPU_READ_MEMORY
   MPU_WRITE_MEMORY
*/
struct mpu_read_write {
	unsigned short address;
	unsigned short length;
	unsigned char *data;
};

struct irq_data {
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
    MPU_SLAVE_CONFIG_NUM_CONFIG_KEYS,
};
/* Structure for the following IOCTS's
 * MPU_CONFIG_ACCEL
 * MPU_CONFIG_COMPASS
 * MPU_CONFIG_PRESSURE
 */
struct ext_slave_config {
	int key;
	int len;
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
	ACCEL_ID_KXSD9,
	ACCEL_ID_KXTF9,
	ACCEL_ID_BMA150,
	ACCEL_ID_BMA222,
	ACCEL_ID_ADI346,
	ACCEL_ID_MMA8450,
	ACCEL_ID_MMA8451,
	ACCEL_ID_MPU6000,

	COMPASS_ID_AKM,
	COMPASS_ID_AMI30X,
	COMPASS_ID_YAS529,
	COMPASS_ID_HMC5883,
	COMPASS_ID_LSM303,
	COMPASS_ID_MMC314X,
	COMPASS_ID_HSCDTD00XX,

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

#ifdef CONFIG_SENSORS_ADXL346	/* ADI accelerometer */
struct ext_slave_descr *adxl346_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr adxl346_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_BMA150	/* Bosch accelerometer */
struct ext_slave_descr *bma150_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr bma150_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_BMA222	/* Bosch 222 accelerometer */
struct ext_slave_descr *bma222_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr bma222_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_KXSD9	/* Kionix accelerometer */
struct ext_slave_descr *kxsd9_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr kxsd9_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_KXTF9	/* Kionix accelerometer */
struct ext_slave_descr *kxtf9_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr kxtf9_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_LIS331DLH	/* ST accelerometer */
struct ext_slave_descr *lis331dlh_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr lis331dlh_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_LSM303DLHA	/* ST accelerometer */
struct ext_slave_descr *lsm303dlha_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr lsm303dlha_get_slave_descr
#endif

/* MPU6000 Accel */
#if defined(CONFIG_SENSORS_MPU6000) || defined(CONFIG_SENSORS_MPU6000_MODULE)
struct ext_slave_descr *mantis_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr mantis_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_MMA8450	/* Freescale accelerometer */
struct ext_slave_descr *mma8450_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr mma8450_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_MMA8451	/* Freescale accelerometer */
struct ext_slave_descr *mma8451_get_slave_descr(void);
#undef get_accel_slave_descr
#define get_accel_slave_descr mma8451_get_slave_descr
#endif


/*
    Compass
*/
#define get_compass_slave_descr NULL

#ifdef CONFIG_SENSORS_AK8975	/* AKM compass */
struct ext_slave_descr *ak8975_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr ak8975_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_AMI30X	/* AICHI Steel compass */
struct ext_slave_descr *ami30x_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr ami30x_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_HMC5883	/* Honeywell compass */
struct ext_slave_descr *hmc5883_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr hmc5883_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_MMC314X	/* MEMSIC compass */
struct ext_slave_descr *mmc314x_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr mmc314x_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_LSM303DLHM	/* ST compass */
struct ext_slave_descr *lsm303dlhm_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr lsm303dlhm_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_YAS529	/* Yamaha compass */
struct ext_slave_descr *yas529_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr yas529_get_slave_descr
#endif

#ifdef CONFIG_SENSORS_HSCDTD00XX	/* Alps compass */
struct ext_slave_descr *hscdtd00xx_get_slave_descr(void);
#undef get_compass_slave_descr
#define get_compass_slave_descr hscdtd00xx_get_slave_descr
#endif

/*
    Pressure
*/
#define get_pressure_slave_descr NULL

#ifdef CONFIG_SENSORS_BMA085	/* BMA pressure */
struct ext_slave_descr *bma085_get_slave_descr(void);
#undef get_pressure_slave_descr
#define get_pressure_slave_descr bma085_get_slave_descr
#endif

#endif				/* __MPU_H_ */
