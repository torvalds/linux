// SPDX-License-Identifier: GPL-2.0
#include <linux/hw_breakpoint.h>
#include <linux/err.h>
#include <linux/list_sort.h>
#include <linux/zalloc.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include "cpumap.h"
#include "term.h"
#include "env.h"
#include "evlist.h"
#include "evsel.h"
#include <subcmd/parse-options.h>
#include "parse-events.h"
#include "string2.h"
#include "strbuf.h"
#include "debug.h"
#include <perf/cpumap.h>
#include <util/parse-events-bison.h>
#include <util/parse-events-flex.h>
#include "pmu.h"
#include "pmus.h"
#include "tp_pmu.h"
#include "asm/bug.h"
#include "ui/ui.h"
#include "util/parse-branch-options.h"
#include "util/evsel_config.h"
#include "util/event.h"
#include "util/bpf-filter.h"
#include "util/stat.h"
#include "util/util.h"
#include "tracepoint.h"
#include <api/fs/tracing_path.h>

#define MAX_NAME_LEN 100

static int get_config_terms(const struct parse_events_terms *head_config,
			    struct list_head *head_terms);
static int parse_events_terms__copy(const struct parse_events_terms *src,
				    struct parse_events_terms *dest);

const struct event_symbol event_symbols_hw[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES] = {
		.symbol = "cpu-cycles",
		.alias  = "cycles",
	},
	[PERF_COUNT_HW_INSTRUCTIONS] = {
		.symbol = "instructions",
		.alias  = "",
	},
	[PERF_COUNT_HW_CACHE_REFERENCES] = {
		.symbol = "cache-references",
		.alias  = "",
	},
	[PERF_COUNT_HW_CACHE_MISSES] = {
		.symbol = "cache-misses",
		.alias  = "",
	},
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = {
		.symbol = "branch-instructions",
		.alias  = "branches",
	},
	[PERF_COUNT_HW_BRANCH_MISSES] = {
		.symbol = "branch-misses",
		.alias  = "",
	},
	[PERF_COUNT_HW_BUS_CYCLES] = {
		.symbol = "bus-cycles",
		.alias  = "",
	},
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = {
		.symbol = "stalled-cycles-frontend",
		.alias  = "idle-cycles-frontend",
	},
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] = {
		.symbol = "stalled-cycles-backend",
		.alias  = "idle-cycles-backend",
	},
	[PERF_COUNT_HW_REF_CPU_CYCLES] = {
		.symbol = "ref-cycles",
		.alias  = "",
	},
};

static const char *const event_types[] = {
	[PERF_TYPE_HARDWARE]	= "hardware",
	[PERF_TYPE_SOFTWARE]	= "software",
	[PERF_TYPE_TRACEPOINT]	= "tracepoint",
	[PERF_TYPE_HW_CACHE]	= "hardware-cache",
	[PERF_TYPE_RAW]		= "raw",
	[PERF_TYPE_BREAKPOINT]	= "breakpoint",
};

const char *event_type(size_t type)
{
	if (type >= PERF_TYPE_MAX)
		return "unknown";

	return event_types[type];
}

static char *get_config_str(const struct parse_events_terms *head_terms,
			    enum parse_events__term_type type_term)
{
	struct parse_events_term *term;

	if (!head_terms)
		return NULL;

	list_for_each_entry(term, &head_terms->terms, list)
		if (term->type_term == type_term)
			return term->val.str;

	return NULL;
}

static char *get_config_metric_id(const struct parse_events_terms *head_terms)
{
	return get_config_str(head_terms, PARSE_EVENTS__TERM_TYPE_METRIC_ID);
}

static char *get_config_name(const struct parse_events_terms *head_terms)
{
	return get_config_str(head_terms, PARSE_EVENTS__TERM_TYPE_NAME);
}

static struct perf_cpu_map *get_config_cpu(const struct parse_events_terms *head_terms,
					   bool fake_pmu)
{
	struct parse_events_term *term;
	struct perf_cpu_map *cpus = NULL;

	if (!head_terms)
		return NULL;

	list_for_each_entry(term, &head_terms->terms, list) {
		struct perf_cpu_map *term_cpus;

		if (term->type_term != PARSE_EVENTS__TERM_TYPE_CPU)
			continue;

		if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
			term_cpus = perf_cpu_map__new_int(term->val.num);
		} else {
			struct perf_pmu *pmu = perf_pmus__find(term->val.str);

			if (pmu) {
				term_cpus = pmu->is_core && perf_cpu_map__is_empty(pmu->cpus)
					    ? cpu_map__online()
					    : perf_cpu_map__get(pmu->cpus);
			} else {
				term_cpus = perf_cpu_map__new(term->val.str);
				if (!term_cpus && fake_pmu) {
					/*
					 * Assume the PMU string makes sense on a different
					 * machine and fake a value with all online CPUs.
					 */
					term_cpus = cpu_map__online();
				}
			}
		}
		perf_cpu_map__merge(&cpus, term_cpus);
		perf_cpu_map__put(term_cpus);
	}

	return cpus;
}

/**
 * fix_raw - For each raw term see if there is an event (aka alias) in pmu that
 *           matches the raw's string value. If the string value matches an
 *           event then change the term to be an event, if not then change it to
 *           be a config term. For example, "read" may be an event of the PMU or
 *           a raw hex encoding of 0xead. The fix-up is done late so the PMU of
 *           the event can be determined and we don't need to scan all PMUs
 *           ahead-of-time.
 * @config_terms: the list of terms that may contain a raw term.
 * @pmu: the PMU to scan for events from.
 */
static void fix_raw(struct parse_events_terms *config_terms, struct perf_pmu *pmu)
{
	struct parse_events_term *term;

	list_for_each_entry(term, &config_terms->terms, list) {
		u64 num;

		if (term->type_term != PARSE_EVENTS__TERM_TYPE_RAW)
			continue;

		if (perf_pmu__have_event(pmu, term->val.str)) {
			zfree(&term->config);
			term->config = term->val.str;
			term->type_val = PARSE_EVENTS__TERM_TYPE_NUM;
			term->type_term = PARSE_EVENTS__TERM_TYPE_USER;
			term->val.num = 1;
			term->no_value = true;
			continue;
		}

		zfree(&term->config);
		term->config = strdup("config");
		errno = 0;
		num = strtoull(term->val.str + 1, NULL, 16);
		assert(errno == 0);
		free(term->val.str);
		term->type_val = PARSE_EVENTS__TERM_TYPE_NUM;
		term->type_term = PARSE_EVENTS__TERM_TYPE_CONFIG;
		term->val.num = num;
		term->no_value = false;
	}
}

static struct evsel *
__add_event(struct list_head *list, int *idx,
	    struct perf_event_attr *attr,
	    bool init_attr,
	    const char *name, const char *metric_id, struct perf_pmu *pmu,
	    struct list_head *config_terms, struct evsel *first_wildcard_match,
	    struct perf_cpu_map *user_cpus, u64 alternate_hw_config)
{
	struct evsel *evsel;
	bool is_pmu_core;
	struct perf_cpu_map *cpus, *pmu_cpus;
	bool has_user_cpus = !perf_cpu_map__is_empty(user_cpus);

	/*
	 * Ensure the first_wildcard_match's PMU matches that of the new event
	 * being added. Otherwise try to match with another event further down
	 * the evlist.
	 */
	if (first_wildcard_match) {
		struct evsel *pos = list_prev_entry(first_wildcard_match, core.node);

		first_wildcard_match = NULL;
		list_for_each_entry_continue(pos, list, core.node) {
			if (perf_pmu__name_no_suffix_match(pos->pmu, pmu->name)) {
				first_wildcard_match = pos;
				break;
			}
			if (pos->pmu->is_core && (!pmu || pmu->is_core)) {
				first_wildcard_match = pos;
				break;
			}
		}
	}

	if (pmu) {
		perf_pmu__warn_invalid_formats(pmu);
		if (attr->type == PERF_TYPE_RAW || attr->type >= PERF_TYPE_MAX) {
			perf_pmu__warn_invalid_config(pmu, attr->config, name,
						PERF_PMU_FORMAT_VALUE_CONFIG, "config");
			perf_pmu__warn_invalid_config(pmu, attr->config1, name,
						PERF_PMU_FORMAT_VALUE_CONFIG1, "config1");
			perf_pmu__warn_invalid_config(pmu, attr->config2, name,
						PERF_PMU_FORMAT_VALUE_CONFIG2, "config2");
			perf_pmu__warn_invalid_config(pmu, attr->config3, name,
						PERF_PMU_FORMAT_VALUE_CONFIG3, "config3");
		}
	}
	/*
	 * If a PMU wasn't given, such as for legacy events, find now that
	 * warnings won't be generated.
	 */
	if (!pmu)
		pmu = perf_pmus__find_by_attr(attr);

	if (pmu) {
		is_pmu_core = pmu->is_core;
		pmu_cpus = perf_cpu_map__get(pmu->cpus);
		if (perf_cpu_map__is_empty(pmu_cpus))
			pmu_cpus = cpu_map__online();
	} else {
		is_pmu_core = (attr->type == PERF_TYPE_HARDWARE ||
			       attr->type == PERF_TYPE_HW_CACHE);
		pmu_cpus = is_pmu_core ? cpu_map__online() : NULL;
	}

	if (has_user_cpus)
		cpus = perf_cpu_map__get(user_cpus);
	else
		cpus = perf_cpu_map__get(pmu_cpus);

	if (init_attr)
		event_attr_init(attr);

	evsel = evsel__new_idx(attr, *idx);
	if (!evsel)
		goto out_err;

	if (name) {
		evsel->name = strdup(name);
		if (!evsel->name)
			goto out_err;
	}

	if (metric_id) {
		evsel->metric_id = strdup(metric_id);
		if (!evsel->metric_id)
			goto out_err;
	}

	(*idx)++;
	evsel->core.cpus = cpus;
	evsel->core.pmu_cpus = pmu_cpus;
	evsel->core.requires_cpu = pmu ? pmu->is_uncore : false;
	evsel->core.is_pmu_core = is_pmu_core;
	evsel->pmu = pmu;
	evsel->alternate_hw_config = alternate_hw_config;
	evsel->first_wildcard_match = first_wildcard_match;

	if (config_terms)
		list_splice_init(config_terms, &evsel->config_terms);

	if (list)
		list_add_tail(&evsel->core.node, list);

	if (has_user_cpus)
		evsel__warn_user_requested_cpus(evsel, user_cpus);

	return evsel;
out_err:
	perf_cpu_map__put(cpus);
	perf_cpu_map__put(pmu_cpus);
	zfree(&evsel->name);
	zfree(&evsel->metric_id);
	free(evsel);
	return NULL;
}

struct evsel *parse_events__add_event(int idx, struct perf_event_attr *attr,
				      const char *name, const char *metric_id,
				      struct perf_pmu *pmu)
{
	return __add_event(/*list=*/NULL, &idx, attr, /*init_attr=*/false, name,
			   metric_id, pmu, /*config_terms=*/NULL,
			   /*first_wildcard_match=*/NULL, /*cpu_list=*/NULL,
			   /*alternate_hw_config=*/PERF_COUNT_HW_MAX);
}

static int add_event(struct list_head *list, int *idx,
		     struct perf_event_attr *attr, const char *name,
		     const char *metric_id, struct list_head *config_terms,
		     u64 alternate_hw_config)
{
	return __add_event(list, idx, attr, /*init_attr*/true, name, metric_id,
			   /*pmu=*/NULL, config_terms,
			   /*first_wildcard_match=*/NULL, /*cpu_list=*/NULL,
			   alternate_hw_config) ? 0 : -ENOMEM;
}

