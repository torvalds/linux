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

struct module;
struct device;
struct i2c_client;
struct spi_device;
struct regmap;

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

/**
 * Configuration for the register map of a device.
 *
 * @reg_bits: Number of bits in a register address, mandatory.
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
 *
 * @cache_type: The actual cache type.
 * @reg_defaults_raw: Power on reset values for registers (for use with
 *                    register cache support).
 * @num_reg_defaults_raw: Number of elements in reg_defaults_raw.
 */
struct regmap_config {
	int reg_bits;
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
};

typedef int (*regmap_hw_write)(struct device *dev, const void *data,
			       size_t count);
typedef int (*regmap_hw_gather_write)(struct device *dev,
				      const void *reg, size_t reg_len,
				      const void *val, size_t val_len);
typedef int (*regmap_hw_read)(struct device *dev,
			      const void *reg_buf, size_t reg_size,
			      void *val_buf, size_t val_size);

/**
 * Description of a hardware bus for the register map infrastructure.
 *
 * @write: Write operation.
 * @gather_write: Write operation with split register/value, return -ENOTSUPP
 *                if not implemented  on a given device.
 * @read: Read operation.  Data is returned in the buffer used to transmit
 *         data.
 * @read_flag_mask: Mask to be set in the top byte of the register when doing
 *                  a read.
 */
struct regmap_bus {
	regmap_hw_write write;
	regmap_hw_gather_write gather_write;
	regmap_hw_read read;
	u8 read_flag_mask;
};

struct regmap *regmap_init(struct device *dev,
			   const struct regmap_bus *bus,
			   const struct regmap_config *config);
struct regmap *regmap_init_i2c(struct i2c_client *i2c,
			       const struct regmap_config *config);
struct regmap *regmap_init_spi(struct spi_device *dev,
			       const struct regmap_config *config);

struct regmap *devm_regmap_init(struct device *dev,
				const struct regmap_bus *bus,
				const struct regmap_config *config);
struct regmap *devm_regmap_init_i2c(struct i2c_client *i2c,
				    const struct regmap_config *config);
struct regmap *devm_regmap_init_spi(struct spi_device *dev,
				    const struct regmap_config *config);

void regmap_exit(struct regmap *map);
int regmap_reinit_cache(struct regmap *map,
			const struct regmap_config *config);
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

	int num_regs;

	const struct regmap_irq *irqs;
	int num_irqs;
};

struct regmap_irq_chip_data;

int regmap_add_irq_chip(struct regmap *map, int irq, int irq_flags,
			int irq_base, struct regmap_irq_chip *chip,
			struct regmap_irq_chip_data **data);
void regmap_del_irq_chip(int irq, struct regmap_irq_chip_data *data);
int regmap_irq_chip_get_base(struct regmap_irq_chip_data *data);

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

#endif

#endif
