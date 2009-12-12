#ifndef __ACPI_HEST_H
#define __ACPI_HEST_H

#include <linux/pci.h>

#ifdef CONFIG_ACPI
extern int acpi_hest_firmware_first_pci(struct pci_dev *pci);
#else
static inline int acpi_hest_firmware_first_pci(struct pci_dev *pci) { return 0; }
#endif

#endif
