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

#ifndef _MEMSTAT_INTERNAL_H_
#define	_MEMSTAT_INTERNAL_H_

/*
 * memstat maintains its own internal notion of statistics on each memory
 * type, common across UMA and kernel malloc.  Some fields are straight from
 * the allocator statistics, others are derived when extracted from the
 * kernel.  A struct memory_type will describe each type supported by an
 * allocator.  memory_type structures can be chained into lists.
 */
struct memory_type {
	/*
	 * Static properties of type.
	 */
	int	 mt_allocator;		/* malloc(9), uma(9), etc. */
	char	 mt_name[MEMTYPE_MAXNAME];	/* name of memory type. */

	/*
	 * (Relatively) static zone settings, that don't uniquely identify
	 * the zone, but also don't change much.
	 */
	uint64_t	 mt_countlimit;	/* 0, or maximum allocations. */
	uint64_t	 mt_byteslimit;	/* 0, or maximum bytes. */
	uint64_t	 mt_sizemask;	/* malloc: allocated size bitmask. */
	uint64_t	 mt_size;	/* uma: size of objects. */
	uint64_t	 mt_rsize;	/* uma: real size of objects. */

	/*
	 * Zone or type information that includes all caches and any central
	 * zone state.  Depending on the allocator, this may be synthesized
	 * from several sources, or directly measured.
	 */
	uint64_t	 mt_memalloced;	/* Bytes allocated over life time. */
	uint64_t	 mt_memfreed;	/* Bytes freed over life time. */
	uint64_t	 mt_numallocs;	/* Allocations over life time. */
	uint64_t	 mt_numfrees;	/* Frees over life time. */
	uint64_t	 mt_bytes;	/* Bytes currently allocated. */
	uint64_t	 mt_count;	/* Number of current allocations. */
	uint64_t	 mt_free;	/* Number of cached free items. */
	uint64_t	 mt_failures;	/* Number of allocation failures. */
	uint64_t	 mt_sleeps;	/* Number of allocation sleeps. */

	/*
	 * Caller-owned memory.
	 */
	void		*mt_caller_pointer[MEMSTAT_MAXCALLER];	/* Pointers. */
	uint64_t	 mt_caller_uint64[MEMSTAT_MAXCALLER];	/* Integers. */

	/*
	 * For allocators making use of per-CPU caches, we also provide raw
	 * statistics from the central allocator and each per-CPU cache,
	 * which (combined) sometimes make up the above general statistics.
	 *
	 * First, central zone/type state, all numbers excluding any items
	 * cached in per-CPU caches.
	 *
	 * XXXRW: Might be desirable to separately expose allocation stats
	 * from zone, which should (combined with per-cpu) add up to the
	 * global stats above.
	 */
	uint64_t	 mt_zonefree;	/* Free items in zone. */
	uint64_t	 mt_kegfree;	/* Free items in keg. */

	/*
	 * Per-CPU measurements fall into two categories: per-CPU allocation,
	 * and per-CPU cache state.
	 */
	struct mt_percpu_alloc_s {
		uint64_t	 mtp_memalloced;/* Per-CPU mt_memalloced. */
		uint64_t	 mtp_memfreed;	/* Per-CPU mt_memfreed. */
		uint64_t	 mtp_numallocs;	/* Per-CPU mt_numallocs. */
		uint64_t	 mtp_numfrees;	/* Per-CPU mt_numfrees. */
		uint64_t	 mtp_sizemask;	/* Per-CPU mt_sizemask. */
		void		*mtp_caller_pointer[MEMSTAT_MAXCALLER];
		uint64_t	 mtp_caller_uint64[MEMSTAT_MAXCALLER];
	}	*mt_percpu_alloc;

	struct mt_percpu_cache_s {
		uint64_t	 mtp_free;	/* Per-CPU cache free items. */
	}	*mt_percpu_cache;

	LIST_ENTRY(memory_type)	mt_list;	/* List of types. */
};

/*
 * Description of struct memory_type_list is in memstat.h.
 */
struct memory_type_list {
	LIST_HEAD(, memory_type)	mtl_list;
	int				mtl_error;
};

void			 _memstat_mtl_empty(struct memory_type_list *list);
struct memory_type	*_memstat_mt_allocate(struct memory_type_list *list,
			    int allocator, const char *name, int maxcpus);
void			 _memstat_mt_reset_stats(struct memory_type *mtp,
			    int maxcpus);

#endif /* !_MEMSTAT_INTERNAL_H_ */
