/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * RISCV (32 and 64) specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_RISCV_H
#define _NOLIBC_ARCH_RISCV_H

/* O_* macros for fcntl/open are architecture-specific */
#define O_RDONLY            0
#define O_WRONLY            1
#define O_RDWR              2
#define O_CREAT         0x100
#define O_EXCL          0x200
#define O_NOCTTY        0x400
#define O_TRUNC        0x1000
#define O_APPEND       0x2000
#define O_NONBLOCK     0x4000
#define O_DIRECTORY  0x200000

struct sys_stat_struct {
	unsigned long	st_dev;		/* Device.  */
	unsigned long	st_ino;		/* File serial number.  */
	unsigned int	st_mode;	/* File mode.  */
	unsigned int	st_nlink;	/* Link count.  */
	unsigned int	st_uid;		/* User ID of the file's owner.  */
	unsigned int	st_gid;		/* Group ID of the file's group. */
	unsigned long	st_rdev;	/* Device number, if device.  */
	unsigned long	__pad1;
	long		st_size;	/* Size of file, in bytes.  */
	int		st_blksize;	/* Optimal block size for I/O.  */
	int		__pad2;
	long		st_blocks;	/* Number 512-byte blocks allocated. */
	long		st_atime;	/* Time of last access.  */
	unsigned long	st_atime_nsec;
	long		st_mtime;	/* Time of last modification.  */
	unsigned long	st_mtime_nsec;
	long		st_ctime;	/* Time of last status change.  */
	unsigned long	st_ctime_nsec;
	unsigned int	__unused4;
	unsigned int	__unused5;
};

#if   __riscv_xlen == 64
#define PTRLOG "3"
#define SZREG  "8"
#elif __riscv_xlen == 32
#define PTRLOG "2"
#define SZREG  "4"
#endif

/* Syscalls for RISCV :
 *   - stack is 16-byte aligned
 *   - syscall number is passed in a7
 *   - arguments are in a0, a1, a2, a3, a4, a5
 *   - the system call is performed by calling ecall
 *   - syscall return comes in a0
 *   - the arguments are cast to long and assigned into the target
 *     registers which are then simply passed as registers to the asm code,
 *     so that we don't have to experience issues with register constraints.
 *
 * On riscv, select() is not implemented so we have to use pselect6().
 */
#define __ARCH_WANT_SYS_PSELECT6

#define my_syscall0(num)                                                      \
({                                                                            \
	register long _num  asm("a7") = (num);                                \
	register long _arg1 asm("a0");                                        \
									      \
	asm volatile (                                                        \
		"ecall\n\t"                                                   \
		: "=r"(_arg1)                                                 \
		: "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	register long _num  asm("a7") = (num);                                \
	register long _arg1 asm("a0") = (long)(arg1);		              \
									      \
	asm volatile (                                                        \
		"ecall\n"                                                     \
		: "+r"(_arg1)                                                 \
		: "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num  asm("a7") = (num);                                \
	register long _arg1 asm("a0") = (long)(arg1);                         \
	register long _arg2 asm("a1") = (long)(arg2);                         \
									      \
	asm volatile (                                                        \
		"ecall\n"                                                     \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2),                                                 \
		  "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	register long _num  asm("a7") = (num);                                \
	register long _arg1 asm("a0") = (long)(arg1);                         \
	register long _arg2 asm("a1") = (long)(arg2);                         \
	register long _arg3 asm("a2") = (long)(arg3);                         \
									      \
	asm volatile (                                                        \
		"ecall\n\t"                                                   \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3),                                     \
		  "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	register long _num  asm("a7") = (num);                                \
	register long _arg1 asm("a0") = (long)(arg1);                         \
	register long _arg2 asm("a1") = (long)(arg2);                         \
	register long _arg3 asm("a2") = (long)(arg3);                         \
	register long _arg4 asm("a3") = (long)(arg4);                         \
									      \
	asm volatile (                                                        \
		"ecall\n"                                                     \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4),                         \
		  "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num  asm("a7") = (num);                                \
	register long _arg1 asm("a0") = (long)(arg1);                         \
	register long _arg2 asm("a1") = (long)(arg2);                         \
	register long _arg3 asm("a2") = (long)(arg3);                         \
	register long _arg4 asm("a3") = (long)(arg4);                         \
	register long _arg5 asm("a4") = (long)(arg5);                         \
									      \
	asm volatile (                                                        \
		"ecall\n"                                                     \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5),             \
		  "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	register long _num  asm("a7") = (num);                                \
	register long _arg1 asm("a0") = (long)(arg1);                         \
	register long _arg2 asm("a1") = (long)(arg2);                         \
	register long _arg3 asm("a2") = (long)(arg3);                         \
	register long _arg4 asm("a3") = (long)(arg4);                         \
	register long _arg5 asm("a4") = (long)(arg5);                         \
	register long _arg6 asm("a5") = (long)(arg6);                         \
									      \
	asm volatile (                                                        \
		"ecall\n"                                                     \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), "r"(_arg6), \
		  "r"(_num)                                                   \
		: "memory", "cc"                                              \
	);                                                                    \
	_arg1;                                                                \
})

/* startup code */
asm(".section .text\n"
    ".global _start\n"
    "_start:\n"
    ".option push\n"
    ".option norelax\n"
    "lla   gp, __global_pointer$\n"
    ".option pop\n"
    "ld    a0, 0(sp)\n"          // argc (a0) was in the stack
    "add   a1, sp, "SZREG"\n"    // argv (a1) = sp
    "slli  a2, a0, "PTRLOG"\n"   // envp (a2) = SZREG*argc ...
    "add   a2, a2, "SZREG"\n"    //             + SZREG (skip null)
    "add   a2,a2,a1\n"           //             + argv
    "andi  sp,a1,-16\n"          // sp must be 16-byte aligned
    "call  main\n"               // main() returns the status code, we'll exit with it.
    "li a7, 93\n"                // NR_exit == 93
    "ecall\n"
    "");

#endif // _NOLIBC_ARCH_RISCV_H