/**
 * parse_aliases - search names for entries beginning or equalling str ignoring
 *                 case. If mutliple entries in names match str then the longest
 *                 is chosen.
 * @str: The needle to look for.
 * @names: The haystack to search.
 * @size: The size of the haystack.
 * @longest: Out argument giving the length of the matching entry.
 */
static int parse_aliases(const char *str, const char *const names[][EVSEL__MAX_ALIASES], int size,
			 int *longest)
{
	*longest = -1;
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < EVSEL__MAX_ALIASES && names[i][j]; j++) {
			int n = strlen(names[i][j]);

			if (n > *longest && !strncasecmp(str, names[i][j], n))
				*longest = n;
		}
		if (*longest > 0)
			return i;
	}

	return -1;
}

typedef int config_term_func_t(struct perf_event_attr *attr,
			       struct parse_events_term *term,
			       struct parse_events_state *parse_state);
static int config_term_common(struct perf_event_attr *attr,
			      struct parse_events_term *term,
			      struct parse_events_state *parse_state);
static int config_attr(struct perf_event_attr *attr,
		       const struct parse_events_terms *head,
		       struct parse_events_state *parse_state,
		       config_term_func_t config_term);

/**
 * parse_events__decode_legacy_cache - Search name for the legacy cache event
 *                                     name composed of 1, 2 or 3 hyphen
 *                                     separated sections. The first section is
 *                                     the cache type while the others are the
 *                                     optional op and optional result. To make
 *                                     life hard the names in the table also
 *                                     contain hyphens and the longest name
 *                                     should always be selected.
 */
int parse_events__decode_legacy_cache(const char *name, int extended_pmu_type, __u64 *config)
{
	int len, cache_type = -1, cache_op = -1, cache_result = -1;
	const char *name_end = &name[strlen(name) + 1];
	const char *str = name;

	cache_type = parse_aliases(str, evsel__hw_cache, PERF_COUNT_HW_CACHE_MAX, &len);
	if (cache_type == -1)
		return -EINVAL;
	str += len + 1;

	if (str < name_end) {
		cache_op = parse_aliases(str, evsel__hw_cache_op,
					PERF_COUNT_HW_CACHE_OP_MAX, &len);
		if (cache_op >= 0) {
			if (!evsel__is_cache_op_valid(cache_type, cache_op))
				return -EINVAL;
			str += len + 1;
		} else {
			cache_result = parse_aliases(str, evsel__hw_cache_result,
						PERF_COUNT_HW_CACHE_RESULT_MAX, &len);
			if (cache_result >= 0)
				str += len + 1;
		}
	}
	if (str < name_end) {
		if (cache_op < 0) {
			cache_op = parse_aliases(str, evsel__hw_cache_op,
						PERF_COUNT_HW_CACHE_OP_MAX, &len);
			if (cache_op >= 0) {
				if (!evsel__is_cache_op_valid(cache_type, cache_op))
					return -EINVAL;
			}
		} else if (cache_result < 0) {
			cache_result = parse_aliases(str, evsel__hw_cache_result,
						PERF_COUNT_HW_CACHE_RESULT_MAX, &len);
		}
	}

	/*
	 * Fall back to reads:
	 */
	if (cache_op == -1)
		cache_op = PERF_COUNT_HW_CACHE_OP_READ;

	/*
	 * Fall back to accesses:
	 */
	if (cache_result == -1)
		cache_result = PERF_COUNT_HW_CACHE_RESULT_ACCESS;

	*config = cache_type | (cache_op << 8) | (cache_result << 16);
	if (perf_pmus__supports_extended_type())
		*config |= (__u64)extended_pmu_type << PERF_PMU_TYPE_SHIFT;
	return 0;
}

/**
 * parse_events__filter_pmu - returns false if a wildcard PMU should be
 *                            considered, true if it should be filtered.
 */
bool parse_events__filter_pmu(const struct parse_events_state *parse_state,
			      const struct perf_pmu *pmu)
{
	if (parse_state->pmu_filter == NULL)
		return false;

	return strcmp(parse_state->pmu_filter, pmu->name) != 0;
}

static int parse_events_add_pmu(struct parse_events_state *parse_state,
				struct list_head *list, struct perf_pmu *pmu,
				const struct parse_events_terms *const_parsed_terms,
				struct evsel *first_wildcard_match, u64 alternate_hw_config);

int parse_events_add_cache(struct list_head *list, int *idx, const char *name,
			   struct parse_events_state *parse_state,
			   struct parse_events_terms *parsed_terms)
{
	struct perf_pmu *pmu = NULL;
	bool found_supported = false;
	const char *config_name = get_config_name(parsed_terms);
	const char *metric_id = get_config_metric_id(parsed_terms);
	struct perf_cpu_map *cpus = get_config_cpu(parsed_terms, parse_state->fake_pmu);
	int ret = 0;
	struct evsel *first_wildcard_match = NULL;

	while ((pmu = perf_pmus__scan_for_event(pmu, name)) != NULL) {
		LIST_HEAD(config_terms);
		struct perf_event_attr attr;

		if (parse_events__filter_pmu(parse_state, pmu))
			continue;

		if (perf_pmu__have_event(pmu, name)) {
			/*
			 * The PMU has the event so add as not a legacy cache
			 * event.
			 */
			ret = parse_events_add_pmu(parse_state, list, pmu,
						   parsed_terms,
						   first_wildcard_match,
						   /*alternate_hw_config=*/PERF_COUNT_HW_MAX);
			if (ret)
				goto out_err;
			if (first_wildcard_match == NULL)
				first_wildcard_match =
					container_of(list->prev, struct evsel, core.node);
			continue;
		}

		if (!pmu->is_core) {
			/* Legacy cache events are only supported by core PMUs. */
			continue;
		}

		memset(&attr, 0, sizeof(attr));
		attr.type = PERF_TYPE_HW_CACHE;

		ret = parse_events__decode_legacy_cache(name, pmu->type, &attr.config);
		if (ret)
			return ret;

		found_supported = true;

		if (parsed_terms) {
			if (config_attr(&attr, parsed_terms, parse_state, config_term_common)) {
				ret = -EINVAL;
				goto out_err;
			}
			if (get_config_terms(parsed_terms, &config_terms)) {
				ret = -ENOMEM;
				goto out_err;
			}
		}

		if (__add_event(list, idx, &attr, /*init_attr*/true, config_name ?: name,
				metric_id, pmu, &config_terms, first_wildcard_match,
				cpus, /*alternate_hw_config=*/PERF_COUNT_HW_MAX) == NULL)
			ret = -ENOMEM;

		if (first_wildcard_match == NULL)
			first_wildcard_match = container_of(list->prev, struct evsel, core.node);
		free_config_terms(&config_terms);
		if (ret)
			goto out_err;
	}
out_err:
	perf_cpu_map__put(cpus);
	return found_supported ? 0 : -EINVAL;
}

static void tracepoint_error(struct parse_events_error *e, int err,
			     const char *sys, const char *name, int column)
{
	const char *str;
	char help[BUFSIZ];

	if (!e)
		return;

	/*
	 * We get error directly from syscall errno ( > 0),
	 * or from encoded pointer's error ( < 0).
	 */
	err = abs(err);

	switch (err) {
	case EACCES:
		str = "can't access trace events";
		break;
	case ENOENT:
		str = "unknown tracepoint";
		break;
	default:
		str = "failed to add tracepoint";
		break;
	}

	tracing_path__strerror_open_tp(err, help, sizeof(help), sys, name);
	parse_events_error__handle(e, column, strdup(str), strdup(help));
}

static int add_tracepoint(struct parse_events_state *parse_state,
			  struct list_head *list,
			  const char *sys_name, const char *evt_name,
			  struct parse_events_error *err,
			  struct parse_events_terms *head_config, void *loc_)
{
	YYLTYPE *loc = loc_;
	struct evsel *evsel = evsel__newtp_idx(sys_name, evt_name, parse_state->idx++,
					       !parse_state->fake_tp);

	if (IS_ERR(evsel)) {
		tracepoint_error(err, PTR_ERR(evsel), sys_name, evt_name, loc->first_column);
		return PTR_ERR(evsel);
	}

	if (head_config) {
		LIST_HEAD(config_terms);

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
		list_splice(&config_terms, &evsel->config_terms);
	}

	list_add_tail(&evsel->core.node, list);
	return 0;
}

struct add_tracepoint_multi_args {
	struct parse_events_state *parse_state;
	struct list_head *list;
	const char *sys_glob;
	const char *evt_glob;
	struct parse_events_error *err;
	struct parse_events_terms *head_config;
	YYLTYPE *loc;
	int found;
};

static int add_tracepoint_multi_event_cb(void *state, const char *sys_name, const char *evt_name)
{
	struct add_tracepoint_multi_args *args = state;
	int ret;

	if (!strglobmatch(evt_name, args->evt_glob))
		return 0;

	args->found++;
	ret = add_tracepoint(args->parse_state, args->list, sys_name, evt_name,
			     args->err, args->head_config, args->loc);

	return ret;
}

static int add_tracepoint_multi_event(struct add_tracepoint_multi_args *args, const char *sys_name)
{
	if (strpbrk(args->evt_glob, "*?") == NULL) {
		/* Not a glob. */
		args->found++;
		return add_tracepoint(args->parse_state, args->list, sys_name, args->evt_glob,
				      args->err, args->head_config, args->loc);
	}

	return tp_pmu__for_each_tp_event(sys_name, args, add_tracepoint_multi_event_cb);
}

static int add_tracepoint_multi_sys_cb(void *state, const char *sys_name)
{
	struct add_tracepoint_multi_args *args = state;

	if (!strglobmatch(sys_name, args->sys_glob))
		return 0;

	return add_tracepoint_multi_event(args, sys_name);
}

static int add_tracepoint_multi_sys(struct parse_events_state *parse_state,
				    struct list_head *list,
				    const char *sys_glob, const char *evt_glob,
				    struct parse_events_error *err,
				    struct parse_events_terms *head_config, YYLTYPE *loc)
{
	struct add_tracepoint_multi_args args = {
		.parse_state = parse_state,
		.list = list,
		.sys_glob = sys_glob,
		.evt_glob = evt_glob,
		.err = err,
		.head_config = head_config,
		.loc = loc,
		.found = 0,
	};
	int ret;

	if (strpbrk(sys_glob, "*?") == NULL) {
		/* Not a glob. */
		ret = add_tracepoint_multi_event(&args, sys_glob);
	} else {
		ret = tp_pmu__for_each_tp_sys(&args, add_tracepoint_multi_sys_cb);
	}
	if (args.found == 0) {
		tracepoint_error(err, ENOENT, sys_glob, evt_glob, loc->first_column);
		return -ENOENT;
	}
	return ret;
}

size_t default_breakpoint_len(void)
{
#if defined(__i386__)
	static int len;

	if (len == 0) {
		struct perf_env env = {};

		perf_env__init(&env);
		len = perf_env__kernel_is_64_bit(&env) ? sizeof(u64) : sizeof(long);
		perf_env__exit(&env);
	}
	return len;
#elif defined(__aarch64__)
	return 4;
#else
	return sizeof(long);
#endif
}

static int
parse_breakpoint_type(const char *type, struct perf_event_attr *attr)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!type || !type[i])
			break;

#define CHECK_SET_TYPE(bit)		\
do {					\
	if (attr->bp_type & bit)	\
		return -EINVAL;		\
	else				\
		attr->bp_type |= bit;	\
} while (0)

		switch (type[i]) {
		case 'r':
			CHECK_SET_TYPE(HW_BREAKPOINT_R);
			break;
		case 'w':
			CHECK_SET_TYPE(HW_BREAKPOINT_W);
			break;
		case 'x':
			CHECK_SET_TYPE(HW_BREAKPOINT_X);
			break;
		default:
			return -EINVAL;
		}
	}

