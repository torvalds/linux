/* $Id: isa.h,v 1.1 2001/05/11 04:31:55 davem Exp $
 * isa.h: Sparc64 layer for PCI to ISA bridge devices.
 *
 * Copyright (C) 2001 David S. Miller (davem@redhat.com)
 */

#ifndef __SPARC64_ISA_H
#define __SPARC64_ISA_H

#include <asm/pbm.h>
#include <asm/oplib.h>
#include <asm/prom.h>

struct sparc_isa_bridge;

struct sparc_isa_device {
	struct sparc_isa_device	*next;
	struct sparc_isa_device	*child;
	struct sparc_isa_bridge	*bus;
	struct device_node	*prom_node;
	struct resource		resource;
	unsigned int		irq;
};

struct sparc_isa_bridge {
	struct sparc_isa_bridge	*next;
	struct sparc_isa_device	*devices;
	struct pci_pbm_info	*parent;
	struct pci_dev		*self;
	int			index;
	struct device_node	*prom_node;
};

extern struct sparc_isa_bridge	*isa_chain;

extern void isa_init(void);

#define for_each_isa(bus)						\
        for((bus) = isa_chain; (bus); (bus) = (bus)->next)

#define for_each_isadev(dev, bus)					\
        for((dev) = (bus)->devices; (dev); (dev) = (dev)->next)

#endif /* !(__SPARC64_ISA_H) */
