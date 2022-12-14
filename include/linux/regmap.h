/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_REGMAP_H
#define __LINUX_REGMAP_H

/*
 * Register map access API
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/lockdep.h>
#include <linux/iopoll.h>
#include <linux/fwnode.h>

struct module;
struct clk;
struct device;
struct device_node;
struct i2c_client;
struct i3c_device;
struct irq_domain;
struct mdio_device;
struct slim_device;
struct spi_device;
struct spmi_device;
struct regmap;
struct regmap_range_cfg;
struct regmap_field;
struct snd_ac97;
struct sdw_slave;

/* An enum of all the supported cache types */
enum regcache_type {
	REGCACHE_NONE,
	REGCACHE_RBTREE,
	REGCACHE_COMPRESSED,
	REGCACHE_FLAT,
};

/**
 * struct reg_default - Default value for a register.
 *
 * @reg: Register address.
 * @def: Register default value.
 *
 * We use an array of structs rather than a simple array as many modern devices
 * have very sparse register maps.
 */
struct reg_default {
	unsigned int reg;
	unsigned int def;
};

/**
 * struct reg_sequence - An individual write from a sequence of writes.
 *
 * @reg: Register address.
 * @def: Register value.
 * @delay_us: Delay to be applied after the register write in microseconds
 *
 * Register/value pairs for sequences of writes with an optional delay in
 * microseconds to be applied after each write.
 */
struct reg_sequence {
	unsigned int reg;
	unsigned int def;
	unsigned int delay_us;
};

#define REG_SEQ(_reg, _def, _delay_us) {		\
				.reg = _reg,		\
				.def = _def,		\
				.delay_us = _delay_us,	\
				}
#define REG_SEQ0(_reg, _def)	REG_SEQ(_reg, _def, 0)

/**
 * regmap_read_poll_timeout - Poll until a condition is met or a timeout occurs
 *
 * @map: Regmap to read from
 * @addr: Address to poll
 * @val: Unsigned integer variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @sleep_us: Maximum time to sleep between reads in us (0
 *            tight-loops).  Should be less than ~20ms since usleep_range
 *            is used (see Documentation/timers/timers-howto.rst).
 * @timeout_us: Timeout in us, 0 means never timeout
 *
 * Returns 0 on success and -ETIMEDOUT upon a timeout or the regmap_read
 * error return value in case of a error read. In the two former cases,
 * the last read value at @addr is stored in @val. Must not be called
 * from atomic context if sleep_us or timeout_us are used.
 *
 * This is modelled after the readx_poll_timeout macros in linux/iopoll.h.
 */
#define regmap_read_poll_timeout(map, addr, val, cond, sleep_us, timeout_us) \
({ \
	int __ret, __tmp; \
	__tmp = read_poll_timeout(regmap_read, __ret, __ret || (cond), \
			sleep_us, timeout_us, false, (map), (addr), &(val)); \
	__ret ?: __tmp; \
})

/**
 * regmap_read_poll_timeout_atomic - Poll until a condition is met or a timeout occurs
 *
 * @map: Regmap to read from
 * @addr: Address to poll
 * @val: Unsigned integer variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @delay_us: Time to udelay between reads in us (0 tight-loops).
 *            Should be less than ~10us since udelay is used
 *            (see Documentation/timers/timers-howto.rst).
 * @timeout_us: Timeout in us, 0 means never timeout
 *
 * Returns 0 on success and -ETIMEDOUT upon a timeout or the regmap_read
 * error return value in case of a error read. In the two former cases,
 * the last read value at @addr is stored in @val.
 *
 * This is modelled after the readx_poll_timeout_atomic macros in linux/iopoll.h.
 *
 * Note: In general regmap cannot be used in atomic context. If you want to use
 * this macro then first setup your regmap for atomic use (flat or no cache
 * and MMIO regmap).
 */
#define regmap_read_poll_timeout_atomic(map, addr, val, cond, delay_us, timeout_us) \
({ \
	u64 __timeout_us = (timeout_us); \
	unsigned long __delay_us = (delay_us); \
	ktime_t __timeout = ktime_add_us(ktime_get(), __timeout_us); \
	int __ret; \
	for (;;) { \
		__ret = regmap_read((map), (addr), &(val)); \
		if (__ret) \
			break; \
		if (cond) \
			break; \
		if ((__timeout_us) && \
		    ktime_compare(ktime_get(), __timeout) > 0) { \
			__ret = regmap_read((map), (addr), &(val)); \
			break; \
		} \
		if (__delay_us) \
			udelay(__delay_us); \
	} \
	__ret ?: ((cond) ? 0 : -ETIMEDOUT); \
})

/**
 * regmap_field_read_poll_timeout - Poll until a condition is met or timeout
 *
 * @field: Regmap field to read from
 * @val: Unsigned integer variable to read the value into
 * @cond: Break condition (usually involving @val)
 * @sleep_us: Maximum time to sleep between reads in us (0
 *            tight-loops).  Should be less than ~20ms since usleep_range
 *            is used (see Documentation/timers/timers-howto.rst).
 * @timeout_us: Timeout in us, 0 means never timeout
 *
 * Returns 0 on success and -ETIMEDOUT upon a timeout or the regmap_field_read
 * error return value in case of a error read. In the two former cases,
 * the last read value at @addr is stored in @val. Must not be called
 * from atomic context if sleep_us or timeout_us are used.
 *
 * This is modelled after the readx_poll_timeout macros in linux/iopoll.h.
 */
#define regmap_field_read_poll_timeout(field, val, cond, sleep_us, timeout_us) \
({ \
	int __ret, __tmp; \
	__tmp = read_poll_timeout(regmap_field_read, __ret, __ret || (cond), \
			sleep_us, timeout_us, false, (field), &(val)); \
	__ret ?: __tmp; \
})

#ifdef CONFIG_REGMAP

enum regmap_endian {
	/* Unspecified -> 0 -> Backwards compatible default */
	REGMAP_ENDIAN_DEFAULT = 0,
	REGMAP_ENDIAN_BIG,
	REGMAP_ENDIAN_LITTLE,
	REGMAP_ENDIAN_NATIVE,
};

/**
 * struct regmap_range - A register range, used for access related checks
 *                       (readable/writeable/volatile/precious checks)
 *
 * @range_min: address of first register
 * @range_max: address of last register
 */
struct regmap_range {
	unsigned int range_min;
	unsigned int range_max;
};

#define regmap_reg_range(low, high) { .range_min = low, .range_max = high, }

/**
 * struct regmap_access_table - A table of register ranges for access checks
 *
 * @yes_ranges : pointer to an array of regmap ranges used as "yes ranges"
 * @n_yes_ranges: size of the above array
 * @no_ranges: pointer to an array of regmap ranges used as "no ranges"
 * @n_no_ranges: size of the above array
 *
 * A table of ranges including some yes ranges and some no ranges.
 * If a register belongs to a no_range, the corresponding check function
 * will return false. If a register belongs to a yes range, the corresponding
 * check function will return true. "no_ranges" are searched first.
 */
struct regmap_access_table {
	const struct regmap_range *yes_ranges;
	unsigned int n_yes_ranges;
	const struct regmap_range *no_ranges;
	unsigned int n_no_ranges;
};

typedef void (*regmap_lock)(void *);
typedef void (*regmap_unlock)(void *);

