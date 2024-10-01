// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "builtin.h"

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
#include "util/pmus.h"
#include "util/sample.h"
#include "util/sort.h"
#include "util/string2.h"
#include "util/util.h"
#include <linux/err.h>

#define MEM_OPERATION_LOAD	0x1
#define MEM_OPERATION_STORE	0x2

struct perf_mem {
	struct perf_tool	tool;
	const char		*input_name;
	const char		*sort_key;
	bool			hide_unresolved;
	bool			dump_raw;
	bool			force;
	bool			phys_addr;
	bool			data_page_size;
	bool			all_kernel;
	bool			all_user;
	bool			data_type;
	int			operation;
	const char		*cpu_list;
	DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);
};

static int parse_record_events(const struct option *opt,
			       const char *str, int unset __maybe_unused)
{
	struct perf_mem *mem = (struct perf_mem *)opt->value;
	struct perf_pmu *pmu;

	pmu = perf_mem_events_find_pmu();
	if (!pmu) {
		pr_err("failed: there is no PMU that supports perf mem\n");
		exit(-1);
	}

	if (!strcmp(str, "list")) {
		perf_pmu__mem_events_list(pmu);
		exit(0);
	}
	if (perf_pmu__mem_events_parse(pmu, str))
		exit(-1);

	mem->operation = 0;
	return 0;
}

static int __cmd_record(int argc, const char **argv, struct perf_mem *mem,
			const struct option *options)
{
	int rec_argc, i = 0, j;
	int start, end;
	const char **rec_argv;
	int ret;
	struct perf_mem_event *e;
	struct perf_pmu *pmu;
	const char * const record_usage[] = {
		"perf mem record [<options>] [<command>]",
		"perf mem record [<options>] -- <command> [<options>]",
		NULL
	};

	pmu = perf_mem_events_find_pmu();
	if (!pmu) {
		pr_err("failed: no PMU supports the memory events\n");
		return -1;
	}

	if (perf_pmu__mem_events_init()) {
		pr_err("failed: memory events not supported\n");
		return -1;
	}

	argc = parse_options(argc, argv, options, record_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

	/* Max number of arguments multiplied by number of PMUs that can support them. */
	rec_argc = argc + 9 * (perf_pmu__mem_events_num_mem_pmus(pmu) + 1);

	if (mem->cpu_list)
		rec_argc += 2;

	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_argv)
		return -1;

	rec_argv[i++] = "record";

	e = perf_pmu__mem_events_ptr(pmu, PERF_MEM_EVENTS__LOAD_STORE);

	/*
	 * The load and store operations are required, use the event
	 * PERF_MEM_EVENTS__LOAD_STORE if it is supported.
	 */
	if (e->tag &&
	    (mem->operation & MEM_OPERATION_LOAD) &&
	    (mem->operation & MEM_OPERATION_STORE)) {
		perf_mem_record[PERF_MEM_EVENTS__LOAD_STORE] = true;
		rec_argv[i++] = "-W";
	} else {
		if (mem->operation & MEM_OPERATION_LOAD)
			perf_mem_record[PERF_MEM_EVENTS__LOAD] = true;

		if (mem->operation & MEM_OPERATION_STORE)
			perf_mem_record[PERF_MEM_EVENTS__STORE] = true;
	}

	if (perf_mem_record[PERF_MEM_EVENTS__LOAD])
		rec_argv[i++] = "-W";

	rec_argv[i++] = "-d";

	if (mem->phys_addr)
		rec_argv[i++] = "--phys-data";

	if (mem->data_page_size)
		rec_argv[i++] = "--data-page-size";

	start = i;
	ret = perf_mem_events__record_args(rec_argv, &i);
	if (ret)
		goto out;
	end = i;

	if (mem->all_user)
		rec_argv[i++] = "--all-user";

	if (mem->all_kernel)
		rec_argv[i++] = "--all-kernel";

	if (mem->cpu_list) {
		rec_argv[i++] = "-C";
		rec_argv[i++] = mem->cpu_list;
	}

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
	free(rec_argv);
	return ret;
}

static int
dump_raw_samples(const struct perf_tool *tool,
		 union perf_event *event,
		 struct perf_sample *sample,
		 struct machine *machine)
{
	struct perf_mem *mem = container_of(tool, struct perf_mem, tool);
	struct addr_location al;
	const char *fmt, *field_sep;
	char str[PAGE_SIZE_NAME_LEN];
	struct dso *dso = NULL;

	addr_location__init(&al);
	if (machine__resolve(machine, &al, sample) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
				event->header.type);
		addr_location__exit(&al);
		return -1;
	}

	if (al.filtered || (mem->hide_unresolved && al.sym == NULL))
		goto out_put;

	if (al.map != NULL) {
		dso = map__dso(al.map);
		if (dso)
			dso__set_hit(dso);
	}

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
		dso ? dso__long_name(dso) : "???",
		al.sym ? al.sym->name : "???");
out_put:
	addr_location__exit(&al);
	return 0;
}

