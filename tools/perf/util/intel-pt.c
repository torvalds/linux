// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel_pt.c: Intel Processor Trace support
 * Copyright (c) 2013-2015, Intel Corporation.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/zalloc.h>

#include "session.h"
#include "machine.h"
#include "memswap.h"
#include "sort.h"
#include "tool.h"
#include "event.h"
#include "evlist.h"
#include "evsel.h"
#include "map.h"
#include "color.h"
#include "thread.h"
#include "thread-stack.h"
#include "symbol.h"
#include "callchain.h"
#include "dso.h"
#include "debug.h"
#include "auxtrace.h"
#include "tsc.h"
#include "intel-pt.h"
#include "config.h"
#include "util/perf_api_probe.h"
#include "util/synthetic-events.h"
#include "time-utils.h"

#include "../arch/x86/include/uapi/asm/perf_regs.h"

#include "intel-pt-decoder/intel-pt-log.h"
#include "intel-pt-decoder/intel-pt-decoder.h"
#include "intel-pt-decoder/intel-pt-insn-decoder.h"
#include "intel-pt-decoder/intel-pt-pkt-decoder.h"

#define MAX_TIMESTAMP (~0ULL)

struct range {
	u64 start;
	u64 end;
};

struct intel_pt {
	struct auxtrace auxtrace;
	struct auxtrace_queues queues;
	struct auxtrace_heap heap;
	u32 auxtrace_type;
	struct perf_session *session;
	struct machine *machine;
	struct evsel *switch_evsel;
	struct thread *unknown_thread;
	bool timeless_decoding;
	bool sampling_mode;
	bool snapshot_mode;
	bool per_cpu_mmaps;
	bool have_tsc;
	bool data_queued;
	bool est_tsc;
	bool sync_switch;
	bool mispred_all;
	bool use_thread_stack;
	bool callstack;
	unsigned int br_stack_sz;
	unsigned int br_stack_sz_plus;
	int have_sched_switch;
	u32 pmu_type;
	u64 kernel_start;
	u64 switch_ip;
	u64 ptss_ip;

	struct perf_tsc_conversion tc;
	bool cap_user_time_zero;

	struct itrace_synth_opts synth_opts;

	bool sample_instructions;
	u64 instructions_sample_type;
	u64 instructions_id;

	bool sample_branches;
	u32 branches_filter;
	u64 branches_sample_type;
	u64 branches_id;

	bool sample_transactions;
	u64 transactions_sample_type;
	u64 transactions_id;

	bool sample_ptwrites;
	u64 ptwrites_sample_type;
	u64 ptwrites_id;

	bool sample_pwr_events;
	u64 pwr_events_sample_type;
	u64 mwait_id;
	u64 pwre_id;
	u64 exstop_id;
	u64 pwrx_id;
	u64 cbr_id;
	u64 psb_id;

	bool sample_pebs;
	struct evsel *pebs_evsel;

	u64 tsc_bit;
	u64 mtc_bit;
	u64 mtc_freq_bits;
	u32 tsc_ctc_ratio_n;
	u32 tsc_ctc_ratio_d;
	u64 cyc_bit;
	u64 noretcomp_bit;
	unsigned max_non_turbo_ratio;
	unsigned cbr2khz;

	unsigned long num_events;

	char *filter;
	struct addr_filters filts;

	struct range *time_ranges;
	unsigned int range_cnt;

	struct ip_callchain *chain;
	struct branch_stack *br_stack;
};

enum switch_state {
	INTEL_PT_SS_NOT_TRACING,
	INTEL_PT_SS_UNKNOWN,
	INTEL_PT_SS_TRACING,
	INTEL_PT_SS_EXPECTING_SWITCH_EVENT,
	INTEL_PT_SS_EXPECTING_SWITCH_IP,
};

struct intel_pt_queue {
	struct intel_pt *pt;
	unsigned int queue_nr;
	struct auxtrace_buffer *buffer;
	struct auxtrace_buffer *old_buffer;
	void *decoder;
	const struct intel_pt_state *state;
	struct ip_callchain *chain;
	struct branch_stack *last_branch;
	union perf_event *event_buf;
	bool on_heap;
	bool stop;
	bool step_through_buffers;
	bool use_buffer_pid_tid;
	bool sync_switch;
	pid_t pid, tid;
	int cpu;
	int switch_state;
	pid_t next_tid;
	struct thread *thread;
	struct machine *guest_machine;
	struct thread *unknown_guest_thread;
	pid_t guest_machine_pid;
	bool exclude_kernel;
	bool have_sample;
	u64 time;
	u64 timestamp;
	u64 sel_timestamp;
	bool sel_start;
	unsigned int sel_idx;
	u32 flags;
	u16 insn_len;
	u64 last_insn_cnt;
	u64 ipc_insn_cnt;
	u64 ipc_cyc_cnt;
	u64 last_in_insn_cnt;
	u64 last_in_cyc_cnt;
	u64 last_br_insn_cnt;
	u64 last_br_cyc_cnt;
	unsigned int cbr_seen;
	char insn[INTEL_PT_INSN_BUF_SZ];
};

static void intel_pt_dump(struct intel_pt *pt __maybe_unused,
			  unsigned char *buf, size_t len)
{
	struct intel_pt_pkt packet;
	size_t pos = 0;
	int ret, pkt_len, i;
	char desc[INTEL_PT_PKT_DESC_MAX];
	const char *color = PERF_COLOR_BLUE;
	enum intel_pt_pkt_ctx ctx = INTEL_PT_NO_CTX;

	color_fprintf(stdout, color,
		      ". ... Intel Processor Trace data: size %zu bytes\n",
		      len);

	while (len) {
		ret = intel_pt_get_packet(buf, len, &packet, &ctx);
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
			ret = intel_pt_pkt_desc(&packet, desc,
						INTEL_PT_PKT_DESC_MAX);
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

static void intel_pt_dump_event(struct intel_pt *pt, unsigned char *buf,
				size_t len)
{
	printf(".\n");
	intel_pt_dump(pt, buf, len);
}

static void intel_pt_log_event(union perf_event *event)
{
	FILE *f = intel_pt_log_fp();

	if (!intel_pt_enable_logging || !f)
		return;

	perf_event__fprintf(event, NULL, f);
}

static void intel_pt_dump_sample(struct perf_session *session,
				 struct perf_sample *sample)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);

	printf("\n");
	intel_pt_dump(pt, sample->aux_sample.data, sample->aux_sample.size);
}

static bool intel_pt_log_events(struct intel_pt *pt, u64 tm)
{
	struct perf_time_interval *range = pt->synth_opts.ptime_range;
	int n = pt->synth_opts.range_num;

	if (pt->synth_opts.log_plus_flags & AUXTRACE_LOG_FLG_ALL_PERF_EVTS)
		return true;

	if (pt->synth_opts.log_minus_flags & AUXTRACE_LOG_FLG_ALL_PERF_EVTS)
		return false;

	/* perf_time__ranges_skip_sample does not work if time is zero */
	if (!tm)
		tm = 1;

	return !n || !perf_time__ranges_skip_sample(range, n, tm);
}

static int intel_pt_do_fix_overlap(struct intel_pt *pt, struct auxtrace_buffer *a,
				   struct auxtrace_buffer *b)
{
	bool consecutive = false;
	void *start;

	start = intel_pt_find_overlap(a->data, a->size, b->data, b->size,
				      pt->have_tsc, &consecutive);
	if (!start)
		return -EINVAL;
	b->use_size = b->data + b->size - start;
	b->use_data = start;
	if (b->use_size && consecutive)
		b->consecutive = true;
	return 0;
}

static int intel_pt_get_buffer(struct intel_pt_queue *ptq,
			       struct auxtrace_buffer *buffer,
			       struct auxtrace_buffer *old_buffer,
			       struct intel_pt_buffer *b)
{
	bool might_overlap;

	if (!buffer->data) {
		int fd = perf_data__fd(ptq->pt->session->data);

		buffer->data = auxtrace_buffer__get_data(buffer, fd);
		if (!buffer->data)
			return -ENOMEM;
	}

	might_overlap = ptq->pt->snapshot_mode || ptq->pt->sampling_mode;
	if (might_overlap && !buffer->consecutive && old_buffer &&
	    intel_pt_do_fix_overlap(ptq->pt, old_buffer, buffer))
		return -ENOMEM;

	if (buffer->use_data) {
		b->len = buffer->use_size;
		b->buf = buffer->use_data;
	} else {
		b->len = buffer->size;
		b->buf = buffer->data;
	}
	b->ref_timestamp = buffer->reference;

	if (!old_buffer || (might_overlap && !buffer->consecutive)) {
		b->consecutive = false;
		b->trace_nr = buffer->buffer_nr + 1;
	} else {
		b->consecutive = true;
	}

	return 0;
}

/* Do not drop buffers with references - refer intel_pt_get_trace() */
static void intel_pt_lookahead_drop_buffer(struct intel_pt_queue *ptq,
					   struct auxtrace_buffer *buffer)
{
	if (!buffer || buffer == ptq->buffer || buffer == ptq->old_buffer)
		return;

	auxtrace_buffer__drop_data(buffer);
}

/* Must be serialized with respect to intel_pt_get_trace() */
static int intel_pt_lookahead(void *data, intel_pt_lookahead_cb_t cb,
			      void *cb_data)
{
	struct intel_pt_queue *ptq = data;
	struct auxtrace_buffer *buffer = ptq->buffer;
	struct auxtrace_buffer *old_buffer = ptq->old_buffer;
	struct auxtrace_queue *queue;
	int err = 0;

	queue = &ptq->pt->queues.queue_array[ptq->queue_nr];

	while (1) {
		struct intel_pt_buffer b = { .len = 0 };

		buffer = auxtrace_buffer__next(queue, buffer);
		if (!buffer)
			break;

		err = intel_pt_get_buffer(ptq, buffer, old_buffer, &b);
		if (err)
			break;

		if (b.len) {
			intel_pt_lookahead_drop_buffer(ptq, old_buffer);
			old_buffer = buffer;
		} else {
			intel_pt_lookahead_drop_buffer(ptq, buffer);
			continue;
		}

		err = cb(&b, cb_data);
		if (err)
			break;
	}

	if (buffer != old_buffer)
		intel_pt_lookahead_drop_buffer(ptq, buffer);
	intel_pt_lookahead_drop_buffer(ptq, old_buffer);

	return err;
}

/*
 * This function assumes data is processed sequentially only.
 * Must be serialized with respect to intel_pt_lookahead()
 */
static int intel_pt_get_trace(struct intel_pt_buffer *b, void *data)
{
	struct intel_pt_queue *ptq = data;
	struct auxtrace_buffer *buffer = ptq->buffer;
	struct auxtrace_buffer *old_buffer = ptq->old_buffer;
	struct auxtrace_queue *queue;
	int err;

	if (ptq->stop) {
		b->len = 0;
		return 0;
	}

	queue = &ptq->pt->queues.queue_array[ptq->queue_nr];

	buffer = auxtrace_buffer__next(queue, buffer);
	if (!buffer) {
		if (old_buffer)
			auxtrace_buffer__drop_data(old_buffer);
		b->len = 0;
		return 0;
	}

	ptq->buffer = buffer;

	err = intel_pt_get_buffer(ptq, buffer, old_buffer, b);
	if (err)
		return err;

	if (ptq->step_through_buffers)
		ptq->stop = true;

	if (b->len) {
		if (old_buffer)
			auxtrace_buffer__drop_data(old_buffer);
		ptq->old_buffer = buffer;
	} else {
		auxtrace_buffer__drop_data(buffer);
		return intel_pt_get_trace(b, data);
	}

	return 0;
}

struct intel_pt_cache_entry {
	struct auxtrace_cache_entry	entry;
	u64				insn_cnt;
	u64				byte_cnt;
	enum intel_pt_insn_op		op;
	enum intel_pt_insn_branch	branch;
	int				length;
	int32_t				rel;
	char				insn[INTEL_PT_INSN_BUF_SZ];
};

static int intel_pt_config_div(const char *var, const char *value, void *data)
{
	int *d = data;
	long val;

	if (!strcmp(var, "intel-pt.cache-divisor")) {
		val = strtol(value, NULL, 0);
		if (val > 0 && val <= INT_MAX)
			*d = val;
	}

	return 0;
}

static int intel_pt_cache_divisor(void)
{
	static int d;

	if (d)
		return d;

	perf_config(intel_pt_config_div, &d);

	if (!d)
		d = 64;

	return d;
}

static unsigned int intel_pt_cache_size(struct dso *dso,
					struct machine *machine)
{
	off_t size;

	size = dso__data_size(dso, machine);
	size /= intel_pt_cache_divisor();
	if (size < 1000)
		return 10;
	if (size > (1 << 21))
		return 21;
	return 32 - __builtin_clz(size);
}

static struct auxtrace_cache *intel_pt_cache(struct dso *dso,
					     struct machine *machine)
{
	struct auxtrace_cache *c;
	unsigned int bits;

	if (dso->auxtrace_cache)
		return dso->auxtrace_cache;

	bits = intel_pt_cache_size(dso, machine);

	/* Ignoring cache creation failure */
	c = auxtrace_cache__new(bits, sizeof(struct intel_pt_cache_entry), 200);

	dso->auxtrace_cache = c;

	return c;
}

static int intel_pt_cache_add(struct dso *dso, struct machine *machine,
			      u64 offset, u64 insn_cnt, u64 byte_cnt,
			      struct intel_pt_insn *intel_pt_insn)
{
	struct auxtrace_cache *c = intel_pt_cache(dso, machine);
	struct intel_pt_cache_entry *e;
	int err;

	if (!c)
		return -ENOMEM;

	e = auxtrace_cache__alloc_entry(c);
	if (!e)
		return -ENOMEM;

	e->insn_cnt = insn_cnt;
	e->byte_cnt = byte_cnt;
	e->op = intel_pt_insn->op;
	e->branch = intel_pt_insn->branch;
	e->length = intel_pt_insn->length;
	e->rel = intel_pt_insn->rel;
	memcpy(e->insn, intel_pt_insn->buf, INTEL_PT_INSN_BUF_SZ);

	err = auxtrace_cache__add(c, offset, &e->entry);
	if (err)
		auxtrace_cache__free_entry(c, e);

	return err;
}

static struct intel_pt_cache_entry *
intel_pt_cache_lookup(struct dso *dso, struct machine *machine, u64 offset)
{
	struct auxtrace_cache *c = intel_pt_cache(dso, machine);

	if (!c)
		return NULL;

	return auxtrace_cache__lookup(dso->auxtrace_cache, offset);
}

