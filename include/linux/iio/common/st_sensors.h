/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics sensors library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 */

#ifndef ST_SENSORS_H
#define ST_SENSORS_H

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/irqreturn.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/bitops.h>
#include <linux/regulator/consumer.h>
#include <linux/regmap.h>

#include <linux/platform_data/st_sensors_pdata.h>

#define LSM9DS0_IMU_DEV_NAME		"lsm9ds0"

/*
 * Buffer size max case: 2bytes per channel, 3 channels in total +
 *			 8bytes timestamp channel (s64)
 */
#define ST_SENSORS_MAX_BUFFER_SIZE		(ALIGN(2 * 3, sizeof(s64)) + \
						 sizeof(s64))

#define ST_SENSORS_ODR_LIST_MAX			10
#define ST_SENSORS_FULLSCALE_AVL_MAX		10

#define ST_SENSORS_NUMBER_ALL_CHANNELS		4
#define ST_SENSORS_ENABLE_ALL_AXIS		0x07
#define ST_SENSORS_SCAN_X			0
#define ST_SENSORS_SCAN_Y			1
#define ST_SENSORS_SCAN_Z			2
#define ST_SENSORS_DEFAULT_POWER_ON_VALUE	0x01
#define ST_SENSORS_DEFAULT_POWER_OFF_VALUE	0x00
#define ST_SENSORS_DEFAULT_WAI_ADDRESS		0x0f
#define ST_SENSORS_DEFAULT_AXIS_ADDR		0x20
#define ST_SENSORS_DEFAULT_AXIS_MASK		0x07
#define ST_SENSORS_DEFAULT_AXIS_N_BIT		3
#define ST_SENSORS_DEFAULT_STAT_ADDR		0x27

#define ST_SENSORS_MAX_NAME			17
#define ST_SENSORS_MAX_4WAI			8

#define ST_SENSORS_LSM_CHANNELS_EXT(device_type, mask, index, mod, \
				    ch2, s, endian, rbits, sbits, addr, ext) \
{ \
	.type = device_type, \
	.modified = mod, \
	.info_mask_separate = mask, \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.scan_index = index, \
	.channel2 = ch2, \
	.address = addr, \
	.scan_type = { \
		.sign = s, \
		.realbits = rbits, \
		.shift = sbits - rbits, \
		.storagebits = sbits, \
		.endianness = endian, \
	}, \
	.ext_info = ext, \
}

#define ST_SENSORS_LSM_CHANNELS(device_type, mask, index, mod, \
				ch2, s, endian, rbits, sbits, addr)	\
	ST_SENSORS_LSM_CHANNELS_EXT(device_type, mask, index, mod,	\
				    ch2, s, endian, rbits, sbits, addr, NULL)

#define ST_SENSORS_DEV_ATTR_SAMP_FREQ_AVAIL() \
		IIO_DEV_ATTR_SAMP_FREQ_AVAIL( \
			st_sensors_sysfs_sampling_frequency_avail)

#define ST_SENSORS_DEV_ATTR_SCALE_AVAIL(name) \
		IIO_DEVICE_ATTR(name, S_IRUGO, \
			st_sensors_sysfs_scale_avail, NULL , 0);

struct st_sensor_odr_avl {
	unsigned int hz;
	u8 value;
};

struct st_sensor_odr {
	u8 addr;
	u8 mask;
	struct st_sensor_odr_avl odr_avl[ST_SENSORS_ODR_LIST_MAX];
};

struct st_sensor_power {
	u8 addr;
	u8 mask;
	u8 value_off;
	u8 value_on;
};

struct st_sensor_axis {
	u8 addr;
	u8 mask;
};

struct st_sensor_fullscale_avl {
	unsigned int num;
	u8 value;
	unsigned int gain;
	unsigned int gain2;
};

struct st_sensor_fullscale {
	u8 addr;
	u8 mask;
	struct st_sensor_fullscale_avl fs_avl[ST_SENSORS_FULLSCALE_AVL_MAX];
};

struct st_sensor_sim {
	u8 addr;
	u8 value;
};

/**
 * struct st_sensor_bdu - ST sensor device block data update
 * @addr: address of the register.
 * @mask: mask to write the block data update flag.
 */
struct st_sensor_bdu {
	u8 addr;
	u8 mask;
};

/**
 * struct st_sensor_das - ST sensor device data alignment selection
 * @addr: address of the register.
 * @mask: mask to write the das flag for left alignment.
 */
struct st_sensor_das {
	u8 addr;
	u8 mask;
};

/**
 * struct st_sensor_int_drdy - ST sensor device drdy line parameters
 * @addr: address of INT drdy register.
 * @mask: mask to enable drdy line.
 * @addr_od: address to enable/disable Open Drain on the INT line.
 * @mask_od: mask to enable/disable Open Drain on the INT line.
 */
struct st_sensor_int_drdy {
	u8 addr;
	u8 mask;
	u8 addr_od;
	u8 mask_od;
};

/**
 * struct st_sensor_data_ready_irq - ST sensor device data-ready interrupt
 * struct int1 - data-ready configuration register for INT1 pin.
 * struct int2 - data-ready configuration register for INT2 pin.
 * @addr_ihl: address to enable/disable active low on the INT lines.
 * @mask_ihl: mask to enable/disable active low on the INT lines.
 * struct stat_drdy - status register of DRDY (data ready) interrupt.
 * struct ig1 - represents the Interrupt Generator 1 of sensors.
 * @en_addr: address of the enable ig1 register.
 * @en_mask: mask to write the on/off value for enable.
 */
