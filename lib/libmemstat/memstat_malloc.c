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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <kvm.h>
#include <nlist.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memstat.h"
#include "memstat_internal.h"

static struct nlist namelist[] = {
#define	X_KMEMSTATISTICS	0
	{ .n_name = "_kmemstatistics" },
#define	X_MP_MAXCPUS		1
	{ .n_name = "_mp_maxcpus" },
	{ .n_name = "" },
};

/*
 * Extract malloc(9) statistics from the running kernel, and store all memory
 * type information in the passed list.  For each type, check the list for an
 * existing entry with the right name/allocator -- if present, update that
 * entry.  Otherwise, add a new entry.  On error, the entire list will be
 * cleared, as entries will be in an inconsistent state.
 *
 * To reduce the level of work for a list that starts empty, we keep around a
 * hint as to whether it was empty when we began, so we can avoid searching
 * the list for entries to update.  Updates are O(n^2) due to searching for
 * each entry before adding it.
 */
int
memstat_sysctl_malloc(struct memory_type_list *list, int flags)
{
	struct malloc_type_stream_header *mtshp;
	struct malloc_type_header *mthp;
	struct malloc_type_stats *mtsp;
	struct memory_type *mtp;
	int count, hint_dontsearch, i, j, maxcpus;
	char *buffer, *p;
	size_t size;

	hint_dontsearch = LIST_EMPTY(&list->mtl_list);

	/*
	 * Query the number of CPUs, number of malloc types so that we can
	 * guess an initial buffer size.  We loop until we succeed or really
	 * fail.  Note that the value of maxcpus we query using sysctl is not
	 * the version we use when processing the real data -- that is read
	 * from the header.
	 */
retry:
	size = sizeof(maxcpus);
	if (sysctlbyname("kern.smp.maxcpus", &maxcpus, &size, NULL, 0) < 0) {
		if (errno == EACCES || errno == EPERM)
			list->mtl_error = MEMSTAT_ERROR_PERMISSION;
		else
			list->mtl_error = MEMSTAT_ERROR_DATAERROR;
		return (-1);
	}
	if (size != sizeof(maxcpus)) {
		list->mtl_error = MEMSTAT_ERROR_DATAERROR;
		return (-1);
	}

	size = sizeof(count);
	if (sysctlbyname("kern.malloc_count", &count, &size, NULL, 0) < 0) {
		if (errno == EACCES || errno == EPERM)
			list->mtl_error = MEMSTAT_ERROR_PERMISSION;
		else
			list->mtl_error = MEMSTAT_ERROR_VERSION;
		return (-1);
	}
	if (size != sizeof(count)) {
		list->mtl_error = MEMSTAT_ERROR_DATAERROR;
		return (-1);
	}

	size = sizeof(*mthp) + count * (sizeof(*mthp) + sizeof(*mtsp) *
	    maxcpus);

	buffer = malloc(size);
	if (buffer == NULL) {
		list->mtl_error = MEMSTAT_ERROR_NOMEMORY;
		return (-1);
	}

	if (sysctlbyname("kern.malloc_stats", buffer, &size, NULL, 0) < 0) {
		/*
		 * XXXRW: ENOMEM is an ambiguous return, we should bound the
		 * number of loops, perhaps.
		 */
		if (errno == ENOMEM) {
			free(buffer);
			goto retry;
		}
		if (errno == EACCES || errno == EPERM)
			list->mtl_error = MEMSTAT_ERROR_PERMISSION;
		else
			list->mtl_error = MEMSTAT_ERROR_VERSION;
		free(buffer);
		return (-1);
	}

	if (size == 0) {
		free(buffer);
		return (0);
	}

	if (size < sizeof(*mtshp)) {
		list->mtl_error = MEMSTAT_ERROR_VERSION;
		free(buffer);
		return (-1);
	}
	p = buffer;
	mtshp = (struct malloc_type_stream_header *)p;
	p += sizeof(*mtshp);

	if (mtshp->mtsh_version != MALLOC_TYPE_STREAM_VERSION) {
		list->mtl_error = MEMSTAT_ERROR_VERSION;
		free(buffer);
		return (-1);
	}

	/*
	 * For the remainder of this function, we are quite trusting about
	 * the layout of structures and sizes, since we've determined we have
	 * a matching version and acceptable CPU count.
	 */
	maxcpus = mtshp->mtsh_maxcpus;
	count = mtshp->mtsh_count;
	for (i = 0; i < count; i++) {
		mthp = (struct malloc_type_header *)p;
		p += sizeof(*mthp);

		if (hint_dontsearch == 0) {
			mtp = memstat_mtl_find(list, ALLOCATOR_MALLOC,
			    mthp->mth_name);
		} else
			mtp = NULL;
		if (mtp == NULL)
			mtp = _memstat_mt_allocate(list, ALLOCATOR_MALLOC,
			    mthp->mth_name, maxcpus);
		if (mtp == NULL) {
			_memstat_mtl_empty(list);
			free(buffer);
			list->mtl_error = MEMSTAT_ERROR_NOMEMORY;
			return (-1);
		}

		/*
		 * Reset the statistics on a current node.
		 */
		_memstat_mt_reset_stats(mtp, maxcpus);

		for (j = 0; j < maxcpus; j++) {
			mtsp = (struct malloc_type_stats *)p;
			p += sizeof(*mtsp);

			/*
			 * Sumarize raw statistics across CPUs into coalesced
			 * statistics.
			 */
			mtp->mt_memalloced += mtsp->mts_memalloced;
			mtp->mt_memfreed += mtsp->mts_memfreed;
			mtp->mt_numallocs += mtsp->mts_numallocs;
			mtp->mt_numfrees += mtsp->mts_numfrees;
			mtp->mt_sizemask |= mtsp->mts_size;

			/*
			 * Copies of per-CPU statistics.
			 */
			mtp->mt_percpu_alloc[j].mtp_memalloced =
			    mtsp->mts_memalloced;
			mtp->mt_percpu_alloc[j].mtp_memfreed =
			    mtsp->mts_memfreed;
			mtp->mt_percpu_alloc[j].mtp_numallocs =
			    mtsp->mts_numallocs;
			mtp->mt_percpu_alloc[j].mtp_numfrees =
			    mtsp->mts_numfrees;
			mtp->mt_percpu_alloc[j].mtp_sizemask =
			    mtsp->mts_size;
		}

		/*
		 * Derived cross-CPU statistics.
		 */
		mtp->mt_bytes = mtp->mt_memalloced - mtp->mt_memfreed;
		mtp->mt_count = mtp->mt_numallocs - mtp->mt_numfrees;
	}

	free(buffer);

	return (0);
}

