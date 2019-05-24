// SPDX-License-Identifier: GPL-2.0
/*
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

#include <opencsd/ocsd_if_types.h>
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
#include "symbol.h"
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
	u8 sample_instructions;

	int num_cpu;
	u32 auxtrace_type;
	u64 branches_sample_type;
	u64 branches_id;
	u64 instructions_sample_type;
	u64 instructions_sample_period;
	u64 instructions_id;
	u64 **metadata;
	u64 kernel_start;
	unsigned int pmu_type;
};

struct cs_etm_queue {
	struct cs_etm_auxtrace *etm;
	struct thread *thread;
	struct cs_etm_decoder *decoder;
	struct auxtrace_buffer *buffer;
	union perf_event *event_buf;
	unsigned int queue_nr;
	pid_t pid, tid;
	int cpu;
	u64 offset;
	u64 period_instructions;
	struct branch_stack *last_branch;
	struct branch_stack *last_branch_rb;
	size_t last_branch_pos;
	struct cs_etm_packet *prev_packet;
	struct cs_etm_packet *packet;
	const unsigned char *buf;
	size_t buf_len, buf_used;
};

static int cs_etm__update_queues(struct cs_etm_auxtrace *etm);
static int cs_etm__process_timeless_queues(struct cs_etm_auxtrace *etm,
					   pid_t tid);

/* PTMs ETMIDR [11:8] set to b0011 */
#define ETMIDR_PTM_VERSION 0x00000300

static u32 cs_etm__get_v7_protocol_version(u32 etmidr)
{
	etmidr &= ETMIDR_PTM_VERSION;

	if (etmidr == ETMIDR_PTM_VERSION)
		return CS_ETM_PROTO_PTM;

	return CS_ETM_PROTO_ETMV3;
}

static int cs_etm__get_magic(u8 trace_chan_id, u64 *magic)
{
	struct int_node *inode;
	u64 *metadata;

	inode = intlist__find(traceid_list, trace_chan_id);
	if (!inode)
		return -EINVAL;

	metadata = inode->priv;
	*magic = metadata[CS_ETM_MAGIC];
	return 0;
}

int cs_etm__get_cpu(u8 trace_chan_id, int *cpu)
{
	struct int_node *inode;
	u64 *metadata;

	inode = intlist__find(traceid_list, trace_chan_id);
	if (!inode)
		return -EINVAL;

	metadata = inode->priv;
	*cpu = (int)metadata[CS_ETM_CPU];
	return 0;
}

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

static void cs_etm__set_trace_param_etmv3(struct cs_etm_trace_params *t_params,
					  struct cs_etm_auxtrace *etm, int idx,
					  u32 etmidr)
{
	u64 **metadata = etm->metadata;

	t_params[idx].protocol = cs_etm__get_v7_protocol_version(etmidr);
	t_params[idx].etmv3.reg_ctrl = metadata[idx][CS_ETM_ETMCR];
	t_params[idx].etmv3.reg_trc_id = metadata[idx][CS_ETM_ETMTRACEIDR];
}

static void cs_etm__set_trace_param_etmv4(struct cs_etm_trace_params *t_params,
					  struct cs_etm_auxtrace *etm, int idx)
{
	u64 **metadata = etm->metadata;

	t_params[idx].protocol = CS_ETM_PROTO_ETMV4i;
	t_params[idx].etmv4.reg_idr0 = metadata[idx][CS_ETMV4_TRCIDR0];
	t_params[idx].etmv4.reg_idr1 = metadata[idx][CS_ETMV4_TRCIDR1];
	t_params[idx].etmv4.reg_idr2 = metadata[idx][CS_ETMV4_TRCIDR2];
	t_params[idx].etmv4.reg_idr8 = metadata[idx][CS_ETMV4_TRCIDR8];
	t_params[idx].etmv4.reg_configr = metadata[idx][CS_ETMV4_TRCCONFIGR];
	t_params[idx].etmv4.reg_traceidr = metadata[idx][CS_ETMV4_TRCTRACEIDR];
}

static int cs_etm__init_trace_params(struct cs_etm_trace_params *t_params,
				     struct cs_etm_auxtrace *etm)
{
	int i;
	u32 etmidr;
	u64 architecture;

