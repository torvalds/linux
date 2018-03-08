/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright(C) 2015-2018 Linaro Limited.
 *
 * Author: Tor Jeremiassen <tor@ti.com>
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/types.h>

#include <stdlib.h>

#include "auxtrace.h"
#include "color.h"
#include "cs-etm.h"
#include "cs-etm-decoder/cs-etm-decoder.h"
#include "debug.h"
#include "evlist.h"
#include "intlist.h"
#include "machine.h"
#include "map.h"
#include "perf.h"
#include "thread.h"
#include "thread_map.h"
#include "thread-stack.h"
#include "util.h"

#define MAX_TIMESTAMP (~0ULL)

struct cs_etm_auxtrace {
	struct auxtrace auxtrace;
	struct auxtrace_queues queues;
	struct auxtrace_heap heap;
	struct itrace_synth_opts synth_opts;
	struct perf_session *session;
	struct machine *machine;
	struct thread *unknown_thread;

	u8 timeless_decoding;
	u8 snapshot_mode;
	u8 data_queued;
	u8 sample_branches;

	int num_cpu;
	u32 auxtrace_type;
	u64 branches_sample_type;
	u64 branches_id;
	u64 **metadata;
	u64 kernel_start;
	unsigned int pmu_type;
};

struct cs_etm_queue {
	struct cs_etm_auxtrace *etm;
	struct thread *thread;
	struct cs_etm_decoder *decoder;
	struct auxtrace_buffer *buffer;
	const struct cs_etm_state *state;
	union perf_event *event_buf;
	unsigned int queue_nr;
	pid_t pid, tid;
	int cpu;
	u64 time;
	u64 timestamp;
	u64 offset;
};

static int cs_etm__update_queues(struct cs_etm_auxtrace *etm);
static int cs_etm__process_timeless_queues(struct cs_etm_auxtrace *etm,
					   pid_t tid, u64 time_);

static void cs_etm__packet_dump(const char *pkt_string)
{
	const char *color = PERF_COLOR_BLUE;
	int len = strlen(pkt_string);

	if (len && (pkt_string[len-1] == '\n'))
		color_fprintf(stdout, color, "	%s", pkt_string);
	else
		color_fprintf(stdout, color, "	%s\n", pkt_string);

	fflush(stdout);
}

static void cs_etm__dump_event(struct cs_etm_auxtrace *etm,
			       struct auxtrace_buffer *buffer)
{
	int i, ret;
	const char *color = PERF_COLOR_BLUE;
	struct cs_etm_decoder_params d_params;
	struct cs_etm_trace_params *t_params;
	struct cs_etm_decoder *decoder;
	size_t buffer_used = 0;

	fprintf(stdout, "\n");
	color_fprintf(stdout, color,
		     ". ... CoreSight ETM Trace data: size %zu bytes\n",
		     buffer->size);

	/* Use metadata to fill in trace parameters for trace decoder */
	t_params = zalloc(sizeof(*t_params) * etm->num_cpu);
	for (i = 0; i < etm->num_cpu; i++) {
		t_params[i].protocol = CS_ETM_PROTO_ETMV4i;
		t_params[i].etmv4.reg_idr0 = etm->metadata[i][CS_ETMV4_TRCIDR0];
		t_params[i].etmv4.reg_idr1 = etm->metadata[i][CS_ETMV4_TRCIDR1];
		t_params[i].etmv4.reg_idr2 = etm->metadata[i][CS_ETMV4_TRCIDR2];
		t_params[i].etmv4.reg_idr8 = etm->metadata[i][CS_ETMV4_TRCIDR8];
		t_params[i].etmv4.reg_configr =
					etm->metadata[i][CS_ETMV4_TRCCONFIGR];
		t_params[i].etmv4.reg_traceidr =
					etm->metadata[i][CS_ETMV4_TRCTRACEIDR];
	}

	/* Set decoder parameters to simply print the trace packets */
	d_params.packet_printer = cs_etm__packet_dump;
	d_params.operation = CS_ETM_OPERATION_PRINT;
	d_params.formatted = true;
	d_params.fsyncs = false;
	d_params.hsyncs = false;
	d_params.frame_aligned = true;

