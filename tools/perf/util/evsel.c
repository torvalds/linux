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
#include <traceevent/event-parse.h>
#include <linux/hw_breakpoint.h>
#include <linux/perf_event.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <dirent.h>
#include "asm/bug.h"
#include "callchain.h"
#include "cgroup.h"
#include "event.h"
#include "evsel.h"
#include "evlist.h"
#include "util.h"
#include "cpumap.h"
#include "thread_map.h"
#include "target.h"
#include "perf_regs.h"
#include "debug.h"
#include "trace-event.h"
#include "stat.h"
#include "memswap.h"
#include "util/parse-branch-options.h"

#include "sane_ctype.h"

struct perf_missing_features perf_missing_features;

static clockid_t clockid;

static int perf_evsel__no_extra_init(struct perf_evsel *evsel __maybe_unused)
{
	return 0;
}

void __weak test_attr__ready(void) { }

static void perf_evsel__no_extra_fini(struct perf_evsel *evsel __maybe_unused)
{
}

static struct {
	size_t	size;
	int	(*init)(struct perf_evsel *evsel);
	void	(*fini)(struct perf_evsel *evsel);
} perf_evsel__object = {
	.size = sizeof(struct perf_evsel),
	.init = perf_evsel__no_extra_init,
	.fini = perf_evsel__no_extra_fini,
};

int perf_evsel__object_config(size_t object_size,
			      int (*init)(struct perf_evsel *evsel),
			      void (*fini)(struct perf_evsel *evsel))
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

/**
 * perf_evsel__is_function_event - Return whether given evsel is a function
 * trace event
 *
 * @evsel - evsel selector to be tested
 *
 * Return %true if event is function trace event
 */
bool perf_evsel__is_function_event(struct perf_evsel *evsel)
{
#define FUNCTION_EVENT "ftrace:function"

	return evsel->name &&
	       !strncmp(FUNCTION_EVENT, evsel->name, sizeof(FUNCTION_EVENT));

#undef FUNCTION_EVENT
}

void perf_evsel__init(struct perf_evsel *evsel,
		      struct perf_event_attr *attr, int idx)
{
	evsel->idx	   = idx;
	evsel->tracking	   = !idx;
	evsel->attr	   = *attr;
	evsel->leader	   = evsel;
	evsel->unit	   = "";
	evsel->scale	   = 1.0;
	evsel->max_events  = ULONG_MAX;
	evsel->evlist	   = NULL;
	evsel->bpf_fd	   = -1;
	INIT_LIST_HEAD(&evsel->node);
	INIT_LIST_HEAD(&evsel->config_terms);
	perf_evsel__object.init(evsel);
	evsel->sample_size = __perf_evsel__sample_size(attr->sample_type);
	perf_evsel__calc_id_pos(evsel);
	evsel->cmdline_group_boundary = false;
	evsel->metric_expr   = NULL;
	evsel->metric_name   = NULL;
	evsel->metric_events = NULL;
	evsel->collect_stat  = false;
	evsel->pmu_name      = NULL;
}

struct perf_evsel *perf_evsel__new_idx(struct perf_event_attr *attr, int idx)
{
	struct perf_evsel *evsel = zalloc(perf_evsel__object.size);

	if (!evsel)
		return NULL;
	perf_evsel__init(evsel, attr, idx);

	if (perf_evsel__is_bpf_output(evsel)) {
		evsel->attr.sample_type |= (PERF_SAMPLE_RAW | PERF_SAMPLE_TIME |
					    PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD),
		evsel->attr.sample_period = 1;
	}

	if (perf_evsel__is_clock(evsel)) {
		/*
		 * The evsel->unit points to static alias->unit
		 * so it's ok to use static string in here.
		 */
		static const char *unit = "msec";

		evsel->unit = unit;
		evsel->scale = 1e-6;
	}

	return evsel;
}

static bool perf_event_can_profile_kernel(void)
{
	return geteuid() == 0 || perf_event_paranoid() == -1;
}

struct perf_evsel *perf_evsel__new_cycles(bool precise)
{
	struct perf_event_attr attr = {
		.type	= PERF_TYPE_HARDWARE,
		.config	= PERF_COUNT_HW_CPU_CYCLES,
		.exclude_kernel	= !perf_event_can_profile_kernel(),
	};
	struct perf_evsel *evsel;

	event_attr_init(&attr);

	if (!precise)
		goto new_event;

	/*
	 * Now let the usual logic to set up the perf_event_attr defaults
	 * to kick in when we return and before perf_evsel__open() is called.
	 */
new_event:
	evsel = perf_evsel__new(&attr);
	if (evsel == NULL)
		goto out;

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
	perf_evsel__delete(evsel);
	evsel = NULL;
	goto out;
}

/*
 * Returns pointer with encoded error via <linux/err.h> interface.
 */
struct perf_evsel *perf_evsel__newtp_idx(const char *sys, const char *name, int idx)
{
	struct perf_evsel *evsel = zalloc(perf_evsel__object.size);
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
		perf_evsel__init(evsel, &attr, idx);
	}

	return evsel;

out_free:
	zfree(&evsel->name);
	free(evsel);
out_err:
	return ERR_PTR(err);
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

