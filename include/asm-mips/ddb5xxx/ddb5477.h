/***********************************************************************
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * include/asm-mips/ddb5xxx/ddb5477.h
 *     DDB 5477 specific definitions and macros.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 ***********************************************************************
 */

#ifndef __ASM_DDB5XXX_DDB5477_H
#define __ASM_DDB5XXX_DDB5477_H


/*
 * This contains macros that are specific to DDB5477 or renamed from
 * DDB5476.
 */

/*
 * renamed PADRs
 */
#define	DDB_LCS0	DDB_DCS2
#define	DDB_LCS1	DDB_DCS3
#define	DDB_LCS2	DDB_DCS4
#define	DDB_VRC5477	DDB_INTCS

/*
 * New CPU interface registers
 */
#define	DDB_INTCTRL0	0x0400	/* Interrupt Control 0 */
#define	DDB_INTCTRL1	0x0404	/* Interrupt Control 1 */
#define	DDB_INTCTRL2	0x0408	/* Interrupt Control 2 */
#define	DDB_INTCTRL3	0x040c	/* Interrupt Control 3 */

#define	DDB_INT0STAT	0x0420 	/* INT0 Status [R] */
#define	DDB_INT1STAT	0x0428 	/* INT1 Status [R] */
#define	DDB_INT2STAT	0x0430 	/* INT2 Status [R] */
#define	DDB_INT3STAT	0x0438 	/* INT3 Status [R] */
#define	DDB_INT4STAT	0x0440 	/* INT4 Status [R] */
#define	DDB_NMISTAT	0x0450	/* NMI Status [R] */

#define	DDB_INTCLR32	0x0468	/* Interrupt Clear */

#define	DDB_INTPPES0	0x0470	/* PCI0 Interrupt Control */
#define	DDB_INTPPES1	0x0478	/* PCI1 Interrupt Control */

#undef  DDB_CPUSTAT		/* duplicate in Vrc-5477 */
#define	DDB_CPUSTAT	0x0480	/* CPU Status [R] */
#define	DDB_BUSCTRL	0x0488	/* Internal Bus Control */


/*
 * Timer registers
 */
#define	DDB_REFCTRL_L	DDB_T0CTRL
#define	DDB_REFCTRL_H	(DDB_T0CTRL+4)
#define	DDB_REFCNTR	DDB_T0CNTR
#define	DDB_SPT0CTRL_L	DDB_T1CTRL
#define	DDB_SPT0CTRL_H	(DDB_T1CTRL+4)
#define	DDB_SPT1CTRL_L	DDB_T2CTRL
#define	DDB_SPT1CTRL_H	(DDB_T2CTRL+4)
#define DDB_SPT1CNTR	DDB_T1CTRL
#define	DDB_WDTCTRL_L	DDB_T3CTRL
#define	DDB_WDTCTRL_H	(DDB_T3CTRL+4)
#define	DDB_WDTCNTR	DDB_T3CNTR

/*
 * DMA registers are moved.  We don't care about it for now. TODO.
 */

/*
 * BARs for ext PCI (PCI0)
 */
#undef	DDB_BARC
#undef	DDB_BARB

#define DDB_BARC0	0x0210	/* PCI0 Control */
#define DDB_BARM010	0x0218	/* PCI0 SDRAM bank01 */
#define	DDB_BARM230	0x0220	/* PCI0 SDRAM bank23 */
#define	DDB_BAR00	0x0240	/* PCI0 LDCS0 */
#define	DDB_BAR10	0x0248	/* PCI0 LDCS1 */
#define	DDB_BAR20	0x0250	/* PCI0 LDCS2 */
#define	DDB_BAR30	0x0258	/* PCI0 LDCS3 */
#define	DDB_BAR40	0x0260	/* PCI0 LDCS4 */
#define	DDB_BAR50	0x0268	/* PCI0 LDCS5 */
#define	DDB_BARB0	0x0280	/* PCI0 BOOT */
#define	DDB_BARP00	0x0290	/* PCI0 for IOPCI Window0 */
#define	DDB_BARP10	0x0298	/* PCI0 for IOPCI Window1 */

/*
 * BARs for IOPIC (PCI1)
 */
