/* $FreeBSD$ */
/* $NetBSD: citrus_db.c,v 1.5 2008/02/09 14:56:20 junyoung Exp $ */

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
#include <sys/endian.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_bcs.h"
#include "citrus_region.h"
#include "citrus_memstream.h"
#include "citrus_mmap.h"
#include "citrus_db.h"
#include "citrus_db_factory.h"
#include "citrus_db_file.h"

struct _citrus_db {
	struct _region		 db_region;
	_citrus_db_hash_func_t	 db_hashfunc;
	void			*db_hashfunc_closure;
};

int
_citrus_db_open(struct _citrus_db **rdb, struct _region *r, const char *magic,
    _citrus_db_hash_func_t hashfunc, void *hashfunc_closure)
{
	struct _citrus_db *db;
	struct _citrus_db_header_x *dhx;
	struct _memstream ms;

	_memstream_bind(&ms, r);

	/* sanity check */
	dhx = _memstream_getregion(&ms, NULL, sizeof(*dhx));
	if (dhx == NULL)
		return (EFTYPE);
	if (strncmp(dhx->dhx_magic, magic, _CITRUS_DB_MAGIC_SIZE) != 0)
		return (EFTYPE);
	if (_memstream_seek(&ms, be32toh(dhx->dhx_entry_offset), SEEK_SET))
		return (EFTYPE);

	if (be32toh(dhx->dhx_num_entries)*_CITRUS_DB_ENTRY_SIZE >
	    _memstream_remainder(&ms))
		return (EFTYPE);

	db = malloc(sizeof(*db));
	if (db == NULL)
		return (errno);
	db->db_region = *r;
	db->db_hashfunc = hashfunc;
	db->db_hashfunc_closure = hashfunc_closure;
	*rdb = db;

	return (0);
}

void
_citrus_db_close(struct _citrus_db *db)
{

	free(db);
}

int
_citrus_db_lookup(struct _citrus_db *db, struct _citrus_region *key,
    struct _citrus_region *data, struct _citrus_db_locator *dl)
{
	struct _citrus_db_entry_x *dex;
	struct _citrus_db_header_x *dhx;
	struct _citrus_region r;
	struct _memstream ms;
	uint32_t hashval, num_entries;
	size_t offset;

	_memstream_bind(&ms, &db->db_region);

	dhx = _memstream_getregion(&ms, NULL, sizeof(*dhx));
	num_entries = be32toh(dhx->dhx_num_entries);
	if (num_entries == 0)
		return (ENOENT);

	if (dl != NULL && dl->dl_offset>0) {
		hashval = dl->dl_hashval;
		offset = dl->dl_offset;
		if (offset >= _region_size(&db->db_region))
			return (ENOENT);
	} else {
		hashval = db->db_hashfunc(key)%num_entries;
		offset = be32toh(dhx->dhx_entry_offset) +
		    hashval * _CITRUS_DB_ENTRY_SIZE;
		if (dl)
			dl->dl_hashval = hashval;
	}
	do {
		/* seek to the next entry */
		if (_citrus_memory_stream_seek(&ms, offset, SEEK_SET))
			return (EFTYPE);
		/* get the entry record */
		dex = _memstream_getregion(&ms, NULL, _CITRUS_DB_ENTRY_SIZE);
		if (dex == NULL)
			return (EFTYPE);

		/* jump to next entry having the same hash value. */
		offset = be32toh(dex->dex_next_offset);

		/* save the current position */
		if (dl) {
			dl->dl_offset = offset;
			if (offset == 0)
				dl->dl_offset = _region_size(&db->db_region);
		}

		/* compare hash value. */
		if (be32toh(dex->dex_hash_value) != hashval)
			/* not found */
			break;
		/* compare key length */
		if (be32toh(dex->dex_key_size) == _region_size(key)) {
			/* seek to the head of the key. */
			if (_memstream_seek(&ms, be32toh(dex->dex_key_offset),
			    SEEK_SET))
				return (EFTYPE);
			/* get the region of the key */
			if (_memstream_getregion(&ms, &r,
			    _region_size(key)) == NULL)
				return (EFTYPE);
			/* compare key byte stream */
			if (memcmp(_region_head(&r), _region_head(key),
			    _region_size(key)) == 0) {
				/* match */
				if (_memstream_seek(
				    &ms, be32toh(dex->dex_data_offset),
				    SEEK_SET))
					return (EFTYPE);
				if (_memstream_getregion(
				    &ms, data,
				    be32toh(dex->dex_data_size)) == NULL)
					return (EFTYPE);
				return (0);
			}
		}
	} while (offset != 0);

