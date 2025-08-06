/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Sysmacro definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_SYSMACROS_H
#define _NOLIBC_SYS_SYSMACROS_H

#include "../std.h"

/* WARNING, it only deals with the 4096 first majors and 256 first minors */
#define makedev(major, minor) ((dev_t)((((major) & 0xfff) << 8) | ((minor) & 0xff)))
#define major(dev) ((unsigned int)(((dev) >> 8) & 0xfff))
#define minor(dev) ((unsigned int)((dev) & 0xff))

#endif /* _NOLIBC_SYS_SYSMACROS_H */
