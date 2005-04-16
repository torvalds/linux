/* $Id: isa.h,v 1.1 2001/05/11 04:31:55 davem Exp $
 * isa.h: Sparc64 layer for PCI to ISA bridge devices.
 *
 * Copyright (C) 2001 David S. Miller (davem@redhat.com)
 */

#ifndef __SPARC64_ISA_H
#define __SPARC64_ISA_H

#include <asm/pbm.h>
#include <asm/oplib.h>

struct sparc_isa_bridge;

struct sparc_isa_device {
	struct sparc_isa_device	*next;
	struct sparc_isa_device	*child;
	struct sparc_isa_bridge	*bus;
	int			prom_node;
	char			prom_name[64];
	char			compatible[64];
	struct resource		resource;
	unsigned int		irq;
};

struct sparc_isa_bridge {
	struct sparc_isa_bridge	*next;
	struct sparc_isa_device	*devices;
	struct pci_pbm_info	*parent;
	struct pci_dev		*self;
	int			index;
	int			prom_node;
	char			prom_name[64];
#define linux_prom_isa_ranges linux_prom_ebus_ranges
	struct linux_prom_isa_ranges	isa_ranges[PROMREG_MAX];
	int			num_isa_ranges;
#define linux_prom_isa_intmap	linux_prom_ebus_intmap
	struct linux_prom_isa_intmap	isa_intmap[PROMREG_MAX];
	int			num_isa_intmap;
#define linux_prom_isa_intmask	linux_prom_ebus_intmask
	struct linux_prom_isa_intmap	isa_intmask;
};

extern struct sparc_isa_bridge	*isa_chain;

extern void isa_init(void);

#define for_each_isa(bus)						\
        for((bus) = isa_chain; (bus); (bus) = (bus)->next)

#define for_each_isadev(dev, bus)					\
        for((dev) = (bus)->devices; (dev); (dev) = (dev)->next)

#endif /* !(__SPARC64_ISA_H) */
