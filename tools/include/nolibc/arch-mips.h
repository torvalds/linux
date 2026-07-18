/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * MIPS specific definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ARCH_MIPS_H
#define _NOLIBC_ARCH_MIPS_H

#include <linux/unistd.h>

#include "compiler.h"
#include "crt.h"
#include "std.h"

#if !defined(_ABIO32) && !defined(_ABIN32) && !defined(_ABI64)
#error Unsupported MIPS ABI
#endif

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
 *
 * Syscalls for MIPS ABI N32, same as ABI O32 with the following differences :
 *   - arguments are in a0, a1, a2, a3, t0, t1, t2, t3.
 *     t0..t3 are also known as a4..a7.
 *   - stack is 16-byte aligned
 */

#if !defined(__mips_isa_rev) || __mips_isa_rev < 6
#define _NOLIBC_SYSCALL_CLOBBER_HI_LO "hi", "lo"
#else
#define _NOLIBC_SYSCALL_CLOBBER_HI_LO "$0"
#endif

#if defined(_ABIO32)

#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"memory", "cc", "at", "v1", \
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", \
	_NOLIBC_SYSCALL_CLOBBER_HI_LO

#define _NOLIBC_SYSCALL_STACK_RESERVE "addiu $sp, $sp, -32\n"
#define _NOLIBC_SYSCALL_STACK_UNRESERVE "addiu $sp, $sp, 32\n"

#define _NOLIBC_SYSCALL_REG register long

#else /* _ABIN32 || _ABI64 */

/* binutils, GCC and clang disagree about register aliases, use numbers instead. */
#define _NOLIBC_SYSCALL_CLOBBERLIST \
	"memory", "cc", "at", "v1", \
	"10", "11", "12", "13", "14", "15", "24", "25", \
	_NOLIBC_SYSCALL_CLOBBER_HI_LO

#define _NOLIBC_SYSCALL_STACK_RESERVE
#define _NOLIBC_SYSCALL_STACK_UNRESERVE

#define _NOLIBC_SYSCALL_REG register long long

#endif /* _ABIO32 */

#define __nolibc_syscall0(num)                                                \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("a3");                             \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "r"(_num)                                                   \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall1(num, arg1)                                          \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("a0") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("a3");                             \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall2(num, arg1, arg2)                                    \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("a0") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg2 __asm__ ("a1") = __nolibc_arg_to_reg(arg2); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("a3");                             \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2)                                      \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall3(num, arg1, arg2, arg3)                              \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("a0") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg2 __asm__ ("a1") = __nolibc_arg_to_reg(arg2); \
	_NOLIBC_SYSCALL_REG _arg3 __asm__ ("a2") = __nolibc_arg_to_reg(arg3); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("a3");                             \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r"(_num), "=r"(_arg4)                                     \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3)                          \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall4(num, arg1, arg2, arg3, arg4)                        \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("a0") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg2 __asm__ ("a1") = __nolibc_arg_to_reg(arg2); \
	_NOLIBC_SYSCALL_REG _arg3 __asm__ ("a2") = __nolibc_arg_to_reg(arg3); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("a3") = __nolibc_arg_to_reg(arg4); \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4)              \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#if defined(_ABIO32)

