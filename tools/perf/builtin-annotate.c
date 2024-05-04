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

#include "util/debug.h"

#include "util/evlist.h"
#include "util/evsel.h"
#include "util/annotate.h"
#include "util/annotate-data.h"
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
#include "util/util.h"
#include "ui/progress.h"

#include <dlfcn.h>
#include <errno.h>
#include <linux/bitmap.h>
#include <linux/err.h>
#include <inttypes.h>

struct perf_annotate {
	struct perf_tool tool;
	struct perf_session *session;
#ifdef HAVE_SLANG_SUPPORT
	bool	   use_tui;
#endif
	bool	   use_stdio, use_stdio2;
#ifdef HAVE_GTK2_SUPPORT
	bool	   use_gtk;
#endif
	bool	   skip_missing;
	bool	   has_br_stack;
	bool	   group_set;
	bool	   data_type;
	bool	   type_stat;
	bool	   insn_stat;
	float	   min_percent;
	const char *sym_hist_filter;
	const char *cpu_list;
	const char *target_data_type;
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
	struct annotated_branch *branch;

	/*
	 * Sanity; NULL isn't executable and the CPU cannot execute backwards
	 */
	if (!start->addr || start->addr > end->addr)
		return;

	iter = block_range__create(start->addr, end->addr);
	if (!block_range_iter__valid(&iter))
		return;

	branch = annotation__get_branch(notes);

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

		if (branch)
			branch->max_coverage = max(branch->max_coverage, entry->coverage);

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
				   struct addr_location *al,
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
	int ret;

	addr_location__init(&a);
	if (machine__resolve(machine, &a, sample) < 0) {
		ret = -1;
		goto out;
	}

	if (a.sym == NULL) {
		ret = 0;
		goto out;
	}

	if (a.map != NULL)
		dso__set_hit(map__dso(a.map));

	hist__account_cycles(sample->branch_stack, al, sample, false, NULL);

	ret = hist_entry_iter__add(&iter, &a, PERF_MAX_STACK_DEPTH, ann);
out:
	addr_location__exit(&a);
	return ret;
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
			struct dso *dso = map__dso(al->map);

			rb_erase_cached(&al->sym->rb_node, dso__symbols(dso));
			symbol__delete(al->sym);
			dso__reset_find_symbol_cache(dso);
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

	he = hists__add_entry(hists, al, NULL, NULL, NULL, NULL, sample, true);
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

	addr_location__init(&al);
	if (machine__resolve(machine, &al, sample) < 0) {
		pr_warning("problem processing %d event, skipping it.\n",
			   event->header.type);
		ret = -1;
		goto out_put;
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
	addr_location__exit(&al);
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
		return symbol__tty_annotate(&he->ms, evsel);

	return symbol__tty_annotate2(&he->ms, evsel);
}

static void print_annotate_data_stat(struct annotated_data_stat *s)
{
#define PRINT_STAT(fld) if (s->fld) printf("%10d : %s\n", s->fld, #fld)

	int bad = s->no_sym +
			s->no_insn +
			s->no_insn_ops +
			s->no_mem_ops +
			s->no_reg +
			s->no_dbginfo +
			s->no_cuinfo +
			s->no_var +
			s->no_typeinfo +
			s->invalid_size +
			s->bad_offset;
	int ok = s->total - bad;

	printf("Annotate data type stats:\n");
	printf("total %d, ok %d (%.1f%%), bad %d (%.1f%%)\n",
		s->total, ok, 100.0 * ok / (s->total ?: 1), bad, 100.0 * bad / (s->total ?: 1));
	printf("-----------------------------------------------------------\n");
	PRINT_STAT(no_sym);
	PRINT_STAT(no_insn);
	PRINT_STAT(no_insn_ops);
	PRINT_STAT(no_mem_ops);
	PRINT_STAT(no_reg);
	PRINT_STAT(no_dbginfo);
	PRINT_STAT(no_cuinfo);
	PRINT_STAT(no_var);
	PRINT_STAT(no_typeinfo);
	PRINT_STAT(invalid_size);
	PRINT_STAT(bad_offset);
	PRINT_STAT(insn_track);
	printf("\n");

#undef PRINT_STAT
}

static void print_annotate_item_stat(struct list_head *head, const char *title)
{
	struct annotated_item_stat *istat, *pos, *iter;
	int total_good, total_bad, total;
	int sum1, sum2;
	LIST_HEAD(tmp);

	/* sort the list by count */
	list_splice_init(head, &tmp);
	total_good = total_bad = 0;

	list_for_each_entry_safe(istat, pos, &tmp, list) {
		total_good += istat->good;
		total_bad += istat->bad;
		sum1 = istat->good + istat->bad;

		list_for_each_entry(iter, head, list) {
			sum2 = iter->good + iter->bad;
			if (sum1 > sum2)
				break;
		}
		list_move_tail(&istat->list, &iter->list);
	}
	total = total_good + total_bad;

	printf("Annotate %s stats\n", title);
	printf("total %d, ok %d (%.1f%%), bad %d (%.1f%%)\n\n", total,
	       total_good, 100.0 * total_good / (total ?: 1),
	       total_bad, 100.0 * total_bad / (total ?: 1));
	printf("  %-10s: %5s %5s\n", "Name", "Good", "Bad");
	printf("-----------------------------------------------------------\n");
	list_for_each_entry(istat, head, list)
		printf("  %-10s: %5d %5d\n", istat->name, istat->good, istat->bad);
	printf("\n");
}

static void hists__find_annotations(struct hists *hists,
				    struct evsel *evsel,
				    struct perf_annotate *ann)
{
	struct rb_node *nd = rb_first_cached(&hists->entries), *next;
	int key = K_RIGHT;

