// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel-bts.c: Intel Processor Trace support
 * Copyright (c) 2013-2015, Intel Corporation.
 */

#include <endian.h>
#include <errno.h>
#include <byteswap.h>
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/zalloc.h>

#include "cpumap.h"
#include "color.h"
#include "evsel.h"
#include "evlist.h"
#include "machine.h"
#include "map.h"
#include "symbol.h"
#include "session.h"
#include "thread.h"
#include "thread-stack.h"
#include "debug.h"
#include "tsc.h"
#include "auxtrace.h"
#include "intel-pt-decoder/intel-pt-insn-decoder.h"
#include "intel-bts.h"

#define MAX_TIMESTAMP (~0ULL)

#define INTEL_BTS_ERR_NOINSN  5
#define INTEL_BTS_ERR_LOST    9

#if __BYTE_ORDER == __BIG_ENDIAN
#define le64_to_cpu bswap_64
#else
#define le64_to_cpu
#endif

struct intel_bts {
	struct auxtrace			auxtrace;
	struct auxtrace_queues		queues;
	struct auxtrace_heap		heap;
	u32				auxtrace_type;
	struct perf_session		*session;
	struct machine			*machine;
	bool				sampling_mode;
	bool				snapshot_mode;
	bool				data_queued;
	u32				pmu_type;
	struct perf_tsc_conversion	tc;
	bool				cap_user_time_zero;
	struct itrace_synth_opts	synth_opts;
	bool				sample_branches;
	u32				branches_filter;
	u64				branches_sample_type;
	u64				branches_id;
	size_t				branches_event_size;
	unsigned long			num_events;
};

struct intel_bts_queue {
	struct intel_bts	*bts;
	unsigned int		queue_nr;
	struct auxtrace_buffer	*buffer;
	bool			on_heap;
	bool			done;
	pid_t			pid;
	pid_t			tid;
	int			cpu;
	u64			time;
	struct intel_pt_insn	intel_pt_insn;
	u32			sample_flags;
};

struct branch {
	u64 from;
	u64 to;
	u64 misc;
};

static void intel_bts_dump(struct intel_bts *bts __maybe_unused,
			   unsigned char *buf, size_t len)
{
	struct branch *branch;
	size_t i, pos = 0, br_sz = sizeof(struct branch), sz;
	const char *color = PERF_COLOR_BLUE;

	color_fprintf(stdout, color,
		      ". ... Intel BTS data: size %zu bytes\n",
		      len);

	while (len) {
		if (len >= br_sz)
			sz = br_sz;
		else
			sz = len;
		printf(".");
		color_fprintf(stdout, color, "  %08x: ", pos);
		for (i = 0; i < sz; i++)
			color_fprintf(stdout, color, " %02x", buf[i]);
		for (; i < br_sz; i++)
			color_fprintf(stdout, color, "   ");
		if (len >= br_sz) {
			branch = (struct branch *)buf;
			color_fprintf(stdout, color, " %"PRIx64" -> %"PRIx64" %s\n",
				      le64_to_cpu(branch->from),
				      le64_to_cpu(branch->to),
				      le64_to_cpu(branch->misc) & 0x10 ?
							"pred" : "miss");
		} else {
			color_fprintf(stdout, color, " Bad record!\n");
		}
		pos += sz;
		buf += sz;
		len -= sz;
	}
}

static void intel_bts_dump_event(struct intel_bts *bts, unsigned char *buf,
				 size_t len)
{
	printf(".\n");
	intel_bts_dump(bts, buf, len);
}

static int intel_bts_lost(struct intel_bts *bts, struct perf_sample *sample)
{
	union perf_event event;
	int err;

	auxtrace_synth_error(&event.auxtrace_error, PERF_AUXTRACE_ERROR_ITRACE,
			     INTEL_BTS_ERR_LOST, sample->cpu, sample->pid,
			     sample->tid, 0, "Lost trace data", sample->time);

	err = perf_session__deliver_synth_event(bts->session, &event, NULL);
	if (err)
		pr_err("Intel BTS: failed to deliver error event, error %d\n",
		       err);

	return err;
}

