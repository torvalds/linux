#ifndef MFD_CORE_H
#define MFD_CORE_H
/*
 * drivers/mfd/mfd-core.h
 *
 * core MFD support
 * Copyright (c) 2006 Ian Molton
 * Copyright (c) 2007 Dmitry Baryshkov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>

/*
 * This struct describes the MFD part ("cell").
 * After registration the copy of this structure will become the platform data
 * of the resulting platform_device
 */
struct mfd_cell {
	const char		*name;

	int			(*enable)(struct platform_device *dev);
	int			(*disable)(struct platform_device *dev);
	int			(*suspend)(struct platform_device *dev);
	int			(*resume)(struct platform_device *dev);

	void			*driver_data; /* driver-specific data */

	/*
	 * This resources can be specified relatievly to the parent device.
	 * For accessing device you should use resources from device
	 */
	int			num_resources;
	const struct resource	*resources;
};

static inline struct mfd_cell *
mfd_get_cell(struct platform_device *pdev)
{
	return (struct mfd_cell *)pdev->dev.platform_data;
}

extern int mfd_add_devices(
		struct platform_device *parent,
		const struct mfd_cell *cells, int n_devs,
		struct resource *mem_base,
		int irq_base);

extern void mfd_remove_devices(struct platform_device *parent);

#endif
