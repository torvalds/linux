/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCI Endpoint *Controller* (EPC) header file
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#ifndef __LINUX_PCI_EPC_H
#define __LINUX_PCI_EPC_H

#include <linux/pci-epf.h>

struct pci_epc;

enum pci_epc_interface_type {
	UNKNOWN_INTERFACE = -1,
	PRIMARY_INTERFACE,
	SECONDARY_INTERFACE,
};

static inline const char *
pci_epc_interface_string(enum pci_epc_interface_type type)
{
	switch (type) {
	case PRIMARY_INTERFACE:
		return "primary";
	case SECONDARY_INTERFACE:
		return "secondary";
	default:
		return "UNKNOWN interface";
	}
}

/**
 * struct pci_epc_ops - set of function pointers for performing EPC operations
 * @write_header: ops to populate configuration space header
 * @set_bar: ops to configure the BAR
 * @clear_bar: ops to reset the BAR
 * @map_addr: ops to map CPU address to PCI address
 * @unmap_addr: ops to unmap CPU address and PCI address
 * @set_msi: ops to set the requested number of MSI interrupts in the MSI
 *	     capability register
 * @get_msi: ops to get the number of MSI interrupts allocated by the RC from
 *	     the MSI capability register
 * @set_msix: ops to set the requested number of MSI-X interrupts in the
 *	     MSI-X capability register
 * @get_msix: ops to get the number of MSI-X interrupts allocated by the RC
 *	     from the MSI-X capability register
 * @raise_irq: ops to raise a legacy, MSI or MSI-X interrupt
 * @map_msi_irq: ops to map physical address to MSI address and return MSI data
 * @start: ops to start the PCI link
 * @stop: ops to stop the PCI link
 * @get_features: ops to get the features supported by the EPC
 * @owner: the module owner containing the ops
 */
struct pci_epc_ops {
	int	(*write_header)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
				struct pci_epf_header *hdr);
	int	(*set_bar)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			   struct pci_epf_bar *epf_bar);
	void	(*clear_bar)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			     struct pci_epf_bar *epf_bar);
	int	(*map_addr)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			    phys_addr_t addr, u64 pci_addr, size_t size);
	void	(*unmap_addr)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			      phys_addr_t addr);
	int	(*set_msi)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			   u8 interrupts);
	int	(*get_msi)(struct pci_epc *epc, u8 func_no, u8 vfunc_no);
	int	(*set_msix)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			    u16 interrupts, enum pci_barno, u32 offset);
	int	(*get_msix)(struct pci_epc *epc, u8 func_no, u8 vfunc_no);
	int	(*raise_irq)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			     unsigned int type, u16 interrupt_num);
	int	(*map_msi_irq)(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			       phys_addr_t phys_addr, u8 interrupt_num,
			       u32 entry_size, u32 *msi_data,
			       u32 *msi_addr_offset);
	int	(*start)(struct pci_epc *epc);
	void	(*stop)(struct pci_epc *epc);
	const struct pci_epc_features* (*get_features)(struct pci_epc *epc,
						       u8 func_no, u8 vfunc_no);
	struct module *owner;
};

/**
 * struct pci_epc_mem_window - address window of the endpoint controller
 * @phys_base: physical base address of the PCI address window
 * @size: the size of the PCI address window
 * @page_size: size of each page
 */
struct pci_epc_mem_window {
	phys_addr_t	phys_base;
	size_t		size;
	size_t		page_size;
};

/**
 * struct pci_epc_mem - address space of the endpoint controller
 * @window: address window of the endpoint controller
 * @bitmap: bitmap to manage the PCI address space
 * @pages: number of bits representing the address region
 * @lock: mutex to protect bitmap
 */
struct pci_epc_mem {
	struct pci_epc_mem_window window;
	unsigned long	*bitmap;
	int		pages;
	/* mutex to protect against concurrent access for memory allocation*/
	struct mutex	lock;
};

