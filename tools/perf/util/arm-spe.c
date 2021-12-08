// SPDX-License-Identifier: GPL-2.0
/*
 * Arm Statistical Profiling Extensions (SPE) support
 * Copyright (c) 2017-2018, Arm Ltd.
 */

#include <byteswap.h>
#include <endian.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/types.h>
#include <linux/zalloc.h>
#include <stdlib.h>
#include <unistd.h>

#include "auxtrace.h"
#include "color.h"
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "machine.h"
#include "session.h"
#include "symbol.h"
#include "thread.h"
#include "thread-stack.h"
#include "tool.h"
#include "util/synthetic-events.h"

#include "arm-spe.h"
#include "arm-spe-decoder/arm-spe-decoder.h"
#include "arm-spe-decoder/arm-spe-pkt-decoder.h"

#define MAX_TIMESTAMP (~0ULL)

struct arm_spe {
	struct auxtrace			auxtrace;
	struct auxtrace_queues		queues;
	struct auxtrace_heap		heap;
	struct itrace_synth_opts        synth_opts;
	u32				auxtrace_type;
	struct perf_session		*session;
	struct machine			*machine;
	u32				pmu_type;

	u8				timeless_decoding;
	u8				data_queued;

	u64				sample_type;
	u8				sample_flc;
	u8				sample_llc;
	u8				sample_tlb;
	u8				sample_branch;
	u8				sample_remote_access;

	u64				l1d_miss_id;
	u64				l1d_access_id;
	u64				llc_miss_id;
	u64				llc_access_id;
	u64				tlb_miss_id;
	u64				tlb_access_id;
	u64				branch_miss_id;
	u64				remote_access_id;

	u64				kernel_start;

	unsigned long			num_events;
};

struct arm_spe_queue {
	struct arm_spe			*spe;
	unsigned int			queue_nr;
	struct auxtrace_buffer		*buffer;
	struct auxtrace_buffer		*old_buffer;
	union perf_event		*event_buf;
	bool				on_heap;
	bool				done;
	pid_t				pid;
	pid_t				tid;
	int				cpu;
	struct arm_spe_decoder		*decoder;
	u64				time;
	u64				timestamp;
	struct thread			*thread;
};

static void arm_spe_dump(struct arm_spe *spe __maybe_unused,
			 unsigned char *buf, size_t len)
{
	struct arm_spe_pkt packet;
	size_t pos = 0;
	int ret, pkt_len, i;
	char desc[ARM_SPE_PKT_DESC_MAX];
	const char *color = PERF_COLOR_BLUE;

	color_fprintf(stdout, color,
		      ". ... ARM SPE data: size %zu bytes\n",
		      len);

	while (len) {
		ret = arm_spe_get_packet(buf, len, &packet);
		if (ret > 0)
			pkt_len = ret;
		else
			pkt_len = 1;
		printf(".");
		color_fprintf(stdout, color, "  %08x: ", pos);
		for (i = 0; i < pkt_len; i++)
			color_fprintf(stdout, color, " %02x", buf[i]);
		for (; i < 16; i++)
			color_fprintf(stdout, color, "   ");
		if (ret > 0) {
			ret = arm_spe_pkt_desc(&packet, desc,
					       ARM_SPE_PKT_DESC_MAX);
			if (ret > 0)
				color_fprintf(stdout, color, " %s\n", desc);
		} else {
			color_fprintf(stdout, color, " Bad packet!\n");
		}
		pos += pkt_len;
		buf += pkt_len;
		len -= pkt_len;
	}
}

static void arm_spe_dump_event(struct arm_spe *spe, unsigned char *buf,
			       size_t len)
{
	printf(".\n");
	arm_spe_dump(spe, buf, len);
}

static int arm_spe_get_trace(struct arm_spe_buffer *b, void *data)
{
	struct arm_spe_queue *speq = data;
	struct auxtrace_buffer *buffer = speq->buffer;
	struct auxtrace_buffer *old_buffer = speq->old_buffer;
	struct auxtrace_queue *queue;

	queue = &speq->spe->queues.queue_array[speq->queue_nr];

	buffer = auxtrace_buffer__next(queue, buffer);
	/* If no more data, drop the previous auxtrace_buffer and return */
	if (!buffer) {
		if (old_buffer)
			auxtrace_buffer__drop_data(old_buffer);
		b->len = 0;
		return 0;
	}

	speq->buffer = buffer;

	/* If the aux_buffer doesn't have data associated, try to load it */
	if (!buffer->data) {
		/* get the file desc associated with the perf data file */
		int fd = perf_data__fd(speq->spe->session->data);

		buffer->data = auxtrace_buffer__get_data(buffer, fd);
		if (!buffer->data)
			return -ENOMEM;
	}

	b->len = buffer->size;
	b->buf = buffer->data;

	if (b->len) {
		if (old_buffer)
			auxtrace_buffer__drop_data(old_buffer);
		speq->old_buffer = buffer;
	} else {
		auxtrace_buffer__drop_data(buffer);
		return arm_spe_get_trace(b, data);
	}

	return 0;
}

