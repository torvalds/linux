/*
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_HARDWARE_H__
#define __ASM_ARCH_MXC_HARDWARE_H__

#include <asm/sizes.h>

#ifdef CONFIG_ARCH_MX3
# include <asm/arch/mx31.h>
#endif

#include <asm/arch/mxc.h>

/*
 * ---------------------------------------------------------------------------
 * Board specific defines
 * ---------------------------------------------------------------------------
 */
#ifdef CONFIG_MACH_MX31ADS
# include <asm/arch/board-mx31ads.h>
#endif

#endif /* __ASM_ARCH_MXC_HARDWARE_H__ */