static void intel_pt_cache_invalidate(struct dso *dso, struct machine *machine,
				      u64 offset)
{
	struct auxtrace_cache *c = intel_pt_cache(dso, machine);

	if (!c)
		return;

	auxtrace_cache__remove(dso->auxtrace_cache, offset);
}

static inline bool intel_pt_guest_kernel_ip(uint64_t ip)
{
	/* Assumes 64-bit kernel */
	return ip & (1ULL << 63);
}

static inline u8 intel_pt_nr_cpumode(struct intel_pt_queue *ptq, uint64_t ip, bool nr)
{
	if (nr) {
		return intel_pt_guest_kernel_ip(ip) ?
		       PERF_RECORD_MISC_GUEST_KERNEL :
		       PERF_RECORD_MISC_GUEST_USER;
	}

	return ip >= ptq->pt->kernel_start ?
	       PERF_RECORD_MISC_KERNEL :
	       PERF_RECORD_MISC_USER;
}

static inline u8 intel_pt_cpumode(struct intel_pt_queue *ptq, uint64_t from_ip, uint64_t to_ip)
{
	/* No support for non-zero CS base */
	if (from_ip)
		return intel_pt_nr_cpumode(ptq, from_ip, ptq->state->from_nr);
	return intel_pt_nr_cpumode(ptq, to_ip, ptq->state->to_nr);
}

static int intel_pt_get_guest(struct intel_pt_queue *ptq)
{
	struct machines *machines = &ptq->pt->session->machines;
	struct machine *machine;
	pid_t pid = ptq->pid <= 0 ? DEFAULT_GUEST_KERNEL_ID : ptq->pid;

	if (ptq->guest_machine && pid == ptq->guest_machine_pid)
		return 0;

	ptq->guest_machine = NULL;
	thread__zput(ptq->unknown_guest_thread);

	machine = machines__find_guest(machines, pid);
	if (!machine)
		return -1;

	ptq->unknown_guest_thread = machine__idle_thread(machine);
	if (!ptq->unknown_guest_thread)
		return -1;

	ptq->guest_machine = machine;
	ptq->guest_machine_pid = pid;

	return 0;
}

static int intel_pt_walk_next_insn(struct intel_pt_insn *intel_pt_insn,
				   uint64_t *insn_cnt_ptr, uint64_t *ip,
				   uint64_t to_ip, uint64_t max_insn_cnt,
				   void *data)
{
	struct intel_pt_queue *ptq = data;
	struct machine *machine = ptq->pt->machine;
	struct thread *thread;
	struct addr_location al;
	unsigned char buf[INTEL_PT_INSN_BUF_SZ];
	ssize_t len;
	int x86_64;
	u8 cpumode;
	u64 offset, start_offset, start_ip;
	u64 insn_cnt = 0;
	bool one_map = true;
	bool nr;

	intel_pt_insn->length = 0;

	if (to_ip && *ip == to_ip)
		goto out_no_cache;

	nr = ptq->state->to_nr;
	cpumode = intel_pt_nr_cpumode(ptq, *ip, nr);

	if (nr) {
		if (cpumode != PERF_RECORD_MISC_GUEST_KERNEL ||
		    intel_pt_get_guest(ptq))
			return -EINVAL;
		machine = ptq->guest_machine;
		thread = ptq->unknown_guest_thread;
	} else {
		thread = ptq->thread;
		if (!thread) {
			if (cpumode != PERF_RECORD_MISC_KERNEL)
				return -EINVAL;
			thread = ptq->pt->unknown_thread;
		}
	}

	while (1) {
		if (!thread__find_map(thread, cpumode, *ip, &al) || !al.map->dso)
			return -EINVAL;

		if (al.map->dso->data.status == DSO_DATA_STATUS_ERROR &&
		    dso__data_status_seen(al.map->dso,
					  DSO_DATA_STATUS_SEEN_ITRACE))
			return -ENOENT;

		offset = al.map->map_ip(al.map, *ip);

		if (!to_ip && one_map) {
			struct intel_pt_cache_entry *e;

			e = intel_pt_cache_lookup(al.map->dso, machine, offset);
			if (e &&
			    (!max_insn_cnt || e->insn_cnt <= max_insn_cnt)) {
				*insn_cnt_ptr = e->insn_cnt;
				*ip += e->byte_cnt;
				intel_pt_insn->op = e->op;
				intel_pt_insn->branch = e->branch;
				intel_pt_insn->length = e->length;
				intel_pt_insn->rel = e->rel;
				memcpy(intel_pt_insn->buf, e->insn,
				       INTEL_PT_INSN_BUF_SZ);
				intel_pt_log_insn_no_data(intel_pt_insn, *ip);
				return 0;
			}
		}

		start_offset = offset;
		start_ip = *ip;

		/* Load maps to ensure dso->is_64_bit has been updated */
		map__load(al.map);

		x86_64 = al.map->dso->is_64_bit;

		while (1) {
			len = dso__data_read_offset(al.map->dso, machine,
						    offset, buf,
						    INTEL_PT_INSN_BUF_SZ);
			if (len <= 0)
				return -EINVAL;

			if (intel_pt_get_insn(buf, len, x86_64, intel_pt_insn))
				return -EINVAL;

			intel_pt_log_insn(intel_pt_insn, *ip);

			insn_cnt += 1;

			if (intel_pt_insn->branch != INTEL_PT_BR_NO_BRANCH)
				goto out;

			if (max_insn_cnt && insn_cnt >= max_insn_cnt)
				goto out_no_cache;

			*ip += intel_pt_insn->length;

			if (to_ip && *ip == to_ip) {
				intel_pt_insn->length = 0;
				goto out_no_cache;
			}

			if (*ip >= al.map->end)
				break;

			offset += intel_pt_insn->length;
		}
		one_map = false;
	}
out:
	*insn_cnt_ptr = insn_cnt;

	if (!one_map)
		goto out_no_cache;

	/*
	 * Didn't lookup in the 'to_ip' case, so do it now to prevent duplicate
	 * entries.
	 */
	if (to_ip) {
		struct intel_pt_cache_entry *e;

		e = intel_pt_cache_lookup(al.map->dso, machine, start_offset);
		if (e)
			return 0;
	}

	/* Ignore cache errors */
	intel_pt_cache_add(al.map->dso, machine, start_offset, insn_cnt,
			   *ip - start_ip, intel_pt_insn);

	return 0;

out_no_cache:
	*insn_cnt_ptr = insn_cnt;
	return 0;
}

static bool intel_pt_match_pgd_ip(struct intel_pt *pt, uint64_t ip,
				  uint64_t offset, const char *filename)
{
	struct addr_filter *filt;
	bool have_filter   = false;
	bool hit_tracestop = false;
	bool hit_filter    = false;

	list_for_each_entry(filt, &pt->filts.head, list) {
		if (filt->start)
			have_filter = true;

		if ((filename && !filt->filename) ||
		    (!filename && filt->filename) ||
		    (filename && strcmp(filename, filt->filename)))
			continue;

		if (!(offset >= filt->addr && offset < filt->addr + filt->size))
			continue;

		intel_pt_log("TIP.PGD ip %#"PRIx64" offset %#"PRIx64" in %s hit filter: %s offset %#"PRIx64" size %#"PRIx64"\n",
			     ip, offset, filename ? filename : "[kernel]",
			     filt->start ? "filter" : "stop",
			     filt->addr, filt->size);

		if (filt->start)
			hit_filter = true;
		else
			hit_tracestop = true;
	}

	if (!hit_tracestop && !hit_filter)
		intel_pt_log("TIP.PGD ip %#"PRIx64" offset %#"PRIx64" in %s is not in a filter region\n",
			     ip, offset, filename ? filename : "[kernel]");

	return hit_tracestop || (have_filter && !hit_filter);
}

static int __intel_pt_pgd_ip(uint64_t ip, void *data)
{
	struct intel_pt_queue *ptq = data;
	struct thread *thread;
	struct addr_location al;
	u8 cpumode;
	u64 offset;

	if (ptq->state->to_nr) {
		if (intel_pt_guest_kernel_ip(ip))
			return intel_pt_match_pgd_ip(ptq->pt, ip, ip, NULL);
		/* No support for decoding guest user space */
		return -EINVAL;
	} else if (ip >= ptq->pt->kernel_start) {
		return intel_pt_match_pgd_ip(ptq->pt, ip, ip, NULL);
	}

	cpumode = PERF_RECORD_MISC_USER;

	thread = ptq->thread;
	if (!thread)
		return -EINVAL;

	if (!thread__find_map(thread, cpumode, ip, &al) || !al.map->dso)
		return -EINVAL;

	offset = al.map->map_ip(al.map, ip);

	return intel_pt_match_pgd_ip(ptq->pt, ip, offset,
				     al.map->dso->long_name);
}

static bool intel_pt_pgd_ip(uint64_t ip, void *data)
{
	return __intel_pt_pgd_ip(ip, data) > 0;
}

static bool intel_pt_get_config(struct intel_pt *pt,
				struct perf_event_attr *attr, u64 *config)
{
	if (attr->type == pt->pmu_type) {
		if (config)
			*config = attr->config;
		return true;
	}

	return false;
}

static bool intel_pt_exclude_kernel(struct intel_pt *pt)
{
	struct evsel *evsel;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->core.attr, NULL) &&
		    !evsel->core.attr.exclude_kernel)
			return false;
	}
	return true;
}

static bool intel_pt_return_compression(struct intel_pt *pt)
{
	struct evsel *evsel;
	u64 config;

	if (!pt->noretcomp_bit)
		return true;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->core.attr, &config) &&
		    (config & pt->noretcomp_bit))
			return false;
	}
	return true;
}

static bool intel_pt_branch_enable(struct intel_pt *pt)
{
	struct evsel *evsel;
	u64 config;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->core.attr, &config) &&
		    (config & 1) && !(config & 0x2000))
			return false;
	}
	return true;
}

static unsigned int intel_pt_mtc_period(struct intel_pt *pt)
{
	struct evsel *evsel;
	unsigned int shift;
	u64 config;

	if (!pt->mtc_freq_bits)
		return 0;

	for (shift = 0, config = pt->mtc_freq_bits; !(config & 1); shift++)
		config >>= 1;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->core.attr, &config))
			return (config & pt->mtc_freq_bits) >> shift;
	}
	return 0;
}

static bool intel_pt_timeless_decoding(struct intel_pt *pt)
{
	struct evsel *evsel;
	bool timeless_decoding = true;
	u64 config;

	if (!pt->tsc_bit || !pt->cap_user_time_zero)
		return true;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (!(evsel->core.attr.sample_type & PERF_SAMPLE_TIME))
			return true;
		if (intel_pt_get_config(pt, &evsel->core.attr, &config)) {
			if (config & pt->tsc_bit)
				timeless_decoding = false;
			else
				return true;
		}
	}
	return timeless_decoding;
}

static bool intel_pt_tracing_kernel(struct intel_pt *pt)
{
	struct evsel *evsel;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->core.attr, NULL) &&
		    !evsel->core.attr.exclude_kernel)
			return true;
	}
	return false;
}

static bool intel_pt_have_tsc(struct intel_pt *pt)
{
	struct evsel *evsel;
	bool have_tsc = false;
	u64 config;

	if (!pt->tsc_bit)
		return false;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->core.attr, &config)) {
			if (config & pt->tsc_bit)
				have_tsc = true;
			else
				return false;
		}
	}
	return have_tsc;
}

static bool intel_pt_sampling_mode(struct intel_pt *pt)
{
	struct evsel *evsel;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if ((evsel->core.attr.sample_type & PERF_SAMPLE_AUX) &&
		    evsel->core.attr.aux_sample_size)
			return true;
	}
	return false;
}

static u64 intel_pt_ctl(struct intel_pt *pt)
{
	struct evsel *evsel;
	u64 config;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (intel_pt_get_config(pt, &evsel->core.attr, &config))
			return config;
	}
	return 0;
}

static u64 intel_pt_ns_to_ticks(const struct intel_pt *pt, u64 ns)
{
	u64 quot, rem;

	quot = ns / pt->tc.time_mult;
	rem  = ns % pt->tc.time_mult;
	return (quot << pt->tc.time_shift) + (rem << pt->tc.time_shift) /
		pt->tc.time_mult;
}

static struct ip_callchain *intel_pt_alloc_chain(struct intel_pt *pt)
{
	size_t sz = sizeof(struct ip_callchain);

	/* Add 1 to callchain_sz for callchain context */
	sz += (pt->synth_opts.callchain_sz + 1) * sizeof(u64);
	return zalloc(sz);
}

static int intel_pt_callchain_init(struct intel_pt *pt)
{
	struct evsel *evsel;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (!(evsel->core.attr.sample_type & PERF_SAMPLE_CALLCHAIN))
			evsel->synth_sample_type |= PERF_SAMPLE_CALLCHAIN;
	}

	pt->chain = intel_pt_alloc_chain(pt);
	if (!pt->chain)
		return -ENOMEM;

	return 0;
}

static void intel_pt_add_callchain(struct intel_pt *pt,
				   struct perf_sample *sample)
{
	struct thread *thread = machine__findnew_thread(pt->machine,
							sample->pid,
							sample->tid);

	thread_stack__sample_late(thread, sample->cpu, pt->chain,
				  pt->synth_opts.callchain_sz + 1, sample->ip,
				  pt->kernel_start);

	sample->callchain = pt->chain;
}

static struct branch_stack *intel_pt_alloc_br_stack(unsigned int entry_cnt)
{
	size_t sz = sizeof(struct branch_stack);

	sz += entry_cnt * sizeof(struct branch_entry);
	return zalloc(sz);
}

static int intel_pt_br_stack_init(struct intel_pt *pt)
{
	struct evsel *evsel;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (!(evsel->core.attr.sample_type & PERF_SAMPLE_BRANCH_STACK))
			evsel->synth_sample_type |= PERF_SAMPLE_BRANCH_STACK;
	}

	pt->br_stack = intel_pt_alloc_br_stack(pt->br_stack_sz);
	if (!pt->br_stack)
		return -ENOMEM;

	return 0;
}

static void intel_pt_add_br_stack(struct intel_pt *pt,
				  struct perf_sample *sample)
{
	struct thread *thread = machine__findnew_thread(pt->machine,
							sample->pid,
							sample->tid);

