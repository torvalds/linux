// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "builtin.h"
#include "perf.h"

#include <subcmd/parse-options.h>
#include "util/auxtrace.h"
#include "util/trace-event.h"
#include "util/tool.h"
#include "util/session.h"
#include "util/data.h"
#include "util/map_symbol.h"
#include "util/mem-events.h"
#include "util/debug.h"
#include "util/dso.h"
#include "util/map.h"
#include "util/symbol.h"
#include "util/pmu.h"
#include "util/pmu-hybrid.h"
#include <linux/err.h>

#define MEM_OPERATION_LOAD	0x1
#define MEM_OPERATION_STORE	0x2

struct perf_mem {
	struct perf_tool	tool;
	char const		*input_name;
	bool			hide_unresolved;
	bool			dump_raw;
	bool			force;
	bool			phys_addr;
	bool			data_page_size;
	int			operation;
	const char		*cpu_list;
	DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);
};

static int parse_record_events(const struct option *opt,
			       const char *str, int unset __maybe_unused)
{
	struct perf_mem *mem = *(struct perf_mem **)opt->value;

	if (!strcmp(str, "list")) {
		perf_mem_events__list();
		exit(0);
	}
	if (perf_mem_events__parse(str))
		exit(-1);

	mem->operation = 0;
	return 0;
}

static const char * const __usage[] = {
	"perf mem record [<options>] [<command>]",
	"perf mem record [<options>] -- <command> [<options>]",
	NULL
};

static const char * const *record_mem_usage = __usage;

static int __cmd_record(int argc, const char **argv, struct perf_mem *mem)
{
	int rec_argc, i = 0, j, tmp_nr = 0;
	int start, end;
	const char **rec_argv;
	char **rec_tmp;
	int ret;
	bool all_user = false, all_kernel = false;
	struct perf_mem_event *e;
	struct option options[] = {
	OPT_CALLBACK('e', "event", &mem, "event",
		     "event selector. use 'perf mem record -e list' to list available events",
		     parse_record_events),
	OPT_UINTEGER(0, "ldlat", &perf_mem_events__loads_ldlat, "mem-loads latency"),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('U', "all-user", &all_user, "collect only user level data"),
	OPT_BOOLEAN('K', "all-kernel", &all_kernel, "collect only kernel level data"),
	OPT_END()
	};

	if (perf_mem_events__init()) {
		pr_err("failed: memory events not supported\n");
		return -1;
	}

	argc = parse_options(argc, argv, options, record_mem_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	if (!perf_pmu__has_hybrid())
		rec_argc = argc + 9; /* max number of arguments */
	else
		rec_argc = argc + 9 * perf_pmu__hybrid_pmu_num();

	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_argv)
		return -1;

	/*
	 * Save the allocated event name strings.
	 */
	rec_tmp = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_tmp) {
		free(rec_argv);
		return -1;
	}

	rec_argv[i++] = "record";

	e = perf_mem_events__ptr(PERF_MEM_EVENTS__LOAD_STORE);

	/*
	 * The load and store operations are required, use the event
	 * PERF_MEM_EVENTS__LOAD_STORE if it is supported.
	 */
	if (e->tag &&
	    (mem->operation & MEM_OPERATION_LOAD) &&
	    (mem->operation & MEM_OPERATION_STORE)) {
		e->record = true;
	} else {
		if (mem->operation & MEM_OPERATION_LOAD) {
			e = perf_mem_events__ptr(PERF_MEM_EVENTS__LOAD);
			e->record = true;
		}

		if (mem->operation & MEM_OPERATION_STORE) {
			e = perf_mem_events__ptr(PERF_MEM_EVENTS__STORE);
			e->record = true;
		}
	}

	e = perf_mem_events__ptr(PERF_MEM_EVENTS__LOAD);
	if (e->record)
		rec_argv[i++] = "-W";

	rec_argv[i++] = "-d";

	if (mem->phys_addr)
		rec_argv[i++] = "--phys-data";

	if (mem->data_page_size)
		rec_argv[i++] = "--data-page-size";

	start = i;
	ret = perf_mem_events__record_args(rec_argv, &i, rec_tmp, &tmp_nr);
	if (ret)
		goto out;
	end = i;

	if (all_user)
		rec_argv[i++] = "--all-user";

	if (all_kernel)
		rec_argv[i++] = "--all-kernel";

	for (j = 0; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	if (verbose > 0) {
		pr_debug("calling: record ");

		for (j = start; j < end; j++)
			pr_debug("%s ", rec_argv[j]);

		pr_debug("\n");
	}

	ret = cmd_record(i, rec_argv);
out:
	for (i = 0; i < tmp_nr; i++)
		free(rec_tmp[i]);

	free(rec_tmp);
	free(rec_argv);
	return ret;
}