#define DDB_BARC1	0x0610	/* PCI1 Control */
#define DDB_BARM011	0x0618	/* PCI1 SDRAM bank01 */
#define	DDB_BARM231	0x0620	/* PCI1 SDRAM bank23 */
#define	DDB_BAR01	0x0640	/* PCI1 LDCS0 */
#define	DDB_BAR11	0x0648	/* PCI1 LDCS1 */
#define	DDB_BAR21	0x0650	/* PCI1 LDCS2 */
#define	DDB_BAR31	0x0658	/* PCI1 LDCS3 */
#define	DDB_BAR41	0x0660	/* PCI1 LDCS4 */
#define	DDB_BAR51	0x0668	/* PCI1 LDCS5 */
#define	DDB_BARB1	0x0680	/* PCI1 BOOT */
#define	DDB_BARP01	0x0690	/* PCI1 for ext PCI Window0 */
#define	DDB_BARP11	0x0698	/* PCI1 for ext PCI Window1 */

/*
 * Other registers for ext PCI (PCI0)
 */
#define	DDB_PCIINIT00	0x02f0	/* PCI0 Initiator 0 */
#define	DDB_PCIINIT10	0x02f8	/* PCI0 Initiator 1 */

#define	DDB_PCISWP0	0x02b0	/* PCI0 Swap */
#define	DDB_PCIERR0	0x02b8	/* PCI0 Error */

#define	DDB_PCICTL0_L	0x02e0	/* PCI0 Control-L */
#define	DDB_PCICTL0_H	0x02e4	/* PCI0 Control-H */
#define	DDB_PCIARB0_L	0x02e8	/* PCI0 Arbitration-L */
#define	DDB_PCIARB0_H	0x02ec	/* PCI0 Arbitration-H */

/*
 * Other registers for IOPCI (PCI1)
 */
#define DDB_IOPCIW0	0x00d0	/* PCI Address Window 0 [R/W] */
#define DDB_IOPCIW1	0x00d8	/* PCI Address Window 1 [R/W] */

#define	DDB_PCIINIT01	0x06f0	/* PCI1 Initiator 0 */
#define	DDB_PCIINIT11	0x06f8	/* PCI1 Initiator 1 */

#define	DDB_PCISWP1	0x06b0	/* PCI1 Swap */
#define	DDB_PCIERR1	0x06b8	/* PCI1 Error */

#define	DDB_PCICTL1_L	0x06e0	/* PCI1 Control-L */
#define	DDB_PCICTL1_H	0x06e4	/* PCI1 Control-H */
#define	DDB_PCIARB1_L	0x06e8	/* PCI1 Arbitration-L */
#define	DDB_PCIARB1_H	0x06ec	/* PCI1 Arbitration-H */

/*
 * Local Bus
 */
#define DDB_LCST0	0x0110  /* LB Chip Select Timing 0 */
#define DDB_LCST1	0x0118  /* LB Chip Select Timing 1 */
#undef DDB_LCST2
#define DDB_LCST2	0x0120  /* LB Chip Select Timing 2 */
#undef DDB_LCST3
#undef DDB_LCST4
#undef DDB_LCST5
#undef DDB_LCST6
#undef DDB_LCST7
#undef DDB_LCST8
#define DDB_ERRADR	0x0150  /* Error Address Register */
#define DDB_ERRCS       0x0160
#define DDB_BTM		0x0170  /* Boot Time Mode value */

/*
 * MISC registers
 */
#define DDB_GIUFUNSEL	0x4040  /* select dual-func pins */
#define DDB_PIBMISC	0x0750	/* USB buffer enable / power saving */

/*
 *  Memory map (physical address)
 *
 *  Note most of the following address must be properly aligned by the
 *  corresponding size.  For example, if PCI_IO_SIZE is 16MB, then
 *  PCI_IO_BASE must be aligned along 16MB boundary.
 */

/* the actual ram size is detected at run-time */
#define	DDB_SDRAM_BASE		0x00000000
#define	DDB_MAX_SDRAM_SIZE	0x08000000	/* less than 128MB */

#define	DDB_PCI0_MEM_BASE	0x08000000
#define	DDB_PCI0_MEM_SIZE	0x08000000	/* 128 MB */

#define	DDB_PCI1_MEM_BASE	0x10000000
#define	DDB_PCI1_MEM_SIZE	0x08000000	/* 128 MB */

#define	DDB_PCI0_CONFIG_BASE	0x18000000
#define	DDB_PCI0_CONFIG_SIZE	0x01000000	/* 16 MB */

#define	DDB_PCI1_CONFIG_BASE	0x19000000
#define	DDB_PCI1_CONFIG_SIZE	0x01000000	/* 16 MB */

#define	DDB_PCI_IO_BASE		0x1a000000	/* we concatenate two IOs */
#define	DDB_PCI0_IO_BASE	0x1a000000
#define	DDB_PCI0_IO_SIZE	0x01000000	/* 16 MB */
#define	DDB_PCI1_IO_BASE	0x1b000000
#define	DDB_PCI1_IO_SIZE	0x01000000	/* 16 MB */

