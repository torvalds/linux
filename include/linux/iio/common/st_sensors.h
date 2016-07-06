/*
 * STMicroelectronics sensors library driver
 *
 * Copyright 2012-2013 STMicroelectronics Inc.
 *
 * Denis Ciocca <denis.ciocca@st.com>
 *
 * Licensed under the GPL-2.
 */

#ifndef ST_SENSORS_H
#define ST_SENSORS_H

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/irqreturn.h>
#include <linux/iio/trigger.h>
#include <linux/bitops.h>
#include <linux/regulator/consumer.h>

#include <linux/platform_data/st_sensors_pdata.h>

#define ST_SENSORS_TX_MAX_LENGTH		2
#define ST_SENSORS_RX_MAX_LENGTH		6

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
#define ST_SENSORS_MAX_4WAI			7

#define ST_SENSORS_LSM_CHANNELS(device_type, mask, index, mod, \
					ch2, s, endian, rbits, sbits, addr) \
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
}

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
 * struct st_sensor_data_ready_irq - ST sensor device data-ready interrupt
 * @addr: address of the register.
 * @mask_int1: mask to enable/disable IRQ on INT1 pin.
 * @mask_int2: mask to enable/disable IRQ on INT2 pin.
 * @addr_ihl: address to enable/disable active low on the INT lines.
 * @mask_ihl: mask to enable/disable active low on the INT lines.
 * @addr_od: address to enable/disable Open Drain on the INT lines.
 * @mask_od: mask to enable/disable Open Drain on the INT lines.
 * @addr_stat_drdy: address to read status of DRDY (data ready) interrupt
 * struct ig1 - represents the Interrupt Generator 1 of sensors.
 * @en_addr: address of the enable ig1 register.
 * @en_mask: mask to write the on/off value for enable.
 */
struct st_sensor_data_ready_irq {
	u8 addr;
	u8 mask_int1;
	u8 mask_int2;
	u8 addr_ihl;
	u8 mask_ihl;
	u8 addr_od;
	u8 mask_od;
	u8 addr_stat_drdy;
	struct {
		u8 en_addr;
		u8 en_mask;
	} ig1;
};

/**
 * struct st_sensor_transfer_buffer - ST sensor device I/O buffer
 * @buf_lock: Mutex to protect rx and tx buffers.
 * @tx_buf: Buffer used by SPI transfer function to send data to the sensors.
 *	This buffer is used to avoid DMA not-aligned issue.
 * @rx_buf: Buffer used by SPI transfer to receive data from sensors.
 *	This buffer is used to avoid DMA not-aligned issue.
 */
struct st_sensor_transfer_buffer {
	struct mutex buf_lock;
	u8 rx_buf[ST_SENSORS_RX_MAX_LENGTH];
	u8 tx_buf[ST_SENSORS_TX_MAX_LENGTH] ____cacheline_aligned;
};

/**
 * struct st_sensor_transfer_function - ST sensor device I/O function
 * @read_byte: Function used to read one byte.
 * @write_byte: Function used to write one byte.
 * @read_multiple_byte: Function used to read multiple byte.
 */
struct st_sensor_transfer_function {
	int (*read_byte) (struct st_sensor_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 *res_byte);
	int (*write_byte) (struct st_sensor_transfer_buffer *tb,
				struct device *dev, u8 reg_addr, u8 data);
	int (*read_multiple_byte) (struct st_sensor_transfer_buffer *tb,
		struct device *dev, u8 reg_addr, int len, u8 *data,
							bool multiread_bit);
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
 * @drdy_irq: Data ready register of the sensor.
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
	struct st_sensor_data_ready_irq drdy_irq;
	bool multi_read_bit;
	unsigned int bootime;
};