static struct arm_spe_queue *arm_spe__alloc_queue(struct arm_spe *spe,
		unsigned int queue_nr)
{
	struct arm_spe_params params = { .get_trace = 0, };
	struct arm_spe_queue *speq;

	speq = zalloc(sizeof(*speq));
	if (!speq)
		return NULL;

	speq->event_buf = malloc(PERF_SAMPLE_MAX_SIZE);
	if (!speq->event_buf)
		goto out_free;

	speq->spe = spe;
	speq->queue_nr = queue_nr;
	speq->pid = -1;
	speq->tid = -1;
	speq->cpu = -1;

	/* params set */
	params.get_trace = arm_spe_get_trace;
	params.data = speq;

	/* create new decoder */
	speq->decoder = arm_spe_decoder_new(&params);
	if (!speq->decoder)
		goto out_free;

	return speq;

out_free:
	zfree(&speq->event_buf);
	free(speq);

	return NULL;
}

static inline u8 arm_spe_cpumode(struct arm_spe *spe, u64 ip)
{
	return ip >= spe->kernel_start ?
		PERF_RECORD_MISC_KERNEL :
		PERF_RECORD_MISC_USER;
}

static void arm_spe_prep_sample(struct arm_spe *spe,
				struct arm_spe_queue *speq,
				union perf_event *event,
				struct perf_sample *sample)
{
	struct arm_spe_record *record = &speq->decoder->record;

	if (!spe->timeless_decoding)
		sample->time = speq->timestamp;

	sample->ip = record->from_ip;
	sample->cpumode = arm_spe_cpumode(spe, sample->ip);
	sample->pid = speq->pid;
	sample->tid = speq->tid;
	sample->addr = record->to_ip;
	sample->period = 1;
	sample->cpu = speq->cpu;

	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = sample->cpumode;
	event->sample.header.size = sizeof(struct perf_event_header);
}

static int arm_spe__inject_event(union perf_event *event, struct perf_sample *sample, u64 type)
{
	event->header.size = perf_event__sample_event_size(sample, type, 0);
	return perf_event__synthesize_sample(event, type, 0, sample);
}

static inline int
arm_spe_deliver_synth_event(struct arm_spe *spe,
			    struct arm_spe_queue *speq __maybe_unused,
			    union perf_event *event,
			    struct perf_sample *sample)
{
	int ret;

	if (spe->synth_opts.inject) {
		ret = arm_spe__inject_event(event, sample, spe->sample_type);
		if (ret)
			return ret;
	}

	ret = perf_session__deliver_synth_event(spe->session, event, sample);
	if (ret)
		pr_err("ARM SPE: failed to deliver event, error %d\n", ret);

	return ret;
}

static int
arm_spe_synth_spe_events_sample(struct arm_spe_queue *speq,
				u64 spe_events_id)
{
	struct arm_spe *spe = speq->spe;
	union perf_event *event = speq->event_buf;
	struct perf_sample sample = { .ip = 0, };

	arm_spe_prep_sample(spe, speq, event, &sample);

	sample.id = spe_events_id;
	sample.stream_id = spe_events_id;

	return arm_spe_deliver_synth_event(spe, speq, event, &sample);
}

