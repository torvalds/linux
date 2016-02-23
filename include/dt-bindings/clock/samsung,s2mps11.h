/*
 * Copyright (C) 2015 Markus Reichl
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Device Tree binding constants clocks for the Samsung S2MPS11 PMIC.
 */

#ifndef _DT_BINDINGS_CLOCK_SAMSUNG_S2MPS11_CLOCK_H
#define _DT_BINDINGS_CLOCK_SAMSUNG_S2MPS11_CLOCK_H

/* Fixed rate clocks. */

#define S2MPS11_CLK_AP		0
#define S2MPS11_CLK_CP		1
#define S2MPS11_CLK_BT		2

/* Total number of clocks. */
#define S2MPS11_CLKS_NUM		(S2MPS11_CLK_BT + 1)

#endif /* _DT_BINDINGS_CLOCK_SAMSUNG_S2MPS11_CLOCK_H */
