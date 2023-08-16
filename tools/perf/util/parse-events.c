// SPDX-License-Identifier: GPL-2.0
#include <linux/hw_breakpoint.h>
#include <linux/err.h>
#include <linux/list_sort.h>
#include <linux/zalloc.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include "term.h"
#include "evlist.h"
#include "evsel.h"
#include <subcmd/parse-options.h>
#include "parse-events.h"
#include "string2.h"
#include "strlist.h"
#include "bpf-loader.h"
#include "debug.h"
#include <api/fs/tracing_path.h>
#include <perf/cpumap.h>
#include "parse-events-bison.h"
#include "parse-events-flex.h"
#include "pmu.h"
#include "pmus.h"
#include "asm/bug.h"
#include "util/parse-branch-options.h"
#include "util/evsel_config.h"
#include "util/event.h"
#include "util/bpf-filter.h"
#include "util/util.h"
#include "tracepoint.h"

#define MAX_NAME_LEN 100

#ifdef PARSER_DEBUG
extern int parse_events_debug;
#endif
int parse_events_parse(void *parse_state, void *scanner);
static int get_config_terms(struct list_head *head_config,
			    struct list_head *head_terms __maybe_unused);

struct event_symbol event_symbols_hw[PERF_COUNT_HW_MAX] = {
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

struct event_symbol event_symbols_sw[PERF_COUNT_SW_MAX] = {
	[PERF_COUNT_SW_CPU_CLOCK] = {
		.symbol = "cpu-clock",
		.alias  = "",
	},
	[PERF_COUNT_SW_TASK_CLOCK] = {
		.symbol = "task-clock",
		.alias  = "",
	},
	[PERF_COUNT_SW_PAGE_FAULTS] = {
		.symbol = "page-faults",
		.alias  = "faults",
	},
	[PERF_COUNT_SW_CONTEXT_SWITCHES] = {
		.symbol = "context-switches",
		.alias  = "cs",
	},
	[PERF_COUNT_SW_CPU_MIGRATIONS] = {
		.symbol = "cpu-migrations",
		.alias  = "migrations",
	},
	[PERF_COUNT_SW_PAGE_FAULTS_MIN] = {
		.symbol = "minor-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_PAGE_FAULTS_MAJ] = {
		.symbol = "major-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_ALIGNMENT_FAULTS] = {
		.symbol = "alignment-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_EMULATION_FAULTS] = {
		.symbol = "emulation-faults",
		.alias  = "",
	},
	[PERF_COUNT_SW_DUMMY] = {
		.symbol = "dummy",
		.alias  = "",
	},
	[PERF_COUNT_SW_BPF_OUTPUT] = {
		.symbol = "bpf-output",
		.alias  = "",
	},
	[PERF_COUNT_SW_CGROUP_SWITCHES] = {
		.symbol = "cgroup-switches",
		.alias  = "",
	},
};

const char *event_type(int type)
{
	switch (type) {
	case PERF_TYPE_HARDWARE:
		return "hardware";

	case PERF_TYPE_SOFTWARE:
		return "software";

	case PERF_TYPE_TRACEPOINT:
		return "tracepoint";

	case PERF_TYPE_HW_CACHE:
		return "hardware-cache";

	default:
		break;
	}

	return "unknown";
}

static char *get_config_str(struct list_head *head_terms, int type_term)
{
	struct parse_events_term *term;

	if (!head_terms)
		return NULL;

	list_for_each_entry(term, head_terms, list)
		if (term->type_term == type_term)
			return term->val.str;

	return NULL;
}

static char *get_config_metric_id(struct list_head *head_terms)
{
	return get_config_str(head_terms, PARSE_EVENTS__TERM_TYPE_METRIC_ID);
}

static char *get_config_name(struct list_head *head_terms)
{
	return get_config_str(head_terms, PARSE_EVENTS__TERM_TYPE_NAME);
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
static void fix_raw(struct list_head *config_terms, struct perf_pmu *pmu)
{
	struct parse_events_term *term;

	list_for_each_entry(term, config_terms, list) {
		struct perf_pmu_alias *alias;
		bool matched = false;

		if (term->type_term != PARSE_EVENTS__TERM_TYPE_RAW)
			continue;

		list_for_each_entry(alias, &pmu->aliases, list) {
			if (!strcmp(alias->name, term->val.str)) {
				free(term->config);
				term->config = term->val.str;
				term->type_val = PARSE_EVENTS__TERM_TYPE_NUM;
				term->type_term = PARSE_EVENTS__TERM_TYPE_USER;
				term->val.num = 1;
				term->no_value = true;
				matched = true;
				break;
			}
		}
		if (!matched) {
			u64 num;

			free(term->config);
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
}

static struct evsel *
__add_event(struct list_head *list, int *idx,
	    struct perf_event_attr *attr,
	    bool init_attr,
	    const char *name, const char *metric_id, struct perf_pmu *pmu,
	    struct list_head *config_terms, bool auto_merge_stats,
	    const char *cpu_list)
{
	struct evsel *evsel;
	struct perf_cpu_map *cpus = pmu ? perf_cpu_map__get(pmu->cpus) :
			       cpu_list ? perf_cpu_map__new(cpu_list) : NULL;

	if (pmu)
		perf_pmu__warn_invalid_formats(pmu);

	if (pmu && (attr->type == PERF_TYPE_RAW || attr->type >= PERF_TYPE_MAX)) {
		perf_pmu__warn_invalid_config(pmu, attr->config, name,
					      PERF_PMU_FORMAT_VALUE_CONFIG, "config");
		perf_pmu__warn_invalid_config(pmu, attr->config1, name,
					      PERF_PMU_FORMAT_VALUE_CONFIG1, "config1");
		perf_pmu__warn_invalid_config(pmu, attr->config2, name,
					      PERF_PMU_FORMAT_VALUE_CONFIG2, "config2");
		perf_pmu__warn_invalid_config(pmu, attr->config3, name,
					      PERF_PMU_FORMAT_VALUE_CONFIG3, "config3");
	}
	if (init_attr)
		event_attr_init(attr);

	evsel = evsel__new_idx(attr, *idx);
	if (!evsel) {
		perf_cpu_map__put(cpus);
		return NULL;
	}

	(*idx)++;
	evsel->core.cpus = cpus;
	evsel->core.own_cpus = perf_cpu_map__get(cpus);
	evsel->core.requires_cpu = pmu ? pmu->is_uncore : false;
	evsel->core.is_pmu_core = pmu ? pmu->is_core : false;
	evsel->auto_merge_stats = auto_merge_stats;
	evsel->pmu = pmu;
	evsel->pmu_name = pmu && pmu->name ? strdup(pmu->name) : NULL;

	if (name)
		evsel->name = strdup(name);

	if (metric_id)
		evsel->metric_id = strdup(metric_id);

	if (config_terms)
		list_splice_init(config_terms, &evsel->config_terms);

	if (list)
		list_add_tail(&evsel->core.node, list);

	return evsel;
}

struct evsel *parse_events__add_event(int idx, struct perf_event_attr *attr,
				      const char *name, const char *metric_id,
				      struct perf_pmu *pmu)
{
	return __add_event(/*list=*/NULL, &idx, attr, /*init_attr=*/false, name,
			   metric_id, pmu, /*config_terms=*/NULL,
			   /*auto_merge_stats=*/false, /*cpu_list=*/NULL);
}

static int add_event(struct list_head *list, int *idx,
		     struct perf_event_attr *attr, const char *name,
		     const char *metric_id, struct list_head *config_terms)
{
	return __add_event(list, idx, attr, /*init_attr*/true, name, metric_id,
			   /*pmu=*/NULL, config_terms,
			   /*auto_merge_stats=*/false, /*cpu_list=*/NULL) ? 0 : -ENOMEM;
}

static int add_event_tool(struct list_head *list, int *idx,
			  enum perf_tool_event tool_event)
{
	struct evsel *evsel;
	struct perf_event_attr attr = {
		.type = PERF_TYPE_SOFTWARE,
		.config = PERF_COUNT_SW_DUMMY,
	};

	evsel = __add_event(list, idx, &attr, /*init_attr=*/true, /*name=*/NULL,
			    /*metric_id=*/NULL, /*pmu=*/NULL,
			    /*config_terms=*/NULL, /*auto_merge_stats=*/false,
			    /*cpu_list=*/"0");
	if (!evsel)
		return -ENOMEM;
	evsel->tool_event = tool_event;
	if (tool_event == PERF_TOOL_DURATION_TIME
	    || tool_event == PERF_TOOL_USER_TIME
	    || tool_event == PERF_TOOL_SYSTEM_TIME) {
		free((char *)evsel->unit);
		evsel->unit = strdup("ns");
	}
	return 0;
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
			       struct parse_events_error *err);
static int config_term_common(struct perf_event_attr *attr,
			      struct parse_events_term *term,
			      struct parse_events_error *err);
static int config_attr(struct perf_event_attr *attr,
		       struct list_head *head,
		       struct parse_events_error *err,
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

	if (pmu->name == NULL)
		return true;

	return strcmp(parse_state->pmu_filter, pmu->name) != 0;
}

int parse_events_add_cache(struct list_head *list, int *idx, const char *name,
			   struct parse_events_state *parse_state,
			   struct list_head *head_config)
{
	struct perf_pmu *pmu = NULL;
	bool found_supported = false;
	const char *config_name = get_config_name(head_config);
	const char *metric_id = get_config_metric_id(head_config);

	/* Legacy cache events are only supported by core PMUs. */
	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		LIST_HEAD(config_terms);
		struct perf_event_attr attr;
		int ret;

		if (parse_events__filter_pmu(parse_state, pmu))
			continue;

		memset(&attr, 0, sizeof(attr));
		attr.type = PERF_TYPE_HW_CACHE;

		ret = parse_events__decode_legacy_cache(name, pmu->type, &attr.config);
		if (ret)
			return ret;

		found_supported = true;

		if (head_config) {
			if (config_attr(&attr, head_config, parse_state->error, config_term_common))
				return -EINVAL;

			if (get_config_terms(head_config, &config_terms))
				return -ENOMEM;
		}

		if (__add_event(list, idx, &attr, /*init_attr*/true, config_name ?: name,
				metric_id, pmu, &config_terms, /*auto_merge_stats=*/false,
				/*cpu_list=*/NULL) == NULL)
			return -ENOMEM;

		free_config_terms(&config_terms);
	}
	return found_supported ? 0 : -EINVAL;
}

#ifdef HAVE_LIBTRACEEVENT
static void tracepoint_error(struct parse_events_error *e, int err,
			     const char *sys, const char *name)
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
	parse_events_error__handle(e, 0, strdup(str), strdup(help));
}

static int add_tracepoint(struct list_head *list, int *idx,
			  const char *sys_name, const char *evt_name,
			  struct parse_events_error *err,
			  struct list_head *head_config)
{
	struct evsel *evsel = evsel__newtp_idx(sys_name, evt_name, (*idx)++);

	if (IS_ERR(evsel)) {
		tracepoint_error(err, PTR_ERR(evsel), sys_name, evt_name);
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

static int add_tracepoint_multi_event(struct list_head *list, int *idx,
				      const char *sys_name, const char *evt_name,
				      struct parse_events_error *err,
				      struct list_head *head_config)
{
	char *evt_path;
	struct dirent *evt_ent;
	DIR *evt_dir;
	int ret = 0, found = 0;

	evt_path = get_events_file(sys_name);
	if (!evt_path) {
		tracepoint_error(err, errno, sys_name, evt_name);
		return -1;
	}
	evt_dir = opendir(evt_path);
	if (!evt_dir) {
		put_events_file(evt_path);
		tracepoint_error(err, errno, sys_name, evt_name);
		return -1;
	}

	while (!ret && (evt_ent = readdir(evt_dir))) {
		if (!strcmp(evt_ent->d_name, ".")
		    || !strcmp(evt_ent->d_name, "..")
		    || !strcmp(evt_ent->d_name, "enable")
		    || !strcmp(evt_ent->d_name, "filter"))
			continue;

		if (!strglobmatch(evt_ent->d_name, evt_name))
			continue;

		found++;

		ret = add_tracepoint(list, idx, sys_name, evt_ent->d_name,
				     err, head_config);
	}

	if (!found) {
		tracepoint_error(err, ENOENT, sys_name, evt_name);
		ret = -1;
	}

	put_events_file(evt_path);
	closedir(evt_dir);
	return ret;
}

static int add_tracepoint_event(struct list_head *list, int *idx,
				const char *sys_name, const char *evt_name,
				struct parse_events_error *err,
				struct list_head *head_config)
{
	return strpbrk(evt_name, "*?") ?
	       add_tracepoint_multi_event(list, idx, sys_name, evt_name,
					  err, head_config) :
	       add_tracepoint(list, idx, sys_name, evt_name,
			      err, head_config);
}

static int add_tracepoint_multi_sys(struct list_head *list, int *idx,
				    const char *sys_name, const char *evt_name,
				    struct parse_events_error *err,
				    struct list_head *head_config)
{
	struct dirent *events_ent;
	DIR *events_dir;
	int ret = 0;

	events_dir = tracing_events__opendir();
	if (!events_dir) {
		tracepoint_error(err, errno, sys_name, evt_name);
		return -1;
	}

	while (!ret && (events_ent = readdir(events_dir))) {
		if (!strcmp(events_ent->d_name, ".")
		    || !strcmp(events_ent->d_name, "..")
		    || !strcmp(events_ent->d_name, "enable")
		    || !strcmp(events_ent->d_name, "header_event")
		    || !strcmp(events_ent->d_name, "header_page"))
			continue;

		if (!strglobmatch(events_ent->d_name, sys_name))
			continue;

		ret = add_tracepoint_event(list, idx, events_ent->d_name,
					   evt_name, err, head_config);
	}

	closedir(events_dir);
	return ret;
}
#endif /* HAVE_LIBTRACEEVENT */

#ifdef HAVE_LIBBPF_SUPPORT
struct __add_bpf_event_param {
	struct parse_events_state *parse_state;
	struct list_head *list;
	struct list_head *head_config;
};

static int add_bpf_event(const char *group, const char *event, int fd, struct bpf_object *obj,
			 void *_param)
{
	LIST_HEAD(new_evsels);
	struct __add_bpf_event_param *param = _param;
	struct parse_events_state *parse_state = param->parse_state;
	struct list_head *list = param->list;
	struct evsel *pos;
	int err;
	/*
	 * Check if we should add the event, i.e. if it is a TP but starts with a '!',
	 * then don't add the tracepoint, this will be used for something else, like
	 * adding to a BPF_MAP_TYPE_PROG_ARRAY.
	 *
	 * See tools/perf/examples/bpf/augmented_raw_syscalls.c
	 */
	if (group[0] == '!')
		return 0;

	pr_debug("add bpf event %s:%s and attach bpf program %d\n",
		 group, event, fd);

	err = parse_events_add_tracepoint(&new_evsels, &parse_state->idx, group,
					  event, parse_state->error,
					  param->head_config);
	if (err) {
		struct evsel *evsel, *tmp;

		pr_debug("Failed to add BPF event %s:%s\n",
			 group, event);
		list_for_each_entry_safe(evsel, tmp, &new_evsels, core.node) {
			list_del_init(&evsel->core.node);
			evsel__delete(evsel);
		}
		return err;
	}
	pr_debug("adding %s:%s\n", group, event);

	list_for_each_entry(pos, &new_evsels, core.node) {
		pr_debug("adding %s:%s to %p\n",
			 group, event, pos);
		pos->bpf_fd = fd;
		pos->bpf_obj = obj;
	}
	list_splice(&new_evsels, list);
	return 0;
}

int parse_events_load_bpf_obj(struct parse_events_state *parse_state,
			      struct list_head *list,
			      struct bpf_object *obj,
			      struct list_head *head_config)
{
	int err;
	char errbuf[BUFSIZ];
	struct __add_bpf_event_param param = {parse_state, list, head_config};
	static bool registered_unprobe_atexit = false;

	if (IS_ERR(obj) || !obj) {
		snprintf(errbuf, sizeof(errbuf),
			 "Internal error: load bpf obj with NULL");
		err = -EINVAL;
		goto errout;
	}

	/*
	 * Register atexit handler before calling bpf__probe() so
	 * bpf__probe() don't need to unprobe probe points its already
	 * created when failure.
	 */
	if (!registered_unprobe_atexit) {
		atexit(bpf__clear);
		registered_unprobe_atexit = true;
	}

	err = bpf__probe(obj);
	if (err) {
		bpf__strerror_probe(obj, err, errbuf, sizeof(errbuf));
		goto errout;
	}

	err = bpf__load(obj);
	if (err) {
		bpf__strerror_load(obj, err, errbuf, sizeof(errbuf));
		goto errout;
	}

	err = bpf__foreach_event(obj, add_bpf_event, &param);
	if (err) {
		snprintf(errbuf, sizeof(errbuf),
			 "Attach events in BPF object failed");
		goto errout;
	}

	return 0;
errout:
	parse_events_error__handle(parse_state->error, 0,
				strdup(errbuf), strdup("(add -v to see detail)"));
	return err;
}

static int
parse_events_config_bpf(struct parse_events_state *parse_state,
			struct bpf_object *obj,
			struct list_head *head_config)
{
	struct parse_events_term *term;
	int error_pos;

	if (!head_config || list_empty(head_config))
		return 0;

	list_for_each_entry(term, head_config, list) {
		int err;

		if (term->type_term != PARSE_EVENTS__TERM_TYPE_USER) {
			parse_events_error__handle(parse_state->error, term->err_term,
						strdup("Invalid config term for BPF object"),
						NULL);
			return -EINVAL;
		}

		err = bpf__config_obj(obj, term, parse_state->evlist, &error_pos);
		if (err) {
			char errbuf[BUFSIZ];
			int idx;

			bpf__strerror_config_obj(obj, term, parse_state->evlist,
						 &error_pos, err, errbuf,
						 sizeof(errbuf));

			if (err == -BPF_LOADER_ERRNO__OBJCONF_MAP_VALUE)
				idx = term->err_val;
			else
				idx = term->err_term + error_pos;

			parse_events_error__handle(parse_state->error, idx,
						strdup(errbuf),
						strdup(
"Hint:\tValid config terms:\n"
"     \tmap:[<arraymap>].value<indices>=[value]\n"
"     \tmap:[<eventmap>].event<indices>=[event]\n"
"\n"
"     \twhere <indices> is something like [0,3...5] or [all]\n"
"     \t(add -v to see detail)"));
			return err;
		}
	}
	return 0;
}

/*
 * Split config terms:
 * perf record -e bpf.c/call-graph=fp,map:array.value[0]=1/ ...
 *  'call-graph=fp' is 'evt config', should be applied to each
 *  events in bpf.c.
 * 'map:array.value[0]=1' is 'obj config', should be processed
 * with parse_events_config_bpf.
 *
 * Move object config terms from the first list to obj_head_config.
 */
static void
split_bpf_config_terms(struct list_head *evt_head_config,
		       struct list_head *obj_head_config)
{
	struct parse_events_term *term, *temp;

	/*
	 * Currently, all possible user config term
	 * belong to bpf object. parse_events__is_hardcoded_term()
	 * happens to be a good flag.
	 *
	 * See parse_events_config_bpf() and
	 * config_term_tracepoint().
	 */
	list_for_each_entry_safe(term, temp, evt_head_config, list)
		if (!parse_events__is_hardcoded_term(term))
			list_move_tail(&term->list, obj_head_config);
}

int parse_events_load_bpf(struct parse_events_state *parse_state,
			  struct list_head *list,
			  char *bpf_file_name,
			  bool source,
			  struct list_head *head_config)
{
	int err;
	struct bpf_object *obj;
	LIST_HEAD(obj_head_config);

	if (head_config)
		split_bpf_config_terms(head_config, &obj_head_config);

	obj = bpf__prepare_load(bpf_file_name, source);
	if (IS_ERR(obj)) {
		char errbuf[BUFSIZ];

		err = PTR_ERR(obj);

		if (err == -ENOTSUP)
			snprintf(errbuf, sizeof(errbuf),
				 "BPF support is not compiled");
		else
			bpf__strerror_prepare_load(bpf_file_name,
						   source,
						   -err, errbuf,
						   sizeof(errbuf));

		parse_events_error__handle(parse_state->error, 0,
					strdup(errbuf), strdup("(add -v to see detail)"));
		return err;
	}

	err = parse_events_load_bpf_obj(parse_state, list, obj, head_config);
	if (err)
		return err;
	err = parse_events_config_bpf(parse_state, obj, &obj_head_config);

	/*
	 * Caller doesn't know anything about obj_head_config,
	 * so combine them together again before returning.
	 */
	if (head_config)
		list_splice_tail(&obj_head_config, head_config);
	return err;
}
#else // HAVE_LIBBPF_SUPPORT
int parse_events_load_bpf_obj(struct parse_events_state *parse_state,
			      struct list_head *list __maybe_unused,
			      struct bpf_object *obj __maybe_unused,
			      struct list_head *head_config __maybe_unused)
{
	parse_events_error__handle(parse_state->error, 0,
				   strdup("BPF support is not compiled"),
				   strdup("Make sure libbpf-devel is available at build time."));
	return -ENOTSUP;
}

int parse_events_load_bpf(struct parse_events_state *parse_state,
			  struct list_head *list __maybe_unused,
			  char *bpf_file_name __maybe_unused,
			  bool source __maybe_unused,
			  struct list_head *head_config __maybe_unused)
{
	parse_events_error__handle(parse_state->error, 0,
				   strdup("BPF support is not compiled"),
				   strdup("Make sure libbpf-devel is available at build time."));
	return -ENOTSUP;
}
#endif // HAVE_LIBBPF_SUPPORT

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
				struct list_head *head_config __maybe_unused)
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
			len = sizeof(long);
		else
			len = HW_BREAKPOINT_LEN_4;
	}

	attr.bp_len = len;

	attr.type = PERF_TYPE_BREAKPOINT;
	attr.sample_period = 1;

	if (head_config) {
		if (config_attr(&attr, head_config, parse_state->error,
				config_term_common))
			return -EINVAL;

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
	}

	name = get_config_name(head_config);

	return add_event(list, &parse_state->idx, &attr, name, /*mertic_id=*/NULL,
			 &config_terms);
}

static int check_type_val(struct parse_events_term *term,
			  struct parse_events_error *err,
			  int type)
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
	[PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE]	= "aux-sample-size",
	[PARSE_EVENTS__TERM_TYPE_METRIC_ID]		= "metric-id",
	[PARSE_EVENTS__TERM_TYPE_RAW]                   = "raw",
	[PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE]          = "legacy-cache",
	[PARSE_EVENTS__TERM_TYPE_HARDWARE]              = "hardware",
};

