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
static __inline__ dev_t __nolibc_makedev(unsigned int maj, unsigned int min)
{
	return ((maj & 0xfff) << 8) | (min & 0xff);
}

#define makedev(maj, min) __nolibc_makedev(maj, min)

static __inline__ unsigned int __nolibc_major(dev_t dev)
{
	return (dev >> 8) & 0xfff;
}

#define major(dev) __nolibc_major(dev)

static __inline__ unsigned int __nolibc_minor(dev_t dev)
{
	return dev & 0xff;
}

#define minor(dev) __nolibc_minor(dev)

#endif /* _NOLIBC_SYS_SYSMACROS_H */