	if (ann->type_stat)
		print_annotate_data_stat(&ann_data_stat);
	if (ann->insn_stat)
		print_annotate_item_stat(&ann_insn_stat, "Instruction");

	while (nd) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry, rb_node);
		struct annotation *notes;

		if (he->ms.sym == NULL || dso__annotate_warned(map__dso(he->ms.map)))
			goto find_next;

		if (ann->sym_hist_filter &&
		    (strcmp(he->ms.sym->name, ann->sym_hist_filter) != 0))
			goto find_next;

		if (ann->min_percent) {
			float percent = 0;
			u64 total = hists__total_period(hists);

			if (total)
				percent = 100.0 * he->stat.period / total;

			if (percent < ann->min_percent)
				goto find_next;
		}

		notes = symbol__annotation(he->ms.sym);
		if (notes->src == NULL) {
find_next:
			if (key == K_LEFT || key == '<')
				nd = rb_prev(nd);
			else
				nd = rb_next(nd);
			continue;
		}

		if (ann->data_type) {
			/* skip unknown type */
			if (he->mem_type->histograms == NULL)
				goto find_next;

			if (ann->target_data_type) {
				const char *type_name = he->mem_type->self.type_name;

				/* skip 'struct ' prefix in the type name */
				if (strncmp(ann->target_data_type, "struct ", 7) &&
				    !strncmp(type_name, "struct ", 7))
					type_name += 7;

				/* skip 'union ' prefix in the type name */
				if (strncmp(ann->target_data_type, "union ", 6) &&
				    !strncmp(type_name, "union ", 6))
					type_name += 6;

				if (strcmp(ann->target_data_type, type_name))
					goto find_next;
			}

			if (use_browser == 1)
				key = hist_entry__annotate_data_tui(he, evsel, NULL);
			else
				key = hist_entry__annotate_data_tty(he, evsel);

			switch (key) {
			case -1:
				if (!ann->skip_missing)
					return;
				/* fall through */
			case K_RIGHT:
			case '>':
				next = rb_next(nd);
				break;
			case K_LEFT:
			case '<':
				next = rb_prev(nd);
				break;
			default:
				return;
			}

			if (use_browser == 0 || next != NULL)
				nd = next;

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
			key = hist_entry__tui_annotate(he, evsel, NULL);

			switch (key) {
			case -1:
				if (!ann->skip_missing)
					return;
				/* fall through */
			case K_RIGHT:
			case '>':
				next = rb_next(nd);
				break;
			case K_LEFT:
			case '<':
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

	if (!annotate_opts.objdump_path) {
		ret = perf_env__lookup_objdump(&session->header.env,
					       &annotate_opts.objdump_path);
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
		struct ui_progress prog;

		if (nr_samples > 0) {
			total_nr_samples += nr_samples;

			ui_progress__init(&prog, nr_samples,
					  "Merging related events...");
			hists__collapse_resort(hists, &prog);
			ui_progress__finish();

			/* Don't sort callchain */
			evsel__reset_sample_bit(pos, CALLCHAIN);

			ui_progress__init(&prog, nr_samples,
					  "Sorting events for output...");
			evsel__output_resort(pos, &prog);
			ui_progress__finish();

			/*
			 * An event group needs to display other events too.
			 * Let's delay printing until other events are processed.
			 */
			if (symbol_conf.event_group) {
				if (!evsel__is_group_leader(pos)) {
					struct hists *leader_hists;

					leader_hists = evsel__hists(evsel__leader(pos));
					hists__match(leader_hists, hists);
					hists__link(leader_hists, hists);
				}
				continue;
			}

			hists__find_annotations(hists, pos, ann);
		}
	}

	if (total_nr_samples == 0) {
		ui__error("The %s data has no samples!\n", session->data->path);
		goto out;
	}

	/* Display group events together */
	evlist__for_each_entry(session->evlist, pos) {
		struct hists *hists = evsel__hists(pos);
		u32 nr_samples = hists->stats.nr_samples;

		if (nr_samples == 0)
			continue;

		if (!symbol_conf.event_group || !evsel__is_group_leader(pos))
			continue;

		hists__find_annotations(hists, pos, ann);
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

static int parse_percent_limit(const struct option *opt, const char *str,
			       int unset __maybe_unused)
{
	struct perf_annotate *ann = opt->value;
	double pcnt = strtof(str, NULL);

	ann->min_percent = pcnt;
	return 0;
}

static int parse_data_type(const struct option *opt, const char *str, int unset)
{
	struct perf_annotate *ann = opt->value;

	ann->data_type = !unset;
	if (str)
		ann->target_data_type = strdup(str);

	return 0;
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
#ifdef HAVE_LIBTRACEEVENT
			.tracing_data   = perf_event__process_tracing_data,
#endif
			.id_index	= perf_event__process_id_index,
			.auxtrace_info	= perf_event__process_auxtrace_info,
			.auxtrace	= perf_event__process_auxtrace,
			.feature	= process_feature_event,
			.ordered_events = true,
			.ordering_requires_timestamps = true,
		},
	};
	struct perf_data data = {
		.mode  = PERF_DATA_MODE_READ,
	};
	struct itrace_synth_opts itrace_synth_opts = {
		.set = 0,
	};
	const char *disassembler_style = NULL, *objdump_path = NULL, *addr2line_path = NULL;
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
	OPT_BOOLEAN('q', "quiet", &quiet, "do now show any warnings or messages"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
#ifdef HAVE_GTK2_SUPPORT
	OPT_BOOLEAN(0, "gtk", &annotate.use_gtk, "Use the GTK interface"),
#endif
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
	OPT_BOOLEAN('l', "print-line", &annotate_opts.print_lines,
		    "print matching source lines (may be slow)"),
	OPT_BOOLEAN('P', "full-paths", &annotate_opts.full_path,
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
	OPT_BOOLEAN(0, "source", &annotate_opts.annotate_src,
		    "Interleave source code with assembly code (default)"),
	OPT_BOOLEAN(0, "asm-raw", &annotate_opts.show_asm_raw,
		    "Display raw encoding of assembly instructions (default)"),
	OPT_STRING('M', "disassembler-style", &disassembler_style, "disassembler style",
		   "Specify disassembler style (e.g. -M intel for intel syntax)"),
	OPT_STRING(0, "prefix", &annotate_opts.prefix, "prefix",
		    "Add prefix to source file path names in programs (with --prefix-strip)"),
	OPT_STRING(0, "prefix-strip", &annotate_opts.prefix_strip, "N",
		    "Strip first N entries of source file path name in programs (with --prefix)"),
	OPT_STRING(0, "objdump", &objdump_path, "path",
		   "objdump binary to use for disassembly and annotations"),
	OPT_STRING(0, "addr2line", &addr2line_path, "path",
		   "addr2line binary to use for line numbers"),
	OPT_BOOLEAN(0, "demangle", &symbol_conf.demangle,
		    "Enable symbol demangling"),
	OPT_BOOLEAN(0, "demangle-kernel", &symbol_conf.demangle_kernel,
		    "Enable kernel symbol demangling"),
	OPT_BOOLEAN(0, "show-total-period", &symbol_conf.show_total_period,
		    "Show a column with the sum of periods"),
	OPT_BOOLEAN('n', "show-nr-samples", &symbol_conf.show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_CALLBACK_DEFAULT(0, "stdio-color", NULL, "mode",
			     "'always' (default), 'never' or 'auto' only applicable to --stdio mode",
			     stdio__config_color, "always"),
	OPT_CALLBACK(0, "percent-type", &annotate_opts, "local-period",
		     "Set percent type local/global-period/hits",
		     annotate_parse_percent_type),
	OPT_CALLBACK(0, "percent-limit", &annotate, "percent",
		     "Don't show entries under that percent", parse_percent_limit),
	OPT_CALLBACK_OPTARG(0, "itrace", &itrace_synth_opts, NULL, "opts",
			    "Instruction Tracing options\n" ITRACE_HELP,
			    itrace_parse_synth_opts),
	OPT_CALLBACK_OPTARG(0, "data-type", &annotate, NULL, "name",
			    "Show data type annotate for the memory accesses",
			    parse_data_type),
	OPT_BOOLEAN(0, "type-stat", &annotate.type_stat,
		    "Show stats for the data type annotation"),
	OPT_BOOLEAN(0, "insn-stat", &annotate.insn_stat,
		    "Show instruction stats for the data type annotation"),
	OPT_END()
	};
	int ret;

	set_option_flag(options, 0, "show-total-period", PARSE_OPT_EXCLUSIVE);
	set_option_flag(options, 0, "show-nr-samples", PARSE_OPT_EXCLUSIVE);

	annotation_options__init();

	ret = hists__init();
	if (ret < 0)
		return ret;

	annotation_config__init();

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

	if (disassembler_style) {
		annotate_opts.disassembler_style = strdup(disassembler_style);
		if (!annotate_opts.disassembler_style)
			return -ENOMEM;
	}
	if (objdump_path) {
		annotate_opts.objdump_path = strdup(objdump_path);
		if (!annotate_opts.objdump_path)
			return -ENOMEM;
	}
	if (addr2line_path) {
		symbol_conf.addr2line_path = strdup(addr2line_path);
		if (!symbol_conf.addr2line_path)
			return -ENOMEM;
	}

	if (annotate_check_args() < 0)
		return -EINVAL;

#ifdef HAVE_GTK2_SUPPORT
	if (symbol_conf.show_nr_samples && annotate.use_gtk) {
		pr_err("--show-nr-samples is not available in --gtk mode at this time\n");
		return ret;
	}
#endif

#ifndef HAVE_DWARF_GETLOCATIONS_SUPPORT
	if (annotate.data_type) {
		pr_err("Error: Data type profiling is disabled due to missing DWARF support\n");
		return -ENOTSUP;
	}
#endif

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
#ifdef HAVE_GTK2_SUPPORT
	else if (annotate.use_gtk)
		use_browser = 2;
#endif

	if (annotate.data_type) {
		annotate_opts.annotate_src = false;
		symbol_conf.annotate_data_member = true;
		symbol_conf.annotate_data_sample = true;
	}

	setup_browser(true);

	/*
	 * Events of different processes may correspond to the same
	 * symbol, we do not care about the processes in annotate,
	 * set sort order to avoid repeated output.
	 */
	if (annotate.data_type)
		sort_order = "dso,type";
	else
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
	 * Speed up the exit process by only deleting for debug builds. For
	 * large files this can save time.
	 */
#ifndef NDEBUG
	perf_session__delete(annotate.session);
#endif
	annotation_options__exit();

	return ret;
}
