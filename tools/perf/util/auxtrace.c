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
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <linux/kernel.h>
#include <linux/perf_event.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/string.h>

#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <linux/list.h>

#include "../perf.h"
#include "util.h"
#include "evlist.h"
#include "dso.h"
#include "map.h"
#include "pmu.h"
#include "evsel.h"
#include "cpumap.h"
#include "thread_map.h"
#include "asm/bug.h"
#include "symbol/kallsyms.h"
#include "auxtrace.h"

#include <linux/hash.h>

#include "event.h"
#include "session.h"
#include "debug.h"
#include <subcmd/parse-options.h>

#include "intel-pt.h"
#include "intel-bts.h"

int auxtrace_mmap__mmap(struct auxtrace_mmap *mm,
			struct auxtrace_mmap_params *mp,
			void *userpg, int fd)
{
	struct perf_event_mmap_page *pc = userpg;

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

#if BITS_PER_LONG != 64 && !defined(HAVE_SYNC_COMPARE_AND_SWAP_SUPPORT)
	pr_err("Cannot use AUX area tracing mmaps\n");
	return -1;
#endif

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
			mp->tid = thread_map__pid(evlist->threads, 0);
		else
			mp->tid = -1;
	} else {
		mp->cpu = -1;
		mp->tid = thread_map__pid(evlist->threads, idx);
	}
}

#define AUXTRACE_INIT_NR_QUEUES	32

static struct auxtrace_queue *auxtrace_alloc_queue_array(unsigned int nr_queues)
{
	struct auxtrace_queue *queue_array;
	unsigned int max_nr_queues, i;

	max_nr_queues = UINT_MAX / sizeof(struct auxtrace_queue);
	if (nr_queues > max_nr_queues)
		return NULL;

	queue_array = calloc(nr_queues, sizeof(struct auxtrace_queue));
	if (!queue_array)
		return NULL;

	for (i = 0; i < nr_queues; i++) {
		INIT_LIST_HEAD(&queue_array[i].head);
		queue_array[i].priv = NULL;
	}

	return queue_array;
}

int auxtrace_queues__init(struct auxtrace_queues *queues)
{
	queues->nr_queues = AUXTRACE_INIT_NR_QUEUES;
	queues->queue_array = auxtrace_alloc_queue_array(queues->nr_queues);
	if (!queues->queue_array)
		return -ENOMEM;
	return 0;
}

static int auxtrace_queues__grow(struct auxtrace_queues *queues,
				 unsigned int new_nr_queues)
{
	unsigned int nr_queues = queues->nr_queues;
	struct auxtrace_queue *queue_array;
	unsigned int i;

	if (!nr_queues)
		nr_queues = AUXTRACE_INIT_NR_QUEUES;

	while (nr_queues && nr_queues < new_nr_queues)
		nr_queues <<= 1;

	if (nr_queues < queues->nr_queues || nr_queues < new_nr_queues)
		return -EINVAL;

	queue_array = auxtrace_alloc_queue_array(nr_queues);
	if (!queue_array)
		return -ENOMEM;

	for (i = 0; i < queues->nr_queues; i++) {
		list_splice_tail(&queues->queue_array[i].head,
				 &queue_array[i].head);
		queue_array[i].priv = queues->queue_array[i].priv;
	}

	queues->nr_queues = nr_queues;
	queues->queue_array = queue_array;

	return 0;
}

static void *auxtrace_copy_data(u64 size, struct perf_session *session)
{
	int fd = perf_data_file__fd(session->file);
	void *p;
	ssize_t ret;

	if (size > SSIZE_MAX)
		return NULL;

	p = malloc(size);
	if (!p)
		return NULL;

	ret = readn(fd, p, size);
	if (ret != (ssize_t)size) {
		free(p);
		return NULL;
	}

	return p;
}

static int auxtrace_queues__add_buffer(struct auxtrace_queues *queues,
				       unsigned int idx,
				       struct auxtrace_buffer *buffer)
{
	struct auxtrace_queue *queue;
	int err;

	if (idx >= queues->nr_queues) {
		err = auxtrace_queues__grow(queues, idx + 1);
		if (err)
			return err;
	}

	queue = &queues->queue_array[idx];

	if (!queue->set) {
		queue->set = true;
		queue->tid = buffer->tid;
		queue->cpu = buffer->cpu;
	} else if (buffer->cpu != queue->cpu || buffer->tid != queue->tid) {
		pr_err("auxtrace queue conflict: cpu %d, tid %d vs cpu %d, tid %d\n",
		       queue->cpu, queue->tid, buffer->cpu, buffer->tid);
		return -EINVAL;
	}

	buffer->buffer_nr = queues->next_buffer_nr++;

	list_add_tail(&buffer->list, &queue->head);

	queues->new_data = true;
	queues->populated = true;

	return 0;
}

/* Limit buffers to 32MiB on 32-bit */
#define BUFFER_LIMIT_FOR_32_BIT (32 * 1024 * 1024)

static int auxtrace_queues__split_buffer(struct auxtrace_queues *queues,
					 unsigned int idx,
					 struct auxtrace_buffer *buffer)
{
	u64 sz = buffer->size;
	bool consecutive = false;
	struct auxtrace_buffer *b;
	int err;

	while (sz > BUFFER_LIMIT_FOR_32_BIT) {
		b = memdup(buffer, sizeof(struct auxtrace_buffer));
		if (!b)
			return -ENOMEM;
		b->size = BUFFER_LIMIT_FOR_32_BIT;
		b->consecutive = consecutive;
		err = auxtrace_queues__add_buffer(queues, idx, b);
		if (err) {
			auxtrace_buffer__free(b);
			return err;
		}
		buffer->data_offset += BUFFER_LIMIT_FOR_32_BIT;
		sz -= BUFFER_LIMIT_FOR_32_BIT;
		consecutive = true;
	}

	buffer->size = sz;
	buffer->consecutive = consecutive;

	return 0;
}

static int auxtrace_queues__add_event_buffer(struct auxtrace_queues *queues,
					     struct perf_session *session,
					     unsigned int idx,
					     struct auxtrace_buffer *buffer)
{
	if (session->one_mmap) {
		buffer->data = buffer->data_offset - session->one_mmap_offset +
			       session->one_mmap_addr;
	} else if (perf_data_file__is_pipe(session->file)) {
		buffer->data = auxtrace_copy_data(buffer->size, session);
		if (!buffer->data)
			return -ENOMEM;
		buffer->data_needs_freeing = true;
	} else if (BITS_PER_LONG == 32 &&
		   buffer->size > BUFFER_LIMIT_FOR_32_BIT) {
		int err;

		err = auxtrace_queues__split_buffer(queues, idx, buffer);
		if (err)
			return err;
	}