/**
 * struct regmap_config - Configuration for the register map of a device.
 *
 * @name: Optional name of the regmap. Useful when a device has multiple
 *        register regions.
 *
 * @reg_bits: Number of bits in a register address, mandatory.
 * @reg_stride: The register address stride. Valid register addresses are a
 *              multiple of this value. If set to 0, a value of 1 will be
 *              used.
 * @reg_downshift: The number of bits to downshift the register before
 *		   performing any operations.
 * @reg_base: Value to be added to every register address before performing any
 *	      operation.
 * @pad_bits: Number of bits of padding between register and value.
 * @val_bits: Number of bits in a register value, mandatory.
 *
 * @writeable_reg: Optional callback returning true if the register
 *		   can be written to. If this field is NULL but wr_table
 *		   (see below) is not, the check is performed on such table
 *                 (a register is writeable if it belongs to one of the ranges
 *                  specified by wr_table).
 * @readable_reg: Optional callback returning true if the register
 *		  can be read from. If this field is NULL but rd_table
 *		   (see below) is not, the check is performed on such table
 *                 (a register is readable if it belongs to one of the ranges
 *                  specified by rd_table).
 * @volatile_reg: Optional callback returning true if the register
 *		  value can't be cached. If this field is NULL but
 *		  volatile_table (see below) is not, the check is performed on
 *                such table (a register is volatile if it belongs to one of
 *                the ranges specified by volatile_table).
 * @precious_reg: Optional callback returning true if the register
 *		  should not be read outside of a call from the driver
 *		  (e.g., a clear on read interrupt status register). If this
 *                field is NULL but precious_table (see below) is not, the
 *                check is performed on such table (a register is precious if
 *                it belongs to one of the ranges specified by precious_table).
 * @writeable_noinc_reg: Optional callback returning true if the register
 *			supports multiple write operations without incrementing
 *			the register number. If this field is NULL but
 *			wr_noinc_table (see below) is not, the check is
 *			performed on such table (a register is no increment
 *			writeable if it belongs to one of the ranges specified
 *			by wr_noinc_table).
 * @readable_noinc_reg: Optional callback returning true if the register
 *			supports multiple read operations without incrementing
 *			the register number. If this field is NULL but
 *			rd_noinc_table (see below) is not, the check is
 *			performed on such table (a register is no increment
 *			readable if it belongs to one of the ranges specified
 *			by rd_noinc_table).
 * @disable_locking: This regmap is either protected by external means or
 *                   is guaranteed not to be accessed from multiple threads.
 *                   Don't use any locking mechanisms.
 * @lock:	  Optional lock callback (overrides regmap's default lock
 *		  function, based on spinlock or mutex).
 * @unlock:	  As above for unlocking.
 * @lock_arg:	  this field is passed as the only argument of lock/unlock
 *		  functions (ignored in case regular lock/unlock functions
 *		  are not overridden).
 * @reg_read:	  Optional callback that if filled will be used to perform
 *           	  all the reads from the registers. Should only be provided for
 *		  devices whose read operation cannot be represented as a simple
 *		  read operation on a bus such as SPI, I2C, etc. Most of the
 *		  devices do not need this.
 * @reg_write:	  Same as above for writing.
 * @reg_update_bits: Optional callback that if filled will be used to perform
 *		     all the update_bits(rmw) operation. Should only be provided
 *		     if the function require special handling with lock and reg
 *		     handling and the operation cannot be represented as a simple
 *		     update_bits operation on a bus such as SPI, I2C, etc.
 * @read: Optional callback that if filled will be used to perform all the
 *        bulk reads from the registers. Data is returned in the buffer used
 *        to transmit data.
 * @write: Same as above for writing.
 * @max_raw_read: Max raw read size that can be used on the device.
 * @max_raw_write: Max raw write size that can be used on the device.
 * @fast_io:	  Register IO is fast. Use a spinlock instead of a mutex
 *	     	  to perform locking. This field is ignored if custom lock/unlock
 *	     	  functions are used (see fields lock/unlock of struct regmap_config).
 *		  This field is a duplicate of a similar file in
 *		  'struct regmap_bus' and serves exact same purpose.
 *		   Use it only for "no-bus" cases.
 * @io_port:	  Support IO port accessors. Makes sense only when MMIO vs. IO port
 *		  access can be distinguished.
 * @max_register: Optional, specifies the maximum valid register address.
 * @wr_table:     Optional, points to a struct regmap_access_table specifying
 *                valid ranges for write access.
 * @rd_table:     As above, for read access.
 * @volatile_table: As above, for volatile registers.
 * @precious_table: As above, for precious registers.
 * @wr_noinc_table: As above, for no increment writeable registers.
 * @rd_noinc_table: As above, for no increment readable registers.
 * @reg_defaults: Power on reset values for registers (for use with
 *                register cache support).
 * @num_reg_defaults: Number of elements in reg_defaults.
 *
 * @read_flag_mask: Mask to be set in the top bytes of the register when doing
 *                  a read.
 * @write_flag_mask: Mask to be set in the top bytes of the register when doing
 *                   a write. If both read_flag_mask and write_flag_mask are
 *                   empty and zero_flag_mask is not set the regmap_bus default
 *                   masks are used.
 * @zero_flag_mask: If set, read_flag_mask and write_flag_mask are used even
 *                   if they are both empty.
 * @use_relaxed_mmio: If set, MMIO R/W operations will not use memory barriers.
 *                    This can avoid load on devices which don't require strict
 *                    orderings, but drivers should carefully add any explicit
 *                    memory barriers when they may require them.
 * @use_single_read: If set, converts the bulk read operation into a series of
 *                   single read operations. This is useful for a device that
 *                   does not support  bulk read.
 * @use_single_write: If set, converts the bulk write operation into a series of
 *                    single write operations. This is useful for a device that
 *                    does not support bulk write.
 * @can_multi_write: If set, the device supports the multi write mode of bulk
 *                   write operations, if clear multi write requests will be
 *                   split into individual write operations
 *
 * @cache_type: The actual cache type.
 * @reg_defaults_raw: Power on reset values for registers (for use with
 *                    register cache support).
 * @num_reg_defaults_raw: Number of elements in reg_defaults_raw.
 * @reg_format_endian: Endianness for formatted register addresses. If this is
 *                     DEFAULT, the @reg_format_endian_default value from the
 *                     regmap bus is used.
 * @val_format_endian: Endianness for formatted register values. If this is
 *                     DEFAULT, the @reg_format_endian_default value from the
 *                     regmap bus is used.
 *
 * @ranges: Array of configuration entries for virtual address ranges.
 * @num_ranges: Number of range configuration entries.
 * @use_hwlock: Indicate if a hardware spinlock should be used.
 * @use_raw_spinlock: Indicate if a raw spinlock should be used.
 * @hwlock_id: Specify the hardware spinlock id.
 * @hwlock_mode: The hardware spinlock mode, should be HWLOCK_IRQSTATE,
 *		 HWLOCK_IRQ or 0.
 * @can_sleep: Optional, specifies whether regmap operations can sleep.
 */
struct regmap_config {
	const char *name;

	int reg_bits;
	int reg_stride;
	int reg_downshift;
	unsigned int reg_base;
	int pad_bits;
	int val_bits;

	bool (*writeable_reg)(struct device *dev, unsigned int reg);
	bool (*readable_reg)(struct device *dev, unsigned int reg);
	bool (*volatile_reg)(struct device *dev, unsigned int reg);
	bool (*precious_reg)(struct device *dev, unsigned int reg);
	bool (*writeable_noinc_reg)(struct device *dev, unsigned int reg);
	bool (*readable_noinc_reg)(struct device *dev, unsigned int reg);

