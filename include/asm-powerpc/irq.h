#ifdef __KERNEL__
#ifndef _ASM_POWERPC_IRQ_H
#define _ASM_POWERPC_IRQ_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <linux/list.h>
#include <linux/radix-tree.h>

#include <asm/types.h>
#include <asm/atomic.h>


#define get_irq_desc(irq) (&irq_desc[(irq)])

/* Define a way to iterate across irqs. */
#define for_each_irq(i) \
	for ((i) = 0; (i) < NR_IRQS; ++(i))

extern atomic_t ppc_n_lost_interrupts;

#ifdef CONFIG_PPC_MERGE

/* This number is used when no interrupt has been assigned */
#define NO_IRQ			(0)

/* This is a special irq number to return from get_irq() to tell that
 * no interrupt happened _and_ ignore it (don't count it as bad). Some
 * platforms like iSeries rely on that.
 */
#define NO_IRQ_IGNORE		((unsigned int)-1)

/* Total number of virq in the platform (make it a CONFIG_* option ? */
#define NR_IRQS		512

/* Number of irqs reserved for the legacy controller */
#define NUM_ISA_INTERRUPTS	16

/* This type is the placeholder for a hardware interrupt number. It has to
 * be big enough to enclose whatever representation is used by a given
 * platform.
 */
typedef unsigned long irq_hw_number_t;

/* Interrupt controller "host" data structure. This could be defined as a
 * irq domain controller. That is, it handles the mapping between hardware
 * and virtual interrupt numbers for a given interrupt domain. The host
 * structure is generally created by the PIC code for a given PIC instance
 * (though a host can cover more than one PIC if they have a flat number
 * model). It's the host callbacks that are responsible for setting the
 * irq_chip on a given irq_desc after it's been mapped.
 *
 * The host code and data structures are fairly agnostic to the fact that
 * we use an open firmware device-tree. We do have references to struct
 * device_node in two places: in irq_find_host() to find the host matching
 * a given interrupt controller node, and of course as an argument to its
 * counterpart host->ops->match() callback. However, those are treated as
 * generic pointers by the core and the fact that it's actually a device-node
 * pointer is purely a convention between callers and implementation. This
 * code could thus be used on other architectures by replacing those two
 * by some sort of arch-specific void * "token" used to identify interrupt
 * controllers.
 */
struct irq_host;
struct radix_tree_root;

/* Functions below are provided by the host and called whenever a new mapping
 * is created or an old mapping is disposed. The host can then proceed to
 * whatever internal data structures management is required. It also needs
 * to setup the irq_desc when returning from map().
 */
struct irq_host_ops {
	/* Match an interrupt controller device node to a host, returns
	 * 1 on a match
	 */
	int (*match)(struct irq_host *h, struct device_node *node);

	/* Create or update a mapping between a virtual irq number and a hw
	 * irq number. This is called only once for a given mapping.
	 */
	int (*map)(struct irq_host *h, unsigned int virq, irq_hw_number_t hw);

	/* Dispose of such a mapping */
	void (*unmap)(struct irq_host *h, unsigned int virq);

	/* Update of such a mapping  */
	void (*remap)(struct irq_host *h, unsigned int virq, irq_hw_number_t hw);

	/* Translate device-tree interrupt specifier from raw format coming
	 * from the firmware to a irq_hw_number_t (interrupt line number) and
	 * type (sense) that can be passed to set_irq_type(). In the absence
	 * of this callback, irq_create_of_mapping() and irq_of_parse_and_map()
	 * will return the hw number in the first cell and IRQ_TYPE_NONE for
	 * the type (which amount to keeping whatever default value the
	 * interrupt controller has for that line)
	 */
	int (*xlate)(struct irq_host *h, struct device_node *ctrler,
		     u32 *intspec, unsigned int intsize,
		     irq_hw_number_t *out_hwirq, unsigned int *out_type);
};

struct irq_host {
	struct list_head	link;

	/* type of reverse mapping technique */
	unsigned int		revmap_type;
#define IRQ_HOST_MAP_LEGACY     0 /* legacy 8259, gets irqs 1..15 */
#define IRQ_HOST_MAP_NOMAP	1 /* no fast reverse mapping */
#define IRQ_HOST_MAP_LINEAR	2 /* linear map of interrupts */
#define IRQ_HOST_MAP_TREE	3 /* radix tree */
	union {
		struct {
			unsigned int size;
			unsigned int *revmap;
		} linear;
		struct radix_tree_root tree;
	} revmap_data;
	struct irq_host_ops	*ops;
	void			*host_data;
	irq_hw_number_t		inval_irq;

