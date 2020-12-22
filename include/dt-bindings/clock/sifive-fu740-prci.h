/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (C) 2019 SiFive, Inc.
 * Wesley Terpstra
 * Paul Walmsley
 * Zong Li
 */

#ifndef __DT_BINDINGS_CLOCK_SIFIVE_FU740_PRCI_H
#define __DT_BINDINGS_CLOCK_SIFIVE_FU740_PRCI_H

/* Clock indexes for use by Device Tree data and the PRCI driver */

#define PRCI_CLK_COREPLL	       0
#define PRCI_CLK_DDRPLL		       1
#define PRCI_CLK_GEMGXLPLL	       2
#define PRCI_CLK_DVFSCOREPLL	       3
#define PRCI_CLK_HFPCLKPLL	       4
#define PRCI_CLK_CLTXPLL	       5
#define PRCI_CLK_TLCLK		       6
#define PRCI_CLK_PCLK		       7

#endif	/* __DT_BINDINGS_CLOCK_SIFIVE_FU740_PRCI_H */
