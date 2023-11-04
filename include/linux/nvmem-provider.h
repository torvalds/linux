/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvmem framework provider.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _LINUX_NVMEM_PROVIDER_H
#define _LINUX_NVMEM_PROVIDER_H

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
 * @keepout:	Optional array of keepout ranges (sorted ascending by start).
 * @nkeepout:	Number of elements in the keepout array.
 * @type:	Type of the nvmem storage
 * @read_only:	Device is read-only.
 * @root_only:	Device is accessibly to root only.
 * @of_node:	If given, this will be used instead of the parent's of_node.
 * @reg_read:	Callback to read data.
 * @reg_write:	Callback to write data.
 * @size:	Device size.
 * @word_size:	Minimum read/write access granularity.
 * @stride:	Minimum read/write access stride.
 * @priv:	User context passed to read/write callbacks.
 * @ignore_wp:  Write Protect pin is managed by the provider.
 * @layout:	Fixed layout associated with this nvmem device.
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
	const struct nvmem_cell_info	*cells;
	int			ncells;
	bool			add_legacy_fixed_of_cells;
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

/**
 * struct nvmem_layout - NVMEM layout definitions
 *
 * @name:		Layout name.
 * @of_match_table:	Open firmware match table.
 * @add_cells:		Will be called if a nvmem device is found which
 *			has this layout. The function will add layout
 *			specific cells with nvmem_add_one_cell().
 * @fixup_cell_info:	Will be called before a cell is added. Can be
 *			used to modify the nvmem_cell_info.
 * @owner:		Pointer to struct module.
 * @node:		List node.
 *
 * A nvmem device can hold a well defined structure which can just be
 * evaluated during runtime. For example a TLV list, or a list of "name=val"
 * pairs. A nvmem layout can parse the nvmem device and add appropriate
 * cells.
 */
struct nvmem_layout {
	const char *name;
	const struct of_device_id *of_match_table;
	int (*add_cells)(struct device *dev, struct nvmem_device *nvmem,
			 struct nvmem_layout *layout);
	void (*fixup_cell_info)(struct nvmem_device *nvmem,
				struct nvmem_layout *layout,
				struct nvmem_cell_info *cell);

	/* private */
	struct module *owner;
	struct list_head node;
};

#if IS_ENABLED(CONFIG_NVMEM)

struct nvmem_device *nvmem_register(const struct nvmem_config *cfg);
void nvmem_unregister(struct nvmem_device *nvmem);

struct nvmem_device *devm_nvmem_register(struct device *dev,
					 const struct nvmem_config *cfg);

void nvmem_add_cell_table(struct nvmem_cell_table *table);
void nvmem_del_cell_table(struct nvmem_cell_table *table);

int nvmem_add_one_cell(struct nvmem_device *nvmem,
		       const struct nvmem_cell_info *info);

int __nvmem_layout_register(struct nvmem_layout *layout, struct module *owner);
#define nvmem_layout_register(layout) \
	__nvmem_layout_register(layout, THIS_MODULE)
void nvmem_layout_unregister(struct nvmem_layout *layout);

const void *nvmem_layout_get_match_data(struct nvmem_device *nvmem,
					struct nvmem_layout *layout);

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

static inline void nvmem_add_cell_table(struct nvmem_cell_table *table) {}
static inline void nvmem_del_cell_table(struct nvmem_cell_table *table) {}
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

static inline const void *
nvmem_layout_get_match_data(struct nvmem_device *nvmem,
			    struct nvmem_layout *layout)
{
	return NULL;
}

#endif /* CONFIG_NVMEM */

#define module_nvmem_layout_driver(__layout_driver)		\
	module_driver(__layout_driver, nvmem_layout_register,	\
		      nvmem_layout_unregister)

#endif  /* ifndef _LINUX_NVMEM_PROVIDER_H */
