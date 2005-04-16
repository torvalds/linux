/* $Id: ebus.h,v 1.2 1999/09/11 23:05:55 zaitcev Exp $
 * ebus.h: PCI to Ebus pseudo driver software state.
 *
 * Copyright (C) 1997 Eddie C. Dost (ecd@skynet.be) 
 *
 * Adopted for sparc by V. Roganov and G. Raiko.
 */

#ifndef __SPARC_EBUS_H
#define __SPARC_EBUS_H

#ifndef _LINUX_IOPORT_H
#include <linux/ioport.h>
#endif
#include <asm/oplib.h>

struct linux_ebus_child {
	struct linux_ebus_child		*next;
	struct linux_ebus_device	*parent;
	struct linux_ebus		*bus;
	int				 prom_node;
	char				 prom_name[64];
	struct resource			 resource[PROMREG_MAX];
	int				 num_addrs;
	unsigned int			 irqs[PROMINTR_MAX];
	int				 num_irqs;
};

struct linux_ebus_device {
	struct linux_ebus_device	*next;
	struct linux_ebus_child		*children;
	struct linux_ebus		*bus;
	int				 prom_node;
	char				 prom_name[64];
	struct resource			 resource[PROMREG_MAX];
	int				 num_addrs;
	unsigned int			 irqs[PROMINTR_MAX];
	int				 num_irqs;
};

struct linux_ebus {
	struct linux_ebus		*next;
	struct linux_ebus_device	*devices;
	struct linux_pbm_info		*parent;
	struct pci_dev			*self;
	int				 prom_node;
	char				 prom_name[64];
	struct linux_prom_ebus_ranges	 ebus_ranges[PROMREG_MAX];
	int				 num_ebus_ranges;
};

struct linux_ebus_dma {
	unsigned int dcsr;
	unsigned int dacr;
	unsigned int dbcr;
};

#define EBUS_DCSR_INT_PEND	0x00000001
#define EBUS_DCSR_ERR_PEND	0x00000002
#define EBUS_DCSR_DRAIN		0x00000004
#define EBUS_DCSR_INT_EN	0x00000010
#define EBUS_DCSR_RESET		0x00000080
#define EBUS_DCSR_WRITE		0x00000100
#define EBUS_DCSR_EN_DMA	0x00000200
#define EBUS_DCSR_CYC_PEND	0x00000400
#define EBUS_DCSR_DIAG_RD_DONE	0x00000800
#define EBUS_DCSR_DIAG_WR_DONE	0x00001000
#define EBUS_DCSR_EN_CNT	0x00002000
#define EBUS_DCSR_TC		0x00004000
#define EBUS_DCSR_DIS_CSR_DRN	0x00010000
#define EBUS_DCSR_BURST_SZ_MASK	0x000c0000
#define EBUS_DCSR_BURST_SZ_1	0x00080000
#define EBUS_DCSR_BURST_SZ_4	0x00000000
#define EBUS_DCSR_BURST_SZ_8	0x00040000
#define EBUS_DCSR_BURST_SZ_16	0x000c0000
#define EBUS_DCSR_DIAG_EN	0x00100000
#define EBUS_DCSR_DIS_ERR_PEND	0x00400000
#define EBUS_DCSR_TCI_DIS	0x00800000
#define EBUS_DCSR_EN_NEXT	0x01000000
#define EBUS_DCSR_DMA_ON	0x02000000
#define EBUS_DCSR_A_LOADED	0x04000000
#define EBUS_DCSR_NA_LOADED	0x08000000
#define EBUS_DCSR_DEV_ID_MASK	0xf0000000

extern struct linux_ebus		*ebus_chain;

extern void ebus_init(void);

#define for_each_ebus(bus)						\
        for((bus) = ebus_chain; (bus); (bus) = (bus)->next)

#define for_each_ebusdev(dev, bus)					\
        for((dev) = (bus)->devices; (dev); (dev) = (dev)->next)

#define for_each_edevchild(dev, child)					\
        for((child) = (dev)->children; (child); (child) = (child)->next)

#endif /* !(__SPARC_EBUS_H) */
