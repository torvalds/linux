/*
 * builtin-timechart.c - make an svg timechart of system activity
 *
 * (C) Copyright 2009 Intel Corporation
 *
 * Authors:
 *     Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <traceevent/event-parse.h>

#include "builtin.h"

#include "util/util.h"

#include "util/color.h"
#include <linux/list.h>
#include "util/cache.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include <linux/rbtree.h>
#include "util/symbol.h"
#include "util/callchain.h"
#include "util/strlist.h"

#include "perf.h"
#include "util/header.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/event.h"
#include "util/session.h"
#include "util/svghelper.h"
#include "util/tool.h"
#include "util/data.h"
#include "util/debug.h"

#define SUPPORT_OLD_POWER_EVENTS 1
#define PWR_EVENT_EXIT -1

struct per_pid;
struct power_event;
struct wake_event;

struct timechart {
	struct perf_tool	tool;
	struct per_pid		*all_data;
	struct power_event	*power_events;
	struct wake_event	*wake_events;
	int			proc_num;
	unsigned int		numcpus;
	u64			min_freq,	/* Lowest CPU frequency seen */
				max_freq,	/* Highest CPU frequency seen */
				turbo_frequency,
				first_time, last_time;
	bool			power_only,
				tasks_only,
				with_backtrace,
				topology;
	bool			force;
	/* IO related settings */
	bool			io_only,
				skip_eagain;
	u64			io_events;
	u64			min_time,
				merge_dist;
};

struct per_pidcomm;
struct cpu_sample;
struct io_sample;

/*
 * Datastructure layout:
 * We keep an list of "pid"s, matching the kernels notion of a task struct.
 * Each "pid" entry, has a list of "comm"s.
 *	this is because we want to track different programs different, while
 *	exec will reuse the original pid (by design).
 * Each comm has a list of samples that will be used to draw
 * final graph.
 */

struct per_pid {
	struct per_pid *next;

	int		pid;
	int		ppid;

	u64		start_time;
	u64		end_time;
	u64		total_time;
	u64		total_bytes;
	int		display;

	struct per_pidcomm *all;
	struct per_pidcomm *current;
};


struct per_pidcomm {
	struct per_pidcomm *next;

	u64		start_time;
	u64		end_time;
	u64		total_time;
	u64		max_bytes;
	u64		total_bytes;

	int		Y;
	int		display;

	long		state;
	u64		state_since;

	char		*comm;

	struct cpu_sample *samples;
	struct io_sample  *io_samples;
};

struct sample_wrapper {
	struct sample_wrapper *next;

	u64		timestamp;
	unsigned char	data[0];
};

#define TYPE_NONE	0
#define TYPE_RUNNING	1
#define TYPE_WAITING	2
#define TYPE_BLOCKED	3

struct cpu_sample {
	struct cpu_sample *next;

	u64 start_time;
	u64 end_time;
	int type;
	int cpu;
	const char *backtrace;
};

enum {
	IOTYPE_READ,
	IOTYPE_WRITE,
	IOTYPE_SYNC,
	IOTYPE_TX,
	IOTYPE_RX,
	IOTYPE_POLL,
};

struct io_sample {
	struct io_sample *next;

	u64 start_time;
	u64 end_time;
	u64 bytes;
	int type;
	int fd;
	int err;
	int merges;
};

#define CSTATE 1
#define PSTATE 2

struct power_event {
	struct power_event *next;
	int type;
	int state;
	u64 start_time;
	u64 end_time;
	int cpu;
};

struct wake_event {
	struct wake_event *next;
	int waker;
	int wakee;
	u64 time;
	const char *backtrace;
};

struct process_filter {
	char			*name;
	int			pid;
	struct process_filter	*next;
};

static struct process_filter *process_filter;


static struct per_pid *find_create_pid(struct timechart *tchart, int pid)
{
	struct per_pid *cursor = tchart->all_data;

	while (cursor) {
		if (cursor->pid == pid)
			return cursor;
		cursor = cursor->next;
	}
	cursor = zalloc(sizeof(*cursor));
	assert(cursor != NULL);
	cursor->pid = pid;
	cursor->next = tchart->all_data;
	tchart->all_data = cursor;
	return cursor;
}

static void pid_set_comm(struct timechart *tchart, int pid, char *comm)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	p = find_create_pid(tchart, pid);
	c = p->all;
	while (c) {
		if (c->comm && strcmp(c->comm, comm) == 0) {
			p->current = c;
			return;
		}
		if (!c->comm) {
			c->comm = strdup(comm);
			p->current = c;
			return;
		}
		c = c->next;
	}
	c = zalloc(sizeof(*c));
	assert(c != NULL);
	c->comm = strdup(comm);
	p->current = c;
	c->next = p->all;
	p->all = c;
}

static void pid_fork(struct timechart *tchart, int pid, int ppid, u64 timestamp)
{
	struct per_pid *p, *pp;
	p = find_create_pid(tchart, pid);
	pp = find_create_pid(tchart, ppid);
	p->ppid = ppid;
	if (pp->current && pp->current->comm && !p->current)
		pid_set_comm(tchart, pid, pp->current->comm);

	p->start_time = timestamp;
	if (p->current && !p->current->start_time) {
		p->current->start_time = timestamp;
		p->current->state_since = timestamp;
	}
}

static void pid_exit(struct timechart *tchart, int pid, u64 timestamp)
{
	struct per_pid *p;
	p = find_create_pid(tchart, pid);
	p->end_time = timestamp;
	if (p->current)
		p->current->end_time = timestamp;
}

static void pid_put_sample(struct timechart *tchart, int pid, int type,
			   unsigned int cpu, u64 start, u64 end,
			   const char *backtrace)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	struct cpu_sample *sample;

	p = find_create_pid(tchart, pid);
	c = p->current;
	if (!c) {
		c = zalloc(sizeof(*c));
		assert(c != NULL);
		p->current = c;
		c->next = p->all;
		p->all = c;
	}

	sample = zalloc(sizeof(*sample));
	assert(sample != NULL);
	sample->start_time = start;
	sample->end_time = end;
	sample->type = type;
	sample->next = c->samples;
	sample->cpu = cpu;
	sample->backtrace = backtrace;
	c->samples = sample;

	if (sample->type == TYPE_RUNNING && end > start && start > 0) {
		c->total_time += (end-start);
		p->total_time += (end-start);
	}

	if (c->start_time == 0 || c->start_time > start)
		c->start_time = start;
	if (p->start_time == 0 || p->start_time > start)
		p->start_time = start;
}

#define MAX_CPUS 4096

static u64 cpus_cstate_start_times[MAX_CPUS];
static int cpus_cstate_state[MAX_CPUS];
static u64 cpus_pstate_start_times[MAX_CPUS];
static u64 cpus_pstate_state[MAX_CPUS];

static int process_comm_event(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	struct timechart *tchart = container_of(tool, struct timechart, tool);
	pid_set_comm(tchart, event->comm.tid, event->comm.comm);
	return 0;
}