struct st_sensor_data_ready_irq {
	struct st_sensor_int_drdy int1;
	struct st_sensor_int_drdy int2;
	u8 addr_ihl;
	u8 mask_ihl;
	struct {
		u8 addr;
		u8 mask;
	} stat_drdy;
	struct {
		u8 en_addr;
		u8 en_mask;
	} ig1;
};

/**
 * struct st_sensor_settings - ST specific sensor settings
 * @wai: Contents of WhoAmI register.
 * @wai_addr: The address of WhoAmI register.
 * @sensors_supported: List of supported sensors by struct itself.
 * @ch: IIO channels for the sensor.
 * @odr: Output data rate register and ODR list available.
 * @pw: Power register of the sensor.
 * @enable_axis: Enable one or more axis of the sensor.
 * @fs: Full scale register and full scale list available.
 * @bdu: Block data update register.
 * @das: Data Alignment Selection register.
 * @drdy_irq: Data ready register of the sensor.
 * @sim: SPI serial interface mode register of the sensor.
 * @multi_read_bit: Use or not particular bit for [I2C/SPI] multi-read.
 * @bootime: samples to discard when sensor passing from power-down to power-up.
 */
struct st_sensor_settings {
	u8 wai;
	u8 wai_addr;
	char sensors_supported[ST_SENSORS_MAX_4WAI][ST_SENSORS_MAX_NAME];
	struct iio_chan_spec *ch;
	int num_ch;
	struct st_sensor_odr odr;
	struct st_sensor_power pw;
	struct st_sensor_axis enable_axis;
	struct st_sensor_fullscale fs;
	struct st_sensor_bdu bdu;
	struct st_sensor_das das;
	struct st_sensor_data_ready_irq drdy_irq;
	struct st_sensor_sim sim;
	bool multi_read_bit;
	unsigned int bootime;
};

/**
 * struct st_sensor_data - ST sensor device status
 * @trig: The trigger in use by the core driver.
 * @mount_matrix: The mounting matrix of the sensor.
 * @sensor_settings: Pointer to the specific sensor settings in use.
 * @current_fullscale: Maximum range of measure by the sensor.
 * @regmap: Pointer to specific sensor regmap configuration.
 * @enabled: Status of the sensor (false->off, true->on).
 * @odr: Output data rate of the sensor [Hz].
 * num_data_channels: Number of data channels used in buffer.
 * @drdy_int_pin: Redirect DRDY on pin 1 (1) or pin 2 (2).
 * @int_pin_open_drain: Set the interrupt/DRDY to open drain.
 * @irq: the IRQ number.
 * @edge_irq: the IRQ triggers on edges and need special handling.
 * @hw_irq_trigger: if we're using the hardware interrupt on the sensor.
 * @hw_timestamp: Latest timestamp from the interrupt handler, when in use.
 * @buffer_data: Data used by buffer part.
 * @odr_lock: Local lock for preventing concurrent ODR accesses/changes
 */
struct st_sensor_data {
	struct iio_trigger *trig;
	struct iio_mount_matrix mount_matrix;
	struct st_sensor_settings *sensor_settings;
	struct st_sensor_fullscale_avl *current_fullscale;
	struct regmap *regmap;

	bool enabled;

	unsigned int odr;
	unsigned int num_data_channels;

	u8 drdy_int_pin;
	bool int_pin_open_drain;
	int irq;

	bool edge_irq;
	bool hw_irq_trigger;
	s64 hw_timestamp;

	char buffer_data[ST_SENSORS_MAX_BUFFER_SIZE] ____cacheline_aligned;

	struct mutex odr_lock;
};

#ifdef CONFIG_IIO_BUFFER
irqreturn_t st_sensors_trigger_handler(int irq, void *p);
#endif

#ifdef CONFIG_IIO_TRIGGER
int st_sensors_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops);

int st_sensors_validate_device(struct iio_trigger *trig,
			       struct iio_dev *indio_dev);
#else
static inline int st_sensors_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops)
{
	return 0;
}
#define st_sensors_validate_device NULL
#endif

int st_sensors_init_sensor(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata);

int st_sensors_set_enable(struct iio_dev *indio_dev, bool enable);

int st_sensors_set_axis_enable(struct iio_dev *indio_dev, u8 axis_enable);

int st_sensors_power_enable(struct iio_dev *indio_dev);

int st_sensors_debugfs_reg_access(struct iio_dev *indio_dev,
				  unsigned reg, unsigned writeval,
				  unsigned *readval);

int st_sensors_set_odr(struct iio_dev *indio_dev, unsigned int odr);

int st_sensors_set_dataready_irq(struct iio_dev *indio_dev, bool enable);

int st_sensors_set_fullscale_by_gain(struct iio_dev *indio_dev, int scale);

int st_sensors_read_info_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *ch, int *val);

int st_sensors_get_settings_index(const char *name,
				  const struct st_sensor_settings *list,
				  const int list_length);

int st_sensors_verify_id(struct iio_dev *indio_dev);

ssize_t st_sensors_sysfs_sampling_frequency_avail(struct device *dev,
				struct device_attribute *attr, char *buf);

ssize_t st_sensors_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf);

void st_sensors_dev_name_probe(struct device *dev, char *name, int len);

/* Accelerometer */
const struct st_sensor_settings *st_accel_get_settings(const char *name);
int st_accel_common_probe(struct iio_dev *indio_dev);

/* Gyroscope */
const struct st_sensor_settings *st_gyro_get_settings(const char *name);
int st_gyro_common_probe(struct iio_dev *indio_dev);

/* Magnetometer */
const struct st_sensor_settings *st_magn_get_settings(const char *name);
int st_magn_common_probe(struct iio_dev *indio_dev);

/* Pressure */
const struct st_sensor_settings *st_press_get_settings(const char *name);
int st_press_common_probe(struct iio_dev *indio_dev);

#endif /* ST_SENSORS_H */