	decoder = cs_etm_decoder__new(etm->num_cpu, &d_params, t_params);

	zfree(&t_params);

	if (!decoder)
		return;
	do {
		size_t consumed;

		ret = cs_etm_decoder__process_data_block(
				decoder, buffer->offset,
				&((u8 *)buffer->data)[buffer_used],
				buffer->size - buffer_used, &consumed);
		if (ret)
			break;

		buffer_used += consumed;
	} while (buffer_used < buffer->size);

	cs_etm_decoder__free(decoder);
}

static int cs_etm__flush_events(struct perf_session *session,
				struct perf_tool *tool)
{
	int ret;
	struct cs_etm_auxtrace *etm = container_of(session->auxtrace,
						   struct cs_etm_auxtrace,
						   auxtrace);
	if (dump_trace)
		return 0;

	if (!tool->ordered_events)
		return -EINVAL;

	if (!etm->timeless_decoding)
		return -EINVAL;

	ret = cs_etm__update_queues(etm);

	if (ret < 0)
		return ret;

	return cs_etm__process_timeless_queues(etm, -1, MAX_TIMESTAMP - 1);
}

static void cs_etm__free_queue(void *priv)
{
	struct cs_etm_queue *etmq = priv;

	free(etmq);
}

static void cs_etm__free_events(struct perf_session *session)
{
	unsigned int i;
	struct cs_etm_auxtrace *aux = container_of(session->auxtrace,
						   struct cs_etm_auxtrace,
						   auxtrace);
	struct auxtrace_queues *queues = &aux->queues;

	for (i = 0; i < queues->nr_queues; i++) {
		cs_etm__free_queue(queues->queue_array[i].priv);
		queues->queue_array[i].priv = NULL;
	}

	auxtrace_queues__free(queues);
}

static void cs_etm__free(struct perf_session *session)
{
	int i;
	struct int_node *inode, *tmp;
	struct cs_etm_auxtrace *aux = container_of(session->auxtrace,
						   struct cs_etm_auxtrace,
						   auxtrace);
	cs_etm__free_events(session);
	session->auxtrace = NULL;

	/* First remove all traceID/CPU# nodes for the RB tree */
	intlist__for_each_entry_safe(inode, tmp, traceid_list)
		intlist__remove(traceid_list, inode);
	/* Then the RB tree itself */
	intlist__delete(traceid_list);

	for (i = 0; i < aux->num_cpu; i++)
		zfree(&aux->metadata[i]);

	zfree(&aux->metadata);
	zfree(&aux);
}

static u32 cs_etm__mem_access(struct cs_etm_queue *etmq, u64 address,
			      size_t size, u8 *buffer)
{
	u8  cpumode;
	u64 offset;
	int len;
	struct	 thread *thread;
	struct	 machine *machine;
	struct	 addr_location al;

	if (!etmq)
		return -1;

	machine = etmq->etm->machine;
	if (address >= etmq->etm->kernel_start)
		cpumode = PERF_RECORD_MISC_KERNEL;
	else
		cpumode = PERF_RECORD_MISC_USER;

	thread = etmq->thread;
	if (!thread) {
		if (cpumode != PERF_RECORD_MISC_KERNEL)
			return -EINVAL;
		thread = etmq->etm->unknown_thread;
	}

	thread__find_addr_map(thread, cpumode, MAP__FUNCTION, address, &al);

	if (!al.map || !al.map->dso)
		return 0;

	if (al.map->dso->data.status == DSO_DATA_STATUS_ERROR &&
	    dso__data_status_seen(al.map->dso, DSO_DATA_STATUS_SEEN_ITRACE))
		return 0;

	offset = al.map->map_ip(al.map, address);

	map__load(al.map);

	len = dso__data_read_offset(al.map->dso, machine, offset, buffer, size);

	if (len <= 0)
		return 0;

	return len;
}