	thread_stack__br_sample_late(thread, sample->cpu, pt->br_stack,
				     pt->br_stack_sz, sample->ip,
				     pt->kernel_start);

	sample->branch_stack = pt->br_stack;
}

/* INTEL_PT_LBR_0, INTEL_PT_LBR_1 and INTEL_PT_LBR_2 */
#define LBRS_MAX (INTEL_PT_BLK_ITEM_ID_CNT * 3U)

static struct intel_pt_queue *intel_pt_alloc_queue(struct intel_pt *pt,
						   unsigned int queue_nr)
{
	struct intel_pt_params params = { .get_trace = 0, };
	struct perf_env *env = pt->machine->env;
	struct intel_pt_queue *ptq;

	ptq = zalloc(sizeof(struct intel_pt_queue));
	if (!ptq)
		return NULL;

	if (pt->synth_opts.callchain) {
		ptq->chain = intel_pt_alloc_chain(pt);
		if (!ptq->chain)
			goto out_free;
	}

	if (pt->synth_opts.last_branch || pt->synth_opts.other_events) {
		unsigned int entry_cnt = max(LBRS_MAX, pt->br_stack_sz);

		ptq->last_branch = intel_pt_alloc_br_stack(entry_cnt);
		if (!ptq->last_branch)
			goto out_free;
	}

	ptq->event_buf = malloc(PERF_SAMPLE_MAX_SIZE);
	if (!ptq->event_buf)
		goto out_free;

	ptq->pt = pt;
	ptq->queue_nr = queue_nr;
	ptq->exclude_kernel = intel_pt_exclude_kernel(pt);
	ptq->pid = -1;
	ptq->tid = -1;
	ptq->cpu = -1;
	ptq->next_tid = -1;

	params.get_trace = intel_pt_get_trace;
	params.walk_insn = intel_pt_walk_next_insn;
	params.lookahead = intel_pt_lookahead;
	params.data = ptq;
	params.return_compression = intel_pt_return_compression(pt);
	params.branch_enable = intel_pt_branch_enable(pt);
	params.ctl = intel_pt_ctl(pt);
	params.max_non_turbo_ratio = pt->max_non_turbo_ratio;
	params.mtc_period = intel_pt_mtc_period(pt);
	params.tsc_ctc_ratio_n = pt->tsc_ctc_ratio_n;
	params.tsc_ctc_ratio_d = pt->tsc_ctc_ratio_d;
	params.quick = pt->synth_opts.quick;

	if (pt->filts.cnt > 0)
		params.pgd_ip = intel_pt_pgd_ip;

	if (pt->synth_opts.instructions) {
		if (pt->synth_opts.period) {
			switch (pt->synth_opts.period_type) {
			case PERF_ITRACE_PERIOD_INSTRUCTIONS:
				params.period_type =
						INTEL_PT_PERIOD_INSTRUCTIONS;
				params.period = pt->synth_opts.period;
				break;
			case PERF_ITRACE_PERIOD_TICKS:
				params.period_type = INTEL_PT_PERIOD_TICKS;
				params.period = pt->synth_opts.period;
				break;
			case PERF_ITRACE_PERIOD_NANOSECS:
				params.period_type = INTEL_PT_PERIOD_TICKS;
				params.period = intel_pt_ns_to_ticks(pt,
							pt->synth_opts.period);
				break;
			default:
				break;
			}
		}

		if (!params.period) {
			params.period_type = INTEL_PT_PERIOD_INSTRUCTIONS;
			params.period = 1;
		}
	}

	if (env->cpuid && !strncmp(env->cpuid, "GenuineIntel,6,92,", 18))
		params.flags |= INTEL_PT_FUP_WITH_NLIP;

	ptq->decoder = intel_pt_decoder_new(&params);
	if (!ptq->decoder)
		goto out_free;

	return ptq;

out_free:
	zfree(&ptq->event_buf);
	zfree(&ptq->last_branch);
	zfree(&ptq->chain);
	free(ptq);
	return NULL;
}

static void intel_pt_free_queue(void *priv)
{
	struct intel_pt_queue *ptq = priv;

	if (!ptq)
		return;
	thread__zput(ptq->thread);
	thread__zput(ptq->unknown_guest_thread);
	intel_pt_decoder_free(ptq->decoder);
	zfree(&ptq->event_buf);
	zfree(&ptq->last_branch);
	zfree(&ptq->chain);
	free(ptq);
}

static void intel_pt_set_pid_tid_cpu(struct intel_pt *pt,
				     struct auxtrace_queue *queue)
{
	struct intel_pt_queue *ptq = queue->priv;

	if (queue->tid == -1 || pt->have_sched_switch) {
		ptq->tid = machine__get_current_tid(pt->machine, ptq->cpu);
		if (ptq->tid == -1)
			ptq->pid = -1;
		thread__zput(ptq->thread);
	}

	if (!ptq->thread && ptq->tid != -1)
		ptq->thread = machine__find_thread(pt->machine, -1, ptq->tid);

	if (ptq->thread) {
		ptq->pid = ptq->thread->pid_;
		if (queue->cpu == -1)
			ptq->cpu = ptq->thread->cpu;
	}
}

static void intel_pt_sample_flags(struct intel_pt_queue *ptq)
{
	ptq->insn_len = 0;
	if (ptq->state->flags & INTEL_PT_ABORT_TX) {
		ptq->flags = PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_TX_ABORT;
	} else if (ptq->state->flags & INTEL_PT_ASYNC) {
		if (!ptq->state->to_ip)
			ptq->flags = PERF_IP_FLAG_BRANCH |
				     PERF_IP_FLAG_TRACE_END;
		else if (ptq->state->from_nr && !ptq->state->to_nr)
			ptq->flags = PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL |
				     PERF_IP_FLAG_VMEXIT;
		else
			ptq->flags = PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL |
				     PERF_IP_FLAG_ASYNC |
				     PERF_IP_FLAG_INTERRUPT;
	} else {
		if (ptq->state->from_ip)
			ptq->flags = intel_pt_insn_type(ptq->state->insn_op);
		else
			ptq->flags = PERF_IP_FLAG_BRANCH |
				     PERF_IP_FLAG_TRACE_BEGIN;
		if (ptq->state->flags & INTEL_PT_IN_TX)
			ptq->flags |= PERF_IP_FLAG_IN_TX;
		ptq->insn_len = ptq->state->insn_len;
		memcpy(ptq->insn, ptq->state->insn, INTEL_PT_INSN_BUF_SZ);
	}

	if (ptq->state->type & INTEL_PT_TRACE_BEGIN)
		ptq->flags |= PERF_IP_FLAG_TRACE_BEGIN;
	if (ptq->state->type & INTEL_PT_TRACE_END)
		ptq->flags |= PERF_IP_FLAG_TRACE_END;
}

static void intel_pt_setup_time_range(struct intel_pt *pt,
				      struct intel_pt_queue *ptq)
{
	if (!pt->range_cnt)
		return;

	ptq->sel_timestamp = pt->time_ranges[0].start;
	ptq->sel_idx = 0;

	if (ptq->sel_timestamp) {
		ptq->sel_start = true;
	} else {
		ptq->sel_timestamp = pt->time_ranges[0].end;
		ptq->sel_start = false;
	}
}

static int intel_pt_setup_queue(struct intel_pt *pt,
				struct auxtrace_queue *queue,
				unsigned int queue_nr)
{
	struct intel_pt_queue *ptq = queue->priv;

	if (list_empty(&queue->head))
		return 0;

	if (!ptq) {
		ptq = intel_pt_alloc_queue(pt, queue_nr);
		if (!ptq)
			return -ENOMEM;
		queue->priv = ptq;

		if (queue->cpu != -1)
			ptq->cpu = queue->cpu;
		ptq->tid = queue->tid;

		ptq->cbr_seen = UINT_MAX;

		if (pt->sampling_mode && !pt->snapshot_mode &&
		    pt->timeless_decoding)
			ptq->step_through_buffers = true;

		ptq->sync_switch = pt->sync_switch;

		intel_pt_setup_time_range(pt, ptq);
	}

	if (!ptq->on_heap &&
	    (!ptq->sync_switch ||
	     ptq->switch_state != INTEL_PT_SS_EXPECTING_SWITCH_EVENT)) {
		const struct intel_pt_state *state;
		int ret;

		if (pt->timeless_decoding)
			return 0;

		intel_pt_log("queue %u getting timestamp\n", queue_nr);
		intel_pt_log("queue %u decoding cpu %d pid %d tid %d\n",
			     queue_nr, ptq->cpu, ptq->pid, ptq->tid);

		if (ptq->sel_start && ptq->sel_timestamp) {
			ret = intel_pt_fast_forward(ptq->decoder,
						    ptq->sel_timestamp);
			if (ret)
				return ret;
		}

		while (1) {
			state = intel_pt_decode(ptq->decoder);
			if (state->err) {
				if (state->err == INTEL_PT_ERR_NODATA) {
					intel_pt_log("queue %u has no timestamp\n",
						     queue_nr);
					return 0;
				}
				continue;
			}
			if (state->timestamp)
				break;
		}

		ptq->timestamp = state->timestamp;
		intel_pt_log("queue %u timestamp 0x%" PRIx64 "\n",
			     queue_nr, ptq->timestamp);
		ptq->state = state;
		ptq->have_sample = true;
		if (ptq->sel_start && ptq->sel_timestamp &&
		    ptq->timestamp < ptq->sel_timestamp)
			ptq->have_sample = false;
		intel_pt_sample_flags(ptq);
		ret = auxtrace_heap__add(&pt->heap, queue_nr, ptq->timestamp);
		if (ret)
			return ret;
		ptq->on_heap = true;
	}

	return 0;
}

static int intel_pt_setup_queues(struct intel_pt *pt)
{
	unsigned int i;
	int ret;

	for (i = 0; i < pt->queues.nr_queues; i++) {
		ret = intel_pt_setup_queue(pt, &pt->queues.queue_array[i], i);
		if (ret)
			return ret;
	}
	return 0;
}

static inline bool intel_pt_skip_event(struct intel_pt *pt)
{
	return pt->synth_opts.initial_skip &&
	       pt->num_events++ < pt->synth_opts.initial_skip;
}

/*
 * Cannot count CBR as skipped because it won't go away until cbr == cbr_seen.
 * Also ensure CBR is first non-skipped event by allowing for 4 more samples
 * from this decoder state.
 */
static inline bool intel_pt_skip_cbr_event(struct intel_pt *pt)
{
	return pt->synth_opts.initial_skip &&
	       pt->num_events + 4 < pt->synth_opts.initial_skip;
}

static void intel_pt_prep_a_sample(struct intel_pt_queue *ptq,
				   union perf_event *event,
				   struct perf_sample *sample)
{
	event->sample.header.type = PERF_RECORD_SAMPLE;
	event->sample.header.size = sizeof(struct perf_event_header);

	sample->pid = ptq->pid;
	sample->tid = ptq->tid;
	sample->cpu = ptq->cpu;
	sample->insn_len = ptq->insn_len;
	memcpy(sample->insn, ptq->insn, INTEL_PT_INSN_BUF_SZ);
}

static void intel_pt_prep_b_sample(struct intel_pt *pt,
				   struct intel_pt_queue *ptq,
				   union perf_event *event,
				   struct perf_sample *sample)
{
	intel_pt_prep_a_sample(ptq, event, sample);

	if (!pt->timeless_decoding)
		sample->time = tsc_to_perf_time(ptq->timestamp, &pt->tc);

	sample->ip = ptq->state->from_ip;
	sample->addr = ptq->state->to_ip;
	sample->cpumode = intel_pt_cpumode(ptq, sample->ip, sample->addr);
	sample->period = 1;
	sample->flags = ptq->flags;

	event->sample.header.misc = sample->cpumode;
}

static int intel_pt_inject_event(union perf_event *event,
				 struct perf_sample *sample, u64 type)
{
	event->header.size = perf_event__sample_event_size(sample, type, 0);
	return perf_event__synthesize_sample(event, type, 0, sample);
}

static inline int intel_pt_opt_inject(struct intel_pt *pt,
				      union perf_event *event,
				      struct perf_sample *sample, u64 type)
{
	if (!pt->synth_opts.inject)
		return 0;

	return intel_pt_inject_event(event, sample, type);
}

static int intel_pt_deliver_synth_event(struct intel_pt *pt,
					union perf_event *event,
					struct perf_sample *sample, u64 type)
{
	int ret;

	ret = intel_pt_opt_inject(pt, event, sample, type);
	if (ret)
		return ret;

	ret = perf_session__deliver_synth_event(pt->session, event, sample);
	if (ret)
		pr_err("Intel PT: failed to deliver event, error %d\n", ret);

	return ret;
}

static int intel_pt_synth_branch_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct dummy_branch_stack {
		u64			nr;
		u64			hw_idx;
		struct branch_entry	entries;
	} dummy_bs;

	if (pt->branches_filter && !(pt->branches_filter & ptq->flags))
		return 0;

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_b_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->branches_id;
	sample.stream_id = ptq->pt->branches_id;

	/*
	 * perf report cannot handle events without a branch stack when using
	 * SORT_MODE__BRANCH so make a dummy one.
	 */
	if (pt->synth_opts.last_branch && sort__mode == SORT_MODE__BRANCH) {
		dummy_bs = (struct dummy_branch_stack){
			.nr = 1,
			.hw_idx = -1ULL,
			.entries = {
				.from = sample.ip,
				.to = sample.addr,
			},
		};
		sample.branch_stack = (struct branch_stack *)&dummy_bs;
	}

	if (ptq->state->flags & INTEL_PT_SAMPLE_IPC)
		sample.cyc_cnt = ptq->ipc_cyc_cnt - ptq->last_br_cyc_cnt;
	if (sample.cyc_cnt) {
		sample.insn_cnt = ptq->ipc_insn_cnt - ptq->last_br_insn_cnt;
		ptq->last_br_insn_cnt = ptq->ipc_insn_cnt;
		ptq->last_br_cyc_cnt = ptq->ipc_cyc_cnt;
	}

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->branches_sample_type);
}

static void intel_pt_prep_sample(struct intel_pt *pt,
				 struct intel_pt_queue *ptq,
				 union perf_event *event,
				 struct perf_sample *sample)
{
	intel_pt_prep_b_sample(pt, ptq, event, sample);

