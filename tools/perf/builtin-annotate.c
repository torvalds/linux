// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-annotate.c
 *
 * Builtin annotate command: Analyze the perf.data input file,
 * look up and read DSOs and symbol information and display
 * a histogram of results, along various sorting keys.
 */
#include "builtin.h"

#include "util/color.h"
#include <linux/list.h>
#include "util/cache.h"
#include <linux/rbtree.h>
#include <linux/zalloc.h>
#include "util/symbol.h"

#include "perf.h"
#include "util/debug.h"

#include "util/evlist.h"
#include "util/evsel.h"
#include "util/annotate.h"
#include "util/event.h"
#include <subcmd/parse-options.h>
#include "util/parse-events.h"
#include "util/sort.h"
#include "util/hist.h"
#include "util/dso.h"
#include "util/machine.h"
#include "util/map.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/data.h"
#include "arch/common.h"
#include "util/block-range.h"
#include "util/map_symbol.h"
#include "util/branch.h"

#include <dlfcn.h>
#include <errno.h>
#include <linux/bitmap.h>
#include <linux/err.h>

struct perf_annotate {
	struct perf_tool tool;
	struct perf_session *session;
	struct annotation_options opts;
#ifdef HAVE_SLANG_SUPPORT
	bool	   use_tui;
#endif
	bool	   use_stdio, use_stdio2;
	bool	   use_gtk;
	bool	   skip_missing;
	bool	   has_br_stack;
	bool	   group_set;
	const char *sym_hist_filter;
	const char *cpu_list;
	DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);
};

/*
 * Given one basic block:
 *
 *	from	to		branch_i
 *	* ----> *
 *		|
 *		| block
 *		v
 *		* ----> *
 *		from	to	branch_i+1
 *
 * where the horizontal are the branches and the vertical is the executed
 * block of instructions.
 *
 * We count, for each 'instruction', the number of blocks that covered it as
 * well as count the ratio each branch is taken.
 *
 * We can do this without knowing the actual instruction stream by keeping
 * track of the address ranges. We break down ranges such that there is no
 * overlap and iterate from the start until the end.
 *
 * @acme: once we parse the objdump output _before_ processing the samples,
 * we can easily fold the branch.cycles IPC bits in.
 */
static void process_basic_block(struct addr_map_symbol *start,
				struct addr_map_symbol *end,
				struct branch_flags *flags)
{
	struct symbol *sym = start->ms.sym;
	struct annotation *notes = sym ? symbol__annotation(sym) : NULL;
	struct block_range_iter iter;
	struct block_range *entry;

	/*
	 * Sanity; NULL isn't executable and the CPU cannot execute backwards
	 */
	if (!start->addr || start->addr > end->addr)
		return;

	iter = block_range__create(start->addr, end->addr);
	if (!block_range_iter__valid(&iter))
		return;

	/*
	 * First block in range is a branch target.
	 */
	entry = block_range_iter(&iter);
	assert(entry->is_target);
	entry->entry++;

	do {
		entry = block_range_iter(&iter);

		entry->coverage++;
		entry->sym = sym;

		if (notes)
			notes->max_coverage = max(notes->max_coverage, entry->coverage);

	} while (block_range_iter__next(&iter));

	/*
	 * Last block in rage is a branch.
	 */
	entry = block_range_iter(&iter);
	assert(entry->is_branch);
	entry->taken++;
	if (flags->predicted)
		entry->pred++;
}

static void process_branch_stack(struct branch_stack *bs, struct addr_location *al,
				 struct perf_sample *sample)
{
	struct addr_map_symbol *prev = NULL;
	struct branch_info *bi;
	int i;

	if (!bs || !bs->nr)
		return;

	bi = sample__resolve_bstack(sample, al);
	if (!bi)
		return;

	for (i = bs->nr - 1; i >= 0; i--) {
		/*
		 * XXX filter against symbol
		 */
		if (prev)
			process_basic_block(prev, &bi[i].from, &bi[i].flags);
		prev = &bi[i].to;
	}

	free(bi);
}

