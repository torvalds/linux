/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Robert N. M. Watson
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memstat.h"
#include "memstat_internal.h"

const char *
memstat_strerror(int error)
{

	switch (error) {
	case MEMSTAT_ERROR_NOMEMORY:
		return ("Cannot allocate memory");
	case MEMSTAT_ERROR_VERSION:
		return ("Version mismatch");
	case MEMSTAT_ERROR_PERMISSION:
		return ("Permission denied");
	case MEMSTAT_ERROR_DATAERROR:
		return ("Data format error");
	case MEMSTAT_ERROR_KVM:
		return ("KVM error");
	case MEMSTAT_ERROR_KVM_NOSYMBOL:
		return ("KVM unable to find symbol");
	case MEMSTAT_ERROR_KVM_SHORTREAD:
		return ("KVM short read");
	case MEMSTAT_ERROR_UNDEFINED:
	default:
		return ("Unknown error");
	}
}

struct memory_type_list *
memstat_mtl_alloc(void)
{
	struct memory_type_list *mtlp;

	mtlp = malloc(sizeof(*mtlp));
	if (mtlp == NULL)
		return (NULL);

	LIST_INIT(&mtlp->mtl_list);
	mtlp->mtl_error = MEMSTAT_ERROR_UNDEFINED;
	return (mtlp);
}

struct memory_type *
memstat_mtl_first(struct memory_type_list *list)
{

	return (LIST_FIRST(&list->mtl_list));
}

struct memory_type *
memstat_mtl_next(struct memory_type *mtp)
{

	return (LIST_NEXT(mtp, mt_list));
}

void
_memstat_mtl_empty(struct memory_type_list *list)
{
	struct memory_type *mtp;

	while ((mtp = LIST_FIRST(&list->mtl_list))) {
		free(mtp->mt_percpu_alloc);
		free(mtp->mt_percpu_cache);
		LIST_REMOVE(mtp, mt_list);
		free(mtp);
	}
}

void
memstat_mtl_free(struct memory_type_list *list)
{

	_memstat_mtl_empty(list);
	free(list);
}

int
memstat_mtl_geterror(struct memory_type_list *list)
{

	return (list->mtl_error);
}

/*
 * Look for an existing memory_type entry in a memory_type list, based on the
 * allocator and name of the type.  If not found, return NULL.  No errno or
 * memstat error.
 */
struct memory_type *
memstat_mtl_find(struct memory_type_list *list, int allocator,
    const char *name)
{
	struct memory_type *mtp;

	LIST_FOREACH(mtp, &list->mtl_list, mt_list) {
		if ((mtp->mt_allocator == allocator ||
		    allocator == ALLOCATOR_ANY) &&
		    strcmp(mtp->mt_name, name) == 0)
			return (mtp);
	}
	return (NULL);
}

/*
 * Allocate a new memory_type with the specificed allocator type and name,
 * then insert into the list.  The structure will be zero'd.
 *
 * libmemstat(3) internal function.
 */
struct memory_type *
_memstat_mt_allocate(struct memory_type_list *list, int allocator,
    const char *name, int maxcpus)
{
	struct memory_type *mtp;

	mtp = malloc(sizeof(*mtp));
	if (mtp == NULL)
		return (NULL);

	bzero(mtp, sizeof(*mtp));

	mtp->mt_allocator = allocator;
	mtp->mt_percpu_alloc = malloc(sizeof(struct mt_percpu_alloc_s) *
	    maxcpus);
	mtp->mt_percpu_cache = malloc(sizeof(struct mt_percpu_cache_s) *
	    maxcpus);
	strlcpy(mtp->mt_name, name, MEMTYPE_MAXNAME);
	LIST_INSERT_HEAD(&list->mtl_list, mtp, mt_list);
	return (mtp);
}

/*
 * Reset any libmemstat(3)-owned statistics in a memory_type record so that
 * it can be reused without incremental addition problems.  Caller-owned
 * memory is left "as-is", and must be updated by the caller if desired.
 *
 * libmemstat(3) internal function.
 */
void
_memstat_mt_reset_stats(struct memory_type *mtp, int maxcpus)
{
	int i;

	mtp->mt_countlimit = 0;
	mtp->mt_byteslimit = 0;
	mtp->mt_sizemask = 0;
	mtp->mt_size = 0;

	mtp->mt_memalloced = 0;
	mtp->mt_memfreed = 0;
	mtp->mt_numallocs = 0;
	mtp->mt_numfrees = 0;
	mtp->mt_bytes = 0;
	mtp->mt_count = 0;
	mtp->mt_free = 0;
	mtp->mt_failures = 0;
	mtp->mt_sleeps = 0;

	mtp->mt_zonefree = 0;
	mtp->mt_kegfree = 0;

	for (i = 0; i < maxcpus; i++) {
		mtp->mt_percpu_alloc[i].mtp_memalloced = 0;
		mtp->mt_percpu_alloc[i].mtp_memfreed = 0;
		mtp->mt_percpu_alloc[i].mtp_numallocs = 0;
		mtp->mt_percpu_alloc[i].mtp_numfrees = 0;
		mtp->mt_percpu_alloc[i].mtp_sizemask = 0;
		mtp->mt_percpu_cache[i].mtp_free = 0;
	}
}

