/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * x86_64 specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_X86_64_H
#define _NOLIBC_ARCH_X86_64_H

/* O_* macros for fcntl/open are architecture-specific */
#define O_RDONLY            0
#define O_WRONLY            1
#define O_RDWR              2
#define O_CREAT          0x40
#define O_EXCL           0x80
#define O_NOCTTY        0x100
#define O_TRUNC         0x200
#define O_APPEND        0x400
#define O_NONBLOCK      0x800
#define O_DIRECTORY   0x10000

/* The struct returned by the stat() syscall, equivalent to stat64(). The
 * syscall returns 116 bytes and stops in the middle of __unused.
 */
struct sys_stat_struct {
	unsigned long st_dev;
	unsigned long st_ino;
	unsigned long st_nlink;
	unsigned int  st_mode;
	unsigned int  st_uid;

	unsigned int  st_gid;
	unsigned int  __pad0;
	unsigned long st_rdev;
	long          st_size;
	long          st_blksize;

	long          st_blocks;
	unsigned long st_atime;
	unsigned long st_atime_nsec;
	unsigned long st_mtime;

	unsigned long st_mtime_nsec;
	unsigned long st_ctime;
	unsigned long st_ctime_nsec;
	long          __unused[3];
};

/* Syscalls for x86_64 :
 *   - registers are 64-bit
 *   - syscall number is passed in rax
 *   - arguments are in rdi, rsi, rdx, r10, r8, r9 respectively
 *   - the system call is performed by calling the syscall instruction
 *   - syscall return comes in rax
 *   - rcx and r11 are clobbered, others are preserved.
 *   - the arguments are cast to long and assigned into the target registers
 *     which are then simply passed as registers to the asm code, so that we
 *     don't have to experience issues with register constraints.
 *   - the syscall number is always specified last in order to allow to force
 *     some registers before (gcc refuses a %-register at the last position).
 *   - see also x86-64 ABI section A.2 AMD64 Linux Kernel Conventions, A.2.1
 *     Calling Conventions.
 *
 * Link x86-64 ABI: https://gitlab.com/x86-psABIs/x86-64-ABI/-/wikis/x86-64-psABI
 *
 */

#define my_syscall0(num)                                                      \
({                                                                            \
	long _ret;                                                            \
	register long _num  asm("rax") = (num);                               \
	                                                                      \
	asm volatile (                                                        \
		"syscall\n"                                                   \
		: "=a"(_ret)                                                  \
		: "0"(_num)                                                   \
		: "rcx", "r11", "memory", "cc"                                \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	long _ret;                                                            \
	register long _num  asm("rax") = (num);                               \
	register long _arg1 asm("rdi") = (long)(arg1);                        \
	                                                                      \
	asm volatile (                                                        \
		"syscall\n"                                                   \
		: "=a"(_ret)                                                  \
		: "r"(_arg1),                                                 \
		  "0"(_num)                                                   \
		: "rcx", "r11", "memory", "cc"                                \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	long _ret;                                                            \
	register long _num  asm("rax") = (num);                               \
	register long _arg1 asm("rdi") = (long)(arg1);                        \
	register long _arg2 asm("rsi") = (long)(arg2);                        \
	                                                                      \
	asm volatile (                                                        \
		"syscall\n"                                                   \
		: "=a"(_ret)                                                  \
		: "r"(_arg1), "r"(_arg2),                                     \
		  "0"(_num)                                                   \
		: "rcx", "r11", "memory", "cc"                                \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	long _ret;                                                            \
	register long _num  asm("rax") = (num);                               \
	register long _arg1 asm("rdi") = (long)(arg1);                        \
	register long _arg2 asm("rsi") = (long)(arg2);                        \
	register long _arg3 asm("rdx") = (long)(arg3);                        \
	                                                                      \
	asm volatile (                                                        \
		"syscall\n"                                                   \
		: "=a"(_ret)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3),                         \
		  "0"(_num)                                                   \
		: "rcx", "r11", "memory", "cc"                                \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	long _ret;                                                            \
	register long _num  asm("rax") = (num);                               \
	register long _arg1 asm("rdi") = (long)(arg1);                        \
	register long _arg2 asm("rsi") = (long)(arg2);                        \
	register long _arg3 asm("rdx") = (long)(arg3);                        \
	register long _arg4 asm("r10") = (long)(arg4);                        \
	                                                                      \
	asm volatile (                                                        \
		"syscall\n"                                                   \
		: "=a"(_ret)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4),             \
		  "0"(_num)                                                   \
		: "rcx", "r11", "memory", "cc"                                \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	long _ret;                                                            \
	register long _num  asm("rax") = (num);                               \
	register long _arg1 asm("rdi") = (long)(arg1);                        \
	register long _arg2 asm("rsi") = (long)(arg2);                        \
	register long _arg3 asm("rdx") = (long)(arg3);                        \
	register long _arg4 asm("r10") = (long)(arg4);                        \
	register long _arg5 asm("r8")  = (long)(arg5);                        \
	                                                                      \
	asm volatile (                                                        \
		"syscall\n"                                                   \
		: "=a"(_ret)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "0"(_num)                                                   \
		: "rcx", "r11", "memory", "cc"                                \
	);                                                                    \
	_ret;                                                                 \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	long _ret;                                                            \
	register long _num  asm("rax") = (num);                               \
	register long _arg1 asm("rdi") = (long)(arg1);                        \
	register long _arg2 asm("rsi") = (long)(arg2);                        \
	register long _arg3 asm("rdx") = (long)(arg3);                        \
	register long _arg4 asm("r10") = (long)(arg4);                        \
	register long _arg5 asm("r8")  = (long)(arg5);                        \
	register long _arg6 asm("r9")  = (long)(arg6);                        \
	                                                                      \
	asm volatile (                                                        \
		"syscall\n"                                                   \
		: "=a"(_ret)                                                  \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6), "0"(_num)                                       \
		: "rcx", "r11", "memory", "cc"                                \
	);                                                                    \
	_ret;                                                                 \
})

/* startup code */
/*
 * x86-64 System V ABI mandates:
 * 1) %rsp must be 16-byte aligned right before the function call.
 * 2) The deepest stack frame should be zero (the %rbp).
 *
 */
asm(".section .text\n"
    ".global _start\n"
    "_start:\n"
    "pop %rdi\n"                // argc   (first arg, %rdi)
    "mov %rsp, %rsi\n"          // argv[] (second arg, %rsi)
    "lea 8(%rsi,%rdi,8),%rdx\n" // then a NULL then envp (third arg, %rdx)
    "xor %ebp, %ebp\n"          // zero the stack frame
    "and $-16, %rsp\n"          // x86 ABI : esp must be 16-byte aligned before call
    "call main\n"               // main() returns the status code, we'll exit with it.
    "mov %eax, %edi\n"          // retrieve exit code (32 bit)
    "mov $60, %eax\n"           // NR_exit == 60
    "syscall\n"                 // really exit
    "hlt\n"                     // ensure it does not return
    "");

#endif // _NOLIBC_ARCH_X86_64_H
