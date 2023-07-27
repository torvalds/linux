/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#ifndef SYSTEM_H
#define SYSTEM_H

#include <linux/types.h>
#include <linux/stddef.h>

typedef __kernel_size_t size_t;
typedef __kernel_ssize_t ssize_t;

#include <linux/errno.h>
#include <linux/compiler.h>

#include <asm/hwcap.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>

long syscall(int nr, ...);

void __noreturn exit(int n);
ssize_t write(int fd, const void *buf, size_t size);

#endif /* ! SYSTEM_H */
