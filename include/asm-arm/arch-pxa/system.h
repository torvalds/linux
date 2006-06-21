/*
 * linux/include/asm-arm/arch-pxa/system.h
 *
 * Author:	Nicolas Pitre
 * Created:	Jun 15, 2001
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/proc-fns.h>
#include "hardware.h"
#include "pxa-regs.h"

static inline void arch_idle(void)
{
	cpu_do_idle();
}


static inline void arch_reset(char mode)
{
	if (mode == 's') {
		/* Jump into ROM at address 0 */
		cpu_reset(0);
	} else {
		/* Initialize the watchdog and let it fire */
		OWER = OWER_WME;
		OSSR = OSSR_M3;
		OSMR3 = OSCR + 368640;	/* ... in 100 ms */
	}
}

