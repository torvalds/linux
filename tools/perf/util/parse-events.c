
#include "../perf.h"
#include "util.h"
#include "parse-options.h"
#include "parse-events.h"
#include "exec_cmd.h"
#include "string.h"

extern char *strcasestr(const char *haystack, const char *needle);

int					nr_counters;

struct perf_counter_attr		attrs[MAX_COUNTERS];

struct event_symbol {
	u8	type;
	u64	config;
	char	*symbol;
	char	*alias;
};

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
 { "L1-d$",	"l1-d",		"l1d",		"L1-data",		},
 { "L1-i$",	"l1-i",		"l1i",		"L1-instruction",	},
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

	default:
		break;
	}

	return "unknown";
}

static int parse_aliases(const char *str, char *names[][MAX_ALIASES], int size)
{
	int i, j;

	for (i = 0; i < size; i++) {
		for (j = 0; j < MAX_ALIASES; j++) {
			if (!names[i][j])
				break;
			if (strcasestr(str, names[i][j]))
				return i;
		}
	}

	return -1;
}

static int
parse_generic_hw_symbols(const char *str, struct perf_counter_attr *attr)
{
	int cache_type = -1, cache_op = 0, cache_result = 0;

	cache_type = parse_aliases(str, hw_cache, PERF_COUNT_HW_CACHE_MAX);
	/*
	 * No fallback - if we cannot get a clear cache type
	 * then bail out:
	 */
	if (cache_type == -1)
		return -EINVAL;

	cache_op = parse_aliases(str, hw_cache_op, PERF_COUNT_HW_CACHE_OP_MAX);
	/*
	 * Fall back to reads:
	 */
	if (cache_op == -1)
		cache_op = PERF_COUNT_HW_CACHE_OP_READ;

	if (!is_cache_op_valid(cache_type, cache_op))
		return -EINVAL;

	cache_result = parse_aliases(str, hw_cache_result,
					PERF_COUNT_HW_CACHE_RESULT_MAX);
	/*
	 * Fall back to accesses:
	 */
	if (cache_result == -1)
		cache_result = PERF_COUNT_HW_CACHE_RESULT_ACCESS;

	attr->config = cache_type | (cache_op << 8) | (cache_result << 16);
	attr->type = PERF_TYPE_HW_CACHE;

	return 0;
}

static int check_events(const char *str, unsigned int i)
{
	if (!strncmp(str, event_symbols[i].symbol,
		     strlen(event_symbols[i].symbol)))
		return 1;

	if (strlen(event_symbols[i].alias))
		if (!strncmp(str, event_symbols[i].alias,
			     strlen(event_symbols[i].alias)))
			return 1;
	return 0;
}

/*
 * Each event can have multiple symbolic names.
 * Symbolic names are (almost) exactly matched.
 */
static int parse_event_symbols(const char *str, struct perf_counter_attr *attr)
{
	u64 config, id;
	int type;
	unsigned int i;
	const char *sep, *pstr;

	if (str[0] == 'r' && hex2u64(str + 1, &config) > 0) {
		attr->type = PERF_TYPE_RAW;
		attr->config = config;

		return 0;
	}

	pstr = str;
	sep = strchr(pstr, ':');
	if (sep) {
		type = atoi(pstr);
		pstr = sep + 1;
		id = atoi(pstr);
		sep = strchr(pstr, ':');
		if (sep) {
			pstr = sep + 1;
			if (strchr(pstr, 'k'))
				attr->exclude_user = 1;
			if (strchr(pstr, 'u'))
				attr->exclude_kernel = 1;
		}
		attr->type = type;
		attr->config = id;

		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++) {
		if (check_events(str, i)) {
			attr->type = event_symbols[i].type;
			attr->config = event_symbols[i].config;

			return 0;
		}
	}

	return parse_generic_hw_symbols(str, attr);
}

int parse_events(const struct option *opt, const char *str, int unset)
{
	struct perf_counter_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
again:
	if (nr_counters == MAX_COUNTERS)
		return -1;

	ret = parse_event_symbols(str, &attr);
	if (ret < 0)
		return ret;

	attrs[nr_counters] = attr;
	nr_counters++;

	str = strstr(str, ",");
	if (str) {
		str++;
		goto again;
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
 * Print the help text for the event symbols:
 */
void print_events(void)
{
	struct event_symbol *syms = event_symbols;
	unsigned int i, type, prev_type = -1;
	char name[40];

	fprintf(stderr, "\n");
	fprintf(stderr, "List of pre-defined events (to be used in -e):\n");

	for (i = 0; i < ARRAY_SIZE(event_symbols); i++, syms++) {
		type = syms->type + 1;
		if (type > ARRAY_SIZE(event_type_descriptors))
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
	fprintf(stderr, "  %-40s [raw hardware event descriptor]\n",
		"rNNN");
	fprintf(stderr, "\n");

	exit(129);
}
