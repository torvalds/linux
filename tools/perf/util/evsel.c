/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include <byteswap.h>
#include <linux/bitops.h>
#include <api/fs/debugfs.h>
#include <traceevent/event-parse.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <sys/resource.h>
#include "asm/bug.h"
#include "evsel.h"
#include "evlist.h"
#include "util.h"
#include "cpumap.h"
#include "thread_map.h"
#include "target.h"
#include "perf_regs.h"
#include "debug.h"
#include "trace-event.h"

static struct {
	bool sample_id_all;
	bool exclude_guest;
	bool mmap2;
} perf_missing_features;

#define FD(e, x, y) (*(int *)xyarray__entry(e->fd, x, y))

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

/**
 * __perf_evsel__calc_id_pos - calculate id_pos.
 * @sample_type: sample type
 *
 * This function returns the position of the event id (PERF_SAMPLE_ID or
 * PERF_SAMPLE_IDENTIFIER) in a sample event i.e. in the array of struct
 * sample_event.
 */
static int __perf_evsel__calc_id_pos(u64 sample_type)
{
	int idx = 0;

	if (sample_type & PERF_SAMPLE_IDENTIFIER)
		return 0;

	if (!(sample_type & PERF_SAMPLE_ID))
		return -1;

	if (sample_type & PERF_SAMPLE_IP)
		idx += 1;

	if (sample_type & PERF_SAMPLE_TID)
		idx += 1;

	if (sample_type & PERF_SAMPLE_TIME)
		idx += 1;

	if (sample_type & PERF_SAMPLE_ADDR)
		idx += 1;

	return idx;
}

/**
 * __perf_evsel__calc_is_pos - calculate is_pos.
 * @sample_type: sample type
 *
 * This function returns the position (counting backwards) of the event id
 * (PERF_SAMPLE_ID or PERF_SAMPLE_IDENTIFIER) in a non-sample event i.e. if
 * sample_id_all is used there is an id sample appended to non-sample events.
 */
static int __perf_evsel__calc_is_pos(u64 sample_type)
{
	int idx = 1;

	if (sample_type & PERF_SAMPLE_IDENTIFIER)
		return 1;

	if (!(sample_type & PERF_SAMPLE_ID))
		return -1;

	if (sample_type & PERF_SAMPLE_CPU)
		idx += 1;

	if (sample_type & PERF_SAMPLE_STREAM_ID)
		idx += 1;

	return idx;
}

void perf_evsel__calc_id_pos(struct perf_evsel *evsel)
{
	evsel->id_pos = __perf_evsel__calc_id_pos(evsel->attr.sample_type);
	evsel->is_pos = __perf_evsel__calc_is_pos(evsel->attr.sample_type);
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

void __perf_evsel__set_sample_bit(struct perf_evsel *evsel,
				  enum perf_event_sample_format bit)
{
	if (!(evsel->attr.sample_type & bit)) {
		evsel->attr.sample_type |= bit;
		evsel->sample_size += sizeof(u64);
		perf_evsel__calc_id_pos(evsel);
	}
}

void __perf_evsel__reset_sample_bit(struct perf_evsel *evsel,
				    enum perf_event_sample_format bit)
{
	if (evsel->attr.sample_type & bit) {
		evsel->attr.sample_type &= ~bit;
		evsel->sample_size -= sizeof(u64);
		perf_evsel__calc_id_pos(evsel);
	}
}

void perf_evsel__set_sample_id(struct perf_evsel *evsel,
			       bool can_sample_identifier)
{
	if (can_sample_identifier) {
		perf_evsel__reset_sample_bit(evsel, ID);
		perf_evsel__set_sample_bit(evsel, IDENTIFIER);
	} else {
		perf_evsel__set_sample_bit(evsel, ID);
	}
	evsel->attr.read_format |= PERF_FORMAT_ID;
}

void perf_evsel__init(struct perf_evsel *evsel,
		      struct perf_event_attr *attr, int idx)
{
	evsel->idx	   = idx;
	evsel->attr	   = *attr;
	evsel->leader	   = evsel;
	evsel->unit	   = "";
	evsel->scale	   = 1.0;
	INIT_LIST_HEAD(&evsel->node);
	hists__init(&evsel->hists);
	evsel->sample_size = __perf_evsel__sample_size(attr->sample_type);
	perf_evsel__calc_id_pos(evsel);
}

struct perf_evsel *perf_evsel__new_idx(struct perf_event_attr *attr, int idx)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL)
		perf_evsel__init(evsel, attr, idx);

	return evsel;
}

struct perf_evsel *perf_evsel__newtp_idx(const char *sys, const char *name, int idx)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL) {
		struct perf_event_attr attr = {
			.type	       = PERF_TYPE_TRACEPOINT,
			.sample_type   = (PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
					  PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD),
		};

		if (asprintf(&evsel->name, "%s:%s", sys, name) < 0)
			goto out_free;

		evsel->tp_format = trace_event__tp_format(sys, name);
		if (evsel->tp_format == NULL)
			goto out_free;

		event_attr_init(&attr);
		attr.config = evsel->tp_format->id;
		attr.sample_period = 1;
		perf_evsel__init(evsel, &attr, idx);
	}

	return evsel;

out_free:
	zfree(&evsel->name);
	free(evsel);
	return NULL;
}