static int process_sample_event(const struct perf_tool *tool,
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
	struct perf_session *session;

	perf_tool__init(&mem->tool, /*ordered_events=*/true);
	mem->tool.sample		= process_sample_event;
	mem->tool.mmap		= perf_event__process_mmap;
	mem->tool.mmap2		= perf_event__process_mmap2;
	mem->tool.comm		= perf_event__process_comm;
	mem->tool.lost		= perf_event__process_lost;
	mem->tool.fork		= perf_event__process_fork;
	mem->tool.attr		= perf_event__process_attr;
	mem->tool.build_id	= perf_event__process_build_id;
	mem->tool.namespaces	= perf_event__process_namespaces;
	mem->tool.auxtrace_info  = perf_event__process_auxtrace_info;
	mem->tool.auxtrace       = perf_event__process_auxtrace;
	mem->tool.auxtrace_error = perf_event__process_auxtrace_error;

	session = perf_session__new(&data, &mem->tool);

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

	if (mem->sort_key)
		scnprintf(sort, sizeof(sort), "--sort=%s", mem->sort_key);
	else if (mem->data_type)
		strcpy(sort, "--sort=mem,snoop,tlb,type");
	/*
	 * there is no weight (cost) associated with stores, so don't print
	 * the column
	 */
	else if (!(mem->operation & MEM_OPERATION_LOAD)) {
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

	/* make sure it has 'type' sort key even -s option is used */
	if (mem->data_type && !strstr(sort, "type"))
		strcat(sort, ",type");

	return strdup(sort);
}

static int __cmd_report(int argc, const char **argv, struct perf_mem *mem,
			const struct option *options)
{
	const char **rep_argv;
	int ret, i = 0, j, rep_argc;
	char *new_sort_order;
	const char * const report_usage[] = {
		"perf mem report [<options>]",
		NULL
	};

	argc = parse_options(argc, argv, options, report_usage,
			     PARSE_OPT_KEEP_UNKNOWN);

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

	for (j = 0; j < argc; j++, i++)
		rep_argv[i] = argv[j];

	ret = cmd_report(i, rep_argv);
	free(new_sort_order);
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
		.input_name		 = "perf.data",
		/*
		 * default to both load an store sampling
		 */
		.operation		 = MEM_OPERATION_LOAD | MEM_OPERATION_STORE,
	};
	char *sort_order_help = sort_help("sort by key(s):", SORT_MODE__MEMORY);
	const struct option mem_options[] = {
	OPT_CALLBACK('t', "type", &mem.operation,
		   "type", "memory operations(load,store) Default load,store",
		    parse_mem_ops),
	OPT_STRING('C', "cpu", &mem.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_BOOLEAN('f', "force", &mem.force, "don't complain, do it"),
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show counter open errors, etc)"),
	OPT_BOOLEAN('p', "phys-data", &mem.phys_addr, "Record/Report sample physical addresses"),
	OPT_BOOLEAN(0, "data-page-size", &mem.data_page_size, "Record/Report sample data address page size"),
	OPT_END()
	};
	const struct option record_options[] = {
	OPT_CALLBACK('e', "event", &mem, "event",
		     "event selector. use 'perf mem record -e list' to list available events",
		     parse_record_events),
	OPT_UINTEGER(0, "ldlat", &perf_mem_events__loads_ldlat, "mem-loads latency"),
	OPT_BOOLEAN('U', "all-user", &mem.all_user, "collect only user level data"),
	OPT_BOOLEAN('K', "all-kernel", &mem.all_kernel, "collect only kernel level data"),
	OPT_PARENT(mem_options)
	};
	const struct option report_options[] = {
	OPT_BOOLEAN('D', "dump-raw-samples", &mem.dump_raw,
		    "dump raw samples in ASCII"),
	OPT_BOOLEAN('U', "hide-unresolved", &mem.hide_unresolved,
		    "Only display entries resolved to a symbol"),
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
	OPT_STRING_NOEMPTY('x', "field-separator", &symbol_conf.field_sep,
		   "separator",
		   "separator for columns, no spaces will be added"
		   " between columns '.' is reserved."),
	OPT_STRING('s', "sort", &mem.sort_key, "key[,key2...]",
		   sort_order_help),
	OPT_BOOLEAN('T', "type-profile", &mem.data_type,
		    "Show data-type profile result"),
	OPT_PARENT(mem_options)
	};
	const char *const mem_subcommands[] = { "record", "report", NULL };
	const char *mem_usage[] = {
		NULL,
		NULL
	};

	argc = parse_options_subcommand(argc, argv, mem_options, mem_subcommands,
					mem_usage, PARSE_OPT_STOP_AT_NON_OPTION);

	if (!argc || !(strncmp(argv[0], "rec", 3) || mem.operation))
		usage_with_options(mem_usage, mem_options);

	if (!mem.input_name || !strlen(mem.input_name)) {
		if (!fstat(STDIN_FILENO, &st) && S_ISFIFO(st.st_mode))
			mem.input_name = "-";
		else
			mem.input_name = "perf.data";
	}

	if (strlen(argv[0]) > 2 && strstarts("record", argv[0]))
		return __cmd_record(argc, argv, &mem, record_options);
	else if (strlen(argv[0]) > 2 && strstarts("report", argv[0]))
		return __cmd_report(argc, argv, &mem, report_options);
	else
		usage_with_options(mem_usage, mem_options);

	/* free usage string allocated by parse_options_subcommand */
	free((void *)mem_usage[0]);

	return 0;
}
