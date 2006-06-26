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

#include <asm/types.h>
#include <asm/atomic.h>

/* this number is used when no interrupt has been assigned */
#define NO_IRQ			(-1)

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

/*
 * IRQ line status macro IRQ_PER_CPU is used
 */
#define ARCH_HAS_IRQ_PER_CPU

#define get_irq_desc(irq) (&irq_desc[(irq)])

/* Define a way to iterate across irqs. */
#define for_each_irq(i) \
	for ((i) = 0; (i) < NR_IRQS; ++(i))

#ifdef CONFIG_PPC64

/*
 * Maximum number of interrupt sources that we can handle.
 */
#define NR_IRQS		512

/* Interrupt numbers are virtual in case they are sparsely
 * distributed by the hardware.
 */
extern unsigned int virt_irq_to_real_map[NR_IRQS];

/* The maximum virtual IRQ number that we support.  This
 * can be set by the platform and will be reduced by the
 * value of __irq_offset_value.  It defaults to and is
 * capped by (NR_IRQS - 1).
 */
extern unsigned int virt_irq_max;

/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
int virt_irq_create_mapping(unsigned int real_irq);
void virt_irq_init(void);

static inline unsigned int virt_irq_to_real(unsigned int virt_irq)
{
	return virt_irq_to_real_map[virt_irq];
}

extern unsigned int real_irq_to_virt_slowpath(unsigned int real_irq);

/*
 * List of interrupt controllers.
 */
#define IC_INVALID    0
#define IC_OPEN_PIC   1
#define IC_PPC_XIC    2
#define IC_CELL_PIC   3
#define IC_ISERIES    4

extern u64 ppc64_interrupt_controller;

#else /* 32-bit */

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

#endif

#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
/* pedantic: these are long because they are used with set_bit --RR */
extern unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];
extern atomic_t ppc_n_lost_interrupts;

#define virt_irq_create_mapping(x)	(x)

#endif

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
extern int call___do_IRQ(int irq, struct pt_regs *regs,
		struct thread_info *tp);

#else
#define irq_ctx_init()

#endif /* CONFIG_IRQSTACKS */

extern void do_IRQ(struct pt_regs *regs);

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
