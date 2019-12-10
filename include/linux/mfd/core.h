/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/mfd/mfd-core.h
 *
 * core MFD support
 * Copyright (c) 2006 Ian Molton
 * Copyright (c) 2007 Dmitry Baryshkov
 */

#ifndef MFD_CORE_H
#define MFD_CORE_H

#include <linux/platform_device.h>

#define MFD_RES_SIZE(arr) (sizeof(arr) / sizeof(struct resource))

#define MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, _compat, _match)\
	{								\
		.name = (_name),					\
		.resources = (_res),					\
		.num_resources = MFD_RES_SIZE((_res)),			\
		.platform_data = (_pdata),				\
		.pdata_size = (_pdsize),				\
		.of_compatible = (_compat),				\
		.acpi_match = (_match),					\
		.id = (_id),						\
	}

#define OF_MFD_CELL(_name, _res, _pdata, _pdsize,_id, _compat)		\
	MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, _compat, NULL)	\

#define ACPI_MFD_CELL(_name, _res, _pdata, _pdsize, _id, _match)	\
	MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, NULL, _match)	\

#define MFD_CELL_BASIC(_name, _res, _pdata, _pdsize, _id)		\
	MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, NULL, NULL)	\

#define MFD_CELL_RES(_name, _res)					\
	MFD_CELL_ALL(_name, _res, NULL, 0, 0, NULL, NULL)		\

#define MFD_CELL_NAME(_name)						\
	MFD_CELL_ALL(_name, NULL, NULL, 0, 0, NULL, NULL)		\

struct irq_domain;
struct property_entry;

/* Matches ACPI PNP id, either _HID or _CID, or ACPI _ADR */
struct mfd_cell_acpi_match {
	const char			*pnpid;
	const unsigned long long	adr;
};

/*
 * This struct describes the MFD part ("cell").
 * After registration the copy of this structure will become the platform data
 * of the resulting platform_device
 */
struct mfd_cell {
	const char		*name;
	int			id;

	int			(*enable)(struct platform_device *dev);
	int			(*disable)(struct platform_device *dev);

	int			(*suspend)(struct platform_device *dev);
	int			(*resume)(struct platform_device *dev);

	/* platform data passed to the sub devices drivers */
	void			*platform_data;
	size_t			pdata_size;

	/* device properties passed to the sub devices drivers */
	struct property_entry *properties;

	/*
	 * Device Tree compatible string
	 * See: Documentation/devicetree/usage-model.txt Chapter 2.2 for details
	 */
	const char		*of_compatible;

	/* Matches ACPI */
	const struct mfd_cell_acpi_match	*acpi_match;

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

	/* A list of regulator supplies that should be mapped to the MFD
	 * device rather than the child device when requested
	 */
	const char * const	*parent_supplies;
	int			num_parent_supplies;
};

/*
 * Convenience functions for clients using shared cells.  Refcounting
 * happens automatically, with the cell's enable/disable callbacks
 * being called only when a device is first being enabled or no other
 * clients are making use of it.
 */
extern int mfd_cell_enable(struct platform_device *pdev);
extern int mfd_cell_disable(struct platform_device *pdev);

/*
 * Given a platform device that's been created by mfd_add_devices(), fetch
 * the mfd_cell that created it.
 */
static inline const struct mfd_cell *mfd_get_cell(struct platform_device *pdev)
{
	return pdev->mfd_cell;
}

extern int mfd_add_devices(struct device *parent, int id,
			   const struct mfd_cell *cells, int n_devs,
			   struct resource *mem_base,
			   int irq_base, struct irq_domain *irq_domain);

static inline int mfd_add_hotplug_devices(struct device *parent,
		const struct mfd_cell *cells, int n_devs)
{
	return mfd_add_devices(parent, PLATFORM_DEVID_AUTO, cells, n_devs,
			NULL, 0, NULL);
}

extern void mfd_remove_devices(struct device *parent);

extern int devm_mfd_add_devices(struct device *dev, int id,
				const struct mfd_cell *cells, int n_devs,
				struct resource *mem_base,
				int irq_base, struct irq_domain *irq_domain);
#endif
