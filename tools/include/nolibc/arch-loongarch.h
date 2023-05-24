/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * LoongArch specific definitions for NOLIBC
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef _NOLIBC_ARCH_LOONGARCH_H
#define _NOLIBC_ARCH_LOONGARCH_H

#include "compiler.h"

/* Syscalls for LoongArch :
 *   - stack is 16-byte aligned
 *   - syscall number is passed in a7
 *   - arguments are in a0, a1, a2, a3, a4, a5
 *   - the system call is performed by calling "syscall 0"
 *   - syscall return comes in a0
 *   - the arguments are cast to long and assigned into the target
 *     registers which are then simply passed as registers to the asm code,
 *     so that we don't have to experience issues with register constraints.
 *
 * On LoongArch, select() is not implemented so we have to use pselect6().
 */
#define __ARCH_WANT_SYS_PSELECT6

#define my_syscall0(num)                                                      \
({                                                                            \
	register long _num  __asm__ ("a7") = (num);                           \
	register long _arg1 __asm__ ("a0");                                   \
									      \
	__asm__  volatile (                                                   \
		"syscall 0\n"                                                 \
		: "=r"(_arg1)                                                 \
		: "r"(_num)                                                   \
		: "memory", "$t0", "$t1", "$t2", "$t3",                       \
		  "$t4", "$t5", "$t6", "$t7", "$t8"                           \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall1(num, arg1)                                                \
({                                                                            \
	register long _num  __asm__ ("a7") = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);		      \
									      \
	__asm__  volatile (                                                   \
		"syscall 0\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_num)                                                   \
		: "memory", "$t0", "$t1", "$t2", "$t3",                       \
		  "$t4", "$t5", "$t6", "$t7", "$t8"                           \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall2(num, arg1, arg2)                                          \
({                                                                            \
	register long _num  __asm__ ("a7") = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
									      \
	__asm__  volatile (                                                   \
		"syscall 0\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2),                                                 \
		  "r"(_num)                                                   \
		: "memory", "$t0", "$t1", "$t2", "$t3",                       \
		  "$t4", "$t5", "$t6", "$t7", "$t8"                           \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall3(num, arg1, arg2, arg3)                                    \
({                                                                            \
	register long _num  __asm__ ("a7") = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
									      \
	__asm__  volatile (                                                   \
		"syscall 0\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3),                                     \
		  "r"(_num)                                                   \
		: "memory", "$t0", "$t1", "$t2", "$t3",                       \
		  "$t4", "$t5", "$t6", "$t7", "$t8"                           \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall4(num, arg1, arg2, arg3, arg4)                              \
({                                                                            \
	register long _num  __asm__ ("a7") = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
									      \
	__asm__  volatile (                                                   \
		"syscall 0\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4),                         \
		  "r"(_num)                                                   \
		: "memory", "$t0", "$t1", "$t2", "$t3",                       \
		  "$t4", "$t5", "$t6", "$t7", "$t8"                           \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall5(num, arg1, arg2, arg3, arg4, arg5)                        \
({                                                                            \
	register long _num  __asm__ ("a7") = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
	register long _arg5 __asm__ ("a4") = (long)(arg5);                    \
									      \
	__asm__  volatile (                                                   \
		"syscall 0\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5),             \
		  "r"(_num)                                                   \
		: "memory", "$t0", "$t1", "$t2", "$t3",                       \
		  "$t4", "$t5", "$t6", "$t7", "$t8"                           \
	);                                                                    \
	_arg1;                                                                \
})

#define my_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)                  \
({                                                                            \
	register long _num  __asm__ ("a7") = (num);                           \
	register long _arg1 __asm__ ("a0") = (long)(arg1);                    \
	register long _arg2 __asm__ ("a1") = (long)(arg2);                    \
	register long _arg3 __asm__ ("a2") = (long)(arg3);                    \
	register long _arg4 __asm__ ("a3") = (long)(arg4);                    \
	register long _arg5 __asm__ ("a4") = (long)(arg5);                    \
	register long _arg6 __asm__ ("a5") = (long)(arg6);                    \
									      \
	__asm__  volatile (                                                   \
		"syscall 0\n"                                                 \
		: "+r"(_arg1)                                                 \
		: "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), "r"(_arg6), \
		  "r"(_num)                                                   \
		: "memory", "$t0", "$t1", "$t2", "$t3",                       \
		  "$t4", "$t5", "$t6", "$t7", "$t8"                           \
	);                                                                    \
	_arg1;                                                                \
})

char **environ __attribute__((weak));
const unsigned long *_auxv __attribute__((weak));

#if __loongarch_grlen == 32
#define LONGLOG      "2"
#define SZREG        "4"
#define REG_L        "ld.w"
#define LONG_S       "st.w"
#define LONG_ADD     "add.w"
#define LONG_ADDI    "addi.w"
#define LONG_SLL     "slli.w"
#define LONG_BSTRINS "bstrins.w"
#else /* __loongarch_grlen == 64 */
#define LONGLOG      "3"
#define SZREG        "8"
#define REG_L        "ld.d"
#define LONG_S       "st.d"
#define LONG_ADD     "add.d"
#define LONG_ADDI    "addi.d"
#define LONG_SLL     "slli.d"
#define LONG_BSTRINS "bstrins.d"
#endif

/* startup code */
void __attribute__((weak,noreturn,optimize("omit-frame-pointer"))) __no_stack_protector _start(void)
{
	__asm__ volatile (
#ifdef _NOLIBC_STACKPROTECTOR
		"bl __stack_chk_init\n"               /* initialize stack protector                          */
#endif
		REG_L        " $a0, $sp, 0\n"         /* argc (a0) was in the stack                          */
		LONG_ADDI    " $a1, $sp, "SZREG"\n"   /* argv (a1) = sp + SZREG                              */
		LONG_SLL     " $a2, $a0, "LONGLOG"\n" /* envp (a2) = SZREG*argc ...                          */
		LONG_ADDI    " $a2, $a2, "SZREG"\n"   /*             + SZREG (skip null)                     */
		LONG_ADD     " $a2, $a2, $a1\n"       /*             + argv                                  */

		"move          $a3, $a2\n"            /* iterate a3 over envp to find auxv (after NULL)      */
		"0:\n"                                /* do {                                                */
		REG_L        " $a4, $a3, 0\n"         /*   a4 = *a3;                                         */
		LONG_ADDI    " $a3, $a3, "SZREG"\n"   /*   a3 += sizeof(void*);                              */
		"bne           $a4, $zero, 0b\n"      /* } while (a4);                                       */
		"la.pcrel      $a4, _auxv\n"          /* a4 = &_auxv                                         */
		LONG_S       " $a3, $a4, 0\n"         /* store a3 into _auxv                                 */

		"la.pcrel      $a3, environ\n"        /* a3 = &environ                                       */
		LONG_S       " $a2, $a3, 0\n"         /* store envp(a2) into environ                         */
		LONG_BSTRINS " $sp, $zero, 3, 0\n"    /* sp must be 16-byte aligned                          */
		"bl            main\n"                /* main() returns the status code, we'll exit with it. */
		"li.w          $a7, 93\n"             /* NR_exit == 93                                       */
		"syscall       0\n"
	);
	__builtin_unreachable();
}

#endif /* _NOLIBC_ARCH_LOONGARCH_H */
