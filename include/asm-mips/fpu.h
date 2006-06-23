/*
 * Copyright (C) 2002 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef _ASM_FPU_H
#define _ASM_FPU_H

#include <linux/sched.h>
#include <linux/thread_info.h>

#include <asm/mipsregs.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/bitops.h>
#include <asm/processor.h>
#include <asm/current.h>

#ifdef CONFIG_MIPS_MT_FPAFF
#include <asm/mips_mt.h>
#endif

struct sigcontext;
struct sigcontext32;

extern asmlinkage int (*save_fp_context)(struct sigcontext *sc);
extern asmlinkage int (*restore_fp_context)(struct sigcontext *sc);

extern asmlinkage int (*save_fp_context32)(struct sigcontext32 *sc);
extern asmlinkage int (*restore_fp_context32)(struct sigcontext32 *sc);

extern void fpu_emulator_init_fpu(void);
extern void _init_fpu(void);
extern void _save_fp(struct task_struct *);
extern void _restore_fp(struct task_struct *);

#if defined(CONFIG_CPU_SB1)
#define __enable_fpu_hazard()						\
do {									\
	asm(".set	push		\n\t"				\
	    ".set	mips64		\n\t"				\
	    ".set	noreorder	\n\t"				\
	    "ssnop			\n\t"				\
	    "bnezl	$0, .+4		\n\t"				\
	    "ssnop			\n\t"				\
	    ".set pop");						\
} while (0)
#else
#define __enable_fpu_hazard()						\
do {									\
	asm("nop;nop;nop;nop");		/* max. hazard */		\
} while (0)
#endif

#define __enable_fpu()							\
do {									\
        set_c0_status(ST0_CU1);						\
        __enable_fpu_hazard();						\
} while (0)

#define __disable_fpu()							\
do {									\
	clear_c0_status(ST0_CU1);					\
	/* We don't care about the c0 hazard here  */			\
} while (0)

#define enable_fpu()							\
do {									\
	if (cpu_has_fpu)						\
		__enable_fpu();						\
} while (0)

#define disable_fpu()							\
do {									\
	if (cpu_has_fpu)						\
		__disable_fpu();					\
} while (0)


#define clear_fpu_owner()	clear_thread_flag(TIF_USEDFPU)

static inline int __is_fpu_owner(void)
{
	return test_thread_flag(TIF_USEDFPU);
}

static inline int is_fpu_owner(void)
{
	return cpu_has_fpu && __is_fpu_owner();
}

static inline void own_fpu(void)
{
	if (cpu_has_fpu) {
		__enable_fpu();
		KSTK_STATUS(current) |= ST0_CU1;
		set_thread_flag(TIF_USEDFPU);
	}
}

static inline void lose_fpu(void)
{
	if (cpu_has_fpu) {
		KSTK_STATUS(current) &= ~ST0_CU1;
		clear_thread_flag(TIF_USEDFPU);
		__disable_fpu();
	}
}

static inline void init_fpu(void)
{
	if (cpu_has_fpu) {
		_init_fpu();
	} else {
		fpu_emulator_init_fpu();
	}
}

static inline void save_fp(struct task_struct *tsk)
{
	if (cpu_has_fpu)
		_save_fp(tsk);
}

static inline void restore_fp(struct task_struct *tsk)
{
	if (cpu_has_fpu)
		_restore_fp(tsk);
}

static inline fpureg_t *get_fpu_regs(struct task_struct *tsk)
{
	if (cpu_has_fpu) {
		if ((tsk == current) && __is_fpu_owner())
			_save_fp(current);
	}

	return tsk->thread.fpu.fpr;
}

#endif /* _ASM_FPU_H */
