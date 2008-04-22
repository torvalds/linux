/*
 * include/asm-arm/arch-ns9xxx/system.h
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/proc-fns.h>
#include <asm/arch-ns9xxx/processor.h>
#include <asm/arch-ns9xxx/processor-ns9360.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
#ifdef CONFIG_PROCESSOR_NS9360
	if (processor_is_ns9360())
		ns9360_reset(mode);
	else
#endif
		BUG();

	BUG();
}

#endif /* ifndef __ASM_ARCH_SYSTEM_H */
