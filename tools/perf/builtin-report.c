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

static bool		force, use_tui, use_stdio;
static bool		hide_unresolved;
static bool		dont_use_callchains;

static bool		show_threads;
static struct perf_read_values	show_threads_values;

static const char	default_pretty_printing_style[] = "normal";
static const char	*pretty_printing_style = default_pretty_printing_style;

static char		callchain_default_opt[] = "fractal,0.5";

static struct hists *perf_session__hists_findnew(struct perf_session *self,
						 u64 event_stream, u32 type,
						 u64 config)
{
	struct rb_node **p = &self->hists_tree.rb_node;
	struct rb_node *parent = NULL;
	struct hists *iter, *new;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct hists, rb_node);
		if (iter->config == config)
			return iter;


		if (config > iter->config)
			p = &(*p)->rb_right;
		else
			p = &(*p)->rb_left;
	}

	new = malloc(sizeof(struct hists));
	if (new == NULL)
		return NULL;
	memset(new, 0, sizeof(struct hists));
	new->event_stream = event_stream;
	new->config = config;
	new->type = type;
	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, &self->hists_tree);
	return new;
}

static int perf_session__add_hist_entry(struct perf_session *self,
					struct addr_location *al,
					struct sample_data *data)
{
	struct map_symbol *syms = NULL;
	struct symbol *parent = NULL;
	int err = -ENOMEM;
	struct hist_entry *he;
	struct hists *hists;
	struct perf_event_attr *attr;

	if ((sort__has_parent || symbol_conf.use_callchain) && data->callchain) {
		syms = perf_session__resolve_callchain(self, al->thread,
						       data->callchain, &parent);
		if (syms == NULL)
			return -ENOMEM;
	}

	attr = perf_header__find_attr(data->id, &self->header);
	if (attr)
		hists = perf_session__hists_findnew(self, data->id, attr->type, attr->config);
	else
		hists = perf_session__hists_findnew(self, data->id, 0, 0);
	if (hists == NULL)
		goto out_free_syms;
	he = __hists__add_entry(hists, al, parent, data->period);
	if (he == NULL)
		goto out_free_syms;
	err = 0;
	if (symbol_conf.use_callchain) {
		err = callchain_append(he->callchain, data->callchain, syms,
				       data->period);
		if (err)
			goto out_free_syms;
	}
	/*
	 * Only in the newt browser we are doing integrated annotation,
	 * so we don't allocated the extra space needed because the stdio
	 * code will not use it.
	 */
	if (use_browser > 0)
		err = hist_entry__inc_addr_samples(he, al->addr);
out_free_syms:
	free(syms);
	return err;
}

static int add_event_total(struct perf_session *session,
			   struct sample_data *data,
			   struct perf_event_attr *attr)
{
	struct hists *hists;

	if (attr)
		hists = perf_session__hists_findnew(session, data->id,
						    attr->type, attr->config);
	else
		hists = perf_session__hists_findnew(session, data->id, 0, 0);

	if (!hists)
		return -ENOMEM;

	hists->stats.total_period += data->period;
	/*
	 * FIXME: add_event_total should be moved from here to
	 * perf_session__process_event so that the proper hist is passed to
	 * the event_op methods.
	 */
	hists__inc_nr_events(hists, PERF_RECORD_SAMPLE);
	session->hists.stats.total_period += data->period;
	return 0;
}

static int process_sample_event(event_t *event, struct sample_data *sample,
				struct perf_session *session)
{
	struct addr_location al;
	struct perf_event_attr *attr;

