/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Common low level (register) ptrace helpers
 *
 * Copyright 2004-2011 Analog Devices Inc.
 */

#ifndef __ASM_GENERIC_PTRACE_H__
#define __ASM_GENERIC_PTRACE_H__

#ifndef __ASSEMBLY__

/* Helpers for working with the instruction pointer */
#ifndef GET_IP
#define GET_IP(regs) ((regs)->pc)
#endif
#ifndef SET_IP
#define SET_IP(regs, val) (GET_IP(regs) = (val))
#endif

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return GET_IP(regs);
}
static inline void instruction_pointer_set(struct pt_regs *regs,
                                           unsigned long val)
{
	SET_IP(regs, val);
}

#ifndef profile_pc
#define profile_pc(regs) instruction_pointer(regs)
#endif

/* Helpers for working with the user stack pointer */
#ifndef GET_USP
#define GET_USP(regs) ((regs)->usp)
#endif
#ifndef SET_USP
#define SET_USP(regs, val) (GET_USP(regs) = (val))
#endif

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return GET_USP(regs);
}
static inline void user_stack_pointer_set(struct pt_regs *regs,
                                          unsigned long val)
{
	SET_USP(regs, val);
}

/* Helpers for working with the frame pointer */
#ifndef GET_FP
#define GET_FP(regs) ((regs)->fp)
#endif
#ifndef SET_FP
#define SET_FP(regs, val) (GET_FP(regs) = (val))
#endif

static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return GET_FP(regs);
}
static inline void frame_pointer_set(struct pt_regs *regs,
                                     unsigned long val)
{
	SET_FP(regs, val);
}

#endif /* __ASSEMBLY__ */

#endif
