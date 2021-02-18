/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Common library for ADIS16XXX devices
 *
 * Copyright 2012 Analog Devices Inc.
 *   Author: Lars-Peter Clausen <lars@metafoo.de>
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
 * struct adis_timeouts - ADIS chip variant timeouts
 * @reset_ms - Wait time after rst pin goes inactive
 * @sw_reset_ms - Wait time after sw reset command
 * @self_test_ms - Wait time after self test command
 */
struct adis_timeout {
	u16 reset_ms;
	u16 sw_reset_ms;
	u16 self_test_ms;
};
/**
 * struct adis_data - ADIS chip variant specific data
 * @read_delay: SPI delay for read operations in us
 * @write_delay: SPI delay for write operations in us
 * @cs_change_delay: SPI delay between CS changes in us
 * @glob_cmd_reg: Register address of the GLOB_CMD register
 * @msc_ctrl_reg: Register address of the MSC_CTRL register
 * @diag_stat_reg: Register address of the DIAG_STAT register
 * @prod_id_reg: Register address of the PROD_ID register
 * @prod_id: Product ID code that should be expected when reading @prod_id_reg
 * @self_test_mask: Bitmask of supported self-test operations
 * @self_test_reg: Register address to request self test command
 * @self_test_no_autoclear: True if device's self-test needs clear of ctrl reg
 * @status_error_msgs: Array of error messgaes
 * @status_error_mask: Bitmask of errors supported by the device
 * @timeouts: Chip specific delays
 * @enable_irq: Hook for ADIS devices that have a special IRQ enable/disable
 * @has_paging: True if ADIS device has paged registers
 * @burst_reg_cmd:	Register command that triggers burst
 * @burst_len:		Burst size in the SPI RX buffer. If @burst_max_len is defined,
 *			this should be the minimum size supported by the device.
 * @burst_max_len:	Holds the maximum burst size when the device supports
 *			more than one burst mode with different sizes
 */
struct adis_data {
	unsigned int read_delay;
	unsigned int write_delay;
	unsigned int cs_change_delay;

	unsigned int glob_cmd_reg;
	unsigned int msc_ctrl_reg;
	unsigned int diag_stat_reg;
	unsigned int prod_id_reg;

	unsigned int prod_id;

	unsigned int self_test_mask;
	unsigned int self_test_reg;
	bool self_test_no_autoclear;
	const struct adis_timeout *timeouts;

	const char * const *status_error_msgs;
	unsigned int status_error_mask;

	int (*enable_irq)(struct adis *adis, bool enable);

	bool has_paging;

	unsigned int burst_reg_cmd;
	unsigned int burst_len;
	unsigned int burst_max_len;
};

/**
 * struct adis - ADIS device instance data
 * @spi: Reference to SPI device which owns this ADIS IIO device
 * @trig: IIO trigger object data
 * @data: ADIS chip variant specific data
 * @burst: ADIS burst transfer information
 * @burst_extra_len: Burst extra length. Should only be used by devices that can
 *		     dynamically change their burst mode length.
 * @state_lock: Lock used by the device to protect state
 * @msg: SPI message object
 * @xfer: SPI transfer objects to be used for a @msg
 * @current_page: Some ADIS devices have registers, this selects current page
 * @irq_flag: IRQ handling flags as passed to request_irq()
 * @buffer: Data buffer for information read from the device
 * @tx: DMA safe TX buffer for SPI transfers
 * @rx: DMA safe RX buffer for SPI transfers
 */
struct adis {
	struct spi_device	*spi;
	struct iio_trigger	*trig;

	const struct adis_data	*data;
	unsigned int		burst_extra_len;
	/**
	 * The state_lock is meant to be used during operations that require
	 * a sequence of SPI R/W in order to protect the SPI transfer
	 * information (fields 'xfer', 'msg' & 'current_page') between
	 * potential concurrent accesses.
	 * This lock is used by all "adis_{functions}" that have to read/write
	 * registers. These functions also have unlocked variants
	 * (see "__adis_{functions}"), which don't hold this lock.
	 * This allows users of the ADIS library to group SPI R/W into
	 * the drivers, but they also must manage this lock themselves.
	 */
	struct mutex		state_lock;
	struct spi_message	msg;
	struct spi_transfer	*xfer;
	unsigned int		current_page;
	unsigned long		irq_flag;
	void			*buffer;

	uint8_t			tx[10] ____cacheline_aligned;
	uint8_t			rx[4];
};

int adis_init(struct adis *adis, struct iio_dev *indio_dev,
	struct spi_device *spi, const struct adis_data *data);
