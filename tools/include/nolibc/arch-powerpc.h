/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * PowerPC specific definitions for NOLIBC
 * Copyright (C) 2023 Zhangjin Wu <falcon@tinylab.org>
 */

#ifndef _NOLIBC_ARCH_POWERPC_H
#define _NOLIBC_ARCH_POWERPC_H

#include "compiler.h"
#include "crt.h"

/* Syscalls for PowerPC :
 *   - stack is 16-byte aligned
 *   - syscall number is passed in r0
 *   - arguments are in r3, r4, r5, r6, r7, r8, r9
 *   - the system call is performed by calling "sc"
 *   - syscall return comes in r3, and the summary overflow bit is checked
 *     to know if an error occurred, in which case errno is in r3.
 *   - the arguments are cast to long and assigned into the target
 *     registers which are then simply passed as registers to the asm code,
 *     so that we don't have to experience issues with register constraints.
 */

#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"memory", "cr0", "r12", "r11", "r10", "r9"

#define my_syscall0(num)                                                     \
({                                                                           \
	register long _ret  __asm__ ("r3");                                  \
	register long _num  __asm__ ("r0") = (num);                          \
									     \
	__asm__ volatile (                                                   \
		"	sc\n"                                                \
		"	bns+ 1f\n"                                           \
		"	neg  %0, %0\n"                                       \
		"1:\n"                                                       \
		: "=r"(_ret), "+r"(_num)                                     \
		:                                                            \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "r8", "r7", "r6", "r5", "r4"  \
	);                                                                   \
	_ret;                                                                \
})

#define my_syscall1(num, arg1)                                               \
({                                                                           \
	register long _ret  __asm__ ("r3");                                  \
	register long _num  __asm__ ("r0") = (num);                          \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                   \
									     \
	__asm__ volatile (                                                   \
		"	sc\n"                                                \
		"	bns+ 1f\n"                                           \
		"	neg  %0, %0\n"                                       \
		"1:\n"                                                       \
		: "=r"(_ret), "+r"(_num)                                     \
		: "0"(_arg1)                                                 \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "r8", "r7", "r6", "r5", "r4"  \
	);                                                                   \
	_ret;                                                                \
})


#define my_syscall2(num, arg1, arg2)                                         \
({                                                                           \
	register long _ret  __asm__ ("r3");                                  \
	register long _num  __asm__ ("r0") = (num);                          \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                   \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                   \
									     \
	__asm__ volatile (                                                   \
		"	sc\n"                                                \
		"	bns+ 1f\n"                                           \
		"	neg  %0, %0\n"                                       \
		"1:\n"                                                       \
		: "=r"(_ret), "+r"(_num), "+r"(_arg2)                        \
		: "0"(_arg1)                                                 \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "r8", "r7", "r6", "r5"        \
	);                                                                   \
	_ret;                                                                \
})


#define my_syscall3(num, arg1, arg2, arg3)                                   \
({                                                                           \
	register long _ret  __asm__ ("r3");                                  \
	register long _num  __asm__ ("r0") = (num);                          \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                   \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                   \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                   \
									     \
	__asm__ volatile (                                                   \
		"	sc\n"                                                \
		"	bns+ 1f\n"                                           \
		"	neg  %0, %0\n"                                       \
		"1:\n"                                                       \
		: "=r"(_ret), "+r"(_num), "+r"(_arg2), "+r"(_arg3)           \
		: "0"(_arg1)                                                 \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "r8", "r7", "r6"              \
	);                                                                   \
	_ret;                                                                \
})


#define my_syscall4(num, arg1, arg2, arg3, arg4)                             \
({                                                                           \
	register long _ret  __asm__ ("r3");                                  \
	register long _num  __asm__ ("r0") = (num);                          \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                   \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                   \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                   \
	register long _arg4 __asm__ ("r6") = (long)(arg4);                   \
									     \
	__asm__ volatile (                                                   \
		"	sc\n"                                                \
		"	bns+ 1f\n"                                           \
		"	neg  %0, %0\n"                                       \
		"1:\n"                                                       \
		: "=r"(_ret), "+r"(_num), "+r"(_arg2), "+r"(_arg3),          \
		  "+r"(_arg4)                                                \
		: "0"(_arg1)                                                 \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "r8", "r7"                    \
	);                                                                   \
	_ret;                                                                \
})


