/*
 * header file specific for ddb5476
 *
 * Copyright (C) 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

/*
 *  Memory map (physical address)
 *
 *  Note most of the following address must be properly aligned by the
 *  corresponding size.  For example, if PCI_IO_SIZE is 16MB, then
 *  PCI_IO_BASE must be aligned along 16MB boundary.
 */
#define DDB_SDRAM_BASE		0x00000000
#define DDB_SDRAM_SIZE		0x04000000      /* 64MB */

#define	DDB_DCS3_BASE		0x04000000	/* flash 1 */
#define	DDB_DCS3_SIZE		0x01000000	/* 16MB */

#define	DDB_DCS2_BASE		0x05000000	/* flash 2 */
#define	DDB_DCS2_SIZE		0x01000000	/* 16MB */

#define DDB_PCI_IO_BASE		0x06000000
#define DDB_PCI_IO_SIZE		0x02000000      /* 32 MB */

#define	DDB_PCI_MEM_BASE	0x08000000
#define	DDB_PCI_MEM_SIZE	0x08000000	/* 128 MB */

#define	DDB_DCS5_BASE		0x13000000	/* DDB status regs */
#define	DDB_DCS5_SIZE		0x00200000	/* 2MB, 8-bit */

#define	DDB_DCS4_BASE		0x14000000	/* DDB control regs */
#define	DDB_DCS4_SIZE		0x00200000	/* 2MB, 8-bit */

#define DDB_INTCS_BASE		0x1fa00000      /* VRC5476 control regs */
#define DDB_INTCS_SIZE		0x00200000      /* 2MB */

#define DDB_BOOTCS_BASE         0x1fc00000      /* Boot ROM / EPROM /Flash */
#define DDB_BOOTCS_SIZE         0x00200000      /* 2 MB - doc says 4MB */


/* aliases */
#define	DDB_PCI_CONFIG_BASE	DDB_PCI_MEM_BASE
#define	DDB_PCI_CONFIG_SIZE	DDB_PCI_MEM_SIZE

/* PCI intr ack share PCIW0 with PCI IO */
#define	DDB_PCI_IACK_BASE	DDB_PCI_IO_BASE

/*
 * Interrupt mapping
 *
 * We have three interrupt controllers:
 *
 *   . CPU itself - 8 sources
 *   . i8259 - 16 sources
 *   . vrc5476 - 16 sources
 *
 *  They connected as follows:
 *    all vrc5476 interrupts are routed to cpu IP2 (by software setting)
 *    all i2869 are routed to INTC in vrc5476 (by hardware connection)
 *
 *  All VRC5476 PCI interrupts are level-triggered (no ack needed).
 *  All PCI irq but INTC are active low.
 */

/*
 * irq number block assignment
 */

#define	NUM_CPU_IRQ		8
#define	NUM_I8259_IRQ		16
#define	NUM_VRC5476_IRQ		16

#define	DDB_IRQ_BASE		0

#define	I8259_IRQ_BASE		DDB_IRQ_BASE
#define	VRC5476_IRQ_BASE	(I8259_IRQ_BASE + NUM_I8259_IRQ)
#define	CPU_IRQ_BASE		(VRC5476_IRQ_BASE + NUM_VRC5476_IRQ)

/*
 * vrc5476 irq defs, see page 52-64 of Vrc5074 system controller manual
 */

#define VRC5476_IRQ_CPCE	0	/* cpu parity error */
#define VRC5476_IRQ_CNTD	1	/* cpu no target */
#define VRC5476_IRQ_MCE		2	/* memory check error */
#define VRC5476_IRQ_DMA		3	/* DMA */
#define VRC5476_IRQ_UART	4	/* vrc5476 builtin UART, not used */
#define VRC5476_IRQ_WDOG	5	/* watchdog timer */
#define VRC5476_IRQ_GPT		6	/* general purpose timer */
#define VRC5476_IRQ_LBRT	7	/* local bus read timeout */
#define VRC5476_IRQ_INTA	8	/* PCI INT #A */
#define VRC5476_IRQ_INTB	9	/* PCI INT #B */
#define VRC5476_IRQ_INTC	10	/* PCI INT #C */
#define VRC5476_IRQ_INTD	11	/* PCI INT #D */
#define VRC5476_IRQ_INTE	12	/* PCI INT #E */
#define VRC5476_IRQ_RESERVED_13	13	/* reserved  */
#define VRC5476_IRQ_PCIS	14	/* PCI SERR #  */
#define VRC5476_IRQ_PCI		15	/* PCI internal error */

/*
 * i2859 irq assignment
 */
#define I8259_IRQ_RESERVED_0	0
#define I8259_IRQ_KEYBOARD	1	/* M1543 default */
#define I8259_IRQ_CASCADE	2
#define I8259_IRQ_UART_B	3	/* M1543 default, may conflict with RTC according to schematic diagram  */
#define I8259_IRQ_UART_A	4	/* M1543 default */
#define I8259_IRQ_PARALLEL	5	/* M1543 default */
#define I8259_IRQ_RESERVED_6	6
#define I8259_IRQ_RESERVED_7	7
#define I8259_IRQ_RTC		8	/* who set this? */
#define I8259_IRQ_USB		9	/* ddb_setup */
#define I8259_IRQ_PMU		10	/* ddb_setup */
#define I8259_IRQ_RESERVED_11	11
#define I8259_IRQ_RESERVED_12	12	/* m1543_irq_setup */
#define I8259_IRQ_RESERVED_13	13
#define I8259_IRQ_HDC1		14	/* default and ddb_setup */
#define I8259_IRQ_HDC2		15	/* default */


/*
 * misc
 */
#define	VRC5476_I8259_CASCADE	VRC5476_IRQ_INTC
#define	CPU_VRC5476_CASCADE	2

#define is_i8259_irq(irq)       ((irq) < NUM_I8259_IRQ)
#define nile4_to_irq(n)         ((n)+NUM_I8259_IRQ)
#define irq_to_nile4(n)         ((n)-NUM_I8259_IRQ)

/*
 * low-level irq functions
 */
#ifndef __ASSEMBLY__
extern void nile4_map_irq(int nile4_irq, int cpu_irq);
extern void nile4_map_irq_all(int cpu_irq);
extern void nile4_enable_irq(int nile4_irq);
extern void nile4_disable_irq(int nile4_irq);
extern void nile4_disable_irq_all(void);
extern u16 nile4_get_irq_stat(int cpu_irq);
extern void nile4_enable_irq_output(int cpu_irq);
extern void nile4_disable_irq_output(int cpu_irq);
extern void nile4_set_pci_irq_polarity(int pci_irq, int high);
extern void nile4_set_pci_irq_level_or_edge(int pci_irq, int level);
extern void nile4_clear_irq(int nile4_irq);
extern void nile4_clear_irq_mask(u32 mask);
extern u8 nile4_i8259_iack(void);
extern void nile4_dump_irq_status(void);        /* Debug */
#endif /* !__ASSEMBLY__ */
