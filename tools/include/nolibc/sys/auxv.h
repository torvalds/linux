/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * auxv definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

/* make sure to include all global symbols */
#include "../nolibc.h"

#ifndef _NOLIBC_SYS_AUXV_H
#define _NOLIBC_SYS_AUXV_H

#include "../crt.h"

static __attribute__((unused))
unsigned long getauxval(unsigned long type)
{
	const unsigned long *auxv = _auxv;
	unsigned long ret;

	if (!auxv)
		return 0;

	while (1) {
		if (!auxv[0] && !auxv[1]) {
			ret = 0;
			break;
		}

		if (auxv[0] == type) {
			ret = auxv[1];
			break;
		}

		auxv += 2;
	}

	return ret;
}

#endif /* _NOLIBC_SYS_AUXV_H */