static int process_fork_event(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	struct timechart *tchart = container_of(tool, struct timechart, tool);
	pid_fork(tchart, event->fork.pid, event->fork.ppid, event->fork.time);
	return 0;
}

static int process_exit_event(struct perf_tool *tool,
			      union perf_event *event,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	struct timechart *tchart = container_of(tool, struct timechart, tool);
	pid_exit(tchart, event->fork.pid, event->fork.time);
	return 0;
}

#ifdef SUPPORT_OLD_POWER_EVENTS
static int use_old_power_events;
#endif

static void c_state_start(int cpu, u64 timestamp, int state)
{
	cpus_cstate_start_times[cpu] = timestamp;
	cpus_cstate_state[cpu] = state;
}

static void c_state_end(struct timechart *tchart, int cpu, u64 timestamp)
{
	struct power_event *pwr = zalloc(sizeof(*pwr));

	if (!pwr)
		return;

	pwr->state = cpus_cstate_state[cpu];
	pwr->start_time = cpus_cstate_start_times[cpu];
	pwr->end_time = timestamp;
	pwr->cpu = cpu;
	pwr->type = CSTATE;
	pwr->next = tchart->power_events;

	tchart->power_events = pwr;
}

static void p_state_change(struct timechart *tchart, int cpu, u64 timestamp, u64 new_freq)
{
	struct power_event *pwr;

	if (new_freq > 8000000) /* detect invalid data */
		return;

	pwr = zalloc(sizeof(*pwr));
	if (!pwr)
		return;

	pwr->state = cpus_pstate_state[cpu];
	pwr->start_time = cpus_pstate_start_times[cpu];
	pwr->end_time = timestamp;
	pwr->cpu = cpu;
	pwr->type = PSTATE;
	pwr->next = tchart->power_events;

	if (!pwr->start_time)
		pwr->start_time = tchart->first_time;

	tchart->power_events = pwr;

	cpus_pstate_state[cpu] = new_freq;
	cpus_pstate_start_times[cpu] = timestamp;

	if ((u64)new_freq > tchart->max_freq)
		tchart->max_freq = new_freq;

	if (new_freq < tchart->min_freq || tchart->min_freq == 0)
		tchart->min_freq = new_freq;

	if (new_freq == tchart->max_freq - 1000)
		tchart->turbo_frequency = tchart->max_freq;
}

static void sched_wakeup(struct timechart *tchart, int cpu, u64 timestamp,
			 int waker, int wakee, u8 flags, const char *backtrace)
{
	struct per_pid *p;
	struct wake_event *we = zalloc(sizeof(*we));

	if (!we)
		return;

	we->time = timestamp;
	we->waker = waker;
	we->backtrace = backtrace;

	if ((flags & TRACE_FLAG_HARDIRQ) || (flags & TRACE_FLAG_SOFTIRQ))
		we->waker = -1;

	we->wakee = wakee;
	we->next = tchart->wake_events;
	tchart->wake_events = we;
	p = find_create_pid(tchart, we->wakee);

	if (p && p->current && p->current->state == TYPE_NONE) {
		p->current->state_since = timestamp;
		p->current->state = TYPE_WAITING;
	}
	if (p && p->current && p->current->state == TYPE_BLOCKED) {
		pid_put_sample(tchart, p->pid, p->current->state, cpu,
			       p->current->state_since, timestamp, NULL);
		p->current->state_since = timestamp;
		p->current->state = TYPE_WAITING;
	}
}

static void sched_switch(struct timechart *tchart, int cpu, u64 timestamp,
			 int prev_pid, int next_pid, u64 prev_state,
			 const char *backtrace)
{
	struct per_pid *p = NULL, *prev_p;

	prev_p = find_create_pid(tchart, prev_pid);

	p = find_create_pid(tchart, next_pid);

	if (prev_p->current && prev_p->current->state != TYPE_NONE)
		pid_put_sample(tchart, prev_pid, TYPE_RUNNING, cpu,
			       prev_p->current->state_since, timestamp,
			       backtrace);
	if (p && p->current) {
		if (p->current->state != TYPE_NONE)
			pid_put_sample(tchart, next_pid, p->current->state, cpu,
				       p->current->state_since, timestamp,
				       backtrace);

		p->current->state_since = timestamp;
		p->current->state = TYPE_RUNNING;
	}

	if (prev_p->current) {
		prev_p->current->state = TYPE_NONE;
		prev_p->current->state_since = timestamp;
		if (prev_state & 2)
			prev_p->current->state = TYPE_BLOCKED;
		if (prev_state == 0)
			prev_p->current->state = TYPE_WAITING;
	}
}

static const char *cat_backtrace(union perf_event *event,
				 struct perf_sample *sample,
				 struct machine *machine)
{
	struct addr_location al;
	unsigned int i;
	char *p = NULL;
	size_t p_len;
	u8 cpumode = PERF_RECORD_MISC_USER;
	struct addr_location tal;
	struct ip_callchain *chain = sample->callchain;
	FILE *f = open_memstream(&p, &p_len);

	if (!f) {
		perror("open_memstream error");
		return NULL;
	}

	if (!chain)
		goto exit;

	if (perf_event__preprocess_sample(event, machine, &al, sample) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
			event->header.type);
		goto exit;
	}

	for (i = 0; i < chain->nr; i++) {
		u64 ip;

		if (callchain_param.order == ORDER_CALLEE)
			ip = chain->ips[i];
		else
			ip = chain->ips[chain->nr - i - 1];

		if (ip >= PERF_CONTEXT_MAX) {
			switch (ip) {
			case PERF_CONTEXT_HV:
				cpumode = PERF_RECORD_MISC_HYPERVISOR;
				break;
			case PERF_CONTEXT_KERNEL:
				cpumode = PERF_RECORD_MISC_KERNEL;
				break;
			case PERF_CONTEXT_USER:
				cpumode = PERF_RECORD_MISC_USER;
				break;
			default:
				pr_debug("invalid callchain context: "
					 "%"PRId64"\n", (s64) ip);

				/*
				 * It seems the callchain is corrupted.
				 * Discard all.
				 */
				zfree(&p);
				goto exit_put;
			}
			continue;
		}

		tal.filtered = 0;
		thread__find_addr_location(al.thread, cpumode,
					   MAP__FUNCTION, ip, &tal);

		if (tal.sym)
			fprintf(f, "..... %016" PRIx64 " %s\n", ip,
				tal.sym->name);
		else
			fprintf(f, "..... %016" PRIx64 "\n", ip);
	}
exit_put:
	addr_location__put(&al);
exit:
	fclose(f);

	return p;
}

typedef int (*tracepoint_handler)(struct timechart *tchart,
				  struct perf_evsel *evsel,
				  struct perf_sample *sample,
				  const char *backtrace);

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct timechart *tchart = container_of(tool, struct timechart, tool);

	if (evsel->attr.sample_type & PERF_SAMPLE_TIME) {
		if (!tchart->first_time || tchart->first_time > sample->time)
			tchart->first_time = sample->time;
		if (tchart->last_time < sample->time)
			tchart->last_time = sample->time;
	}

	if (evsel->handler != NULL) {
		tracepoint_handler f = evsel->handler;
		return f(tchart, evsel, sample,
			 cat_backtrace(event, sample, machine));
	}

	return 0;
}

