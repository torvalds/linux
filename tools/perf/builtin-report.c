/*
 * builtin-report.c
 *
 * Builtin report command: Analyze the perf.data input file,
 * look up and read DSOs and symbol information and display
 * a histogram of results, along various sorting keys.
 */
#include "builtin.h"

#include "util/util.h"

#include "util/color.h"
#include <linux/list.h>
#include "util/cache.h"
#include <linux/rbtree.h>
#include "util/symbol.h"
#include "util/string.h"
#include "util/callchain.h"
#include "util/strlist.h"
#include "util/values.h"

#include "perf.h"
#include "util/debug.h"
#include "util/header.h"
#include "util/session.h"

#include "util/parse-options.h"
#include "util/parse-events.h"

#include "util/thread.h"
#include "util/sort.h"
#include "util/hist.h"

static char		const *input_name = "perf.data";

static int		force;

static int		show_threads;
static struct perf_read_values	show_threads_values;

static char		default_pretty_printing_style[] = "normal";
static char		*pretty_printing_style = default_pretty_printing_style;

static char		callchain_default_opt[] = "fractal,0.5";

static int perf_session__add_hist_entry(struct perf_session *self,
					struct addr_location *al,
					struct ip_callchain *chain, u64 count)
{
	struct symbol **syms = NULL, *parent = NULL;
	bool hit;
	struct hist_entry *he;

	if ((sort__has_parent || symbol_conf.use_callchain) && chain)
		syms = perf_session__resolve_callchain(self, al->thread,
						       chain, &parent);
	he = __perf_session__add_hist_entry(self, al, parent, count, &hit);
	if (he == NULL)
		return -ENOMEM;

	if (hit)
		he->count += count;

	if (symbol_conf.use_callchain) {
		if (!hit)
			callchain_init(&he->callchain);
		append_chain(&he->callchain, chain, syms);
		free(syms);
	}

	return 0;
}

static int validate_chain(struct ip_callchain *chain, event_t *event)
{
	unsigned int chain_size;

	chain_size = event->header.size;
	chain_size -= (unsigned long)&event->ip.__more_data - (unsigned long)event;

	if (chain->nr*sizeof(u64) > chain_size)
		return -1;

	return 0;
}

static int process_sample_event(event_t *event, struct perf_session *session)
{
	struct sample_data data = { .period = 1, };
	struct addr_location al;

	event__parse_sample(event, session->sample_type, &data);

	dump_printf("(IP, %d): %d/%d: %p period: %Ld\n",
		event->header.misc,
		data.pid, data.tid,
		(void *)(long)data.ip,
		(long long)data.period);

	if (session->sample_type & PERF_SAMPLE_CALLCHAIN) {
		unsigned int i;

		dump_printf("... chain: nr:%Lu\n", data.callchain->nr);

		if (validate_chain(data.callchain, event) < 0) {
			pr_debug("call-chain problem with event, "
				 "skipping it.\n");
			return 0;
		}

		if (dump_trace) {
			for (i = 0; i < data.callchain->nr; i++)
				dump_printf("..... %2d: %016Lx\n",
					    i, data.callchain->ips[i]);
		}
	}

	if (event__preprocess_sample(event, session, &al, NULL) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (al.filtered)
		return 0;

	if (perf_session__add_hist_entry(session, &al, data.callchain, data.period)) {
		pr_debug("problem incrementing symbol count, skipping event\n");
		return -1;
	}

	session->events_stats.total += data.period;
	return 0;
}

static int process_read_event(event_t *event, struct perf_session *session __used)
{
	struct perf_event_attr *attr;

	attr = perf_header__find_attr(event->read.id, &session->header);

	if (show_threads) {
		const char *name = attr ? __event_name(attr->type, attr->config)
				   : "unknown";
		perf_read_values_add_value(&show_threads_values,
					   event->read.pid, event->read.tid,
					   event->read.id,
					   name,
					   event->read.value);
	}

	dump_printf(": %d %d %s %Lu\n", event->read.pid, event->read.tid,
		    attr ? __event_name(attr->type, attr->config) : "FAIL",
		    event->read.value);

	return 0;
}

static int sample_type_check(struct perf_session *session)
{
	if (!(session->sample_type & PERF_SAMPLE_CALLCHAIN)) {
		if (sort__has_parent) {
			fprintf(stderr, "selected --sort parent, but no"
					" callchain data. Did you call"
					" perf record without -g?\n");
			return -1;
		}
		if (symbol_conf.use_callchain) {
			fprintf(stderr, "selected -g but no callchain data."
					" Did you call perf record without"
					" -g?\n");
			return -1;
		}
	} else if (callchain_param.mode != CHAIN_NONE && !symbol_conf.use_callchain) {
			symbol_conf.use_callchain = true;
			if (register_callchain_param(&callchain_param) < 0) {
				fprintf(stderr, "Can't register callchain"
						" params\n");
				return -1;
			}
	}

	return 0;
}

static struct perf_event_ops event_ops = {
	.process_sample_event	= process_sample_event,
	.process_mmap_event	= event__process_mmap,
	.process_comm_event	= event__process_comm,
	.process_exit_event	= event__process_task,
	.process_fork_event	= event__process_task,
	.process_lost_event	= event__process_lost,
	.process_read_event	= process_read_event,
	.sample_type_check	= sample_type_check,
};


static int __cmd_report(void)
{
	int ret;
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, force);
	if (session == NULL)
		return -ENOMEM;

	if (show_threads)
		perf_read_values_init(&show_threads_values);

	ret = perf_session__process_events(session, &event_ops);
	if (ret)
		goto out_delete;

	if (dump_trace) {
		event__print_totals();
		goto out_delete;
	}

	if (verbose > 3)
		perf_session__fprintf(session, stdout);

	if (verbose > 2)
		dsos__fprintf(stdout);

	perf_session__collapse_resort(session);
	perf_session__output_resort(session, session->events_stats.total);
	fprintf(stdout, "# Samples: %Ld\n#\n", session->events_stats.total);
	perf_session__fprintf_hists(session, NULL, false, stdout);
	if (sort_order == default_sort_order &&
	    parent_pattern == default_parent_pattern)
		fprintf(stdout, "#\n# (For a higher level overview, try: perf report --sort comm,dso)\n#\n");

	if (show_threads) {
		bool raw_printing_style = !strcmp(pretty_printing_style, "raw");
		perf_read_values_display(stdout, &show_threads_values,
					 raw_printing_style);
		perf_read_values_destroy(&show_threads_values);
	}
out_delete:
	perf_session__delete(session);
	return ret;
}

