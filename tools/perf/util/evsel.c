// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Parts came from builtin-{top,stat,record}.c, see those files for further
 * copyright notes.
 */

#include <byteswap.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/bitops.h>
#include <api/fs/fs.h>
#include <api/fs/tracing_path.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/zalloc.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <perf/evsel.h>
#include "asm/bug.h"
#include "bpf_counter.h"
#include "callchain.h"
#include "cgroup.h"
#include "counts.h"
#include "event.h"
#include "evsel.h"
#include "util/env.h"
#include "util/evsel_config.h"
#include "util/evsel_fprintf.h"
#include "evlist.h"
#include <perf/cpumap.h>
#include "thread_map.h"
#include "target.h"
#include "perf_regs.h"
#include "record.h"
#include "debug.h"
#include "trace-event.h"
#include "stat.h"
#include "string2.h"
#include "memswap.h"
#include "util.h"
#include "util/hashmap.h"
#include "pmu-hybrid.h"
#include "off_cpu.h"
#include "../perf-sys.h"
#include "util/parse-branch-options.h"
#include "util/bpf-filter.h"
#include <internal/xyarray.h>
#include <internal/lib.h>
#include <internal/threadmap.h>

#include <linux/ctype.h>

#ifdef HAVE_LIBTRACEEVENT
#include <traceevent/event-parse.h>
#endif

struct perf_missing_features perf_missing_features;

static clockid_t clockid;

static const char *const perf_tool_event__tool_names[PERF_TOOL_MAX] = {
	NULL,
	"duration_time",
	"user_time",
	"system_time",
};

const char *perf_tool_event__to_str(enum perf_tool_event ev)
{
	if (ev > PERF_TOOL_NONE && ev < PERF_TOOL_MAX)
		return perf_tool_event__tool_names[ev];

	return NULL;
}

enum perf_tool_event perf_tool_event__from_str(const char *str)
{
	int i;

	perf_tool_event__for_each_event(i) {
		if (!strcmp(str, perf_tool_event__tool_names[i]))
			return i;
	}
	return PERF_TOOL_NONE;
}


static int evsel__no_extra_init(struct evsel *evsel __maybe_unused)
{
	return 0;
}

void __weak test_attr__ready(void) { }

static void evsel__no_extra_fini(struct evsel *evsel __maybe_unused)
{
}

static struct {
	size_t	size;
	int	(*init)(struct evsel *evsel);
	void	(*fini)(struct evsel *evsel);
} perf_evsel__object = {
	.size = sizeof(struct evsel),
	.init = evsel__no_extra_init,
	.fini = evsel__no_extra_fini,
};

int evsel__object_config(size_t object_size, int (*init)(struct evsel *evsel),
			 void (*fini)(struct evsel *evsel))
{

	if (object_size == 0)
		goto set_methods;

	if (perf_evsel__object.size > object_size)
		return -EINVAL;

	perf_evsel__object.size = object_size;

set_methods:
	if (init != NULL)
		perf_evsel__object.init = init;

	if (fini != NULL)
		perf_evsel__object.fini = fini;

	return 0;
}

#define FD(e, x, y) (*(int *)xyarray__entry(e->core.fd, x, y))

int __evsel__sample_size(u64 sample_type)
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
 * perf_record_sample.
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

void evsel__calc_id_pos(struct evsel *evsel)
{
	evsel->id_pos = __perf_evsel__calc_id_pos(evsel->core.attr.sample_type);
	evsel->is_pos = __perf_evsel__calc_is_pos(evsel->core.attr.sample_type);
}

void __evsel__set_sample_bit(struct evsel *evsel,
				  enum perf_event_sample_format bit)
{
	if (!(evsel->core.attr.sample_type & bit)) {
		evsel->core.attr.sample_type |= bit;
		evsel->sample_size += sizeof(u64);
		evsel__calc_id_pos(evsel);
	}
}

void __evsel__reset_sample_bit(struct evsel *evsel,
				    enum perf_event_sample_format bit)
{
	if (evsel->core.attr.sample_type & bit) {
		evsel->core.attr.sample_type &= ~bit;
		evsel->sample_size -= sizeof(u64);
		evsel__calc_id_pos(evsel);
	}
}

void evsel__set_sample_id(struct evsel *evsel,
			       bool can_sample_identifier)
{
	if (can_sample_identifier) {
		evsel__reset_sample_bit(evsel, ID);
		evsel__set_sample_bit(evsel, IDENTIFIER);
	} else {
		evsel__set_sample_bit(evsel, ID);
	}
	evsel->core.attr.read_format |= PERF_FORMAT_ID;
}

/**
 * evsel__is_function_event - Return whether given evsel is a function
 * trace event
 *
 * @evsel - evsel selector to be tested
 *
 * Return %true if event is function trace event
 */
bool evsel__is_function_event(struct evsel *evsel)
{
#define FUNCTION_EVENT "ftrace:function"

	return evsel->name &&
	       !strncmp(FUNCTION_EVENT, evsel->name, sizeof(FUNCTION_EVENT));

#undef FUNCTION_EVENT
}

void evsel__init(struct evsel *evsel,
		 struct perf_event_attr *attr, int idx)
{
	perf_evsel__init(&evsel->core, attr, idx);
	evsel->tracking	   = !idx;
	evsel->unit	   = strdup("");
	evsel->scale	   = 1.0;
	evsel->max_events  = ULONG_MAX;
	evsel->evlist	   = NULL;
	evsel->bpf_obj	   = NULL;
	evsel->bpf_fd	   = -1;
	INIT_LIST_HEAD(&evsel->config_terms);
	INIT_LIST_HEAD(&evsel->bpf_counter_list);
	perf_evsel__object.init(evsel);
	evsel->sample_size = __evsel__sample_size(attr->sample_type);
	evsel__calc_id_pos(evsel);
	evsel->cmdline_group_boundary = false;
	evsel->metric_events = NULL;
	evsel->per_pkg_mask  = NULL;
	evsel->collect_stat  = false;
	evsel->pmu_name      = NULL;
	evsel->skippable     = false;
}

struct evsel *evsel__new_idx(struct perf_event_attr *attr, int idx)
{
	struct evsel *evsel = zalloc(perf_evsel__object.size);

	if (!evsel)
		return NULL;
	evsel__init(evsel, attr, idx);

	if (evsel__is_bpf_output(evsel) && !attr->sample_type) {
		evsel->core.attr.sample_type = (PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
					    PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD),
		evsel->core.attr.sample_period = 1;
	}

	if (evsel__is_clock(evsel)) {
		free((char *)evsel->unit);
		evsel->unit = strdup("msec");
		evsel->scale = 1e-6;
	}

	return evsel;
}

static bool perf_event_can_profile_kernel(void)
{
	return perf_event_paranoid_check(1);
}

struct evsel *evsel__new_cycles(bool precise __maybe_unused, __u32 type, __u64 config)
{
	struct perf_event_attr attr = {
		.type	= type,
		.config	= config,
		.exclude_kernel	= !perf_event_can_profile_kernel(),
	};
	struct evsel *evsel;

	event_attr_init(&attr);

	/*
	 * Now let the usual logic to set up the perf_event_attr defaults
	 * to kick in when we return and before perf_evsel__open() is called.
	 */
	evsel = evsel__new(&attr);
	if (evsel == NULL)
		goto out;

	arch_evsel__fixup_new_cycles(&evsel->core.attr);

	evsel->precise_max = true;

	/* use asprintf() because free(evsel) assumes name is allocated */
	if (asprintf(&evsel->name, "cycles%s%s%.*s",
		     (attr.precise_ip || attr.exclude_kernel) ? ":" : "",
		     attr.exclude_kernel ? "u" : "",
		     attr.precise_ip ? attr.precise_ip + 1 : 0, "ppp") < 0)
		goto error_free;
out:
	return evsel;
error_free:
	evsel__delete(evsel);
	evsel = NULL;
	goto out;
}

int copy_config_terms(struct list_head *dst, struct list_head *src)
{
	struct evsel_config_term *pos, *tmp;

	list_for_each_entry(pos, src, list) {
		tmp = malloc(sizeof(*tmp));
		if (tmp == NULL)
			return -ENOMEM;

		*tmp = *pos;
		if (tmp->free_str) {
			tmp->val.str = strdup(pos->val.str);
			if (tmp->val.str == NULL) {
				free(tmp);
				return -ENOMEM;
			}
		}
		list_add_tail(&tmp->list, dst);
	}
	return 0;
}

static int evsel__copy_config_terms(struct evsel *dst, struct evsel *src)
{
	return copy_config_terms(&dst->config_terms, &src->config_terms);
}

/**
 * evsel__clone - create a new evsel copied from @orig
 * @orig: original evsel
 *
 * The assumption is that @orig is not configured nor opened yet.
 * So we only care about the attributes that can be set while it's parsed.
 */
struct evsel *evsel__clone(struct evsel *orig)
{
	struct evsel *evsel;

	BUG_ON(orig->core.fd);
	BUG_ON(orig->counts);
	BUG_ON(orig->priv);
	BUG_ON(orig->per_pkg_mask);

	/* cannot handle BPF objects for now */
	if (orig->bpf_obj)
		return NULL;

	evsel = evsel__new(&orig->core.attr);
	if (evsel == NULL)
		return NULL;

	evsel->core.cpus = perf_cpu_map__get(orig->core.cpus);
	evsel->core.own_cpus = perf_cpu_map__get(orig->core.own_cpus);
	evsel->core.threads = perf_thread_map__get(orig->core.threads);
	evsel->core.nr_members = orig->core.nr_members;
	evsel->core.system_wide = orig->core.system_wide;
	evsel->core.requires_cpu = orig->core.requires_cpu;

	if (orig->name) {
		evsel->name = strdup(orig->name);
		if (evsel->name == NULL)
			goto out_err;
	}
	if (orig->group_name) {
		evsel->group_name = strdup(orig->group_name);
		if (evsel->group_name == NULL)
			goto out_err;
	}
	if (orig->pmu_name) {
		evsel->pmu_name = strdup(orig->pmu_name);
		if (evsel->pmu_name == NULL)
			goto out_err;
	}
	if (orig->filter) {
		evsel->filter = strdup(orig->filter);
		if (evsel->filter == NULL)
			goto out_err;
	}
	if (orig->metric_id) {
		evsel->metric_id = strdup(orig->metric_id);
		if (evsel->metric_id == NULL)
			goto out_err;
	}
	evsel->cgrp = cgroup__get(orig->cgrp);
#ifdef HAVE_LIBTRACEEVENT
	evsel->tp_format = orig->tp_format;
#endif
	evsel->handler = orig->handler;
	evsel->core.leader = orig->core.leader;

	evsel->max_events = orig->max_events;
	evsel->tool_event = orig->tool_event;
	free((char *)evsel->unit);
	evsel->unit = strdup(orig->unit);
	if (evsel->unit == NULL)
		goto out_err;

	evsel->scale = orig->scale;
	evsel->snapshot = orig->snapshot;
	evsel->per_pkg = orig->per_pkg;
	evsel->percore = orig->percore;
	evsel->precise_max = orig->precise_max;
	evsel->is_libpfm_event = orig->is_libpfm_event;

	evsel->exclude_GH = orig->exclude_GH;
	evsel->sample_read = orig->sample_read;
	evsel->auto_merge_stats = orig->auto_merge_stats;
	evsel->collect_stat = orig->collect_stat;
	evsel->weak_group = orig->weak_group;
	evsel->use_config_name = orig->use_config_name;
	evsel->pmu = orig->pmu;

