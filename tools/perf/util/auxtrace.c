/*
 * auxtrace.c: AUX area trace support
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

#include <sys/types.h>
#include <sys/mman.h>
#include <stdbool.h>

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../perf.h"
#include "util.h"
#include "evlist.h"
#include "cpumap.h"
#include "thread_map.h"
#include "asm/bug.h"
#include "auxtrace.h"

#include "event.h"
#include "debug.h"

int auxtrace_mmap__mmap(struct auxtrace_mmap *mm,
			struct auxtrace_mmap_params *mp,
			void *userpg, int fd)
{
	struct perf_event_mmap_page *pc = userpg;

#if BITS_PER_LONG != 64 && !defined(HAVE_SYNC_COMPARE_AND_SWAP_SUPPORT)
	pr_err("Cannot use AUX area tracing mmaps\n");
	return -1;
#endif

	WARN_ONCE(mm->base, "Uninitialized auxtrace_mmap\n");

	mm->userpg = userpg;
	mm->mask = mp->mask;
	mm->len = mp->len;
	mm->prev = 0;
	mm->idx = mp->idx;
	mm->tid = mp->tid;
	mm->cpu = mp->cpu;

	if (!mp->len) {
		mm->base = NULL;
		return 0;
	}

	pc->aux_offset = mp->offset;
	pc->aux_size = mp->len;

	mm->base = mmap(NULL, mp->len, mp->prot, MAP_SHARED, fd, mp->offset);
	if (mm->base == MAP_FAILED) {
		pr_debug2("failed to mmap AUX area\n");
		mm->base = NULL;
		return -1;
	}

	return 0;
}

void auxtrace_mmap__munmap(struct auxtrace_mmap *mm)
{
	if (mm->base) {
		munmap(mm->base, mm->len);
		mm->base = NULL;
	}
}

void auxtrace_mmap_params__init(struct auxtrace_mmap_params *mp,
				off_t auxtrace_offset,
				unsigned int auxtrace_pages,
				bool auxtrace_overwrite)
{
	if (auxtrace_pages) {
		mp->offset = auxtrace_offset;
		mp->len = auxtrace_pages * (size_t)page_size;
		mp->mask = is_power_of_2(mp->len) ? mp->len - 1 : 0;
		mp->prot = PROT_READ | (auxtrace_overwrite ? 0 : PROT_WRITE);
		pr_debug2("AUX area mmap length %zu\n", mp->len);
	} else {
		mp->len = 0;
	}
}

void auxtrace_mmap_params__set_idx(struct auxtrace_mmap_params *mp,
				   struct perf_evlist *evlist, int idx,
				   bool per_cpu)
{
	mp->idx = idx;

	if (per_cpu) {
		mp->cpu = evlist->cpus->map[idx];
		if (evlist->threads)
			mp->tid = evlist->threads->map[0];
		else
			mp->tid = -1;
	} else {
		mp->cpu = -1;
		mp->tid = evlist->threads->map[idx];
	}
}

size_t auxtrace_record__info_priv_size(struct auxtrace_record *itr)
{
	if (itr)
		return itr->info_priv_size(itr);
	return 0;
}

static int auxtrace_not_supported(void)
{
	pr_err("AUX area tracing is not supported on this architecture\n");
	return -EINVAL;
}

int auxtrace_record__info_fill(struct auxtrace_record *itr,
			       struct perf_session *session,
			       struct auxtrace_info_event *auxtrace_info,
			       size_t priv_size)
{
	if (itr)
		return itr->info_fill(itr, session, auxtrace_info, priv_size);
	return auxtrace_not_supported();
}

void auxtrace_record__free(struct auxtrace_record *itr)
{
	if (itr)
		itr->free(itr);
}

int auxtrace_record__options(struct auxtrace_record *itr,
			     struct perf_evlist *evlist,
			     struct record_opts *opts)
{
	if (itr)
		return itr->recording_options(itr, evlist, opts);
	return 0;
}

u64 auxtrace_record__reference(struct auxtrace_record *itr)
{
	if (itr)
		return itr->reference(itr);
	return 0;
}

struct auxtrace_record *__weak
auxtrace_record__init(struct perf_evlist *evlist __maybe_unused, int *err)
{
	*err = 0;
	return NULL;
}

int perf_event__synthesize_auxtrace_info(struct auxtrace_record *itr,
					 struct perf_tool *tool,
					 struct perf_session *session,
					 perf_event__handler_t process)
{
	union perf_event *ev;
	size_t priv_size;
	int err;

	pr_debug2("Synthesizing auxtrace information\n");
	priv_size = auxtrace_record__info_priv_size(itr);
	ev = zalloc(sizeof(struct auxtrace_info_event) + priv_size);
	if (!ev)
		return -ENOMEM;

	ev->auxtrace_info.header.type = PERF_RECORD_AUXTRACE_INFO;
	ev->auxtrace_info.header.size = sizeof(struct auxtrace_info_event) +
					priv_size;
	err = auxtrace_record__info_fill(itr, session, &ev->auxtrace_info,
					 priv_size);
	if (err)
		goto out_free;

	err = process(tool, ev, NULL, NULL);
out_free:
	free(ev);
	return err;
}

int auxtrace_mmap__read(struct auxtrace_mmap *mm, struct auxtrace_record *itr,
			struct perf_tool *tool, process_auxtrace_t fn)
{
	u64 head = auxtrace_mmap__read_head(mm);
	u64 old = mm->prev, offset, ref;
	unsigned char *data = mm->base;
	size_t size, head_off, old_off, len1, len2, padding;
	union perf_event ev;
	void *data1, *data2;

	if (old == head)
		return 0;

	pr_debug3("auxtrace idx %d old %#"PRIx64" head %#"PRIx64" diff %#"PRIx64"\n",
		  mm->idx, old, head, head - old);

	if (mm->mask) {
		head_off = head & mm->mask;
		old_off = old & mm->mask;
	} else {
		head_off = head % mm->len;
		old_off = old % mm->len;
	}

	if (head_off > old_off)
		size = head_off - old_off;
	else
		size = mm->len - (old_off - head_off);

	ref = auxtrace_record__reference(itr);

	if (head > old || size <= head || mm->mask) {
		offset = head - size;
	} else {
		/*
		 * When the buffer size is not a power of 2, 'head' wraps at the
		 * highest multiple of the buffer size, so we have to subtract
		 * the remainder here.
		 */
		u64 rem = (0ULL - mm->len) % mm->len;

		offset = head - size - rem;
	}

	if (size > head_off) {
		len1 = size - head_off;
		data1 = &data[mm->len - len1];
		len2 = head_off;
		data2 = &data[0];
	} else {
		len1 = size;
		data1 = &data[head_off - len1];
		len2 = 0;
		data2 = NULL;
	}

	/* padding must be written by fn() e.g. record__process_auxtrace() */
	padding = size & 7;
	if (padding)
		padding = 8 - padding;

	memset(&ev, 0, sizeof(ev));
	ev.auxtrace.header.type = PERF_RECORD_AUXTRACE;
	ev.auxtrace.header.size = sizeof(ev.auxtrace);
	ev.auxtrace.size = size + padding;
	ev.auxtrace.offset = offset;
	ev.auxtrace.reference = ref;
	ev.auxtrace.idx = mm->idx;
	ev.auxtrace.tid = mm->tid;
	ev.auxtrace.cpu = mm->cpu;

	if (fn(tool, &ev, data1, len1, data2, len2))
		return -1;

	mm->prev = head;

	auxtrace_mmap__write_tail(mm, head);
	if (itr->read_finish) {
		int err;

		err = itr->read_finish(itr, mm->idx);
		if (err < 0)
			return err;
	}

	return 1;
}
