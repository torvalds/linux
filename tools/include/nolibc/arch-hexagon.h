/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * hexagon specific definitions for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

#ifndef _NOLIBC_ARCH_HEXAGON_H
#define _NOLIBC_ARCH_HEXAGON_H

#include <linux/unistd.h>

#include "compiler.h"
#include "crt.h"

/*
 * Syscalls for hexagon:
 *   - syscall number is passed in r6
 *   - arguments are in r0, r1, r2, r3, r4, r5
 *   - the system call is performed by calling trap0(#1)
 *   - syscall return value is in r0
 */

#define _NOLIBC_SYSCALL_CLOBBERLIST "memory"

#define __nolibc_syscall0(num)                                                \
({                                                                            \
	register long _num __asm__ ("r6") = (num);                            \
	register long _arg1 __asm__ ("r0");                                   \
									      \
	__asm__ volatile (                                                    \
		"trap0(#1)\n"                                                 \
		: "=r"(_arg1)                                                 \
		: "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg1;                                                                \
})

#define __nolibc_syscall1(num, arg1)                                          \
({                                                                            \
	register long _num __asm__ ("r6") = (num);                            \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
									      \
	__asm__ volatile (                                                    \
		"trap0(#1)\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg1;                                                                \
})

#define __nolibc_syscall2(num, arg1, arg2)                                    \
({                                                                            \
	register long _num __asm__ ("r6") = (num);                            \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
									      \
	__asm__ volatile (                                                    \
		"trap0(#1)\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2),                                                 \
		  "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg1;                                                                \
})

#define __nolibc_syscall3(num, arg1, arg2, arg3)                              \
({                                                                            \
	register long _num __asm__ ("r6") = (num);                            \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
									      \
	__asm__ volatile (                                                    \
		"trap0(#1)\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3),                                     \
		  "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg1;                                                                \
})

#define __nolibc_syscall4(num, arg1, arg2, arg3, arg4)                        \
({                                                                            \
	register long _num __asm__ ("r6") = (num);                            \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r3") = (long)(arg4);                    \
									      \
	__asm__ volatile (                                                    \
		"trap0(#1)\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4),                         \
		  "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg1;                                                                \
})

#define __nolibc_syscall5(num, arg1, arg2, arg3, arg4, arg5)                  \
({                                                                            \
	register long _num __asm__ ("r6") = (num);                            \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r3") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r4") = (long)(arg5);                    \
									      \
	__asm__ volatile (                                                    \
		"trap0(#1)\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5),             \
		  "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg1;                                                                \
})

#define __nolibc_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)            \
({                                                                            \
	register long _num __asm__ ("r6") = (num);                            \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r3") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r4") = (long)(arg5);                    \
	register long _arg6 __asm__ ("r5") = (long)(arg6);                    \
									      \
	__asm__ volatile (                                                    \
		"trap0(#1)\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), "r"(_arg6), \
		  "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg1;                                                                \
})

#ifndef NOLIBC_NO_RUNTIME
/* startup code */
void __attribute__((weak, noreturn))
__nolibc_entrypoint __nolibc_no_stack_protector
_start(void)
{
	__asm__ volatile (
		"r0 = sp\n"                 /* save stack pointer to r0, as arg1 of _start_c */
		"call _start_c\n"           /* transfer to c runtime                        */
	);
	__nolibc_entrypoint_epilogue();
}
#endif /* NOLIBC_NO_RUNTIME */

#define __NOLIBC_PAD_64BIT_SYSCALL_ARGUMENT_PAIR

#endif /* _NOLIBC_ARCH_HEXAGON_H */