static int
process_sample_cpu_idle(struct timechart *tchart __maybe_unused,
			struct perf_evsel *evsel,
			struct perf_sample *sample,
			const char *backtrace __maybe_unused)
{
	u32 state = perf_evsel__intval(evsel, sample, "state");
	u32 cpu_id = perf_evsel__intval(evsel, sample, "cpu_id");

	if (state == (u32)PWR_EVENT_EXIT)
		c_state_end(tchart, cpu_id, sample->time);
	else
		c_state_start(cpu_id, sample->time, state);
	return 0;
}

static int
process_sample_cpu_frequency(struct timechart *tchart,
			     struct perf_evsel *evsel,
			     struct perf_sample *sample,
			     const char *backtrace __maybe_unused)
{
	u32 state = perf_evsel__intval(evsel, sample, "state");
	u32 cpu_id = perf_evsel__intval(evsel, sample, "cpu_id");

	p_state_change(tchart, cpu_id, sample->time, state);
	return 0;
}

static int
process_sample_sched_wakeup(struct timechart *tchart,
			    struct perf_evsel *evsel,
			    struct perf_sample *sample,
			    const char *backtrace)
{
	u8 flags = perf_evsel__intval(evsel, sample, "common_flags");
	int waker = perf_evsel__intval(evsel, sample, "common_pid");
	int wakee = perf_evsel__intval(evsel, sample, "pid");

	sched_wakeup(tchart, sample->cpu, sample->time, waker, wakee, flags, backtrace);
	return 0;
}

static int
process_sample_sched_switch(struct timechart *tchart,
			    struct perf_evsel *evsel,
			    struct perf_sample *sample,
			    const char *backtrace)
{
	int prev_pid = perf_evsel__intval(evsel, sample, "prev_pid");
	int next_pid = perf_evsel__intval(evsel, sample, "next_pid");
	u64 prev_state = perf_evsel__intval(evsel, sample, "prev_state");

	sched_switch(tchart, sample->cpu, sample->time, prev_pid, next_pid,
		     prev_state, backtrace);
	return 0;
}

#ifdef SUPPORT_OLD_POWER_EVENTS
static int
process_sample_power_start(struct timechart *tchart __maybe_unused,
			   struct perf_evsel *evsel,
			   struct perf_sample *sample,
			   const char *backtrace __maybe_unused)
{
	u64 cpu_id = perf_evsel__intval(evsel, sample, "cpu_id");
	u64 value = perf_evsel__intval(evsel, sample, "value");

	c_state_start(cpu_id, sample->time, value);
	return 0;
}

static int
process_sample_power_end(struct timechart *tchart,
			 struct perf_evsel *evsel __maybe_unused,
			 struct perf_sample *sample,
			 const char *backtrace __maybe_unused)
{
	c_state_end(tchart, sample->cpu, sample->time);
	return 0;
}

static int
process_sample_power_frequency(struct timechart *tchart,
			       struct perf_evsel *evsel,
			       struct perf_sample *sample,
			       const char *backtrace __maybe_unused)
{
	u64 cpu_id = perf_evsel__intval(evsel, sample, "cpu_id");
	u64 value = perf_evsel__intval(evsel, sample, "value");

	p_state_change(tchart, cpu_id, sample->time, value);
	return 0;
}
#endif /* SUPPORT_OLD_POWER_EVENTS */

/*
 * After the last sample we need to wrap up the current C/P state
 * and close out each CPU for these.
 */
static void end_sample_processing(struct timechart *tchart)
{
	u64 cpu;
	struct power_event *pwr;

	for (cpu = 0; cpu <= tchart->numcpus; cpu++) {
		/* C state */
#if 0
		pwr = zalloc(sizeof(*pwr));
		if (!pwr)
			return;

		pwr->state = cpus_cstate_state[cpu];
		pwr->start_time = cpus_cstate_start_times[cpu];
		pwr->end_time = tchart->last_time;
		pwr->cpu = cpu;
		pwr->type = CSTATE;
		pwr->next = tchart->power_events;

		tchart->power_events = pwr;
#endif
		/* P state */

		pwr = zalloc(sizeof(*pwr));
		if (!pwr)
			return;

		pwr->state = cpus_pstate_state[cpu];
		pwr->start_time = cpus_pstate_start_times[cpu];
		pwr->end_time = tchart->last_time;
		pwr->cpu = cpu;
		pwr->type = PSTATE;
		pwr->next = tchart->power_events;

		if (!pwr->start_time)
			pwr->start_time = tchart->first_time;
		if (!pwr->state)
			pwr->state = tchart->min_freq;
		tchart->power_events = pwr;
	}
}

static int pid_begin_io_sample(struct timechart *tchart, int pid, int type,
			       u64 start, int fd)
{
	struct per_pid *p = find_create_pid(tchart, pid);
	struct per_pidcomm *c = p->current;
	struct io_sample *sample;
	struct io_sample *prev;

	if (!c) {
		c = zalloc(sizeof(*c));
		if (!c)
			return -ENOMEM;
		p->current = c;
		c->next = p->all;
		p->all = c;
	}

	prev = c->io_samples;

	if (prev && prev->start_time && !prev->end_time) {
		pr_warning("Skip invalid start event: "
			   "previous event already started!\n");

		/* remove previous event that has been started,
		 * we are not sure we will ever get an end for it */
		c->io_samples = prev->next;
		free(prev);
		return 0;
	}

	sample = zalloc(sizeof(*sample));
	if (!sample)
		return -ENOMEM;
	sample->start_time = start;
	sample->type = type;
	sample->fd = fd;
	sample->next = c->io_samples;
	c->io_samples = sample;

	if (c->start_time == 0 || c->start_time > start)
		c->start_time = start;

	return 0;
}

static int pid_end_io_sample(struct timechart *tchart, int pid, int type,
			     u64 end, long ret)
{
	struct per_pid *p = find_create_pid(tchart, pid);
	struct per_pidcomm *c = p->current;
	struct io_sample *sample, *prev;

	if (!c) {
		pr_warning("Invalid pidcomm!\n");
		return -1;
	}

	sample = c->io_samples;

	if (!sample) /* skip partially captured events */
		return 0;

	if (sample->end_time) {
		pr_warning("Skip invalid end event: "
			   "previous event already ended!\n");
		return 0;
	}

	if (sample->type != type) {
		pr_warning("Skip invalid end event: invalid event type!\n");
		return 0;
	}

	sample->end_time = end;
	prev = sample->next;

	/* we want to be able to see small and fast transfers, so make them
	 * at least min_time long, but don't overlap them */
	if (sample->end_time - sample->start_time < tchart->min_time)
		sample->end_time = sample->start_time + tchart->min_time;
	if (prev && sample->start_time < prev->end_time) {
		if (prev->err) /* try to make errors more visible */
			sample->start_time = prev->end_time;
		else
			prev->end_time = sample->start_time;
	}

	if (ret < 0) {
		sample->err = ret;
	} else if (type == IOTYPE_READ || type == IOTYPE_WRITE ||
		   type == IOTYPE_TX || type == IOTYPE_RX) {

		if ((u64)ret > c->max_bytes)
			c->max_bytes = ret;

		c->total_bytes += ret;
		p->total_bytes += ret;
		sample->bytes = ret;
	}

	/* merge two requests to make svg smaller and render-friendly */
	if (prev &&
	    prev->type == sample->type &&
	    prev->err == sample->err &&
	    prev->fd == sample->fd &&
	    prev->end_time + tchart->merge_dist >= sample->start_time) {

		sample->bytes += prev->bytes;
		sample->merges += prev->merges + 1;

		sample->start_time = prev->start_time;
		sample->next = prev->next;
		free(prev);

		if (!sample->err && sample->bytes > c->max_bytes)
			c->max_bytes = sample->bytes;
	}

	tchart->io_events++;

	return 0;
}