	if (pt->synth_opts.callchain) {
		thread_stack__sample(ptq->thread, ptq->cpu, ptq->chain,
				     pt->synth_opts.callchain_sz + 1,
				     sample->ip, pt->kernel_start);
		sample->callchain = ptq->chain;
	}

	if (pt->synth_opts.last_branch) {
		thread_stack__br_sample(ptq->thread, ptq->cpu, ptq->last_branch,
					pt->br_stack_sz);
		sample->branch_stack = ptq->last_branch;
	}
}

static int intel_pt_synth_instruction_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->instructions_id;
	sample.stream_id = ptq->pt->instructions_id;
	if (pt->synth_opts.quick)
		sample.period = 1;
	else
		sample.period = ptq->state->tot_insn_cnt - ptq->last_insn_cnt;

	if (ptq->state->flags & INTEL_PT_SAMPLE_IPC)
		sample.cyc_cnt = ptq->ipc_cyc_cnt - ptq->last_in_cyc_cnt;
	if (sample.cyc_cnt) {
		sample.insn_cnt = ptq->ipc_insn_cnt - ptq->last_in_insn_cnt;
		ptq->last_in_insn_cnt = ptq->ipc_insn_cnt;
		ptq->last_in_cyc_cnt = ptq->ipc_cyc_cnt;
	}

	ptq->last_insn_cnt = ptq->state->tot_insn_cnt;

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->instructions_sample_type);
}

static int intel_pt_synth_transaction_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->transactions_id;
	sample.stream_id = ptq->pt->transactions_id;

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->transactions_sample_type);
}

static void intel_pt_prep_p_sample(struct intel_pt *pt,
				   struct intel_pt_queue *ptq,
				   union perf_event *event,
				   struct perf_sample *sample)
{
	intel_pt_prep_sample(pt, ptq, event, sample);

	/*
	 * Zero IP is used to mean "trace start" but that is not the case for
	 * power or PTWRITE events with no IP, so clear the flags.
	 */
	if (!sample->ip)
		sample->flags = 0;
}

static int intel_pt_synth_ptwrite_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct perf_synth_intel_ptwrite raw;

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_p_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->ptwrites_id;
	sample.stream_id = ptq->pt->ptwrites_id;

	raw.flags = 0;
	raw.ip = !!(ptq->state->flags & INTEL_PT_FUP_IP);
	raw.payload = cpu_to_le64(ptq->state->ptw_payload);

	sample.raw_size = perf_synth__raw_size(raw);
	sample.raw_data = perf_synth__raw_data(&raw);

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->ptwrites_sample_type);
}

static int intel_pt_synth_cbr_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct perf_synth_intel_cbr raw;
	u32 flags;

	if (intel_pt_skip_cbr_event(pt))
		return 0;

	ptq->cbr_seen = ptq->state->cbr;

	intel_pt_prep_p_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->cbr_id;
	sample.stream_id = ptq->pt->cbr_id;

	flags = (u16)ptq->state->cbr_payload | (pt->max_non_turbo_ratio << 16);
	raw.flags = cpu_to_le32(flags);
	raw.freq = cpu_to_le32(raw.cbr * pt->cbr2khz);
	raw.reserved3 = 0;

	sample.raw_size = perf_synth__raw_size(raw);
	sample.raw_data = perf_synth__raw_data(&raw);

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->pwr_events_sample_type);
}

static int intel_pt_synth_psb_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct perf_synth_intel_psb raw;

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_p_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->psb_id;
	sample.stream_id = ptq->pt->psb_id;
	sample.flags = 0;

	raw.reserved = 0;
	raw.offset = ptq->state->psb_offset;

	sample.raw_size = perf_synth__raw_size(raw);
	sample.raw_data = perf_synth__raw_data(&raw);

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->pwr_events_sample_type);
}

static int intel_pt_synth_mwait_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct perf_synth_intel_mwait raw;

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_p_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->mwait_id;
	sample.stream_id = ptq->pt->mwait_id;

	raw.reserved = 0;
	raw.payload = cpu_to_le64(ptq->state->mwait_payload);

	sample.raw_size = perf_synth__raw_size(raw);
	sample.raw_data = perf_synth__raw_data(&raw);

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->pwr_events_sample_type);
}

static int intel_pt_synth_pwre_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct perf_synth_intel_pwre raw;

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_p_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->pwre_id;
	sample.stream_id = ptq->pt->pwre_id;

	raw.reserved = 0;
	raw.payload = cpu_to_le64(ptq->state->pwre_payload);

	sample.raw_size = perf_synth__raw_size(raw);
	sample.raw_data = perf_synth__raw_data(&raw);

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->pwr_events_sample_type);
}

static int intel_pt_synth_exstop_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct perf_synth_intel_exstop raw;

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_p_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->exstop_id;
	sample.stream_id = ptq->pt->exstop_id;

	raw.flags = 0;
	raw.ip = !!(ptq->state->flags & INTEL_PT_FUP_IP);

	sample.raw_size = perf_synth__raw_size(raw);
	sample.raw_data = perf_synth__raw_data(&raw);

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->pwr_events_sample_type);
}

static int intel_pt_synth_pwrx_sample(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;
	union perf_event *event = ptq->event_buf;
	struct perf_sample sample = { .ip = 0, };
	struct perf_synth_intel_pwrx raw;

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_p_sample(pt, ptq, event, &sample);

	sample.id = ptq->pt->pwrx_id;
	sample.stream_id = ptq->pt->pwrx_id;

	raw.reserved = 0;
	raw.payload = cpu_to_le64(ptq->state->pwrx_payload);

	sample.raw_size = perf_synth__raw_size(raw);
	sample.raw_data = perf_synth__raw_data(&raw);

	return intel_pt_deliver_synth_event(pt, event, &sample,
					    pt->pwr_events_sample_type);
}

/*
 * PEBS gp_regs array indexes plus 1 so that 0 means not present. Refer
 * intel_pt_add_gp_regs().
 */
static const int pebs_gp_regs[] = {
	[PERF_REG_X86_FLAGS]	= 1,
	[PERF_REG_X86_IP]	= 2,
	[PERF_REG_X86_AX]	= 3,
	[PERF_REG_X86_CX]	= 4,
	[PERF_REG_X86_DX]	= 5,
	[PERF_REG_X86_BX]	= 6,
	[PERF_REG_X86_SP]	= 7,
	[PERF_REG_X86_BP]	= 8,
	[PERF_REG_X86_SI]	= 9,
	[PERF_REG_X86_DI]	= 10,
	[PERF_REG_X86_R8]	= 11,
	[PERF_REG_X86_R9]	= 12,
	[PERF_REG_X86_R10]	= 13,
	[PERF_REG_X86_R11]	= 14,
	[PERF_REG_X86_R12]	= 15,
	[PERF_REG_X86_R13]	= 16,
	[PERF_REG_X86_R14]	= 17,
	[PERF_REG_X86_R15]	= 18,
};

static u64 *intel_pt_add_gp_regs(struct regs_dump *intr_regs, u64 *pos,
				 const struct intel_pt_blk_items *items,
				 u64 regs_mask)
{
	const u64 *gp_regs = items->val[INTEL_PT_GP_REGS_POS];
	u32 mask = items->mask[INTEL_PT_GP_REGS_POS];
	u32 bit;
	int i;

	for (i = 0, bit = 1; i < PERF_REG_X86_64_MAX; i++, bit <<= 1) {
		/* Get the PEBS gp_regs array index */
		int n = pebs_gp_regs[i] - 1;

		if (n < 0)
			continue;
		/*
		 * Add only registers that were requested (i.e. 'regs_mask') and
		 * that were provided (i.e. 'mask'), and update the resulting
		 * mask (i.e. 'intr_regs->mask') accordingly.
		 */
		if (mask & 1 << n && regs_mask & bit) {
			intr_regs->mask |= bit;
			*pos++ = gp_regs[n];
		}
	}

	return pos;
}

#ifndef PERF_REG_X86_XMM0
#define PERF_REG_X86_XMM0 32
#endif

static void intel_pt_add_xmm(struct regs_dump *intr_regs, u64 *pos,
			     const struct intel_pt_blk_items *items,
			     u64 regs_mask)
{
	u32 mask = items->has_xmm & (regs_mask >> PERF_REG_X86_XMM0);
	const u64 *xmm = items->xmm;

	/*
	 * If there are any XMM registers, then there should be all of them.
	 * Nevertheless, follow the logic to add only registers that were
	 * requested (i.e. 'regs_mask') and that were provided (i.e. 'mask'),
	 * and update the resulting mask (i.e. 'intr_regs->mask') accordingly.
	 */
	intr_regs->mask |= (u64)mask << PERF_REG_X86_XMM0;

	for (; mask; mask >>= 1, xmm++) {
		if (mask & 1)
			*pos++ = *xmm;
	}
}

#define LBR_INFO_MISPRED	(1ULL << 63)
#define LBR_INFO_IN_TX		(1ULL << 62)
#define LBR_INFO_ABORT		(1ULL << 61)
#define LBR_INFO_CYCLES		0xffff

/* Refer kernel's intel_pmu_store_pebs_lbrs() */
static u64 intel_pt_lbr_flags(u64 info)
{
	union {
		struct branch_flags flags;
		u64 result;
	} u;

	u.result	  = 0;
	u.flags.mispred	  = !!(info & LBR_INFO_MISPRED);
	u.flags.predicted = !(info & LBR_INFO_MISPRED);
	u.flags.in_tx	  = !!(info & LBR_INFO_IN_TX);
	u.flags.abort	  = !!(info & LBR_INFO_ABORT);
	u.flags.cycles	  = info & LBR_INFO_CYCLES;

	return u.result;
}

static void intel_pt_add_lbrs(struct branch_stack *br_stack,
			      const struct intel_pt_blk_items *items)
{
	u64 *to;
	int i;

	br_stack->nr = 0;

	to = &br_stack->entries[0].from;

	for (i = INTEL_PT_LBR_0_POS; i <= INTEL_PT_LBR_2_POS; i++) {
		u32 mask = items->mask[i];
		const u64 *from = items->val[i];

		for (; mask; mask >>= 3, from += 3) {
			if ((mask & 7) == 7) {
				*to++ = from[0];
				*to++ = from[1];
				*to++ = intel_pt_lbr_flags(from[2]);
				br_stack->nr += 1;
			}
		}
	}
}

static int intel_pt_synth_pebs_sample(struct intel_pt_queue *ptq)
{
	const struct intel_pt_blk_items *items = &ptq->state->items;
	struct perf_sample sample = { .ip = 0, };
	union perf_event *event = ptq->event_buf;
	struct intel_pt *pt = ptq->pt;
	struct evsel *evsel = pt->pebs_evsel;
	u64 sample_type = evsel->core.attr.sample_type;
	u64 id = evsel->core.id[0];
	u8 cpumode;
	u64 regs[8 * sizeof(sample.intr_regs.mask)];

	if (intel_pt_skip_event(pt))
		return 0;

	intel_pt_prep_a_sample(ptq, event, &sample);

	sample.id = id;
	sample.stream_id = id;

	if (!evsel->core.attr.freq)
		sample.period = evsel->core.attr.sample_period;

	/* No support for non-zero CS base */
	if (items->has_ip)
		sample.ip = items->ip;
	else if (items->has_rip)
		sample.ip = items->rip;
	else
		sample.ip = ptq->state->from_ip;

	cpumode = intel_pt_cpumode(ptq, sample.ip, 0);

	event->sample.header.misc = cpumode | PERF_RECORD_MISC_EXACT_IP;

	sample.cpumode = cpumode;

	if (sample_type & PERF_SAMPLE_TIME) {
		u64 timestamp = 0;

		if (items->has_timestamp)
			timestamp = items->timestamp;
		else if (!pt->timeless_decoding)
			timestamp = ptq->timestamp;
		if (timestamp)
			sample.time = tsc_to_perf_time(timestamp, &pt->tc);
	}

	if (sample_type & PERF_SAMPLE_CALLCHAIN &&
	    pt->synth_opts.callchain) {
		thread_stack__sample(ptq->thread, ptq->cpu, ptq->chain,
				     pt->synth_opts.callchain_sz, sample.ip,
				     pt->kernel_start);
		sample.callchain = ptq->chain;
	}

	if (sample_type & PERF_SAMPLE_REGS_INTR &&
	    (items->mask[INTEL_PT_GP_REGS_POS] ||
	     items->mask[INTEL_PT_XMM_POS])) {
		u64 regs_mask = evsel->core.attr.sample_regs_intr;
		u64 *pos;

		sample.intr_regs.abi = items->is_32_bit ?
				       PERF_SAMPLE_REGS_ABI_32 :
				       PERF_SAMPLE_REGS_ABI_64;
		sample.intr_regs.regs = regs;

		pos = intel_pt_add_gp_regs(&sample.intr_regs, regs, items, regs_mask);

		intel_pt_add_xmm(&sample.intr_regs, pos, items, regs_mask);
	}

	if (sample_type & PERF_SAMPLE_BRANCH_STACK) {
		if (items->mask[INTEL_PT_LBR_0_POS] ||
		    items->mask[INTEL_PT_LBR_1_POS] ||
		    items->mask[INTEL_PT_LBR_2_POS]) {
			intel_pt_add_lbrs(ptq->last_branch, items);
		} else if (pt->synth_opts.last_branch) {
			thread_stack__br_sample(ptq->thread, ptq->cpu,
						ptq->last_branch,
						pt->br_stack_sz);
		} else {
			ptq->last_branch->nr = 0;
		}
		sample.branch_stack = ptq->last_branch;
	}

	if (sample_type & PERF_SAMPLE_ADDR && items->has_mem_access_address)
		sample.addr = items->mem_access_address;

	if (sample_type & PERF_SAMPLE_WEIGHT_TYPE) {
		/*
		 * Refer kernel's setup_pebs_adaptive_sample_data() and
		 * intel_hsw_weight().
		 */
		if (items->has_mem_access_latency) {
			u64 weight = items->mem_access_latency >> 32;

			/*
			 * Starts from SPR, the mem access latency field
			 * contains both cache latency [47:32] and instruction
			 * latency [15:0]. The cache latency is the same as the
			 * mem access latency on previous platforms.
			 *
			 * In practice, no memory access could last than 4G
			 * cycles. Use latency >> 32 to distinguish the
			 * different format of the mem access latency field.
			 */
			if (weight > 0) {
				sample.weight = weight & 0xffff;
				sample.ins_lat = items->mem_access_latency & 0xffff;
			} else
				sample.weight = items->mem_access_latency;
		}
		if (!sample.weight && items->has_tsx_aux_info) {
			/* Cycles last block */
			sample.weight = (u32)items->tsx_aux_info;
		}
	}

	if (sample_type & PERF_SAMPLE_TRANSACTION && items->has_tsx_aux_info) {
		u64 ax = items->has_rax ? items->rax : 0;
		/* Refer kernel's intel_hsw_transaction() */
		u64 txn = (u8)(items->tsx_aux_info >> 32);

		/* For RTM XABORTs also log the abort code from AX */
		if (txn & PERF_TXN_TRANSACTION && ax & 1)
			txn |= ((ax >> 24) & 0xff) << PERF_TXN_ABORT_SHIFT;
		sample.transaction = txn;
	}

	return intel_pt_deliver_synth_event(pt, event, &sample, sample_type);
}

