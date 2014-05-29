#include <linux/hw_breakpoint.h>
#include "util.h"
#include "../perf.h"
#include "evlist.h"
#include "evsel.h"
#include "parse-options.h"
#include "parse-events.h"
#include "exec_cmd.h"
#include "string.h"
#include "symbol.h"
#include "cache.h"
#include "header.h"
#include "debug.h"
#include <api/fs/debugfs.h>
#include "parse-events-bison.h"
#define YY_EXTRA_TYPE int
#include "parse-events-flex.h"
#include "pmu.h"
#include "thread_map.h"

#define MAX_NAME_LEN 100

struct event_symbol {
	const char	*symbol;
	const char	*alias;
};

#ifdef PARSER_DEBUG
extern int parse_events_debug;
#endif
int parse_events_parse(void *data, void *scanner);

static struct perf_pmu_event_symbol *perf_pmu_events_list;
/*
 * The variable indicates the number of supported pmu event symbols.
 * 0 means not initialized and ready to init
 * -1 means failed to init, don't try anymore
 * >0 is the number of supported pmu event symbols
 */
static int perf_pmu_events_list_num;

static struct event_symbol event_symbols_hw[PERF_COUNT_HW_MAX] = {
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

static struct event_symbol event_symbols_sw[PERF_COUNT_SW_MAX] = {
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
};

#define __PERF_EVENT_FIELD(config, name) \
	((config & PERF_EVENT_##name##_MASK) >> PERF_EVENT_##name##_SHIFT)

#define PERF_EVENT_RAW(config)		__PERF_EVENT_FIELD(config, RAW)
#define PERF_EVENT_CONFIG(config)	__PERF_EVENT_FIELD(config, CONFIG)
#define PERF_EVENT_TYPE(config)		__PERF_EVENT_FIELD(config, TYPE)
#define PERF_EVENT_ID(config)		__PERF_EVENT_FIELD(config, EVENT)

#define for_each_subsystem(sys_dir, sys_dirent, sys_next)	       \
	while (!readdir_r(sys_dir, &sys_dirent, &sys_next) && sys_next)	       \
	if (sys_dirent.d_type == DT_DIR &&				       \
	   (strcmp(sys_dirent.d_name, ".")) &&				       \
	   (strcmp(sys_dirent.d_name, "..")))

static int tp_event_has_id(struct dirent *sys_dir, struct dirent *evt_dir)
{
	char evt_path[MAXPATHLEN];
	int fd;

	snprintf(evt_path, MAXPATHLEN, "%s/%s/%s/id", tracing_events_path,
			sys_dir->d_name, evt_dir->d_name);
	fd = open(evt_path, O_RDONLY);
	if (fd < 0)
		return -EINVAL;
	close(fd);

	return 0;
}

#define for_each_event(sys_dirent, evt_dir, evt_dirent, evt_next)	       \
	while (!readdir_r(evt_dir, &evt_dirent, &evt_next) && evt_next)        \
	if (evt_dirent.d_type == DT_DIR &&				       \
	   (strcmp(evt_dirent.d_name, ".")) &&				       \
	   (strcmp(evt_dirent.d_name, "..")) &&				       \
	   (!tp_event_has_id(&sys_dirent, &evt_dirent)))

#define MAX_EVENT_LENGTH 512


struct tracepoint_path *tracepoint_id_to_path(u64 config)
{
	struct tracepoint_path *path = NULL;
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_next, *evt_next, sys_dirent, evt_dirent;
	char id_buf[24];
	int fd;
	u64 id;
	char evt_path[MAXPATHLEN];
	char dir_path[MAXPATHLEN];

	if (debugfs_valid_mountpoint(tracing_events_path))
		return NULL;

	sys_dir = opendir(tracing_events_path);
	if (!sys_dir)
		return NULL;

	for_each_subsystem(sys_dir, sys_dirent, sys_next) {

		snprintf(dir_path, MAXPATHLEN, "%s/%s", tracing_events_path,
			 sys_dirent.d_name);
		evt_dir = opendir(dir_path);
		if (!evt_dir)
			continue;

		for_each_event(sys_dirent, evt_dir, evt_dirent, evt_next) {

			snprintf(evt_path, MAXPATHLEN, "%s/%s/id", dir_path,
				 evt_dirent.d_name);
			fd = open(evt_path, O_RDONLY);
			if (fd < 0)
				continue;
			if (read(fd, id_buf, sizeof(id_buf)) < 0) {
				close(fd);
				continue;
			}
			close(fd);
			id = atoll(id_buf);
			if (id == config) {
				closedir(evt_dir);
				closedir(sys_dir);
				path = zalloc(sizeof(*path));
				path->system = malloc(MAX_EVENT_LENGTH);
				if (!path->system) {
					free(path);
					return NULL;
				}
				path->name = malloc(MAX_EVENT_LENGTH);
				if (!path->name) {
					zfree(&path->system);
					free(path);
					return NULL;
				}
				strncpy(path->system, sys_dirent.d_name,
					MAX_EVENT_LENGTH);
				strncpy(path->name, evt_dirent.d_name,
					MAX_EVENT_LENGTH);
				return path;
			}
		}
		closedir(evt_dir);
	}

	closedir(sys_dir);
	return NULL;
}

struct tracepoint_path *tracepoint_name_to_path(const char *name)
{
	struct tracepoint_path *path = zalloc(sizeof(*path));
	char *str = strchr(name, ':');

	if (path == NULL || str == NULL) {
		free(path);
		return NULL;
	}

	path->system = strndup(name, str - name);
	path->name = strdup(str+1);

	if (path->system == NULL || path->name == NULL) {
		zfree(&path->system);
		zfree(&path->name);
		free(path);
		path = NULL;
	}

	return path;
}

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



static struct perf_evsel *
__add_event(struct list_head *list, int *idx,
	    struct perf_event_attr *attr,
	    char *name, struct cpu_map *cpus)
{
	struct perf_evsel *evsel;

	event_attr_init(attr);

	evsel = perf_evsel__new_idx(attr, (*idx)++);
	if (!evsel)
		return NULL;

	evsel->cpus = cpus;
	if (name)
		evsel->name = strdup(name);
	list_add_tail(&evsel->node, list);
	return evsel;
}

static int add_event(struct list_head *list, int *idx,
		     struct perf_event_attr *attr, char *name)
{
	return __add_event(list, idx, attr, name, NULL) ? 0 : -ENOMEM;
}

static int parse_aliases(char *str, const char *names[][PERF_EVSEL__MAX_ALIASES], int size)
{
	int i, j;
	int n, longest = -1;

	for (i = 0; i < size; i++) {
		for (j = 0; j < PERF_EVSEL__MAX_ALIASES && names[i][j]; j++) {
			n = strlen(names[i][j]);
			if (n > longest && !strncasecmp(str, names[i][j], n))
				longest = n;
		}
		if (longest > 0)
			return i;
	}

	return -1;
}

int parse_events_add_cache(struct list_head *list, int *idx,
			   char *type, char *op_result1, char *op_result2)
{
	struct perf_event_attr attr;
	char name[MAX_NAME_LEN];
	int cache_type = -1, cache_op = -1, cache_result = -1;
	char *op_result[2] = { op_result1, op_result2 };
	int i, n;

	/*
	 * No fallback - if we cannot get a clear cache type
	 * then bail out:
	 */
	cache_type = parse_aliases(type, perf_evsel__hw_cache,
				   PERF_COUNT_HW_CACHE_MAX);
	if (cache_type == -1)
		return -EINVAL;

	n = snprintf(name, MAX_NAME_LEN, "%s", type);

	for (i = 0; (i < 2) && (op_result[i]); i++) {
		char *str = op_result[i];

		n += snprintf(name + n, MAX_NAME_LEN - n, "-%s", str);

		if (cache_op == -1) {
			cache_op = parse_aliases(str, perf_evsel__hw_cache_op,
						 PERF_COUNT_HW_CACHE_OP_MAX);
			if (cache_op >= 0) {
				if (!perf_evsel__is_cache_op_valid(cache_type, cache_op))
					return -EINVAL;
				continue;
			}
		}

		if (cache_result == -1) {
			cache_result = parse_aliases(str, perf_evsel__hw_cache_result,
						     PERF_COUNT_HW_CACHE_RESULT_MAX);
			if (cache_result >= 0)
				continue;
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

	memset(&attr, 0, sizeof(attr));
	attr.config = cache_type | (cache_op << 8) | (cache_result << 16);
	attr.type = PERF_TYPE_HW_CACHE;
	return add_event(list, idx, &attr, name);
}

static int add_tracepoint(struct list_head *list, int *idx,
			  char *sys_name, char *evt_name)
{
	struct perf_evsel *evsel;

	evsel = perf_evsel__newtp_idx(sys_name, evt_name, (*idx)++);
	if (!evsel)
		return -ENOMEM;

	list_add_tail(&evsel->node, list);

	return 0;
}

static int add_tracepoint_multi_event(struct list_head *list, int *idx,
				      char *sys_name, char *evt_name)
{
	char evt_path[MAXPATHLEN];
	struct dirent *evt_ent;
	DIR *evt_dir;
	int ret = 0;

	snprintf(evt_path, MAXPATHLEN, "%s/%s", tracing_events_path, sys_name);
	evt_dir = opendir(evt_path);
	if (!evt_dir) {
		perror("Can't open event dir");
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

		ret = add_tracepoint(list, idx, sys_name, evt_ent->d_name);
	}

	closedir(evt_dir);
	return ret;
}

static int add_tracepoint_event(struct list_head *list, int *idx,
				char *sys_name, char *evt_name)
{
	return strpbrk(evt_name, "*?") ?
	       add_tracepoint_multi_event(list, idx, sys_name, evt_name) :
	       add_tracepoint(list, idx, sys_name, evt_name);
}

static int add_tracepoint_multi_sys(struct list_head *list, int *idx,
				    char *sys_name, char *evt_name)
{
	struct dirent *events_ent;
	DIR *events_dir;
	int ret = 0;

	events_dir = opendir(tracing_events_path);
	if (!events_dir) {
		perror("Can't open event dir");
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
					   evt_name);
	}

	closedir(events_dir);
	return ret;
}

int parse_events_add_tracepoint(struct list_head *list, int *idx,
				char *sys, char *event)
{
	int ret;

	ret = debugfs_valid_mountpoint(tracing_events_path);
	if (ret)
		return ret;

	if (strpbrk(sys, "*?"))
		return add_tracepoint_multi_sys(list, idx, sys, event);
	else
		return add_tracepoint_event(list, idx, sys, event);
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

int parse_events_add_breakpoint(struct list_head *list, int *idx,
				void *ptr, char *type, u64 len)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.bp_addr = (unsigned long) ptr;

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

	return add_event(list, idx, &attr, NULL);
}

static int config_term(struct perf_event_attr *attr,
		       struct parse_events_term *term)
{
#define CHECK_TYPE_VAL(type)					\
do {								\
	if (PARSE_EVENTS__TERM_TYPE_ ## type != term->type_val)	\
		return -EINVAL;					\
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
	case PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD:
		CHECK_TYPE_VAL(NUM);
		attr->sample_period = term->val.num;
		break;
	case PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE:
		/*
		 * TODO uncomment when the field is available
		 * attr->branch_sample_type = term->val.num;
		 */
		break;
	case PARSE_EVENTS__TERM_TYPE_NAME:
		CHECK_TYPE_VAL(STR);
		break;
	default:
		return -EINVAL;
	}

	return 0;
#undef CHECK_TYPE_VAL
}

static int config_attr(struct perf_event_attr *attr,
		       struct list_head *head, int fail)
{
	struct parse_events_term *term;

	list_for_each_entry(term, head, list)
		if (config_term(attr, term) && fail)
			return -EINVAL;

	return 0;
}

int parse_events_add_numeric(struct list_head *list, int *idx,
			     u32 type, u64 config,
			     struct list_head *head_config)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = type;
	attr.config = config;

	if (head_config &&
	    config_attr(&attr, head_config, 1))
		return -EINVAL;

	return add_event(list, idx, &attr, NULL);
}

static int parse_events__is_name_term(struct parse_events_term *term)
{
	return term->type_term == PARSE_EVENTS__TERM_TYPE_NAME;
}

static char *pmu_event_name(struct list_head *head_terms)
{
	struct parse_events_term *term;

	list_for_each_entry(term, head_terms, list)
		if (parse_events__is_name_term(term))
			return term->val.str;

	return NULL;
}

int parse_events_add_pmu(struct list_head *list, int *idx,
			 char *name, struct list_head *head_config)
{
	struct perf_event_attr attr;
	struct perf_pmu_info info;
	struct perf_pmu *pmu;
	struct perf_evsel *evsel;

	pmu = perf_pmu__find(name);
	if (!pmu)
		return -EINVAL;

	if (pmu->default_config) {
		memcpy(&attr, pmu->default_config,
		       sizeof(struct perf_event_attr));
	} else {
		memset(&attr, 0, sizeof(attr));
	}

	if (!head_config) {
		attr.type = pmu->type;
		evsel = __add_event(list, idx, &attr, NULL, pmu->cpus);
		return evsel ? 0 : -ENOMEM;
	}

	if (perf_pmu__check_alias(pmu, head_config, &info))
		return -EINVAL;

	/*
	 * Configure hardcoded terms first, no need to check
	 * return value when called with fail == 0 ;)
	 */
	config_attr(&attr, head_config, 0);

	if (perf_pmu__config(pmu, &attr, head_config))
		return -EINVAL;

	evsel = __add_event(list, idx, &attr, pmu_event_name(head_config),
			    pmu->cpus);
	if (evsel) {
		evsel->unit = info.unit;
		evsel->scale = info.scale;
	}

	return evsel ? 0 : -ENOMEM;
}

int parse_events__modifier_group(struct list_head *list,
				 char *event_mod)
{
	return parse_events__modifier_event(list, event_mod, true);
}

void parse_events__set_leader(char *name, struct list_head *list)
{
	struct perf_evsel *leader;

	__perf_evlist__set_leader(list);
	leader = list_entry(list->next, struct perf_evsel, node);
	leader->group_name = name ? strdup(name) : NULL;
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
	int precise;
	int exclude_GH;
	int sample_read;
	int pinned;
};

static int get_event_modifier(struct event_modifier *mod, char *str,
			       struct perf_evsel *evsel)
{
	int eu = evsel ? evsel->attr.exclude_user : 0;
	int ek = evsel ? evsel->attr.exclude_kernel : 0;
	int eh = evsel ? evsel->attr.exclude_hv : 0;
	int eH = evsel ? evsel->attr.exclude_host : 0;
	int eG = evsel ? evsel->attr.exclude_guest : 0;
	int precise = evsel ? evsel->attr.precise_ip : 0;
	int sample_read = 0;
	int pinned = evsel ? evsel->attr.pinned : 0;

	int exclude = eu | ek | eh;
	int exclude_GH = evsel ? evsel->exclude_GH : 0;

	memset(mod, 0, sizeof(*mod));

	while (*str) {
		if (*str == 'u') {
			if (!exclude)
				exclude = eu = ek = eh = 1;
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
		} else if (*str == 'p') {
			precise++;
			/* use of precise requires exclude_guest */
			if (!exclude_GH)
				eG = 1;
		} else if (*str == 'S') {
			sample_read = 1;
		} else if (*str == 'D') {
			pinned = 1;
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
	mod->precise = precise;
	mod->exclude_GH = exclude_GH;
	mod->sample_read = sample_read;
	mod->pinned = pinned;

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
	if (strlen(str) > (sizeof("ukhGHpppSD") - 1))
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
	struct perf_evsel *evsel;
	struct event_modifier mod;

	if (str == NULL)
		return 0;

	if (check_modifier(str))
		return -EINVAL;

	if (!add && get_event_modifier(&mod, str, NULL))
		return -EINVAL;

	__evlist__for_each(list, evsel) {
		if (add && get_event_modifier(&mod, str, evsel))
			return -EINVAL;

		evsel->attr.exclude_user   = mod.eu;
		evsel->attr.exclude_kernel = mod.ek;
		evsel->attr.exclude_hv     = mod.eh;
		evsel->attr.precise_ip     = mod.precise;
		evsel->attr.exclude_host   = mod.eH;
		evsel->attr.exclude_guest  = mod.eG;
		evsel->exclude_GH          = mod.exclude_GH;
		evsel->sample_read         = mod.sample_read;

		if (perf_evsel__is_group_leader(evsel))
			evsel->attr.pinned = mod.pinned;
	}

	return 0;
}

int parse_events_name(struct list_head *list, char *name)
{
	struct perf_evsel *evsel;

	__evlist__for_each(list, evsel) {
		if (!evsel->name)
			evsel->name = strdup(name);
	}

	return 0;
}

static int
comp_pmu(const void *p1, const void *p2)
{
	struct perf_pmu_event_symbol *pmu1 = (struct perf_pmu_event_symbol *) p1;
	struct perf_pmu_event_symbol *pmu2 = (struct perf_pmu_event_symbol *) p2;

	return strcmp(pmu1->symbol, pmu2->symbol);
}

static void perf_pmu__parse_cleanup(void)
{
	if (perf_pmu_events_list_num > 0) {
		struct perf_pmu_event_symbol *p;
		int i;

		for (i = 0; i < perf_pmu_events_list_num; i++) {
			p = perf_pmu_events_list + i;
			free(p->symbol);
		}
		free(perf_pmu_events_list);
		perf_pmu_events_list = NULL;
		perf_pmu_events_list_num = 0;
	}
}

#define SET_SYMBOL(str, stype)		\
do {					\
	p->symbol = str;		\
	if (!p->symbol)			\
		goto err;		\
	p->type = stype;		\
} while (0)

/*
 * Read the pmu events list from sysfs
 * Save it into perf_pmu_events_list
 */
static void perf_pmu__parse_init(void)
{

	struct perf_pmu *pmu = NULL;
	struct perf_pmu_alias *alias;
	int len = 0;

	pmu = perf_pmu__find("cpu");
	if ((pmu == NULL) || list_empty(&pmu->aliases)) {
		perf_pmu_events_list_num = -1;
		return;
	}
	list_for_each_entry(alias, &pmu->aliases, list) {
		if (strchr(alias->name, '-'))
			len++;
		len++;
	}
	perf_pmu_events_list = malloc(sizeof(struct perf_pmu_event_symbol) * len);
	if (!perf_pmu_events_list)
		return;
	perf_pmu_events_list_num = len;

	len = 0;
	list_for_each_entry(alias, &pmu->aliases, list) {
		struct perf_pmu_event_symbol *p = perf_pmu_events_list + len;
		char *tmp = strchr(alias->name, '-');

		if (tmp != NULL) {
			SET_SYMBOL(strndup(alias->name, tmp - alias->name),
					PMU_EVENT_SYMBOL_PREFIX);
			p++;
			SET_SYMBOL(strdup(++tmp), PMU_EVENT_SYMBOL_SUFFIX);
			len += 2;
		} else {
			SET_SYMBOL(strdup(alias->name), PMU_EVENT_SYMBOL);
			len++;
		}
	}
	qsort(perf_pmu_events_list, len,
		sizeof(struct perf_pmu_event_symbol), comp_pmu);

	return;
err:
	perf_pmu__parse_cleanup();
}

enum perf_pmu_event_symbol_type
perf_pmu__parse_check(const char *name)
{
	struct perf_pmu_event_symbol p, *r;

	/* scan kernel pmu events from sysfs if needed */
	if (perf_pmu_events_list_num == 0)
		perf_pmu__parse_init();
	/*
	 * name "cpu" could be prefix of cpu-cycles or cpu// events.
	 * cpu-cycles has been handled by hardcode.
	 * So it must be cpu// events, not kernel pmu event.
	 */
	if ((perf_pmu_events_list_num <= 0) || !strcmp(name, "cpu"))
		return PMU_EVENT_SYMBOL_ERR;

	p.symbol = strdup(name);
	r = bsearch(&p, perf_pmu_events_list,
			(size_t) perf_pmu_events_list_num,
			sizeof(struct perf_pmu_event_symbol), comp_pmu);
	free(p.symbol);
	return r ? r->type : PMU_EVENT_SYMBOL_ERR;
}

static int parse_events__scanner(const char *str, void *data, int start_token)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	ret = parse_events_lex_init_extra(start_token, &scanner);
	if (ret)
		return ret;

	buffer = parse_events__scan_string(str, scanner);

#ifdef PARSER_DEBUG
	parse_events_debug = 1;
#endif
	ret = parse_events_parse(data, scanner);

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
	struct parse_events_terms data = {
		.terms = NULL,
	};
	int ret;

	ret = parse_events__scanner(str, &data, PE_START_TERMS);
	if (!ret) {
		list_splice(data.terms, terms);
		zfree(&data.terms);
		return 0;
	}

	if (data.terms)
		parse_events__free_terms(data.terms);
	return ret;
}

int parse_events(struct perf_evlist *evlist, const char *str)
{
	struct parse_events_evlist data = {
		.list = LIST_HEAD_INIT(data.list),
		.idx  = evlist->nr_entries,
	};
	int ret;

	ret = parse_events__scanner(str, &data, PE_START_EVENTS);
	perf_pmu__parse_cleanup();
	if (!ret) {
		int entries = data.idx - evlist->nr_entries;
		perf_evlist__splice_list_tail(evlist, &data.list, entries);
		evlist->nr_groups += data.nr_groups;
		return 0;
	}

	/*
	 * There are 2 users - builtin-record and builtin-test objects.
	 * Both call perf_evlist__delete in case of error, so we dont
	 * need to bother.
	 */
	return ret;
}

int parse_events_option(const struct option *opt, const char *str,
			int unset __maybe_unused)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;
	int ret = parse_events(evlist, str);

	if (ret) {
		fprintf(stderr, "invalid or unsupported event: '%s'\n", str);
		fprintf(stderr, "Run 'perf list' for a list of valid events\n");
	}
	return ret;
}

int parse_filter(const struct option *opt, const char *str,
		 int unset __maybe_unused)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;
	struct perf_evsel *last = NULL;

	if (evlist->nr_entries > 0)
		last = perf_evlist__last(evlist);

	if (last == NULL || last->attr.type != PERF_TYPE_TRACEPOINT) {
		fprintf(stderr,
			"--filter option should follow a -e tracepoint option\n");
		return -1;
	}

	last->filter = strdup(str);
	if (last->filter == NULL) {
		fprintf(stderr, "not enough memory to hold filter string\n");
		return -1;
	}

	return 0;
}

static const char * const event_type_descriptors[] = {
	"Hardware event",
	"Software event",
	"Tracepoint event",
	"Hardware cache event",
	"Raw hardware event descriptor",
	"Hardware breakpoint",
};

/*
 * Print the events from <debugfs_mount_point>/tracing/events
 */

void print_tracepoint_events(const char *subsys_glob, const char *event_glob,
			     bool name_only)
{
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_next, *evt_next, sys_dirent, evt_dirent;
	char evt_path[MAXPATHLEN];
	char dir_path[MAXPATHLEN];
	char sbuf[STRERR_BUFSIZE];

	if (debugfs_valid_mountpoint(tracing_events_path)) {
		printf("  [ Tracepoints not available: %s ]\n",
			strerror_r(errno, sbuf, sizeof(sbuf)));
		return;
	}

	sys_dir = opendir(tracing_events_path);
	if (!sys_dir)
		return;

	for_each_subsystem(sys_dir, sys_dirent, sys_next) {
		if (subsys_glob != NULL && 
		    !strglobmatch(sys_dirent.d_name, subsys_glob))
			continue;

		snprintf(dir_path, MAXPATHLEN, "%s/%s", tracing_events_path,
			 sys_dirent.d_name);
		evt_dir = opendir(dir_path);
		if (!evt_dir)
			continue;

		for_each_event(sys_dirent, evt_dir, evt_dirent, evt_next) {
			if (event_glob != NULL && 
			    !strglobmatch(evt_dirent.d_name, event_glob))
				continue;

			if (name_only) {
				printf("%s:%s ", sys_dirent.d_name, evt_dirent.d_name);
				continue;
			}

			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent.d_name, evt_dirent.d_name);
			printf("  %-50s [%s]\n", evt_path,
				event_type_descriptors[PERF_TYPE_TRACEPOINT]);
		}
		closedir(evt_dir);
	}
	closedir(sys_dir);
}

/*
 * Check whether event is in <debugfs_mount_point>/tracing/events
 */

int is_valid_tracepoint(const char *event_string)
{
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_next, *evt_next, sys_dirent, evt_dirent;
	char evt_path[MAXPATHLEN];
	char dir_path[MAXPATHLEN];

	if (debugfs_valid_mountpoint(tracing_events_path))
		return 0;

	sys_dir = opendir(tracing_events_path);
	if (!sys_dir)
		return 0;

	for_each_subsystem(sys_dir, sys_dirent, sys_next) {

		snprintf(dir_path, MAXPATHLEN, "%s/%s", tracing_events_path,
			 sys_dirent.d_name);
		evt_dir = opendir(dir_path);
		if (!evt_dir)
			continue;

		for_each_event(sys_dirent, evt_dir, evt_dirent, evt_next) {
			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent.d_name, evt_dirent.d_name);
			if (!strcmp(evt_path, event_string)) {
				closedir(evt_dir);
				closedir(sys_dir);
				return 1;
			}
		}
		closedir(evt_dir);
	}
	closedir(sys_dir);
	return 0;
}

static bool is_event_supported(u8 type, unsigned config)
{
	bool ret = true;
	int open_return;
	struct perf_evsel *evsel;
	struct perf_event_attr attr = {
		.type = type,
		.config = config,
		.disabled = 1,
	};
	struct {
		struct thread_map map;
		int threads[1];
	} tmap = {
		.map.nr	 = 1,
		.threads = { 0 },
	};

	evsel = perf_evsel__new(&attr);
	if (evsel) {
		open_return = perf_evsel__open(evsel, NULL, &tmap.map);
		ret = open_return >= 0;

		if (open_return == -EACCES) {
			/*
			 * This happens if the paranoid value
			 * /proc/sys/kernel/perf_event_paranoid is set to 2
			 * Re-run with exclude_kernel set; we don't do that
			 * by default as some ARM machines do not support it.
			 *
			 */
			evsel->attr.exclude_kernel = 1;
			ret = perf_evsel__open(evsel, NULL, &tmap.map) >= 0;
		}
		perf_evsel__delete(evsel);
	}

	return ret;
}

static void __print_events_type(u8 type, struct event_symbol *syms,
				unsigned max)
{
	char name[64];
	unsigned i;

	for (i = 0; i < max ; i++, syms++) {
		if (!is_event_supported(type, i))
			continue;

		if (strlen(syms->alias))
			snprintf(name, sizeof(name),  "%s OR %s",
				 syms->symbol, syms->alias);
		else
			snprintf(name, sizeof(name), "%s", syms->symbol);

		printf("  %-50s [%s]\n", name, event_type_descriptors[type]);
	}
}

void print_events_type(u8 type)
{
	if (type == PERF_TYPE_SOFTWARE)
		__print_events_type(type, event_symbols_sw, PERF_COUNT_SW_MAX);
	else
		__print_events_type(type, event_symbols_hw, PERF_COUNT_HW_MAX);
}

int print_hwcache_events(const char *event_glob, bool name_only)
{
	unsigned int type, op, i, printed = 0;
	char name[64];

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				if (event_glob != NULL && !strglobmatch(name, event_glob))
					continue;

				if (!is_event_supported(PERF_TYPE_HW_CACHE,
							type | (op << 8) | (i << 16)))
					continue;

				if (name_only)
					printf("%s ", name);
				else
					printf("  %-50s [%s]\n", name,
					       event_type_descriptors[PERF_TYPE_HW_CACHE]);
				++printed;
			}
		}
	}

	if (printed)
		printf("\n");
	return printed;
}

