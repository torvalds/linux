/* $Id: fhc.h,v 1.5 1999/09/21 14:39:29 davem Exp $
 * fhc.h: Structures for central/fhc pseudo driver on Sunfire/Starfire/Wildfire.
 *
 * Copyright (C) 1997, 1999 David S. Miller (davem@redhat.com)
 */

#ifndef _SPARC64_FHC_H
#define _SPARC64_FHC_H

#include <linux/timer.h>

#include <asm/oplib.h>
#include <asm/upa.h>

struct linux_fhc;

/* Clock board register offsets. */
#define CLOCK_CTRL	0x00UL	/* Main control */
#define CLOCK_STAT1	0x10UL	/* Status one */
#define CLOCK_STAT2	0x20UL	/* Status two */
#define CLOCK_PWRSTAT	0x30UL	/* Power status */
#define CLOCK_PWRPRES	0x40UL	/* Power presence */
#define CLOCK_TEMP	0x50UL	/* Temperature */
#define CLOCK_IRQDIAG	0x60UL	/* IRQ diagnostics */
#define CLOCK_PWRSTAT2	0x70UL	/* Power status two */

#define CLOCK_CTRL_LLED		0x04	/* Left LED, 0 == on */
#define CLOCK_CTRL_MLED		0x02	/* Mid LED, 1 == on */
#define CLOCK_CTRL_RLED		0x01	/* RIght LED, 1 == on */

struct linux_central {
	struct linux_fhc		*child;
	unsigned long			cfreg;
	unsigned long			clkregs;
	unsigned long			clkver;
	int				slots;
	int				prom_node;
	char				prom_name[64];

	struct linux_prom_ranges	central_ranges[PROMREG_MAX];
	int				num_central_ranges;
};