static struct intel_bts_queue *intel_bts_alloc_queue(struct intel_bts *bts,
						     unsigned int queue_nr)
{
	struct intel_bts_queue *btsq;

	btsq = zalloc(sizeof(struct intel_bts_queue));
	if (!btsq)
		return NULL;

	btsq->bts = bts;
	btsq->queue_nr = queue_nr;
	btsq->pid = -1;
	btsq->tid = -1;
	btsq->cpu = -1;

	return btsq;
}

static int intel_bts_setup_queue(struct intel_bts *bts,
				 struct auxtrace_queue *queue,
				 unsigned int queue_nr)
{
	struct intel_bts_queue *btsq = queue->priv;

	if (list_empty(&queue->head))
		return 0;

	if (!btsq) {
		btsq = intel_bts_alloc_queue(bts, queue_nr);
		if (!btsq)
			return -ENOMEM;
		queue->priv = btsq;

		if (queue->cpu != -1)
			btsq->cpu = queue->cpu;
		btsq->tid = queue->tid;
	}

	if (bts->sampling_mode)
		return 0;

	if (!btsq->on_heap && !btsq->buffer) {
		int ret;

		btsq->buffer = auxtrace_buffer__next(queue, NULL);
		if (!btsq->buffer)
			return 0;

		ret = auxtrace_heap__add(&bts->heap, queue_nr,
					 btsq->buffer->reference);
		if (ret)
			return ret;
		btsq->on_heap = true;
	}

	return 0;
}

static int intel_bts_setup_queues(struct intel_bts *bts)
{
	unsigned int i;
	int ret;

	for (i = 0; i < bts->queues.nr_queues; i++) {
		ret = intel_bts_setup_queue(bts, &bts->queues.queue_array[i],
					    i);
		if (ret)
			return ret;
	}
	return 0;
}

static inline int intel_bts_update_queues(struct intel_bts *bts)
{
	if (bts->queues.new_data) {
		bts->queues.new_data = false;
		return intel_bts_setup_queues(bts);
	}
	return 0;
}

static unsigned char *intel_bts_find_overlap(unsigned char *buf_a, size_t len_a,
					     unsigned char *buf_b, size_t len_b)
{
	size_t offs, len;

	if (len_a > len_b)
		offs = len_a - len_b;
	else
		offs = 0;

	for (; offs < len_a; offs += sizeof(struct branch)) {
		len = len_a - offs;
		if (!memcmp(buf_a + offs, buf_b, len))
			return buf_b + len;
	}

	return buf_b;
}

static int intel_bts_do_fix_overlap(struct auxtrace_queue *queue,
				    struct auxtrace_buffer *b)
{
	struct auxtrace_buffer *a;
	void *start;

	if (b->list.prev == &queue->head)
		return 0;
	a = list_entry(b->list.prev, struct auxtrace_buffer, list);
	start = intel_bts_find_overlap(a->data, a->size, b->data, b->size);
	if (!start)
		return -EINVAL;
	b->use_size = b->data + b->size - start;
	b->use_data = start;
	return 0;
}

static inline u8 intel_bts_cpumode(struct intel_bts *bts, uint64_t ip)
{
	return machine__kernel_ip(bts->machine, ip) ?
	       PERF_RECORD_MISC_KERNEL :
	       PERF_RECORD_MISC_USER;
}

static int intel_bts_synth_branch_sample(struct intel_bts_queue *btsq,
					 struct branch *branch)
{
	int ret;
	struct intel_bts *bts = btsq->bts;
	union perf_event event;
	struct perf_sample sample = { .ip = 0, };

	if (bts->synth_opts.initial_skip &&
	    bts->num_events++ <= bts->synth_opts.initial_skip)
		return 0;

	sample.ip = le64_to_cpu(branch->from);
	sample.cpumode = intel_bts_cpumode(bts, sample.ip);
	sample.pid = btsq->pid;
	sample.tid = btsq->tid;
	sample.addr = le64_to_cpu(branch->to);
	sample.id = btsq->bts->branches_id;
	sample.stream_id = btsq->bts->branches_id;
	sample.period = 1;
	sample.cpu = btsq->cpu;
	sample.flags = btsq->sample_flags;
	sample.insn_len = btsq->intel_pt_insn.length;
	memcpy(sample.insn, btsq->intel_pt_insn.buf, INTEL_PT_INSN_BUF_SZ);