const char *perf_evsel__hw_names[PERF_COUNT_HW_MAX] = {
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

static const char *__perf_evsel__hw_name(u64 config)
{
	if (config < PERF_COUNT_HW_MAX && perf_evsel__hw_names[config])
		return perf_evsel__hw_names[config];

	return "unknown-hardware";
}

static int perf_evsel__add_modifiers(struct perf_evsel *evsel, char *bf, size_t size)
{
	int colon = 0, r = 0;
	struct perf_event_attr *attr = &evsel->attr;
	bool exclude_guest_default = false;

#define MOD_PRINT(context, mod)	do {					\
		if (!attr->exclude_##context) {				\
			if (!colon) colon = ++r;			\
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
			colon = ++r;
		r += scnprintf(bf + r, size - r, "%.*s", attr->precise_ip, "ppp");
		exclude_guest_default = true;
	}

	if (attr->exclude_host || attr->exclude_guest == exclude_guest_default) {
		MOD_PRINT(host, 'H');
		MOD_PRINT(guest, 'G');
	}
#undef MOD_PRINT
	if (colon)
		bf[colon - 1] = ':';
	return r;
}

static int perf_evsel__hw_name(struct perf_evsel *evsel, char *bf, size_t size)
{
	int r = scnprintf(bf, size, "%s", __perf_evsel__hw_name(evsel->attr.config));
	return r + perf_evsel__add_modifiers(evsel, bf + r, size - r);
}

const char *perf_evsel__sw_names[PERF_COUNT_SW_MAX] = {
	"cpu-clock",
	"task-clock",
	"page-faults",
	"context-switches",
	"cpu-migrations",
	"minor-faults",
	"major-faults",
	"alignment-faults",
	"emulation-faults",
	"dummy",
};

static const char *__perf_evsel__sw_name(u64 config)
{
	if (config < PERF_COUNT_SW_MAX && perf_evsel__sw_names[config])
		return perf_evsel__sw_names[config];
	return "unknown-software";
}

static int perf_evsel__sw_name(struct perf_evsel *evsel, char *bf, size_t size)
{
	int r = scnprintf(bf, size, "%s", __perf_evsel__sw_name(evsel->attr.config));
	return r + perf_evsel__add_modifiers(evsel, bf + r, size - r);
}

static int __perf_evsel__bp_name(char *bf, size_t size, u64 addr, u64 type)
{
	int r;

	r = scnprintf(bf, size, "mem:0x%" PRIx64 ":", addr);

	if (type & HW_BREAKPOINT_R)
		r += scnprintf(bf + r, size - r, "r");

	if (type & HW_BREAKPOINT_W)
		r += scnprintf(bf + r, size - r, "w");

	if (type & HW_BREAKPOINT_X)
		r += scnprintf(bf + r, size - r, "x");

	return r;
}

static int perf_evsel__bp_name(struct perf_evsel *evsel, char *bf, size_t size)
{
	struct perf_event_attr *attr = &evsel->attr;
	int r = __perf_evsel__bp_name(bf, size, attr->bp_addr, attr->bp_type);
	return r + perf_evsel__add_modifiers(evsel, bf + r, size - r);
}

const char *perf_evsel__hw_cache[PERF_COUNT_HW_CACHE_MAX]
				[PERF_EVSEL__MAX_ALIASES] = {
 { "L1-dcache",	"l1-d",		"l1d",		"L1-data",		},
 { "L1-icache",	"l1-i",		"l1i",		"L1-instruction",	},
 { "LLC",	"L2",							},
 { "dTLB",	"d-tlb",	"Data-TLB",				},
 { "iTLB",	"i-tlb",	"Instruction-TLB",			},
 { "branch",	"branches",	"bpu",		"btb",		"bpc",	},
 { "node",								},
};

const char *perf_evsel__hw_cache_op[PERF_COUNT_HW_CACHE_OP_MAX]
				   [PERF_EVSEL__MAX_ALIASES] = {
 { "load",	"loads",	"read",					},
 { "store",	"stores",	"write",				},
 { "prefetch",	"prefetches",	"speculative-read", "speculative-load",	},
};

const char *perf_evsel__hw_cache_result[PERF_COUNT_HW_CACHE_RESULT_MAX]
				       [PERF_EVSEL__MAX_ALIASES] = {
 { "refs",	"Reference",	"ops",		"access",		},
 { "misses",	"miss",							},
};

#define C(x)		PERF_COUNT_HW_CACHE_##x
#define CACHE_READ	(1 << C(OP_READ))
#define CACHE_WRITE	(1 << C(OP_WRITE))
#define CACHE_PREFETCH	(1 << C(OP_PREFETCH))
#define COP(x)		(1 << x)

/*
 * cache operartion stat
 * L1I : Read and prefetch only
 * ITLB and BPU : Read-only
 */
static unsigned long perf_evsel__hw_cache_stat[C(MAX)] = {
 [C(L1D)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(L1I)]	= (CACHE_READ | CACHE_PREFETCH),
 [C(LL)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(DTLB)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(ITLB)]	= (CACHE_READ),
 [C(BPU)]	= (CACHE_READ),
 [C(NODE)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
};

bool perf_evsel__is_cache_op_valid(u8 type, u8 op)
{
	if (perf_evsel__hw_cache_stat[type] & COP(op))
		return true;	/* valid */
	else
		return false;	/* invalid */
}

int __perf_evsel__hw_cache_type_op_res_name(u8 type, u8 op, u8 result,
					    char *bf, size_t size)
{
	if (result) {
		return scnprintf(bf, size, "%s-%s-%s", perf_evsel__hw_cache[type][0],
				 perf_evsel__hw_cache_op[op][0],
				 perf_evsel__hw_cache_result[result][0]);
	}

	return scnprintf(bf, size, "%s-%s", perf_evsel__hw_cache[type][0],
			 perf_evsel__hw_cache_op[op][1]);
}

static int __perf_evsel__hw_cache_name(u64 config, char *bf, size_t size)
{
	u8 op, result, type = (config >>  0) & 0xff;
	const char *err = "unknown-ext-hardware-cache-type";

	if (type > PERF_COUNT_HW_CACHE_MAX)
		goto out_err;

	op = (config >>  8) & 0xff;
	err = "unknown-ext-hardware-cache-op";
	if (op > PERF_COUNT_HW_CACHE_OP_MAX)
		goto out_err;

	result = (config >> 16) & 0xff;
	err = "unknown-ext-hardware-cache-result";
	if (result > PERF_COUNT_HW_CACHE_RESULT_MAX)
		goto out_err;

	err = "invalid-cache";
	if (!perf_evsel__is_cache_op_valid(type, op))
		goto out_err;

	return __perf_evsel__hw_cache_type_op_res_name(type, op, result, bf, size);
out_err:
	return scnprintf(bf, size, "%s", err);
}

static int perf_evsel__hw_cache_name(struct perf_evsel *evsel, char *bf, size_t size)
{
	int ret = __perf_evsel__hw_cache_name(evsel->attr.config, bf, size);
	return ret + perf_evsel__add_modifiers(evsel, bf + ret, size - ret);
}

static int perf_evsel__raw_name(struct perf_evsel *evsel, char *bf, size_t size)
{
	int ret = scnprintf(bf, size, "raw 0x%" PRIx64, evsel->attr.config);
	return ret + perf_evsel__add_modifiers(evsel, bf + ret, size - ret);
}

const char *perf_evsel__name(struct perf_evsel *evsel)
{
	char bf[128];

	if (evsel->name)
		return evsel->name;

	switch (evsel->attr.type) {
	case PERF_TYPE_RAW:
		perf_evsel__raw_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_HARDWARE:
		perf_evsel__hw_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_HW_CACHE:
		perf_evsel__hw_cache_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_SOFTWARE:
		perf_evsel__sw_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_TRACEPOINT:
		scnprintf(bf, sizeof(bf), "%s", "unknown tracepoint");
		break;

	case PERF_TYPE_BREAKPOINT:
		perf_evsel__bp_name(evsel, bf, sizeof(bf));
		break;

	default:
		scnprintf(bf, sizeof(bf), "unknown attr type: %d",
			  evsel->attr.type);
		break;
	}

	evsel->name = strdup(bf);

	return evsel->name ?: "unknown";
}

const char *perf_evsel__group_name(struct perf_evsel *evsel)
{
	return evsel->group_name ?: "anon group";
}

int perf_evsel__group_desc(struct perf_evsel *evsel, char *buf, size_t size)
{
	int ret;
	struct perf_evsel *pos;
	const char *group_name = perf_evsel__group_name(evsel);

	ret = scnprintf(buf, size, "%s", group_name);

	ret += scnprintf(buf + ret, size - ret, " { %s",
			 perf_evsel__name(evsel));

	for_each_group_member(pos, evsel)
		ret += scnprintf(buf + ret, size - ret, ", %s",
				 perf_evsel__name(pos));

	ret += scnprintf(buf + ret, size - ret, " }");

	return ret;
}

/*
 * The enable_on_exec/disabled value strategy:
 *
 *  1) For any type of traced program:
 *    - all independent events and group leaders are disabled
 *    - all group members are enabled
 *
 *     Group members are ruled by group leaders. They need to
 *     be enabled, because the group scheduling relies on that.
 *
 *  2) For traced programs executed by perf:
 *     - all independent events and group leaders have
 *       enable_on_exec set
 *     - we don't specifically enable or disable any event during
 *       the record command
 *
 *     Independent events and group leaders are initially disabled
 *     and get enabled by exec. Group members are ruled by group
 *     leaders as stated in 1).
 *
 *  3) For traced programs attached by perf (pid/tid):
 *     - we specifically enable or disable all events during
 *       the record command
 *
 *     When attaching events to already running traced we
 *     enable/disable events specifically, as there's no
 *     initial traced exec call.
 */
void perf_evsel__config(struct perf_evsel *evsel, struct record_opts *opts)
{
	struct perf_evsel *leader = evsel->leader;
	struct perf_event_attr *attr = &evsel->attr;
	int track = !evsel->idx; /* only the first counter needs these */
	bool per_cpu = opts->target.default_per_cpu && !opts->target.per_thread;

	attr->sample_id_all = perf_missing_features.sample_id_all ? 0 : 1;
	attr->inherit	    = !opts->no_inherit;

	perf_evsel__set_sample_bit(evsel, IP);
	perf_evsel__set_sample_bit(evsel, TID);

	if (evsel->sample_read) {
		perf_evsel__set_sample_bit(evsel, READ);

		/*
		 * We need ID even in case of single event, because
		 * PERF_SAMPLE_READ process ID specific data.
		 */
		perf_evsel__set_sample_id(evsel, false);

		/*
		 * Apply group format only if we belong to group
		 * with more than one members.
		 */
		if (leader->nr_members > 1) {
			attr->read_format |= PERF_FORMAT_GROUP;
			attr->inherit = 0;
		}
	}

	/*
	 * We default some events to a 1 default interval. But keep
	 * it a weak assumption overridable by the user.
	 */
	if (!attr->sample_period || (opts->user_freq != UINT_MAX &&
				     opts->user_interval != ULLONG_MAX)) {
		if (opts->freq) {
			perf_evsel__set_sample_bit(evsel, PERIOD);
			attr->freq		= 1;
			attr->sample_freq	= opts->freq;
		} else {
			attr->sample_period = opts->default_interval;
		}
	}

	/*
	 * Disable sampling for all group members other
	 * than leader in case leader 'leads' the sampling.
	 */
	if ((leader != evsel) && leader->sample_read) {
		attr->sample_freq   = 0;
		attr->sample_period = 0;
	}

	if (opts->no_samples)
		attr->sample_freq = 0;

	if (opts->inherit_stat)
		attr->inherit_stat = 1;

	if (opts->sample_address) {
		perf_evsel__set_sample_bit(evsel, ADDR);
		attr->mmap_data = track;
	}

	if (opts->call_graph) {
		perf_evsel__set_sample_bit(evsel, CALLCHAIN);

		if (opts->call_graph == CALLCHAIN_DWARF) {
			perf_evsel__set_sample_bit(evsel, REGS_USER);
			perf_evsel__set_sample_bit(evsel, STACK_USER);
			attr->sample_regs_user = PERF_REGS_MASK;
			attr->sample_stack_user = opts->stack_dump_size;
			attr->exclude_callchain_user = 1;
		}
	}

	if (target__has_cpu(&opts->target))
		perf_evsel__set_sample_bit(evsel, CPU);

	if (opts->period)
		perf_evsel__set_sample_bit(evsel, PERIOD);

	if (!perf_missing_features.sample_id_all &&
	    (opts->sample_time || !opts->no_inherit ||
	     target__has_cpu(&opts->target) || per_cpu))
		perf_evsel__set_sample_bit(evsel, TIME);

	if (opts->raw_samples) {
		perf_evsel__set_sample_bit(evsel, TIME);
		perf_evsel__set_sample_bit(evsel, RAW);
		perf_evsel__set_sample_bit(evsel, CPU);
	}

	if (opts->sample_address)
		perf_evsel__set_sample_bit(evsel, DATA_SRC);

	if (opts->no_buffering) {
		attr->watermark = 0;
		attr->wakeup_events = 1;
	}
	if (opts->branch_stack) {
		perf_evsel__set_sample_bit(evsel, BRANCH_STACK);
		attr->branch_sample_type = opts->branch_stack;
	}

	if (opts->sample_weight)
		perf_evsel__set_sample_bit(evsel, WEIGHT);

	attr->mmap  = track;
	attr->comm  = track;

	if (opts->sample_transaction)
		perf_evsel__set_sample_bit(evsel, TRANSACTION);

	/*
	 * XXX see the function comment above
	 *
	 * Disabling only independent events or group leaders,
	 * keeping group members enabled.
	 */
	if (perf_evsel__is_group_leader(evsel))
		attr->disabled = 1;

	/*
	 * Setting enable_on_exec for independent events and
	 * group leaders for traced executed by perf.
	 */
	if (target__none(&opts->target) && perf_evsel__is_group_leader(evsel) &&
		!opts->initial_delay)
		attr->enable_on_exec = 1;
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

static int perf_evsel__run_ioctl(struct perf_evsel *evsel, int ncpus, int nthreads,
			  int ioc,  void *arg)
{
	int cpu, thread;

	for (cpu = 0; cpu < ncpus; cpu++) {
		for (thread = 0; thread < nthreads; thread++) {
			int fd = FD(evsel, cpu, thread),
			    err = ioctl(fd, ioc, arg);

			if (err)
				return err;
		}
	}

	return 0;
}

int perf_evsel__set_filter(struct perf_evsel *evsel, int ncpus, int nthreads,
			   const char *filter)
{
	return perf_evsel__run_ioctl(evsel, ncpus, nthreads,
				     PERF_EVENT_IOC_SET_FILTER,
				     (void *)filter);
}

int perf_evsel__enable(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	return perf_evsel__run_ioctl(evsel, ncpus, nthreads,
				     PERF_EVENT_IOC_ENABLE,
				     0);
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

void perf_evsel__reset_counts(struct perf_evsel *evsel, int ncpus)
{
	memset(evsel->counts, 0, (sizeof(*evsel->counts) +
				 (ncpus * sizeof(struct perf_counts_values))));
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
	zfree(&evsel->id);
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

void perf_evsel__free_counts(struct perf_evsel *evsel)
{
	zfree(&evsel->counts);
}

void perf_evsel__exit(struct perf_evsel *evsel)
{
	assert(list_empty(&evsel->node));
	perf_evsel__free_fd(evsel);
	perf_evsel__free_id(evsel);
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	perf_evsel__exit(evsel);
	close_cgroup(evsel->cgrp);
	zfree(&evsel->group_name);
	if (evsel->tp_format)
		pevent_free_format(evsel->tp_format);
	zfree(&evsel->name);
	free(evsel);
}

static inline void compute_deltas(struct perf_evsel *evsel,
				  int cpu,
				  struct perf_counts_values *count)
{
	struct perf_counts_values tmp;

	if (!evsel->prev_raw_counts)
		return;

	if (cpu == -1) {
		tmp = evsel->prev_raw_counts->aggr;
		evsel->prev_raw_counts->aggr = *count;
	} else {
		tmp = evsel->prev_raw_counts->cpu[cpu];
		evsel->prev_raw_counts->cpu[cpu] = *count;
	}

	count->val = count->val - tmp.val;
	count->ena = count->ena - tmp.ena;
	count->run = count->run - tmp.run;
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

	compute_deltas(evsel, cpu, &count);

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

	compute_deltas(evsel, -1, aggr);

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

static int get_group_fd(struct perf_evsel *evsel, int cpu, int thread)
{
	struct perf_evsel *leader = evsel->leader;
	int fd;

	if (perf_evsel__is_group_leader(evsel))
		return -1;

	/*
	 * Leader must be already processed/open,
	 * if not it's a bug.
	 */
	BUG_ON(!leader->fd);

	fd = FD(leader, cpu, thread);
	BUG_ON(fd == -1);

	return fd;
}

#define __PRINT_ATTR(fmt, cast, field)  \
	fprintf(fp, "  %-19s "fmt"\n", #field, cast attr->field)

#define PRINT_ATTR_U32(field)  __PRINT_ATTR("%u" , , field)
#define PRINT_ATTR_X32(field)  __PRINT_ATTR("%#x", , field)
#define PRINT_ATTR_U64(field)  __PRINT_ATTR("%" PRIu64, (uint64_t), field)
#define PRINT_ATTR_X64(field)  __PRINT_ATTR("%#"PRIx64, (uint64_t), field)

#define PRINT_ATTR2N(name1, field1, name2, field2)	\
	fprintf(fp, "  %-19s %u    %-19s %u\n",		\
	name1, attr->field1, name2, attr->field2)

#define PRINT_ATTR2(field1, field2) \
	PRINT_ATTR2N(#field1, field1, #field2, field2)

static size_t perf_event_attr__fprintf(struct perf_event_attr *attr, FILE *fp)
{
	size_t ret = 0;

	ret += fprintf(fp, "%.60s\n", graph_dotted_line);
	ret += fprintf(fp, "perf_event_attr:\n");

	ret += PRINT_ATTR_U32(type);
	ret += PRINT_ATTR_U32(size);
	ret += PRINT_ATTR_X64(config);
	ret += PRINT_ATTR_U64(sample_period);
	ret += PRINT_ATTR_U64(sample_freq);
	ret += PRINT_ATTR_X64(sample_type);
	ret += PRINT_ATTR_X64(read_format);

	ret += PRINT_ATTR2(disabled, inherit);
	ret += PRINT_ATTR2(pinned, exclusive);
	ret += PRINT_ATTR2(exclude_user, exclude_kernel);
	ret += PRINT_ATTR2(exclude_hv, exclude_idle);
	ret += PRINT_ATTR2(mmap, comm);
	ret += PRINT_ATTR2(freq, inherit_stat);
	ret += PRINT_ATTR2(enable_on_exec, task);
	ret += PRINT_ATTR2(watermark, precise_ip);
	ret += PRINT_ATTR2(mmap_data, sample_id_all);
	ret += PRINT_ATTR2(exclude_host, exclude_guest);
	ret += PRINT_ATTR2N("excl.callchain_kern", exclude_callchain_kernel,
			    "excl.callchain_user", exclude_callchain_user);
	ret += PRINT_ATTR_U32(mmap2);

	ret += PRINT_ATTR_U32(wakeup_events);
	ret += PRINT_ATTR_U32(wakeup_watermark);
	ret += PRINT_ATTR_X32(bp_type);
	ret += PRINT_ATTR_X64(bp_addr);
	ret += PRINT_ATTR_X64(config1);
	ret += PRINT_ATTR_U64(bp_len);
	ret += PRINT_ATTR_X64(config2);
	ret += PRINT_ATTR_X64(branch_sample_type);
	ret += PRINT_ATTR_X64(sample_regs_user);
	ret += PRINT_ATTR_U32(sample_stack_user);

	ret += fprintf(fp, "%.60s\n", graph_dotted_line);

	return ret;
}

static int __perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
			      struct thread_map *threads)
{
	int cpu, thread;
	unsigned long flags = 0;
	int pid = -1, err;
	enum { NO_CHANGE, SET_TO_MAX, INCREASED_MAX } set_rlimit = NO_CHANGE;

	if (evsel->fd == NULL &&
	    perf_evsel__alloc_fd(evsel, cpus->nr, threads->nr) < 0)
		return -ENOMEM;

	if (evsel->cgrp) {
		flags = PERF_FLAG_PID_CGROUP;
		pid = evsel->cgrp->fd;
	}

fallback_missing_features:
	if (perf_missing_features.mmap2)
		evsel->attr.mmap2 = 0;
	if (perf_missing_features.exclude_guest)
		evsel->attr.exclude_guest = evsel->attr.exclude_host = 0;
retry_sample_id:
	if (perf_missing_features.sample_id_all)
		evsel->attr.sample_id_all = 0;

	if (verbose >= 2)
		perf_event_attr__fprintf(&evsel->attr, stderr);

	for (cpu = 0; cpu < cpus->nr; cpu++) {

		for (thread = 0; thread < threads->nr; thread++) {
			int group_fd;

			if (!evsel->cgrp)
				pid = threads->map[thread];

			group_fd = get_group_fd(evsel, cpu, thread);
retry_open:
			pr_debug2("perf_event_open: pid %d  cpu %d  group_fd %d  flags %#lx\n",
				  pid, cpus->map[cpu], group_fd, flags);

			FD(evsel, cpu, thread) = sys_perf_event_open(&evsel->attr,
								     pid,
								     cpus->map[cpu],
								     group_fd, flags);
			if (FD(evsel, cpu, thread) < 0) {
				err = -errno;
				pr_debug2("perf_event_open failed, error %d\n",
					  err);
				goto try_fallback;
			}
			set_rlimit = NO_CHANGE;
		}
	}

	return 0;

try_fallback:
	/*
	 * perf stat needs between 5 and 22 fds per CPU. When we run out
	 * of them try to increase the limits.
	 */
	if (err == -EMFILE && set_rlimit < INCREASED_MAX) {
		struct rlimit l;
		int old_errno = errno;

		if (getrlimit(RLIMIT_NOFILE, &l) == 0) {
			if (set_rlimit == NO_CHANGE)
				l.rlim_cur = l.rlim_max;
			else {
				l.rlim_cur = l.rlim_max + 1000;
				l.rlim_max = l.rlim_cur;
			}
			if (setrlimit(RLIMIT_NOFILE, &l) == 0) {
				set_rlimit++;
				errno = old_errno;
				goto retry_open;
			}
		}
		errno = old_errno;
	}

	if (err != -EINVAL || cpu > 0 || thread > 0)
		goto out_close;

	if (!perf_missing_features.mmap2 && evsel->attr.mmap2) {
		perf_missing_features.mmap2 = true;
		goto fallback_missing_features;
	} else if (!perf_missing_features.exclude_guest &&
		   (evsel->attr.exclude_guest || evsel->attr.exclude_host)) {
		perf_missing_features.exclude_guest = true;
		goto fallback_missing_features;
	} else if (!perf_missing_features.sample_id_all) {
		perf_missing_features.sample_id_all = true;
		goto retry_sample_id;
	}

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
		     struct thread_map *threads)
{
	if (cpus == NULL) {
		/* Work around old compiler warnings about strict aliasing */
		cpus = &empty_cpu_map.map;
	}

	if (threads == NULL)
		threads = &empty_thread_map.map;

	return __perf_evsel__open(evsel, cpus, threads);
}

int perf_evsel__open_per_cpu(struct perf_evsel *evsel,
			     struct cpu_map *cpus)
{
	return __perf_evsel__open(evsel, cpus, &empty_thread_map.map);
}

int perf_evsel__open_per_thread(struct perf_evsel *evsel,
				struct thread_map *threads)
{
	return __perf_evsel__open(evsel, &empty_cpu_map.map, threads);
}

static int perf_evsel__parse_id_sample(const struct perf_evsel *evsel,
				       const union perf_event *event,
				       struct perf_sample *sample)
{
	u64 type = evsel->attr.sample_type;
	const u64 *array = event->sample.array;
	bool swapped = evsel->needs_swap;
	union u64_swap u;

	array += ((event->header.size -
		   sizeof(event->header)) / sizeof(u64)) - 1;

	if (type & PERF_SAMPLE_IDENTIFIER) {
		sample->id = *array;
		array--;
	}

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
		array--;
	}

	return 0;
}

static inline bool overflow(const void *endp, u16 max_size, const void *offset,
			    u64 size)
{
	return size > max_size || offset + size > endp;
}

#define OVERFLOW_CHECK(offset, size, max_size)				\
	do {								\
		if (overflow(endp, (max_size), (offset), (size)))	\
			return -EFAULT;					\
	} while (0)

#define OVERFLOW_CHECK_u64(offset) \
	OVERFLOW_CHECK(offset, sizeof(u64), sizeof(u64))

int perf_evsel__parse_sample(struct perf_evsel *evsel, union perf_event *event,
			     struct perf_sample *data)
{
	u64 type = evsel->attr.sample_type;
	bool swapped = evsel->needs_swap;
	const u64 *array;
	u16 max_size = event->header.size;
	const void *endp = (void *)event + max_size;
	u64 sz;

	/*
	 * used for cross-endian analysis. See git commit 65014ab3
	 * for why this goofiness is needed.
	 */
	union u64_swap u;

	memset(data, 0, sizeof(*data));
	data->cpu = data->pid = data->tid = -1;
	data->stream_id = data->id = data->time = -1ULL;
	data->period = 1;
	data->weight = 0;

	if (event->header.type != PERF_RECORD_SAMPLE) {
		if (!evsel->attr.sample_id_all)
			return 0;
		return perf_evsel__parse_id_sample(evsel, event, data);
	}

	array = event->sample.array;

	/*
	 * The evsel's sample_size is based on PERF_SAMPLE_MASK which includes
	 * up to PERF_SAMPLE_PERIOD.  After that overflow() must be used to
	 * check the format does not go past the end of the event.
	 */
	if (evsel->sample_size + sizeof(event->header) > event->header.size)
		return -EFAULT;

	data->id = -1ULL;
	if (type & PERF_SAMPLE_IDENTIFIER) {
		data->id = *array;
		array++;
	}

	if (type & PERF_SAMPLE_IP) {
		data->ip = *array;
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
		u64 read_format = evsel->attr.read_format;

		OVERFLOW_CHECK_u64(array);
		if (read_format & PERF_FORMAT_GROUP)
			data->read.group.nr = *array;
		else
			data->read.one.value = *array;

		array++;

		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
			OVERFLOW_CHECK_u64(array);
			data->read.time_enabled = *array;
			array++;
		}

		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
			OVERFLOW_CHECK_u64(array);
			data->read.time_running = *array;
			array++;
		}

		/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
		if (read_format & PERF_FORMAT_GROUP) {
			const u64 max_group_nr = UINT64_MAX /
					sizeof(struct sample_read_value);

			if (data->read.group.nr > max_group_nr)
				return -EFAULT;
			sz = data->read.group.nr *
			     sizeof(struct sample_read_value);
			OVERFLOW_CHECK(array, sz, max_size);
			data->read.group.values =
					(struct sample_read_value *)array;
			array = (void *)array + sz;
		} else {
			OVERFLOW_CHECK_u64(array);
			data->read.one.id = *array;
			array++;
		}
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		const u64 max_callchain_nr = UINT64_MAX / sizeof(u64);

		OVERFLOW_CHECK_u64(array);
		data->callchain = (struct ip_callchain *)array++;
		if (data->callchain->nr > max_callchain_nr)
			return -EFAULT;
		sz = data->callchain->nr * sizeof(u64);
		OVERFLOW_CHECK(array, sz, max_size);
		array = (void *)array + sz;
	}

	if (type & PERF_SAMPLE_RAW) {
		OVERFLOW_CHECK_u64(array);
		u.val64 = *array;
		if (WARN_ONCE(swapped,
			      "Endianness of raw data not corrected!\n")) {
			/* undo swap of u64, then swap on individual u32s */
			u.val64 = bswap_64(u.val64);
			u.val32[0] = bswap_32(u.val32[0]);
			u.val32[1] = bswap_32(u.val32[1]);
		}
		data->raw_size = u.val32[0];
		array = (void *)array + sizeof(u32);

		OVERFLOW_CHECK(array, data->raw_size, max_size);
		data->raw_data = (void *)array;
		array = (void *)array + data->raw_size;
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		const u64 max_branch_nr = UINT64_MAX /
					  sizeof(struct branch_entry);

		OVERFLOW_CHECK_u64(array);
		data->branch_stack = (struct branch_stack *)array++;

		if (data->branch_stack->nr > max_branch_nr)
			return -EFAULT;
		sz = data->branch_stack->nr * sizeof(struct branch_entry);
		OVERFLOW_CHECK(array, sz, max_size);
		array = (void *)array + sz;
	}

	if (type & PERF_SAMPLE_REGS_USER) {
		OVERFLOW_CHECK_u64(array);
		data->user_regs.abi = *array;
		array++;

		if (data->user_regs.abi) {
			u64 regs_user = evsel->attr.sample_regs_user;

			sz = hweight_long(regs_user) * sizeof(u64);
			OVERFLOW_CHECK(array, sz, max_size);
			data->user_regs.regs = (u64 *)array;
			array = (void *)array + sz;
		}
	}

	if (type & PERF_SAMPLE_STACK_USER) {
		OVERFLOW_CHECK_u64(array);
		sz = *array++;

		data->user_stack.offset = ((char *)(array - 1)
					  - (char *) event);

		if (!sz) {
			data->user_stack.size = 0;
		} else {
			OVERFLOW_CHECK(array, sz, max_size);
			data->user_stack.data = (char *)array;
			array = (void *)array + sz;
			OVERFLOW_CHECK_u64(array);
			data->user_stack.size = *array++;
			if (WARN_ONCE(data->user_stack.size > sz,
				      "user stack dump failure\n"))
				return -EFAULT;
		}
	}

	data->weight = 0;
	if (type & PERF_SAMPLE_WEIGHT) {
		OVERFLOW_CHECK_u64(array);
		data->weight = *array;
		array++;
	}

	data->data_src = PERF_MEM_DATA_SRC_NONE;
	if (type & PERF_SAMPLE_DATA_SRC) {
		OVERFLOW_CHECK_u64(array);
		data->data_src = *array;
		array++;
	}

	data->transaction = 0;
	if (type & PERF_SAMPLE_TRANSACTION) {
		OVERFLOW_CHECK_u64(array);
		data->transaction = *array;
		array++;
	}

	return 0;
}

size_t perf_event__sample_event_size(const struct perf_sample *sample, u64 type,
				     u64 sample_regs_user, u64 read_format)
{
	size_t sz, result = sizeof(struct sample_event);

	if (type & PERF_SAMPLE_IDENTIFIER)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_IP)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_TID)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_TIME)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_ADDR)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_ID)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_STREAM_ID)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_CPU)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_PERIOD)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_READ) {
		result += sizeof(u64);
		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
			result += sizeof(u64);
		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
			result += sizeof(u64);
		/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
		if (read_format & PERF_FORMAT_GROUP) {
			sz = sample->read.group.nr *
			     sizeof(struct sample_read_value);
			result += sz;
		} else {
			result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		sz = (sample->callchain->nr + 1) * sizeof(u64);
		result += sz;
	}

	if (type & PERF_SAMPLE_RAW) {
		result += sizeof(u32);
		result += sample->raw_size;
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		sz = sample->branch_stack->nr * sizeof(struct branch_entry);
		sz += sizeof(u64);
		result += sz;
	}

	if (type & PERF_SAMPLE_REGS_USER) {
		if (sample->user_regs.abi) {
			result += sizeof(u64);
			sz = hweight_long(sample_regs_user) * sizeof(u64);
			result += sz;
		} else {
			result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_STACK_USER) {
		sz = sample->user_stack.size;
		result += sizeof(u64);
		if (sz) {
			result += sz;
			result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_WEIGHT)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_DATA_SRC)
		result += sizeof(u64);

	if (type & PERF_SAMPLE_TRANSACTION)
		result += sizeof(u64);

	return result;
}

int perf_event__synthesize_sample(union perf_event *event, u64 type,
				  u64 sample_regs_user, u64 read_format,
				  const struct perf_sample *sample,
				  bool swapped)
{
	u64 *array;
	size_t sz;
	/*
	 * used for cross-endian analysis. See git commit 65014ab3
	 * for why this goofiness is needed.
	 */
	union u64_swap u;

	array = event->sample.array;

	if (type & PERF_SAMPLE_IDENTIFIER) {
		*array = sample->id;
		array++;
	}

	if (type & PERF_SAMPLE_IP) {
		*array = sample->ip;
		array++;
	}

	if (type & PERF_SAMPLE_TID) {
		u.val32[0] = sample->pid;
		u.val32[1] = sample->tid;
		if (swapped) {
			/*
			 * Inverse of what is done in perf_evsel__parse_sample
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
			 * Inverse of what is done in perf_evsel__parse_sample
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

	if (type & PERF_SAMPLE_READ) {
		if (read_format & PERF_FORMAT_GROUP)
			*array = sample->read.group.nr;
		else
			*array = sample->read.one.value;
		array++;

		if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) {
			*array = sample->read.time_enabled;
			array++;
		}

		if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) {
			*array = sample->read.time_running;
			array++;
		}

		/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
		if (read_format & PERF_FORMAT_GROUP) {
			sz = sample->read.group.nr *
			     sizeof(struct sample_read_value);
			memcpy(array, sample->read.group.values, sz);
			array = (void *)array + sz;
		} else {
			*array = sample->read.one.id;
			array++;
		}
	}

	if (type & PERF_SAMPLE_CALLCHAIN) {
		sz = (sample->callchain->nr + 1) * sizeof(u64);
		memcpy(array, sample->callchain, sz);
		array = (void *)array + sz;
	}

	if (type & PERF_SAMPLE_RAW) {
		u.val32[0] = sample->raw_size;
		if (WARN_ONCE(swapped,
			      "Endianness of raw data not corrected!\n")) {
			/*
			 * Inverse of what is done in perf_evsel__parse_sample
			 */
			u.val32[0] = bswap_32(u.val32[0]);
			u.val32[1] = bswap_32(u.val32[1]);
			u.val64 = bswap_64(u.val64);
		}
		*array = u.val64;
		array = (void *)array + sizeof(u32);

		memcpy(array, sample->raw_data, sample->raw_size);
		array = (void *)array + sample->raw_size;
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		sz = sample->branch_stack->nr * sizeof(struct branch_entry);
		sz += sizeof(u64);
		memcpy(array, sample->branch_stack, sz);
		array = (void *)array + sz;
	}

	if (type & PERF_SAMPLE_REGS_USER) {
		if (sample->user_regs.abi) {
			*array++ = sample->user_regs.abi;
			sz = hweight_long(sample_regs_user) * sizeof(u64);
			memcpy(array, sample->user_regs.regs, sz);
			array = (void *)array + sz;
		} else {
			*array++ = 0;
		}
	}

	if (type & PERF_SAMPLE_STACK_USER) {
		sz = sample->user_stack.size;
		*array++ = sz;
		if (sz) {
			memcpy(array, sample->user_stack.data, sz);
			array = (void *)array + sz;
			*array++ = sz;
		}
	}

	if (type & PERF_SAMPLE_WEIGHT) {
		*array = sample->weight;
		array++;
	}

	if (type & PERF_SAMPLE_DATA_SRC) {
		*array = sample->data_src;
		array++;
	}

	if (type & PERF_SAMPLE_TRANSACTION) {
		*array = sample->transaction;
		array++;
	}

	return 0;
}

struct format_field *perf_evsel__field(struct perf_evsel *evsel, const char *name)
{
	return pevent_find_field(evsel->tp_format, name);
}

void *perf_evsel__rawptr(struct perf_evsel *evsel, struct perf_sample *sample,
			 const char *name)
{
	struct format_field *field = perf_evsel__field(evsel, name);
	int offset;

	if (!field)
		return NULL;

	offset = field->offset;

	if (field->flags & FIELD_IS_DYNAMIC) {
		offset = *(int *)(sample->raw_data + field->offset);
		offset &= 0xffff;
	}

	return sample->raw_data + offset;
}

u64 perf_evsel__intval(struct perf_evsel *evsel, struct perf_sample *sample,
		       const char *name)
{
	struct format_field *field = perf_evsel__field(evsel, name);
	void *ptr;
	u64 value;

	if (!field)
		return 0;

	ptr = sample->raw_data + field->offset;

	switch (field->size) {
	case 1:
		return *(u8 *)ptr;
	case 2:
		value = *(u16 *)ptr;
		break;
	case 4:
		value = *(u32 *)ptr;
		break;
	case 8:
		value = *(u64 *)ptr;
		break;
	default:
		return 0;
	}

	if (!evsel->needs_swap)
		return value;

	switch (field->size) {
	case 2:
		return bswap_16(value);
	case 4:
		return bswap_32(value);
	case 8:
		return bswap_64(value);
	default:
		return 0;
	}

	return 0;
}

static int comma_fprintf(FILE *fp, bool *first, const char *fmt, ...)
{
	va_list args;
	int ret = 0;

	if (!*first) {
		ret += fprintf(fp, ",");
	} else {
		ret += fprintf(fp, ":");
		*first = false;
	}

	va_start(args, fmt);
	ret += vfprintf(fp, fmt, args);
	va_end(args);
	return ret;
}

static int __if_fprintf(FILE *fp, bool *first, const char *field, u64 value)
{
	if (value == 0)
		return 0;

	return comma_fprintf(fp, first, " %s: %" PRIu64, field, value);
}

#define if_print(field) printed += __if_fprintf(fp, &first, #field, evsel->attr.field)

struct bit_names {
	int bit;
	const char *name;
};

static int bits__fprintf(FILE *fp, const char *field, u64 value,
			 struct bit_names *bits, bool *first)
{
	int i = 0, printed = comma_fprintf(fp, first, " %s: ", field);
	bool first_bit = true;

	do {
		if (value & bits[i].bit) {
			printed += fprintf(fp, "%s%s", first_bit ? "" : "|", bits[i].name);
			first_bit = false;
		}
	} while (bits[++i].name != NULL);

	return printed;
}

static int sample_type__fprintf(FILE *fp, bool *first, u64 value)
{
#define bit_name(n) { PERF_SAMPLE_##n, #n }
	struct bit_names bits[] = {
		bit_name(IP), bit_name(TID), bit_name(TIME), bit_name(ADDR),
		bit_name(READ), bit_name(CALLCHAIN), bit_name(ID), bit_name(CPU),
		bit_name(PERIOD), bit_name(STREAM_ID), bit_name(RAW),
		bit_name(BRANCH_STACK), bit_name(REGS_USER), bit_name(STACK_USER),
		bit_name(IDENTIFIER),
		{ .name = NULL, }
	};
#undef bit_name
	return bits__fprintf(fp, "sample_type", value, bits, first);
}

static int read_format__fprintf(FILE *fp, bool *first, u64 value)
{
#define bit_name(n) { PERF_FORMAT_##n, #n }
	struct bit_names bits[] = {
		bit_name(TOTAL_TIME_ENABLED), bit_name(TOTAL_TIME_RUNNING),
		bit_name(ID), bit_name(GROUP),
		{ .name = NULL, }
	};
#undef bit_name
	return bits__fprintf(fp, "read_format", value, bits, first);
}

int perf_evsel__fprintf(struct perf_evsel *evsel,
			struct perf_attr_details *details, FILE *fp)
{
	bool first = true;
	int printed = 0;

	if (details->event_group) {
		struct perf_evsel *pos;

		if (!perf_evsel__is_group_leader(evsel))
			return 0;

		if (evsel->nr_members > 1)
			printed += fprintf(fp, "%s{", evsel->group_name ?: "");

		printed += fprintf(fp, "%s", perf_evsel__name(evsel));
		for_each_group_member(pos, evsel)
			printed += fprintf(fp, ",%s", perf_evsel__name(pos));

		if (evsel->nr_members > 1)
			printed += fprintf(fp, "}");
		goto out;
	}

	printed += fprintf(fp, "%s", perf_evsel__name(evsel));

	if (details->verbose || details->freq) {
		printed += comma_fprintf(fp, &first, " sample_freq=%" PRIu64,
					 (u64)evsel->attr.sample_freq);
	}

	if (details->verbose) {
		if_print(type);
		if_print(config);
		if_print(config1);
		if_print(config2);
		if_print(size);
		printed += sample_type__fprintf(fp, &first, evsel->attr.sample_type);
		if (evsel->attr.read_format)
			printed += read_format__fprintf(fp, &first, evsel->attr.read_format);
		if_print(disabled);
		if_print(inherit);
		if_print(pinned);
		if_print(exclusive);
		if_print(exclude_user);
		if_print(exclude_kernel);
		if_print(exclude_hv);
		if_print(exclude_idle);
		if_print(mmap);
		if_print(mmap2);
		if_print(comm);
		if_print(freq);
		if_print(inherit_stat);
		if_print(enable_on_exec);
		if_print(task);
		if_print(watermark);
		if_print(precise_ip);
		if_print(mmap_data);
		if_print(sample_id_all);
		if_print(exclude_host);
		if_print(exclude_guest);
		if_print(__reserved_1);
		if_print(wakeup_events);
		if_print(bp_type);
		if_print(branch_sample_type);
	}
out:
	fputc('\n', fp);
	return ++printed;
}

bool perf_evsel__fallback(struct perf_evsel *evsel, int err,
			  char *msg, size_t msgsize)
{
	if ((err == ENOENT || err == ENXIO || err == ENODEV) &&
	    evsel->attr.type   == PERF_TYPE_HARDWARE &&
	    evsel->attr.config == PERF_COUNT_HW_CPU_CYCLES) {
		/*
		 * If it's cycles then fall back to hrtimer based
		 * cpu-clock-tick sw counter, which is always available even if
		 * no PMU support.
		 *
		 * PPC returns ENXIO until 2.6.37 (behavior changed with commit
		 * b0a873e).
		 */
		scnprintf(msg, msgsize, "%s",
"The cycles event is not supported, trying to fall back to cpu-clock-ticks");

		evsel->attr.type   = PERF_TYPE_SOFTWARE;
		evsel->attr.config = PERF_COUNT_SW_CPU_CLOCK;

		zfree(&evsel->name);
		return true;
	}

	return false;
}

int perf_evsel__open_strerror(struct perf_evsel *evsel, struct target *target,
			      int err, char *msg, size_t size)
{
	switch (err) {
	case EPERM:
	case EACCES:
		return scnprintf(msg, size,
		 "You may not have permission to collect %sstats.\n"
		 "Consider tweaking /proc/sys/kernel/perf_event_paranoid:\n"
		 " -1 - Not paranoid at all\n"
		 "  0 - Disallow raw tracepoint access for unpriv\n"
		 "  1 - Disallow cpu events for unpriv\n"
		 "  2 - Disallow kernel profiling for unpriv",
				 target->system_wide ? "system-wide " : "");
	case ENOENT:
		return scnprintf(msg, size, "The %s event is not supported.",
				 perf_evsel__name(evsel));
	case EMFILE:
		return scnprintf(msg, size, "%s",
			 "Too many events are opened.\n"
			 "Try again after reducing the number of events.");
	case ENODEV:
		if (target->cpu_list)
			return scnprintf(msg, size, "%s",
	 "No such device - did you specify an out-of-range profile CPU?\n");
		break;
	case EOPNOTSUPP:
		if (evsel->attr.precise_ip)
			return scnprintf(msg, size, "%s",
	"\'precise\' request may not be supported. Try removing 'p' modifier.");
#if defined(__i386__) || defined(__x86_64__)
		if (evsel->attr.type == PERF_TYPE_HARDWARE)
			return scnprintf(msg, size, "%s",
	"No hardware sampling interrupt available.\n"
	"No APIC? If so then you can boot the kernel with the \"lapic\" boot parameter to force-enable it.");
#endif
		break;
	default:
		break;
	}

	return scnprintf(msg, size,
	"The sys_perf_event_open() syscall returned with %d (%s) for event (%s).  \n"
	"/bin/dmesg may provide additional information.\n"
	"No CONFIG_PERF_EVENTS=y kernel support configured?\n",
			 err, strerror(err), perf_evsel__name(evsel));
}