static int arm_spe_sample(struct arm_spe_queue *speq)
{
	const struct arm_spe_record *record = &speq->decoder->record;
	struct arm_spe *spe = speq->spe;
	int err;

	if (spe->sample_flc) {
		if (record->type & ARM_SPE_L1D_MISS) {
			err = arm_spe_synth_spe_events_sample(
					speq, spe->l1d_miss_id);
			if (err)
				return err;
		}

		if (record->type & ARM_SPE_L1D_ACCESS) {
			err = arm_spe_synth_spe_events_sample(
					speq, spe->l1d_access_id);
			if (err)
				return err;
		}
	}

	if (spe->sample_llc) {
		if (record->type & ARM_SPE_LLC_MISS) {
			err = arm_spe_synth_spe_events_sample(
					speq, spe->llc_miss_id);
			if (err)
				return err;
		}

		if (record->type & ARM_SPE_LLC_ACCESS) {
			err = arm_spe_synth_spe_events_sample(
					speq, spe->llc_access_id);
			if (err)
				return err;
		}
	}

	if (spe->sample_tlb) {
		if (record->type & ARM_SPE_TLB_MISS) {
			err = arm_spe_synth_spe_events_sample(
					speq, spe->tlb_miss_id);
			if (err)
				return err;
		}

		if (record->type & ARM_SPE_TLB_ACCESS) {
			err = arm_spe_synth_spe_events_sample(
					speq, spe->tlb_access_id);
			if (err)
				return err;
		}
	}

	if (spe->sample_branch && (record->type & ARM_SPE_BRANCH_MISS)) {
		err = arm_spe_synth_spe_events_sample(speq,
						      spe->branch_miss_id);
		if (err)
			return err;
	}

	if (spe->sample_remote_access &&
	    (record->type & ARM_SPE_REMOTE_ACCESS)) {
		err = arm_spe_synth_spe_events_sample(speq,
						      spe->remote_access_id);
		if (err)
			return err;
	}

	return 0;
}

static int arm_spe_run_decoder(struct arm_spe_queue *speq, u64 *timestamp)
{
	struct arm_spe *spe = speq->spe;
	int ret;

	if (!spe->kernel_start)
		spe->kernel_start = machine__kernel_start(spe->machine);

	while (1) {
		ret = arm_spe_decode(speq->decoder);
		if (!ret) {
			pr_debug("No data or all data has been processed.\n");
			return 1;
		}

		/*
		 * Error is detected when decode SPE trace data, continue to
		 * the next trace data and find out more records.
		 */
		if (ret < 0)
			continue;

		ret = arm_spe_sample(speq);
		if (ret)
			return ret;

		if (!spe->timeless_decoding && speq->timestamp >= *timestamp) {
			*timestamp = speq->timestamp;
			return 0;
		}
	}

	return 0;
}

static int arm_spe__setup_queue(struct arm_spe *spe,
			       struct auxtrace_queue *queue,
			       unsigned int queue_nr)
{
	struct arm_spe_queue *speq = queue->priv;
	struct arm_spe_record *record;

	if (list_empty(&queue->head) || speq)
		return 0;

	speq = arm_spe__alloc_queue(spe, queue_nr);

	if (!speq)
		return -ENOMEM;

	queue->priv = speq;

	if (queue->cpu != -1)
		speq->cpu = queue->cpu;

	if (!speq->on_heap) {
		int ret;

		if (spe->timeless_decoding)
			return 0;

retry:
		ret = arm_spe_decode(speq->decoder);

		if (!ret)
			return 0;

		if (ret < 0)
			goto retry;

		record = &speq->decoder->record;

		speq->timestamp = record->timestamp;
		ret = auxtrace_heap__add(&spe->heap, queue_nr, speq->timestamp);
		if (ret)
			return ret;
		speq->on_heap = true;
	}

	return 0;
}

static int arm_spe__setup_queues(struct arm_spe *spe)
{
	unsigned int i;
	int ret;

	for (i = 0; i < spe->queues.nr_queues; i++) {
		ret = arm_spe__setup_queue(spe, &spe->queues.queue_array[i], i);
		if (ret)
			return ret;
	}

	return 0;
}

static int arm_spe__update_queues(struct arm_spe *spe)
{
	if (spe->queues.new_data) {
		spe->queues.new_data = false;
		return arm_spe__setup_queues(spe);
	}

	return 0;
}

static bool arm_spe__is_timeless_decoding(struct arm_spe *spe)
{
	struct evsel *evsel;
	struct evlist *evlist = spe->session->evlist;
	bool timeless_decoding = true;

	/*
	 * Circle through the list of event and complain if we find one
	 * with the time bit set.
	 */
	evlist__for_each_entry(evlist, evsel) {
		if ((evsel->core.attr.sample_type & PERF_SAMPLE_TIME))
			timeless_decoding = false;
	}

	return timeless_decoding;
}

static void arm_spe_set_pid_tid_cpu(struct arm_spe *spe,
				    struct auxtrace_queue *queue)
{
	struct arm_spe_queue *speq = queue->priv;
	pid_t tid;

