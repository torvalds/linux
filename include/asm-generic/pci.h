/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_GENERIC_PCI_H
#define __ASM_GENERIC_PCI_H

#ifndef PCIBIOS_MIN_IO
#define PCIBIOS_MIN_IO		0
#endif

#ifndef PCIBIOS_MIN_MEM
#define PCIBIOS_MIN_MEM		0
#endif

#ifndef pcibios_assign_all_busses
/* For bootloaders that do not initialize the PCI bus */
#define pcibios_assign_all_busses() 1
#endif

/* Enable generic resource mapping code in drivers/pci/ */
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

#ifdef CONFIG_PCI_DOMAINS
static inline int pci_proc_domain(struct pci_bus *bus)
{
	/* always show the domain in /proc */
	return 1;
}
#endif /* CONFIG_PCI_DOMAINS */

#endif /* __ASM_GENERIC_PCI_H */
