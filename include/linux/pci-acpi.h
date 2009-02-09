/*
 * File		pci-acpi.h
 *
 * Copyright (C) 2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef _PCI_ACPI_H_
#define _PCI_ACPI_H_

#include <linux/acpi.h>

#ifdef CONFIG_ACPI
static inline acpi_handle acpi_find_root_bridge_handle(struct pci_dev *pdev)
{
	/* Find root host bridge */
	while (pdev->bus->self)
		pdev = pdev->bus->self;

	return acpi_get_pci_rootbridge_handle(pci_domain_nr(pdev->bus),
			pdev->bus->number);
}

static inline acpi_handle acpi_pci_get_bridge_handle(struct pci_bus *pbus)
{
	int seg = pci_domain_nr(pbus), busnr = pbus->number;
	struct pci_dev *bridge = pbus->self;
	if (bridge)
		return DEVICE_ACPI_HANDLE(&(bridge->dev));
	return acpi_get_pci_rootbridge_handle(seg, busnr);
}
#else
static inline acpi_handle acpi_find_root_bridge_handle(struct pci_dev *pdev)
{ return NULL; }
#endif

#endif	/* _PCI_ACPI_H_ */
