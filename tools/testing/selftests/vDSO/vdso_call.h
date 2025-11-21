/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Macro to call vDSO functions
 *
 * Copyright (C) 2024 Christophe Leroy <christophe.leroy@csgroup.eu>, CS GROUP France
 */
#ifndef __VDSO_CALL_H__
#define __VDSO_CALL_H__

#ifdef __powerpc__

#define LOADARGS_1(fn, __arg1) do {					\
	_r0 = fn;							\
	_r3 = (long)__arg1;						\
} while (0)

#define LOADARGS_2(fn, __arg1, __arg2) do {				\
	_r0 = fn;							\
	_r3 = (long)__arg1;						\
	_r4 = (long)__arg2;						\
} while (0)

#define LOADARGS_3(fn, __arg1, __arg2, __arg3) do {			\
	_r0 = fn;							\
	_r3 = (long)__arg1;						\
	_r4 = (long)__arg2;						\
	_r5 = (long)__arg3;						\
} while (0)

#define LOADARGS_5(fn, __arg1, __arg2, __arg3, __arg4, __arg5) do {	\
	_r0 = fn;							\
	_r3 = (long)__arg1;						\
	_r4 = (long)__arg2;						\
	_r5 = (long)__arg3;						\
	_r6 = (long)__arg4;						\
	_r7 = (long)__arg5;						\
} while (0)

#define VDSO_CALL(fn, nr, args...) ({					\
	register void *_r0 asm ("r0");					\
	register long _r3 asm ("r3");					\
	register long _r4 asm ("r4");					\
	register long _r5 asm ("r5");					\
	register long _r6 asm ("r6");					\
	register long _r7 asm ("r7");					\
	register long _r8 asm ("r8");					\
									\
	LOADARGS_##nr(fn, args);					\
									\
	asm volatile(							\
		"	mtctr %0\n"					\
		"	bctrl\n"					\
		"	bns+	1f\n"					\
		"	neg	3, 3\n"					\
		"1:"							\
		: "+r" (_r0), "+r" (_r3), "+r" (_r4), "+r" (_r5),	\
		  "+r" (_r6), "+r" (_r7), "+r" (_r8)			\
		:							\
		: "r9", "r10", "r11", "r12", "cr0", "cr1", "cr5",	\
		  "cr6", "cr7", "xer", "lr", "ctr", "memory"		\
	);								\
	_r3;								\
})

#else
#define VDSO_CALL(fn, nr, args...)	fn(args)
#endif

#endif
