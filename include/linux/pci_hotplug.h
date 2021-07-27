/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * PCI HotPlug Core Functions
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * Send feedback to <kristen.c.accardi@intel.com>
 *
 */
#ifndef _PCI_HOTPLUG_H
#define _PCI_HOTPLUG_H

/**
 * struct hotplug_slot_ops -the callbacks that the hotplug pci core can use
 * @enable_slot: Called when the user wants to enable a specific pci slot
 * @disable_slot: Called when the user wants to disable a specific pci slot
 * @set_attention_status: Called to set the specific slot's attention LED to
 * the specified value
 * @hardware_test: Called to run a specified hardware test on the specified
 * slot.
 * @get_power_status: Called to get the current power status of a slot.
 * @get_attention_status: Called to get the current attention status of a slot.
 * @get_latch_status: Called to get the current latch status of a slot.
 * @get_adapter_status: Called to get see if an adapter is present in the slot or not.
 * @reset_slot: Optional interface to allow override of a bus reset for the
 *	slot for cases where a secondary bus reset can result in spurious
 *	hotplug events or where a slot can be reset independent of the bus.
 *
 * The table of function pointers that is passed to the hotplug pci core by a
 * hotplug pci driver.  These functions are called by the hotplug pci core when
 * the user wants to do something to a specific slot (query it for information,
 * set an LED, enable / disable power, etc.)
 */
struct hotplug_slot_ops {
	int (*enable_slot)		(struct hotplug_slot *slot);
	int (*disable_slot)		(struct hotplug_slot *slot);
	int (*set_attention_status)	(struct hotplug_slot *slot, u8 value);
	int (*hardware_test)		(struct hotplug_slot *slot, u32 value);
	int (*get_power_status)		(struct hotplug_slot *slot, u8 *value);
	int (*get_attention_status)	(struct hotplug_slot *slot, u8 *value);
	int (*get_latch_status)		(struct hotplug_slot *slot, u8 *value);
	int (*get_adapter_status)	(struct hotplug_slot *slot, u8 *value);
	int (*reset_slot)		(struct hotplug_slot *slot, int probe);
};

/**
 * struct hotplug_slot - used to register a physical slot with the hotplug pci core
 * @ops: pointer to the &struct hotplug_slot_ops to be used for this slot
 * @slot_list: internal list used to track hotplug PCI slots
 * @pci_slot: represents a physical slot
 * @owner: The module owner of this structure
 * @mod_name: The module name (KBUILD_MODNAME) of this structure
 */
struct hotplug_slot {
	const struct hotplug_slot_ops	*ops;

	/* Variables below this are for use only by the hotplug pci core. */
	struct list_head		slot_list;
	struct pci_slot			*pci_slot;
	struct module			*owner;
	const char			*mod_name;
};

static inline const char *hotplug_slot_name(const struct hotplug_slot *slot)
{
	return pci_slot_name(slot->pci_slot);
}

int __pci_hp_register(struct hotplug_slot *slot, struct pci_bus *pbus, int nr,
		      const char *name, struct module *owner,
		      const char *mod_name);
int __pci_hp_initialize(struct hotplug_slot *slot, struct pci_bus *bus, int nr,
			const char *name, struct module *owner,
			const char *mod_name);
int pci_hp_add(struct hotplug_slot *slot);

void pci_hp_del(struct hotplug_slot *slot);
void pci_hp_destroy(struct hotplug_slot *slot);
void pci_hp_deregister(struct hotplug_slot *slot);

/* use a define to avoid include chaining to get THIS_MODULE & friends */
#define pci_hp_register(slot, pbus, devnr, name) \
	__pci_hp_register(slot, pbus, devnr, name, THIS_MODULE, KBUILD_MODNAME)
#define pci_hp_initialize(slot, bus, nr, name) \
	__pci_hp_initialize(slot, bus, nr, name, THIS_MODULE, KBUILD_MODNAME)

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
bool pciehp_is_native(struct pci_dev *bridge);
int acpi_get_hp_hw_control_from_firmware(struct pci_dev *bridge);
bool shpchp_is_native(struct pci_dev *bridge);
int acpi_pci_check_ejectable(struct pci_bus *pbus, acpi_handle handle);
int acpi_pci_detect_ejectable(acpi_handle handle);
#else
static inline int acpi_get_hp_hw_control_from_firmware(struct pci_dev *bridge)
{
	return 0;
}
static inline bool pciehp_is_native(struct pci_dev *bridge) { return true; }
static inline bool shpchp_is_native(struct pci_dev *bridge) { return true; }
#endif

static inline bool hotplug_is_native(struct pci_dev *bridge)
{
	return pciehp_is_native(bridge) || shpchp_is_native(bridge);
}
#endif
