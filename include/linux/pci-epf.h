/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCI Endpoint *Function* (EPF) header file
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#ifndef __LINUX_PCI_EPF_H
#define __LINUX_PCI_EPF_H

#include <linux/configfs.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/pci.h>

struct pci_epf;
enum pci_epc_interface_type;

enum pci_barno {
	NO_BAR = -1,
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
 * @add_cfs: ops to initialize function specific configfs attributes
 */
struct pci_epf_ops {
	int	(*bind)(struct pci_epf *epf);
	void	(*unbind)(struct pci_epf *epf);
	struct config_group *(*add_cfs)(struct pci_epf *epf,
					struct config_group *group);
};

/**
 * struct pci_epf_event_ops - Callbacks for capturing the EPC events
 * @core_init: Callback for the EPC initialization complete event
 * @link_up: Callback for the EPC link up event
 * @link_down: Callback for the EPC link down event
 * @bme: Callback for the EPC BME (Bus Master Enable) event
 */
struct pci_epc_event_ops {
	int (*core_init)(struct pci_epf *epf);
	int (*link_up)(struct pci_epf *epf);
	int (*link_down)(struct pci_epf *epf);
	int (*bme)(struct pci_epf *epf);
};

/**
 * struct pci_epf_driver - represents the PCI EPF driver
 * @probe: ops to perform when a new EPF device has been bound to the EPF driver
 * @remove: ops to perform when the binding between the EPF device and EPF
 *	    driver is broken
 * @driver: PCI EPF driver
 * @ops: set of function pointers for performing EPF operations
 * @owner: the owner of the module that registers the PCI EPF driver
 * @epf_group: list of configfs group corresponding to the PCI EPF driver
 * @id_table: identifies EPF devices for probing
 */
struct pci_epf_driver {
	int	(*probe)(struct pci_epf *epf,
			 const struct pci_epf_device_id *id);
	void	(*remove)(struct pci_epf *epf);

	struct device_driver	driver;
	struct pci_epf_ops	*ops;
	struct module		*owner;
	struct list_head	epf_group;
	const struct pci_epf_device_id	*id_table;
};

#define to_pci_epf_driver(drv) (container_of((drv), struct pci_epf_driver, \
				driver))

/**
 * struct pci_epf_bar - represents the BAR of EPF device
 * @phys_addr: physical address that should be mapped to the BAR
 * @addr: virtual address corresponding to the @phys_addr
 * @size: the size of the address space present in BAR
 * @barno: BAR number
 * @flags: flags that are set for the BAR
 */
struct pci_epf_bar {
	dma_addr_t	phys_addr;
	void		*addr;
	size_t		size;
	enum pci_barno	barno;
	int		flags;
};

/**
 * struct pci_epf - represents the PCI EPF device
 * @dev: the PCI EPF device
 * @name: the name of the PCI EPF device
 * @header: represents standard configuration header
 * @bar: represents the BAR of EPF device
 * @msi_interrupts: number of MSI interrupts required by this function
 * @msix_interrupts: number of MSI-X interrupts required by this function
 * @func_no: unique (physical) function number within this endpoint device
 * @vfunc_no: unique virtual function number within a physical function
 * @epc: the EPC device to which this EPF device is bound
 * @epf_pf: the physical EPF device to which this virtual EPF device is bound
 * @driver: the EPF driver to which this EPF device is bound
 * @id: Pointer to the EPF device ID
 * @list: to add pci_epf as a list of PCI endpoint functions to pci_epc
 * @lock: mutex to protect pci_epf_ops
 * @sec_epc: the secondary EPC device to which this EPF device is bound
 * @sec_epc_list: to add pci_epf as list of PCI endpoint functions to secondary
 *   EPC device
 * @sec_epc_bar: represents the BAR of EPF device associated with secondary EPC
 * @sec_epc_func_no: unique (physical) function number within the secondary EPC
 * @group: configfs group associated with the EPF device
 * @is_bound: indicates if bind notification to function driver has been invoked
 * @is_vf: true - virtual function, false - physical function
 * @vfunction_num_map: bitmap to manage virtual function number
 * @pci_vepf: list of virtual endpoint functions associated with this function
 * @event_ops: Callbacks for capturing the EPC events
 */
struct pci_epf {
	struct device		dev;
	const char		*name;
	struct pci_epf_header	*header;
	struct pci_epf_bar	bar[6];
	u8			msi_interrupts;
	u16			msix_interrupts;
	u8			func_no;
	u8			vfunc_no;

	struct pci_epc		*epc;
	struct pci_epf		*epf_pf;
	struct pci_epf_driver	*driver;
	const struct pci_epf_device_id *id;
	struct list_head	list;
	/* mutex to protect against concurrent access of pci_epf_ops */
	struct mutex		lock;

	/* Below members are to attach secondary EPC to an endpoint function */
	struct pci_epc		*sec_epc;
	struct list_head	sec_epc_list;
	struct pci_epf_bar	sec_epc_bar[6];
	u8			sec_epc_func_no;
	struct config_group	*group;
	unsigned int		is_bound;
	unsigned int		is_vf;
	unsigned long		vfunction_num_map;
	struct list_head	pci_vepf;
	const struct pci_epc_event_ops *event_ops;
};

/**
 * struct pci_epf_msix_tbl - represents the MSIX table entry structure
 * @msg_addr: Writes to this address will trigger MSIX interrupt in host
 * @msg_data: Data that should be written to @msg_addr to trigger MSIX interrupt
 * @vector_ctrl: Identifies if the function is prohibited from sending a message
 * using this MSIX table entry
 */
struct pci_epf_msix_tbl {
	u64 msg_addr;
	u32 msg_data;
	u32 vector_ctrl;
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

struct pci_epf *pci_epf_create(const char *name);
void pci_epf_destroy(struct pci_epf *epf);
int __pci_epf_register_driver(struct pci_epf_driver *driver,
			      struct module *owner);
void pci_epf_unregister_driver(struct pci_epf_driver *driver);
void *pci_epf_alloc_space(struct pci_epf *epf, size_t size, enum pci_barno bar,
			  size_t align, enum pci_epc_interface_type type);
void pci_epf_free_space(struct pci_epf *epf, void *addr, enum pci_barno bar,
			enum pci_epc_interface_type type);
int pci_epf_bind(struct pci_epf *epf);
void pci_epf_unbind(struct pci_epf *epf);
int pci_epf_add_vepf(struct pci_epf *epf_pf, struct pci_epf *epf_vf);
void pci_epf_remove_vepf(struct pci_epf *epf_pf, struct pci_epf *epf_vf);
#endif /* __LINUX_PCI_EPF_H */
