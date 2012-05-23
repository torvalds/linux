#include "../../../include/linux/hw_breakpoint.h"
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
#include "debugfs.h"
#include "parse-events-flex.h"
#include "pmu.h"

#define MAX_NAME_LEN 100

struct event_symbol {
	u8		type;
	u64		config;
	const char	*symbol;
	const char	*alias;
};

int parse_events_parse(struct list_head *list, struct list_head *list_tmp,
		       int *idx);

#define CHW(x) .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_##x
#define CSW(x) .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_##x

static struct event_symbol event_symbols[] = {
  { CHW(CPU_CYCLES),			"cpu-cycles",			"cycles"		},
  { CHW(STALLED_CYCLES_FRONTEND),	"stalled-cycles-frontend",	"idle-cycles-frontend"	},
  { CHW(STALLED_CYCLES_BACKEND),	"stalled-cycles-backend",	"idle-cycles-backend"	},
  { CHW(INSTRUCTIONS),			"instructions",			""			},
  { CHW(CACHE_REFERENCES),		"cache-references",		""			},
  { CHW(CACHE_MISSES),			"cache-misses",			""			},
  { CHW(BRANCH_INSTRUCTIONS),		"branch-instructions",		"branches"		},
  { CHW(BRANCH_MISSES),			"branch-misses",		""			},
  { CHW(BUS_CYCLES),			"bus-cycles",			""			},
  { CHW(REF_CPU_CYCLES),		"ref-cycles",			""			},

  { CSW(CPU_CLOCK),			"cpu-clock",			""			},
  { CSW(TASK_CLOCK),			"task-clock",			""			},
  { CSW(PAGE_FAULTS),			"page-faults",			"faults"		},
  { CSW(PAGE_FAULTS_MIN),		"minor-faults",			""			},
  { CSW(PAGE_FAULTS_MAJ),		"major-faults",			""			},
  { CSW(CONTEXT_SWITCHES),		"context-switches",		"cs"			},
  { CSW(CPU_MIGRATIONS),		"cpu-migrations",		"migrations"		},
  { CSW(ALIGNMENT_FAULTS),		"alignment-faults",		""			},
  { CSW(EMULATION_FAULTS),		"emulation-faults",		""			},
};

