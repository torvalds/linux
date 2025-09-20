/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * MIPS specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_MIPS_H
#define _NOLIBC_ARCH_MIPS_H

#include "compiler.h"
#include "crt.h"

#if !defined(_ABIO32) && !defined(_ABIN32) && !defined(_ABI64)
#error Unsupported MIPS ABI
#endif

/* Syscalls for MIPS ABI O32 :
 *   - WARNING! there's always a delayed slot!
 *   - WARNING again, the syntax is different, registers take a '$' and numbers
 *     do not.
 *   - registers are 32-bit
 *   - stack is 8-byte aligned
 *   - syscall number is passed in v0 (starts at 0xfa0).
 *   - arguments are in a0, a1, a2, a3, then the stack. The caller needs to
 *     leave some room in the stack for the callee to save a0..a3 if needed.
 *   - Many registers are clobbered, in fact only a0..a2 and s0..s8 are
 *     preserved. See: https://www.linux-mips.org/wiki/Syscall as well as
 *     scall32-o32.S in the kernel sources.
 *   - the system call is performed by calling "syscall"
 *   - syscall return comes in v0, and register a3 needs to be checked to know
 *     if an error occurred, in which case errno is in v0.
 *   - the arguments are cast to long and assigned into the target registers
 *     which are then simply passed as registers to the asm code, so that we
 *     don't have to experience issues with register constraints.
 *
 * Syscalls for MIPS ABI N32, same as ABI O32 with the following differences :
 *   - arguments are in a0, a1, a2, a3, t0, t1, t2, t3.
 *     t0..t3 are also known as a4..a7.
 *   - stack is 16-byte aligned
 */

#if defined(_ABIO32)

#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"memory", "cc", "at", "v1", "hi", "lo", \
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9"
#define _NOLIBC_SYSCALL_STACK_RESERVE "addiu $sp, $sp, -32\n"
#define _NOLIBC_SYSCALL_STACK_UNRESERVE "addiu $sp, $sp, 32\n"

#else /* _ABIN32 || _ABI64 */

/* binutils, GCC and clang disagree about register aliases, use numbers instead. */
#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"memory", "cc", "at", "v1", \
	"10", "11", "12", "13", "14", "15", "24", "25"

#define _NOLIBC_SYSCALL_STACK_RESERVE
#define _NOLIBC_SYSCALL_STACK_UNRESERVE

#endif /* _ABIO32 */

#define my_syscall0(num)                                                      \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2)                                      \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	register long _num __asm__ ("v0")  = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3)                          \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4)              \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#if defined(_ABIO32)

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
	register long _arg5 = (long)(arg5);                                   \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"sw %7, 16($sp)\n"                                            \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5)  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	register long _num __asm__ ("v0")  = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
	register long _arg5 = (long)(arg5);                                   \
	register long _arg6 = (long)(arg6);                                   \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"sw %7, 16($sp)\n"                                            \
		"sw %8, 20($sp)\n"                                            \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#else /* _ABIN32 || _ABI64 */

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("$4") = (long)(arg1);                    \
	register long _arg2 __asm__ ("$5") = (long)(arg2);                    \
	register long _arg3 __asm__ ("$6") = (long)(arg3);                    \
	register long _arg4 __asm__ ("$7") = (long)(arg4);                    \
	register long _arg5 __asm__ ("$8") = (long)(arg5);                    \
									      \
	__asm__ volatile (                                                    \
		"syscall\n"                                                   \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5)  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	register long _num __asm__ ("v0")  = (num);                           \
	register long _arg1 __asm__ ("$4") = (long)(arg1);                    \
	register long _arg2 __asm__ ("$5") = (long)(arg2);                    \
	register long _arg3 __asm__ ("$6") = (long)(arg3);                    \
	register long _arg4 __asm__ ("$7") = (long)(arg4);                    \
	register long _arg5 __asm__ ("$8") = (long)(arg5);                    \
	register long _arg6 __asm__ ("$9") = (long)(arg6);                    \
									      \
	__asm__ volatile (                                                    \
		"syscall\n"                                                   \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#endif /* _ABIO32 */

/* startup code, note that it's called __start on MIPS */
void __start(void);
void __attribute__((weak, noreturn)) __nolibc_entrypoint __no_stack_protector __start(void)
{
	__asm__ volatile (
		"move  $a0, $sp\n"       /* save stack pointer to $a0, as arg1 of _start_c */
#if defined(_ABIO32)
		"addiu $sp, $sp, -16\n"  /* the callee expects to save a0..a3 there        */
#endif /* _ABIO32 */
		"lui $t9, %hi(_start_c)\n" /* ABI requires current function address in $t9 */
		"ori $t9, %lo(_start_c)\n"
#if defined(_ABI64)
		"lui  $t0, %highest(_start_c)\n"
		"ori  $t0, %higher(_start_c)\n"
		"dsll $t0, 0x20\n"
		"or   $t9, $t0\n"
#endif /* _ABI64 */
		"jalr $t9\n"             /* transfer to c runtime                          */
	);
	__nolibc_entrypoint_epilogue();
}

#endif /* _NOLIBC_ARCH_MIPS_H */
