/* $FreeBSD$ */
/* $NetBSD: citrus_esdb.c,v 1.5 2008/02/09 14:56:20 junyoung Exp $ */

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

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_region.h"
#include "citrus_memstream.h"
#include "citrus_mmap.h"
#include "citrus_lookup.h"
#include "citrus_db.h"
#include "citrus_db_hash.h"
#include "citrus_esdb.h"
#include "citrus_esdb_file.h"

#define ESDB_DIR	"esdb.dir"
#define ESDB_ALIAS	"esdb.alias"

/*
 * _citrus_esdb_alias:
 *	resolve encoding scheme name aliases.
 */
const char *
_citrus_esdb_alias(const char *esname, char *buf, size_t bufsize)
{

	return (_lookup_alias(_PATH_ESDB "/" ESDB_ALIAS, esname, buf, bufsize,
	    _LOOKUP_CASE_IGNORE));
}


/*
 * conv_esdb:
 *	external representation -> local structure.
 */
static int
conv_esdb(struct _citrus_esdb *esdb, struct _region *fr)
{
	struct _citrus_db *db;
	const char *str;
	char buf[100];
	uint32_t csid, i, num_charsets, tmp, version;
	int ret;

	/* open db */
	ret = _db_open(&db, fr, _CITRUS_ESDB_MAGIC, &_db_hash_std, NULL);
	if (ret)
		goto err0;

	/* check version */
	ret = _db_lookup32_by_s(db, _CITRUS_ESDB_SYM_VERSION, &version, NULL);
	if (ret)
		goto err1;
	switch (version) {
	case 0x00000001:
		/* current version */
		/* initial version */
		break;
	default:
		ret = EFTYPE;
		goto err1;
	}

	/* get encoding/variable */
	ret = _db_lookupstr_by_s(db, _CITRUS_ESDB_SYM_ENCODING, &str, NULL);
	if (ret)
		goto err1;
	esdb->db_encname = strdup(str);
	if (esdb->db_encname == NULL) {
		ret = errno;
		goto err1;
	}

	esdb->db_len_variable = 0;
	esdb->db_variable = NULL;
	ret = _db_lookupstr_by_s(db, _CITRUS_ESDB_SYM_VARIABLE, &str, NULL);
	if (ret == 0) {
		esdb->db_len_variable = strlen(str) + 1;
		esdb->db_variable = strdup(str);
		if (esdb->db_variable == NULL) {
			ret = errno;
			goto err2;
		}
	} else if (ret != ENOENT)
		goto err2;

	/* get number of charsets */
	ret = _db_lookup32_by_s(db, _CITRUS_ESDB_SYM_NUM_CHARSETS,
	    &num_charsets, NULL);
	if (ret)
		goto err3;
	esdb->db_num_charsets = num_charsets;

	/* get invalid character */
	ret = _db_lookup32_by_s(db, _CITRUS_ESDB_SYM_INVALID, &tmp, NULL);
	if (ret == 0) {
		esdb->db_use_invalid = 1;
		esdb->db_invalid = tmp;
	} else if (ret == ENOENT)
		esdb->db_use_invalid = 0;
	else
		goto err3;

	/* get charsets */
	esdb->db_charsets = malloc(num_charsets * sizeof(*esdb->db_charsets));
	if (esdb->db_charsets == NULL) {
		ret = errno;
		goto err3;
	}
	for (i = 0; i < num_charsets; i++) {
		snprintf(buf, sizeof(buf),
		    _CITRUS_ESDB_SYM_CSID_PREFIX "%d", i);
		ret = _db_lookup32_by_s(db, buf, &csid, NULL);
		if (ret)
			goto err4;
		esdb->db_charsets[i].ec_csid = csid;

		snprintf(buf, sizeof(buf),
		    _CITRUS_ESDB_SYM_CSNAME_PREFIX "%d", i);
		ret = _db_lookupstr_by_s(db, buf, &str, NULL);
		if (ret)
			goto err4;
		esdb->db_charsets[i].ec_csname = strdup(str);
		if (esdb->db_charsets[i].ec_csname == NULL) {
			ret = errno;
			goto err4;
		}
	}

	_db_close(db);
	return (0);

err4:
	for (; i > 0; i--)
		free(esdb->db_charsets[i - 1].ec_csname);
	free(esdb->db_charsets);
err3:
	free(esdb->db_variable);
err2:
	free(esdb->db_encname);
err1:
	_db_close(db);
	if (ret == ENOENT)
		ret = EFTYPE;
err0:
	return (ret);
}

/*
 * _citrus_esdb_open:
 *	open an ESDB file.
 */