static int hist_iter__branch_callback(struct hist_entry_iter *iter,
				      struct addr_location *al __maybe_unused,
				      bool single __maybe_unused,
				      void *arg __maybe_unused)
{
	struct hist_entry *he = iter->he;
	struct branch_info *bi;
	struct perf_sample *sample = iter->sample;
	struct evsel *evsel = iter->evsel;
	int err;

	bi = he->branch_info;
	err = addr_map_symbol__inc_samples(&bi->from, sample, evsel);

	if (err)
		goto out;

	err = addr_map_symbol__inc_samples(&bi->to, sample, evsel);

out:
	return err;
}

static int process_branch_callback(struct evsel *evsel,
				   struct perf_sample *sample,
				   struct addr_location *al __maybe_unused,
				   struct perf_annotate *ann,
				   struct machine *machine)
{
	struct hist_entry_iter iter = {
		.evsel		= evsel,
		.sample		= sample,
		.add_entry_cb	= hist_iter__branch_callback,
		.hide_unresolved	= symbol_conf.hide_unresolved,
		.ops		= &hist_iter_branch,
	};

	struct addr_location a;

	if (machine__resolve(machine, &a, sample) < 0)
		return -1;

	if (a.sym == NULL)
		return 0;

	if (a.map != NULL)
		a.map->dso->hit = 1;

	hist__account_cycles(sample->branch_stack, al, sample, false, NULL);

	return hist_entry_iter__add(&iter, &a, PERF_MAX_STACK_DEPTH, ann);
}

static bool has_annotation(struct perf_annotate *ann)
{
	return ui__has_annotation() || ann->use_stdio2;
}

static int evsel__add_sample(struct evsel *evsel, struct perf_sample *sample,
			     struct addr_location *al, struct perf_annotate *ann,
			     struct machine *machine)
{
	struct hists *hists = evsel__hists(evsel);
	struct hist_entry *he;
	int ret;

	if ((!ann->has_br_stack || !has_annotation(ann)) &&
	    ann->sym_hist_filter != NULL &&
	    (al->sym == NULL ||
	     strcmp(ann->sym_hist_filter, al->sym->name) != 0)) {
		/* We're only interested in a symbol named sym_hist_filter */
		/*
		 * FIXME: why isn't this done in the symbol_filter when loading
		 * the DSO?
		 */
		if (al->sym != NULL) {
			rb_erase_cached(&al->sym->rb_node,
				 &al->map->dso->symbols);
			symbol__delete(al->sym);
			dso__reset_find_symbol_cache(al->map->dso);
		}
		return 0;
	}

	/*
	 * XXX filtered samples can still have branch entries pointing into our
	 * symbol and are missed.
	 */
	process_branch_stack(sample->branch_stack, al, sample);

	if (ann->has_br_stack && has_annotation(ann))
		return process_branch_callback(evsel, sample, al, ann, machine);

	he = hists__add_entry(hists, al, NULL, NULL, NULL, sample, true);
	if (he == NULL)
		return -ENOMEM;

	ret = hist_entry__inc_addr_samples(he, sample, evsel, al->addr);
	hists__inc_nr_samples(hists, true);
	return ret;
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine)
{
	struct perf_annotate *ann = container_of(tool, struct perf_annotate, tool);
	struct addr_location al;
	int ret = 0;

	if (machine__resolve(machine, &al, sample) < 0) {
		pr_warning("problem processing %d event, skipping it.\n",
			   event->header.type);
		return -1;
	}

	if (ann->cpu_list && !test_bit(sample->cpu, ann->cpu_bitmap))
		goto out_put;

	if (!al.filtered &&
	    evsel__add_sample(evsel, sample, &al, ann, machine)) {
		pr_warning("problem incrementing symbol count, "
			   "skipping event\n");
		ret = -1;
	}
out_put:
	addr_location__put(&al);
	return ret;
}

static int process_feature_event(struct perf_session *session,
				 union perf_event *event)
{
	if (event->feat.feat_id < HEADER_LAST_FEATURE)
		return perf_event__process_feature(session, event);
	return 0;
}