static void print_symbol_events(const char *event_glob, unsigned type,
				struct event_symbol *syms, unsigned max,
				bool name_only)
{
	unsigned i, printed = 0;
	char name[MAX_NAME_LEN];

	for (i = 0; i < max; i++, syms++) {

		if (event_glob != NULL && 
		    !(strglobmatch(syms->symbol, event_glob) ||
		      (syms->alias && strglobmatch(syms->alias, event_glob))))
			continue;

		if (!is_event_supported(type, i))
			continue;

		if (name_only) {
			printf("%s ", syms->symbol);
			continue;
		}

		if (strlen(syms->alias))
			snprintf(name, MAX_NAME_LEN, "%s OR %s", syms->symbol, syms->alias);
		else
			strncpy(name, syms->symbol, MAX_NAME_LEN);

		printf("  %-50s [%s]\n", name, event_type_descriptors[type]);

		printed++;
	}

	if (printed)
		printf("\n");
}

/*
 * Print the help text for the event symbols:
 */
void print_events(const char *event_glob, bool name_only)
{
	if (!name_only) {
		printf("\n");
		printf("List of pre-defined events (to be used in -e):\n");
	}

	print_symbol_events(event_glob, PERF_TYPE_HARDWARE,
			    event_symbols_hw, PERF_COUNT_HW_MAX, name_only);

	print_symbol_events(event_glob, PERF_TYPE_SOFTWARE,
			    event_symbols_sw, PERF_COUNT_SW_MAX, name_only);

	print_hwcache_events(event_glob, name_only);

	print_pmu_events(event_glob, name_only);

	if (event_glob != NULL)
		return;

	if (!name_only) {
		printf("  %-50s [%s]\n",
		       "rNNN",
		       event_type_descriptors[PERF_TYPE_RAW]);
		printf("  %-50s [%s]\n",
		       "cpu/t1=v1[,t2=v2,t3 ...]/modifier",
		       event_type_descriptors[PERF_TYPE_RAW]);
		printf("   (see 'man perf-list' on how to encode it)\n");
		printf("\n");

		printf("  %-50s [%s]\n",
		       "mem:<addr>[/len][:access]",
			event_type_descriptors[PERF_TYPE_BREAKPOINT]);
		printf("\n");
	}

	print_tracepoint_events(NULL, NULL, name_only);
}

