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

#include "util/data_map.h"
#include "util/thread.h"
#include "util/sort.h"
#include "util/hist.h"

static char		const *input_name = "perf.data";

static char		*dso_list_str, *comm_list_str, *sym_list_str,
			*col_width_list_str;
static struct strlist	*dso_list, *comm_list, *sym_list;

static int		force;

static int		full_paths;
static int		show_nr_samples;

static int		show_threads;
static struct perf_read_values	show_threads_values;

static char		default_pretty_printing_style[] = "normal";
static char		*pretty_printing_style = default_pretty_printing_style;

static int		exclude_other = 1;

static char		callchain_default_opt[] = "fractal,0.5";

static u64		sample_type;

struct symbol_conf	symbol_conf;


static size_t
callchain__fprintf_left_margin(FILE *fp, int left_margin)
{
	int i;
	int ret;

	ret = fprintf(fp, "            ");

	for (i = 0; i < left_margin; i++)
		ret += fprintf(fp, " ");

	return ret;
}

static size_t ipchain__fprintf_graph_line(FILE *fp, int depth, int depth_mask,
					  int left_margin)
{
	int i;
	size_t ret = 0;

	ret += callchain__fprintf_left_margin(fp, left_margin);

	for (i = 0; i < depth; i++)
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|          ");
		else
			ret += fprintf(fp, "           ");

	ret += fprintf(fp, "\n");

	return ret;
}
static size_t
ipchain__fprintf_graph(FILE *fp, struct callchain_list *chain, int depth,
		       int depth_mask, int count, u64 total_samples,
		       int hits, int left_margin)
{
	int i;
	size_t ret = 0;

	ret += callchain__fprintf_left_margin(fp, left_margin);
	for (i = 0; i < depth; i++) {
		if (depth_mask & (1 << i))
			ret += fprintf(fp, "|");
		else
			ret += fprintf(fp, " ");
		if (!count && i == depth - 1) {
			double percent;

			percent = hits * 100.0 / total_samples;
			ret += percent_color_fprintf(fp, "--%2.2f%%-- ", percent);
		} else
			ret += fprintf(fp, "%s", "          ");
	}
	if (chain->sym)
		ret += fprintf(fp, "%s\n", chain->sym->name);
	else
		ret += fprintf(fp, "%p\n", (void *)(long)chain->ip);

	return ret;
}

static struct symbol *rem_sq_bracket;
static struct callchain_list rem_hits;

static void init_rem_hits(void)
{
	rem_sq_bracket = malloc(sizeof(*rem_sq_bracket) + 6);
	if (!rem_sq_bracket) {
		fprintf(stderr, "Not enough memory to display remaining hits\n");
		return;
	}

	strcpy(rem_sq_bracket->name, "[...]");
	rem_hits.sym = rem_sq_bracket;
}

static size_t
__callchain__fprintf_graph(FILE *fp, struct callchain_node *self,
			   u64 total_samples, int depth, int depth_mask,
			   int left_margin)
{
	struct rb_node *node, *next;
	struct callchain_node *child;
	struct callchain_list *chain;
	int new_depth_mask = depth_mask;
	u64 new_total;
	u64 remaining;
	size_t ret = 0;
	int i;

	if (callchain_param.mode == CHAIN_GRAPH_REL)
		new_total = self->children_hit;
	else
		new_total = total_samples;

	remaining = new_total;

	node = rb_first(&self->rb_root);
	while (node) {
		u64 cumul;

		child = rb_entry(node, struct callchain_node, rb_node);
		cumul = cumul_hits(child);
		remaining -= cumul;

		/*
		 * The depth mask manages the output of pipes that show
		 * the depth. We don't want to keep the pipes of the current
		 * level for the last child of this depth.
		 * Except if we have remaining filtered hits. They will
		 * supersede the last child
		 */
		next = rb_next(node);
		if (!next && (callchain_param.mode != CHAIN_GRAPH_REL || !remaining))
			new_depth_mask &= ~(1 << (depth - 1));

		/*
		 * But we keep the older depth mask for the line seperator
		 * to keep the level link until we reach the last child
		 */
		ret += ipchain__fprintf_graph_line(fp, depth, depth_mask,
						   left_margin);
		i = 0;
		list_for_each_entry(chain, &child->val, list) {
			if (chain->ip >= PERF_CONTEXT_MAX)
				continue;
			ret += ipchain__fprintf_graph(fp, chain, depth,
						      new_depth_mask, i++,
						      new_total,
						      cumul,
						      left_margin);
		}
		ret += __callchain__fprintf_graph(fp, child, new_total,
						  depth + 1,
						  new_depth_mask | (1 << depth),
						  left_margin);
		node = next;
	}

	if (callchain_param.mode == CHAIN_GRAPH_REL &&
		remaining && remaining != new_total) {

		if (!rem_sq_bracket)
			return ret;

		new_depth_mask &= ~(1 << (depth - 1));

		ret += ipchain__fprintf_graph(fp, &rem_hits, depth,
					      new_depth_mask, 0, new_total,
					      remaining, left_margin);
	}

	return ret;
}


