/* $FreeBSD$ */
/*	$NetBSD: citrus_db_factory.c,v 1.10 2013/09/14 13:05:51 joerg Exp $	*/

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
#include <sys/types.h>
#include <sys/queue.h>

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_region.h"
#include "citrus_db_file.h"
#include "citrus_db_factory.h"

struct _citrus_db_factory_entry {
	STAILQ_ENTRY(_citrus_db_factory_entry)	 de_entry;
	struct _citrus_db_factory_entry		*de_next;
	uint32_t				 de_hashvalue;
	struct _region				 de_key;
	int					 de_key_free;
	struct _region				 de_data;
	int					 de_data_free;
	int					de_idx;
};

struct _citrus_db_factory {
	size_t					 df_num_entries;
	STAILQ_HEAD(, _citrus_db_factory_entry)	 df_entries;
	size_t					 df_total_key_size;
	size_t					 df_total_data_size;
	uint32_t (*df_hashfunc)(struct _citrus_region *);
	void					*df_hashfunc_closure;
};

#define DB_ALIGN 16

int
_citrus_db_factory_create(struct _citrus_db_factory **rdf,
    _citrus_db_hash_func_t hashfunc, void *hashfunc_closure)
{
	struct _citrus_db_factory *df;

	df = malloc(sizeof(*df));
	if (df == NULL)
		return (errno);
	df->df_num_entries = 0;
	df->df_total_key_size = df->df_total_data_size = 0;
	STAILQ_INIT(&df->df_entries);
	df->df_hashfunc = hashfunc;
	df->df_hashfunc_closure = hashfunc_closure;

	*rdf = df;

	return (0);
}

void
_citrus_db_factory_free(struct _citrus_db_factory *df)
{
	struct _citrus_db_factory_entry *de;

	while ((de = STAILQ_FIRST(&df->df_entries)) != NULL) {
		STAILQ_REMOVE_HEAD(&df->df_entries, de_entry);
		if (de->de_key_free)
			free(_region_head(&de->de_key));
		if (de->de_data_free)
			free(_region_head(&de->de_data));
		free(de);
	}
	free(df);
}

static __inline size_t
ceilto(size_t sz)
{
	return ((sz + DB_ALIGN - 1) & ~(DB_ALIGN - 1));
}

int
_citrus_db_factory_add(struct _citrus_db_factory *df, struct _region *key,
    int keyfree, struct _region *data, int datafree)
{
	struct _citrus_db_factory_entry *de;

	de = malloc(sizeof(*de));
	if (de == NULL)
		return (-1);

	de->de_hashvalue = df->df_hashfunc(key);
	de->de_key = *key;
	de->de_key_free = keyfree;
	de->de_data = *data;
	de->de_data_free = datafree;
	de->de_idx = -1;

	STAILQ_INSERT_TAIL(&df->df_entries, de, de_entry);
	df->df_total_key_size += _region_size(key);
	df->df_total_data_size += ceilto(_region_size(data));
	df->df_num_entries++;

	return (0);

}

int
_citrus_db_factory_add_by_string(struct _citrus_db_factory *df,
    const char *key, struct _citrus_region *data, int datafree)
{
	struct _region r;
	char *tmp;

	tmp = strdup(key);
	if (tmp == NULL)
		return (errno);
	_region_init(&r, tmp, strlen(key));
	return _citrus_db_factory_add(df, &r, 1, data, datafree);
}

int
_citrus_db_factory_add8_by_string(struct _citrus_db_factory *df,
    const char *key, uint8_t val)
{
	struct _region r;
	uint8_t *p;

	p = malloc(sizeof(*p));
	if (p == NULL)
		return (errno);
	*p = val;
	_region_init(&r, p, 1);
	return (_citrus_db_factory_add_by_string(df, key, &r, 1));
}

int
_citrus_db_factory_add16_by_string(struct _citrus_db_factory *df,
    const char *key, uint16_t val)
{
	struct _region r;
	uint16_t *p;

	p = malloc(sizeof(*p));
	if (p == NULL)
		return (errno);
	*p = htons(val);
	_region_init(&r, p, 2);
	return (_citrus_db_factory_add_by_string(df, key, &r, 1));
}

int
_citrus_db_factory_add32_by_string(struct _citrus_db_factory *df,
    const char *key, uint32_t val)
{
	struct _region r;
	uint32_t *p;

	p = malloc(sizeof(*p));
	if (p == NULL)
		return (errno);
	*p = htonl(val);
	_region_init(&r, p, 4);
	return (_citrus_db_factory_add_by_string(df, key, &r, 1));
}

