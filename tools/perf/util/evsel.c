/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include <byteswap.h>
#include "asm/bug.h"
#include "evsel.h"
#include "evlist.h"
#include "util.h"
#include "cpumap.h"
#include "thread_map.h"
#include "target.h"
#include "../../include/linux/perf_event.h"

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))
#define GROUP_FD(group_fd, cpu) (*(int *)xyarray__entry(group_fd, cpu, 0))

int __perf_evsel__sample_size(u64 sample_type)
{
	u64 mask = sample_type & PERF_SAMPLE_MASK;
	int size = 0;
	int i;

	for (i = 0; i < 64; i++) {
		if (mask & (1ULL << i))
			size++;
	}

	size *= sizeof(u64);

	return size;
}

void hists__init(struct hists *hists)
{
	memset(hists, 0, sizeof(*hists));
	hists->entries_in_array[0] = hists->entries_in_array[1] = RB_ROOT;
	hists->entries_in = &hists->entries_in_array[0];
	hists->entries_collapsed = RB_ROOT;
	hists->entries = RB_ROOT;
	pthread_mutex_init(&hists->lock, NULL);
}

void perf_evsel__init(struct perf_evsel *evsel,
		      struct perf_event_attr *attr, int idx)
{
	evsel->idx	   = idx;
	evsel->attr	   = *attr;
	INIT_LIST_HEAD(&evsel->node);
	hists__init(&evsel->hists);
}

struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr, int idx)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL)
		perf_evsel__init(evsel, attr, idx);

	return evsel;
}

static const char *perf_evsel__hw_names[PERF_COUNT_HW_MAX] = {
	"cycles",
	"instructions",
	"cache-references",
	"cache-misses",
	"branches",
	"branch-misses",
	"bus-cycles",
	"stalled-cycles-frontend",
	"stalled-cycles-backend",
	"ref-cycles",
};

const char *__perf_evsel__hw_name(u64 config)
{
	if (config < PERF_COUNT_HW_MAX && perf_evsel__hw_names[config])
		return perf_evsel__hw_names[config];

	return "unknown-hardware";
}

static int perf_evsel__hw_name(struct perf_evsel *evsel, char *bf, size_t size)
{
	int colon = 0;
	struct perf_event_attr *attr = &evsel->attr;
	int r = scnprintf(bf, size, "%s", __perf_evsel__hw_name(attr->config));
	bool exclude_guest_default = false;

#define MOD_PRINT(context, mod)	do {					\
		if (!attr->exclude_##context) {				\
			if (!colon) colon = r++;			\
			r += scnprintf(bf + r, size - r, "%c", mod);	\
		} } while(0)

	if (attr->exclude_kernel || attr->exclude_user || attr->exclude_hv) {
		MOD_PRINT(kernel, 'k');
		MOD_PRINT(user, 'u');
		MOD_PRINT(hv, 'h');
		exclude_guest_default = true;
	}

	if (attr->precise_ip) {
		if (!colon)
			colon = r++;
		r += scnprintf(bf + r, size - r, "%.*s", attr->precise_ip, "ppp");
		exclude_guest_default = true;
	}

	if (attr->exclude_host || attr->exclude_guest == exclude_guest_default) {
		MOD_PRINT(host, 'H');
		MOD_PRINT(guest, 'G');
	}
#undef MOD_PRINT
	if (colon)
		bf[colon] = ':';
	return r;
}

int perf_evsel__name(struct perf_evsel *evsel, char *bf, size_t size)
{
	int ret;

	switch (evsel->attr.type) {
	case PERF_TYPE_RAW:
		ret = scnprintf(bf, size, "raw 0x%" PRIx64, evsel->attr.config);
		break;

	case PERF_TYPE_HARDWARE:
		ret = perf_evsel__hw_name(evsel, bf, size);
		break;
	default:
		/*
		 * FIXME
 		 *
		 * This is the minimal perf_evsel__name so that we can
		 * reconstruct event names taking into account event modifiers.
		 *
		 * The old event_name uses it now for raw anr hw events, so that
		 * we don't drag all the parsing stuff into the python binding.
		 *
		 * On the next devel cycle the rest of the event naming will be
		 * brought here.
 		 */
		return 0;
	}

	return ret;
}