static int
process_enter_read(struct timechart *tchart,
		   struct perf_evsel *evsel,
		   struct perf_sample *sample)
{
	long fd = perf_evsel__intval(evsel, sample, "fd");
	return pid_begin_io_sample(tchart, sample->tid, IOTYPE_READ,
				   sample->time, fd);
}

static int
process_exit_read(struct timechart *tchart,
		  struct perf_evsel *evsel,
		  struct perf_sample *sample)
{
	long ret = perf_evsel__intval(evsel, sample, "ret");
	return pid_end_io_sample(tchart, sample->tid, IOTYPE_READ,
				 sample->time, ret);
}

static int
process_enter_write(struct timechart *tchart,
		    struct perf_evsel *evsel,
		    struct perf_sample *sample)
{
	long fd = perf_evsel__intval(evsel, sample, "fd");
	return pid_begin_io_sample(tchart, sample->tid, IOTYPE_WRITE,
				   sample->time, fd);
}

static int
process_exit_write(struct timechart *tchart,
		   struct perf_evsel *evsel,
		   struct perf_sample *sample)
{
	long ret = perf_evsel__intval(evsel, sample, "ret");
	return pid_end_io_sample(tchart, sample->tid, IOTYPE_WRITE,
				 sample->time, ret);
}

static int
process_enter_sync(struct timechart *tchart,
		   struct perf_evsel *evsel,
		   struct perf_sample *sample)
{
	long fd = perf_evsel__intval(evsel, sample, "fd");
	return pid_begin_io_sample(tchart, sample->tid, IOTYPE_SYNC,
				   sample->time, fd);
}

static int
process_exit_sync(struct timechart *tchart,
		  struct perf_evsel *evsel,
		  struct perf_sample *sample)
{
	long ret = perf_evsel__intval(evsel, sample, "ret");
	return pid_end_io_sample(tchart, sample->tid, IOTYPE_SYNC,
				 sample->time, ret);
}

static int
process_enter_tx(struct timechart *tchart,
		 struct perf_evsel *evsel,
		 struct perf_sample *sample)
{
	long fd = perf_evsel__intval(evsel, sample, "fd");
	return pid_begin_io_sample(tchart, sample->tid, IOTYPE_TX,
				   sample->time, fd);
}

static int
process_exit_tx(struct timechart *tchart,
		struct perf_evsel *evsel,
		struct perf_sample *sample)
{
	long ret = perf_evsel__intval(evsel, sample, "ret");
	return pid_end_io_sample(tchart, sample->tid, IOTYPE_TX,
				 sample->time, ret);
}

static int
process_enter_rx(struct timechart *tchart,
		 struct perf_evsel *evsel,
		 struct perf_sample *sample)
{
	long fd = perf_evsel__intval(evsel, sample, "fd");
	return pid_begin_io_sample(tchart, sample->tid, IOTYPE_RX,
				   sample->time, fd);
}

static int
process_exit_rx(struct timechart *tchart,
		struct perf_evsel *evsel,
		struct perf_sample *sample)
{
	long ret = perf_evsel__intval(evsel, sample, "ret");
	return pid_end_io_sample(tchart, sample->tid, IOTYPE_RX,
				 sample->time, ret);
}

static int
process_enter_poll(struct timechart *tchart,
		   struct perf_evsel *evsel,
		   struct perf_sample *sample)
{
	long fd = perf_evsel__intval(evsel, sample, "fd");
	return pid_begin_io_sample(tchart, sample->tid, IOTYPE_POLL,
				   sample->time, fd);
}

static int
process_exit_poll(struct timechart *tchart,
		  struct perf_evsel *evsel,
		  struct perf_sample *sample)
{
	long ret = perf_evsel__intval(evsel, sample, "ret");
	return pid_end_io_sample(tchart, sample->tid, IOTYPE_POLL,
				 sample->time, ret);
}

/*
 * Sort the pid datastructure
 */
static void sort_pids(struct timechart *tchart)
{
	struct per_pid *new_list, *p, *cursor, *prev;
	/* sort by ppid first, then by pid, lowest to highest */

	new_list = NULL;

	while (tchart->all_data) {
		p = tchart->all_data;
		tchart->all_data = p->next;
		p->next = NULL;

		if (new_list == NULL) {
			new_list = p;
			p->next = NULL;
			continue;
		}
		prev = NULL;
		cursor = new_list;
		while (cursor) {
			if (cursor->ppid > p->ppid ||
				(cursor->ppid == p->ppid && cursor->pid > p->pid)) {
				/* must insert before */
				if (prev) {
					p->next = prev->next;
					prev->next = p;
					cursor = NULL;
					continue;
				} else {
					p->next = new_list;
					new_list = p;
					cursor = NULL;
					continue;
				}
			}

			prev = cursor;
			cursor = cursor->next;
			if (!cursor)
				prev->next = p;
		}
	}
	tchart->all_data = new_list;
}


static void draw_c_p_states(struct timechart *tchart)
{
	struct power_event *pwr;
	pwr = tchart->power_events;

	/*
	 * two pass drawing so that the P state bars are on top of the C state blocks
	 */
	while (pwr) {
		if (pwr->type == CSTATE)
			svg_cstate(pwr->cpu, pwr->start_time, pwr->end_time, pwr->state);
		pwr = pwr->next;
	}

	pwr = tchart->power_events;
	while (pwr) {
		if (pwr->type == PSTATE) {
			if (!pwr->state)
				pwr->state = tchart->min_freq;
			svg_pstate(pwr->cpu, pwr->start_time, pwr->end_time, pwr->state);
		}
		pwr = pwr->next;
	}
}