	bool disable_locking;
	regmap_lock lock;
	regmap_unlock unlock;
	void *lock_arg;

	int (*reg_read)(void *context, unsigned int reg, unsigned int *val);
	int (*reg_write)(void *context, unsigned int reg, unsigned int val);
	int (*reg_update_bits)(void *context, unsigned int reg,
			       unsigned int mask, unsigned int val);
	/* Bulk read/write */
	int (*read)(void *context, const void *reg_buf, size_t reg_size,
		    void *val_buf, size_t val_size);
	int (*write)(void *context, const void *data, size_t count);
	size_t max_raw_read;
	size_t max_raw_write;

	bool fast_io;
	bool io_port;

	unsigned int max_register;
	const struct regmap_access_table *wr_table;
	const struct regmap_access_table *rd_table;
	const struct regmap_access_table *volatile_table;
	const struct regmap_access_table *precious_table;
	const struct regmap_access_table *wr_noinc_table;
	const struct regmap_access_table *rd_noinc_table;
	const struct reg_default *reg_defaults;
	unsigned int num_reg_defaults;
	enum regcache_type cache_type;
	const void *reg_defaults_raw;
	unsigned int num_reg_defaults_raw;

	unsigned long read_flag_mask;
	unsigned long write_flag_mask;
	bool zero_flag_mask;

	bool use_single_read;
	bool use_single_write;
	bool use_relaxed_mmio;
	bool can_multi_write;

	enum regmap_endian reg_format_endian;
	enum regmap_endian val_format_endian;

	const struct regmap_range_cfg *ranges;
	unsigned int num_ranges;

	bool use_hwlock;
	bool use_raw_spinlock;
	unsigned int hwlock_id;
	unsigned int hwlock_mode;

	bool can_sleep;
};

/**
 * struct regmap_range_cfg - Configuration for indirectly accessed or paged
 *                           registers.
 *
 * @name: Descriptive name for diagnostics
 *
 * @range_min: Address of the lowest register address in virtual range.
 * @range_max: Address of the highest register in virtual range.
 *
 * @selector_reg: Register with selector field.
 * @selector_mask: Bit mask for selector value.
 * @selector_shift: Bit shift for selector value.
 *
 * @window_start: Address of first (lowest) register in data window.
 * @window_len: Number of registers in data window.
 *
 * Registers, mapped to this virtual range, are accessed in two steps:
 *     1. page selector register update;
 *     2. access through data window registers.
 */
struct regmap_range_cfg {
	const char *name;

	/* Registers of virtual address range */
	unsigned int range_min;
	unsigned int range_max;

	/* Page selector for indirect addressing */
	unsigned int selector_reg;
	unsigned int selector_mask;
	int selector_shift;

	/* Data window (per each page) */
	unsigned int window_start;
	unsigned int window_len;
};

struct regmap_async;

typedef int (*regmap_hw_write)(void *context, const void *data,
			       size_t count);
typedef int (*regmap_hw_gather_write)(void *context,
				      const void *reg, size_t reg_len,
				      const void *val, size_t val_len);
typedef int (*regmap_hw_async_write)(void *context,
				     const void *reg, size_t reg_len,
				     const void *val, size_t val_len,
				     struct regmap_async *async);
typedef int (*regmap_hw_read)(void *context,
			      const void *reg_buf, size_t reg_size,
			      void *val_buf, size_t val_size);
typedef int (*regmap_hw_reg_read)(void *context, unsigned int reg,
				  unsigned int *val);
typedef int (*regmap_hw_reg_noinc_read)(void *context, unsigned int reg,
					void *val, size_t val_count);
typedef int (*regmap_hw_reg_write)(void *context, unsigned int reg,
				   unsigned int val);
typedef int (*regmap_hw_reg_noinc_write)(void *context, unsigned int reg,
					 const void *val, size_t val_count);
typedef int (*regmap_hw_reg_update_bits)(void *context, unsigned int reg,
					 unsigned int mask, unsigned int val);
typedef struct regmap_async *(*regmap_hw_async_alloc)(void);
typedef void (*regmap_hw_free_context)(void *context);

/**
 * struct regmap_bus - Description of a hardware bus for the register map
 *                     infrastructure.
 *
 * @fast_io: Register IO is fast. Use a spinlock instead of a mutex
 *	     to perform locking. This field is ignored if custom lock/unlock
 *	     functions are used (see fields lock/unlock of
 *	     struct regmap_config).
 * @write: Write operation.
 * @gather_write: Write operation with split register/value, return -ENOTSUPP
 *                if not implemented  on a given device.
 * @async_write: Write operation which completes asynchronously, optional and
 *               must serialise with respect to non-async I/O.
 * @reg_write: Write a single register value to the given register address. This
 *             write operation has to complete when returning from the function.
 * @reg_write_noinc: Write multiple register value to the same register. This
 *             write operation has to complete when returning from the function.
 * @reg_update_bits: Update bits operation to be used against volatile
 *                   registers, intended for devices supporting some mechanism
 *                   for setting clearing bits without having to
 *                   read/modify/write.
 * @read: Read operation.  Data is returned in the buffer used to transmit
 *         data.
 * @reg_read: Read a single register value from a given register address.
 * @free_context: Free context.
 * @async_alloc: Allocate a regmap_async() structure.
 * @read_flag_mask: Mask to be set in the top byte of the register when doing
 *                  a read.
 * @reg_format_endian_default: Default endianness for formatted register
 *     addresses. Used when the regmap_config specifies DEFAULT. If this is
 *     DEFAULT, BIG is assumed.
 * @val_format_endian_default: Default endianness for formatted register
 *     values. Used when the regmap_config specifies DEFAULT. If this is
 *     DEFAULT, BIG is assumed.
 * @max_raw_read: Max raw read size that can be used on the bus.
 * @max_raw_write: Max raw write size that can be used on the bus.
 * @free_on_exit: kfree this on exit of regmap
 */
struct regmap_bus {
	bool fast_io;
	regmap_hw_write write;
	regmap_hw_gather_write gather_write;
	regmap_hw_async_write async_write;
	regmap_hw_reg_write reg_write;
	regmap_hw_reg_noinc_write reg_noinc_write;
	regmap_hw_reg_update_bits reg_update_bits;
	regmap_hw_read read;
	regmap_hw_reg_read reg_read;
	regmap_hw_reg_noinc_read reg_noinc_read;
	regmap_hw_free_context free_context;
	regmap_hw_async_alloc async_alloc;
	u8 read_flag_mask;
	enum regmap_endian reg_format_endian_default;
	enum regmap_endian val_format_endian_default;
	size_t max_raw_read;
	size_t max_raw_write;
	bool free_on_exit;
};

/*
 * __regmap_init functions.
 *
 * These functions take a lock key and name parameter, and should not be called
 * directly. Instead, use the regmap_init macros that generate a key and name
 * for each call.
 */
struct regmap *__regmap_init(struct device *dev,
			     const struct regmap_bus *bus,
			     void *bus_context,
			     const struct regmap_config *config,
			     struct lock_class_key *lock_key,
			     const char *lock_name);
