#include "builtin.h"
#include "perf.h"

#include "util/parse-options.h"
#include "util/trace-event.h"
#include "util/tool.h"
#include "util/session.h"

#define MEM_OPERATION_LOAD	"load"
#define MEM_OPERATION_STORE	"store"

static const char	*mem_operation		= MEM_OPERATION_LOAD;

struct perf_mem {
	struct perf_tool	tool;
	char const		*input_name;
	bool			hide_unresolved;
	bool			dump_raw;
	const char		*cpu_list;
	DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);
};

static const char * const mem_usage[] = {
	"perf mem [<options>] {record <command> |report}",
	NULL
};

static int __cmd_record(int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;
	char event[64];
	int ret;

	rec_argc = argc + 4;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_argv)
		return -1;

	rec_argv[i++] = strdup("record");
	if (!strcmp(mem_operation, MEM_OPERATION_LOAD))
		rec_argv[i++] = strdup("-W");
	rec_argv[i++] = strdup("-d");
	rec_argv[i++] = strdup("-e");

	if (strcmp(mem_operation, MEM_OPERATION_LOAD))
		sprintf(event, "cpu/mem-stores/pp");
	else
		sprintf(event, "cpu/mem-loads/pp");

	rec_argv[i++] = strdup(event);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	ret = cmd_record(i, rec_argv, NULL);
	free(rec_argv);
	return ret;
}

static int
dump_raw_samples(struct perf_tool *tool,
		 union perf_event *event,
		 struct perf_sample *sample,
		 struct perf_evsel *evsel __maybe_unused,
		 struct machine *machine)
{
	struct perf_mem *mem = container_of(tool, struct perf_mem, tool);
	struct addr_location al;
	const char *fmt;

	if (perf_event__preprocess_sample(event, machine, &al, sample) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
				event->header.type);
		return -1;
	}

	if (al.filtered || (mem->hide_unresolved && al.sym == NULL))
		return 0;

	if (al.map != NULL)
		al.map->dso->hit = 1;

	if (symbol_conf.field_sep) {
		fmt = "%d%s%d%s0x%"PRIx64"%s0x%"PRIx64"%s%"PRIu64
		      "%s0x%"PRIx64"%s%s:%s\n";
	} else {
		fmt = "%5d%s%5d%s0x%016"PRIx64"%s0x016%"PRIx64
		      "%s%5"PRIu64"%s0x%06"PRIx64"%s%s:%s\n";
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
		symbol_conf.field_sep,
		sample->weight,
		symbol_conf.field_sep,
		sample->data_src,
		symbol_conf.field_sep,
		al.map ? (al.map->dso ? al.map->dso->long_name : "???") : "???",
		al.sym ? al.sym->name : "???");

	return 0;
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	return dump_raw_samples(tool, event, sample, evsel, machine);
}

static int report_raw_events(struct perf_mem *mem)
{
	int err = -EINVAL;
	int ret;
	struct perf_session *session = perf_session__new(input_name, O_RDONLY,
							 0, false, &mem->tool);

	if (session == NULL)
		return -ENOMEM;

	if (mem->cpu_list) {
		ret = perf_session__cpu_bitmap(session, mem->cpu_list,
					       mem->cpu_bitmap);
		if (ret)
			goto out_delete;
	}

	if (symbol__init() < 0)
		return -1;

	printf("# PID, TID, IP, ADDR, LOCAL WEIGHT, DSRC, SYMBOL\n");

	err = perf_session__process_events(session, &mem->tool);
	if (err)
		return err;

	return 0;

out_delete:
	perf_session__delete(session);
	return err;
}

static int report_events(int argc, const char **argv, struct perf_mem *mem)
{
	const char **rep_argv;
	int ret, i = 0, j, rep_argc;

	if (mem->dump_raw)
		return report_raw_events(mem);

	rep_argc = argc + 3;
	rep_argv = calloc(rep_argc + 1, sizeof(char *));
	if (!rep_argv)
		return -1;

	rep_argv[i++] = strdup("report");
	rep_argv[i++] = strdup("--mem-mode");
	rep_argv[i++] = strdup("-n"); /* display number of samples */

	/*
	 * there is no weight (cost) associated with stores, so don't print
	 * the column
	 */
	if (strcmp(mem_operation, MEM_OPERATION_LOAD))
		rep_argv[i++] = strdup("--sort=mem,sym,dso,symbol_daddr,"
				       "dso_daddr,tlb,locked");

	for (j = 1; j < argc; j++, i++)
		rep_argv[i] = argv[j];

	ret = cmd_report(i, rep_argv, NULL);
	free(rep_argv);
	return ret;
}

int cmd_mem(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct stat st;
	struct perf_mem mem = {
		.tool = {
			.sample		= process_sample_event,
			.mmap		= perf_event__process_mmap,
			.comm		= perf_event__process_comm,
			.lost		= perf_event__process_lost,
			.fork		= perf_event__process_fork,
			.build_id	= perf_event__process_build_id,
			.ordered_samples = true,
		},
		.input_name		 = "perf.data",
	};
	const struct option mem_options[] = {
	OPT_STRING('t', "type", &mem_operation,
		   "type", "memory operations(load/store)"),
	OPT_BOOLEAN('D', "dump-raw-samples", &mem.dump_raw,
		    "dump raw samples in ASCII"),
	OPT_BOOLEAN('U', "hide-unresolved", &mem.hide_unresolved,
		    "Only display entries resolved to a symbol"),
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
	OPT_STRING('C', "cpu", &mem.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_STRING('x', "field-separator", &symbol_conf.field_sep,
		   "separator",
		   "separator for columns, no spaces will be added"
		   " between columns '.' is reserved."),
	OPT_END()
	};

	argc = parse_options(argc, argv, mem_options, mem_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	if (!argc || !(strncmp(argv[0], "rec", 3) || mem_operation))
		usage_with_options(mem_usage, mem_options);

	if (!mem.input_name || !strlen(mem.input_name)) {
		if (!fstat(STDIN_FILENO, &st) && S_ISFIFO(st.st_mode))
			mem.input_name = "-";
		else
			mem.input_name = "perf.data";
	}

	if (!strncmp(argv[0], "rec", 3))
		return __cmd_record(argc, argv);
	else if (!strncmp(argv[0], "rep", 3))
		return report_events(argc, argv, &mem);
	else
		usage_with_options(mem_usage, mem_options);

	return 0;
}
