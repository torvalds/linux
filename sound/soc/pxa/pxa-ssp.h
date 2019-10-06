/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ASoC PXA SSP port support
 */

#ifndef _PXA_SSP_H
#define _PXA_SSP_H

/* SSP clock sources */
#define PXA_SSP_CLK_PLL	0
#define PXA_SSP_CLK_EXT	1
#define PXA_SSP_CLK_NET	2
#define PXA_SSP_CLK_AUDIO	3
#define PXA_SSP_CLK_NET_PLL	4

/* SSP audio dividers */
#define PXA_SSP_AUDIO_DIV_ACDS		0
#define PXA_SSP_AUDIO_DIV_SCDB		1
#define PXA_SSP_DIV_SCR				2

/* SSP ACDS audio dividers values */
#define PXA_SSP_CLK_AUDIO_DIV_1		0
#define PXA_SSP_CLK_AUDIO_DIV_2		1
#define PXA_SSP_CLK_AUDIO_DIV_4		2
#define PXA_SSP_CLK_AUDIO_DIV_8		3
#define PXA_SSP_CLK_AUDIO_DIV_16	4
#define PXA_SSP_CLK_AUDIO_DIV_32	5

/* SSP divider bypass */
#define PXA_SSP_CLK_SCDB_4		0
#define PXA_SSP_CLK_SCDB_1		1
#define PXA_SSP_CLK_SCDB_8		2

#define PXA_SSP_PLL_OUT  0

#endif