static int intel_pt_synth_error(struct intel_pt *pt, int code, int cpu,
				pid_t pid, pid_t tid, u64 ip, u64 timestamp)
{
	union perf_event event;
	char msg[MAX_AUXTRACE_ERROR_MSG];
	int err;

	if (pt->synth_opts.error_minus_flags) {
		if (code == INTEL_PT_ERR_OVR &&
		    pt->synth_opts.error_minus_flags & AUXTRACE_ERR_FLG_OVERFLOW)
			return 0;
		if (code == INTEL_PT_ERR_LOST &&
		    pt->synth_opts.error_minus_flags & AUXTRACE_ERR_FLG_DATA_LOST)
			return 0;
	}

	intel_pt__strerror(code, msg, MAX_AUXTRACE_ERROR_MSG);

	auxtrace_synth_error(&event.auxtrace_error, PERF_AUXTRACE_ERROR_ITRACE,
			     code, cpu, pid, tid, ip, msg, timestamp);

	err = perf_session__deliver_synth_event(pt->session, &event, NULL);
	if (err)
		pr_err("Intel Processor Trace: failed to deliver error event, error %d\n",
		       err);

	return err;
}

static int intel_ptq_synth_error(struct intel_pt_queue *ptq,
				 const struct intel_pt_state *state)
{
	struct intel_pt *pt = ptq->pt;
	u64 tm = ptq->timestamp;

	tm = pt->timeless_decoding ? 0 : tsc_to_perf_time(tm, &pt->tc);

	return intel_pt_synth_error(pt, state->err, ptq->cpu, ptq->pid,
				    ptq->tid, state->from_ip, tm);
}

static int intel_pt_next_tid(struct intel_pt *pt, struct intel_pt_queue *ptq)
{
	struct auxtrace_queue *queue;
	pid_t tid = ptq->next_tid;
	int err;

	if (tid == -1)
		return 0;

	intel_pt_log("switch: cpu %d tid %d\n", ptq->cpu, tid);

	err = machine__set_current_tid(pt->machine, ptq->cpu, -1, tid);

	queue = &pt->queues.queue_array[ptq->queue_nr];
	intel_pt_set_pid_tid_cpu(pt, queue);

	ptq->next_tid = -1;

	return err;
}

static inline bool intel_pt_is_switch_ip(struct intel_pt_queue *ptq, u64 ip)
{
	struct intel_pt *pt = ptq->pt;

	return ip == pt->switch_ip &&
	       (ptq->flags & PERF_IP_FLAG_BRANCH) &&
	       !(ptq->flags & (PERF_IP_FLAG_CONDITIONAL | PERF_IP_FLAG_ASYNC |
			       PERF_IP_FLAG_INTERRUPT | PERF_IP_FLAG_TX_ABORT));
}

#define INTEL_PT_PWR_EVT (INTEL_PT_MWAIT_OP | INTEL_PT_PWR_ENTRY | \
			  INTEL_PT_EX_STOP | INTEL_PT_PWR_EXIT)

static int intel_pt_sample(struct intel_pt_queue *ptq)
{
	const struct intel_pt_state *state = ptq->state;
	struct intel_pt *pt = ptq->pt;
	int err;

	if (!ptq->have_sample)
		return 0;

	ptq->have_sample = false;

	ptq->ipc_insn_cnt = ptq->state->tot_insn_cnt;
	ptq->ipc_cyc_cnt = ptq->state->tot_cyc_cnt;

	/*
	 * Do PEBS first to allow for the possibility that the PEBS timestamp
	 * precedes the current timestamp.
	 */
	if (pt->sample_pebs && state->type & INTEL_PT_BLK_ITEMS) {
		err = intel_pt_synth_pebs_sample(ptq);
		if (err)
			return err;
	}

	if (pt->sample_pwr_events) {
		if (state->type & INTEL_PT_PSB_EVT) {
			err = intel_pt_synth_psb_sample(ptq);
			if (err)
				return err;
		}
		if (ptq->state->cbr != ptq->cbr_seen) {
			err = intel_pt_synth_cbr_sample(ptq);
			if (err)
				return err;
		}
		if (state->type & INTEL_PT_PWR_EVT) {
			if (state->type & INTEL_PT_MWAIT_OP) {
				err = intel_pt_synth_mwait_sample(ptq);
				if (err)
					return err;
			}
			if (state->type & INTEL_PT_PWR_ENTRY) {
				err = intel_pt_synth_pwre_sample(ptq);
				if (err)
					return err;
			}
			if (state->type & INTEL_PT_EX_STOP) {
				err = intel_pt_synth_exstop_sample(ptq);
				if (err)
					return err;
			}
			if (state->type & INTEL_PT_PWR_EXIT) {
				err = intel_pt_synth_pwrx_sample(ptq);
				if (err)
					return err;
			}
		}
	}

	if (pt->sample_instructions && (state->type & INTEL_PT_INSTRUCTION)) {
		err = intel_pt_synth_instruction_sample(ptq);
		if (err)
			return err;
	}

	if (pt->sample_transactions && (state->type & INTEL_PT_TRANSACTION)) {
		err = intel_pt_synth_transaction_sample(ptq);
		if (err)
			return err;
	}

	if (pt->sample_ptwrites && (state->type & INTEL_PT_PTW)) {
		err = intel_pt_synth_ptwrite_sample(ptq);
		if (err)
			return err;
	}

	if (!(state->type & INTEL_PT_BRANCH))
		return 0;

	if (pt->use_thread_stack) {
		thread_stack__event(ptq->thread, ptq->cpu, ptq->flags,
				    state->from_ip, state->to_ip, ptq->insn_len,
				    state->trace_nr, pt->callstack,
				    pt->br_stack_sz_plus,
				    pt->mispred_all);
	} else {
		thread_stack__set_trace_nr(ptq->thread, ptq->cpu, state->trace_nr);
	}

	if (pt->sample_branches) {
		if (state->from_nr != state->to_nr &&
		    state->from_ip && state->to_ip) {
			struct intel_pt_state *st = (struct intel_pt_state *)state;
			u64 to_ip = st->to_ip;
			u64 from_ip = st->from_ip;

			/*
			 * perf cannot handle having different machines for ip
			 * and addr, so create 2 branches.
			 */
			st->to_ip = 0;
			err = intel_pt_synth_branch_sample(ptq);
			if (err)
				return err;
			st->from_ip = 0;
			st->to_ip = to_ip;
			err = intel_pt_synth_branch_sample(ptq);
			st->from_ip = from_ip;
		} else {
			err = intel_pt_synth_branch_sample(ptq);
		}
		if (err)
			return err;
	}

	if (!ptq->sync_switch)
		return 0;

	if (intel_pt_is_switch_ip(ptq, state->to_ip)) {
		switch (ptq->switch_state) {
		case INTEL_PT_SS_NOT_TRACING:
		case INTEL_PT_SS_UNKNOWN:
		case INTEL_PT_SS_EXPECTING_SWITCH_IP:
			err = intel_pt_next_tid(pt, ptq);
			if (err)
				return err;
			ptq->switch_state = INTEL_PT_SS_TRACING;
			break;
		default:
			ptq->switch_state = INTEL_PT_SS_EXPECTING_SWITCH_EVENT;
			return 1;
		}
	} else if (!state->to_ip) {
		ptq->switch_state = INTEL_PT_SS_NOT_TRACING;
	} else if (ptq->switch_state == INTEL_PT_SS_NOT_TRACING) {
		ptq->switch_state = INTEL_PT_SS_UNKNOWN;
	} else if (ptq->switch_state == INTEL_PT_SS_UNKNOWN &&
		   state->to_ip == pt->ptss_ip &&
		   (ptq->flags & PERF_IP_FLAG_CALL)) {
		ptq->switch_state = INTEL_PT_SS_TRACING;
	}

	return 0;
}

static u64 intel_pt_switch_ip(struct intel_pt *pt, u64 *ptss_ip)
{
	struct machine *machine = pt->machine;
	struct map *map;
	struct symbol *sym, *start;
	u64 ip, switch_ip = 0;
	const char *ptss;

	if (ptss_ip)
		*ptss_ip = 0;

	map = machine__kernel_map(machine);
	if (!map)
		return 0;

	if (map__load(map))
		return 0;

	start = dso__first_symbol(map->dso);

	for (sym = start; sym; sym = dso__next_symbol(sym)) {
		if (sym->binding == STB_GLOBAL &&
		    !strcmp(sym->name, "__switch_to")) {
			ip = map->unmap_ip(map, sym->start);
			if (ip >= map->start && ip < map->end) {
				switch_ip = ip;
				break;
			}
		}
	}

	if (!switch_ip || !ptss_ip)
		return 0;

	if (pt->have_sched_switch == 1)
		ptss = "perf_trace_sched_switch";
	else
		ptss = "__perf_event_task_sched_out";

	for (sym = start; sym; sym = dso__next_symbol(sym)) {
		if (!strcmp(sym->name, ptss)) {
			ip = map->unmap_ip(map, sym->start);
			if (ip >= map->start && ip < map->end) {
				*ptss_ip = ip;
				break;
			}
		}
	}

	return switch_ip;
}

static void intel_pt_enable_sync_switch(struct intel_pt *pt)
{
	unsigned int i;

	pt->sync_switch = true;

	for (i = 0; i < pt->queues.nr_queues; i++) {
		struct auxtrace_queue *queue = &pt->queues.queue_array[i];
		struct intel_pt_queue *ptq = queue->priv;

		if (ptq)
			ptq->sync_switch = true;
	}
}

/*
 * To filter against time ranges, it is only necessary to look at the next start
 * or end time.
 */
static bool intel_pt_next_time(struct intel_pt_queue *ptq)
{
	struct intel_pt *pt = ptq->pt;

	if (ptq->sel_start) {
		/* Next time is an end time */
		ptq->sel_start = false;
		ptq->sel_timestamp = pt->time_ranges[ptq->sel_idx].end;
		return true;
	} else if (ptq->sel_idx + 1 < pt->range_cnt) {
		/* Next time is a start time */
		ptq->sel_start = true;
		ptq->sel_idx += 1;
		ptq->sel_timestamp = pt->time_ranges[ptq->sel_idx].start;
		return true;
	}

	/* No next time */
	return false;
}

static int intel_pt_time_filter(struct intel_pt_queue *ptq, u64 *ff_timestamp)
{
	int err;

	while (1) {
		if (ptq->sel_start) {
			if (ptq->timestamp >= ptq->sel_timestamp) {
				/* After start time, so consider next time */
				intel_pt_next_time(ptq);
				if (!ptq->sel_timestamp) {
					/* No end time */
					return 0;
				}
				/* Check against end time */
				continue;
			}
			/* Before start time, so fast forward */
			ptq->have_sample = false;
			if (ptq->sel_timestamp > *ff_timestamp) {
				if (ptq->sync_switch) {
					intel_pt_next_tid(ptq->pt, ptq);
					ptq->switch_state = INTEL_PT_SS_UNKNOWN;
				}
				*ff_timestamp = ptq->sel_timestamp;
				err = intel_pt_fast_forward(ptq->decoder,
							    ptq->sel_timestamp);
				if (err)
					return err;
			}
			return 0;
		} else if (ptq->timestamp > ptq->sel_timestamp) {
			/* After end time, so consider next time */
			if (!intel_pt_next_time(ptq)) {
				/* No next time range, so stop decoding */
				ptq->have_sample = false;
				ptq->switch_state = INTEL_PT_SS_NOT_TRACING;
				return 1;
			}
			/* Check against next start time */
			continue;
		} else {
			/* Before end time */
			return 0;
		}
	}
}

static int intel_pt_run_decoder(struct intel_pt_queue *ptq, u64 *timestamp)
{
	const struct intel_pt_state *state = ptq->state;
	struct intel_pt *pt = ptq->pt;
	u64 ff_timestamp = 0;
	int err;

	if (!pt->kernel_start) {
		pt->kernel_start = machine__kernel_start(pt->machine);
		if (pt->per_cpu_mmaps &&
		    (pt->have_sched_switch == 1 || pt->have_sched_switch == 3) &&
		    !pt->timeless_decoding && intel_pt_tracing_kernel(pt) &&
		    !pt->sampling_mode) {
			pt->switch_ip = intel_pt_switch_ip(pt, &pt->ptss_ip);
			if (pt->switch_ip) {
				intel_pt_log("switch_ip: %"PRIx64" ptss_ip: %"PRIx64"\n",
					     pt->switch_ip, pt->ptss_ip);
				intel_pt_enable_sync_switch(pt);
			}
		}
	}

	intel_pt_log("queue %u decoding cpu %d pid %d tid %d\n",
		     ptq->queue_nr, ptq->cpu, ptq->pid, ptq->tid);
	while (1) {
		err = intel_pt_sample(ptq);
		if (err)
			return err;

		state = intel_pt_decode(ptq->decoder);
		if (state->err) {
			if (state->err == INTEL_PT_ERR_NODATA)
				return 1;
			if (ptq->sync_switch &&
			    state->from_ip >= pt->kernel_start) {
				ptq->sync_switch = false;
				intel_pt_next_tid(pt, ptq);
			}
			if (pt->synth_opts.errors) {
				err = intel_ptq_synth_error(ptq, state);
				if (err)
					return err;
			}
			continue;
		}

		ptq->state = state;
		ptq->have_sample = true;
		intel_pt_sample_flags(ptq);

		/* Use estimated TSC upon return to user space */
		if (pt->est_tsc &&
		    (state->from_ip >= pt->kernel_start || !state->from_ip) &&
		    state->to_ip && state->to_ip < pt->kernel_start) {
			intel_pt_log("TSC %"PRIx64" est. TSC %"PRIx64"\n",
				     state->timestamp, state->est_timestamp);
			ptq->timestamp = state->est_timestamp;
		/* Use estimated TSC in unknown switch state */
		} else if (ptq->sync_switch &&
			   ptq->switch_state == INTEL_PT_SS_UNKNOWN &&
			   intel_pt_is_switch_ip(ptq, state->to_ip) &&
			   ptq->next_tid == -1) {
			intel_pt_log("TSC %"PRIx64" est. TSC %"PRIx64"\n",
				     state->timestamp, state->est_timestamp);
			ptq->timestamp = state->est_timestamp;
		} else if (state->timestamp > ptq->timestamp) {
			ptq->timestamp = state->timestamp;
		}

		if (ptq->sel_timestamp) {
			err = intel_pt_time_filter(ptq, &ff_timestamp);
			if (err)
				return err;
		}

		if (!pt->timeless_decoding && ptq->timestamp >= *timestamp) {
			*timestamp = ptq->timestamp;
			return 0;
		}
	}
	return 0;
}

