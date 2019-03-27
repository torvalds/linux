/* $FreeBSD$ */
/*	$NetBSD: citrus_iconv.c,v 1.10 2011/11/19 18:34:21 tnozaki Exp $	*/

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

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <iconv.h>
#include <langinfo.h>
#include <limits.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "citrus_namespace.h"
#include "citrus_bcs.h"
#include "citrus_esdb.h"
#include "citrus_region.h"
#include "citrus_memstream.h"
#include "citrus_mmap.h"
#include "citrus_module.h"
#include "citrus_lock.h"
#include "citrus_lookup.h"
#include "citrus_hash.h"
#include "citrus_iconv.h"

#define _CITRUS_ICONV_DIR	"iconv.dir"
#define _CITRUS_ICONV_ALIAS	"iconv.alias"

#define CI_HASH_SIZE 101
#define CI_INITIAL_MAX_REUSE	5
#define CI_ENV_MAX_REUSE	"ICONV_MAX_REUSE"

static bool			 isinit = false;
static int			 shared_max_reuse, shared_num_unused;
static _CITRUS_HASH_HEAD(, _citrus_iconv_shared, CI_HASH_SIZE) shared_pool;
static TAILQ_HEAD(, _citrus_iconv_shared) shared_unused;

static pthread_rwlock_t		 ci_lock = PTHREAD_RWLOCK_INITIALIZER;

static __inline void
init_cache(void)
{

	WLOCK(&ci_lock);
	if (!isinit) {
		_CITRUS_HASH_INIT(&shared_pool, CI_HASH_SIZE);
		TAILQ_INIT(&shared_unused);
		shared_max_reuse = -1;
		if (!issetugid() && getenv(CI_ENV_MAX_REUSE))
			shared_max_reuse = atoi(getenv(CI_ENV_MAX_REUSE));
		if (shared_max_reuse < 0)
			shared_max_reuse = CI_INITIAL_MAX_REUSE;
		isinit = true;
	}
	UNLOCK(&ci_lock);
}

static __inline void
close_shared(struct _citrus_iconv_shared *ci)
{

	if (ci) {
		if (ci->ci_module) {
			if (ci->ci_ops) {
				if (ci->ci_closure)
					(*ci->ci_ops->io_uninit_shared)(ci);
				free(ci->ci_ops);
			}
			_citrus_unload_module(ci->ci_module);
		}
		free(ci);
	}
}

static __inline int
open_shared(struct _citrus_iconv_shared * __restrict * __restrict rci,
    const char * __restrict convname, const char * __restrict src,
    const char * __restrict dst)
{
	struct _citrus_iconv_shared *ci;
	_citrus_iconv_getops_t getops;
	const char *module;
	size_t len_convname;
	int ret;

#ifdef INCOMPATIBLE_WITH_GNU_ICONV
	/*
	 * Sadly, the gnu tools expect iconv to actually parse the
	 * byte stream and don't allow for a pass-through when
	 * the (src,dest) encodings are the same.
	 * See gettext-0.18.3+ NEWS:
	 *   msgfmt now checks PO file headers more strictly with less
	 *   false-positives.
	 * NetBSD don't do this either.
	 */
	module = (strcmp(src, dst) != 0) ? "iconv_std" : "iconv_none";
#else
	module = "iconv_std";
#endif

	/* initialize iconv handle */
	len_convname = strlen(convname);
	ci = malloc(sizeof(*ci) + len_convname + 1);
	if (!ci) {
		ret = errno;
		goto err;
	}
	ci->ci_module = NULL;
	ci->ci_ops = NULL;
	ci->ci_closure = NULL;
	ci->ci_convname = (void *)&ci[1];
	memcpy(ci->ci_convname, convname, len_convname + 1);

	/* load module */
	ret = _citrus_load_module(&ci->ci_module, module);
	if (ret)
		goto err;

	/* get operators */
	getops = (_citrus_iconv_getops_t)_citrus_find_getops(ci->ci_module,
	    module, "iconv");
	if (!getops) {
		ret = EOPNOTSUPP;
		goto err;
	}
	ci->ci_ops = malloc(sizeof(*ci->ci_ops));
	if (!ci->ci_ops) {
		ret = errno;
		goto err;
	}
	ret = (*getops)(ci->ci_ops);
	if (ret)
		goto err;

	if (ci->ci_ops->io_init_shared == NULL ||
	    ci->ci_ops->io_uninit_shared == NULL ||
	    ci->ci_ops->io_init_context == NULL ||
	    ci->ci_ops->io_uninit_context == NULL ||
	    ci->ci_ops->io_convert == NULL) {
		ret = EINVAL;
		goto err;
	}

	/* initialize the converter */
	ret = (*ci->ci_ops->io_init_shared)(ci, src, dst);
	if (ret)
		goto err;

	*rci = ci;

	return (0);
err:
	close_shared(ci);
	return (ret);
}

static __inline int
hash_func(const char *key)
{

	return (_string_hash_func(key, CI_HASH_SIZE));
}