static void draw_wakeups(struct timechart *tchart)
{
	struct wake_event *we;
	struct per_pid *p;
	struct per_pidcomm *c;

	we = tchart->wake_events;
	while (we) {
		int from = 0, to = 0;
		char *task_from = NULL, *task_to = NULL;

		/* locate the column of the waker and wakee */
		p = tchart->all_data;
		while (p) {
			if (p->pid == we->waker || p->pid == we->wakee) {
				c = p->all;
				while (c) {
					if (c->Y && c->start_time <= we->time && c->end_time >= we->time) {
						if (p->pid == we->waker && !from) {
							from = c->Y;
							task_from = strdup(c->comm);
						}
						if (p->pid == we->wakee && !to) {
							to = c->Y;
							task_to = strdup(c->comm);
						}
					}
					c = c->next;
				}
				c = p->all;
				while (c) {
					if (p->pid == we->waker && !from) {
						from = c->Y;
						task_from = strdup(c->comm);
					}
					if (p->pid == we->wakee && !to) {
						to = c->Y;
						task_to = strdup(c->comm);
					}
					c = c->next;
				}
			}
			p = p->next;
		}

		if (!task_from) {
			task_from = malloc(40);
			sprintf(task_from, "[%i]", we->waker);
		}
		if (!task_to) {
			task_to = malloc(40);
			sprintf(task_to, "[%i]", we->wakee);
		}

		if (we->waker == -1)
			svg_interrupt(we->time, to, we->backtrace);
		else if (from && to && abs(from - to) == 1)
			svg_wakeline(we->time, from, to, we->backtrace);
		else
			svg_partial_wakeline(we->time, from, task_from, to,
					     task_to, we->backtrace);
		we = we->next;

		free(task_from);
		free(task_to);
	}
}

static void draw_cpu_usage(struct timechart *tchart)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	struct cpu_sample *sample;
	p = tchart->all_data;
	while (p) {
		c = p->all;
		while (c) {
			sample = c->samples;
			while (sample) {
				if (sample->type == TYPE_RUNNING) {
					svg_process(sample->cpu,
						    sample->start_time,
						    sample->end_time,
						    p->pid,
						    c->comm,
						    sample->backtrace);
				}

				sample = sample->next;
			}
			c = c->next;
		}
		p = p->next;
	}
}

static void draw_io_bars(struct timechart *tchart)
{
	const char *suf;
	double bytes;
	char comm[256];
	struct per_pid *p;
	struct per_pidcomm *c;
	struct io_sample *sample;
	int Y = 1;

	p = tchart->all_data;
	while (p) {
		c = p->all;
		while (c) {
			if (!c->display) {
				c->Y = 0;
				c = c->next;
				continue;
			}

			svg_box(Y, c->start_time, c->end_time, "process3");
			sample = c->io_samples;
			for (sample = c->io_samples; sample; sample = sample->next) {
				double h = (double)sample->bytes / c->max_bytes;

				if (tchart->skip_eagain &&
				    sample->err == -EAGAIN)
					continue;

				if (sample->err)
					h = 1;

				if (sample->type == IOTYPE_SYNC)
					svg_fbox(Y,
						sample->start_time,
						sample->end_time,
						1,
						sample->err ? "error" : "sync",
						sample->fd,
						sample->err,
						sample->merges);
				else if (sample->type == IOTYPE_POLL)
					svg_fbox(Y,
						sample->start_time,
						sample->end_time,
						1,
						sample->err ? "error" : "poll",
						sample->fd,
						sample->err,
						sample->merges);
				else if (sample->type == IOTYPE_READ)
					svg_ubox(Y,
						sample->start_time,
						sample->end_time,
						h,
						sample->err ? "error" : "disk",
						sample->fd,
						sample->err,
						sample->merges);
				else if (sample->type == IOTYPE_WRITE)
					svg_lbox(Y,
						sample->start_time,
						sample->end_time,
						h,
						sample->err ? "error" : "disk",
						sample->fd,
						sample->err,
						sample->merges);
				else if (sample->type == IOTYPE_RX)
					svg_ubox(Y,
						sample->start_time,
						sample->end_time,
						h,
						sample->err ? "error" : "net",
						sample->fd,
						sample->err,
						sample->merges);
				else if (sample->type == IOTYPE_TX)
					svg_lbox(Y,
						sample->start_time,
						sample->end_time,
						h,
						sample->err ? "error" : "net",
						sample->fd,
						sample->err,
						sample->merges);
			}

			suf = "";
			bytes = c->total_bytes;
			if (bytes > 1024) {
				bytes = bytes / 1024;
				suf = "K";
			}
			if (bytes > 1024) {
				bytes = bytes / 1024;
				suf = "M";
			}
			if (bytes > 1024) {
				bytes = bytes / 1024;
				suf = "G";
			}


			sprintf(comm, "%s:%i (%3.1f %sbytes)", c->comm ?: "", p->pid, bytes, suf);
			svg_text(Y, c->start_time, comm);

			c->Y = Y;
			Y++;
			c = c->next;
		}
		p = p->next;
	}
}

static void draw_process_bars(struct timechart *tchart)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	struct cpu_sample *sample;
	int Y = 0;

	Y = 2 * tchart->numcpus + 2;

	p = tchart->all_data;
	while (p) {
		c = p->all;
		while (c) {
			if (!c->display) {
				c->Y = 0;
				c = c->next;
				continue;
			}

			svg_box(Y, c->start_time, c->end_time, "process");
			sample = c->samples;
			while (sample) {
				if (sample->type == TYPE_RUNNING)
					svg_running(Y, sample->cpu,
						    sample->start_time,
						    sample->end_time,
						    sample->backtrace);
				if (sample->type == TYPE_BLOCKED)
					svg_blocked(Y, sample->cpu,
						    sample->start_time,
						    sample->end_time,
						    sample->backtrace);
				if (sample->type == TYPE_WAITING)
					svg_waiting(Y, sample->cpu,
						    sample->start_time,
						    sample->end_time,
						    sample->backtrace);
				sample = sample->next;
			}

			if (c->comm) {
				char comm[256];
				if (c->total_time > 5000000000) /* 5 seconds */
					sprintf(comm, "%s:%i (%2.2fs)", c->comm, p->pid, c->total_time / 1000000000.0);
				else
					sprintf(comm, "%s:%i (%3.1fms)", c->comm, p->pid, c->total_time / 1000000.0);

				svg_text(Y, c->start_time, comm);
			}
			c->Y = Y;
			Y++;
			c = c->next;
		}
		p = p->next;
	}
}

static void add_process_filter(const char *string)
{
	int pid = strtoull(string, NULL, 10);
	struct process_filter *filt = malloc(sizeof(*filt));

	if (!filt)
		return;

	filt->name = strdup(string);
	filt->pid  = pid;
	filt->next = process_filter;

	process_filter = filt;
}

static int passes_filter(struct per_pid *p, struct per_pidcomm *c)
{
	struct process_filter *filt;
	if (!process_filter)
		return 1;

	filt = process_filter;
	while (filt) {
		if (filt->pid && p->pid == filt->pid)
			return 1;
		if (strcmp(filt->name, c->comm) == 0)
			return 1;
		filt = filt->next;
	}
	return 0;
}