static inline int intel_pt_update_queues(struct intel_pt *pt)
{
	if (pt->queues.new_data) {
		pt->queues.new_data = false;
		return intel_pt_setup_queues(pt);
	}
	return 0;
}

static int intel_pt_process_queues(struct intel_pt *pt, u64 timestamp)
{
	unsigned int queue_nr;
	u64 ts;
	int ret;

	while (1) {
		struct auxtrace_queue *queue;
		struct intel_pt_queue *ptq;

		if (!pt->heap.heap_cnt)
			return 0;

		if (pt->heap.heap_array[0].ordinal >= timestamp)
			return 0;

		queue_nr = pt->heap.heap_array[0].queue_nr;
		queue = &pt->queues.queue_array[queue_nr];
		ptq = queue->priv;

		intel_pt_log("queue %u processing 0x%" PRIx64 " to 0x%" PRIx64 "\n",
			     queue_nr, pt->heap.heap_array[0].ordinal,
			     timestamp);

		auxtrace_heap__pop(&pt->heap);

		if (pt->heap.heap_cnt) {
			ts = pt->heap.heap_array[0].ordinal + 1;
			if (ts > timestamp)
				ts = timestamp;
		} else {
			ts = timestamp;
		}

		intel_pt_set_pid_tid_cpu(pt, queue);

		ret = intel_pt_run_decoder(ptq, &ts);

		if (ret < 0) {
			auxtrace_heap__add(&pt->heap, queue_nr, ts);
			return ret;
		}

		if (!ret) {
			ret = auxtrace_heap__add(&pt->heap, queue_nr, ts);
			if (ret < 0)
				return ret;
		} else {
			ptq->on_heap = false;
		}
	}

	return 0;
}

static int intel_pt_process_timeless_queues(struct intel_pt *pt, pid_t tid,
					    u64 time_)
{
	struct auxtrace_queues *queues = &pt->queues;
	unsigned int i;
	u64 ts = 0;

	for (i = 0; i < queues->nr_queues; i++) {
		struct auxtrace_queue *queue = &pt->queues.queue_array[i];
		struct intel_pt_queue *ptq = queue->priv;

		if (ptq && (tid == -1 || ptq->tid == tid)) {
			ptq->time = time_;
			intel_pt_set_pid_tid_cpu(pt, queue);
			intel_pt_run_decoder(ptq, &ts);
		}
	}
	return 0;
}

static void intel_pt_sample_set_pid_tid_cpu(struct intel_pt_queue *ptq,
					    struct auxtrace_queue *queue,
					    struct perf_sample *sample)
{
	struct machine *m = ptq->pt->machine;

	ptq->pid = sample->pid;
	ptq->tid = sample->tid;
	ptq->cpu = queue->cpu;

	intel_pt_log("queue %u cpu %d pid %d tid %d\n",
		     ptq->queue_nr, ptq->cpu, ptq->pid, ptq->tid);

	thread__zput(ptq->thread);

	if (ptq->tid == -1)
		return;

	if (ptq->pid == -1) {
		ptq->thread = machine__find_thread(m, -1, ptq->tid);
		if (ptq->thread)
			ptq->pid = ptq->thread->pid_;
		return;
	}

	ptq->thread = machine__findnew_thread(m, ptq->pid, ptq->tid);
}

static int intel_pt_process_timeless_sample(struct intel_pt *pt,
					    struct perf_sample *sample)
{
	struct auxtrace_queue *queue;
	struct intel_pt_queue *ptq;
	u64 ts = 0;

	queue = auxtrace_queues__sample_queue(&pt->queues, sample, pt->session);
	if (!queue)
		return -EINVAL;

	ptq = queue->priv;
	if (!ptq)
		return 0;

	ptq->stop = false;
	ptq->time = sample->time;
	intel_pt_sample_set_pid_tid_cpu(ptq, queue, sample);
	intel_pt_run_decoder(ptq, &ts);
	return 0;
}

static int intel_pt_lost(struct intel_pt *pt, struct perf_sample *sample)
{
	return intel_pt_synth_error(pt, INTEL_PT_ERR_LOST, sample->cpu,
				    sample->pid, sample->tid, 0, sample->time);
}

static struct intel_pt_queue *intel_pt_cpu_to_ptq(struct intel_pt *pt, int cpu)
{
	unsigned i, j;

	if (cpu < 0 || !pt->queues.nr_queues)
		return NULL;

	if ((unsigned)cpu >= pt->queues.nr_queues)
		i = pt->queues.nr_queues - 1;
	else
		i = cpu;

	if (pt->queues.queue_array[i].cpu == cpu)
		return pt->queues.queue_array[i].priv;

	for (j = 0; i > 0; j++) {
		if (pt->queues.queue_array[--i].cpu == cpu)
			return pt->queues.queue_array[i].priv;
	}

	for (; j < pt->queues.nr_queues; j++) {
		if (pt->queues.queue_array[j].cpu == cpu)
			return pt->queues.queue_array[j].priv;
	}

	return NULL;
}

static int intel_pt_sync_switch(struct intel_pt *pt, int cpu, pid_t tid,
				u64 timestamp)
{
	struct intel_pt_queue *ptq;
	int err;

	if (!pt->sync_switch)
		return 1;

	ptq = intel_pt_cpu_to_ptq(pt, cpu);
	if (!ptq || !ptq->sync_switch)
		return 1;

	switch (ptq->switch_state) {
	case INTEL_PT_SS_NOT_TRACING:
		break;
	case INTEL_PT_SS_UNKNOWN:
	case INTEL_PT_SS_TRACING:
		ptq->next_tid = tid;
		ptq->switch_state = INTEL_PT_SS_EXPECTING_SWITCH_IP;
		return 0;
	case INTEL_PT_SS_EXPECTING_SWITCH_EVENT:
		if (!ptq->on_heap) {
			ptq->timestamp = perf_time_to_tsc(timestamp,
							  &pt->tc);
			err = auxtrace_heap__add(&pt->heap, ptq->queue_nr,
						 ptq->timestamp);
			if (err)
				return err;
			ptq->on_heap = true;
		}
		ptq->switch_state = INTEL_PT_SS_TRACING;
		break;
	case INTEL_PT_SS_EXPECTING_SWITCH_IP:
		intel_pt_log("ERROR: cpu %d expecting switch ip\n", cpu);
		break;
	default:
		break;
	}

	ptq->next_tid = -1;

	return 1;
}

static int intel_pt_process_switch(struct intel_pt *pt,
				   struct perf_sample *sample)
{
	pid_t tid;
	int cpu, ret;
	struct evsel *evsel = evlist__id2evsel(pt->session->evlist, sample->id);

	if (evsel != pt->switch_evsel)
		return 0;

	tid = evsel__intval(evsel, sample, "next_pid");
	cpu = sample->cpu;

	intel_pt_log("sched_switch: cpu %d tid %d time %"PRIu64" tsc %#"PRIx64"\n",
		     cpu, tid, sample->time, perf_time_to_tsc(sample->time,
		     &pt->tc));

	ret = intel_pt_sync_switch(pt, cpu, tid, sample->time);
	if (ret <= 0)
		return ret;

	return machine__set_current_tid(pt->machine, cpu, -1, tid);
}

static int intel_pt_context_switch_in(struct intel_pt *pt,
				      struct perf_sample *sample)
{
	pid_t pid = sample->pid;
	pid_t tid = sample->tid;
	int cpu = sample->cpu;

	if (pt->sync_switch) {
		struct intel_pt_queue *ptq;

		ptq = intel_pt_cpu_to_ptq(pt, cpu);
		if (ptq && ptq->sync_switch) {
			ptq->next_tid = -1;
			switch (ptq->switch_state) {
			case INTEL_PT_SS_NOT_TRACING:
			case INTEL_PT_SS_UNKNOWN:
			case INTEL_PT_SS_TRACING:
				break;
			case INTEL_PT_SS_EXPECTING_SWITCH_EVENT:
			case INTEL_PT_SS_EXPECTING_SWITCH_IP:
				ptq->switch_state = INTEL_PT_SS_TRACING;
				break;
			default:
				break;
			}
		}
	}

	/*
	 * If the current tid has not been updated yet, ensure it is now that
	 * a "switch in" event has occurred.
	 */
	if (machine__get_current_tid(pt->machine, cpu) == tid)
		return 0;

	return machine__set_current_tid(pt->machine, cpu, pid, tid);
}

static int intel_pt_context_switch(struct intel_pt *pt, union perf_event *event,
				   struct perf_sample *sample)
{
	bool out = event->header.misc & PERF_RECORD_MISC_SWITCH_OUT;
	pid_t pid, tid;
	int cpu, ret;

	cpu = sample->cpu;

	if (pt->have_sched_switch == 3) {
		if (!out)
			return intel_pt_context_switch_in(pt, sample);
		if (event->header.type != PERF_RECORD_SWITCH_CPU_WIDE) {
			pr_err("Expecting CPU-wide context switch event\n");
			return -EINVAL;
		}
		pid = event->context_switch.next_prev_pid;
		tid = event->context_switch.next_prev_tid;
	} else {
		if (out)
			return 0;
		pid = sample->pid;
		tid = sample->tid;
	}

	if (tid == -1)
		intel_pt_log("context_switch event has no tid\n");

	ret = intel_pt_sync_switch(pt, cpu, tid, sample->time);
	if (ret <= 0)
		return ret;

	return machine__set_current_tid(pt->machine, cpu, pid, tid);
}

static int intel_pt_process_itrace_start(struct intel_pt *pt,
					 union perf_event *event,
					 struct perf_sample *sample)
{
	if (!pt->per_cpu_mmaps)
		return 0;

	intel_pt_log("itrace_start: cpu %d pid %d tid %d time %"PRIu64" tsc %#"PRIx64"\n",
		     sample->cpu, event->itrace_start.pid,
		     event->itrace_start.tid, sample->time,
		     perf_time_to_tsc(sample->time, &pt->tc));

	return machine__set_current_tid(pt->machine, sample->cpu,
					event->itrace_start.pid,
					event->itrace_start.tid);
}

static int intel_pt_find_map(struct thread *thread, u8 cpumode, u64 addr,
			     struct addr_location *al)
{
	if (!al->map || addr < al->map->start || addr >= al->map->end) {
		if (!thread__find_map(thread, cpumode, addr, al))
			return -1;
	}

	return 0;
}

/* Invalidate all instruction cache entries that overlap the text poke */
static int intel_pt_text_poke(struct intel_pt *pt, union perf_event *event)
{
	u8 cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	u64 addr = event->text_poke.addr + event->text_poke.new_len - 1;
	/* Assume text poke begins in a basic block no more than 4096 bytes */
	int cnt = 4096 + event->text_poke.new_len;
	struct thread *thread = pt->unknown_thread;
	struct addr_location al = { .map = NULL };
	struct machine *machine = pt->machine;
	struct intel_pt_cache_entry *e;
	u64 offset;

	if (!event->text_poke.new_len)
		return 0;

	for (; cnt; cnt--, addr--) {
		if (intel_pt_find_map(thread, cpumode, addr, &al)) {
			if (addr < event->text_poke.addr)
				return 0;
			continue;
		}

		if (!al.map->dso || !al.map->dso->auxtrace_cache)
			continue;

		offset = al.map->map_ip(al.map, addr);

		e = intel_pt_cache_lookup(al.map->dso, machine, offset);
		if (!e)
			continue;

		if (addr + e->byte_cnt + e->length <= event->text_poke.addr) {
			/*
			 * No overlap. Working backwards there cannot be another
			 * basic block that overlaps the text poke if there is a
			 * branch instruction before the text poke address.
			 */
			if (e->branch != INTEL_PT_BR_NO_BRANCH)
				return 0;
		} else {
			intel_pt_cache_invalidate(al.map->dso, machine, offset);
			intel_pt_log("Invalidated instruction cache for %s at %#"PRIx64"\n",
				     al.map->dso->long_name, addr);
		}
	}

	return 0;
}

static int intel_pt_process_event(struct perf_session *session,
				  union perf_event *event,
				  struct perf_sample *sample,
				  struct perf_tool *tool)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);
	u64 timestamp;
	int err = 0;

	if (dump_trace)
		return 0;

	if (!tool->ordered_events) {
		pr_err("Intel Processor Trace requires ordered events\n");
		return -EINVAL;
	}

	if (sample->time && sample->time != (u64)-1)
		timestamp = perf_time_to_tsc(sample->time, &pt->tc);
	else
		timestamp = 0;

	if (timestamp || pt->timeless_decoding) {
		err = intel_pt_update_queues(pt);
		if (err)
			return err;
	}

	if (pt->timeless_decoding) {
		if (pt->sampling_mode) {
			if (sample->aux_sample.size)
				err = intel_pt_process_timeless_sample(pt,
								       sample);
		} else if (event->header.type == PERF_RECORD_EXIT) {
			err = intel_pt_process_timeless_queues(pt,
							       event->fork.tid,
							       sample->time);
		}
	} else if (timestamp) {
		err = intel_pt_process_queues(pt, timestamp);
	}
	if (err)
		return err;

	if (event->header.type == PERF_RECORD_SAMPLE) {
		if (pt->synth_opts.add_callchain && !sample->callchain)
			intel_pt_add_callchain(pt, sample);
		if (pt->synth_opts.add_last_branch && !sample->branch_stack)
			intel_pt_add_br_stack(pt, sample);
	}

	if (event->header.type == PERF_RECORD_AUX &&
	    (event->aux.flags & PERF_AUX_FLAG_TRUNCATED) &&
	    pt->synth_opts.errors) {
		err = intel_pt_lost(pt, sample);
		if (err)
			return err;
	}

	if (pt->switch_evsel && event->header.type == PERF_RECORD_SAMPLE)
		err = intel_pt_process_switch(pt, sample);
	else if (event->header.type == PERF_RECORD_ITRACE_START)
		err = intel_pt_process_itrace_start(pt, event, sample);
	else if (event->header.type == PERF_RECORD_SWITCH ||
		 event->header.type == PERF_RECORD_SWITCH_CPU_WIDE)
		err = intel_pt_context_switch(pt, event, sample);

	if (!err && event->header.type == PERF_RECORD_TEXT_POKE)
		err = intel_pt_text_poke(pt, event);

	if (intel_pt_enable_logging && intel_pt_log_events(pt, sample->time)) {
		intel_pt_log("event %u: cpu %d time %"PRIu64" tsc %#"PRIx64" ",
			     event->header.type, sample->cpu, sample->time, timestamp);
		intel_pt_log_event(event);
	}

	return err;
}