/*
 * Accessor methods for struct memory_type.  Avoids encoding the structure
 * ABI into the application.
 */
const char *
memstat_get_name(const struct memory_type *mtp)
{

	return (mtp->mt_name);
}

int
memstat_get_allocator(const struct memory_type *mtp)
{

	return (mtp->mt_allocator);
}

uint64_t
memstat_get_countlimit(const struct memory_type *mtp)
{

	return (mtp->mt_countlimit);
}

uint64_t
memstat_get_byteslimit(const struct memory_type *mtp)
{

	return (mtp->mt_byteslimit);
}

uint64_t
memstat_get_sizemask(const struct memory_type *mtp)
{

	return (mtp->mt_sizemask);
}

uint64_t
memstat_get_size(const struct memory_type *mtp)
{

	return (mtp->mt_size);
}

uint64_t
memstat_get_rsize(const struct memory_type *mtp)
{

	return (mtp->mt_rsize);
}

uint64_t
memstat_get_memalloced(const struct memory_type *mtp)
{

	return (mtp->mt_memalloced);
}

uint64_t
memstat_get_memfreed(const struct memory_type *mtp)
{

	return (mtp->mt_memfreed);
}

uint64_t
memstat_get_numallocs(const struct memory_type *mtp)
{

	return (mtp->mt_numallocs);
}

uint64_t
memstat_get_numfrees(const struct memory_type *mtp)
{

	return (mtp->mt_numfrees);
}

uint64_t
memstat_get_bytes(const struct memory_type *mtp)
{

	return (mtp->mt_bytes);
}

uint64_t
memstat_get_count(const struct memory_type *mtp)
{

	return (mtp->mt_count);
}

uint64_t
memstat_get_free(const struct memory_type *mtp)
{

	return (mtp->mt_free);
}

uint64_t
memstat_get_failures(const struct memory_type *mtp)
{

	return (mtp->mt_failures);
}

uint64_t
memstat_get_sleeps(const struct memory_type *mtp)
{

	return (mtp->mt_sleeps);
}

void *
memstat_get_caller_pointer(const struct memory_type *mtp, int index)
{

	return (mtp->mt_caller_pointer[index]);
}

void
memstat_set_caller_pointer(struct memory_type *mtp, int index, void *value)
{

	mtp->mt_caller_pointer[index] = value;
}

uint64_t
memstat_get_caller_uint64(const struct memory_type *mtp, int index)
{

	return (mtp->mt_caller_uint64[index]);
}

void
memstat_set_caller_uint64(struct memory_type *mtp, int index, uint64_t value)
{

	mtp->mt_caller_uint64[index] = value;
}

uint64_t
memstat_get_zonefree(const struct memory_type *mtp)
{

	return (mtp->mt_zonefree);
}

uint64_t
memstat_get_kegfree(const struct memory_type *mtp)
{

	return (mtp->mt_kegfree);
}

uint64_t
memstat_get_percpu_memalloced(const struct memory_type *mtp, int cpu)
{

	return (mtp->mt_percpu_alloc[cpu].mtp_memalloced);
}

uint64_t
memstat_get_percpu_memfreed(const struct memory_type *mtp, int cpu)
{

	return (mtp->mt_percpu_alloc[cpu].mtp_memfreed);
}

uint64_t
memstat_get_percpu_numallocs(const struct memory_type *mtp, int cpu)
{

	return (mtp->mt_percpu_alloc[cpu].mtp_numallocs);
}

uint64_t
memstat_get_percpu_numfrees(const struct memory_type *mtp, int cpu)
{

	return (mtp->mt_percpu_alloc[cpu].mtp_numfrees);
}

uint64_t
memstat_get_percpu_sizemask(const struct memory_type *mtp, int cpu)
{

	return (mtp->mt_percpu_alloc[cpu].mtp_sizemask);
}

void *
memstat_get_percpu_caller_pointer(const struct memory_type *mtp, int cpu,
    int index)
{

	return (mtp->mt_percpu_alloc[cpu].mtp_caller_pointer[index]);
}

void
memstat_set_percpu_caller_pointer(struct memory_type *mtp, int cpu,
    int index, void *value)
{

	mtp->mt_percpu_alloc[cpu].mtp_caller_pointer[index] = value;
}

uint64_t
memstat_get_percpu_caller_uint64(const struct memory_type *mtp, int cpu,
    int index)
{

	return (mtp->mt_percpu_alloc[cpu].mtp_caller_uint64[index]);
}

void
memstat_set_percpu_caller_uint64(struct memory_type *mtp, int cpu, int index,
    uint64_t value)
{

	mtp->mt_percpu_alloc[cpu].mtp_caller_uint64[index] = value;
}

uint64_t
memstat_get_percpu_free(const struct memory_type *mtp, int cpu)
{

	return (mtp->mt_percpu_cache[cpu].mtp_free);
}