#define	DDB_LCS0_BASE		0x1c000000	/* flash memory */
#define	DDB_LCS0_SIZE		0x01000000	/* 16 MB */

#define	DDB_LCS1_BASE		0x1d000000	/* misc */
#define	DDB_LCS1_SIZE		0x01000000	/* 16 MB */

#define	DDB_LCS2_BASE		0x1e000000	/* Mezzanine */
#define	DDB_LCS2_SIZE		0x01000000	/* 16 MB */

#define	DDB_VRC5477_BASE	0x1fa00000	/* VRC5477 control regs */
#define	DDB_VRC5477_SIZE	0x00200000	/* 2MB */

#define	DDB_BOOTCS_BASE		0x1fc00000	/* Boot ROM / EPROM /Flash */
#define	DDB_BOOTCS_SIZE		0x00200000	/* 2 MB - doc says 4MB */

#define	DDB_LED			DDB_LCS1_BASE + 0x10000


/*
 * DDB5477 specific functions
 */
#ifndef __ASSEMBLY__
extern void ddb5477_irq_setup(void);

/* route irq to cpu int pin */
extern void ll_vrc5477_irq_route(int vrc5477_irq, int ip);

/* low-level routine for enabling vrc5477 irq, bypassing high-level */
extern void ll_vrc5477_irq_enable(int vrc5477_irq);
extern void ll_vrc5477_irq_disable(int vrc5477_irq);
#endif /* !__ASSEMBLY__ */

/* PCI intr ack share PCIW0 with PCI IO */
#define	DDB_PCI_IACK_BASE	DDB_PCI_IO_BASE

/*
 * Interrupt mapping
 *
 * We have three interrupt controllers:
 *
 *   . CPU itself - 8 sources
 *   . i8259 - 16 sources
 *   . vrc5477 - 32 sources
 *
 *  They connected as follows:
 *    all vrc5477 interrupts are routed to cpu IP2 (by software setting)
 *    all i8359 are routed to INTC in vrc5477 (by hardware connection)
 *
 *  All VRC5477 PCI interrupts are level-triggered (no ack needed).
 *  All PCI irq but INTC are active low.
 */

/*
 * irq number block assignment
 */

#define	NUM_CPU_IRQ		8
#define	NUM_I8259_IRQ		16
#define	NUM_VRC5477_IRQ		32

#define	DDB_IRQ_BASE		0

#define	I8259_IRQ_BASE		DDB_IRQ_BASE
#define	VRC5477_IRQ_BASE	(I8259_IRQ_BASE + NUM_I8259_IRQ)
#define	CPU_IRQ_BASE		(VRC5477_IRQ_BASE + NUM_VRC5477_IRQ)

/*
 * vrc5477 irq defs
 */

