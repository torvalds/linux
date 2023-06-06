/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Standard definitions and types for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STD_H
#define _NOLIBC_STD_H

/* Declare a few quite common macros and types that usually are in stdlib.h,
 * stdint.h, ctype.h, unistd.h and a few other common locations. Please place
 * integer type definitions and generic macros here, but avoid OS-specific and
 * syscall-specific stuff, as this file is expected to be included very early.
 */

/* note: may already be defined */
#ifndef NULL
#define NULL ((void *)0)
#endif

#include "stdint.h"

/* those are commonly provided by sys/types.h */
typedef unsigned int          dev_t;
typedef unsigned long         ino_t;
typedef unsigned int         mode_t;
typedef   signed int          pid_t;
typedef unsigned int          uid_t;
typedef unsigned int          gid_t;
typedef unsigned long       nlink_t;
typedef   signed long         off_t;
typedef   signed long     blksize_t;
typedef   signed long      blkcnt_t;
typedef   signed long        time_t;

#endif /* _NOLIBC_STD_H */
