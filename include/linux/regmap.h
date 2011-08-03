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

#include <linux/device.h>
#include <linux/list.h>
#include <linux/module.h>

struct i2c_client;
struct spi_device;

struct regmap_config {
	int reg_bits;
	int val_bits;
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
 * @list: Internal use.
 * @type: Bus type, used to identify bus to be used for a device.
 * @write: Write operation.
 * @gather_write: Write operation with split register/value, return -ENOTSUPP
 *                if not implemented  on a given device.
 * @read: Read operation.  Data is returned in the buffer used to transmit
 *         data.
 * @owner: Module with the bus implementation, used to pin the implementation
 *         in memory.
 * @read_flag_mask: Mask to be set in the top byte of the register when doing
 *                  a read.
 */
struct regmap_bus {
	struct list_head list;
	struct bus_type *type;
	regmap_hw_write write;
	regmap_hw_gather_write gather_write;
	regmap_hw_read read;
	struct module *owner;
	u8 read_flag_mask;
};

struct regmap *regmap_init(struct device *dev,
			   const struct regmap_bus *bus,
			   const struct regmap_config *config);
struct regmap *regmap_init_i2c(struct i2c_client *i2c,
			       const struct regmap_config *config);
struct regmap *regmap_init_spi(struct spi_device *dev,
			       const struct regmap_config *config);

void regmap_exit(struct regmap *map);
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);
int regmap_raw_write(struct regmap *map, unsigned int reg,
		     const void *val, size_t val_len);
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);
int regmap_raw_read(struct regmap *map, unsigned int reg,
		    void *val, size_t val_len);
int regmap_bulk_read(struct regmap *map, unsigned int reg, void *val,
		     size_t val_count);
int regmap_update_bits(struct regmap *map, unsigned int reg,
		       unsigned int mask, unsigned int val);

#endif