	/* Optional device node pointer */
	struct device_node	*of_node;
};

/* The main irq map itself is an array of NR_IRQ entries containing the
 * associate host and irq number. An entry with a host of NULL is free.
 * An entry can be allocated if it's free, the allocator always then sets
 * hwirq first to the host's invalid irq number and then fills ops.
 */
struct irq_map_entry {
	irq_hw_number_t	hwirq;
	struct irq_host	*host;
};

extern struct irq_map_entry irq_map[NR_IRQS];

extern irq_hw_number_t virq_to_hw(unsigned int virq);

/**
 * irq_alloc_host - Allocate a new irq_host data structure
 * @of_node: optional device-tree node of the interrupt controller
 * @revmap_type: type of reverse mapping to use
 * @revmap_arg: for IRQ_HOST_MAP_LINEAR linear only: size of the map
 * @ops: map/unmap host callbacks
 * @inval_irq: provide a hw number in that host space that is always invalid
 *
 * Allocates and initialize and irq_host structure. Note that in the case of
 * IRQ_HOST_MAP_LEGACY, the map() callback will be called before this returns
 * for all legacy interrupts except 0 (which is always the invalid irq for
 * a legacy controller). For a IRQ_HOST_MAP_LINEAR, the map is allocated by
 * this call as well. For a IRQ_HOST_MAP_TREE, the radix tree will be allocated
 * later during boot automatically (the reverse mapping will use the slow path
 * until that happens).
 */
extern struct irq_host *irq_alloc_host(struct device_node *of_node,
				       unsigned int revmap_type,
				       unsigned int revmap_arg,
				       struct irq_host_ops *ops,
				       irq_hw_number_t inval_irq);


/**
 * irq_find_host - Locates a host for a given device node
 * @node: device-tree node of the interrupt controller
 */
extern struct irq_host *irq_find_host(struct device_node *node);


/**
 * irq_set_default_host - Set a "default" host
 * @host: default host pointer
 *
 * For convenience, it's possible to set a "default" host that will be used
 * whenever NULL is passed to irq_create_mapping(). It makes life easier for
 * platforms that want to manipulate a few hard coded interrupt numbers that
 * aren't properly represented in the device-tree.
 */
extern void irq_set_default_host(struct irq_host *host);


/**
 * irq_set_virq_count - Set the maximum number of virt irqs
 * @count: number of linux virtual irqs, capped with NR_IRQS
 *
 * This is mainly for use by platforms like iSeries who want to program
 * the virtual irq number in the controller to avoid the reverse mapping
 */
extern void irq_set_virq_count(unsigned int count);


/**
 * irq_create_mapping - Map a hardware interrupt into linux virq space
 * @host: host owning this hardware interrupt or NULL for default host
 * @hwirq: hardware irq number in that host space
 *
 * Only one mapping per hardware interrupt is permitted. Returns a linux
 * virq number.
 * If the sense/trigger is to be specified, set_irq_type() should be called
 * on the number returned from that call.
 */
extern unsigned int irq_create_mapping(struct irq_host *host,
				       irq_hw_number_t hwirq);


/**
 * irq_dispose_mapping - Unmap an interrupt
 * @virq: linux virq number of the interrupt to unmap
 */
extern void irq_dispose_mapping(unsigned int virq);

/**
 * irq_find_mapping - Find a linux virq from an hw irq number.
 * @host: host owning this hardware interrupt
 * @hwirq: hardware irq number in that host space
 *
 * This is a slow path, for use by generic code. It's expected that an
 * irq controller implementation directly calls the appropriate low level
 * mapping function.
 */
extern unsigned int irq_find_mapping(struct irq_host *host,
				     irq_hw_number_t hwirq);

/**
 * irq_create_direct_mapping - Allocate a virq for direct mapping
 * @host: host to allocate the virq for or NULL for default host
 *
 * This routine is used for irq controllers which can choose the hardware
 * interrupt numbers they generate. In such a case it's simplest to use
 * the linux virq as the hardware interrupt number.
 */