static bool config_term_shrinked;

static bool
config_term_avail(int term_type, struct parse_events_error *err)
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
		return true;
	default:
		if (!err)
			return false;

		/* term_type is validated so indexing is safe */
		if (asprintf(&err_str, "'%s' is not usable in 'perf stat'",
				config_term_names[term_type]) >= 0)
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
			      struct parse_events_error *err)
{
#define CHECK_TYPE_VAL(type)						   \
do {									   \
	if (check_type_val(term, err, PARSE_EVENTS__TERM_TYPE_ ## type)) \
		return -EINVAL;						   \
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
			parse_events_error__handle(err, term->err_val,
					strdup("invalid branch sample type"),
					NULL);
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_TIME:
		CHECK_TYPE_VAL(NUM);
		if (term->val.num > 1) {
			parse_events_error__handle(err, term->err_val,
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
			parse_events_error__handle(err, term->err_val,
						strdup("expected 0 or 1"),
						NULL);
			return -EINVAL;
		}
		break;
	case PARSE_EVENTS__TERM_TYPE_AUX_OUTPUT:
		CHECK_TYPE_VAL(NUM);
		break;
	case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
		CHECK_TYPE_VAL(NUM);
		if (term->val.num > UINT_MAX) {
			parse_events_error__handle(err, term->err_val,
						strdup("too big"),
						NULL);
			return -EINVAL;
		}
		break;
	default:
		parse_events_error__handle(err, term->err_term,
				strdup("unknown term"),
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
	if (!config_term_avail(term->type_term, err))
		return -EINVAL;
	return 0;
#undef CHECK_TYPE_VAL
}

static int config_term_pmu(struct perf_event_attr *attr,
			   struct parse_events_term *term,
			   struct parse_events_error *err)
{
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_LEGACY_CACHE) {
		const struct perf_pmu *pmu = perf_pmus__find_by_type(attr->type);

		if (!pmu) {
			char *err_str;

			if (asprintf(&err_str, "Failed to find PMU for type %d", attr->type) >= 0)
				parse_events_error__handle(err, term->err_term,
							   err_str, /*help=*/NULL);
			return -EINVAL;
		}
		if (perf_pmu__supports_legacy_cache(pmu)) {
			attr->type = PERF_TYPE_HW_CACHE;
			return parse_events__decode_legacy_cache(term->config, pmu->type,
								 &attr->config);
		} else
			term->type_term = PARSE_EVENTS__TERM_TYPE_USER;
	}
	if (term->type_term == PARSE_EVENTS__TERM_TYPE_HARDWARE) {
		const struct perf_pmu *pmu = perf_pmus__find_by_type(attr->type);

		if (!pmu) {
			char *err_str;

			if (asprintf(&err_str, "Failed to find PMU for type %d", attr->type) >= 0)
				parse_events_error__handle(err, term->err_term,
							   err_str, /*help=*/NULL);
			return -EINVAL;
		}
		attr->type = PERF_TYPE_HARDWARE;
		attr->config = term->val.num;
		if (perf_pmus__supports_extended_type())
			attr->config |= (__u64)pmu->type << PERF_PMU_TYPE_SHIFT;
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
	return config_term_common(attr, term, err);
}

#ifdef HAVE_LIBTRACEEVENT
static int config_term_tracepoint(struct perf_event_attr *attr,
				  struct parse_events_term *term,
				  struct parse_events_error *err)
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
	case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
		return config_term_common(attr, term, err);
	default:
		if (err) {
			parse_events_error__handle(err, term->err_term,
				strdup("unknown term"),
				strdup("valid terms: call-graph,stack-size\n"));
		}
		return -EINVAL;
	}

	return 0;
}
#endif

static int config_attr(struct perf_event_attr *attr,
		       struct list_head *head,
		       struct parse_events_error *err,
		       config_term_func_t config_term)
{
	struct parse_events_term *term;

	list_for_each_entry(term, head, list)
		if (config_term(attr, term, err))
			return -EINVAL;

	return 0;
}

static int get_config_terms(struct list_head *head_config,
			    struct list_head *head_terms __maybe_unused)
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

	list_for_each_entry(term, head_config, list) {
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
		case PARSE_EVENTS__TERM_TYPE_AUX_SAMPLE_SIZE:
			ADD_CONFIG_TERM_VAL(AUX_SAMPLE_SIZE, aux_sample_size,
					    term->val.num, term->weak);
			break;
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
static int get_config_chgs(struct perf_pmu *pmu, struct list_head *head_config,
			   struct list_head *head_terms)
{
	struct parse_events_term *term;
	u64 bits = 0;
	int type;

	list_for_each_entry(term, head_config, list) {
		switch (term->type_term) {
		case PARSE_EVENTS__TERM_TYPE_USER:
			type = perf_pmu__format_type(&pmu->format, term->config);
			if (type != PERF_PMU_FORMAT_VALUE_CONFIG)
				continue;
			bits |= perf_pmu__format_bits(&pmu->format, term->config);
			break;
		case PARSE_EVENTS__TERM_TYPE_CONFIG:
			bits = ~(u64)0;
			break;
		default:
			break;
		}
	}

	if (bits)
		ADD_CONFIG_TERM_VAL(CFG_CHG, cfg_chg, bits, false);

#undef ADD_CONFIG_TERM
	return 0;
}

int parse_events_add_tracepoint(struct list_head *list, int *idx,
				const char *sys, const char *event,
				struct parse_events_error *err,
				struct list_head *head_config)
{
#ifdef HAVE_LIBTRACEEVENT
	if (head_config) {
		struct perf_event_attr attr;

		if (config_attr(&attr, head_config, err,
				config_term_tracepoint))
			return -EINVAL;
	}

	if (strpbrk(sys, "*?"))
		return add_tracepoint_multi_sys(list, idx, sys, event,
						err, head_config);
	else
		return add_tracepoint_event(list, idx, sys, event,
					    err, head_config);
#else
	(void)list;
	(void)idx;
	(void)sys;
	(void)event;
	(void)head_config;
	parse_events_error__handle(err, 0, strdup("unsupported tracepoint"),
				strdup("libtraceevent is necessary for tracepoint support"));
	return -1;
#endif
}

static int __parse_events_add_numeric(struct parse_events_state *parse_state,
				struct list_head *list,
				struct perf_pmu *pmu, u32 type, u32 extended_type,
				u64 config, struct list_head *head_config)
{
	struct perf_event_attr attr;
	LIST_HEAD(config_terms);
	const char *name, *metric_id;
	int ret;

	memset(&attr, 0, sizeof(attr));
	attr.type = type;
	attr.config = config;
	if (extended_type && (type == PERF_TYPE_HARDWARE || type == PERF_TYPE_HW_CACHE)) {
		assert(perf_pmus__supports_extended_type());
		attr.config |= (u64)extended_type << PERF_PMU_TYPE_SHIFT;
	}

	if (head_config) {
		if (config_attr(&attr, head_config, parse_state->error,
				config_term_common))
			return -EINVAL;

		if (get_config_terms(head_config, &config_terms))
			return -ENOMEM;
	}

	name = get_config_name(head_config);
	metric_id = get_config_metric_id(head_config);
	ret = __add_event(list, &parse_state->idx, &attr, /*init_attr*/true, name,
			metric_id, pmu, &config_terms, /*auto_merge_stats=*/false,
			/*cpu_list=*/NULL) ? 0 : -ENOMEM;
	free_config_terms(&config_terms);
	return ret;
}

int parse_events_add_numeric(struct parse_events_state *parse_state,
			     struct list_head *list,
			     u32 type, u64 config,
			     struct list_head *head_config,
			     bool wildcard)
{
	struct perf_pmu *pmu = NULL;
	bool found_supported = false;

	/* Wildcards on numeric values are only supported by core PMUs. */
	if (wildcard && perf_pmus__supports_extended_type()) {
		while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
			int ret;

			found_supported = true;
			if (parse_events__filter_pmu(parse_state, pmu))
				continue;

			ret = __parse_events_add_numeric(parse_state, list, pmu,
							 type, pmu->type,
							 config, head_config);
			if (ret)
				return ret;
		}
		if (found_supported)
			return 0;
	}
	return __parse_events_add_numeric(parse_state, list, perf_pmus__find_by_type(type),
					type, /*extended_type=*/0, config, head_config);
}

int parse_events_add_tool(struct parse_events_state *parse_state,
			  struct list_head *list,
			  int tool_event)
{
	return add_event_tool(list, &parse_state->idx, tool_event);
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

int parse_events_add_pmu(struct parse_events_state *parse_state,
			 struct list_head *list, char *name,
			 struct list_head *head_config,
			 bool auto_merge_stats)
{
	struct perf_event_attr attr;
	struct perf_pmu_info info;
	struct perf_pmu *pmu;
	struct evsel *evsel;
	struct parse_events_error *err = parse_state->error;
	LIST_HEAD(config_terms);

	pmu = parse_state->fake_pmu ?: perf_pmus__find(name);

	if (verbose > 1 && !(pmu && pmu->selectable)) {
		fprintf(stderr, "Attempting to add event pmu '%s' with '",
			name);
		if (head_config) {
			struct parse_events_term *term;

			list_for_each_entry(term, head_config, list) {
				fprintf(stderr, "%s,", term->config);
			}
		}
		fprintf(stderr, "' that may result in non-fatal errors\n");
	}

	if (!pmu) {
		char *err_str;

		if (asprintf(&err_str,
				"Cannot find PMU `%s'. Missing kernel support?",
				name) >= 0)
			parse_events_error__handle(err, 0, err_str, NULL);
		return -EINVAL;
	}
	if (head_config)
		fix_raw(head_config, pmu);

	if (pmu->default_config) {
		memcpy(&attr, pmu->default_config,
		       sizeof(struct perf_event_attr));
	} else {
		memset(&attr, 0, sizeof(attr));
	}
	attr.type = pmu->type;

	if (!head_config) {
		evsel = __add_event(list, &parse_state->idx, &attr,
				    /*init_attr=*/true, /*name=*/NULL,
				    /*metric_id=*/NULL, pmu,
				    /*config_terms=*/NULL, auto_merge_stats,
				    /*cpu_list=*/NULL);
		return evsel ? 0 : -ENOMEM;
	}

	if (!parse_state->fake_pmu && perf_pmu__check_alias(pmu, head_config, &info))
		return -EINVAL;

	if (verbose > 1) {
		fprintf(stderr, "After aliases, add event pmu '%s' with '",
			name);
		if (head_config) {
			struct parse_events_term *term;

			list_for_each_entry(term, head_config, list) {
				fprintf(stderr, "%s,", term->config);
			}
		}
		fprintf(stderr, "' that may result in non-fatal errors\n");
	}

	/*
	 * Configure hardcoded terms first, no need to check
	 * return value when called with fail == 0 ;)
	 */
	if (config_attr(&attr, head_config, parse_state->error, config_term_pmu))
		return -EINVAL;

	if (get_config_terms(head_config, &config_terms))
		return -ENOMEM;

	/*
	 * When using default config, record which bits of attr->config were
	 * changed by the user.
	 */
	if (pmu->default_config && get_config_chgs(pmu, head_config, &config_terms))
		return -ENOMEM;

	if (!parse_state->fake_pmu && perf_pmu__config(pmu, &attr, head_config, parse_state->error)) {
		free_config_terms(&config_terms);
		return -EINVAL;
	}

	evsel = __add_event(list, &parse_state->idx, &attr, /*init_attr=*/true,
			    get_config_name(head_config),
			    get_config_metric_id(head_config), pmu,
			    &config_terms, auto_merge_stats, /*cpu_list=*/NULL);
	if (!evsel)
		return -ENOMEM;

	if (evsel->name)
		evsel->use_config_name = true;

	evsel->percore = config_term_percore(&evsel->config_terms);

	if (parse_state->fake_pmu)
		return 0;

	free((char *)evsel->unit);
	evsel->unit = strdup(info.unit);
	evsel->scale = info.scale;
	evsel->per_pkg = info.per_pkg;
	evsel->snapshot = info.snapshot;
	return 0;
}

int parse_events_multi_pmu_add(struct parse_events_state *parse_state,
			       char *str, struct list_head *head,
			       struct list_head **listp)
{
	struct parse_events_term *term;
	struct list_head *list = NULL;
	struct list_head *orig_head = NULL;
	struct perf_pmu *pmu = NULL;
	int ok = 0;
	char *config;

	*listp = NULL;

	if (!head) {
		head = malloc(sizeof(struct list_head));
		if (!head)
			goto out_err;

		INIT_LIST_HEAD(head);
	}
	config = strdup(str);
	if (!config)
		goto out_err;

	if (parse_events_term__num(&term,
				   PARSE_EVENTS__TERM_TYPE_USER,
				   config, 1, false, NULL,
					NULL) < 0) {
		free(config);
		goto out_err;
	}
	list_add_tail(&term->list, head);

	/* Add it for all PMUs that support the alias */
	list = malloc(sizeof(struct list_head));
	if (!list)
		goto out_err;

	INIT_LIST_HEAD(list);

	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		struct perf_pmu_alias *alias;
		bool auto_merge_stats;

		if (parse_events__filter_pmu(parse_state, pmu))
			continue;

		auto_merge_stats = perf_pmu__auto_merge_stats(pmu);

		list_for_each_entry(alias, &pmu->aliases, list) {
			if (!strcasecmp(alias->name, str)) {
				parse_events_copy_term_list(head, &orig_head);
				if (!parse_events_add_pmu(parse_state, list,
							  pmu->name, orig_head,
							  auto_merge_stats)) {
					pr_debug("%s -> %s/%s/\n", str,
						 pmu->name, alias->str);
					ok++;
				}
				parse_events_terms__delete(orig_head);
			}
		}
	}

	if (parse_state->fake_pmu) {
		if (!parse_events_add_pmu(parse_state, list, str, head,
					  /*auto_merge_stats=*/true)) {
			pr_debug("%s -> %s/%s/\n", str, "fake_pmu", str);
			ok++;
		}
	}

out_err:
	if (ok)
		*listp = list;
	else
		free(list);

	parse_events_terms__delete(head);
	return ok ? 0 : -1;
}

int parse_events__modifier_group(struct list_head *list,
				 char *event_mod)
{
	return parse_events__modifier_event(list, event_mod, true);
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
	leader->group_name = name;
}

/* list_event is assumed to point to malloc'ed memory */
void parse_events_update_lists(struct list_head *list_event,
			       struct list_head *list_all)
{
	/*
	 * Called for single event definition. Update the
	 * 'all event' list, and reinit the 'single event'
	 * list, for next event definition.
	 */
	list_splice_tail(list_event, list_all);
	free(list_event);
}

struct event_modifier {
	int eu;
	int ek;
	int eh;
	int eH;
	int eG;
	int eI;
	int precise;
	int precise_max;
	int exclude_GH;
	int sample_read;
	int pinned;
	int weak;
	int exclusive;
	int bpf_counter;
};

static int get_event_modifier(struct event_modifier *mod, char *str,
			       struct evsel *evsel)
{
	int eu = evsel ? evsel->core.attr.exclude_user : 0;
	int ek = evsel ? evsel->core.attr.exclude_kernel : 0;
	int eh = evsel ? evsel->core.attr.exclude_hv : 0;
	int eH = evsel ? evsel->core.attr.exclude_host : 0;
	int eG = evsel ? evsel->core.attr.exclude_guest : 0;
	int eI = evsel ? evsel->core.attr.exclude_idle : 0;
	int precise = evsel ? evsel->core.attr.precise_ip : 0;
	int precise_max = 0;
	int sample_read = 0;
	int pinned = evsel ? evsel->core.attr.pinned : 0;
	int exclusive = evsel ? evsel->core.attr.exclusive : 0;

	int exclude = eu | ek | eh;
	int exclude_GH = evsel ? evsel->exclude_GH : 0;
	int weak = 0;
	int bpf_counter = 0;

	memset(mod, 0, sizeof(*mod));

	while (*str) {
		if (*str == 'u') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			if (!exclude_GH && !perf_guest)
				eG = 1;
			eu = 0;
		} else if (*str == 'k') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			ek = 0;
		} else if (*str == 'h') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
			eh = 0;
		} else if (*str == 'G') {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eG = 0;
		} else if (*str == 'H') {
			if (!exclude_GH)
				exclude_GH = eG = eH = 1;
			eH = 0;
		} else if (*str == 'I') {
			eI = 1;
		} else if (*str == 'p') {
			precise++;
			/* use of precise requires exclude_guest */
			if (!exclude_GH)
				eG = 1;
		} else if (*str == 'P') {
			precise_max = 1;
		} else if (*str == 'S') {
			sample_read = 1;
		} else if (*str == 'D') {
			pinned = 1;
		} else if (*str == 'e') {
			exclusive = 1;
		} else if (*str == 'W') {
			weak = 1;
		} else if (*str == 'b') {
			bpf_counter = 1;
		} else
			break;

		++str;
	}

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
	if (precise > 3)
		return -EINVAL;