	event.sample.header.type = PERF_RECORD_SAMPLE;
	event.sample.header.misc = sample.cpumode;
	event.sample.header.size = sizeof(struct perf_event_header);

	if (bts->synth_opts.inject) {
		event.sample.header.size = bts->branches_event_size;
		ret = perf_event__synthesize_sample(&event,
						    bts->branches_sample_type,
						    0, &sample);
		if (ret)
			return ret;
	}

	ret = perf_session__deliver_synth_event(bts->session, &event, &sample);
	if (ret)
		pr_err("Intel BTS: failed to deliver branch event, error %d\n",
		       ret);

	return ret;
}

static int intel_bts_get_next_insn(struct intel_bts_queue *btsq, u64 ip)
{
	struct machine *machine = btsq->bts->machine;
	struct thread *thread;
	unsigned char buf[INTEL_PT_INSN_BUF_SZ];
	ssize_t len;
	bool x86_64;
	int err = -1;

	thread = machine__find_thread(machine, -1, btsq->tid);
	if (!thread)
		return -1;

	len = thread__memcpy(thread, machine, buf, ip, INTEL_PT_INSN_BUF_SZ, &x86_64);
	if (len <= 0)
		goto out_put;

	if (intel_pt_get_insn(buf, len, x86_64, &btsq->intel_pt_insn))
		goto out_put;

	err = 0;
out_put:
	thread__put(thread);
	return err;
}

static int intel_bts_synth_error(struct intel_bts *bts, int cpu, pid_t pid,
				 pid_t tid, u64 ip)
{
	union perf_event event;
	int err;

	auxtrace_synth_error(&event.auxtrace_error, PERF_AUXTRACE_ERROR_ITRACE,
			     INTEL_BTS_ERR_NOINSN, cpu, pid, tid, ip,
			     "Failed to get instruction", 0);

	err = perf_session__deliver_synth_event(bts->session, &event, NULL);
	if (err)
		pr_err("Intel BTS: failed to deliver error event, error %d\n",
		       err);

	return err;
}

static int intel_bts_get_branch_type(struct intel_bts_queue *btsq,
				     struct branch *branch)
{
	int err;

	if (!branch->from) {
		if (branch->to)
			btsq->sample_flags = PERF_IP_FLAG_BRANCH |
					     PERF_IP_FLAG_TRACE_BEGIN;
		else
			btsq->sample_flags = 0;
		btsq->intel_pt_insn.length = 0;
	} else if (!branch->to) {
		btsq->sample_flags = PERF_IP_FLAG_BRANCH |
				     PERF_IP_FLAG_TRACE_END;
		btsq->intel_pt_insn.length = 0;
	} else {
		err = intel_bts_get_next_insn(btsq, branch->from);
		if (err) {
			btsq->sample_flags = 0;
			btsq->intel_pt_insn.length = 0;
			if (!btsq->bts->synth_opts.errors)
				return 0;
			err = intel_bts_synth_error(btsq->bts, btsq->cpu,
						    btsq->pid, btsq->tid,
						    branch->from);
			return err;
		}
		btsq->sample_flags = intel_pt_insn_type(btsq->intel_pt_insn.op);
		/* Check for an async branch into the kernel */
		if (!machine__kernel_ip(btsq->bts->machine, branch->from) &&
		    machine__kernel_ip(btsq->bts->machine, branch->to) &&
		    btsq->sample_flags != (PERF_IP_FLAG_BRANCH |
					   PERF_IP_FLAG_CALL |
					   PERF_IP_FLAG_SYSCALLRET))
			btsq->sample_flags = PERF_IP_FLAG_BRANCH |
					     PERF_IP_FLAG_CALL |
					     PERF_IP_FLAG_ASYNC |
					     PERF_IP_FLAG_INTERRUPT;
	}

	return 0;
}

