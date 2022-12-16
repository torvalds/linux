/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_IRQDOMAIN_DEFS_H
#define _LINUX_IRQDOMAIN_DEFS_H

/*
 * Should several domains have the same device node, but serve
 * different purposes (for example one domain is for PCI/MSI, and the
 * other for wired IRQs), they can be distinguished using a
 * bus-specific token. Most domains are expected to only carry
 * DOMAIN_BUS_ANY.
 */
enum irq_domain_bus_token {
	DOMAIN_BUS_ANY		= 0,
	DOMAIN_BUS_WIRED,
	DOMAIN_BUS_GENERIC_MSI,
	DOMAIN_BUS_PCI_MSI,
	DOMAIN_BUS_PLATFORM_MSI,
	DOMAIN_BUS_NEXUS,
	DOMAIN_BUS_IPI,
	DOMAIN_BUS_FSL_MC_MSI,
	DOMAIN_BUS_TI_SCI_INTA_MSI,
	DOMAIN_BUS_WAKEUP,
	DOMAIN_BUS_VMD_MSI,
	DOMAIN_BUS_PCI_DEVICE_MSI,
	DOMAIN_BUS_PCI_DEVICE_MSIX,
	DOMAIN_BUS_DMAR,
	DOMAIN_BUS_AMDVI,
	DOMAIN_BUS_PCI_DEVICE_IMS,
};

#endif /* _LINUX_IRQDOMAIN_DEFS_H */
