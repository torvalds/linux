/* $FreeBSD$ */
/* $NetBSD: citrus_lookup_factory.c,v 1.4 2003/10/27 00:12:42 lukem Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
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
 */

#include <sys/cdefs.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_region.h"
#include "citrus_bcs.h"
#include "citrus_db_factory.h"
#include "citrus_db_hash.h"
#include "citrus_lookup_factory.h"
#include "citrus_lookup_file.h"

#define T_COMM '#'
static int
convert_line(struct _citrus_db_factory *df, const char *line, size_t len)
{
	const char *p;
	char data[LINE_MAX], key[LINE_MAX];

	/* cut off trailing comment */
	p = memchr(line, T_COMM, len);
	if (p)
		len = p - line;

	/* key */
	line = _bcs_skip_ws_len(line, &len);
	if (len == 0)
		return (0);
	p = _bcs_skip_nonws_len(line, &len);
	if (p == line)
		return (0);
	snprintf(key, sizeof(key), "%.*s", (int)(p-line), line);
	_bcs_convert_to_lower(key);

	/* data */
	line = _bcs_skip_ws_len(p, &len);
	_bcs_trunc_rws_len(line, &len);
	snprintf(data, sizeof(data), "%.*s", (int)len, line);

	return (_db_factory_addstr_by_s(df, key, data));
}

static int
dump_db(struct _citrus_db_factory *df, struct _region *r)
{
	void *ptr;
	size_t size;

	size = _db_factory_calc_size(df);
	ptr = malloc(size);
	if (ptr == NULL)
		return (errno);
	_region_init(r, ptr, size);

	return (_db_factory_serialize(df, _CITRUS_LOOKUP_MAGIC, r));
}

int
_citrus_lookup_factory_convert(FILE *out, FILE *in)
{
	struct _citrus_db_factory *df;
	struct _region r;
	char *line;
	size_t size;
	int ret;

	ret = _db_factory_create(&df, &_db_hash_std, NULL);
	if (ret)
		return (ret);

	while ((line = fgetln(in, &size)) != NULL)
		if ((ret = convert_line(df, line, size))) {
			_db_factory_free(df);
			return (ret);
		}

	ret = dump_db(df, &r);
	_db_factory_free(df);
	if (ret)
		return (ret);

	if (fwrite(_region_head(&r), _region_size(&r), 1, out) != 1)
		return (errno);

	return (0);
}