static int intel_bts_process_buffer(struct intel_bts_queue *btsq,
				    struct auxtrace_buffer *buffer,
				    struct thread *thread)
{
	struct branch *branch;
	size_t sz, bsz = sizeof(struct branch);
	u32 filter = btsq->bts->branches_filter;
	int err = 0;

	if (buffer->use_data) {
		sz = buffer->use_size;
		branch = buffer->use_data;
	} else {
		sz = buffer->size;
		branch = buffer->data;
	}

	if (!btsq->bts->sample_branches)
		return 0;

	for (; sz > bsz; branch += 1, sz -= bsz) {
		if (!branch->from && !branch->to)
			continue;
		intel_bts_get_branch_type(btsq, branch);
		if (btsq->bts->synth_opts.thread_stack)
			thread_stack__event(thread, btsq->cpu, btsq->sample_flags,
					    le64_to_cpu(branch->from),
					    le64_to_cpu(branch->to),
					    btsq->intel_pt_insn.length,
					    buffer->buffer_nr + 1);
		if (filter && !(filter & btsq->sample_flags))
			continue;
		err = intel_bts_synth_branch_sample(btsq, branch);
		if (err)
			break;
	}
	return err;
}

static int intel_bts_process_queue(struct intel_bts_queue *btsq, u64 *timestamp)
{
	struct auxtrace_buffer *buffer = btsq->buffer, *old_buffer = buffer;
	struct auxtrace_queue *queue;
	struct thread *thread;
	int err;

	if (btsq->done)
		return 1;

	if (btsq->pid == -1) {
		thread = machine__find_thread(btsq->bts->machine, -1,
					      btsq->tid);
		if (thread)
			btsq->pid = thread->pid_;
	} else {
		thread = machine__findnew_thread(btsq->bts->machine, btsq->pid,
						 btsq->tid);
	}

	queue = &btsq->bts->queues.queue_array[btsq->queue_nr];

	if (!buffer)
		buffer = auxtrace_buffer__next(queue, NULL);

	if (!buffer) {
		if (!btsq->bts->sampling_mode)
			btsq->done = 1;
		err = 1;
		goto out_put;
	}

	/* Currently there is no support for split buffers */
	if (buffer->consecutive) {
		err = -EINVAL;
		goto out_put;
	}

	if (!buffer->data) {
		int fd = perf_data__fd(btsq->bts->session->data);

		buffer->data = auxtrace_buffer__get_data(buffer, fd);
		if (!buffer->data) {
			err = -ENOMEM;
			goto out_put;
		}
	}

	if (btsq->bts->snapshot_mode && !buffer->consecutive &&
	    intel_bts_do_fix_overlap(queue, buffer)) {
		err = -ENOMEM;
		goto out_put;
	}

	if (!btsq->bts->synth_opts.callchain &&
	    !btsq->bts->synth_opts.thread_stack && thread &&
	    (!old_buffer || btsq->bts->sampling_mode ||
	     (btsq->bts->snapshot_mode && !buffer->consecutive)))
		thread_stack__set_trace_nr(thread, btsq->cpu, buffer->buffer_nr + 1);

	err = intel_bts_process_buffer(btsq, buffer, thread);

	auxtrace_buffer__drop_data(buffer);

	btsq->buffer = auxtrace_buffer__next(queue, buffer);
	if (btsq->buffer) {
		if (timestamp)
			*timestamp = btsq->buffer->reference;
	} else {
		if (!btsq->bts->sampling_mode)
			btsq->done = 1;
	}
out_put:
	thread__put(thread);
	return err;
}

static int intel_bts_flush_queue(struct intel_bts_queue *btsq)
{
	u64 ts = 0;
	int ret;

	while (1) {
		ret = intel_bts_process_queue(btsq, &ts);
		if (ret < 0)
			return ret;
		if (ret)
			break;
	}
	return 0;
}

static int intel_bts_process_tid_exit(struct intel_bts *bts, pid_t tid)
{
	struct auxtrace_queues *queues = &bts->queues;
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++) {
		struct auxtrace_queue *queue = &bts->queues.queue_array[i];
		struct intel_bts_queue *btsq = queue->priv;

		if (btsq && btsq->tid == tid)
			return intel_bts_flush_queue(btsq);
	}
	return 0;
}

