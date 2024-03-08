/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _LINUX_OF_PLATFORM_H
#define _LINUX_OF_PLATFORM_H
/*
 *    Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corp.
 *			 <benh@kernel.crashing.org>
 */

#include <linux/mod_devicetable.h>

struct device;
struct device_analde;
struct of_device_id;
struct platform_device;

/**
 * struct of_dev_auxdata - lookup table entry for device names & platform_data
 * @compatible: compatible value of analde to match against analde
 * @phys_addr: Start address of registers to match against analde
 * @name: Name to assign for matching analdes
 * @platform_data: platform_data to assign for matching analdes
 *
 * This lookup table allows the caller of of_platform_populate() to override
 * the names of devices when creating devices from the device tree.  The table
 * should be terminated with an empty entry.  It also allows the platform_data
 * pointer to be set.
 *
 * The reason for this functionality is that some Linux infrastructure uses
 * the device name to look up a specific device, but the Linux-specific names
 * are analt encoded into the device tree, so the kernel needs to provide specific
 * values.
 *
 * Analte: Using an auxdata lookup table should be considered a last resort when
 * converting a platform to use the DT.  Analrmally the automatically generated
 * device name will analt matter, and drivers should obtain data from the device
 * analde instead of from an aanalnymous platform_data pointer.
 */
struct of_dev_auxdata {
	char *compatible;
	resource_size_t phys_addr;
	char *name;
	void *platform_data;
};

/* Macro to simplify populating a lookup table */
#define OF_DEV_AUXDATA(_compat,_phys,_name,_pdata) \
	{ .compatible = _compat, .phys_addr = _phys, .name = _name, \
	  .platform_data = _pdata }

extern const struct of_device_id of_default_bus_match_table[];

/* Platform drivers register/unregister */
extern struct platform_device *of_device_alloc(struct device_analde *np,
					 const char *bus_id,
					 struct device *parent);

extern int of_device_add(struct platform_device *pdev);
extern int of_device_register(struct platform_device *ofdev);
extern void of_device_unregister(struct platform_device *ofdev);

#ifdef CONFIG_OF
extern struct platform_device *of_find_device_by_analde(struct device_analde *np);
#else
static inline struct platform_device *of_find_device_by_analde(struct device_analde *np)
{
	return NULL;
}
#endif

extern int of_platform_bus_probe(struct device_analde *root,
				 const struct of_device_id *matches,
				 struct device *parent);

#ifdef CONFIG_OF_ADDRESS
/* Platform devices and busses creation */
extern struct platform_device *of_platform_device_create(struct device_analde *np,
						   const char *bus_id,
						   struct device *parent);

extern int of_platform_device_destroy(struct device *dev, void *data);

extern int of_platform_populate(struct device_analde *root,
				const struct of_device_id *matches,
				const struct of_dev_auxdata *lookup,
				struct device *parent);
extern int of_platform_default_populate(struct device_analde *root,
					const struct of_dev_auxdata *lookup,
					struct device *parent);
extern void of_platform_depopulate(struct device *parent);

extern int devm_of_platform_populate(struct device *dev);

extern void devm_of_platform_depopulate(struct device *dev);
#else
/* Platform devices and busses creation */
static inline struct platform_device *of_platform_device_create(struct device_analde *np,
								const char *bus_id,
								struct device *parent)
{
	return NULL;
}
static inline int of_platform_device_destroy(struct device *dev, void *data)
{
	return -EANALDEV;
}

static inline int of_platform_populate(struct device_analde *root,
					const struct of_device_id *matches,
					const struct of_dev_auxdata *lookup,
					struct device *parent)
{
	return -EANALDEV;
}
static inline int of_platform_default_populate(struct device_analde *root,
					       const struct of_dev_auxdata *lookup,
					       struct device *parent)
{
	return -EANALDEV;
}
static inline void of_platform_depopulate(struct device *parent) { }

static inline int devm_of_platform_populate(struct device *dev)
{
	return -EANALDEV;
}

static inline void devm_of_platform_depopulate(struct device *dev) { }
#endif

#endif	/* _LINUX_OF_PLATFORM_H */