static int
kread(kvm_t *kvm, void *kvm_pointer, void *address, size_t size,
    size_t offset)
{
	ssize_t ret;

	ret = kvm_read(kvm, (unsigned long)kvm_pointer + offset, address,
	    size);
	if (ret < 0)
		return (MEMSTAT_ERROR_KVM);
	if ((size_t)ret != size)
		return (MEMSTAT_ERROR_KVM_SHORTREAD);
	return (0);
}

static int
kread_string(kvm_t *kvm, const void *kvm_pointer, char *buffer, int buflen)
{
	ssize_t ret;
	int i;

	for (i = 0; i < buflen; i++) {
		ret = kvm_read(kvm, __DECONST(unsigned long, kvm_pointer) +
		    i, &(buffer[i]), sizeof(char));
		if (ret < 0)
			return (MEMSTAT_ERROR_KVM);
		if ((size_t)ret != sizeof(char))
			return (MEMSTAT_ERROR_KVM_SHORTREAD);
		if (buffer[i] == '\0')
			return (0);
	}
	/* Truncate. */
	buffer[i-1] = '\0';
	return (0);
}

static int
kread_symbol(kvm_t *kvm, int index, void *address, size_t size,
    size_t offset)
{
	ssize_t ret;

	ret = kvm_read(kvm, namelist[index].n_value + offset, address, size);
	if (ret < 0)
		return (MEMSTAT_ERROR_KVM);
	if ((size_t)ret != size)
		return (MEMSTAT_ERROR_KVM_SHORTREAD);
	return (0);
}

static int
kread_zpcpu(kvm_t *kvm, u_long base, void *buf, size_t size, int cpu)
{
	ssize_t ret;

	ret = kvm_read_zpcpu(kvm, base, buf, size, cpu);
	if (ret < 0)
		return (MEMSTAT_ERROR_KVM);
	if ((size_t)ret != size)
		return (MEMSTAT_ERROR_KVM_SHORTREAD);
	return (0);
}

