/* $FreeBSD$ */
/* $NetBSD: citrus_pivot_factory.c,v 1.7 2009/04/12 14:20:19 lukem Exp $ */

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
#include <sys/queue.h>

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
#include "citrus_pivot_file.h"
#include "citrus_pivot_factory.h"

struct src_entry {
	char				*se_name;
	struct _citrus_db_factory	*se_df;
	STAILQ_ENTRY(src_entry)		 se_entry;
};
STAILQ_HEAD(src_head, src_entry);

static int
find_src(struct src_head *sh, struct src_entry **rse, const char *name)
{
	int ret;
	struct src_entry *se;

	STAILQ_FOREACH(se, sh, se_entry) {
		if (_bcs_strcasecmp(se->se_name, name) == 0) {
			*rse = se;
			return (0);
		}
	}
	se = malloc(sizeof(*se));
	if (se == NULL)
		return (errno);
	se->se_name = strdup(name);
	if (se->se_name == NULL) {
		ret = errno;
		free(se);
		return (ret);
	}
	ret = _db_factory_create(&se->se_df, &_db_hash_std, NULL);
	if (ret) {
		free(se->se_name);
		free(se);
		return (ret);
	}
	STAILQ_INSERT_TAIL(sh, se, se_entry);
	*rse = se;

	return (0);
}

static void
free_src(struct src_head *sh)
{
	struct src_entry *se;

	while ((se = STAILQ_FIRST(sh)) != NULL) {
		STAILQ_REMOVE_HEAD(sh, se_entry);
		_db_factory_free(se->se_df);
		free(se->se_name);
		free(se);
	}
}


#define T_COMM '#'
static int
convert_line(struct src_head *sh, const char *line, size_t len)
{
	struct src_entry *se;
	const char *p;
	char key1[LINE_MAX], key2[LINE_MAX], data[LINE_MAX];
	char *ep;
	uint32_t val;
	int ret;

	se = NULL;

	/* cut off trailing comment */
	p = memchr(line, T_COMM, len);
	if (p)
		len = p - line;

	/* key1 */
	line = _bcs_skip_ws_len(line, &len);
	if (len == 0)
		return (0);
	p = _bcs_skip_nonws_len(line, &len);
	if (p == line)
		return (0);
	snprintf(key1, sizeof(key1), "%.*s", (int)(p - line), line);

	/* key2 */
	line = _bcs_skip_ws_len(p, &len);
	if (len == 0)
		return (0);
	p = _bcs_skip_nonws_len(line, &len);
	if (p == line)
		return (0);
	snprintf(key2, sizeof(key2), "%.*s", (int)(p - line), line);

	/* data */
	line = _bcs_skip_ws_len(p, &len);
	_bcs_trunc_rws_len(line, &len);
	snprintf(data, sizeof(data), "%.*s", (int)len, line);
	val = strtoul(data, &ep, 0);
	if (*ep != '\0')
		return (EFTYPE);

	/* insert to DB */
	ret = find_src(sh, &se, key1);
	if (ret)
		return (ret);

	return (_db_factory_add32_by_s(se->se_df, key2, val));
}

static int
dump_db(struct src_head *sh, struct _region *r)
{
	struct _db_factory *df;
	struct src_entry *se;
	struct _region subr;
	void *ptr;
	size_t size;
	int ret;

	ret = _db_factory_create(&df, &_db_hash_std, NULL);
	if (ret)
		return (ret);

	STAILQ_FOREACH(se, sh, se_entry) {
		size = _db_factory_calc_size(se->se_df);
		ptr = malloc(size);
		if (ptr == NULL)
			goto quit;
		_region_init(&subr, ptr, size);
		ret = _db_factory_serialize(se->se_df, _CITRUS_PIVOT_SUB_MAGIC,
		    &subr);
		if (ret)
			goto quit;
		ret = _db_factory_add_by_s(df, se->se_name, &subr, 1);
		if (ret)
			goto quit;
	}

	size = _db_factory_calc_size(df);
	ptr = malloc(size);
	if (ptr == NULL)
		goto quit;
	_region_init(r, ptr, size);

	ret = _db_factory_serialize(df, _CITRUS_PIVOT_MAGIC, r);
	ptr = NULL;

quit:
	free(ptr);
	_db_factory_free(df);
	return (ret);
}

int
_citrus_pivot_factory_convert(FILE *out, FILE *in)
{
	struct src_head sh;
	struct _region r;
	char *line;
	size_t size;
	int ret;

	STAILQ_INIT(&sh);

	while ((line = fgetln(in, &size)) != NULL)
		if ((ret = convert_line(&sh, line, size))) {
			free_src(&sh);
			return (ret);
		}

	ret = dump_db(&sh, &r);
	free_src(&sh);
	if (ret)
		return (ret);

	if (fwrite(_region_head(&r), _region_size(&r), 1, out) != 1)
		return (errno);

	return (0);
}