	if (event__preprocess_sample(event, session, &al, sample, NULL) < 0) {
		fprintf(stderr, "problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (al.filtered || (hide_unresolved && al.sym == NULL))
		return 0;

	if (perf_session__add_hist_entry(session, &al, sample)) {
		pr_debug("problem incrementing symbol period, skipping event\n");
		return -1;
	}

	attr = perf_header__find_attr(sample->id, &session->header);

	if (add_event_total(session, sample, attr)) {
		pr_debug("problem adding event period\n");
		return -1;
	}

	return 0;
}

static int process_read_event(event_t *event, struct sample_data *sample __used,
			      struct perf_session *session __used)
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

static int perf_session__setup_sample_type(struct perf_session *self)
{
	if (!(self->sample_type & PERF_SAMPLE_CALLCHAIN)) {
		if (sort__has_parent) {
			fprintf(stderr, "selected --sort parent, but no"
					" callchain data. Did you call"
					" perf record without -g?\n");
			return -EINVAL;
		}
		if (symbol_conf.use_callchain) {
			fprintf(stderr, "selected -g but no callchain data."
					" Did you call perf record without"
					" -g?\n");
			return -1;
		}
	} else if (!dont_use_callchains && callchain_param.mode != CHAIN_NONE &&
		   !symbol_conf.use_callchain) {
			symbol_conf.use_callchain = true;
			if (register_callchain_param(&callchain_param) < 0) {
				fprintf(stderr, "Can't register callchain"
						" params\n");
				return -EINVAL;
			}
	}

	return 0;
}

static struct perf_event_ops event_ops = {
	.sample	= process_sample_event,
	.mmap	= event__process_mmap,
	.comm	= event__process_comm,
	.exit	= event__process_task,
	.fork	= event__process_task,
	.lost	= event__process_lost,
	.read	= process_read_event,
	.attr	= event__process_attr,
	.event_type = event__process_event_type,
	.tracing_data = event__process_tracing_data,
	.build_id = event__process_build_id,
};

extern volatile int session_done;

static void sig_handler(int sig __used)
{
	session_done = 1;
}

static size_t hists__fprintf_nr_sample_events(struct hists *self,
					      const char *evname, FILE *fp)
{
	size_t ret;
	char unit;
	unsigned long nr_events = self->stats.nr_events[PERF_RECORD_SAMPLE];

	nr_events = convert_unit(nr_events, &unit);
	ret = fprintf(fp, "# Events: %lu%c", nr_events, unit);
	if (evname != NULL)
		ret += fprintf(fp, " %s", evname);
	return ret + fprintf(fp, "\n#\n");
}

static int hists__tty_browse_tree(struct rb_root *tree, const char *help)
{
	struct rb_node *next = rb_first(tree);

	while (next) {
		struct hists *hists = rb_entry(next, struct hists, rb_node);
		const char *evname = NULL;

		if (rb_first(&hists->entries) != rb_last(&hists->entries))
			evname = __event_name(hists->type, hists->config);

		hists__fprintf_nr_sample_events(hists, evname, stdout);
		hists__fprintf(hists, NULL, false, stdout);
		fprintf(stdout, "\n\n");
		next = rb_next(&hists->rb_node);
	}

	if (sort_order == default_sort_order &&
	    parent_pattern == default_parent_pattern) {
		fprintf(stdout, "#\n# (%s)\n#\n", help);

		if (show_threads) {
			bool style = !strcmp(pretty_printing_style, "raw");
			perf_read_values_display(stdout, &show_threads_values,
						 style);
			perf_read_values_destroy(&show_threads_values);
		}
	}

	return 0;
}

static int __cmd_report(void)
{
	int ret = -EINVAL;
	struct perf_session *session;
	struct rb_node *next;
	const char *help = "For a higher level overview, try: perf report --sort comm,dso";

	signal(SIGINT, sig_handler);

	session = perf_session__new(input_name, O_RDONLY, force, false);
	if (session == NULL)
		return -ENOMEM;

	if (show_threads)
		perf_read_values_init(&show_threads_values);

	ret = perf_session__setup_sample_type(session);
	if (ret)
		goto out_delete;

	ret = perf_session__process_events(session, &event_ops);
	if (ret)
		goto out_delete;

	if (dump_trace) {
		perf_session__fprintf_nr_events(session, stdout);
		goto out_delete;
	}

	if (verbose > 3)
		perf_session__fprintf(session, stdout);

	if (verbose > 2)
		perf_session__fprintf_dsos(session, stdout);

	next = rb_first(&session->hists_tree);
	while (next) {
		struct hists *hists;

		hists = rb_entry(next, struct hists, rb_node);
		hists__collapse_resort(hists);
		hists__output_resort(hists);
		next = rb_next(&hists->rb_node);
	}

	if (use_browser > 0)
		hists__tui_browse_tree(&session->hists_tree, help);
	else
		hists__tty_browse_tree(&session->hists_tree, help);

out_delete:
	/*
	 * Speed up the exit process, for large files this can
	 * take quite a while.
	 *
	 * XXX Enable this when using valgrind or if we ever
	 * librarize this command.
	 *
	 * Also experiment with obstacks to see how much speed
	 * up we'll get here.
	 *
 	 * perf_session__delete(session);
 	 */
	return ret;
}

static int
parse_callchain_opt(const struct option *opt __used, const char *arg,
		    int unset)
{
	char *tok, *tok2;
	char *endptr;

	/*
	 * --no-call-graph
	 */
	if (unset) {
		dont_use_callchains = true;
		return 0;
	}

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

	tok2 = strtok(NULL, ",");
	callchain_param.min_percent = strtod(tok, &endptr);
	if (tok == endptr)
		return -1;

	if (tok2)
		callchain_param.print_limit = strtod(tok2, &endptr);
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
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('n', "show-nr-samples", &symbol_conf.show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_BOOLEAN('T', "threads", &show_threads,
		    "Show per-thread event counters"),
	OPT_STRING(0, "pretty", &pretty_printing_style, "key",
		   "pretty printing style key: normal raw"),
	OPT_BOOLEAN(0, "tui", &use_tui, "Use the TUI interface"),
	OPT_BOOLEAN(0, "stdio", &use_stdio, "Use the stdio interface"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent"),
	OPT_BOOLEAN(0, "showcpuutilization", &symbol_conf.show_cpu_utilization,
		    "Show sample percentage for different cpu modes"),
	OPT_STRING('p', "parent", &parent_pattern, "regex",
		   "regex filter to identify parent, see: '--sort parent'"),
	OPT_BOOLEAN('x', "exclude-other", &symbol_conf.exclude_other,
		    "Only display entries with parent-match"),
	OPT_CALLBACK_DEFAULT('g', "call-graph", NULL, "output_type,min_percent",
		     "Display callchains using output_type (graph, flat, fractal, or none) and min percent threshold. "
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
	OPT_BOOLEAN('U', "hide-unresolved", &hide_unresolved,
		    "Only display entries resolved to a symbol"),
	OPT_END()
};

int cmd_report(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, report_usage, 0);

	if (use_stdio)
		use_browser = 0;
	else if (use_tui)
		use_browser = 1;

	if (strcmp(input_name, "-") != 0)
		setup_browser();
	else
		use_browser = 0;
	/*
	 * Only in the newt browser we are doing integrated annotation,
	 * so don't allocate extra space that won't be used in the stdio
	 * implementation.
	 */
	if (use_browser > 0) {
		symbol_conf.priv_size = sizeof(struct sym_priv);
		/*
 		 * For searching by name on the "Browse map details".
 		 * providing it only in verbose mode not to bloat too
 		 * much struct symbol.
 		 */
		if (verbose) {
			/*
			 * XXX: Need to provide a less kludgy way to ask for
			 * more space per symbol, the u32 is for the index on
			 * the ui browser.
			 * See symbol__browser_index.
			 */
			symbol_conf.priv_size += sizeof(u32);
			symbol_conf.sort_by_name = true;
		}
	}

	if (symbol__init() < 0)
		return -1;

	setup_sorting(report_usage, options);

	if (parent_pattern != default_parent_pattern) {
		if (sort_dimension__add("parent") < 0)
			return -1;
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