static int intel_pt_flush(struct perf_session *session, struct perf_tool *tool)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);
	int ret;

	if (dump_trace)
		return 0;

	if (!tool->ordered_events)
		return -EINVAL;

	ret = intel_pt_update_queues(pt);
	if (ret < 0)
		return ret;

	if (pt->timeless_decoding)
		return intel_pt_process_timeless_queues(pt, -1,
							MAX_TIMESTAMP - 1);

	return intel_pt_process_queues(pt, MAX_TIMESTAMP);
}

static void intel_pt_free_events(struct perf_session *session)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);
	struct auxtrace_queues *queues = &pt->queues;
	unsigned int i;

	for (i = 0; i < queues->nr_queues; i++) {
		intel_pt_free_queue(queues->queue_array[i].priv);
		queues->queue_array[i].priv = NULL;
	}
	intel_pt_log_disable();
	auxtrace_queues__free(queues);
}

static void intel_pt_free(struct perf_session *session)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);

	auxtrace_heap__free(&pt->heap);
	intel_pt_free_events(session);
	session->auxtrace = NULL;
	thread__put(pt->unknown_thread);
	addr_filters__exit(&pt->filts);
	zfree(&pt->chain);
	zfree(&pt->filter);
	zfree(&pt->time_ranges);
	free(pt);
}

static bool intel_pt_evsel_is_auxtrace(struct perf_session *session,
				       struct evsel *evsel)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);

	return evsel->core.attr.type == pt->pmu_type;
}

static int intel_pt_process_auxtrace_event(struct perf_session *session,
					   union perf_event *event,
					   struct perf_tool *tool __maybe_unused)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);

	if (!pt->data_queued) {
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

		err = auxtrace_queues__add_event(&pt->queues, session, event,
						 data_offset, &buffer);
		if (err)
			return err;

		/* Dump here now we have copied a piped trace out of the pipe */
		if (dump_trace) {
			if (auxtrace_buffer__get_data(buffer, fd)) {
				intel_pt_dump_event(pt, buffer->data,
						    buffer->size);
				auxtrace_buffer__put_data(buffer);
			}
		}
	}

	return 0;
}

static int intel_pt_queue_data(struct perf_session *session,
			       struct perf_sample *sample,
			       union perf_event *event, u64 data_offset)
{
	struct intel_pt *pt = container_of(session->auxtrace, struct intel_pt,
					   auxtrace);
	u64 timestamp;

	if (event) {
		return auxtrace_queues__add_event(&pt->queues, session, event,
						  data_offset, NULL);
	}

	if (sample->time && sample->time != (u64)-1)
		timestamp = perf_time_to_tsc(sample->time, &pt->tc);
	else
		timestamp = 0;

	return auxtrace_queues__add_sample(&pt->queues, session, sample,
					   data_offset, timestamp);
}

struct intel_pt_synth {
	struct perf_tool dummy_tool;
	struct perf_session *session;
};

static int intel_pt_event_synth(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample __maybe_unused,
				struct machine *machine __maybe_unused)
{
	struct intel_pt_synth *intel_pt_synth =
			container_of(tool, struct intel_pt_synth, dummy_tool);

	return perf_session__deliver_synth_event(intel_pt_synth->session, event,
						 NULL);
}

static int intel_pt_synth_event(struct perf_session *session, const char *name,
				struct perf_event_attr *attr, u64 id)
{
	struct intel_pt_synth intel_pt_synth;
	int err;

	pr_debug("Synthesizing '%s' event with id %" PRIu64 " sample type %#" PRIx64 "\n",
		 name, id, (u64)attr->sample_type);

	memset(&intel_pt_synth, 0, sizeof(struct intel_pt_synth));
	intel_pt_synth.session = session;

	err = perf_event__synthesize_attr(&intel_pt_synth.dummy_tool, attr, 1,
					  &id, intel_pt_event_synth);
	if (err)
		pr_err("%s: failed to synthesize '%s' event type\n",
		       __func__, name);

	return err;
}

static void intel_pt_set_event_name(struct evlist *evlist, u64 id,
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

static struct evsel *intel_pt_evsel(struct intel_pt *pt,
					 struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.type == pt->pmu_type && evsel->core.ids)
			return evsel;
	}

	return NULL;
}

static int intel_pt_synth_events(struct intel_pt *pt,
				 struct perf_session *session)
{
	struct evlist *evlist = session->evlist;
	struct evsel *evsel = intel_pt_evsel(pt, evlist);
	struct perf_event_attr attr;
	u64 id;
	int err;

	if (!evsel) {
		pr_debug("There are no selected events with Intel Processor Trace data\n");
		return 0;
	}

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.size = sizeof(struct perf_event_attr);
	attr.type = PERF_TYPE_HARDWARE;
	attr.sample_type = evsel->core.attr.sample_type & PERF_SAMPLE_MASK;
	attr.sample_type |= PERF_SAMPLE_IP | PERF_SAMPLE_TID |
			    PERF_SAMPLE_PERIOD;
	if (pt->timeless_decoding)
		attr.sample_type &= ~(u64)PERF_SAMPLE_TIME;
	else
		attr.sample_type |= PERF_SAMPLE_TIME;
	if (!pt->per_cpu_mmaps)
		attr.sample_type &= ~(u64)PERF_SAMPLE_CPU;
	attr.exclude_user = evsel->core.attr.exclude_user;
	attr.exclude_kernel = evsel->core.attr.exclude_kernel;
	attr.exclude_hv = evsel->core.attr.exclude_hv;
	attr.exclude_host = evsel->core.attr.exclude_host;
	attr.exclude_guest = evsel->core.attr.exclude_guest;
	attr.sample_id_all = evsel->core.attr.sample_id_all;
	attr.read_format = evsel->core.attr.read_format;

	id = evsel->core.id[0] + 1000000000;
	if (!id)
		id = 1;

	if (pt->synth_opts.branches) {
		attr.config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS;
		attr.sample_period = 1;
		attr.sample_type |= PERF_SAMPLE_ADDR;
		err = intel_pt_synth_event(session, "branches", &attr, id);
		if (err)
			return err;
		pt->sample_branches = true;
		pt->branches_sample_type = attr.sample_type;
		pt->branches_id = id;
		id += 1;
		attr.sample_type &= ~(u64)PERF_SAMPLE_ADDR;
	}

	if (pt->synth_opts.callchain)
		attr.sample_type |= PERF_SAMPLE_CALLCHAIN;
	if (pt->synth_opts.last_branch) {
		attr.sample_type |= PERF_SAMPLE_BRANCH_STACK;
		/*
		 * We don't use the hardware index, but the sample generation
		 * code uses the new format branch_stack with this field,
		 * so the event attributes must indicate that it's present.
		 */
		attr.branch_sample_type |= PERF_SAMPLE_BRANCH_HW_INDEX;
	}

	if (pt->synth_opts.instructions) {
		attr.config = PERF_COUNT_HW_INSTRUCTIONS;
		if (pt->synth_opts.period_type == PERF_ITRACE_PERIOD_NANOSECS)
			attr.sample_period =
				intel_pt_ns_to_ticks(pt, pt->synth_opts.period);
		else
			attr.sample_period = pt->synth_opts.period;
		err = intel_pt_synth_event(session, "instructions", &attr, id);
		if (err)
			return err;
		pt->sample_instructions = true;
		pt->instructions_sample_type = attr.sample_type;
		pt->instructions_id = id;
		id += 1;
	}

	attr.sample_type &= ~(u64)PERF_SAMPLE_PERIOD;
	attr.sample_period = 1;

	if (pt->synth_opts.transactions) {
		attr.config = PERF_COUNT_HW_INSTRUCTIONS;
		err = intel_pt_synth_event(session, "transactions", &attr, id);
		if (err)
			return err;
		pt->sample_transactions = true;
		pt->transactions_sample_type = attr.sample_type;
		pt->transactions_id = id;
		intel_pt_set_event_name(evlist, id, "transactions");
		id += 1;
	}

	attr.type = PERF_TYPE_SYNTH;
	attr.sample_type |= PERF_SAMPLE_RAW;

	if (pt->synth_opts.ptwrites) {
		attr.config = PERF_SYNTH_INTEL_PTWRITE;
		err = intel_pt_synth_event(session, "ptwrite", &attr, id);
		if (err)
			return err;
		pt->sample_ptwrites = true;
		pt->ptwrites_sample_type = attr.sample_type;
		pt->ptwrites_id = id;
		intel_pt_set_event_name(evlist, id, "ptwrite");
		id += 1;
	}

	if (pt->synth_opts.pwr_events) {
		pt->sample_pwr_events = true;
		pt->pwr_events_sample_type = attr.sample_type;

		attr.config = PERF_SYNTH_INTEL_CBR;
		err = intel_pt_synth_event(session, "cbr", &attr, id);
		if (err)
			return err;
		pt->cbr_id = id;
		intel_pt_set_event_name(evlist, id, "cbr");
		id += 1;

		attr.config = PERF_SYNTH_INTEL_PSB;
		err = intel_pt_synth_event(session, "psb", &attr, id);
		if (err)
			return err;
		pt->psb_id = id;
		intel_pt_set_event_name(evlist, id, "psb");
		id += 1;
	}

	if (pt->synth_opts.pwr_events && (evsel->core.attr.config & 0x10)) {
		attr.config = PERF_SYNTH_INTEL_MWAIT;
		err = intel_pt_synth_event(session, "mwait", &attr, id);
		if (err)
			return err;
		pt->mwait_id = id;
		intel_pt_set_event_name(evlist, id, "mwait");
		id += 1;

		attr.config = PERF_SYNTH_INTEL_PWRE;
		err = intel_pt_synth_event(session, "pwre", &attr, id);
		if (err)
			return err;
		pt->pwre_id = id;
		intel_pt_set_event_name(evlist, id, "pwre");
		id += 1;

		attr.config = PERF_SYNTH_INTEL_EXSTOP;
		err = intel_pt_synth_event(session, "exstop", &attr, id);
		if (err)
			return err;
		pt->exstop_id = id;
		intel_pt_set_event_name(evlist, id, "exstop");
		id += 1;

		attr.config = PERF_SYNTH_INTEL_PWRX;
		err = intel_pt_synth_event(session, "pwrx", &attr, id);
		if (err)
			return err;
		pt->pwrx_id = id;
		intel_pt_set_event_name(evlist, id, "pwrx");
		id += 1;
	}

	return 0;
}

static void intel_pt_setup_pebs_events(struct intel_pt *pt)
{
	struct evsel *evsel;

	if (!pt->synth_opts.other_events)
		return;

	evlist__for_each_entry(pt->session->evlist, evsel) {
		if (evsel->core.attr.aux_output && evsel->core.id) {
			pt->sample_pebs = true;
			pt->pebs_evsel = evsel;
			return;
		}
	}
}

static struct evsel *intel_pt_find_sched_switch(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry_reverse(evlist, evsel) {
		const char *name = evsel__name(evsel);

		if (!strcmp(name, "sched:sched_switch"))
			return evsel;
	}

	return NULL;
}

static bool intel_pt_find_switch(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->core.attr.context_switch)
			return true;
	}

	return false;
}

static int intel_pt_perf_config(const char *var, const char *value, void *data)
{
	struct intel_pt *pt = data;

	if (!strcmp(var, "intel-pt.mispred-all"))
		pt->mispred_all = perf_config_bool(var, value);

	return 0;
}

/* Find least TSC which converts to ns or later */
static u64 intel_pt_tsc_start(u64 ns, struct intel_pt *pt)
{
	u64 tsc, tm;

	tsc = perf_time_to_tsc(ns, &pt->tc);

	while (1) {
		tm = tsc_to_perf_time(tsc, &pt->tc);
		if (tm < ns)
			break;
		tsc -= 1;
	}

	while (tm < ns)
		tm = tsc_to_perf_time(++tsc, &pt->tc);

	return tsc;
}

/* Find greatest TSC which converts to ns or earlier */
static u64 intel_pt_tsc_end(u64 ns, struct intel_pt *pt)
{
	u64 tsc, tm;

	tsc = perf_time_to_tsc(ns, &pt->tc);

	while (1) {
		tm = tsc_to_perf_time(tsc, &pt->tc);
		if (tm > ns)
			break;
		tsc += 1;
	}

	while (tm > ns)
		tm = tsc_to_perf_time(--tsc, &pt->tc);

	return tsc;
}

static int intel_pt_setup_time_ranges(struct intel_pt *pt,
				      struct itrace_synth_opts *opts)
{
	struct perf_time_interval *p = opts->ptime_range;
	int n = opts->range_num;
	int i;

	if (!n || !p || pt->timeless_decoding)
		return 0;

	pt->time_ranges = calloc(n, sizeof(struct range));
	if (!pt->time_ranges)
		return -ENOMEM;

	pt->range_cnt = n;

	intel_pt_log("%s: %u range(s)\n", __func__, n);

	for (i = 0; i < n; i++) {
		struct range *r = &pt->time_ranges[i];
		u64 ts = p[i].start;
		u64 te = p[i].end;

		/*
		 * Take care to ensure the TSC range matches the perf-time range
		 * when converted back to perf-time.
		 */
		r->start = ts ? intel_pt_tsc_start(ts, pt) : 0;
		r->end   = te ? intel_pt_tsc_end(te, pt) : 0;

		intel_pt_log("range %d: perf time interval: %"PRIu64" to %"PRIu64"\n",
			     i, ts, te);
		intel_pt_log("range %d: TSC time interval: %#"PRIx64" to %#"PRIx64"\n",
			     i, r->start, r->end);
	}

