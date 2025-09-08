/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCI Endpoint *Function* side MSI header file
 *
 * Copyright (C) 2024 NXP
 * Author: Frank Li <Frank.Li@nxp.com>
 */

#ifndef __PCI_EP_MSI__
#define __PCI_EP_MSI__

struct pci_epf;

#ifdef CONFIG_PCI_ENDPOINT_MSI_DOORBELL
int pci_epf_alloc_doorbell(struct pci_epf *epf, u16 nums);
void pci_epf_free_doorbell(struct pci_epf *epf);
#else
static inline int pci_epf_alloc_doorbell(struct pci_epf *epf, u16 nums)
{
	return -ENODATA;
}

static inline void pci_epf_free_doorbell(struct pci_epf *epf)
{
}
#endif /* CONFIG_GENERIC_MSI_IRQ */

#endif /* __PCI_EP_MSI__ */