static int
parse_callchain_opt(const struct option *opt __used, const char *arg,
		    int unset __used)
{
	char *tok;
	char *endptr;

	symbol_conf.use_callchain = true;

	if (!arg)
		return 0;

	tok = strtok((char *)arg, ",");
	if (!tok)
		return -1;

	/* get the output mode */
	if (!strncmp(tok, "graph", strlen(arg)))
		callchain_param.mode = CHAIN_GRAPH_ABS;

	else if (!strncmp(tok, "flat", strlen(arg)))
		callchain_param.mode = CHAIN_FLAT;

	else if (!strncmp(tok, "fractal", strlen(arg)))
		callchain_param.mode = CHAIN_GRAPH_REL;

	else if (!strncmp(tok, "none", strlen(arg))) {
		callchain_param.mode = CHAIN_NONE;
		symbol_conf.use_callchain = false;

		return 0;
	}

	else
		return -1;

	/* get the min percentage */
	tok = strtok(NULL, ",");
	if (!tok)
		goto setup;

	callchain_param.min_percent = strtod(tok, &endptr);
	if (tok == endptr)
		return -1;

setup:
	if (register_callchain_param(&callchain_param) < 0) {
		fprintf(stderr, "Can't register callchain params\n");
		return -1;
	}
	return 0;
}

static const char * const report_usage[] = {
	"perf report [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('n', "show-nr-samples", &symbol_conf.show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_BOOLEAN('T', "threads", &show_threads,
		    "Show per-thread event counters"),
	OPT_STRING(0, "pretty", &pretty_printing_style, "key",
		   "pretty printing style key: normal raw"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent"),
	OPT_BOOLEAN('P', "full-paths", &event_ops.full_paths,
		    "Don't shorten the pathnames taking into account the cwd"),
	OPT_STRING('p', "parent", &parent_pattern, "regex",
		   "regex filter to identify parent, see: '--sort parent'"),
	OPT_BOOLEAN('x', "exclude-other", &symbol_conf.exclude_other,
		    "Only display entries with parent-match"),
	OPT_CALLBACK_DEFAULT('g', "call-graph", NULL, "output_type,min_percent",
		     "Display callchains using output_type and min percent threshold. "
		     "Default: fractal,0.5", &parse_callchain_opt, callchain_default_opt),
	OPT_STRING('d', "dsos", &symbol_conf.dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('C', "comms", &symbol_conf.comm_list_str, "comm[,comm...]",
		   "only consider symbols in these comms"),
	OPT_STRING('S', "symbols", &symbol_conf.sym_list_str, "symbol[,symbol...]",
		   "only consider these symbols"),
	OPT_STRING('w', "column-widths", &symbol_conf.col_width_list_str,
		   "width[,width...]",
		   "don't try to adjust column width, use these fixed values"),
	OPT_STRING('t', "field-separator", &symbol_conf.field_sep, "separator",
		   "separator for columns, no spaces will be added between "
		   "columns '.' is reserved."),
	OPT_END()
};

int cmd_report(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, report_usage, 0);

	setup_pager();

	if (symbol__init() < 0)
		return -1;

	setup_sorting(report_usage, options);

	if (parent_pattern != default_parent_pattern) {
		sort_dimension__add("parent");
		sort_parent.elide = 1;
	} else
		symbol_conf.exclude_other = false;

	/*
	 * Any (unrecognized) arguments left?
	 */
	if (argc)
		usage_with_options(report_usage, options);

	sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list, "dso", stdout);
	sort_entry__setup_elide(&sort_comm, symbol_conf.comm_list, "comm", stdout);
	sort_entry__setup_elide(&sort_sym, symbol_conf.sym_list, "symbol", stdout);

	return __cmd_report();
}
