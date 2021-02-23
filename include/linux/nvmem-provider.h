/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvmem framework provider.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _LINUX_NVMEM_PROVIDER_H
#define _LINUX_NVMEM_PROVIDER_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>

struct nvmem_device;
struct nvmem_cell_info;
typedef int (*nvmem_reg_read_t)(void *priv, unsigned int offset,
				void *val, size_t bytes);
typedef int (*nvmem_reg_write_t)(void *priv, unsigned int offset,
				 void *val, size_t bytes);

enum nvmem_type {
	NVMEM_TYPE_UNKNOWN = 0,
	NVMEM_TYPE_EEPROM,
	NVMEM_TYPE_OTP,
	NVMEM_TYPE_BATTERY_BACKED,
};

#define NVMEM_DEVID_NONE	(-1)
#define NVMEM_DEVID_AUTO	(-2)

/**
 * struct nvmem_keepout - NVMEM register keepout range.
 *
 * @start:	The first byte offset to avoid.
 * @end:	One beyond the last byte offset to avoid.
 * @value:	The byte to fill reads with for this region.
 */
struct nvmem_keepout {
	unsigned int start;
	unsigned int end;
	unsigned char value;
};

/**
 * struct nvmem_config - NVMEM device configuration
 *
 * @dev:	Parent device.
 * @name:	Optional name.
 * @id:		Optional device ID used in full name. Ignored if name is NULL.
 * @owner:	Pointer to exporter module. Used for refcounting.
 * @cells:	Optional array of pre-defined NVMEM cells.
 * @ncells:	Number of elements in cells.
 * @keepout:	Optional array of keepout ranges (sorted ascending by start).
 * @nkeepout:	Number of elements in the keepout array.
 * @type:	Type of the nvmem storage
 * @read_only:	Device is read-only.
 * @root_only:	Device is accessibly to root only.
 * @no_of_node:	Device should not use the parent's of_node even if it's !NULL.
 * @reg_read:	Callback to read data.
 * @reg_write:	Callback to write data.
 * @size:	Device size.
 * @word_size:	Minimum read/write access granularity.
 * @stride:	Minimum read/write access stride.
 * @priv:	User context passed to read/write callbacks.
 * @wp-gpio:   Write protect pin
 *
 * Note: A default "nvmem<id>" name will be assigned to the device if
 * no name is specified in its configuration. In such case "<id>" is
 * generated with ida_simple_get() and provided id field is ignored.
 *
 * Note: Specifying name and setting id to -1 implies a unique device
 * whose name is provided as-is (kept unaltered).
 */
struct nvmem_config {
	struct device		*dev;
	const char		*name;
	int			id;
	struct module		*owner;
	struct gpio_desc	*wp_gpio;
	const struct nvmem_cell_info	*cells;
	int			ncells;
	const struct nvmem_keepout *keepout;
	unsigned int		nkeepout;
	enum nvmem_type		type;
	bool			read_only;
	bool			root_only;
	bool			no_of_node;
	nvmem_reg_read_t	reg_read;
	nvmem_reg_write_t	reg_write;
	int	size;
	int	word_size;
	int	stride;
	void	*priv;
	/* To be only used by old driver/misc/eeprom drivers */
	bool			compat;
	struct device		*base_dev;
};

/**
 * struct nvmem_cell_table - NVMEM cell definitions for given provider
 *
 * @nvmem_name:		Provider name.
 * @cells:		Array of cell definitions.
 * @ncells:		Number of cell definitions in the array.
 * @node:		List node.
 *
 * This structure together with related helper functions is provided for users
 * that don't can't access the nvmem provided structure but wish to register
 * cell definitions for it e.g. board files registering an EEPROM device.
 */
struct nvmem_cell_table {
	const char		*nvmem_name;
	const struct nvmem_cell_info	*cells;
	size_t			ncells;
	struct list_head	node;
};

#if IS_ENABLED(CONFIG_NVMEM)

struct nvmem_device *nvmem_register(const struct nvmem_config *cfg);
void nvmem_unregister(struct nvmem_device *nvmem);

struct nvmem_device *devm_nvmem_register(struct device *dev,
					 const struct nvmem_config *cfg);

int devm_nvmem_unregister(struct device *dev, struct nvmem_device *nvmem);

void nvmem_add_cell_table(struct nvmem_cell_table *table);
void nvmem_del_cell_table(struct nvmem_cell_table *table);

#else

static inline struct nvmem_device *nvmem_register(const struct nvmem_config *c)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void nvmem_unregister(struct nvmem_device *nvmem) {}

static inline struct nvmem_device *
devm_nvmem_register(struct device *dev, const struct nvmem_config *c)
{
	return nvmem_register(c);
}

static inline int
devm_nvmem_unregister(struct device *dev, struct nvmem_device *nvmem)
{
	return -EOPNOTSUPP;
}

static inline void nvmem_add_cell_table(struct nvmem_cell_table *table) {}
static inline void nvmem_del_cell_table(struct nvmem_cell_table *table) {}

#endif /* CONFIG_NVMEM */
#endif  /* ifndef _LINUX_NVMEM_PROVIDER_H */
