/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * syscall() definition for NOLIBC
 * Copyright (C) 2024 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_SYSCALL_H
#define _NOLIBC_SYS_SYSCALL_H

#define __syscall_narg(_0, _1, _2, _3, _4, _5, _6, N, ...) N
#define _syscall_narg(...) __syscall_narg(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define _syscall(N, ...) __sysret(my_syscall##N(__VA_ARGS__))
#define _syscall_n(N, ...) _syscall(N, __VA_ARGS__)
#define syscall(...) _syscall_n(_syscall_narg(__VA_ARGS__), ##__VA_ARGS__)

#endif /* _NOLIBC_SYS_SYSCALL_H */
