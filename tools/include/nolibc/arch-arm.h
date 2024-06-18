/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * ARM specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_ARM_H
#define _NOLIBC_ARCH_ARM_H

#include "compiler.h"
#include "crt.h"

/* Syscalls for ARM in ARM or Thumb modes :
 *   - registers are 32-bit
 *   - stack is 8-byte aligned
 *     ( http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.faqs/ka4127.html)
 *   - syscall number is passed in r7
 *   - arguments are in r0, r1, r2, r3, r4, r5
 *   - the system call is performed by calling svc #0
 *   - syscall return comes in r0.
 *   - only lr is clobbered.
 *   - the arguments are cast to long and assigned into the target registers
 *     which are then simply passed as registers to the asm code, so that we
 *     don't have to experience issues with register constraints.
 *   - the syscall number is always specified last in order to allow to force
 *     some registers before (gcc refuses a %-register at the last position).
 *   - in thumb mode without -fomit-frame-pointer, r7 is also used to store the
 *     frame pointer, and we cannot directly assign it as a register variable,
 *     nor can we clobber it. Instead we assign the r6 register and swap it
 *     with r7 before calling svc, and r6 is marked as clobbered.
 *     We're just using any regular register which we assign to r7 after saving
 *     it.
 *
 * Also, ARM supports the old_select syscall if newselect is not available
 */
#define __ARCH_WANT_SYS_OLD_SELECT

#if (defined(__THUMBEB__) || defined(__THUMBEL__)) && \
    !defined(NOLIBC_OMIT_FRAME_POINTER)
/* swap r6,r7 needed in Thumb mode since we can't use nor clobber r7 */
#define _NOLIBC_SYSCALL_REG         "r6"
#define _NOLIBC_THUMB_SET_R7        "eor r7, r6\neor r6, r7\neor r7, r6\n"
#define _NOLIBC_THUMB_RESTORE_R7    "mov r7, r6\n"

#else  /* we're in ARM mode */
/* in Arm mode we can directly use r7 */
#define _NOLIBC_SYSCALL_REG         "r7"
#define _NOLIBC_THUMB_SET_R7        ""
#define _NOLIBC_THUMB_RESTORE_R7    ""

#endif /* end THUMB */

#define my_syscall0(num)                                                      \
({                                                                            \
	register long _num  __asm__(_NOLIBC_SYSCALL_REG) = (num);             \
	register long _arg1 __asm__ ("r0");                                   \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_THUMB_SET_R7                                          \
		"svc #0\n"                                                    \
		_NOLIBC_THUMB_RESTORE_R7                                      \
		: "=r"(_arg1), "=r"(_num)                                     \
		: "r"(_arg1),                                                 \
		  "r"(_num)                                                   \
		: "memory", "cc", "lr"                                        \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	register long _num  __asm__(_NOLIBC_SYSCALL_REG) = (num);             \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_THUMB_SET_R7                                          \
		"svc #0\n"                                                    \
		_NOLIBC_THUMB_RESTORE_R7                                      \
		: "=r"(_arg1), "=r" (_num)                                    \
		: "r"(_arg1),                                                 \
		  "r"(_num)                                                   \
		: "memory", "cc", "lr"                                        \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num  __asm__(_NOLIBC_SYSCALL_REG) = (num);             \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_THUMB_SET_R7                                          \
		"svc #0\n"                                                    \
		_NOLIBC_THUMB_RESTORE_R7                                      \
		: "=r"(_arg1), "=r" (_num)                                    \
		: "r"(_arg1), "r"(_arg2),                                     \
		  "r"(_num)                                                   \
		: "memory", "cc", "lr"                                        \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	register long _num  __asm__(_NOLIBC_SYSCALL_REG) = (num);             \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_THUMB_SET_R7                                          \
		"svc #0\n"                                                    \
		_NOLIBC_THUMB_RESTORE_R7                                      \
		: "=r"(_arg1), "=r" (_num)                                    \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3),                         \
		  "r"(_num)                                                   \
		: "memory", "cc", "lr"                                        \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	register long _num  __asm__(_NOLIBC_SYSCALL_REG) = (num);             \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r3") = (long)(arg4);                    \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_THUMB_SET_R7                                          \
		"svc #0\n"                                                    \
		_NOLIBC_THUMB_RESTORE_R7                                      \
		: "=r"(_arg1), "=r" (_num)                                    \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4),             \
		  "r"(_num)                                                   \
		: "memory", "cc", "lr"                                        \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num  __asm__(_NOLIBC_SYSCALL_REG) = (num);             \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r3") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r4") = (long)(arg5);                    \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_THUMB_SET_R7                                          \
		"svc #0\n"                                                    \
		_NOLIBC_THUMB_RESTORE_R7                                      \
		: "=r"(_arg1), "=r" (_num)                                    \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_num)                                                   \
		: "memory", "cc", "lr"                                        \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	register long _num  __asm__(_NOLIBC_SYSCALL_REG) = (num);             \
	register long _arg1 __asm__ ("r0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("r1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("r2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("r3") = (long)(arg4);                    \
	register long _arg5 __asm__ ("r4") = (long)(arg5);                    \
	register long _arg6 __asm__ ("r5") = (long)(arg6);                    \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_THUMB_SET_R7                                          \
		"svc #0\n"                                                    \
		_NOLIBC_THUMB_RESTORE_R7                                      \
		: "=r"(_arg1), "=r" (_num)                                    \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6), "r"(_num)                                       \
		: "memory", "cc", "lr"                                        \
	);                                                                    \
	_arg1;                                                                \
})

/* startup code */
void __attribute__((weak, noreturn, optimize("Os", "omit-frame-pointer"))) __no_stack_protector _start(void)
{
	__asm__ volatile (
		"mov %r0, sp\n"         /* save stack pointer to %r0, as arg1 of _start_c */
		"and ip, %r0, #-8\n"    /* sp must be 8-byte aligned in the callee        */
		"mov sp, ip\n"
		"bl  _start_c\n"        /* transfer to c runtime                          */
	);
	__builtin_unreachable();
}

#endif /* _NOLIBC_ARCH_ARM_H */