static int determine_display_tasks_filtered(struct timechart *tchart)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	int count = 0;

	p = tchart->all_data;
	while (p) {
		p->display = 0;
		if (p->start_time == 1)
			p->start_time = tchart->first_time;

		/* no exit marker, task kept running to the end */
		if (p->end_time == 0)
			p->end_time = tchart->last_time;

		c = p->all;

		while (c) {
			c->display = 0;

			if (c->start_time == 1)
				c->start_time = tchart->first_time;

			if (passes_filter(p, c)) {
				c->display = 1;
				p->display = 1;
				count++;
			}

			if (c->end_time == 0)
				c->end_time = tchart->last_time;

			c = c->next;
		}
		p = p->next;
	}
	return count;
}

static int determine_display_tasks(struct timechart *tchart, u64 threshold)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	int count = 0;

	p = tchart->all_data;
	while (p) {
		p->display = 0;
		if (p->start_time == 1)
			p->start_time = tchart->first_time;

		/* no exit marker, task kept running to the end */
		if (p->end_time == 0)
			p->end_time = tchart->last_time;
		if (p->total_time >= threshold)
			p->display = 1;

		c = p->all;

		while (c) {
			c->display = 0;

			if (c->start_time == 1)
				c->start_time = tchart->first_time;

			if (c->total_time >= threshold) {
				c->display = 1;
				count++;
			}

			if (c->end_time == 0)
				c->end_time = tchart->last_time;

			c = c->next;
		}
		p = p->next;
	}
	return count;
}

static int determine_display_io_tasks(struct timechart *timechart, u64 threshold)
{
	struct per_pid *p;
	struct per_pidcomm *c;
	int count = 0;

	p = timechart->all_data;
	while (p) {
		/* no exit marker, task kept running to the end */
		if (p->end_time == 0)
			p->end_time = timechart->last_time;

		c = p->all;

		while (c) {
			c->display = 0;

			if (c->total_bytes >= threshold) {
				c->display = 1;
				count++;
			}

			if (c->end_time == 0)
				c->end_time = timechart->last_time;

			c = c->next;
		}
		p = p->next;
	}
	return count;
}

#define BYTES_THRESH (1 * 1024 * 1024)
#define TIME_THRESH 10000000

static void write_svg_file(struct timechart *tchart, const char *filename)
{
	u64 i;
	int count;
	int thresh = tchart->io_events ? BYTES_THRESH : TIME_THRESH;

	if (tchart->power_only)
		tchart->proc_num = 0;

	/* We'd like to show at least proc_num tasks;
	 * be less picky if we have fewer */
	do {
		if (process_filter)
			count = determine_display_tasks_filtered(tchart);
		else if (tchart->io_events)
			count = determine_display_io_tasks(tchart, thresh);
		else
			count = determine_display_tasks(tchart, thresh);
		thresh /= 10;
	} while (!process_filter && thresh && count < tchart->proc_num);

	if (!tchart->proc_num)
		count = 0;

	if (tchart->io_events) {
		open_svg(filename, 0, count, tchart->first_time, tchart->last_time);

		svg_time_grid(0.5);
		svg_io_legenda();

		draw_io_bars(tchart);
	} else {
		open_svg(filename, tchart->numcpus, count, tchart->first_time, tchart->last_time);

		svg_time_grid(0);

		svg_legenda();

		for (i = 0; i < tchart->numcpus; i++)
			svg_cpu_box(i, tchart->max_freq, tchart->turbo_frequency);

		draw_cpu_usage(tchart);
		if (tchart->proc_num)
			draw_process_bars(tchart);
		if (!tchart->tasks_only)
			draw_c_p_states(tchart);
		if (tchart->proc_num)
			draw_wakeups(tchart);
	}

	svg_close();
}

static int process_header(struct perf_file_section *section __maybe_unused,
			  struct perf_header *ph,
			  int feat,
			  int fd __maybe_unused,
			  void *data)
{
	struct timechart *tchart = data;

	switch (feat) {
	case HEADER_NRCPUS:
		tchart->numcpus = ph->env.nr_cpus_avail;
		break;

	case HEADER_CPU_TOPOLOGY:
		if (!tchart->topology)
			break;

		if (svg_build_topology_map(ph->env.sibling_cores,
					   ph->env.nr_sibling_cores,
					   ph->env.sibling_threads,
					   ph->env.nr_sibling_threads))
			fprintf(stderr, "problem building topology\n");
		break;

	default:
		break;
	}

	return 0;
}

static int __cmd_timechart(struct timechart *tchart, const char *output_name)
{
	const struct perf_evsel_str_handler power_tracepoints[] = {
		{ "power:cpu_idle",		process_sample_cpu_idle },
		{ "power:cpu_frequency",	process_sample_cpu_frequency },
		{ "sched:sched_wakeup",		process_sample_sched_wakeup },
		{ "sched:sched_switch",		process_sample_sched_switch },
#ifdef SUPPORT_OLD_POWER_EVENTS
		{ "power:power_start",		process_sample_power_start },
		{ "power:power_end",		process_sample_power_end },
		{ "power:power_frequency",	process_sample_power_frequency },
#endif

		{ "syscalls:sys_enter_read",		process_enter_read },
		{ "syscalls:sys_enter_pread64",		process_enter_read },
		{ "syscalls:sys_enter_readv",		process_enter_read },
		{ "syscalls:sys_enter_preadv",		process_enter_read },
		{ "syscalls:sys_enter_write",		process_enter_write },
		{ "syscalls:sys_enter_pwrite64",	process_enter_write },
		{ "syscalls:sys_enter_writev",		process_enter_write },
		{ "syscalls:sys_enter_pwritev",		process_enter_write },
		{ "syscalls:sys_enter_sync",		process_enter_sync },
		{ "syscalls:sys_enter_sync_file_range",	process_enter_sync },
		{ "syscalls:sys_enter_fsync",		process_enter_sync },
		{ "syscalls:sys_enter_msync",		process_enter_sync },
		{ "syscalls:sys_enter_recvfrom",	process_enter_rx },
		{ "syscalls:sys_enter_recvmmsg",	process_enter_rx },
		{ "syscalls:sys_enter_recvmsg",		process_enter_rx },
		{ "syscalls:sys_enter_sendto",		process_enter_tx },
		{ "syscalls:sys_enter_sendmsg",		process_enter_tx },
		{ "syscalls:sys_enter_sendmmsg",	process_enter_tx },
		{ "syscalls:sys_enter_epoll_pwait",	process_enter_poll },
		{ "syscalls:sys_enter_epoll_wait",	process_enter_poll },
		{ "syscalls:sys_enter_poll",		process_enter_poll },
		{ "syscalls:sys_enter_ppoll",		process_enter_poll },
		{ "syscalls:sys_enter_pselect6",	process_enter_poll },
		{ "syscalls:sys_enter_select",		process_enter_poll },

		{ "syscalls:sys_exit_read",		process_exit_read },
		{ "syscalls:sys_exit_pread64",		process_exit_read },
		{ "syscalls:sys_exit_readv",		process_exit_read },
		{ "syscalls:sys_exit_preadv",		process_exit_read },
		{ "syscalls:sys_exit_write",		process_exit_write },
		{ "syscalls:sys_exit_pwrite64",		process_exit_write },
		{ "syscalls:sys_exit_writev",		process_exit_write },
		{ "syscalls:sys_exit_pwritev",		process_exit_write },
		{ "syscalls:sys_exit_sync",		process_exit_sync },
		{ "syscalls:sys_exit_sync_file_range",	process_exit_sync },
		{ "syscalls:sys_exit_fsync",		process_exit_sync },
		{ "syscalls:sys_exit_msync",		process_exit_sync },
		{ "syscalls:sys_exit_recvfrom",		process_exit_rx },
		{ "syscalls:sys_exit_recvmmsg",		process_exit_rx },
		{ "syscalls:sys_exit_recvmsg",		process_exit_rx },
		{ "syscalls:sys_exit_sendto",		process_exit_tx },
		{ "syscalls:sys_exit_sendmsg",		process_exit_tx },
		{ "syscalls:sys_exit_sendmmsg",		process_exit_tx },
		{ "syscalls:sys_exit_epoll_pwait",	process_exit_poll },
		{ "syscalls:sys_exit_epoll_wait",	process_exit_poll },
		{ "syscalls:sys_exit_poll",		process_exit_poll },
		{ "syscalls:sys_exit_ppoll",		process_exit_poll },
		{ "syscalls:sys_exit_pselect6",		process_exit_poll },
		{ "syscalls:sys_exit_select",		process_exit_poll },
	};
	struct perf_data_file file = {
		.path = input_name,
		.mode = PERF_DATA_MODE_READ,
		.force = tchart->force,
	};

	struct perf_session *session = perf_session__new(&file, false,
							 &tchart->tool);
	int ret = -EINVAL;

	if (session == NULL)
		return -1;

	symbol__init(&session->header.env);

	(void)perf_header__process_sections(&session->header,
					    perf_data_file__fd(session->file),
					    tchart,
					    process_header);

	if (!perf_session__has_traces(session, "timechart record"))
		goto out_delete;

	if (perf_session__set_tracepoints_handlers(session,
						   power_tracepoints)) {
		pr_err("Initializing session tracepoint handlers failed\n");
		goto out_delete;
	}

	ret = perf_session__process_events(session);
	if (ret)
		goto out_delete;

	end_sample_processing(tchart);

	sort_pids(tchart);

	write_svg_file(tchart, output_name);

	pr_info("Written %2.1f seconds of trace to %s.\n",
		(tchart->last_time - tchart->first_time) / 1000000000.0, output_name);
out_delete:
	perf_session__delete(session);
	return ret;
}

