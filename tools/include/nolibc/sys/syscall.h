/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * syscall() definition for NOLIBC
 * Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_SYSCALL_H
#define _NOLIBC_SYS_SYSCALL_H

#define ___nolibc_syscall_narg(_0, _1, _2, _3, _4, _5, _6, N, ...) N
#define __nolibc_syscall_narg(...) ___nolibc_syscall_narg(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define __nolibc_syscall(N, ...) __nolibc_syscall##N(__VA_ARGS__)
#define __nolibc_syscall_n(N, ...) __nolibc_syscall(N, __VA_ARGS__)
#define _syscall(...) __nolibc_syscall_n(__nolibc_syscall_narg(__VA_ARGS__), ##__VA_ARGS__)
#define syscall(...) __sysret(_syscall(__VA_ARGS__))

#endif /* _NOLIBC_SYS_SYSCALL_H */
