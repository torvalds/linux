/*
 * Common library for ADIS16XXX devices
 *
 * Copyright 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __IIO_ADIS_H__
#define __IIO_ADIS_H__

#include <linux/spi/spi.h>
#include <linux/interrupt.h>
#include <linux/iio/types.h>

#define ADIS_WRITE_REG(reg) ((0x80 | (reg)))
#define ADIS_READ_REG(reg) ((reg) & 0x7f)

#define ADIS_PAGE_SIZE 0x80
#define ADIS_REG_PAGE_ID 0x00

struct adis;

/**
 * struct adis_data - ADIS chip variant specific data
 * @read_delay: SPI delay for read operations in us
 * @write_delay: SPI delay for write operations in us
 * @glob_cmd_reg: Register address of the GLOB_CMD register
 * @msc_ctrl_reg: Register address of the MSC_CTRL register
 * @diag_stat_reg: Register address of the DIAG_STAT register
 * @status_error_msgs: Array of error messgaes
 * @status_error_mask:
 */
struct adis_data {
	unsigned int read_delay;
	unsigned int write_delay;

	unsigned int glob_cmd_reg;
	unsigned int msc_ctrl_reg;
	unsigned int diag_stat_reg;

	unsigned int self_test_mask;
	unsigned int startup_delay;

	const char * const *status_error_msgs;
	unsigned int status_error_mask;

	int (*enable_irq)(struct adis *adis, bool enable);

	bool has_paging;
};

struct adis {
	struct spi_device	*spi;
	struct iio_trigger	*trig;

	const struct adis_data	*data;

	struct mutex		txrx_lock;
	struct spi_message	msg;
	struct spi_transfer	*xfer;
	unsigned int		current_page;
	void			*buffer;

	uint8_t			tx[10] ____cacheline_aligned;
	uint8_t			rx[4];
};

int adis_init(struct adis *adis, struct iio_dev *indio_dev,
	struct spi_device *spi, const struct adis_data *data);
int adis_reset(struct adis *adis);

int adis_write_reg(struct adis *adis, unsigned int reg,
	unsigned int val, unsigned int size);
int adis_read_reg(struct adis *adis, unsigned int reg,
	unsigned int *val, unsigned int size);

/**
 * adis_write_reg_8() - Write single byte to a register
 * @adis: The adis device
 * @reg: The address of the register to be written
 * @value: The value to write
 */
static inline int adis_write_reg_8(struct adis *adis, unsigned int reg,
	uint8_t val)
{
	return adis_write_reg(adis, reg, val, 1);
}

/**
 * adis_write_reg_16() - Write 2 bytes to a pair of registers
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @value: Value to be written
 */
static inline int adis_write_reg_16(struct adis *adis, unsigned int reg,
	uint16_t val)
{
	return adis_write_reg(adis, reg, val, 2);
}

/**
 * adis_write_reg_32() - write 4 bytes to four registers
 * @adis: The adis device
 * @reg: The address of the lower of the four register
 * @value: Value to be written
 */
static inline int adis_write_reg_32(struct adis *adis, unsigned int reg,
	uint32_t val)
{
	return adis_write_reg(adis, reg, val, 4);
}

/**
 * adis_read_reg_16() - read 2 bytes from a 16-bit register
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @val: The value read back from the device
 */
static inline int adis_read_reg_16(struct adis *adis, unsigned int reg,
	uint16_t *val)
{
	unsigned int tmp;
	int ret;

	ret = adis_read_reg(adis, reg, &tmp, 2);
	*val = tmp;

	return ret;
}

/**
 * adis_read_reg_32() - read 4 bytes from a 32-bit register
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @val: The value read back from the device
 */
static inline int adis_read_reg_32(struct adis *adis, unsigned int reg,
	uint32_t *val)
{
	unsigned int tmp;
	int ret;

	ret = adis_read_reg(adis, reg, &tmp, 4);
	*val = tmp;

	return ret;
}

int adis_enable_irq(struct adis *adis, bool enable);
int adis_check_status(struct adis *adis);

int adis_initial_startup(struct adis *adis);

int adis_single_conversion(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int error_mask,
	int *val);

#define ADIS_VOLTAGE_CHAN(addr, si, chan, name, bits) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (chan), \
	.extend_name = name, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = 16, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS_SUPPLY_CHAN(addr, si, bits) \
	ADIS_VOLTAGE_CHAN(addr, si, 0, "supply", bits)

#define ADIS_AUX_ADC_CHAN(addr, si, bits) \
	ADIS_VOLTAGE_CHAN(addr, si, 1, NULL, bits)

#define ADIS_TEMP_CHAN(addr, si, bits) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.channel = 0, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_OFFSET), \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = 16, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS_MOD_CHAN(_type, mod, addr, si, info_sep, bits) { \
	.type = (_type), \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		 info_sep, \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS_ACCEL_CHAN(mod, addr, si, info_sep, bits) \
	ADIS_MOD_CHAN(IIO_ACCEL, mod, addr, si, info_sep, bits)

#define ADIS_GYRO_CHAN(mod, addr, si, info_sep, bits) \
	ADIS_MOD_CHAN(IIO_ANGL_VEL, mod, addr, si, info_sep, bits)

#define ADIS_INCLI_CHAN(mod, addr, si, info_sep, bits) \
	ADIS_MOD_CHAN(IIO_INCLI, mod, addr, si, info_sep, bits)

#define ADIS_ROT_CHAN(mod, addr, si, info_sep, bits) \
	ADIS_MOD_CHAN(IIO_ROT, mod, addr, si, info_sep, bits)

#ifdef CONFIG_IIO_ADIS_LIB_BUFFER

int adis_setup_buffer_and_trigger(struct adis *adis,
	struct iio_dev *indio_dev, irqreturn_t (*trigger_handler)(int, void *));
void adis_cleanup_buffer_and_trigger(struct adis *adis,
	struct iio_dev *indio_dev);

int adis_probe_trigger(struct adis *adis, struct iio_dev *indio_dev);
void adis_remove_trigger(struct adis *adis);

int adis_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask);

#else /* CONFIG_IIO_BUFFER */

static inline int adis_setup_buffer_and_trigger(struct adis *adis,
	struct iio_dev *indio_dev, irqreturn_t (*trigger_handler)(int, void *))
{
	return 0;
}

static inline void adis_cleanup_buffer_and_trigger(struct adis *adis,
	struct iio_dev *indio_dev)
{
}

static inline int adis_probe_trigger(struct adis *adis,
	struct iio_dev *indio_dev)
{
	return 0;
}

static inline void adis_remove_trigger(struct adis *adis)
{
}

#define adis_update_scan_mode NULL

#endif /* CONFIG_IIO_BUFFER */

#ifdef CONFIG_DEBUG_FS

int adis_debugfs_reg_access(struct iio_dev *indio_dev,
	unsigned int reg, unsigned int writeval, unsigned int *readval);

#else

#define adis_debugfs_reg_access NULL

#endif

#endif
