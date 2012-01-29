/* Generic I/O port emulation, based on MN10300 code
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef __ASM_GENERIC_PCI_IOMAP_H
#define __ASM_GENERIC_PCI_IOMAP_H

struct pci_dev;
#ifdef CONFIG_PCI
/* Create a virtual mapping cookie for a PCI BAR (memory or IO) */
extern void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max);
/* Create a virtual mapping cookie for a port on a given PCI device.
 * Do not call this directly, it exists to make it easier for architectures
 * to override */
#ifdef CONFIG_NO_GENERIC_PCI_IOPORT_MAP
extern void __iomem *__pci_ioport_map(struct pci_dev *dev, unsigned long port,
				      unsigned int nr);
#else
#define __pci_ioport_map(dev, port, nr) ioport_map((port), (nr))
#endif

#else
static inline void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max)
{
	return NULL;
}
#endif

#endif /* __ASM_GENERIC_IO_H */