static size_t
callchain__fprintf_graph(FILE *fp, struct callchain_node *self,
			 u64 total_samples, int left_margin)
{
	struct callchain_list *chain;
	bool printed = false;
	int i = 0;
	int ret = 0;

	list_for_each_entry(chain, &self->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;

		if (!i++ && sort__first_dimension == SORT_SYM)
			continue;

		if (!printed) {
			ret += callchain__fprintf_left_margin(fp, left_margin);
			ret += fprintf(fp, "|\n");
			ret += callchain__fprintf_left_margin(fp, left_margin);
			ret += fprintf(fp, "---");

			left_margin += 3;
			printed = true;
		} else
			ret += callchain__fprintf_left_margin(fp, left_margin);

		if (chain->sym)
			ret += fprintf(fp, " %s\n", chain->sym->name);
		else
			ret += fprintf(fp, " %p\n", (void *)(long)chain->ip);
	}

	ret += __callchain__fprintf_graph(fp, self, total_samples, 1, 1, left_margin);

	return ret;
}

static size_t
callchain__fprintf_flat(FILE *fp, struct callchain_node *self,
			u64 total_samples)
{
	struct callchain_list *chain;
	size_t ret = 0;

	if (!self)
		return 0;

	ret += callchain__fprintf_flat(fp, self->parent, total_samples);


	list_for_each_entry(chain, &self->val, list) {
		if (chain->ip >= PERF_CONTEXT_MAX)
			continue;
		if (chain->sym)
			ret += fprintf(fp, "                %s\n", chain->sym->name);
		else
			ret += fprintf(fp, "                %p\n",
					(void *)(long)chain->ip);
	}

	return ret;
}

static size_t
hist_entry_callchain__fprintf(FILE *fp, struct hist_entry *self,
			      u64 total_samples, int left_margin)
{
	struct rb_node *rb_node;
	struct callchain_node *chain;
	size_t ret = 0;

	rb_node = rb_first(&self->sorted_chain);
	while (rb_node) {
		double percent;

		chain = rb_entry(rb_node, struct callchain_node, rb_node);
		percent = chain->hit * 100.0 / total_samples;
		switch (callchain_param.mode) {
		case CHAIN_FLAT:
			ret += percent_color_fprintf(fp, "           %6.2f%%\n",
						     percent);
			ret += callchain__fprintf_flat(fp, chain, total_samples);
			break;
		case CHAIN_GRAPH_ABS: /* Falldown */
		case CHAIN_GRAPH_REL:
			ret += callchain__fprintf_graph(fp, chain, total_samples,
							left_margin);
		case CHAIN_NONE:
		default:
			break;
		}
		ret += fprintf(fp, "\n");
		rb_node = rb_next(rb_node);
	}

	return ret;
}

static size_t
hist_entry__fprintf(FILE *fp, struct hist_entry *self, u64 total_samples)
{
	struct sort_entry *se;
	size_t ret;

	if (exclude_other && !self->parent)
		return 0;

	if (total_samples)
		ret = percent_color_fprintf(fp,
					    field_sep ? "%.2f" : "   %6.2f%%",
					(self->count * 100.0) / total_samples);
	else
		ret = fprintf(fp, field_sep ? "%lld" : "%12lld ", self->count);

	if (show_nr_samples) {
		if (field_sep)
			fprintf(fp, "%c%lld", *field_sep, self->count);
		else
			fprintf(fp, "%11lld", self->count);
	}

	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;

		fprintf(fp, "%s", field_sep ?: "  ");
		ret += se->print(fp, self, se->width ? *se->width : 0);
	}

	ret += fprintf(fp, "\n");

	if (callchain) {
		int left_margin = 0;

		if (sort__first_dimension == SORT_COMM) {
			se = list_first_entry(&hist_entry__sort_list, typeof(*se),
						list);
			left_margin = se->width ? *se->width : 0;
			left_margin -= thread__comm_len(self->thread);
		}

		hist_entry_callchain__fprintf(fp, self, total_samples,
					      left_margin);
	}

	return ret;
}