static int intel_bts_process_queues(struct intel_bts *bts, u64 timestamp)
{
	while (1) {
		unsigned int queue_nr;
		struct auxtrace_queue *queue;
		struct intel_bts_queue *btsq;
		u64 ts = 0;
		int ret;

		if (!bts->heap.heap_cnt)
			return 0;

		if (bts->heap.heap_array[0].ordinal > timestamp)
			return 0;

		queue_nr = bts->heap.heap_array[0].queue_nr;
		queue = &bts->queues.queue_array[queue_nr];
		btsq = queue->priv;

		auxtrace_heap__pop(&bts->heap);

		ret = intel_bts_process_queue(btsq, &ts);
		if (ret < 0) {
			auxtrace_heap__add(&bts->heap, queue_nr, ts);
			return ret;
		}

		if (!ret) {
			ret = auxtrace_heap__add(&bts->heap, queue_nr, ts);
			if (ret < 0)
				return ret;
		} else {
			btsq->on_heap = false;
		}
	}

	return 0;
}

static int intel_bts_process_event(struct perf_session *session,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct perf_tool *tool)
{
	struct intel_bts *bts = container_of(session->auxtrace, struct intel_bts,
					     auxtrace);
	u64 timestamp;
	int err;

	if (dump_trace)
		return 0;

	if (!tool->ordered_events) {
		pr_err("Intel BTS requires ordered events\n");
		return -EINVAL;
	}

	if (sample->time && sample->time != (u64)-1)
		timestamp = perf_time_to_tsc(sample->time, &bts->tc);
	else
		timestamp = 0;

	err = intel_bts_update_queues(bts);
	if (err)
		return err;

	err = intel_bts_process_queues(bts, timestamp);
	if (err)
		return err;
	if (event->header.type == PERF_RECORD_EXIT) {
		err = intel_bts_process_tid_exit(bts, event->fork.tid);
		if (err)
			return err;
	}

	if (event->header.type == PERF_RECORD_AUX &&
	    (event->aux.flags & PERF_AUX_FLAG_TRUNCATED) &&
	    bts->synth_opts.errors)
		err = intel_bts_lost(bts, sample);

	return err;
}

static int intel_bts_process_auxtrace_event(struct perf_session *session,
					    union perf_event *event,
					    struct perf_tool *tool __maybe_unused)
{
	struct intel_bts *bts = container_of(session->auxtrace, struct intel_bts,
					     auxtrace);

	if (bts->sampling_mode)
		return 0;

	if (!bts->data_queued) {
		struct auxtrace_buffer *buffer;
		off_t data_offset;
		int fd = perf_data__fd(session->data);
		int err;

		if (perf_data__is_pipe(session->data)) {
			data_offset = 0;
		} else {
			data_offset = lseek(fd, 0, SEEK_CUR);
			if (data_offset == -1)
				return -errno;
		}

		err = auxtrace_queues__add_event(&bts->queues, session, event,
						 data_offset, &buffer);
		if (err)
			return err;

		/* Dump here now we have copied a piped trace out of the pipe */
		if (dump_trace) {
			if (auxtrace_buffer__get_data(buffer, fd)) {
				intel_bts_dump_event(bts, buffer->data,
						     buffer->size);
				auxtrace_buffer__put_data(buffer);
			}
		}
	}

	return 0;
}

static int intel_bts_flush(struct perf_session *session,
			   struct perf_tool *tool __maybe_unused)
{
	struct intel_bts *bts = container_of(session->auxtrace, struct intel_bts,
					     auxtrace);
	int ret;

	if (dump_trace || bts->sampling_mode)
		return 0;

	if (!tool->ordered_events)
		return -EINVAL;

	ret = intel_bts_update_queues(bts);
	if (ret < 0)
		return ret;

	return intel_bts_process_queues(bts, MAX_TIMESTAMP);
}

static void intel_bts_free_queue(void *priv)
{
	struct intel_bts_queue *btsq = priv;

	if (!btsq)
		return;
	free(btsq);
}

