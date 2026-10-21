/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * parisc/hppa (32-bit) specific definitions for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

#ifndef _NOLIBC_ARCH_PARISC_H
#define _NOLIBC_ARCH_PARISC_H

#include <linux/unistd.h>

#include "compiler.h"
#include "crt.h"

/* Syscalls for parisc :
 *   - syscall number is passed in r20
 *   - arguments are in r26 to r21
 *   - the system call is performed by calling "ble 0x100(%sr2, %r0)",
 *     the instruction after that is in the delay slot and executed before
 *     the jump to 0x100 actually happens, use it to load the syscall number
 *   - syscall return comes in r28
 *   - the arguments are cast to long and assigned into the target
 *     registers which are then simply passed as registers to the asm code,
 *     so that we don't have to experience issues with register constraints.
 */

#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"memory", "%r1", "%r2", "%r4", "%r20", "%r29", "%r31"

#define __nolibc_syscall0(num)                                                \
({                                                                            \
	register long _ret __asm__ ("r28");                                   \
									      \
	__asm__ volatile (                                                    \
		"ble 0x100(%%sr2, %%r0)\n\t"                                  \
		"copy %1, %%r20\n\t"                                          \
		: "=r"(_ret)                                                  \
		: "r"(num)                                                    \
		: _NOLIBC_SYSCALL_CLOBBERLIST,                                \
		  "%r21", "%r22", "%r23", "%r24", "%r25", "%r26"              \
	);                                                                    \
	_ret;                                                                 \
})

#define __nolibc_syscall1(num, arg1)                                          \
({                                                                            \
	register long _ret __asm__ ("r28");                                   \
	register long _arg1 __asm__ ("r26") = (long)(arg1);		      \
									      \
	__asm__ volatile (                                                    \
		"ble 0x100(%%sr2, %%r0)\n\t"                                  \
		"copy %2, %%r20\n\t"                                          \
		: "=r"(_ret),                                                 \
		  "+r"(_arg1)                                                 \
		: "r"(num)                                                    \
		: _NOLIBC_SYSCALL_CLOBBERLIST,                                \
		  "%r21", "%r22", "%r23", "%r24", "%r25"                      \
	);                                                                    \
	_ret;                                                                 \
})

#define __nolibc_syscall2(num, arg1, arg2)                                    \
({                                                                            \
	register long _ret __asm__ ("r28");                                   \
	register long _arg1 __asm__ ("r26") = (long)(arg1);		      \
	register long _arg2 __asm__ ("r25") = (long)(arg2);		      \
									      \
	__asm__ volatile (                                                    \
		"ble 0x100(%%sr2, %%r0)\n\t"                                  \
		"copy %3, %%r20\n\t"                                          \
		: "=r"(_ret),                                                 \
		  "+r"(_arg1), "+r"(_arg2)                                    \
		: "r"(num)                                                    \
		: _NOLIBC_SYSCALL_CLOBBERLIST,                                \
		  "%r21", "%r22", "%r23", "%r24"                              \
	);                                                                    \
	_ret;                                                                 \
})

#define __nolibc_syscall3(num, arg1, arg2, arg3)                              \
({                                                                            \
	register long _ret __asm__ ("r28");                                   \
	register long _arg1 __asm__ ("r26") = (long)(arg1);		      \
	register long _arg2 __asm__ ("r25") = (long)(arg2);		      \
	register long _arg3 __asm__ ("r24") = (long)(arg3);		      \
									      \
	__asm__ volatile (                                                    \
		"ble 0x100(%%sr2, %%r0)\n\t"                                  \
		"copy %4, %%r20\n\t"                                          \
		: "=r"(_ret),                                                 \
		  "+r"(_arg1), "+r"(_arg2), "+r"(_arg3)                       \
		: "r"(num)                                                    \
		: _NOLIBC_SYSCALL_CLOBBERLIST,                                \
		  "%r21", "%r22", "%r23"                                      \
	);                                                                    \
	_ret;                                                                 \
})

