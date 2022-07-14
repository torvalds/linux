/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Minimal errno definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_ERRNO_H
#define _NOLIBC_ERRNO_H

#include <asm/errno.h>

/* this way it will be removed if unused */
static int errno;

#ifndef NOLIBC_IGNORE_ERRNO
#define SET_ERRNO(v) do { errno = (v); } while (0)
#else
#define SET_ERRNO(v) do { } while (0)
#endif


/* errno codes all ensure that they will not conflict with a valid pointer
 * because they all correspond to the highest addressable memory page.
 */
#define MAX_ERRNO 4095

#endif /* _NOLIBC_ERRNO_H */
