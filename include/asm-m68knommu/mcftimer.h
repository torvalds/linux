/****************************************************************************/

/*
 *	mcftimer.h -- ColdFire internal TIMER support defines.
 *
 *	(C) Copyright 1999-2002, Greg Ungerer (gerg@snapgear.com)
 * 	(C) Copyright 2000, Lineo Inc. (www.lineo.com) 
 */

/****************************************************************************/
#ifndef	mcftimer_h
#define	mcftimer_h
/****************************************************************************/


/*
 *	Get address specific defines for this ColdFire member.
 */
#if defined(CONFIG_M5204) || defined(CONFIG_M5206) || defined(CONFIG_M5206e)
#define	MCFTIMER_BASE1		0x100		/* Base address of TIMER1 */
#define	MCFTIMER_BASE2		0x120		/* Base address of TIMER2 */
#elif defined(CONFIG_M5272)
#define MCFTIMER_BASE1		0x200           /* Base address of TIMER1 */
#define MCFTIMER_BASE2		0x220           /* Base address of TIMER2 */
#define MCFTIMER_BASE3		0x240           /* Base address of TIMER4 */
#define MCFTIMER_BASE4		0x260           /* Base address of TIMER3 */
#elif defined(CONFIG_M5249) || defined(CONFIG_M5307) || defined(CONFIG_M5407)
#define MCFTIMER_BASE1		0x140           /* Base address of TIMER1 */
#define MCFTIMER_BASE2		0x180           /* Base address of TIMER2 */
#endif


/*
 *	Define the TIMER register set addresses.
 */
#define	MCFTIMER_TMR		0x00		/* Timer Mode reg (r/w) */
#define	MCFTIMER_TRR		0x02		/* Timer Reference (r/w) */
#define	MCFTIMER_TCR		0x04		/* Timer Capture reg (r/w) */
#define	MCFTIMER_TCN		0x06		/* Timer Counter reg (r/w) */
#define	MCFTIMER_TER		0x11		/* Timer Event reg (r/w) */

struct mcftimer {
	unsigned short	tmr;			/* Timer Mode reg (r/w) */
	unsigned short	reserved1;
	unsigned short	trr;			/* Timer Reference (r/w) */
	unsigned short	reserved2;
	unsigned short	tcr;			/* Timer Capture reg (r/w) */
	unsigned short	reserved3;
	unsigned short	tcn;			/* Timer Counter reg (r/w) */
	unsigned short	reserved4;
	unsigned char	reserved5;
	unsigned char	ter;			/* Timer Event reg (r/w) */
} __attribute__((packed));

/*
 *	Bit definitions for the Timer Mode Register (TMR).
 *	Register bit flags are common accross ColdFires.
 */
#define	MCFTIMER_TMR_PREMASK	0xff00		/* Prescalar mask */
#define	MCFTIMER_TMR_DISCE	0x0000		/* Disable capture */
#define	MCFTIMER_TMR_ANYCE	0x00c0		/* Capture any edge */
#define	MCFTIMER_TMR_FALLCE	0x0080		/* Capture fallingedge */
#define	MCFTIMER_TMR_RISECE	0x0040		/* Capture rising edge */
#define	MCFTIMER_TMR_ENOM	0x0020		/* Enable output toggle */
#define	MCFTIMER_TMR_DISOM	0x0000		/* Do single output pulse  */
#define	MCFTIMER_TMR_ENORI	0x0010		/* Enable ref interrupt */
#define	MCFTIMER_TMR_DISORI	0x0000		/* Disable ref interrupt */
#define	MCFTIMER_TMR_RESTART	0x0008		/* Restart counter */
#define	MCFTIMER_TMR_FREERUN	0x0000		/* Free running counter */
#define	MCFTIMER_TMR_CLKTIN	0x0006		/* Input clock is TIN */
#define	MCFTIMER_TMR_CLK16	0x0004		/* Input clock is /16 */
#define	MCFTIMER_TMR_CLK1	0x0002		/* Input clock is /1 */
#define	MCFTIMER_TMR_CLKSTOP	0x0000		/* Stop counter */
#define	MCFTIMER_TMR_ENABLE	0x0001		/* Enable timer */
#define	MCFTIMER_TMR_DISABLE	0x0000		/* Disable timer */

/*
 *	Bit definitions for the Timer Event Registers (TER).
 */
#define	MCFTIMER_TER_CAP	0x01		/* Capture event */
#define	MCFTIMER_TER_REF	0x02		/* Refernece event */

/****************************************************************************/
#endif	/* mcftimer_h */
