/*
 * Platform defintions for Titan
 */

#ifndef _ASM_SH_TITAN_TITAN_H
#define _ASM_SH_TITAN_TITAN_H

#define __IO_PREFIX titan
#include <asm/io_generic.h>

/* IRQ assignments */
#define TITAN_IRQ_WAN		2	/* eth0 (WAN) */
#define TITAN_IRQ_LAN		5	/* eth1 (LAN) */
#define TITAN_IRQ_MPCIA		8	/* mPCI A */
#define TITAN_IRQ_MPCIB		11	/* mPCI B */
#define TITAN_IRQ_USB		11	/* USB */

/*
 * The external interrupt lines, these take up ints 0 - 15 inclusive
 * depending on the priority for the interrupt.  In fact the priority
 * is the interrupt :-)
 */
#define IRL0_IRQ	0
#define IRL0_IPR_ADDR	INTC_IPRD
#define IRL0_IPR_POS	3
#define IRL0_PRIORITY	8

#define IRL1_IRQ	1
#define IRL1_IPR_ADDR	INTC_IPRD
#define IRL1_IPR_POS	2
#define IRL1_PRIORITY	8

#define IRL2_IRQ	2
#define IRL2_IPR_ADDR	INTC_IPRD
#define IRL2_IPR_POS	1
#define IRL2_PRIORITY	8

#define IRL3_IRQ	3
#define IRL3_IPR_ADDR	INTC_IPRD
#define IRL3_IPR_POS	0
#define IRL3_PRIORITY	8

#endif