#define VRC5477_IRQ_CPCE	(0 + VRC5477_IRQ_BASE)	/* cpu parity error */
#define VRC5477_IRQ_CNTD	(1 + VRC5477_IRQ_BASE)	/* cpu no target */
#define VRC5477_IRQ_I2C		(2 + VRC5477_IRQ_BASE)	/* I2C */
#define VRC5477_IRQ_DMA		(3 + VRC5477_IRQ_BASE)	/* DMA */
#define VRC5477_IRQ_UART0	(4 + VRC5477_IRQ_BASE)
#define VRC5477_IRQ_WDOG	(5 + VRC5477_IRQ_BASE)	/* watchdog timer */
#define VRC5477_IRQ_SPT1	(6 + VRC5477_IRQ_BASE)    /* special purpose timer 1 */
#define VRC5477_IRQ_LBRT	(7 + VRC5477_IRQ_BASE)	/* local bus read timeout */
#define VRC5477_IRQ_INTA	(8 + VRC5477_IRQ_BASE)	/* PCI INT #A */
#define VRC5477_IRQ_INTB	(9 + VRC5477_IRQ_BASE)	/* PCI INT #B */
#define VRC5477_IRQ_INTC	(10 + VRC5477_IRQ_BASE)	/* PCI INT #C */
#define VRC5477_IRQ_INTD	(11 + VRC5477_IRQ_BASE)	/* PCI INT #D */
#define VRC5477_IRQ_INTE	(12 + VRC5477_IRQ_BASE)	/* PCI INT #E */
#define VRC5477_IRQ_RESERVED_13	(13 + VRC5477_IRQ_BASE)	/* reserved  */
#define VRC5477_IRQ_PCIS	(14 + VRC5477_IRQ_BASE)	/* PCI SERR #  */
#define VRC5477_IRQ_PCI		(15 + VRC5477_IRQ_BASE)	/* PCI internal error */
#define VRC5477_IRQ_IOPCI_INTA	(16 + VRC5477_IRQ_BASE)      /* USB-H */
#define VRC5477_IRQ_IOPCI_INTB	(17 + VRC5477_IRQ_BASE)      /* USB-P */
#define VRC5477_IRQ_IOPCI_INTC	(18 + VRC5477_IRQ_BASE)      /* AC97 */
#define VRC5477_IRQ_IOPCI_INTD	(19 + VRC5477_IRQ_BASE)      /* Reserved */
#define VRC5477_IRQ_UART1	(20 + VRC5477_IRQ_BASE)
#define VRC5477_IRQ_SPT0	(21 + VRC5477_IRQ_BASE)      /* special purpose timer 0 */
#define VRC5477_IRQ_GPT0	(22 + VRC5477_IRQ_BASE)      /* general purpose timer 0 */
#define VRC5477_IRQ_GPT1	(23 + VRC5477_IRQ_BASE)      /* general purpose timer 1 */
#define VRC5477_IRQ_GPT2	(24 + VRC5477_IRQ_BASE)      /* general purpose timer 2 */
#define VRC5477_IRQ_GPT3	(25 + VRC5477_IRQ_BASE)      /* general purpose timer 3 */
#define VRC5477_IRQ_GPIO	(26 + VRC5477_IRQ_BASE)
#define VRC5477_IRQ_SIO0	(27 + VRC5477_IRQ_BASE)
#define VRC5477_IRQ_SIO1        (28 + VRC5477_IRQ_BASE)
#define VRC5477_IRQ_RESERVED_29 (29 + VRC5477_IRQ_BASE)      /* reserved */
#define VRC5477_IRQ_IOPCISERR	(30 + VRC5477_IRQ_BASE)      /* IO PCI SERR # */
#define VRC5477_IRQ_IOPCI	(31 + VRC5477_IRQ_BASE)

/*
 * i2859 irq assignment
 */
#define I8259_IRQ_RESERVED_0	(0 + I8259_IRQ_BASE)
#define I8259_IRQ_KEYBOARD	(1 + I8259_IRQ_BASE)	/* M1543 default */
#define I8259_IRQ_CASCADE	(2 + I8259_IRQ_BASE)
#define I8259_IRQ_UART_B	(3 + I8259_IRQ_BASE)	/* M1543 default, may conflict with RTC according to schematic diagram  */
#define I8259_IRQ_UART_A	(4 + I8259_IRQ_BASE)	/* M1543 default */
#define I8259_IRQ_PARALLEL	(5 + I8259_IRQ_BASE)	/* M1543 default */
#define I8259_IRQ_RESERVED_6	(6 + I8259_IRQ_BASE)
#define I8259_IRQ_RESERVED_7	(7 + I8259_IRQ_BASE)
#define I8259_IRQ_RTC		(8 + I8259_IRQ_BASE)	/* who set this? */
#define I8259_IRQ_USB		(9 + I8259_IRQ_BASE)	/* ddb_setup */
#define I8259_IRQ_PMU		(10 + I8259_IRQ_BASE)	/* ddb_setup */
#define I8259_IRQ_RESERVED_11	(11 + I8259_IRQ_BASE)
#define I8259_IRQ_RESERVED_12	(12 + I8259_IRQ_BASE)	/* m1543_irq_setup */
#define I8259_IRQ_RESERVED_13	(13 + I8259_IRQ_BASE)
#define I8259_IRQ_HDC1		(14 + I8259_IRQ_BASE)	/* default and ddb_setup */
#define I8259_IRQ_HDC2		(15 + I8259_IRQ_BASE)	/* default */


/*
 * misc
 */
#define	VRC5477_I8259_CASCADE	(VRC5477_IRQ_INTC - VRC5477_IRQ_BASE)
#define	CPU_VRC5477_CASCADE	2

/*
 * debug routines
 */
#ifndef __ASSEMBLY__
#if defined(CONFIG_RUNTIME_DEBUG)
extern void vrc5477_show_pdar_regs(void);
extern void vrc5477_show_pci_regs(void);
extern void vrc5477_show_bar_regs(void);
extern void vrc5477_show_int_regs(void);
extern void vrc5477_show_all_regs(void);
#endif

/*
 * RAM size
 */
extern int board_ram_size;
#endif /* !__ASSEMBLY__ */

#endif /* __ASM_DDB5XXX_DDB5477_H */
