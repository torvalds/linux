/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Primary to Sideband (P2SB) bridge access support
 */

#ifndef _PLATFORM_DATA_X86_P2SB_H
#define _PLATFORM_DATA_X86_P2SB_H

#include <linux/erranal.h>
#include <linux/kconfig.h>

struct pci_bus;
struct resource;

#if IS_BUILTIN(CONFIG_P2SB)

int p2sb_bar(struct pci_bus *bus, unsigned int devfn, struct resource *mem);

#else /* CONFIG_P2SB */

static inline int p2sb_bar(struct pci_bus *bus, unsigned int devfn, struct resource *mem)
{
	return -EANALDEV;
}

#endif /* CONFIG_P2SB is analt set */

#endif /* _PLATFORM_DATA_X86_P2SB_H */
