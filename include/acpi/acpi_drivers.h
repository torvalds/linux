/*
 *  acpi_drivers.h  ($Revision: 31 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef __ACPI_DRIVERS_H__
#define __ACPI_DRIVERS_H__

#include <linux/acpi.h>
#include <acpi/acpi_bus.h>

#define ACPI_MAX_STRING			80

#define ACPI_BUS_COMPONENT		0x00010000
#define ACPI_SYSTEM_COMPONENT		0x02000000

/* _HID definitions */

#define ACPI_POWER_HID			"ACPI_PWR"
#define ACPI_PROCESSOR_HID		"ACPI_CPU"
#define ACPI_SYSTEM_HID			"ACPI_SYS"
#define ACPI_THERMAL_HID		"ACPI_THM"
#define ACPI_BUTTON_HID_POWERF		"ACPI_FPB"
#define ACPI_BUTTON_HID_SLEEPF		"ACPI_FSB"

/* --------------------------------------------------------------------------
                                       PCI
   -------------------------------------------------------------------------- */

#define ACPI_PCI_COMPONENT		0x00400000

/* ACPI PCI Interrupt Link (pci_link.c) */

int acpi_irq_penalty_init(void);
int acpi_pci_link_allocate_irq(acpi_handle handle, int index, int *triggering,
			       int *polarity, char **name);
int acpi_pci_link_free_irq(acpi_handle handle);

/* ACPI PCI Interrupt Routing (pci_irq.c) */

int acpi_pci_irq_add_prt(acpi_handle handle, int segment, int bus);
void acpi_pci_irq_del_prt(int segment, int bus);

/* ACPI PCI Device Binding (pci_bind.c) */

struct pci_bus;

acpi_status acpi_get_pci_id(acpi_handle handle, struct acpi_pci_id *id);
int acpi_pci_bind(struct acpi_device *device);
int acpi_pci_unbind(struct acpi_device *device);
int acpi_pci_bind_root(struct acpi_device *device, struct acpi_pci_id *id,
		       struct pci_bus *bus);

/* Arch-defined function to add a bus to the system */

struct pci_bus *pci_acpi_scan_root(struct acpi_device *device, int domain,
				   int bus);

/* --------------------------------------------------------------------------
                                  Power Resource
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_POWER
int acpi_enable_wakeup_device_power(struct acpi_device *dev);
int acpi_disable_wakeup_device_power(struct acpi_device *dev);
int acpi_power_get_inferred_state(struct acpi_device *device);
int acpi_power_transition(struct acpi_device *device, int state);
#endif

/* --------------------------------------------------------------------------
                                  Embedded Controller
   -------------------------------------------------------------------------- */
#ifdef CONFIG_ACPI_EC
int acpi_ec_ecdt_probe(void);
#endif

/* --------------------------------------------------------------------------
                                    Processor
   -------------------------------------------------------------------------- */

#define ACPI_PROCESSOR_LIMIT_NONE	0x00
#define ACPI_PROCESSOR_LIMIT_INCREMENT	0x01
#define ACPI_PROCESSOR_LIMIT_DECREMENT	0x02

int acpi_processor_set_thermal_limit(acpi_handle handle, int type);

/* --------------------------------------------------------------------------
                                    Hot Keys
   -------------------------------------------------------------------------- */

extern int acpi_specific_hotkey_enabled;

/*--------------------------------------------------------------------------
                                  Dock Station
  -------------------------------------------------------------------------- */
#if defined(CONFIG_ACPI_DOCK) || defined(CONFIG_ACPI_DOCK_MODULE)
extern int is_dock_device(acpi_handle handle);
extern int register_dock_notifier(struct notifier_block *nb);
extern void unregister_dock_notifier(struct notifier_block *nb);
extern int register_hotplug_dock_device(acpi_handle handle,
	acpi_notify_handler handler, void *context);
extern void unregister_hotplug_dock_device(acpi_handle handle);
#else
#define is_dock_device(h)			(0)
#define register_dock_notifier(nb) 		(-ENODEV)
#define unregister_dock_notifier(nb)           	do { } while(0)
#define register_hotplug_dock_device(h1, h2, c)	(-ENODEV)
#define unregister_hotplug_dock_device(h)       do { } while(0)
#endif
#endif /*__ACPI_DRIVERS_H__*/
