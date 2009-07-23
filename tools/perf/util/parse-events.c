
#include "../perf.h"
#include "util.h"
#include "parse-options.h"
#include "parse-events.h"
#include "exec_cmd.h"
#include "string.h"
#include "cache.h"

extern char *strcasestr(const char *haystack, const char *needle);

int					nr_counters;

struct perf_counter_attr		attrs[MAX_COUNTERS];

struct event_symbol {
	u8	type;
	u64	config;
	char	*symbol;
	char	*alias;
};

char debugfs_path[MAXPATHLEN];

#define CHW(x) .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_##x
#define CSW(x) .type = PERF_TYPE_SOFTWARE, .config = PERF_COUNT_SW_##x

static struct event_symbol event_symbols[] = {
  { CHW(CPU_CYCLES),		"cpu-cycles",		"cycles"	},
  { CHW(INSTRUCTIONS),		"instructions",		""		},
  { CHW(CACHE_REFERENCES),	"cache-references",	""		},
  { CHW(CACHE_MISSES),		"cache-misses",		""		},
  { CHW(BRANCH_INSTRUCTIONS),	"branch-instructions",	"branches"	},
  { CHW(BRANCH_MISSES),		"branch-misses",	""		},
  { CHW(BUS_CYCLES),		"bus-cycles",		""		},

  { CSW(CPU_CLOCK),		"cpu-clock",		""		},
  { CSW(TASK_CLOCK),		"task-clock",		""		},
  { CSW(PAGE_FAULTS),		"page-faults",		"faults"	},
  { CSW(PAGE_FAULTS_MIN),	"minor-faults",		""		},
  { CSW(PAGE_FAULTS_MAJ),	"major-faults",		""		},
  { CSW(CONTEXT_SWITCHES),	"context-switches",	"cs"		},
  { CSW(CPU_MIGRATIONS),	"cpu-migrations",	"migrations"	},
};

