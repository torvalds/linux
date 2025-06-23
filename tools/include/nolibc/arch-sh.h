/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * SuperH specific definitions for NOLIBC
 * Copyright (C) 2025 Thomas Weißschuh <linux@weissschuh.net>
 */

#ifndef _NOLIBC_ARCH_SH_H
#define _NOLIBC_ARCH_SH_H

#include "compiler.h"
#include "crt.h"

/*
 * Syscalls for SuperH:
 *   - registers are 32bit wide
 *   - syscall number is passed in r3
 *   - arguments are in r4, r5, r6, r7, r0, r1, r2
 *   - the system call is performed by calling trapa #31
 *   - syscall return value is in r0
 */

#define my_syscall0(num)                                                      \
({                                                                            \
	register long _num __asm__ ("r3") = (num);                            \
	register long _ret __asm__ ("r0");                                    \
									      \
	__asm__ volatile (                                                    \
		"trapa #31"                                                   \
		: "=r"(_ret)                                                  \
		: "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	register long _num __asm__ ("r3") = (num);                            \
	register long _ret __asm__ ("r0");                                    \
	register long _arg1 __asm__ ("r4") = (long)(arg1);                    \
									      \
	__asm__ volatile (                                                    \
		"trapa #31"                                                   \
		: "=r"(_ret)                                                  \
		: "r"(_num), "r"(_arg1)                                       \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num __asm__ ("r3") = (num);                            \
	register long _ret __asm__ ("r0");                                    \
	register long _arg1 __asm__ ("r4") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r5") = (long)(arg2);                    \
									      \
	__asm__ volatile (                                                    \
		"trapa #31"                                                   \
		: "=r"(_ret)                                                  \
		: "r"(_num), "r"(_arg1), "r"(_arg2)                           \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	register long _num __asm__ ("r3") = (num);                            \
	register long _ret __asm__ ("r0");                                    \
	register long _arg1 __asm__ ("r4") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r5") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r6") = (long)(arg3);                    \
									      \
	__asm__ volatile (                                                    \
		"trapa #31"                                                   \
		: "=r"(_ret)                                                  \
		: "r"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3)               \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	register long _num __asm__ ("r3") = (num);                            \
	register long _ret __asm__ ("r0");                                    \
	register long _arg1 __asm__ ("r4") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r5") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r6") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r7") = (long)(arg4);                    \
									      \
	__asm__ volatile (                                                    \
		"trapa #31"                                                   \
		: "=r"(_ret)                                                  \
		: "r"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4)   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num __asm__ ("r3") = (num);                            \
	register long _ret __asm__ ("r0");                                    \
	register long _arg1 __asm__ ("r4") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r5") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r6") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r7") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r0") = (long)(arg5);                    \
									      \
	__asm__ volatile (                                                    \
		"trapa #31"                                                   \
		: "=r"(_ret)                                                  \
		: "r"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4),  \
		  "r"(_arg5)                                                  \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	register long _num __asm__ ("r3") = (num);                            \
	register long _ret __asm__ ("r0");                                    \
	register long _arg1 __asm__ ("r4") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r5") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r6") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r7") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r0") = (long)(arg5);                    \
	register long _arg6 __asm__ ("r1") = (long)(arg6);                    \
									      \
	__asm__ volatile (                                                    \
		"trapa #31"                                                   \
		: "=r"(_ret)                                                  \
		: "r"(_num), "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4),  \
		  "r"(_arg5), "r"(_arg6)                                      \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

/* startup code */
void _start_wrapper(void);
void __attribute__((weak,noreturn)) __nolibc_entrypoint __no_stack_protector _start_wrapper(void)
{
	__asm__ volatile (
		".global _start\n"           /* The C function will have a prologue,         */
		".type _start, @function\n"  /* corrupting "sp"                              */
		".weak _start\n"
		"_start:\n"

		"mov sp, r4\n"               /* save argc pointer to r4, as arg1 of _start_c */
		"bsr _start_c\n"             /* transfer to c runtime                        */
		"nop\n"                      /* delay slot                                   */

		".size _start, .-_start\n"
	);
	__nolibc_entrypoint_epilogue();
}

#endif /* _NOLIBC_ARCH_SH_H */