int
_citrus_db_factory_add_string_by_string(struct _citrus_db_factory *df,
    const char *key, const char *data)
{
	char *p;
	struct _region r;

	p = strdup(data);
	if (p == NULL)
		return (errno);
	_region_init(&r, p, strlen(p) + 1);
	return (_citrus_db_factory_add_by_string(df, key, &r, 1));
}

size_t
_citrus_db_factory_calc_size(struct _citrus_db_factory *df)
{
	size_t sz;

	sz = ceilto(_CITRUS_DB_HEADER_SIZE);
	sz += ceilto(_CITRUS_DB_ENTRY_SIZE * df->df_num_entries);
	sz += ceilto(df->df_total_key_size);
	sz += df->df_total_data_size;

	return (sz);
}

static __inline void
put8(struct _region *r, size_t *rofs, uint8_t val)
{

	*(uint8_t *)_region_offset(r, *rofs) = val;
	*rofs += 1;
}

static __inline void
put32(struct _region *r, size_t *rofs, uint32_t val)
{

	val = htonl(val);
	memcpy(_region_offset(r, *rofs), &val, 4);
	*rofs += 4;
}

static __inline void
putpad(struct _region *r, size_t *rofs)
{
	size_t i;

	for (i = ceilto(*rofs) - *rofs; i > 0; i--)
		put8(r, rofs, 0);
}

static __inline void
dump_header(struct _region *r, const char *magic, size_t *rofs,
    size_t num_entries)
{

	while (*rofs<_CITRUS_DB_MAGIC_SIZE)
		put8(r, rofs, *magic++);
	put32(r, rofs, num_entries);
	put32(r, rofs, _CITRUS_DB_HEADER_SIZE);
}

int
_citrus_db_factory_serialize(struct _citrus_db_factory *df, const char *magic,
    struct _region *r)
{
	struct _citrus_db_factory_entry *de, **depp, *det;
	size_t dataofs, i, keyofs, nextofs, ofs;

	ofs = 0;
	/* check whether more than 0 entries exist */
	if (df->df_num_entries == 0) {
		dump_header(r, magic, &ofs, 0);
		return (0);
	}
	/* allocate hash table */
	depp = calloc(df->df_num_entries, sizeof(*depp));
	if (depp == NULL)
		return (-1);

	/* step1: store the entries which are not conflicting */
	STAILQ_FOREACH(de, &df->df_entries, de_entry) {
		de->de_hashvalue %= df->df_num_entries;
		de->de_idx = -1;
		de->de_next = NULL;
		if (depp[de->de_hashvalue] == NULL) {
			depp[de->de_hashvalue] = de;
			de->de_idx = (int)de->de_hashvalue;
		}
	}

	/* step2: resolve conflicts */
	i = 0;
	STAILQ_FOREACH(de, &df->df_entries, de_entry) {
		if (de->de_idx == -1) {
			det = depp[de->de_hashvalue];
			while (det->de_next != NULL)
				det = det->de_next;
			det->de_next = de;
			while (depp[i] != NULL)
				i++;
			depp[i] = de;
			de->de_idx = (int)i;
		}
	}

	keyofs = _CITRUS_DB_HEADER_SIZE +
	    ceilto(df->df_num_entries*_CITRUS_DB_ENTRY_SIZE);
	dataofs = keyofs + ceilto(df->df_total_key_size);

	/* dump header */
	dump_header(r, magic, &ofs, df->df_num_entries);

	/* dump entries */
	for (i = 0; i < df->df_num_entries; i++) {
		de = depp[i];
		nextofs = 0;
		if (de->de_next) {
			nextofs = _CITRUS_DB_HEADER_SIZE +
			    de->de_next->de_idx * _CITRUS_DB_ENTRY_SIZE;
		}
		put32(r, &ofs, de->de_hashvalue);
		put32(r, &ofs, nextofs);
		put32(r, &ofs, keyofs);
		put32(r, &ofs, _region_size(&de->de_key));
		put32(r, &ofs, dataofs);
		put32(r, &ofs, _region_size(&de->de_data));
		memcpy(_region_offset(r, keyofs),
		    _region_head(&de->de_key), _region_size(&de->de_key));
		keyofs += _region_size(&de->de_key);
		memcpy(_region_offset(r, dataofs),
		    _region_head(&de->de_data), _region_size(&de->de_data));
		dataofs += _region_size(&de->de_data);
		putpad(r, &dataofs);
	}
	putpad(r, &ofs);
	putpad(r, &keyofs);
	free(depp);

	return (0);
}