	mod->eu = eu;
	mod->ek = ek;
	mod->eh = eh;
	mod->eH = eH;
	mod->eG = eG;
	mod->eI = eI;
	mod->precise = precise;
	mod->precise_max = precise_max;
	mod->exclude_GH = exclude_GH;
	mod->sample_read = sample_read;
	mod->pinned = pinned;
	mod->weak = weak;
	mod->bpf_counter = bpf_counter;
	mod->exclusive = exclusive;

	return 0;
}

/*
 * Basic modifier sanity check to validate it contains only one
 * instance of any modifier (apart from 'p') present.
 */
static int check_modifier(char *str)
{
	char *p = str;

	/* The sizeof includes 0 byte as well. */
	if (strlen(str) > (sizeof("ukhGHpppPSDIWeb") - 1))
		return -1;

	while (*p) {
		if (*p != 'p' && strchr(p + 1, *p))
			return -1;
		p++;
	}

	return 0;
}

int parse_events__modifier_event(struct list_head *list, char *str, bool add)
{
	struct evsel *evsel;
	struct event_modifier mod;

	if (str == NULL)
		return 0;

	if (check_modifier(str))
		return -EINVAL;

	if (!add && get_event_modifier(&mod, str, NULL))
		return -EINVAL;

	__evlist__for_each_entry(list, evsel) {
		if (add && get_event_modifier(&mod, str, evsel))
			return -EINVAL;

		evsel->core.attr.exclude_user   = mod.eu;
		evsel->core.attr.exclude_kernel = mod.ek;
		evsel->core.attr.exclude_hv     = mod.eh;
		evsel->core.attr.precise_ip     = mod.precise;
		evsel->core.attr.exclude_host   = mod.eH;
		evsel->core.attr.exclude_guest  = mod.eG;
		evsel->core.attr.exclude_idle   = mod.eI;
		evsel->exclude_GH          = mod.exclude_GH;
		evsel->sample_read         = mod.sample_read;
		evsel->precise_max         = mod.precise_max;
		evsel->weak_group	   = mod.weak;
		evsel->bpf_counter	   = mod.bpf_counter;

		if (evsel__is_group_leader(evsel)) {
			evsel->core.attr.pinned = mod.pinned;
			evsel->core.attr.exclusive = mod.exclusive;
		}
	}

	return 0;
}

