/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * ctype function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_CTYPE_H
#define _NOLIBC_CTYPE_H

#include "std.h"

/*
 * As much as possible, please keep functions alphabetically sorted.
 */

static __attribute__((unused))
int isdigit(int c)
{
	return (unsigned int)(c - '0') <= 9;
}

#endif /* _NOLIBC_CTYPE_H */