static void intel_bts_free_events(struct perf_session *session)
{
	struct intel_bts *bts = container_of(session->auxtrace, struct intel_bts,
					     auxtrace);
	struct auxtrace_queues *queues = &bts->queues;
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++) {
		intel_bts_free_queue(queues->queue_array[i].priv);
		queues->queue_array[i].priv = NULL;
	}
	auxtrace_queues__free(queues);
}

static void intel_bts_free(struct perf_session *session)
{
	struct intel_bts *bts = container_of(session->auxtrace, struct intel_bts,
					     auxtrace);

	auxtrace_heap__free(&bts->heap);
	intel_bts_free_events(session);
	session->auxtrace = NULL;
	free(bts);
}

struct intel_bts_synth {
	struct perf_tool dummy_tool;
	struct perf_session *session;
};

static int intel_bts_event_synth(struct perf_tool *tool,
				 union perf_event *event,
				 struct perf_sample *sample __maybe_unused,
				 struct machine *machine __maybe_unused)
{
	struct intel_bts_synth *intel_bts_synth =
			container_of(tool, struct intel_bts_synth, dummy_tool);

	return perf_session__deliver_synth_event(intel_bts_synth->session,
						 event, NULL);
}

static int intel_bts_synth_event(struct perf_session *session,
				 struct perf_event_attr *attr, u64 id)
{
	struct intel_bts_synth intel_bts_synth;

	memset(&intel_bts_synth, 0, sizeof(struct intel_bts_synth));
	intel_bts_synth.session = session;

	return perf_event__synthesize_attr(&intel_bts_synth.dummy_tool, attr, 1,
					   &id, intel_bts_event_synth);
}

static int intel_bts_synth_events(struct intel_bts *bts,
				  struct perf_session *session)
{
	struct perf_evlist *evlist = session->evlist;
	struct perf_evsel *evsel;
	struct perf_event_attr attr;
	bool found = false;
	u64 id;
	int err;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->attr.type == bts->pmu_type && evsel->ids) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("There are no selected events with Intel BTS data\n");
		return 0;
	}

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.type = PERF_TYPE_HARDWARE;
	attr.sample_type = evsel->attr.sample_type & PERF_SAMPLE_MASK;
	attr.sample_type |= PERF_SAMPLE_IP | PERF_SAMPLE_TID |
			    PERF_SAMPLE_PERIOD;
	attr.sample_type &= ~(u64)PERF_SAMPLE_TIME;
	attr.sample_type &= ~(u64)PERF_SAMPLE_CPU;
	attr.exclude_user = evsel->attr.exclude_user;
	attr.exclude_kernel = evsel->attr.exclude_kernel;
	attr.exclude_hv = evsel->attr.exclude_hv;
	attr.exclude_host = evsel->attr.exclude_host;
	attr.exclude_guest = evsel->attr.exclude_guest;
	attr.sample_id_all = evsel->attr.sample_id_all;
	attr.read_format = evsel->attr.read_format;

	id = evsel->id[0] + 1000000000;
	if (!id)
		id = 1;

	if (bts->synth_opts.branches) {
		attr.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
		attr.sample_period = 1;
		attr.sample_type |= PERF_SAMPLE_ADDR;
		pr_debug("Synthesizing 'branches' event with id %" PRIu64 " sample type %#" PRIx64 "\n",
			 id, (u64)attr.sample_type);
		err = intel_bts_synth_event(session, &attr, id);
		if (err) {
			pr_err("%s: failed to synthesize 'branches' event type\n",
			       __func__);
			return err;
		}
		bts->sample_branches = true;
		bts->branches_sample_type = attr.sample_type;
		bts->branches_id = id;
		/*
		 * We only use sample types from PERF_SAMPLE_MASK so we can use
		 * __perf_evsel__sample_size() here.
		 */
		bts->branches_event_size = sizeof(struct sample_event) +
				__perf_evsel__sample_size(attr.sample_type);
	}

	return 0;
}

