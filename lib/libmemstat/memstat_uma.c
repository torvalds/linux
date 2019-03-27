/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2006 Robert N. M. Watson
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
#include <sys/counter.h>
#include <sys/cpuset.h>
#include <sys/sysctl.h>

#include <vm/uma.h>
#include <vm/uma_int.h>

#include <err.h>
#include <errno.h>
#include <kvm.h>
#include <nlist.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memstat.h"
#include "memstat_internal.h"

static struct nlist namelist[] = {
#define	X_UMA_KEGS	0
	{ .n_name = "_uma_kegs" },
#define	X_MP_MAXID	1
	{ .n_name = "_mp_maxid" },
#define	X_ALL_CPUS	2
	{ .n_name = "_all_cpus" },
#define	X_VM_NDOMAINS	3
	{ .n_name = "_vm_ndomains" },
	{ .n_name = "" },
};

/*
 * Extract uma(9) statistics from the running kernel, and store all memory
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
memstat_sysctl_uma(struct memory_type_list *list, int flags)
{
	struct uma_stream_header *ushp;
	struct uma_type_header *uthp;
	struct uma_percpu_stat *upsp;
	struct memory_type *mtp;
	int count, hint_dontsearch, i, j, maxcpus, maxid;
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
	size = sizeof(maxid);
	if (sysctlbyname("kern.smp.maxid", &maxid, &size, NULL, 0) < 0) {
		if (errno == EACCES || errno == EPERM)
			list->mtl_error = MEMSTAT_ERROR_PERMISSION;
		else
			list->mtl_error = MEMSTAT_ERROR_DATAERROR;
		return (-1);
	}
	if (size != sizeof(maxid)) {
		list->mtl_error = MEMSTAT_ERROR_DATAERROR;
		return (-1);
	}

	size = sizeof(count);
	if (sysctlbyname("vm.zone_count", &count, &size, NULL, 0) < 0) {
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

	size = sizeof(*uthp) + count * (sizeof(*uthp) + sizeof(*upsp) *
	    (maxid + 1));

	buffer = malloc(size);
	if (buffer == NULL) {
		list->mtl_error = MEMSTAT_ERROR_NOMEMORY;
		return (-1);
	}

	if (sysctlbyname("vm.zone_stats", buffer, &size, NULL, 0) < 0) {
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

	if (size < sizeof(*ushp)) {
		list->mtl_error = MEMSTAT_ERROR_VERSION;
		free(buffer);
		return (-1);
	}
	p = buffer;
	ushp = (struct uma_stream_header *)p;
	p += sizeof(*ushp);

	if (ushp->ush_version != UMA_STREAM_VERSION) {
		list->mtl_error = MEMSTAT_ERROR_VERSION;
		free(buffer);
		return (-1);
	}

	/*
	 * For the remainder of this function, we are quite trusting about
	 * the layout of structures and sizes, since we've determined we have
	 * a matching version and acceptable CPU count.
	 */
	maxcpus = ushp->ush_maxcpus;
	count = ushp->ush_count;
	for (i = 0; i < count; i++) {
		uthp = (struct uma_type_header *)p;
		p += sizeof(*uthp);

		if (hint_dontsearch == 0) {
			mtp = memstat_mtl_find(list, ALLOCATOR_UMA,
			    uthp->uth_name);
		} else
			mtp = NULL;
		if (mtp == NULL)
			mtp = _memstat_mt_allocate(list, ALLOCATOR_UMA,
			    uthp->uth_name, maxid + 1);
		if (mtp == NULL) {
			_memstat_mtl_empty(list);
			free(buffer);
			list->mtl_error = MEMSTAT_ERROR_NOMEMORY;
			return (-1);
		}

		/*
		 * Reset the statistics on a current node.
		 */
		_memstat_mt_reset_stats(mtp, maxid + 1);

		mtp->mt_numallocs = uthp->uth_allocs;
		mtp->mt_numfrees = uthp->uth_frees;
		mtp->mt_failures = uthp->uth_fails;
		mtp->mt_sleeps = uthp->uth_sleeps;

		for (j = 0; j < maxcpus; j++) {
			upsp = (struct uma_percpu_stat *)p;
			p += sizeof(*upsp);

			mtp->mt_percpu_cache[j].mtp_free =
			    upsp->ups_cache_free;
			mtp->mt_free += upsp->ups_cache_free;
			mtp->mt_numallocs += upsp->ups_allocs;
			mtp->mt_numfrees += upsp->ups_frees;
		}

		/*
		 * Values for uth_allocs and uth_frees frees are snap.
		 * It may happen that kernel reports that number of frees
		 * is greater than number of allocs. See counter(9) for
		 * details.
		 */
		if (mtp->mt_numallocs < mtp->mt_numfrees)
			mtp->mt_numallocs = mtp->mt_numfrees;

		mtp->mt_size = uthp->uth_size;
		mtp->mt_rsize = uthp->uth_rsize;
		mtp->mt_memalloced = mtp->mt_numallocs * uthp->uth_size;
		mtp->mt_memfreed = mtp->mt_numfrees * uthp->uth_size;
		mtp->mt_bytes = mtp->mt_memalloced - mtp->mt_memfreed;
		mtp->mt_countlimit = uthp->uth_limit;
		mtp->mt_byteslimit = uthp->uth_limit * uthp->uth_size;

		mtp->mt_count = mtp->mt_numallocs - mtp->mt_numfrees;
		mtp->mt_zonefree = uthp->uth_zone_free;

		/*
		 * UMA secondary zones share a keg with the primary zone.  To
		 * avoid double-reporting of free items, report keg free
		 * items only in the primary zone.
		 */
		if (!(uthp->uth_zone_flags & UTH_ZONE_SECONDARY)) {
			mtp->mt_kegfree = uthp->uth_keg_free;
			mtp->mt_free += mtp->mt_kegfree;
		}
		mtp->mt_free += mtp->mt_zonefree;
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
		ret = kvm_read(kvm, (unsigned long)kvm_pointer + i,
		    &(buffer[i]), sizeof(char));
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

/*
 * memstat_kvm_uma() is similar to memstat_sysctl_uma(), only it extracts
 * UMA(9) statistics from a kernel core/memory file.
 */
int
memstat_kvm_uma(struct memory_type_list *list, void *kvm_handle)
{
	LIST_HEAD(, uma_keg) uma_kegs;
	struct memory_type *mtp;
	struct uma_zone_domain uzd;
	struct uma_bucket *ubp, ub;
	struct uma_cache *ucp, *ucp_array;
	struct uma_zone *uzp, uz;
	struct uma_keg *kzp, kz;
	int hint_dontsearch, i, mp_maxid, ndomains, ret;
	char name[MEMTYPE_MAXNAME];
	cpuset_t all_cpus;
	long cpusetsize;
	kvm_t *kvm;

	kvm = (kvm_t *)kvm_handle;
	hint_dontsearch = LIST_EMPTY(&list->mtl_list);
	if (kvm_nlist(kvm, namelist) != 0) {
		list->mtl_error = MEMSTAT_ERROR_KVM;
		return (-1);
	}
	if (namelist[X_UMA_KEGS].n_type == 0 ||
	    namelist[X_UMA_KEGS].n_value == 0) {
		list->mtl_error = MEMSTAT_ERROR_KVM_NOSYMBOL;
		return (-1);
	}
	ret = kread_symbol(kvm, X_MP_MAXID, &mp_maxid, sizeof(mp_maxid), 0);
	if (ret != 0) {
		list->mtl_error = ret;
		return (-1);
	}
	ret = kread_symbol(kvm, X_VM_NDOMAINS, &ndomains,
	    sizeof(ndomains), 0);
	if (ret != 0) {
		list->mtl_error = ret;
		return (-1);
	}
	ret = kread_symbol(kvm, X_UMA_KEGS, &uma_kegs, sizeof(uma_kegs), 0);
	if (ret != 0) {
		list->mtl_error = ret;
		return (-1);
	}
	cpusetsize = sysconf(_SC_CPUSET_SIZE);
	if (cpusetsize == -1 || (u_long)cpusetsize > sizeof(cpuset_t)) {
		list->mtl_error = MEMSTAT_ERROR_KVM_NOSYMBOL;
		return (-1);
	}
	CPU_ZERO(&all_cpus);
	ret = kread_symbol(kvm, X_ALL_CPUS, &all_cpus, cpusetsize, 0);
	if (ret != 0) {
		list->mtl_error = ret;
		return (-1);
	}
	ucp_array = malloc(sizeof(struct uma_cache) * (mp_maxid + 1));
	if (ucp_array == NULL) {
		list->mtl_error = MEMSTAT_ERROR_NOMEMORY;
		return (-1);
	}
	for (kzp = LIST_FIRST(&uma_kegs); kzp != NULL; kzp =
	    LIST_NEXT(&kz, uk_link)) {
		ret = kread(kvm, kzp, &kz, sizeof(kz), 0);
		if (ret != 0) {
			free(ucp_array);
			_memstat_mtl_empty(list);
			list->mtl_error = ret;
			return (-1);
		}
		for (uzp = LIST_FIRST(&kz.uk_zones); uzp != NULL; uzp =
		    LIST_NEXT(&uz, uz_link)) {
			ret = kread(kvm, uzp, &uz, sizeof(uz), 0);
			if (ret != 0) {
				free(ucp_array);
				_memstat_mtl_empty(list);
				list->mtl_error = ret;
				return (-1);
			}
			ret = kread(kvm, uzp, ucp_array,
			    sizeof(struct uma_cache) * (mp_maxid + 1),
			    offsetof(struct uma_zone, uz_cpu[0]));
			if (ret != 0) {
				free(ucp_array);
				_memstat_mtl_empty(list);
				list->mtl_error = ret;
				return (-1);
			}
			ret = kread_string(kvm, uz.uz_name, name,
			    MEMTYPE_MAXNAME);
			if (ret != 0) {
				free(ucp_array);
				_memstat_mtl_empty(list);
				list->mtl_error = ret;
				return (-1);
			}
			if (hint_dontsearch == 0) {
				mtp = memstat_mtl_find(list, ALLOCATOR_UMA,
				    name);
			} else
				mtp = NULL;
			if (mtp == NULL)
				mtp = _memstat_mt_allocate(list, ALLOCATOR_UMA,
				    name, mp_maxid + 1);
			if (mtp == NULL) {
				free(ucp_array);
				_memstat_mtl_empty(list);
				list->mtl_error = MEMSTAT_ERROR_NOMEMORY;
				return (-1);
			}
			/*
			 * Reset the statistics on a current node.
			 */
			_memstat_mt_reset_stats(mtp, mp_maxid + 1);
			mtp->mt_numallocs = kvm_counter_u64_fetch(kvm,
			    (unsigned long )uz.uz_allocs);
			mtp->mt_numfrees = kvm_counter_u64_fetch(kvm,
			    (unsigned long )uz.uz_frees);
			mtp->mt_failures = kvm_counter_u64_fetch(kvm,
			    (unsigned long )uz.uz_fails);
			mtp->mt_sleeps = uz.uz_sleeps;
			if (kz.uk_flags & UMA_ZFLAG_INTERNAL)
				goto skip_percpu;
			for (i = 0; i < mp_maxid + 1; i++) {
				if (!CPU_ISSET(i, &all_cpus))
					continue;
				ucp = &ucp_array[i];
				mtp->mt_numallocs += ucp->uc_allocs;
				mtp->mt_numfrees += ucp->uc_frees;

				if (ucp->uc_allocbucket != NULL) {
					ret = kread(kvm, ucp->uc_allocbucket,
					    &ub, sizeof(ub), 0);
					if (ret != 0) {
						free(ucp_array);
						_memstat_mtl_empty(list);
						list->mtl_error = ret;
						return (-1);
					}
					mtp->mt_free += ub.ub_cnt;
				}
				if (ucp->uc_freebucket != NULL) {
					ret = kread(kvm, ucp->uc_freebucket,
					    &ub, sizeof(ub), 0);
					if (ret != 0) {
						free(ucp_array);
						_memstat_mtl_empty(list);
						list->mtl_error = ret;
						return (-1);
					}
					mtp->mt_free += ub.ub_cnt;
				}
			}
skip_percpu:
			mtp->mt_size = kz.uk_size;
			mtp->mt_rsize = kz.uk_rsize;
			mtp->mt_memalloced = mtp->mt_numallocs * mtp->mt_size;
			mtp->mt_memfreed = mtp->mt_numfrees * mtp->mt_size;
			mtp->mt_bytes = mtp->mt_memalloced - mtp->mt_memfreed;
			mtp->mt_countlimit = uz.uz_max_items;
			mtp->mt_byteslimit = mtp->mt_countlimit * mtp->mt_size;
			mtp->mt_count = mtp->mt_numallocs - mtp->mt_numfrees;
			for (i = 0; i < ndomains; i++) {
				ret = kread(kvm, &uz.uz_domain[i], &uzd,
				   sizeof(uzd), 0);
				for (ubp =
				    LIST_FIRST(&uzd.uzd_buckets);
				    ubp != NULL;
				    ubp = LIST_NEXT(&ub, ub_link)) {
					ret = kread(kvm, ubp, &ub,
					   sizeof(ub), 0);
					mtp->mt_zonefree += ub.ub_cnt;
				}
			}
			if (!((kz.uk_flags & UMA_ZONE_SECONDARY) &&
			    LIST_FIRST(&kz.uk_zones) != uzp)) {
				mtp->mt_kegfree = kz.uk_free;
				mtp->mt_free += mtp->mt_kegfree;
			}
			mtp->mt_free += mtp->mt_zonefree;
		}
	}
	free(ucp_array);
	return (0);
}