void perf_evsel__config(struct perf_evsel *evsel, struct perf_record_opts *opts,
			struct perf_evsel *first)
{
	struct perf_event_attr *attr = &evsel->attr;
	int track = !evsel->idx; /* only the first counter needs these */

	attr->disabled = 1;
	attr->sample_id_all = opts->sample_id_all_missing ? 0 : 1;
	attr->inherit	    = !opts->no_inherit;
	attr->read_format   = PERF_FORMAT_TOTAL_TIME_ENABLED |
			      PERF_FORMAT_TOTAL_TIME_RUNNING |
			      PERF_FORMAT_ID;

	attr->sample_type  |= PERF_SAMPLE_IP | PERF_SAMPLE_TID;

	/*
	 * We default some events to a 1 default interval. But keep
	 * it a weak assumption overridable by the user.
	 */
	if (!attr->sample_period || (opts->user_freq != UINT_MAX &&
				     opts->user_interval != ULLONG_MAX)) {
		if (opts->freq) {
			attr->sample_type	|= PERF_SAMPLE_PERIOD;
			attr->freq		= 1;
			attr->sample_freq	= opts->freq;
		} else {
			attr->sample_period = opts->default_interval;
		}
	}

	if (opts->no_samples)
		attr->sample_freq = 0;

	if (opts->inherit_stat)
		attr->inherit_stat = 1;

	if (opts->sample_address) {
		attr->sample_type	|= PERF_SAMPLE_ADDR;
		attr->mmap_data = track;
	}

	if (opts->call_graph)
		attr->sample_type	|= PERF_SAMPLE_CALLCHAIN;

	if (perf_target__has_cpu(&opts->target))
		attr->sample_type	|= PERF_SAMPLE_CPU;

	if (opts->period)
		attr->sample_type	|= PERF_SAMPLE_PERIOD;

	if (!opts->sample_id_all_missing &&
	    (opts->sample_time || !opts->no_inherit ||
	     perf_target__has_cpu(&opts->target)))
		attr->sample_type	|= PERF_SAMPLE_TIME;

	if (opts->raw_samples) {
		attr->sample_type	|= PERF_SAMPLE_TIME;
		attr->sample_type	|= PERF_SAMPLE_RAW;
		attr->sample_type	|= PERF_SAMPLE_CPU;
	}

	if (opts->no_delay) {
		attr->watermark = 0;
		attr->wakeup_events = 1;
	}
	if (opts->branch_stack) {
		attr->sample_type	|= PERF_SAMPLE_BRANCH_STACK;
		attr->branch_sample_type = opts->branch_stack;
	}

	attr->mmap = track;
	attr->comm = track;

	if (perf_target__none(&opts->target) &&
	    (!opts->group || evsel == first)) {
		attr->enable_on_exec = 1;
	}
}

int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	int cpu, thread;
	evsel->fd = xyarray__new(ncpus, nthreads, sizeof(int));

	if (evsel->fd) {
		for (cpu = 0; cpu < ncpus; cpu++) {
			for (thread = 0; thread < nthreads; thread++) {
				FD(evsel, cpu, thread) = -1;
			}
		}
	}

	return evsel->fd != NULL ? 0 : -ENOMEM;
}

int perf_evsel__alloc_id(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	evsel->sample_id = xyarray__new(ncpus, nthreads, sizeof(struct perf_sample_id));
	if (evsel->sample_id == NULL)
		return -ENOMEM;

	evsel->id = zalloc(ncpus * nthreads * sizeof(u64));
	if (evsel->id == NULL) {
		xyarray__delete(evsel->sample_id);
		evsel->sample_id = NULL;
		return -ENOMEM;
	}

	return 0;
}

int perf_evsel__alloc_counts(struct perf_evsel *evsel, int ncpus)
{
	evsel->counts = zalloc((sizeof(*evsel->counts) +
				(ncpus * sizeof(struct perf_counts_values))));
	return evsel->counts != NULL ? 0 : -ENOMEM;
}

void perf_evsel__free_fd(struct perf_evsel *evsel)
{
	xyarray__delete(evsel->fd);
	evsel->fd = NULL;
}

void perf_evsel__free_id(struct perf_evsel *evsel)
{
	xyarray__delete(evsel->sample_id);
	evsel->sample_id = NULL;
	free(evsel->id);
	evsel->id = NULL;
}

