/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __OF_RESERVED_MEM_H
#define __OF_RESERVED_MEM_H

#include <linux/device.h>
#include <linux/of.h>

struct of_phandle_args;
struct reserved_mem_ops;
struct resource;

struct reserved_mem {
	const char			*name;
	unsigned long			fdt_node;
	const struct reserved_mem_ops	*ops;
	phys_addr_t			base;
	phys_addr_t			size;
	void				*priv;
};

struct reserved_mem_ops {
	int	(*device_init)(struct reserved_mem *rmem,
			       struct device *dev);
	void	(*device_release)(struct reserved_mem *rmem,
				  struct device *dev);
};

typedef int (*reservedmem_of_init_fn)(struct reserved_mem *rmem);

#ifdef CONFIG_OF_RESERVED_MEM

#define RESERVEDMEM_OF_DECLARE(name, compat, init)			\
	_OF_DECLARE(reservedmem, name, compat, init, reservedmem_of_init_fn)

int of_reserved_mem_device_init_by_idx(struct device *dev,
				       struct device_node *np, int idx);
int of_reserved_mem_device_init_by_name(struct device *dev,
					struct device_node *np,
					const char *name);
void of_reserved_mem_device_release(struct device *dev);

struct reserved_mem *of_reserved_mem_lookup(struct device_node *np);
int of_reserved_mem_region_to_resource(const struct device_node *np,
				       unsigned int idx, struct resource *res);
int of_reserved_mem_region_to_resource_byname(const struct device_node *np,
					      const char *name, struct resource *res);
int of_reserved_mem_region_count(const struct device_node *np);

#else

#define RESERVEDMEM_OF_DECLARE(name, compat, init)			\
	_OF_DECLARE_STUB(reservedmem, name, compat, init, reservedmem_of_init_fn)

static inline int of_reserved_mem_device_init_by_idx(struct device *dev,
					struct device_node *np, int idx)
{
	return -ENOSYS;
}

static inline int of_reserved_mem_device_init_by_name(struct device *dev,
						      struct device_node *np,
						      const char *name)
{
	return -ENOSYS;
}

static inline void of_reserved_mem_device_release(struct device *pdev) { }

static inline struct reserved_mem *of_reserved_mem_lookup(struct device_node *np)
{
	return NULL;
}

static inline int of_reserved_mem_region_to_resource(const struct device_node *np,
						     unsigned int idx,
						     struct resource *res)
{
	return -ENOSYS;
}

static inline int of_reserved_mem_region_to_resource_byname(const struct device_node *np,
							    const char *name,
							    struct resource *res)
{
	return -ENOSYS;
}

static inline int of_reserved_mem_region_count(const struct device_node *np)
{
	return 0;
}
#endif

/**
 * of_reserved_mem_device_init() - assign reserved memory region to given device
 * @dev:	Pointer to the device to configure
 *
 * This function assigns respective DMA-mapping operations based on the first
 * reserved memory region specified by 'memory-region' property in device tree
 * node of the given device.
 *
 * Returns error code or zero on success.
 */
static inline int of_reserved_mem_device_init(struct device *dev)
{
	return of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0);
}

#endif /* __OF_RESERVED_MEM_H */