int
memstat_kvm_malloc(struct memory_type_list *list, void *kvm_handle)
{
	struct memory_type *mtp;
	void *kmemstatistics;
	int hint_dontsearch, j, mp_maxcpus, mp_ncpus, ret;
	char name[MEMTYPE_MAXNAME];
	struct malloc_type_stats mts;
	struct malloc_type_internal mti, *mtip;
	struct malloc_type type, *typep;
	kvm_t *kvm;

	kvm = (kvm_t *)kvm_handle;

	hint_dontsearch = LIST_EMPTY(&list->mtl_list);

	if (kvm_nlist(kvm, namelist) != 0) {
		list->mtl_error = MEMSTAT_ERROR_KVM;
		return (-1);
	}

	if (namelist[X_KMEMSTATISTICS].n_type == 0 ||
	    namelist[X_KMEMSTATISTICS].n_value == 0) {
		list->mtl_error = MEMSTAT_ERROR_KVM_NOSYMBOL;
		return (-1);
	}

	ret = kread_symbol(kvm, X_MP_MAXCPUS, &mp_maxcpus,
	    sizeof(mp_maxcpus), 0);
	if (ret != 0) {
		list->mtl_error = ret;
		return (-1);
	}

	ret = kread_symbol(kvm, X_KMEMSTATISTICS, &kmemstatistics,
	    sizeof(kmemstatistics), 0);
	if (ret != 0) {
		list->mtl_error = ret;
		return (-1);
	}

	mp_ncpus = kvm_getncpus(kvm);

	for (typep = kmemstatistics; typep != NULL; typep = type.ks_next) {
		ret = kread(kvm, typep, &type, sizeof(type), 0);
		if (ret != 0) {
			_memstat_mtl_empty(list);
			list->mtl_error = ret;
			return (-1);
		}
		ret = kread_string(kvm, (void *)type.ks_shortdesc, name,
		    MEMTYPE_MAXNAME);
		if (ret != 0) {
			_memstat_mtl_empty(list);
			list->mtl_error = ret;
			return (-1);
		}

		/*
		 * Since our compile-time value for MAXCPU may differ from the
		 * kernel's, we populate our own array.
		 */
		mtip = type.ks_handle;
		ret = kread(kvm, mtip, &mti, sizeof(mti), 0);
		if (ret != 0) {
			_memstat_mtl_empty(list);
			list->mtl_error = ret;
			return (-1);
		}

		if (hint_dontsearch == 0) {
			mtp = memstat_mtl_find(list, ALLOCATOR_MALLOC, name);
		} else
			mtp = NULL;
		if (mtp == NULL)
			mtp = _memstat_mt_allocate(list, ALLOCATOR_MALLOC,
			    name, mp_maxcpus);
		if (mtp == NULL) {
			_memstat_mtl_empty(list);
			list->mtl_error = MEMSTAT_ERROR_NOMEMORY;
			return (-1);
		}

		/*
		 * This logic is replicated from kern_malloc.c, and should
		 * be kept in sync.
		 */
		_memstat_mt_reset_stats(mtp, mp_maxcpus);
		for (j = 0; j < mp_ncpus; j++) {
			ret = kread_zpcpu(kvm, (u_long)mti.mti_stats, &mts,
			    sizeof(mts), j);
			if (ret != 0) {
				_memstat_mtl_empty(list);
				list->mtl_error = ret;
				return (-1);
			}
			mtp->mt_memalloced += mts.mts_memalloced;
			mtp->mt_memfreed += mts.mts_memfreed;
			mtp->mt_numallocs += mts.mts_numallocs;
			mtp->mt_numfrees += mts.mts_numfrees;
			mtp->mt_sizemask |= mts.mts_size;

			mtp->mt_percpu_alloc[j].mtp_memalloced =
			    mts.mts_memalloced;
			mtp->mt_percpu_alloc[j].mtp_memfreed =
			    mts.mts_memfreed;
			mtp->mt_percpu_alloc[j].mtp_numallocs =
			    mts.mts_numallocs;
			mtp->mt_percpu_alloc[j].mtp_numfrees =
			    mts.mts_numfrees;
			mtp->mt_percpu_alloc[j].mtp_sizemask =
			    mts.mts_size;
		}
		for (; j < mp_maxcpus; j++) {
			bzero(&mtp->mt_percpu_alloc[j],
			    sizeof(mtp->mt_percpu_alloc[0]));
		}

		mtp->mt_bytes = mtp->mt_memalloced - mtp->mt_memfreed;
		mtp->mt_count = mtp->mt_numallocs - mtp->mt_numfrees;
	}

	return (0);
}
