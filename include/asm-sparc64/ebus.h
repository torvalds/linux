/* $Id: ebus.h,v 1.10 2001/03/14 05:00:55 davem Exp $
 * ebus.h: PCI to Ebus pseudo driver software state.
 *
 * Copyright (C) 1997 Eddie C. Dost (ecd@skynet.be)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#ifndef __SPARC64_EBUS_H
#define __SPARC64_EBUS_H

#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/of_device.h>

struct linux_ebus_child {
	struct linux_ebus_child		*next;
	struct linux_ebus_device	*parent;
	struct linux_ebus		*bus;
	struct device_node		*prom_node;
	struct resource			 resource[PROMREG_MAX];
	int				 num_addrs;
	unsigned int			 irqs[PROMINTR_MAX];
	int				 num_irqs;
};

struct linux_ebus_device {
	struct of_device		ofdev;
	struct linux_ebus_device	*next;
	struct linux_ebus_child		*children;
	struct linux_ebus		*bus;
	struct device_node		*prom_node;
	struct resource			 resource[PROMREG_MAX];
	int				 num_addrs;
	unsigned int			 irqs[PROMINTR_MAX];
	int				 num_irqs;
};
#define to_ebus_device(d) container_of(d, struct linux_ebus_device, ofdev.dev)

struct linux_ebus {
	struct of_device		ofdev;
	struct linux_ebus		*next;
	struct linux_ebus_device	*devices;
	struct pci_dev			*self;
	int				 index;
	int				 is_rio;
	struct device_node		*prom_node;
};
#define to_ebus(d) container_of(d, struct linux_ebus, ofdev.dev)

struct ebus_dma_info {
	spinlock_t	lock;
	void __iomem	*regs;

	unsigned int	flags;
#define EBUS_DMA_FLAG_USE_EBDMA_HANDLER		0x00000001
#define EBUS_DMA_FLAG_TCI_DISABLE		0x00000002

	/* These are only valid is EBUS_DMA_FLAG_USE_EBDMA_HANDLER is
	 * set.
	 */
	void (*callback)(struct ebus_dma_info *p, int event, void *cookie);
	void *client_cookie;
	unsigned int	irq;
#define EBUS_DMA_EVENT_ERROR	1
#define EBUS_DMA_EVENT_DMA	2
#define EBUS_DMA_EVENT_DEVICE	4

	unsigned char	name[64];
};

extern int ebus_dma_register(struct ebus_dma_info *p);
extern int ebus_dma_irq_enable(struct ebus_dma_info *p, int on);
extern void ebus_dma_unregister(struct ebus_dma_info *p);
extern int ebus_dma_request(struct ebus_dma_info *p, dma_addr_t bus_addr,
			    size_t len);
extern void ebus_dma_prepare(struct ebus_dma_info *p, int write);
extern unsigned int ebus_dma_residue(struct ebus_dma_info *p);
extern unsigned int ebus_dma_addr(struct ebus_dma_info *p);
extern void ebus_dma_enable(struct ebus_dma_info *p, int on);

extern struct linux_ebus		*ebus_chain;

extern void ebus_init(void);

#define for_each_ebus(bus)						\
        for((bus) = ebus_chain; (bus); (bus) = (bus)->next)

#define for_each_ebusdev(dev, bus)					\
        for((dev) = (bus)->devices; (dev); (dev) = (dev)->next)

#define for_each_edevchild(dev, child)					\
        for((child) = (dev)->children; (child); (child) = (child)->next)

#endif /* !(__SPARC64_EBUS_H) */