struct regmap *__regmap_init_i2c(struct i2c_client *i2c,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__regmap_init_mdio(struct mdio_device *mdio_dev,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__regmap_init_sccb(struct i2c_client *i2c,
				  const struct regmap_config *config,
				  struct lock_class_key *lock_key,
				  const char *lock_name);
struct regmap *__regmap_init_slimbus(struct slim_device *slimbus,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__regmap_init_spi(struct spi_device *dev,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__regmap_init_spmi_base(struct spmi_device *dev,
				       const struct regmap_config *config,
				       struct lock_class_key *lock_key,
				       const char *lock_name);
struct regmap *__regmap_init_spmi_ext(struct spmi_device *dev,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);
struct regmap *__regmap_init_w1(struct device *w1_dev,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__regmap_init_mmio_clk(struct device *dev, const char *clk_id,
				      void __iomem *regs,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);
struct regmap *__regmap_init_ac97(struct snd_ac97 *ac97,
				  const struct regmap_config *config,
				  struct lock_class_key *lock_key,
				  const char *lock_name);
struct regmap *__regmap_init_sdw(struct sdw_slave *sdw,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__regmap_init_sdw_mbq(struct sdw_slave *sdw,
				     const struct regmap_config *config,
				     struct lock_class_key *lock_key,
				     const char *lock_name);
struct regmap *__regmap_init_spi_avmm(struct spi_device *spi,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);

struct regmap *__devm_regmap_init(struct device *dev,
				  const struct regmap_bus *bus,
				  void *bus_context,
				  const struct regmap_config *config,
				  struct lock_class_key *lock_key,
				  const char *lock_name);
struct regmap *__devm_regmap_init_i2c(struct i2c_client *i2c,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);
struct regmap *__devm_regmap_init_mdio(struct mdio_device *mdio_dev,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);
struct regmap *__devm_regmap_init_sccb(struct i2c_client *i2c,
				       const struct regmap_config *config,
				       struct lock_class_key *lock_key,
				       const char *lock_name);
struct regmap *__devm_regmap_init_spi(struct spi_device *dev,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);
struct regmap *__devm_regmap_init_spmi_base(struct spmi_device *dev,
					    const struct regmap_config *config,
					    struct lock_class_key *lock_key,
					    const char *lock_name);
struct regmap *__devm_regmap_init_spmi_ext(struct spmi_device *dev,
					   const struct regmap_config *config,
					   struct lock_class_key *lock_key,
					   const char *lock_name);
struct regmap *__devm_regmap_init_w1(struct device *w1_dev,
				      const struct regmap_config *config,
				      struct lock_class_key *lock_key,
				      const char *lock_name);
struct regmap *__devm_regmap_init_mmio_clk(struct device *dev,
					   const char *clk_id,
					   void __iomem *regs,
					   const struct regmap_config *config,
					   struct lock_class_key *lock_key,
					   const char *lock_name);
struct regmap *__devm_regmap_init_ac97(struct snd_ac97 *ac97,
				       const struct regmap_config *config,
				       struct lock_class_key *lock_key,
				       const char *lock_name);
struct regmap *__devm_regmap_init_sdw(struct sdw_slave *sdw,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__devm_regmap_init_sdw_mbq(struct sdw_slave *sdw,
					  const struct regmap_config *config,
					  struct lock_class_key *lock_key,
					  const char *lock_name);
struct regmap *__devm_regmap_init_slimbus(struct slim_device *slimbus,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__devm_regmap_init_i3c(struct i3c_device *i3c,
				 const struct regmap_config *config,
				 struct lock_class_key *lock_key,
				 const char *lock_name);
struct regmap *__devm_regmap_init_spi_avmm(struct spi_device *spi,
					   const struct regmap_config *config,
					   struct lock_class_key *lock_key,
					   const char *lock_name);
/*
 * Wrapper for regmap_init macros to include a unique lockdep key and name
 * for each call. No-op if CONFIG_LOCKDEP is not set.
 *
 * @fn: Real function to call (in the form __[*_]regmap_init[_*])
 * @name: Config variable name (#config in the calling macro)
 **/
#ifdef CONFIG_LOCKDEP
#define __regmap_lockdep_wrapper(fn, name, ...)				\
(									\
	({								\
		static struct lock_class_key _key;			\
		fn(__VA_ARGS__, &_key,					\
			KBUILD_BASENAME ":"				\
			__stringify(__LINE__) ":"			\
			"(" name ")->lock");				\
	})								\
)
#else
#define __regmap_lockdep_wrapper(fn, name, ...) fn(__VA_ARGS__, NULL, NULL)
#endif

/**
 * regmap_init() - Initialise register map
 *
 * @dev: Device that will be interacted with
 * @bus: Bus-specific callbacks to use with device
 * @bus_context: Data passed to bus-specific callbacks
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.  This function should generally not be called
 * directly, it should be called by bus-specific init functions.
 */
#define regmap_init(dev, bus, bus_context, config)			\
	__regmap_lockdep_wrapper(__regmap_init, #config,		\
				dev, bus, bus_context, config)
int regmap_attach_dev(struct device *dev, struct regmap *map,
		      const struct regmap_config *config);

/**
 * regmap_init_i2c() - Initialise register map
 *
 * @i2c: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_i2c(i2c, config)					\
	__regmap_lockdep_wrapper(__regmap_init_i2c, #config,		\
				i2c, config)

/**
 * regmap_init_mdio() - Initialise register map
 *
 * @mdio_dev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_mdio(mdio_dev, config)				\
	__regmap_lockdep_wrapper(__regmap_init_mdio, #config,		\
				mdio_dev, config)

/**
 * regmap_init_sccb() - Initialise register map
 *
 * @i2c: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_sccb(i2c, config)					\
	__regmap_lockdep_wrapper(__regmap_init_sccb, #config,		\
				i2c, config)

/**
 * regmap_init_slimbus() - Initialise register map
 *
 * @slimbus: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_slimbus(slimbus, config)				\
	__regmap_lockdep_wrapper(__regmap_init_slimbus, #config,	\
				slimbus, config)

/**
 * regmap_init_spi() - Initialise register map
 *
 * @dev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_spi(dev, config)					\
	__regmap_lockdep_wrapper(__regmap_init_spi, #config,		\
				dev, config)

/**
 * regmap_init_spmi_base() - Create regmap for the Base register space
 *
 * @dev:	SPMI device that will be interacted with
 * @config:	Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_spmi_base(dev, config)				\
	__regmap_lockdep_wrapper(__regmap_init_spmi_base, #config,	\
				dev, config)

/**
 * regmap_init_spmi_ext() - Create regmap for Ext register space
 *
 * @dev:	Device that will be interacted with
 * @config:	Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_spmi_ext(dev, config)				\
	__regmap_lockdep_wrapper(__regmap_init_spmi_ext, #config,	\
				dev, config)

/**
 * regmap_init_w1() - Initialise register map
 *
 * @w1_dev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_w1(w1_dev, config)					\
	__regmap_lockdep_wrapper(__regmap_init_w1, #config,		\
				w1_dev, config)

/**
 * regmap_init_mmio_clk() - Initialise register map with register clock
 *
 * @dev: Device that will be interacted with
 * @clk_id: register clock consumer ID
 * @regs: Pointer to memory-mapped IO region
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_mmio_clk(dev, clk_id, regs, config)			\
	__regmap_lockdep_wrapper(__regmap_init_mmio_clk, #config,	\
				dev, clk_id, regs, config)

/**
 * regmap_init_mmio() - Initialise register map
 *
 * @dev: Device that will be interacted with
 * @regs: Pointer to memory-mapped IO region
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_mmio(dev, regs, config)		\
	regmap_init_mmio_clk(dev, NULL, regs, config)

/**
 * regmap_init_ac97() - Initialise AC'97 register map
 *
 * @ac97: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_ac97(ac97, config)					\
	__regmap_lockdep_wrapper(__regmap_init_ac97, #config,		\
				ac97, config)
bool regmap_ac97_default_volatile(struct device *dev, unsigned int reg);

/**
 * regmap_init_sdw() - Initialise register map
 *
 * @sdw: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_sdw(sdw, config)					\
	__regmap_lockdep_wrapper(__regmap_init_sdw, #config,		\
				sdw, config)

/**
 * regmap_init_sdw_mbq() - Initialise register map
 *
 * @sdw: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
#define regmap_init_sdw_mbq(sdw, config)					\
	__regmap_lockdep_wrapper(__regmap_init_sdw_mbq, #config,		\
				sdw, config)

/**
 * regmap_init_spi_avmm() - Initialize register map for Intel SPI Slave
 * to AVMM Bus Bridge
 *
 * @spi: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.
 */
#define regmap_init_spi_avmm(spi, config)					\
	__regmap_lockdep_wrapper(__regmap_init_spi_avmm, #config,		\
				 spi, config)

/**
 * devm_regmap_init() - Initialise managed register map
 *
 * @dev: Device that will be interacted with
 * @bus: Bus-specific callbacks to use with device
 * @bus_context: Data passed to bus-specific callbacks
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  This function should generally not be called
 * directly, it should be called by bus-specific init functions.  The
 * map will be automatically freed by the device management code.
 */
#define devm_regmap_init(dev, bus, bus_context, config)			\
	__regmap_lockdep_wrapper(__devm_regmap_init, #config,		\
				dev, bus, bus_context, config)

/**
 * devm_regmap_init_i2c() - Initialise managed register map
 *
 * @i2c: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_i2c(i2c, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_i2c, #config,	\
				i2c, config)

/**
 * devm_regmap_init_mdio() - Initialise managed register map
 *
 * @mdio_dev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_mdio(mdio_dev, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_mdio, #config,	\
				mdio_dev, config)

/**
 * devm_regmap_init_sccb() - Initialise managed register map
 *
 * @i2c: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_sccb(i2c, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_sccb, #config,	\
				i2c, config)

/**
 * devm_regmap_init_spi() - Initialise register map
 *
 * @dev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The map will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_spi(dev, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_spi, #config,	\
				dev, config)

/**
 * devm_regmap_init_spmi_base() - Create managed regmap for Base register space
 *
 * @dev:	SPMI device that will be interacted with
 * @config:	Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_spmi_base(dev, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_spmi_base, #config,	\
				dev, config)

/**
 * devm_regmap_init_spmi_ext() - Create managed regmap for Ext register space
 *
 * @dev:	SPMI device that will be interacted with
 * @config:	Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_spmi_ext(dev, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_spmi_ext, #config,	\
				dev, config)

/**
 * devm_regmap_init_w1() - Initialise managed register map
 *
 * @w1_dev: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_w1(w1_dev, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_w1, #config,	\
				w1_dev, config)
/**
 * devm_regmap_init_mmio_clk() - Initialise managed register map with clock
 *
 * @dev: Device that will be interacted with
 * @clk_id: register clock consumer ID
 * @regs: Pointer to memory-mapped IO region
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_mmio_clk(dev, clk_id, regs, config)		\
	__regmap_lockdep_wrapper(__devm_regmap_init_mmio_clk, #config,	\
				dev, clk_id, regs, config)

/**
 * devm_regmap_init_mmio() - Initialise managed register map
 *
 * @dev: Device that will be interacted with
 * @regs: Pointer to memory-mapped IO region
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_mmio(dev, regs, config)		\
	devm_regmap_init_mmio_clk(dev, NULL, regs, config)

/**
 * devm_regmap_init_ac97() - Initialise AC'97 register map
 *
 * @ac97: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_ac97(ac97, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_ac97, #config,	\
				ac97, config)

/**
 * devm_regmap_init_sdw() - Initialise managed register map
 *
 * @sdw: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap. The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_sdw(sdw, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_sdw, #config,	\
				sdw, config)

/**
 * devm_regmap_init_sdw_mbq() - Initialise managed register map
 *
 * @sdw: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap. The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_sdw_mbq(sdw, config)			\
	__regmap_lockdep_wrapper(__devm_regmap_init_sdw_mbq, #config,   \
				sdw, config)

/**
 * devm_regmap_init_slimbus() - Initialise managed register map
 *
 * @slimbus: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap. The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_slimbus(slimbus, config)			\
	__regmap_lockdep_wrapper(__devm_regmap_init_slimbus, #config,	\
				slimbus, config)

/**
 * devm_regmap_init_i3c() - Initialise managed register map
 *
 * @i3c: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The regmap will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_i3c(i3c, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_i3c, #config,	\
				i3c, config)

/**
 * devm_regmap_init_spi_avmm() - Initialize register map for Intel SPI Slave
 * to AVMM Bus Bridge
 *
 * @spi: Device that will be interacted with
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer
 * to a struct regmap.  The map will be automatically freed by the
 * device management code.
 */
#define devm_regmap_init_spi_avmm(spi, config)				\
	__regmap_lockdep_wrapper(__devm_regmap_init_spi_avmm, #config,	\
				 spi, config)

int regmap_mmio_attach_clk(struct regmap *map, struct clk *clk);
void regmap_mmio_detach_clk(struct regmap *map);
void regmap_exit(struct regmap *map);
int regmap_reinit_cache(struct regmap *map,
			const struct regmap_config *config);
struct regmap *dev_get_regmap(struct device *dev, const char *name);
struct device *regmap_get_device(struct regmap *map);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_write_async(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_raw_write(struct regmap *map, unsigned int reg,
		     const void *val, size_t val_len);
int regmap_noinc_write(struct regmap *map, unsigned int reg,
		     const void *val, size_t val_len);
int regmap_bulk_write(struct regmap *map, unsigned int reg, const void *val,
			size_t val_count);
int regmap_multi_reg_write(struct regmap *map, const struct reg_sequence *regs,
			int num_regs);
int regmap_multi_reg_write_bypassed(struct regmap *map,
				    const struct reg_sequence *regs,
				    int num_regs);
int regmap_raw_write_async(struct regmap *map, unsigned int reg,
			   const void *val, size_t val_len);
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_raw_read(struct regmap *map, unsigned int reg,
		    void *val, size_t val_len);
int regmap_noinc_read(struct regmap *map, unsigned int reg,
		      void *val, size_t val_len);
int regmap_bulk_read(struct regmap *map, unsigned int reg, void *val,
		     size_t val_count);
int regmap_update_bits_base(struct regmap *map, unsigned int reg,
			    unsigned int mask, unsigned int val,
			    bool *change, bool async, bool force);

static inline int regmap_update_bits(struct regmap *map, unsigned int reg,
				     unsigned int mask, unsigned int val)
{
	return regmap_update_bits_base(map, reg, mask, val, NULL, false, false);
}

static inline int regmap_update_bits_async(struct regmap *map, unsigned int reg,
					   unsigned int mask, unsigned int val)
{
	return regmap_update_bits_base(map, reg, mask, val, NULL, true, false);
}

static inline int regmap_update_bits_check(struct regmap *map, unsigned int reg,
					   unsigned int mask, unsigned int val,
					   bool *change)
{
	return regmap_update_bits_base(map, reg, mask, val,
				       change, false, false);
}

static inline int
regmap_update_bits_check_async(struct regmap *map, unsigned int reg,
			       unsigned int mask, unsigned int val,
			       bool *change)
{
	return regmap_update_bits_base(map, reg, mask, val,
				       change, true, false);
}

static inline int regmap_write_bits(struct regmap *map, unsigned int reg,
				    unsigned int mask, unsigned int val)
{
	return regmap_update_bits_base(map, reg, mask, val, NULL, false, true);
}

int regmap_get_val_bytes(struct regmap *map);
int regmap_get_max_register(struct regmap *map);
int regmap_get_reg_stride(struct regmap *map);
int regmap_async_complete(struct regmap *map);
bool regmap_can_raw_write(struct regmap *map);
size_t regmap_get_raw_read_max(struct regmap *map);
size_t regmap_get_raw_write_max(struct regmap *map);

int regcache_sync(struct regmap *map);
int regcache_sync_region(struct regmap *map, unsigned int min,
			 unsigned int max);
int regcache_drop_region(struct regmap *map, unsigned int min,
			 unsigned int max);
void regcache_cache_only(struct regmap *map, bool enable);
void regcache_cache_bypass(struct regmap *map, bool enable);
void regcache_mark_dirty(struct regmap *map);

bool regmap_check_range_table(struct regmap *map, unsigned int reg,
			      const struct regmap_access_table *table);

int regmap_register_patch(struct regmap *map, const struct reg_sequence *regs,
			  int num_regs);
int regmap_parse_val(struct regmap *map, const void *buf,
				unsigned int *val);

static inline bool regmap_reg_in_range(unsigned int reg,
				       const struct regmap_range *range)
{
	return reg >= range->range_min && reg <= range->range_max;
}

bool regmap_reg_in_ranges(unsigned int reg,
			  const struct regmap_range *ranges,
			  unsigned int nranges);

static inline int regmap_set_bits(struct regmap *map,
				  unsigned int reg, unsigned int bits)
{
	return regmap_update_bits_base(map, reg, bits, bits,
				       NULL, false, false);
}

static inline int regmap_clear_bits(struct regmap *map,
				    unsigned int reg, unsigned int bits)
{
	return regmap_update_bits_base(map, reg, bits, 0, NULL, false, false);
}

int regmap_test_bits(struct regmap *map, unsigned int reg, unsigned int bits);

/**
 * struct reg_field - Description of an register field
 *
 * @reg: Offset of the register within the regmap bank
 * @lsb: lsb of the register field.
 * @msb: msb of the register field.
 * @id_size: port size if it has some ports
 * @id_offset: address offset for each ports
 */
struct reg_field {
	unsigned int reg;
	unsigned int lsb;
	unsigned int msb;
	unsigned int id_size;
	unsigned int id_offset;
};

#define REG_FIELD(_reg, _lsb, _msb) {		\
				.reg = _reg,	\
				.lsb = _lsb,	\
				.msb = _msb,	\
				}

#define REG_FIELD_ID(_reg, _lsb, _msb, _size, _offset) {	\
				.reg = _reg,			\
				.lsb = _lsb,			\
				.msb = _msb,			\
				.id_size = _size,		\
				.id_offset = _offset,		\
				}

struct regmap_field *regmap_field_alloc(struct regmap *regmap,
		struct reg_field reg_field);
void regmap_field_free(struct regmap_field *field);

struct regmap_field *devm_regmap_field_alloc(struct device *dev,
		struct regmap *regmap, struct reg_field reg_field);
void devm_regmap_field_free(struct device *dev,	struct regmap_field *field);

int regmap_field_bulk_alloc(struct regmap *regmap,
			     struct regmap_field **rm_field,
			     const struct reg_field *reg_field,
			     int num_fields);
void regmap_field_bulk_free(struct regmap_field *field);
int devm_regmap_field_bulk_alloc(struct device *dev, struct regmap *regmap,
				 struct regmap_field **field,
				 const struct reg_field *reg_field,
				 int num_fields);
void devm_regmap_field_bulk_free(struct device *dev,
				 struct regmap_field *field);

int regmap_field_read(struct regmap_field *field, unsigned int *val);
int regmap_field_update_bits_base(struct regmap_field *field,
				  unsigned int mask, unsigned int val,
				  bool *change, bool async, bool force);
int regmap_fields_read(struct regmap_field *field, unsigned int id,
		       unsigned int *val);
int regmap_fields_update_bits_base(struct regmap_field *field,  unsigned int id,
				   unsigned int mask, unsigned int val,
				   bool *change, bool async, bool force);

static inline int regmap_field_write(struct regmap_field *field,
				     unsigned int val)
{
	return regmap_field_update_bits_base(field, ~0, val,
					     NULL, false, false);
}

static inline int regmap_field_force_write(struct regmap_field *field,
					   unsigned int val)
{
	return regmap_field_update_bits_base(field, ~0, val, NULL, false, true);
}

static inline int regmap_field_update_bits(struct regmap_field *field,
					   unsigned int mask, unsigned int val)
{
	return regmap_field_update_bits_base(field, mask, val,
					     NULL, false, false);
}

static inline int regmap_field_set_bits(struct regmap_field *field,
					unsigned int bits)
{
	return regmap_field_update_bits_base(field, bits, bits, NULL, false,
					     false);
}

static inline int regmap_field_clear_bits(struct regmap_field *field,
					  unsigned int bits)
{
	return regmap_field_update_bits_base(field, bits, 0, NULL, false,
					     false);
}

int regmap_field_test_bits(struct regmap_field *field, unsigned int bits);

static inline int
regmap_field_force_update_bits(struct regmap_field *field,
			       unsigned int mask, unsigned int val)
{
	return regmap_field_update_bits_base(field, mask, val,
					     NULL, false, true);
}

static inline int regmap_fields_write(struct regmap_field *field,
				      unsigned int id, unsigned int val)
{
	return regmap_fields_update_bits_base(field, id, ~0, val,
					      NULL, false, false);
}

static inline int regmap_fields_force_write(struct regmap_field *field,
					    unsigned int id, unsigned int val)
{
	return regmap_fields_update_bits_base(field, id, ~0, val,
					      NULL, false, true);
}

static inline int
regmap_fields_update_bits(struct regmap_field *field, unsigned int id,
			  unsigned int mask, unsigned int val)
{
	return regmap_fields_update_bits_base(field, id, mask, val,
					      NULL, false, false);
}

static inline int
regmap_fields_force_update_bits(struct regmap_field *field, unsigned int id,
				unsigned int mask, unsigned int val)
{
	return regmap_fields_update_bits_base(field, id, mask, val,
					      NULL, false, true);
}

/**
 * struct regmap_irq_type - IRQ type definitions.
 *
 * @type_reg_offset: Offset register for the irq type setting.
 * @type_rising_val: Register value to configure RISING type irq.
 * @type_falling_val: Register value to configure FALLING type irq.
 * @type_level_low_val: Register value to configure LEVEL_LOW type irq.
 * @type_level_high_val: Register value to configure LEVEL_HIGH type irq.
 * @types_supported: logical OR of IRQ_TYPE_* flags indicating supported types.
 */
struct regmap_irq_type {
	unsigned int type_reg_offset;
	unsigned int type_reg_mask;
	unsigned int type_rising_val;
	unsigned int type_falling_val;
	unsigned int type_level_low_val;
	unsigned int type_level_high_val;
	unsigned int types_supported;
};

/**
 * struct regmap_irq - Description of an IRQ for the generic regmap irq_chip.
 *
 * @reg_offset: Offset of the status/mask register within the bank
 * @mask:       Mask used to flag/control the register.
 * @type:	IRQ trigger type setting details if supported.
 */
struct regmap_irq {
	unsigned int reg_offset;
	unsigned int mask;
	struct regmap_irq_type type;
};

#define REGMAP_IRQ_REG(_irq, _off, _mask)		\
	[_irq] = { .reg_offset = (_off), .mask = (_mask) }

#define REGMAP_IRQ_REG_LINE(_id, _reg_bits) \
	[_id] = {				\
		.mask = BIT((_id) % (_reg_bits)),	\
		.reg_offset = (_id) / (_reg_bits),	\
	}

#define REGMAP_IRQ_MAIN_REG_OFFSET(arr)				\
	{ .num_regs = ARRAY_SIZE((arr)), .offset = &(arr)[0] }

struct regmap_irq_sub_irq_map {
	unsigned int num_regs;
	unsigned int *offset;
};

struct regmap_irq_chip_data;

/**
 * struct regmap_irq_chip - Description of a generic regmap irq_chip.
 *
 * @name:        Descriptive name for IRQ controller.
 *
 * @main_status: Base main status register address. For chips which have
 *		 interrupts arranged in separate sub-irq blocks with own IRQ
 *		 registers and which have a main IRQ registers indicating
 *		 sub-irq blocks with unhandled interrupts. For such chips fill
 *		 sub-irq register information in status_base, mask_base and
 *		 ack_base.
 * @num_main_status_bits: Should be given to chips where number of meaningfull
 *			  main status bits differs from num_regs.
 * @sub_reg_offsets: arrays of mappings from main register bits to sub irq
 *		     registers. First item in array describes the registers
 *		     for first main status bit. Second array for second bit etc.
 *		     Offset is given as sub register status offset to
 *		     status_base. Should contain num_regs arrays.
 *		     Can be provided for chips with more complex mapping than
 *		     1.st bit to 1.st sub-reg, 2.nd bit to 2.nd sub-reg, ...
 *		     When used with not_fixed_stride, each one-element array
 *		     member contains offset calculated as address from each
 *		     peripheral to first peripheral.
 * @num_main_regs: Number of 'main status' irq registers for chips which have
 *		   main_status set.
 *
 * @status_base: Base status register address.
 * @mask_base:   Base mask register address. Mask bits are set to 1 when an
 *               interrupt is masked, 0 when unmasked.
 * @unmask_base:  Base unmask register address. Unmask bits are set to 1 when
 *                an interrupt is unmasked and 0 when masked.
 * @ack_base:    Base ack address. If zero then the chip is clear on read.
 *               Using zero value is possible with @use_ack bit.
 * @wake_base:   Base address for wake enables.  If zero unsupported.
 * @type_base:   Base address for irq type.  If zero unsupported.  Deprecated,
 *		 use @config_base instead.
 * @virt_reg_base:   Base addresses for extra config regs. Deprecated, use
 *		     @config_base instead.
 * @config_base: Base address for IRQ type config regs. If null unsupported.
 * @irq_reg_stride:  Stride to use for chips where registers are not contiguous.
 * @init_ack_masked: Ack all masked interrupts once during initalization.
 * @mask_invert: Inverted mask register: cleared bits are masked out.
 *		 Deprecated; prefer describing an inverted mask register as
 *		 an unmask register.
 * @mask_unmask_non_inverted: Controls mask bit inversion for chips that set
 *	both @mask_base and @unmask_base. If false, mask and unmask bits are
 *	inverted (which is deprecated behavior); if true, bits will not be
 *	inverted and the registers keep their normal behavior. Note that if
 *	you use only one of @mask_base or @unmask_base, this flag has no
 *	effect and is unnecessary. Any new drivers that set both @mask_base
 *	and @unmask_base should set this to true to avoid relying on the
 *	deprecated behavior.
 * @use_ack:     Use @ack register even if it is zero.
 * @ack_invert:  Inverted ack register: cleared bits for ack.
 * @clear_ack:  Use this to set 1 and 0 or vice-versa to clear interrupts.
 * @wake_invert: Inverted wake register: cleared bits are wake enabled.
 * @type_invert: Invert the type flags. Deprecated, use config registers
 *		 instead.
 * @type_in_mask: Use the mask registers for controlling irq type. Use this if
 *		  the hardware provides separate bits for rising/falling edge
 *		  or low/high level interrupts and they should be combined into
 *		  a single logical interrupt. Use &struct regmap_irq_type data
 *		  to define the mask bit for each irq type.
 * @clear_on_unmask: For chips with interrupts cleared on read: read the status
 *                   registers before unmasking interrupts to clear any bits
 *                   set when they were masked.
 * @not_fixed_stride: Used when chip peripherals are not laid out with fixed
 *		      stride. Must be used with sub_reg_offsets containing the
 *		      offsets to each peripheral. Deprecated; the same thing
 *		      can be accomplished with a @get_irq_reg callback, without
 *		      the need for a @sub_reg_offsets table.
 * @status_invert: Inverted status register: cleared bits are active interrupts.
 * @runtime_pm:  Hold a runtime PM lock on the device when accessing it.
 *
 * @num_regs:    Number of registers in each control bank.
 * @irqs:        Descriptors for individual IRQs.  Interrupt numbers are
 *               assigned based on the index in the array of the interrupt.
 * @num_irqs:    Number of descriptors.
 * @num_type_reg:    Number of type registers. Deprecated, use config registers
 *		     instead.
 * @num_virt_regs:   Number of non-standard irq configuration registers.
 *		     If zero unsupported. Deprecated, use config registers
 *		     instead.
 * @num_config_bases:	Number of config base registers.
 * @num_config_regs:	Number of config registers for each config base register.
 * @handle_pre_irq:  Driver specific callback to handle interrupt from device
 *		     before regmap_irq_handler process the interrupts.
 * @handle_post_irq: Driver specific callback to handle interrupt from device
 *		     after handling the interrupts in regmap_irq_handler().
 * @set_type_virt:   Driver specific callback to extend regmap_irq_set_type()
 *		     and configure virt regs. Deprecated, use @set_type_config
 *		     callback and config registers instead.
 * @set_type_config: Callback used for configuring irq types.
 * @get_irq_reg: Callback for mapping (base register, index) pairs to register
 *		 addresses. The base register will be one of @status_base,
 *		 @mask_base, etc., @main_status, or any of @config_base.
 *		 The index will be in the range [0, num_main_regs[ for the
 *		 main status base, [0, num_type_settings[ for any config
 *		 register base, and [0, num_regs[ for any other base.
 *		 If unspecified then regmap_irq_get_irq_reg_linear() is used.
 * @irq_drv_data:    Driver specific IRQ data which is passed as parameter when
 *		     driver specific pre/post interrupt handler is called.
 *
 * This is not intended to handle every possible interrupt controller, but
 * it should handle a substantial proportion of those that are found in the
 * wild.
 */
struct regmap_irq_chip {
	const char *name;

	unsigned int main_status;
	unsigned int num_main_status_bits;
	struct regmap_irq_sub_irq_map *sub_reg_offsets;
	int num_main_regs;

	unsigned int status_base;
	unsigned int mask_base;
	unsigned int unmask_base;
	unsigned int ack_base;
	unsigned int wake_base;
	unsigned int type_base;
	unsigned int *virt_reg_base;
	const unsigned int *config_base;
	unsigned int irq_reg_stride;
	unsigned int init_ack_masked:1;
	unsigned int mask_invert:1;
	unsigned int mask_unmask_non_inverted:1;
	unsigned int use_ack:1;
	unsigned int ack_invert:1;
	unsigned int clear_ack:1;
	unsigned int wake_invert:1;
	unsigned int runtime_pm:1;
	unsigned int type_invert:1;
	unsigned int type_in_mask:1;
	unsigned int clear_on_unmask:1;
	unsigned int not_fixed_stride:1;
	unsigned int status_invert:1;

	int num_regs;

	const struct regmap_irq *irqs;
	int num_irqs;

	int num_type_reg;
	int num_virt_regs;
	int num_config_bases;
	int num_config_regs;

	int (*handle_pre_irq)(void *irq_drv_data);
	int (*handle_post_irq)(void *irq_drv_data);
	int (*set_type_virt)(unsigned int **buf, unsigned int type,
			     unsigned long hwirq, int reg);
	int (*set_type_config)(unsigned int **buf, unsigned int type,
			       const struct regmap_irq *irq_data, int idx);
	unsigned int (*get_irq_reg)(struct regmap_irq_chip_data *data,
				    unsigned int base, int index);
	void *irq_drv_data;
};

unsigned int regmap_irq_get_irq_reg_linear(struct regmap_irq_chip_data *data,
					   unsigned int base, int index);
int regmap_irq_set_type_config_simple(unsigned int **buf, unsigned int type,
				      const struct regmap_irq *irq_data, int idx);

int regmap_add_irq_chip(struct regmap *map, int irq, int irq_flags,
			int irq_base, const struct regmap_irq_chip *chip,
			struct regmap_irq_chip_data **data);
int regmap_add_irq_chip_fwnode(struct fwnode_handle *fwnode,
			       struct regmap *map, int irq,
			       int irq_flags, int irq_base,
			       const struct regmap_irq_chip *chip,
			       struct regmap_irq_chip_data **data);
void regmap_del_irq_chip(int irq, struct regmap_irq_chip_data *data);

int devm_regmap_add_irq_chip(struct device *dev, struct regmap *map, int irq,
			     int irq_flags, int irq_base,
			     const struct regmap_irq_chip *chip,
			     struct regmap_irq_chip_data **data);
int devm_regmap_add_irq_chip_fwnode(struct device *dev,
				    struct fwnode_handle *fwnode,
				    struct regmap *map, int irq,
				    int irq_flags, int irq_base,
				    const struct regmap_irq_chip *chip,
				    struct regmap_irq_chip_data **data);
void devm_regmap_del_irq_chip(struct device *dev, int irq,
			      struct regmap_irq_chip_data *data);

int regmap_irq_chip_get_base(struct regmap_irq_chip_data *data);
int regmap_irq_get_virq(struct regmap_irq_chip_data *data, int irq);
struct irq_domain *regmap_irq_get_domain(struct regmap_irq_chip_data *data);

#else

/*
 * These stubs should only ever be called by generic code which has
 * regmap based facilities, if they ever get called at runtime
 * something is going wrong and something probably needs to select
 * REGMAP.
 */

static inline int regmap_write(struct regmap *map, unsigned int reg,
			       unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_write_async(struct regmap *map, unsigned int reg,
				     unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_raw_write(struct regmap *map, unsigned int reg,
				   const void *val, size_t val_len)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_raw_write_async(struct regmap *map, unsigned int reg,
					 const void *val, size_t val_len)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_noinc_write(struct regmap *map, unsigned int reg,
				    const void *val, size_t val_len)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_bulk_write(struct regmap *map, unsigned int reg,
				    const void *val, size_t val_count)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_read(struct regmap *map, unsigned int reg,
			      unsigned int *val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_raw_read(struct regmap *map, unsigned int reg,
				  void *val, size_t val_len)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_noinc_read(struct regmap *map, unsigned int reg,
				    void *val, size_t val_len)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_bulk_read(struct regmap *map, unsigned int reg,
				   void *val, size_t val_count)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_update_bits_base(struct regmap *map, unsigned int reg,
					  unsigned int mask, unsigned int val,
					  bool *change, bool async, bool force)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_set_bits(struct regmap *map,
				  unsigned int reg, unsigned int bits)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_clear_bits(struct regmap *map,
				    unsigned int reg, unsigned int bits)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_test_bits(struct regmap *map,
				   unsigned int reg, unsigned int bits)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_field_update_bits_base(struct regmap_field *field,
					unsigned int mask, unsigned int val,
					bool *change, bool async, bool force)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_fields_update_bits_base(struct regmap_field *field,
				   unsigned int id,
				   unsigned int mask, unsigned int val,
				   bool *change, bool async, bool force)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_update_bits(struct regmap *map, unsigned int reg,
				     unsigned int mask, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_update_bits_async(struct regmap *map, unsigned int reg,
					   unsigned int mask, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_update_bits_check(struct regmap *map, unsigned int reg,
					   unsigned int mask, unsigned int val,
					   bool *change)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int
regmap_update_bits_check_async(struct regmap *map, unsigned int reg,
			       unsigned int mask, unsigned int val,
			       bool *change)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_write_bits(struct regmap *map, unsigned int reg,
				    unsigned int mask, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_field_write(struct regmap_field *field,
				     unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_field_force_write(struct regmap_field *field,
					   unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_field_update_bits(struct regmap_field *field,
					   unsigned int mask, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int
regmap_field_force_update_bits(struct regmap_field *field,
			       unsigned int mask, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_field_set_bits(struct regmap_field *field,
					unsigned int bits)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_field_clear_bits(struct regmap_field *field,
					  unsigned int bits)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_field_test_bits(struct regmap_field *field,
					 unsigned int bits)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_fields_write(struct regmap_field *field,
				      unsigned int id, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_fields_force_write(struct regmap_field *field,
					    unsigned int id, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int
regmap_fields_update_bits(struct regmap_field *field, unsigned int id,
			  unsigned int mask, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int
regmap_fields_force_update_bits(struct regmap_field *field, unsigned int id,
				unsigned int mask, unsigned int val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_get_val_bytes(struct regmap *map)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_get_max_register(struct regmap *map)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_get_reg_stride(struct regmap *map)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regcache_sync(struct regmap *map)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regcache_sync_region(struct regmap *map, unsigned int min,
				       unsigned int max)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regcache_drop_region(struct regmap *map, unsigned int min,
				       unsigned int max)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline void regcache_cache_only(struct regmap *map, bool enable)
{
	WARN_ONCE(1, "regmap API is disabled");
}

static inline void regcache_cache_bypass(struct regmap *map, bool enable)
{
	WARN_ONCE(1, "regmap API is disabled");
}

static inline void regcache_mark_dirty(struct regmap *map)
{
	WARN_ONCE(1, "regmap API is disabled");
}

static inline void regmap_async_complete(struct regmap *map)
{
	WARN_ONCE(1, "regmap API is disabled");
}

static inline int regmap_register_patch(struct regmap *map,
					const struct reg_sequence *regs,
					int num_regs)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_parse_val(struct regmap *map, const void *buf,
				unsigned int *val)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline struct regmap *dev_get_regmap(struct device *dev,
					    const char *name)
{
	return NULL;
}

static inline struct device *regmap_get_device(struct regmap *map)
{
	WARN_ONCE(1, "regmap API is disabled");
	return NULL;
}

#endif

#endif