static int hist_entry__tty_annotate(struct hist_entry *he,
				    struct evsel *evsel,
				    struct perf_annotate *ann)
{
	if (!ann->use_stdio2)
		return symbol__tty_annotate(&he->ms, evsel, &ann->opts);

	return symbol__tty_annotate2(&he->ms, evsel, &ann->opts);
}

static void hists__find_annotations(struct hists *hists,
				    struct evsel *evsel,
				    struct perf_annotate *ann)
{
	struct rb_node *nd = rb_first_cached(&hists->entries), *next;
	int key = K_RIGHT;

	while (nd) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct annotation *notes;

		if (he->ms.sym == NULL || he->ms.map->dso->annotate_warned)
			goto find_next;

		if (ann->sym_hist_filter &&
		    (strcmp(he->ms.sym->name, ann->sym_hist_filter) != 0))
			goto find_next;

		notes = symbol__annotation(he->ms.sym);
		if (notes->src == NULL) {
find_next:
			if (key == K_LEFT)
				nd = rb_prev(nd);
			else
				nd = rb_next(nd);
			continue;
		}

		if (use_browser == 2) {
			int ret;
			int (*annotate)(struct hist_entry *he,
					struct evsel *evsel,
					struct hist_browser_timer *hbt);

			annotate = dlsym(perf_gtk_handle,
					 "hist_entry__gtk_annotate");
			if (annotate == NULL) {
				ui__error("GTK browser not found!\n");
				return;
			}

			ret = annotate(he, evsel, NULL);
			if (!ret || !ann->skip_missing)
				return;

			/* skip missing symbols */
			nd = rb_next(nd);
		} else if (use_browser == 1) {
			key = hist_entry__tui_annotate(he, evsel, NULL, &ann->opts);

			switch (key) {
			case -1:
				if (!ann->skip_missing)
					return;
				/* fall through */
			case K_RIGHT:
				next = rb_next(nd);
				break;
			case K_LEFT:
				next = rb_prev(nd);
				break;
			default:
				return;
			}

			if (next != NULL)
				nd = next;
		} else {
			hist_entry__tty_annotate(he, evsel, ann);
			nd = rb_next(nd);
		}
	}
}

static int __cmd_annotate(struct perf_annotate *ann)
{
	int ret;
	struct perf_session *session = ann->session;
	struct evsel *pos;
	u64 total_nr_samples;

	if (ann->cpu_list) {
		ret = perf_session__cpu_bitmap(session, ann->cpu_list,
					       ann->cpu_bitmap);
		if (ret)
			goto out;
	}

	if (!ann->opts.objdump_path) {
		ret = perf_env__lookup_objdump(&session->header.env,
					       &ann->opts.objdump_path);
		if (ret)
			goto out;
	}

	ret = perf_session__process_events(session);
	if (ret)
		goto out;

	if (dump_trace) {
		perf_session__fprintf_nr_events(session, stdout, false);
		evlist__fprintf_nr_events(session->evlist, stdout, false);
		goto out;
	}

	if (verbose > 3)
		perf_session__fprintf(session, stdout);

	if (verbose > 2)
		perf_session__fprintf_dsos(session, stdout);

	total_nr_samples = 0;
	evlist__for_each_entry(session->evlist, pos) {
		struct hists *hists = evsel__hists(pos);
		u32 nr_samples = hists->stats.nr_samples;

		if (nr_samples > 0) {
			total_nr_samples += nr_samples;
			hists__collapse_resort(hists, NULL);
			/* Don't sort callchain */
			evsel__reset_sample_bit(pos, CALLCHAIN);
			evsel__output_resort(pos, NULL);

			if (symbol_conf.event_group && !evsel__is_group_leader(pos))
				continue;

			hists__find_annotations(hists, pos, ann);
		}
	}

	if (total_nr_samples == 0) {
		ui__error("The %s data has no samples!\n", session->data->path);
		goto out;
	}

	if (use_browser == 2) {
		void (*show_annotations)(void);

		show_annotations = dlsym(perf_gtk_handle,
					 "perf_gtk__show_annotations");
		if (show_annotations == NULL) {
			ui__error("GTK browser not found!\n");
			goto out;
		}
		show_annotations();
	}

out:
	return ret;
}