int parse_events__is_hardcoded_term(struct parse_events_term *term)
{
	return term->type_term != PARSE_EVENTS__TERM_TYPE_USER;
}

static int new_term(struct parse_events_term **_term, int type_val,
		    int type_term, char *config,
		    char *str, u64 num)
{
	struct parse_events_term *term;

	term = zalloc(sizeof(*term));
	if (!term)
		return -ENOMEM;

	INIT_LIST_HEAD(&term->list);
	term->type_val  = type_val;
	term->type_term = type_term;
	term->config = config;

	switch (type_val) {
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
			   int type_term, char *config, u64 num)
{
	return new_term(term, PARSE_EVENTS__TERM_TYPE_NUM, type_term,
			config, NULL, num);
}

int parse_events_term__str(struct parse_events_term **term,
			   int type_term, char *config, char *str)
{
	return new_term(term, PARSE_EVENTS__TERM_TYPE_STR, type_term,
			config, str, 0);
}

int parse_events_term__sym_hw(struct parse_events_term **term,
			      char *config, unsigned idx)
{
	struct event_symbol *sym;

	BUG_ON(idx >= PERF_COUNT_HW_MAX);
	sym = &event_symbols_hw[idx];

	if (config)
		return new_term(term, PARSE_EVENTS__TERM_TYPE_STR,
				PARSE_EVENTS__TERM_TYPE_USER, config,
				(char *) sym->symbol, 0);
	else
		return new_term(term, PARSE_EVENTS__TERM_TYPE_STR,
				PARSE_EVENTS__TERM_TYPE_USER,
				(char *) "event", (char *) sym->symbol, 0);
}

int parse_events_term__clone(struct parse_events_term **new,
			     struct parse_events_term *term)
{
	return new_term(new, term->type_val, term->type_term, term->config,
			term->val.str, term->val.num);
}

void parse_events__free_terms(struct list_head *terms)
{
	struct parse_events_term *term, *h;

	list_for_each_entry_safe(term, h, terms, list)
		free(term);
}