#define __nolibc_syscall5(num, arg1, arg2, arg3, arg4, arg5)                  \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("a0") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg2 __asm__ ("a1") = __nolibc_arg_to_reg(arg2); \
	_NOLIBC_SYSCALL_REG _arg3 __asm__ ("a2") = __nolibc_arg_to_reg(arg3); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("a3") = __nolibc_arg_to_reg(arg4); \
	_NOLIBC_SYSCALL_REG _arg5 = __nolibc_arg_to_reg(arg5);                \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"sw %7, 16($sp)\n"                                            \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5)  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)            \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("a0") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg2 __asm__ ("a1") = __nolibc_arg_to_reg(arg2); \
	_NOLIBC_SYSCALL_REG _arg3 __asm__ ("a2") = __nolibc_arg_to_reg(arg3); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("a3") = __nolibc_arg_to_reg(arg4); \
	_NOLIBC_SYSCALL_REG _arg5 = __nolibc_arg_to_reg(arg5);                \
	_NOLIBC_SYSCALL_REG _arg6 = __nolibc_arg_to_reg(arg6);                \
									      \
	__asm__ volatile (                                                    \
		_NOLIBC_SYSCALL_STACK_RESERVE                                 \
		"sw %7, 16($sp)\n"                                            \
		"sw %8, 20($sp)\n"                                            \
		"syscall\n"                                                   \
		_NOLIBC_SYSCALL_STACK_UNRESERVE                               \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#else /* _ABIN32 || _ABI64 */

#define __nolibc_syscall5(num, arg1, arg2, arg3, arg4, arg5)                  \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("$4") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg2 __asm__ ("$5") = __nolibc_arg_to_reg(arg2); \
	_NOLIBC_SYSCALL_REG _arg3 __asm__ ("$6") = __nolibc_arg_to_reg(arg3); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("$7") = __nolibc_arg_to_reg(arg4); \
	_NOLIBC_SYSCALL_REG _arg5 __asm__ ("$8") = __nolibc_arg_to_reg(arg5); \
									      \
	__asm__ volatile (                                                    \
		"syscall\n"                                                   \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5)  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#define __nolibc_syscall6(num, arg1, arg2, arg3, arg4, arg5, arg6)            \
({                                                                            \
	_NOLIBC_SYSCALL_REG _num __asm__ ("v0")  = (num);                     \
	_NOLIBC_SYSCALL_REG _arg1 __asm__ ("$4") = __nolibc_arg_to_reg(arg1); \
	_NOLIBC_SYSCALL_REG _arg2 __asm__ ("$5") = __nolibc_arg_to_reg(arg2); \
	_NOLIBC_SYSCALL_REG _arg3 __asm__ ("$6") = __nolibc_arg_to_reg(arg3); \
	_NOLIBC_SYSCALL_REG _arg4 __asm__ ("$7") = __nolibc_arg_to_reg(arg4); \
	_NOLIBC_SYSCALL_REG _arg5 __asm__ ("$8") = __nolibc_arg_to_reg(arg5); \
	_NOLIBC_SYSCALL_REG _arg6 __asm__ ("$9") = __nolibc_arg_to_reg(arg6); \
									      \
	__asm__ volatile (                                                    \
		"syscall\n"                                                   \
		: "=r" (_num), "=r"(_arg4)                                    \
		: "0"(_num),                                                  \
		  "r"(_arg1), "r"(_arg2), "r"(_arg3), "r"(_arg4), "r"(_arg5), \
		  "r"(_arg6)                                                  \
		: _NOLIBC_SYSCALL_CLOBBERLIST                                 \
	);                                                                    \
	_arg4 ? -_num : _num;                                                 \
})

#endif /* _ABIO32 */

#ifndef NOLIBC_NO_RUNTIME
/* startup code, note that it's called __start on MIPS */
void __start(void);
void __attribute__((weak, noreturn)) __nolibc_entrypoint __nolibc_no_stack_protector __start(void)
{
	__asm__ volatile (
		"move  $a0, $sp\n"       /* save stack pointer to $a0, as arg1 of _start_c */
#if defined(_ABIO32)
		"addiu $sp, $sp, -16\n"  /* the callee expects to save a0..a3 there        */
#endif /* _ABIO32 */
		"lui $t9, %hi(_start_c)\n" /* ABI requires current function address in $t9 */
		"ori $t9, %lo(_start_c)\n"
#if defined(_ABI64)
		"lui  $t0, %highest(_start_c)\n"
		"ori  $t0, %higher(_start_c)\n"
		"dsll $t0, 0x20\n"
		"or   $t9, $t0\n"
#endif /* _ABI64 */
		"jalr $t9\n"             /* transfer to c runtime                          */
	);
	__nolibc_entrypoint_epilogue();
}
#endif /* NOLIBC_NO_RUNTIME */

#if defined(_ABIO32)
static __attribute__((unused))
int _sys_ftruncate64(int fd, uint32_t length0, uint32_t length1)
{
	return __nolibc_syscall4(__NR_ftruncate64, fd, 0, length0, length1);
}
#define _sys_ftruncate64 _sys_ftruncate64
#endif

#endif /* _NOLIBC_ARCH_MIPS_H */
