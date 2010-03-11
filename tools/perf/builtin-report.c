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
static bool		hide_unresolved;
static bool		dont_use_callchains;

static int		show_threads;
static struct perf_read_values	show_threads_values;

static char		default_pretty_printing_style[] = "normal";
static char		*pretty_printing_style = default_pretty_printing_style;

static char		callchain_default_opt[] = "fractal,0.5";

static struct event_stat_id *get_stats(struct perf_session *self,
				       u64 event_stream, u32 type, u64 config)
{
	struct rb_node **p = &self->stats_by_id.rb_node;
	struct rb_node *parent = NULL;
	struct event_stat_id *iter, *new;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct event_stat_id, rb_node);
		if (iter->config == config)
			return iter;


		if (config > iter->config)
			p = &(*p)->rb_right;
		else
			p = &(*p)->rb_left;
	}

	new = malloc(sizeof(struct event_stat_id));
	if (new == NULL)
		return NULL;
	memset(new, 0, sizeof(struct event_stat_id));
	new->event_stream = event_stream;
	new->config = config;
	new->type = type;
	rb_link_node(&new->rb_node, parent, p);
	rb_insert_color(&new->rb_node, &self->stats_by_id);
	return new;
}

static int perf_session__add_hist_entry(struct perf_session *self,
					struct addr_location *al,
					struct sample_data *data)
{
	struct symbol **syms = NULL, *parent = NULL;
	bool hit;
	struct hist_entry *he;
	struct event_stat_id *stats;
	struct perf_event_attr *attr;

	if ((sort__has_parent || symbol_conf.use_callchain) && data->callchain)
		syms = perf_session__resolve_callchain(self, al->thread,
						       data->callchain, &parent);

	attr = perf_header__find_attr(data->id, &self->header);
	if (attr)
		stats = get_stats(self, data->id, attr->type, attr->config);
	else
		stats = get_stats(self, data->id, 0, 0);
	if (stats == NULL)
		return -ENOMEM;
	he = __perf_session__add_hist_entry(&stats->hists, al, parent,
					    data->period, &hit);
	if (he == NULL)
		return -ENOMEM;

	if (hit)
		he->count += data->period;

	if (symbol_conf.use_callchain) {
		if (!hit)
			callchain_init(&he->callchain);
		append_chain(&he->callchain, data->callchain, syms);
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

static int add_event_total(struct perf_session *session,
			   struct sample_data *data,
			   struct perf_event_attr *attr)
{
	struct event_stat_id *stats;

	if (attr)
		stats = get_stats(session, data->id, attr->type, attr->config);
	else
		stats = get_stats(session, data->id, 0, 0);

	if (!stats)
		return -ENOMEM;

	stats->stats.total += data->period;
	session->events_stats.total += data->period;
	return 0;
}

static int process_sample_event(event_t *event, struct perf_session *session)
{
	struct sample_data data = { .period = 1, };
	struct addr_location al;
	struct perf_event_attr *attr;

	event__parse_sample(event, session->sample_type, &data);

	dump_printf("(IP, %d): %d/%d: %#Lx period: %Ld\n", event->header.misc,
		    data.pid, data.tid, data.ip, data.period);

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

	if (al.filtered || (hide_unresolved && al.sym == NULL))
		return 0;

	if (perf_session__add_hist_entry(session, &al, &data)) {
		pr_debug("problem incrementing symbol count, skipping event\n");
		return -1;
	}

	attr = perf_header__find_attr(data.id, &session->header);

	if (add_event_total(session, &data, attr)) {
		pr_debug("problem adding event count\n");
		return -1;
	}

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
};

static int __cmd_report(void)
{
	int ret = -EINVAL;
	struct perf_session *session;
	struct rb_node *next;
	const char *help = "For a higher level overview, try: perf report --sort comm,dso";

	session = perf_session__new(input_name, O_RDONLY, force);
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
		event__print_totals();
		goto out_delete;
	}

	if (verbose > 3)
		perf_session__fprintf(session, stdout);

	if (verbose > 2)
		dsos__fprintf(stdout);

	next = rb_first(&session->stats_by_id);
	while (next) {
		struct event_stat_id *stats;

		stats = rb_entry(next, struct event_stat_id, rb_node);
		perf_session__collapse_resort(&stats->hists);
		perf_session__output_resort(&stats->hists, stats->stats.total);

		if (use_browser)
			perf_session__browse_hists(&stats->hists,
						   stats->stats.total, help);
		else {
			if (rb_first(&session->stats_by_id) ==
			    rb_last(&session->stats_by_id))
				fprintf(stdout, "# Samples: %Ld\n#\n",
					stats->stats.total);
			else
				fprintf(stdout, "# Samples: %Ld %s\n#\n",
					stats->stats.total,
					__event_name(stats->type, stats->config));

			perf_session__fprintf_hists(&stats->hists, NULL, false, stdout,
					    stats->stats.total);
			fprintf(stdout, "\n\n");
		}

		next = rb_next(&stats->rb_node);
	}

	if (!use_browser && sort_order == default_sort_order &&
	    parent_pattern == default_parent_pattern) {
		fprintf(stdout, "#\n# (%s)\n#\n", help);

		if (show_threads) {
			bool style = !strcmp(pretty_printing_style, "raw");
			perf_read_values_display(stdout, &show_threads_values,
						 style);
			perf_read_values_destroy(&show_threads_values);
		}
	}
out_delete:
	perf_session__delete(session);
	return ret;
}

static int
parse_callchain_opt(const struct option *opt __used, const char *arg,
		    int unset)
{
	char *tok;
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
	OPT_BOOLEAN('P', "full-paths", &symbol_conf.full_paths,
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
	OPT_BOOLEAN('U', "hide-unresolved", &hide_unresolved,
		    "Only display entries resolved to a symbol"),
	OPT_END()
};

int cmd_report(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, report_usage, 0);

	setup_browser();

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
