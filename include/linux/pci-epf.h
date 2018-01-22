/**
 * PCI Endpoint *Function* (EPF) header file
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 */

#ifndef __LINUX_PCI_EPF_H
#define __LINUX_PCI_EPF_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/pci.h>

struct pci_epf;

enum pci_barno {
	BAR_0,
	BAR_1,
	BAR_2,
	BAR_3,
	BAR_4,
	BAR_5,
};

/**
 * struct pci_epf_header - represents standard configuration header
 * @vendorid: identifies device manufacturer
 * @deviceid: identifies a particular device
 * @revid: specifies a device-specific revision identifier
 * @progif_code: identifies a specific register-level programming interface
 * @subclass_code: identifies more specifically the function of the device
 * @baseclass_code: broadly classifies the type of function the device performs
 * @cache_line_size: specifies the system cacheline size in units of DWORDs
 * @subsys_vendor_id: vendor of the add-in card or subsystem
 * @subsys_id: id specific to vendor
 * @interrupt_pin: interrupt pin the device (or device function) uses
 */
struct pci_epf_header {
	u16	vendorid;
	u16	deviceid;
	u8	revid;
	u8	progif_code;
	u8	subclass_code;
	u8	baseclass_code;
	u8	cache_line_size;
	u16	subsys_vendor_id;
	u16	subsys_id;
	enum pci_interrupt_pin interrupt_pin;
};

/**
 * struct pci_epf_ops - set of function pointers for performing EPF operations
 * @bind: ops to perform when a EPC device has been bound to EPF device
 * @unbind: ops to perform when a binding has been lost between a EPC device
 *	    and EPF device
 * @linkup: ops to perform when the EPC device has established a connection with
 *	    a host system
 */
struct pci_epf_ops {
	int	(*bind)(struct pci_epf *epf);
	void	(*unbind)(struct pci_epf *epf);
	void	(*linkup)(struct pci_epf *epf);
};

/**
 * struct pci_epf_driver - represents the PCI EPF driver
 * @probe: ops to perform when a new EPF device has been bound to the EPF driver
 * @remove: ops to perform when the binding between the EPF device and EPF
 *	    driver is broken
 * @driver: PCI EPF driver
 * @ops: set of function pointers for performing EPF operations
 * @owner: the owner of the module that registers the PCI EPF driver
 * @group: configfs group corresponding to the PCI EPF driver
 * @id_table: identifies EPF devices for probing
 */
struct pci_epf_driver {
	int	(*probe)(struct pci_epf *epf);
	int	(*remove)(struct pci_epf *epf);

	struct device_driver	driver;
	struct pci_epf_ops	*ops;
	struct module		*owner;
	struct config_group	*group;
	const struct pci_epf_device_id	*id_table;
};

#define to_pci_epf_driver(drv) (container_of((drv), struct pci_epf_driver, \
				driver))

/**
 * struct pci_epf_bar - represents the BAR of EPF device
 * @phys_addr: physical address that should be mapped to the BAR
 * @size: the size of the address space present in BAR
 */
struct pci_epf_bar {
	dma_addr_t	phys_addr;
	size_t		size;
};

/**
 * struct pci_epf - represents the PCI EPF device
 * @dev: the PCI EPF device
 * @name: the name of the PCI EPF device
 * @header: represents standard configuration header
 * @bar: represents the BAR of EPF device
 * @msi_interrupts: number of MSI interrupts required by this function
 * @func_no: unique function number within this endpoint device
 * @epc: the EPC device to which this EPF device is bound
 * @driver: the EPF driver to which this EPF device is bound
 * @list: to add pci_epf as a list of PCI endpoint functions to pci_epc
 */
struct pci_epf {
	struct device		dev;
	const char		*name;
	struct pci_epf_header	*header;
	struct pci_epf_bar	bar[6];
	u8			msi_interrupts;
	u8			func_no;

	struct pci_epc		*epc;
	struct pci_epf_driver	*driver;
	struct list_head	list;
};

#define to_pci_epf(epf_dev) container_of((epf_dev), struct pci_epf, dev)

#define pci_epf_register_driver(driver)    \
		__pci_epf_register_driver((driver), THIS_MODULE)

static inline void epf_set_drvdata(struct pci_epf *epf, void *data)
{
	dev_set_drvdata(&epf->dev, data);
}

static inline void *epf_get_drvdata(struct pci_epf *epf)
{
	return dev_get_drvdata(&epf->dev);
}

const struct pci_epf_device_id *
pci_epf_match_device(const struct pci_epf_device_id *id, struct pci_epf *epf);
struct pci_epf *pci_epf_create(const char *name);
void pci_epf_destroy(struct pci_epf *epf);
int __pci_epf_register_driver(struct pci_epf_driver *driver,
			      struct module *owner);
void pci_epf_unregister_driver(struct pci_epf_driver *driver);
void *pci_epf_alloc_space(struct pci_epf *epf, size_t size, enum pci_barno bar);
void pci_epf_free_space(struct pci_epf *epf, void *addr, enum pci_barno bar);
int pci_epf_bind(struct pci_epf *epf);
void pci_epf_unbind(struct pci_epf *epf);
void pci_epf_linkup(struct pci_epf *epf);
#endif /* __LINUX_PCI_EPF_H */
