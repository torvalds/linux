/*
 * include/asm-arm/arch-ns9xxx/processor.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_PROCESSOR_H
#define __ASM_ARCH_PROCESSOR_H

#include <asm/mach-types.h>

#define processor_is_ns9360()	(machine_is_cc9p9360dev()		\
		|| machine_is_cc9p9360js())

#endif /* ifndef __ASM_ARCH_PROCESSOR_H */