static struct cs_etm_queue *cs_etm__alloc_queue(struct cs_etm_auxtrace *etm,
						unsigned int queue_nr)
{
	int i;
	struct cs_etm_decoder_params d_params;
	struct cs_etm_trace_params  *t_params;
	struct cs_etm_queue *etmq;

	etmq = zalloc(sizeof(*etmq));
	if (!etmq)
		return NULL;

	etmq->event_buf = malloc(PERF_SAMPLE_MAX_SIZE);
	if (!etmq->event_buf)
		goto out_free;

	etmq->etm = etm;
	etmq->queue_nr = queue_nr;
	etmq->pid = -1;
	etmq->tid = -1;
	etmq->cpu = -1;

	/* Use metadata to fill in trace parameters for trace decoder */
	t_params = zalloc(sizeof(*t_params) * etm->num_cpu);

	if (!t_params)
		goto out_free;

	for (i = 0; i < etm->num_cpu; i++) {
		t_params[i].protocol = CS_ETM_PROTO_ETMV4i;
		t_params[i].etmv4.reg_idr0 = etm->metadata[i][CS_ETMV4_TRCIDR0];
		t_params[i].etmv4.reg_idr1 = etm->metadata[i][CS_ETMV4_TRCIDR1];
		t_params[i].etmv4.reg_idr2 = etm->metadata[i][CS_ETMV4_TRCIDR2];
		t_params[i].etmv4.reg_idr8 = etm->metadata[i][CS_ETMV4_TRCIDR8];
		t_params[i].etmv4.reg_configr =
					etm->metadata[i][CS_ETMV4_TRCCONFIGR];
		t_params[i].etmv4.reg_traceidr =
					etm->metadata[i][CS_ETMV4_TRCTRACEIDR];
	}

	/* Set decoder parameters to simply print the trace packets */
	d_params.packet_printer = cs_etm__packet_dump;
	d_params.operation = CS_ETM_OPERATION_DECODE;
	d_params.formatted = true;
	d_params.fsyncs = false;
	d_params.hsyncs = false;
	d_params.frame_aligned = true;
	d_params.data = etmq;

	etmq->decoder = cs_etm_decoder__new(etm->num_cpu, &d_params, t_params);

	zfree(&t_params);

	if (!etmq->decoder)
		goto out_free;

	/*
	 * Register a function to handle all memory accesses required by
	 * the trace decoder library.
	 */
	if (cs_etm_decoder__add_mem_access_cb(etmq->decoder,
					      0x0L, ((u64) -1L),
					      cs_etm__mem_access))
		goto out_free_decoder;

	etmq->offset = 0;

	return etmq;

out_free_decoder:
	cs_etm_decoder__free(etmq->decoder);
out_free:
	zfree(&etmq->event_buf);
	free(etmq);

	return NULL;
}

static int cs_etm__setup_queue(struct cs_etm_auxtrace *etm,
			       struct auxtrace_queue *queue,
			       unsigned int queue_nr)
{
	struct cs_etm_queue *etmq = queue->priv;

	if (list_empty(&queue->head) || etmq)
		return 0;

	etmq = cs_etm__alloc_queue(etm, queue_nr);

	if (!etmq)
		return -ENOMEM;

	queue->priv = etmq;

	if (queue->cpu != -1)
		etmq->cpu = queue->cpu;

	etmq->tid = queue->tid;

	return 0;
}

static int cs_etm__setup_queues(struct cs_etm_auxtrace *etm)
{
	unsigned int i;
	int ret;

	for (i = 0; i < etm->queues.nr_queues; i++) {
		ret = cs_etm__setup_queue(etm, &etm->queues.queue_array[i], i);
		if (ret)
			return ret;
	}

	return 0;
}

static int cs_etm__update_queues(struct cs_etm_auxtrace *etm)
{
	if (etm->queues.new_data) {
		etm->queues.new_data = false;
		return cs_etm__setup_queues(etm);
	}

	return 0;
}