	tid = machine__get_current_tid(spe->machine, speq->cpu);
	if (tid != -1) {
		speq->tid = tid;
		thread__zput(speq->thread);
	} else
		speq->tid = queue->tid;

	if ((!speq->thread) && (speq->tid != -1)) {
		speq->thread = machine__find_thread(spe->machine, -1,
						    speq->tid);
	}

	if (speq->thread) {
		speq->pid = speq->thread->pid_;
		if (queue->cpu == -1)
			speq->cpu = speq->thread->cpu;
	}
}

static int arm_spe_process_queues(struct arm_spe *spe, u64 timestamp)
{
	unsigned int queue_nr;
	u64 ts;
	int ret;

	while (1) {
		struct auxtrace_queue *queue;
		struct arm_spe_queue *speq;

		if (!spe->heap.heap_cnt)
			return 0;

		if (spe->heap.heap_array[0].ordinal >= timestamp)
			return 0;

		queue_nr = spe->heap.heap_array[0].queue_nr;
		queue = &spe->queues.queue_array[queue_nr];
		speq = queue->priv;

		auxtrace_heap__pop(&spe->heap);

		if (spe->heap.heap_cnt) {
			ts = spe->heap.heap_array[0].ordinal + 1;
			if (ts > timestamp)
				ts = timestamp;
		} else {
			ts = timestamp;
		}

		arm_spe_set_pid_tid_cpu(spe, queue);

		ret = arm_spe_run_decoder(speq, &ts);
		if (ret < 0) {
			auxtrace_heap__add(&spe->heap, queue_nr, ts);
			return ret;
		}

		if (!ret) {
			ret = auxtrace_heap__add(&spe->heap, queue_nr, ts);
			if (ret < 0)
				return ret;
		} else {
			speq->on_heap = false;
		}
	}

	return 0;
}

static int arm_spe_process_timeless_queues(struct arm_spe *spe, pid_t tid,
					    u64 time_)
{
	struct auxtrace_queues *queues = &spe->queues;
	unsigned int i;
	u64 ts = 0;

	for (i = 0; i < queues->nr_queues; i++) {
		struct auxtrace_queue *queue = &spe->queues.queue_array[i];
		struct arm_spe_queue *speq = queue->priv;

		if (speq && (tid == -1 || speq->tid == tid)) {
			speq->time = time_;
			arm_spe_set_pid_tid_cpu(spe, queue);
			arm_spe_run_decoder(speq, &ts);
		}
	}
	return 0;
}

static int arm_spe_process_event(struct perf_session *session,
				 union perf_event *event,
				 struct perf_sample *sample,
				 struct perf_tool *tool)
{
	int err = 0;
	u64 timestamp;
	struct arm_spe *spe = container_of(session->auxtrace,
			struct arm_spe, auxtrace);

	if (dump_trace)
		return 0;

	if (!tool->ordered_events) {
		pr_err("SPE trace requires ordered events\n");
		return -EINVAL;
	}

	if (sample->time && (sample->time != (u64) -1))
		timestamp = sample->time;
	else
		timestamp = 0;

	if (timestamp || spe->timeless_decoding) {
		err = arm_spe__update_queues(spe);
		if (err)
			return err;
	}

	if (spe->timeless_decoding) {
		if (event->header.type == PERF_RECORD_EXIT) {
			err = arm_spe_process_timeless_queues(spe,
					event->fork.tid,
					sample->time);
		}
	} else if (timestamp) {
		if (event->header.type == PERF_RECORD_EXIT) {
			err = arm_spe_process_queues(spe, timestamp);
			if (err)
				return err;
		}
	}

	return err;
}

static int arm_spe_process_auxtrace_event(struct perf_session *session,
					  union perf_event *event,
					  struct perf_tool *tool __maybe_unused)
{
	struct arm_spe *spe = container_of(session->auxtrace, struct arm_spe,
					     auxtrace);

	if (!spe->data_queued) {
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

		err = auxtrace_queues__add_event(&spe->queues, session, event,
				data_offset, &buffer);
		if (err)
			return err;

		/* Dump here now we have copied a piped trace out of the pipe */
		if (dump_trace) {
			if (auxtrace_buffer__get_data(buffer, fd)) {
				arm_spe_dump_event(spe, buffer->data,
						buffer->size);
				auxtrace_buffer__put_data(buffer);
			}
		}
	}

	return 0;
}