extern unsigned int irq_create_direct_mapping(struct irq_host *host);

/**
 * irq_radix_revmap - Find a linux virq from a hw irq number.
 * @host: host owning this hardware interrupt
 * @hwirq: hardware irq number in that host space
 *
 * This is a fast path, for use by irq controller code that uses radix tree
 * revmaps
 */
extern unsigned int irq_radix_revmap(struct irq_host *host,
				     irq_hw_number_t hwirq);

/**
 * irq_linear_revmap - Find a linux virq from a hw irq number.
 * @host: host owning this hardware interrupt
 * @hwirq: hardware irq number in that host space
 *
 * This is a fast path, for use by irq controller code that uses linear
 * revmaps. It does fallback to the slow path if the revmap doesn't exist
 * yet and will create the revmap entry with appropriate locking
 */

extern unsigned int irq_linear_revmap(struct irq_host *host,
				      irq_hw_number_t hwirq);



/**
 * irq_alloc_virt - Allocate virtual irq numbers
 * @host: host owning these new virtual irqs
 * @count: number of consecutive numbers to allocate
 * @hint: pass a hint number, the allocator will try to use a 1:1 mapping
 *
 * This is a low level function that is used internally by irq_create_mapping()
 * and that can be used by some irq controllers implementations for things
 * like allocating ranges of numbers for MSIs. The revmaps are left untouched.
 */
extern unsigned int irq_alloc_virt(struct irq_host *host,
				   unsigned int count,
				   unsigned int hint);

/**
 * irq_free_virt - Free virtual irq numbers
 * @virq: virtual irq number of the first interrupt to free
 * @count: number of interrupts to free
 *
 * This function is the opposite of irq_alloc_virt. It will not clear reverse
 * maps, this should be done previously by unmap'ing the interrupt. In fact,
 * all interrupts covered by the range being freed should have been unmapped
 * prior to calling this.
 */
extern void irq_free_virt(unsigned int virq, unsigned int count);


/* -- OF helpers -- */

/* irq_create_of_mapping - Map a hardware interrupt into linux virq space
 * @controller: Device node of the interrupt controller
 * @inspec: Interrupt specifier from the device-tree
 * @intsize: Size of the interrupt specifier from the device-tree
 *
 * This function is identical to irq_create_mapping except that it takes
 * as input informations straight from the device-tree (typically the results
 * of the of_irq_map_*() functions.
 */
extern unsigned int irq_create_of_mapping(struct device_node *controller,
					  u32 *intspec, unsigned int intsize);


/* irq_of_parse_and_map - Parse nad Map an interrupt into linux virq space
 * @device: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_map_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
extern unsigned int irq_of_parse_and_map(struct device_node *dev, int index);

/* -- End OF helpers -- */

/**
 * irq_early_init - Init irq remapping subsystem
 */
extern void irq_early_init(void);

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}


#else /* CONFIG_PPC_MERGE */

/* This number is used when no interrupt has been assigned */
#define NO_IRQ			(-1)
#define NO_IRQ_IGNORE		(-2)


/*
 * These constants are used for passing information about interrupt
 * signal polarity and level/edge sensing to the low-level PIC chip
 * drivers.
 */
#define IRQ_SENSE_MASK		0x1
#define IRQ_SENSE_LEVEL		0x1	/* interrupt on active level */
#define IRQ_SENSE_EDGE		0x0	/* interrupt triggered by edge */

#define IRQ_POLARITY_MASK	0x2
#define IRQ_POLARITY_POSITIVE	0x2	/* high level or low->high edge */
#define IRQ_POLARITY_NEGATIVE	0x0	/* low level or high->low edge */


#if defined(CONFIG_40x)
#include <asm/ibm4xx.h>

#ifndef NR_BOARD_IRQS
#define NR_BOARD_IRQS 0
#endif

#ifndef UIC_WIDTH /* Number of interrupts per device */
#define UIC_WIDTH 32
#endif

#ifndef NR_UICS /* number  of UIC devices */
#define NR_UICS 1
#endif

