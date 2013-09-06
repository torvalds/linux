/*
 * include/asm-generic/fncpy.h - helper macros for function body copying
 *
 * Copyright (C) 2011 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 * These macros are intended for use when there is a need to copy a low-level
 * function body into special memory.
 *
 * For example, when reconfiguring the SDRAM controller, the code doing the
 * reconfiguration may need to run from SRAM.
 *
 * NOTE: that the copied function body must be entirely self-contained and
 * position-independent in order for this to work properly.
 *
 * Typical usage example:
 *
 * extern int f(args);
 * extern uint32_t size_of_f;
 * int (*copied_f)(args);
 * void *sram_buffer;
 *
 * copied_f = fncpy(sram_buffer, &f, size_of_f);
 *
 * ... later, call the function: ...
 *
 * copied_f(args);
 *
 * The size of the function to be copied can't be determined from C:
 * this must be determined by other means, such as adding assmbler directives
 * in the file where f is defined.
 */

#ifndef __ASM_GENERIC_FNCPY_H
#define __ASM_GENERIC_FNCPY_H

#include <linux/types.h>
#include <linux/string.h>

#include <asm/bug.h>
#include <asm/cacheflush.h>

/*
 * Minimum alignment requirement for the source and destination addresses
 * for function copying.
 */
#ifndef ARCH_FNCPY_ALIGN
#define ARCH_FNCPY_ALIGN	0
#endif

#define ARCH_FNCPY_MASK		((1 << (ARCH_FNCPY_ALIGN)) - 1)

#ifndef fnptr_to_addr
#define fnptr_to_addr(funcp) ({						\
	(uintptr_t) (funcp);						\
})
#endif

#ifndef fnptr_translate
#define fnptr_translate(orig_funcp, new_addr) ({			\
	(typeof(orig_funcp)) (new_addr);				\
})
#endif

/* Ensure alignment of source and destination addresses */
#ifndef fn_dest_invalid
#define fn_dest_invalid(funcp, dest_buf) ({				\
	uintptr_t __funcp_address;					\
									\
	__funcp_address = fnptr_to_addr(funcp);				\
									\
	((uintptr_t)(dest_buf) & ARCH_FNCPY_MASK) ||			\
		(__funcp_address & ARCH_FNCPY_MASK);			\
})
#endif

#ifndef fncpy
#define fncpy(dest_buf, funcp, size) ({					\
	BUG_ON(fn_dest_invalid(funcp, dest_buf));			\
									\
	memcpy(dest_buf, (void const *)(funcp), size);			\
	flush_icache_range((unsigned long)(dest_buf),			\
		(unsigned long)(dest_buf) + (size));			\
									\
	fnptr_translate(funcp, dest_buf);				\
})
#endif

#endif /* !__ASM_GENERIC_FNCPY_H */

