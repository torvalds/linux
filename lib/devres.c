// SPDX-License-Identifier: GPL-2.0
#include <linux/bug.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/gfp_types.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/types.h>

enum devm_ioremap_type {
	DEVM_IOREMAP = 0,
	DEVM_IOREMAP_UC,
	DEVM_IOREMAP_WC,
	DEVM_IOREMAP_NP,
};

void devm_ioremap_release(struct device *dev, void *res)
{
	iounmap(*(void __iomem **)res);
}

static int devm_ioremap_match(struct device *dev, void *res, void *match_data)
{
	return *(void **)res == match_data;
}

static void __iomem *__devm_ioremap(struct device *dev, resource_size_t offset,
				    resource_size_t size,
				    enum devm_ioremap_type type)
{
	void __iomem **ptr, *addr = NULL;

	ptr = devres_alloc_node(devm_ioremap_release, sizeof(*ptr), GFP_KERNEL,
				dev_to_node(dev));
	if (!ptr)
		return NULL;

	switch (type) {
	case DEVM_IOREMAP:
		addr = ioremap(offset, size);
		break;
	case DEVM_IOREMAP_UC:
		addr = ioremap_uc(offset, size);
		break;
	case DEVM_IOREMAP_WC:
		addr = ioremap_wc(offset, size);
		break;
	case DEVM_IOREMAP_NP:
		addr = ioremap_np(offset, size);
		break;
	}

	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}

/**
 * devm_ioremap - Managed ioremap()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed ioremap().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap(struct device *dev, resource_size_t offset,
			   resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP);
}
EXPORT_SYMBOL(devm_ioremap);

/**
 * devm_ioremap_uc - Managed ioremap_uc()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed ioremap_uc().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap_uc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_UC);
}
EXPORT_SYMBOL_GPL(devm_ioremap_uc);

/**
 * devm_ioremap_wc - Managed ioremap_wc()
 * @dev: Generic device to remap IO address for
 * @offset: Resource address to map
 * @size: Size of map
 *
 * Managed ioremap_wc().  Map is automatically unmapped on driver detach.
 */
void __iomem *devm_ioremap_wc(struct device *dev, resource_size_t offset,
			      resource_size_t size)
{
	return __devm_ioremap(dev, offset, size, DEVM_IOREMAP_WC);
}
EXPORT_SYMBOL(devm_ioremap_wc);

/**
 * devm_iounmap - Managed iounmap()
 * @dev: Generic device to unmap for
 * @addr: Address to unmap
 *
 * Managed iounmap().  @addr must have been mapped using devm_ioremap*().
 */
void devm_iounmap(struct device *dev, void __iomem *addr)
{
	WARN_ON(devres_release(dev, devm_ioremap_release, devm_ioremap_match,
			       (__force void *)addr));
}
EXPORT_SYMBOL(devm_iounmap);

static void __iomem *
__devm_ioremap_resource(struct device *dev, const struct resource *res,
			enum devm_ioremap_type type)
{
	resource_size_t size;
	void __iomem *dest_ptr;
	char *pretty_name;
	int ret;

	BUG_ON(!dev);

	if (!res || resource_type(res) != IORESOURCE_MEM) {
		ret = dev_err_probe(dev, -EINVAL, "invalid resource %pR\n", res);
		return IOMEM_ERR_PTR(ret);
	}

	if (type == DEVM_IOREMAP && res->flags & IORESOURCE_MEM_NONPOSTED)
		type = DEVM_IOREMAP_NP;

	size = resource_size(res);

	if (res->name)
		pretty_name = devm_kasprintf(dev, GFP_KERNEL, "%s %s",
					     dev_name(dev), res->name);
	else
		pretty_name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!pretty_name) {
		ret = dev_err_probe(dev, -ENOMEM, "can't generate pretty name for resource %pR\n", res);
		return IOMEM_ERR_PTR(ret);
	}

	if (!devm_request_mem_region(dev, res->start, size, pretty_name)) {
		ret = dev_err_probe(dev, -EBUSY, "can't request region for resource %pR\n", res);
		return IOMEM_ERR_PTR(ret);
	}

	dest_ptr = __devm_ioremap(dev, res->start, size, type);
	if (!dest_ptr) {
		devm_release_mem_region(dev, res->start, size);
		ret = dev_err_probe(dev, -ENOMEM, "ioremap failed for resource %pR\n", res);
		return IOMEM_ERR_PTR(ret);
	}

	return dest_ptr;
}

/**
 * devm_ioremap_resource() - check, request region, and ioremap resource
 * @dev: generic device to handle the resource for
 * @res: resource to be handled
 *
 * Checks that a resource is a valid memory region, requests the memory
 * region and ioremaps it. All operations are managed and will be undone
 * on driver detach.
 *
 * Usage example:
 *
 *	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 *	base = devm_ioremap_resource(&pdev->dev, res);
 *	if (IS_ERR(base))
 *		return PTR_ERR(base);
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *devm_ioremap_resource(struct device *dev,
				    const struct resource *res)
{
	return __devm_ioremap_resource(dev, res, DEVM_IOREMAP);
}
EXPORT_SYMBOL(devm_ioremap_resource);

/**
 * devm_ioremap_resource_wc() - write-combined variant of
 *				devm_ioremap_resource()
 * @dev: generic device to handle the resource for
 * @res: resource to be handled
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *devm_ioremap_resource_wc(struct device *dev,
				       const struct resource *res)
{
	return __devm_ioremap_resource(dev, res, DEVM_IOREMAP_WC);
}
EXPORT_SYMBOL(devm_ioremap_resource_wc);

/*
 * devm_of_iomap - Requests a resource and maps the memory mapped IO
 *		   for a given device_node managed by a given device
 *
 * Checks that a resource is a valid memory region, requests the memory
 * region and ioremaps it. All operations are managed and will be undone
 * on driver detach of the device.
 *
 * This is to be used when a device requests/maps resources described
 * by other device tree nodes (children or otherwise).
 *
 * @dev:	The device "managing" the resource
 * @node:       The device-tree node where the resource resides
 * @index:	index of the MMIO range in the "reg" property
 * @size:	Returns the size of the resource (pass NULL if not needed)
 *
 * Usage example:
 *
 *	base = devm_of_iomap(&pdev->dev, node, 0, NULL);
 *	if (IS_ERR(base))
 *		return PTR_ERR(base);
 *
 * Please Note: This is not a one-to-one replacement for of_iomap() because the
 * of_iomap() function does not track whether the region is already mapped.  If
 * two drivers try to map the same memory, the of_iomap() function will succeed
 * but the devm_of_iomap() function will return -EBUSY.
 *
 * Return: a pointer to the requested and mapped memory or an ERR_PTR() encoded
 * error code on failure.
 */
