/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Stack protector support for NOLIBC
 * Copyright (C) 2023 Thomas Weißschuh <linux@weissschuh.net>
 */

#ifndef _NOLIBC_STACKPROTECTOR_H
#define _NOLIBC_STACKPROTECTOR_H

#include "compiler.h"

#if defined(_NOLIBC_STACKPROTECTOR)

#include "sys.h"
#include "stdlib.h"

/* The functions in this header are using raw syscall macros to avoid
 * triggering stack protector errors themselves
 */

__attribute__((weak,used,noreturn,section(".text.nolibc_stack_chk")))
void __stack_chk_fail(void)
{
	pid_t pid;
	my_syscall3(__NR_write, STDERR_FILENO, "!!Stack smashing detected!!\n", 28);
	pid = my_syscall0(__NR_getpid);
	my_syscall2(__NR_kill, pid, SIGABRT);
	for (;;);
}

__attribute__((weak,noreturn,section(".text.nolibc_stack_chk")))
void __stack_chk_fail_local(void)
{
	__stack_chk_fail();
}

__attribute__((weak,used,section(".data.nolibc_stack_chk")))
uintptr_t __stack_chk_guard;

static __no_stack_protector void __stack_chk_init(void)
{
	my_syscall3(__NR_getrandom, &__stack_chk_guard, sizeof(__stack_chk_guard), 0);
	/* a bit more randomness in case getrandom() fails, ensure the guard is never 0 */
	if (__stack_chk_guard != (uintptr_t) &__stack_chk_guard)
		__stack_chk_guard ^= (uintptr_t) &__stack_chk_guard;
}
#else /* !defined(_NOLIBC_STACKPROTECTOR) */
static void __stack_chk_init(void) {}
#endif /* defined(_NOLIBC_STACKPROTECTOR) */

#endif /* _NOLIBC_STACKPROTECTOR_H */