static const char * const annotate_usage[] = {
	"perf annotate [<options>]",
	NULL
};

int cmd_annotate(int argc, const char **argv)
{
	struct perf_annotate annotate = {
		.tool = {
			.sample	= process_sample_event,
			.mmap	= perf_event__process_mmap,
			.mmap2	= perf_event__process_mmap2,
			.comm	= perf_event__process_comm,
			.exit	= perf_event__process_exit,
			.fork	= perf_event__process_fork,
			.namespaces = perf_event__process_namespaces,
			.attr	= perf_event__process_attr,
			.build_id = perf_event__process_build_id,
			.tracing_data   = perf_event__process_tracing_data,
			.id_index	= perf_event__process_id_index,
			.auxtrace_info	= perf_event__process_auxtrace_info,
			.auxtrace	= perf_event__process_auxtrace,
			.feature	= process_feature_event,
			.ordered_events = true,
			.ordering_requires_timestamps = true,
		},
		.opts = annotation__default_options,
	};
	struct perf_data data = {
		.mode  = PERF_DATA_MODE_READ,
	};
	struct itrace_synth_opts itrace_synth_opts = {
		.set = 0,
	};
	struct option options[] = {
	OPT_STRING('i', "input", &input_name, "file",
		    "input file name"),
	OPT_STRING('d', "dsos", &symbol_conf.dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING('s', "symbol", &annotate.sym_hist_filter, "symbol",
		    "symbol to annotate"),
	OPT_BOOLEAN('f', "force", &data.force, "don't complain, do it"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('q', "quiet", &quiet, "do now show any message"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_BOOLEAN(0, "gtk", &annotate.use_gtk, "Use the GTK interface"),
#ifdef HAVE_SLANG_SUPPORT
	OPT_BOOLEAN(0, "tui", &annotate.use_tui, "Use the TUI interface"),
#endif
	OPT_BOOLEAN(0, "stdio", &annotate.use_stdio, "Use the stdio interface"),
	OPT_BOOLEAN(0, "stdio2", &annotate.use_stdio2, "Use the stdio interface"),
	OPT_BOOLEAN(0, "ignore-vmlinux", &symbol_conf.ignore_vmlinux,
                    "don't load vmlinux even if found"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_BOOLEAN('m', "modules", &symbol_conf.use_modules,
		    "load module symbols - WARNING: use only with -k and LIVE kernel"),
	OPT_BOOLEAN('l', "print-line", &annotate.opts.print_lines,
		    "print matching source lines (may be slow)"),
	OPT_BOOLEAN('P', "full-paths", &annotate.opts.full_path,
		    "Don't shorten the displayed pathnames"),
	OPT_BOOLEAN(0, "skip-missing", &annotate.skip_missing,
		    "Skip symbols that cannot be annotated"),
	OPT_BOOLEAN_SET(0, "group", &symbol_conf.event_group,
			&annotate.group_set,
			"Show event group information together"),
	OPT_STRING('C', "cpu", &annotate.cpu_list, "cpu", "list of cpus to profile"),
	OPT_CALLBACK(0, "symfs", NULL, "directory",
		     "Look for files with symbols relative to this directory",
		     symbol__config_symfs),
	OPT_BOOLEAN(0, "source", &annotate.opts.annotate_src,
		    "Interleave source code with assembly code (default)"),
	OPT_BOOLEAN(0, "asm-raw", &annotate.opts.show_asm_raw,
		    "Display raw encoding of assembly instructions (default)"),
	OPT_STRING('M', "disassembler-style", &annotate.opts.disassembler_style, "disassembler style",
		   "Specify disassembler style (e.g. -M intel for intel syntax)"),
	OPT_STRING(0, "prefix", &annotate.opts.prefix, "prefix",
		    "Add prefix to source file path names in programs (with --prefix-strip)"),
	OPT_STRING(0, "prefix-strip", &annotate.opts.prefix_strip, "N",
		    "Strip first N entries of source file path name in programs (with --prefix)"),
	OPT_STRING(0, "objdump", &annotate.opts.objdump_path, "path",
		   "objdump binary to use for disassembly and annotations"),
	OPT_BOOLEAN(0, "demangle", &symbol_conf.demangle,
		    "Enable symbol demangling"),
	OPT_BOOLEAN(0, "demangle-kernel", &symbol_conf.demangle_kernel,
		    "Enable kernel symbol demangling"),
	OPT_BOOLEAN(0, "group", &symbol_conf.event_group,
		    "Show event group information together"),
	OPT_BOOLEAN(0, "show-total-period", &symbol_conf.show_total_period,
		    "Show a column with the sum of periods"),
	OPT_BOOLEAN('n', "show-nr-samples", &symbol_conf.show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_CALLBACK_DEFAULT(0, "stdio-color", NULL, "mode",
			     "'always' (default), 'never' or 'auto' only applicable to --stdio mode",
			     stdio__config_color, "always"),
	OPT_CALLBACK(0, "percent-type", &annotate.opts, "local-period",
		     "Set percent type local/global-period/hits",
		     annotate_parse_percent_type),
	OPT_CALLBACK_OPTARG(0, "itrace", &itrace_synth_opts, NULL, "opts",
			    "Instruction Tracing options\n" ITRACE_HELP,
			    itrace_parse_synth_opts),

	OPT_END()
	};
	int ret;

	set_option_flag(options, 0, "show-total-period", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 0, "show-nr-samples", PARSE_OPT_EXCLUSIVE);


	ret = hists__init();
	if (ret < 0)
		return ret;

	annotation_config__init(&annotate.opts);

	argc = parse_options(argc, argv, options, annotate_usage, 0);
	if (argc) {
		/*
		 * Special case: if there's an argument left then assume that
		 * it's a symbol filter:
		 */
		if (argc > 1)
			usage_with_options(annotate_usage, options);

		annotate.sym_hist_filter = argv[0];
	}

	if (annotate_check_args(&annotate.opts) < 0)
		return -EINVAL;

	if (symbol_conf.show_nr_samples && annotate.use_gtk) {
		pr_err("--show-nr-samples is not available in --gtk mode at this time\n");
		return ret;
	}

	ret = symbol__validate_sym_arguments();
	if (ret)
		return ret;

	if (quiet)
		perf_quiet_option();

	data.path = input_name;

	annotate.session = perf_session__new(&data, &annotate.tool);
	if (IS_ERR(annotate.session))
		return PTR_ERR(annotate.session);

	annotate.session->itrace_synth_opts = &itrace_synth_opts;

	annotate.has_br_stack = perf_header__has_feat(&annotate.session->header,
						      HEADER_BRANCH_STACK);

	if (annotate.group_set)
		evlist__force_leader(annotate.session->evlist);

	ret = symbol__annotation_init();
	if (ret < 0)
		goto out_delete;

	symbol_conf.try_vmlinux_path = true;

	ret = symbol__init(&annotate.session->header.env);
	if (ret < 0)
		goto out_delete;

	if (annotate.use_stdio || annotate.use_stdio2)
		use_browser = 0;
#ifdef HAVE_SLANG_SUPPORT
	else if (annotate.use_tui)
		use_browser = 1;
#endif
	else if (annotate.use_gtk)
		use_browser = 2;

	setup_browser(true);

	/*
	 * Events of different processes may correspond to the same
	 * symbol, we do not care about the processes in annotate,
	 * set sort order to avoid repeated output.
	 */
	sort_order = "dso,symbol";

	/*
	 * Set SORT_MODE__BRANCH so that annotate display IPC/Cycle
	 * if branch info is in perf data in TUI mode.
	 */
	if ((use_browser == 1 || annotate.use_stdio2) && annotate.has_br_stack)
		sort__mode = SORT_MODE__BRANCH;

	if (setup_sorting(NULL) < 0)
		usage_with_options(annotate_usage, options);

	ret = __cmd_annotate(&annotate);

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
