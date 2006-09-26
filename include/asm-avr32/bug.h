/*
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_BUG_H
#define __ASM_AVR32_BUG_H

#ifdef CONFIG_BUG

/*
 * According to our Chief Architect, this compact opcode is very
 * unlikely to ever be implemented.
 */
#define AVR32_BUG_OPCODE	0x5df0

#ifdef CONFIG_DEBUG_BUGVERBOSE

#define BUG()								\
	do {								\
		asm volatile(".hword	%0\n\t"				\
			     ".hword	%1\n\t"				\
			     ".long	%2"				\
			     :						\
			     : "n"(AVR32_BUG_OPCODE),			\
			       "i"(__LINE__), "X"(__FILE__));		\
	} while (0)

#else

#define BUG()								\
	do {								\
		asm volatile(".hword	%0\n\t"				\
			     : : "n"(AVR32_BUG_OPCODE));		\
	} while (0)

#endif /* CONFIG_DEBUG_BUGVERBOSE */

#define HAVE_ARCH_BUG

#endif /* CONFIG_BUG */

#include <asm-generic/bug.h>

#endif /* __ASM_AVR32_BUG_H */