static __inline int
match_func(struct _citrus_iconv_shared * __restrict ci,
    const char * __restrict key)
{

	return (strcmp(ci->ci_convname, key));
}

static int
get_shared(struct _citrus_iconv_shared * __restrict * __restrict rci,
    const char *src, const char *dst)
{
	struct _citrus_iconv_shared * ci;
	char convname[PATH_MAX];
	int hashval, ret = 0;

	snprintf(convname, sizeof(convname), "%s/%s", src, dst);

	WLOCK(&ci_lock);

	/* lookup alread existing entry */
	hashval = hash_func(convname);
	_CITRUS_HASH_SEARCH(&shared_pool, ci, ci_hash_entry, match_func,
	    convname, hashval);
	if (ci != NULL) {
		/* found */
		if (ci->ci_used_count == 0) {
			TAILQ_REMOVE(&shared_unused, ci, ci_tailq_entry);
			shared_num_unused--;
		}
		ci->ci_used_count++;
		*rci = ci;
		goto quit;
	}

	/* create new entry */
	ret = open_shared(&ci, convname, src, dst);
	if (ret)
		goto quit;

	_CITRUS_HASH_INSERT(&shared_pool, ci, ci_hash_entry, hashval);
	ci->ci_used_count = 1;
	*rci = ci;

quit:
	UNLOCK(&ci_lock);

	return (ret);
}

static void
release_shared(struct _citrus_iconv_shared * __restrict ci)
{

	WLOCK(&ci_lock);
	ci->ci_used_count--;
	if (ci->ci_used_count == 0) {
		/* put it into unused list */
		shared_num_unused++;
		TAILQ_INSERT_TAIL(&shared_unused, ci, ci_tailq_entry);
		/* flood out */
		while (shared_num_unused > shared_max_reuse) {
			ci = TAILQ_FIRST(&shared_unused);
			TAILQ_REMOVE(&shared_unused, ci, ci_tailq_entry);
			_CITRUS_HASH_REMOVE(ci, ci_hash_entry);
			shared_num_unused--;
			close_shared(ci);
		}
	}

	UNLOCK(&ci_lock);
}

/*
 * _citrus_iconv_open:
 *	open a converter for the specified in/out codes.
 */
int
_citrus_iconv_open(struct _citrus_iconv * __restrict * __restrict rcv,
    const char * __restrict src, const char * __restrict dst)
{
	struct _citrus_iconv *cv = NULL;
	struct _citrus_iconv_shared *ci = NULL;
	char realdst[PATH_MAX], realsrc[PATH_MAX];
#ifdef _PATH_ICONV
	char buf[PATH_MAX], path[PATH_MAX];
#endif
	int ret;

	init_cache();

	/* GNU behaviour, using locale encoding if "" or "char" is specified */
	if ((strcmp(src, "") == 0) || (strcmp(src, "char") == 0))
		src = nl_langinfo(CODESET);
	if ((strcmp(dst, "") == 0) || (strcmp(dst, "char") == 0))
		dst = nl_langinfo(CODESET);

	/* resolve codeset name aliases */
#ifdef _PATH_ICONV
	snprintf(path, sizeof(path), "%s/%s", _PATH_ICONV, _CITRUS_ICONV_ALIAS);
	strlcpy(realsrc, _lookup_alias(path, src, buf, (size_t)PATH_MAX,
	    _LOOKUP_CASE_IGNORE), (size_t)PATH_MAX);
	strlcpy(realdst, _lookup_alias(path, dst, buf, (size_t)PATH_MAX,
	    _LOOKUP_CASE_IGNORE), (size_t)PATH_MAX);
#else
	strlcpy(realsrc, src, (size_t)PATH_MAX);
	strlcpy(realdst, dst, (size_t)PATH_MAX);
#endif

	/* sanity check */
	if (strchr(realsrc, '/') != NULL || strchr(realdst, '/'))
		return (EINVAL);

	/* get shared record */
	ret = get_shared(&ci, realsrc, realdst);
	if (ret)
		return (ret);

	/* create/init context */
	if (*rcv == NULL) {
		cv = malloc(sizeof(*cv));
		if (cv == NULL) {
			ret = errno;
			release_shared(ci);
			return (ret);
		}
		*rcv = cv;
	}
	(*rcv)->cv_shared = ci;
	ret = (*ci->ci_ops->io_init_context)(*rcv);
	if (ret) {
		release_shared(ci);
		free(cv);
		return (ret);
	}
	return (0);
}

/*
 * _citrus_iconv_close:
 *	close the specified converter.
 */
void
_citrus_iconv_close(struct _citrus_iconv *cv)
{

	if (cv) {
		(*cv->cv_shared->ci_ops->io_uninit_context)(cv);
		release_shared(cv->cv_shared);
		free(cv);
	}
}

const char
*_citrus_iconv_canonicalize(const char *name)
{
	char *buf;

	if ((buf = calloc((size_t)PATH_MAX, sizeof(*buf))) == NULL)
		return (NULL);
	_citrus_esdb_alias(name, buf, (size_t)PATH_MAX);
	return (buf);
}