static int
cs_etm__get_trace(struct cs_etm_buffer *buff, struct cs_etm_queue *etmq)
{
	struct auxtrace_buffer *aux_buffer = etmq->buffer;
	struct auxtrace_buffer *old_buffer = aux_buffer;
	struct auxtrace_queue *queue;

	queue = &etmq->etm->queues.queue_array[etmq->queue_nr];

	aux_buffer = auxtrace_buffer__next(queue, aux_buffer);

	/* If no more data, drop the previous auxtrace_buffer and return */
	if (!aux_buffer) {
		if (old_buffer)
			auxtrace_buffer__drop_data(old_buffer);
		buff->len = 0;
		return 0;
	}

	etmq->buffer = aux_buffer;

	/* If the aux_buffer doesn't have data associated, try to load it */
	if (!aux_buffer->data) {
		/* get the file desc associated with the perf data file */
		int fd = perf_data__fd(etmq->etm->session->data);

		aux_buffer->data = auxtrace_buffer__get_data(aux_buffer, fd);
		if (!aux_buffer->data)
			return -ENOMEM;
	}

	/* If valid, drop the previous buffer */
	if (old_buffer)
		auxtrace_buffer__drop_data(old_buffer);

	buff->offset = aux_buffer->offset;
	buff->len = aux_buffer->size;
	buff->buf = aux_buffer->data;

	buff->ref_timestamp = aux_buffer->reference;

	return buff->len;
}

static void  cs_etm__set_pid_tid_cpu(struct cs_etm_auxtrace *etm,
				     struct auxtrace_queue *queue)
{
	struct cs_etm_queue *etmq = queue->priv;

	/* CPU-wide tracing isn't supported yet */
	if (queue->tid == -1)
		return;

	if ((!etmq->thread) && (etmq->tid != -1))
		etmq->thread = machine__find_thread(etm->machine, -1,
						    etmq->tid);

	if (etmq->thread) {
		etmq->pid = etmq->thread->pid_;
		if (queue->cpu == -1)
			etmq->cpu = etmq->thread->cpu;
	}
}

/*
 * The cs etm packet encodes an instruction range between a branch target
 * and the next taken branch. Generate sample accordingly.
 */
static int cs_etm__synth_branch_sample(struct cs_etm_queue *etmq,
				       struct cs_etm_packet *packet)
{
	int ret = 0;
	struct cs_etm_auxtrace *etm = etmq->etm;
	struct perf_sample sample = {.ip = 0,};
	union perf_event *event = etmq->event_buf;
	u64 start_addr = packet->start_addr;
	u64 end_addr = packet->end_addr;

	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = PERF_RECORD_MISC_USER;
	event->sample.header.size = sizeof(struct perf_event_header);

	sample.ip = start_addr;
	sample.pid = etmq->pid;
	sample.tid = etmq->tid;
	sample.addr = end_addr;
	sample.id = etmq->etm->branches_id;
	sample.stream_id = etmq->etm->branches_id;
	sample.period = 1;
	sample.cpu = packet->cpu;
	sample.flags = 0;
	sample.cpumode = PERF_RECORD_MISC_USER;

	ret = perf_session__deliver_synth_event(etm->session, event, &sample);

	if (ret)
		pr_err(
		"CS ETM Trace: failed to deliver instruction event, error %d\n",
		ret);

	return ret;
}

struct cs_etm_synth {
	struct perf_tool dummy_tool;
	struct perf_session *session;
};

static int cs_etm__event_synth(struct perf_tool *tool,
			       union perf_event *event,
			       struct perf_sample *sample __maybe_unused,
			       struct machine *machine __maybe_unused)
{
	struct cs_etm_synth *cs_etm_synth =
		      container_of(tool, struct cs_etm_synth, dummy_tool);

	return perf_session__deliver_synth_event(cs_etm_synth->session,
						 event, NULL);
}

static int cs_etm__synth_event(struct perf_session *session,
			       struct perf_event_attr *attr, u64 id)
{
	struct cs_etm_synth cs_etm_synth;

	memset(&cs_etm_synth, 0, sizeof(struct cs_etm_synth));
	cs_etm_synth.session = session;

	return perf_event__synthesize_attr(&cs_etm_synth.dummy_tool, attr, 1,
					   &id, cs_etm__event_synth);
}