static int timechart__io_record(int argc, const char **argv)
{
	unsigned int rec_argc, i;
	const char **rec_argv;
	const char **p;
	char *filter = NULL;

	const char * const common_args[] = {
		"record", "-a", "-R", "-c", "1",
	};
	unsigned int common_args_nr = ARRAY_SIZE(common_args);

	const char * const disk_events[] = {
		"syscalls:sys_enter_read",
		"syscalls:sys_enter_pread64",
		"syscalls:sys_enter_readv",
		"syscalls:sys_enter_preadv",
		"syscalls:sys_enter_write",
		"syscalls:sys_enter_pwrite64",
		"syscalls:sys_enter_writev",
		"syscalls:sys_enter_pwritev",
		"syscalls:sys_enter_sync",
		"syscalls:sys_enter_sync_file_range",
		"syscalls:sys_enter_fsync",
		"syscalls:sys_enter_msync",

		"syscalls:sys_exit_read",
		"syscalls:sys_exit_pread64",
		"syscalls:sys_exit_readv",
		"syscalls:sys_exit_preadv",
		"syscalls:sys_exit_write",
		"syscalls:sys_exit_pwrite64",
		"syscalls:sys_exit_writev",
		"syscalls:sys_exit_pwritev",
		"syscalls:sys_exit_sync",
		"syscalls:sys_exit_sync_file_range",
		"syscalls:sys_exit_fsync",
		"syscalls:sys_exit_msync",
	};
	unsigned int disk_events_nr = ARRAY_SIZE(disk_events);

	const char * const net_events[] = {
		"syscalls:sys_enter_recvfrom",
		"syscalls:sys_enter_recvmmsg",
		"syscalls:sys_enter_recvmsg",
		"syscalls:sys_enter_sendto",
		"syscalls:sys_enter_sendmsg",
		"syscalls:sys_enter_sendmmsg",

		"syscalls:sys_exit_recvfrom",
		"syscalls:sys_exit_recvmmsg",
		"syscalls:sys_exit_recvmsg",
		"syscalls:sys_exit_sendto",
		"syscalls:sys_exit_sendmsg",
		"syscalls:sys_exit_sendmmsg",
	};
	unsigned int net_events_nr = ARRAY_SIZE(net_events);

	const char * const poll_events[] = {
		"syscalls:sys_enter_epoll_pwait",
		"syscalls:sys_enter_epoll_wait",
		"syscalls:sys_enter_poll",
		"syscalls:sys_enter_ppoll",
		"syscalls:sys_enter_pselect6",
		"syscalls:sys_enter_select",

		"syscalls:sys_exit_epoll_pwait",
		"syscalls:sys_exit_epoll_wait",
		"syscalls:sys_exit_poll",
		"syscalls:sys_exit_ppoll",
		"syscalls:sys_exit_pselect6",
		"syscalls:sys_exit_select",
	};
	unsigned int poll_events_nr = ARRAY_SIZE(poll_events);

	rec_argc = common_args_nr +
		disk_events_nr * 4 +
		net_events_nr * 4 +
		poll_events_nr * 4 +
		argc;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	if (asprintf(&filter, "common_pid != %d", getpid()) < 0)
		return -ENOMEM;

	p = rec_argv;
	for (i = 0; i < common_args_nr; i++)
		*p++ = strdup(common_args[i]);

	for (i = 0; i < disk_events_nr; i++) {
		if (!is_valid_tracepoint(disk_events[i])) {
			rec_argc -= 4;
			continue;
		}

		*p++ = "-e";
		*p++ = strdup(disk_events[i]);
		*p++ = "--filter";
		*p++ = filter;
	}
	for (i = 0; i < net_events_nr; i++) {
		if (!is_valid_tracepoint(net_events[i])) {
			rec_argc -= 4;
			continue;
		}

		*p++ = "-e";
		*p++ = strdup(net_events[i]);
		*p++ = "--filter";
		*p++ = filter;
	}
	for (i = 0; i < poll_events_nr; i++) {
		if (!is_valid_tracepoint(poll_events[i])) {
			rec_argc -= 4;
			continue;
		}

		*p++ = "-e";
		*p++ = strdup(poll_events[i]);
		*p++ = "--filter";
		*p++ = filter;
	}

	for (i = 0; i < (unsigned int)argc; i++)
		*p++ = argv[i];

	return cmd_record(rec_argc, rec_argv, NULL);
}


