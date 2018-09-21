/*
 * nvmem framework provider.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _LINUX_NVMEM_PROVIDER_H
#define _LINUX_NVMEM_PROVIDER_H

#include <linux/err.h>
#include <linux/errno.h>

struct nvmem_device;
struct nvmem_cell_info;
typedef int (*nvmem_reg_read_t)(void *priv, unsigned int offset,
				void *val, size_t bytes);
typedef int (*nvmem_reg_write_t)(void *priv, unsigned int offset,
				 void *val, size_t bytes);

/**
 * struct nvmem_config - NVMEM device configuration
 *
 * @dev:	Parent device.
 * @name:	Optional name.
 * @id:		Optional device ID used in full name. Ignored if name is NULL.
 * @owner:	Pointer to exporter module. Used for refcounting.
 * @cells:	Optional array of pre-defined NVMEM cells.
 * @ncells:	Number of elements in cells.
 * @read_only:	Device is read-only.
 * @root_only:	Device is accessibly to root only.
 * @reg_read:	Callback to read data.
 * @reg_write:	Callback to write data.
 * @size:	Device size.
 * @word_size:	Minimum read/write access granularity.
 * @stride:	Minimum read/write access stride.
 * @priv:	User context passed to read/write callbacks.
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
	bool			read_only;
	bool			root_only;
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

#if IS_ENABLED(CONFIG_NVMEM)

struct nvmem_device *nvmem_register(const struct nvmem_config *cfg);
void nvmem_unregister(struct nvmem_device *nvmem);

struct nvmem_device *devm_nvmem_register(struct device *dev,
					 const struct nvmem_config *cfg);

int devm_nvmem_unregister(struct device *dev, struct nvmem_device *nvmem);

int nvmem_add_cells(struct nvmem_device *nvmem,
		    const struct nvmem_cell_info *info,
		    int ncells);
#else

static inline struct nvmem_device *nvmem_register(const struct nvmem_config *c)
{
	return ERR_PTR(-ENOSYS);
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
	return -ENOSYS;

}

static inline int nvmem_add_cells(struct nvmem_device *nvmem,
				  const struct nvmem_cell_info *info,
				  int ncells)
{
	return -ENOSYS;
}

#endif /* CONFIG_NVMEM */
#endif  /* ifndef _LINUX_NVMEM_PROVIDER_H */