	if (evsel__copy_config_terms(evsel, orig) < 0)
		goto out_err;

	return evsel;

out_err:
	evsel__delete(evsel);
	return NULL;
}

/*
 * Returns pointer with encoded error via <linux/err.h> interface.
 */
#ifdef HAVE_LIBTRACEEVENT
struct evsel *evsel__newtp_idx(const char *sys, const char *name, int idx)
{
	struct evsel *evsel = zalloc(perf_evsel__object.size);
	int err = -ENOMEM;

	if (evsel == NULL) {
		goto out_err;
	} else {
		struct perf_event_attr attr = {
			.type	       = PERF_TYPE_TRACEPOINT,
			.sample_type   = (PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
					  PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD),
		};

		if (asprintf(&evsel->name, "%s:%s", sys, name) < 0)
			goto out_free;

		evsel->tp_format = trace_event__tp_format(sys, name);
		if (IS_ERR(evsel->tp_format)) {
			err = PTR_ERR(evsel->tp_format);
			goto out_free;
		}

		event_attr_init(&attr);
		attr.config = evsel->tp_format->id;
		attr.sample_period = 1;
		evsel__init(evsel, &attr, idx);
	}

	return evsel;

out_free:
	zfree(&evsel->name);
	free(evsel);
out_err:
	return ERR_PTR(err);
}
#endif