static int perf_evsel__tool_name(char *bf, size_t size)
{
	int ret = scnprintf(bf, size, "duration_time");
	return ret;
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
		if (evsel->tool_event)
			perf_evsel__tool_name(bf, sizeof(bf));
		else
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
int perf_evsel__group_desc(struct perf_evsel *evsel, char *buf, size_t size)
{
	int ret = 0;
	struct perf_evsel *pos;
	const char *group_name = perf_evsel__group_name(evsel);

	if (!evsel->forced_leader)
		ret = scnprintf(buf, size, "%s { ", group_name);

	ret += scnprintf(buf + ret, size - ret, "%s",
			 perf_evsel__name(evsel));

	for_each_group_member(pos, evsel)
		ret += scnprintf(buf + ret, size - ret, ", %s",
				 perf_evsel__name(pos));

	if (!evsel->forced_leader)
		ret += scnprintf(buf + ret, size - ret, " }");

	return ret;
}

static void __perf_evsel__config_callchain(struct perf_evsel *evsel,
					   struct record_opts *opts,
					   struct callchain_param *param)
{
	bool function = perf_evsel__is_function_event(evsel);
	struct perf_event_attr *attr = &evsel->attr;

	perf_evsel__set_sample_bit(evsel, CALLCHAIN);

	attr->sample_max_stack = param->max_stack;

	if (param->record_mode == CALLCHAIN_LBR) {
		if (!opts->branch_stack) {
			if (attr->exclude_user) {
				pr_warning("LBR callstack option is only available "
					   "to get user callchain information. "
					   "Falling back to framepointers.\n");
			} else {
				perf_evsel__set_sample_bit(evsel, BRANCH_STACK);
				attr->branch_sample_type = PERF_SAMPLE_BRANCH_USER |
							PERF_SAMPLE_BRANCH_CALL_STACK |
							PERF_SAMPLE_BRANCH_NO_CYCLES |
							PERF_SAMPLE_BRANCH_NO_FLAGS;
			}
		} else
			 pr_warning("Cannot use LBR callstack with branch stack. "
				    "Falling back to framepointers.\n");
	}

	if (param->record_mode == CALLCHAIN_DWARF) {
		if (!function) {
			perf_evsel__set_sample_bit(evsel, REGS_USER);
			perf_evsel__set_sample_bit(evsel, STACK_USER);
			attr->sample_regs_user |= PERF_REGS_MASK;
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

void perf_evsel__config_callchain(struct perf_evsel *evsel,
				  struct record_opts *opts,
				  struct callchain_param *param)
{
	if (param->enabled)
		return __perf_evsel__config_callchain(evsel, opts, param);
}

static void
perf_evsel__reset_callgraph(struct perf_evsel *evsel,
			    struct callchain_param *param)
{
	struct perf_event_attr *attr = &evsel->attr;

	perf_evsel__reset_sample_bit(evsel, CALLCHAIN);
	if (param->record_mode == CALLCHAIN_LBR) {
		perf_evsel__reset_sample_bit(evsel, BRANCH_STACK);
		attr->branch_sample_type &= ~(PERF_SAMPLE_BRANCH_USER |
					      PERF_SAMPLE_BRANCH_CALL_STACK);
	}
	if (param->record_mode == CALLCHAIN_DWARF) {
		perf_evsel__reset_sample_bit(evsel, REGS_USER);
		perf_evsel__reset_sample_bit(evsel, STACK_USER);
	}
}

static void apply_config_terms(struct perf_evsel *evsel,
			       struct record_opts *opts, bool track)
{
	struct perf_evsel_config_term *term;
	struct list_head *config_terms = &evsel->config_terms;
	struct perf_event_attr *attr = &evsel->attr;
	/* callgraph default */
	struct callchain_param param = {
		.record_mode = callchain_param.record_mode,
	};
	u32 dump_size = 0;
	int max_stack = 0;
	const char *callgraph_buf = NULL;

	list_for_each_entry(term, config_terms, list) {
		switch (term->type) {
		case PERF_EVSEL__CONFIG_TERM_PERIOD:
			if (!(term->weak && opts->user_interval != ULLONG_MAX)) {
				attr->sample_period = term->val.period;
				attr->freq = 0;
				perf_evsel__reset_sample_bit(evsel, PERIOD);
			}
			break;
		case PERF_EVSEL__CONFIG_TERM_FREQ:
			if (!(term->weak && opts->user_freq != UINT_MAX)) {
				attr->sample_freq = term->val.freq;
				attr->freq = 1;
				perf_evsel__set_sample_bit(evsel, PERIOD);
			}
			break;
		case PERF_EVSEL__CONFIG_TERM_TIME:
			if (term->val.time)
				perf_evsel__set_sample_bit(evsel, TIME);
			else
				perf_evsel__reset_sample_bit(evsel, TIME);
			break;
		case PERF_EVSEL__CONFIG_TERM_CALLGRAPH:
			callgraph_buf = term->val.callgraph;
			break;
		case PERF_EVSEL__CONFIG_TERM_BRANCH:
			if (term->val.branch && strcmp(term->val.branch, "no")) {
				perf_evsel__set_sample_bit(evsel, BRANCH_STACK);
				parse_branch_str(term->val.branch,
						 &attr->branch_sample_type);
			} else
				perf_evsel__reset_sample_bit(evsel, BRANCH_STACK);
			break;
		case PERF_EVSEL__CONFIG_TERM_STACK_USER:
			dump_size = term->val.stack_user;
			break;
		case PERF_EVSEL__CONFIG_TERM_MAX_STACK:
			max_stack = term->val.max_stack;
			break;
		case PERF_EVSEL__CONFIG_TERM_MAX_EVENTS:
			evsel->max_events = term->val.max_events;
			break;
		case PERF_EVSEL__CONFIG_TERM_INHERIT:
			/*
			 * attr->inherit should has already been set by
			 * perf_evsel__config. If user explicitly set
			 * inherit using config terms, override global
			 * opt->no_inherit setting.
			 */
			attr->inherit = term->val.inherit ? 1 : 0;
			break;
		case PERF_EVSEL__CONFIG_TERM_OVERWRITE:
			attr->write_backward = term->val.overwrite ? 1 : 0;
			break;
		case PERF_EVSEL__CONFIG_TERM_DRV_CFG:
			break;
		case PERF_EVSEL__CONFIG_TERM_PERCORE:
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
			perf_evsel__reset_callgraph(evsel, &callchain_param);

		/* set perf-event callgraph */
		if (param.enabled) {
			if (sample_address) {
				perf_evsel__set_sample_bit(evsel, ADDR);
				perf_evsel__set_sample_bit(evsel, DATA_SRC);
				evsel->attr.mmap_data = track;
			}
			perf_evsel__config_callchain(evsel, opts, &param);
		}
	}
}

static bool is_dummy_event(struct perf_evsel *evsel)
{
	return (evsel->attr.type == PERF_TYPE_SOFTWARE) &&
	       (evsel->attr.config == PERF_COUNT_SW_DUMMY);
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
void perf_evsel__config(struct perf_evsel *evsel, struct record_opts *opts,
			struct callchain_param *callchain)
{
	struct perf_evsel *leader = evsel->leader;
	struct perf_event_attr *attr = &evsel->attr;
	int track = evsel->tracking;
	bool per_cpu = opts->target.default_per_cpu && !opts->target.per_thread;

	attr->sample_id_all = perf_missing_features.sample_id_all ? 0 : 1;
	attr->inherit	    = !opts->no_inherit;
	attr->write_backward = opts->overwrite ? 1 : 0;

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
	 * We default some events to have a default interval. But keep
	 * it a weak assumption overridable by the user.
	 */
	if (!attr->sample_period || (opts->user_freq != UINT_MAX ||
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
		attr->freq           = 0;
		attr->sample_freq    = 0;
		attr->sample_period  = 0;
		attr->write_backward = 0;

		/*
		 * We don't get sample for slave events, we make them
		 * when delivering group leader sample. Set the slave
		 * event to follow the master sample_type to ease up
		 * report.
		 */
		attr->sample_type = leader->attr.sample_type;
	}

	if (opts->no_samples)
		attr->sample_freq = 0;

	if (opts->inherit_stat) {
		evsel->attr.read_format |=
			PERF_FORMAT_TOTAL_TIME_ENABLED |
			PERF_FORMAT_TOTAL_TIME_RUNNING |
			PERF_FORMAT_ID;
		attr->inherit_stat = 1;
	}

	if (opts->sample_address) {
		perf_evsel__set_sample_bit(evsel, ADDR);
		attr->mmap_data = track;
	}

	/*
	 * We don't allow user space callchains for  function trace
	 * event, due to issues with page faults while tracing page
	 * fault handler and its overall trickiness nature.
	 */
	if (perf_evsel__is_function_event(evsel))
		evsel->attr.exclude_callchain_user = 1;

	if (callchain && callchain->enabled && !evsel->no_aux_samples)
		perf_evsel__config_callchain(evsel, opts, callchain);

	if (opts->sample_intr_regs) {
		attr->sample_regs_intr = opts->sample_intr_regs;
		perf_evsel__set_sample_bit(evsel, REGS_INTR);
	}

	if (opts->sample_user_regs) {
		attr->sample_regs_user |= opts->sample_user_regs;
		perf_evsel__set_sample_bit(evsel, REGS_USER);
	}

	if (target__has_cpu(&opts->target) || opts->sample_cpu)
		perf_evsel__set_sample_bit(evsel, CPU);

	/*
	 * When the user explicitly disabled time don't force it here.
	 */
	if (opts->sample_time &&
	    (!perf_missing_features.sample_id_all &&
	    (!opts->no_inherit || target__has_cpu(&opts->target) || per_cpu ||
	     opts->sample_time_set)))
		perf_evsel__set_sample_bit(evsel, TIME);

	if (opts->raw_samples && !evsel->no_aux_samples) {
		perf_evsel__set_sample_bit(evsel, TIME);
		perf_evsel__set_sample_bit(evsel, RAW);
		perf_evsel__set_sample_bit(evsel, CPU);
	}

	if (opts->sample_address)
		perf_evsel__set_sample_bit(evsel, DATA_SRC);

	if (opts->sample_phys_addr)
		perf_evsel__set_sample_bit(evsel, PHYS_ADDR);

	if (opts->no_buffering) {
		attr->watermark = 0;
		attr->wakeup_events = 1;
	}
	if (opts->branch_stack && !evsel->no_aux_samples) {
		perf_evsel__set_sample_bit(evsel, BRANCH_STACK);
		attr->branch_sample_type = opts->branch_stack;
	}

	if (opts->sample_weight)
		perf_evsel__set_sample_bit(evsel, WEIGHT);

	attr->task  = track;
	attr->mmap  = track;
	attr->mmap2 = track && !perf_missing_features.mmap2;
	attr->comm  = track;
	attr->ksymbol = track && !perf_missing_features.ksymbol;
	attr->bpf_event = track && !opts->no_bpf_event &&
		!perf_missing_features.bpf_event;

	if (opts->record_namespaces)
		attr->namespaces  = track;

	if (opts->record_switch_events)
		attr->context_switch = track;

	if (opts->sample_transaction)
		perf_evsel__set_sample_bit(evsel, TRANSACTION);

	if (opts->running_time) {
		evsel->attr.read_format |=
			PERF_FORMAT_TOTAL_TIME_ENABLED |
			PERF_FORMAT_TOTAL_TIME_RUNNING;
	}

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

	if (evsel->own_cpus || evsel->unit)
		evsel->attr.read_format |= PERF_FORMAT_ID;

	/*
	 * Apply event specific term settings,
	 * it overloads any global configuration.
	 */
	apply_config_terms(evsel, opts, track);

	evsel->ignore_missing_thread = opts->ignore_missing_thread;

	/* The --period option takes the precedence. */
	if (opts->period_set) {
		if (opts->period)
			perf_evsel__set_sample_bit(evsel, PERIOD);
		else
			perf_evsel__reset_sample_bit(evsel, PERIOD);
	}

	/*
	 * For initial_delay, a dummy event is added implicitly.
	 * The software event will trigger -EOPNOTSUPP error out,
	 * if BRANCH_STACK bit is set.
	 */
	if (opts->initial_delay && is_dummy_event(evsel))
		perf_evsel__reset_sample_bit(evsel, BRANCH_STACK);
}

static int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	if (evsel->system_wide)
		nthreads = 1;

	evsel->fd = xyarray__new(ncpus, nthreads, sizeof(int));

	if (evsel->fd) {
		int cpu, thread;
		for (cpu = 0; cpu < ncpus; cpu++) {
			for (thread = 0; thread < nthreads; thread++) {
				FD(evsel, cpu, thread) = -1;
			}
		}
	}

	return evsel->fd != NULL ? 0 : -ENOMEM;
}

static int perf_evsel__run_ioctl(struct perf_evsel *evsel,
			  int ioc,  void *arg)
{
	int cpu, thread;

	for (cpu = 0; cpu < xyarray__max_x(evsel->fd); cpu++) {
		for (thread = 0; thread < xyarray__max_y(evsel->fd); thread++) {
			int fd = FD(evsel, cpu, thread),
			    err = ioctl(fd, ioc, arg);

			if (err)
				return err;
		}
	}

	return 0;
}

int perf_evsel__apply_filter(struct perf_evsel *evsel, const char *filter)
{
	return perf_evsel__run_ioctl(evsel,
				     PERF_EVENT_IOC_SET_FILTER,
				     (void *)filter);
}

int perf_evsel__set_filter(struct perf_evsel *evsel, const char *filter)
{
	char *new_filter = strdup(filter);

	if (new_filter != NULL) {
		free(evsel->filter);
		evsel->filter = new_filter;
		return 0;
	}

	return -1;
}

static int perf_evsel__append_filter(struct perf_evsel *evsel,
				     const char *fmt, const char *filter)
{
	char *new_filter;

	if (evsel->filter == NULL)
		return perf_evsel__set_filter(evsel, filter);

	if (asprintf(&new_filter, fmt, evsel->filter, filter) > 0) {
		free(evsel->filter);
		evsel->filter = new_filter;
		return 0;
	}

	return -1;
}

int perf_evsel__append_tp_filter(struct perf_evsel *evsel, const char *filter)
{
	return perf_evsel__append_filter(evsel, "(%s) && (%s)", filter);
}

int perf_evsel__append_addr_filter(struct perf_evsel *evsel, const char *filter)
{
	return perf_evsel__append_filter(evsel, "%s,%s", filter);
}

int perf_evsel__enable(struct perf_evsel *evsel)
{
	int err = perf_evsel__run_ioctl(evsel, PERF_EVENT_IOC_ENABLE, 0);

	if (!err)
		evsel->disabled = false;

	return err;
}

int perf_evsel__disable(struct perf_evsel *evsel)
{
	int err = perf_evsel__run_ioctl(evsel, PERF_EVENT_IOC_DISABLE, 0);
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

int perf_evsel__alloc_id(struct perf_evsel *evsel, int ncpus, int nthreads)
{
	if (ncpus == 0 || nthreads == 0)
		return 0;

	if (evsel->system_wide)
		nthreads = 1;

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

static void perf_evsel__free_fd(struct perf_evsel *evsel)
{
	xyarray__delete(evsel->fd);
	evsel->fd = NULL;
}

static void perf_evsel__free_id(struct perf_evsel *evsel)
{
	xyarray__delete(evsel->sample_id);
	evsel->sample_id = NULL;
	zfree(&evsel->id);
}

static void perf_evsel__free_config_terms(struct perf_evsel *evsel)
{
	struct perf_evsel_config_term *term, *h;

	list_for_each_entry_safe(term, h, &evsel->config_terms, list) {
		list_del(&term->list);
		free(term);
	}
}

void perf_evsel__close_fd(struct perf_evsel *evsel)
{
	int cpu, thread;

	for (cpu = 0; cpu < xyarray__max_x(evsel->fd); cpu++)
		for (thread = 0; thread < xyarray__max_y(evsel->fd); ++thread) {
			close(FD(evsel, cpu, thread));
			FD(evsel, cpu, thread) = -1;
		}
}

void perf_evsel__exit(struct perf_evsel *evsel)
{
	assert(list_empty(&evsel->node));
	assert(evsel->evlist == NULL);
	perf_evsel__free_counts(evsel);
	perf_evsel__free_fd(evsel);
	perf_evsel__free_id(evsel);
	perf_evsel__free_config_terms(evsel);
	cgroup__put(evsel->cgrp);
	cpu_map__put(evsel->cpus);
	cpu_map__put(evsel->own_cpus);
	thread_map__put(evsel->threads);
	zfree(&evsel->group_name);
	zfree(&evsel->name);
	perf_evsel__object.fini(evsel);
}

void perf_evsel__delete(struct perf_evsel *evsel)
{
	perf_evsel__exit(evsel);
	free(evsel);
}

void perf_evsel__compute_deltas(struct perf_evsel *evsel, int cpu, int thread,
				struct perf_counts_values *count)
{
	struct perf_counts_values tmp;

	if (!evsel->prev_raw_counts)
		return;

	if (cpu == -1) {
		tmp = evsel->prev_raw_counts->aggr;
		evsel->prev_raw_counts->aggr = *count;
	} else {
		tmp = *perf_counts(evsel->prev_raw_counts, cpu, thread);
		*perf_counts(evsel->prev_raw_counts, cpu, thread) = *count;
	}

	count->val = count->val - tmp.val;
	count->ena = count->ena - tmp.ena;
	count->run = count->run - tmp.run;
}

void perf_counts_values__scale(struct perf_counts_values *count,
			       bool scale, s8 *pscaled)
{
	s8 scaled = 0;

	if (scale) {
		if (count->run == 0) {
			scaled = -1;
			count->val = 0;
		} else if (count->run < count->ena) {
			scaled = 1;
			count->val = (u64)((double) count->val * count->ena / count->run);
		}
	}

	if (pscaled)
		*pscaled = scaled;
}

static int perf_evsel__read_size(struct perf_evsel *evsel)
{
	u64 read_format = evsel->attr.read_format;
	int entry = sizeof(u64); /* value */
	int size = 0;
	int nr = 1;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		size += sizeof(u64);

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		size += sizeof(u64);

	if (read_format & PERF_FORMAT_ID)
		entry += sizeof(u64);

	if (read_format & PERF_FORMAT_GROUP) {
		nr = evsel->nr_members;
		size += sizeof(u64);
	}

	size += entry * nr;
	return size;
}

int perf_evsel__read(struct perf_evsel *evsel, int cpu, int thread,
		     struct perf_counts_values *count)
{
	size_t size = perf_evsel__read_size(evsel);

	memset(count, 0, sizeof(*count));

	if (FD(evsel, cpu, thread) < 0)
		return -EINVAL;

	if (readn(FD(evsel, cpu, thread), count->values, size) <= 0)
		return -errno;

	return 0;
}

static int
perf_evsel__read_one(struct perf_evsel *evsel, int cpu, int thread)
{
	struct perf_counts_values *count = perf_counts(evsel->counts, cpu, thread);

	return perf_evsel__read(evsel, cpu, thread, count);
}

static void
perf_evsel__set_count(struct perf_evsel *counter, int cpu, int thread,
		      u64 val, u64 ena, u64 run)
{
	struct perf_counts_values *count;

	count = perf_counts(counter->counts, cpu, thread);

	count->val    = val;
	count->ena    = ena;
	count->run    = run;
	count->loaded = true;
}

static int
perf_evsel__process_group_data(struct perf_evsel *leader,
			       int cpu, int thread, u64 *data)
{
	u64 read_format = leader->attr.read_format;
	struct sample_read_value *v;
	u64 nr, ena = 0, run = 0, i;

	nr = *data++;

	if (nr != (u64) leader->nr_members)
		return -EINVAL;

	if (read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		ena = *data++;

	if (read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		run = *data++;

	v = (struct sample_read_value *) data;

	perf_evsel__set_count(leader, cpu, thread,
			      v[0].value, ena, run);

	for (i = 1; i < nr; i++) {
		struct perf_evsel *counter;

		counter = perf_evlist__id2evsel(leader->evlist, v[i].id);
		if (!counter)
			return -EINVAL;

		perf_evsel__set_count(counter, cpu, thread,
				      v[i].value, ena, run);
	}

	return 0;
}

static int
perf_evsel__read_group(struct perf_evsel *leader, int cpu, int thread)
{
	struct perf_stat_evsel *ps = leader->stats;
	u64 read_format = leader->attr.read_format;
	int size = perf_evsel__read_size(leader);
	u64 *data = ps->group_data;

	if (!(read_format & PERF_FORMAT_ID))
		return -EINVAL;

	if (!perf_evsel__is_group_leader(leader))
		return -EINVAL;

	if (!data) {
		data = zalloc(size);
		if (!data)
			return -ENOMEM;

		ps->group_data = data;
	}

	if (FD(leader, cpu, thread) < 0)
		return -EINVAL;

	if (readn(FD(leader, cpu, thread), data, size) <= 0)
		return -errno;

	return perf_evsel__process_group_data(leader, cpu, thread, data);
}

int perf_evsel__read_counter(struct perf_evsel *evsel, int cpu, int thread)
{
	u64 read_format = evsel->attr.read_format;

	if (read_format & PERF_FORMAT_GROUP)
		return perf_evsel__read_group(evsel, cpu, thread);
	else
		return perf_evsel__read_one(evsel, cpu, thread);
}

int __perf_evsel__read_on_cpu(struct perf_evsel *evsel,
			      int cpu, int thread, bool scale)
{
	struct perf_counts_values count;
	size_t nv = scale ? 3 : 1;

	if (FD(evsel, cpu, thread) < 0)
		return -EINVAL;

	if (evsel->counts == NULL && perf_evsel__alloc_counts(evsel, cpu + 1, thread + 1) < 0)
		return -ENOMEM;

	if (readn(FD(evsel, cpu, thread), &count, nv * sizeof(u64)) <= 0)
		return -errno;

	perf_evsel__compute_deltas(evsel, cpu, thread, &count);
	perf_counts_values__scale(&count, scale, NULL);
	*perf_counts(evsel->counts, cpu, thread) = count;
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

struct bit_names {
	int bit;
	const char *name;
};

static void __p_bits(char *buf, size_t size, u64 value, struct bit_names *bits)
{
	bool first_bit = true;
	int i = 0;

	do {
		if (value & bits[i].bit) {
			buf += scnprintf(buf, size, "%s%s", first_bit ? "" : "|", bits[i].name);
			first_bit = false;
		}
	} while (bits[++i].name != NULL);
}

static void __p_sample_type(char *buf, size_t size, u64 value)
{
#define bit_name(n) { PERF_SAMPLE_##n, #n }
	struct bit_names bits[] = {
		bit_name(IP), bit_name(TID), bit_name(TIME), bit_name(ADDR),
		bit_name(READ), bit_name(CALLCHAIN), bit_name(ID), bit_name(CPU),
		bit_name(PERIOD), bit_name(STREAM_ID), bit_name(RAW),
		bit_name(BRANCH_STACK), bit_name(REGS_USER), bit_name(STACK_USER),
		bit_name(IDENTIFIER), bit_name(REGS_INTR), bit_name(DATA_SRC),
		bit_name(WEIGHT), bit_name(PHYS_ADDR),
		{ .name = NULL, }
	};
#undef bit_name
	__p_bits(buf, size, value, bits);
}

static void __p_branch_sample_type(char *buf, size_t size, u64 value)
{
#define bit_name(n) { PERF_SAMPLE_BRANCH_##n, #n }
	struct bit_names bits[] = {
		bit_name(USER), bit_name(KERNEL), bit_name(HV), bit_name(ANY),
		bit_name(ANY_CALL), bit_name(ANY_RETURN), bit_name(IND_CALL),
		bit_name(ABORT_TX), bit_name(IN_TX), bit_name(NO_TX),
		bit_name(COND), bit_name(CALL_STACK), bit_name(IND_JUMP),
		bit_name(CALL), bit_name(NO_FLAGS), bit_name(NO_CYCLES),
		{ .name = NULL, }
	};
#undef bit_name
	__p_bits(buf, size, value, bits);
}

static void __p_read_format(char *buf, size_t size, u64 value)
{
#define bit_name(n) { PERF_FORMAT_##n, #n }
	struct bit_names bits[] = {
		bit_name(TOTAL_TIME_ENABLED), bit_name(TOTAL_TIME_RUNNING),
		bit_name(ID), bit_name(GROUP),
		{ .name = NULL, }
	};
#undef bit_name
	__p_bits(buf, size, value, bits);
}

#define BUF_SIZE		1024

#define p_hex(val)		snprintf(buf, BUF_SIZE, "%#"PRIx64, (uint64_t)(val))
#define p_unsigned(val)		snprintf(buf, BUF_SIZE, "%"PRIu64, (uint64_t)(val))
#define p_signed(val)		snprintf(buf, BUF_SIZE, "%"PRId64, (int64_t)(val))
#define p_sample_type(val)	__p_sample_type(buf, BUF_SIZE, val)
#define p_branch_sample_type(val) __p_branch_sample_type(buf, BUF_SIZE, val)
#define p_read_format(val)	__p_read_format(buf, BUF_SIZE, val)

#define PRINT_ATTRn(_n, _f, _p)				\
do {							\
	if (attr->_f) {					\
		_p(attr->_f);				\
		ret += attr__fprintf(fp, _n, buf, priv);\
	}						\
} while (0)

#define PRINT_ATTRf(_f, _p)	PRINT_ATTRn(#_f, _f, _p)

int perf_event_attr__fprintf(FILE *fp, struct perf_event_attr *attr,
			     attr__fprintf_f attr__fprintf, void *priv)
{
	char buf[BUF_SIZE];
	int ret = 0;

	PRINT_ATTRf(type, p_unsigned);
	PRINT_ATTRf(size, p_unsigned);
	PRINT_ATTRf(config, p_hex);
	PRINT_ATTRn("{ sample_period, sample_freq }", sample_period, p_unsigned);
	PRINT_ATTRf(sample_type, p_sample_type);
	PRINT_ATTRf(read_format, p_read_format);

	PRINT_ATTRf(disabled, p_unsigned);
	PRINT_ATTRf(inherit, p_unsigned);
	PRINT_ATTRf(pinned, p_unsigned);
	PRINT_ATTRf(exclusive, p_unsigned);
	PRINT_ATTRf(exclude_user, p_unsigned);
	PRINT_ATTRf(exclude_kernel, p_unsigned);
	PRINT_ATTRf(exclude_hv, p_unsigned);
	PRINT_ATTRf(exclude_idle, p_unsigned);
	PRINT_ATTRf(mmap, p_unsigned);
	PRINT_ATTRf(comm, p_unsigned);
	PRINT_ATTRf(freq, p_unsigned);
	PRINT_ATTRf(inherit_stat, p_unsigned);
	PRINT_ATTRf(enable_on_exec, p_unsigned);
	PRINT_ATTRf(task, p_unsigned);
	PRINT_ATTRf(watermark, p_unsigned);
	PRINT_ATTRf(precise_ip, p_unsigned);
	PRINT_ATTRf(mmap_data, p_unsigned);
	PRINT_ATTRf(sample_id_all, p_unsigned);
	PRINT_ATTRf(exclude_host, p_unsigned);
	PRINT_ATTRf(exclude_guest, p_unsigned);
	PRINT_ATTRf(exclude_callchain_kernel, p_unsigned);
	PRINT_ATTRf(exclude_callchain_user, p_unsigned);
	PRINT_ATTRf(mmap2, p_unsigned);
	PRINT_ATTRf(comm_exec, p_unsigned);
	PRINT_ATTRf(use_clockid, p_unsigned);
	PRINT_ATTRf(context_switch, p_unsigned);
	PRINT_ATTRf(write_backward, p_unsigned);
	PRINT_ATTRf(namespaces, p_unsigned);
	PRINT_ATTRf(ksymbol, p_unsigned);
	PRINT_ATTRf(bpf_event, p_unsigned);

	PRINT_ATTRn("{ wakeup_events, wakeup_watermark }", wakeup_events, p_unsigned);
	PRINT_ATTRf(bp_type, p_unsigned);
	PRINT_ATTRn("{ bp_addr, config1 }", bp_addr, p_hex);
	PRINT_ATTRn("{ bp_len, config2 }", bp_len, p_hex);
	PRINT_ATTRf(branch_sample_type, p_branch_sample_type);
	PRINT_ATTRf(sample_regs_user, p_hex);
	PRINT_ATTRf(sample_stack_user, p_unsigned);
	PRINT_ATTRf(clockid, p_signed);
	PRINT_ATTRf(sample_regs_intr, p_hex);
	PRINT_ATTRf(aux_watermark, p_unsigned);
	PRINT_ATTRf(sample_max_stack, p_unsigned);

	return ret;
}

static int __open_attr__fprintf(FILE *fp, const char *name, const char *val,
				void *priv __maybe_unused)
{
	return fprintf(fp, "  %-32s %s\n", name, val);
}

static void perf_evsel__remove_fd(struct perf_evsel *pos,
				  int nr_cpus, int nr_threads,
				  int thread_idx)
{
	for (int cpu = 0; cpu < nr_cpus; cpu++)
		for (int thread = thread_idx; thread < nr_threads - 1; thread++)
			FD(pos, cpu, thread) = FD(pos, cpu, thread + 1);
}

static int update_fds(struct perf_evsel *evsel,
		      int nr_cpus, int cpu_idx,
		      int nr_threads, int thread_idx)
{
	struct perf_evsel *pos;

	if (cpu_idx >= nr_cpus || thread_idx >= nr_threads)
		return -EINVAL;

	evlist__for_each_entry(evsel->evlist, pos) {
		nr_cpus = pos != evsel ? nr_cpus : cpu_idx;

		perf_evsel__remove_fd(pos, nr_cpus, nr_threads, thread_idx);

		/*
		 * Since fds for next evsel has not been created,
		 * there is no need to iterate whole event list.
		 */
		if (pos == evsel)
			break;
	}
	return 0;
}

static bool ignore_missing_thread(struct perf_evsel *evsel,
				  int nr_cpus, int cpu,
				  struct thread_map *threads,
				  int thread, int err)
{
	pid_t ignore_pid = thread_map__pid(threads, thread);

	if (!evsel->ignore_missing_thread)
		return false;

	/* The system wide setup does not work with threads. */
	if (evsel->system_wide)
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
	if (update_fds(evsel, nr_cpus, cpu, threads->nr, thread))
		return false;

	if (thread_map__remove(threads, thread))
		return false;

	pr_warning("WARNING: Ignored open failure for pid %d\n",
		   ignore_pid);
	return true;
}

static void display_attr(struct perf_event_attr *attr)
{
	if (verbose >= 2) {
		fprintf(stderr, "%.60s\n", graph_dotted_line);
		fprintf(stderr, "perf_event_attr:\n");
		perf_event_attr__fprintf(stderr, attr, __open_attr__fprintf, NULL);
		fprintf(stderr, "%.60s\n", graph_dotted_line);
	}
}

static int perf_event_open(struct perf_evsel *evsel,
			   pid_t pid, int cpu, int group_fd,
			   unsigned long flags)
{
	int precise_ip = evsel->attr.precise_ip;
	int fd;

	while (1) {
		pr_debug2("sys_perf_event_open: pid %d  cpu %d  group_fd %d  flags %#lx",
			  pid, cpu, group_fd, flags);

		fd = sys_perf_event_open(&evsel->attr, pid, cpu, group_fd, flags);
		if (fd >= 0)
			break;

		/* Do not try less precise if not requested. */
		if (!evsel->precise_max)
			break;

		/*
		 * We tried all the precise_ip values, and it's
		 * still failing, so leave it to standard fallback.
		 */
		if (!evsel->attr.precise_ip) {
			evsel->attr.precise_ip = precise_ip;
			break;
		}

		pr_debug2("\nsys_perf_event_open failed, error %d\n", -ENOTSUP);
		evsel->attr.precise_ip--;
		pr_debug2("decreasing precise_ip by one (%d)\n", evsel->attr.precise_ip);
		display_attr(&evsel->attr);
	}

	return fd;
}

int perf_evsel__open(struct perf_evsel *evsel, struct cpu_map *cpus,
		     struct thread_map *threads)
{
	int cpu, thread, nthreads;
	unsigned long flags = PERF_FLAG_FD_CLOEXEC;
	int pid = -1, err;
	enum { NO_CHANGE, SET_TO_MAX, INCREASED_MAX } set_rlimit = NO_CHANGE;

	if (perf_missing_features.write_backward && evsel->attr.write_backward)
		return -EINVAL;

	if (cpus == NULL) {
		static struct cpu_map *empty_cpu_map;

		if (empty_cpu_map == NULL) {
			empty_cpu_map = cpu_map__dummy_new();
			if (empty_cpu_map == NULL)
				return -ENOMEM;
		}

		cpus = empty_cpu_map;
	}

	if (threads == NULL) {
		static struct thread_map *empty_thread_map;

		if (empty_thread_map == NULL) {
			empty_thread_map = thread_map__new_by_tid(-1);
			if (empty_thread_map == NULL)
				return -ENOMEM;
		}

		threads = empty_thread_map;
	}

	if (evsel->system_wide)
		nthreads = 1;
	else
		nthreads = threads->nr;

	if (evsel->fd == NULL &&
	    perf_evsel__alloc_fd(evsel, cpus->nr, nthreads) < 0)
		return -ENOMEM;

	if (evsel->cgrp) {
		flags |= PERF_FLAG_PID_CGROUP;
		pid = evsel->cgrp->fd;
	}

fallback_missing_features:
	if (perf_missing_features.clockid_wrong)
		evsel->attr.clockid = CLOCK_MONOTONIC; /* should always work */
	if (perf_missing_features.clockid) {
		evsel->attr.use_clockid = 0;
		evsel->attr.clockid = 0;
	}
	if (perf_missing_features.cloexec)
		flags &= ~(unsigned long)PERF_FLAG_FD_CLOEXEC;
	if (perf_missing_features.mmap2)
		evsel->attr.mmap2 = 0;
	if (perf_missing_features.exclude_guest)
		evsel->attr.exclude_guest = evsel->attr.exclude_host = 0;
	if (perf_missing_features.lbr_flags)
		evsel->attr.branch_sample_type &= ~(PERF_SAMPLE_BRANCH_NO_FLAGS |
				     PERF_SAMPLE_BRANCH_NO_CYCLES);
	if (perf_missing_features.group_read && evsel->attr.inherit)
		evsel->attr.read_format &= ~(PERF_FORMAT_GROUP|PERF_FORMAT_ID);
	if (perf_missing_features.ksymbol)
		evsel->attr.ksymbol = 0;
	if (perf_missing_features.bpf_event)
		evsel->attr.bpf_event = 0;
retry_sample_id:
	if (perf_missing_features.sample_id_all)
		evsel->attr.sample_id_all = 0;

	display_attr(&evsel->attr);

	for (cpu = 0; cpu < cpus->nr; cpu++) {

		for (thread = 0; thread < nthreads; thread++) {
			int fd, group_fd;

			if (!evsel->cgrp && !evsel->system_wide)
				pid = thread_map__pid(threads, thread);

			group_fd = get_group_fd(evsel, cpu, thread);
retry_open:
			test_attr__ready();

			fd = perf_event_open(evsel, pid, cpus->map[cpu],
					     group_fd, flags);

			FD(evsel, cpu, thread) = fd;

			if (fd < 0) {
				err = -errno;

				if (ignore_missing_thread(evsel, cpus->nr, cpu, threads, thread, err)) {
					/*
					 * We just removed 1 thread, so take a step
					 * back on thread index and lower the upper
					 * nthreads limit.
					 */
					nthreads--;
					thread--;

					/* ... and pretend like nothing have happened. */
					err = 0;
					continue;
				}

				pr_debug2("\nsys_perf_event_open failed, error %d\n",
					  err);
				goto try_fallback;
			}

			pr_debug2(" = %d\n", fd);

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
			 * have perf_evsel__open_strerror() print us a nice
			 * error.
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

	/*
	 * Must probe features in the order they were added to the
	 * perf_event_attr interface.
	 */
	if (!perf_missing_features.bpf_event && evsel->attr.bpf_event) {
		perf_missing_features.bpf_event = true;
		pr_debug2("switching off bpf_event\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.ksymbol && evsel->attr.ksymbol) {
		perf_missing_features.ksymbol = true;
		pr_debug2("switching off ksymbol\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.write_backward && evsel->attr.write_backward) {
		perf_missing_features.write_backward = true;
		pr_debug2("switching off write_backward\n");
		goto out_close;
	} else if (!perf_missing_features.clockid_wrong && evsel->attr.use_clockid) {
		perf_missing_features.clockid_wrong = true;
		pr_debug2("switching off clockid\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.clockid && evsel->attr.use_clockid) {
		perf_missing_features.clockid = true;
		pr_debug2("switching off use_clockid\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.cloexec && (flags & PERF_FLAG_FD_CLOEXEC)) {
		perf_missing_features.cloexec = true;
		pr_debug2("switching off cloexec flag\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.mmap2 && evsel->attr.mmap2) {
		perf_missing_features.mmap2 = true;
		pr_debug2("switching off mmap2\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.exclude_guest &&
		   (evsel->attr.exclude_guest || evsel->attr.exclude_host)) {
		perf_missing_features.exclude_guest = true;
		pr_debug2("switching off exclude_guest, exclude_host\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.sample_id_all) {
		perf_missing_features.sample_id_all = true;
		pr_debug2("switching off sample_id_all\n");
		goto retry_sample_id;
	} else if (!perf_missing_features.lbr_flags &&
			(evsel->attr.branch_sample_type &
			 (PERF_SAMPLE_BRANCH_NO_CYCLES |
			  PERF_SAMPLE_BRANCH_NO_FLAGS))) {
		perf_missing_features.lbr_flags = true;
		pr_debug2("switching off branch sample type no (cycles/flags)\n");
		goto fallback_missing_features;
	} else if (!perf_missing_features.group_read &&
		    evsel->attr.inherit &&
		   (evsel->attr.read_format & PERF_FORMAT_GROUP) &&
		   perf_evsel__is_group_leader(evsel)) {
		perf_missing_features.group_read = true;
		pr_debug2("switching off group read\n");
		goto fallback_missing_features;
	}
out_close:
	if (err)
		threads->err_thread = thread;

	do {
		while (--thread >= 0) {
			close(FD(evsel, cpu, thread));
			FD(evsel, cpu, thread) = -1;
		}
		thread = nthreads;
	} while (--cpu >= 0);
	return err;
}

void perf_evsel__close(struct perf_evsel *evsel)
{
	if (evsel->fd == NULL)
		return;

	perf_evsel__close_fd(evsel);
	perf_evsel__free_fd(evsel);
}

int perf_evsel__open_per_cpu(struct perf_evsel *evsel,
			     struct cpu_map *cpus)
{
	return perf_evsel__open(evsel, cpus, NULL);
}

int perf_evsel__open_per_thread(struct perf_evsel *evsel,
				struct thread_map *threads)
{
	return perf_evsel__open(evsel, NULL, threads);
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
	data->period = evsel->attr.sample_period;
	data->cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;
	data->misc    = event->header.misc;
	data->id = -1ULL;
	data->data_src = PERF_MEM_DATA_SRC_NONE;

	if (event->header.type != PERF_RECORD_SAMPLE) {
		if (!evsel->attr.sample_id_all)
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

	if (evsel__has_callchain(evsel)) {
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
		 * swap. The pevent interface handles endianity by
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
			u64 mask = evsel->attr.sample_regs_user;

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

	if (type & PERF_SAMPLE_WEIGHT) {
		OVERFLOW_CHECK_u64(array);
		data->weight = *array;
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
			u64 mask = evsel->attr.sample_regs_intr;

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

	return 0;
}

int perf_evsel__parse_sample_timestamp(struct perf_evsel *evsel,
				       union perf_event *event,
				       u64 *timestamp)
{
	u64 type = evsel->attr.sample_type;
	const u64 *array;

	if (!(type & PERF_SAMPLE_TIME))
		return -1;

	if (event->header.type != PERF_RECORD_SAMPLE) {
		struct perf_sample data = {
			.time = -1ULL,
		};

		if (!evsel->attr.sample_id_all)
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

size_t perf_event__sample_event_size(const struct perf_sample *sample, u64 type,
				     u64 read_format)
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
			sz = hweight64(sample->user_regs.mask) * sizeof(u64);
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

	if (type & PERF_SAMPLE_REGS_INTR) {
		if (sample->intr_regs.abi) {
			result += sizeof(u64);
			sz = hweight64(sample->intr_regs.mask) * sizeof(u64);
			result += sz;
		} else {
			result += sizeof(u64);
		}
	}

	if (type & PERF_SAMPLE_PHYS_ADDR)
		result += sizeof(u64);

	return result;
}

int perf_event__synthesize_sample(union perf_event *event, u64 type,
				  u64 read_format,
				  const struct perf_sample *sample)
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
		u.val32[1] = 0;
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
			sz = hweight64(sample->user_regs.mask) * sizeof(u64);
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

	if (type & PERF_SAMPLE_REGS_INTR) {
		if (sample->intr_regs.abi) {
			*array++ = sample->intr_regs.abi;
			sz = hweight64(sample->intr_regs.mask) * sizeof(u64);
			memcpy(array, sample->intr_regs.regs, sz);
			array = (void *)array + sz;
		} else {
			*array++ = 0;
		}
	}

	if (type & PERF_SAMPLE_PHYS_ADDR) {
		*array = sample->phys_addr;
		array++;
	}

	return 0;
}

struct tep_format_field *perf_evsel__field(struct perf_evsel *evsel, const char *name)
{
	return tep_find_field(evsel->tp_format, name);
}

void *perf_evsel__rawptr(struct perf_evsel *evsel, struct perf_sample *sample,
			 const char *name)
{
	struct tep_format_field *field = perf_evsel__field(evsel, name);
	int offset;

	if (!field)
		return NULL;

	offset = field->offset;

	if (field->flags & TEP_FIELD_IS_DYNAMIC) {
		offset = *(int *)(sample->raw_data + field->offset);
		offset &= 0xffff;
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

u64 perf_evsel__intval(struct perf_evsel *evsel, struct perf_sample *sample,
		       const char *name)
{
	struct tep_format_field *field = perf_evsel__field(evsel, name);

	if (!field)
		return 0;

	return field ? format_field__intval(field, sample, evsel->needs_swap) : 0;
}

bool perf_evsel__fallback(struct perf_evsel *evsel, int err,
			  char *msg, size_t msgsize)
{
	int paranoid;

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
	} else if (err == EACCES && !evsel->attr.exclude_kernel &&
		   (paranoid = perf_event_paranoid()) > 1) {
		const char *name = perf_evsel__name(evsel);
		char *new_name;
		const char *sep = ":";

		/* Is there already the separator in the name. */
		if (strchr(name, '/') ||
		    strchr(name, ':'))
			sep = "";

		if (asprintf(&new_name, "%s%su", name, sep) < 0)
			return false;

		if (evsel->name)
			free(evsel->name);
		evsel->name = new_name;
		scnprintf(msg, msgsize,
"kernel.perf_event_paranoid=%d, trying to fall back to excluding kernel samples", paranoid);
		evsel->attr.exclude_kernel = 1;

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

int perf_evsel__open_strerror(struct perf_evsel *evsel, struct target *target,
			      int err, char *msg, size_t size)
{
	char sbuf[STRERR_BUFSIZE];
	int printed = 0;

	switch (err) {
	case EPERM:
	case EACCES:
		if (err == EPERM)
			printed = scnprintf(msg, size,
				"No permission to enable %s event.\n\n",
				perf_evsel__name(evsel));

		return scnprintf(msg + printed, size - printed,
		 "You may not have permission to collect %sstats.\n\n"
		 "Consider tweaking /proc/sys/kernel/perf_event_paranoid,\n"
		 "which controls use of the performance events system by\n"
		 "unprivileged users (without CAP_SYS_ADMIN).\n\n"
		 "The current value is %d:\n\n"
		 "  -1: Allow use of (almost) all events by all users\n"
		 "      Ignore mlock limit after perf_event_mlock_kb without CAP_IPC_LOCK\n"
		 ">= 0: Disallow ftrace function tracepoint by users without CAP_SYS_ADMIN\n"
		 "      Disallow raw tracepoint access by users without CAP_SYS_ADMIN\n"
		 ">= 1: Disallow CPU event access by users without CAP_SYS_ADMIN\n"
		 ">= 2: Disallow kernel profiling by users without CAP_SYS_ADMIN\n\n"
		 "To make this setting permanent, edit /etc/sysctl.conf too, e.g.:\n\n"
		 "	kernel.perf_event_paranoid = -1\n" ,
				 target->system_wide ? "system-wide " : "",
				 perf_event_paranoid());
	case ENOENT:
		return scnprintf(msg, size, "The %s event is not supported.",
				 perf_evsel__name(evsel));
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
		if (evsel->attr.sample_period != 0)
			return scnprintf(msg, size,
	"%s: PMU Hardware doesn't support sampling/overflow-interrupts. Try 'perf stat'",
					 perf_evsel__name(evsel));
		if (evsel->attr.precise_ip)
			return scnprintf(msg, size, "%s",
	"\'precise\' request may not be supported. Try removing 'p' modifier.");
#if defined(__i386__) || defined(__x86_64__)
		if (evsel->attr.type == PERF_TYPE_HARDWARE)
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
		if (evsel->attr.write_backward && perf_missing_features.write_backward)
			return scnprintf(msg, size, "Reading from overwrite event is not supported by this kernel.");
		if (perf_missing_features.clockid)
			return scnprintf(msg, size, "clockid feature not supported.");
		if (perf_missing_features.clockid_wrong)
			return scnprintf(msg, size, "wrong clockid (%d).", clockid);
		break;
	default:
		break;
	}

	return scnprintf(msg, size,
	"The sys_perf_event_open() syscall returned with %d (%s) for event (%s).\n"
	"/bin/dmesg | grep -i perf may provide additional information.\n",
			 err, str_error_r(err, sbuf, sizeof(sbuf)),
			 perf_evsel__name(evsel));
}

struct perf_env *perf_evsel__env(struct perf_evsel *evsel)
{
	if (evsel && evsel->evlist)
		return evsel->evlist->env;
	return NULL;
}

static int store_evsel_ids(struct perf_evsel *evsel, struct perf_evlist *evlist)
{
	int cpu, thread;

	for (cpu = 0; cpu < xyarray__max_x(evsel->fd); cpu++) {
		for (thread = 0; thread < xyarray__max_y(evsel->fd);
		     thread++) {
			int fd = FD(evsel, cpu, thread);

			if (perf_evlist__id_add_fd(evlist, evsel,
						   cpu, thread, fd) < 0)
				return -1;
		}
	}

	return 0;
}

int perf_evsel__store_ids(struct perf_evsel *evsel, struct perf_evlist *evlist)
{
	struct cpu_map *cpus = evsel->cpus;
	struct thread_map *threads = evsel->threads;

	if (perf_evsel__alloc_id(evsel, cpus->nr, threads->nr))
		return -ENOMEM;

	return store_evsel_ids(evsel, evlist);
}
