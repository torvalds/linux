/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019-20 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2020 Western Digital Corporation or its affiliates.
 */
#ifndef K210_CLK_H
#define K210_CLK_H

/*
 * Arbitrary identifiers for clocks.
 * The structure is: in0 -> pll0 -> aclk -> cpu
 *
 * Since we use the hardware defaults for now, set all these to the same clock.
 */
#define K210_CLK_PLL0   0
#define K210_CLK_PLL1   0
#define K210_CLK_ACLK   0
#define K210_CLK_CPU    0

#endif /* K210_CLK_H */
