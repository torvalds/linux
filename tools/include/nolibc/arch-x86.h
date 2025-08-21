/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * x86 specific definitions for NOLIBC (both 32- and 64-bit)
 * Copyright (C) 2017-2025 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_X86_H
#define _NOLIBC_ARCH_X86_H

#include "compiler.h"
#include "crt.h"

#if !defined(__x86_64__)

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

/* startup code */
/*
 * i386 System V ABI mandates:
 * 1) last pushed argument must be 16-byte aligned.
 * 2) The deepest stack frame should be set to zero
 *
 */
void __attribute__((weak, noreturn)) __nolibc_entrypoint __no_stack_protector _start(void)
{
	__asm__ volatile (
		"xor  %ebp, %ebp\n"       /* zero the stack frame                                */
		"mov  %esp, %eax\n"       /* save stack pointer to %eax, as arg1 of _start_c     */
		"sub  $12, %esp\n"        /* sub 12 to keep it aligned after the push %eax       */
		"push %eax\n"             /* push arg1 on stack to support plain stack modes too */
		"call _start_c\n"         /* transfer to c runtime                               */
		"hlt\n"                   /* ensure it does not return                           */
	);
	__nolibc_entrypoint_epilogue();
}

#else /* !defined(__x86_64__) */

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
 * Link x86-64 ABI: https://gitlab.com/x86-psABIs/x86-64-ABI/-/wikis/home
 *
 */

#define my_syscall0(num)                                                      \
({                                                                            \
	long _ret;                                                            \
	register long _num  __asm__ ("rax") = (num);                          \
									      \
	__asm__ volatile (                                                    \
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
	register long _num  __asm__ ("rax") = (num);                          \
	register long _arg1 __asm__ ("rdi") = (long)(arg1);                   \
									      \
	__asm__ volatile (                                                    \
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
	register long _num  __asm__ ("rax") = (num);                          \
	register long _arg1 __asm__ ("rdi") = (long)(arg1);                   \
	register long _arg2 __asm__ ("rsi") = (long)(arg2);                   \
									      \
	__asm__ volatile (                                                    \
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
	register long _num  __asm__ ("rax") = (num);                          \
	register long _arg1 __asm__ ("rdi") = (long)(arg1);                   \
	register long _arg2 __asm__ ("rsi") = (long)(arg2);                   \
	register long _arg3 __asm__ ("rdx") = (long)(arg3);                   \
									      \
	__asm__ volatile (                                                    \
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
	register long _num  __asm__ ("rax") = (num);                          \
	register long _arg1 __asm__ ("rdi") = (long)(arg1);                   \
	register long _arg2 __asm__ ("rsi") = (long)(arg2);                   \
	register long _arg3 __asm__ ("rdx") = (long)(arg3);                   \
	register long _arg4 __asm__ ("r10") = (long)(arg4);                   \
									      \
	__asm__ volatile (                                                    \
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
	register long _num  __asm__ ("rax") = (num);                          \
	register long _arg1 __asm__ ("rdi") = (long)(arg1);                   \
	register long _arg2 __asm__ ("rsi") = (long)(arg2);                   \
	register long _arg3 __asm__ ("rdx") = (long)(arg3);                   \
	register long _arg4 __asm__ ("r10") = (long)(arg4);                   \
	register long _arg5 __asm__ ("r8")  = (long)(arg5);                   \
									      \
	__asm__ volatile (                                                    \
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
	register long _num  __asm__ ("rax") = (num);                          \
	register long _arg1 __asm__ ("rdi") = (long)(arg1);                   \
	register long _arg2 __asm__ ("rsi") = (long)(arg2);                   \
	register long _arg3 __asm__ ("rdx") = (long)(arg3);                   \
	register long _arg4 __asm__ ("r10") = (long)(arg4);                   \
	register long _arg5 __asm__ ("r8")  = (long)(arg5);                   \
	register long _arg6 __asm__ ("r9")  = (long)(arg6);                   \
									      \
	__asm__ volatile (                                                    \
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
void __attribute__((weak, noreturn)) __nolibc_entrypoint __no_stack_protector _start(void)
{
	__asm__ volatile (
		"xor  %ebp, %ebp\n"       /* zero the stack frame                            */
		"mov  %rsp, %rdi\n"       /* save stack pointer to %rdi, as arg1 of _start_c */
		"call _start_c\n"         /* transfer to c runtime                           */
		"hlt\n"                   /* ensure it does not return                       */
	);
	__nolibc_entrypoint_epilogue();
}

#define NOLIBC_ARCH_HAS_MEMMOVE
void *memmove(void *dst, const void *src, size_t len);

#define NOLIBC_ARCH_HAS_MEMCPY
void *memcpy(void *dst, const void *src, size_t len);

#define NOLIBC_ARCH_HAS_MEMSET
void *memset(void *dst, int c, size_t len);

__asm__ (
".section .text.nolibc_memmove_memcpy\n"
".weak memmove\n"
".weak memcpy\n"
"memmove:\n"
"memcpy:\n"
	"movq %rdx, %rcx\n\t"
	"movq %rdi, %rax\n\t"
	"movq %rdi, %rdx\n\t"
	"subq %rsi, %rdx\n\t"
	"cmpq %rcx, %rdx\n\t"
	"jb   1f\n\t"
	"rep movsb\n\t"
	"retq\n"
"1:" /* backward copy */
	"leaq -1(%rdi, %rcx, 1), %rdi\n\t"
	"leaq -1(%rsi, %rcx, 1), %rsi\n\t"
	"std\n\t"
	"rep movsb\n\t"
	"cld\n\t"
	"retq\n"

".section .text.nolibc_memset\n"
".weak memset\n"
"memset:\n"
	"xchgl %eax, %esi\n\t"
	"movq  %rdx, %rcx\n\t"
	"pushq %rdi\n\t"
	"rep stosb\n\t"
	"popq  %rax\n\t"
	"retq\n"
);

#endif /* !defined(__x86_64__) */
#endif /* _NOLIBC_ARCH_X86_H */
