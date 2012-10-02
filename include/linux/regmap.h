#ifndef __LINUX_REGMAP_H
#define __LINUX_REGMAP_H

/*
 * Register map access API
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/list.h>
#include <linux/rbtree.h>

struct module;
struct device;
struct i2c_client;
struct spi_device;
struct regmap;
struct regmap_range_cfg;

/* An enum of all the supported cache types */
enum regcache_type {
	REGCACHE_NONE,
	REGCACHE_RBTREE,
	REGCACHE_COMPRESSED
};

/**
 * Default value for a register.  We use an array of structs rather
 * than a simple array as many modern devices have very sparse
 * register maps.
 *
 * @reg: Register address.
 * @def: Register default value.
 */
struct reg_default {
	unsigned int reg;
	unsigned int def;
};

#ifdef CONFIG_REGMAP

enum regmap_endian {
	/* Unspecified -> 0 -> Backwards compatible default */
	REGMAP_ENDIAN_DEFAULT = 0,
	REGMAP_ENDIAN_BIG,
	REGMAP_ENDIAN_LITTLE,
	REGMAP_ENDIAN_NATIVE,
};

/**
 * Configuration for the register map of a device.
 *
 * @name: Optional name of the regmap. Useful when a device has multiple
 *        register regions.
 *
 * @reg_bits: Number of bits in a register address, mandatory.
 * @reg_stride: The register address stride. Valid register addresses are a
 *              multiple of this value. If set to 0, a value of 1 will be
 *              used.
 * @pad_bits: Number of bits of padding between register and value.
 * @val_bits: Number of bits in a register value, mandatory.
 *
 * @writeable_reg: Optional callback returning true if the register
 *                 can be written to.
 * @readable_reg: Optional callback returning true if the register
 *                can be read from.
 * @volatile_reg: Optional callback returning true if the register
 *                value can't be cached.
 * @precious_reg: Optional callback returning true if the rgister
 *                should not be read outside of a call from the driver
 *                (eg, a clear on read interrupt status register).
 *
 * @max_register: Optional, specifies the maximum valid register index.
 * @reg_defaults: Power on reset values for registers (for use with
 *                register cache support).
 * @num_reg_defaults: Number of elements in reg_defaults.
 *
 * @read_flag_mask: Mask to be set in the top byte of the register when doing
 *                  a read.
 * @write_flag_mask: Mask to be set in the top byte of the register when doing
 *                   a write. If both read_flag_mask and write_flag_mask are
 *                   empty the regmap_bus default masks are used.
 * @use_single_rw: If set, converts the bulk read and write operations into
 *		    a series of single read and write operations. This is useful
 *		    for device that does not support bulk read and write.
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
 */
struct regmap_config {
	const char *name;

	int reg_bits;
	int reg_stride;
	int pad_bits;
	int val_bits;

	bool (*writeable_reg)(struct device *dev, unsigned int reg);
	bool (*readable_reg)(struct device *dev, unsigned int reg);
	bool (*volatile_reg)(struct device *dev, unsigned int reg);
	bool (*precious_reg)(struct device *dev, unsigned int reg);

	unsigned int max_register;
	const struct reg_default *reg_defaults;
	unsigned int num_reg_defaults;
	enum regcache_type cache_type;
	const void *reg_defaults_raw;
	unsigned int num_reg_defaults_raw;

	u8 read_flag_mask;
	u8 write_flag_mask;

	bool use_single_rw;

	enum regmap_endian reg_format_endian;
	enum regmap_endian val_format_endian;

	const struct regmap_range_cfg *ranges;
	unsigned int num_ranges;
};

/**
 * Configuration for indirectly accessed or paged registers.
 * Registers, mapped to this virtual range, are accessed in two steps:
 *     1. page selector register update;
 *     2. access through data window registers.
 *
 * @range_min: Address of the lowest register address in virtual range.
 * @range_max: Address of the highest register in virtual range.
 *
 * @page_sel_reg: Register with selector field.
 * @page_sel_mask: Bit shift for selector value.
 * @page_sel_shift: Bit mask for selector value.
 *
 * @window_start: Address of first (lowest) register in data window.
 * @window_len: Number of registers in data window.
 */
