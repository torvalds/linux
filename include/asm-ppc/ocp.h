/*
 * ocp.h
 *
 *      (c) Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *          Mipsys - France
 *
 *          Derived from work (c) Armin Kuster akuster@pacbell.net
 *
 *          Additional support and port to 2.6 LDM/sysfs by
 *          Matt Porter <mporter@kernel.crashing.org>
 *          Copyright 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 *  TODO: - Add get/put interface & fixup locking to provide same API for
 *          2.4 and 2.5
 *	  - Rework PM callbacks
 */

#ifdef __KERNEL__
#ifndef __OCP_H__
#define __OCP_H__

#include <linux/init.h>
#include <linux/list.h>
#include <linux/config.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/device.h>

#include <asm/mmu.h>
#include <asm/ocp_ids.h>
#include <asm/rwsem.h>
#include <asm/semaphore.h>

#ifdef CONFIG_PPC_OCP

#define OCP_MAX_IRQS	7
#define MAX_EMACS	4
#define OCP_IRQ_NA	-1	/* used when ocp device does not have an irq */
#define OCP_IRQ_MUL	-2	/* used for ocp devices with multiply irqs */
#define OCP_NULL_TYPE	-1	/* used to mark end of list */
#define OCP_CPM_NA	0	/* No Clock or Power Management avaliable */
#define OCP_PADDR_NA	0	/* No MMIO registers */

#define OCP_ANY_ID	(~0)
#define OCP_ANY_INDEX	-1

extern struct list_head 	ocp_devices;
extern struct rw_semaphore	ocp_devices_sem;

struct ocp_device_id {
	unsigned int	vendor, function;	/* Vendor and function ID or OCP_ANY_ID */
	unsigned long	driver_data;		/* Data private to the driver */
};


/*
 * Static definition of an OCP device.
 *
 * @vendor:    Vendor code. It is _STRONGLY_ discouraged to use
 *             the vendor code as a way to match a unique device,
 *             though I kept that possibility open, you should
 *             really define different function codes for different
 *             device types
 * @function:  This is the function code for this device.
 * @index:     This index is used for mapping the Nth function of a
 *             given core. This is typically used for cross-driver
 *             matching, like looking for a given MAL or ZMII from
 *             an EMAC or for getting to the proper set of DCRs.
 *             Indices are no longer magically calculated based on
 *             structure ordering, they have to be actually coded
 *             into the ocp_def to avoid any possible confusion
 *             I _STRONGLY_ (again ? wow !) encourage anybody relying
 *             on index mapping to encode the "target" index in an
 *             associated structure pointed to by "additions", see
 *             how it's done for the EMAC driver.
 * @paddr:     Device physical address (may not mean anything...)
 * @irq:       Interrupt line for this device (TODO: think about making
 *             an array with this)
 * @pm:        Currently, contains the bitmask in CPMFR DCR for the device
 * @additions: Optionally points to a function specific structure
 *             providing additional informations for a given device
 *             instance. It's currently used by the EMAC driver for MAL
 *             channel & ZMII port mapping among others.
 * @show:      Optionally points to a function specific structure
 *             providing a sysfs show routine for additions fields.
 */
struct ocp_def {
	unsigned int	vendor;
	unsigned int	function;
	int		index;
	phys_addr_t	paddr;
	int	  	irq;
	unsigned long	pm;
	void		*additions;
	void		(*show)(struct device *);
};


/* Struct for a given device instance */
struct ocp_device {
	struct list_head	link;
	char			name[80];	/* device name */
	struct ocp_def		*def;		/* device definition */
	void			*drvdata;	/* driver data for this device */
	struct ocp_driver	*driver;
	u32			current_state;	/* Current operating state. In ACPI-speak,
						   this is D0-D3, D0 being fully functional,
						   and D3 being off. */
	struct			device dev;
};

struct ocp_driver {
	struct list_head node;
	char *name;
	const struct ocp_device_id *id_table;	/* NULL if wants all devices */
	int  (*probe)  (struct ocp_device *dev);	/* New device inserted */
	void (*remove) (struct ocp_device *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	int  (*suspend) (struct ocp_device *dev, u32 state);	/* Device suspended */
	int  (*resume) (struct ocp_device *dev);	                /* Device woken up */
	struct device_driver driver;
};

#define to_ocp_dev(n) container_of(n, struct ocp_device, dev)
#define to_ocp_drv(n) container_of(n, struct ocp_driver, driver)

/* Similar to the helpers above, these manipulate per-ocp_dev
 * driver-specific data.  Currently stored as ocp_dev::ocpdev,
 * a void pointer, but it is not present on older kernels.
 */
static inline void *
ocp_get_drvdata(struct ocp_device *pdev)
{
	return pdev->drvdata;
}

static inline void
ocp_set_drvdata(struct ocp_device *pdev, void *data)
{
	pdev->drvdata = data;
}

#if defined (CONFIG_PM)
/*
 * This is right for the IBM 405 and 440 but will need to be
 * generalized if the OCP stuff gets used on other processors.
 */
static inline void
ocp_force_power_off(struct ocp_device *odev)
{
	mtdcr(DCRN_CPMFR, mfdcr(DCRN_CPMFR) | odev->def->pm);
}

static inline void
ocp_force_power_on(struct ocp_device *odev)
{
	mtdcr(DCRN_CPMFR, mfdcr(DCRN_CPMFR) & ~odev->def->pm);
}
#else
#define ocp_force_power_off(x)	(void)(x)
#define ocp_force_power_on(x)	(void)(x)
#endif

/* Register/Unregister an OCP driver */
extern int ocp_register_driver(struct ocp_driver *drv);
extern void ocp_unregister_driver(struct ocp_driver *drv);

/* Build list of devices */
extern int ocp_early_init(void) __init;

/* Find a device by index */
extern struct ocp_device *ocp_find_device(unsigned int vendor, unsigned int function, int index);

/* Get a def by index */
extern struct ocp_def *ocp_get_one_device(unsigned int vendor, unsigned int function, int index);

/* Add a device by index */
extern int ocp_add_one_device(struct ocp_def *def);

/* Remove a device by index */
extern int ocp_remove_one_device(unsigned int vendor, unsigned int function, int index);

/* Iterate over devices and execute a routine */
extern void ocp_for_each_device(void(*callback)(struct ocp_device *, void *arg), void *arg);

/* Sysfs support */
#define OCP_SYSFS_ADDTL(type, format, name, field)			\
static ssize_t								\
show_##name##_##field(struct device *dev, char *buf)			\
{									\
	struct ocp_device *odev = to_ocp_dev(dev);			\
	type *add = odev->def->additions;				\
									\
	return sprintf(buf, format, add->field);			\
}									\
static DEVICE_ATTR(name##_##field, S_IRUGO, show_##name##_##field, NULL);

#ifdef CONFIG_IBM_OCP
#include <asm/ibm_ocp.h>
#endif

#ifdef CONFIG_FSL_OCP
#include <asm/fsl_ocp.h>
#endif

#endif				/* CONFIG_PPC_OCP */
#endif				/* __OCP_H__ */
#endif				/* __KERNEL__ */