#if defined (CONFIG_403)
/*
 * The PowerPC 403 cores' Asynchronous Interrupt Controller (AIC) has
 * 32 possible interrupts, a majority of which are not implemented on
 * all cores. There are six configurable, external interrupt pins and
 * there are eight internal interrupts for the on-chip serial port
 * (SPU), DMA controller, and JTAG controller.
 *
 */

#define	NR_AIC_IRQS 32
#define	NR_IRQS	 (NR_AIC_IRQS + NR_BOARD_IRQS)

#elif !defined (CONFIG_403)

/*
 *  The PowerPC 405 cores' Universal Interrupt Controller (UIC) has 32
 * possible interrupts as well. There are seven, configurable external
 * interrupt pins and there are 17 internal interrupts for the on-chip
 * serial port, DMA controller, on-chip Ethernet controller, PCI, etc.
 *
 */


#define NR_UIC_IRQS UIC_WIDTH
#define NR_IRQS		((NR_UIC_IRQS * NR_UICS) + NR_BOARD_IRQS)
#endif

#elif defined(CONFIG_44x)
#include <asm/ibm44x.h>

#define	NR_UIC_IRQS	32
#define	NR_IRQS		((NR_UIC_IRQS * NR_UICS) + NR_BOARD_IRQS)

#elif defined(CONFIG_8xx)

/* Now include the board configuration specific associations.
*/
#include <asm/mpc8xx.h>

/* The MPC8xx cores have 16 possible interrupts.  There are eight
 * possible level sensitive interrupts assigned and generated internally
 * from such devices as CPM, PCMCIA, RTC, PIT, TimeBase and Decrementer.
 * There are eight external interrupts (IRQs) that can be configured
 * as either level or edge sensitive.
 *
 * On some implementations, there is also the possibility of an 8259
 * through the PCI and PCI-ISA bridges.
 *
 * We are "flattening" the interrupt vectors of the cascaded CPM
 * and 8259 interrupt controllers so that we can uniquely identify
 * any interrupt source with a single integer.
 */
#define NR_SIU_INTS	16
#define NR_CPM_INTS	32
#ifndef NR_8259_INTS
#define NR_8259_INTS 0
#endif

#define SIU_IRQ_OFFSET		0
#define CPM_IRQ_OFFSET		(SIU_IRQ_OFFSET + NR_SIU_INTS)
#define I8259_IRQ_OFFSET	(CPM_IRQ_OFFSET + NR_CPM_INTS)

#define NR_IRQS	(NR_SIU_INTS + NR_CPM_INTS + NR_8259_INTS)

/* These values must be zero-based and map 1:1 with the SIU configuration.
 * They are used throughout the 8xx I/O subsystem to generate
 * interrupt masks, flags, and other control patterns.  This is why the
 * current kernel assumption of the 8259 as the base controller is such
 * a pain in the butt.
 */
#define	SIU_IRQ0	(0)	/* Highest priority */
#define	SIU_LEVEL0	(1)
#define	SIU_IRQ1	(2)
#define	SIU_LEVEL1	(3)
#define	SIU_IRQ2	(4)
#define	SIU_LEVEL2	(5)
#define	SIU_IRQ3	(6)
#define	SIU_LEVEL3	(7)
#define	SIU_IRQ4	(8)
#define	SIU_LEVEL4	(9)
#define	SIU_IRQ5	(10)
#define	SIU_LEVEL5	(11)
#define	SIU_IRQ6	(12)
#define	SIU_LEVEL6	(13)
#define	SIU_IRQ7	(14)
#define	SIU_LEVEL7	(15)

#define MPC8xx_INT_FEC1		SIU_LEVEL1
#define MPC8xx_INT_FEC2		SIU_LEVEL3

#define MPC8xx_INT_SCC1		(CPM_IRQ_OFFSET + CPMVEC_SCC1)
#define MPC8xx_INT_SCC2		(CPM_IRQ_OFFSET + CPMVEC_SCC2)
#define MPC8xx_INT_SCC3		(CPM_IRQ_OFFSET + CPMVEC_SCC3)
#define MPC8xx_INT_SCC4		(CPM_IRQ_OFFSET + CPMVEC_SCC4)
#define MPC8xx_INT_SMC1		(CPM_IRQ_OFFSET + CPMVEC_SMC1)
#define MPC8xx_INT_SMC2		(CPM_IRQ_OFFSET + CPMVEC_SMC2)

