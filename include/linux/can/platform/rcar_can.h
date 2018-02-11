/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CAN_PLATFORM_RCAR_CAN_H_
#define _CAN_PLATFORM_RCAR_CAN_H_

#include <linux/types.h>

/* Clock Select Register settings */
enum CLKR {
	CLKR_CLKP1 = 0,	/* Peripheral clock (clkp1) */
	CLKR_CLKP2 = 1,	/* Peripheral clock (clkp2) */
	CLKR_CLKEXT = 3	/* Externally input clock */
};

struct rcar_can_platform_data {
	enum CLKR clock_select;	/* Clock source select */
};

#endif	/* !_CAN_PLATFORM_RCAR_CAN_H_ */
