/****************************************************************************/

/*
 *	m5272sim.h -- ColdFire 5272 System Integration Module support.
 *
 *	(C) Copyright 1999, Greg Ungerer (gerg@snapgear.com)
 * 	(C) Copyright 2000, Lineo Inc. (www.lineo.com) 
 */

/****************************************************************************/
#ifndef	m5272sim_h
#define	m5272sim_h
/****************************************************************************/

#include <linux/config.h>

/*
 *	Define the 5272 SIM register set addresses.
 */
#define	MCFSIM_SCR		0x04		/* SIM Config reg (r/w) */
#define	MCFSIM_SPR		0x06		/* System Protection reg (r/w)*/
#define	MCFSIM_PMR		0x08		/* Power Management reg (r/w) */
#define	MCFSIM_APMR		0x0e		/* Active Low Power reg (r/w) */
#define	MCFSIM_DIR		0x10		/* Device Identity reg (r/w) */

#define	MCFSIM_ICR1		0x20		/* Intr Ctrl reg 1 (r/w) */
#define	MCFSIM_ICR2		0x24		/* Intr Ctrl reg 2 (r/w) */
#define	MCFSIM_ICR3		0x28		/* Intr Ctrl reg 3 (r/w) */
#define	MCFSIM_ICR4		0x2c		/* Intr Ctrl reg 4 (r/w) */

#define MCFSIM_ISR		0x30		/* Interrupt Source reg (r/w) */
#define MCFSIM_PITR		0x34		/* Interrupt Transition (r/w) */
#define	MCFSIM_PIWR		0x38		/* Interrupt Wakeup reg (r/w) */
#define	MCFSIM_PIVR		0x3f		/* Interrupt Vector reg (r/w( */

#define	MCFSIM_WRRR		0x280		/* Watchdog reference (r/w) */
#define	MCFSIM_WIRR		0x284		/* Watchdog interrupt (r/w) */
#define	MCFSIM_WCR		0x288		/* Watchdog counter (r/w) */
#define	MCFSIM_WER		0x28c		/* Watchdog event (r/w) */

#define	MCFSIM_CSBR0		0x40		/* CS0 Base Address (r/w) */
#define	MCFSIM_CSOR0		0x44		/* CS0 Option (r/w) */
#define	MCFSIM_CSBR1		0x48		/* CS1 Base Address (r/w) */
#define	MCFSIM_CSOR1		0x4c		/* CS1 Option (r/w) */
#define	MCFSIM_CSBR2		0x50		/* CS2 Base Address (r/w) */
#define	MCFSIM_CSOR2		0x54		/* CS2 Option (r/w) */
#define	MCFSIM_CSBR3		0x58		/* CS3 Base Address (r/w) */
#define	MCFSIM_CSOR3		0x5c		/* CS3 Option (r/w) */
#define	MCFSIM_CSBR4		0x60		/* CS4 Base Address (r/w) */
#define	MCFSIM_CSOR4		0x64		/* CS4 Option (r/w) */
#define	MCFSIM_CSBR5		0x68		/* CS5 Base Address (r/w) */
#define	MCFSIM_CSOR5		0x6c		/* CS5 Option (r/w) */
#define	MCFSIM_CSBR6		0x70		/* CS6 Base Address (r/w) */
#define	MCFSIM_CSOR6		0x74		/* CS6 Option (r/w) */
#define	MCFSIM_CSBR7		0x78		/* CS7 Base Address (r/w) */
#define	MCFSIM_CSOR7		0x7c		/* CS7 Option (r/w) */

#define	MCFSIM_SDCR		0x180		/* SDRAM Configuration (r/w) */
#define	MCFSIM_SDTR		0x184		/* SDRAM Timing (r/w) */
#define	MCFSIM_DCAR0		0x4c		/* DRAM 0 Address reg(r/w) */
#define	MCFSIM_DCMR0		0x50		/* DRAM 0 Mask reg (r/w) */
#define	MCFSIM_DCCR0		0x57		/* DRAM 0 Control reg (r/w) */
#define	MCFSIM_DCAR1		0x58		/* DRAM 1 Address reg (r/w) */
#define	MCFSIM_DCMR1		0x5c		/* DRAM 1 Mask reg (r/w) */
#define	MCFSIM_DCCR1		0x63		/* DRAM 1 Control reg (r/w) */

#define	MCFSIM_PACNT		0x80		/* Port A Control (r/w) */
#define	MCFSIM_PADDR		0x84		/* Port A Direction (r/w) */
#define	MCFSIM_PADAT		0x86		/* Port A Data (r/w) */
#define	MCFSIM_PBCNT		0x88		/* Port B Control (r/w) */
#define	MCFSIM_PBDDR		0x8c		/* Port B Direction (r/w) */
#define	MCFSIM_PBDAT		0x8e		/* Port B Data (r/w) */
#define	MCFSIM_PCDDR		0x94		/* Port C Direction (r/w) */
#define	MCFSIM_PCDAT		0x96		/* Port C Data (r/w) */
#define	MCFSIM_PDCNT		0x98		/* Port D Control (r/w) */


/****************************************************************************/
#endif	/* m5272sim_h */