/* The internal interrupts we can configure as we see fit.
 * My personal preference is CPM at level 2, which puts it above the
 * MBX PCI/ISA/IDE interrupts.
 */
#ifndef PIT_INTERRUPT
#define PIT_INTERRUPT		SIU_LEVEL0
#endif
#ifndef	CPM_INTERRUPT
#define CPM_INTERRUPT		SIU_LEVEL2
#endif
#ifndef	PCMCIA_INTERRUPT
#define PCMCIA_INTERRUPT	SIU_LEVEL6
#endif
#ifndef	DEC_INTERRUPT
#define DEC_INTERRUPT		SIU_LEVEL7
#endif

/* Some internal interrupt registers use an 8-bit mask for the interrupt
 * level instead of a number.
 */
#define	mk_int_int_mask(IL) (1 << (7 - (IL/2)))

#elif defined(CONFIG_83xx)
#include <asm/mpc83xx.h>

#define	NR_IRQS	(NR_IPIC_INTS)

#elif defined(CONFIG_85xx)
/* Now include the board configuration specific associations.
*/
#include <asm/mpc85xx.h>

/* The MPC8548 openpic has 48 internal interrupts and 12 external
 * interrupts.
 *
 * We are "flattening" the interrupt vectors of the cascaded CPM
 * so that we can uniquely identify any interrupt source with a
 * single integer.
 */
#define NR_CPM_INTS	64
#define NR_EPIC_INTS	60
#ifndef NR_8259_INTS
#define NR_8259_INTS	0
#endif
#define NUM_8259_INTERRUPTS NR_8259_INTS

#ifndef CPM_IRQ_OFFSET
#define CPM_IRQ_OFFSET	0
#endif

#define NR_IRQS	(NR_EPIC_INTS + NR_CPM_INTS + NR_8259_INTS)

/* Internal IRQs on MPC85xx OpenPIC */

#ifndef MPC85xx_OPENPIC_IRQ_OFFSET
#ifdef CONFIG_CPM2
#define MPC85xx_OPENPIC_IRQ_OFFSET	(CPM_IRQ_OFFSET + NR_CPM_INTS)
#else
#define MPC85xx_OPENPIC_IRQ_OFFSET	0
#endif
#endif

/* Not all of these exist on all MPC85xx implementations */
#define MPC85xx_IRQ_L2CACHE	( 0 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_ECM		( 1 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_DDR		( 2 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_LBIU	( 3 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_DMA0	( 4 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_DMA1	( 5 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_DMA2	( 6 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_DMA3	( 7 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_PCI1	( 8 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_PCI2	( 9 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_RIO_ERROR	( 9 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_RIO_BELL	(10 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_RIO_TX	(11 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_RIO_RX	(12 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC1_TX	(13 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC1_RX	(14 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC3_TX	(15 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC3_RX	(16 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC3_ERROR	(17 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC1_ERROR	(18 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC2_TX	(19 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC2_RX	(20 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC4_TX	(21 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC4_RX	(22 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC4_ERROR	(23 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_TSEC2_ERROR	(24 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_FEC		(25 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_DUART	(26 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_IIC1	(27 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_PERFMON	(28 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_SEC2	(29 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_CPM		(30 + MPC85xx_OPENPIC_IRQ_OFFSET)

/* The 12 external interrupt lines */
#define MPC85xx_IRQ_EXT0        (48 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT1        (49 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT2        (50 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT3        (51 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT4        (52 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT5        (53 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT6        (54 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT7        (55 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT8        (56 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT9        (57 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT10       (58 + MPC85xx_OPENPIC_IRQ_OFFSET)
#define MPC85xx_IRQ_EXT11       (59 + MPC85xx_OPENPIC_IRQ_OFFSET)

