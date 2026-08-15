/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * OpenRISC specific definitions for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

#ifndef _NOLIBC_ARCH_OPENRISC_H
#define _NOLIBC_ARCH_OPENRISC_H

#include "compiler.h"
#include "crt.h"

/*
 * Syscalls for OpenRISC:
 *   - syscall number is passed in r11
 *   - arguments are in r3, r4, r5, r6, r7, r8
 *   - the system call is performed by calling l.sys 1
 *   - syscall return value is in r11
 */

#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"r12", "r13", "r15", "r17", "r19", "r21", "r23", "r25", "r27", "r29", "r31", "memory"

#define __nolibc_syscall0(num)                                                \
({                                                                            \
	register long _num __asm__ ("r11") = (num);                           \
									      \
	__asm__ volatile (                                                    \
		"l.sys 1\n"                                                   \
		: "+r"(_num)                                                  \
		:                                                             \
		: "r3", "r4", "r5", "r6", "r7", "r8",                         \
		  _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#define __nolibc_syscall1(num, arg1)                                          \
({                                                                            \
	register long _num __asm__ ("r11") = (num);                           \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                    \
									      \
	__asm__ volatile (                                                    \
		"l.sys 1\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1)                                                  \
		: "r4", "r5", "r6", "r7", "r8", _NOLIBC_SYSCALL_CLOBBERLIST   \
	);                                                                    \
	_num;                                                                 \
})

#define __nolibc_syscall2(num, arg1, arg2)                                    \
({                                                                            \
	register long _num __asm__ ("r11") = (num);                           \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                    \
									      \
	__asm__ volatile (                                                    \
		"l.sys 1\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1), "r"(_arg2)                                      \
		: "r5", "r6", "r7", "r8", _NOLIBC_SYSCALL_CLOBBERLIST         \
	);                                                                    \
	_num;                                                                 \
})

#define __nolibc_syscall3(num, arg1, arg2, arg3)                              \
({                                                                            \
	register long _num __asm__ ("r11") = (num);                           \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                    \
									      \
	__asm__ volatile (                                                    \
		"l.sys 1\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3)                          \
		: "r6", "r7", "r8", _NOLIBC_SYSCALL_CLOBBERLIST               \
	);                                                                    \
	_num;                                                                 \
})

#define __nolibc_syscall4(num, arg1, arg2, arg3, arg4)                        \
({                                                                            \
	register long _num __asm__ ("r11") = (num);                           \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r6") = (long)(arg4);                    \
									      \
	__asm__ volatile (                                                    \
		"l.sys 1\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4)              \
		: "r7", "r8", _NOLIBC_SYSCALL_CLOBBERLIST                     \
	);                                                                    \
	_num;                                                                 \
})

#define __nolibc_syscall5(num, arg1, arg2, arg3, arg4, arg5)                  \
({                                                                            \
	register long _num __asm__ ("r11") = (num);                           \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r6") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r7") = (long)(arg5);                    \
									      \
	__asm__ volatile (                                                    \
		"l.sys 1\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5)  \
		: "r8", _NOLIBC_SYSCALL_CLOBBERLIST                           \
	);                                                                    \
	_num;                                                                 \
})

#define __nolibc_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)            \
({                                                                            \
	register long _num __asm__ ("r11") = (num);                           \
	register long _arg1 __asm__ ("r3") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r4") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r5") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r6") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r7") = (long)(arg5);                    \
	register long _arg6 __asm__ ("r8") = (long)(arg6);                    \
									      \
	__asm__ volatile (                                                    \
		"l.sys 1\n"                                                   \
		: "+r"(_num)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_num;                                                                 \
})

#ifndef NOLIBC_NO_RUNTIME
/* startup code */
void _start_wrapper(void);
void __attribute__((weak,noreturn))
__nolibc_entrypoint __nolibc_no_stack_protector
_start_wrapper(void)
{
	__asm__ volatile (
		".global _start\n"           /* The C function will have a prologue,         */
		".type _start, @function\n"  /* corrupting "sp/r1"                           */
		".weak _start\n"
		"_start:\n"

		"l.jal _start_c\n"           /* transfer to c runtime                        */
		"l.or r3,r1,r1\n"            /* save stack pointer to r3, as arg1 of _start_c */

		".size _start, .-_start\n"
	);
	__nolibc_entrypoint_epilogue();
}
#endif /* NOLIBC_NO_RUNTIME */

#endif /* _NOLIBC_ARCH_OPENRISC_H */
