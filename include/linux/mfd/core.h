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

#ifndef MFD_CORE_H
#define MFD_CORE_H

#include <linux/platform_device.h>

/*
 * This struct describes the MFD part ("cell").
 * After registration the copy of this structure will become the platform data
 * of the resulting platform_device
 */
struct mfd_cell {
	const char		*name;
	int			id;

	/* refcounting for multiple drivers to use a single cell */
	atomic_t		*usage_count;
	int			(*enable)(struct platform_device *dev);
	int			(*disable)(struct platform_device *dev);

	int			(*suspend)(struct platform_device *dev);
	int			(*resume)(struct platform_device *dev);

	/* mfd_data can be used to pass data to client drivers */
	void			*mfd_data;

	/*
	 * These resources can be specified relative to the parent device.
	 * For accessing hardware you should use resources from the platform dev
	 */
	int			num_resources;
	const struct resource	*resources;

	/* don't check for resource conflicts */
	bool			ignore_resource_conflicts;

	/*
	 * Disable runtime PM callbacks for this subdevice - see
	 * pm_runtime_no_callbacks().
	 */
	bool			pm_runtime_no_callbacks;
};

/*
 * Convenience functions for clients using shared cells.  Refcounting
 * happens automatically, with the cell's enable/disable callbacks
 * being called only when a device is first being enabled or no other
 * clients are making use of it.
 */
extern int mfd_shared_cell_enable(struct platform_device *pdev);
extern int mfd_shared_cell_disable(struct platform_device *pdev);

/*
 * Given a platform device that's been created by mfd_add_devices(), fetch
 * the mfd_cell that created it.
 */
static inline const struct mfd_cell *mfd_get_cell(struct platform_device *pdev)
{
	return pdev->dev.platform_data;
}

/*
 * Given a platform device that's been created by mfd_add_devices(), fetch
 * the .mfd_data entry from the mfd_cell that created it.
 */
static inline void *mfd_get_data(struct platform_device *pdev)
{
	return mfd_get_cell(pdev)->mfd_data;
}

extern int mfd_add_devices(struct device *parent, int id,
			   struct mfd_cell *cells, int n_devs,
			   struct resource *mem_base,
			   int irq_base);

extern void mfd_remove_devices(struct device *parent);

#endif