static int cs_etm__synth_events(struct cs_etm_auxtrace *etm,
				struct perf_session *session)
{
	struct perf_evlist *evlist = session->evlist;
	struct perf_evsel *evsel;
	struct perf_event_attr attr;
	bool found = false;
	u64 id;
	int err;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->attr.type == etm->pmu_type) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_debug("No selected events with CoreSight Trace data\n");
		return 0;
	}

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.type = PERF_TYPE_HARDWARE;
	attr.sample_type = evsel->attr.sample_type & PERF_SAMPLE_MASK;
	attr.sample_type |= PERF_SAMPLE_IP | PERF_SAMPLE_TID |
			    PERF_SAMPLE_PERIOD;
	if (etm->timeless_decoding)
		attr.sample_type &= ~(u64)PERF_SAMPLE_TIME;
	else
		attr.sample_type |= PERF_SAMPLE_TIME;

	attr.exclude_user = evsel->attr.exclude_user;
	attr.exclude_kernel = evsel->attr.exclude_kernel;
	attr.exclude_hv = evsel->attr.exclude_hv;
	attr.exclude_host = evsel->attr.exclude_host;
	attr.exclude_guest = evsel->attr.exclude_guest;
	attr.sample_id_all = evsel->attr.sample_id_all;
	attr.read_format = evsel->attr.read_format;

	/* create new id val to be a fixed offset from evsel id */
	id = evsel->id[0] + 1000000000;

	if (!id)
		id = 1;

	if (etm->synth_opts.branches) {
		attr.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
		attr.sample_period = 1;
		attr.sample_type |= PERF_SAMPLE_ADDR;
		err = cs_etm__synth_event(session, &attr, id);
		if (err)
			return err;
		etm->sample_branches = true;
		etm->branches_sample_type = attr.sample_type;
		etm->branches_id = id;
	}

	return 0;
}

static int cs_etm__sample(struct cs_etm_queue *etmq)
{
	int ret;
	struct cs_etm_packet packet;

	while (1) {
		ret = cs_etm_decoder__get_packet(etmq->decoder, &packet);
		if (ret <= 0)
			return ret;

		/*
		 * If the packet contains an instruction range, generate an
		 * instruction sequence event.
		 */
		if (packet.sample_type & CS_ETM_RANGE)
			cs_etm__synth_branch_sample(etmq, &packet);
	}

	return 0;
}

static int cs_etm__run_decoder(struct cs_etm_queue *etmq)
{
	struct cs_etm_auxtrace *etm = etmq->etm;
	struct cs_etm_buffer buffer;
	size_t buffer_used, processed;
	int err = 0;

	if (!etm->kernel_start)
		etm->kernel_start = machine__kernel_start(etm->machine);

	/* Go through each buffer in the queue and decode them one by one */
more:
	buffer_used = 0;
	memset(&buffer, 0, sizeof(buffer));
	err = cs_etm__get_trace(&buffer, etmq);
	if (err <= 0)
		return err;
	/*
	 * We cannot assume consecutive blocks in the data file are contiguous,
	 * reset the decoder to force re-sync.
	 */
	err = cs_etm_decoder__reset(etmq->decoder);
	if (err != 0)
		return err;

	/* Run trace decoder until buffer consumed or end of trace */
	do {
		processed = 0;

		err = cs_etm_decoder__process_data_block(
						etmq->decoder,
						etmq->offset,
						&buffer.buf[buffer_used],
						buffer.len - buffer_used,
						&processed);

		if (err)
			return err;

		etmq->offset += processed;
		buffer_used += processed;

		/*
		 * Nothing to do with an error condition, let's hope the next
		 * chunk will be better.
		 */
		err = cs_etm__sample(etmq);
	} while (buffer.len > buffer_used);

goto more;

	return err;
}

static int cs_etm__process_timeless_queues(struct cs_etm_auxtrace *etm,
					   pid_t tid, u64 time_)
{
	unsigned int i;
	struct auxtrace_queues *queues = &etm->queues;

	for (i = 0; i < queues->nr_queues; i++) {
		struct auxtrace_queue *queue = &etm->queues.queue_array[i];
		struct cs_etm_queue *etmq = queue->priv;

		if (etmq && ((tid == -1) || (etmq->tid == tid))) {
			etmq->time = time_;
			cs_etm__set_pid_tid_cpu(etm, queue);
			cs_etm__run_decoder(etmq);
		}
	}

	return 0;
}

