/*
 * This header provides constants for AT91 pmc status.
 *
 * The constants defined in this header are being used in dts.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _DT_BINDINGS_CLK_AT91_H
#define _DT_BINDINGS_CLK_AT91_H

#define AT91_PMC_MOSCS		0		/* MOSCS Flag */
#define AT91_PMC_LOCKA		1		/* PLLA Lock */
#define AT91_PMC_LOCKB		2		/* PLLB Lock */
#define AT91_PMC_MCKRDY		3		/* Master Clock */
#define AT91_PMC_LOCKU		6		/* UPLL Lock */
#define AT91_PMC_PCKRDY(id)	(8 + (id))	/* Programmable Clock */
#define AT91_PMC_MOSCSELS	16		/* Main Oscillator Selection */
#define AT91_PMC_MOSCRCS	17		/* Main On-Chip RC */
#define AT91_PMC_CFDEV		18		/* Clock Failure Detector Event */

#endif