/*
 *
 */

static void dso__calc_col_width(struct dso *self)
{
	if (!col_width_list_str && !field_sep &&
	    (!dso_list || strlist__has_entry(dso_list, self->name))) {
		unsigned int slen = strlen(self->name);
		if (slen > dsos__col_width)
			dsos__col_width = slen;
	}

	self->slen_calculated = 1;
}

static void thread__comm_adjust(struct thread *self)
{
	char *comm = self->comm;

	if (!col_width_list_str && !field_sep &&
	    (!comm_list || strlist__has_entry(comm_list, comm))) {
		unsigned int slen = strlen(comm);

		if (slen > comms__col_width) {
			comms__col_width = slen;
			threads__col_width = slen + 6;
		}
	}
}

static int thread__set_comm_adjust(struct thread *self, const char *comm)
{
	int ret = thread__set_comm(self, comm);

	if (ret)
		return ret;

	thread__comm_adjust(self);

	return 0;
}

static int call__match(struct symbol *sym)
{
	if (sym->name && !regexec(&parent_regex, sym->name, 0, NULL, 0))
		return 1;

	return 0;
}

static struct symbol **resolve_callchain(struct thread *thread,
					 struct ip_callchain *chain,
					 struct symbol **parent)
{
	u8 cpumode = PERF_RECORD_MISC_USER;
	struct symbol **syms = NULL;
	unsigned int i;

	if (callchain) {
		syms = calloc(chain->nr, sizeof(*syms));
		if (!syms) {
			fprintf(stderr, "Can't allocate memory for symbols\n");
			exit(-1);
		}
	}

	for (i = 0; i < chain->nr; i++) {
		u64 ip = chain->ips[i];
		struct addr_location al;

		if (ip >= PERF_CONTEXT_MAX) {
			switch (ip) {
			case PERF_CONTEXT_HV:
				cpumode = PERF_RECORD_MISC_HYPERVISOR;	break;
			case PERF_CONTEXT_KERNEL:
				cpumode = PERF_RECORD_MISC_KERNEL;	break;
			case PERF_CONTEXT_USER:
				cpumode = PERF_RECORD_MISC_USER;	break;
			default:
				break;
			}
			continue;
		}

		thread__find_addr_location(thread, cpumode, MAP__FUNCTION,
					   ip, &al, NULL);
		if (al.sym != NULL) {
			if (sort__has_parent && !*parent &&
			    call__match(al.sym))
				*parent = al.sym;
			if (!callchain)
				break;
			syms[i] = al.sym;
		}
	}

	return syms;
}

/*
 * collect histogram counts
 */

static int hist_entry__add(struct addr_location *al,
			   struct ip_callchain *chain, u64 count)
{
	struct symbol **syms = NULL, *parent = NULL;
	bool hit;
	struct hist_entry *he;

	if ((sort__has_parent || callchain) && chain)
		syms = resolve_callchain(al->thread, chain, &parent);

	he = __hist_entry__add(al, parent, count, &hit);
	if (he == NULL)
		return -ENOMEM;

	if (hit)
		he->count += count;

	if (callchain) {
		if (!hit)
			callchain_init(&he->callchain);
		append_chain(&he->callchain, chain, syms);
		free(syms);
	}

	return 0;
}