	return auxtrace_queues__add_buffer(queues, idx, buffer);
}

int auxtrace_queues__add_event(struct auxtrace_queues *queues,
			       struct perf_session *session,
			       union perf_event *event, off_t data_offset,
			       struct auxtrace_buffer **buffer_ptr)
{
	struct auxtrace_buffer *buffer;
	unsigned int idx;
	int err;

	buffer = zalloc(sizeof(struct auxtrace_buffer));
	if (!buffer)
		return -ENOMEM;

	buffer->pid = -1;
	buffer->tid = event->auxtrace.tid;
	buffer->cpu = event->auxtrace.cpu;
	buffer->data_offset = data_offset;
	buffer->offset = event->auxtrace.offset;
	buffer->reference = event->auxtrace.reference;
	buffer->size = event->auxtrace.size;
	idx = event->auxtrace.idx;

	err = auxtrace_queues__add_event_buffer(queues, session, idx, buffer);
	if (err)
		goto out_err;

	if (buffer_ptr)
		*buffer_ptr = buffer;

	return 0;

out_err:
	auxtrace_buffer__free(buffer);
	return err;
}

static int auxtrace_queues__add_indexed_event(struct auxtrace_queues *queues,
					      struct perf_session *session,
					      off_t file_offset, size_t sz)
{
	union perf_event *event;
	int err;
	char buf[PERF_SAMPLE_MAX_SIZE];

	err = perf_session__peek_event(session, file_offset, buf,
				       PERF_SAMPLE_MAX_SIZE, &event, NULL);
	if (err)
		return err;

	if (event->header.type == PERF_RECORD_AUXTRACE) {
		if (event->header.size < sizeof(struct auxtrace_event) ||
		    event->header.size != sz) {
			err = -EINVAL;
			goto out;
		}
		file_offset += event->header.size;
		err = auxtrace_queues__add_event(queues, session, event,
						 file_offset, NULL);
	}
out:
	return err;
}

void auxtrace_queues__free(struct auxtrace_queues *queues)
{
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++) {
		while (!list_empty(&queues->queue_array[i].head)) {
			struct auxtrace_buffer *buffer;

			buffer = list_entry(queues->queue_array[i].head.next,
					    struct auxtrace_buffer, list);
			list_del(&buffer->list);
			auxtrace_buffer__free(buffer);
		}
	}

	zfree(&queues->queue_array);
	queues->nr_queues = 0;
}

static void auxtrace_heapify(struct auxtrace_heap_item *heap_array,
			     unsigned int pos, unsigned int queue_nr,
			     u64 ordinal)
{
	unsigned int parent;

	while (pos) {
		parent = (pos - 1) >> 1;
		if (heap_array[parent].ordinal <= ordinal)
			break;
		heap_array[pos] = heap_array[parent];
		pos = parent;
	}
	heap_array[pos].queue_nr = queue_nr;
	heap_array[pos].ordinal = ordinal;
}

int auxtrace_heap__add(struct auxtrace_heap *heap, unsigned int queue_nr,
		       u64 ordinal)
{
	struct auxtrace_heap_item *heap_array;

	if (queue_nr >= heap->heap_sz) {
		unsigned int heap_sz = AUXTRACE_INIT_NR_QUEUES;

		while (heap_sz <= queue_nr)
			heap_sz <<= 1;
		heap_array = realloc(heap->heap_array,
				     heap_sz * sizeof(struct auxtrace_heap_item));
		if (!heap_array)
			return -ENOMEM;
		heap->heap_array = heap_array;
		heap->heap_sz = heap_sz;
	}

	auxtrace_heapify(heap->heap_array, heap->heap_cnt++, queue_nr, ordinal);

	return 0;
}

void auxtrace_heap__free(struct auxtrace_heap *heap)
{
	zfree(&heap->heap_array);
	heap->heap_cnt = 0;
	heap->heap_sz = 0;
}

void auxtrace_heap__pop(struct auxtrace_heap *heap)
{
	unsigned int pos, last, heap_cnt = heap->heap_cnt;
	struct auxtrace_heap_item *heap_array;

	if (!heap_cnt)
		return;

	heap->heap_cnt -= 1;

	heap_array = heap->heap_array;

	pos = 0;
	while (1) {
		unsigned int left, right;

		left = (pos << 1) + 1;
		if (left >= heap_cnt)
			break;
		right = left + 1;
		if (right >= heap_cnt) {
			heap_array[pos] = heap_array[left];
			return;
		}
		if (heap_array[left].ordinal < heap_array[right].ordinal) {
			heap_array[pos] = heap_array[left];
			pos = left;
		} else {
			heap_array[pos] = heap_array[right];
			pos = right;
		}
	}

	last = heap_cnt - 1;
	auxtrace_heapify(heap_array, pos, heap_array[last].queue_nr,
			 heap_array[last].ordinal);
}

