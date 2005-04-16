/****************************************************************************/

/*
 *	coldfire.h -- Motorola ColdFire CPU sepecific defines
 *
 *	(C) Copyright 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000, Lineo (www.lineo.com)
 */

/****************************************************************************/
#ifndef	coldfire_h
#define	coldfire_h
/****************************************************************************/

#include <linux/config.h>

/*
 *	Define the processor support peripherals base address.
 *	This is generally setup by the boards start up code.
 */
#define	MCF_MBAR	0x10000000
#define	MCF_MBAR2	0x80000000
#define	MCF_IPSBAR	0x40000000

#if defined(CONFIG_M527x) || defined(CONFIG_M528x)
#undef MCF_MBAR
#define	MCF_MBAR	MCF_IPSBAR
#endif

/*
 *	Define master clock frequency.
 */
#if defined(CONFIG_CLOCK_11MHz)
#define	MCF_CLK		11289600
#elif defined(CONFIG_CLOCK_16MHz)
#define	MCF_CLK		16000000
#elif defined(CONFIG_CLOCK_20MHz)
#define	MCF_CLK		20000000
#elif defined(CONFIG_CLOCK_24MHz)
#define	MCF_CLK		24000000
#elif defined(CONFIG_CLOCK_25MHz)
#define	MCF_CLK		25000000
#elif defined(CONFIG_CLOCK_33MHz)
#define	MCF_CLK		33000000
#elif defined(CONFIG_CLOCK_40MHz)
#define	MCF_CLK		40000000
#elif defined(CONFIG_CLOCK_45MHz)
#define	MCF_CLK		45000000
#elif defined(CONFIG_CLOCK_48MHz)
#define	MCF_CLK		48000000
#elif defined(CONFIG_CLOCK_50MHz)
#define	MCF_CLK		50000000
#elif defined(CONFIG_CLOCK_54MHz)
#define	MCF_CLK		54000000
#elif defined(CONFIG_CLOCK_60MHz)
#define	MCF_CLK		60000000
#elif defined(CONFIG_CLOCK_64MHz)
#define	MCF_CLK		64000000
#elif defined(CONFIG_CLOCK_66MHz)
#define	MCF_CLK		66000000
#elif defined(CONFIG_CLOCK_70MHz)
#define	MCF_CLK		70000000
#elif defined(CONFIG_CLOCK_100MHz)
#define	MCF_CLK		100000000
#elif defined(CONFIG_CLOCK_140MHz)
#define	MCF_CLK		140000000
#elif defined(CONFIG_CLOCK_150MHz)
#define	MCF_CLK		150000000
#elif defined(CONFIG_CLOCK_166MHz)
#define	MCF_CLK		166000000
#else
#error "Don't know what your ColdFire CPU clock frequency is??"
#endif

/*
 *	One some ColdFire family members the bus clock (used by internal
 *	peripherals) is not the same as the CPU clock.
 */
#if defined(CONFIG_M5249) || defined(CONFIG_M527x)
#define	MCF_BUSCLK	(MCF_CLK / 2)
#else
#define	MCF_BUSCLK	MCF_CLK
#endif

/****************************************************************************/
#endif	/* coldfire_h */