#define __nolibc_syscall4(num, arg1, arg2, arg3, arg4)                        \
({                                                                            \
	register long _ret __asm__ ("r28");                                   \
	register long _arg1 __asm__ ("r26") = (long)(arg1);		      \
	register long _arg2 __asm__ ("r25") = (long)(arg2);		      \
	register long _arg3 __asm__ ("r24") = (long)(arg3);		      \
	register long _arg4 __asm__ ("r23") = (long)(arg4);		      \
									      \
	__asm__ volatile (                                                    \
		"ble 0x100(%%sr2, %%r0)\n\t"                                  \
		"copy %5, %%r20\n\t"                                          \
		: "=r"(_ret),                                                 \
		  "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4)          \
		: "r"(num)                                                    \
		: _NOLIBC_SYSCALL_CLOBBERLIST,                                \
		  "%r21", "%r22"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define __nolibc_syscall5(num, arg1, arg2, arg3, arg4, arg5)                  \
({                                                                            \
	register long _ret __asm__ ("r28");                                   \
	register long _arg1 __asm__ ("r26") = (long)(arg1);		      \
	register long _arg2 __asm__ ("r25") = (long)(arg2);		      \
	register long _arg3 __asm__ ("r24") = (long)(arg3);		      \
	register long _arg4 __asm__ ("r23") = (long)(arg4);		      \
	register long _arg5 __asm__ ("r22") = (long)(arg5);		      \
									      \
	__asm__ volatile (                                                    \
		"ble 0x100(%%sr2, %%r0)\n\t"                                  \
		"copy %6, %%r20\n\t"                                          \
		: "=r"(_ret),                                                 \
		  "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),         \
		  "+r"(_arg5)                                                 \
		: "r"(num)                                                    \
		: _NOLIBC_SYSCALL_CLOBBERLIST,                                \
		  "%r21"                                                      \
	);                                                                    \
	_ret;                                                                 \
})

#define __nolibc_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)            \
({                                                                            \
	register long _ret __asm__ ("r28");                                   \
	register long _arg1 __asm__ ("r26") = (long)(arg1);		      \
	register long _arg2 __asm__ ("r25") = (long)(arg2);		      \
	register long _arg3 __asm__ ("r24") = (long)(arg3);		      \
	register long _arg4 __asm__ ("r23") = (long)(arg4);		      \
	register long _arg5 __asm__ ("r22") = (long)(arg5);		      \
	register long _arg6 __asm__ ("r21") = (long)(arg6);		      \
									      \
	__asm__ volatile (                                                    \
		"ble 0x100(%%sr2, %%r0)\n\t"                                  \
		"copy %7, %%r20\n\t"                                          \
		: "=r"(_ret),                                                 \
		  "+r"(_arg1), "+r"(_arg2), "+r"(_arg3), "+r"(_arg4),         \
		  "+r"(_arg5), "+r"(_arg6)                                    \
		: "r"(num)                                                    \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_ret;                                                                 \
})

#ifndef NOLIBC_NO_RUNTIME
/* startup code */
void __attribute__((weak, noreturn)) __nolibc_entrypoint __nolibc_no_stack_protector _start(void)
{
	__asm__ volatile (
#ifdef __LP64__
		".import __gp\n"               /* Set up the dp register */
		"ldil L%__gp, %dp\n"
		"ldo R%__gp(%dp), %dp\n"
#else
		".import $global$\n"           /* Set up the dp register */
		"ldil L%$global$, %dp\n"
		"ldo R%$global$(%dp), %dp\n"
#endif

		"b _start_c\n"                 /* Call _start_c, the load below is executed first */

		"ldo -4(%r24), %r26\n"         /* The sp register is special on parisc.
						* r24 points to argv. Subtract 4 to get &argc.
						* Pass that as first argument to _start_c.
						*/
	);
	__nolibc_entrypoint_epilogue();
}
#endif /* NOLIBC_NO_RUNTIME */

#ifdef __LP64__
/*
 * This override is normally only meant for 32-bit architectures.
 * But parisc 64-bit has __NR_ftruncate64, so this path is taken.
 * Add an unused second argument to satisfy the calling convention.
 */
static __attribute__((unused))
int _sys_ftruncate64(int fd, uint64_t length, uint64_t unused)
{
	(void)unused;

	return __nolibc_syscall2(__NR_ftruncate64, fd, length);
}
#define _sys_ftruncate64 _sys_ftruncate64
#endif

#endif /* _NOLIBC_ARCH_PARISC_H */
