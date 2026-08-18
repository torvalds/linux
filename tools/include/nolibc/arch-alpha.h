/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Alpha specific definitions for NOLIBC
 * Copyright (C) 2025 Thomas Weißschuh <linux@weissschuh.net>
 */

#ifndef _NOLIBC_ARCH_ALPHA_H
#define _NOLIBC_ARCH_ALPHA_H

#include "compiler.h"
#include "crt.h"

/*
 * Syscalls for Alpha:
 *   - registers are 64-bit
 *   - syscall number is passed in $0/v0
 *   - the system call is performed by calling callsys
 *   - syscall return comes in $0/v0, error flag in $19/a3
 *   - arguments are passed in $16/a0 to $21/a5
 *   - GCC does not support symbol register names
 */

#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8", \
	"$22", "$23", "$24", "$25", "$27", "$28", "memory", "cc"

#define __nolibc_syscall0(num)                                                \
({                                                                            \
	register long _num __asm__ ("$0") = (num);                            \
	register long _err __asm__ ("$19");                                   \
                                                                              \
	__asm__ volatile (                                                    \
		"callsys"                                                     \
		: "+r"(_num), "=r"(_err)                                      \
		:                                                             \
		: _NOLIBC_SYSCALL_CLOBBERLIST,                                \
		  "$16", "$17", "$18", "$20", "$21"                           \
	);                                                                    \
	_err ? -_num : _num;                                                  \
})

#define __nolibc_syscall1(num, arg1)                                          \
({                                                                            \
	register long _num __asm__ ("$0") = (num);                            \
	register long _err __asm__ ("$19");                                   \
	register long _arg1 __asm__ ("$16") = (long)(arg1);                   \
                                                                              \
	__asm__ volatile (                                                    \
		"callsys"                                                     \
		: "+r"(_num), "=r"(_err)                                      \
		: "r"(_arg1)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "$17", "$18", "$20", "$21"     \
	);                                                                    \
	_err ? -_num : _num;                                                  \
})

#define __nolibc_syscall2(num, arg1, arg2)                                    \
({                                                                            \
	register long _num __asm__ ("$0") = (num);                            \
	register long _err __asm__ ("$19");                                   \
	register long _arg1 __asm__ ("$16") = (long)(arg1);                   \
	register long _arg2 __asm__ ("$17") = (long)(arg2);                   \
                                                                              \
	__asm__ volatile (                                                    \
		"callsys"                                                     \
		: "+r"(_num), "=r"(_err)                                      \
		: "r"(_arg1), "r"(_arg2)                                      \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "$18", "$20", "$21"            \
	);                                                                    \
	_err ? -_num : _num;                                                  \
})

#define __nolibc_syscall3(num, arg1, arg2, arg3)                              \
({                                                                            \
	register long _num __asm__ ("$0") = (num);                            \
	register long _err __asm__ ("$19");                                   \
	register long _arg1 __asm__ ("$16") = (long)(arg1);                   \
	register long _arg2 __asm__ ("$17") = (long)(arg2);                   \
	register long _arg3 __asm__ ("$18") = (long)(arg3);                   \
                                                                              \
	__asm__ volatile (                                                    \
		"callsys"                                                     \
		: "+r"(_num), "=r"(_err)                                      \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3)                          \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "$20", "$21"                   \
	);                                                                    \
	_err ? -_num : _num;                                                  \
})

#define __nolibc_syscall4(num, arg1, arg2, arg3, arg4)                        \
({                                                                            \
	register long _num __asm__ ("$0") = (num);                            \
	register long _arg1 __asm__ ("$16") = (long)(arg1);                   \
	register long _arg2 __asm__ ("$17") = (long)(arg2);                   \
	register long _arg3 __asm__ ("$18") = (long)(arg3);                   \
	register long _arg4 __asm__ ("$19") = (long)(arg4);                   \
                                                                              \
	__asm__ volatile (                                                    \
		"callsys"                                                     \
		: "+r"(_num), "+r"(_arg4)                                     \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3)                          \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "$20", "$21"                   \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall5(num, arg1, arg2, arg3, arg4, arg5)                  \
({                                                                            \
	register long _num __asm__ ("$0") = (num);                            \
	register long _arg1 __asm__ ("$16") = (long)(arg1);                   \
	register long _arg2 __asm__ ("$17") = (long)(arg2);                   \
	register long _arg3 __asm__ ("$18") = (long)(arg3);                   \
	register long _arg4 __asm__ ("$19") = (long)(arg4);                   \
	register long _arg5 __asm__ ("$20") = (long)(arg5);                   \
                                                                              \
	__asm__ volatile (                                                    \
		"callsys"                                                     \
		: "+r"(_num), "+r"(_arg4)                                     \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg5)              \
		: _NOLIBC_SYSCALL_CLOBBERLIST, "$21"                          \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)            \
({                                                                            \
	register long _num __asm__ ("$0") = (num);                            \
	register long _arg1 __asm__ ("$16") = (long)(arg1);                   \
	register long _arg2 __asm__ ("$17") = (long)(arg2);                   \
	register long _arg3 __asm__ ("$18") = (long)(arg3);                   \
	register long _arg4 __asm__ ("$19") = (long)(arg4);                   \
	register long _arg5 __asm__ ("$20") = (long)(arg5);                   \
	register long _arg6 __asm__ ("$21") = (long)(arg6);                   \
                                                                              \
	__asm__ volatile (                                                    \
		"callsys"                                                     \
		: "+r"(_num), "+r"(_arg4)                                     \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg5),             \
		  "r"(_arg6)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

/* startup code */
void __attribute__((weak, noreturn)) __nolibc_entrypoint __nolibc_no_stack_protector
_start(void)
{
	__asm__ volatile (
		"br $gp, 0f\n"               /* setup $gp, so that 'lda' works                */
		"0: ldgp $gp, 0($gp)\n"
		"lda $27, _start_c\n"        /* setup current function address for _start_c   */
		"mov $sp, $16\n"             /* save argc pointer to $16, as arg1 of _start_c */
		"br  _start_c\n"             /* transfer to c runtime                         */
	);
	__nolibc_entrypoint_epilogue();
}

#endif /* _NOLIBC_ARCH_ALPHA_H */
