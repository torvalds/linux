/*
 * nvmem framework consumer.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _LINUX_NVMEM_CONSUMER_H
#define _LINUX_NVMEM_CONSUMER_H

struct device;
struct device_node;
/* consumer cookie */
struct nvmem_cell;

struct nvmem_cell_info {
	const char		*name;
	unsigned int		offset;
	unsigned int		bytes;
	unsigned int		bit_offset;
	unsigned int		nbits;
};

#if IS_ENABLED(CONFIG_NVMEM)

/* Cell based interface */
struct nvmem_cell *nvmem_cell_get(struct device *dev, const char *name);
struct nvmem_cell *devm_nvmem_cell_get(struct device *dev, const char *name);
void nvmem_cell_put(struct nvmem_cell *cell);
void devm_nvmem_cell_put(struct device *dev, struct nvmem_cell *cell);
void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len);
int nvmem_cell_write(struct nvmem_cell *cell, void *buf, size_t len);

#else

static inline struct nvmem_cell *nvmem_cell_get(struct device *dev,
						const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct nvmem_cell *devm_nvmem_cell_get(struct device *dev,
				       const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static inline void devm_nvmem_cell_put(struct device *dev,
				       struct nvmem_cell *cell)
{

}
static inline void nvmem_cell_put(struct nvmem_cell *cell)
{
}

static inline char *nvmem_cell_read(struct nvmem_cell *cell, size_t *len)
{
	return ERR_PTR(-ENOSYS);
}

static inline int nvmem_cell_write(struct nvmem_cell *cell,
				    const char *buf, size_t len)
{
	return -ENOSYS;
}
#endif /* CONFIG_NVMEM */

#if IS_ENABLED(CONFIG_NVMEM) && IS_ENABLED(CONFIG_OF)
struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
				     const char *name);
#else
static inline struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
				     const char *name)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* CONFIG_NVMEM && CONFIG_OF */

#endif  /* ifndef _LINUX_NVMEM_CONSUMER_H */