static const char * const intel_bts_info_fmts[] = {
	[INTEL_BTS_PMU_TYPE]		= "  PMU Type           %"PRId64"\n",
	[INTEL_BTS_TIME_SHIFT]		= "  Time Shift         %"PRIu64"\n",
	[INTEL_BTS_TIME_MULT]		= "  Time Muliplier     %"PRIu64"\n",
	[INTEL_BTS_TIME_ZERO]		= "  Time Zero          %"PRIu64"\n",
	[INTEL_BTS_CAP_USER_TIME_ZERO]	= "  Cap Time Zero      %"PRId64"\n",
	[INTEL_BTS_SNAPSHOT_MODE]	= "  Snapshot mode      %"PRId64"\n",
};

static void intel_bts_print_info(u64 *arr, int start, int finish)
{
	int i;

	if (!dump_trace)
		return;

	for (i = start; i <= finish; i++)
		fprintf(stdout, intel_bts_info_fmts[i], arr[i]);
}

int intel_bts_process_auxtrace_info(union perf_event *event,
				    struct perf_session *session)
{
	struct auxtrace_info_event *auxtrace_info = &event->auxtrace_info;
	size_t min_sz = sizeof(u64) * INTEL_BTS_SNAPSHOT_MODE;
	struct intel_bts *bts;
	int err;

	if (auxtrace_info->header.size < sizeof(struct auxtrace_info_event) +
					min_sz)
		return -EINVAL;

	bts = zalloc(sizeof(struct intel_bts));
	if (!bts)
		return -ENOMEM;

	err = auxtrace_queues__init(&bts->queues);
	if (err)
		goto err_free;

	bts->session = session;
	bts->machine = &session->machines.host; /* No kvm support */
	bts->auxtrace_type = auxtrace_info->type;
	bts->pmu_type = auxtrace_info->priv[INTEL_BTS_PMU_TYPE];
	bts->tc.time_shift = auxtrace_info->priv[INTEL_BTS_TIME_SHIFT];
	bts->tc.time_mult = auxtrace_info->priv[INTEL_BTS_TIME_MULT];
	bts->tc.time_zero = auxtrace_info->priv[INTEL_BTS_TIME_ZERO];
	bts->cap_user_time_zero =
			auxtrace_info->priv[INTEL_BTS_CAP_USER_TIME_ZERO];
	bts->snapshot_mode = auxtrace_info->priv[INTEL_BTS_SNAPSHOT_MODE];

	bts->sampling_mode = false;

	bts->auxtrace.process_event = intel_bts_process_event;
	bts->auxtrace.process_auxtrace_event = intel_bts_process_auxtrace_event;
	bts->auxtrace.flush_events = intel_bts_flush;
	bts->auxtrace.free_events = intel_bts_free_events;
	bts->auxtrace.free = intel_bts_free;
	session->auxtrace = &bts->auxtrace;

	intel_bts_print_info(&auxtrace_info->priv[0], INTEL_BTS_PMU_TYPE,
			     INTEL_BTS_SNAPSHOT_MODE);

	if (dump_trace)
		return 0;

	if (session->itrace_synth_opts && session->itrace_synth_opts->set) {
		bts->synth_opts = *session->itrace_synth_opts;
	} else {
		itrace_synth_opts__set_default(&bts->synth_opts,
				session->itrace_synth_opts->default_no_sample);
		if (session->itrace_synth_opts)
			bts->synth_opts.thread_stack =
				session->itrace_synth_opts->thread_stack;
	}

	if (bts->synth_opts.calls)
		bts->branches_filter |= PERF_IP_FLAG_CALL | PERF_IP_FLAG_ASYNC |
					PERF_IP_FLAG_TRACE_END;
	if (bts->synth_opts.returns)
		bts->branches_filter |= PERF_IP_FLAG_RETURN |
					PERF_IP_FLAG_TRACE_BEGIN;

	err = intel_bts_synth_events(bts, session);
	if (err)
		goto err_free_queues;

	err = auxtrace_queues__process_index(&bts->queues, session);
	if (err)
		goto err_free_queues;

	if (bts->queues.populated)
		bts->data_queued = true;

	return 0;

err_free_queues:
	auxtrace_queues__free(&bts->queues);
	session->auxtrace = NULL;
err_free:
	free(bts);
	return err;
}