static int
dump_raw_samples(struct perf_tool *tool,
		 union perf_event *event,
		 struct perf_sample *sample,
		 struct machine *machine)
{
	struct perf_mem *mem = container_of(tool, struct perf_mem, tool);
	struct addr_location al;
	const char *fmt, *field_sep;
	char str[PAGE_SIZE_NAME_LEN];

	if (machine__resolve(machine, &al, sample) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
				event->header.type);
		return -1;
	}

	if (al.filtered || (mem->hide_unresolved && al.sym == NULL))
		goto out_put;

	if (al.map != NULL)
		al.map->dso->hit = 1;

	field_sep = symbol_conf.field_sep;
	if (field_sep) {
		fmt = "%d%s%d%s0x%"PRIx64"%s0x%"PRIx64"%s";
	} else {
		fmt = "%5d%s%5d%s0x%016"PRIx64"%s0x016%"PRIx64"%s";
		symbol_conf.field_sep = " ";
	}
	printf(fmt,
		sample->pid,
		symbol_conf.field_sep,
		sample->tid,
		symbol_conf.field_sep,
		sample->ip,
		symbol_conf.field_sep,
		sample->addr,
		symbol_conf.field_sep);

	if (mem->phys_addr) {
		printf("0x%016"PRIx64"%s",
			sample->phys_addr,
			symbol_conf.field_sep);
	}

	if (mem->data_page_size) {
		printf("%s%s",
			get_page_size_name(sample->data_page_size, str),
			symbol_conf.field_sep);
	}

	if (field_sep)
		fmt = "%"PRIu64"%s0x%"PRIx64"%s%s:%s\n";
	else
		fmt = "%5"PRIu64"%s0x%06"PRIx64"%s%s:%s\n";

	printf(fmt,
		sample->weight,
		symbol_conf.field_sep,
		sample->data_src,
		symbol_conf.field_sep,
		al.map ? (al.map->dso ? al.map->dso->long_name : "???") : "???",
		al.sym ? al.sym->name : "???");
out_put:
	addr_location__put(&al);
	return 0;
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct evsel *evsel __maybe_unused,
				struct machine *machine)
{
	return dump_raw_samples(tool, event, sample, machine);
}

static int report_raw_events(struct perf_mem *mem)
{
	struct itrace_synth_opts itrace_synth_opts = {
		.set = true,
		.mem = true,	/* Only enable memory event */
		.default_no_sample = true,
	};

	struct perf_data data = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = mem->force,
	};
	int ret;
	struct perf_session *session = perf_session__new(&data, false,
							 &mem->tool);

	if (IS_ERR(session))
		return PTR_ERR(session);

	session->itrace_synth_opts = &itrace_synth_opts;

	if (mem->cpu_list) {
		ret = perf_session__cpu_bitmap(session, mem->cpu_list,
					       mem->cpu_bitmap);
		if (ret < 0)
			goto out_delete;
	}

	ret = symbol__init(&session->header.env);
	if (ret < 0)
		goto out_delete;

	printf("# PID, TID, IP, ADDR, ");

	if (mem->phys_addr)
		printf("PHYS ADDR, ");

	if (mem->data_page_size)
		printf("DATA PAGE SIZE, ");

	printf("LOCAL WEIGHT, DSRC, SYMBOL\n");

	ret = perf_session__process_events(session);

out_delete:
	perf_session__delete(session);
	return ret;
}
static char *get_sort_order(struct perf_mem *mem)
{
	bool has_extra_options = (mem->phys_addr | mem->data_page_size) ? true : false;
	char sort[128];

	/*
	 * there is no weight (cost) associated with stores, so don't print
	 * the column
	 */
	if (!(mem->operation & MEM_OPERATION_LOAD)) {
		strcpy(sort, "--sort=mem,sym,dso,symbol_daddr,"
			     "dso_daddr,tlb,locked");
	} else if (has_extra_options) {
		strcpy(sort, "--sort=local_weight,mem,sym,dso,symbol_daddr,"
			     "dso_daddr,snoop,tlb,locked,blocked");
	} else
		return NULL;

	if (mem->phys_addr)
		strcat(sort, ",phys_daddr");

	if (mem->data_page_size)
		strcat(sort, ",data_page_size");

	return strdup(sort);
}

static int report_events(int argc, const char **argv, struct perf_mem *mem)
{
	const char **rep_argv;
	int ret, i = 0, j, rep_argc;
	char *new_sort_order;

	if (mem->dump_raw)
		return report_raw_events(mem);

	rep_argc = argc + 3;
	rep_argv = calloc(rep_argc + 1, sizeof(char *));
	if (!rep_argv)
		return -1;

	rep_argv[i++] = "report";
	rep_argv[i++] = "--mem-mode";
	rep_argv[i++] = "-n"; /* display number of samples */

	new_sort_order = get_sort_order(mem);
	if (new_sort_order)
		rep_argv[i++] = new_sort_order;

	for (j = 1; j < argc; j++, i++)
		rep_argv[i] = argv[j];

	ret = cmd_report(i, rep_argv);
	free(rep_argv);
	return ret;
}