	for (i = 0; i < etm->num_cpu; i++) {
		architecture = etm->metadata[i][CS_ETM_MAGIC];

		switch (architecture) {
		case __perf_cs_etmv3_magic:
			etmidr = etm->metadata[i][CS_ETM_ETMIDR];
			cs_etm__set_trace_param_etmv3(t_params, etm, i, etmidr);
			break;
		case __perf_cs_etmv4_magic:
			cs_etm__set_trace_param_etmv4(t_params, etm, i);
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int cs_etm__init_decoder_params(struct cs_etm_decoder_params *d_params,
				       struct cs_etm_queue *etmq,
				       enum cs_etm_decoder_operation mode)
{
	int ret = -EINVAL;

	if (!(mode < CS_ETM_OPERATION_MAX))
		goto out;

	d_params->packet_printer = cs_etm__packet_dump;
	d_params->operation = mode;
	d_params->data = etmq;
	d_params->formatted = true;
	d_params->fsyncs = false;
	d_params->hsyncs = false;
	d_params->frame_aligned = true;

	ret = 0;
out:
	return ret;
}

static void cs_etm__dump_event(struct cs_etm_auxtrace *etm,
			       struct auxtrace_buffer *buffer)
{
	int ret;
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

	if (!t_params)
		return;

	if (cs_etm__init_trace_params(t_params, etm))
		goto out_free;

	/* Set decoder parameters to simply print the trace packets */
	if (cs_etm__init_decoder_params(&d_params, NULL,
					CS_ETM_OPERATION_PRINT))
		goto out_free;

	decoder = cs_etm_decoder__new(etm->num_cpu, &d_params, t_params);

	if (!decoder)
		goto out_free;
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

out_free:
	zfree(&t_params);
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

	return cs_etm__process_timeless_queues(etm, -1);
}

static void cs_etm__free_queue(void *priv)
{
	struct cs_etm_queue *etmq = priv;

	if (!etmq)
		return;

	thread__zput(etmq->thread);
	cs_etm_decoder__free(etmq->decoder);
	zfree(&etmq->event_buf);
	zfree(&etmq->last_branch);
	zfree(&etmq->last_branch_rb);
	zfree(&etmq->prev_packet);
	zfree(&etmq->packet);
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

	/* First remove all traceID/metadata nodes for the RB tree */
	intlist__for_each_entry_safe(inode, tmp, traceid_list)
		intlist__remove(traceid_list, inode);
	/* Then the RB tree itself */
	intlist__delete(traceid_list);

	for (i = 0; i < aux->num_cpu; i++)
		zfree(&aux->metadata[i]);

	thread__zput(aux->unknown_thread);
	zfree(&aux->metadata);
	zfree(&aux);
}

static u8 cs_etm__cpu_mode(struct cs_etm_queue *etmq, u64 address)
{
	struct machine *machine;

	machine = etmq->etm->machine;

	if (address >= etmq->etm->kernel_start) {
		if (machine__is_host(machine))
			return PERF_RECORD_MISC_KERNEL;
		else
			return PERF_RECORD_MISC_GUEST_KERNEL;
	} else {
		if (machine__is_host(machine))
			return PERF_RECORD_MISC_USER;
		else if (perf_guest)
			return PERF_RECORD_MISC_GUEST_USER;
		else
			return PERF_RECORD_MISC_HYPERVISOR;
	}
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
		return 0;

	machine = etmq->etm->machine;
	cpumode = cs_etm__cpu_mode(etmq, address);

	thread = etmq->thread;
	if (!thread) {
		if (cpumode != PERF_RECORD_MISC_KERNEL)
			return 0;
		thread = etmq->etm->unknown_thread;
	}

	if (!thread__find_map(thread, cpumode, address, &al) || !al.map->dso)
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

static struct cs_etm_queue *cs_etm__alloc_queue(struct cs_etm_auxtrace *etm)
{
	struct cs_etm_decoder_params d_params;
	struct cs_etm_trace_params  *t_params = NULL;
	struct cs_etm_queue *etmq;
	size_t szp = sizeof(struct cs_etm_packet);

	etmq = zalloc(sizeof(*etmq));
	if (!etmq)
		return NULL;

	etmq->packet = zalloc(szp);
	if (!etmq->packet)
		goto out_free;

	etmq->prev_packet = zalloc(szp);
	if (!etmq->prev_packet)
		goto out_free;

	if (etm->synth_opts.last_branch) {
		size_t sz = sizeof(struct branch_stack);

		sz += etm->synth_opts.last_branch_sz *
		      sizeof(struct branch_entry);
		etmq->last_branch = zalloc(sz);
		if (!etmq->last_branch)
			goto out_free;
		etmq->last_branch_rb = zalloc(sz);
		if (!etmq->last_branch_rb)
			goto out_free;
	}

	etmq->event_buf = malloc(PERF_SAMPLE_MAX_SIZE);
	if (!etmq->event_buf)
		goto out_free;

	/* Use metadata to fill in trace parameters for trace decoder */
	t_params = zalloc(sizeof(*t_params) * etm->num_cpu);

	if (!t_params)
		goto out_free;

	if (cs_etm__init_trace_params(t_params, etm))
		goto out_free;

	/* Set decoder parameters to decode trace packets */
	if (cs_etm__init_decoder_params(&d_params, etmq,
					CS_ETM_OPERATION_DECODE))
		goto out_free;

	etmq->decoder = cs_etm_decoder__new(etm->num_cpu, &d_params, t_params);

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

	zfree(&t_params);
	return etmq;

out_free_decoder:
	cs_etm_decoder__free(etmq->decoder);
out_free:
	zfree(&t_params);
	zfree(&etmq->event_buf);
	zfree(&etmq->last_branch);
	zfree(&etmq->last_branch_rb);
	zfree(&etmq->prev_packet);
	zfree(&etmq->packet);
	free(etmq);

	return NULL;
}

static int cs_etm__setup_queue(struct cs_etm_auxtrace *etm,
			       struct auxtrace_queue *queue,
			       unsigned int queue_nr)
{
	int ret = 0;
	struct cs_etm_queue *etmq = queue->priv;

	if (list_empty(&queue->head) || etmq)
		goto out;

	etmq = cs_etm__alloc_queue(etm);

	if (!etmq) {
		ret = -ENOMEM;
		goto out;
	}

	queue->priv = etmq;
	etmq->etm = etm;
	etmq->queue_nr = queue_nr;
	etmq->cpu = queue->cpu;
	etmq->tid = queue->tid;
	etmq->pid = -1;
	etmq->offset = 0;
	etmq->period_instructions = 0;

out:
	return ret;
}

static int cs_etm__setup_queues(struct cs_etm_auxtrace *etm)
{
	unsigned int i;
	int ret;

	if (!etm->kernel_start)
		etm->kernel_start = machine__kernel_start(etm->machine);

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

static inline void cs_etm__copy_last_branch_rb(struct cs_etm_queue *etmq)
{
	struct branch_stack *bs_src = etmq->last_branch_rb;
	struct branch_stack *bs_dst = etmq->last_branch;
	size_t nr = 0;

	/*
	 * Set the number of records before early exit: ->nr is used to
	 * determine how many branches to copy from ->entries.
	 */
	bs_dst->nr = bs_src->nr;

	/*
	 * Early exit when there is nothing to copy.
	 */
	if (!bs_src->nr)
		return;

	/*
	 * As bs_src->entries is a circular buffer, we need to copy from it in
	 * two steps.  First, copy the branches from the most recently inserted
	 * branch ->last_branch_pos until the end of bs_src->entries buffer.
	 */
	nr = etmq->etm->synth_opts.last_branch_sz - etmq->last_branch_pos;
	memcpy(&bs_dst->entries[0],
	       &bs_src->entries[etmq->last_branch_pos],
	       sizeof(struct branch_entry) * nr);

	/*
	 * If we wrapped around at least once, the branches from the beginning
	 * of the bs_src->entries buffer and until the ->last_branch_pos element
	 * are older valid branches: copy them over.  The total number of
	 * branches copied over will be equal to the number of branches asked by
	 * the user in last_branch_sz.
	 */
	if (bs_src->nr >= etmq->etm->synth_opts.last_branch_sz) {
		memcpy(&bs_dst->entries[nr],
		       &bs_src->entries[0],
		       sizeof(struct branch_entry) * etmq->last_branch_pos);
	}
}

static inline void cs_etm__reset_last_branch_rb(struct cs_etm_queue *etmq)
{
	etmq->last_branch_pos = 0;
	etmq->last_branch_rb->nr = 0;
}

static inline int cs_etm__t32_instr_size(struct cs_etm_queue *etmq,
					 u64 addr) {
	u8 instrBytes[2];

	cs_etm__mem_access(etmq, addr, ARRAY_SIZE(instrBytes), instrBytes);
	/*
	 * T32 instruction size is indicated by bits[15:11] of the first
	 * 16-bit word of the instruction: 0b11101, 0b11110 and 0b11111
	 * denote a 32-bit instruction.
	 */
	return ((instrBytes[1] & 0xF8) >= 0xE8) ? 4 : 2;
}

static inline u64 cs_etm__first_executed_instr(struct cs_etm_packet *packet)
{
	/* Returns 0 for the CS_ETM_DISCONTINUITY packet */
	if (packet->sample_type == CS_ETM_DISCONTINUITY)
		return 0;

	return packet->start_addr;
}

static inline
u64 cs_etm__last_executed_instr(const struct cs_etm_packet *packet)
{
	/* Returns 0 for the CS_ETM_DISCONTINUITY packet */
	if (packet->sample_type == CS_ETM_DISCONTINUITY)
		return 0;

	return packet->end_addr - packet->last_instr_size;
}

static inline u64 cs_etm__instr_addr(struct cs_etm_queue *etmq,
				     const struct cs_etm_packet *packet,
				     u64 offset)
{
	if (packet->isa == CS_ETM_ISA_T32) {
		u64 addr = packet->start_addr;

		while (offset > 0) {
			addr += cs_etm__t32_instr_size(etmq, addr);
			offset--;
		}
		return addr;
	}

	/* Assume a 4 byte instruction size (A32/A64) */
	return packet->start_addr + offset * 4;
}

static void cs_etm__update_last_branch_rb(struct cs_etm_queue *etmq)
{
	struct branch_stack *bs = etmq->last_branch_rb;
	struct branch_entry *be;

	/*
	 * The branches are recorded in a circular buffer in reverse
	 * chronological order: we start recording from the last element of the
	 * buffer down.  After writing the first element of the stack, move the
	 * insert position back to the end of the buffer.
	 */
	if (!etmq->last_branch_pos)
		etmq->last_branch_pos = etmq->etm->synth_opts.last_branch_sz;

	etmq->last_branch_pos -= 1;

	be       = &bs->entries[etmq->last_branch_pos];
	be->from = cs_etm__last_executed_instr(etmq->prev_packet);
	be->to	 = cs_etm__first_executed_instr(etmq->packet);
	/* No support for mispredict */
	be->flags.mispred = 0;
	be->flags.predicted = 1;

	/*
	 * Increment bs->nr until reaching the number of last branches asked by
	 * the user on the command line.
	 */
	if (bs->nr < etmq->etm->synth_opts.last_branch_sz)
		bs->nr += 1;
}

static int cs_etm__inject_event(union perf_event *event,
			       struct perf_sample *sample, u64 type)
{
	event->header.size = perf_event__sample_event_size(sample, type, 0);
	return perf_event__synthesize_sample(event, type, 0, sample);
}


static int
cs_etm__get_trace(struct cs_etm_queue *etmq)
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
		etmq->buf_len = 0;
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

	etmq->buf_used = 0;
	etmq->buf_len = aux_buffer->size;
	etmq->buf = aux_buffer->data;

	return etmq->buf_len;
}

static void cs_etm__set_pid_tid_cpu(struct cs_etm_auxtrace *etm,
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

static int cs_etm__synth_instruction_sample(struct cs_etm_queue *etmq,
					    u64 addr, u64 period)
{
	int ret = 0;
	struct cs_etm_auxtrace *etm = etmq->etm;
	union perf_event *event = etmq->event_buf;
	struct perf_sample sample = {.ip = 0,};

	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = cs_etm__cpu_mode(etmq, addr);
	event->sample.header.size = sizeof(struct perf_event_header);

	sample.ip = addr;
	sample.pid = etmq->pid;
	sample.tid = etmq->tid;
	sample.id = etmq->etm->instructions_id;
	sample.stream_id = etmq->etm->instructions_id;
	sample.period = period;
	sample.cpu = etmq->packet->cpu;
	sample.flags = etmq->prev_packet->flags;
	sample.insn_len = 1;
	sample.cpumode = event->sample.header.misc;

	if (etm->synth_opts.last_branch) {
		cs_etm__copy_last_branch_rb(etmq);
		sample.branch_stack = etmq->last_branch;
	}

	if (etm->synth_opts.inject) {
		ret = cs_etm__inject_event(event, &sample,
					   etm->instructions_sample_type);
		if (ret)
			return ret;
	}

	ret = perf_session__deliver_synth_event(etm->session, event, &sample);

	if (ret)
		pr_err(
			"CS ETM Trace: failed to deliver instruction event, error %d\n",
			ret);

	if (etm->synth_opts.last_branch)
		cs_etm__reset_last_branch_rb(etmq);

	return ret;
}

/*
 * The cs etm packet encodes an instruction range between a branch target
 * and the next taken branch. Generate sample accordingly.
 */
static int cs_etm__synth_branch_sample(struct cs_etm_queue *etmq)
{
	int ret = 0;
	struct cs_etm_auxtrace *etm = etmq->etm;
	struct perf_sample sample = {.ip = 0,};
	union perf_event *event = etmq->event_buf;
	struct dummy_branch_stack {
		u64			nr;
		struct branch_entry	entries;
	} dummy_bs;
	u64 ip;

	ip = cs_etm__last_executed_instr(etmq->prev_packet);

	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.misc = cs_etm__cpu_mode(etmq, ip);
	event->sample.header.size = sizeof(struct perf_event_header);

	sample.ip = ip;
	sample.pid = etmq->pid;
	sample.tid = etmq->tid;
	sample.addr = cs_etm__first_executed_instr(etmq->packet);
	sample.id = etmq->etm->branches_id;
	sample.stream_id = etmq->etm->branches_id;
	sample.period = 1;
	sample.cpu = etmq->packet->cpu;
	sample.flags = etmq->prev_packet->flags;
	sample.cpumode = event->sample.header.misc;

	/*
	 * perf report cannot handle events without a branch stack
	 */
	if (etm->synth_opts.last_branch) {
		dummy_bs = (struct dummy_branch_stack){
			.nr = 1,
			.entries = {
				.from = sample.ip,
				.to = sample.addr,
			},
		};
		sample.branch_stack = (struct branch_stack *)&dummy_bs;
	}

	if (etm->synth_opts.inject) {
		ret = cs_etm__inject_event(event, &sample,
					   etm->branches_sample_type);
		if (ret)
			return ret;
	}

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
		id += 1;
		attr.sample_type &= ~(u64)PERF_SAMPLE_ADDR;
	}

	if (etm->synth_opts.last_branch)
		attr.sample_type |= PERF_SAMPLE_BRANCH_STACK;

	if (etm->synth_opts.instructions) {
		attr.config = PERF_COUNT_HW_INSTRUCTIONS;
		attr.sample_period = etm->synth_opts.period;
		etm->instructions_sample_period = attr.sample_period;
		err = cs_etm__synth_event(session, &attr, id);
		if (err)
			return err;
		etm->sample_instructions = true;
		etm->instructions_sample_type = attr.sample_type;
		etm->instructions_id = id;
		id += 1;
	}

	return 0;
}

static int cs_etm__sample(struct cs_etm_queue *etmq)
{
	struct cs_etm_auxtrace *etm = etmq->etm;
	struct cs_etm_packet *tmp;
	int ret;
	u64 instrs_executed = etmq->packet->instr_count;

	etmq->period_instructions += instrs_executed;

	/*
	 * Record a branch when the last instruction in
	 * PREV_PACKET is a branch.
	 */
	if (etm->synth_opts.last_branch &&
	    etmq->prev_packet->sample_type == CS_ETM_RANGE &&
	    etmq->prev_packet->last_instr_taken_branch)
		cs_etm__update_last_branch_rb(etmq);

	if (etm->sample_instructions &&
	    etmq->period_instructions >= etm->instructions_sample_period) {
		/*
		 * Emit instruction sample periodically
		 * TODO: allow period to be defined in cycles and clock time
		 */

		/* Get number of instructions executed after the sample point */
		u64 instrs_over = etmq->period_instructions -
			etm->instructions_sample_period;

		/*
		 * Calculate the address of the sampled instruction (-1 as
		 * sample is reported as though instruction has just been
		 * executed, but PC has not advanced to next instruction)
		 */
		u64 offset = (instrs_executed - instrs_over - 1);
		u64 addr = cs_etm__instr_addr(etmq, etmq->packet, offset);

		ret = cs_etm__synth_instruction_sample(
			etmq, addr, etm->instructions_sample_period);
		if (ret)
			return ret;

		/* Carry remaining instructions into next sample period */
		etmq->period_instructions = instrs_over;
	}

	if (etm->sample_branches) {
		bool generate_sample = false;

		/* Generate sample for tracing on packet */
		if (etmq->prev_packet->sample_type == CS_ETM_DISCONTINUITY)
			generate_sample = true;

		/* Generate sample for branch taken packet */
		if (etmq->prev_packet->sample_type == CS_ETM_RANGE &&
		    etmq->prev_packet->last_instr_taken_branch)
			generate_sample = true;

		if (generate_sample) {
			ret = cs_etm__synth_branch_sample(etmq);
			if (ret)
				return ret;
		}
	}

	if (etm->sample_branches || etm->synth_opts.last_branch) {
		/*
		 * Swap PACKET with PREV_PACKET: PACKET becomes PREV_PACKET for
		 * the next incoming packet.
		 */
		tmp = etmq->packet;
		etmq->packet = etmq->prev_packet;
		etmq->prev_packet = tmp;
	}

	return 0;
}

static int cs_etm__exception(struct cs_etm_queue *etmq)
{
	/*
	 * When the exception packet is inserted, whether the last instruction
	 * in previous range packet is taken branch or not, we need to force
	 * to set 'prev_packet->last_instr_taken_branch' to true.  This ensures
	 * to generate branch sample for the instruction range before the
	 * exception is trapped to kernel or before the exception returning.
	 *
	 * The exception packet includes the dummy address values, so don't
	 * swap PACKET with PREV_PACKET.  This keeps PREV_PACKET to be useful
	 * for generating instruction and branch samples.
	 */
	if (etmq->prev_packet->sample_type == CS_ETM_RANGE)
		etmq->prev_packet->last_instr_taken_branch = true;

	return 0;
}

static int cs_etm__flush(struct cs_etm_queue *etmq)
{
	int err = 0;
	struct cs_etm_auxtrace *etm = etmq->etm;
	struct cs_etm_packet *tmp;

	/* Handle start tracing packet */
	if (etmq->prev_packet->sample_type == CS_ETM_EMPTY)
		goto swap_packet;

	if (etmq->etm->synth_opts.last_branch &&
	    etmq->prev_packet->sample_type == CS_ETM_RANGE) {
		/*
		 * Generate a last branch event for the branches left in the
		 * circular buffer at the end of the trace.
		 *
		 * Use the address of the end of the last reported execution
		 * range
		 */
		u64 addr = cs_etm__last_executed_instr(etmq->prev_packet);

		err = cs_etm__synth_instruction_sample(
			etmq, addr,
			etmq->period_instructions);
		if (err)
			return err;

		etmq->period_instructions = 0;

	}

	if (etm->sample_branches &&
	    etmq->prev_packet->sample_type == CS_ETM_RANGE) {
		err = cs_etm__synth_branch_sample(etmq);
		if (err)
			return err;
	}

swap_packet:
	if (etm->sample_branches || etm->synth_opts.last_branch) {
		/*
		 * Swap PACKET with PREV_PACKET: PACKET becomes PREV_PACKET for
		 * the next incoming packet.
		 */
		tmp = etmq->packet;
		etmq->packet = etmq->prev_packet;
		etmq->prev_packet = tmp;
	}

	return err;
}

static int cs_etm__end_block(struct cs_etm_queue *etmq)
{
	int err;

	/*
	 * It has no new packet coming and 'etmq->packet' contains the stale
	 * packet which was set at the previous time with packets swapping;
	 * so skip to generate branch sample to avoid stale packet.
	 *
	 * For this case only flush branch stack and generate a last branch
	 * event for the branches left in the circular buffer at the end of
	 * the trace.
	 */
	if (etmq->etm->synth_opts.last_branch &&
	    etmq->prev_packet->sample_type == CS_ETM_RANGE) {
		/*
		 * Use the address of the end of the last reported execution
		 * range.
		 */
		u64 addr = cs_etm__last_executed_instr(etmq->prev_packet);

		err = cs_etm__synth_instruction_sample(
			etmq, addr,
			etmq->period_instructions);
		if (err)
			return err;

		etmq->period_instructions = 0;
	}

	return 0;
}
/*
 * cs_etm__get_data_block: Fetch a block from the auxtrace_buffer queue
 *			   if need be.
 * Returns:	< 0	if error
 *		= 0	if no more auxtrace_buffer to read
 *		> 0	if the current buffer isn't empty yet
 */
static int cs_etm__get_data_block(struct cs_etm_queue *etmq)
{
	int ret;

	if (!etmq->buf_len) {
		ret = cs_etm__get_trace(etmq);
		if (ret <= 0)
			return ret;
		/*
		 * We cannot assume consecutive blocks in the data file
		 * are contiguous, reset the decoder to force re-sync.
		 */
		ret = cs_etm_decoder__reset(etmq->decoder);
		if (ret)
			return ret;
	}

	return etmq->buf_len;
}

static bool cs_etm__is_svc_instr(struct cs_etm_queue *etmq,
				 struct cs_etm_packet *packet,
				 u64 end_addr)
{
	u16 instr16;
	u32 instr32;
	u64 addr;

	switch (packet->isa) {
	case CS_ETM_ISA_T32:
		/*
		 * The SVC of T32 is defined in ARM DDI 0487D.a, F5.1.247:
		 *
		 *  b'15         b'8
		 * +-----------------+--------+
		 * | 1 1 0 1 1 1 1 1 |  imm8  |
		 * +-----------------+--------+
		 *
		 * According to the specifiction, it only defines SVC for T32
		 * with 16 bits instruction and has no definition for 32bits;
		 * so below only read 2 bytes as instruction size for T32.
		 */
		addr = end_addr - 2;
		cs_etm__mem_access(etmq, addr, sizeof(instr16), (u8 *)&instr16);
		if ((instr16 & 0xFF00) == 0xDF00)
			return true;

		break;
	case CS_ETM_ISA_A32:
		/*
		 * The SVC of A32 is defined in ARM DDI 0487D.a, F5.1.247:
		 *
		 *  b'31 b'28 b'27 b'24
		 * +---------+---------+-------------------------+
		 * |  !1111  | 1 1 1 1 |        imm24            |
		 * +---------+---------+-------------------------+
		 */
		addr = end_addr - 4;
		cs_etm__mem_access(etmq, addr, sizeof(instr32), (u8 *)&instr32);
		if ((instr32 & 0x0F000000) == 0x0F000000 &&
		    (instr32 & 0xF0000000) != 0xF0000000)
			return true;

		break;
	case CS_ETM_ISA_A64:
		/*
		 * The SVC of A64 is defined in ARM DDI 0487D.a, C6.2.294:
		 *
		 *  b'31               b'21           b'4     b'0
		 * +-----------------------+---------+-----------+
		 * | 1 1 0 1 0 1 0 0 0 0 0 |  imm16  | 0 0 0 0 1 |
		 * +-----------------------+---------+-----------+
		 */
		addr = end_addr - 4;
		cs_etm__mem_access(etmq, addr, sizeof(instr32), (u8 *)&instr32);
		if ((instr32 & 0xFFE0001F) == 0xd4000001)
			return true;

		break;
	case CS_ETM_ISA_UNKNOWN:
	default:
		break;
	}

	return false;
}

static bool cs_etm__is_syscall(struct cs_etm_queue *etmq, u64 magic)
{
	struct cs_etm_packet *packet = etmq->packet;
	struct cs_etm_packet *prev_packet = etmq->prev_packet;

	if (magic == __perf_cs_etmv3_magic)
		if (packet->exception_number == CS_ETMV3_EXC_SVC)
			return true;

	/*
	 * ETMv4 exception type CS_ETMV4_EXC_CALL covers SVC, SMC and
	 * HVC cases; need to check if it's SVC instruction based on
	 * packet address.
	 */
	if (magic == __perf_cs_etmv4_magic) {
		if (packet->exception_number == CS_ETMV4_EXC_CALL &&
		    cs_etm__is_svc_instr(etmq, prev_packet,
					 prev_packet->end_addr))
			return true;
	}

	return false;
}

static bool cs_etm__is_async_exception(struct cs_etm_queue *etmq, u64 magic)
{
	struct cs_etm_packet *packet = etmq->packet;

	if (magic == __perf_cs_etmv3_magic)
		if (packet->exception_number == CS_ETMV3_EXC_DEBUG_HALT ||
		    packet->exception_number == CS_ETMV3_EXC_ASYNC_DATA_ABORT ||
		    packet->exception_number == CS_ETMV3_EXC_PE_RESET ||
		    packet->exception_number == CS_ETMV3_EXC_IRQ ||
		    packet->exception_number == CS_ETMV3_EXC_FIQ)
			return true;

	if (magic == __perf_cs_etmv4_magic)
		if (packet->exception_number == CS_ETMV4_EXC_RESET ||
		    packet->exception_number == CS_ETMV4_EXC_DEBUG_HALT ||
		    packet->exception_number == CS_ETMV4_EXC_SYSTEM_ERROR ||
		    packet->exception_number == CS_ETMV4_EXC_INST_DEBUG ||
		    packet->exception_number == CS_ETMV4_EXC_DATA_DEBUG ||
		    packet->exception_number == CS_ETMV4_EXC_IRQ ||
		    packet->exception_number == CS_ETMV4_EXC_FIQ)
			return true;

	return false;
}

static bool cs_etm__is_sync_exception(struct cs_etm_queue *etmq, u64 magic)
{
	struct cs_etm_packet *packet = etmq->packet;
	struct cs_etm_packet *prev_packet = etmq->prev_packet;

	if (magic == __perf_cs_etmv3_magic)
		if (packet->exception_number == CS_ETMV3_EXC_SMC ||
		    packet->exception_number == CS_ETMV3_EXC_HYP ||
		    packet->exception_number == CS_ETMV3_EXC_JAZELLE_THUMBEE ||
		    packet->exception_number == CS_ETMV3_EXC_UNDEFINED_INSTR ||
		    packet->exception_number == CS_ETMV3_EXC_PREFETCH_ABORT ||
		    packet->exception_number == CS_ETMV3_EXC_DATA_FAULT ||
		    packet->exception_number == CS_ETMV3_EXC_GENERIC)
			return true;

	if (magic == __perf_cs_etmv4_magic) {
		if (packet->exception_number == CS_ETMV4_EXC_TRAP ||
		    packet->exception_number == CS_ETMV4_EXC_ALIGNMENT ||
		    packet->exception_number == CS_ETMV4_EXC_INST_FAULT ||
		    packet->exception_number == CS_ETMV4_EXC_DATA_FAULT)
			return true;

		/*
		 * For CS_ETMV4_EXC_CALL, except SVC other instructions
		 * (SMC, HVC) are taken as sync exceptions.
		 */
		if (packet->exception_number == CS_ETMV4_EXC_CALL &&
		    !cs_etm__is_svc_instr(etmq, prev_packet,
					  prev_packet->end_addr))
			return true;

		/*
		 * ETMv4 has 5 bits for exception number; if the numbers
		 * are in the range ( CS_ETMV4_EXC_FIQ, CS_ETMV4_EXC_END ]
		 * they are implementation defined exceptions.
		 *
		 * For this case, simply take it as sync exception.
		 */
		if (packet->exception_number > CS_ETMV4_EXC_FIQ &&
		    packet->exception_number <= CS_ETMV4_EXC_END)
			return true;
	}

	return false;
}

static int cs_etm__set_sample_flags(struct cs_etm_queue *etmq)
{
	struct cs_etm_packet *packet = etmq->packet;
	struct cs_etm_packet *prev_packet = etmq->prev_packet;
	u64 magic;
	int ret;

	switch (packet->sample_type) {
	case CS_ETM_RANGE:
		/*
		 * Immediate branch instruction without neither link nor
		 * return flag, it's normal branch instruction within
		 * the function.
		 */
		if (packet->last_instr_type == OCSD_INSTR_BR &&
		    packet->last_instr_subtype == OCSD_S_INSTR_NONE) {
			packet->flags = PERF_IP_FLAG_BRANCH;

			if (packet->last_instr_cond)
				packet->flags |= PERF_IP_FLAG_CONDITIONAL;
		}

		/*
		 * Immediate branch instruction with link (e.g. BL), this is
		 * branch instruction for function call.
		 */
		if (packet->last_instr_type == OCSD_INSTR_BR &&
		    packet->last_instr_subtype == OCSD_S_INSTR_BR_LINK)
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_CALL;

		/*
		 * Indirect branch instruction with link (e.g. BLR), this is
		 * branch instruction for function call.
		 */
		if (packet->last_instr_type == OCSD_INSTR_BR_INDIRECT &&
		    packet->last_instr_subtype == OCSD_S_INSTR_BR_LINK)
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_CALL;

		/*
		 * Indirect branch instruction with subtype of
		 * OCSD_S_INSTR_V7_IMPLIED_RET, this is explicit hint for
		 * function return for A32/T32.
		 */
		if (packet->last_instr_type == OCSD_INSTR_BR_INDIRECT &&
		    packet->last_instr_subtype == OCSD_S_INSTR_V7_IMPLIED_RET)
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_RETURN;

		/*
		 * Indirect branch instruction without link (e.g. BR), usually
		 * this is used for function return, especially for functions
		 * within dynamic link lib.
		 */
		if (packet->last_instr_type == OCSD_INSTR_BR_INDIRECT &&
		    packet->last_instr_subtype == OCSD_S_INSTR_NONE)
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_RETURN;

		/* Return instruction for function return. */
		if (packet->last_instr_type == OCSD_INSTR_BR_INDIRECT &&
		    packet->last_instr_subtype == OCSD_S_INSTR_V8_RET)
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_RETURN;

		/*
		 * Decoder might insert a discontinuity in the middle of
		 * instruction packets, fixup prev_packet with flag
		 * PERF_IP_FLAG_TRACE_BEGIN to indicate restarting trace.
		 */
		if (prev_packet->sample_type == CS_ETM_DISCONTINUITY)
			prev_packet->flags |= PERF_IP_FLAG_BRANCH |
					      PERF_IP_FLAG_TRACE_BEGIN;

		/*
		 * If the previous packet is an exception return packet
		 * and the return address just follows SVC instuction,
		 * it needs to calibrate the previous packet sample flags
		 * as PERF_IP_FLAG_SYSCALLRET.
		 */
		if (prev_packet->flags == (PERF_IP_FLAG_BRANCH |
					   PERF_IP_FLAG_RETURN |
					   PERF_IP_FLAG_INTERRUPT) &&
		    cs_etm__is_svc_instr(etmq, packet, packet->start_addr))
			prev_packet->flags = PERF_IP_FLAG_BRANCH |
					     PERF_IP_FLAG_RETURN |
					     PERF_IP_FLAG_SYSCALLRET;
		break;
	case CS_ETM_DISCONTINUITY:
		/*
		 * The trace is discontinuous, if the previous packet is
		 * instruction packet, set flag PERF_IP_FLAG_TRACE_END
		 * for previous packet.
		 */
		if (prev_packet->sample_type == CS_ETM_RANGE)
			prev_packet->flags |= PERF_IP_FLAG_BRANCH |
					      PERF_IP_FLAG_TRACE_END;
		break;
	case CS_ETM_EXCEPTION:
		ret = cs_etm__get_magic(packet->trace_chan_id, &magic);
		if (ret)
			return ret;

		/* The exception is for system call. */
		if (cs_etm__is_syscall(etmq, magic))
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_CALL |
					PERF_IP_FLAG_SYSCALLRET;
		/*
		 * The exceptions are triggered by external signals from bus,
		 * interrupt controller, debug module, PE reset or halt.
		 */
		else if (cs_etm__is_async_exception(etmq, magic))
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_CALL |
					PERF_IP_FLAG_ASYNC |
					PERF_IP_FLAG_INTERRUPT;
		/*
		 * Otherwise, exception is caused by trap, instruction &
		 * data fault, or alignment errors.
		 */
		else if (cs_etm__is_sync_exception(etmq, magic))
			packet->flags = PERF_IP_FLAG_BRANCH |
					PERF_IP_FLAG_CALL |
					PERF_IP_FLAG_INTERRUPT;

		/*
		 * When the exception packet is inserted, since exception
		 * packet is not used standalone for generating samples
		 * and it's affiliation to the previous instruction range
		 * packet; so set previous range packet flags to tell perf
		 * it is an exception taken branch.
		 */
		if (prev_packet->sample_type == CS_ETM_RANGE)
			prev_packet->flags = packet->flags;
		break;
	case CS_ETM_EXCEPTION_RET:
		/*
		 * When the exception return packet is inserted, since
		 * exception return packet is not used standalone for
		 * generating samples and it's affiliation to the previous
		 * instruction range packet; so set previous range packet
		 * flags to tell perf it is an exception return branch.
		 *
		 * The exception return can be for either system call or
		 * other exception types; unfortunately the packet doesn't
		 * contain exception type related info so we cannot decide
		 * the exception type purely based on exception return packet.
		 * If we record the exception number from exception packet and
		 * reuse it for excpetion return packet, this is not reliable
		 * due the trace can be discontinuity or the interrupt can
		 * be nested, thus the recorded exception number cannot be
		 * used for exception return packet for these two cases.
		 *
		 * For exception return packet, we only need to distinguish the
		 * packet is for system call or for other types.  Thus the
		 * decision can be deferred when receive the next packet which
		 * contains the return address, based on the return address we
		 * can read out the previous instruction and check if it's a
		 * system call instruction and then calibrate the sample flag
		 * as needed.
		 */
		if (prev_packet->sample_type == CS_ETM_RANGE)
			prev_packet->flags = PERF_IP_FLAG_BRANCH |
					     PERF_IP_FLAG_RETURN |
					     PERF_IP_FLAG_INTERRUPT;
		break;
	case CS_ETM_EMPTY:
	default:
		break;
	}

	return 0;
}

static int cs_etm__decode_data_block(struct cs_etm_queue *etmq)
{
	int ret = 0;
	size_t processed = 0;

	/*
	 * Packets are decoded and added to the decoder's packet queue
	 * until the decoder packet processing callback has requested that
	 * processing stops or there is nothing left in the buffer.  Normal
	 * operations that stop processing are a timestamp packet or a full
	 * decoder buffer queue.
	 */
	ret = cs_etm_decoder__process_data_block(etmq->decoder,
						 etmq->offset,
						 &etmq->buf[etmq->buf_used],
						 etmq->buf_len,
						 &processed);
	if (ret)
		goto out;

	etmq->offset += processed;
	etmq->buf_used += processed;
	etmq->buf_len -= processed;

out:
	return ret;
}

static int cs_etm__process_decoder_queue(struct cs_etm_queue *etmq)
{
	int ret;

		/* Process each packet in this chunk */
		while (1) {
			ret = cs_etm_decoder__get_packet(etmq->decoder,
							 etmq->packet);
			if (ret <= 0)
				/*
				 * Stop processing this chunk on
				 * end of data or error
				 */
				break;

			/*
			 * Since packet addresses are swapped in packet
			 * handling within below switch() statements,
			 * thus setting sample flags must be called
			 * prior to switch() statement to use address
			 * information before packets swapping.
			 */
			ret = cs_etm__set_sample_flags(etmq);
			if (ret < 0)
				break;

			switch (etmq->packet->sample_type) {
			case CS_ETM_RANGE:
				/*
				 * If the packet contains an instruction
				 * range, generate instruction sequence
				 * events.
				 */
				cs_etm__sample(etmq);
				break;
			case CS_ETM_EXCEPTION:
			case CS_ETM_EXCEPTION_RET:
				/*
				 * If the exception packet is coming,
				 * make sure the previous instruction
				 * range packet to be handled properly.
				 */
				cs_etm__exception(etmq);
				break;
			case CS_ETM_DISCONTINUITY:
				/*
				 * Discontinuity in trace, flush
				 * previous branch stack
				 */
				cs_etm__flush(etmq);
				break;
			case CS_ETM_EMPTY:
				/*
				 * Should not receive empty packet,
				 * report error.
				 */
				pr_err("CS ETM Trace: empty packet\n");
				return -EINVAL;
			default:
				break;
			}
		}

	return ret;
}

static int cs_etm__run_decoder(struct cs_etm_queue *etmq)
{
	int err = 0;

	/* Go through each buffer in the queue and decode them one by one */
	while (1) {
		err = cs_etm__get_data_block(etmq);
		if (err <= 0)
			return err;

		/* Run trace decoder until buffer consumed or end of trace */
		do {
			err = cs_etm__decode_data_block(etmq);
			if (err)
				return err;

			/*
			 * Process each packet in this chunk, nothing to do if
			 * an error occurs other than hoping the next one will
			 * be better.
			 */
			err = cs_etm__process_decoder_queue(etmq);

		} while (etmq->buf_len);

		if (err == 0)
			/* Flush any remaining branch stack entries */
			err = cs_etm__end_block(etmq);
	}

	return err;
}

static int cs_etm__process_timeless_queues(struct cs_etm_auxtrace *etm,
					   pid_t tid)
{
	unsigned int i;
	struct auxtrace_queues *queues = &etm->queues;

	for (i = 0; i < queues->nr_queues; i++) {
		struct auxtrace_queue *queue = &etm->queues.queue_array[i];
		struct cs_etm_queue *etmq = queue->priv;

		if (etmq && ((tid == -1) || (etmq->tid == tid))) {
			cs_etm__set_pid_tid_cpu(etm, queue);
			cs_etm__run_decoder(etmq);
		}
	}

	return 0;
}

static int cs_etm__process_itrace_start(struct cs_etm_auxtrace *etm,
					union perf_event *event)
{
	struct thread *th;

	if (etm->timeless_decoding)
		return 0;

	/*
	 * Add the tid/pid to the log so that we can get a match when
	 * we get a contextID from the decoder.
	 */
	th = machine__findnew_thread(etm->machine,
				     event->itrace_start.pid,
				     event->itrace_start.tid);
	if (!th)
		return -ENOMEM;

	thread__put(th);

	return 0;
}

static int cs_etm__process_switch_cpu_wide(struct cs_etm_auxtrace *etm,
					   union perf_event *event)
{
	struct thread *th;
	bool out = event->header.misc & PERF_RECORD_MISC_SWITCH_OUT;

	/*
	 * Context switch in per-thread mode are irrelevant since perf
	 * will start/stop tracing as the process is scheduled.
	 */
	if (etm->timeless_decoding)
		return 0;

	/*
	 * SWITCH_IN events carry the next process to be switched out while
	 * SWITCH_OUT events carry the process to be switched in.  As such
	 * we don't care about IN events.
	 */
	if (!out)
		return 0;

	/*
	 * Add the tid/pid to the log so that we can get a match when
	 * we get a contextID from the decoder.
	 */
	th = machine__findnew_thread(etm->machine,
				     event->context_switch.next_prev_pid,
				     event->context_switch.next_prev_tid);
	if (!th)
		return -ENOMEM;

	thread__put(th);

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
						       event->fork.tid);

	if (event->header.type == PERF_RECORD_ITRACE_START)
		return cs_etm__process_itrace_start(etm, event);
	else if (event->header.type == PERF_RECORD_SWITCH_CPU_WIDE)
		return cs_etm__process_switch_cpu_wide(etm, event);

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
	 * Create an RB tree for traceID-metadata tuple.  Since the conversion
	 * has to be made for each packet that gets decoded, optimizing access
	 * in anything other than a sequential array is worth doing.
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
		/* All good, associate the traceID with the metadata pointer */
		inode->priv = metadata[j];
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

	etm->unknown_thread = thread__new(999999999, 999999999);
	if (!etm->unknown_thread)
		goto err_free_queues;

	/*
	 * Initialize list node so that at thread__zput() we can avoid
	 * segmentation fault at list_del_init().
	 */
	INIT_LIST_HEAD(&etm->unknown_thread->node);

	err = thread__set_comm(etm->unknown_thread, "unknown", 0);
	if (err)
		goto err_delete_thread;

	if (thread__init_map_groups(etm->unknown_thread, etm->machine))
		goto err_delete_thread;

	if (dump_trace) {
		cs_etm__print_auxtrace_info(auxtrace_info->priv, num_cpu);
		return 0;
	}

	if (session->itrace_synth_opts && session->itrace_synth_opts->set) {
		etm->synth_opts = *session->itrace_synth_opts;
	} else {
		itrace_synth_opts__set_default(&etm->synth_opts,
				session->itrace_synth_opts->default_no_sample);
		etm->synth_opts.callchain = false;
	}

	err = cs_etm__synth_events(etm, session);
	if (err)
		goto err_delete_thread;

	err = auxtrace_queues__process_index(&etm->queues, session);
	if (err)
		goto err_delete_thread;

	etm->data_queued = etm->queues.populated;

	return 0;

err_delete_thread:
	thread__zput(etm->unknown_thread);
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
