/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_COMMON_H__
#define __ASM_ARCH_MXC_COMMON_H__

extern void mxc_map_io(void);
extern void mxc_init_irq(void);
extern void mxc_timer_init(const char *clk_timer);
extern int mxc_clocks_init(unsigned long fref);
extern int mxc_register_gpios(void);

#endif
