/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvmem framework provider.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _LINUX_NVMEM_PROVIDER_H
#define _LINUX_NVMEM_PROVIDER_H

#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>

struct nvmem_device;
typedef int (*nvmem_reg_read_t)(void *priv, unsigned int offset,
				void *val, size_t bytes);
typedef int (*nvmem_reg_write_t)(void *priv, unsigned int offset,
				 void *val, size_t bytes);
/* used for vendor specific post processing of cell data */
typedef int (*nvmem_cell_post_process_t)(void *priv, const char *id, int index,
					 unsigned int offset, void *buf,
					 size_t bytes);

enum nvmem_type {
	NVMEM_TYPE_UNKNOWN = 0,
	NVMEM_TYPE_EEPROM,
	NVMEM_TYPE_OTP,
	NVMEM_TYPE_BATTERY_BACKED,
	NVMEM_TYPE_FRAM,
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
 * struct nvmem_cell_info - NVMEM cell description
 * @name:	Name.
 * @offset:	Offset within the NVMEM device.
 * @raw_len:	Length of raw data (without post processing).
 * @bytes:	Length of the cell.
 * @bit_offset:	Bit offset if cell is smaller than a byte.
 * @nbits:	Number of bits.
 * @np:		Optional device_node pointer.
 * @read_post_process:	Callback for optional post processing of cell data
 *			on reads.
 * @priv:	Opaque data passed to the read_post_process hook.
 */
struct nvmem_cell_info {
	const char		*name;
	unsigned int		offset;
	size_t			raw_len;
	unsigned int		bytes;
	unsigned int		bit_offset;
	unsigned int		nbits;
	struct device_node	*np;
	nvmem_cell_post_process_t read_post_process;
	void			*priv;
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
 * @add_legacy_fixed_of_cells:	Read fixed NVMEM cells from old OF syntax.
 * @fixup_dt_cell_info: Will be called before a cell is added. Can be
 *		used to modify the nvmem_cell_info.
 * @keepout:	Optional array of keepout ranges (sorted ascending by start).
 * @nkeepout:	Number of elements in the keepout array.
 * @type:	Type of the nvmem storage
 * @read_only:	Device is read-only.
 * @root_only:	Device is accessibly to root only.
 * @of_node:	If given, this will be used instead of the parent's of_node.
 * @reg_read:	Callback to read data; return zero if successful.
 * @reg_write:	Callback to write data; return zero if successful.
 * @size:	Device size.
 * @word_size:	Minimum read/write access granularity.
 * @stride:	Minimum read/write access stride.
 * @priv:	User context passed to read/write callbacks.
 * @ignore_wp:  Write Protect pin is managed by the provider.
 * @layout:	Fixed layout associated with this nvmem device.
 *
 * Note: A default "nvmem<id>" name will be assigned to the device if
 * no name is specified in its configuration. In such case "<id>" is
 * generated with ida_alloc() and provided id field is ignored.
 *
 * Note: Specifying name and setting id to -1 implies a unique device
 * whose name is provided as-is (kept unaltered).
 */
struct nvmem_config {
	struct device		*dev;
	const char		*name;
	int			id;
	struct module		*owner;
	const struct nvmem_cell_info	*cells;
	int			ncells;
	bool			add_legacy_fixed_of_cells;
	void (*fixup_dt_cell_info)(struct nvmem_device *nvmem,
				   struct nvmem_cell_info *cell);
	const struct nvmem_keepout *keepout;
	unsigned int		nkeepout;
	enum nvmem_type		type;
	bool			read_only;
	bool			root_only;
	bool			ignore_wp;
	struct nvmem_layout	*layout;
	struct device_node	*of_node;
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
 * struct nvmem_layout - NVMEM layout definitions
 *
 * @dev:		Device-model layout device.
 * @nvmem:		The underlying NVMEM device
 * @add_cells:		Will be called if a nvmem device is found which
 *			has this layout. The function will add layout
 *			specific cells with nvmem_add_one_cell().
 *
 * A nvmem device can hold a well defined structure which can just be
 * evaluated during runtime. For example a TLV list, or a list of "name=val"
 * pairs. A nvmem layout can parse the nvmem device and add appropriate
 * cells.
 */
struct nvmem_layout {
	struct device dev;
	struct nvmem_device *nvmem;
	int (*add_cells)(struct nvmem_layout *layout);
};

struct nvmem_layout_driver {
	struct device_driver driver;
	int (*probe)(struct nvmem_layout *layout);
	void (*remove)(struct nvmem_layout *layout);
};

#if IS_ENABLED(CONFIG_NVMEM)

struct nvmem_device *nvmem_register(const struct nvmem_config *cfg);
void nvmem_unregister(struct nvmem_device *nvmem);

struct nvmem_device *devm_nvmem_register(struct device *dev,
					 const struct nvmem_config *cfg);

int nvmem_add_one_cell(struct nvmem_device *nvmem,
		       const struct nvmem_cell_info *info);

int nvmem_layout_register(struct nvmem_layout *layout);
void nvmem_layout_unregister(struct nvmem_layout *layout);

#define nvmem_layout_driver_register(drv) \
	__nvmem_layout_driver_register(drv, THIS_MODULE)
int __nvmem_layout_driver_register(struct nvmem_layout_driver *drv,
				   struct module *owner);
void nvmem_layout_driver_unregister(struct nvmem_layout_driver *drv);
#define module_nvmem_layout_driver(__nvmem_layout_driver)		\
	module_driver(__nvmem_layout_driver, nvmem_layout_driver_register, \
		      nvmem_layout_driver_unregister)

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

static inline int nvmem_add_one_cell(struct nvmem_device *nvmem,
				     const struct nvmem_cell_info *info)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_layout_register(struct nvmem_layout *layout)
{
	return -EOPNOTSUPP;
}

static inline void nvmem_layout_unregister(struct nvmem_layout *layout) {}

#endif /* CONFIG_NVMEM */

#if IS_ENABLED(CONFIG_NVMEM) && IS_ENABLED(CONFIG_OF)

/**
 * of_nvmem_layout_get_container() - Get OF node of layout container
 *
 * @nvmem: nvmem device
 *
 * Return: a node pointer with refcount incremented or NULL if no
 * container exists. Use of_node_put() on it when done.
 */
struct device_node *of_nvmem_layout_get_container(struct nvmem_device *nvmem);

#else  /* CONFIG_NVMEM && CONFIG_OF */

static inline struct device_node *of_nvmem_layout_get_container(struct nvmem_device *nvmem)
{
	return NULL;
}

#endif /* CONFIG_NVMEM && CONFIG_OF */

#endif  /* ifndef _LINUX_NVMEM_PROVIDER_H */