/**
 * struct pci_epc - represents the PCI EPC device
 * @dev: PCI EPC device
 * @pci_epf: list of endpoint functions present in this EPC device
 * @list_lock: Mutex for protecting pci_epf list
 * @ops: function pointers for performing endpoint operations
 * @windows: array of address space of the endpoint controller
 * @mem: first window of the endpoint controller, which corresponds to
 *       default address space of the endpoint controller supporting
 *       single window.
 * @num_windows: number of windows supported by device
 * @max_functions: max number of functions that can be configured in this EPC
 * @max_vfs: Array indicating the maximum number of virtual functions that can
 *   be associated with each physical function
 * @group: configfs group representing the PCI EPC device
 * @lock: mutex to protect pci_epc ops
 * @function_num_map: bitmap to manage physical function number
 * @domain_nr: PCI domain number of the endpoint controller
 * @init_complete: flag to indicate whether the EPC initialization is complete
 *                 or not
 */
struct pci_epc {
	struct device			dev;
	struct list_head		pci_epf;
	struct mutex			list_lock;
	const struct pci_epc_ops	*ops;
	struct pci_epc_mem		**windows;
	struct pci_epc_mem		*mem;
	unsigned int			num_windows;
	u8				max_functions;
	u8				*max_vfs;
	struct config_group		*group;
	/* mutex to protect against concurrent access of EP controller */
	struct mutex			lock;
	unsigned long			function_num_map;
	int				domain_nr;
	bool				init_complete;
};

/**
 * @BAR_PROGRAMMABLE: The BAR mask can be configured by the EPC.
 * @BAR_FIXED: The BAR mask is fixed by the hardware.
 * @BAR_RESERVED: The BAR should not be touched by an EPF driver.
 */
enum pci_epc_bar_type {
	BAR_PROGRAMMABLE = 0,
	BAR_FIXED,
	BAR_RESERVED,
};

/**
 * struct pci_epc_bar_desc - hardware description for a BAR
 * @type: the type of the BAR
 * @fixed_size: the fixed size, only applicable if type is BAR_FIXED_MASK.
 * @only_64bit: if true, an EPF driver is not allowed to choose if this BAR
 *		should be configured as 32-bit or 64-bit, the EPF driver must
 *		configure this BAR as 64-bit. Additionally, the BAR succeeding
 *		this BAR must be set to type BAR_RESERVED.
 *
 *		only_64bit should not be set on a BAR of type BAR_RESERVED.
 *		(If BARx is a 64-bit BAR that an EPF driver is not allowed to
 *		touch, then both BARx and BARx+1 must be set to type
 *		BAR_RESERVED.)
 */
struct pci_epc_bar_desc {
	enum pci_epc_bar_type type;
	u64 fixed_size;
	bool only_64bit;
};

/**
 * struct pci_epc_features - features supported by a EPC device per function
 * @linkup_notifier: indicate if the EPC device can notify EPF driver on link up
 * @msi_capable: indicate if the endpoint function has MSI capability
 * @msix_capable: indicate if the endpoint function has MSI-X capability
 * @bar: array specifying the hardware description for each BAR
 * @align: alignment size required for BAR buffer allocation
 */
struct pci_epc_features {
	unsigned int	linkup_notifier : 1;
	unsigned int	msi_capable : 1;
	unsigned int	msix_capable : 1;
	struct	pci_epc_bar_desc bar[PCI_STD_NUM_BARS];
	size_t	align;
};

#define to_pci_epc(device) container_of((device), struct pci_epc, dev)

#ifdef CONFIG_PCI_ENDPOINT

#define pci_epc_create(dev, ops)    \
		__pci_epc_create((dev), (ops), THIS_MODULE)
#define devm_pci_epc_create(dev, ops)    \
		__devm_pci_epc_create((dev), (ops), THIS_MODULE)