void perf_evsel__close_fd(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	int cpu, thread;

	for (cpu = 0; cpu < ncpus; cpu++)
		for (thread = 0; thread < nthreads; ++thread) {
			close(FD(evsel, cpu, thread));
			FD(evsel, cpu, thread) = -1;
		}
}

void perf_evsel__exit(struct perf_evsel *evsel)
{
	assert(list_empty(&evsel->node));
	xyarray__delete(evsel->fd);
	xyarray__delete(evsel->sample_id);
	free(evsel->id);
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	perf_evsel__exit(evsel);
	close_cgroup(evsel->cgrp);
	free(evsel->name);
	free(evsel);
}

int __perf_evsel__read_on_cpu(struct perf_evsel *evsel,
			      int cpu, int thread, bool scale)
{
	struct perf_counts_values count;
	size_t nv = scale ? 3 : 1;

	if (FD(evsel, cpu, thread) < 0)
		return -EINVAL;

	if (evsel->counts == NULL && perf_evsel__alloc_counts(evsel, cpu + 1) < 0)
		return -ENOMEM;

	if (readn(FD(evsel, cpu, thread), &count, nv * sizeof(u64)) < 0)
		return -errno;

	if (scale) {
		if (count.run == 0)
			count.val = 0;
		else if (count.run < count.ena)
			count.val = (u64)((double)count.val * count.ena / count.run + 0.5);
	} else
		count.ena = count.run = 0;

	evsel->counts->cpu[cpu] = count;
	return 0;
}

int __perf_evsel__read(struct perf_evsel *evsel,
		       int ncpus, int nthreads, bool scale)
{
	size_t nv = scale ? 3 : 1;
	int cpu, thread;
	struct perf_counts_values *aggr = &evsel->counts->aggr, count;

	aggr->val = aggr->ena = aggr->run = 0;

	for (cpu = 0; cpu < ncpus; cpu++) {
		for (thread = 0; thread < nthreads; thread++) {
			if (FD(evsel, cpu, thread) < 0)
				continue;

			if (readn(FD(evsel, cpu, thread),
				  &count, nv * sizeof(u64)) < 0)
				return -errno;

			aggr->val += count.val;
			if (scale) {
				aggr->ena += count.ena;
				aggr->run += count.run;
			}
		}
	}

	evsel->counts->scaled = 0;
	if (scale) {
		if (aggr->run == 0) {
			evsel->counts->scaled = -1;
			aggr->val = 0;
			return 0;
		}

		if (aggr->run < aggr->ena) {
			evsel->counts->scaled = 1;
			aggr->val = (u64)((double)aggr->val * aggr->ena / aggr->run + 0.5);
		}
	} else
		aggr->ena = aggr->run = 0;

	return 0;
}

static int __perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
			      struct thread_map *threads, bool group,
			      struct xyarray *group_fds)
{
	int cpu, thread;
	unsigned long flags = 0;
	int pid = -1, err;

	if (evsel->fd == NULL &&
	    perf_evsel__alloc_fd(evsel, cpus->nr, threads->nr) < 0)
		return -ENOMEM;

	if (evsel->cgrp) {
		flags = PERF_FLAG_PID_CGROUP;
		pid = evsel->cgrp->fd;
	}

	for (cpu = 0; cpu < cpus->nr; cpu++) {
		int group_fd = group_fds ? GROUP_FD(group_fds, cpu) : -1;

		for (thread = 0; thread < threads->nr; thread++) {

			if (!evsel->cgrp)
				pid = threads->map[thread];

			FD(evsel, cpu, thread) = sys_perf_event_open(&evsel->attr,
								     pid,
								     cpus->map[cpu],
								     group_fd, flags);
			if (FD(evsel, cpu, thread) < 0) {
				err = -errno;
				goto out_close;
			}

			if (group && group_fd == -1)
				group_fd = FD(evsel, cpu, thread);
		}
	}

	return 0;

out_close:
	do {
		while (--thread >= 0) {
			close(FD(evsel, cpu, thread));
			FD(evsel, cpu, thread) = -1;
		}
		thread = threads->nr;
	} while (--cpu >= 0);
	return err;
}

