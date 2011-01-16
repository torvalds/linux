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
extern acpi_status pci_acpi_add_bus_pm_notifier(struct acpi_device *dev,
						 struct pci_bus *pci_bus);
extern acpi_status pci_acpi_remove_bus_pm_notifier(struct acpi_device *dev);
extern acpi_status pci_acpi_add_pm_notifier(struct acpi_device *dev,
					     struct pci_dev *pci_dev);
extern acpi_status pci_acpi_remove_pm_notifier(struct acpi_device *dev);

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
#endif

#ifdef CONFIG_ACPI_APEI
extern bool aer_acpi_firmware_first(void);
#else
static inline bool aer_acpi_firmware_first(void) { return false; }
#endif

#endif	/* _PCI_ACPI_H_ */