#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                       \
({                                                                           \
	register long _ret  __asm__ ("r3");                                  \
	register long _num  __asm__ ("r0") = (num);                          \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                   \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                   \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                   \
	register long _arg4 __asm__ ("r6") = (long)(arg4);                   \
	register long _arg5 __asm__ ("r7") = (long)(arg5);                   \
									     \
	__asm__ volatile (                                                   \
		"	sc\n"                                                \
		"	bns+ 1f\n"                                           \
		"	neg  %0, %0\n"                                       \
		"1:\n"                                                       \
		: "=r"(_ret), "+r"(_num), "+r"(_arg2), "+r"(_arg3),          \
		  "+r"(_arg4), "+r"(_arg5)                                   \
		: "0"(_arg1)                                                 \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "r8"                          \
	);                                                                   \
	_ret;                                                                \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                 \
({                                                                           \
	register long _ret  __asm__ ("r3");                                  \
	register long _num  __asm__ ("r0") = (num);                          \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                   \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                   \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                   \
	register long _arg4 __asm__ ("r6") = (long)(arg4);                   \
	register long _arg5 __asm__ ("r7") = (long)(arg5);                   \
	register long _arg6 __asm__ ("r8") = (long)(arg6);                   \
									     \
	__asm__ volatile (                                                   \
		"	sc\n"                                                \
		"	bns+ 1f\n"                                           \
		"	neg  %0, %0\n"                                       \
		"1:\n"                                                       \
		: "=r"(_ret), "+r"(_num), "+r"(_arg2), "+r"(_arg3),          \
		  "+r"(_arg4), "+r"(_arg5), "+r"(_arg6)                      \
		: "0"(_arg1)                                                 \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                \
	);                                                                   \
	_ret;                                                                \
})

#ifndef __powerpc64__
/* FIXME: For 32-bit PowerPC, with newer gcc compilers (e.g. gcc 13.1.0),
 * "omit-frame-pointer" fails with __attribute__((no_stack_protector)) but
 * works with __attribute__((__optimize__("-fno-stack-protector")))
 */
#ifdef __no_stack_protector
#undef __no_stack_protector
#define __no_stack_protector __attribute__((__optimize__("-fno-stack-protector")))
#endif
#endif /* !__powerpc64__ */

/* startup code */
void __attribute__((weak, noreturn, optimize("Os", "omit-frame-pointer"))) __no_stack_protector _start(void)
{
#ifdef __powerpc64__
#if _CALL_ELF == 2
	/* with -mabi=elfv2, save TOC/GOT pointer to r2
	 * r12 is global entry pointer, we use it to compute TOC from r12
	 * https://www.llvm.org/devmtg/2014-04/PDFs/Talks/Euro-LLVM-2014-Weigand.pdf
	 * https://refspecs.linuxfoundation.org/ELF/ppc64/PPC-elf64abi.pdf
	 */
	__asm__ volatile (
		"addis  2, 12, .TOC. - _start@ha\n"
		"addi   2,  2, .TOC. - _start@l\n"
	);
#endif /* _CALL_ELF == 2 */

	__asm__ volatile (
		"mr     3, 1\n"         /* save stack pointer to r3, as arg1 of _start_c */
		"clrrdi 1, 1, 4\n"      /* align the stack to 16 bytes                   */
		"li     0, 0\n"         /* zero the frame pointer                        */
		"stdu   1, -32(1)\n"    /* the initial stack frame                       */
		"bl     _start_c\n"     /* transfer to c runtime                         */
	);
#else
	__asm__ volatile (
		"mr     3, 1\n"         /* save stack pointer to r3, as arg1 of _start_c */
		"clrrwi 1, 1, 4\n"      /* align the stack to 16 bytes                   */
		"li     0, 0\n"         /* zero the frame pointer                        */
		"stwu   1, -16(1)\n"    /* the initial stack frame                       */
		"bl     _start_c\n"     /* transfer to c runtime                         */
	);
#endif
	__builtin_unreachable();
}

#endif /* _NOLIBC_ARCH_POWERPC_H */