struct mem_mode {
	const char *name;
	int mode;
};

#define MEM_OPT(n, m) \
	{ .name = n, .mode = (m) }

#define MEM_END { .name = NULL }

static const struct mem_mode mem_modes[]={
	MEM_OPT("load", MEM_OPERATION_LOAD),
	MEM_OPT("store", MEM_OPERATION_STORE),
	MEM_END
};

static int
parse_mem_ops(const struct option *opt, const char *str, int unset)
{
	int *mode = (int *)opt->value;
	const struct mem_mode *m;
	char *s, *os = NULL, *p;
	int ret = -1;

	if (unset)
		return 0;

	/* str may be NULL in case no arg is passed to -t */
	if (str) {
		/* because str is read-only */
		s = os = strdup(str);
		if (!s)
			return -1;

		/* reset mode */
		*mode = 0;

		for (;;) {
			p = strchr(s, ',');
			if (p)
				*p = '\0';

			for (m = mem_modes; m->name; m++) {
				if (!strcasecmp(s, m->name))
					break;
			}
			if (!m->name) {
				fprintf(stderr, "unknown sampling op %s,"
					    " check man page\n", s);
				goto error;
			}

			*mode |= m->mode;

			if (!p)
				break;

			s = p + 1;
		}
	}
	ret = 0;

	if (*mode == 0)
		*mode = MEM_OPERATION_LOAD;
error:
	free(os);
	return ret;
}

int cmd_mem(int argc, const char **argv)
{
	struct stat st;
	struct perf_mem mem = {
		.tool = {
			.sample		= process_sample_event,
			.mmap		= perf_event__process_mmap,
			.mmap2		= perf_event__process_mmap2,
			.comm		= perf_event__process_comm,
			.lost		= perf_event__process_lost,
			.fork		= perf_event__process_fork,
			.attr		= perf_event__process_attr,
			.build_id	= perf_event__process_build_id,
			.namespaces	= perf_event__process_namespaces,
			.auxtrace_info  = perf_event__process_auxtrace_info,
			.auxtrace       = perf_event__process_auxtrace,
			.auxtrace_error = perf_event__process_auxtrace_error,
			.ordered_events	= true,
		},
		.input_name		 = "perf.data",
		/*
		 * default to both load an store sampling
		 */
		.operation		 = MEM_OPERATION_LOAD | MEM_OPERATION_STORE,
	};
	const struct option mem_options[] = {
	OPT_CALLBACK('t', "type", &mem.operation,
		   "type", "memory operations(load,store) Default load,store",
		    parse_mem_ops),
	OPT_BOOLEAN('D', "dump-raw-samples", &mem.dump_raw,
		    "dump raw samples in ASCII"),
	OPT_BOOLEAN('U', "hide-unresolved", &mem.hide_unresolved,
		    "Only display entries resolved to a symbol"),
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
	OPT_STRING('C', "cpu", &mem.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_STRING_NOEMPTY('x', "field-separator", &symbol_conf.field_sep,
		   "separator",
		   "separator for columns, no spaces will be added"
		   " between columns '.' is reserved."),
	OPT_BOOLEAN('f', "force", &mem.force, "don't complain, do it"),
	OPT_BOOLEAN('p', "phys-data", &mem.phys_addr, "Record/Report sample physical addresses"),
	OPT_BOOLEAN(0, "data-page-size", &mem.data_page_size, "Record/Report sample data address page size"),
	OPT_END()
	};
	const char *const mem_subcommands[] = { "record", "report", NULL };
	const char *mem_usage[] = {
		NULL,
		NULL
	};

	argc = parse_options_subcommand(argc, argv, mem_options, mem_subcommands,
					mem_usage, PARSE_OPT_KEEP_UNKNOWN);

	if (!argc || !(strncmp(argv[0], "rec", 3) || mem.operation))
		usage_with_options(mem_usage, mem_options);

	if (!mem.input_name || !strlen(mem.input_name)) {
		if (!fstat(STDIN_FILENO, &st) && S_ISFIFO(st.st_mode))
			mem.input_name = "-";
		else
			mem.input_name = "perf.data";
	}

	if (!strncmp(argv[0], "rec", 3))
		return __cmd_record(argc, argv, &mem);
	else if (!strncmp(argv[0], "rep", 3))
		return report_events(argc, argv, &mem);
	else
		usage_with_options(mem_usage, mem_options);

	return 0;
}
