/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * i386 specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_I386_H
#define _NOLIBC_ARCH_I386_H

#include "compiler.h"

/* The struct returned by the stat() syscall, 32-bit only, the syscall returns
 * exactly 56 bytes (stops before the unused array).
 */
struct sys_stat_struct {
	unsigned long  st_dev;
	unsigned long  st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;

	unsigned long  st_rdev;
	unsigned long  st_size;
	unsigned long  st_blksize;
	unsigned long  st_blocks;

	unsigned long  st_atime;
	unsigned long  st_atime_nsec;
	unsigned long  st_mtime;
	unsigned long  st_mtime_nsec;

	unsigned long  st_ctime;
	unsigned long  st_ctime_nsec;
	unsigned long  __unused[2];
};

/* Syscalls for i386 :
 *   - mostly similar to x86_64
 *   - registers are 32-bit
 *   - syscall number is passed in eax
 *   - arguments are in ebx, ecx, edx, esi, edi, ebp respectively
 *   - all registers are preserved (except eax of course)
 *   - the system call is performed by calling int $0x80
 *   - syscall return comes in eax
 *   - the arguments are cast to long and assigned into the target registers
 *     which are then simply passed as registers to the asm code, so that we
 *     don't have to experience issues with register constraints.
 *   - the syscall number is always specified last in order to allow to force
 *     some registers before (gcc refuses a %-register at the last position).
 *
 * Also, i386 supports the old_select syscall if newselect is not available
 */
#define __ARCH_WANT_SYS_OLD_SELECT

#define my_syscall0(num)                                                      \
({                                                                            \
	long _ret;                                                            \
	register long _num __asm__ ("eax") = (num);                           \
									      \
	__asm__ volatile (                                                    \
		"int $0x80\n"                                                 \
		: "=a" (_ret)                                                 \
		: "0"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	long _ret;                                                            \
	register long _num __asm__ ("eax") = (num);                           \
	register long _arg1 __asm__ ("ebx") = (long)(arg1);                   \
									      \
	__asm__ volatile (                                                    \
		"int $0x80\n"                                                 \
		: "=a" (_ret)                                                 \
		: "r"(_arg1),                                                 \
		  "0"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	long _ret;                                                            \
	register long _num __asm__ ("eax") = (num);                           \
	register long _arg1 __asm__ ("ebx") = (long)(arg1);                   \
	register long _arg2 __asm__ ("ecx") = (long)(arg2);                   \
									      \
	__asm__ volatile (                                                    \
		"int $0x80\n"                                                 \
		: "=a" (_ret)                                                 \
		: "r"(_arg1), "r"(_arg2),                                     \
		  "0"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	long _ret;                                                            \
	register long _num __asm__ ("eax") = (num);                           \
	register long _arg1 __asm__ ("ebx") = (long)(arg1);                   \
	register long _arg2 __asm__ ("ecx") = (long)(arg2);                   \
	register long _arg3 __asm__ ("edx") = (long)(arg3);                   \
									      \
	__asm__ volatile (                                                    \
		"int $0x80\n"                                                 \
		: "=a" (_ret)                                                 \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3),                         \
		  "0"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	long _ret;                                                            \
	register long _num __asm__ ("eax") = (num);                           \
	register long _arg1 __asm__ ("ebx") = (long)(arg1);                   \
	register long _arg2 __asm__ ("ecx") = (long)(arg2);                   \
	register long _arg3 __asm__ ("edx") = (long)(arg3);                   \
	register long _arg4 __asm__ ("esi") = (long)(arg4);                   \
									      \
	__asm__ volatile (                                                    \
		"int $0x80\n"                                                 \
		: "=a" (_ret)                                                 \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4),             \
		  "0"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	long _ret;                                                            \
	register long _num __asm__ ("eax") = (num);                           \
	register long _arg1 __asm__ ("ebx") = (long)(arg1);                   \
	register long _arg2 __asm__ ("ecx") = (long)(arg2);                   \
	register long _arg3 __asm__ ("edx") = (long)(arg3);                   \
	register long _arg4 __asm__ ("esi") = (long)(arg4);                   \
	register long _arg5 __asm__ ("edi") = (long)(arg5);                   \
									      \
	__asm__ volatile (                                                    \
		"int $0x80\n"                                                 \
		: "=a" (_ret)                                                 \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "0"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)	\
({								\
	long _eax  = (long)(num);				\
	long _arg6 = (long)(arg6); /* Always in memory */	\
	__asm__ volatile (					\
		"pushl	%[_arg6]\n\t"				\
		"pushl	%%ebp\n\t"				\
		"movl	4(%%esp),%%ebp\n\t"			\
		"int	$0x80\n\t"				\
		"popl	%%ebp\n\t"				\
		"addl	$4,%%esp\n\t"				\
		: "+a"(_eax)		/* %eax */		\
		: "b"(arg1),		/* %ebx */		\
		  "c"(arg2),		/* %ecx */		\
		  "d"(arg3),		/* %edx */		\
		  "S"(arg4),		/* %esi */		\
		  "D"(arg5),		/* %edi */		\
		  [_arg6]"m"(_arg6)	/* memory */		\
		: "memory", "cc"				\
	);							\
	_eax;							\
})

char **environ __attribute__((weak));
const unsigned long *_auxv __attribute__((weak));

/* startup code */
/*
 * i386 System V ABI mandates:
 * 1) last pushed argument must be 16-byte aligned.
 * 2) The deepest stack frame should be set to zero
 *
 */
void __attribute__((weak, noreturn, optimize("Os", "omit-frame-pointer"))) __no_stack_protector _start(void)
{
	__asm__ volatile (
#ifdef _NOLIBC_STACKPROTECTOR
		"call __stack_chk_init\n"   /* initialize stack protector                    */
#endif
		"pop %eax\n"                /* argc   (first arg, %eax)                      */
		"mov %esp, %ebx\n"          /* argv[] (second arg, %ebx)                     */
		"lea 4(%ebx,%eax,4),%ecx\n" /* then a NULL then envp (third arg, %ecx)       */
		"mov %ecx, environ\n"       /* save environ                                  */
		"xor %ebp, %ebp\n"          /* zero the stack frame                          */
		"mov %ecx, %edx\n"          /* search for auxv (follows NULL after last env) */
		"0:\n"
		"add $4, %edx\n"            /* search for auxv using edx, it follows the     */
		"cmp -4(%edx), %ebp\n"      /* ... NULL after last env (ebp is zero here)    */
		"jnz 0b\n"
		"mov %edx, _auxv\n"         /* save it into _auxv                            */
		"and $-16, %esp\n"          /* x86 ABI : esp must be 16-byte aligned before  */
		"sub $4, %esp\n"            /* the call instruction (args are aligned)       */
		"push %ecx\n"               /* push all registers on the stack so that we    */
		"push %ebx\n"               /* support both regparm and plain stack modes    */
		"push %eax\n"
		"call main\n"               /* main() returns the status code in %eax        */
		"mov %eax, %ebx\n"          /* retrieve exit code (32-bit int)               */
		"movl $1, %eax\n"           /* NR_exit == 1                                  */
		"int $0x80\n"               /* exit now                                      */
		"hlt\n"                     /* ensure it does not                            */
	);
	__builtin_unreachable();
}

#endif /* _NOLIBC_ARCH_I386_H */
