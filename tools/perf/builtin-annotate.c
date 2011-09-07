/*
 * builtin-annotate.c
 *
 * Builtin annotate command: Analyze the perf.data input file,
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

#include "perf.h"
#include "util/debug.h"

#include "util/evlist.h"
#include "util/evsel.h"
#include "util/annotate.h"
#include "util/event.h"
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/thread.h"
#include "util/sort.h"
#include "util/hist.h"
#include "util/session.h"

#include <linux/bitmap.h>

static char		const *input_name = "perf.data";

static bool		force, use_tui, use_stdio;

static bool		full_paths;

static bool		print_line;

static const char *sym_hist_filter;

static const char	*cpu_list;
static DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);

static int perf_evlist__add_sample(struct perf_evlist *evlist,
				   struct perf_sample *sample,
				   struct perf_evsel *evsel,
				   struct addr_location *al)
{
	struct hist_entry *he;
	int ret;

	if (sym_hist_filter != NULL &&
	    (al->sym == NULL || strcmp(sym_hist_filter, al->sym->name) != 0)) {
		/* We're only interested in a symbol named sym_hist_filter */
		if (al->sym != NULL) {
			rb_erase(&al->sym->rb_node,
				 &al->map->dso->symbols[al->map->type]);
			symbol__delete(al->sym);
		}
		return 0;
	}

	he = __hists__add_entry(&evsel->hists, al, NULL, 1);
	if (he == NULL)
		return -ENOMEM;

	ret = 0;
	if (he->ms.sym != NULL) {
		struct annotation *notes = symbol__annotation(he->ms.sym);
		if (notes->src == NULL &&
		    symbol__alloc_hist(he->ms.sym, evlist->nr_entries) < 0)
			return -ENOMEM;

		ret = hist_entry__inc_addr_samples(he, evsel->idx, al->addr);
	}

	evsel->hists.stats.total_period += sample->period;
	hists__inc_nr_events(&evsel->hists, PERF_RECORD_SAMPLE);
	return ret;
}

static int process_sample_event(union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct perf_session *session)
{
	struct addr_location al;

	if (perf_event__preprocess_sample(event, session, &al, sample,
					  symbol__annotate_init) < 0) {
		pr_warning("problem processing %d event, skipping it.\n",
			   event->header.type);
		return -1;
	}

	if (cpu_list && !test_bit(sample->cpu, cpu_bitmap))
		return 0;

	if (!al.filtered &&
	    perf_evlist__add_sample(session->evlist, sample, evsel, &al)) {
		pr_warning("problem incrementing symbol count, "
			   "skipping event\n");
		return -1;
	}

	return 0;
}

static int hist_entry__tty_annotate(struct hist_entry *he, int evidx)
{
	return symbol__tty_annotate(he->ms.sym, he->ms.map, evidx,
				    print_line, full_paths, 0, 0);
}

static void hists__find_annotations(struct hists *self, int evidx)
{
	struct rb_node *nd = rb_first(&self->entries), *next;
	int key = KEY_RIGHT;

	while (nd) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct annotation *notes;

		if (he->ms.sym == NULL || he->ms.map->dso->annotate_warned)
			goto find_next;

		notes = symbol__annotation(he->ms.sym);
		if (notes->src == NULL) {
find_next:
			if (key == KEY_LEFT)
				nd = rb_prev(nd);
			else
				nd = rb_next(nd);
			continue;
		}

		if (use_browser > 0) {
			key = hist_entry__tui_annotate(he, evidx);
			switch (key) {
			case KEY_RIGHT:
				next = rb_next(nd);
				break;
			case KEY_LEFT:
				next = rb_prev(nd);
				break;
			default:
				return;
			}

			if (next != NULL)
				nd = next;
		} else {
			hist_entry__tty_annotate(he, evidx);
			nd = rb_next(nd);
			/*
			 * Since we have a hist_entry per IP for the same
			 * symbol, free he->ms.sym->src to signal we already
			 * processed this symbol.
			 */
			free(notes->src);
			notes->src = NULL;
		}
	}
}

static struct perf_event_ops event_ops = {
	.sample	= process_sample_event,
	.mmap	= perf_event__process_mmap,
	.comm	= perf_event__process_comm,
	.fork	= perf_event__process_task,
	.ordered_samples = true,
	.ordering_requires_timestamps = true,
};

static int __cmd_annotate(void)
{
	int ret;
	struct perf_session *session;
	struct perf_evsel *pos;
	u64 total_nr_samples;

	session = perf_session__new(input_name, O_RDONLY, force, false, &event_ops);
	if (session == NULL)
		return -ENOMEM;

	if (cpu_list) {
		ret = perf_session__cpu_bitmap(session, cpu_list, cpu_bitmap);
		if (ret)
			goto out_delete;
	}

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

	total_nr_samples = 0;
	list_for_each_entry(pos, &session->evlist->entries, node) {
		struct hists *hists = &pos->hists;
		u32 nr_samples = hists->stats.nr_events[PERF_RECORD_SAMPLE];

		if (nr_samples > 0) {
			total_nr_samples += nr_samples;
			hists__collapse_resort(hists);
			hists__output_resort(hists);
			hists__find_annotations(hists, pos->idx);
		}
	}

	if (total_nr_samples == 0) {
		ui__warning("The %s file has no samples!\n", input_name);
		goto out_delete;
	}
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

static const char * const annotate_usage[] = {
	"perf annotate [<options>] <command>",
	NULL
};

static const struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_STRING('d', "dsos", &symbol_conf.dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('s', "symbol", &sym_hist_filter, "symbol",
		    "symbol to annotate"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN(0, "tui", &use_tui, "Use the TUI interface"),
	OPT_BOOLEAN(0, "stdio", &use_stdio, "Use the stdio interface"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('l', "print-line", &print_line,
		    "print matching source lines (may be slow)"),
	OPT_BOOLEAN('P', "full-paths", &full_paths,
		    "Don't shorten the displayed pathnames"),
	OPT_STRING('c', "cpu", &cpu_list, "cpu", "list of cpus to profile"),
	OPT_END()
};

int cmd_annotate(int argc, const char **argv, const char *prefix __used)
{
	argc = parse_options(argc, argv, options, annotate_usage, 0);

	if (use_stdio)
		use_browser = 0;
	else if (use_tui)
		use_browser = 1;

	setup_browser(true);

	symbol_conf.priv_size = sizeof(struct annotation);
	symbol_conf.try_vmlinux_path = true;

	if (symbol__init() < 0)
		return -1;

	setup_sorting(annotate_usage, options);

	if (argc) {
		/*
		 * Special case: if there's an argument left then assume tha
		 * it's a symbol filter:
		 */
		if (argc > 1)
			usage_with_options(annotate_usage, options);

		sym_hist_filter = argv[0];
	}

	if (field_sep && *field_sep == '.') {
		pr_err("'.' is the only non valid --field-separator argument\n");
		return -1;
	}

	return __cmd_annotate();
}