/* CPM related interrupts */
#define	SIU_INT_ERROR		((uint)0x00+CPM_IRQ_OFFSET)
#define	SIU_INT_I2C		((uint)0x01+CPM_IRQ_OFFSET)
#define	SIU_INT_SPI		((uint)0x02+CPM_IRQ_OFFSET)
#define	SIU_INT_RISC		((uint)0x03+CPM_IRQ_OFFSET)
#define	SIU_INT_SMC1		((uint)0x04+CPM_IRQ_OFFSET)
#define	SIU_INT_SMC2		((uint)0x05+CPM_IRQ_OFFSET)
#define	SIU_INT_USB		((uint)0x0b+CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER1		((uint)0x0c+CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER2		((uint)0x0d+CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER3		((uint)0x0e+CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER4		((uint)0x0f+CPM_IRQ_OFFSET)
#define	SIU_INT_FCC1		((uint)0x20+CPM_IRQ_OFFSET)
#define	SIU_INT_FCC2		((uint)0x21+CPM_IRQ_OFFSET)
#define	SIU_INT_FCC3		((uint)0x22+CPM_IRQ_OFFSET)
#define	SIU_INT_MCC1		((uint)0x24+CPM_IRQ_OFFSET)
#define	SIU_INT_MCC2		((uint)0x25+CPM_IRQ_OFFSET)
#define	SIU_INT_SCC1		((uint)0x28+CPM_IRQ_OFFSET)
#define	SIU_INT_SCC2		((uint)0x29+CPM_IRQ_OFFSET)
#define	SIU_INT_SCC3		((uint)0x2a+CPM_IRQ_OFFSET)
#define	SIU_INT_SCC4		((uint)0x2b+CPM_IRQ_OFFSET)
#define	SIU_INT_PC15		((uint)0x30+CPM_IRQ_OFFSET)
#define	SIU_INT_PC14		((uint)0x31+CPM_IRQ_OFFSET)
#define	SIU_INT_PC13		((uint)0x32+CPM_IRQ_OFFSET)
#define	SIU_INT_PC12		((uint)0x33+CPM_IRQ_OFFSET)
#define	SIU_INT_PC11		((uint)0x34+CPM_IRQ_OFFSET)
#define	SIU_INT_PC10		((uint)0x35+CPM_IRQ_OFFSET)
#define	SIU_INT_PC9		((uint)0x36+CPM_IRQ_OFFSET)
#define	SIU_INT_PC8		((uint)0x37+CPM_IRQ_OFFSET)
#define	SIU_INT_PC7		((uint)0x38+CPM_IRQ_OFFSET)
#define	SIU_INT_PC6		((uint)0x39+CPM_IRQ_OFFSET)
#define	SIU_INT_PC5		((uint)0x3a+CPM_IRQ_OFFSET)
#define	SIU_INT_PC4		((uint)0x3b+CPM_IRQ_OFFSET)
#define	SIU_INT_PC3		((uint)0x3c+CPM_IRQ_OFFSET)
#define	SIU_INT_PC2		((uint)0x3d+CPM_IRQ_OFFSET)
#define	SIU_INT_PC1		((uint)0x3e+CPM_IRQ_OFFSET)
#define	SIU_INT_PC0		((uint)0x3f+CPM_IRQ_OFFSET)

#elif defined(CONFIG_PPC_86xx)
#include <asm/mpc86xx.h>

#define NR_EPIC_INTS 48
#ifndef NR_8259_INTS
#define NR_8259_INTS 16 /*ULI 1575 can route 12 interrupts */
#endif
#define NUM_8259_INTERRUPTS NR_8259_INTS

#ifndef I8259_OFFSET
#define I8259_OFFSET 0
#endif

#define NR_IRQS 256

/* Internal IRQs on MPC86xx OpenPIC */

#ifndef MPC86xx_OPENPIC_IRQ_OFFSET
#define MPC86xx_OPENPIC_IRQ_OFFSET NR_8259_INTS
#endif

/* The 48 internal sources */
#define MPC86xx_IRQ_NULL        ( 0 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_MCM         ( 1 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_DDR         ( 2 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_LBC         ( 3 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_DMA0        ( 4 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_DMA1        ( 5 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_DMA2        ( 6 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_DMA3        ( 7 + MPC86xx_OPENPIC_IRQ_OFFSET)

