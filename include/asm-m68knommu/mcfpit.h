/****************************************************************************/

/*
 *	mcfpit.h -- ColdFire internal PIT timer support defines.
 *
 *	(C) Copyright 2003, Greg Ungerer (gerg@snapgear.com)
 */

/****************************************************************************/
#ifndef	mcfpit_h
#define	mcfpit_h
/****************************************************************************/

#include <linux/config.h>

/*
 *	Get address specific defines for the 5270/5271 and 5280/5282.
 */
#define	MCFPIT_BASE1		0x00150000	/* Base address of TIMER1 */
#define	MCFPIT_BASE2		0x00160000	/* Base address of TIMER2 */
#define	MCFPIT_BASE3		0x00170000	/* Base address of TIMER3 */
#define	MCFPIT_BASE4		0x00180000	/* Base address of TIMER4 */


/*
 *	Define the PIT timer register set addresses.
 */
struct mcfpit {
	unsigned short	pcsr;			/* PIT control and status */
	unsigned short	pmr;			/* PIT modulus register */
	unsigned short	pcntr;			/* PIT count register */
} __attribute__((packed));

/*
 *	Bit definitions for the PIT Control and Status register.
 */
#define	MCFPIT_PCSR_CLK1	0x0000		/* System clock divisor */
#define	MCFPIT_PCSR_CLK2	0x0100		/* System clock divisor */
#define	MCFPIT_PCSR_CLK4	0x0200		/* System clock divisor */
#define	MCFPIT_PCSR_CLK8	0x0300		/* System clock divisor */
#define	MCFPIT_PCSR_CLK16	0x0400		/* System clock divisor */
#define	MCFPIT_PCSR_CLK32	0x0500		/* System clock divisor */
#define	MCFPIT_PCSR_CLK64	0x0600		/* System clock divisor */
#define	MCFPIT_PCSR_CLK128	0x0700		/* System clock divisor */
#define	MCFPIT_PCSR_CLK256	0x0800		/* System clock divisor */
#define	MCFPIT_PCSR_CLK512	0x0900		/* System clock divisor */
#define	MCFPIT_PCSR_CLK1024	0x0a00		/* System clock divisor */
#define	MCFPIT_PCSR_CLK2048	0x0b00		/* System clock divisor */
#define	MCFPIT_PCSR_CLK4096	0x0c00		/* System clock divisor */
#define	MCFPIT_PCSR_CLK8192	0x0d00		/* System clock divisor */
#define	MCFPIT_PCSR_CLK16384	0x0e00		/* System clock divisor */
#define	MCFPIT_PCSR_CLK32768	0x0f00		/* System clock divisor */
#define	MCFPIT_PCSR_DOZE	0x0040		/* Clock run in doze mode */
#define	MCFPIT_PCSR_HALTED	0x0020		/* Clock run in halt mode */
#define	MCFPIT_PCSR_OVW		0x0010		/* Overwrite PIT counter now */
#define	MCFPIT_PCSR_PIE		0x0008		/* Enable PIT interrupt */
#define	MCFPIT_PCSR_PIF		0x0004		/* PIT interrupt flag */
#define	MCFPIT_PCSR_RLD		0x0002		/* Reload counter */
#define	MCFPIT_PCSR_EN		0x0001		/* Enable PIT */
#define	MCFPIT_PCSR_DISABLE	0x0000		/* Disable PIT */

/****************************************************************************/
#endif	/* mcfpit_h */