static int arm_spe_flush(struct perf_session *session __maybe_unused,
			 struct perf_tool *tool __maybe_unused)
{
	struct arm_spe *spe = container_of(session->auxtrace, struct arm_spe,
			auxtrace);
	int ret;

	if (dump_trace)
		return 0;

	if (!tool->ordered_events)
		return -EINVAL;

	ret = arm_spe__update_queues(spe);
	if (ret < 0)
		return ret;

	if (spe->timeless_decoding)
		return arm_spe_process_timeless_queues(spe, -1,
				MAX_TIMESTAMP - 1);

	return arm_spe_process_queues(spe, MAX_TIMESTAMP);
}

static void arm_spe_free_queue(void *priv)
{
	struct arm_spe_queue *speq = priv;

	if (!speq)
		return;
	thread__zput(speq->thread);
	arm_spe_decoder_free(speq->decoder);
	zfree(&speq->event_buf);
	free(speq);
}

static void arm_spe_free_events(struct perf_session *session)
{
	struct arm_spe *spe = container_of(session->auxtrace, struct arm_spe,
					     auxtrace);
	struct auxtrace_queues *queues = &spe->queues;
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++) {
		arm_spe_free_queue(queues->queue_array[i].priv);
		queues->queue_array[i].priv = NULL;
	}
	auxtrace_queues__free(queues);
}

static void arm_spe_free(struct perf_session *session)
{
	struct arm_spe *spe = container_of(session->auxtrace, struct arm_spe,
					     auxtrace);

	auxtrace_heap__free(&spe->heap);
	arm_spe_free_events(session);
	session->auxtrace = NULL;
	free(spe);
}

static bool arm_spe_evsel_is_auxtrace(struct perf_session *session,
				      struct evsel *evsel)
{
	struct arm_spe *spe = container_of(session->auxtrace, struct arm_spe, auxtrace);

	return evsel->core.attr.type == spe->pmu_type;
}

static const char * const arm_spe_info_fmts[] = {
	[ARM_SPE_PMU_TYPE]		= "  PMU Type           %"PRId64"\n",
};

static void arm_spe_print_info(__u64 *arr)
{
	if (!dump_trace)
		return;

	fprintf(stdout, arm_spe_info_fmts[ARM_SPE_PMU_TYPE], arr[ARM_SPE_PMU_TYPE]);
}

struct arm_spe_synth {
	struct perf_tool dummy_tool;
	struct perf_session *session;
};

static int arm_spe_event_synth(struct perf_tool *tool,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused,
			       struct machine *machine __maybe_unused)
{
	struct arm_spe_synth *arm_spe_synth =
		      container_of(tool, struct arm_spe_synth, dummy_tool);

	return perf_session__deliver_synth_event(arm_spe_synth->session,
						 event, NULL);
}

static int arm_spe_synth_event(struct perf_session *session,
			       struct perf_event_attr *attr, u64 id)
{
	struct arm_spe_synth arm_spe_synth;

	memset(&arm_spe_synth, 0, sizeof(struct arm_spe_synth));
	arm_spe_synth.session = session;

	return perf_event__synthesize_attr(&arm_spe_synth.dummy_tool, attr, 1,
					   &id, arm_spe_event_synth);
}

static void arm_spe_set_event_name(struct evlist *evlist, u64 id,
				    const char *name)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.id && evsel->core.id[0] == id) {
			if (evsel->name)
				zfree(&evsel->name);
			evsel->name = strdup(name);
			break;
		}
	}
}

