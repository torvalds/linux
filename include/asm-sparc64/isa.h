/* $Id: isa.h,v 1.1 2001/05/11 04:31:55 davem Exp $
 * isa.h: Sparc64 layer for PCI to ISA bridge devices.
 *
 * Copyright (C) 2001 David S. Miller (davem@redhat.com)
 */

#ifndef __SPARC64_ISA_H
#define __SPARC64_ISA_H

#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/of_device.h>

struct sparc_isa_bridge;

struct sparc_isa_device {
	struct of_device	ofdev;
	struct sparc_isa_device	*next;
	struct sparc_isa_device	*child;
	struct sparc_isa_bridge	*bus;
	struct device_node	*prom_node;
	struct resource		resource;
	unsigned int		irq;
};
#define to_isa_device(d) container_of(d, struct sparc_isa_device, ofdev.dev)

struct sparc_isa_bridge {
	struct of_device	ofdev;
	struct sparc_isa_bridge	*next;
	struct sparc_isa_device	*devices;
	struct pci_dev		*self;
	int			index;
	struct device_node	*prom_node;
};
#define to_isa_bridge(d) container_of(d, struct sparc_isa_bridge, ofdev.dev)

extern struct sparc_isa_bridge	*isa_chain;

extern void isa_init(void);

#define for_each_isa(bus)						\
        for((bus) = isa_chain; (bus); (bus) = (bus)->next)

#define for_each_isadev(dev, bus)					\
        for((dev) = (bus)->devices; (dev); (dev) = (dev)->next)

#endif /* !(__SPARC64_ISA_H) */