#undef CHECK_SET_TYPE

	if (!attr->bp_type) /* Default */
		attr->bp_type = HW_BREAKPOINT_R | HW_BREAKPOINT_W;

	return 0;
}

int parse_events_add_breakpoint(struct parse_events_state *parse_state,
				struct list_head *list,
				u64 addr, char *type, u64 len,
				struct parse_events_terms *head_config)
{
	struct perf_event_attr attr;
	LIST_HEAD(config_terms);
	const char *name;

	memset(&attr, 0, sizeof(attr));
	attr.bp_addr = addr;

	if (parse_breakpoint_type(type, &attr))
		return -EINVAL;

	/* Provide some defaults if len is not specified */
	if (!len) {
		if (attr.bp_type == HW_BREAKPOINT_X)
			len = default_breakpoint_len();
		else
			len = HW_BREAKPOINT_LEN_4;
	}

	attr.bp_len = len;

	attr.type = PERF_TYPE_BREAKPOINT;
	attr.sample_period = 1;

	if (head_config) {
		if (config_attr(&attr, head_config, parse_state, config_term_common))
			return -EINVAL;

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
	}

	name = get_config_name(head_config);

	return add_event(list, &parse_state->idx, &attr, name, /*mertic_id=*/NULL,
			&config_terms, /*alternate_hw_config=*/PERF_COUNT_HW_MAX);
}

static int check_type_val(struct parse_events_term *term,
			  struct parse_events_error *err,
			  enum parse_events__term_val_type type)
{
	if (type == term->type_val)
		return 0;

	if (err) {
		parse_events_error__handle(err, term->err_val,
					type == PARSE_EVENTS__TERM_TYPE_NUM
					? strdup("expected numeric value")
					: strdup("expected string value"),
					NULL);
	}
	return -EINVAL;
}

static bool config_term_shrinked;

const char *parse_events__term_type_str(enum parse_events__term_type term_type)
{
	/*
	 * Update according to parse-events.l
	 */
	static const char *config_term_names[__PARSE_EVENTS__TERM_TYPE_NR] = {
		[PARSE_EVENTS__TERM_TYPE_USER]			= "<sysfs term>",
		[PARSE_EVENTS__TERM_TYPE_CONFIG]		= "config",
		[PARSE_EVENTS__TERM_TYPE_CONFIG1]		= "config1",
		[PARSE_EVENTS__TERM_TYPE_CONFIG2]		= "config2",
		[PARSE_EVENTS__TERM_TYPE_CONFIG3]		= "config3",
		[PARSE_EVENTS__TERM_TYPE_NAME]			= "name",
		[PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD]		= "period",
		[PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ]		= "freq",
		[PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE]	= "branch_type",
		[PARSE_EVENTS__TERM_TYPE_TIME]			= "time",
		[PARSE_EVENTS__TERM_TYPE_CALLGRAPH]		= "call-graph",
		[PARSE_EVENTS__TERM_TYPE_STACKSIZE]		= "stack-size",
		[PARSE_EVENTS__TERM_TYPE_NOINHERIT]		= "no-inherit",
		[PARSE_EVENTS__TERM_TYPE_INHERIT]		= "inherit",
		[PARSE_EVENTS__TERM_TYPE_MAX_STACK]		= "max-stack",
		[PARSE_EVENTS__TERM_TYPE_MAX_EVENTS]		= "nr",
		[PARSE_EVENTS__TERM_TYPE_OVERWRITE]		= "overwrite",
		[PARSE_EVENTS__TERM_TYPE_NOOVERWRITE]		= "no-overwrite",
		[PARSE_EVENTS__TERM_TYPE_DRV_CFG]		= "driver-config",
		[PARSE_EVENTS__TERM_TYPE_PERCORE]		= "percore",
		[PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT]		= "aux-output",
		[PARSE_EVENTS__TERM_TYPE_AUX_ACTION]		= "aux-action",
		[PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE]	= "aux-sample-size",
		[PARSE_EVENTS__TERM_TYPE_METRIC_ID]		= "metric-id",
		[PARSE_EVENTS__TERM_TYPE_RAW]                   = "raw",
		[PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE]          = "legacy-cache",
		[PARSE_EVENTS__TERM_TYPE_HARDWARE]              = "hardware",
		[PARSE_EVENTS__TERM_TYPE_CPU]			= "cpu",
		[PARSE_EVENTS__TERM_TYPE_RATIO_TO_PREV]         = "ratio-to-prev",
	};
	if ((unsigned int)term_type >= __PARSE_EVENTS__TERM_TYPE_NR)
		return "unknown term";

	return config_term_names[term_type];
}

static bool
config_term_avail(enum parse_events__term_type term_type, struct parse_events_error *err)
{
	char *err_str;

	if (term_type < 0 || term_type >= __PARSE_EVENTS__TERM_TYPE_NR) {
		parse_events_error__handle(err, -1,
					strdup("Invalid term_type"), NULL);
		return false;
	}
	if (!config_term_shrinked)
		return true;

	switch (term_type) {
	case PARSE_EVENTS__TERM_TYPE_CONFIG:
	case PARSE_EVENTS__TERM_TYPE_CONFIG1:
	case PARSE_EVENTS__TERM_TYPE_CONFIG2:
	case PARSE_EVENTS__TERM_TYPE_CONFIG3:
	case PARSE_EVENTS__TERM_TYPE_NAME:
	case PARSE_EVENTS__TERM_TYPE_METRIC_ID:
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
	case PARSE_EVENTS__TERM_TYPE_PERCORE:
	case PARSE_EVENTS__TERM_TYPE_CPU:
		return true;
	case PARSE_EVENTS__TERM_TYPE_USER:
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ:
	case PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE:
	case PARSE_EVENTS__TERM_TYPE_TIME:
	case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
	case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
	case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
	case PARSE_EVENTS__TERM_TYPE_INHERIT:
	case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
	case PARSE_EVENTS__TERM_TYPE_MAX_EVENTS:
	case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
	case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
	case PARSE_EVENTS__TERM_TYPE_DRV_CFG:
	case PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT:
	case PARSE_EVENTS__TERM_TYPE_AUX_ACTION:
	case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
	case PARSE_EVENTS__TERM_TYPE_RAW:
	case PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE:
	case PARSE_EVENTS__TERM_TYPE_HARDWARE:
	case PARSE_EVENTS__TERM_TYPE_RATIO_TO_PREV:
	default:
		if (!err)
			return false;

		/* term_type is validated so indexing is safe */
		if (asprintf(&err_str, "'%s' is not usable in 'perf stat'",
			     parse_events__term_type_str(term_type)) >= 0)
			parse_events_error__handle(err, -1, err_str, NULL);
		return false;
	}
}

void parse_events__shrink_config_terms(void)
{
	config_term_shrinked = true;
}

