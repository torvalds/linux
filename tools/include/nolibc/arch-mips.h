/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * MIPS specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_MIPS_H
#define _NOLIBC_ARCH_MIPS_H

#include "compiler.h"

/* The struct returned by the stat() syscall. 88 bytes are returned by the
 * syscall.
 */
struct sys_stat_struct {
	unsigned int  st_dev;
	long          st_pad1[3];
	unsigned long st_ino;
	unsigned int  st_mode;
	unsigned int  st_nlink;
	unsigned int  st_uid;
	unsigned int  st_gid;
	unsigned int  st_rdev;
	long          st_pad2[2];
	long          st_size;
	long          st_pad3;

	long          st_atime;
	long          st_atime_nsec;
	long          st_mtime;
	long          st_mtime_nsec;

	long          st_ctime;
	long          st_ctime_nsec;
	long          st_blksize;
	long          st_blocks;
	long          st_pad4[14];
};

/* Syscalls for MIPS ABI O32 :
 *   - WARNING! there's always a delayed slot!
 *   - WARNING again, the syntax is different, registers take a '$' and numbers
 *     do not.
 *   - registers are 32-bit
 *   - stack is 8-byte aligned
 *   - syscall number is passed in v0 (starts at 0xfa0).
 *   - arguments are in a0, a1, a2, a3, then the stack. The caller needs to
 *     leave some room in the stack for the callee to save a0..a3 if needed.
 *   - Many registers are clobbered, in fact only a0..a2 and s0..s8 are
 *     preserved. See: https://www.linux-mips.org/wiki/Syscall as well as
 *     scall32-o32.S in the kernel sources.
 *   - the system call is performed by calling "syscall"
 *   - syscall return comes in v0, and register a3 needs to be checked to know
 *     if an error occurred, in which case errno is in v0.
 *   - the arguments are cast to long and assigned into the target registers
 *     which are then simply passed as registers to the asm code, so that we
 *     don't have to experience issues with register constraints.
 */

#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"memory", "cc", "at", "v1", "hi", "lo", \
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9"

#define my_syscall0(num)                                                      \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		"addiu $sp, $sp, -32\n"                                       \
		"syscall\n"                                                   \
		"addiu $sp, $sp, 32\n"                                        \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		"addiu $sp, $sp, -32\n"                                       \
		"syscall\n"                                                   \
		"addiu $sp, $sp, 32\n"                                        \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		"addiu $sp, $sp, -32\n"                                       \
		"syscall\n"                                                   \
		"addiu $sp, $sp, 32\n"                                        \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2)                                      \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	register long _num __asm__ ("v0")  = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3");                                   \
									      \
	__asm__ volatile (                                                    \
		"addiu $sp, $sp, -32\n"                                       \
		"syscall\n"                                                   \
		"addiu $sp, $sp, 32\n"                                        \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3)                          \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
									      \
	__asm__ volatile (                                                    \
		"addiu $sp, $sp, -32\n"                                       \
		"syscall\n"                                                   \
		"addiu $sp, $sp, 32\n"                                        \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4)              \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num __asm__ ("v0") = (num);                            \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
	register long _arg5 = (long)(arg5);                                   \
									      \
	__asm__ volatile (                                                    \
		"addiu $sp, $sp, -32\n"                                       \
		"sw %7, 16($sp)\n"                                            \
		"syscall\n  "                                                 \
		"addiu $sp, $sp, 32\n"                                        \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5)  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

char **environ __attribute__((weak));
const unsigned long *_auxv __attribute__((weak));

/* startup code, note that it's called __start on MIPS */
void __attribute__((weak,noreturn,optimize("omit-frame-pointer"))) __no_stack_protector __start(void)
{
	__asm__ volatile (
		/*".set nomips16\n"*/
		".set push\n"
		".set    noreorder\n"
		".option pic0\n"
#ifdef _NOLIBC_STACKPROTECTOR
		"jal __stack_chk_init\n" /* initialize stack protector                         */
		"nop\n"                  /* delayed slot                                       */
#endif
		/*".ent __start\n"*/
		/*"__start:\n"*/
		"lw $a0,($sp)\n"        /* argc was in the stack                               */
		"addiu  $a1, $sp, 4\n"  /* argv = sp + 4                                       */
		"sll $a2, $a0, 2\n"     /* a2 = argc * 4                                       */
		"add   $a2, $a2, $a1\n" /* envp = argv + 4*argc ...                            */
		"addiu $a2, $a2, 4\n"   /*        ... + 4                                      */
		"lui $a3, %hi(environ)\n"     /* load environ into a3 (hi)                     */
		"addiu $a3, %lo(environ)\n"   /* load environ into a3 (lo)                     */
		"sw $a2,($a3)\n"              /* store envp(a2) into environ                   */

		"move $t0, $a2\n"             /* iterate t0 over envp, look for NULL           */
		"0:"                          /* do {                                          */
		"lw $a3, ($t0)\n"             /*   a3=*(t0);                                   */
		"bne $a3, $0, 0b\n"           /* } while (a3);                                 */
		"addiu $t0, $t0, 4\n"         /* delayed slot: t0+=4;                          */
		"lui $a3, %hi(_auxv)\n"       /* load _auxv into a3 (hi)                       */
		"addiu $a3, %lo(_auxv)\n"     /* load _auxv into a3 (lo)                       */
		"sw $t0, ($a3)\n"             /* store t0 into _auxv                           */

		"li $t0, -8\n"
		"and $sp, $sp, $t0\n"   /* sp must be 8-byte aligned                           */
		"addiu $sp,$sp,-16\n"   /* the callee expects to save a0..a3 there!            */
		"jal main\n"            /* main() returns the status code, we'll exit with it. */
		"nop\n"                 /* delayed slot                                        */
		"move $a0, $v0\n"       /* retrieve 32-bit exit code from v0                   */
		"li $v0, 4001\n"        /* NR_exit == 4001                                     */
		"syscall\n"
		/*".end __start\n"*/
		".set pop\n"
	);
	__builtin_unreachable();
}

#endif /* _NOLIBC_ARCH_MIPS_H */