#define __PERF_EVENT_FIELD(config, name) \
	((config & PERF_EVENT_##name##_MASK) >> PERF_EVENT_##name##_SHIFT)

#define PERF_EVENT_RAW(config)		__PERF_EVENT_FIELD(config, RAW)
#define PERF_EVENT_CONFIG(config)	__PERF_EVENT_FIELD(config, CONFIG)
#define PERF_EVENT_TYPE(config)		__PERF_EVENT_FIELD(config, TYPE)
#define PERF_EVENT_ID(config)		__PERF_EVENT_FIELD(config, EVENT)

static const char *hw_event_names[PERF_COUNT_HW_MAX] = {
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

static const char *sw_event_names[PERF_COUNT_SW_MAX] = {
	"cpu-clock",
	"task-clock",
	"page-faults",
	"context-switches",
	"CPU-migrations",
	"minor-faults",
	"major-faults",
	"alignment-faults",
	"emulation-faults",
};

#define MAX_ALIASES 8

static const char *hw_cache[PERF_COUNT_HW_CACHE_MAX][MAX_ALIASES] = {
 { "L1-dcache",	"l1-d",		"l1d",		"L1-data",		},
 { "L1-icache",	"l1-i",		"l1i",		"L1-instruction",	},
 { "LLC",	"L2",							},
 { "dTLB",	"d-tlb",	"Data-TLB",				},
 { "iTLB",	"i-tlb",	"Instruction-TLB",			},
 { "branch",	"branches",	"bpu",		"btb",		"bpc",	},
 { "node",								},
};

static const char *hw_cache_op[PERF_COUNT_HW_CACHE_OP_MAX][MAX_ALIASES] = {
 { "load",	"loads",	"read",					},
 { "store",	"stores",	"write",				},
 { "prefetch",	"prefetches",	"speculative-read", "speculative-load",	},
};

static const char *hw_cache_result[PERF_COUNT_HW_CACHE_RESULT_MAX]
				  [MAX_ALIASES] = {
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
static unsigned long hw_cache_stat[C(MAX)] = {
 [C(L1D)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(L1I)]	= (CACHE_READ | CACHE_PREFETCH),
 [C(LL)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(DTLB)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
 [C(ITLB)]	= (CACHE_READ),
 [C(BPU)]	= (CACHE_READ),
 [C(NODE)]	= (CACHE_READ | CACHE_WRITE | CACHE_PREFETCH),
};

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
					free(path->system);
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

#define TP_PATH_LEN (MAX_EVENT_LENGTH * 2 + 1)
static const char *tracepoint_id_to_name(u64 config)
{
	static char buf[TP_PATH_LEN];
	struct tracepoint_path *path;

	path = tracepoint_id_to_path(config);
	if (path) {
		snprintf(buf, TP_PATH_LEN, "%s:%s", path->system, path->name);
		free(path->name);
		free(path->system);
		free(path);
	} else
		snprintf(buf, TP_PATH_LEN, "%s:%s", "unknown", "unknown");

	return buf;
}

static int is_cache_op_valid(u8 cache_type, u8 cache_op)
{
	if (hw_cache_stat[cache_type] & COP(cache_op))
		return 1;	/* valid */
	else
		return 0;	/* invalid */
}

static char *event_cache_name(u8 cache_type, u8 cache_op, u8 cache_result)
{
	static char name[50];

	if (cache_result) {
		sprintf(name, "%s-%s-%s", hw_cache[cache_type][0],
			hw_cache_op[cache_op][0],
			hw_cache_result[cache_result][0]);
	} else {
		sprintf(name, "%s-%s", hw_cache[cache_type][0],
			hw_cache_op[cache_op][1]);
	}

	return name;
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

const char *event_name(struct perf_evsel *evsel)
{
	u64 config = evsel->attr.config;
	int type = evsel->attr.type;

	if (evsel->name)
		return evsel->name;

	return __event_name(type, config);
}

const char *__event_name(int type, u64 config)
{
	static char buf[32];

	if (type == PERF_TYPE_RAW) {
		sprintf(buf, "raw 0x%" PRIx64, config);
		return buf;
	}

	switch (type) {
	case PERF_TYPE_HARDWARE:
		if (config < PERF_COUNT_HW_MAX && hw_event_names[config])
			return hw_event_names[config];
		return "unknown-hardware";

	case PERF_TYPE_HW_CACHE: {
		u8 cache_type, cache_op, cache_result;

		cache_type   = (config >>  0) & 0xff;
		if (cache_type > PERF_COUNT_HW_CACHE_MAX)
			return "unknown-ext-hardware-cache-type";

		cache_op     = (config >>  8) & 0xff;
		if (cache_op > PERF_COUNT_HW_CACHE_OP_MAX)
			return "unknown-ext-hardware-cache-op";

		cache_result = (config >> 16) & 0xff;
		if (cache_result > PERF_COUNT_HW_CACHE_RESULT_MAX)
			return "unknown-ext-hardware-cache-result";

		if (!is_cache_op_valid(cache_type, cache_op))
			return "invalid-cache";

		return event_cache_name(cache_type, cache_op, cache_result);
	}

	case PERF_TYPE_SOFTWARE:
		if (config < PERF_COUNT_SW_MAX && sw_event_names[config])
			return sw_event_names[config];
		return "unknown-software";

	case PERF_TYPE_TRACEPOINT:
		return tracepoint_id_to_name(config);

	default:
		break;
	}

	return "unknown";
}

static int add_event(struct list_head *list, int *idx,
		     struct perf_event_attr *attr, char *name)
{
	struct perf_evsel *evsel;

	event_attr_init(attr);

	evsel = perf_evsel__new(attr, (*idx)++);
	if (!evsel)
		return -ENOMEM;

	list_add_tail(&evsel->node, list);

	evsel->name = strdup(name);
	return 0;
}

static int parse_aliases(char *str, const char *names[][MAX_ALIASES], int size)
{
	int i, j;
	int n, longest = -1;

	for (i = 0; i < size; i++) {
		for (j = 0; j < MAX_ALIASES && names[i][j]; j++) {
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
	cache_type = parse_aliases(type, hw_cache,
				   PERF_COUNT_HW_CACHE_MAX);
	if (cache_type == -1)
		return -EINVAL;

	n = snprintf(name, MAX_NAME_LEN, "%s", type);

	for (i = 0; (i < 2) && (op_result[i]); i++) {
		char *str = op_result[i];

		snprintf(name + n, MAX_NAME_LEN - n, "-%s\n", str);

		if (cache_op == -1) {
			cache_op = parse_aliases(str, hw_cache_op,
						 PERF_COUNT_HW_CACHE_OP_MAX);
			if (cache_op >= 0) {
				if (!is_cache_op_valid(cache_type, cache_op))
					return -EINVAL;
				continue;
			}
		}

		if (cache_result == -1) {
			cache_result = parse_aliases(str, hw_cache_result,
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
	struct perf_event_attr attr;
	char name[MAX_NAME_LEN];
	char evt_path[MAXPATHLEN];
	char id_buf[4];
	u64 id;
	int fd;

	snprintf(evt_path, MAXPATHLEN, "%s/%s/%s/id", tracing_events_path,
		 sys_name, evt_name);

	fd = open(evt_path, O_RDONLY);
	if (fd < 0)
		return -1;

	if (read(fd, id_buf, sizeof(id_buf)) < 0) {
		close(fd);
		return -1;
	}

	close(fd);
	id = atoll(id_buf);

	memset(&attr, 0, sizeof(attr));
	attr.config = id;
	attr.type = PERF_TYPE_TRACEPOINT;
	attr.sample_type |= PERF_SAMPLE_RAW;
	attr.sample_type |= PERF_SAMPLE_TIME;
	attr.sample_type |= PERF_SAMPLE_CPU;
	attr.sample_period = 1;

	snprintf(name, MAX_NAME_LEN, "%s:%s", sys_name, evt_name);
	return add_event(list, idx, &attr, name);
}

static int add_tracepoint_multi(struct list_head *list, int *idx,
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

	return ret;
}

int parse_events_add_tracepoint(struct list_head *list, int *idx,
				char *sys, char *event)
{
	int ret;

	ret = debugfs_valid_mountpoint(tracing_events_path);
	if (ret)
		return ret;

	return strpbrk(event, "*?") ?
	       add_tracepoint_multi(list, idx, sys, event) :
	       add_tracepoint(list, idx, sys, event);
}

static int
parse_breakpoint_type(const char *type, struct perf_event_attr *attr)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (!type || !type[i])
			break;

		switch (type[i]) {
		case 'r':
			attr->bp_type |= HW_BREAKPOINT_R;
			break;
		case 'w':
			attr->bp_type |= HW_BREAKPOINT_W;
			break;
		case 'x':
			attr->bp_type |= HW_BREAKPOINT_X;
			break;
		default:
			return -EINVAL;
		}
	}

	if (!attr->bp_type) /* Default */
		attr->bp_type = HW_BREAKPOINT_R | HW_BREAKPOINT_W;

	return 0;
}

int parse_events_add_breakpoint(struct list_head *list, int *idx,
				void *ptr, char *type)
{
	struct perf_event_attr attr;
	char name[MAX_NAME_LEN];

	memset(&attr, 0, sizeof(attr));
	attr.bp_addr = (unsigned long) ptr;

	if (parse_breakpoint_type(type, &attr))
		return -EINVAL;

	/*
	 * We should find a nice way to override the access length
	 * Provide some defaults for now
	 */
	if (attr.bp_type == HW_BREAKPOINT_X)
		attr.bp_len = sizeof(long);
	else
		attr.bp_len = HW_BREAKPOINT_LEN_4;

	attr.type = PERF_TYPE_BREAKPOINT;

	snprintf(name, MAX_NAME_LEN, "mem:%p:%s", ptr, type ? type : "rw");
	return add_event(list, idx, &attr, name);
}

static int config_term(struct perf_event_attr *attr,
		       struct parse_events__term *term)
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
	default:
		return -EINVAL;
	}

	return 0;
#undef CHECK_TYPE_VAL
}

static int config_attr(struct perf_event_attr *attr,
		       struct list_head *head, int fail)
{
	struct parse_events__term *term;

	list_for_each_entry(term, head, list)
		if (config_term(attr, term) && fail)
			return -EINVAL;

	return 0;
}

int parse_events_add_numeric(struct list_head *list, int *idx,
			     unsigned long type, unsigned long config,
			     struct list_head *head_config)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = type;
	attr.config = config;

	if (head_config &&
	    config_attr(&attr, head_config, 1))
		return -EINVAL;

	return add_event(list, idx, &attr,
			 (char *) __event_name(type, config));
}

int parse_events_add_pmu(struct list_head *list, int *idx,
			 char *name, struct list_head *head_config)
{
	struct perf_event_attr attr;
	struct perf_pmu *pmu;

	pmu = perf_pmu__find(name);
	if (!pmu)
		return -EINVAL;

	memset(&attr, 0, sizeof(attr));

	/*
	 * Configure hardcoded terms first, no need to check
	 * return value when called with fail == 0 ;)
	 */
	config_attr(&attr, head_config, 0);

	if (perf_pmu__config(pmu, &attr, head_config))
		return -EINVAL;

	return add_event(list, idx, &attr, (char *) "pmu");
}

void parse_events_update_lists(struct list_head *list_event,
			       struct list_head *list_all)
{
	/*
	 * Called for single event definition. Update the
	 * 'all event' list, and reinit the 'signle event'
	 * list, for next event definition.
	 */
	list_splice_tail(list_event, list_all);
	INIT_LIST_HEAD(list_event);
}

int parse_events_modifier(struct list_head *list, char *str)
{
	struct perf_evsel *evsel;
	int exclude = 0, exclude_GH = 0;
	int eu = 0, ek = 0, eh = 0, eH = 0, eG = 0, precise = 0;

	if (str == NULL)
		return 0;

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

	list_for_each_entry(evsel, list, node) {
		evsel->attr.exclude_user   = eu;
		evsel->attr.exclude_kernel = ek;
		evsel->attr.exclude_hv     = eh;
		evsel->attr.precise_ip     = precise;
		evsel->attr.exclude_host   = eH;
		evsel->attr.exclude_guest  = eG;
	}

	return 0;
}

int parse_events(struct perf_evlist *evlist, const char *str, int unset __used)
{
	LIST_HEAD(list);
	LIST_HEAD(list_tmp);
	YY_BUFFER_STATE buffer;
	int ret, idx = evlist->nr_entries;

	buffer = parse_events__scan_string(str);

	ret = parse_events_parse(&list, &list_tmp, &idx);

	parse_events__flush_buffer(buffer);
	parse_events__delete_buffer(buffer);

	if (!ret) {
		int entries = idx - evlist->nr_entries;
		perf_evlist__splice_list_tail(evlist, &list, entries);
		return 0;
	}

	/*
	 * There are 2 users - builtin-record and builtin-test objects.
	 * Both call perf_evlist__delete in case of error, so we dont
	 * need to bother.
	 */
	fprintf(stderr, "invalid or unsupported event: '%s'\n", str);
	fprintf(stderr, "Run 'perf list' for a list of valid events\n");
	return ret;
}

int parse_events_option(const struct option *opt, const char *str,
			int unset __used)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;
	return parse_events(evlist, str, unset);
}

int parse_filter(const struct option *opt, const char *str,
		 int unset __used)
{
	struct perf_evlist *evlist = *(struct perf_evlist **)opt->value;
	struct perf_evsel *last = NULL;

	if (evlist->nr_entries > 0)
		last = list_entry(evlist->entries.prev, struct perf_evsel, node);

	if (last == NULL || last->attr.type != PERF_TYPE_TRACEPOINT) {
		fprintf(stderr,
			"-F option should follow a -e tracepoint option\n");
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

void print_tracepoint_events(const char *subsys_glob, const char *event_glob)
{
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_next, *evt_next, sys_dirent, evt_dirent;
	char evt_path[MAXPATHLEN];
	char dir_path[MAXPATHLEN];

	if (debugfs_valid_mountpoint(tracing_events_path))
		return;

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

void print_events_type(u8 type)
{
	struct event_symbol *syms = event_symbols;
	unsigned int i;
	char name[64];

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++, syms++) {
		if (type != syms->type)
			continue;

		if (strlen(syms->alias))
			snprintf(name, sizeof(name),  "%s OR %s",
				 syms->symbol, syms->alias);
		else
			snprintf(name, sizeof(name), "%s", syms->symbol);

		printf("  %-50s [%s]\n", name,
			event_type_descriptors[type]);
	}
}

int print_hwcache_events(const char *event_glob)
{
	unsigned int type, op, i, printed = 0;

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				char *name = event_cache_name(type, op, i);

				if (event_glob != NULL && !strglobmatch(name, event_glob))
					continue;

				printf("  %-50s [%s]\n", name,
					event_type_descriptors[PERF_TYPE_HW_CACHE]);
				++printed;
			}
		}
	}

	return printed;
}

/*
 * Print the help text for the event symbols:
 */
void print_events(const char *event_glob)
{
	unsigned int i, type, prev_type = -1, printed = 0, ntypes_printed = 0;
	struct event_symbol *syms = event_symbols;
	char name[MAX_NAME_LEN];

	printf("\n");
	printf("List of pre-defined events (to be used in -e):\n");

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++, syms++) {
		type = syms->type;

		if (type != prev_type && printed) {
			printf("\n");
			printed = 0;
			ntypes_printed++;
		}

		if (event_glob != NULL && 
		    !(strglobmatch(syms->symbol, event_glob) ||
		      (syms->alias && strglobmatch(syms->alias, event_glob))))
			continue;

		if (strlen(syms->alias))
			snprintf(name, MAX_NAME_LEN, "%s OR %s", syms->symbol, syms->alias);
		else
			strncpy(name, syms->symbol, MAX_NAME_LEN);
		printf("  %-50s [%s]\n", name,
			event_type_descriptors[type]);

		prev_type = type;
		++printed;
	}

	if (ntypes_printed) {
		printed = 0;
		printf("\n");
	}
	print_hwcache_events(event_glob);

	if (event_glob != NULL)
		return;

	printf("\n");
	printf("  %-50s [%s]\n",
	       "rNNN",
	       event_type_descriptors[PERF_TYPE_RAW]);
	printf("  %-50s [%s]\n",
	       "cpu/t1=v1[,t2=v2,t3 ...]/modifier",
	       event_type_descriptors[PERF_TYPE_RAW]);
	printf("   (see 'perf list --help' on how to encode it)\n");
	printf("\n");

	printf("  %-50s [%s]\n",
			"mem:<addr>[:access]",
			event_type_descriptors[PERF_TYPE_BREAKPOINT]);
	printf("\n");

	print_tracepoint_events(NULL, NULL);
}

int parse_events__is_hardcoded_term(struct parse_events__term *term)
{
	return term->type_term != PARSE_EVENTS__TERM_TYPE_USER;
}

static int new_term(struct parse_events__term **_term, int type_val,
		    int type_term, char *config,
		    char *str, long num)
{
	struct parse_events__term *term;

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
		return -EINVAL;
	}

	*_term = term;
	return 0;
}

int parse_events__term_num(struct parse_events__term **term,
			   int type_term, char *config, long num)
{
	return new_term(term, PARSE_EVENTS__TERM_TYPE_NUM, type_term,
			config, NULL, num);
}

int parse_events__term_str(struct parse_events__term **term,
			   int type_term, char *config, char *str)
{
	return new_term(term, PARSE_EVENTS__TERM_TYPE_STR, type_term,
			config, str, 0);
}

void parse_events__free_terms(struct list_head *terms)
{
	struct parse_events__term *term, *h;

	list_for_each_entry_safe(term, h, terms, list)
		free(term);

	free(terms);
}