	return 0;
}

static const char * const intel_pt_info_fmts[] = {
	[INTEL_PT_PMU_TYPE]		= "  PMU Type            %"PRId64"\n",
	[INTEL_PT_TIME_SHIFT]		= "  Time Shift          %"PRIu64"\n",
	[INTEL_PT_TIME_MULT]		= "  Time Muliplier      %"PRIu64"\n",
	[INTEL_PT_TIME_ZERO]		= "  Time Zero           %"PRIu64"\n",
	[INTEL_PT_CAP_USER_TIME_ZERO]	= "  Cap Time Zero       %"PRId64"\n",
	[INTEL_PT_TSC_BIT]		= "  TSC bit             %#"PRIx64"\n",
	[INTEL_PT_NORETCOMP_BIT]	= "  NoRETComp bit       %#"PRIx64"\n",
	[INTEL_PT_HAVE_SCHED_SWITCH]	= "  Have sched_switch   %"PRId64"\n",
	[INTEL_PT_SNAPSHOT_MODE]	= "  Snapshot mode       %"PRId64"\n",
	[INTEL_PT_PER_CPU_MMAPS]	= "  Per-cpu maps        %"PRId64"\n",
	[INTEL_PT_MTC_BIT]		= "  MTC bit             %#"PRIx64"\n",
	[INTEL_PT_TSC_CTC_N]		= "  TSC:CTC numerator   %"PRIu64"\n",
	[INTEL_PT_TSC_CTC_D]		= "  TSC:CTC denominator %"PRIu64"\n",
	[INTEL_PT_CYC_BIT]		= "  CYC bit             %#"PRIx64"\n",
	[INTEL_PT_MAX_NONTURBO_RATIO]	= "  Max non-turbo ratio %"PRIu64"\n",
	[INTEL_PT_FILTER_STR_LEN]	= "  Filter string len.  %"PRIu64"\n",
};

static void intel_pt_print_info(__u64 *arr, int start, int finish)
{
	int i;

	if (!dump_trace)
		return;

	for (i = start; i <= finish; i++)
		fprintf(stdout, intel_pt_info_fmts[i], arr[i]);
}

static void intel_pt_print_info_str(const char *name, const char *str)
{
	if (!dump_trace)
		return;

	fprintf(stdout, "  %-20s%s\n", name, str ? str : "");
}

static bool intel_pt_has(struct perf_record_auxtrace_info *auxtrace_info, int pos)
{
	return auxtrace_info->header.size >=
		sizeof(struct perf_record_auxtrace_info) + (sizeof(u64) * (pos + 1));
}

int intel_pt_process_auxtrace_info(union perf_event *event,
				   struct perf_session *session)
{
	struct perf_record_auxtrace_info *auxtrace_info = &event->auxtrace_info;
	size_t min_sz = sizeof(u64) * INTEL_PT_PER_CPU_MMAPS;
	struct intel_pt *pt;
	void *info_end;
	__u64 *info;
	int err;

	if (auxtrace_info->header.size < sizeof(struct perf_record_auxtrace_info) +
					min_sz)
		return -EINVAL;

	pt = zalloc(sizeof(struct intel_pt));
	if (!pt)
		return -ENOMEM;

	addr_filters__init(&pt->filts);

	err = perf_config(intel_pt_perf_config, pt);
	if (err)
		goto err_free;

	err = auxtrace_queues__init(&pt->queues);
	if (err)
		goto err_free;

	intel_pt_log_set_name(INTEL_PT_PMU_NAME);

	pt->session = session;
	pt->machine = &session->machines.host; /* No kvm support */
	pt->auxtrace_type = auxtrace_info->type;
	pt->pmu_type = auxtrace_info->priv[INTEL_PT_PMU_TYPE];
	pt->tc.time_shift = auxtrace_info->priv[INTEL_PT_TIME_SHIFT];
	pt->tc.time_mult = auxtrace_info->priv[INTEL_PT_TIME_MULT];
	pt->tc.time_zero = auxtrace_info->priv[INTEL_PT_TIME_ZERO];
	pt->cap_user_time_zero = auxtrace_info->priv[INTEL_PT_CAP_USER_TIME_ZERO];
	pt->tsc_bit = auxtrace_info->priv[INTEL_PT_TSC_BIT];
	pt->noretcomp_bit = auxtrace_info->priv[INTEL_PT_NORETCOMP_BIT];
	pt->have_sched_switch = auxtrace_info->priv[INTEL_PT_HAVE_SCHED_SWITCH];
	pt->snapshot_mode = auxtrace_info->priv[INTEL_PT_SNAPSHOT_MODE];
	pt->per_cpu_mmaps = auxtrace_info->priv[INTEL_PT_PER_CPU_MMAPS];
	intel_pt_print_info(&auxtrace_info->priv[0], INTEL_PT_PMU_TYPE,
			    INTEL_PT_PER_CPU_MMAPS);

	if (intel_pt_has(auxtrace_info, INTEL_PT_CYC_BIT)) {
		pt->mtc_bit = auxtrace_info->priv[INTEL_PT_MTC_BIT];
		pt->mtc_freq_bits = auxtrace_info->priv[INTEL_PT_MTC_FREQ_BITS];
		pt->tsc_ctc_ratio_n = auxtrace_info->priv[INTEL_PT_TSC_CTC_N];
		pt->tsc_ctc_ratio_d = auxtrace_info->priv[INTEL_PT_TSC_CTC_D];
		pt->cyc_bit = auxtrace_info->priv[INTEL_PT_CYC_BIT];
		intel_pt_print_info(&auxtrace_info->priv[0], INTEL_PT_MTC_BIT,
				    INTEL_PT_CYC_BIT);
	}

	if (intel_pt_has(auxtrace_info, INTEL_PT_MAX_NONTURBO_RATIO)) {
		pt->max_non_turbo_ratio =
			auxtrace_info->priv[INTEL_PT_MAX_NONTURBO_RATIO];
		intel_pt_print_info(&auxtrace_info->priv[0],
				    INTEL_PT_MAX_NONTURBO_RATIO,
				    INTEL_PT_MAX_NONTURBO_RATIO);
	}

	info = &auxtrace_info->priv[INTEL_PT_FILTER_STR_LEN] + 1;
	info_end = (void *)info + auxtrace_info->header.size;

	if (intel_pt_has(auxtrace_info, INTEL_PT_FILTER_STR_LEN)) {
		size_t len;

		len = auxtrace_info->priv[INTEL_PT_FILTER_STR_LEN];
		intel_pt_print_info(&auxtrace_info->priv[0],
				    INTEL_PT_FILTER_STR_LEN,
				    INTEL_PT_FILTER_STR_LEN);
		if (len) {
			const char *filter = (const char *)info;

			len = roundup(len + 1, 8);
			info += len >> 3;
			if ((void *)info > info_end) {
				pr_err("%s: bad filter string length\n", __func__);
				err = -EINVAL;
				goto err_free_queues;
			}
			pt->filter = memdup(filter, len);
			if (!pt->filter) {
				err = -ENOMEM;
				goto err_free_queues;
			}
			if (session->header.needs_swap)
				mem_bswap_64(pt->filter, len);
			if (pt->filter[len - 1]) {
				pr_err("%s: filter string not null terminated\n", __func__);
				err = -EINVAL;
				goto err_free_queues;
			}
			err = addr_filters__parse_bare_filter(&pt->filts,
							      filter);
			if (err)
				goto err_free_queues;
		}
		intel_pt_print_info_str("Filter string", pt->filter);
	}

	pt->timeless_decoding = intel_pt_timeless_decoding(pt);
	if (pt->timeless_decoding && !pt->tc.time_mult)
		pt->tc.time_mult = 1;
	pt->have_tsc = intel_pt_have_tsc(pt);
	pt->sampling_mode = intel_pt_sampling_mode(pt);
	pt->est_tsc = !pt->timeless_decoding;

	pt->unknown_thread = thread__new(999999999, 999999999);
	if (!pt->unknown_thread) {
		err = -ENOMEM;
		goto err_free_queues;
	}

	/*
	 * Since this thread will not be kept in any rbtree not in a
	 * list, initialize its list node so that at thread__put() the
	 * current thread lifetime assumption is kept and we don't segfault
	 * at list_del_init().
	 */
	INIT_LIST_HEAD(&pt->unknown_thread->node);

	err = thread__set_comm(pt->unknown_thread, "unknown", 0);
	if (err)
		goto err_delete_thread;
	if (thread__init_maps(pt->unknown_thread, pt->machine)) {
		err = -ENOMEM;
		goto err_delete_thread;
	}

	pt->auxtrace.process_event = intel_pt_process_event;
	pt->auxtrace.process_auxtrace_event = intel_pt_process_auxtrace_event;
	pt->auxtrace.queue_data = intel_pt_queue_data;
	pt->auxtrace.dump_auxtrace_sample = intel_pt_dump_sample;
	pt->auxtrace.flush_events = intel_pt_flush;
	pt->auxtrace.free_events = intel_pt_free_events;
	pt->auxtrace.free = intel_pt_free;
	pt->auxtrace.evsel_is_auxtrace = intel_pt_evsel_is_auxtrace;
	session->auxtrace = &pt->auxtrace;

	if (dump_trace)
		return 0;

	if (pt->have_sched_switch == 1) {
		pt->switch_evsel = intel_pt_find_sched_switch(session->evlist);
		if (!pt->switch_evsel) {
			pr_err("%s: missing sched_switch event\n", __func__);
			err = -EINVAL;
			goto err_delete_thread;
		}
	} else if (pt->have_sched_switch == 2 &&
		   !intel_pt_find_switch(session->evlist)) {
		pr_err("%s: missing context_switch attribute flag\n", __func__);
		err = -EINVAL;
		goto err_delete_thread;
	}

	if (session->itrace_synth_opts->set) {
		pt->synth_opts = *session->itrace_synth_opts;
	} else {
		itrace_synth_opts__set_default(&pt->synth_opts,
				session->itrace_synth_opts->default_no_sample);
		if (!session->itrace_synth_opts->default_no_sample &&
		    !session->itrace_synth_opts->inject) {
			pt->synth_opts.branches = false;
			pt->synth_opts.callchain = true;
			pt->synth_opts.add_callchain = true;
		}
		pt->synth_opts.thread_stack =
				session->itrace_synth_opts->thread_stack;
	}

	if (pt->synth_opts.log)
		intel_pt_log_enable();

	/* Maximum non-turbo ratio is TSC freq / 100 MHz */
	if (pt->tc.time_mult) {
		u64 tsc_freq = intel_pt_ns_to_ticks(pt, 1000000000);

		if (!pt->max_non_turbo_ratio)
			pt->max_non_turbo_ratio =
					(tsc_freq + 50000000) / 100000000;
		intel_pt_log("TSC frequency %"PRIu64"\n", tsc_freq);
		intel_pt_log("Maximum non-turbo ratio %u\n",
			     pt->max_non_turbo_ratio);
		pt->cbr2khz = tsc_freq / pt->max_non_turbo_ratio / 1000;
	}

	err = intel_pt_setup_time_ranges(pt, session->itrace_synth_opts);
	if (err)
		goto err_delete_thread;

	if (pt->synth_opts.calls)
		pt->branches_filter |= PERF_IP_FLAG_CALL | PERF_IP_FLAG_ASYNC |
				       PERF_IP_FLAG_TRACE_END;
	if (pt->synth_opts.returns)
		pt->branches_filter |= PERF_IP_FLAG_RETURN |
				       PERF_IP_FLAG_TRACE_BEGIN;

	if ((pt->synth_opts.callchain || pt->synth_opts.add_callchain) &&
	    !symbol_conf.use_callchain) {
		symbol_conf.use_callchain = true;
		if (callchain_register_param(&callchain_param) < 0) {
			symbol_conf.use_callchain = false;
			pt->synth_opts.callchain = false;
			pt->synth_opts.add_callchain = false;
		}
	}

	if (pt->synth_opts.add_callchain) {
		err = intel_pt_callchain_init(pt);
		if (err)
			goto err_delete_thread;
	}

	if (pt->synth_opts.last_branch || pt->synth_opts.add_last_branch) {
		pt->br_stack_sz = pt->synth_opts.last_branch_sz;
		pt->br_stack_sz_plus = pt->br_stack_sz;
	}

	if (pt->synth_opts.add_last_branch) {
		err = intel_pt_br_stack_init(pt);
		if (err)
			goto err_delete_thread;
		/*
		 * Additional branch stack size to cater for tracing from the
		 * actual sample ip to where the sample time is recorded.
		 * Measured at about 200 branches, but generously set to 1024.
		 * If kernel space is not being traced, then add just 1 for the
		 * branch to kernel space.
		 */
		if (intel_pt_tracing_kernel(pt))
			pt->br_stack_sz_plus += 1024;
		else
			pt->br_stack_sz_plus += 1;
	}

	pt->use_thread_stack = pt->synth_opts.callchain ||
			       pt->synth_opts.add_callchain ||
			       pt->synth_opts.thread_stack ||
			       pt->synth_opts.last_branch ||
			       pt->synth_opts.add_last_branch;

	pt->callstack = pt->synth_opts.callchain ||
			pt->synth_opts.add_callchain ||
			pt->synth_opts.thread_stack;

	err = intel_pt_synth_events(pt, session);
	if (err)
		goto err_delete_thread;

	intel_pt_setup_pebs_events(pt);

	if (pt->sampling_mode || list_empty(&session->auxtrace_index))
		err = auxtrace_queue_data(session, true, true);
	else
		err = auxtrace_queues__process_index(&pt->queues, session);
	if (err)
		goto err_delete_thread;

	if (pt->queues.populated)
		pt->data_queued = true;

	if (pt->timeless_decoding)
		pr_debug2("Intel PT decoding without timestamps\n");

	return 0;

err_delete_thread:
	zfree(&pt->chain);
	thread__zput(pt->unknown_thread);
err_free_queues:
	intel_pt_log_disable();
	auxtrace_queues__free(&pt->queues);
	session->auxtrace = NULL;
err_free:
	addr_filters__exit(&pt->filts);
	zfree(&pt->filter);
	zfree(&pt->time_ranges);
	free(pt);
	return err;
}