/**
 * struct st_sensor_data - ST sensor device status
 * @dev: Pointer to instance of struct device (I2C or SPI).
 * @trig: The trigger in use by the core driver.
 * @sensor_settings: Pointer to the specific sensor settings in use.
 * @current_fullscale: Maximum range of measure by the sensor.
 * @vdd: Pointer to sensor's Vdd power supply
 * @vdd_io: Pointer to sensor's Vdd-IO power supply
 * @enabled: Status of the sensor (false->off, true->on).
 * @multiread_bit: Use or not particular bit for [I2C/SPI] multiread.
 * @buffer_data: Data used by buffer part.
 * @odr: Output data rate of the sensor [Hz].
 * num_data_channels: Number of data channels used in buffer.
 * @drdy_int_pin: Redirect DRDY on pin 1 (1) or pin 2 (2).
 * @int_pin_open_drain: Set the interrupt/DRDY to open drain.
 * @get_irq_data_ready: Function to get the IRQ used for data ready signal.
 * @tf: Transfer function structure used by I/O operations.
 * @tb: Transfer buffers and mutex used by I/O operations.
 * @hw_irq_trigger: if we're using the hardware interrupt on the sensor.
 * @hw_timestamp: Latest timestamp from the interrupt handler, when in use.
 */
struct st_sensor_data {
	struct device *dev;
	struct iio_trigger *trig;
	struct st_sensor_settings *sensor_settings;
	struct st_sensor_fullscale_avl *current_fullscale;
	struct regulator *vdd;
	struct regulator *vdd_io;

	bool enabled;
	bool multiread_bit;

	char *buffer_data;

	unsigned int odr;
	unsigned int num_data_channels;

	u8 drdy_int_pin;
	bool int_pin_open_drain;

	unsigned int (*get_irq_data_ready) (struct iio_dev *indio_dev);

	const struct st_sensor_transfer_function *tf;
	struct st_sensor_transfer_buffer tb;

	bool hw_irq_trigger;
	s64 hw_timestamp;
};

#ifdef CONFIG_IIO_BUFFER
irqreturn_t st_sensors_trigger_handler(int irq, void *p);

int st_sensors_get_buffer_element(struct iio_dev *indio_dev, u8 *buf);
#endif

#ifdef CONFIG_IIO_TRIGGER
int st_sensors_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops);

void st_sensors_deallocate_trigger(struct iio_dev *indio_dev);
int st_sensors_validate_device(struct iio_trigger *trig,
			       struct iio_dev *indio_dev);
#else
static inline int st_sensors_allocate_trigger(struct iio_dev *indio_dev,
				const struct iio_trigger_ops *trigger_ops)
{
	return 0;
}
static inline void st_sensors_deallocate_trigger(struct iio_dev *indio_dev)
{
	return;
}
#define st_sensors_validate_device NULL
#endif

int st_sensors_init_sensor(struct iio_dev *indio_dev,
					struct st_sensors_platform_data *pdata);

int st_sensors_set_enable(struct iio_dev *indio_dev, bool enable);

int st_sensors_set_axis_enable(struct iio_dev *indio_dev, u8 axis_enable);

void st_sensors_power_enable(struct iio_dev *indio_dev);

void st_sensors_power_disable(struct iio_dev *indio_dev);

int st_sensors_debugfs_reg_access(struct iio_dev *indio_dev,
				  unsigned reg, unsigned writeval,
				  unsigned *readval);

int st_sensors_set_odr(struct iio_dev *indio_dev, unsigned int odr);

int st_sensors_set_dataready_irq(struct iio_dev *indio_dev, bool enable);

int st_sensors_set_fullscale_by_gain(struct iio_dev *indio_dev, int scale);

int st_sensors_read_info_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *ch, int *val);

int st_sensors_check_device_support(struct iio_dev *indio_dev,
	int num_sensors_list, const struct st_sensor_settings *sensor_settings);

ssize_t st_sensors_sysfs_sampling_frequency_avail(struct device *dev,
				struct device_attribute *attr, char *buf);

ssize_t st_sensors_sysfs_scale_avail(struct device *dev,
				struct device_attribute *attr, char *buf);

#endif /* ST_SENSORS_H */
