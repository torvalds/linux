/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This header provides constants for AT91 pmc status.
 *
 * The constants defined in this header are being used in dts.
 */

#ifndef _DT_BINDINGS_CLK_AT91_H
#define _DT_BINDINGS_CLK_AT91_H

#define PMC_TYPE_CORE		0
#define PMC_TYPE_SYSTEM		1
#define PMC_TYPE_PERIPHERAL	2
#define PMC_TYPE_GCK		3

#define PMC_SLOW		0
#define PMC_MCK			1
#define PMC_UTMI		2
#define PMC_MAIN		3
#define PMC_MCK2		4
#define PMC_I2S0_MUX		5
#define PMC_I2S1_MUX		6

#ifndef AT91_PMC_MOSCS
#define AT91_PMC_MOSCS		0		/* MOSCS Flag */
#define AT91_PMC_LOCKA		1		/* PLLA Lock */
#define AT91_PMC_LOCKB		2		/* PLLB Lock */
#define AT91_PMC_MCKRDY		3		/* Master Clock */
#define AT91_PMC_LOCKU		6		/* UPLL Lock */
#define AT91_PMC_PCKRDY(id)	(8 + (id))	/* Programmable Clock */
#define AT91_PMC_MOSCSELS	16		/* Main Oscillator Selection */
#define AT91_PMC_MOSCRCS	17		/* Main On-Chip RC */
#define AT91_PMC_CFDEV		18		/* Clock Failure Detector Event */
#define AT91_PMC_GCKRDY		24		/* Generated Clocks */
#endif

#endif