int __adis_reset(struct adis *adis);

/**
 * adis_reset() - Reset the device
 * @adis: The adis device
 *
 * Returns 0 on success, a negative error code otherwise
 */
static inline int adis_reset(struct adis *adis)
{
	int ret;

	mutex_lock(&adis->state_lock);
	ret = __adis_reset(adis);
	mutex_unlock(&adis->state_lock);

	return ret;
}

int __adis_write_reg(struct adis *adis, unsigned int reg,
	unsigned int val, unsigned int size);
int __adis_read_reg(struct adis *adis, unsigned int reg,
	unsigned int *val, unsigned int size);

/**
 * __adis_write_reg_8() - Write single byte to a register (unlocked)
 * @adis: The adis device
 * @reg: The address of the register to be written
 * @value: The value to write
 */
static inline int __adis_write_reg_8(struct adis *adis, unsigned int reg,
	uint8_t val)
{
	return __adis_write_reg(adis, reg, val, 1);
}

/**
 * __adis_write_reg_16() - Write 2 bytes to a pair of registers (unlocked)
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @value: Value to be written
 */
static inline int __adis_write_reg_16(struct adis *adis, unsigned int reg,
	uint16_t val)
{
	return __adis_write_reg(adis, reg, val, 2);
}

/**
 * __adis_write_reg_32() - write 4 bytes to four registers (unlocked)
 * @adis: The adis device
 * @reg: The address of the lower of the four register
 * @value: Value to be written
 */
static inline int __adis_write_reg_32(struct adis *adis, unsigned int reg,
	uint32_t val)
{
	return __adis_write_reg(adis, reg, val, 4);
}

/**
 * __adis_read_reg_16() - read 2 bytes from a 16-bit register (unlocked)
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @val: The value read back from the device
 */
static inline int __adis_read_reg_16(struct adis *adis, unsigned int reg,
	uint16_t *val)
{
	unsigned int tmp;
	int ret;

	ret = __adis_read_reg(adis, reg, &tmp, 2);
	if (ret == 0)
		*val = tmp;

	return ret;
}

/**
 * __adis_read_reg_32() - read 4 bytes from a 32-bit register (unlocked)
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @val: The value read back from the device
 */
static inline int __adis_read_reg_32(struct adis *adis, unsigned int reg,
	uint32_t *val)
{
	unsigned int tmp;
	int ret;

	ret = __adis_read_reg(adis, reg, &tmp, 4);
	if (ret == 0)
		*val = tmp;

	return ret;
}

/**
 * adis_write_reg() - write N bytes to register
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @value: The value to write to device (up to 4 bytes)
 * @size: The size of the @value (in bytes)
 */
static inline int adis_write_reg(struct adis *adis, unsigned int reg,
	unsigned int val, unsigned int size)
{
	int ret;

	mutex_lock(&adis->state_lock);
	ret = __adis_write_reg(adis, reg, val, size);
	mutex_unlock(&adis->state_lock);

	return ret;
}

/**
 * adis_read_reg() - read N bytes from register
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @val: The value read back from the device
 * @size: The size of the @val buffer
 */
static int adis_read_reg(struct adis *adis, unsigned int reg,
	unsigned int *val, unsigned int size)
{
	int ret;

	mutex_lock(&adis->state_lock);
	ret = __adis_read_reg(adis, reg, val, size);
	mutex_unlock(&adis->state_lock);

	return ret;
}

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
	if (ret == 0)
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
	if (ret == 0)
		*val = tmp;

	return ret;
}

int __adis_update_bits_base(struct adis *adis, unsigned int reg, const u32 mask,
			    const u32 val, u8 size);
/**
 * adis_update_bits_base() - ADIS Update bits function - Locked version
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @mask: Bitmask to change
 * @val: Value to be written
 * @size: Size of the register to update
 *
 * Updates the desired bits of @reg in accordance with @mask and @val.
 */
static inline int adis_update_bits_base(struct adis *adis, unsigned int reg,
					const u32 mask, const u32 val, u8 size)
{
	int ret;

	mutex_lock(&adis->state_lock);
	ret = __adis_update_bits_base(adis, reg, mask, val, size);
	mutex_unlock(&adis->state_lock);
	return ret;
}

/**
 * adis_update_bits() - Wrapper macro for adis_update_bits_base - Locked version
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @mask: Bitmask to change
 * @val: Value to be written
 *
 * This macro evaluates the sizeof of @val at compile time and calls
 * adis_update_bits_base() accordingly. Be aware that using MACROS/DEFINES for
 * @val can lead to undesired behavior if the register to update is 16bit.
 */