void perf_evsel__close(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	if (evsel->fd == NULL)
		return;

	perf_evsel__close_fd(evsel, ncpus, nthreads);
	perf_evsel__free_fd(evsel);
	evsel->fd = NULL;
}

static struct {
	struct cpu_map map;
	int cpus[1];
} empty_cpu_map = {
	.map.nr	= 1,
	.cpus	= { -1, },
};

static struct {
	struct thread_map map;
	int threads[1];
} empty_thread_map = {
	.map.nr	 = 1,
	.threads = { -1, },
};

int perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
		     struct thread_map *threads, bool group,
		     struct xyarray *group_fd)
{
	if (cpus == NULL) {
		/* Work around old compiler warnings about strict aliasing */
		cpus = &empty_cpu_map.map;
	}

	if (threads == NULL)
		threads = &empty_thread_map.map;

	return __perf_evsel__open(evsel, cpus, threads, group, group_fd);
}

int perf_evsel__open_per_cpu(struct perf_evsel *evsel,
			     struct cpu_map *cpus, bool group,
			     struct xyarray *group_fd)
{
	return __perf_evsel__open(evsel, cpus, &empty_thread_map.map, group,
				  group_fd);
}

int perf_evsel__open_per_thread(struct perf_evsel *evsel,
				struct thread_map *threads, bool group,
				struct xyarray *group_fd)
{
	return __perf_evsel__open(evsel, &empty_cpu_map.map, threads, group,
				  group_fd);
}

static int perf_event__parse_id_sample(const union perf_event *event, u64 type,
				       struct perf_sample *sample,
				       bool swapped)
{
	const u64 *array = event->sample.array;
	union u64_swap u;

	array += ((event->header.size -
		   sizeof(event->header)) / sizeof(u64)) - 1;

	if (type & PERF_SAMPLE_CPU) {
		u.val64 = *array;
		if (swapped) {
			/* undo swap of u64, then swap on individual u32s */
			u.val64 = bswap_64(u.val64);
			u.val32[0] = bswap_32(u.val32[0]);
		}

		sample->cpu = u.val32[0];
		array--;
	}

	if (type & PERF_SAMPLE_STREAM_ID) {
		sample->stream_id = *array;
		array--;
	}

	if (type & PERF_SAMPLE_ID) {
		sample->id = *array;
		array--;
	}

	if (type & PERF_SAMPLE_TIME) {
		sample->time = *array;
		array--;
	}

	if (type & PERF_SAMPLE_TID) {
		u.val64 = *array;
		if (swapped) {
			/* undo swap of u64, then swap on individual u32s */
			u.val64 = bswap_64(u.val64);
			u.val32[0] = bswap_32(u.val32[0]);
			u.val32[1] = bswap_32(u.val32[1]);
		}

		sample->pid = u.val32[0];
		sample->tid = u.val32[1];
	}

	return 0;
}

static bool sample_overlap(const union perf_event *event,
			   const void *offset, u64 size)
{
	const void *base = event;

	if (offset + size > base + event->header.size)
		return true;

	return false;
}

int perf_event__parse_sample(const union perf_event *event, u64 type,
			     int sample_size, bool sample_id_all,
			     struct perf_sample *data, bool swapped)
{
	const u64 *array;

	/*
	 * used for cross-endian analysis. See git commit 65014ab3
	 * for why this goofiness is needed.
	 */
	union u64_swap u;

	memset(data, 0, sizeof(*data));
	data->cpu = data->pid = data->tid = -1;
	data->stream_id = data->id = data->time = -1ULL;
	data->period = 1;

	if (event->header.type != PERF_RECORD_SAMPLE) {
		if (!sample_id_all)
			return 0;
		return perf_event__parse_id_sample(event, type, data, swapped);
	}

	array = event->sample.array;

	if (sample_size + sizeof(event->header) > event->header.size)
		return -EFAULT;

	if (type & PERF_SAMPLE_IP) {
		data->ip = event->ip.ip;
		array++;
	}

	if (type & PERF_SAMPLE_TID) {
		u.val64 = *array;
		if (swapped) {
			/* undo swap of u64, then swap on individual u32s */
			u.val64 = bswap_64(u.val64);
			u.val32[0] = bswap_32(u.val32[0]);
			u.val32[1] = bswap_32(u.val32[1]);
		}

		data->pid = u.val32[0];
		data->tid = u.val32[1];
		array++;
	}