#define __PERF_COUNTER_FIELD(config, name) \
	((config & PERF_COUNTER_##name##_MASK) >> PERF_COUNTER_##name##_SHIFT)

#define PERF_COUNTER_RAW(config)	__PERF_COUNTER_FIELD(config, RAW)
#define PERF_COUNTER_CONFIG(config)	__PERF_COUNTER_FIELD(config, CONFIG)
#define PERF_COUNTER_TYPE(config)	__PERF_COUNTER_FIELD(config, TYPE)
#define PERF_COUNTER_ID(config)		__PERF_COUNTER_FIELD(config, EVENT)

static char *hw_event_names[] = {
	"cycles",
	"instructions",
	"cache-references",
	"cache-misses",
	"branches",
	"branch-misses",
	"bus-cycles",
};

static char *sw_event_names[] = {
	"cpu-clock-msecs",
	"task-clock-msecs",
	"page-faults",
	"context-switches",
	"CPU-migrations",
	"minor-faults",
	"major-faults",
};

#define MAX_ALIASES 8

static char *hw_cache[][MAX_ALIASES] = {
 { "L1-dcache",	"l1-d",		"l1d",		"L1-data",		},
 { "L1-icache",	"l1-i",		"l1i",		"L1-instruction",	},
 { "LLC",	"L2"							},
 { "dTLB",	"d-tlb",	"Data-TLB",				},
 { "iTLB",	"i-tlb",	"Instruction-TLB",			},
 { "branch",	"branches",	"bpu",		"btb",		"bpc",	},
};

static char *hw_cache_op[][MAX_ALIASES] = {
 { "load",	"loads",	"read",					},
 { "store",	"stores",	"write",				},
 { "prefetch",	"prefetches",	"speculative-read", "speculative-load",	},
};

static char *hw_cache_result[][MAX_ALIASES] = {
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
};

#define for_each_subsystem(sys_dir, sys_dirent, sys_next, file, st)	       \
	while (!readdir_r(sys_dir, &sys_dirent, &sys_next) && sys_next)	       \
	if (snprintf(file, MAXPATHLEN, "%s/%s", debugfs_path,	       	       \
			sys_dirent.d_name) &&		       		       \
	   (!stat(file, &st)) && (S_ISDIR(st.st_mode)) &&		       \
	   (strcmp(sys_dirent.d_name, ".")) &&				       \
	   (strcmp(sys_dirent.d_name, "..")))

#define for_each_event(sys_dirent, evt_dir, evt_dirent, evt_next, file, st)    \
	while (!readdir_r(evt_dir, &evt_dirent, &evt_next) && evt_next)        \
	if (snprintf(file, MAXPATHLEN, "%s/%s/%s", debugfs_path,	       \
		     sys_dirent.d_name, evt_dirent.d_name) &&		       \
	   (!stat(file, &st)) && (S_ISDIR(st.st_mode)) &&		       \
	   (strcmp(evt_dirent.d_name, ".")) &&				       \
	   (strcmp(evt_dirent.d_name, "..")))

#define MAX_EVENT_LENGTH 30

int valid_debugfs_mount(const char *debugfs)
{
	struct statfs st_fs;

	if (statfs(debugfs, &st_fs) < 0)
		return -ENOENT;
	else if (st_fs.f_type != (long) DEBUGFS_MAGIC)
		return -ENOENT;
	return 0;
}

static char *tracepoint_id_to_name(u64 config)
{
	static char tracepoint_name[2 * MAX_EVENT_LENGTH];
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_next, *evt_next, sys_dirent, evt_dirent;
	struct stat st;
	char id_buf[4];
	int fd;
	u64 id;
	char evt_path[MAXPATHLEN];

	if (valid_debugfs_mount(debugfs_path))
		return "unkown";

	sys_dir = opendir(debugfs_path);
	if (!sys_dir)
		goto cleanup;

	for_each_subsystem(sys_dir, sys_dirent, sys_next, evt_path, st) {
		evt_dir = opendir(evt_path);
		if (!evt_dir)
			goto cleanup;
		for_each_event(sys_dirent, evt_dir, evt_dirent, evt_next,
								evt_path, st) {
			snprintf(evt_path, MAXPATHLEN, "%s/%s/%s/id",
				 debugfs_path, sys_dirent.d_name,
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
				snprintf(tracepoint_name, 2 * MAX_EVENT_LENGTH,
					"%s:%s", sys_dirent.d_name,
					evt_dirent.d_name);
				return tracepoint_name;
			}
		}
		closedir(evt_dir);
	}

cleanup:
	closedir(sys_dir);
	return "unkown";
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

char *event_name(int counter)
{
	u64 config = attrs[counter].config;
	int type = attrs[counter].type;
	static char buf[32];

	if (attrs[counter].type == PERF_TYPE_RAW) {
		sprintf(buf, "raw 0x%llx", config);
		return buf;
	}

	switch (type) {
	case PERF_TYPE_HARDWARE:
		if (config < PERF_COUNT_HW_MAX)
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
		if (config < PERF_COUNT_SW_MAX)
			return sw_event_names[config];
		return "unknown-software";

	case PERF_TYPE_TRACEPOINT:
		return tracepoint_id_to_name(config);

	default:
		break;
	}

	return "unknown";
}

static int parse_aliases(const char **str, char *names[][MAX_ALIASES], int size)
{
	int i, j;
	int n, longest = -1;

	for (i = 0; i < size; i++) {
		for (j = 0; j < MAX_ALIASES && names[i][j]; j++) {
			n = strlen(names[i][j]);
			if (n > longest && !strncasecmp(*str, names[i][j], n))
				longest = n;
		}
		if (longest > 0) {
			*str += longest;
			return i;
		}
	}

	return -1;
}

static int
parse_generic_hw_event(const char **str, struct perf_counter_attr *attr)
{
	const char *s = *str;
	int cache_type = -1, cache_op = -1, cache_result = -1;

	cache_type = parse_aliases(&s, hw_cache, PERF_COUNT_HW_CACHE_MAX);
	/*
	 * No fallback - if we cannot get a clear cache type
	 * then bail out:
	 */
	if (cache_type == -1)
		return 0;

	while ((cache_op == -1 || cache_result == -1) && *s == '-') {
		++s;

		if (cache_op == -1) {
			cache_op = parse_aliases(&s, hw_cache_op,
						PERF_COUNT_HW_CACHE_OP_MAX);
			if (cache_op >= 0) {
				if (!is_cache_op_valid(cache_type, cache_op))
					return 0;
				continue;
			}
		}

		if (cache_result == -1) {
			cache_result = parse_aliases(&s, hw_cache_result,
						PERF_COUNT_HW_CACHE_RESULT_MAX);
			if (cache_result >= 0)
				continue;
		}

		/*
		 * Can't parse this as a cache op or result, so back up
		 * to the '-'.
		 */
		--s;
		break;
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

	attr->config = cache_type | (cache_op << 8) | (cache_result << 16);
	attr->type = PERF_TYPE_HW_CACHE;

	*str = s;
	return 1;
}

static int parse_tracepoint_event(const char **strp,
				    struct perf_counter_attr *attr)
{
	const char *evt_name;
	char sys_name[MAX_EVENT_LENGTH];
	char id_buf[4];
	int fd;
	unsigned int sys_length, evt_length;
	u64 id;
	char evt_path[MAXPATHLEN];

	if (valid_debugfs_mount(debugfs_path))
		return 0;

	evt_name = strchr(*strp, ':');
	if (!evt_name)
		return 0;

	sys_length = evt_name - *strp;
	if (sys_length >= MAX_EVENT_LENGTH)
		return 0;

	strncpy(sys_name, *strp, sys_length);
	sys_name[sys_length] = '\0';
	evt_name = evt_name + 1;
	evt_length = strlen(evt_name);
	if (evt_length >= MAX_EVENT_LENGTH)
		return 0;

	snprintf(evt_path, MAXPATHLEN, "%s/%s/%s/id", debugfs_path,
		 sys_name, evt_name);
	fd = open(evt_path, O_RDONLY);
	if (fd < 0)
		return 0;

	if (read(fd, id_buf, sizeof(id_buf)) < 0) {
		close(fd);
		return 0;
	}
	close(fd);
	id = atoll(id_buf);
	attr->config = id;
	attr->type = PERF_TYPE_TRACEPOINT;
	*strp = evt_name + evt_length;
	return 1;
}

static int check_events(const char *str, unsigned int i)
{
	int n;

	n = strlen(event_symbols[i].symbol);
	if (!strncmp(str, event_symbols[i].symbol, n))
		return n;

	n = strlen(event_symbols[i].alias);
	if (n)
		if (!strncmp(str, event_symbols[i].alias, n))
			return n;
	return 0;
}

static int
parse_symbolic_event(const char **strp, struct perf_counter_attr *attr)
{
	const char *str = *strp;
	unsigned int i;
	int n;

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		n = check_events(str, i);
		if (n > 0) {
			attr->type = event_symbols[i].type;
			attr->config = event_symbols[i].config;
			*strp = str + n;
			return 1;
		}
	}
	return 0;
}

static int parse_raw_event(const char **strp, struct perf_counter_attr *attr)
{
	const char *str = *strp;
	u64 config;
	int n;

	if (*str != 'r')
		return 0;
	n = hex2u64(str + 1, &config);
	if (n > 0) {
		*strp = str + n + 1;
		attr->type = PERF_TYPE_RAW;
		attr->config = config;
		return 1;
	}
	return 0;
}

static int
parse_numeric_event(const char **strp, struct perf_counter_attr *attr)
{
	const char *str = *strp;
	char *endp;
	unsigned long type;
	u64 config;

	type = strtoul(str, &endp, 0);
	if (endp > str && type < PERF_TYPE_MAX && *endp == ':') {
		str = endp + 1;
		config = strtoul(str, &endp, 0);
		if (endp > str) {
			attr->type = type;
			attr->config = config;
			*strp = endp;
			return 1;
		}
	}
	return 0;
}

static int
parse_event_modifier(const char **strp, struct perf_counter_attr *attr)
{
	const char *str = *strp;
	int eu = 1, ek = 1, eh = 1;

	if (*str++ != ':')
		return 0;
	while (*str) {
		if (*str == 'u')
			eu = 0;
		else if (*str == 'k')
			ek = 0;
		else if (*str == 'h')
			eh = 0;
		else
			break;
		++str;
	}
	if (str >= *strp + 2) {
		*strp = str;
		attr->exclude_user   = eu;
		attr->exclude_kernel = ek;
		attr->exclude_hv     = eh;
		return 1;
	}
	return 0;
}

/*
 * Each event can have multiple symbolic names.
 * Symbolic names are (almost) exactly matched.
 */
static int parse_event_symbols(const char **str, struct perf_counter_attr *attr)
{
	if (!(parse_tracepoint_event(str, attr) ||
	      parse_raw_event(str, attr) ||
	      parse_numeric_event(str, attr) ||
	      parse_symbolic_event(str, attr) ||
	      parse_generic_hw_event(str, attr)))
		return 0;

	parse_event_modifier(str, attr);

	return 1;
}

int parse_events(const struct option *opt __used, const char *str, int unset __used)
{
	struct perf_counter_attr attr;

	for (;;) {
		if (nr_counters == MAX_COUNTERS)
			return -1;

		memset(&attr, 0, sizeof(attr));
		if (!parse_event_symbols(&str, &attr))
			return -1;

		if (!(*str == 0 || *str == ',' || isspace(*str)))
			return -1;

		attrs[nr_counters] = attr;
		nr_counters++;

		if (*str == 0)
			break;
		if (*str == ',')
			++str;
		while (isspace(*str))
			++str;
	}

	return 0;
}

static const char * const event_type_descriptors[] = {
	"",
	"Hardware event",
	"Software event",
	"Tracepoint event",
	"Hardware cache event",
};

/*
 * Print the events from <debugfs_mount_point>/tracing/events
 */

static void print_tracepoint_events(void)
{
	DIR *sys_dir, *evt_dir;
	struct dirent *sys_next, *evt_next, sys_dirent, evt_dirent;
	struct stat st;
	char evt_path[MAXPATHLEN];

	if (valid_debugfs_mount(debugfs_path))
		return;

	sys_dir = opendir(debugfs_path);
	if (!sys_dir)
		goto cleanup;

	for_each_subsystem(sys_dir, sys_dirent, sys_next, evt_path, st) {
		evt_dir = opendir(evt_path);
		if (!evt_dir)
			goto cleanup;
		for_each_event(sys_dirent, evt_dir, evt_dirent, evt_next,
								evt_path, st) {
			snprintf(evt_path, MAXPATHLEN, "%s:%s",
				 sys_dirent.d_name, evt_dirent.d_name);
			fprintf(stderr, "  %-40s [%s]\n", evt_path,
				event_type_descriptors[PERF_TYPE_TRACEPOINT+1]);
		}
		closedir(evt_dir);
	}

cleanup:
	closedir(sys_dir);
}

/*
 * Print the help text for the event symbols:
 */
void print_events(void)
{
	struct event_symbol *syms = event_symbols;
	unsigned int i, type, op, prev_type = -1;
	char name[40];

	fprintf(stderr, "\n");
	fprintf(stderr, "List of pre-defined events (to be used in -e):\n");

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++, syms++) {
		type = syms->type + 1;
		if (type >= ARRAY_SIZE(event_type_descriptors))
			type = 0;

		if (type != prev_type)
			fprintf(stderr, "\n");

		if (strlen(syms->alias))
			sprintf(name, "%s OR %s", syms->symbol, syms->alias);
		else
			strcpy(name, syms->symbol);
		fprintf(stderr, "  %-40s [%s]\n", name,
			event_type_descriptors[type]);

		prev_type = type;
	}

	fprintf(stderr, "\n");
	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				fprintf(stderr, "  %-40s [%s]\n",
					event_cache_name(type, op, i),
					event_type_descriptors[4]);
			}
		}
	}

	fprintf(stderr, "\n");
	fprintf(stderr, "  %-40s [raw hardware event descriptor]\n",
		"rNNN");
	fprintf(stderr, "\n");

	print_tracepoint_events();

	exit(129);
}
