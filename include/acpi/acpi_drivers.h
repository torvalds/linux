/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  acpi_drivers.h  ($Revision: 31 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#ifndef __ACPI_DRIVERS_H__
#define __ACPI_DRIVERS_H__

#define ACPI_MAX_STRING			80

/*
 * _HID definitions
 * HIDs must conform to ACPI spec(6.1.4)
 * Linux specific HIDs do not apply to this and begin with LNX:
 */

#define ACPI_POWER_HID			"LNXPOWER"
#define ACPI_PROCESSOR_OBJECT_HID	"LNXCPU"
#define ACPI_SYSTEM_HID			"LNXSYSTM"
#define ACPI_THERMAL_HID		"LNXTHERM"
#define ACPI_BUTTON_HID_POWERF		"LNXPWRBN"
#define ACPI_BUTTON_HID_SLEEPF		"LNXSLPBN"
#define ACPI_VIDEO_HID			"LNXVIDEO"
#define ACPI_BAY_HID			"LNXIOBAY"
#define ACPI_DOCK_HID			"LNXDOCK"
#define ACPI_ECDT_HID			"LNXEC"
/* SMBUS HID definition as supported by Microsoft Windows */
#define ACPI_SMBUS_MS_HID		"SMB0001"
/* Quirk for broken IBM BIOSes */
#define ACPI_SMBUS_IBM_HID		"SMBUSIBM"

/*
 * For fixed hardware buttons, we fabricate acpi_devices with HID
 * ACPI_BUTTON_HID_POWERF or ACPI_BUTTON_HID_SLEEPF.  Fixed hardware
 * signals only an event; it doesn't supply a notification value.
 * To allow drivers to treat notifications from fixed hardware the
 * same as those from real devices, we turn the events into this
 * notification value.
 */
#define ACPI_FIXED_HARDWARE_EVENT	0x100

/* --------------------------------------------------------------------------
                                       PCI
   -------------------------------------------------------------------------- */


/* ACPI PCI Interrupt Link */

int acpi_irq_penalty_init(void);
int acpi_pci_link_allocate_irq(acpi_handle handle, int index, int *triggering,
			       int *polarity, char **name);
int acpi_pci_link_free_irq(acpi_handle handle);

/* ACPI PCI Device Binding */

struct pci_bus;

#ifdef CONFIG_PCI
struct pci_dev *acpi_get_pci_dev(acpi_handle);
#else
static inline struct pci_dev *acpi_get_pci_dev(acpi_handle handle)
{
	return NULL;
}
#endif

/* Arch-defined function to add a bus to the system */

struct pci_bus *pci_acpi_scan_root(struct acpi_pci_root *root);

#ifdef CONFIG_X86
void pci_acpi_crs_quirks(void);
#else
static inline void pci_acpi_crs_quirks(void) { }
#endif

/*--------------------------------------------------------------------------
                                  Dock Station
  -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_DOCK
extern int is_dock_device(struct acpi_device *adev);
#else
static inline int is_dock_device(struct acpi_device *adev)
{
	return 0;
}
#endif /* CONFIG_ACPI_DOCK */

#endif /*__ACPI_DRIVERS_H__*/