/* no 10,11 */
#define MPC86xx_IRQ_UART2       (12 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC1_TX    (13 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC1_RX    (14 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC3_TX    (15 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC3_RX    (16 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC3_ERROR (17 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC1_ERROR (18 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC2_TX    (19 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC2_RX    (20 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC4_TX    (21 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC4_RX    (22 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC4_ERROR (23 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_TSEC2_ERROR (24 + MPC86xx_OPENPIC_IRQ_OFFSET)
/* no 25 */
#define MPC86xx_IRQ_UART1       (26 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_IIC         (27 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_PERFMON       (28 + MPC86xx_OPENPIC_IRQ_OFFSET)
/* no 29,30,31 */
#define MPC86xx_IRQ_SRIO_ERROR    (32 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_SRIO_OUT_BELL (33 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_SRIO_IN_BELL  (34 + MPC86xx_OPENPIC_IRQ_OFFSET)
/* no 35,36 */
#define MPC86xx_IRQ_SRIO_OUT_MSG1 (37 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_SRIO_IN_MSG1  (38 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_SRIO_OUT_MSG2 (39 + MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_SRIO_IN_MSG2  (40 + MPC86xx_OPENPIC_IRQ_OFFSET)

/* The 12 external interrupt lines */
#define MPC86xx_IRQ_EXT_BASE	48
#define MPC86xx_IRQ_EXT0	(0 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT1	(1 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT2	(2 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT3	(3 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT4	(4 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT5	(5 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT6	(6 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT7	(7 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT8	(8 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT9	(9 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT10	(10 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)
#define MPC86xx_IRQ_EXT11	(11 + MPC86xx_IRQ_EXT_BASE \
		+ MPC86xx_OPENPIC_IRQ_OFFSET)

#else /* CONFIG_40x + CONFIG_8xx */
/*
 * this is the # irq's for all ppc arch's (pmac/chrp/prep)
 * so it is the max of them all
 */
#define NR_IRQS			256
#define __DO_IRQ_CANON	1

#ifndef CONFIG_8260

#define NUM_8259_INTERRUPTS	16

#else /* CONFIG_8260 */

/* The 8260 has an internal interrupt controller with a maximum of
 * 64 IRQs.  We will use NR_IRQs from above since it is large enough.
 * Don't be confused by the 8260 documentation where they list an
 * "interrupt number" and "interrupt vector".  We are only interested
 * in the interrupt vector.  There are "reserved" holes where the
 * vector number increases, but the interrupt number in the table does not.
 * (Document errata updates have fixed this...make sure you have up to
 * date processor documentation -- Dan).
 */

#ifndef CPM_IRQ_OFFSET
#define CPM_IRQ_OFFSET	0
#endif

#define NR_CPM_INTS	64