const char *const evsel__hw_names[PERF_COUNT_HW_MAX] = {
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

char *evsel__bpf_counter_events;

bool evsel__match_bpf_counter_events(const char *name)
{
	int name_len;
	bool match;
	char *ptr;

	if (!evsel__bpf_counter_events)
		return false;

	ptr = strstr(evsel__bpf_counter_events, name);
	name_len = strlen(name);

	/* check name matches a full token in evsel__bpf_counter_events */
	match = (ptr != NULL) &&
		((ptr == evsel__bpf_counter_events) || (*(ptr - 1) == ',')) &&
		((*(ptr + name_len) == ',') || (*(ptr + name_len) == '\0'));

	return match;
}

static const char *__evsel__hw_name(u64 config)
{
	if (config < PERF_COUNT_HW_MAX && evsel__hw_names[config])
		return evsel__hw_names[config];

	return "unknown-hardware";
}

static int evsel__add_modifiers(struct evsel *evsel, char *bf, size_t size)
{
	int colon = 0, r = 0;
	struct perf_event_attr *attr = &evsel->core.attr;
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

int __weak arch_evsel__hw_name(struct evsel *evsel, char *bf, size_t size)
{
	return scnprintf(bf, size, "%s", __evsel__hw_name(evsel->core.attr.config));
}

static int evsel__hw_name(struct evsel *evsel, char *bf, size_t size)
{
	int r = arch_evsel__hw_name(evsel, bf, size);
	return r + evsel__add_modifiers(evsel, bf + r, size - r);
}

const char *const evsel__sw_names[PERF_COUNT_SW_MAX] = {
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

static const char *__evsel__sw_name(u64 config)
{
	if (config < PERF_COUNT_SW_MAX && evsel__sw_names[config])
		return evsel__sw_names[config];
	return "unknown-software";
}

static int evsel__sw_name(struct evsel *evsel, char *bf, size_t size)
{
	int r = scnprintf(bf, size, "%s", __evsel__sw_name(evsel->core.attr.config));
	return r + evsel__add_modifiers(evsel, bf + r, size - r);
}

static int evsel__tool_name(enum perf_tool_event ev, char *bf, size_t size)
{
	return scnprintf(bf, size, "%s", perf_tool_event__to_str(ev));
}

static int __evsel__bp_name(char *bf, size_t size, u64 addr, u64 type)
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

static int evsel__bp_name(struct evsel *evsel, char *bf, size_t size)
{
	struct perf_event_attr *attr = &evsel->core.attr;
	int r = __evsel__bp_name(bf, size, attr->bp_addr, attr->bp_type);
	return r + evsel__add_modifiers(evsel, bf + r, size - r);
}

const char *const evsel__hw_cache[PERF_COUNT_HW_CACHE_MAX][EVSEL__MAX_ALIASES] = {
 { "L1-dcache",	"l1-d",		"l1d",		"L1-data",		},
 { "L1-icache",	"l1-i",		"l1i",		"L1-instruction",	},
 { "LLC",	"L2",							},
 { "dTLB",	"d-tlb",	"Data-TLB",				},
 { "iTLB",	"i-tlb",	"Instruction-TLB",			},
 { "branch",	"branches",	"bpu",		"btb",		"bpc",	},
 { "node",								},
};

const char *const evsel__hw_cache_op[PERF_COUNT_HW_CACHE_OP_MAX][EVSEL__MAX_ALIASES] = {
 { "load",	"loads",	"read",					},
 { "store",	"stores",	"write",				},
 { "prefetch",	"prefetches",	"speculative-read", "speculative-load",	},
};

const char *const evsel__hw_cache_result[PERF_COUNT_HW_CACHE_RESULT_MAX][EVSEL__MAX_ALIASES] = {
 { "refs",	"Reference",	"ops",		"access",		},
 { "misses",	"miss",							},
};

#define C(x)		PERF_COUNT_HW_CACHE_##x
#define CACHE_READ	(1 << C(OP_READ))
#define CACHE_WRITE	(1 << C(OP_WRITE))
#define CACHE_PREFETCH	(1 << C(OP_PREFETCH))
#define COP(x)		(1 << x)

/*
 * cache operation stat
 * L1I : Read and prefetch only
 * ITLB and BPU : Read-only
 */
static const unsigned long evsel__hw_cache_stat[C(MAX)] = {
 [C(L1D)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(L1I)]	= (CACHE_READ | CACHE_PREFETCH),
 [C(LL)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(DTLB)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(ITLB)]	= (CACHE_READ),
 [C(BPU)]	= (CACHE_READ),
 [C(NODE)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
};

bool evsel__is_cache_op_valid(u8 type, u8 op)
{
	if (evsel__hw_cache_stat[type] & COP(op))
		return true;	/* valid */
	else
		return false;	/* invalid */
}

int __evsel__hw_cache_type_op_res_name(u8 type, u8 op, u8 result, char *bf, size_t size)
{
	if (result) {
		return scnprintf(bf, size, "%s-%s-%s", evsel__hw_cache[type][0],
				 evsel__hw_cache_op[op][0],
				 evsel__hw_cache_result[result][0]);
	}

	return scnprintf(bf, size, "%s-%s", evsel__hw_cache[type][0],
			 evsel__hw_cache_op[op][1]);
}

static int __evsel__hw_cache_name(u64 config, char *bf, size_t size)
{
	u8 op, result, type = (config >>  0) & 0xff;
	const char *err = "unknown-ext-hardware-cache-type";

	if (type >= PERF_COUNT_HW_CACHE_MAX)
		goto out_err;

	op = (config >>  8) & 0xff;
	err = "unknown-ext-hardware-cache-op";
	if (op >= PERF_COUNT_HW_CACHE_OP_MAX)
		goto out_err;

	result = (config >> 16) & 0xff;
	err = "unknown-ext-hardware-cache-result";
	if (result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		goto out_err;

	err = "invalid-cache";
	if (!evsel__is_cache_op_valid(type, op))
		goto out_err;

	return __evsel__hw_cache_type_op_res_name(type, op, result, bf, size);
out_err:
	return scnprintf(bf, size, "%s", err);
}

static int evsel__hw_cache_name(struct evsel *evsel, char *bf, size_t size)
{
	int ret = __evsel__hw_cache_name(evsel->core.attr.config, bf, size);
	return ret + evsel__add_modifiers(evsel, bf + ret, size - ret);
}

static int evsel__raw_name(struct evsel *evsel, char *bf, size_t size)
{
	int ret = scnprintf(bf, size, "raw 0x%" PRIx64, evsel->core.attr.config);
	return ret + evsel__add_modifiers(evsel, bf + ret, size - ret);
}

const char *evsel__name(struct evsel *evsel)
{
	char bf[128];

	if (!evsel)
		goto out_unknown;

	if (evsel->name)
		return evsel->name;

	switch (evsel->core.attr.type) {
	case PERF_TYPE_RAW:
		evsel__raw_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_HARDWARE:
		evsel__hw_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_HW_CACHE:
		evsel__hw_cache_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_SOFTWARE:
		if (evsel__is_tool(evsel))
			evsel__tool_name(evsel->tool_event, bf, sizeof(bf));
		else
			evsel__sw_name(evsel, bf, sizeof(bf));
		break;

	case PERF_TYPE_TRACEPOINT:
		scnprintf(bf, sizeof(bf), "%s", "unknown tracepoint");
		break;

	case PERF_TYPE_BREAKPOINT:
		evsel__bp_name(evsel, bf, sizeof(bf));
		break;

	default:
		scnprintf(bf, sizeof(bf), "unknown attr type: %d",
			  evsel->core.attr.type);
		break;
	}

	evsel->name = strdup(bf);

	if (evsel->name)
		return evsel->name;
out_unknown:
	return "unknown";
}

bool evsel__name_is(struct evsel *evsel, const char *name)
{
	return !strcmp(evsel__name(evsel), name);
}

const char *evsel__group_pmu_name(const struct evsel *evsel)
{
	struct evsel *leader = evsel__leader(evsel);
	struct evsel *pos;

	/*
	 * Software events may be in a group with other uncore PMU events. Use
	 * the pmu_name of the first non-software event to avoid breaking the
	 * software event out of the group.
	 *
	 * Aux event leaders, like intel_pt, expect a group with events from
	 * other PMUs, so substitute the AUX event's PMU in this case.
	 */
	if (evsel->core.attr.type == PERF_TYPE_SOFTWARE || evsel__is_aux_event(leader)) {
		/* Starting with the leader, find the first event with a named PMU. */
		for_each_group_evsel(pos, leader) {
			if (pos->pmu_name)
				return pos->pmu_name;
		}
	}

	return evsel->pmu_name ?: "cpu";
}

const char *evsel__metric_id(const struct evsel *evsel)
{
	if (evsel->metric_id)
		return evsel->metric_id;

	if (evsel__is_tool(evsel))
		return perf_tool_event__to_str(evsel->tool_event);

	return "unknown";
}

const char *evsel__group_name(struct evsel *evsel)
{
	return evsel->group_name ?: "anon group";
}

/*
 * Returns the group details for the specified leader,
 * with following rules.
 *
 *  For record -e '{cycles,instructions}'
 *    'anon group { cycles:u, instructions:u }'
 *
 *  For record -e 'cycles,instructions' and report --group
 *    'cycles:u, instructions:u'
 */
int evsel__group_desc(struct evsel *evsel, char *buf, size_t size)
{
	int ret = 0;
	struct evsel *pos;
	const char *group_name = evsel__group_name(evsel);

	if (!evsel->forced_leader)
		ret = scnprintf(buf, size, "%s { ", group_name);

	ret += scnprintf(buf + ret, size - ret, "%s", evsel__name(evsel));

	for_each_group_member(pos, evsel)
		ret += scnprintf(buf + ret, size - ret, ", %s", evsel__name(pos));

	if (!evsel->forced_leader)
		ret += scnprintf(buf + ret, size - ret, " }");

	return ret;
}

static void __evsel__config_callchain(struct evsel *evsel, struct record_opts *opts,
				      struct callchain_param *param)
{
	bool function = evsel__is_function_event(evsel);
	struct perf_event_attr *attr = &evsel->core.attr;

	evsel__set_sample_bit(evsel, CALLCHAIN);

	attr->sample_max_stack = param->max_stack;

	if (opts->kernel_callchains)
		attr->exclude_callchain_user = 1;
	if (opts->user_callchains)
		attr->exclude_callchain_kernel = 1;
	if (param->record_mode == CALLCHAIN_LBR) {
		if (!opts->branch_stack) {
			if (attr->exclude_user) {
				pr_warning("LBR callstack option is only available "
					   "to get user callchain information. "
					   "Falling back to framepointers.\n");
			} else {
				evsel__set_sample_bit(evsel, BRANCH_STACK);
				attr->branch_sample_type = PERF_SAMPLE_BRANCH_USER |
							PERF_SAMPLE_BRANCH_CALL_STACK |
							PERF_SAMPLE_BRANCH_NO_CYCLES |
							PERF_SAMPLE_BRANCH_NO_FLAGS |
							PERF_SAMPLE_BRANCH_HW_INDEX;
			}
		} else
			 pr_warning("Cannot use LBR callstack with branch stack. "
				    "Falling back to framepointers.\n");
	}

	if (param->record_mode == CALLCHAIN_DWARF) {
		if (!function) {
			evsel__set_sample_bit(evsel, REGS_USER);
			evsel__set_sample_bit(evsel, STACK_USER);
			if (opts->sample_user_regs && DWARF_MINIMAL_REGS != PERF_REGS_MASK) {
				attr->sample_regs_user |= DWARF_MINIMAL_REGS;
				pr_warning("WARNING: The use of --call-graph=dwarf may require all the user registers, "
					   "specifying a subset with --user-regs may render DWARF unwinding unreliable, "
					   "so the minimal registers set (IP, SP) is explicitly forced.\n");
			} else {
				attr->sample_regs_user |= arch__user_reg_mask();
			}
			attr->sample_stack_user = param->dump_size;
			attr->exclude_callchain_user = 1;
		} else {
			pr_info("Cannot use DWARF unwind for function trace event,"
				" falling back to framepointers.\n");
		}
	}

	if (function) {
		pr_info("Disabling user space callchains for function trace event.\n");
		attr->exclude_callchain_user = 1;
	}
}

void evsel__config_callchain(struct evsel *evsel, struct record_opts *opts,
			     struct callchain_param *param)
{
	if (param->enabled)
		return __evsel__config_callchain(evsel, opts, param);
}

static void evsel__reset_callgraph(struct evsel *evsel, struct callchain_param *param)
{
	struct perf_event_attr *attr = &evsel->core.attr;

	evsel__reset_sample_bit(evsel, CALLCHAIN);
	if (param->record_mode == CALLCHAIN_LBR) {
		evsel__reset_sample_bit(evsel, BRANCH_STACK);
		attr->branch_sample_type &= ~(PERF_SAMPLE_BRANCH_USER |
					      PERF_SAMPLE_BRANCH_CALL_STACK |
					      PERF_SAMPLE_BRANCH_HW_INDEX);
	}
	if (param->record_mode == CALLCHAIN_DWARF) {
		evsel__reset_sample_bit(evsel, REGS_USER);
		evsel__reset_sample_bit(evsel, STACK_USER);
	}
}

static void evsel__apply_config_terms(struct evsel *evsel,
				      struct record_opts *opts, bool track)
{
	struct evsel_config_term *term;
	struct list_head *config_terms = &evsel->config_terms;
	struct perf_event_attr *attr = &evsel->core.attr;
	/* callgraph default */
	struct callchain_param param = {
		.record_mode = callchain_param.record_mode,
	};
	u32 dump_size = 0;
	int max_stack = 0;
	const char *callgraph_buf = NULL;

	list_for_each_entry(term, config_terms, list) {
		switch (term->type) {
		case EVSEL__CONFIG_TERM_PERIOD:
			if (!(term->weak && opts->user_interval != ULLONG_MAX)) {
				attr->sample_period = term->val.period;
				attr->freq = 0;
				evsel__reset_sample_bit(evsel, PERIOD);
			}
			break;
		case EVSEL__CONFIG_TERM_FREQ:
			if (!(term->weak && opts->user_freq != UINT_MAX)) {
				attr->sample_freq = term->val.freq;
				attr->freq = 1;
				evsel__set_sample_bit(evsel, PERIOD);
			}
			break;
		case EVSEL__CONFIG_TERM_TIME:
			if (term->val.time)
				evsel__set_sample_bit(evsel, TIME);
			else
				evsel__reset_sample_bit(evsel, TIME);
			break;
		case EVSEL__CONFIG_TERM_CALLGRAPH:
			callgraph_buf = term->val.str;
			break;
		case EVSEL__CONFIG_TERM_BRANCH:
			if (term->val.str && strcmp(term->val.str, "no")) {
				evsel__set_sample_bit(evsel, BRANCH_STACK);
				parse_branch_str(term->val.str,
						 &attr->branch_sample_type);
			} else
				evsel__reset_sample_bit(evsel, BRANCH_STACK);
			break;
		case EVSEL__CONFIG_TERM_STACK_USER:
			dump_size = term->val.stack_user;
			break;
		case EVSEL__CONFIG_TERM_MAX_STACK:
			max_stack = term->val.max_stack;
			break;
		case EVSEL__CONFIG_TERM_MAX_EVENTS:
			evsel->max_events = term->val.max_events;
			break;
		case EVSEL__CONFIG_TERM_INHERIT:
			/*
			 * attr->inherit should has already been set by
			 * evsel__config. If user explicitly set
			 * inherit using config terms, override global
			 * opt->no_inherit setting.
			 */
			attr->inherit = term->val.inherit ? 1 : 0;
			break;
		case EVSEL__CONFIG_TERM_OVERWRITE:
			attr->write_backward = term->val.overwrite ? 1 : 0;
			break;
		case EVSEL__CONFIG_TERM_DRV_CFG:
			break;
		case EVSEL__CONFIG_TERM_PERCORE:
			break;
		case EVSEL__CONFIG_TERM_AUX_OUTPUT:
			attr->aux_output = term->val.aux_output ? 1 : 0;
			break;
		case EVSEL__CONFIG_TERM_AUX_SAMPLE_SIZE:
			/* Already applied by auxtrace */
			break;
		case EVSEL__CONFIG_TERM_CFG_CHG:
			break;
		default:
			break;
		}
	}

	/* User explicitly set per-event callgraph, clear the old setting and reset. */
	if ((callgraph_buf != NULL) || (dump_size > 0) || max_stack) {
		bool sample_address = false;

		if (max_stack) {
			param.max_stack = max_stack;
			if (callgraph_buf == NULL)
				callgraph_buf = "fp";
		}

		/* parse callgraph parameters */
		if (callgraph_buf != NULL) {
			if (!strcmp(callgraph_buf, "no")) {
				param.enabled = false;
				param.record_mode = CALLCHAIN_NONE;
			} else {
				param.enabled = true;
				if (parse_callchain_record(callgraph_buf, &param)) {
					pr_err("per-event callgraph setting for %s failed. "
					       "Apply callgraph global setting for it\n",
					       evsel->name);
					return;
				}
				if (param.record_mode == CALLCHAIN_DWARF)
					sample_address = true;
			}
		}
		if (dump_size > 0) {
			dump_size = round_up(dump_size, sizeof(u64));
			param.dump_size = dump_size;
		}

		/* If global callgraph set, clear it */
		if (callchain_param.enabled)
			evsel__reset_callgraph(evsel, &callchain_param);

		/* set perf-event callgraph */
		if (param.enabled) {
			if (sample_address) {
				evsel__set_sample_bit(evsel, ADDR);
				evsel__set_sample_bit(evsel, DATA_SRC);
				evsel->core.attr.mmap_data = track;
			}
			evsel__config_callchain(evsel, opts, &param);
		}
	}
}

struct evsel_config_term *__evsel__get_config_term(struct evsel *evsel, enum evsel_term_type type)
{
	struct evsel_config_term *term, *found_term = NULL;

	list_for_each_entry(term, &evsel->config_terms, list) {
		if (term->type == type)
			found_term = term;
	}

	return found_term;
}

void __weak arch_evsel__set_sample_weight(struct evsel *evsel)
{
	evsel__set_sample_bit(evsel, WEIGHT);
}

void __weak arch_evsel__fixup_new_cycles(struct perf_event_attr *attr __maybe_unused)
{
}

void __weak arch__post_evsel_config(struct evsel *evsel __maybe_unused,
				    struct perf_event_attr *attr __maybe_unused)
{
}

static void evsel__set_default_freq_period(struct record_opts *opts,
					   struct perf_event_attr *attr)
{
	if (opts->freq) {
		attr->freq = 1;
		attr->sample_freq = opts->freq;
	} else {
		attr->sample_period = opts->default_interval;
	}
}

static bool evsel__is_offcpu_event(struct evsel *evsel)
{
	return evsel__is_bpf_output(evsel) && evsel__name_is(evsel, OFFCPU_EVENT);
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
void evsel__config(struct evsel *evsel, struct record_opts *opts,
		   struct callchain_param *callchain)
{
	struct evsel *leader = evsel__leader(evsel);
	struct perf_event_attr *attr = &evsel->core.attr;
	int track = evsel->tracking;
	bool per_cpu = opts->target.default_per_cpu && !opts->target.per_thread;

	attr->sample_id_all = perf_missing_features.sample_id_all ? 0 : 1;
	attr->inherit	    = !opts->no_inherit;
	attr->write_backward = opts->overwrite ? 1 : 0;
	attr->read_format   = PERF_FORMAT_LOST;

	evsel__set_sample_bit(evsel, IP);
	evsel__set_sample_bit(evsel, TID);

	if (evsel->sample_read) {
		evsel__set_sample_bit(evsel, READ);

		/*
		 * We need ID even in case of single event, because
		 * PERF_SAMPLE_READ process ID specific data.
		 */
		evsel__set_sample_id(evsel, false);

		/*
		 * Apply group format only if we belong to group
		 * with more than one members.
		 */
		if (leader->core.nr_members > 1) {
			attr->read_format |= PERF_FORMAT_GROUP;
			attr->inherit = 0;
		}
	}

	/*
	 * We default some events to have a default interval. But keep
	 * it a weak assumption overridable by the user.
	 */
	if ((evsel->is_libpfm_event && !attr->sample_period) ||
	    (!evsel->is_libpfm_event && (!attr->sample_period ||
					 opts->user_freq != UINT_MAX ||
					 opts->user_interval != ULLONG_MAX)))
		evsel__set_default_freq_period(opts, attr);

	/*
	 * If attr->freq was set (here or earlier), ask for period
	 * to be sampled.
	 */
	if (attr->freq)
		evsel__set_sample_bit(evsel, PERIOD);

	if (opts->no_samples)
		attr->sample_freq = 0;

	if (opts->inherit_stat) {
		evsel->core.attr.read_format |=
			PERF_FORMAT_TOTAL_TIME_ENABLED |
			PERF_FORMAT_TOTAL_TIME_RUNNING |
			PERF_FORMAT_ID;
		attr->inherit_stat = 1;
	}

	if (opts->sample_address) {
		evsel__set_sample_bit(evsel, ADDR);
		attr->mmap_data = track;
	}

	/*
	 * We don't allow user space callchains for  function trace
	 * event, due to issues with page faults while tracing page
	 * fault handler and its overall trickiness nature.
	 */
	if (evsel__is_function_event(evsel))
		evsel->core.attr.exclude_callchain_user = 1;

	if (callchain && callchain->enabled && !evsel->no_aux_samples)
		evsel__config_callchain(evsel, opts, callchain);

	if (opts->sample_intr_regs && !evsel->no_aux_samples &&
	    !evsel__is_dummy_event(evsel)) {
		attr->sample_regs_intr = opts->sample_intr_regs;
		evsel__set_sample_bit(evsel, REGS_INTR);
	}

	if (opts->sample_user_regs && !evsel->no_aux_samples &&
	    !evsel__is_dummy_event(evsel)) {
		attr->sample_regs_user |= opts->sample_user_regs;
		evsel__set_sample_bit(evsel, REGS_USER);
	}

	if (target__has_cpu(&opts->target) || opts->sample_cpu)
		evsel__set_sample_bit(evsel, CPU);

	/*
	 * When the user explicitly disabled time don't force it here.
	 */
	if (opts->sample_time &&
	    (!perf_missing_features.sample_id_all &&
	    (!opts->no_inherit || target__has_cpu(&opts->target) || per_cpu ||
	     opts->sample_time_set)))
		evsel__set_sample_bit(evsel, TIME);

	if (opts->raw_samples && !evsel->no_aux_samples) {
		evsel__set_sample_bit(evsel, TIME);
		evsel__set_sample_bit(evsel, RAW);
		evsel__set_sample_bit(evsel, CPU);
	}

	if (opts->sample_address)
		evsel__set_sample_bit(evsel, DATA_SRC);

	if (opts->sample_phys_addr)
		evsel__set_sample_bit(evsel, PHYS_ADDR);

	if (opts->no_buffering) {
		attr->watermark = 0;
		attr->wakeup_events = 1;
	}
	if (opts->branch_stack && !evsel->no_aux_samples) {
		evsel__set_sample_bit(evsel, BRANCH_STACK);
		attr->branch_sample_type = opts->branch_stack;
	}

	if (opts->sample_weight)
		arch_evsel__set_sample_weight(evsel);

	attr->task     = track;
	attr->mmap     = track;
	attr->mmap2    = track && !perf_missing_features.mmap2;
	attr->comm     = track;
	attr->build_id = track && opts->build_id;

	/*
	 * ksymbol is tracked separately with text poke because it needs to be
	 * system wide and enabled immediately.
	 */
	if (!opts->text_poke)
		attr->ksymbol = track && !perf_missing_features.ksymbol;
	attr->bpf_event = track && !opts->no_bpf_event && !perf_missing_features.bpf;

	if (opts->record_namespaces)
		attr->namespaces  = track;

	if (opts->record_cgroup) {
		attr->cgroup = track && !perf_missing_features.cgroup;
		evsel__set_sample_bit(evsel, CGROUP);
	}

	if (opts->sample_data_page_size)
		evsel__set_sample_bit(evsel, DATA_PAGE_SIZE);

	if (opts->sample_code_page_size)
		evsel__set_sample_bit(evsel, CODE_PAGE_SIZE);

	if (opts->record_switch_events)
		attr->context_switch = track;

	if (opts->sample_transaction)
		evsel__set_sample_bit(evsel, TRANSACTION);

	if (opts->running_time) {
		evsel->core.attr.read_format |=
			PERF_FORMAT_TOTAL_TIME_ENABLED |
			PERF_FORMAT_TOTAL_TIME_RUNNING;
	}

	/*
	 * XXX see the function comment above
	 *
	 * Disabling only independent events or group leaders,
	 * keeping group members enabled.
	 */
	if (evsel__is_group_leader(evsel))
		attr->disabled = 1;

	/*
	 * Setting enable_on_exec for independent events and
	 * group leaders for traced executed by perf.
	 */
	if (target__none(&opts->target) && evsel__is_group_leader(evsel) &&
	    !opts->target.initial_delay)
		attr->enable_on_exec = 1;

	if (evsel->immediate) {
		attr->disabled = 0;
		attr->enable_on_exec = 0;
	}

	clockid = opts->clockid;
	if (opts->use_clockid) {
		attr->use_clockid = 1;
		attr->clockid = opts->clockid;
	}

	if (evsel->precise_max)
		attr->precise_ip = 3;

	if (opts->all_user) {
		attr->exclude_kernel = 1;
		attr->exclude_user   = 0;
	}

	if (opts->all_kernel) {
		attr->exclude_kernel = 0;
		attr->exclude_user   = 1;
	}

	if (evsel->core.own_cpus || evsel->unit)
		evsel->core.attr.read_format |= PERF_FORMAT_ID;

	/*
	 * Apply event specific term settings,
	 * it overloads any global configuration.
	 */
	evsel__apply_config_terms(evsel, opts, track);

	evsel->ignore_missing_thread = opts->ignore_missing_thread;

	/* The --period option takes the precedence. */
	if (opts->period_set) {
		if (opts->period)
			evsel__set_sample_bit(evsel, PERIOD);
		else
			evsel__reset_sample_bit(evsel, PERIOD);
	}

	/*
	 * A dummy event never triggers any actual counter and therefore
	 * cannot be used with branch_stack.
	 *
	 * For initial_delay, a dummy event is added implicitly.
	 * The software event will trigger -EOPNOTSUPP error out,
	 * if BRANCH_STACK bit is set.
	 */
	if (evsel__is_dummy_event(evsel))
		evsel__reset_sample_bit(evsel, BRANCH_STACK);

	if (evsel__is_offcpu_event(evsel))
		evsel->core.attr.sample_type &= OFFCPU_SAMPLE_TYPES;

	arch__post_evsel_config(evsel, attr);
}

int evsel__set_filter(struct evsel *evsel, const char *filter)
{
	char *new_filter = strdup(filter);

	if (new_filter != NULL) {
		free(evsel->filter);
		evsel->filter = new_filter;
		return 0;
	}

	return -1;
}

static int evsel__append_filter(struct evsel *evsel, const char *fmt, const char *filter)
{
	char *new_filter;

	if (evsel->filter == NULL)
		return evsel__set_filter(evsel, filter);

	if (asprintf(&new_filter, fmt, evsel->filter, filter) > 0) {
		free(evsel->filter);
		evsel->filter = new_filter;
		return 0;
	}

	return -1;
}

int evsel__append_tp_filter(struct evsel *evsel, const char *filter)
{
	return evsel__append_filter(evsel, "(%s) && (%s)", filter);
}

int evsel__append_addr_filter(struct evsel *evsel, const char *filter)
{
	return evsel__append_filter(evsel, "%s,%s", filter);
}

/* Caller has to clear disabled after going through all CPUs. */
int evsel__enable_cpu(struct evsel *evsel, int cpu_map_idx)
{
	return perf_evsel__enable_cpu(&evsel->core, cpu_map_idx);
}

int evsel__enable(struct evsel *evsel)
{
	int err = perf_evsel__enable(&evsel->core);

	if (!err)
		evsel->disabled = false;
	return err;
}

/* Caller has to set disabled after going through all CPUs. */
int evsel__disable_cpu(struct evsel *evsel, int cpu_map_idx)
{
	return perf_evsel__disable_cpu(&evsel->core, cpu_map_idx);
}

int evsel__disable(struct evsel *evsel)
{
	int err = perf_evsel__disable(&evsel->core);
	/*
	 * We mark it disabled here so that tools that disable a event can
	 * ignore events after they disable it. I.e. the ring buffer may have
	 * already a few more events queued up before the kernel got the stop
	 * request.
	 */
	if (!err)
		evsel->disabled = true;

	return err;
}

void free_config_terms(struct list_head *config_terms)
{
	struct evsel_config_term *term, *h;

	list_for_each_entry_safe(term, h, config_terms, list) {
		list_del_init(&term->list);
		if (term->free_str)
			zfree(&term->val.str);
		free(term);
	}
}

static void evsel__free_config_terms(struct evsel *evsel)
{
	free_config_terms(&evsel->config_terms);
}

void evsel__exit(struct evsel *evsel)
{
	assert(list_empty(&evsel->core.node));
	assert(evsel->evlist == NULL);
	bpf_counter__destroy(evsel);
	perf_bpf_filter__destroy(evsel);
	evsel__free_counts(evsel);
	perf_evsel__free_fd(&evsel->core);
	perf_evsel__free_id(&evsel->core);
	evsel__free_config_terms(evsel);
	cgroup__put(evsel->cgrp);
	perf_cpu_map__put(evsel->core.cpus);
	perf_cpu_map__put(evsel->core.own_cpus);
	perf_thread_map__put(evsel->core.threads);
	zfree(&evsel->group_name);
	zfree(&evsel->name);
	zfree(&evsel->pmu_name);
	zfree(&evsel->unit);
	zfree(&evsel->metric_id);
	evsel__zero_per_pkg(evsel);
	hashmap__free(evsel->per_pkg_mask);
	evsel->per_pkg_mask = NULL;
	zfree(&evsel->metric_events);
	perf_evsel__object.fini(evsel);
}

void evsel__delete(struct evsel *evsel)
{
	if (!evsel)
		return;

	evsel__exit(evsel);
	free(evsel);
}

void evsel__compute_deltas(struct evsel *evsel, int cpu_map_idx, int thread,
			   struct perf_counts_values *count)
{
	struct perf_counts_values tmp;

	if (!evsel->prev_raw_counts)
		return;

	tmp = *perf_counts(evsel->prev_raw_counts, cpu_map_idx, thread);
	*perf_counts(evsel->prev_raw_counts, cpu_map_idx, thread) = *count;

	count->val = count->val - tmp.val;
	count->ena = count->ena - tmp.ena;
	count->run = count->run - tmp.run;
}

static int evsel__read_one(struct evsel *evsel, int cpu_map_idx, int thread)
{
	struct perf_counts_values *count = perf_counts(evsel->counts, cpu_map_idx, thread);

	return perf_evsel__read(&evsel->core, cpu_map_idx, thread, count);
}

static void evsel__set_count(struct evsel *counter, int cpu_map_idx, int thread,
			     u64 val, u64 ena, u64 run, u64 lost)
{
	struct perf_counts_values *count;

	count = perf_counts(counter->counts, cpu_map_idx, thread);

	count->val    = val;
	count->ena    = ena;
	count->run    = run;
	count->lost   = lost;

	perf_counts__set_loaded(counter->counts, cpu_map_idx, thread, true);
}

static int evsel__process_group_data(struct evsel *leader, int cpu_map_idx, int thread, u64 *data)
{
	u64 read_format = leader->core.attr.read_format;
	struct sample_read_value *v;
	u64 nr, ena = 0, run = 0, lost = 0;

	nr = *data++;

	if (nr != (u64) leader->core.nr_members)
		return -EINVAL;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		ena = *data++;

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		run = *data++;

	v = (void *)data;
	sample_read_group__for_each(v, nr, read_format) {
		struct evsel *counter;

		counter = evlist__id2evsel(leader->evlist, v->id);
		if (!counter)
			return -EINVAL;

		if (read_format & PERF_FORMAT_LOST)
			lost = v->lost;

		evsel__set_count(counter, cpu_map_idx, thread, v->value, ena, run, lost);
	}

	return 0;
}

static int evsel__read_group(struct evsel *leader, int cpu_map_idx, int thread)
{
	struct perf_stat_evsel *ps = leader->stats;
	u64 read_format = leader->core.attr.read_format;
	int size = perf_evsel__read_size(&leader->core);
	u64 *data = ps->group_data;

	if (!(read_format & PERF_FORMAT_ID))
		return -EINVAL;

	if (!evsel__is_group_leader(leader))
		return -EINVAL;

	if (!data) {
		data = zalloc(size);
		if (!data)
			return -ENOMEM;

		ps->group_data = data;
	}

	if (FD(leader, cpu_map_idx, thread) < 0)
		return -EINVAL;

	if (readn(FD(leader, cpu_map_idx, thread), data, size) <= 0)
		return -errno;

	return evsel__process_group_data(leader, cpu_map_idx, thread, data);
}

int evsel__read_counter(struct evsel *evsel, int cpu_map_idx, int thread)
{
	u64 read_format = evsel->core.attr.read_format;

	if (read_format & PERF_FORMAT_GROUP)
		return evsel__read_group(evsel, cpu_map_idx, thread);

	return evsel__read_one(evsel, cpu_map_idx, thread);
}

int __evsel__read_on_cpu(struct evsel *evsel, int cpu_map_idx, int thread, bool scale)
{
	struct perf_counts_values count;
	size_t nv = scale ? 3 : 1;

	if (FD(evsel, cpu_map_idx, thread) < 0)
		return -EINVAL;

	if (evsel->counts == NULL && evsel__alloc_counts(evsel) < 0)
		return -ENOMEM;

	if (readn(FD(evsel, cpu_map_idx, thread), &count, nv * sizeof(u64)) <= 0)
		return -errno;

	evsel__compute_deltas(evsel, cpu_map_idx, thread, &count);
	perf_counts_values__scale(&count, scale, NULL);
	*perf_counts(evsel->counts, cpu_map_idx, thread) = count;
	return 0;
}

static int evsel__match_other_cpu(struct evsel *evsel, struct evsel *other,
				  int cpu_map_idx)
{
	struct perf_cpu cpu;

	cpu = perf_cpu_map__cpu(evsel->core.cpus, cpu_map_idx);
	return perf_cpu_map__idx(other->core.cpus, cpu);
}

static int evsel__hybrid_group_cpu_map_idx(struct evsel *evsel, int cpu_map_idx)
{
	struct evsel *leader = evsel__leader(evsel);

	if ((evsel__is_hybrid(evsel) && !evsel__is_hybrid(leader)) ||
	    (!evsel__is_hybrid(evsel) && evsel__is_hybrid(leader))) {
		return evsel__match_other_cpu(evsel, leader, cpu_map_idx);
	}

	return cpu_map_idx;
}

static int get_group_fd(struct evsel *evsel, int cpu_map_idx, int thread)
{
	struct evsel *leader = evsel__leader(evsel);
	int fd;

	if (evsel__is_group_leader(evsel))
		return -1;

	/*
	 * Leader must be already processed/open,
	 * if not it's a bug.
	 */
	BUG_ON(!leader->core.fd);

	cpu_map_idx = evsel__hybrid_group_cpu_map_idx(evsel, cpu_map_idx);
	if (cpu_map_idx == -1)
		return -1;

	fd = FD(leader, cpu_map_idx, thread);
	BUG_ON(fd == -1 && !leader->skippable);

	/*
	 * When the leader has been skipped, return -2 to distinguish from no
	 * group leader case.
	 */
	return fd == -1 ? -2 : fd;
}

static void evsel__remove_fd(struct evsel *pos, int nr_cpus, int nr_threads, int thread_idx)
{
	for (int cpu = 0; cpu < nr_cpus; cpu++)
		for (int thread = thread_idx; thread < nr_threads - 1; thread++)
			FD(pos, cpu, thread) = FD(pos, cpu, thread + 1);
}

static int update_fds(struct evsel *evsel,
		      int nr_cpus, int cpu_map_idx,
		      int nr_threads, int thread_idx)
{
	struct evsel *pos;

	if (cpu_map_idx >= nr_cpus || thread_idx >= nr_threads)
		return -EINVAL;

	evlist__for_each_entry(evsel->evlist, pos) {
		nr_cpus = pos != evsel ? nr_cpus : cpu_map_idx;

		evsel__remove_fd(pos, nr_cpus, nr_threads, thread_idx);

		/*
		 * Since fds for next evsel has not been created,
		 * there is no need to iterate whole event list.
		 */
		if (pos == evsel)
			break;
	}
	return 0;
}

static bool evsel__ignore_missing_thread(struct evsel *evsel,
					 int nr_cpus, int cpu_map_idx,
					 struct perf_thread_map *threads,
					 int thread, int err)
{
	pid_t ignore_pid = perf_thread_map__pid(threads, thread);

	if (!evsel->ignore_missing_thread)
		return false;

	/* The system wide setup does not work with threads. */
	if (evsel->core.system_wide)
		return false;

	/* The -ESRCH is perf event syscall errno for pid's not found. */
	if (err != -ESRCH)
		return false;

	/* If there's only one thread, let it fail. */
	if (threads->nr == 1)
		return false;

	/*
	 * We should remove fd for missing_thread first
	 * because thread_map__remove() will decrease threads->nr.
	 */
	if (update_fds(evsel, nr_cpus, cpu_map_idx, threads->nr, thread))
		return false;

	if (thread_map__remove(threads, thread))
		return false;

	pr_warning("WARNING: Ignored open failure for pid %d\n",
		   ignore_pid);
	return true;
}

static int __open_attr__fprintf(FILE *fp, const char *name, const char *val,
				void *priv __maybe_unused)
{
	return fprintf(fp, "  %-32s %s\n", name, val);
}

static void display_attr(struct perf_event_attr *attr)
{
	if (verbose >= 2 || debug_peo_args) {
		fprintf(stderr, "%.60s\n", graph_dotted_line);
		fprintf(stderr, "perf_event_attr:\n");
		perf_event_attr__fprintf(stderr, attr, __open_attr__fprintf, NULL);
		fprintf(stderr, "%.60s\n", graph_dotted_line);
	}
}

bool evsel__precise_ip_fallback(struct evsel *evsel)
{
	/* Do not try less precise if not requested. */
	if (!evsel->precise_max)
		return false;

	/*
	 * We tried all the precise_ip values, and it's
	 * still failing, so leave it to standard fallback.
	 */
	if (!evsel->core.attr.precise_ip) {
		evsel->core.attr.precise_ip = evsel->precise_ip_original;
		return false;
	}

	if (!evsel->precise_ip_original)
		evsel->precise_ip_original = evsel->core.attr.precise_ip;

	evsel->core.attr.precise_ip--;
	pr_debug2_peo("decreasing precise_ip by one (%d)\n", evsel->core.attr.precise_ip);
	display_attr(&evsel->core.attr);
	return true;
}

static struct perf_cpu_map *empty_cpu_map;
static struct perf_thread_map *empty_thread_map;

static int __evsel__prepare_open(struct evsel *evsel, struct perf_cpu_map *cpus,
		struct perf_thread_map *threads)
{
	int nthreads = perf_thread_map__nr(threads);

	if ((perf_missing_features.write_backward && evsel->core.attr.write_backward) ||
	    (perf_missing_features.aux_output     && evsel->core.attr.aux_output))
		return -EINVAL;

	if (cpus == NULL) {
		if (empty_cpu_map == NULL) {
			empty_cpu_map = perf_cpu_map__dummy_new();
			if (empty_cpu_map == NULL)
				return -ENOMEM;
		}

		cpus = empty_cpu_map;
	}

	if (threads == NULL) {
		if (empty_thread_map == NULL) {
			empty_thread_map = thread_map__new_by_tid(-1);
			if (empty_thread_map == NULL)
				return -ENOMEM;
		}

		threads = empty_thread_map;
	}

	if (evsel->core.fd == NULL &&
	    perf_evsel__alloc_fd(&evsel->core, perf_cpu_map__nr(cpus), nthreads) < 0)
		return -ENOMEM;

	evsel->open_flags = PERF_FLAG_FD_CLOEXEC;
	if (evsel->cgrp)
		evsel->open_flags |= PERF_FLAG_PID_CGROUP;

	return 0;
}

static void evsel__disable_missing_features(struct evsel *evsel)
{
	if (perf_missing_features.read_lost)
		evsel->core.attr.read_format &= ~PERF_FORMAT_LOST;
	if (perf_missing_features.weight_struct) {
		evsel__set_sample_bit(evsel, WEIGHT);
		evsel__reset_sample_bit(evsel, WEIGHT_STRUCT);
	}
	if (perf_missing_features.clockid_wrong)
		evsel->core.attr.clockid = CLOCK_MONOTONIC; /* should always work */
	if (perf_missing_features.clockid) {
		evsel->core.attr.use_clockid = 0;
		evsel->core.attr.clockid = 0;
	}
	if (perf_missing_features.cloexec)
		evsel->open_flags &= ~(unsigned long)PERF_FLAG_FD_CLOEXEC;
	if (perf_missing_features.mmap2)
		evsel->core.attr.mmap2 = 0;
	if (evsel->pmu && evsel->pmu->missing_features.exclude_guest)
		evsel->core.attr.exclude_guest = evsel->core.attr.exclude_host = 0;
	if (perf_missing_features.lbr_flags)
		evsel->core.attr.branch_sample_type &= ~(PERF_SAMPLE_BRANCH_NO_FLAGS |
				     PERF_SAMPLE_BRANCH_NO_CYCLES);
	if (perf_missing_features.group_read && evsel->core.attr.inherit)
		evsel->core.attr.read_format &= ~(PERF_FORMAT_GROUP|PERF_FORMAT_ID);
	if (perf_missing_features.ksymbol)
		evsel->core.attr.ksymbol = 0;
	if (perf_missing_features.bpf)
		evsel->core.attr.bpf_event = 0;
	if (perf_missing_features.branch_hw_idx)
		evsel->core.attr.branch_sample_type &= ~PERF_SAMPLE_BRANCH_HW_INDEX;
	if (perf_missing_features.sample_id_all)
		evsel->core.attr.sample_id_all = 0;
}

int evsel__prepare_open(struct evsel *evsel, struct perf_cpu_map *cpus,
			struct perf_thread_map *threads)
{
	int err;

	err = __evsel__prepare_open(evsel, cpus, threads);
	if (err)
		return err;

	evsel__disable_missing_features(evsel);

	return err;
}

bool evsel__detect_missing_features(struct evsel *evsel)
{
	/*
	 * Must probe features in the order they were added to the
	 * perf_event_attr interface.
	 */
	if (!perf_missing_features.read_lost &&
	    (evsel->core.attr.read_format & PERF_FORMAT_LOST)) {
		perf_missing_features.read_lost = true;
		pr_debug2("switching off PERF_FORMAT_LOST support\n");
		return true;
	} else if (!perf_missing_features.weight_struct &&
	    (evsel->core.attr.sample_type & PERF_SAMPLE_WEIGHT_STRUCT)) {
		perf_missing_features.weight_struct = true;
		pr_debug2("switching off weight struct support\n");
		return true;
	} else if (!perf_missing_features.code_page_size &&
	    (evsel->core.attr.sample_type & PERF_SAMPLE_CODE_PAGE_SIZE)) {
		perf_missing_features.code_page_size = true;
		pr_debug2_peo("Kernel has no PERF_SAMPLE_CODE_PAGE_SIZE support, bailing out\n");
		return false;
	} else if (!perf_missing_features.data_page_size &&
	    (evsel->core.attr.sample_type & PERF_SAMPLE_DATA_PAGE_SIZE)) {
		perf_missing_features.data_page_size = true;
		pr_debug2_peo("Kernel has no PERF_SAMPLE_DATA_PAGE_SIZE support, bailing out\n");
		return false;
	} else if (!perf_missing_features.cgroup && evsel->core.attr.cgroup) {
		perf_missing_features.cgroup = true;
		pr_debug2_peo("Kernel has no cgroup sampling support, bailing out\n");
		return false;
	} else if (!perf_missing_features.branch_hw_idx &&
	    (evsel->core.attr.branch_sample_type & PERF_SAMPLE_BRANCH_HW_INDEX)) {
		perf_missing_features.branch_hw_idx = true;
		pr_debug2("switching off branch HW index support\n");
		return true;
	} else if (!perf_missing_features.aux_output && evsel->core.attr.aux_output) {
		perf_missing_features.aux_output = true;
		pr_debug2_peo("Kernel has no attr.aux_output support, bailing out\n");
		return false;
	} else if (!perf_missing_features.bpf && evsel->core.attr.bpf_event) {
		perf_missing_features.bpf = true;
		pr_debug2_peo("switching off bpf_event\n");
		return true;
	} else if (!perf_missing_features.ksymbol && evsel->core.attr.ksymbol) {
		perf_missing_features.ksymbol = true;
		pr_debug2_peo("switching off ksymbol\n");
		return true;
	} else if (!perf_missing_features.write_backward && evsel->core.attr.write_backward) {
		perf_missing_features.write_backward = true;
		pr_debug2_peo("switching off write_backward\n");
		return false;
	} else if (!perf_missing_features.clockid_wrong && evsel->core.attr.use_clockid) {
		perf_missing_features.clockid_wrong = true;
		pr_debug2_peo("switching off clockid\n");
		return true;
	} else if (!perf_missing_features.clockid && evsel->core.attr.use_clockid) {
		perf_missing_features.clockid = true;
		pr_debug2_peo("switching off use_clockid\n");
		return true;
	} else if (!perf_missing_features.cloexec && (evsel->open_flags & PERF_FLAG_FD_CLOEXEC)) {
		perf_missing_features.cloexec = true;
		pr_debug2_peo("switching off cloexec flag\n");
		return true;
	} else if (!perf_missing_features.mmap2 && evsel->core.attr.mmap2) {
		perf_missing_features.mmap2 = true;
		pr_debug2_peo("switching off mmap2\n");
		return true;
	} else if (evsel->core.attr.exclude_guest || evsel->core.attr.exclude_host) {
		if (evsel->pmu == NULL)
			evsel->pmu = evsel__find_pmu(evsel);

		if (evsel->pmu)
			evsel->pmu->missing_features.exclude_guest = true;
		else {
			/* we cannot find PMU, disable attrs now */
			evsel->core.attr.exclude_host = false;
			evsel->core.attr.exclude_guest = false;
		}

		if (evsel->exclude_GH) {
			pr_debug2_peo("PMU has no exclude_host/guest support, bailing out\n");
			return false;
		}
		if (!perf_missing_features.exclude_guest) {
			perf_missing_features.exclude_guest = true;
			pr_debug2_peo("switching off exclude_guest, exclude_host\n");
		}
		return true;
	} else if (!perf_missing_features.sample_id_all) {
		perf_missing_features.sample_id_all = true;
		pr_debug2_peo("switching off sample_id_all\n");
		return true;
	} else if (!perf_missing_features.lbr_flags &&
			(evsel->core.attr.branch_sample_type &
			 (PERF_SAMPLE_BRANCH_NO_CYCLES |
			  PERF_SAMPLE_BRANCH_NO_FLAGS))) {
		perf_missing_features.lbr_flags = true;
		pr_debug2_peo("switching off branch sample type no (cycles/flags)\n");
		return true;
	} else if (!perf_missing_features.group_read &&
		    evsel->core.attr.inherit &&
		   (evsel->core.attr.read_format & PERF_FORMAT_GROUP) &&
		   evsel__is_group_leader(evsel)) {
		perf_missing_features.group_read = true;
		pr_debug2_peo("switching off group read\n");
		return true;
	} else {
		return false;
	}
}

bool evsel__increase_rlimit(enum rlimit_action *set_rlimit)
{
	int old_errno;
	struct rlimit l;

	if (*set_rlimit < INCREASED_MAX) {
		old_errno = errno;

		if (getrlimit(RLIMIT_NOFILE, &l) == 0) {
			if (*set_rlimit == NO_CHANGE) {
				l.rlim_cur = l.rlim_max;
			} else {
				l.rlim_cur = l.rlim_max + 1000;
				l.rlim_max = l.rlim_cur;
			}
			if (setrlimit(RLIMIT_NOFILE, &l) == 0) {
				(*set_rlimit) += 1;
				errno = old_errno;
				return true;
			}
		}
		errno = old_errno;
	}

	return false;
}

static int evsel__open_cpu(struct evsel *evsel, struct perf_cpu_map *cpus,
		struct perf_thread_map *threads,
		int start_cpu_map_idx, int end_cpu_map_idx)
{
	int idx, thread, nthreads;
	int pid = -1, err, old_errno;
	enum rlimit_action set_rlimit = NO_CHANGE;

	err = __evsel__prepare_open(evsel, cpus, threads);
	if (err)
		return err;

	if (cpus == NULL)
		cpus = empty_cpu_map;

	if (threads == NULL)
		threads = empty_thread_map;

	nthreads = perf_thread_map__nr(threads);

	if (evsel->cgrp)
		pid = evsel->cgrp->fd;

fallback_missing_features:
	evsel__disable_missing_features(evsel);

	display_attr(&evsel->core.attr);

	for (idx = start_cpu_map_idx; idx < end_cpu_map_idx; idx++) {

		for (thread = 0; thread < nthreads; thread++) {
			int fd, group_fd;
retry_open:
			if (thread >= nthreads)
				break;

			if (!evsel->cgrp && !evsel->core.system_wide)
				pid = perf_thread_map__pid(threads, thread);

			group_fd = get_group_fd(evsel, idx, thread);

			if (group_fd == -2) {
				pr_debug("broken group leader for %s\n", evsel->name);
				err = -EINVAL;
				goto out_close;
			}

			test_attr__ready();

			/* Debug message used by test scripts */
			pr_debug2_peo("sys_perf_event_open: pid %d  cpu %d  group_fd %d  flags %#lx",
				pid, perf_cpu_map__cpu(cpus, idx).cpu, group_fd, evsel->open_flags);

			fd = sys_perf_event_open(&evsel->core.attr, pid,
						perf_cpu_map__cpu(cpus, idx).cpu,
						group_fd, evsel->open_flags);

			FD(evsel, idx, thread) = fd;

			if (fd < 0) {
				err = -errno;

				pr_debug2_peo("\nsys_perf_event_open failed, error %d\n",
					  err);
				goto try_fallback;
			}

			bpf_counter__install_pe(evsel, idx, fd);

			if (unlikely(test_attr__enabled)) {
				test_attr__open(&evsel->core.attr, pid,
						perf_cpu_map__cpu(cpus, idx),
						fd, group_fd, evsel->open_flags);
			}

			/* Debug message used by test scripts */
			pr_debug2_peo(" = %d\n", fd);

			if (evsel->bpf_fd >= 0) {
				int evt_fd = fd;
				int bpf_fd = evsel->bpf_fd;

				err = ioctl(evt_fd,
					    PERF_EVENT_IOC_SET_BPF,
					    bpf_fd);
				if (err && errno != EEXIST) {
					pr_err("failed to attach bpf fd %d: %s\n",
					       bpf_fd, strerror(errno));
					err = -EINVAL;
					goto out_close;
				}
			}

			set_rlimit = NO_CHANGE;

			/*
			 * If we succeeded but had to kill clockid, fail and
			 * have evsel__open_strerror() print us a nice error.
			 */
			if (perf_missing_features.clockid ||
			    perf_missing_features.clockid_wrong) {
				err = -EINVAL;
				goto out_close;
			}
		}
	}

	return 0;

try_fallback:
	if (evsel__precise_ip_fallback(evsel))
		goto retry_open;

	if (evsel__ignore_missing_thread(evsel, perf_cpu_map__nr(cpus),
					 idx, threads, thread, err)) {
		/* We just removed 1 thread, so lower the upper nthreads limit. */
		nthreads--;

		/* ... and pretend like nothing have happened. */
		err = 0;
		goto retry_open;
	}
	/*
	 * perf stat needs between 5 and 22 fds per CPU. When we run out
	 * of them try to increase the limits.
	 */
	if (err == -EMFILE && evsel__increase_rlimit(&set_rlimit))
		goto retry_open;

	if (err != -EINVAL || idx > 0 || thread > 0)
		goto out_close;

	if (evsel__detect_missing_features(evsel))
		goto fallback_missing_features;
out_close:
	if (err)
		threads->err_thread = thread;

	old_errno = errno;
	do {
		while (--thread >= 0) {
			if (FD(evsel, idx, thread) >= 0)
				close(FD(evsel, idx, thread));
			FD(evsel, idx, thread) = -1;
		}
		thread = nthreads;
	} while (--idx >= 0);
	errno = old_errno;
	return err;
}

int evsel__open(struct evsel *evsel, struct perf_cpu_map *cpus,
		struct perf_thread_map *threads)
{
	return evsel__open_cpu(evsel, cpus, threads, 0, perf_cpu_map__nr(cpus));
}

void evsel__close(struct evsel *evsel)
{
	perf_evsel__close(&evsel->core);
	perf_evsel__free_id(&evsel->core);
}

int evsel__open_per_cpu(struct evsel *evsel, struct perf_cpu_map *cpus, int cpu_map_idx)
{
	if (cpu_map_idx == -1)
		return evsel__open_cpu(evsel, cpus, NULL, 0, perf_cpu_map__nr(cpus));

	return evsel__open_cpu(evsel, cpus, NULL, cpu_map_idx, cpu_map_idx + 1);
}

int evsel__open_per_thread(struct evsel *evsel, struct perf_thread_map *threads)
{
	return evsel__open(evsel, NULL, threads);
}

static int perf_evsel__parse_id_sample(const struct evsel *evsel,
				       const union perf_event *event,
				       struct perf_sample *sample)
{
	u64 type = evsel->core.attr.sample_type;
	const __u64 *array = event->sample.array;
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

static int
perf_event__check_size(union perf_event *event, unsigned int sample_size)
{
	/*
	 * The evsel's sample_size is based on PERF_SAMPLE_MASK which includes
	 * up to PERF_SAMPLE_PERIOD.  After that overflow() must be used to
	 * check the format does not go past the end of the event.
	 */
	if (sample_size + sizeof(event->header) > event->header.size)
		return -EFAULT;

	return 0;
}

void __weak arch_perf_parse_sample_weight(struct perf_sample *data,
					  const __u64 *array,
					  u64 type __maybe_unused)
{
	data->weight = *array;
}

u64 evsel__bitfield_swap_branch_flags(u64 value)
{
	u64 new_val = 0;

	/*
	 * branch_flags
	 * union {
	 * 	u64 values;
	 * 	struct {
	 * 		mispred:1	//target mispredicted
	 * 		predicted:1	//target predicted
	 * 		in_tx:1		//in transaction
	 * 		abort:1		//transaction abort
	 * 		cycles:16	//cycle count to last branch
	 * 		type:4		//branch type
	 * 		spec:2		//branch speculation info
	 * 		new_type:4	//additional branch type
	 * 		priv:3		//privilege level
	 * 		reserved:31
	 * 	}
	 * }
	 *
	 * Avoid bswap64() the entire branch_flag.value,
	 * as it has variable bit-field sizes. Instead the
	 * macro takes the bit-field position/size,
	 * swaps it based on the host endianness.
	 */
	if (host_is_bigendian()) {
		new_val = bitfield_swap(value, 0, 1);
		new_val |= bitfield_swap(value, 1, 1);
		new_val |= bitfield_swap(value, 2, 1);
		new_val |= bitfield_swap(value, 3, 1);
		new_val |= bitfield_swap(value, 4, 16);
		new_val |= bitfield_swap(value, 20, 4);
		new_val |= bitfield_swap(value, 24, 2);
		new_val |= bitfield_swap(value, 26, 4);
		new_val |= bitfield_swap(value, 30, 3);
		new_val |= bitfield_swap(value, 33, 31);
	} else {
		new_val = bitfield_swap(value, 63, 1);
		new_val |= bitfield_swap(value, 62, 1);
		new_val |= bitfield_swap(value, 61, 1);
		new_val |= bitfield_swap(value, 60, 1);
		new_val |= bitfield_swap(value, 44, 16);
		new_val |= bitfield_swap(value, 40, 4);
		new_val |= bitfield_swap(value, 38, 2);
		new_val |= bitfield_swap(value, 34, 4);
		new_val |= bitfield_swap(value, 31, 3);
		new_val |= bitfield_swap(value, 0, 31);
	}

	return new_val;
}

int evsel__parse_sample(struct evsel *evsel, union perf_event *event,
			struct perf_sample *data)
{
	u64 type = evsel->core.attr.sample_type;
	bool swapped = evsel->needs_swap;
	const __u64 *array;
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
	data->period = evsel->core.attr.sample_period;
	data->cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	data->misc    = event->header.misc;
	data->id = -1ULL;
	data->data_src = PERF_MEM_DATA_SRC_NONE;
	data->vcpu = -1;

	if (event->header.type != PERF_RECORD_SAMPLE) {
		if (!evsel->core.attr.sample_id_all)
			return 0;
		return perf_evsel__parse_id_sample(evsel, event, data);
	}

	array = event->sample.array;

	if (perf_event__check_size(event, evsel->sample_size))
		return -EFAULT;

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
		u64 read_format = evsel->core.attr.read_format;

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

			sz = data->read.group.nr * sample_read_value_size(read_format);
			OVERFLOW_CHECK(array, sz, max_size);
			data->read.group.values =
					(struct sample_read_value *)array;
			array = (void *)array + sz;
		} else {
			OVERFLOW_CHECK_u64(array);
			data->read.one.id = *array;
			array++;

			if (read_format & PERF_FORMAT_LOST) {
				OVERFLOW_CHECK_u64(array);
				data->read.one.lost = *array;
				array++;
			}
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

		/*
		 * Undo swap of u64, then swap on individual u32s,
		 * get the size of the raw area and undo all of the
		 * swap. The pevent interface handles endianness by
		 * itself.
		 */
		if (swapped) {
			u.val64 = bswap_64(u.val64);
			u.val32[0] = bswap_32(u.val32[0]);
			u.val32[1] = bswap_32(u.val32[1]);
		}
		data->raw_size = u.val32[0];

		/*
		 * The raw data is aligned on 64bits including the
		 * u32 size, so it's safe to use mem_bswap_64.
		 */
		if (swapped)
			mem_bswap_64((void *) array, data->raw_size);

		array = (void *)array + sizeof(u32);

		OVERFLOW_CHECK(array, data->raw_size, max_size);
		data->raw_data = (void *)array;
		array = (void *)array + data->raw_size;
	}

	if (type & PERF_SAMPLE_BRANCH_STACK) {
		const u64 max_branch_nr = UINT64_MAX /
					  sizeof(struct branch_entry);
		struct branch_entry *e;
		unsigned int i;

		OVERFLOW_CHECK_u64(array);
		data->branch_stack = (struct branch_stack *)array++;

		if (data->branch_stack->nr > max_branch_nr)
			return -EFAULT;

		sz = data->branch_stack->nr * sizeof(struct branch_entry);
		if (evsel__has_branch_hw_idx(evsel)) {
			sz += sizeof(u64);
			e = &data->branch_stack->entries[0];
		} else {
			data->no_hw_idx = true;
			/*
			 * if the PERF_SAMPLE_BRANCH_HW_INDEX is not applied,
			 * only nr and entries[] will be output by kernel.
			 */
			e = (struct branch_entry *)&data->branch_stack->hw_idx;
		}

		if (swapped) {
			/*
			 * struct branch_flag does not have endian
			 * specific bit field definition. And bswap
			 * will not resolve the issue, since these
			 * are bit fields.
			 *
			 * evsel__bitfield_swap_branch_flags() uses a
			 * bitfield_swap macro to swap the bit position
			 * based on the host endians.
			 */
			for (i = 0; i < data->branch_stack->nr; i++, e++)
				e->flags.value = evsel__bitfield_swap_branch_flags(e->flags.value);
		}

		OVERFLOW_CHECK(array, sz, max_size);
		array = (void *)array + sz;
	}

	if (type & PERF_SAMPLE_REGS_USER) {
		OVERFLOW_CHECK_u64(array);
		data->user_regs.abi = *array;
		array++;

		if (data->user_regs.abi) {
			u64 mask = evsel->core.attr.sample_regs_user;

			sz = hweight64(mask) * sizeof(u64);
			OVERFLOW_CHECK(array, sz, max_size);
			data->user_regs.mask = mask;
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

	if (type & PERF_SAMPLE_WEIGHT_TYPE) {
		OVERFLOW_CHECK_u64(array);
		arch_perf_parse_sample_weight(data, array, type);
		array++;
	}

	if (type & PERF_SAMPLE_DATA_SRC) {
		OVERFLOW_CHECK_u64(array);
		data->data_src = *array;
		array++;
	}

	if (type & PERF_SAMPLE_TRANSACTION) {
		OVERFLOW_CHECK_u64(array);
		data->transaction = *array;
		array++;
	}

	data->intr_regs.abi = PERF_SAMPLE_REGS_ABI_NONE;
	if (type & PERF_SAMPLE_REGS_INTR) {
		OVERFLOW_CHECK_u64(array);
		data->intr_regs.abi = *array;
		array++;

		if (data->intr_regs.abi != PERF_SAMPLE_REGS_ABI_NONE) {
			u64 mask = evsel->core.attr.sample_regs_intr;

			sz = hweight64(mask) * sizeof(u64);
			OVERFLOW_CHECK(array, sz, max_size);
			data->intr_regs.mask = mask;
			data->intr_regs.regs = (u64 *)array;
			array = (void *)array + sz;
		}
	}

	data->phys_addr = 0;
	if (type & PERF_SAMPLE_PHYS_ADDR) {
		data->phys_addr = *array;
		array++;
	}

	data->cgroup = 0;
	if (type & PERF_SAMPLE_CGROUP) {
		data->cgroup = *array;
		array++;
	}

	data->data_page_size = 0;
	if (type & PERF_SAMPLE_DATA_PAGE_SIZE) {
		data->data_page_size = *array;
		array++;
	}

	data->code_page_size = 0;
	if (type & PERF_SAMPLE_CODE_PAGE_SIZE) {
		data->code_page_size = *array;
		array++;
	}

	if (type & PERF_SAMPLE_AUX) {
		OVERFLOW_CHECK_u64(array);
		sz = *array++;

		OVERFLOW_CHECK(array, sz, max_size);
		/* Undo swap of data */
		if (swapped)
			mem_bswap_64((char *)array, sz);
		data->aux_sample.size = sz;
		data->aux_sample.data = (char *)array;
		array = (void *)array + sz;
	}

	return 0;
}

int evsel__parse_sample_timestamp(struct evsel *evsel, union perf_event *event,
				  u64 *timestamp)
{
	u64 type = evsel->core.attr.sample_type;
	const __u64 *array;

	if (!(type & PERF_SAMPLE_TIME))
		return -1;

	if (event->header.type != PERF_RECORD_SAMPLE) {
		struct perf_sample data = {
			.time = -1ULL,
		};

		if (!evsel->core.attr.sample_id_all)
			return -1;
		if (perf_evsel__parse_id_sample(evsel, event, &data))
			return -1;

		*timestamp = data.time;
		return 0;
	}

	array = event->sample.array;

	if (perf_event__check_size(event, evsel->sample_size))
		return -EFAULT;

	if (type & PERF_SAMPLE_IDENTIFIER)
		array++;

	if (type & PERF_SAMPLE_IP)
		array++;

	if (type & PERF_SAMPLE_TID)
		array++;

	if (type & PERF_SAMPLE_TIME)
		*timestamp = *array;

	return 0;
}

u16 evsel__id_hdr_size(struct evsel *evsel)
{
	u64 sample_type = evsel->core.attr.sample_type;
	u16 size = 0;

	if (sample_type & PERF_SAMPLE_TID)
		size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_TIME)
		size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_ID)
		size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_STREAM_ID)
		size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_CPU)
		size += sizeof(u64);

	if (sample_type & PERF_SAMPLE_IDENTIFIER)
		size += sizeof(u64);

	return size;
}

#ifdef HAVE_LIBTRACEEVENT
struct tep_format_field *evsel__field(struct evsel *evsel, const char *name)
{
	return tep_find_field(evsel->tp_format, name);
}

void *evsel__rawptr(struct evsel *evsel, struct perf_sample *sample, const char *name)
{
	struct tep_format_field *field = evsel__field(evsel, name);
	int offset;

	if (!field)
		return NULL;

	offset = field->offset;

	if (field->flags & TEP_FIELD_IS_DYNAMIC) {
		offset = *(int *)(sample->raw_data + field->offset);
		offset &= 0xffff;
		if (tep_field_is_relative(field->flags))
			offset += field->offset + field->size;
	}

	return sample->raw_data + offset;
}

u64 format_field__intval(struct tep_format_field *field, struct perf_sample *sample,
			 bool needs_swap)
{
	u64 value;
	void *ptr = sample->raw_data + field->offset;

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
		memcpy(&value, ptr, sizeof(u64));
		break;
	default:
		return 0;
	}

	if (!needs_swap)
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

u64 evsel__intval(struct evsel *evsel, struct perf_sample *sample, const char *name)
{
	struct tep_format_field *field = evsel__field(evsel, name);

	if (!field)
		return 0;

	return field ? format_field__intval(field, sample, evsel->needs_swap) : 0;
}
#endif

bool evsel__fallback(struct evsel *evsel, int err, char *msg, size_t msgsize)
{
	int paranoid;

	if ((err == ENOENT || err == ENXIO || err == ENODEV) &&
	    evsel->core.attr.type   == PERF_TYPE_HARDWARE &&
	    evsel->core.attr.config == PERF_COUNT_HW_CPU_CYCLES) {
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

		evsel->core.attr.type   = PERF_TYPE_SOFTWARE;
		evsel->core.attr.config = PERF_COUNT_SW_CPU_CLOCK;

		zfree(&evsel->name);
		return true;
	} else if (err == EACCES && !evsel->core.attr.exclude_kernel &&
		   (paranoid = perf_event_paranoid()) > 1) {
		const char *name = evsel__name(evsel);
		char *new_name;
		const char *sep = ":";

		/* If event has exclude user then don't exclude kernel. */
		if (evsel->core.attr.exclude_user)
			return false;

		/* Is there already the separator in the name. */
		if (strchr(name, '/') ||
		    (strchr(name, ':') && !evsel->is_libpfm_event))
			sep = "";

		if (asprintf(&new_name, "%s%su", name, sep) < 0)
			return false;

		free(evsel->name);
		evsel->name = new_name;
		scnprintf(msg, msgsize, "kernel.perf_event_paranoid=%d, trying "
			  "to fall back to excluding kernel and hypervisor "
			  " samples", paranoid);
		evsel->core.attr.exclude_kernel = 1;
		evsel->core.attr.exclude_hv     = 1;

		return true;
	}

	return false;
}

static bool find_process(const char *name)
{
	size_t len = strlen(name);
	DIR *dir;
	struct dirent *d;
	int ret = -1;

	dir = opendir(procfs__mountpoint());
	if (!dir)
		return false;

	/* Walk through the directory. */
	while (ret && (d = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		char *data;
		size_t size;

		if ((d->d_type != DT_DIR) ||
		     !strcmp(".", d->d_name) ||
		     !strcmp("..", d->d_name))
			continue;

		scnprintf(path, sizeof(path), "%s/%s/comm",
			  procfs__mountpoint(), d->d_name);

		if (filename__read_str(path, &data, &size))
			continue;

		ret = strncmp(name, data, len);
		free(data);
	}

	closedir(dir);
	return ret ? false : true;
}

static bool is_amd(const char *arch, const char *cpuid)
{
	return arch && !strcmp("x86", arch) && cpuid && strstarts(cpuid, "AuthenticAMD");
}

static bool is_amd_ibs(struct evsel *evsel)
{
	return evsel->core.attr.precise_ip
	    || (evsel->pmu_name && !strncmp(evsel->pmu_name, "ibs", 3));
}

int evsel__open_strerror(struct evsel *evsel, struct target *target,
			 int err, char *msg, size_t size)
{
	struct perf_env *env = evsel__env(evsel);
	const char *arch = perf_env__arch(env);
	const char *cpuid = perf_env__cpuid(env);
	char sbuf[STRERR_BUFSIZE];
	int printed = 0, enforced = 0;

	switch (err) {
	case EPERM:
	case EACCES:
		printed += scnprintf(msg + printed, size - printed,
			"Access to performance monitoring and observability operations is limited.\n");

		if (!sysfs__read_int("fs/selinux/enforce", &enforced)) {
			if (enforced) {
				printed += scnprintf(msg + printed, size - printed,
					"Enforced MAC policy settings (SELinux) can limit access to performance\n"
					"monitoring and observability operations. Inspect system audit records for\n"
					"more perf_event access control information and adjusting the policy.\n");
			}
		}

		if (err == EPERM)
			printed += scnprintf(msg, size,
				"No permission to enable %s event.\n\n", evsel__name(evsel));

		return scnprintf(msg + printed, size - printed,
		 "Consider adjusting /proc/sys/kernel/perf_event_paranoid setting to open\n"
		 "access to performance monitoring and observability operations for processes\n"
		 "without CAP_PERFMON, CAP_SYS_PTRACE or CAP_SYS_ADMIN Linux capability.\n"
		 "More information can be found at 'Perf events and tool security' document:\n"
		 "https://www.kernel.org/doc/html/latest/admin-guide/perf-security.html\n"
		 "perf_event_paranoid setting is %d:\n"
		 "  -1: Allow use of (almost) all events by all users\n"
		 "      Ignore mlock limit after perf_event_mlock_kb without CAP_IPC_LOCK\n"
		 ">= 0: Disallow raw and ftrace function tracepoint access\n"
		 ">= 1: Disallow CPU event access\n"
		 ">= 2: Disallow kernel profiling\n"
		 "To make the adjusted perf_event_paranoid setting permanent preserve it\n"
		 "in /etc/sysctl.conf (e.g. kernel.perf_event_paranoid = <setting>)",
		 perf_event_paranoid());
	case ENOENT:
		return scnprintf(msg, size, "The %s event is not supported.", evsel__name(evsel));
	case EMFILE:
		return scnprintf(msg, size, "%s",
			 "Too many events are opened.\n"
			 "Probably the maximum number of open file descriptors has been reached.\n"
			 "Hint: Try again after reducing the number of events.\n"
			 "Hint: Try increasing the limit with 'ulimit -n <limit>'");
	case ENOMEM:
		if (evsel__has_callchain(evsel) &&
		    access("/proc/sys/kernel/perf_event_max_stack", F_OK) == 0)
			return scnprintf(msg, size,
					 "Not enough memory to setup event with callchain.\n"
					 "Hint: Try tweaking /proc/sys/kernel/perf_event_max_stack\n"
					 "Hint: Current value: %d", sysctl__max_stack());
		break;
	case ENODEV:
		if (target->cpu_list)
			return scnprintf(msg, size, "%s",
	 "No such device - did you specify an out-of-range profile CPU?");
		break;
	case EOPNOTSUPP:
		if (evsel->core.attr.sample_type & PERF_SAMPLE_BRANCH_STACK)
			return scnprintf(msg, size,
	"%s: PMU Hardware or event type doesn't support branch stack sampling.",
					 evsel__name(evsel));
		if (evsel->core.attr.aux_output)
			return scnprintf(msg, size,
	"%s: PMU Hardware doesn't support 'aux_output' feature",
					 evsel__name(evsel));
		if (evsel->core.attr.sample_period != 0)
			return scnprintf(msg, size,
	"%s: PMU Hardware doesn't support sampling/overflow-interrupts. Try 'perf stat'",
					 evsel__name(evsel));
		if (evsel->core.attr.precise_ip)
			return scnprintf(msg, size, "%s",
	"\'precise\' request may not be supported. Try removing 'p' modifier.");
#if defined(__i386__) || defined(__x86_64__)
		if (evsel->core.attr.type == PERF_TYPE_HARDWARE)
			return scnprintf(msg, size, "%s",
	"No hardware sampling interrupt available.\n");
#endif
		break;
	case EBUSY:
		if (find_process("oprofiled"))
			return scnprintf(msg, size,
	"The PMU counters are busy/taken by another profiler.\n"
	"We found oprofile daemon running, please stop it and try again.");
		break;
	case EINVAL:
		if (evsel->core.attr.sample_type & PERF_SAMPLE_CODE_PAGE_SIZE && perf_missing_features.code_page_size)
			return scnprintf(msg, size, "Asking for the code page size isn't supported by this kernel.");
		if (evsel->core.attr.sample_type & PERF_SAMPLE_DATA_PAGE_SIZE && perf_missing_features.data_page_size)
			return scnprintf(msg, size, "Asking for the data page size isn't supported by this kernel.");
		if (evsel->core.attr.write_backward && perf_missing_features.write_backward)
			return scnprintf(msg, size, "Reading from overwrite event is not supported by this kernel.");
		if (perf_missing_features.clockid)
			return scnprintf(msg, size, "clockid feature not supported.");
		if (perf_missing_features.clockid_wrong)
			return scnprintf(msg, size, "wrong clockid (%d).", clockid);
		if (perf_missing_features.aux_output)
			return scnprintf(msg, size, "The 'aux_output' feature is not supported, update the kernel.");
		if (!target__has_cpu(target))
			return scnprintf(msg, size,
	"Invalid event (%s) in per-thread mode, enable system wide with '-a'.",
					evsel__name(evsel));
		if (is_amd(arch, cpuid)) {
			if (is_amd_ibs(evsel)) {
				if (evsel->core.attr.exclude_kernel)
					return scnprintf(msg, size,
	"AMD IBS can't exclude kernel events.  Try running at a higher privilege level.");
				if (!evsel->core.system_wide)
					return scnprintf(msg, size,
	"AMD IBS may only be available in system-wide/per-cpu mode.  Try using -a, or -C and workload affinity");
			}
		}

		break;
	case ENODATA:
		return scnprintf(msg, size, "Cannot collect data source with the load latency event alone. "
				 "Please add an auxiliary event in front of the load latency event.");
	default:
		break;
	}

	return scnprintf(msg, size,
	"The sys_perf_event_open() syscall returned with %d (%s) for event (%s).\n"
	"/bin/dmesg | grep -i perf may provide additional information.\n",
			 err, str_error_r(err, sbuf, sizeof(sbuf)), evsel__name(evsel));
}

struct perf_env *evsel__env(struct evsel *evsel)
{
	if (evsel && evsel->evlist && evsel->evlist->env)
		return evsel->evlist->env;
	return &perf_env;
}

static int store_evsel_ids(struct evsel *evsel, struct evlist *evlist)
{
	int cpu_map_idx, thread;

	for (cpu_map_idx = 0; cpu_map_idx < xyarray__max_x(evsel->core.fd); cpu_map_idx++) {
		for (thread = 0; thread < xyarray__max_y(evsel->core.fd);
		     thread++) {
			int fd = FD(evsel, cpu_map_idx, thread);

			if (perf_evlist__id_add_fd(&evlist->core, &evsel->core,
						   cpu_map_idx, thread, fd) < 0)
				return -1;
		}
	}

	return 0;
}

int evsel__store_ids(struct evsel *evsel, struct evlist *evlist)
{
	struct perf_cpu_map *cpus = evsel->core.cpus;
	struct perf_thread_map *threads = evsel->core.threads;

	if (perf_evsel__alloc_id(&evsel->core, perf_cpu_map__nr(cpus), threads->nr))
		return -ENOMEM;

	return store_evsel_ids(evsel, evlist);
}

void evsel__zero_per_pkg(struct evsel *evsel)
{
	struct hashmap_entry *cur;
	size_t bkt;

	if (evsel->per_pkg_mask) {
		hashmap__for_each_entry(evsel->per_pkg_mask, cur, bkt)
			zfree(&cur->pkey);

		hashmap__clear(evsel->per_pkg_mask);
	}
}

bool evsel__is_hybrid(const struct evsel *evsel)
{
	return evsel->pmu_name && perf_pmu__is_hybrid(evsel->pmu_name);
}

struct evsel *evsel__leader(const struct evsel *evsel)
{
	return container_of(evsel->core.leader, struct evsel, core);
}

bool evsel__has_leader(struct evsel *evsel, struct evsel *leader)
{
	return evsel->core.leader == &leader->core;
}

bool evsel__is_leader(struct evsel *evsel)
{
	return evsel__has_leader(evsel, evsel);
}

void evsel__set_leader(struct evsel *evsel, struct evsel *leader)
{
	evsel->core.leader = &leader->core;
}

int evsel__source_count(const struct evsel *evsel)
{
	struct evsel *pos;
	int count = 0;

	evlist__for_each_entry(evsel->evlist, pos) {
		if (pos->metric_leader == evsel)
			count++;
	}
	return count;
}

bool __weak arch_evsel__must_be_in_group(const struct evsel *evsel __maybe_unused)
{
	return false;
}

/*
 * Remove an event from a given group (leader).
 * Some events, e.g., perf metrics Topdown events,
 * must always be grouped. Ignore the events.
 */
void evsel__remove_from_group(struct evsel *evsel, struct evsel *leader)
{
	if (!arch_evsel__must_be_in_group(evsel) && evsel != leader) {
		evsel__set_leader(evsel, evsel);
		evsel->core.nr_members = 0;
		leader->core.nr_members--;
	}
}
