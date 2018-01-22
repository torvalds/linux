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

#include <linux/err.h>
#include <linux/errno.h>

struct device;
struct device_node;
/* consumer cookie */
struct nvmem_cell;
struct nvmem_device;

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
int nvmem_cell_read_u32(struct device *dev, const char *cell_id, u32 *val);

/* direct nvmem device read/write interface */
struct nvmem_device *nvmem_device_get(struct device *dev, const char *name);
struct nvmem_device *devm_nvmem_device_get(struct device *dev,
					   const char *name);
void nvmem_device_put(struct nvmem_device *nvmem);
void devm_nvmem_device_put(struct device *dev, struct nvmem_device *nvmem);
int nvmem_device_read(struct nvmem_device *nvmem, unsigned int offset,
		      size_t bytes, void *buf);
int nvmem_device_write(struct nvmem_device *nvmem, unsigned int offset,
		       size_t bytes, void *buf);
ssize_t nvmem_device_cell_read(struct nvmem_device *nvmem,
			   struct nvmem_cell_info *info, void *buf);
int nvmem_device_cell_write(struct nvmem_device *nvmem,
			    struct nvmem_cell_info *info, void *buf);

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

static inline void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len)
{
	return ERR_PTR(-ENOSYS);
}

static inline int nvmem_cell_write(struct nvmem_cell *cell,
				    const char *buf, size_t len)
{
	return -ENOSYS;
}

static inline int nvmem_cell_read_u32(struct device *dev,
				      const char *cell_id, u32 *val)
{
	return -ENOSYS;
}

static inline struct nvmem_device *nvmem_device_get(struct device *dev,
						    const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct nvmem_device *devm_nvmem_device_get(struct device *dev,
							 const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static inline void nvmem_device_put(struct nvmem_device *nvmem)
{
}

static inline void devm_nvmem_device_put(struct device *dev,
					 struct nvmem_device *nvmem)
{
}

static inline ssize_t nvmem_device_cell_read(struct nvmem_device *nvmem,
					 struct nvmem_cell_info *info,
					 void *buf)
{
	return -ENOSYS;
}

static inline int nvmem_device_cell_write(struct nvmem_device *nvmem,
					  struct nvmem_cell_info *info,
					  void *buf)
{
	return -ENOSYS;
}

static inline int nvmem_device_read(struct nvmem_device *nvmem,
				    unsigned int offset, size_t bytes,
				    void *buf)
{
	return -ENOSYS;
}

static inline int nvmem_device_write(struct nvmem_device *nvmem,
				     unsigned int offset, size_t bytes,
				     void *buf)
{
	return -ENOSYS;
}
#endif /* CONFIG_NVMEM */

#if IS_ENABLED(CONFIG_NVMEM) && IS_ENABLED(CONFIG_OF)
struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
				     const char *name);
struct nvmem_device *of_nvmem_device_get(struct device_node *np,
					 const char *name);
#else
static inline struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
				     const char *name)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct nvmem_device *of_nvmem_device_get(struct device_node *np,
						       const char *name)
{
	return ERR_PTR(-ENOSYS);
}
#endif /* CONFIG_NVMEM && CONFIG_OF */

#endif  /* ifndef _LINUX_NVMEM_CONSUMER_H */
