/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Loongson-1 clock tree IDs
 *
 * Copyright (C) 2023 Keguang Zhang <keguang.zhang@gmail.com>
 */

#ifndef __DT_BINDINGS_CLOCK_LS1X_CLK_H__
#define __DT_BINDINGS_CLOCK_LS1X_CLK_H__

#define LS1X_CLKID_PLL	0
#define LS1X_CLKID_CPU	1
#define LS1X_CLKID_DC	2
#define LS1X_CLKID_AHB	3
#define LS1X_CLKID_APB	4

#define CLK_NR_CLKS	(LS1X_CLKID_APB + 1)

#endif /* __DT_BINDINGS_CLOCK_LS1X_CLK_H__ */
