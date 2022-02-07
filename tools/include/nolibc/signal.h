/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * signal function definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_SIGNAL_H
#define _NOLIBC_SIGNAL_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"

/* This one is not marked static as it's needed by libgcc for divide by zero */
__attribute__((weak,unused,section(".text.nolibc_raise")))
int raise(int signal)
{
	return sys_kill(sys_getpid(), signal);
}

#endif /* _NOLIBC_SIGNAL_H */
