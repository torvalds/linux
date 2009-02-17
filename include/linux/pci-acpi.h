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
	if (pbus->parent)
		return DEVICE_ACPI_HANDLE(&(pbus->self->dev));
	return acpi_get_pci_rootbridge_handle(pci_domain_nr(pbus),
					      pbus->number);
}
#else
static inline acpi_handle acpi_find_root_bridge_handle(struct pci_dev *pdev)
{ return NULL; }
#endif

#endif	/* _PCI_ACPI_H_ */