size_t auxtrace_record__info_priv_size(struct auxtrace_record *itr,
				       struct perf_evlist *evlist)
{
	if (itr)
		return itr->info_priv_size(itr, evlist);
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

int auxtrace_record__snapshot_start(struct auxtrace_record *itr)
{
	if (itr && itr->snapshot_start)
		return itr->snapshot_start(itr);
	return 0;
}

int auxtrace_record__snapshot_finish(struct auxtrace_record *itr)
{
	if (itr && itr->snapshot_finish)
		return itr->snapshot_finish(itr);
	return 0;
}

int auxtrace_record__find_snapshot(struct auxtrace_record *itr, int idx,
				   struct auxtrace_mmap *mm,
				   unsigned char *data, u64 *head, u64 *old)
{
	if (itr && itr->find_snapshot)
		return itr->find_snapshot(itr, idx, mm, data, head, old);
	return 0;
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

int auxtrace_parse_snapshot_options(struct auxtrace_record *itr,
				    struct record_opts *opts, const char *str)
{
	if (!str)
		return 0;

	if (itr)
		return itr->parse_snapshot_options(itr, opts, str);

	pr_err("No AUX area tracing to snapshot\n");
	return -EINVAL;
}

struct auxtrace_record *__weak
auxtrace_record__init(struct perf_evlist *evlist __maybe_unused, int *err)
{
	*err = 0;
	return NULL;
}

static int auxtrace_index__alloc(struct list_head *head)
{
	struct auxtrace_index *auxtrace_index;

	auxtrace_index = malloc(sizeof(struct auxtrace_index));
	if (!auxtrace_index)
		return -ENOMEM;

	auxtrace_index->nr = 0;
	INIT_LIST_HEAD(&auxtrace_index->list);

	list_add_tail(&auxtrace_index->list, head);

	return 0;
}

void auxtrace_index__free(struct list_head *head)
{
	struct auxtrace_index *auxtrace_index, *n;

	list_for_each_entry_safe(auxtrace_index, n, head, list) {
		list_del(&auxtrace_index->list);
		free(auxtrace_index);
	}
}

static struct auxtrace_index *auxtrace_index__last(struct list_head *head)
{
	struct auxtrace_index *auxtrace_index;
	int err;

	if (list_empty(head)) {
		err = auxtrace_index__alloc(head);
		if (err)
			return NULL;
	}

	auxtrace_index = list_entry(head->prev, struct auxtrace_index, list);

	if (auxtrace_index->nr >= PERF_AUXTRACE_INDEX_ENTRY_COUNT) {
		err = auxtrace_index__alloc(head);
		if (err)
			return NULL;
		auxtrace_index = list_entry(head->prev, struct auxtrace_index,
					    list);
	}

	return auxtrace_index;
}

int auxtrace_index__auxtrace_event(struct list_head *head,
				   union perf_event *event, off_t file_offset)
{
	struct auxtrace_index *auxtrace_index;
	size_t nr;

	auxtrace_index = auxtrace_index__last(head);
	if (!auxtrace_index)
		return -ENOMEM;

	nr = auxtrace_index->nr;
	auxtrace_index->entries[nr].file_offset = file_offset;
	auxtrace_index->entries[nr].sz = event->header.size;
	auxtrace_index->nr += 1;

	return 0;
}

static int auxtrace_index__do_write(int fd,
				    struct auxtrace_index *auxtrace_index)
{
	struct auxtrace_index_entry ent;
	size_t i;

	for (i = 0; i < auxtrace_index->nr; i++) {
		ent.file_offset = auxtrace_index->entries[i].file_offset;
		ent.sz = auxtrace_index->entries[i].sz;
		if (writen(fd, &ent, sizeof(ent)) != sizeof(ent))
			return -errno;
	}
	return 0;
}

int auxtrace_index__write(int fd, struct list_head *head)
{
	struct auxtrace_index *auxtrace_index;
	u64 total = 0;
	int err;

	list_for_each_entry(auxtrace_index, head, list)
		total += auxtrace_index->nr;

	if (writen(fd, &total, sizeof(total)) != sizeof(total))
		return -errno;

	list_for_each_entry(auxtrace_index, head, list) {
		err = auxtrace_index__do_write(fd, auxtrace_index);
		if (err)
			return err;
	}

	return 0;
}

static int auxtrace_index__process_entry(int fd, struct list_head *head,
					 bool needs_swap)
{
	struct auxtrace_index *auxtrace_index;
	struct auxtrace_index_entry ent;
	size_t nr;

	if (readn(fd, &ent, sizeof(ent)) != sizeof(ent))
		return -1;

	auxtrace_index = auxtrace_index__last(head);
	if (!auxtrace_index)
		return -1;

	nr = auxtrace_index->nr;
	if (needs_swap) {
		auxtrace_index->entries[nr].file_offset =
						bswap_64(ent.file_offset);
		auxtrace_index->entries[nr].sz = bswap_64(ent.sz);
	} else {
		auxtrace_index->entries[nr].file_offset = ent.file_offset;
		auxtrace_index->entries[nr].sz = ent.sz;
	}

	auxtrace_index->nr = nr + 1;

	return 0;
}

int auxtrace_index__process(int fd, u64 size, struct perf_session *session,
			    bool needs_swap)
{
	struct list_head *head = &session->auxtrace_index;
	u64 nr;

	if (readn(fd, &nr, sizeof(u64)) != sizeof(u64))
		return -1;

	if (needs_swap)
		nr = bswap_64(nr);

	if (sizeof(u64) + nr * sizeof(struct auxtrace_index_entry) > size)
		return -1;

	while (nr--) {
		int err;

		err = auxtrace_index__process_entry(fd, head, needs_swap);
		if (err)
			return -1;
	}

	return 0;
}

static int auxtrace_queues__process_index_entry(struct auxtrace_queues *queues,
						struct perf_session *session,
						struct auxtrace_index_entry *ent)
{
	return auxtrace_queues__add_indexed_event(queues, session,
						  ent->file_offset, ent->sz);
}

int auxtrace_queues__process_index(struct auxtrace_queues *queues,
				   struct perf_session *session)
{
	struct auxtrace_index *auxtrace_index;
	struct auxtrace_index_entry *ent;
	size_t i;
	int err;

	list_for_each_entry(auxtrace_index, &session->auxtrace_index, list) {
		for (i = 0; i < auxtrace_index->nr; i++) {
			ent = &auxtrace_index->entries[i];
			err = auxtrace_queues__process_index_entry(queues,
								   session,
								   ent);
			if (err)
				return err;
		}
	}
	return 0;
}

struct auxtrace_buffer *auxtrace_buffer__next(struct auxtrace_queue *queue,
					      struct auxtrace_buffer *buffer)
{
	if (buffer) {
		if (list_is_last(&buffer->list, &queue->head))
			return NULL;
		return list_entry(buffer->list.next, struct auxtrace_buffer,
				  list);
	} else {
		if (list_empty(&queue->head))
			return NULL;
		return list_entry(queue->head.next, struct auxtrace_buffer,
				  list);
	}
}

void *auxtrace_buffer__get_data(struct auxtrace_buffer *buffer, int fd)
{
	size_t adj = buffer->data_offset & (page_size - 1);
	size_t size = buffer->size + adj;
	off_t file_offset = buffer->data_offset - adj;
	void *addr;

	if (buffer->data)
		return buffer->data;

	addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, file_offset);
	if (addr == MAP_FAILED)
		return NULL;

	buffer->mmap_addr = addr;
	buffer->mmap_size = size;

	buffer->data = addr + adj;

	return buffer->data;
}

void auxtrace_buffer__put_data(struct auxtrace_buffer *buffer)
{
	if (!buffer->data || !buffer->mmap_addr)
		return;
	munmap(buffer->mmap_addr, buffer->mmap_size);
	buffer->mmap_addr = NULL;
	buffer->mmap_size = 0;
	buffer->data = NULL;
	buffer->use_data = NULL;
}

void auxtrace_buffer__drop_data(struct auxtrace_buffer *buffer)
{
	auxtrace_buffer__put_data(buffer);
	if (buffer->data_needs_freeing) {
		buffer->data_needs_freeing = false;
		zfree(&buffer->data);
		buffer->use_data = NULL;
		buffer->size = 0;
	}
}

void auxtrace_buffer__free(struct auxtrace_buffer *buffer)
{
	auxtrace_buffer__drop_data(buffer);
	free(buffer);
}

void auxtrace_synth_error(struct auxtrace_error_event *auxtrace_error, int type,
			  int code, int cpu, pid_t pid, pid_t tid, u64 ip,
			  const char *msg)
{
	size_t size;

	memset(auxtrace_error, 0, sizeof(struct auxtrace_error_event));

	auxtrace_error->header.type = PERF_RECORD_AUXTRACE_ERROR;
	auxtrace_error->type = type;
	auxtrace_error->code = code;
	auxtrace_error->cpu = cpu;
	auxtrace_error->pid = pid;
	auxtrace_error->tid = tid;
	auxtrace_error->ip = ip;
	strlcpy(auxtrace_error->msg, msg, MAX_AUXTRACE_ERROR_MSG);

	size = (void *)auxtrace_error->msg - (void *)auxtrace_error +
	       strlen(auxtrace_error->msg) + 1;
	auxtrace_error->header.size = PERF_ALIGN(size, sizeof(u64));
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
	priv_size = auxtrace_record__info_priv_size(itr, session->evlist);
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

static bool auxtrace__dont_decode(struct perf_session *session)
{
	return !session->itrace_synth_opts ||
	       session->itrace_synth_opts->dont_decode;
}

int perf_event__process_auxtrace_info(struct perf_tool *tool __maybe_unused,
				      union perf_event *event,
				      struct perf_session *session)
{
	enum auxtrace_type type = event->auxtrace_info.type;

	if (dump_trace)
		fprintf(stdout, " type: %u\n", type);

	switch (type) {
	case PERF_AUXTRACE_INTEL_PT:
		return intel_pt_process_auxtrace_info(event, session);
	case PERF_AUXTRACE_INTEL_BTS:
		return intel_bts_process_auxtrace_info(event, session);
	case PERF_AUXTRACE_CS_ETM:
	case PERF_AUXTRACE_UNKNOWN:
	default:
		return -EINVAL;
	}
}

s64 perf_event__process_auxtrace(struct perf_tool *tool,
				 union perf_event *event,
				 struct perf_session *session)
{
	s64 err;

	if (dump_trace)
		fprintf(stdout, " size: %#"PRIx64"  offset: %#"PRIx64"  ref: %#"PRIx64"  idx: %u  tid: %d  cpu: %d\n",
			event->auxtrace.size, event->auxtrace.offset,
			event->auxtrace.reference, event->auxtrace.idx,
			event->auxtrace.tid, event->auxtrace.cpu);

	if (auxtrace__dont_decode(session))
		return event->auxtrace.size;

	if (!session->auxtrace || event->header.type != PERF_RECORD_AUXTRACE)
		return -EINVAL;

	err = session->auxtrace->process_auxtrace_event(session, event, tool);
	if (err < 0)
		return err;

	return event->auxtrace.size;
}

#define PERF_ITRACE_DEFAULT_PERIOD_TYPE		PERF_ITRACE_PERIOD_NANOSECS
#define PERF_ITRACE_DEFAULT_PERIOD		100000
#define PERF_ITRACE_DEFAULT_CALLCHAIN_SZ	16
#define PERF_ITRACE_MAX_CALLCHAIN_SZ		1024
#define PERF_ITRACE_DEFAULT_LAST_BRANCH_SZ	64
#define PERF_ITRACE_MAX_LAST_BRANCH_SZ		1024

void itrace_synth_opts__set_default(struct itrace_synth_opts *synth_opts)
{
	synth_opts->instructions = true;
	synth_opts->branches = true;
	synth_opts->transactions = true;
	synth_opts->errors = true;
	synth_opts->period_type = PERF_ITRACE_DEFAULT_PERIOD_TYPE;
	synth_opts->period = PERF_ITRACE_DEFAULT_PERIOD;
	synth_opts->callchain_sz = PERF_ITRACE_DEFAULT_CALLCHAIN_SZ;
	synth_opts->last_branch_sz = PERF_ITRACE_DEFAULT_LAST_BRANCH_SZ;
	synth_opts->initial_skip = 0;
}

/*
 * Please check tools/perf/Documentation/perf-script.txt for information
 * about the options parsed here, which is introduced after this cset,
 * when support in 'perf script' for these options is introduced.
 */
int itrace_parse_synth_opts(const struct option *opt, const char *str,
			    int unset)
{
	struct itrace_synth_opts *synth_opts = opt->value;
	const char *p;
	char *endptr;
	bool period_type_set = false;
	bool period_set = false;

	synth_opts->set = true;

	if (unset) {
		synth_opts->dont_decode = true;
		return 0;
	}

	if (!str) {
		itrace_synth_opts__set_default(synth_opts);
		return 0;
	}

	for (p = str; *p;) {
		switch (*p++) {
		case 'i':
			synth_opts->instructions = true;
			while (*p == ' ' || *p == ',')
				p += 1;
			if (isdigit(*p)) {
				synth_opts->period = strtoull(p, &endptr, 10);
				period_set = true;
				p = endptr;
				while (*p == ' ' || *p == ',')
					p += 1;
				switch (*p++) {
				case 'i':
					synth_opts->period_type =
						PERF_ITRACE_PERIOD_INSTRUCTIONS;
					period_type_set = true;
					break;
				case 't':
					synth_opts->period_type =
						PERF_ITRACE_PERIOD_TICKS;
					period_type_set = true;
					break;
				case 'm':
					synth_opts->period *= 1000;
					/* Fall through */
				case 'u':
					synth_opts->period *= 1000;
					/* Fall through */
				case 'n':
					if (*p++ != 's')
						goto out_err;
					synth_opts->period_type =
						PERF_ITRACE_PERIOD_NANOSECS;
					period_type_set = true;
					break;
				case '\0':
					goto out;
				default:
					goto out_err;
				}
			}
			break;
		case 'b':
			synth_opts->branches = true;
			break;
		case 'x':
			synth_opts->transactions = true;
			break;
		case 'e':
			synth_opts->errors = true;
			break;
		case 'd':
			synth_opts->log = true;
			break;
		case 'c':
			synth_opts->branches = true;
			synth_opts->calls = true;
			break;
		case 'r':
			synth_opts->branches = true;
			synth_opts->returns = true;
			break;
		case 'g':
			synth_opts->callchain = true;
			synth_opts->callchain_sz =
					PERF_ITRACE_DEFAULT_CALLCHAIN_SZ;
			while (*p == ' ' || *p == ',')
				p += 1;
			if (isdigit(*p)) {
				unsigned int val;

				val = strtoul(p, &endptr, 10);
				p = endptr;
				if (!val || val > PERF_ITRACE_MAX_CALLCHAIN_SZ)
					goto out_err;
				synth_opts->callchain_sz = val;
			}
			break;
		case 'l':
			synth_opts->last_branch = true;
			synth_opts->last_branch_sz =
					PERF_ITRACE_DEFAULT_LAST_BRANCH_SZ;
			while (*p == ' ' || *p == ',')
				p += 1;
			if (isdigit(*p)) {
				unsigned int val;

				val = strtoul(p, &endptr, 10);
				p = endptr;
				if (!val ||
				    val > PERF_ITRACE_MAX_LAST_BRANCH_SZ)
					goto out_err;
				synth_opts->last_branch_sz = val;
			}
			break;
		case 's':
			synth_opts->initial_skip = strtoul(p, &endptr, 10);
			if (p == endptr)
				goto out_err;
			p = endptr;
			break;
		case ' ':
		case ',':
			break;
		default:
			goto out_err;
		}
	}
out:
	if (synth_opts->instructions) {
		if (!period_type_set)
			synth_opts->period_type =
					PERF_ITRACE_DEFAULT_PERIOD_TYPE;
		if (!period_set)
			synth_opts->period = PERF_ITRACE_DEFAULT_PERIOD;
	}

	return 0;

out_err:
	pr_err("Bad Instruction Tracing options '%s'\n", str);
	return -EINVAL;
}

static const char * const auxtrace_error_type_name[] = {
	[PERF_AUXTRACE_ERROR_ITRACE] = "instruction trace",
};

static const char *auxtrace_error_name(int type)
{
	const char *error_type_name = NULL;

	if (type < PERF_AUXTRACE_ERROR_MAX)
		error_type_name = auxtrace_error_type_name[type];
	if (!error_type_name)
		error_type_name = "unknown AUX";
	return error_type_name;
}

size_t perf_event__fprintf_auxtrace_error(union perf_event *event, FILE *fp)
{
	struct auxtrace_error_event *e = &event->auxtrace_error;
	int ret;

	ret = fprintf(fp, " %s error type %u",
		      auxtrace_error_name(e->type), e->type);
	ret += fprintf(fp, " cpu %d pid %d tid %d ip %#"PRIx64" code %u: %s\n",
		       e->cpu, e->pid, e->tid, e->ip, e->code, e->msg);
	return ret;
}

void perf_session__auxtrace_error_inc(struct perf_session *session,
				      union perf_event *event)
{
	struct auxtrace_error_event *e = &event->auxtrace_error;

	if (e->type < PERF_AUXTRACE_ERROR_MAX)
		session->evlist->stats.nr_auxtrace_errors[e->type] += 1;
}

void events_stats__auxtrace_error_warn(const struct events_stats *stats)
{
	int i;

	for (i = 0; i < PERF_AUXTRACE_ERROR_MAX; i++) {
		if (!stats->nr_auxtrace_errors[i])
			continue;
		ui__warning("%u %s errors\n",
			    stats->nr_auxtrace_errors[i],
			    auxtrace_error_name(i));
	}
}

int perf_event__process_auxtrace_error(struct perf_tool *tool __maybe_unused,
				       union perf_event *event,
				       struct perf_session *session)
{
	if (auxtrace__dont_decode(session))
		return 0;

	perf_event__fprintf_auxtrace_error(event, stdout);
	return 0;
}

static int __auxtrace_mmap__read(struct auxtrace_mmap *mm,
				 struct auxtrace_record *itr,
				 struct perf_tool *tool, process_auxtrace_t fn,
				 bool snapshot, size_t snapshot_size)
{
	u64 head, old = mm->prev, offset, ref;
	unsigned char *data = mm->base;
	size_t size, head_off, old_off, len1, len2, padding;
	union perf_event ev;
	void *data1, *data2;

	if (snapshot) {
		head = auxtrace_mmap__read_snapshot_head(mm);
		if (auxtrace_record__find_snapshot(itr, mm->idx, mm, data,
						   &head, &old))
			return -1;
	} else {
		head = auxtrace_mmap__read_head(mm);
	}

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

	if (snapshot && size > snapshot_size)
		size = snapshot_size;

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

	if (itr->alignment) {
		unsigned int unwanted = len1 % itr->alignment;

		len1 -= unwanted;
		size -= unwanted;
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

	if (!snapshot) {
		auxtrace_mmap__write_tail(mm, head);
		if (itr->read_finish) {
			int err;

			err = itr->read_finish(itr, mm->idx);
			if (err < 0)
				return err;
		}
	}

	return 1;
}

int auxtrace_mmap__read(struct auxtrace_mmap *mm, struct auxtrace_record *itr,
			struct perf_tool *tool, process_auxtrace_t fn)
{
	return __auxtrace_mmap__read(mm, itr, tool, fn, false, 0);
}

int auxtrace_mmap__read_snapshot(struct auxtrace_mmap *mm,
				 struct auxtrace_record *itr,
				 struct perf_tool *tool, process_auxtrace_t fn,
				 size_t snapshot_size)
{
	return __auxtrace_mmap__read(mm, itr, tool, fn, true, snapshot_size);
}

/**
 * struct auxtrace_cache - hash table to implement a cache
 * @hashtable: the hashtable
 * @sz: hashtable size (number of hlists)
 * @entry_size: size of an entry
 * @limit: limit the number of entries to this maximum, when reached the cache
 *         is dropped and caching begins again with an empty cache
 * @cnt: current number of entries
 * @bits: hashtable size (@sz = 2^@bits)
 */
struct auxtrace_cache {
	struct hlist_head *hashtable;
	size_t sz;
	size_t entry_size;
	size_t limit;
	size_t cnt;
	unsigned int bits;
};

struct auxtrace_cache *auxtrace_cache__new(unsigned int bits, size_t entry_size,
					   unsigned int limit_percent)
{
	struct auxtrace_cache *c;
	struct hlist_head *ht;
	size_t sz, i;

	c = zalloc(sizeof(struct auxtrace_cache));
	if (!c)
		return NULL;

	sz = 1UL << bits;

	ht = calloc(sz, sizeof(struct hlist_head));
	if (!ht)
		goto out_free;

	for (i = 0; i < sz; i++)
		INIT_HLIST_HEAD(&ht[i]);

	c->hashtable = ht;
	c->sz = sz;
	c->entry_size = entry_size;
	c->limit = (c->sz * limit_percent) / 100;
	c->bits = bits;

	return c;

out_free:
	free(c);
	return NULL;
}

static void auxtrace_cache__drop(struct auxtrace_cache *c)
{
	struct auxtrace_cache_entry *entry;
	struct hlist_node *tmp;
	size_t i;

	if (!c)
		return;

	for (i = 0; i < c->sz; i++) {
		hlist_for_each_entry_safe(entry, tmp, &c->hashtable[i], hash) {
			hlist_del(&entry->hash);
			auxtrace_cache__free_entry(c, entry);
		}
	}

	c->cnt = 0;
}

void auxtrace_cache__free(struct auxtrace_cache *c)
{
	if (!c)
		return;

	auxtrace_cache__drop(c);
	free(c->hashtable);
	free(c);
}

void *auxtrace_cache__alloc_entry(struct auxtrace_cache *c)
{
	return malloc(c->entry_size);
}

void auxtrace_cache__free_entry(struct auxtrace_cache *c __maybe_unused,
				void *entry)
{
	free(entry);
}

int auxtrace_cache__add(struct auxtrace_cache *c, u32 key,
			struct auxtrace_cache_entry *entry)
{
	if (c->limit && ++c->cnt > c->limit)
		auxtrace_cache__drop(c);

	entry->key = key;
	hlist_add_head(&entry->hash, &c->hashtable[hash_32(key, c->bits)]);

	return 0;
}

void *auxtrace_cache__lookup(struct auxtrace_cache *c, u32 key)
{
	struct auxtrace_cache_entry *entry;
	struct hlist_head *hlist;

	if (!c)
		return NULL;

	hlist = &c->hashtable[hash_32(key, c->bits)];
	hlist_for_each_entry(entry, hlist, hash) {
		if (entry->key == key)
			return entry;
	}

	return NULL;
}

static void addr_filter__free_str(struct addr_filter *filt)
{
	free(filt->str);
	filt->action   = NULL;
	filt->sym_from = NULL;
	filt->sym_to   = NULL;
	filt->filename = NULL;
	filt->str      = NULL;
}

static struct addr_filter *addr_filter__new(void)
{
	struct addr_filter *filt = zalloc(sizeof(*filt));

	if (filt)
		INIT_LIST_HEAD(&filt->list);

	return filt;
}

static void addr_filter__free(struct addr_filter *filt)
{
	if (filt)
		addr_filter__free_str(filt);
	free(filt);
}

static void addr_filters__add(struct addr_filters *filts,
			      struct addr_filter *filt)
{
	list_add_tail(&filt->list, &filts->head);
	filts->cnt += 1;
}

static void addr_filters__del(struct addr_filters *filts,
			      struct addr_filter *filt)
{
	list_del_init(&filt->list);
	filts->cnt -= 1;
}

void addr_filters__init(struct addr_filters *filts)
{
	INIT_LIST_HEAD(&filts->head);
	filts->cnt = 0;
}

void addr_filters__exit(struct addr_filters *filts)
{
	struct addr_filter *filt, *n;

	list_for_each_entry_safe(filt, n, &filts->head, list) {
		addr_filters__del(filts, filt);
		addr_filter__free(filt);
	}
}

static int parse_num_or_str(char **inp, u64 *num, const char **str,
			    const char *str_delim)
{
	*inp += strspn(*inp, " ");

	if (isdigit(**inp)) {
		char *endptr;

		if (!num)
			return -EINVAL;
		errno = 0;
		*num = strtoull(*inp, &endptr, 0);
		if (errno)
			return -errno;
		if (endptr == *inp)
			return -EINVAL;
		*inp = endptr;
	} else {
		size_t n;

		if (!str)
			return -EINVAL;
		*inp += strspn(*inp, " ");
		*str = *inp;
		n = strcspn(*inp, str_delim);
		if (!n)
			return -EINVAL;
		*inp += n;
		if (**inp) {
			**inp = '\0';
			*inp += 1;
		}
	}
	return 0;
}

static int parse_action(struct addr_filter *filt)
{
	if (!strcmp(filt->action, "filter")) {
		filt->start = true;
		filt->range = true;
	} else if (!strcmp(filt->action, "start")) {
		filt->start = true;
	} else if (!strcmp(filt->action, "stop")) {
		filt->start = false;
	} else if (!strcmp(filt->action, "tracestop")) {
		filt->start = false;
		filt->range = true;
		filt->action += 5; /* Change 'tracestop' to 'stop' */
	} else {
		return -EINVAL;
	}
	return 0;
}

static int parse_sym_idx(char **inp, int *idx)
{
	*idx = -1;

	*inp += strspn(*inp, " ");

	if (**inp != '#')
		return 0;

	*inp += 1;

	if (**inp == 'g' || **inp == 'G') {
		*inp += 1;
		*idx = 0;
	} else {
		unsigned long num;
		char *endptr;

		errno = 0;
		num = strtoul(*inp, &endptr, 0);
		if (errno)
			return -errno;
		if (endptr == *inp || num > INT_MAX)
			return -EINVAL;
		*inp = endptr;
		*idx = num;
	}

	return 0;
}

static int parse_addr_size(char **inp, u64 *num, const char **str, int *idx)
{
	int err = parse_num_or_str(inp, num, str, " ");

	if (!err && *str)
		err = parse_sym_idx(inp, idx);

	return err;
}

static int parse_one_filter(struct addr_filter *filt, const char **filter_inp)
{
	char *fstr;
	int err;

	filt->str = fstr = strdup(*filter_inp);
	if (!fstr)
		return -ENOMEM;

	err = parse_num_or_str(&fstr, NULL, &filt->action, " ");
	if (err)
		goto out_err;

	err = parse_action(filt);
	if (err)
		goto out_err;

	err = parse_addr_size(&fstr, &filt->addr, &filt->sym_from,
			      &filt->sym_from_idx);
	if (err)
		goto out_err;

	fstr += strspn(fstr, " ");

	if (*fstr == '/') {
		fstr += 1;
		err = parse_addr_size(&fstr, &filt->size, &filt->sym_to,
				      &filt->sym_to_idx);
		if (err)
			goto out_err;
		filt->range = true;
	}

	fstr += strspn(fstr, " ");

	if (*fstr == '@') {
		fstr += 1;
		err = parse_num_or_str(&fstr, NULL, &filt->filename, " ,");
		if (err)
			goto out_err;
	}

	fstr += strspn(fstr, " ,");

	*filter_inp += fstr - filt->str;

	return 0;

out_err:
	addr_filter__free_str(filt);

	return err;
}

int addr_filters__parse_bare_filter(struct addr_filters *filts,
				    const char *filter)
{
	struct addr_filter *filt;
	const char *fstr = filter;
	int err;

	while (*fstr) {
		filt = addr_filter__new();
		err = parse_one_filter(filt, &fstr);
		if (err) {
			addr_filter__free(filt);
			addr_filters__exit(filts);
			return err;
		}
		addr_filters__add(filts, filt);
	}

	return 0;
}

struct sym_args {
	const char	*name;
	u64		start;
	u64		size;
	int		idx;
	int		cnt;
	bool		started;
	bool		global;
	bool		selected;
	bool		duplicate;
	bool		near;
};

static bool kern_sym_match(struct sym_args *args, const char *name, char type)
{
	/* A function with the same name, and global or the n'th found or any */
	return symbol_type__is_a(type, MAP__FUNCTION) &&
	       !strcmp(name, args->name) &&
	       ((args->global && isupper(type)) ||
		(args->selected && ++(args->cnt) == args->idx) ||
		(!args->global && !args->selected));
}

static int find_kern_sym_cb(void *arg, const char *name, char type, u64 start)
{
	struct sym_args *args = arg;

	if (args->started) {
		if (!args->size)
			args->size = start - args->start;
		if (args->selected) {
			if (args->size)
				return 1;
		} else if (kern_sym_match(args, name, type)) {
			args->duplicate = true;
			return 1;
		}
	} else if (kern_sym_match(args, name, type)) {
		args->started = true;
		args->start = start;
	}

	return 0;
}

static int print_kern_sym_cb(void *arg, const char *name, char type, u64 start)
{
	struct sym_args *args = arg;

	if (kern_sym_match(args, name, type)) {
		pr_err("#%d\t0x%"PRIx64"\t%c\t%s\n",
		       ++args->cnt, start, type, name);
		args->near = true;
	} else if (args->near) {
		args->near = false;
		pr_err("\t\twhich is near\t\t%s\n", name);
	}

	return 0;
}

static int sym_not_found_error(const char *sym_name, int idx)
{
	if (idx > 0) {
		pr_err("N'th occurrence (N=%d) of symbol '%s' not found.\n",
		       idx, sym_name);
	} else if (!idx) {
		pr_err("Global symbol '%s' not found.\n", sym_name);
	} else {
		pr_err("Symbol '%s' not found.\n", sym_name);
	}
	pr_err("Note that symbols must be functions.\n");

	return -EINVAL;
}

static int find_kern_sym(const char *sym_name, u64 *start, u64 *size, int idx)
{
	struct sym_args args = {
		.name = sym_name,
		.idx = idx,
		.global = !idx,
		.selected = idx > 0,
	};
	int err;

	*start = 0;
	*size = 0;

	err = kallsyms__parse("/proc/kallsyms", &args, find_kern_sym_cb);
	if (err < 0) {
		pr_err("Failed to parse /proc/kallsyms\n");
		return err;
	}

	if (args.duplicate) {
		pr_err("Multiple kernel symbols with name '%s'\n", sym_name);
		args.cnt = 0;
		kallsyms__parse("/proc/kallsyms", &args, print_kern_sym_cb);
		pr_err("Disambiguate symbol name by inserting #n after the name e.g. %s #2\n",
		       sym_name);
		pr_err("Or select a global symbol by inserting #0 or #g or #G\n");
		return -EINVAL;
	}

	if (!args.started) {
		pr_err("Kernel symbol lookup: ");
		return sym_not_found_error(sym_name, idx);
	}

	*start = args.start;
	*size = args.size;

	return 0;
}

static int find_entire_kern_cb(void *arg, const char *name __maybe_unused,
			       char type, u64 start)
{
	struct sym_args *args = arg;

	if (!symbol_type__is_a(type, MAP__FUNCTION))
		return 0;

	if (!args->started) {
		args->started = true;
		args->start = start;
	}
	/* Don't know exactly where the kernel ends, so we add a page */
	args->size = round_up(start, page_size) + page_size - args->start;

	return 0;
}

static int addr_filter__entire_kernel(struct addr_filter *filt)
{
	struct sym_args args = { .started = false };
	int err;

	err = kallsyms__parse("/proc/kallsyms", &args, find_entire_kern_cb);
	if (err < 0 || !args.started) {
		pr_err("Failed to parse /proc/kallsyms\n");
		return err;
	}

	filt->addr = args.start;
	filt->size = args.size;

	return 0;
}

static int check_end_after_start(struct addr_filter *filt, u64 start, u64 size)
{
	if (start + size >= filt->addr)
		return 0;

	if (filt->sym_from) {
		pr_err("Symbol '%s' (0x%"PRIx64") comes before '%s' (0x%"PRIx64")\n",
		       filt->sym_to, start, filt->sym_from, filt->addr);
	} else {
		pr_err("Symbol '%s' (0x%"PRIx64") comes before address 0x%"PRIx64")\n",
		       filt->sym_to, start, filt->addr);
	}

	return -EINVAL;
}

static int addr_filter__resolve_kernel_syms(struct addr_filter *filt)
{
	bool no_size = false;
	u64 start, size;
	int err;

	if (symbol_conf.kptr_restrict) {
		pr_err("Kernel addresses are restricted. Unable to resolve kernel symbols.\n");
		return -EINVAL;
	}

	if (filt->sym_from && !strcmp(filt->sym_from, "*"))
		return addr_filter__entire_kernel(filt);

	if (filt->sym_from) {
		err = find_kern_sym(filt->sym_from, &start, &size,
				    filt->sym_from_idx);
		if (err)
			return err;
		filt->addr = start;
		if (filt->range && !filt->size && !filt->sym_to) {
			filt->size = size;
			no_size = !size;
		}
	}

	if (filt->sym_to) {
		err = find_kern_sym(filt->sym_to, &start, &size,
				    filt->sym_to_idx);
		if (err)
			return err;

		err = check_end_after_start(filt, start, size);
		if (err)
			return err;
		filt->size = start + size - filt->addr;
		no_size = !size;
	}

	/* The very last symbol in kallsyms does not imply a particular size */
	if (no_size) {
		pr_err("Cannot determine size of symbol '%s'\n",
		       filt->sym_to ? filt->sym_to : filt->sym_from);
		return -EINVAL;
	}

	return 0;
}

static struct dso *load_dso(const char *name)
{
	struct map *map;
	struct dso *dso;

	map = dso__new_map(name);
	if (!map)
		return NULL;

	map__load(map);

	dso = dso__get(map->dso);

	map__put(map);

	return dso;
}

static bool dso_sym_match(struct symbol *sym, const char *name, int *cnt,
			  int idx)
{
	/* Same name, and global or the n'th found or any */
	return !arch__compare_symbol_names(name, sym->name) &&
	       ((!idx && sym->binding == STB_GLOBAL) ||
		(idx > 0 && ++*cnt == idx) ||
		idx < 0);
}

static void print_duplicate_syms(struct dso *dso, const char *sym_name)
{
	struct symbol *sym;
	bool near = false;
	int cnt = 0;

	pr_err("Multiple symbols with name '%s'\n", sym_name);

	sym = dso__first_symbol(dso, MAP__FUNCTION);
	while (sym) {
		if (dso_sym_match(sym, sym_name, &cnt, -1)) {
			pr_err("#%d\t0x%"PRIx64"\t%c\t%s\n",
			       ++cnt, sym->start,
			       sym->binding == STB_GLOBAL ? 'g' :
			       sym->binding == STB_LOCAL  ? 'l' : 'w',
			       sym->name);
			near = true;
		} else if (near) {
			near = false;
			pr_err("\t\twhich is near\t\t%s\n", sym->name);
		}
		sym = dso__next_symbol(sym);
	}

	pr_err("Disambiguate symbol name by inserting #n after the name e.g. %s #2\n",
	       sym_name);
	pr_err("Or select a global symbol by inserting #0 or #g or #G\n");
}

static int find_dso_sym(struct dso *dso, const char *sym_name, u64 *start,
			u64 *size, int idx)
{
	struct symbol *sym;
	int cnt = 0;

	*start = 0;
	*size = 0;

	sym = dso__first_symbol(dso, MAP__FUNCTION);
	while (sym) {
		if (*start) {
			if (!*size)
				*size = sym->start - *start;
			if (idx > 0) {
				if (*size)
					return 1;
			} else if (dso_sym_match(sym, sym_name, &cnt, idx)) {
				print_duplicate_syms(dso, sym_name);
				return -EINVAL;
			}
		} else if (dso_sym_match(sym, sym_name, &cnt, idx)) {
			*start = sym->start;
			*size = sym->end - sym->start;
		}
		sym = dso__next_symbol(sym);
	}

	if (!*start)
		return sym_not_found_error(sym_name, idx);

	return 0;
}

static int addr_filter__entire_dso(struct addr_filter *filt, struct dso *dso)
{
	struct symbol *first_sym = dso__first_symbol(dso, MAP__FUNCTION);
	struct symbol *last_sym = dso__last_symbol(dso, MAP__FUNCTION);

	if (!first_sym || !last_sym) {
		pr_err("Failed to determine filter for %s\nNo symbols found.\n",
		       filt->filename);
		return -EINVAL;
	}

	filt->addr = first_sym->start;
	filt->size = last_sym->end - first_sym->start;

	return 0;
}

static int addr_filter__resolve_syms(struct addr_filter *filt)
{
	u64 start, size;
	struct dso *dso;
	int err = 0;

	if (!filt->sym_from && !filt->sym_to)
		return 0;

	if (!filt->filename)
		return addr_filter__resolve_kernel_syms(filt);

	dso = load_dso(filt->filename);
	if (!dso) {
		pr_err("Failed to load symbols from: %s\n", filt->filename);
		return -EINVAL;
	}

	if (filt->sym_from && !strcmp(filt->sym_from, "*")) {
		err = addr_filter__entire_dso(filt, dso);
		goto put_dso;
	}

	if (filt->sym_from) {
		err = find_dso_sym(dso, filt->sym_from, &start, &size,
				   filt->sym_from_idx);
		if (err)
			goto put_dso;
		filt->addr = start;
		if (filt->range && !filt->size && !filt->sym_to)
			filt->size = size;
	}

	if (filt->sym_to) {
		err = find_dso_sym(dso, filt->sym_to, &start, &size,
				   filt->sym_to_idx);
		if (err)
			goto put_dso;

		err = check_end_after_start(filt, start, size);
		if (err)
			return err;

		filt->size = start + size - filt->addr;
	}

put_dso:
	dso__put(dso);

	return err;
}

static char *addr_filter__to_str(struct addr_filter *filt)
{
	char filename_buf[PATH_MAX];
	const char *at = "";
	const char *fn = "";
	char *filter;
	int err;

	if (filt->filename) {
		at = "@";
		fn = realpath(filt->filename, filename_buf);
		if (!fn)
			return NULL;
	}

	if (filt->range) {
		err = asprintf(&filter, "%s 0x%"PRIx64"/0x%"PRIx64"%s%s",
			       filt->action, filt->addr, filt->size, at, fn);
	} else {
		err = asprintf(&filter, "%s 0x%"PRIx64"%s%s",
			       filt->action, filt->addr, at, fn);
	}

	return err < 0 ? NULL : filter;
}

static int parse_addr_filter(struct perf_evsel *evsel, const char *filter,
			     int max_nr)
{
	struct addr_filters filts;
	struct addr_filter *filt;
	int err;

	addr_filters__init(&filts);

	err = addr_filters__parse_bare_filter(&filts, filter);
	if (err)
		goto out_exit;

	if (filts.cnt > max_nr) {
		pr_err("Error: number of address filters (%d) exceeds maximum (%d)\n",
		       filts.cnt, max_nr);
		err = -EINVAL;
		goto out_exit;
	}

	list_for_each_entry(filt, &filts.head, list) {
		char *new_filter;

		err = addr_filter__resolve_syms(filt);
		if (err)
			goto out_exit;

		new_filter = addr_filter__to_str(filt);
		if (!new_filter) {
			err = -ENOMEM;
			goto out_exit;
		}

		if (perf_evsel__append_addr_filter(evsel, new_filter)) {
			err = -ENOMEM;
			goto out_exit;
		}
	}

out_exit:
	addr_filters__exit(&filts);

	if (err) {
		pr_err("Failed to parse address filter: '%s'\n", filter);
		pr_err("Filter format is: filter|start|stop|tracestop <start symbol or address> [/ <end symbol or size>] [@<file name>]\n");
		pr_err("Where multiple filters are separated by space or comma.\n");
	}

	return err;
}

static struct perf_pmu *perf_evsel__find_pmu(struct perf_evsel *evsel)
{
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		if (pmu->type == evsel->attr.type)
			break;
	}

	return pmu;
}

static int perf_evsel__nr_addr_filter(struct perf_evsel *evsel)
{
	struct perf_pmu *pmu = perf_evsel__find_pmu(evsel);
	int nr_addr_filters = 0;

	if (!pmu)
		return 0;

	perf_pmu__scan_file(pmu, "nr_addr_filters", "%d", &nr_addr_filters);

	return nr_addr_filters;
}

int auxtrace_parse_filters(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	char *filter;
	int err, max_nr;

	evlist__for_each_entry(evlist, evsel) {
		filter = evsel->filter;
		max_nr = perf_evsel__nr_addr_filter(evsel);
		if (!filter || !max_nr)
			continue;
		evsel->filter = NULL;
		err = parse_addr_filter(evsel, filter, max_nr);
		free(filter);
		if (err)
			return err;
		pr_debug("Address filter: %s\n", evsel->filter);
	}

	return 0;
}
