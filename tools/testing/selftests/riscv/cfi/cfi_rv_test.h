/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SELFTEST_RISCV_CFI_H
#define SELFTEST_RISCV_CFI_H
#include <stddef.h>
#include <sys/types.h>
#include "shadowstack.h"

#define CHILD_EXIT_CODE_SSWRITE		10
#define CHILD_EXIT_CODE_SIG_TEST	11

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)			\
({									\
	register long _num  __asm__ ("a7") = (num);			\
	register long _arg1 __asm__ ("a0") = (long)(arg1);		\
	register long _arg2 __asm__ ("a1") = (long)(arg2);		\
	register long _arg3 __asm__ ("a2") = (long)(arg3);		\
	register long _arg4 __asm__ ("a3") = (long)(arg4);		\
	register long _arg5 __asm__ ("a4") = (long)(arg5);		\
									\
	__asm__ volatile(						\
		"ecall\n"						\
		: "+r"							\
		(_arg1)							\
		: "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5),	\
		  "r"(_num)						\
		: "memory", "cc"					\
	);								\
	_arg1;								\
})

#define my_syscall3(num, arg1, arg2, arg3)				\
({									\
	register long _num  __asm__ ("a7") = (num);			\
	register long _arg1 __asm__ ("a0") = (long)(arg1);		\
	register long _arg2 __asm__ ("a1") = (long)(arg2);		\
	register long _arg3 __asm__ ("a2") = (long)(arg3);		\
									\
	__asm__ volatile(						\
		"ecall\n"						\
		: "+r" (_arg1)						\
		: "r"(_arg2), "r"(_arg3),				\
		  "r"(_num)						\
		: "memory", "cc"					\
	);								\
	_arg1;								\
})

#ifndef __NR_prctl
#define __NR_prctl 167
#endif

#ifndef __NR_map_shadow_stack
#define __NR_map_shadow_stack 453
#endif

#define CSR_SSP 0x011

#ifdef __ASSEMBLY__
#define __ASM_STR(x)    x
#else
#define __ASM_STR(x)    #x
#endif

#define csr_read(csr)							\
({									\
	register unsigned long __v;					\
	__asm__ __volatile__ ("csrr %0, " __ASM_STR(csr)		\
				: "=r" (__v) :				\
				: "memory");				\
	__v;								\
})

#define csr_write(csr, val)						\
({									\
	unsigned long __v = (unsigned long)(val);			\
	__asm__ __volatile__ ("csrw " __ASM_STR(csr) ", %0"		\
				: : "rK" (__v)				\
				: "memory");				\
})

#endif
