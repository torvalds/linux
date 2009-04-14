/*
 * File:	pcieport_if.h
 * Purpose:	PCI Express Port Bus Driver's IF Data Structure
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef _PCIEPORT_IF_H_
#define _PCIEPORT_IF_H_

/* Port Type */
#define PCIE_RC_PORT			4	/* Root port of RC */
#define PCIE_SW_UPSTREAM_PORT		5	/* Upstream port of Switch */
#define PCIE_SW_DOWNSTREAM_PORT		6	/* Downstream port of Switch */
#define PCIE_ANY_PORT			7

/* Service Type */
#define PCIE_PORT_SERVICE_PME_SHIFT	0	/* Power Management Event */
#define PCIE_PORT_SERVICE_PME		(1 << PCIE_PORT_SERVICE_PME_SHIFT)
#define PCIE_PORT_SERVICE_AER_SHIFT	1	/* Advanced Error Reporting */
#define PCIE_PORT_SERVICE_AER		(1 << PCIE_PORT_SERVICE_AER_SHIFT)
#define PCIE_PORT_SERVICE_HP_SHIFT	2	/* Native Hotplug */
#define PCIE_PORT_SERVICE_HP		(1 << PCIE_PORT_SERVICE_HP_SHIFT)
#define PCIE_PORT_SERVICE_VC_SHIFT	3	/* Virtual Channel */
#define PCIE_PORT_SERVICE_VC		(1 << PCIE_PORT_SERVICE_VC_SHIFT)

/* Root/Upstream/Downstream Port's Interrupt Mode */
#define PCIE_PORT_NO_IRQ		(-1)
#define PCIE_PORT_INTx_MODE		0
#define PCIE_PORT_MSI_MODE		1
#define PCIE_PORT_MSIX_MODE		2

struct pcie_port_data {
	int port_type;		/* Type of the port */
	int port_irq_mode;	/* [0:INTx | 1:MSI | 2:MSI-X] */
};

struct pcie_device {
	int 		irq;	    /* Service IRQ/MSI/MSI-X Vector */
	struct pci_dev *port;	    /* Root/Upstream/Downstream Port */
	u32		service;    /* Port service this device represents */
	void		*priv_data; /* Service Private Data */
	struct device	device;     /* Generic Device Interface */
};
#define to_pcie_device(d) container_of(d, struct pcie_device, device)

static inline void set_service_data(struct pcie_device *dev, void *data)
{
	dev->priv_data = data;
}

static inline void* get_service_data(struct pcie_device *dev)
{
	return dev->priv_data;
}

struct pcie_port_service_driver {
	const char *name;
	int (*probe) (struct pcie_device *dev);
	void (*remove) (struct pcie_device *dev);
	int (*suspend) (struct pcie_device *dev);
	int (*resume) (struct pcie_device *dev);

	/* Service Error Recovery Handler */
	struct pci_error_handlers *err_handler;

	/* Link Reset Capability - AER service driver specific */
	pci_ers_result_t (*reset_link) (struct pci_dev *dev);

	int port_type;  /* Type of the port this driver can handle */
	u32 service;    /* Port service this device represents */

	struct device_driver driver;
};
#define to_service_driver(d) \
	container_of(d, struct pcie_port_service_driver, driver)

extern int pcie_port_service_register(struct pcie_port_service_driver *new);
extern void pcie_port_service_unregister(struct pcie_port_service_driver *new);

#endif /* _PCIEPORT_IF_H_ */