#define adis_update_bits(adis, reg, mask, val) ({			\
	BUILD_BUG_ON(sizeof(val) == 1 || sizeof(val) == 8);		\
	__builtin_choose_expr(sizeof(val) == 4,				\
		adis_update_bits_base(adis, reg, mask, val, 4),         \
		adis_update_bits_base(adis, reg, mask, val, 2));	\
})

/**
 * adis_update_bits() - Wrapper macro for adis_update_bits_base
 * @adis: The adis device
 * @reg: The address of the lower of the two registers
 * @mask: Bitmask to change
 * @val: Value to be written
 *
 * This macro evaluates the sizeof of @val at compile time and calls
 * adis_update_bits_base() accordingly. Be aware that using MACROS/DEFINES for
 * @val can lead to undesired behavior if the register to update is 16bit.
 */
#define __adis_update_bits(adis, reg, mask, val) ({			\
	BUILD_BUG_ON(sizeof(val) == 1 || sizeof(val) == 8);		\
	__builtin_choose_expr(sizeof(val) == 4,				\
		__adis_update_bits_base(adis, reg, mask, val, 4),	\
		__adis_update_bits_base(adis, reg, mask, val, 2));	\
})

int adis_enable_irq(struct adis *adis, bool enable);
int __adis_check_status(struct adis *adis);
int __adis_initial_startup(struct adis *adis);

static inline int adis_check_status(struct adis *adis)
{
	int ret;

	mutex_lock(&adis->state_lock);
	ret = __adis_check_status(adis);
	mutex_unlock(&adis->state_lock);

	return ret;
}

/* locked version of __adis_initial_startup() */
static inline int adis_initial_startup(struct adis *adis)
{
	int ret;

	mutex_lock(&adis->state_lock);
	ret = __adis_initial_startup(adis);
	mutex_unlock(&adis->state_lock);

	return ret;
}

static inline void adis_dev_lock(struct adis *adis)
{
	mutex_lock(&adis->state_lock);
}

static inline void adis_dev_unlock(struct adis *adis)
{
	mutex_unlock(&adis->state_lock);
}

int adis_single_conversion(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, unsigned int error_mask,
	int *val);

#define ADIS_VOLTAGE_CHAN(addr, si, chan, name, info_all, bits) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (chan), \
	.extend_name = name, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = info_all, \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = 16, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS_SUPPLY_CHAN(addr, si, info_all, bits) \
	ADIS_VOLTAGE_CHAN(addr, si, 0, "supply", info_all, bits)

#define ADIS_AUX_ADC_CHAN(addr, si, info_all, bits) \
	ADIS_VOLTAGE_CHAN(addr, si, 1, NULL, info_all, bits)

#define ADIS_TEMP_CHAN(addr, si, info_all, bits) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.channel = 0, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_OFFSET), \
	.info_mask_shared_by_all = info_all, \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = 16, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS_MOD_CHAN(_type, mod, addr, si, info_sep, info_all, bits) { \
	.type = (_type), \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		 info_sep, \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = info_all, \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS_ACCEL_CHAN(mod, addr, si, info_sep, info_all, bits) \
	ADIS_MOD_CHAN(IIO_ACCEL, mod, addr, si, info_sep, info_all, bits)

#define ADIS_GYRO_CHAN(mod, addr, si, info_sep, info_all, bits)		\
	ADIS_MOD_CHAN(IIO_ANGL_VEL, mod, addr, si, info_sep, info_all, bits)

#define ADIS_INCLI_CHAN(mod, addr, si, info_sep, info_all, bits) \
	ADIS_MOD_CHAN(IIO_INCLI, mod, addr, si, info_sep, info_all, bits)

#define ADIS_ROT_CHAN(mod, addr, si, info_sep, info_all, bits) \
	ADIS_MOD_CHAN(IIO_ROT, mod, addr, si, info_sep, info_all, bits)

#ifdef CONFIG_IIO_ADIS_LIB_BUFFER

int
devm_adis_setup_buffer_and_trigger(struct adis *adis, struct iio_dev *indio_dev,
				   irq_handler_t trigger_handler);

int devm_adis_probe_trigger(struct adis *adis, struct iio_dev *indio_dev);

int adis_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask);

#else /* CONFIG_IIO_BUFFER */

static inline int
devm_adis_setup_buffer_and_trigger(struct adis *adis, struct iio_dev *indio_dev,
				   irq_handler_t trigger_handler)
{
	return 0;
}

static inline int devm_adis_probe_trigger(struct adis *adis,
					  struct iio_dev *indio_dev)
{
	return 0;
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
