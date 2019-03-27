/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003, Trent Nelson, <trent@arpa.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "convtbl.h"

#define BIT		(8)
#define BITS		(1)
#define KILOBIT		(1000LL)
#define MEGABIT		(KILOBIT * 1000)
#define GIGABIT		(MEGABIT * 1000)
#define TERABIT		(GIGABIT * 1000)

#define BYTE		(1)
#define BYTES		(1)
#define KILOBYTE	(1024LL)
#define MEGABYTE	(KILOBYTE * 1024)
#define GIGABYTE	(MEGABYTE * 1024)
#define TERABYTE	(GIGABYTE * 1024)

struct convtbl {
	uintmax_t	 mul;
	uintmax_t	 scale;
	const char	*str;
	const char	*name;
};

static struct convtbl convtbl[] = {
	/* mul, scale, str, name */
	[SC_BYTE] =	{ BYTE, BYTES, "B", "byte" },
	[SC_KILOBYTE] =	{ BYTE, KILOBYTE, "KB", "kbyte" },
	[SC_MEGABYTE] =	{ BYTE, MEGABYTE, "MB", "mbyte" },
	[SC_GIGABYTE] =	{ BYTE, GIGABYTE, "GB", "gbyte" },
	[SC_TERABYTE] =	{ BYTE, TERABYTE, "TB", "tbyte" },

	[SC_BIT] =	{ BIT, BITS, "b", "bit" },
	[SC_KILOBIT] =	{ BIT, KILOBIT, "Kb", "kbit" },
	[SC_MEGABIT] =	{ BIT, MEGABIT, "Mb", "mbit" },
	[SC_GIGABIT] =	{ BIT, GIGABIT, "Gb", "gbit" },
	[SC_TERABIT] =	{ BIT, TERABIT, "Tb", "tbit" },

	[SC_AUTO] =	{ 0, 0, "", "auto" }
};

static
struct convtbl *
get_tbl_ptr(const uintmax_t size, const int scale)
{
	uintmax_t	 tmp;
	int		 idx;

	/* If our index is out of range, default to auto-scaling. */
	idx = scale < SC_AUTO ? scale : SC_AUTO;

	if (idx == SC_AUTO)
		/*
		 * Simple but elegant algorithm.  Count how many times
		 * we can shift our size value right by a factor of ten,
		 * incrementing an index each time.  We then use the
		 * index as the array index into the conversion table.
		 */
		for (tmp = size, idx = SC_KILOBYTE;
		     tmp >= MEGABYTE && idx < SC_BIT - 1;
		     tmp >>= 10, idx++);

	return (&convtbl[idx]);
}

double
convert(const uintmax_t size, const int scale)
{
	struct convtbl	*tp;

	tp = get_tbl_ptr(size, scale);
	return ((double)size * tp->mul / tp->scale);

}

const char *
get_string(const uintmax_t size, const int scale)
{
	struct convtbl	*tp;

	tp = get_tbl_ptr(size, scale);
	return (tp->str);
}

int
get_scale(const char *name)
{
	int i;

	for (i = 0; i <= SC_AUTO; i++)
		if (strcmp(convtbl[i].name, name) == 0)
			return (i);
	return (-1);
}

const char *
get_helplist(void)
{
	int i;
	size_t len;
	static char *buf;

	if (buf == NULL) {
		len = 0;
		for (i = 0; i <= SC_AUTO; i++)
			len += strlen(convtbl[i].name) + 2;
		if ((buf = malloc(len)) != NULL) {
			buf[0] = '\0';
			for (i = 0; i <= SC_AUTO; i++) {
				strcat(buf, convtbl[i].name);
				if (i < SC_AUTO)
					strcat(buf, ", ");
			}
		} else
			return ("");
	}
	return (buf);
}