static size_t output__fprintf(FILE *fp, u64 total_samples)
{
	struct hist_entry *pos;
	struct sort_entry *se;
	struct rb_node *nd;
	size_t ret = 0;
	unsigned int width;
	char *col_width = col_width_list_str;
	int raw_printing_style;

	raw_printing_style = !strcmp(pretty_printing_style, "raw");

	init_rem_hits();

	fprintf(fp, "# Samples: %Ld\n", (u64)total_samples);
	fprintf(fp, "#\n");

	fprintf(fp, "# Overhead");
	if (show_nr_samples) {
		if (field_sep)
			fprintf(fp, "%cSamples", *field_sep);
		else
			fputs("  Samples  ", fp);
	}
	list_for_each_entry(se, &hist_entry__sort_list, list) {
		if (se->elide)
			continue;
		if (field_sep) {
			fprintf(fp, "%c%s", *field_sep, se->header);
			continue;
		}
		width = strlen(se->header);
		if (se->width) {
			if (col_width_list_str) {
				if (col_width) {
					*se->width = atoi(col_width);
					col_width = strchr(col_width, ',');
					if (col_width)
						++col_width;
				}
			}
			width = *se->width = max(*se->width, width);
		}
		fprintf(fp, "  %*s", width, se->header);
	}
	fprintf(fp, "\n");

	if (field_sep)
		goto print_entries;

	fprintf(fp, "# ........");
	if (show_nr_samples)
		fprintf(fp, " ..........");
	list_for_each_entry(se, &hist_entry__sort_list, list) {
		unsigned int i;

		if (se->elide)
			continue;

		fprintf(fp, "  ");
		if (se->width)
			width = *se->width;
		else
			width = strlen(se->header);
		for (i = 0; i < width; i++)
			fprintf(fp, ".");
	}
	fprintf(fp, "\n");

	fprintf(fp, "#\n");

print_entries:
	for (nd = rb_first(&output_hists); nd; nd = rb_next(nd)) {
		pos = rb_entry(nd, struct hist_entry, rb_node);
		ret += hist_entry__fprintf(fp, pos, total_samples);
	}

	if (sort_order == default_sort_order &&
			parent_pattern == default_parent_pattern) {
		fprintf(fp, "#\n");
		fprintf(fp, "# (For a higher level overview, try: perf report --sort comm,dso)\n");
		fprintf(fp, "#\n");
	}
	fprintf(fp, "\n");

	free(rem_sq_bracket);

	if (show_threads)
		perf_read_values_display(fp, &show_threads_values,
					 raw_printing_style);

	return ret;
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

static int process_sample_event(event_t *event, struct perf_session *session __used)
{
	struct sample_data data;
	int cpumode;
	struct addr_location al;
	struct thread *thread;

	memset(&data, 0, sizeof(data));
	data.period = 1;

	event__parse_sample(event, sample_type, &data);

	dump_printf("(IP, %d): %d/%d: %p period: %Ld\n",
		event->header.misc,
		data.pid, data.tid,
		(void *)(long)data.ip,
		(long long)data.period);

	if (sample_type & PERF_SAMPLE_CALLCHAIN) {
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

	thread = threads__findnew(data.pid);
	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (comm_list && !strlist__has_entry(comm_list, thread->comm))
		return 0;

	cpumode = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

	thread__find_addr_location(thread, cpumode,
				   MAP__FUNCTION, data.ip, &al, NULL);
	/*
	 * We have to do this here as we may have a dso with no symbol hit that
	 * has a name longer than the ones with symbols sampled.
	 */
	if (al.map && !sort_dso.elide && !al.map->dso->slen_calculated)
		dso__calc_col_width(al.map->dso);

	if (dso_list &&
	    (!al.map || !al.map->dso ||
	     !(strlist__has_entry(dso_list, al.map->dso->short_name) ||
	       (al.map->dso->short_name != al.map->dso->long_name &&
		strlist__has_entry(dso_list, al.map->dso->long_name)))))
		return 0;

	if (sym_list && al.sym && !strlist__has_entry(sym_list, al.sym->name))
		return 0;

	if (hist_entry__add(&al, data.callchain, data.period)) {
		pr_debug("problem incrementing symbol count, skipping event\n");
		return -1;
	}

	event__stats.total += data.period;

	return 0;
}

static int process_comm_event(event_t *event, struct perf_session *session __used)
{
	struct thread *thread = threads__findnew(event->comm.pid);

	dump_printf(": %s:%d\n", event->comm.comm, event->comm.pid);

	if (thread == NULL ||
	    thread__set_comm_adjust(thread, event->comm.comm)) {
		dump_printf("problem processing PERF_RECORD_COMM, skipping event.\n");
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

static int sample_type_check(u64 type)
{
	sample_type = type;

	if (!(sample_type & PERF_SAMPLE_CALLCHAIN)) {
		if (sort__has_parent) {
			fprintf(stderr, "selected --sort parent, but no"
					" callchain data. Did you call"
					" perf record without -g?\n");
			return -1;
		}
		if (callchain) {
			fprintf(stderr, "selected -g but no callchain data."
					" Did you call perf record without"
					" -g?\n");
			return -1;
		}
	} else if (callchain_param.mode != CHAIN_NONE && !callchain) {
			callchain = 1;
			if (register_callchain_param(&callchain_param) < 0) {
				fprintf(stderr, "Can't register callchain"
						" params\n");
				return -1;
			}
	}

	return 0;
}

static struct perf_file_handler file_handler = {
	.process_sample_event	= process_sample_event,
	.process_mmap_event	= event__process_mmap,
	.process_comm_event	= process_comm_event,
	.process_exit_event	= event__process_task,
	.process_fork_event	= event__process_task,
	.process_lost_event	= event__process_lost,
	.process_read_event	= process_read_event,
	.sample_type_check	= sample_type_check,
};


static int __cmd_report(void)
{
	struct thread *idle;
	int ret;
	struct perf_session *session;

	session = perf_session__new(input_name, O_RDONLY, force);
	if (session == NULL)
		return -ENOMEM;

	idle = register_idle_thread();
	thread__comm_adjust(idle);

	if (show_threads)
		perf_read_values_init(&show_threads_values);

	register_perf_file_handler(&file_handler);

	ret = perf_session__process_events(session, full_paths,
					   &event__cwdlen, &event__cwd);
	if (ret)
		goto out_delete;

	if (dump_trace) {
		event__print_totals();
		goto out_delete;
	}

	if (verbose > 3)
		threads__fprintf(stdout);

	if (verbose > 2)
		dsos__fprintf(stdout);

	collapse__resort();
	output__resort(event__stats.total);
	output__fprintf(stdout, event__stats.total);

	if (show_threads)
		perf_read_values_destroy(&show_threads_values);
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

	callchain = 1;

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
		callchain = 0;

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

//static const char * const report_usage[] = {
const char * const report_usage[] = {
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
	OPT_BOOLEAN('n', "show-nr-samples", &show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_BOOLEAN('T', "threads", &show_threads,
		    "Show per-thread event counters"),
	OPT_STRING(0, "pretty", &pretty_printing_style, "key",
		   "pretty printing style key: normal raw"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent"),
	OPT_BOOLEAN('P', "full-paths", &full_paths,
		    "Don't shorten the pathnames taking into account the cwd"),
	OPT_STRING('p', "parent", &parent_pattern, "regex",
		   "regex filter to identify parent, see: '--sort parent'"),
	OPT_BOOLEAN('x', "exclude-other", &exclude_other,
		    "Only display entries with parent-match"),
	OPT_CALLBACK_DEFAULT('g', "call-graph", NULL, "output_type,min_percent",
		     "Display callchains using output_type and min percent threshold. "
		     "Default: fractal,0.5", &parse_callchain_opt, callchain_default_opt),
	OPT_STRING('d', "dsos", &dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('C', "comms", &comm_list_str, "comm[,comm...]",
		   "only consider symbols in these comms"),
	OPT_STRING('S', "symbols", &sym_list_str, "symbol[,symbol...]",
		   "only consider these symbols"),
	OPT_STRING('w', "column-widths", &col_width_list_str,
		   "width[,width...]",
		   "don't try to adjust column width, use these fixed values"),
	OPT_STRING('t', "field-separator", &field_sep, "separator",
		   "separator for columns, no spaces will be added between "
		   "columns '.' is reserved."),
	OPT_END()
};

static void setup_sorting(void)
{
	char *tmp, *tok, *str = strdup(sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
			tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (sort_dimension__add(tok) < 0) {
			error("Unknown --sort key: `%s'", tok);
			usage_with_options(report_usage, options);
		}
	}

	free(str);
}

static void setup_list(struct strlist **list, const char *list_str,
		       struct sort_entry *se, const char *list_name,
		       FILE *fp)
{
	if (list_str) {
		*list = strlist__new(true, list_str);
		if (!*list) {
			fprintf(stderr, "problems parsing %s list\n",
				list_name);
			exit(129);
		}
		if (strlist__nr_entries(*list) == 1) {
			fprintf(fp, "# %s: %s\n", list_name,
				strlist__entry(*list, 0)->s);
			se->elide = true;
		}
	}
}

int cmd_report(int argc, const char **argv, const char *prefix __used)
{
	if (symbol__init(&symbol_conf) < 0)
		return -1;

	argc = parse_options(argc, argv, options, report_usage, 0);

	setup_sorting();

	if (parent_pattern != default_parent_pattern) {
		sort_dimension__add("parent");
		sort_parent.elide = 1;
	} else
		exclude_other = 0;

	/*
	 * Any (unrecognized) arguments left?
	 */
	if (argc)
		usage_with_options(report_usage, options);

	setup_pager();

	setup_list(&dso_list, dso_list_str, &sort_dso, "dso", stdout);
	setup_list(&comm_list, comm_list_str, &sort_comm, "comm", stdout);
	setup_list(&sym_list, sym_list_str, &sort_sym, "symbol", stdout);

	if (field_sep && *field_sep == '.') {
		fputs("'.' is the only non valid --field-separator argument\n",
		      stderr);
		exit(129);
	}

	return __cmd_report();
}