static int
arm_spe_synth_events(struct arm_spe *spe, struct perf_session *session)
{
	struct evlist *evlist = session->evlist;
	struct evsel *evsel;
	struct perf_event_attr attr;
	bool found = false;
	u64 id;
	int err;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == spe->pmu_type) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("No selected events with SPE trace data\n");
		return 0;
	}

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.type = PERF_TYPE_HARDWARE;
	attr.sample_type = evsel->core.attr.sample_type & PERF_SAMPLE_MASK;
	attr.sample_type |= PERF_SAMPLE_IP | PERF_SAMPLE_TID |
		PERF_SAMPLE_PERIOD;
	if (spe->timeless_decoding)
		attr.sample_type &= ~(u64)PERF_SAMPLE_TIME;
	else
		attr.sample_type |= PERF_SAMPLE_TIME;

	spe->sample_type = attr.sample_type;

	attr.exclude_user = evsel->core.attr.exclude_user;
	attr.exclude_kernel = evsel->core.attr.exclude_kernel;
	attr.exclude_hv = evsel->core.attr.exclude_hv;
	attr.exclude_host = evsel->core.attr.exclude_host;
	attr.exclude_guest = evsel->core.attr.exclude_guest;
	attr.sample_id_all = evsel->core.attr.sample_id_all;
	attr.read_format = evsel->core.attr.read_format;

	/* create new id val to be a fixed offset from evsel id */
	id = evsel->core.id[0] + 1000000000;

	if (!id)
		id = 1;

	if (spe->synth_opts.flc) {
		spe->sample_flc = true;

		/* Level 1 data cache miss */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->l1d_miss_id = id;
		arm_spe_set_event_name(evlist, id, "l1d-miss");
		id += 1;

		/* Level 1 data cache access */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->l1d_access_id = id;
		arm_spe_set_event_name(evlist, id, "l1d-access");
		id += 1;
	}

	if (spe->synth_opts.llc) {
		spe->sample_llc = true;

		/* Last level cache miss */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->llc_miss_id = id;
		arm_spe_set_event_name(evlist, id, "llc-miss");
		id += 1;

		/* Last level cache access */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->llc_access_id = id;
		arm_spe_set_event_name(evlist, id, "llc-access");
		id += 1;
	}

	if (spe->synth_opts.tlb) {
		spe->sample_tlb = true;

		/* TLB miss */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->tlb_miss_id = id;
		arm_spe_set_event_name(evlist, id, "tlb-miss");
		id += 1;

		/* TLB access */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->tlb_access_id = id;
		arm_spe_set_event_name(evlist, id, "tlb-access");
		id += 1;
	}

	if (spe->synth_opts.branches) {
		spe->sample_branch = true;

		/* Branch miss */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->branch_miss_id = id;
		arm_spe_set_event_name(evlist, id, "branch-miss");
		id += 1;
	}

	if (spe->synth_opts.remote_access) {
		spe->sample_remote_access = true;

		/* Remote access */
		err = arm_spe_synth_event(session, &attr, id);
		if (err)
			return err;
		spe->remote_access_id = id;
		arm_spe_set_event_name(evlist, id, "remote-access");
		id += 1;
	}

	return 0;
}

int arm_spe_process_auxtrace_info(union perf_event *event,
				  struct perf_session *session)
{
	struct perf_record_auxtrace_info *auxtrace_info = &event->auxtrace_info;
	size_t min_sz = sizeof(u64) * ARM_SPE_AUXTRACE_PRIV_MAX;
	struct arm_spe *spe;
	int err;

	if (auxtrace_info->header.size < sizeof(struct perf_record_auxtrace_info) +
					min_sz)
		return -EINVAL;

	spe = zalloc(sizeof(struct arm_spe));
	if (!spe)
		return -ENOMEM;

	err = auxtrace_queues__init(&spe->queues);
	if (err)
		goto err_free;

	spe->session = session;
	spe->machine = &session->machines.host; /* No kvm support */
	spe->auxtrace_type = auxtrace_info->type;
	spe->pmu_type = auxtrace_info->priv[ARM_SPE_PMU_TYPE];

	spe->timeless_decoding = arm_spe__is_timeless_decoding(spe);
	spe->auxtrace.process_event = arm_spe_process_event;
	spe->auxtrace.process_auxtrace_event = arm_spe_process_auxtrace_event;
	spe->auxtrace.flush_events = arm_spe_flush;
	spe->auxtrace.free_events = arm_spe_free_events;
	spe->auxtrace.free = arm_spe_free;
	spe->auxtrace.evsel_is_auxtrace = arm_spe_evsel_is_auxtrace;
	session->auxtrace = &spe->auxtrace;

	arm_spe_print_info(&auxtrace_info->priv[0]);

	if (dump_trace)
		return 0;

	if (session->itrace_synth_opts && session->itrace_synth_opts->set)
		spe->synth_opts = *session->itrace_synth_opts;
	else
		itrace_synth_opts__set_default(&spe->synth_opts, false);

	err = arm_spe_synth_events(spe, session);
	if (err)
		goto err_free_queues;

	err = auxtrace_queues__process_index(&spe->queues, session);
	if (err)
		goto err_free_queues;

	if (spe->queues.populated)
		spe->data_queued = true;

	return 0;

err_free_queues:
	auxtrace_queues__free(&spe->queues);
	session->auxtrace = NULL;
err_free:
	free(spe);
	return err;
}