static int config_term_common(struct perf_event_attr *attr,
			      struct parse_events_term *term,
			      struct parse_events_state *parse_state)
{
#define CHECK_TYPE_VAL(type)								\
do {											\
	if (check_type_val(term, parse_state->error, PARSE_EVENTS__TERM_TYPE_ ## type))	\
		return -EINVAL;								\
} while (0)

	switch (term->type_term) {
	case PARSE_EVENTS__TERM_TYPE_CONFIG:
		CHECK_TYPE_VAL(NUM);
		attr->config = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_CONFIG1:
		CHECK_TYPE_VAL(NUM);
		attr->config1 = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_CONFIG2:
		CHECK_TYPE_VAL(NUM);
		attr->config2 = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_CONFIG3:
		CHECK_TYPE_VAL(NUM);
		attr->config3 = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE:
		CHECK_TYPE_VAL(STR);
		if (strcmp(term->val.str, "no") &&
		    parse_branch_str(term->val.str,
				    &attr->branch_sample_type)) {
			parse_events_error__handle(parse_state->error, term->err_val,
					strdup("invalid branch sample type"),
					NULL);
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_TIME:
		CHECK_TYPE_VAL(NUM);
		if (term->val.num > 1) {
			parse_events_error__handle(parse_state->error, term->err_val,
						strdup("expected 0 or 1"),
						NULL);
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
		CHECK_TYPE_VAL(STR);
		break;
	case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_INHERIT:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_NAME:
		CHECK_TYPE_VAL(STR);
		break;
	case PARSE_EVENTS__TERM_TYPE_METRIC_ID:
		CHECK_TYPE_VAL(STR);
		break;
	case PARSE_EVENTS__TERM_TYPE_RAW:
		CHECK_TYPE_VAL(STR);
		break;
	case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_MAX_EVENTS:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_PERCORE:
		CHECK_TYPE_VAL(NUM);
		if ((unsigned int)term->val.num > 1) {
			parse_events_error__handle(parse_state->error, term->err_val,
						strdup("expected 0 or 1"),
						NULL);
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_AUX_ACTION:
		CHECK_TYPE_VAL(STR);
		break;
	case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
		CHECK_TYPE_VAL(NUM);
		if (term->val.num > UINT_MAX) {
			parse_events_error__handle(parse_state->error, term->err_val,
						strdup("too big"),
						NULL);
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_CPU: {
		struct perf_cpu_map *map;

		if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM) {
			if (term->val.num >= (u64)cpu__max_present_cpu().cpu) {
				parse_events_error__handle(parse_state->error, term->err_val,
							strdup("too big"),
							/*help=*/NULL);
				return -EINVAL;
			}
			break;
		}
		assert(term->type_val == PARSE_EVENTS__TERM_TYPE_STR);
		if (perf_pmus__find(term->val.str) != NULL)
			break;

		map = perf_cpu_map__new(term->val.str);
		if (!map && !parse_state->fake_pmu) {
			parse_events_error__handle(parse_state->error, term->err_val,
						   strdup("not a valid PMU or CPU number"),
						   /*help=*/NULL);
			return -EINVAL;
		}
		perf_cpu_map__put(map);
		break;
	}
	case PARSE_EVENTS__TERM_TYPE_RATIO_TO_PREV:
		CHECK_TYPE_VAL(STR);
		if (strtod(term->val.str, NULL) <= 0) {
			parse_events_error__handle(parse_state->error, term->err_val,
						   strdup("zero or negative"),
						   NULL);
			return -EINVAL;
		}
		if (errno == ERANGE) {
			parse_events_error__handle(parse_state->error, term->err_val,
						   strdup("too big"),
						   NULL);
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_DRV_CFG:
	case PARSE_EVENTS__TERM_TYPE_USER:
	case PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE:
	case PARSE_EVENTS__TERM_TYPE_HARDWARE:
	default:
		parse_events_error__handle(parse_state->error, term->err_term,
					strdup(parse_events__term_type_str(term->type_term)),
					parse_events_formats_error_string(NULL));
		return -EINVAL;
	}

	/*
	 * Check term availability after basic checking so
	 * PARSE_EVENTS__TERM_TYPE_USER can be found and filtered.
	 *
	 * If check availability at the entry of this function,
	 * user will see "'<sysfs term>' is not usable in 'perf stat'"
	 * if an invalid config term is provided for legacy events
	 * (for example, instructions/badterm/...), which is confusing.
	 */
	if (!config_term_avail(term->type_term, parse_state->error))
		return -EINVAL;
	return 0;
#undef CHECK_TYPE_VAL
}

static int config_term_pmu(struct perf_event_attr *attr,
			   struct parse_events_term *term,
			   struct parse_events_state *parse_state)
{
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE) {
		struct perf_pmu *pmu = perf_pmus__find_by_type(attr->type);

		if (!pmu) {
			char *err_str;

			if (asprintf(&err_str, "Failed to find PMU for type %d", attr->type) >= 0)
				parse_events_error__handle(parse_state->error, term->err_term,
							   err_str, /*help=*/NULL);
			return -EINVAL;
		}
		/*
		 * Rewrite the PMU event to a legacy cache one unless the PMU
		 * doesn't support legacy cache events or the event is present
		 * within the PMU.
		 */
		if (perf_pmu__supports_legacy_cache(pmu) &&
		    !perf_pmu__have_event(pmu, term->config)) {
			attr->type = PERF_TYPE_HW_CACHE;
			return parse_events__decode_legacy_cache(term->config, pmu->type,
								 &attr->config);
		} else {
			term->type_term = PARSE_EVENTS__TERM_TYPE_USER;
			term->no_value = true;
		}
	}
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_HARDWARE) {
		struct perf_pmu *pmu = perf_pmus__find_by_type(attr->type);

		if (!pmu) {
			char *err_str;

			if (asprintf(&err_str, "Failed to find PMU for type %d", attr->type) >= 0)
				parse_events_error__handle(parse_state->error, term->err_term,
							   err_str, /*help=*/NULL);
			return -EINVAL;
		}
		/*
		 * If the PMU has a sysfs or json event prefer it over
		 * legacy. ARM requires this.
		 */
		if (perf_pmu__have_event(pmu, term->config)) {
			term->type_term = PARSE_EVENTS__TERM_TYPE_USER;
			term->no_value = true;
			term->alternate_hw_config = true;
		} else {
			attr->type = PERF_TYPE_HARDWARE;
			attr->config = term->val.num;
			if (perf_pmus__supports_extended_type())
				attr->config |= (__u64)pmu->type << PERF_PMU_TYPE_SHIFT;
		}
		return 0;
	}
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_USER ||
	    term->type_term == PARSE_EVENTS__TERM_TYPE_DRV_CFG) {
		/*
		 * Always succeed for sysfs terms, as we dont know
		 * at this point what type they need to have.
		 */
		return 0;
	}
	return config_term_common(attr, term, parse_state);
}

static int config_term_tracepoint(struct perf_event_attr *attr,
				  struct parse_events_term *term,
				  struct parse_events_state *parse_state)
{
	switch (term->type_term) {
	case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
	case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
	case PARSE_EVENTS__TERM_TYPE_INHERIT:
	case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
	case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
	case PARSE_EVENTS__TERM_TYPE_MAX_EVENTS:
	case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
	case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
	case PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT:
	case PARSE_EVENTS__TERM_TYPE_AUX_ACTION:
	case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
		return config_term_common(attr, term, parse_state);
	case PARSE_EVENTS__TERM_TYPE_USER:
	case PARSE_EVENTS__TERM_TYPE_CONFIG:
	case PARSE_EVENTS__TERM_TYPE_CONFIG1:
	case PARSE_EVENTS__TERM_TYPE_CONFIG2:
	case PARSE_EVENTS__TERM_TYPE_CONFIG3:
	case PARSE_EVENTS__TERM_TYPE_NAME:
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ:
	case PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE:
	case PARSE_EVENTS__TERM_TYPE_TIME:
	case PARSE_EVENTS__TERM_TYPE_DRV_CFG:
	case PARSE_EVENTS__TERM_TYPE_PERCORE:
	case PARSE_EVENTS__TERM_TYPE_METRIC_ID:
	case PARSE_EVENTS__TERM_TYPE_RAW:
	case PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE:
	case PARSE_EVENTS__TERM_TYPE_HARDWARE:
	case PARSE_EVENTS__TERM_TYPE_CPU:
	case PARSE_EVENTS__TERM_TYPE_RATIO_TO_PREV:
	default:
		parse_events_error__handle(parse_state->error, term->err_term,
					strdup(parse_events__term_type_str(term->type_term)),
					strdup("valid terms: call-graph,stack-size\n")
				);
		return -EINVAL;
	}

	return 0;
}

static int config_attr(struct perf_event_attr *attr,
		       const struct parse_events_terms *head,
		       struct parse_events_state *parse_state,
		       config_term_func_t config_term)
{
	struct parse_events_term *term;

	list_for_each_entry(term, &head->terms, list)
		if (config_term(attr, term, parse_state))
			return -EINVAL;

	return 0;
}

static int get_config_terms(const struct parse_events_terms *head_config,
			    struct list_head *head_terms)
{
#define ADD_CONFIG_TERM(__type, __weak)				\
	struct evsel_config_term *__t;			\
								\
	__t = zalloc(sizeof(*__t));				\
	if (!__t)						\
		return -ENOMEM;					\
								\
	INIT_LIST_HEAD(&__t->list);				\
	__t->type       = EVSEL__CONFIG_TERM_ ## __type;	\
	__t->weak	= __weak;				\
	list_add_tail(&__t->list, head_terms)

#define ADD_CONFIG_TERM_VAL(__type, __name, __val, __weak)	\
do {								\
	ADD_CONFIG_TERM(__type, __weak);			\
	__t->val.__name = __val;				\
} while (0)

#define ADD_CONFIG_TERM_STR(__type, __val, __weak)		\
do {								\
	ADD_CONFIG_TERM(__type, __weak);			\
	__t->val.str = strdup(__val);				\
	if (!__t->val.str) {					\
		zfree(&__t);					\
		return -ENOMEM;					\
	}							\
	__t->free_str = true;					\
} while (0)

	struct parse_events_term *term;

	list_for_each_entry(term, &head_config->terms, list) {
		switch (term->type_term) {
		case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
			ADD_CONFIG_TERM_VAL(PERIOD, period, term->val.num, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ:
			ADD_CONFIG_TERM_VAL(FREQ, freq, term->val.num, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_TIME:
			ADD_CONFIG_TERM_VAL(TIME, time, term->val.num, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
			ADD_CONFIG_TERM_STR(CALLGRAPH, term->val.str, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE:
			ADD_CONFIG_TERM_STR(BRANCH, term->val.str, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
			ADD_CONFIG_TERM_VAL(STACK_USER, stack_user,
					    term->val.num, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_INHERIT:
			ADD_CONFIG_TERM_VAL(INHERIT, inherit,
					    term->val.num ? 1 : 0, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
			ADD_CONFIG_TERM_VAL(INHERIT, inherit,
					    term->val.num ? 0 : 1, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
			ADD_CONFIG_TERM_VAL(MAX_STACK, max_stack,
					    term->val.num, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_MAX_EVENTS:
			ADD_CONFIG_TERM_VAL(MAX_EVENTS, max_events,
					    term->val.num, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
			ADD_CONFIG_TERM_VAL(OVERWRITE, overwrite,
					    term->val.num ? 1 : 0, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
			ADD_CONFIG_TERM_VAL(OVERWRITE, overwrite,
					    term->val.num ? 0 : 1, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_DRV_CFG:
			ADD_CONFIG_TERM_STR(DRV_CFG, term->val.str, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_PERCORE:
			ADD_CONFIG_TERM_VAL(PERCORE, percore,
					    term->val.num ? true : false, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT:
			ADD_CONFIG_TERM_VAL(AUX_OUTPUT, aux_output,
					    term->val.num ? 1 : 0, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_AUX_ACTION:
			ADD_CONFIG_TERM_STR(AUX_ACTION, term->val.str, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
			ADD_CONFIG_TERM_VAL(AUX_SAMPLE_SIZE, aux_sample_size,
					    term->val.num, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_RATIO_TO_PREV:
			ADD_CONFIG_TERM_STR(RATIO_TO_PREV, term->val.str, term->weak);
			break;
		case PARSE_EVENTS__TERM_TYPE_USER:
		case PARSE_EVENTS__TERM_TYPE_CONFIG:
		case PARSE_EVENTS__TERM_TYPE_CONFIG1:
		case PARSE_EVENTS__TERM_TYPE_CONFIG2:
		case PARSE_EVENTS__TERM_TYPE_CONFIG3:
		case PARSE_EVENTS__TERM_TYPE_NAME:
		case PARSE_EVENTS__TERM_TYPE_METRIC_ID:
		case PARSE_EVENTS__TERM_TYPE_RAW:
		case PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE:
		case PARSE_EVENTS__TERM_TYPE_HARDWARE:
		case PARSE_EVENTS__TERM_TYPE_CPU:
		default:
			break;
		}
	}
	return 0;
}

/*
 * Add EVSEL__CONFIG_TERM_CFG_CHG where cfg_chg will have a bit set for
 * each bit of attr->config that the user has changed.
 */
static int get_config_chgs(struct perf_pmu *pmu, struct parse_events_terms *head_config,
			   struct list_head *head_terms)
{
	struct parse_events_term *term;
	u64 bits = 0;
	int type;

	list_for_each_entry(term, &head_config->terms, list) {
		switch (term->type_term) {
		case PARSE_EVENTS__TERM_TYPE_USER:
			type = perf_pmu__format_type(pmu, term->config);
			if (type != PERF_PMU_FORMAT_VALUE_CONFIG)
				continue;
			bits |= perf_pmu__format_bits(pmu, term->config);
			break;
		case PARSE_EVENTS__TERM_TYPE_CONFIG:
			bits = ~(u64)0;
			break;
		case PARSE_EVENTS__TERM_TYPE_CONFIG1:
		case PARSE_EVENTS__TERM_TYPE_CONFIG2:
		case PARSE_EVENTS__TERM_TYPE_CONFIG3:
		case PARSE_EVENTS__TERM_TYPE_NAME:
		case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
		case PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ:
		case PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE:
		case PARSE_EVENTS__TERM_TYPE_TIME:
		case PARSE_EVENTS__TERM_TYPE_CALLGRAPH:
		case PARSE_EVENTS__TERM_TYPE_STACKSIZE:
		case PARSE_EVENTS__TERM_TYPE_NOINHERIT:
		case PARSE_EVENTS__TERM_TYPE_INHERIT:
		case PARSE_EVENTS__TERM_TYPE_MAX_STACK:
		case PARSE_EVENTS__TERM_TYPE_MAX_EVENTS:
		case PARSE_EVENTS__TERM_TYPE_NOOVERWRITE:
		case PARSE_EVENTS__TERM_TYPE_OVERWRITE:
		case PARSE_EVENTS__TERM_TYPE_DRV_CFG:
		case PARSE_EVENTS__TERM_TYPE_PERCORE:
		case PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT:
		case PARSE_EVENTS__TERM_TYPE_AUX_ACTION:
		case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
		case PARSE_EVENTS__TERM_TYPE_METRIC_ID:
		case PARSE_EVENTS__TERM_TYPE_RAW:
		case PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE:
		case PARSE_EVENTS__TERM_TYPE_HARDWARE:
		case PARSE_EVENTS__TERM_TYPE_CPU:
		case PARSE_EVENTS__TERM_TYPE_RATIO_TO_PREV:
		default:
			break;
		}
	}

	if (bits)
		ADD_CONFIG_TERM_VAL(CFG_CHG, cfg_chg, bits, false);

#undef ADD_CONFIG_TERM
	return 0;
}

int parse_events_add_tracepoint(struct parse_events_state *parse_state,
				struct list_head *list,
				const char *sys, const char *event,
				struct parse_events_error *err,
				struct parse_events_terms *head_config, void *loc_)
{
	YYLTYPE *loc = loc_;

	if (head_config) {
		struct perf_event_attr attr;

		if (config_attr(&attr, head_config, parse_state, config_term_tracepoint))
			return -EINVAL;
	}

	return add_tracepoint_multi_sys(parse_state, list, sys, event,
					err, head_config, loc);
}

static int __parse_events_add_numeric(struct parse_events_state *parse_state,
				struct list_head *list,
				struct perf_pmu *pmu, u32 type, u32 extended_type,
				u64 config, const struct parse_events_terms *head_config,
				struct evsel *first_wildcard_match)
{
	struct perf_event_attr attr;
	LIST_HEAD(config_terms);
	const char *name, *metric_id;
	struct perf_cpu_map *cpus;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.type = type;
	attr.config = config;
	if (extended_type && (type == PERF_TYPE_HARDWARE || type == PERF_TYPE_HW_CACHE)) {
		assert(perf_pmus__supports_extended_type());
		attr.config |= (u64)extended_type << PERF_PMU_TYPE_SHIFT;
	}

	if (head_config) {
		if (config_attr(&attr, head_config, parse_state, config_term_common))
			return -EINVAL;

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
	}

	name = get_config_name(head_config);
	metric_id = get_config_metric_id(head_config);
	cpus = get_config_cpu(head_config, parse_state->fake_pmu);
	ret = __add_event(list, &parse_state->idx, &attr, /*init_attr*/true, name,
			metric_id, pmu, &config_terms, first_wildcard_match,
			cpus, /*alternate_hw_config=*/PERF_COUNT_HW_MAX) ? 0 : -ENOMEM;
	perf_cpu_map__put(cpus);
	free_config_terms(&config_terms);
	return ret;
}

int parse_events_add_numeric(struct parse_events_state *parse_state,
			     struct list_head *list,
			     u32 type, u64 config,
			     const struct parse_events_terms *head_config,
			     bool wildcard)
{
	struct perf_pmu *pmu = NULL;
	bool found_supported = false;

	/* Wildcards on numeric values are only supported by core PMUs. */
	if (wildcard && perf_pmus__supports_extended_type()) {
		struct evsel *first_wildcard_match = NULL;
		while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
			int ret;

			found_supported = true;
			if (parse_events__filter_pmu(parse_state, pmu))
				continue;

			ret = __parse_events_add_numeric(parse_state, list, pmu,
							 type, pmu->type,
							 config, head_config,
							 first_wildcard_match);
			if (ret)
				return ret;
			if (first_wildcard_match == NULL)
				first_wildcard_match =
					container_of(list->prev, struct evsel, core.node);
		}
		if (found_supported)
			return 0;
	}
	return __parse_events_add_numeric(parse_state, list, perf_pmus__find_by_type(type),
					type, /*extended_type=*/0, config, head_config,
					/*first_wildcard_match=*/NULL);
}

static bool config_term_percore(struct list_head *config_terms)
{
	struct evsel_config_term *term;

	list_for_each_entry(term, config_terms, list) {
		if (term->type == EVSEL__CONFIG_TERM_PERCORE)
			return term->val.percore;
	}

	return false;
}

static int parse_events_add_pmu(struct parse_events_state *parse_state,
				struct list_head *list, struct perf_pmu *pmu,
				const struct parse_events_terms *const_parsed_terms,
				struct evsel *first_wildcard_match, u64 alternate_hw_config)
{
	struct perf_event_attr attr;
	struct perf_pmu_info info;
	struct evsel *evsel;
	struct parse_events_error *err = parse_state->error;
	LIST_HEAD(config_terms);
	struct parse_events_terms parsed_terms;
	bool alias_rewrote_terms = false;
	struct perf_cpu_map *term_cpu = NULL;

	if (verbose > 1) {
		struct strbuf sb;

		strbuf_init(&sb, /*hint=*/ 0);
		if (pmu->selectable && const_parsed_terms &&
		    list_empty(&const_parsed_terms->terms)) {
			strbuf_addf(&sb, "%s//", pmu->name);
		} else {
			strbuf_addf(&sb, "%s/", pmu->name);
			parse_events_terms__to_strbuf(const_parsed_terms, &sb);
			strbuf_addch(&sb, '/');
		}
		fprintf(stderr, "Attempt to add: %s\n", sb.buf);
		strbuf_release(&sb);
	}

	memset(&attr, 0, sizeof(attr));
	if (pmu->perf_event_attr_init_default)
		pmu->perf_event_attr_init_default(pmu, &attr);

	attr.type = pmu->type;

	if (!const_parsed_terms || list_empty(&const_parsed_terms->terms)) {
		evsel = __add_event(list, &parse_state->idx, &attr,
				    /*init_attr=*/true, /*name=*/NULL,
				    /*metric_id=*/NULL, pmu,
				    /*config_terms=*/NULL, first_wildcard_match,
				    /*cpu_list=*/NULL, alternate_hw_config);
		return evsel ? 0 : -ENOMEM;
	}

	parse_events_terms__init(&parsed_terms);
	if (const_parsed_terms) {
		int ret = parse_events_terms__copy(const_parsed_terms, &parsed_terms);

		if (ret)
			return ret;
	}
	fix_raw(&parsed_terms, pmu);

	/* Configure attr/terms with a known PMU, this will set hardcoded terms. */
	if (config_attr(&attr, &parsed_terms, parse_state, config_term_pmu)) {
		parse_events_terms__exit(&parsed_terms);
		return -EINVAL;
	}

	/* Look for event names in the terms and rewrite into format based terms. */
	if (perf_pmu__check_alias(pmu, &parsed_terms,
				  &info, &alias_rewrote_terms,
				  &alternate_hw_config, err)) {
		parse_events_terms__exit(&parsed_terms);
		return -EINVAL;
	}

	if (verbose > 1) {
		struct strbuf sb;

		strbuf_init(&sb, /*hint=*/ 0);
		parse_events_terms__to_strbuf(&parsed_terms, &sb);
		fprintf(stderr, "..after resolving event: %s/%s/\n", pmu->name, sb.buf);
		strbuf_release(&sb);
	}

	/* Configure attr/terms again if an alias was expanded. */
	if (alias_rewrote_terms &&
	    config_attr(&attr, &parsed_terms, parse_state, config_term_pmu)) {
		parse_events_terms__exit(&parsed_terms);
		return -EINVAL;
	}

	if (get_config_terms(&parsed_terms, &config_terms)) {
		parse_events_terms__exit(&parsed_terms);
		return -ENOMEM;
	}

	/*
	 * When using default config, record which bits of attr->config were
	 * changed by the user.
	 */
	if (pmu->perf_event_attr_init_default &&
	    get_config_chgs(pmu, &parsed_terms, &config_terms)) {
		parse_events_terms__exit(&parsed_terms);
		return -ENOMEM;
	}

	/* Skip configuring hard coded terms that were applied by config_attr. */
	if (perf_pmu__config(pmu, &attr, &parsed_terms, /*apply_hardcoded=*/false,
			     parse_state->error)) {
		free_config_terms(&config_terms);
		parse_events_terms__exit(&parsed_terms);
		return -EINVAL;
	}

	term_cpu = get_config_cpu(&parsed_terms, parse_state->fake_pmu);
	evsel = __add_event(list, &parse_state->idx, &attr, /*init_attr=*/true,
			    get_config_name(&parsed_terms),
			    get_config_metric_id(&parsed_terms), pmu,
			    &config_terms, first_wildcard_match, term_cpu, alternate_hw_config);
	perf_cpu_map__put(term_cpu);
	if (!evsel) {
		parse_events_terms__exit(&parsed_terms);
		return -ENOMEM;
	}

	if (evsel->name)
		evsel->use_config_name = true;

	evsel->percore = config_term_percore(&evsel->config_terms);

	parse_events_terms__exit(&parsed_terms);
	free((char *)evsel->unit);
	evsel->unit = strdup(info.unit);
	evsel->scale = info.scale;
	evsel->per_pkg = info.per_pkg;
	evsel->snapshot = info.snapshot;
	evsel->retirement_latency.mean = info.retirement_latency_mean;
	evsel->retirement_latency.min = info.retirement_latency_min;
	evsel->retirement_latency.max = info.retirement_latency_max;

	return 0;
}

int parse_events_multi_pmu_add(struct parse_events_state *parse_state,
			       const char *event_name, u64 hw_config,
			       const struct parse_events_terms *const_parsed_terms,
			       struct list_head **listp, void *loc_)
{
	struct parse_events_term *term;
	struct list_head *list = NULL;
	struct perf_pmu *pmu = NULL;
	YYLTYPE *loc = loc_;
	int ok = 0;
	const char *config;
	struct parse_events_terms parsed_terms;
	struct evsel *first_wildcard_match = NULL;

	*listp = NULL;

	parse_events_terms__init(&parsed_terms);
	if (const_parsed_terms) {
		int ret = parse_events_terms__copy(const_parsed_terms, &parsed_terms);

		if (ret)
			return ret;
	}

	config = strdup(event_name);
	if (!config)
		goto out_err;

	if (parse_events_term__num(&term,
				   PARSE_EVENTS__TERM_TYPE_USER,
				   config, /*num=*/1, /*novalue=*/true,
				   loc, /*loc_val=*/NULL) < 0) {
		zfree(&config);
		goto out_err;
	}
	list_add_tail(&term->list, &parsed_terms.terms);

	/* Add it for all PMUs that support the alias */
	list = malloc(sizeof(struct list_head));
	if (!list)
		goto out_err;

	INIT_LIST_HEAD(list);

	while ((pmu = perf_pmus__scan_for_event(pmu, event_name)) != NULL) {

		if (parse_events__filter_pmu(parse_state, pmu))
			continue;

		if (!perf_pmu__have_event(pmu, event_name))
			continue;

		if (!parse_events_add_pmu(parse_state, list, pmu,
					  &parsed_terms, first_wildcard_match, hw_config)) {
			struct strbuf sb;

			strbuf_init(&sb, /*hint=*/ 0);
			parse_events_terms__to_strbuf(&parsed_terms, &sb);
			pr_debug("%s -> %s/%s/\n", event_name, pmu->name, sb.buf);
			strbuf_release(&sb);
			ok++;
		}
		if (first_wildcard_match == NULL)
			first_wildcard_match = container_of(list->prev, struct evsel, core.node);
	}

	if (parse_state->fake_pmu) {
		if (!parse_events_add_pmu(parse_state, list, perf_pmus__fake_pmu(), &parsed_terms,
					 first_wildcard_match, hw_config)) {
			struct strbuf sb;

			strbuf_init(&sb, /*hint=*/ 0);
			parse_events_terms__to_strbuf(&parsed_terms, &sb);
			pr_debug("%s -> fake/%s/\n", event_name, sb.buf);
			strbuf_release(&sb);
			ok++;
		}
	}

out_err:
	parse_events_terms__exit(&parsed_terms);
	if (ok)
		*listp = list;
	else
		free(list);

	return ok ? 0 : -1;
}

int parse_events_multi_pmu_add_or_add_pmu(struct parse_events_state *parse_state,
					const char *event_or_pmu,
					const struct parse_events_terms *const_parsed_terms,
					struct list_head **listp,
					void *loc_)
{
	YYLTYPE *loc = loc_;
	struct perf_pmu *pmu;
	int ok = 0;
	char *help;
	struct evsel *first_wildcard_match = NULL;

	*listp = malloc(sizeof(**listp));
	if (!*listp)
		return -ENOMEM;

	INIT_LIST_HEAD(*listp);

	/* Attempt to add to list assuming event_or_pmu is a PMU name. */
	pmu = perf_pmus__find(event_or_pmu);
	if (pmu && !parse_events_add_pmu(parse_state, *listp, pmu, const_parsed_terms,
					 first_wildcard_match,
					 /*alternate_hw_config=*/PERF_COUNT_HW_MAX))
		return 0;

	if (parse_state->fake_pmu) {
		if (!parse_events_add_pmu(parse_state, *listp, perf_pmus__fake_pmu(),
					  const_parsed_terms,
					  first_wildcard_match,
					  /*alternate_hw_config=*/PERF_COUNT_HW_MAX))
			return 0;
	}

	pmu = NULL;
	/* Failed to add, try wildcard expansion of event_or_pmu as a PMU name. */
	while ((pmu = perf_pmus__scan_matching_wildcard(pmu, event_or_pmu)) != NULL) {

		if (parse_events__filter_pmu(parse_state, pmu))
			continue;

		if (!parse_events_add_pmu(parse_state, *listp, pmu,
					  const_parsed_terms,
					  first_wildcard_match,
					  /*alternate_hw_config=*/PERF_COUNT_HW_MAX)) {
			ok++;
			parse_state->wild_card_pmus = true;
		}
		if (first_wildcard_match == NULL) {
			first_wildcard_match =
				container_of((*listp)->prev, struct evsel, core.node);
		}
	}
	if (ok)
		return 0;

	/* Failure to add, assume event_or_pmu is an event name. */
	zfree(listp);
	if (!parse_events_multi_pmu_add(parse_state, event_or_pmu, PERF_COUNT_HW_MAX,
					const_parsed_terms, listp, loc))
		return 0;

	if (asprintf(&help, "Unable to find PMU or event on a PMU of '%s'", event_or_pmu) < 0)
		help = NULL;
	parse_events_error__handle(parse_state->error, loc->first_column,
				strdup("Bad event or PMU"),
				help);
	zfree(listp);
	return -EINVAL;
}

void parse_events__set_leader(char *name, struct list_head *list)
{
	struct evsel *leader;

	if (list_empty(list)) {
		WARN_ONCE(true, "WARNING: failed to set leader: empty list");
		return;
	}

	leader = list_first_entry(list, struct evsel, core.node);
	__perf_evlist__set_leader(list, &leader->core);
	zfree(&leader->group_name);
	leader->group_name = name;
}

static int parse_events__modifier_list(struct parse_events_state *parse_state,
				       YYLTYPE *loc,
				       struct list_head *list,
				       struct parse_events_modifier mod,
				       bool group)
{
	struct evsel *evsel;

	if (!group && mod.weak) {
		parse_events_error__handle(parse_state->error, loc->first_column,
					   strdup("Weak modifier is for use with groups"), NULL);
		return -EINVAL;
	}

	__evlist__for_each_entry(list, evsel) {
		/* Translate modifiers into the equivalent evsel excludes. */
		int eu = group ? evsel->core.attr.exclude_user : 0;
		int ek = group ? evsel->core.attr.exclude_kernel : 0;
		int eh = group ? evsel->core.attr.exclude_hv : 0;
		int eH = group ? evsel->core.attr.exclude_host : 0;
		int eG = group ? evsel->core.attr.exclude_guest : 0;
		int exclude = eu | ek | eh;
		int exclude_GH = eG | eH;

		if (mod.user) {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			eu = 0;
		}
		if (mod.kernel) {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			ek = 0;
		}
		if (mod.hypervisor) {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			eh = 0;
		}
		if (mod.guest) {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eG = 0;
		}
		if (mod.host) {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eH = 0;
		}
		if (!exclude_GH && exclude_GH_default) {
			if (perf_host)
				eG = 1;
			else if (perf_guest)
				eH = 1;
		}

		evsel->core.attr.exclude_user   = eu;
		evsel->core.attr.exclude_kernel = ek;
		evsel->core.attr.exclude_hv     = eh;
		evsel->core.attr.exclude_host   = eH;
		evsel->core.attr.exclude_guest  = eG;
		evsel->exclude_GH               = exclude_GH;

		/* Simple modifiers copied to the evsel. */
		if (mod.precise) {
			u8 precise = evsel->core.attr.precise_ip + mod.precise;
			/*
			 * precise ip:
			 *
			 *  0 - SAMPLE_IP can have arbitrary skid
			 *  1 - SAMPLE_IP must have constant skid
			 *  2 - SAMPLE_IP requested to have 0 skid
			 *  3 - SAMPLE_IP must have 0 skid
			 *
			 *  See also PERF_RECORD_MISC_EXACT_IP
			 */
			if (precise > 3) {
				char *help;

				if (asprintf(&help,
					     "Maximum combined precise value is 3, adding precision to \"%s\"",
					     evsel__name(evsel)) > 0) {
					parse_events_error__handle(parse_state->error,
								   loc->first_column,
								   help, NULL);
				}
				return -EINVAL;
			}
			evsel->core.attr.precise_ip = precise;
		}
		if (mod.precise_max)
			evsel->precise_max = 1;
		if (mod.non_idle)
			evsel->core.attr.exclude_idle = 1;
		if (mod.sample_read)
			evsel->sample_read = 1;
		if (mod.pinned && evsel__is_group_leader(evsel))
			evsel->core.attr.pinned = 1;
		if (mod.exclusive && evsel__is_group_leader(evsel))
			evsel->core.attr.exclusive = 1;
		if (mod.weak)
			evsel->weak_group = true;
		if (mod.bpf)
			evsel->bpf_counter = true;
		if (mod.retire_lat)
			evsel->retire_lat = true;
		if (mod.dont_regroup)
			evsel->dont_regroup = true;
	}
	return 0;
}

int parse_events__modifier_group(struct parse_events_state *parse_state, void *loc,
				 struct list_head *list,
				 struct parse_events_modifier mod)
{
	return parse_events__modifier_list(parse_state, loc, list, mod, /*group=*/true);
}

int parse_events__modifier_event(struct parse_events_state *parse_state, void *loc,
				 struct list_head *list,
				 struct parse_events_modifier mod)
{
	return parse_events__modifier_list(parse_state, loc, list, mod, /*group=*/false);
}

int parse_events__set_default_name(struct list_head *list, char *name)
{
	struct evsel *evsel;
	bool used_name = false;

	__evlist__for_each_entry(list, evsel) {
		if (!evsel->name) {
			evsel->name = used_name ? strdup(name) : name;
			used_name = true;
			if (!evsel->name)
				return -ENOMEM;
		}
	}
	if (!used_name)
		free(name);
	return 0;
}

static int parse_events__scanner(const char *str,
				 FILE *input,
				 struct parse_events_state *parse_state)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	ret = parse_events_lex_init_extra(parse_state, &scanner);
	if (ret)
		return ret;

	if (str)
		buffer = parse_events__scan_string(str, scanner);
	else
	        parse_events_set_in(input, scanner);

#ifdef PARSER_DEBUG
	parse_events_debug = 1;
	parse_events_set_debug(1, scanner);
#endif
	ret = parse_events_parse(parse_state, scanner);

	if (str) {
		parse_events__flush_buffer(buffer, scanner);
		parse_events__delete_buffer(buffer, scanner);
	}
	parse_events_lex_destroy(scanner);
	return ret;
}

/*
 * parse event config string, return a list of event terms.
 */
int parse_events_terms(struct parse_events_terms *terms, const char *str, FILE *input)
{
	struct parse_events_state parse_state = {
		.terms  = NULL,
		.stoken = PE_START_TERMS,
	};
	int ret;

	ret = parse_events__scanner(str, input, &parse_state);
	if (!ret)
		list_splice(&parse_state.terms->terms, &terms->terms);

	zfree(&parse_state.terms);
	return ret;
}

static int evsel__compute_group_pmu_name(struct evsel *evsel,
					  const struct list_head *head)
{
	struct evsel *leader = evsel__leader(evsel);
	struct evsel *pos;
	const char *group_pmu_name;
	struct perf_pmu *pmu = evsel__find_pmu(evsel);

	if (!pmu) {
		/*
		 * For PERF_TYPE_HARDWARE and PERF_TYPE_HW_CACHE types the PMU
		 * is a core PMU, but in heterogeneous systems this is
		 * unknown. For now pick the first core PMU.
		 */
		pmu = perf_pmus__scan_core(NULL);
	}
	if (!pmu) {
		pr_debug("No PMU found for '%s'\n", evsel__name(evsel));
		return -EINVAL;
	}
	group_pmu_name = pmu->name;
	/*
	 * Software events may be in a group with other uncore PMU events. Use
	 * the pmu_name of the first non-software event to avoid breaking the
	 * software event out of the group.
	 *
	 * Aux event leaders, like intel_pt, expect a group with events from
	 * other PMUs, so substitute the AUX event's PMU in this case.
	 */
	if (perf_pmu__is_software(pmu) || evsel__is_aux_event(leader)) {
		struct perf_pmu *leader_pmu = evsel__find_pmu(leader);

		if (!leader_pmu) {
			/* As with determining pmu above. */
			leader_pmu = perf_pmus__scan_core(NULL);
		}
		/*
		 * Starting with the leader, find the first event with a named
		 * non-software PMU. for_each_group_(member|evsel) isn't used as
		 * the list isn't yet sorted putting evsel's in the same group
		 * together.
		 */
		if (leader_pmu && !perf_pmu__is_software(leader_pmu)) {
			group_pmu_name = leader_pmu->name;
		} else if (leader->core.nr_members > 1) {
			list_for_each_entry(pos, head, core.node) {
				struct perf_pmu *pos_pmu;

				if (pos == leader || evsel__leader(pos) != leader)
					continue;
				pos_pmu = evsel__find_pmu(pos);
				if (!pos_pmu) {
					/* As with determining pmu above. */
					pos_pmu = perf_pmus__scan_core(NULL);
				}
				if (pos_pmu && !perf_pmu__is_software(pos_pmu)) {
					group_pmu_name = pos_pmu->name;
					break;
				}
			}
		}
	}
	/* Record computed name. */
	evsel->group_pmu_name = strdup(group_pmu_name);
	return evsel->group_pmu_name ? 0 : -ENOMEM;
}

__weak int arch_evlist__cmp(const struct evsel *lhs, const struct evsel *rhs)
{
	/* Order by insertion index. */
	return lhs->core.idx - rhs->core.idx;
}

static int evlist__cmp(void *_fg_idx, const struct list_head *l, const struct list_head *r)
{
	const struct perf_evsel *lhs_core = container_of(l, struct perf_evsel, node);
	const struct evsel *lhs = container_of(lhs_core, struct evsel, core);
	const struct perf_evsel *rhs_core = container_of(r, struct perf_evsel, node);
	const struct evsel *rhs = container_of(rhs_core, struct evsel, core);
	int *force_grouped_idx = _fg_idx;
	int lhs_sort_idx, rhs_sort_idx, ret;
	const char *lhs_pmu_name, *rhs_pmu_name;

	/*
	 * Get the indexes of the 2 events to sort. If the events are
	 * in groups then the leader's index is used otherwise the
	 * event's index is used. An index may be forced for events that
	 * must be in the same group, namely Intel topdown events.
	 */
	if (*force_grouped_idx != -1 && arch_evsel__must_be_in_group(lhs)) {
		lhs_sort_idx = *force_grouped_idx;
	} else {
		bool lhs_has_group = lhs_core->leader != lhs_core || lhs_core->nr_members > 1;

		lhs_sort_idx = lhs_has_group ? lhs_core->leader->idx : lhs_core->idx;
	}
	if (*force_grouped_idx != -1 && arch_evsel__must_be_in_group(rhs)) {
		rhs_sort_idx = *force_grouped_idx;
	} else {
		bool rhs_has_group = rhs_core->leader != rhs_core || rhs_core->nr_members > 1;

		rhs_sort_idx = rhs_has_group ? rhs_core->leader->idx : rhs_core->idx;
	}

	/* If the indices differ then respect the insertion order. */
	if (lhs_sort_idx != rhs_sort_idx)
		return lhs_sort_idx - rhs_sort_idx;

	/*
	 * Ignoring forcing, lhs_sort_idx == rhs_sort_idx so lhs and rhs should
	 * be in the same group. Events in the same group need to be ordered by
	 * their grouping PMU name as the group will be broken to ensure only
	 * events on the same PMU are programmed together.
	 *
	 * With forcing the lhs_sort_idx == rhs_sort_idx shows that one or both
	 * events are being forced to be at force_group_index. If only one event
	 * is being forced then the other event is the group leader of the group
	 * we're trying to force the event into. Ensure for the force grouped
	 * case that the PMU name ordering is also respected.
	 */
	lhs_pmu_name = lhs->group_pmu_name;
	rhs_pmu_name = rhs->group_pmu_name;
	ret = strcmp(lhs_pmu_name, rhs_pmu_name);
	if (ret)
		return ret;

	/*
	 * Architecture specific sorting, by default sort events in the same
	 * group with the same PMU by their insertion index. On Intel topdown
	 * constraints must be adhered to - slots first, etc.
	 */
	return arch_evlist__cmp(lhs, rhs);
}

int __weak arch_evlist__add_required_events(struct list_head *list __always_unused)
{
	return 0;
}

static int parse_events__sort_events_and_fix_groups(struct list_head *list)
{
	int idx = 0, force_grouped_idx = -1;
	struct evsel *pos, *cur_leader = NULL;
	struct perf_evsel *cur_leaders_grp = NULL;
	bool idx_changed = false;
	int orig_num_leaders = 0, num_leaders = 0;
	int ret;
	struct evsel *force_grouped_leader = NULL;
	bool last_event_was_forced_leader = false;

	/* On x86 topdown metrics events require a slots event. */
	ret = arch_evlist__add_required_events(list);
	if (ret)
		return ret;

	/*
	 * Compute index to insert ungrouped events at. Place them where the
	 * first ungrouped event appears.
	 */
	list_for_each_entry(pos, list, core.node) {
		const struct evsel *pos_leader = evsel__leader(pos);

		ret = evsel__compute_group_pmu_name(pos, list);
		if (ret)
			return ret;

		if (pos == pos_leader)
			orig_num_leaders++;

		/*
		 * Ensure indexes are sequential, in particular for multiple
		 * event lists being merged. The indexes are used to detect when
		 * the user order is modified.
		 */
		pos->core.idx = idx++;

		/*
		 * Remember an index to sort all forced grouped events
		 * together to. Use the group leader as some events
		 * must appear first within the group.
		 */
		if (force_grouped_idx == -1 && arch_evsel__must_be_in_group(pos))
			force_grouped_idx = pos_leader->core.idx;
	}

	/* Sort events. */
	list_sort(&force_grouped_idx, list, evlist__cmp);

	/*
	 * Recompute groups, splitting for PMUs and adding groups for events
	 * that require them.
	 */
	idx = 0;
	list_for_each_entry(pos, list, core.node) {
		const struct evsel *pos_leader = evsel__leader(pos);
		const char *pos_pmu_name = pos->group_pmu_name;
		const char *cur_leader_pmu_name;
		bool pos_force_grouped = force_grouped_idx != -1 &&
			arch_evsel__must_be_in_group(pos);

		/* Reset index and nr_members. */
		if (pos->core.idx != idx)
			idx_changed = true;
		pos->core.idx = idx++;
		pos->core.nr_members = 0;

		/*
		 * Set the group leader respecting the given groupings and that
		 * groups can't span PMUs.
		 */
		if (!cur_leader || pos->dont_regroup) {
			cur_leader = pos;
			cur_leaders_grp = &pos->core;
			if (pos_force_grouped)
				force_grouped_leader = pos;
		}
		cur_leader_pmu_name = cur_leader->group_pmu_name;
		if (strcmp(cur_leader_pmu_name, pos_pmu_name)) {
			/* PMU changed so the group/leader must change. */
			cur_leader = pos;
			cur_leaders_grp = pos->core.leader;
			if (pos_force_grouped && force_grouped_leader == NULL)
				force_grouped_leader = pos;
		} else if (cur_leaders_grp != pos->core.leader) {
			bool split_even_if_last_leader_was_forced = true;

			/*
			 * Event is for a different group. If the last event was
			 * the forced group leader then subsequent group events
			 * and forced events should be in the same group. If
			 * there are no other forced group events then the
			 * forced group leader wasn't really being forced into a
			 * group, it just set arch_evsel__must_be_in_group, and
			 * we don't want the group to split here.
			 */
			if (force_grouped_idx != -1 && last_event_was_forced_leader) {
				struct evsel *pos2 = pos;
				/*
				 * Search the whole list as the group leaders
				 * aren't currently valid.
				 */
				list_for_each_entry_continue(pos2, list, core.node) {
					if (pos->core.leader == pos2->core.leader &&
					    arch_evsel__must_be_in_group(pos2)) {
						split_even_if_last_leader_was_forced = false;
						break;
					}
				}
			}
			if (!last_event_was_forced_leader || split_even_if_last_leader_was_forced) {
				if (pos_force_grouped) {
					if (force_grouped_leader) {
						cur_leader = force_grouped_leader;
						cur_leaders_grp = force_grouped_leader->core.leader;
					} else {
						cur_leader = force_grouped_leader = pos;
						cur_leaders_grp = &pos->core;
					}
				} else {
					cur_leader = pos;
					cur_leaders_grp = pos->core.leader;
				}
			}
		}
		if (pos_leader != cur_leader) {
			/* The leader changed so update it. */
			evsel__set_leader(pos, cur_leader);
		}
		last_event_was_forced_leader = (force_grouped_leader == pos);
	}
	list_for_each_entry(pos, list, core.node) {
		struct evsel *pos_leader = evsel__leader(pos);

		if (pos == pos_leader)
			num_leaders++;
		pos_leader->core.nr_members++;
	}
	return (idx_changed || num_leaders != orig_num_leaders) ? 1 : 0;
}

int __parse_events(struct evlist *evlist, const char *str, const char *pmu_filter,
		   struct parse_events_error *err, bool fake_pmu,
		   bool warn_if_reordered, bool fake_tp)
{
	struct parse_events_state parse_state = {
		.list	  = LIST_HEAD_INIT(parse_state.list),
		.idx	  = evlist->core.nr_entries,
		.error	  = err,
		.stoken	  = PE_START_EVENTS,
		.fake_pmu = fake_pmu,
		.fake_tp  = fake_tp,
		.pmu_filter = pmu_filter,
		.match_legacy_cache_terms = true,
	};
	int ret, ret2;

	ret = parse_events__scanner(str, /*input=*/ NULL, &parse_state);

	if (!ret && list_empty(&parse_state.list)) {
		WARN_ONCE(true, "WARNING: event parser found nothing\n");
		return -1;
	}

	ret2 = parse_events__sort_events_and_fix_groups(&parse_state.list);
	if (ret2 < 0)
		return ret;

	/*
	 * Add list to the evlist even with errors to allow callers to clean up.
	 */
	evlist__splice_list_tail(evlist, &parse_state.list);

	if (ret2 && warn_if_reordered && !parse_state.wild_card_pmus) {
		pr_warning("WARNING: events were regrouped to match PMUs\n");

		if (verbose > 0) {
			struct strbuf sb = STRBUF_INIT;

			evlist__uniquify_evsel_names(evlist, &stat_config);
			evlist__format_evsels(evlist, &sb, 2048);
			pr_debug("evlist after sorting/fixing: '%s'\n", sb.buf);
			strbuf_release(&sb);
		}
	}
	if (!ret) {
		struct evsel *last;

		last = evlist__last(evlist);
		last->cmdline_group_boundary = true;

		return 0;
	}

	/*
	 * There are 2 users - builtin-record and builtin-test objects.
	 * Both call evlist__delete in case of error, so we dont
	 * need to bother.
	 */
	return ret;
}

int parse_event(struct evlist *evlist, const char *str)
{
	struct parse_events_error err;
	int ret;

	parse_events_error__init(&err);
	ret = parse_events(evlist, str, &err);
	parse_events_error__exit(&err);
	return ret;
}

struct parse_events_error_entry {
	/** @list: The list the error is part of. */
	struct list_head list;
	/** @idx: index in the parsed string */
	int   idx;
	/** @str: string to display at the index */
	char *str;
	/** @help: optional help string */
	char *help;
};

void parse_events_error__init(struct parse_events_error *err)
{
	INIT_LIST_HEAD(&err->list);
}

void parse_events_error__exit(struct parse_events_error *err)
{
	struct parse_events_error_entry *pos, *tmp;

	list_for_each_entry_safe(pos, tmp, &err->list, list) {
		zfree(&pos->str);
		zfree(&pos->help);
		list_del_init(&pos->list);
		free(pos);
	}
}

void parse_events_error__handle(struct parse_events_error *err, int idx,
				char *str, char *help)
{
	struct parse_events_error_entry *entry;

	if (WARN(!str || !err, "WARNING: failed to provide error string or struct\n"))
		goto out_free;

	entry = zalloc(sizeof(*entry));
	if (!entry) {
		pr_err("Failed to allocate memory for event parsing error: %s (%s)\n",
			str, help ?: "<no help>");
		goto out_free;
	}
	entry->idx = idx;
	entry->str = str;
	entry->help = help;
	list_add(&entry->list, &err->list);
	return;
out_free:
	free(str);
	free(help);
}

#define MAX_WIDTH 1000
static int get_term_width(void)
{
	struct winsize ws;

	get_term_dimensions(&ws);
	return ws.ws_col > MAX_WIDTH ? MAX_WIDTH : ws.ws_col;
}

static void __parse_events_error__print(int err_idx, const char *err_str,
					const char *err_help, const char *event)
{
	const char *str = "invalid or unsupported event: ";
	char _buf[MAX_WIDTH];
	char *buf = (char *) event;
	int idx = 0;
	if (err_str) {
		/* -2 for extra '' in the final fprintf */
		int width       = get_term_width() - 2;
		int len_event   = strlen(event);
		int len_str, max_len, cut = 0;

		/*
		 * Maximum error index indent, we will cut
		 * the event string if it's bigger.
		 */
		int max_err_idx = 13;

		/*
		 * Let's be specific with the message when
		 * we have the precise error.
		 */
		str     = "event syntax error: ";
		len_str = strlen(str);
		max_len = width - len_str;

		buf = _buf;

		/* We're cutting from the beginning. */
		if (err_idx > max_err_idx)
			cut = err_idx - max_err_idx;

		strncpy(buf, event + cut, max_len);

		/* Mark cut parts with '..' on both sides. */
		if (cut)
			buf[0] = buf[1] = '.';

		if ((len_event - cut) > max_len) {
			buf[max_len - 1] = buf[max_len - 2] = '.';
			buf[max_len] = 0;
		}

		idx = len_str + err_idx - cut;
	}

	fprintf(stderr, "%s'%s'\n", str, buf);
	if (idx) {
		fprintf(stderr, "%*s\\___ %s\n", idx + 1, "", err_str);
		if (err_help)
			fprintf(stderr, "\n%s\n", err_help);
	}
}

void parse_events_error__print(const struct parse_events_error *err,
			       const char *event)
{
	struct parse_events_error_entry *pos;
	bool first = true;

	list_for_each_entry(pos, &err->list, list) {
		if (!first)
			fputs("\n", stderr);
		__parse_events_error__print(pos->idx, pos->str, pos->help, event);
		first = false;
	}
}

/*
 * In the list of errors err, do any of the error strings (str) contain the
 * given needle string?
 */
bool parse_events_error__contains(const struct parse_events_error *err,
				  const char *needle)
{
	struct parse_events_error_entry *pos;

	list_for_each_entry(pos, &err->list, list) {
		if (strstr(pos->str, needle) != NULL)
			return true;
	}
	return false;
}

#undef MAX_WIDTH

int parse_events_option(const struct option *opt, const char *str,
			int unset __maybe_unused)
{
	struct parse_events_option_args *args = opt->value;
	struct parse_events_error err;
	int ret;

	parse_events_error__init(&err);
	ret = __parse_events(*args->evlistp, str, args->pmu_filter, &err,
			     /*fake_pmu=*/false, /*warn_if_reordered=*/true,
			     /*fake_tp=*/false);

	if (ret) {
		parse_events_error__print(&err, str);
		fprintf(stderr, "Run 'perf list' for a list of valid events\n");
	}
	parse_events_error__exit(&err);

	return ret;
}

int parse_events_option_new_evlist(const struct option *opt, const char *str, int unset)
{
	struct parse_events_option_args *args = opt->value;
	int ret;

	if (*args->evlistp == NULL) {
		*args->evlistp = evlist__new();

		if (*args->evlistp == NULL) {
			fprintf(stderr, "Not enough memory to create evlist\n");
			return -1;
		}
	}
	ret = parse_events_option(opt, str, unset);
	if (ret) {
		evlist__delete(*args->evlistp);
		*args->evlistp = NULL;
	}

	return ret;
}

static int
foreach_evsel_in_last_glob(struct evlist *evlist,
			   int (*func)(struct evsel *evsel,
				       const void *arg),
			   const void *arg)
{
	struct evsel *last = NULL;
	int err;

	/*
	 * Don't return when list_empty, give func a chance to report
	 * error when it found last == NULL.
	 *
	 * So no need to WARN here, let *func do this.
	 */
	if (evlist->core.nr_entries > 0)
		last = evlist__last(evlist);

	do {
		err = (*func)(last, arg);
		if (err)
			return -1;
		if (!last)
			return 0;

		if (last->core.node.prev == &evlist->core.entries)
			return 0;
		last = list_entry(last->core.node.prev, struct evsel, core.node);
	} while (!last->cmdline_group_boundary);

	return 0;
}

/* Will a tracepoint filter work for str or should a BPF filter be used? */
static bool is_possible_tp_filter(const char *str)
{
	return strstr(str, "uid") == NULL;
}

static int set_filter(struct evsel *evsel, const void *arg)
{
	const char *str = arg;
	int nr_addr_filters = 0;
	struct perf_pmu *pmu;

	if (evsel == NULL) {
		fprintf(stderr,
			"--filter option should follow a -e tracepoint or HW tracer option\n");
		return -1;
	}

	if (evsel->core.attr.type == PERF_TYPE_TRACEPOINT && is_possible_tp_filter(str)) {
		if (evsel__append_tp_filter(evsel, str) < 0) {
			fprintf(stderr,
				"not enough memory to hold filter string\n");
			return -1;
		}

		return 0;
	}

	pmu = evsel__find_pmu(evsel);
	if (pmu) {
		perf_pmu__scan_file(pmu, "nr_addr_filters",
				    "%d", &nr_addr_filters);
	}
	if (!nr_addr_filters)
		return perf_bpf_filter__parse(&evsel->bpf_filters, str);

	if (evsel__append_addr_filter(evsel, str) < 0) {
		fprintf(stderr,
			"not enough memory to hold filter string\n");
		return -1;
	}

	return 0;
}

int parse_filter(const struct option *opt, const char *str,
		 int unset __maybe_unused)
{
	struct evlist *evlist = *(struct evlist **)opt->value;

	return foreach_evsel_in_last_glob(evlist, set_filter,
					  (const void *)str);
}

int parse_uid_filter(struct evlist *evlist, uid_t uid)
{
	struct option opt = {
		.value = &evlist,
	};
	char buf[128];
	int ret;

	snprintf(buf, sizeof(buf), "uid == %d", uid);
	ret = parse_filter(&opt, buf, /*unset=*/0);
	if (ret) {
		if (use_browser >= 1) {
			/*
			 * Use ui__warning so a pop up appears above the
			 * underlying BPF error message.
			 */
			ui__warning("Failed to add UID filtering that uses BPF filtering.\n");
		} else {
			fprintf(stderr, "Failed to add UID filtering that uses BPF filtering.\n");
		}
	}
	return ret;
}

static int add_exclude_perf_filter(struct evsel *evsel,
				   const void *arg __maybe_unused)
{
	char new_filter[64];

	if (evsel == NULL || evsel->core.attr.type != PERF_TYPE_TRACEPOINT) {
		fprintf(stderr,
			"--exclude-perf option should follow a -e tracepoint option\n");
		return -1;
	}

	snprintf(new_filter, sizeof(new_filter), "common_pid != %d", getpid());

	if (evsel__append_tp_filter(evsel, new_filter) < 0) {
		fprintf(stderr,
			"not enough memory to hold filter string\n");
		return -1;
	}

	return 0;
}

int exclude_perf(const struct option *opt,
		 const char *arg __maybe_unused,
		 int unset __maybe_unused)
{
	struct evlist *evlist = *(struct evlist **)opt->value;

	return foreach_evsel_in_last_glob(evlist, add_exclude_perf_filter,
					  NULL);
}

int parse_events__is_hardcoded_term(struct parse_events_term *term)
{
	return term->type_term != PARSE_EVENTS__TERM_TYPE_USER;
}

static int new_term(struct parse_events_term **_term,
		    struct parse_events_term *temp,
		    char *str, u64 num)
{
	struct parse_events_term *term;

	term = malloc(sizeof(*term));
	if (!term)
		return -ENOMEM;

	*term = *temp;
	INIT_LIST_HEAD(&term->list);
	term->weak = false;

	switch (term->type_val) {
	case PARSE_EVENTS__TERM_TYPE_NUM:
		term->val.num = num;
		break;
	case PARSE_EVENTS__TERM_TYPE_STR:
		term->val.str = str;
		break;
	default:
		free(term);
		return -EINVAL;
	}

	*_term = term;
	return 0;
}

int parse_events_term__num(struct parse_events_term **term,
			   enum parse_events__term_type type_term,
			   const char *config, u64 num,
			   bool no_value,
			   void *loc_term_, void *loc_val_)
{
	YYLTYPE *loc_term = loc_term_;
	YYLTYPE *loc_val = loc_val_;

	struct parse_events_term temp = {
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = type_term,
		.config    = config ? : strdup(parse_events__term_type_str(type_term)),
		.no_value  = no_value,
		.err_term  = loc_term ? loc_term->first_column : 0,
		.err_val   = loc_val  ? loc_val->first_column  : 0,
	};

	return new_term(term, &temp, /*str=*/NULL, num);
}

int parse_events_term__str(struct parse_events_term **term,
			   enum parse_events__term_type type_term,
			   char *config, char *str,
			   void *loc_term_, void *loc_val_)
{
	YYLTYPE *loc_term = loc_term_;
	YYLTYPE *loc_val = loc_val_;

	struct parse_events_term temp = {
		.type_val  = PARSE_EVENTS__TERM_TYPE_STR,
		.type_term = type_term,
		.config    = config,
		.err_term  = loc_term ? loc_term->first_column : 0,
		.err_val   = loc_val  ? loc_val->first_column  : 0,
	};

	return new_term(term, &temp, str, /*num=*/0);
}

int parse_events_term__term(struct parse_events_term **term,
			    enum parse_events__term_type term_lhs,
			    enum parse_events__term_type term_rhs,
			    void *loc_term, void *loc_val)
{
	return parse_events_term__str(term, term_lhs, NULL,
				      strdup(parse_events__term_type_str(term_rhs)),
				      loc_term, loc_val);
}

int parse_events_term__clone(struct parse_events_term **new,
			     const struct parse_events_term *term)
{
	char *str;
	struct parse_events_term temp = *term;

	temp.used = false;
	if (term->config) {
		temp.config = strdup(term->config);
		if (!temp.config)
			return -ENOMEM;
	}
	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM)
		return new_term(new, &temp, /*str=*/NULL, term->val.num);

	str = strdup(term->val.str);
	if (!str) {
		zfree(&temp.config);
		return -ENOMEM;
	}
	return new_term(new, &temp, str, /*num=*/0);
}

void parse_events_term__delete(struct parse_events_term *term)
{
	if (term->type_val != PARSE_EVENTS__TERM_TYPE_NUM)
		zfree(&term->val.str);

	zfree(&term->config);
	free(term);
}

static int parse_events_terms__copy(const struct parse_events_terms *src,
				    struct parse_events_terms *dest)
{
	struct parse_events_term *term;

	list_for_each_entry (term, &src->terms, list) {
		struct parse_events_term *n;
		int ret;

		ret = parse_events_term__clone(&n, term);
		if (ret)
			return ret;

		list_add_tail(&n->list, &dest->terms);
	}
	return 0;
}

void parse_events_terms__init(struct parse_events_terms *terms)
{
	INIT_LIST_HEAD(&terms->terms);
}

void parse_events_terms__exit(struct parse_events_terms *terms)
{
	struct parse_events_term *term, *h;

	list_for_each_entry_safe(term, h, &terms->terms, list) {
		list_del_init(&term->list);
		parse_events_term__delete(term);
	}
}

void parse_events_terms__delete(struct parse_events_terms *terms)
{
	if (!terms)
		return;
	parse_events_terms__exit(terms);
	free(terms);
}

int parse_events_terms__to_strbuf(const struct parse_events_terms *terms, struct strbuf *sb)
{
	struct parse_events_term *term;
	bool first = true;

	if (!terms)
		return 0;

	list_for_each_entry(term, &terms->terms, list) {
		int ret;

		if (!first) {
			ret = strbuf_addch(sb, ',');
			if (ret < 0)
				return ret;
		}
		first = false;

		if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM)
			if (term->no_value) {
				assert(term->val.num == 1);
				ret = strbuf_addf(sb, "%s", term->config);
			} else
				ret = strbuf_addf(sb, "%s=%#"PRIx64, term->config, term->val.num);
		else if (term->type_val == PARSE_EVENTS__TERM_TYPE_STR) {
			if (term->config) {
				ret = strbuf_addf(sb, "%s=", term->config);
				if (ret < 0)
					return ret;
			} else if ((unsigned int)term->type_term < __PARSE_EVENTS__TERM_TYPE_NR) {
				ret = strbuf_addf(sb, "%s=",
						  parse_events__term_type_str(term->type_term));
				if (ret < 0)
					return ret;
			}
			assert(!term->no_value);
			ret = strbuf_addf(sb, "%s", term->val.str);
		}
		if (ret < 0)
			return ret;
	}
	return 0;
}

static void config_terms_list(char *buf, size_t buf_sz)
{
	int i;
	bool first = true;

	buf[0] = '\0';
	for (i = 0; i < __PARSE_EVENTS__TERM_TYPE_NR; i++) {
		const char *name = parse_events__term_type_str(i);

		if (!config_term_avail(i, NULL))
			continue;
		if (!name)
			continue;
		if (name[0] == '<')
			continue;

		if (strlen(buf) + strlen(name) + 2 >= buf_sz)
			return;

		if (!first)
			strcat(buf, ",");
		else
			first = false;
		strcat(buf, name);
	}
}

/*
 * Return string contains valid config terms of an event.
 * @additional_terms: For terms such as PMU sysfs terms.
 */
char *parse_events_formats_error_string(char *additional_terms)
{
	char *str;
	/* "no-overwrite" is the longest name */
	char static_terms[__PARSE_EVENTS__TERM_TYPE_NR *
			  (sizeof("no-overwrite") - 1)];

	config_terms_list(static_terms, sizeof(static_terms));
	/* valid terms */
	if (additional_terms) {
		if (asprintf(&str, "valid terms: %s,%s",
			     additional_terms, static_terms) < 0)
			goto fail;
	} else {
		if (asprintf(&str, "valid terms: %s", static_terms) < 0)
			goto fail;
	}
	return str;

fail:
	return NULL;
}