int parse_events_name(struct list_head *list, const char *name)
{
	struct evsel *evsel;

	__evlist__for_each_entry(list, evsel) {
		if (!evsel->name)
			evsel->name = strdup(name);
	}

	return 0;
}

static int parse_events__scanner(const char *str,
				 struct parse_events_state *parse_state)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	ret = parse_events_lex_init_extra(parse_state, &scanner);
	if (ret)
		return ret;

	buffer = parse_events__scan_string(str, scanner);

#ifdef PARSER_DEBUG
	parse_events_debug = 1;
	parse_events_set_debug(1, scanner);
#endif
	ret = parse_events_parse(parse_state, scanner);

	parse_events__flush_buffer(buffer, scanner);
	parse_events__delete_buffer(buffer, scanner);
	parse_events_lex_destroy(scanner);
	return ret;
}

/*
 * parse event config string, return a list of event terms.
 */
int parse_events_terms(struct list_head *terms, const char *str)
{
	struct parse_events_state parse_state = {
		.terms  = NULL,
		.stoken = PE_START_TERMS,
	};
	int ret;

	ret = parse_events__scanner(str, &parse_state);

	if (!ret) {
		list_splice(parse_state.terms, terms);
		zfree(&parse_state.terms);
		return 0;
	}

	parse_events_terms__delete(parse_state.terms);
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
	/* Assign the actual name taking care that the fake PMU lacks a name. */
	evsel->group_pmu_name = strdup(group_pmu_name ?: "fake");
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
	bool lhs_has_group, rhs_has_group;

	/*
	 * First sort by grouping/leader. Read the leader idx only if the evsel
	 * is part of a group, by default ungrouped events will be sorted
	 * relative to grouped events based on where the first ungrouped event
	 * occurs. If both events don't have a group we want to fall-through to
	 * the arch specific sorting, that can reorder and fix things like
	 * Intel's topdown events.
	 */
	if (lhs_core->leader != lhs_core || lhs_core->nr_members > 1) {
		lhs_has_group = true;
		lhs_sort_idx = lhs_core->leader->idx;
	} else {
		lhs_has_group = false;
		lhs_sort_idx = *force_grouped_idx != -1 && arch_evsel__must_be_in_group(lhs)
			? *force_grouped_idx
			: lhs_core->idx;
	}
	if (rhs_core->leader != rhs_core || rhs_core->nr_members > 1) {
		rhs_has_group = true;
		rhs_sort_idx = rhs_core->leader->idx;
	} else {
		rhs_has_group = false;
		rhs_sort_idx = *force_grouped_idx != -1 && arch_evsel__must_be_in_group(rhs)
			? *force_grouped_idx
			: rhs_core->idx;
	}

	if (lhs_sort_idx != rhs_sort_idx)
		return lhs_sort_idx - rhs_sort_idx;

	/* Group by PMU if there is a group. Groups can't span PMUs. */
	if (lhs_has_group && rhs_has_group) {
		lhs_pmu_name = lhs->group_pmu_name;
		rhs_pmu_name = rhs->group_pmu_name;
		ret = strcmp(lhs_pmu_name, rhs_pmu_name);
		if (ret)
			return ret;
	}

	/* Architecture specific sorting. */
	return arch_evlist__cmp(lhs, rhs);
}