#define	SIU_INT_ERROR		((uint)0x00 + CPM_IRQ_OFFSET)
#define	SIU_INT_I2C		((uint)0x01 + CPM_IRQ_OFFSET)
#define	SIU_INT_SPI		((uint)0x02 + CPM_IRQ_OFFSET)
#define	SIU_INT_RISC		((uint)0x03 + CPM_IRQ_OFFSET)
#define	SIU_INT_SMC1		((uint)0x04 + CPM_IRQ_OFFSET)
#define	SIU_INT_SMC2		((uint)0x05 + CPM_IRQ_OFFSET)
#define	SIU_INT_IDMA1		((uint)0x06 + CPM_IRQ_OFFSET)
#define	SIU_INT_IDMA2		((uint)0x07 + CPM_IRQ_OFFSET)
#define	SIU_INT_IDMA3		((uint)0x08 + CPM_IRQ_OFFSET)
#define	SIU_INT_IDMA4		((uint)0x09 + CPM_IRQ_OFFSET)
#define	SIU_INT_SDMA		((uint)0x0a + CPM_IRQ_OFFSET)
#define	SIU_INT_USB		((uint)0x0b + CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER1		((uint)0x0c + CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER2		((uint)0x0d + CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER3		((uint)0x0e + CPM_IRQ_OFFSET)
#define	SIU_INT_TIMER4		((uint)0x0f + CPM_IRQ_OFFSET)
#define	SIU_INT_TMCNT		((uint)0x10 + CPM_IRQ_OFFSET)
#define	SIU_INT_PIT		((uint)0x11 + CPM_IRQ_OFFSET)
#define	SIU_INT_PCI		((uint)0x12 + CPM_IRQ_OFFSET)
#define	SIU_INT_IRQ1		((uint)0x13 + CPM_IRQ_OFFSET)
#define	SIU_INT_IRQ2		((uint)0x14 + CPM_IRQ_OFFSET)
#define	SIU_INT_IRQ3		((uint)0x15 + CPM_IRQ_OFFSET)
#define	SIU_INT_IRQ4		((uint)0x16 + CPM_IRQ_OFFSET)
#define	SIU_INT_IRQ5		((uint)0x17 + CPM_IRQ_OFFSET)
#define	SIU_INT_IRQ6		((uint)0x18 + CPM_IRQ_OFFSET)
#define	SIU_INT_IRQ7		((uint)0x19 + CPM_IRQ_OFFSET)
#define	SIU_INT_FCC1		((uint)0x20 + CPM_IRQ_OFFSET)
#define	SIU_INT_FCC2		((uint)0x21 + CPM_IRQ_OFFSET)
#define	SIU_INT_FCC3		((uint)0x22 + CPM_IRQ_OFFSET)
#define	SIU_INT_MCC1		((uint)0x24 + CPM_IRQ_OFFSET)
#define	SIU_INT_MCC2		((uint)0x25 + CPM_IRQ_OFFSET)
#define	SIU_INT_SCC1		((uint)0x28 + CPM_IRQ_OFFSET)
#define	SIU_INT_SCC2		((uint)0x29 + CPM_IRQ_OFFSET)
#define	SIU_INT_SCC3		((uint)0x2a + CPM_IRQ_OFFSET)
#define	SIU_INT_SCC4		((uint)0x2b + CPM_IRQ_OFFSET)
#define	SIU_INT_PC15		((uint)0x30 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC14		((uint)0x31 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC13		((uint)0x32 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC12		((uint)0x33 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC11		((uint)0x34 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC10		((uint)0x35 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC9		((uint)0x36 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC8		((uint)0x37 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC7		((uint)0x38 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC6		((uint)0x39 + CPM_IRQ_OFFSET)
#define	SIU_INT_PC5		((uint)0x3a + CPM_IRQ_OFFSET)
#define	SIU_INT_PC4		((uint)0x3b + CPM_IRQ_OFFSET)
#define	SIU_INT_PC3		((uint)0x3c + CPM_IRQ_OFFSET)
#define	SIU_INT_PC2		((uint)0x3d + CPM_IRQ_OFFSET)
#define	SIU_INT_PC1		((uint)0x3e + CPM_IRQ_OFFSET)
#define	SIU_INT_PC0		((uint)0x3f + CPM_IRQ_OFFSET)

#endif /* CONFIG_8260 */

#endif /* Whatever way too big #ifdef */

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
/* pedantic: these are long because they are used with set_bit --RR */
extern unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];

/*
 * Because many systems have two overlapping names spaces for
 * interrupts (ISA and XICS for example), and the ISA interrupts
 * have historically not been easy to renumber, we allow ISA
 * interrupts to take values 0 - 15, and shift up the remaining
 * interrupts by 0x10.
 */
#define NUM_ISA_INTERRUPTS	0x10
extern int __irq_offset_value;

static inline int irq_offset_up(int irq)
{
	return(irq + __irq_offset_value);
}

static inline int irq_offset_down(int irq)
{
	return(irq - __irq_offset_value);
}

static inline int irq_offset_value(void)
{
	return __irq_offset_value;
}

#ifdef __DO_IRQ_CANON
extern int ppc_do_canonicalize_irqs;
#else
#define ppc_do_canonicalize_irqs	0
#endif

static __inline__ int irq_canonicalize(int irq)
{
	if (ppc_do_canonicalize_irqs && irq == 2)
		irq = 9;
	return irq;
}
#endif /* CONFIG_PPC_MERGE */

extern int distribute_irqs;

struct irqaction;
struct pt_regs;

#define __ARCH_HAS_DO_SOFTIRQ

extern void __do_softirq(void);

#ifdef CONFIG_IRQSTACKS
/*
 * Per-cpu stacks for handling hard and soft interrupts.
 */
extern struct thread_info *hardirq_ctx[NR_CPUS];
extern struct thread_info *softirq_ctx[NR_CPUS];

extern void irq_ctx_init(void);
extern void call_do_softirq(struct thread_info *tp);
extern int call_handle_irq(int irq, void *p1,
			   struct thread_info *tp, void *func);
#else
#define irq_ctx_init()

#endif /* CONFIG_IRQSTACKS */

extern void do_IRQ(struct pt_regs *regs);

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