static int cs_etm__process_event(struct perf_session *session,
				 union perf_event *event,
				 struct perf_sample *sample,
				 struct perf_tool *tool)
{
	int err = 0;
	u64 timestamp;
	struct cs_etm_auxtrace *etm = container_of(session->auxtrace,
						   struct cs_etm_auxtrace,
						   auxtrace);

	if (dump_trace)
		return 0;

	if (!tool->ordered_events) {
		pr_err("CoreSight ETM Trace requires ordered events\n");
		return -EINVAL;
	}

	if (!etm->timeless_decoding)
		return -EINVAL;

	if (sample->time && (sample->time != (u64) -1))
		timestamp = sample->time;
	else
		timestamp = 0;

	if (timestamp || etm->timeless_decoding) {
		err = cs_etm__update_queues(etm);
		if (err)
			return err;
	}

	if (event->header.type == PERF_RECORD_EXIT)
		return cs_etm__process_timeless_queues(etm,
						       event->fork.tid,
						       sample->time);

	return 0;
}

static int cs_etm__process_auxtrace_event(struct perf_session *session,
					  union perf_event *event,
					  struct perf_tool *tool __maybe_unused)
{
	struct cs_etm_auxtrace *etm = container_of(session->auxtrace,
						   struct cs_etm_auxtrace,
						   auxtrace);
	if (!etm->data_queued) {
		struct auxtrace_buffer *buffer;
		off_t  data_offset;
		int fd = perf_data__fd(session->data);
		bool is_pipe = perf_data__is_pipe(session->data);
		int err;

		if (is_pipe)
			data_offset = 0;
		else {
			data_offset = lseek(fd, 0, SEEK_CUR);
			if (data_offset == -1)
				return -errno;
		}

		err = auxtrace_queues__add_event(&etm->queues, session,
						 event, data_offset, &buffer);
		if (err)
			return err;

		if (dump_trace)
			if (auxtrace_buffer__get_data(buffer, fd)) {
				cs_etm__dump_event(etm, buffer);
				auxtrace_buffer__put_data(buffer);
			}
	}

	return 0;
}

static bool cs_etm__is_timeless_decoding(struct cs_etm_auxtrace *etm)
{
	struct perf_evsel *evsel;
	struct perf_evlist *evlist = etm->session->evlist;
	bool timeless_decoding = true;

	/*
	 * Circle through the list of event and complain if we find one
	 * with the time bit set.
	 */
	evlist__for_each_entry(evlist, evsel) {
		if ((evsel->attr.sample_type & PERF_SAMPLE_TIME))
			timeless_decoding = false;
	}

	return timeless_decoding;
}

static const char * const cs_etm_global_header_fmts[] = {
	[CS_HEADER_VERSION_0]	= "	Header version		       %llx\n",
	[CS_PMU_TYPE_CPUS]	= "	PMU type/num cpus	       %llx\n",
	[CS_ETM_SNAPSHOT]	= "	Snapshot		       %llx\n",
};

static const char * const cs_etm_priv_fmts[] = {
	[CS_ETM_MAGIC]		= "	Magic number		       %llx\n",
	[CS_ETM_CPU]		= "	CPU			       %lld\n",
	[CS_ETM_ETMCR]		= "	ETMCR			       %llx\n",
	[CS_ETM_ETMTRACEIDR]	= "	ETMTRACEIDR		       %llx\n",
	[CS_ETM_ETMCCER]	= "	ETMCCER			       %llx\n",
	[CS_ETM_ETMIDR]		= "	ETMIDR			       %llx\n",
};

