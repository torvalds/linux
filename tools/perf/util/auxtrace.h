/*
 * auxtrace.h: AUX area trace support
 * Copyright (c) 2013-2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __PERF_AUXTRACE_H
#define __PERF_AUXTRACE_H

#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <linux/perf_event.h>
#include <linux/types.h>

#include "../perf.h"

union perf_event;
struct perf_session;
struct perf_evlist;
struct perf_tool;
struct record_opts;
struct auxtrace_info_event;

/**
 * struct auxtrace_mmap - records an mmap of the auxtrace buffer.
 * @base: address of mapped area
 * @userpg: pointer to buffer's perf_event_mmap_page
 * @mask: %0 if @len is not a power of two, otherwise (@len - %1)
 * @len: size of mapped area
 * @prev: previous aux_head
 * @idx: index of this mmap
 * @tid: tid for a per-thread mmap (also set if there is only 1 tid on a per-cpu
 *       mmap) otherwise %0
 * @cpu: cpu number for a per-cpu mmap otherwise %-1
 */
struct auxtrace_mmap {
	void		*base;
	void		*userpg;
	size_t		mask;
	size_t		len;
	u64		prev;
	int		idx;
	pid_t		tid;
	int		cpu;
};

/**
 * struct auxtrace_mmap_params - parameters to set up struct auxtrace_mmap.
 * @mask: %0 if @len is not a power of two, otherwise (@len - %1)
 * @offset: file offset of mapped area
 * @len: size of mapped area
 * @prot: mmap memory protection
 * @idx: index of this mmap
 * @tid: tid for a per-thread mmap (also set if there is only 1 tid on a per-cpu
 *       mmap) otherwise %0
 * @cpu: cpu number for a per-cpu mmap otherwise %-1
 */
struct auxtrace_mmap_params {
	size_t		mask;
	off_t		offset;
	size_t		len;
	int		prot;
	int		idx;
	pid_t		tid;
	int		cpu;
};

/**
 * struct auxtrace_record - callbacks for recording AUX area data.
 * @recording_options: validate and process recording options
 * @info_priv_size: return the size of the private data in auxtrace_info_event
 * @info_fill: fill-in the private data in auxtrace_info_event
 * @free: free this auxtrace record structure
 * @reference: provide a 64-bit reference number for auxtrace_event
 * @read_finish: called after reading from an auxtrace mmap
 */
struct auxtrace_record {
	int (*recording_options)(struct auxtrace_record *itr,
				 struct perf_evlist *evlist,
				 struct record_opts *opts);
	size_t (*info_priv_size)(struct auxtrace_record *itr);
	int (*info_fill)(struct auxtrace_record *itr,
			 struct perf_session *session,
			 struct auxtrace_info_event *auxtrace_info,
			 size_t priv_size);
	void (*free)(struct auxtrace_record *itr);
	u64 (*reference)(struct auxtrace_record *itr);
	int (*read_finish)(struct auxtrace_record *itr, int idx);
};

static inline u64 auxtrace_mmap__read_head(struct auxtrace_mmap *mm)
{
	struct perf_event_mmap_page *pc = mm->userpg;
#if BITS_PER_LONG == 64 || !defined(HAVE_SYNC_COMPARE_AND_SWAP_SUPPORT)
	u64 head = ACCESS_ONCE(pc->aux_head);
#else
	u64 head = __sync_val_compare_and_swap(&pc->aux_head, 0, 0);
#endif

	/* Ensure all reads are done after we read the head */
	rmb();
	return head;
}

static inline void auxtrace_mmap__write_tail(struct auxtrace_mmap *mm, u64 tail)
{
	struct perf_event_mmap_page *pc = mm->userpg;
#if BITS_PER_LONG != 64 && defined(HAVE_SYNC_COMPARE_AND_SWAP_SUPPORT)
	u64 old_tail;
#endif

	/* Ensure all reads are done before we write the tail out */
	mb();
#if BITS_PER_LONG == 64 || !defined(HAVE_SYNC_COMPARE_AND_SWAP_SUPPORT)
	pc->aux_tail = tail;
#else
	do {
		old_tail = __sync_val_compare_and_swap(&pc->aux_tail, 0, 0);
	} while (!__sync_bool_compare_and_swap(&pc->aux_tail, old_tail, tail));
#endif
}

int auxtrace_mmap__mmap(struct auxtrace_mmap *mm,
			struct auxtrace_mmap_params *mp,
			void *userpg, int fd);
void auxtrace_mmap__munmap(struct auxtrace_mmap *mm);
void auxtrace_mmap_params__init(struct auxtrace_mmap_params *mp,
				off_t auxtrace_offset,
				unsigned int auxtrace_pages,
				bool auxtrace_overwrite);
void auxtrace_mmap_params__set_idx(struct auxtrace_mmap_params *mp,
				   struct perf_evlist *evlist, int idx,
				   bool per_cpu);

typedef int (*process_auxtrace_t)(struct perf_tool *tool,
				  union perf_event *event, void *data1,
				  size_t len1, void *data2, size_t len2);

int auxtrace_mmap__read(struct auxtrace_mmap *mm, struct auxtrace_record *itr,
			struct perf_tool *tool, process_auxtrace_t fn);

struct auxtrace_record *auxtrace_record__init(struct perf_evlist *evlist,
					      int *err);

int auxtrace_record__options(struct auxtrace_record *itr,
			     struct perf_evlist *evlist,
			     struct record_opts *opts);
size_t auxtrace_record__info_priv_size(struct auxtrace_record *itr);
int auxtrace_record__info_fill(struct auxtrace_record *itr,
			       struct perf_session *session,
			       struct auxtrace_info_event *auxtrace_info,
			       size_t priv_size);
void auxtrace_record__free(struct auxtrace_record *itr);
u64 auxtrace_record__reference(struct auxtrace_record *itr);

int perf_event__synthesize_auxtrace_info(struct auxtrace_record *itr,
					 struct perf_tool *tool,
					 struct perf_session *session,
					 perf_event__handler_t process);

#endif