/* Firehose controller register offsets */
struct fhc_regs {
	unsigned long			pregs;	/* FHC internal regs */
#define FHC_PREGS_ID	0x00UL	/* FHC ID */
#define  FHC_ID_VERS		0xf0000000 /* Version of this FHC		*/
#define  FHC_ID_PARTID		0x0ffff000 /* Part ID code (0x0f9f == FHC)	*/
#define  FHC_ID_MANUF		0x0000007e /* Manufacturer (0x3e == SUN's JEDEC)*/
#define  FHC_ID_RESV		0x00000001 /* Read as one			*/
#define FHC_PREGS_RCS	0x10UL	/* FHC Reset Control/Status Register */
#define  FHC_RCS_POR		0x80000000 /* Last reset was a power cycle	*/
#define  FHC_RCS_SPOR		0x40000000 /* Last reset was sw power on reset	*/
#define  FHC_RCS_SXIR		0x20000000 /* Last reset was sw XIR reset	*/
#define  FHC_RCS_BPOR		0x10000000 /* Last reset was due to POR button	*/
#define  FHC_RCS_BXIR		0x08000000 /* Last reset was due to XIR button	*/
#define  FHC_RCS_WEVENT		0x04000000 /* CPU reset was due to wakeup event	*/
#define  FHC_RCS_CFATAL		0x02000000 /* Centerplane Fatal Error signalled	*/
#define  FHC_RCS_FENAB		0x01000000 /* Fatal errors elicit system reset	*/
#define FHC_PREGS_CTRL	0x20UL	/* FHC Control Register */
#define  FHC_CONTROL_ICS	0x00100000 /* Ignore Centerplane Signals	*/
#define  FHC_CONTROL_FRST	0x00080000 /* Fatal Error Reset Enable		*/
#define  FHC_CONTROL_LFAT	0x00040000 /* AC/DC signalled a local error	*/
#define  FHC_CONTROL_SLINE	0x00010000 /* Firmware Synchronization Line	*/
#define  FHC_CONTROL_DCD	0x00008000 /* DC-->DC Converter Disable		*/
#define  FHC_CONTROL_POFF	0x00004000 /* AC/DC Controller PLL Disable	*/
#define  FHC_CONTROL_FOFF	0x00002000 /* FHC Controller PLL Disable	*/
#define  FHC_CONTROL_AOFF	0x00001000 /* CPU A SRAM/SBD Low Power Mode	*/
#define  FHC_CONTROL_BOFF	0x00000800 /* CPU B SRAM/SBD Low Power Mode	*/
#define  FHC_CONTROL_PSOFF	0x00000400 /* Turns off this FHC's power supply	*/
#define  FHC_CONTROL_IXIST	0x00000200 /* 0=FHC tells clock board it exists	*/
#define  FHC_CONTROL_XMSTR	0x00000100 /* 1=Causes this FHC to be XIR master*/
#define  FHC_CONTROL_LLED	0x00000040 /* 0=Left LED ON			*/
#define  FHC_CONTROL_MLED	0x00000020 /* 1=Middle LED ON			*/
#define  FHC_CONTROL_RLED	0x00000010 /* 1=Right LED			*/
#define  FHC_CONTROL_BPINS	0x00000003 /* Spare Bidirectional Pins		*/
#define FHC_PREGS_BSR	0x30UL	/* FHC Board Status Register */
#define  FHC_BSR_DA64		0x00040000 /* Port A: 0=128bit 1=64bit data path */
#define  FHC_BSR_DB64		0x00020000 /* Port B: 0=128bit 1=64bit data path */
#define  FHC_BSR_BID		0x0001e000 /* Board ID                           */
#define  FHC_BSR_SA		0x00001c00 /* Port A UPA Speed (from the pins)   */
#define  FHC_BSR_SB		0x00000380 /* Port B UPA Speed (from the pins)   */
#define  FHC_BSR_NDIAG		0x00000040 /* Not in Diag Mode                   */
#define  FHC_BSR_NTBED		0x00000020 /* Not in TestBED Mode                */
#define  FHC_BSR_NIA		0x0000001c /* Jumper, bit 18 in PROM space       */
#define  FHC_BSR_SI		0x00000001 /* Spare input pin value              */
#define FHC_PREGS_ECC	0x40UL	/* FHC ECC Control Register (16 bits) */
#define FHC_PREGS_JCTRL	0xf0UL	/* FHC JTAG Control Register */
#define  FHC_JTAG_CTRL_MENAB	0x80000000 /* Indicates this is JTAG Master	 */
#define  FHC_JTAG_CTRL_MNONE	0x40000000 /* Indicates no JTAG Master present	 */
#define FHC_PREGS_JCMD	0x100UL	/* FHC JTAG Command Register */
	unsigned long			ireg;	/* FHC IGN reg */
#define FHC_IREG_IGN	0x00UL	/* This FHC's IGN */
	unsigned long			ffregs;	/* FHC fanfail regs */
#define FHC_FFREGS_IMAP	0x00UL	/* FHC Fanfail IMAP */
#define FHC_FFREGS_ICLR	0x10UL	/* FHC Fanfail ICLR */
	unsigned long			sregs;	/* FHC system regs */
#define FHC_SREGS_IMAP	0x00UL	/* FHC System IMAP */
#define FHC_SREGS_ICLR	0x10UL	/* FHC System ICLR */
	unsigned long			uregs;	/* FHC uart regs */
#define FHC_UREGS_IMAP	0x00UL	/* FHC Uart IMAP */
#define FHC_UREGS_ICLR	0x10UL	/* FHC Uart ICLR */
	unsigned long			tregs;	/* FHC TOD regs */
#define FHC_TREGS_IMAP	0x00UL	/* FHC TOD IMAP */
#define FHC_TREGS_ICLR	0x10UL	/* FHC TOD ICLR */
};

struct linux_fhc {
	struct linux_fhc		*next;
	struct linux_central		*parent;	/* NULL if not central FHC */
	struct fhc_regs			fhc_regs;
	int				board;
	int				jtag_master;
	int				prom_node;
	char				prom_name[64];

	struct linux_prom_ranges	fhc_ranges[PROMREG_MAX];
	int				num_fhc_ranges;
};

extern struct linux_central *central_bus;

extern void apply_central_ranges(struct linux_central *central, 
				 struct linux_prom_registers *regs,
				 int nregs);

extern void apply_fhc_ranges(struct linux_fhc *fhc, 
			     struct linux_prom_registers *regs,
			     int nregs);

#endif /* !(_SPARC64_FHC_H) */
