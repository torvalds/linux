/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * m68k specific definitions for NOLIBC
 * Copyright (C) 2025 Daniel Palmer<daniel@thingy.jp>
 *
 * Roughly based on one or more of the other arch files.
 *
 */

#ifndef _NOLIBC_ARCH_M68K_H
#define _NOLIBC_ARCH_M68K_H

#include "compiler.h"
#include "crt.h"

#define _NOLIBC_SYSCALL_CLOBBERLIST "memory"

#define my_syscall0(num)                                                      \
({                                                                            \
	register long _num __asm__ ("d0") = (num);                            \
									      \
	__asm__ volatile (                                                    \
		"trap #0\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	register long _num __asm__ ("d0") = (num);                            \
	register long _arg1 __asm__ ("d1") = (long)(arg1);                    \
									      \
	__asm__ volatile (                                                    \
		"trap #0\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num __asm__ ("d0") = (num);                            \
	register long _arg1 __asm__ ("d1") = (long)(arg1);                    \
	register long _arg2 __asm__ ("d2") = (long)(arg2);                    \
									      \
	__asm__ volatile (                                                    \
		"trap #0\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1), "r"(_arg2)                                      \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	register long _num __asm__ ("d0")  = (num);                           \
	register long _arg1 __asm__ ("d1") = (long)(arg1);                    \
	register long _arg2 __asm__ ("d2") = (long)(arg2);                    \
	register long _arg3 __asm__ ("d3") = (long)(arg3);                    \
									      \
	__asm__ volatile (                                                    \
		"trap #0\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3)                          \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	register long _num __asm__ ("d0") = (num);                            \
	register long _arg1 __asm__ ("d1") = (long)(arg1);                    \
	register long _arg2 __asm__ ("d2") = (long)(arg2);                    \
	register long _arg3 __asm__ ("d3") = (long)(arg3);                    \
	register long _arg4 __asm__ ("d4") = (long)(arg4);                    \
									      \
	__asm__ volatile (                                                    \
		"trap #0\n"                                                   \
		: "+r" (_num)                                                 \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4)              \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num __asm__ ("d0") = (num);                            \
	register long _arg1 __asm__ ("d1") = (long)(arg1);                    \
	register long _arg2 __asm__ ("d2") = (long)(arg2);                    \
	register long _arg3 __asm__ ("d3") = (long)(arg3);                    \
	register long _arg4 __asm__ ("d4") = (long)(arg4);                    \
	register long _arg5 __asm__ ("d5") = (long)(arg5);                    \
									      \
	__asm__ volatile (                                                    \
		"trap #0\n"                                                   \
		: "+r" (_num)                                                 \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5)  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	register long _num __asm__ ("d0")  = (num);                           \
	register long _arg1 __asm__ ("d1") = (long)(arg1);                    \
	register long _arg2 __asm__ ("d2") = (long)(arg2);                    \
	register long _arg3 __asm__ ("d3") = (long)(arg3);                    \
	register long _arg4 __asm__ ("d4") = (long)(arg4);                    \
	register long _arg5 __asm__ ("d5") = (long)(arg5);                    \
	register long _arg6 __asm__ ("a0") = (long)(arg6);                    \
									      \
	__asm__ volatile (                                                    \
		"trap #0\n"                                                   \
		: "+r" (_num)                                                 \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

void _start(void);
void __attribute__((weak, noreturn)) __nolibc_entrypoint __no_stack_protector _start(void)
{
	__asm__ volatile (
		"movel %sp, %sp@-\n"
		"jsr _start_c\n"
	);
	__nolibc_entrypoint_epilogue();
}

#endif /* _NOLIBC_ARCH_M68K_H */