	if (type & PERF_SAMPLE_TIME) {
		data->time = *array;
		array++;
	}

	data->addr = 0;
	if (type & PERF_SAMPLE_ADDR) {
		data->addr = *array;
		array++;
	}

	data->id = -1ULL;
	if (type & PERF_SAMPLE_ID) {
		data->id = *array;
		array++;
	}

	if (type & PERF_SAMPLE_STREAM_ID) {
		data->stream_id = *array;
		array++;
	}

	if (type & PERF_SAMPLE_CPU) {

		u.val64 = *array;
		if (swapped) {
			/* undo swap of u64, then swap on individual u32s */
			u.val64 = bswap_64(u.val64);
			u.val32[0] = bswap_32(u.val32[0]);
		}

		data->cpu = u.val32[0];
		array++;
	}

	if (type & PERF_SAMPLE_PERIOD) {
		data->period = *array;
		array++;
	}

	if (type & PERF_SAMPLE_READ) {
		fprintf(stderr, "PERF_SAMPLE_READ is unsupported for now\n");
		return -1;
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		if (sample_overlap(event, array, sizeof(data->callchain->nr)))
			return -EFAULT;

		data->callchain = (struct ip_callchain *)array;

		if (sample_overlap(event, array, data->callchain->nr))
			return -EFAULT;

		array += 1 + data->callchain->nr;
	}

	if (type & PERF_SAMPLE_RAW) {
		const u64 *pdata;

		u.val64 = *array;
		if (WARN_ONCE(swapped,
			      "Endianness of raw data not corrected!\n")) {
			/* undo swap of u64, then swap on individual u32s */
			u.val64 = bswap_64(u.val64);
			u.val32[0] = bswap_32(u.val32[0]);
			u.val32[1] = bswap_32(u.val32[1]);
		}

		if (sample_overlap(event, array, sizeof(u32)))
			return -EFAULT;

		data->raw_size = u.val32[0];
		pdata = (void *) array + sizeof(u32);

		if (sample_overlap(event, pdata, data->raw_size))
			return -EFAULT;

		data->raw_data = (void *) pdata;

		array = (void *)array + data->raw_size + sizeof(u32);
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		u64 sz;

		data->branch_stack = (struct branch_stack *)array;
		array++; /* nr */

		sz = data->branch_stack->nr * sizeof(struct branch_entry);
		sz /= sizeof(u64);
		array += sz;
	}
	return 0;
}

int perf_event__synthesize_sample(union perf_event *event, u64 type,
				  const struct perf_sample *sample,
				  bool swapped)
{
	u64 *array;

	/*
	 * used for cross-endian analysis. See git commit 65014ab3
	 * for why this goofiness is needed.
	 */
	union u64_swap u;

	array = event->sample.array;

	if (type & PERF_SAMPLE_IP) {
		event->ip.ip = sample->ip;
		array++;
	}

	if (type & PERF_SAMPLE_TID) {
		u.val32[0] = sample->pid;
		u.val32[1] = sample->tid;
		if (swapped) {
			/*
			 * Inverse of what is done in perf_event__parse_sample
			 */
			u.val32[0] = bswap_32(u.val32[0]);
			u.val32[1] = bswap_32(u.val32[1]);
			u.val64 = bswap_64(u.val64);
		}

		*array = u.val64;
		array++;
	}

	if (type & PERF_SAMPLE_TIME) {
		*array = sample->time;
		array++;
	}

	if (type & PERF_SAMPLE_ADDR) {
		*array = sample->addr;
		array++;
	}

	if (type & PERF_SAMPLE_ID) {
		*array = sample->id;
		array++;
	}

	if (type & PERF_SAMPLE_STREAM_ID) {
		*array = sample->stream_id;
		array++;
	}

	if (type & PERF_SAMPLE_CPU) {
		u.val32[0] = sample->cpu;
		if (swapped) {
			/*
			 * Inverse of what is done in perf_event__parse_sample
			 */
			u.val32[0] = bswap_32(u.val32[0]);
			u.val64 = bswap_64(u.val64);
		}
		*array = u.val64;
		array++;
	}

	if (type & PERF_SAMPLE_PERIOD) {
		*array = sample->period;
		array++;
	}

	return 0;
}
