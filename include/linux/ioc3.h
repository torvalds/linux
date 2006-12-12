/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005 Stanislaw Skowronek <skylark@linux-mips.org>
 */

#ifndef _LINUX_IOC3_H
#define _LINUX_IOC3_H

#include <asm/sn/ioc3.h>

#define IOC3_MAX_SUBMODULES	32

#define IOC3_CLASS_NONE		0
#define IOC3_CLASS_BASE_IP27	1
#define IOC3_CLASS_BASE_IP30	2
#define IOC3_CLASS_MENET_123	3
#define IOC3_CLASS_MENET_4	4
#define IOC3_CLASS_CADDUO	5
#define IOC3_CLASS_SERIAL	6

/* One of these per IOC3 */
struct ioc3_driver_data {
	struct list_head list;
	int id;				/* IOC3 sequence number */
	/* PCI mapping */
	unsigned long pma;		/* physical address */
	struct ioc3 __iomem *vma;	/* pointer to registers */
	struct pci_dev *pdev;		/* PCI device */
	/* IRQ stuff */
	int dual_irq;			/* set if separate IRQs are used */
	int irq_io, irq_eth;		/* IRQ numbers */
	/* GPIO magic */
	spinlock_t gpio_lock;
	unsigned int gpdr_shadow;
	/* NIC identifiers */
	char nic_part[32];
	char nic_serial[16];
	char nic_mac[6];
	/* submodule set */
	int class;
	void *data[IOC3_MAX_SUBMODULES];	/* for submodule use */
	int active[IOC3_MAX_SUBMODULES];	/* set if probe succeeds */
	/* is_ir_lock must be held while
	 * modifying sio_ie values, so
	 * we can be sure that sio_ie is
	 * not changing when we read it
	 * along with sio_ir.
	 */
	spinlock_t ir_lock;	/* SIO_IE[SC] mod lock */
};

/* One per submodule */
struct ioc3_submodule {
	char *name;		/* descriptive submodule name */
	struct module *owner;	/* owning kernel module */
	int ethernet;		/* set for ethernet drivers */
	int (*probe) (struct ioc3_submodule *, struct ioc3_driver_data *);
	int (*remove) (struct ioc3_submodule *, struct ioc3_driver_data *);
	int id;			/* assigned by IOC3, index for the "data" array */
	/* IRQ stuff */
	unsigned int irq_mask;	/* IOC3 IRQ mask, leave clear for Ethernet */
	int reset_mask;		/* non-zero if you want the ioc3.c module to reset interrupts */
	int (*intr) (struct ioc3_submodule *, struct ioc3_driver_data *, unsigned int);
	/* private submodule data */
	void *data;		/* assigned by submodule */
};

/**********************************
 * Functions needed by submodules *
 **********************************/

#define IOC3_W_IES		0
#define IOC3_W_IEC		1

/* registers a submodule for all existing and future IOC3 chips */
extern int ioc3_register_submodule(struct ioc3_submodule *);
/* unregisters a submodule */
extern void ioc3_unregister_submodule(struct ioc3_submodule *);
/* enables IRQs indicated by irq_mask for a specified IOC3 chip */
extern void ioc3_enable(struct ioc3_submodule *, struct ioc3_driver_data *, unsigned int);
/* ackowledges specified IRQs */
extern void ioc3_ack(struct ioc3_submodule *, struct ioc3_driver_data *, unsigned int);
/* disables IRQs indicated by irq_mask for a specified IOC3 chip */
extern void ioc3_disable(struct ioc3_submodule *, struct ioc3_driver_data *, unsigned int);
/* atomically sets GPCR bits */
extern void ioc3_gpcr_set(struct ioc3_driver_data *, unsigned int);
/* general ireg writer */
extern void ioc3_write_ireg(struct ioc3_driver_data *idd, uint32_t value, int reg);

#endif
