/*
 * include/asm-v850/entry.h -- Definitions used by low-level trap handlers
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_ENTRY_H__
#define __V850_ENTRY_H__


#include <asm/ptrace.h>
#include <asm/machdep.h>


/* These are special variables using by the kernel trap/interrupt code
   to save registers in, at a time when there are no spare registers we
   can use to do so, and we can't depend on the value of the stack
   pointer.  This means that they must be within a signed 16-bit
   displacement of 0x00000000.  */

#define KERNEL_VAR_SPACE_ADDR	R0_RAM_ADDR

#ifdef __ASSEMBLY__
#define KERNEL_VAR(addr)	addr[r0]
#else
#define KERNEL_VAR(addr)	(*(volatile unsigned long *)(addr))
#endif

/* Kernel stack pointer, 4 bytes.  */
#define KSP_ADDR		(KERNEL_VAR_SPACE_ADDR +  0)
#define KSP			KERNEL_VAR (KSP_ADDR)
/* 1 if in kernel-mode, 0 if in user mode, 1 byte.  */
#define KM_ADDR 		(KERNEL_VAR_SPACE_ADDR +  4)
#define KM			KERNEL_VAR (KM_ADDR)
/* Temporary storage for interrupt handlers, 4 bytes.  */
#define INT_SCRATCH_ADDR	(KERNEL_VAR_SPACE_ADDR +  8)
#define INT_SCRATCH		KERNEL_VAR (INT_SCRATCH_ADDR)
/* Where the stack-pointer is saved when jumping to various sorts of
   interrupt handlers.  ENTRY_SP is used by everything except NMIs,
   which have their own location.  Higher-priority NMIs can clobber the
   value written by a lower priority NMI, since they can't be disabled,
   but that's OK, because only NMI0 (the lowest-priority one) is allowed
   to return.  */
#define ENTRY_SP_ADDR		(KERNEL_VAR_SPACE_ADDR + 12)
#define ENTRY_SP		KERNEL_VAR (ENTRY_SP_ADDR)
#define NMI_ENTRY_SP_ADDR	(KERNEL_VAR_SPACE_ADDR + 16)
#define NMI_ENTRY_SP		KERNEL_VAR (NMI_ENTRY_SP_ADDR)

#ifdef CONFIG_RESET_GUARD
/* Used to detect unexpected resets (since the v850 has no MMU, any call
   through a null pointer will jump to the reset vector).  We detect
   such resets by checking for a magic value, RESET_GUARD_ACTIVE, in
   this location.  Properly resetting the machine stores zero there, so
   it shouldn't trigger the guard; the power-on value is uncertain, but
   it's unlikely to be RESET_GUARD_ACTIVE.  */
#define RESET_GUARD_ADDR	(KERNEL_VAR_SPACE_ADDR + 28)
#define RESET_GUARD		KERNEL_VAR (RESET_GUARD_ADDR)
#define RESET_GUARD_ACTIVE	0xFAB4BEEF
#endif /* CONFIG_RESET_GUARD */

#ifdef CONFIG_V850E_HIGHRES_TIMER
#define HIGHRES_TIMER_SLOW_TICKS_ADDR (KERNEL_VAR_SPACE_ADDR + 32)
#define HIGHRES_TIMER_SLOW_TICKS     KERNEL_VAR (HIGHRES_TIMER_SLOW_TICKS_ADDR)
#endif /* CONFIG_V850E_HIGHRES_TIMER */

#ifndef __ASSEMBLY__

#ifdef CONFIG_RESET_GUARD
/* Turn off reset guard, so that resetting the machine works normally.
   This should be called in the various machine_halt, etc., functions.  */
static inline void disable_reset_guard (void)
{
	RESET_GUARD = 0;
}
#endif /* CONFIG_RESET_GUARD */

#endif /* !__ASSEMBLY__ */


/* A `state save frame' is a struct pt_regs preceded by some extra space
   suitable for a function call stack frame.  */

/* Amount of room on the stack reserved for arguments and to satisfy the
   C calling conventions, in addition to the space used by the struct
   pt_regs that actually holds saved values.  */
#define STATE_SAVE_ARG_SPACE	(6*4) /* Up to six arguments.  */


#ifdef __ASSEMBLY__

/* The size of a state save frame.  */
#define STATE_SAVE_SIZE		(PT_SIZE + STATE_SAVE_ARG_SPACE)

#else /* !__ASSEMBLY__ */

/* The size of a state save frame.  */
#define STATE_SAVE_SIZE	       (sizeof (struct pt_regs) + STATE_SAVE_ARG_SPACE)

#endif /* __ASSEMBLY__ */


/* Offset of the struct pt_regs in a state save frame.  */
#define STATE_SAVE_PT_OFFSET	STATE_SAVE_ARG_SPACE


#endif /* __V850_ENTRY_H__ */
