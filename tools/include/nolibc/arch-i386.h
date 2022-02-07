/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * i386 specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_I386_H
#define _NOLIBC_ARCH_I386_H

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
	register long _num asm("eax") = (num);                                \
	                                                                      \
	asm volatile (                                                        \
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
	register long _num asm("eax") = (num);                                \
	register long _arg1 asm("ebx") = (long)(arg1);                        \
	                                                                      \
	asm volatile (                                                        \
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
	register long _num asm("eax") = (num);                                \
	register long _arg1 asm("ebx") = (long)(arg1);                        \
	register long _arg2 asm("ecx") = (long)(arg2);                        \
	                                                                      \
	asm volatile (                                                        \
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
	register long _num asm("eax") = (num);                                \
	register long _arg1 asm("ebx") = (long)(arg1);                        \
	register long _arg2 asm("ecx") = (long)(arg2);                        \
	register long _arg3 asm("edx") = (long)(arg3);                        \
	                                                                      \
	asm volatile (                                                        \
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
	register long _num asm("eax") = (num);                                \
	register long _arg1 asm("ebx") = (long)(arg1);                        \
	register long _arg2 asm("ecx") = (long)(arg2);                        \
	register long _arg3 asm("edx") = (long)(arg3);                        \
	register long _arg4 asm("esi") = (long)(arg4);                        \
	                                                                      \
	asm volatile (                                                        \
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
	register long _num asm("eax") = (num);                                \
	register long _arg1 asm("ebx") = (long)(arg1);                        \
	register long _arg2 asm("ecx") = (long)(arg2);                        \
	register long _arg3 asm("edx") = (long)(arg3);                        \
	register long _arg4 asm("esi") = (long)(arg4);                        \
	register long _arg5 asm("edi") = (long)(arg5);                        \
	                                                                      \
	asm volatile (                                                        \
		"int $0x80\n"                                                 \
		: "=a" (_ret)                                                 \
		: "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "0"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_ret;                                                                 \
})

/* startup code */
/*
 * i386 System V ABI mandates:
 * 1) last pushed argument must be 16-byte aligned.
 * 2) The deepest stack frame should be set to zero
 *
 */
asm(".section .text\n"
    ".weak _start\n"
    ".global _start\n"
    "_start:\n"
    "pop %eax\n"                // argc   (first arg, %eax)
    "mov %esp, %ebx\n"          // argv[] (second arg, %ebx)
    "lea 4(%ebx,%eax,4),%ecx\n" // then a NULL then envp (third arg, %ecx)
    "xor %ebp, %ebp\n"          // zero the stack frame
    "and $-16, %esp\n"          // x86 ABI : esp must be 16-byte aligned before
    "sub $4, %esp\n"            // the call instruction (args are aligned)
    "push %ecx\n"               // push all registers on the stack so that we
    "push %ebx\n"               // support both regparm and plain stack modes
    "push %eax\n"
    "call main\n"               // main() returns the status code in %eax
    "mov %eax, %ebx\n"          // retrieve exit code (32-bit int)
    "movl $1, %eax\n"           // NR_exit == 1
    "int $0x80\n"               // exit now
    "hlt\n"                     // ensure it does not
    "");

#endif // _NOLIBC_ARCH_I386_H