	return (ENOENT);
}

int
_citrus_db_lookup_by_string(struct _citrus_db *db, const char *key,
    struct _citrus_region *data, struct _citrus_db_locator *dl)
{
	struct _region r;

	_region_init(&r, __DECONST(void *, key), strlen(key));

	return (_citrus_db_lookup(db, &r, data, dl));
}

int
_citrus_db_lookup8_by_string(struct _citrus_db *db, const char *key,
    uint8_t *rval, struct _citrus_db_locator *dl)
{
	struct _region r;
	int ret;

	ret = _citrus_db_lookup_by_string(db, key, &r, dl);
	if (ret)
		return (ret);

	if (_region_size(&r) != 1)
		return (EFTYPE);

	if (rval)
		memcpy(rval, _region_head(&r), 1);

	return (0);
}

int
_citrus_db_lookup16_by_string(struct _citrus_db *db, const char *key,
    uint16_t *rval, struct _citrus_db_locator *dl)
{
	struct _region r;
	int ret;
	uint16_t val;

	ret = _citrus_db_lookup_by_string(db, key, &r, dl);
	if (ret)
		return (ret);

	if (_region_size(&r) != 2)
		return (EFTYPE);

	if (rval) {
		memcpy(&val, _region_head(&r), 2);
		*rval = be16toh(val);
	}

	return (0);
}

int
_citrus_db_lookup32_by_string(struct _citrus_db *db, const char *key,
    uint32_t *rval, struct _citrus_db_locator *dl)
{
	struct _region r;
	uint32_t val;
	int ret;

	ret = _citrus_db_lookup_by_string(db, key, &r, dl);
	if (ret)
		return (ret);

	if (_region_size(&r) != 4)
		return (EFTYPE);

	if (rval) {
		memcpy(&val, _region_head(&r), 4);
		*rval = be32toh(val);
	}

	return (0);
}

int
_citrus_db_lookup_string_by_string(struct _citrus_db *db, const char *key,
    const char **rdata, struct _citrus_db_locator *dl)
{
	struct _region r;
	int ret;

	ret = _citrus_db_lookup_by_string(db, key, &r, dl);
	if (ret)
		return (ret);

	/* check whether the string is null terminated */
	if (_region_size(&r) == 0)
		return (EFTYPE);
	if (*((const char*)_region_head(&r)+_region_size(&r)-1) != '\0')
		return (EFTYPE);

	if (rdata)
		*rdata = _region_head(&r);

	return (0);
}

int
_citrus_db_get_number_of_entries(struct _citrus_db *db)
{
	struct _citrus_db_header_x *dhx;
	struct _memstream ms;

	_memstream_bind(&ms, &db->db_region);

	dhx = _memstream_getregion(&ms, NULL, sizeof(*dhx));
	return ((int)be32toh(dhx->dhx_num_entries));
}

int
_citrus_db_get_entry(struct _citrus_db *db, int idx, struct _region *key,
    struct _region *data)
{
	struct _citrus_db_entry_x *dex;
	struct _citrus_db_header_x *dhx;
	struct _memstream ms;
	uint32_t num_entries;
	size_t offset;

	_memstream_bind(&ms, &db->db_region);

	dhx = _memstream_getregion(&ms, NULL, sizeof(*dhx));
	num_entries = be32toh(dhx->dhx_num_entries);
	if (idx < 0 || (uint32_t)idx >= num_entries)
		return (EINVAL);

	/* seek to the next entry */
	offset = be32toh(dhx->dhx_entry_offset) + idx * _CITRUS_DB_ENTRY_SIZE;
	if (_citrus_memory_stream_seek(&ms, offset, SEEK_SET))
		return (EFTYPE);
	/* get the entry record */
	dex = _memstream_getregion(&ms, NULL, _CITRUS_DB_ENTRY_SIZE);
	if (dex == NULL)
		return (EFTYPE);
	/* seek to the head of the key. */
	if (_memstream_seek(&ms, be32toh(dex->dex_key_offset), SEEK_SET))
		return (EFTYPE);
	/* get the region of the key. */
	if (_memstream_getregion(&ms, key, be32toh(dex->dex_key_size))==NULL)
		return (EFTYPE);
	/* seek to the head of the data. */
	if (_memstream_seek(&ms, be32toh(dex->dex_data_offset), SEEK_SET))
		return (EFTYPE);
	/* get the region of the data. */
	if (_memstream_getregion(&ms, data, be32toh(dex->dex_data_size))==NULL)
		return (EFTYPE);

	return (0);
}
