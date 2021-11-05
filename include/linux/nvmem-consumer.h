/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nvmem framework consumer.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _LINUX_NVMEM_CONSUMER_H
#define _LINUX_NVMEM_CONSUMER_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/notifier.h>

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

/**
 * struct nvmem_cell_lookup - cell lookup entry
 *
 * @nvmem_name:	Name of the provider.
 * @cell_name:	Name of the nvmem cell as defined in the name field of
 *		struct nvmem_cell_info.
 * @dev_id:	Name of the consumer device that will be associated with
 *		this cell.
 * @con_id:	Connector id for this cell lookup.
 */
struct nvmem_cell_lookup {
	const char		*nvmem_name;
	const char		*cell_name;
	const char		*dev_id;
	const char		*con_id;
	struct list_head	node;
};

enum {
	NVMEM_ADD = 1,
	NVMEM_REMOVE,
	NVMEM_CELL_ADD,
	NVMEM_CELL_REMOVE,
};

#if IS_ENABLED(CONFIG_NVMEM)

/* Cell based interface */
struct nvmem_cell *nvmem_cell_get(struct device *dev, const char *id);
struct nvmem_cell *devm_nvmem_cell_get(struct device *dev, const char *id);
void nvmem_cell_put(struct nvmem_cell *cell);
void devm_nvmem_cell_put(struct device *dev, struct nvmem_cell *cell);
void *nvmem_cell_read(struct nvmem_cell *cell, size_t *len);
int nvmem_cell_write(struct nvmem_cell *cell, void *buf, size_t len);
int nvmem_cell_read_u8(struct device *dev, const char *cell_id, u8 *val);
int nvmem_cell_read_u16(struct device *dev, const char *cell_id, u16 *val);
int nvmem_cell_read_u32(struct device *dev, const char *cell_id, u32 *val);
int nvmem_cell_read_u64(struct device *dev, const char *cell_id, u64 *val);
int nvmem_cell_read_variable_le_u32(struct device *dev, const char *cell_id,
				    u32 *val);
int nvmem_cell_read_variable_le_u64(struct device *dev, const char *cell_id,
				    u64 *val);

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

const char *nvmem_dev_name(struct nvmem_device *nvmem);

void nvmem_add_cell_lookups(struct nvmem_cell_lookup *entries,
			    size_t nentries);
void nvmem_del_cell_lookups(struct nvmem_cell_lookup *entries,
			    size_t nentries);

int nvmem_register_notifier(struct notifier_block *nb);
int nvmem_unregister_notifier(struct notifier_block *nb);

struct nvmem_device *nvmem_device_find(void *data,
			int (*match)(struct device *dev, const void *data));

#else

static inline struct nvmem_cell *nvmem_cell_get(struct device *dev,
						const char *id)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct nvmem_cell *devm_nvmem_cell_get(struct device *dev,
						     const char *id)
{
	return ERR_PTR(-EOPNOTSUPP);
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
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int nvmem_cell_write(struct nvmem_cell *cell,
				   void *buf, size_t len)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_cell_read_u16(struct device *dev,
				      const char *cell_id, u16 *val)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_cell_read_u32(struct device *dev,
				      const char *cell_id, u32 *val)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_cell_read_u64(struct device *dev,
				      const char *cell_id, u64 *val)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_cell_read_variable_le_u32(struct device *dev,
						 const char *cell_id,
						 u32 *val)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_cell_read_variable_le_u64(struct device *dev,
						  const char *cell_id,
						  u64 *val)
{
	return -EOPNOTSUPP;
}

static inline struct nvmem_device *nvmem_device_get(struct device *dev,
						    const char *name)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct nvmem_device *devm_nvmem_device_get(struct device *dev,
							 const char *name)
{
	return ERR_PTR(-EOPNOTSUPP);
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
	return -EOPNOTSUPP;
}

static inline int nvmem_device_cell_write(struct nvmem_device *nvmem,
					  struct nvmem_cell_info *info,
					  void *buf)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_device_read(struct nvmem_device *nvmem,
				    unsigned int offset, size_t bytes,
				    void *buf)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_device_write(struct nvmem_device *nvmem,
				     unsigned int offset, size_t bytes,
				     void *buf)
{
	return -EOPNOTSUPP;
}

static inline const char *nvmem_dev_name(struct nvmem_device *nvmem)
{
	return NULL;
}

static inline void
nvmem_add_cell_lookups(struct nvmem_cell_lookup *entries, size_t nentries) {}
static inline void
nvmem_del_cell_lookups(struct nvmem_cell_lookup *entries, size_t nentries) {}

static inline int nvmem_register_notifier(struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline int nvmem_unregister_notifier(struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline struct nvmem_device *nvmem_device_find(void *data,
			int (*match)(struct device *dev, const void *data))
{
	return NULL;
}

#endif /* CONFIG_NVMEM */

#if IS_ENABLED(CONFIG_NVMEM) && IS_ENABLED(CONFIG_OF)
struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
				     const char *id);
struct nvmem_device *of_nvmem_device_get(struct device_node *np,
					 const char *name);
#else
static inline struct nvmem_cell *of_nvmem_cell_get(struct device_node *np,
						   const char *id)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct nvmem_device *of_nvmem_device_get(struct device_node *np,
						       const char *name)
{
	return ERR_PTR(-EOPNOTSUPP);
}
#endif /* CONFIG_NVMEM && CONFIG_OF */

#endif  /* ifndef _LINUX_NVMEM_CONSUMER_H */