static const char * const cs_etmv4_priv_fmts[] = {
	[CS_ETM_MAGIC]		= "	Magic number		       %llx\n",
	[CS_ETM_CPU]		= "	CPU			       %lld\n",
	[CS_ETMV4_TRCCONFIGR]	= "	TRCCONFIGR		       %llx\n",
	[CS_ETMV4_TRCTRACEIDR]	= "	TRCTRACEIDR		       %llx\n",
	[CS_ETMV4_TRCIDR0]	= "	TRCIDR0			       %llx\n",
	[CS_ETMV4_TRCIDR1]	= "	TRCIDR1			       %llx\n",
	[CS_ETMV4_TRCIDR2]	= "	TRCIDR2			       %llx\n",
	[CS_ETMV4_TRCIDR8]	= "	TRCIDR8			       %llx\n",
	[CS_ETMV4_TRCAUTHSTATUS] = "	TRCAUTHSTATUS		       %llx\n",
};

static void cs_etm__print_auxtrace_info(u64 *val, int num)
{
	int i, j, cpu = 0;

	for (i = 0; i < CS_HEADER_VERSION_0_MAX; i++)
		fprintf(stdout, cs_etm_global_header_fmts[i], val[i]);

	for (i = CS_HEADER_VERSION_0_MAX; cpu < num; cpu++) {
		if (val[i] == __perf_cs_etmv3_magic)
			for (j = 0; j < CS_ETM_PRIV_MAX; j++, i++)
				fprintf(stdout, cs_etm_priv_fmts[j], val[i]);
		else if (val[i] == __perf_cs_etmv4_magic)
			for (j = 0; j < CS_ETMV4_PRIV_MAX; j++, i++)
				fprintf(stdout, cs_etmv4_priv_fmts[j], val[i]);
		else
			/* failure.. return */
			return;
	}
}

int cs_etm__process_auxtrace_info(union perf_event *event,
				  struct perf_session *session)
{
	struct auxtrace_info_event *auxtrace_info = &event->auxtrace_info;
	struct cs_etm_auxtrace *etm = NULL;
	struct int_node *inode;
	unsigned int pmu_type;
	int event_header_size = sizeof(struct perf_event_header);
	int info_header_size;
	int total_size = auxtrace_info->header.size;
	int priv_size = 0;
	int num_cpu;
	int err = 0, idx = -1;
	int i, j, k;
	u64 *ptr, *hdr = NULL;
	u64 **metadata = NULL;

	/*
	 * sizeof(auxtrace_info_event::type) +
	 * sizeof(auxtrace_info_event::reserved) == 8
	 */
	info_header_size = 8;

	if (total_size < (event_header_size + info_header_size))
		return -EINVAL;

	priv_size = total_size - event_header_size - info_header_size;

	/* First the global part */
	ptr = (u64 *) auxtrace_info->priv;

	/* Look for version '0' of the header */
	if (ptr[0] != 0)
		return -EINVAL;

	hdr = zalloc(sizeof(*hdr) * CS_HEADER_VERSION_0_MAX);
	if (!hdr)
		return -ENOMEM;

	/* Extract header information - see cs-etm.h for format */
	for (i = 0; i < CS_HEADER_VERSION_0_MAX; i++)
		hdr[i] = ptr[i];
	num_cpu = hdr[CS_PMU_TYPE_CPUS] & 0xffffffff;
	pmu_type = (unsigned int) ((hdr[CS_PMU_TYPE_CPUS] >> 32) &
				    0xffffffff);

	/*
	 * Create an RB tree for traceID-CPU# tuple. Since the conversion has
	 * to be made for each packet that gets decoded, optimizing access in
	 * anything other than a sequential array is worth doing.
	 */
	traceid_list = intlist__new(NULL);
	if (!traceid_list) {
		err = -ENOMEM;
		goto err_free_hdr;
	}

	metadata = zalloc(sizeof(*metadata) * num_cpu);
	if (!metadata) {
		err = -ENOMEM;
		goto err_free_traceid_list;
	}

