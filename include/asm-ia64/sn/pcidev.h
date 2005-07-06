/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_PCI_PCIDEV_H
#define _ASM_IA64_SN_PCI_PCIDEV_H

#include <linux/pci.h>

#define SN_PCIDEV_INFO(pci_dev) \
        ((struct pcidev_info *)(pci_dev)->sysdata)

#define SN_PCIBUS_BUSSOFT_INFO(pci_bus) \
	(struct pcibus_info *)((struct pcibus_bussoft *)(PCI_CONTROLLER((pci_bus))->platform_data))
/*
 * Given a pci_bus, return the sn pcibus_bussoft struct.  Note that
 * this only works for root busses, not for busses represented by PPB's.
 */

#define SN_PCIBUS_BUSSOFT(pci_bus) \
        ((struct pcibus_bussoft *)(PCI_CONTROLLER((pci_bus))->platform_data))

#define SN_PCIBUS_BUSSOFT_INFO(pci_bus) \
	(struct pcibus_info *)((struct pcibus_bussoft *)(PCI_CONTROLLER((pci_bus))->platform_data))
/*
 * Given a struct pci_dev, return the sn pcibus_bussoft struct.  Note
 * that this is not equivalent to SN_PCIBUS_BUSSOFT(pci_dev->bus) due
 * due to possible PPB's in the path.
 */

#define SN_PCIDEV_BUSSOFT(pci_dev) \
	(SN_PCIDEV_INFO(pci_dev)->pdi_host_pcidev_info->pdi_pcibus_info)

#define SN_PCIDEV_BUSPROVIDER(pci_dev) \
	(SN_PCIDEV_INFO(pci_dev)->pdi_provider)

#define PCIIO_BUS_NONE	255      /* bus 255 reserved */
#define PCIIO_SLOT_NONE 255
#define PCIIO_FUNC_NONE 255
#define PCIIO_VENDOR_ID_NONE	(-1)

struct pcidev_info {
	uint64_t		pdi_pio_mapped_addr[7]; /* 6 BARs PLUS 1 ROM */
	uint64_t		pdi_slot_host_handle;	/* Bus and devfn Host pci_dev */

	struct pcibus_bussoft	*pdi_pcibus_info;	/* Kernel common bus soft */
	struct pcidev_info	*pdi_host_pcidev_info;	/* Kernel Host pci_dev */
	struct pci_dev		*pdi_linux_pcidev;	/* Kernel pci_dev */

	struct sn_irq_info	*pdi_sn_irq_info;
	struct sn_pcibus_provider *pdi_provider;	/* sn pci ops */
	struct pci_dev 		*host_pci_dev;		/* host bus link */
};

extern void sn_irq_fixup(struct pci_dev *pci_dev,
			 struct sn_irq_info *sn_irq_info);
extern void sn_irq_unfixup(struct pci_dev *pci_dev);
extern void sn_pci_controller_fixup(int segment, int busnum,
 				    struct pci_bus *bus);
extern void sn_bus_store_sysdata(struct pci_dev *dev);
extern void sn_bus_free_sysdata(void);
extern void sn_pci_fixup_slot(struct pci_dev *dev);
extern void sn_pci_unfixup_slot(struct pci_dev *dev);
extern void sn_irq_lh_init(void);
#endif				/* _ASM_IA64_SN_PCI_PCIDEV_H */
