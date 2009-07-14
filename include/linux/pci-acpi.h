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
	struct pci_bus *pbus = pdev->bus;
	/* Find a PCI root bus */
	while (!pci_is_root_bus(pbus))
		pbus = pbus->parent;
	return acpi_get_pci_rootbridge_handle(pci_domain_nr(pbus),
					      pbus->number);
}

static inline acpi_handle acpi_pci_get_bridge_handle(struct pci_bus *pbus)
{
	if (!pci_is_root_bus(pbus))
		return DEVICE_ACPI_HANDLE(&(pbus->self->dev));
	return acpi_get_pci_rootbridge_handle(pci_domain_nr(pbus),
					      pbus->number);
}
#else
static inline acpi_handle acpi_find_root_bridge_handle(struct pci_dev *pdev)
{ return NULL; }
#endif

#endif	/* _PCI_ACPI_H_ */