static inline void epc_set_drvdata(struct pci_epc *epc, void *data)
{
	dev_set_drvdata(&epc->dev, data);
}

static inline void *epc_get_drvdata(struct pci_epc *epc)
{
	return dev_get_drvdata(&epc->dev);
}

struct pci_epc *
__devm_pci_epc_create(struct device *dev, const struct pci_epc_ops *ops,
		      struct module *owner);
struct pci_epc *
__pci_epc_create(struct device *dev, const struct pci_epc_ops *ops,
		 struct module *owner);
void devm_pci_epc_destroy(struct device *dev, struct pci_epc *epc);
void pci_epc_destroy(struct pci_epc *epc);
int pci_epc_add_epf(struct pci_epc *epc, struct pci_epf *epf,
		    enum pci_epc_interface_type type);
void pci_epc_linkup(struct pci_epc *epc);
void pci_epc_linkdown(struct pci_epc *epc);
void pci_epc_init_notify(struct pci_epc *epc);
void pci_epc_notify_pending_init(struct pci_epc *epc, struct pci_epf *epf);
void pci_epc_deinit_notify(struct pci_epc *epc);
void pci_epc_bus_master_enable_notify(struct pci_epc *epc);
void pci_epc_remove_epf(struct pci_epc *epc, struct pci_epf *epf,
			enum pci_epc_interface_type type);
int pci_epc_write_header(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			 struct pci_epf_header *hdr);
int pci_epc_set_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		    struct pci_epf_bar *epf_bar);
void pci_epc_clear_bar(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		       struct pci_epf_bar *epf_bar);
int pci_epc_map_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		     phys_addr_t phys_addr,
		     u64 pci_addr, size_t size);
void pci_epc_unmap_addr(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			phys_addr_t phys_addr);
int pci_epc_set_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		    u8 interrupts);
int pci_epc_get_msi(struct pci_epc *epc, u8 func_no, u8 vfunc_no);
int pci_epc_set_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		     u16 interrupts, enum pci_barno, u32 offset);
int pci_epc_get_msix(struct pci_epc *epc, u8 func_no, u8 vfunc_no);
int pci_epc_map_msi_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
			phys_addr_t phys_addr, u8 interrupt_num,
			u32 entry_size, u32 *msi_data, u32 *msi_addr_offset);
int pci_epc_raise_irq(struct pci_epc *epc, u8 func_no, u8 vfunc_no,
		      unsigned int type, u16 interrupt_num);
int pci_epc_start(struct pci_epc *epc);
void pci_epc_stop(struct pci_epc *epc);
const struct pci_epc_features *pci_epc_get_features(struct pci_epc *epc,
						    u8 func_no, u8 vfunc_no);
enum pci_barno
pci_epc_get_first_free_bar(const struct pci_epc_features *epc_features);
enum pci_barno pci_epc_get_next_free_bar(const struct pci_epc_features
					 *epc_features, enum pci_barno bar);
struct pci_epc *pci_epc_get(const char *epc_name);
void pci_epc_put(struct pci_epc *epc);

int pci_epc_mem_init(struct pci_epc *epc, phys_addr_t base,
		     size_t size, size_t page_size);
int pci_epc_multi_mem_init(struct pci_epc *epc,
			   struct pci_epc_mem_window *window,
			   unsigned int num_windows);
void pci_epc_mem_exit(struct pci_epc *epc);
void __iomem *pci_epc_mem_alloc_addr(struct pci_epc *epc,
				     phys_addr_t *phys_addr, size_t size);
void pci_epc_mem_free_addr(struct pci_epc *epc, phys_addr_t phys_addr,
			   void __iomem *virt_addr, size_t size);

#else
static inline void pci_epc_init_notify(struct pci_epc *epc)
{
}

static inline void pci_epc_deinit_notify(struct pci_epc *epc)
{
}
#endif /* CONFIG_PCI_ENDPOINT */
#endif /* __LINUX_PCI_EPC_H */