struct regmap_range_cfg {
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

typedef int (*regmap_hw_write)(void *context, const void *data,
			       size_t count);
typedef int (*regmap_hw_gather_write)(void *context,
				      const void *reg, size_t reg_len,
				      const void *val, size_t val_len);
typedef int (*regmap_hw_read)(void *context,
			      const void *reg_buf, size_t reg_size,
			      void *val_buf, size_t val_size);
typedef void (*regmap_hw_free_context)(void *context);

/**
 * Description of a hardware bus for the register map infrastructure.
 *
 * @fast_io: Register IO is fast. Use a spinlock instead of a mutex
 *           to perform locking.
 * @write: Write operation.
 * @gather_write: Write operation with split register/value, return -ENOTSUPP
 *                if not implemented  on a given device.
 * @read: Read operation.  Data is returned in the buffer used to transmit
 *         data.
 * @read_flag_mask: Mask to be set in the top byte of the register when doing
 *                  a read.
 * @reg_format_endian_default: Default endianness for formatted register
 *     addresses. Used when the regmap_config specifies DEFAULT. If this is
 *     DEFAULT, BIG is assumed.
 * @val_format_endian_default: Default endianness for formatted register
 *     values. Used when the regmap_config specifies DEFAULT. If this is
 *     DEFAULT, BIG is assumed.
 */
struct regmap_bus {
	bool fast_io;
	regmap_hw_write write;
	regmap_hw_gather_write gather_write;
	regmap_hw_read read;
	regmap_hw_free_context free_context;
	u8 read_flag_mask;
	enum regmap_endian reg_format_endian_default;
	enum regmap_endian val_format_endian_default;
};

struct regmap *regmap_init(struct device *dev,
			   const struct regmap_bus *bus,
			   void *bus_context,
			   const struct regmap_config *config);
struct regmap *regmap_init_i2c(struct i2c_client *i2c,
			       const struct regmap_config *config);
struct regmap *regmap_init_spi(struct spi_device *dev,
			       const struct regmap_config *config);
struct regmap *regmap_init_mmio(struct device *dev,
				void __iomem *regs,
				const struct regmap_config *config);

struct regmap *devm_regmap_init(struct device *dev,
				const struct regmap_bus *bus,
				void *bus_context,
				const struct regmap_config *config);
struct regmap *devm_regmap_init_i2c(struct i2c_client *i2c,
				    const struct regmap_config *config);
struct regmap *devm_regmap_init_spi(struct spi_device *dev,
				    const struct regmap_config *config);
struct regmap *devm_regmap_init_mmio(struct device *dev,
				     void __iomem *regs,
				     const struct regmap_config *config);

void regmap_exit(struct regmap *map);
int regmap_reinit_cache(struct regmap *map,
			const struct regmap_config *config);
struct regmap *dev_get_regmap(struct device *dev, const char *name);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_raw_write(struct regmap *map, unsigned int reg,
		     const void *val, size_t val_len);
int regmap_bulk_write(struct regmap *map, unsigned int reg, const void *val,
			size_t val_count);
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_raw_read(struct regmap *map, unsigned int reg,
		    void *val, size_t val_len);
int regmap_bulk_read(struct regmap *map, unsigned int reg, void *val,
		     size_t val_count);
int regmap_update_bits(struct regmap *map, unsigned int reg,
		       unsigned int mask, unsigned int val);
int regmap_update_bits_check(struct regmap *map, unsigned int reg,
			     unsigned int mask, unsigned int val,
			     bool *change);
int regmap_get_val_bytes(struct regmap *map);

int regcache_sync(struct regmap *map);
int regcache_sync_region(struct regmap *map, unsigned int min,
			 unsigned int max);
void regcache_cache_only(struct regmap *map, bool enable);
void regcache_cache_bypass(struct regmap *map, bool enable);
void regcache_mark_dirty(struct regmap *map);

int regmap_register_patch(struct regmap *map, const struct reg_default *regs,
			  int num_regs);

/**
 * Description of an IRQ for the generic regmap irq_chip.
 *
 * @reg_offset: Offset of the status/mask register within the bank
 * @mask:       Mask used to flag/control the register.
 */
struct regmap_irq {
	unsigned int reg_offset;
	unsigned int mask;
};

/**
 * Description of a generic regmap irq_chip.  This is not intended to
 * handle every possible interrupt controller, but it should handle a
 * substantial proportion of those that are found in the wild.
 *
 * @name:        Descriptive name for IRQ controller.
 *
 * @status_base: Base status register address.
 * @mask_base:   Base mask register address.
 * @ack_base:    Base ack address.  If zero then the chip is clear on read.
 * @wake_base:   Base address for wake enables.  If zero unsupported.
 * @irq_reg_stride:  Stride to use for chips where registers are not contiguous.
 * @runtime_pm:  Hold a runtime PM lock on the device when accessing it.
 *
 * @num_regs:    Number of registers in each control bank.
 * @irqs:        Descriptors for individual IRQs.  Interrupt numbers are
 *               assigned based on the index in the array of the interrupt.
 * @num_irqs:    Number of descriptors.
 */
struct regmap_irq_chip {
	const char *name;

	unsigned int status_base;
	unsigned int mask_base;
	unsigned int ack_base;
	unsigned int wake_base;
	unsigned int irq_reg_stride;
	unsigned int mask_invert;
	bool runtime_pm;

	int num_regs;

	const struct regmap_irq *irqs;
	int num_irqs;
};

struct regmap_irq_chip_data;

int regmap_add_irq_chip(struct regmap *map, int irq, int irq_flags,
			int irq_base, const struct regmap_irq_chip *chip,
			struct regmap_irq_chip_data **data);
void regmap_del_irq_chip(int irq, struct regmap_irq_chip_data *data);
int regmap_irq_chip_get_base(struct regmap_irq_chip_data *data);
int regmap_irq_get_virq(struct regmap_irq_chip_data *data, int irq);

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

static inline int regmap_raw_write(struct regmap *map, unsigned int reg,
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

static inline int regmap_bulk_read(struct regmap *map, unsigned int reg,
				   void *val, size_t val_count)
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

static inline int regmap_update_bits_check(struct regmap *map,
					   unsigned int reg,
					   unsigned int mask, unsigned int val,
					   bool *change)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline int regmap_get_val_bytes(struct regmap *map)
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

static inline int regmap_register_patch(struct regmap *map,
					const struct reg_default *regs,
					int num_regs)
{
	WARN_ONCE(1, "regmap API is disabled");
	return -EINVAL;
}

static inline struct regmap *dev_get_regmap(struct device *dev,
					    const char *name)
{
	return NULL;
}

#endif

#endif
