// SPDX-License-Identifier: GPL-2.0+

/*
 * Part of fork context switch microbenchmark.
 *
 * Copyright 2018, Anton Blanchard, IBM Corp.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>

void _start(void)
{
	syscall(SYS_exit, 0);
}
