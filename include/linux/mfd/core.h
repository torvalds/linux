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

#define MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, _compat, _of_reg, _use_of_reg, _match) \
	{								\
		.name = (_name),					\
		.resources = (_res),					\
		.num_resources = MFD_RES_SIZE((_res)),			\
		.platform_data = (_pdata),				\
		.pdata_size = (_pdsize),				\
		.of_compatible = (_compat),				\
		.of_reg = (_of_reg),					\
		.use_of_reg = (_use_of_reg),				\
		.acpi_match = (_match),					\
		.id = (_id),						\
	}

#define MFD_CELL_OF_REG(_name, _res, _pdata, _pdsize, _id, _compat, _of_reg) \
	MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, _compat, _of_reg, true, NULL)

#define MFD_CELL_OF(_name, _res, _pdata, _pdsize, _id, _compat) \
	MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, _compat, 0, false, NULL)

#define MFD_CELL_ACPI(_name, _res, _pdata, _pdsize, _id, _match) \
	MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, NULL, 0, false, _match)

#define MFD_CELL_BASIC(_name, _res, _pdata, _pdsize, _id) \
	MFD_CELL_ALL(_name, _res, _pdata, _pdsize, _id, NULL, 0, false, NULL)

#define MFD_CELL_RES(_name, _res) \
	MFD_CELL_ALL(_name, _res, NULL, 0, 0, NULL, 0, false, NULL)

#define MFD_CELL_NAME(_name) \
	MFD_CELL_ALL(_name, NULL, NULL, 0, 0, NULL, 0, false, NULL)

#define MFD_DEP_LEVEL_NORMAL 0
#define MFD_DEP_LEVEL_HIGH 1

struct irq_domain;
struct software_node;

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
	int			level;

	int			(*suspend)(struct platform_device *dev);
	int			(*resume)(struct platform_device *dev);

	/* platform data passed to the sub devices drivers */
	void			*platform_data;
	size_t			pdata_size;

	/* Matches ACPI */
	const struct mfd_cell_acpi_match	*acpi_match;

	/* Software node for the device. */
	const struct software_node *swnode;

	/*
	 * Device Tree compatible string
	 * See: Documentation/devicetree/usage-model.rst Chapter 2.2 for details
	 */
	const char		*of_compatible;

	/*
	 * Address as defined in Device Tree.  Used to complement 'of_compatible'
	 * (above) when matching OF nodes with devices that have identical
	 * compatible strings
	 */
	const u64 of_reg;

	/* Set to 'true' to use 'of_reg' (above) - allows for of_reg=0 */
	bool use_of_reg;

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
	int			num_parent_supplies;
	const char * const	*parent_supplies;
};

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
extern void mfd_remove_devices_late(struct device *parent);

extern int devm_mfd_add_devices(struct device *dev, int id,
				const struct mfd_cell *cells, int n_devs,
				struct resource *mem_base,
				int irq_base, struct irq_domain *irq_domain);
#endif