	/*
	 * The metadata is stored in the auxtrace_info section and encodes
	 * the configuration of the ARM embedded trace macrocell which is
	 * required by the trace decoder to properly decode the trace due
	 * to its highly compressed nature.
	 */
	for (j = 0; j < num_cpu; j++) {
		if (ptr[i] == __perf_cs_etmv3_magic) {
			metadata[j] = zalloc(sizeof(*metadata[j]) *
					     CS_ETM_PRIV_MAX);
			if (!metadata[j]) {
				err = -ENOMEM;
				goto err_free_metadata;
			}
			for (k = 0; k < CS_ETM_PRIV_MAX; k++)
				metadata[j][k] = ptr[i + k];

			/* The traceID is our handle */
			idx = metadata[j][CS_ETM_ETMTRACEIDR];
			i += CS_ETM_PRIV_MAX;
		} else if (ptr[i] == __perf_cs_etmv4_magic) {
			metadata[j] = zalloc(sizeof(*metadata[j]) *
					     CS_ETMV4_PRIV_MAX);
			if (!metadata[j]) {
				err = -ENOMEM;
				goto err_free_metadata;
			}
			for (k = 0; k < CS_ETMV4_PRIV_MAX; k++)
				metadata[j][k] = ptr[i + k];

			/* The traceID is our handle */
			idx = metadata[j][CS_ETMV4_TRCTRACEIDR];
			i += CS_ETMV4_PRIV_MAX;
		}

		/* Get an RB node for this CPU */
		inode = intlist__findnew(traceid_list, idx);

		/* Something went wrong, no need to continue */
		if (!inode) {
			err = PTR_ERR(inode);
			goto err_free_metadata;
		}

		/*
		 * The node for that CPU should not be taken.
		 * Back out if that's the case.
		 */
		if (inode->priv) {
			err = -EINVAL;
			goto err_free_metadata;
		}
		/* All good, associate the traceID with the CPU# */
		inode->priv = &metadata[j][CS_ETM_CPU];
	}

	/*
	 * Each of CS_HEADER_VERSION_0_MAX, CS_ETM_PRIV_MAX and
	 * CS_ETMV4_PRIV_MAX mark how many double words are in the
	 * global metadata, and each cpu's metadata respectively.
	 * The following tests if the correct number of double words was
	 * present in the auxtrace info section.
	 */
	if (i * 8 != priv_size) {
		err = -EINVAL;
		goto err_free_metadata;
	}

	etm = zalloc(sizeof(*etm));

	if (!etm) {
		err = -ENOMEM;
		goto err_free_metadata;
	}

	err = auxtrace_queues__init(&etm->queues);
	if (err)
		goto err_free_etm;

	etm->session = session;
	etm->machine = &session->machines.host;

	etm->num_cpu = num_cpu;
	etm->pmu_type = pmu_type;
	etm->snapshot_mode = (hdr[CS_ETM_SNAPSHOT] != 0);
	etm->metadata = metadata;
	etm->auxtrace_type = auxtrace_info->type;
	etm->timeless_decoding = cs_etm__is_timeless_decoding(etm);

	etm->auxtrace.process_event = cs_etm__process_event;
	etm->auxtrace.process_auxtrace_event = cs_etm__process_auxtrace_event;
	etm->auxtrace.flush_events = cs_etm__flush_events;
	etm->auxtrace.free_events = cs_etm__free_events;
	etm->auxtrace.free = cs_etm__free;
	session->auxtrace = &etm->auxtrace;

	if (dump_trace) {
		cs_etm__print_auxtrace_info(auxtrace_info->priv, num_cpu);
		return 0;
	}

	if (session->itrace_synth_opts && session->itrace_synth_opts->set) {
		etm->synth_opts = *session->itrace_synth_opts;
	} else {
		itrace_synth_opts__set_default(&etm->synth_opts);
		etm->synth_opts.callchain = false;
	}

	err = cs_etm__synth_events(etm, session);
	if (err)
		goto err_free_queues;

	err = auxtrace_queues__process_index(&etm->queues, session);
	if (err)
		goto err_free_queues;

	etm->data_queued = etm->queues.populated;

	return 0;

err_free_queues:
	auxtrace_queues__free(&etm->queues);
	session->auxtrace = NULL;
err_free_etm:
	zfree(&etm);
err_free_metadata:
	/* No need to check @metadata[j], free(NULL) is supported */
	for (j = 0; j < num_cpu; j++)
		free(metadata[j]);
	zfree(&metadata);
err_free_traceid_list:
	intlist__delete(traceid_list);
err_free_hdr:
	zfree(&hdr);

	return -EINVAL;
}
