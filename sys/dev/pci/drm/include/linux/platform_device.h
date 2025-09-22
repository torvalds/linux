/* Public domain. */

#ifndef _LINUX_PLATFORM_DEVICE_H
#define _LINUX_PLATFORM_DEVICE_H

#include <linux/device.h>

struct platform_driver;

struct platform_device {
	struct device dev;
	int num_resources;
	struct resource *resource;
	struct device *parent;
	bus_space_tag_t iot;
	bus_dma_tag_t dmat;
	int node;

#ifdef __HAVE_FDT
	struct fdt_attach_args *faa;
#endif

	LIST_ENTRY(platform_device) next;
};

#define to_platform_device(p)	(struct platform_device *)(p)

extern struct bus_type platform_bus_type;

void __iomem *
devm_platform_ioremap_resource_byname(struct platform_device *, const char *);

static inline void
platform_set_drvdata(struct platform_device *pdev, void *data)
{
	dev_set_drvdata(&pdev->dev, data);
}

static inline void *
platform_get_drvdata(struct platform_device *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

static inline int
platform_driver_register(struct platform_driver *platform_drv)
{
	return 0;
}

void	platform_device_register(struct platform_device *);
struct resource *platform_get_resource(struct platform_device *, u_int, u_int);

#endif