static int parse_events__sort_events_and_fix_groups(struct list_head *list)
{
	int idx = 0, force_grouped_idx = -1;
	struct evsel *pos, *cur_leader = NULL;
	struct perf_evsel *cur_leaders_grp = NULL;
	bool idx_changed = false, cur_leader_force_grouped = false;
	int orig_num_leaders = 0, num_leaders = 0;
	int ret;

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

		/* Remember an index to sort all forced grouped events together to. */
		if (force_grouped_idx == -1 && pos == pos_leader && pos->core.nr_members < 2 &&
		    arch_evsel__must_be_in_group(pos))
			force_grouped_idx = pos->core.idx;
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
		if (!cur_leader)
			cur_leader = pos;

		cur_leader_pmu_name = cur_leader->group_pmu_name;
		if ((cur_leaders_grp != pos->core.leader &&
		     (!pos_force_grouped || !cur_leader_force_grouped)) ||
		    strcmp(cur_leader_pmu_name, pos_pmu_name)) {
			/* Event is for a different group/PMU than last. */
			cur_leader = pos;
			/*
			 * Remember the leader's group before it is overwritten,
			 * so that later events match as being in the same
			 * group.
			 */
			cur_leaders_grp = pos->core.leader;
			/*
			 * Avoid forcing events into groups with events that
			 * don't need to be in the group.
			 */
			cur_leader_force_grouped = pos_force_grouped;
		}
		if (pos_leader != cur_leader) {
			/* The leader changed so update it. */
			evsel__set_leader(pos, cur_leader);
		}
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
		   struct parse_events_error *err, struct perf_pmu *fake_pmu,
		   bool warn_if_reordered)
{
	struct parse_events_state parse_state = {
		.list	  = LIST_HEAD_INIT(parse_state.list),
		.idx	  = evlist->core.nr_entries,
		.error	  = err,
		.evlist	  = evlist,
		.stoken	  = PE_START_EVENTS,
		.fake_pmu = fake_pmu,
		.pmu_filter = pmu_filter,
		.match_legacy_cache_terms = true,
	};
	int ret, ret2;

	ret = parse_events__scanner(str, &parse_state);

	if (!ret && list_empty(&parse_state.list)) {
		WARN_ONCE(true, "WARNING: event parser found nothing\n");
		return -1;
	}

	ret2 = parse_events__sort_events_and_fix_groups(&parse_state.list);
	if (ret2 < 0)
		return ret;

	if (ret2 && warn_if_reordered && !parse_state.wild_card_pmus)
		pr_warning("WARNING: events were regrouped to match PMUs\n");

	/*
	 * Add list to the evlist even with errors to allow callers to clean up.
	 */
	evlist__splice_list_tail(evlist, &parse_state.list);

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

void parse_events_error__init(struct parse_events_error *err)
{
	bzero(err, sizeof(*err));
}

void parse_events_error__exit(struct parse_events_error *err)
{
	zfree(&err->str);
	zfree(&err->help);
	zfree(&err->first_str);
	zfree(&err->first_help);
}

void parse_events_error__handle(struct parse_events_error *err, int idx,
				char *str, char *help)
{
	if (WARN(!str || !err, "WARNING: failed to provide error string or struct\n"))
		goto out_free;
	switch (err->num_errors) {
	case 0:
		err->idx = idx;
		err->str = str;
		err->help = help;
		break;
	case 1:
		err->first_idx = err->idx;
		err->idx = idx;
		err->first_str = err->str;
		err->str = str;
		err->first_help = err->help;
		err->help = help;
		break;
	default:
		pr_debug("Multiple errors dropping message: %s (%s)\n",
			err->str, err->help);
		free(err->str);
		err->str = str;
		free(err->help);
		err->help = help;
		break;
	}
	err->num_errors++;
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

void parse_events_error__print(struct parse_events_error *err,
			       const char *event)
{
	if (!err->num_errors)
		return;

	__parse_events_error__print(err->idx, err->str, err->help, event);

	if (err->num_errors > 1) {
		fputs("\nInitial error:\n", stderr);
		__parse_events_error__print(err->first_idx, err->first_str,
					err->first_help, event);
	}
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
			     /*fake_pmu=*/NULL, /*warn_if_reordered=*/true);

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

static int set_filter(struct evsel *evsel, const void *arg)
{
	const char *str = arg;
	bool found = false;
	int nr_addr_filters = 0;
	struct perf_pmu *pmu = NULL;

	if (evsel == NULL) {
		fprintf(stderr,
			"--filter option should follow a -e tracepoint or HW tracer option\n");
		return -1;
	}

	if (evsel->core.attr.type == PERF_TYPE_TRACEPOINT) {
		if (evsel__append_tp_filter(evsel, str) < 0) {
			fprintf(stderr,
				"not enough memory to hold filter string\n");
			return -1;
		}

		return 0;
	}

	while ((pmu = perf_pmus__scan(pmu)) != NULL)
		if (pmu->type == evsel->core.attr.type) {
			found = true;
			break;
		}

	if (found)
		perf_pmu__scan_file(pmu, "nr_addr_filters",
				    "%d", &nr_addr_filters);

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
			   int type_term, char *config, u64 num,
			   bool no_value,
			   void *loc_term_, void *loc_val_)
{
	YYLTYPE *loc_term = loc_term_;
	YYLTYPE *loc_val = loc_val_;

	struct parse_events_term temp = {
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = type_term,
		.config    = config ? : strdup(config_term_names[type_term]),
		.no_value  = no_value,
		.err_term  = loc_term ? loc_term->first_column : 0,
		.err_val   = loc_val  ? loc_val->first_column  : 0,
	};

	return new_term(term, &temp, NULL, num);
}

int parse_events_term__str(struct parse_events_term **term,
			   int type_term, char *config, char *str,
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

	return new_term(term, &temp, str, 0);
}

int parse_events_term__term(struct parse_events_term **term,
			    int term_lhs, int term_rhs,
			    void *loc_term, void *loc_val)
{
	return parse_events_term__str(term, term_lhs, NULL,
				      strdup(config_term_names[term_rhs]),
				      loc_term, loc_val);
}

int parse_events_term__clone(struct parse_events_term **new,
			     struct parse_events_term *term)
{
	char *str;
	struct parse_events_term temp = {
		.type_val  = term->type_val,
		.type_term = term->type_term,
		.config    = NULL,
		.err_term  = term->err_term,
		.err_val   = term->err_val,
	};

	if (term->config) {
		temp.config = strdup(term->config);
		if (!temp.config)
			return -ENOMEM;
	}
	if (term->type_val == PARSE_EVENTS__TERM_TYPE_NUM)
		return new_term(new, &temp, NULL, term->val.num);

	str = strdup(term->val.str);
	if (!str)
		return -ENOMEM;
	return new_term(new, &temp, str, 0);
}

void parse_events_term__delete(struct parse_events_term *term)
{
	if (term->array.nr_ranges)
		zfree(&term->array.ranges);

	if (term->type_val != PARSE_EVENTS__TERM_TYPE_NUM)
		zfree(&term->val.str);

	zfree(&term->config);
	free(term);
}

int parse_events_copy_term_list(struct list_head *old,
				 struct list_head **new)
{
	struct parse_events_term *term, *n;
	int ret;

	if (!old) {
		*new = NULL;
		return 0;
	}

	*new = malloc(sizeof(struct list_head));
	if (!*new)
		return -ENOMEM;
	INIT_LIST_HEAD(*new);

	list_for_each_entry (term, old, list) {
		ret = parse_events_term__clone(&n, term);
		if (ret)
			return ret;
		list_add_tail(&n->list, *new);
	}
	return 0;
}

void parse_events_terms__purge(struct list_head *terms)
{
	struct parse_events_term *term, *h;

	list_for_each_entry_safe(term, h, terms, list) {
		list_del_init(&term->list);
		parse_events_term__delete(term);
	}
}

void parse_events_terms__delete(struct list_head *terms)
{
	if (!terms)
		return;
	parse_events_terms__purge(terms);
	free(terms);
}

void parse_events__clear_array(struct parse_events_array *a)
{
	zfree(&a->ranges);
}

void parse_events_evlist_error(struct parse_events_state *parse_state,
			       int idx, const char *str)
{
	if (!parse_state->error)
		return;

	parse_events_error__handle(parse_state->error, idx, strdup(str), NULL);
}

static void config_terms_list(char *buf, size_t buf_sz)
{
	int i;
	bool first = true;

	buf[0] = '\0';
	for (i = 0; i < __PARSE_EVENTS__TERM_TYPE_NR; i++) {
		const char *name = config_term_names[i];

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
