/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Minimal errno definitions for NOLIBC
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_ERRNO_H
#define _NOLIBC_ERRNO_H

#include <linux/errno.h>

#ifndef NOLIBC_IGNORE_ERRNO
#define SET_ERRNO(v) do { errno = (v); } while (0)
int errno __attribute__((weak));
#else
#define SET_ERRNO(v) do { } while (0)
#endif


/* errno codes all ensure that they will not conflict with a valid pointer
 * because they all correspond to the highest addressable memory page.
 */
#define MAX_ERRNO 4095

#endif /* _NOLIBC_ERRNO_H */