static int timechart__record(struct timechart *tchart, int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;
	const char **p;
	unsigned int record_elems;

	const char * const common_args[] = {
		"record", "-a", "-R", "-c", "1",
	};
	unsigned int common_args_nr = ARRAY_SIZE(common_args);

	const char * const backtrace_args[] = {
		"-g",
	};
	unsigned int backtrace_args_no = ARRAY_SIZE(backtrace_args);

	const char * const power_args[] = {
		"-e", "power:cpu_frequency",
		"-e", "power:cpu_idle",
	};
	unsigned int power_args_nr = ARRAY_SIZE(power_args);

	const char * const old_power_args[] = {
#ifdef SUPPORT_OLD_POWER_EVENTS
		"-e", "power:power_start",
		"-e", "power:power_end",
		"-e", "power:power_frequency",
#endif
	};
	unsigned int old_power_args_nr = ARRAY_SIZE(old_power_args);

	const char * const tasks_args[] = {
		"-e", "sched:sched_wakeup",
		"-e", "sched:sched_switch",
	};
	unsigned int tasks_args_nr = ARRAY_SIZE(tasks_args);

#ifdef SUPPORT_OLD_POWER_EVENTS
	if (!is_valid_tracepoint("power:cpu_idle") &&
	    is_valid_tracepoint("power:power_start")) {
		use_old_power_events = 1;
		power_args_nr = 0;
	} else {
		old_power_args_nr = 0;
	}
#endif

	if (tchart->power_only)
		tasks_args_nr = 0;

	if (tchart->tasks_only) {
		power_args_nr = 0;
		old_power_args_nr = 0;
	}

	if (!tchart->with_backtrace)
		backtrace_args_no = 0;

	record_elems = common_args_nr + tasks_args_nr +
		power_args_nr + old_power_args_nr + backtrace_args_no;

	rec_argc = record_elems + argc;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	p = rec_argv;
	for (i = 0; i < common_args_nr; i++)
		*p++ = strdup(common_args[i]);

	for (i = 0; i < backtrace_args_no; i++)
		*p++ = strdup(backtrace_args[i]);

	for (i = 0; i < tasks_args_nr; i++)
		*p++ = strdup(tasks_args[i]);

	for (i = 0; i < power_args_nr; i++)
		*p++ = strdup(power_args[i]);

	for (i = 0; i < old_power_args_nr; i++)
		*p++ = strdup(old_power_args[i]);

	for (j = 0; j < (unsigned int)argc; j++)
		*p++ = argv[j];

	return cmd_record(rec_argc, rec_argv, NULL);
}

static int
parse_process(const struct option *opt __maybe_unused, const char *arg,
	      int __maybe_unused unset)
{
	if (arg)
		add_process_filter(arg);
	return 0;
}

static int
parse_highlight(const struct option *opt __maybe_unused, const char *arg,
		int __maybe_unused unset)
{
	unsigned long duration = strtoul(arg, NULL, 0);

	if (svg_highlight || svg_highlight_name)
		return -1;

	if (duration)
		svg_highlight = duration;
	else
		svg_highlight_name = strdup(arg);

	return 0;
}

static int
parse_time(const struct option *opt, const char *arg, int __maybe_unused unset)
{
	char unit = 'n';
	u64 *value = opt->value;

	if (sscanf(arg, "%" PRIu64 "%cs", value, &unit) > 0) {
		switch (unit) {
		case 'm':
			*value *= 1000000;
			break;
		case 'u':
			*value *= 1000;
			break;
		case 'n':
			break;
		default:
			return -1;
		}
	}

	return 0;
}

int cmd_timechart(int argc, const char **argv,
		  const char *prefix __maybe_unused)
{
	struct timechart tchart = {
		.tool = {
			.comm		 = process_comm_event,
			.fork		 = process_fork_event,
			.exit		 = process_exit_event,
			.sample		 = process_sample_event,
			.ordered_events	 = true,
		},
		.proc_num = 15,
		.min_time = 1000000,
		.merge_dist = 1000,
	};
	const char *output_name = "output.svg";
	const struct option timechart_options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_STRING('o', "output", &output_name, "file", "output file name"),
	OPT_INTEGER('w', "width", &svg_page_width, "page width"),
	OPT_CALLBACK(0, "highlight", NULL, "duration or task name",
		      "highlight tasks. Pass duration in ns or process name.",
		       parse_highlight),
	OPT_BOOLEAN('P', "power-only", &tchart.power_only, "output power data only"),
	OPT_BOOLEAN('T', "tasks-only", &tchart.tasks_only,
		    "output processes data only"),
	OPT_CALLBACK('p', "process", NULL, "process",
		      "process selector. Pass a pid or process name.",
		       parse_process),
	OPT_STRING(0, "symfs", &symbol_conf.symfs, "directory",
		    "Look for files with symbols relative to this directory"),
	OPT_INTEGER('n', "proc-num", &tchart.proc_num,
		    "min. number of tasks to print"),
	OPT_BOOLEAN('t', "topology", &tchart.topology,
		    "sort CPUs according to topology"),
	OPT_BOOLEAN(0, "io-skip-eagain", &tchart.skip_eagain,
		    "skip EAGAIN errors"),
	OPT_CALLBACK(0, "io-min-time", &tchart.min_time, "time",
		     "all IO faster than min-time will visually appear longer",
		     parse_time),
	OPT_CALLBACK(0, "io-merge-dist", &tchart.merge_dist, "time",
		     "merge events that are merge-dist us apart",
		     parse_time),
	OPT_BOOLEAN('f', "force", &tchart.force, "don't complain, do it"),
	OPT_END()
	};
	const char * const timechart_subcommands[] = { "record", NULL };
	const char *timechart_usage[] = {
		"perf timechart [<options>] {record}",
		NULL
	};

	const struct option timechart_record_options[] = {
	OPT_BOOLEAN('P', "power-only", &tchart.power_only, "output power data only"),
	OPT_BOOLEAN('T', "tasks-only", &tchart.tasks_only,
		    "output processes data only"),
	OPT_BOOLEAN('I', "io-only", &tchart.io_only,
		    "record only IO data"),
	OPT_BOOLEAN('g', "callchain", &tchart.with_backtrace, "record callchain"),
	OPT_END()
	};
	const char * const timechart_record_usage[] = {
		"perf timechart record [<options>]",
		NULL
	};
	argc = parse_options_subcommand(argc, argv, timechart_options, timechart_subcommands,
			timechart_usage, PARSE_OPT_STOP_AT_NON_OPTION);

	if (tchart.power_only && tchart.tasks_only) {
		pr_err("-P and -T options cannot be used at the same time.\n");
		return -1;
	}

	if (argc && !strncmp(argv[0], "rec", 3)) {
		argc = parse_options(argc, argv, timechart_record_options,
				     timechart_record_usage,
				     PARSE_OPT_STOP_AT_NON_OPTION);

		if (tchart.power_only && tchart.tasks_only) {
			pr_err("-P and -T options cannot be used at the same time.\n");
			return -1;
		}

		if (tchart.io_only)
			return timechart__io_record(argc, argv);
		else
			return timechart__record(&tchart, argc, argv);
	} else if (argc)
		usage_with_options(timechart_usage, timechart_options);

	setup_pager();

	return __cmd_timechart(&tchart, output_name);
}