void __iomem *devm_of_iomap(struct device *dev, struct device_node *node, int index,
			    resource_size_t *size)
{
	struct resource res;

	if (of_address_to_resource(node, index, &res))
		return IOMEM_ERR_PTR(-EINVAL);
	if (size)
		*size = resource_size(&res);
	return devm_ioremap_resource(dev, &res);
}
EXPORT_SYMBOL(devm_of_iomap);

#ifdef CONFIG_HAS_IOPORT_MAP
/*
 * Generic iomap devres
 */
static void devm_ioport_map_release(struct device *dev, void *res)
{
	ioport_unmap(*(void __iomem **)res);
}

static int devm_ioport_map_match(struct device *dev, void *res,
				 void *match_data)
{
	return *(void **)res == match_data;
}

/**
 * devm_ioport_map - Managed ioport_map()
 * @dev: Generic device to map ioport for
 * @port: Port to map
 * @nr: Number of ports to map
 *
 * Managed ioport_map().  Map is automatically unmapped on driver
 * detach.
 *
 * Return: a pointer to the remapped memory or NULL on failure.
 */
void __iomem *devm_ioport_map(struct device *dev, unsigned long port,
			       unsigned int nr)
{
	void __iomem **ptr, *addr;

	ptr = devres_alloc_node(devm_ioport_map_release, sizeof(*ptr), GFP_KERNEL,
				dev_to_node(dev));
	if (!ptr)
		return NULL;

	addr = ioport_map(port, nr);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_ioport_map);

/**
 * devm_ioport_unmap - Managed ioport_unmap()
 * @dev: Generic device to unmap for
 * @addr: Address to unmap
 *
 * Managed ioport_unmap().  @addr must have been mapped using
 * devm_ioport_map().
 */
void devm_ioport_unmap(struct device *dev, void __iomem *addr)
{
	WARN_ON(devres_release(dev, devm_ioport_map_release,
			       devm_ioport_map_match, (__force void *)addr));
}
EXPORT_SYMBOL(devm_ioport_unmap);
#endif /* CONFIG_HAS_IOPORT_MAP */

static void devm_arch_phys_ac_add_release(struct device *dev, void *res)
{
	arch_phys_wc_del(*((int *)res));
}

/**
 * devm_arch_phys_wc_add - Managed arch_phys_wc_add()
 * @dev: Managed device
 * @base: Memory base address
 * @size: Size of memory range
 *
 * Adds a WC MTRR using arch_phys_wc_add() and sets up a release callback.
 * See arch_phys_wc_add() for more information.
 */
int devm_arch_phys_wc_add(struct device *dev, unsigned long base, unsigned long size)
{
	int *mtrr;
	int ret;

	mtrr = devres_alloc_node(devm_arch_phys_ac_add_release, sizeof(*mtrr), GFP_KERNEL,
				 dev_to_node(dev));
	if (!mtrr)
		return -ENOMEM;

	ret = arch_phys_wc_add(base, size);
	if (ret < 0) {
		devres_free(mtrr);
		return ret;
	}

	*mtrr = ret;
	devres_add(dev, mtrr);

	return ret;
}
EXPORT_SYMBOL(devm_arch_phys_wc_add);

struct arch_io_reserve_memtype_wc_devres {
	resource_size_t start;
	resource_size_t size;
};

static void devm_arch_io_free_memtype_wc_release(struct device *dev, void *res)
{
	const struct arch_io_reserve_memtype_wc_devres *this = res;

	arch_io_free_memtype_wc(this->start, this->size);
}

/**
 * devm_arch_io_reserve_memtype_wc - Managed arch_io_reserve_memtype_wc()
 * @dev: Managed device
 * @start: Memory base address
 * @size: Size of memory range
 *
 * Reserves a memory range with WC caching using arch_io_reserve_memtype_wc()
 * and sets up a release callback See arch_io_reserve_memtype_wc() for more
 * information.
 */
int devm_arch_io_reserve_memtype_wc(struct device *dev, resource_size_t start,
				    resource_size_t size)
{
	struct arch_io_reserve_memtype_wc_devres *dr;
	int ret;

	dr = devres_alloc_node(devm_arch_io_free_memtype_wc_release, sizeof(*dr), GFP_KERNEL,
			       dev_to_node(dev));
	if (!dr)
		return -ENOMEM;

	ret = arch_io_reserve_memtype_wc(start, size);
	if (ret < 0) {
		devres_free(dr);
		return ret;
	}

	dr->start = start;
	dr->size = size;
	devres_add(dev, dr);

	return ret;
}
EXPORT_SYMBOL(devm_arch_io_reserve_memtype_wc);