int
_citrus_esdb_open(struct _citrus_esdb *db, const char *esname)
{
	struct _region fr;
	const char *realname, *encfile;
	char buf1[PATH_MAX], buf2[PATH_MAX], path[PATH_MAX];
	int ret;

	snprintf(path, sizeof(path), "%s/%s", _PATH_ESDB, ESDB_ALIAS);
	realname = _lookup_alias(path, esname, buf1, sizeof(buf1),
	    _LOOKUP_CASE_IGNORE);

	snprintf(path, sizeof(path), "%s/%s", _PATH_ESDB, ESDB_DIR);
	encfile = _lookup_simple(path, realname, buf2, sizeof(buf2),
	    _LOOKUP_CASE_IGNORE);
	if (encfile == NULL)
		return (ENOENT);

	/* open file */
	snprintf(path, sizeof(path), "%s/%s", _PATH_ESDB, encfile);
	ret = _map_file(&fr, path);
	if (ret)
		return (ret);

	ret = conv_esdb(db, &fr);

	_unmap_file(&fr);

	return (ret);
}

/*
 * _citrus_esdb_close:
 *	free an ESDB.
 */
void
_citrus_esdb_close(struct _citrus_esdb *db)
{

	for (int i = 0; i < db->db_num_charsets; i++)
		free(db->db_charsets[i].ec_csname);
	db->db_num_charsets = 0;
	free(db->db_charsets); db->db_charsets = NULL;
	free(db->db_encname); db->db_encname = NULL;
	db->db_len_variable = 0;
	free(db->db_variable); db->db_variable = NULL;
}

/*
 * _citrus_esdb_free_list:
 *	free the list.
 */
void
_citrus_esdb_free_list(char **list, size_t num)
{

	for (size_t i = 0; i < num; i++)
		free(list[i]);
	free(list);
}

/*
 * _citrus_esdb_get_list:
 *	get esdb entries.
 */
int
_citrus_esdb_get_list(char ***rlist, size_t *rnum, bool sorted)
{
	struct _citrus_lookup *cla, *cld;
	struct _region key, data;
	char **list, **q;
	char buf[PATH_MAX];
	size_t num;
	int ret;

	ret = _lookup_seq_open(&cla, _PATH_ESDB "/" ESDB_ALIAS,
	    _LOOKUP_CASE_IGNORE);
	if (ret)
		goto quit0;

	ret = _lookup_seq_open(&cld, _PATH_ESDB "/" ESDB_DIR,
	    _LOOKUP_CASE_IGNORE);
	if (ret)
		goto quit1;

	/* count number of entries */
	num = _lookup_get_num_entries(cla) + _lookup_get_num_entries(cld);

	_lookup_seq_rewind(cla);
	_lookup_seq_rewind(cld);

	/* allocate list pointer space */
	list = malloc(num * sizeof(char *));
	num = 0;
	if (list == NULL) {
		ret = errno;
		goto quit3;
	}

	/* get alias entries */
	while ((ret = _lookup_seq_next(cla, &key, &data)) == 0) {
		/* XXX: sorted? */
		snprintf(buf, sizeof(buf), "%.*s/%.*s",
		    (int)_region_size(&data),
		    (const char *)_region_head(&data),
		    (int)_region_size(&key),
		    (const char *)_region_head(&key));
		_bcs_convert_to_upper(buf);
		list[num] = strdup(buf);
		if (list[num] == NULL) {
			ret = errno;
			goto quit3;
		}
		num++;
	}
	if (ret != ENOENT)
		goto quit3;
	/* get dir entries */
	while ((ret = _lookup_seq_next(cld, &key, &data)) == 0) {
		if (!sorted)
			snprintf(buf, sizeof(buf), "%.*s",
			    (int)_region_size(&key),
			    (const char *)_region_head(&key));
		else {
			/* check duplicated entry */
			char *p;
			char buf1[PATH_MAX];

			snprintf(buf1, sizeof(buf1), "%.*s",
			    (int)_region_size(&data),
			    (const char *)_region_head(&data));
			if ((p = strchr(buf1, '/')) != NULL)
				memmove(buf1, p + 1, strlen(p) - 1);
			if ((p = strstr(buf1, ".esdb")) != NULL)
				*p = '\0';
			snprintf(buf, sizeof(buf), "%s/%.*s", buf1,
			    (int)_region_size(&key),
			    (const char *)_region_head(&key));
		}
		_bcs_convert_to_upper(buf);
		ret = _lookup_seq_lookup(cla, buf, NULL);
		if (ret) {
			if (ret != ENOENT)
				goto quit3;
			/* not duplicated */
			list[num] = strdup(buf);
			if (list[num] == NULL) {
				ret = errno;
				goto quit3;
			}
			num++;
		}
	}
	if (ret != ENOENT)
		goto quit3;

	ret = 0;
	/* XXX: why reallocing the list space posteriorly?
	    shouldn't be done earlier? */
	q = reallocarray(list, num, sizeof(char *));
	if (!q) {
		ret = ENOMEM;
		goto quit3;
	}
	list = q;
	*rlist = list;
	*rnum = num;
quit3:
	if (ret)
		_citrus_esdb_free_list(list, num);
	_lookup_seq_close(cld);
quit1:
	_lookup_seq_close(cla);
quit0:
	return (ret);
}
