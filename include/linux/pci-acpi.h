/* SPDX-License-Identifier: GPL-2.0 */
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
extern acpi_status pci_acpi_add_bus_pm_notifier(struct acpi_device *dev);
static inline acpi_status pci_acpi_remove_bus_pm_notifier(struct acpi_device *dev)
{
	return acpi_remove_pm_notifier(dev);
}
extern acpi_status pci_acpi_add_pm_notifier(struct acpi_device *dev,
					     struct pci_dev *pci_dev);
static inline acpi_status pci_acpi_remove_pm_notifier(struct acpi_device *dev)
{
	return acpi_remove_pm_notifier(dev);
}
extern phys_addr_t acpi_pci_root_get_mcfg_addr(acpi_handle handle);

struct pci_ecam_ops;
extern int pci_mcfg_lookup(struct acpi_pci_root *root, struct resource *cfgres,
			   struct pci_ecam_ops **ecam_ops);

static inline acpi_handle acpi_find_root_bridge_handle(struct pci_dev *pdev)
{
	struct pci_bus *pbus = pdev->bus;

	/* Find a PCI root bus */
	while (!pci_is_root_bus(pbus))
		pbus = pbus->parent;

	return ACPI_HANDLE(pbus->bridge);
}

static inline acpi_handle acpi_pci_get_bridge_handle(struct pci_bus *pbus)
{
	struct device *dev;

	if (pci_is_root_bus(pbus))
		dev = pbus->bridge;
	else {
		/* If pbus is a virtual bus, there is no bridge to it */
		if (!pbus->self)
			return NULL;

		dev = &pbus->self->dev;
	}

	return ACPI_HANDLE(dev);
}

struct acpi_pci_root;
struct acpi_pci_root_ops;

struct acpi_pci_root_info {
	struct acpi_pci_root		*root;
	struct acpi_device		*bridge;
	struct acpi_pci_root_ops	*ops;
	struct list_head		resources;
	char				name[16];
};

struct acpi_pci_root_ops {
	struct pci_ops *pci_ops;
	int (*init_info)(struct acpi_pci_root_info *info);
	void (*release_info)(struct acpi_pci_root_info *info);
	int (*prepare_resources)(struct acpi_pci_root_info *info);
};

extern int acpi_pci_probe_root_resources(struct acpi_pci_root_info *info);
extern struct pci_bus *acpi_pci_root_create(struct acpi_pci_root *root,
					    struct acpi_pci_root_ops *ops,
					    struct acpi_pci_root_info *info,
					    void *sd);

void acpi_pci_add_bus(struct pci_bus *bus);
void acpi_pci_remove_bus(struct pci_bus *bus);

#ifdef	CONFIG_ACPI_PCI_SLOT
void acpi_pci_slot_init(void);
void acpi_pci_slot_enumerate(struct pci_bus *bus);
void acpi_pci_slot_remove(struct pci_bus *bus);
#else
static inline void acpi_pci_slot_init(void) { }
static inline void acpi_pci_slot_enumerate(struct pci_bus *bus) { }
static inline void acpi_pci_slot_remove(struct pci_bus *bus) { }
#endif

#ifdef	CONFIG_HOTPLUG_PCI_ACPI
void acpiphp_init(void);
void acpiphp_enumerate_slots(struct pci_bus *bus);
void acpiphp_remove_slots(struct pci_bus *bus);
void acpiphp_check_host_bridge(struct acpi_device *adev);
#else
static inline void acpiphp_init(void) { }
static inline void acpiphp_enumerate_slots(struct pci_bus *bus) { }
static inline void acpiphp_remove_slots(struct pci_bus *bus) { }
static inline void acpiphp_check_host_bridge(struct acpi_device *adev) { }
#endif

extern const guid_t pci_acpi_dsm_guid;
#define IGNORE_PCI_BOOT_CONFIG_DSM	0x05
#define DEVICE_LABEL_DSM		0x07
#define RESET_DELAY_DSM			0x08
#define FUNCTION_DELAY_DSM		0x09

#else	/* CONFIG_ACPI */
static inline void acpi_pci_add_bus(struct pci_bus *bus) { }
static inline void acpi_pci_remove_bus(struct pci_bus *bus) { }
#endif	/* CONFIG_ACPI */

#ifdef CONFIG_ACPI_APEI
extern bool aer_acpi_firmware_first(void);
#else
static inline bool aer_acpi_firmware_first(void) { return false; }
#endif

#endif	/* _PCI_ACPI_H_ */
