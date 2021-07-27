// SPDX-License-Identifier: GPL-2.0-only
/*
 * builtin-top.c
 *
 * Builtin top command: Display a continuously updated profile of
 * any workload, CPU or specific PID.
 *
 * Copyright (C) 2008, Red Hat Inc, Ingo Molnar <mingo@redhat.com>
 *		 2011, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Improvements and fixes by:
 *
 *   Arjan van de Ven <arjan@linux.intel.com>
 *   Yanmin Zhang <yanmin.zhang@intel.com>
 *   Wu Fengguang <fengguang.wu@intel.com>
 *   Mike Galbraith <efault@gmx.de>
 *   Paul Mackerras <paulus@samba.org>
 */
#include "builtin.h"

#include "perf.h"

#include "util/annotate.h"
#include "util/bpf-event.h"
#include "util/cgroup.h"
#include "util/config.h"
#include "util/color.h"
#include "util/dso.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/evsel_config.h"
#include "util/event.h"
#include "util/machine.h"
#include "util/map.h"
#include "util/mmap.h"
#include "util/session.h"
#include "util/thread.h"
#include "util/symbol.h"
#include "util/synthetic-events.h"
#include "util/top.h"
#include "util/util.h"
#include <linux/rbtree.h>
#include <subcmd/parse-options.h>
#include "util/parse-events.h"
#include "util/callchain.h"
#include "util/cpumap.h"
#include "util/sort.h"
#include "util/string2.h"
#include "util/term.h"
#include "util/intlist.h"
#include "util/parse-branch-options.h"
#include "arch/common.h"
#include "ui/ui.h"

#include "util/debug.h"
#include "util/ordered-events.h"
#include "util/pfm.h"

#include <assert.h>
#include <elf.h>
#include <fcntl.h>

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <inttypes.h>

#include <errno.h>
#include <time.h>
#include <sched.h>
#include <signal.h>

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/mman.h>

#include <linux/stringify.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/err.h>

#include <linux/ctype.h>
#include <perf/mmap.h>

static volatile int done;
static volatile int resize;

#define HEADER_LINE_NR  5

static void perf_top__update_print_entries(struct perf_top *top)
{
	top->print_entries = top->winsize.ws_row - HEADER_LINE_NR;
}

static void winch_sig(int sig __maybe_unused)
{
	resize = 1;
}

static void perf_top__resize(struct perf_top *top)
{
	get_term_dimensions(&top->winsize);
	perf_top__update_print_entries(top);
}

static int perf_top__parse_source(struct perf_top *top, struct hist_entry *he)
{
	struct evsel *evsel;
	struct symbol *sym;
	struct annotation *notes;
	struct map *map;
	int err = -1;

	if (!he || !he->ms.sym)
		return -1;

	evsel = hists_to_evsel(he->hists);

	sym = he->ms.sym;
	map = he->ms.map;

	/*
	 * We can't annotate with just /proc/kallsyms
	 */
	if (map->dso->symtab_type == DSO_BINARY_TYPE__KALLSYMS &&
	    !dso__is_kcore(map->dso)) {
		pr_err("Can't annotate %s: No vmlinux file was found in the "
		       "path\n", sym->name);
		sleep(1);
		return -1;
	}

	notes = symbol__annotation(sym);
	pthread_mutex_lock(&notes->lock);

	if (!symbol__hists(sym, top->evlist->core.nr_entries)) {
		pthread_mutex_unlock(&notes->lock);
		pr_err("Not enough memory for annotating '%s' symbol!\n",
		       sym->name);
		sleep(1);
		return err;
	}

	err = symbol__annotate(&he->ms, evsel, &top->annotation_opts, NULL);
	if (err == 0) {
		top->sym_filter_entry = he;
	} else {
		char msg[BUFSIZ];
		symbol__strerror_disassemble(&he->ms, err, msg, sizeof(msg));
		pr_err("Couldn't annotate %s: %s\n", sym->name, msg);
	}

	pthread_mutex_unlock(&notes->lock);
	return err;
}

static void __zero_source_counters(struct hist_entry *he)
{
	struct symbol *sym = he->ms.sym;
	symbol__annotate_zero_histograms(sym);
}

static void ui__warn_map_erange(struct map *map, struct symbol *sym, u64 ip)
{
	struct utsname uts;
	int err = uname(&uts);

	ui__warning("Out of bounds address found:\n\n"
		    "Addr:   %" PRIx64 "\n"
		    "DSO:    %s %c\n"
		    "Map:    %" PRIx64 "-%" PRIx64 "\n"
		    "Symbol: %" PRIx64 "-%" PRIx64 " %c %s\n"
		    "Arch:   %s\n"
		    "Kernel: %s\n"
		    "Tools:  %s\n\n"
		    "Not all samples will be on the annotation output.\n\n"
		    "Please report to linux-kernel@vger.kernel.org\n",
		    ip, map->dso->long_name, dso__symtab_origin(map->dso),
		    map->start, map->end, sym->start, sym->end,
		    sym->binding == STB_GLOBAL ? 'g' :
		    sym->binding == STB_LOCAL  ? 'l' : 'w', sym->name,
		    err ? "[unknown]" : uts.machine,
		    err ? "[unknown]" : uts.release, perf_version_string);
	if (use_browser <= 0)
		sleep(5);

	map->erange_warned = true;
}

static void perf_top__record_precise_ip(struct perf_top *top,
					struct hist_entry *he,
					struct perf_sample *sample,
					struct evsel *evsel, u64 ip)
{
	struct annotation *notes;
	struct symbol *sym = he->ms.sym;
	int err = 0;

	if (sym == NULL || (use_browser == 0 &&
			    (top->sym_filter_entry == NULL ||
			     top->sym_filter_entry->ms.sym != sym)))
		return;

	notes = symbol__annotation(sym);

	if (pthread_mutex_trylock(&notes->lock))
		return;

	err = hist_entry__inc_addr_samples(he, sample, evsel, ip);

	pthread_mutex_unlock(&notes->lock);

	if (unlikely(err)) {
		/*
		 * This function is now called with he->hists->lock held.
		 * Release it before going to sleep.
		 */
		pthread_mutex_unlock(&he->hists->lock);

		if (err == -ERANGE && !he->ms.map->erange_warned)
			ui__warn_map_erange(he->ms.map, sym, ip);
		else if (err == -ENOMEM) {
			pr_err("Not enough memory for annotating '%s' symbol!\n",
			       sym->name);
			sleep(1);
		}

		pthread_mutex_lock(&he->hists->lock);
	}
}

static void perf_top__show_details(struct perf_top *top)
{
	struct hist_entry *he = top->sym_filter_entry;
	struct evsel *evsel;
	struct annotation *notes;
	struct symbol *symbol;
	int more;

	if (!he)
		return;

	evsel = hists_to_evsel(he->hists);

	symbol = he->ms.sym;
	notes = symbol__annotation(symbol);

	pthread_mutex_lock(&notes->lock);

	symbol__calc_percent(symbol, evsel);

	if (notes->src == NULL)
		goto out_unlock;

	printf("Showing %s for %s\n", evsel__name(top->sym_evsel), symbol->name);
	printf("  Events  Pcnt (>=%d%%)\n", top->annotation_opts.min_pcnt);

	more = symbol__annotate_printf(&he->ms, top->sym_evsel, &top->annotation_opts);

	if (top->evlist->enabled) {
		if (top->zero)
			symbol__annotate_zero_histogram(symbol, top->sym_evsel->core.idx);
		else
			symbol__annotate_decay_histogram(symbol, top->sym_evsel->core.idx);
	}
	if (more != 0)
		printf("%d lines not displayed, maybe increase display entries [e]\n", more);
out_unlock:
	pthread_mutex_unlock(&notes->lock);
}

static void perf_top__resort_hists(struct perf_top *t)
{
	struct evlist *evlist = t->evlist;
	struct evsel *pos;

	evlist__for_each_entry(evlist, pos) {
		struct hists *hists = evsel__hists(pos);

		/*
		 * unlink existing entries so that they can be linked
		 * in a correct order in hists__match() below.
		 */
		hists__unlink(hists);

		if (evlist->enabled) {
			if (t->zero) {
				hists__delete_entries(hists);
			} else {
				hists__decay_entries(hists, t->hide_user_symbols,
						     t->hide_kernel_symbols);
			}
		}

		hists__collapse_resort(hists, NULL);

		/* Non-group events are considered as leader */
		if (symbol_conf.event_group && !evsel__is_group_leader(pos)) {
			struct hists *leader_hists = evsel__hists(evsel__leader(pos));

			hists__match(leader_hists, hists);
			hists__link(leader_hists, hists);
		}
	}

	evlist__for_each_entry(evlist, pos) {
		evsel__output_resort(pos, NULL);
	}
}

static void perf_top__print_sym_table(struct perf_top *top)
{
	char bf[160];
	int printed = 0;
	const int win_width = top->winsize.ws_col - 1;
	struct evsel *evsel = top->sym_evsel;
	struct hists *hists = evsel__hists(evsel);

	puts(CONSOLE_CLEAR);

	perf_top__header_snprintf(top, bf, sizeof(bf));
	printf("%s\n", bf);

	printf("%-*.*s\n", win_width, win_width, graph_dotted_line);

	if (!top->record_opts.overwrite &&
	    (top->evlist->stats.nr_lost_warned !=
	     top->evlist->stats.nr_events[PERF_RECORD_LOST])) {
		top->evlist->stats.nr_lost_warned =
			      top->evlist->stats.nr_events[PERF_RECORD_LOST];
		color_fprintf(stdout, PERF_COLOR_RED,
			      "WARNING: LOST %d chunks, Check IO/CPU overload",
			      top->evlist->stats.nr_lost_warned);
		++printed;
	}

	if (top->sym_filter_entry) {
		perf_top__show_details(top);
		return;
	}

	perf_top__resort_hists(top);

	hists__output_recalc_col_len(hists, top->print_entries - printed);
	putchar('\n');
	hists__fprintf(hists, false, top->print_entries - printed, win_width,
		       top->min_percent, stdout, !symbol_conf.use_callchain);
}

static void prompt_integer(int *target, const char *msg)
{
	char *buf = malloc(0), *p;
	size_t dummy = 0;
	int tmp;

	fprintf(stdout, "\n%s: ", msg);
	if (getline(&buf, &dummy, stdin) < 0)
		return;

	p = strchr(buf, '\n');
	if (p)
		*p = 0;

	p = buf;
	while(*p) {
		if (!isdigit(*p))
			goto out_free;
		p++;
	}
	tmp = strtoul(buf, NULL, 10);
	*target = tmp;
out_free:
	free(buf);
}

static void prompt_percent(int *target, const char *msg)
{
	int tmp = 0;

	prompt_integer(&tmp, msg);
	if (tmp >= 0 && tmp <= 100)
		*target = tmp;
}

static void perf_top__prompt_symbol(struct perf_top *top, const char *msg)
{
	char *buf = malloc(0), *p;
	struct hist_entry *syme = top->sym_filter_entry, *n, *found = NULL;
	struct hists *hists = evsel__hists(top->sym_evsel);
	struct rb_node *next;
	size_t dummy = 0;

	/* zero counters of active symbol */
	if (syme) {
		__zero_source_counters(syme);
		top->sym_filter_entry = NULL;
	}

	fprintf(stdout, "\n%s: ", msg);
	if (getline(&buf, &dummy, stdin) < 0)
		goto out_free;

	p = strchr(buf, '\n');
	if (p)
		*p = 0;

	next = rb_first_cached(&hists->entries);
	while (next) {
		n = rb_entry(next, struct hist_entry, rb_node);
		if (n->ms.sym && !strcmp(buf, n->ms.sym->name)) {
			found = n;
			break;
		}
		next = rb_next(&n->rb_node);
	}

	if (!found) {
		fprintf(stderr, "Sorry, %s is not active.\n", buf);
		sleep(1);
	} else
		perf_top__parse_source(top, found);

out_free:
	free(buf);
}

static void perf_top__print_mapped_keys(struct perf_top *top)
{
	char *name = NULL;

	if (top->sym_filter_entry) {
		struct symbol *sym = top->sym_filter_entry->ms.sym;
		name = sym->name;
	}

	fprintf(stdout, "\nMapped keys:\n");
	fprintf(stdout, "\t[d]     display refresh delay.             \t(%d)\n", top->delay_secs);
	fprintf(stdout, "\t[e]     display entries (lines).           \t(%d)\n", top->print_entries);

	if (top->evlist->core.nr_entries > 1)
		fprintf(stdout, "\t[E]     active event counter.              \t(%s)\n", evsel__name(top->sym_evsel));

	fprintf(stdout, "\t[f]     profile display filter (count).    \t(%d)\n", top->count_filter);

	fprintf(stdout, "\t[F]     annotate display filter (percent). \t(%d%%)\n", top->annotation_opts.min_pcnt);
	fprintf(stdout, "\t[s]     annotate symbol.                   \t(%s)\n", name?: "NULL");
	fprintf(stdout, "\t[S]     stop annotation.\n");

	fprintf(stdout,
		"\t[K]     hide kernel symbols.             \t(%s)\n",
		top->hide_kernel_symbols ? "yes" : "no");
	fprintf(stdout,
		"\t[U]     hide user symbols.               \t(%s)\n",
		top->hide_user_symbols ? "yes" : "no");
	fprintf(stdout, "\t[z]     toggle sample zeroing.             \t(%d)\n", top->zero ? 1 : 0);
	fprintf(stdout, "\t[qQ]    quit.\n");
}

static int perf_top__key_mapped(struct perf_top *top, int c)
{
	switch (c) {
		case 'd':
		case 'e':
		case 'f':
		case 'z':
		case 'q':
		case 'Q':
		case 'K':
		case 'U':
		case 'F':
		case 's':
		case 'S':
			return 1;
		case 'E':
			return top->evlist->core.nr_entries > 1 ? 1 : 0;
		default:
			break;
	}

	return 0;
}

static bool perf_top__handle_keypress(struct perf_top *top, int c)
{
	bool ret = true;

	if (!perf_top__key_mapped(top, c)) {
		struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };
		struct termios save;

		perf_top__print_mapped_keys(top);
		fprintf(stdout, "\nEnter selection, or unmapped key to continue: ");
		fflush(stdout);

		set_term_quiet_input(&save);

		poll(&stdin_poll, 1, -1);
		c = getc(stdin);

		tcsetattr(0, TCSAFLUSH, &save);
		if (!perf_top__key_mapped(top, c))
			return ret;
	}

	switch (c) {
		case 'd':
			prompt_integer(&top->delay_secs, "Enter display delay");
			if (top->delay_secs < 1)
				top->delay_secs = 1;
			break;
		case 'e':
			prompt_integer(&top->print_entries, "Enter display entries (lines)");
			if (top->print_entries == 0) {
				perf_top__resize(top);
				signal(SIGWINCH, winch_sig);
			} else {
				signal(SIGWINCH, SIG_DFL);
			}
			break;
		case 'E':
			if (top->evlist->core.nr_entries > 1) {
				/* Select 0 as the default event: */
				int counter = 0;

				fprintf(stderr, "\nAvailable events:");

				evlist__for_each_entry(top->evlist, top->sym_evsel)
					fprintf(stderr, "\n\t%d %s", top->sym_evsel->core.idx, evsel__name(top->sym_evsel));

				prompt_integer(&counter, "Enter details event counter");

				if (counter >= top->evlist->core.nr_entries) {
					top->sym_evsel = evlist__first(top->evlist);
					fprintf(stderr, "Sorry, no such event, using %s.\n", evsel__name(top->sym_evsel));
					sleep(1);
					break;
				}
				evlist__for_each_entry(top->evlist, top->sym_evsel)
					if (top->sym_evsel->core.idx == counter)
						break;
			} else
				top->sym_evsel = evlist__first(top->evlist);
			break;
		case 'f':
			prompt_integer(&top->count_filter, "Enter display event count filter");
			break;
		case 'F':
			prompt_percent(&top->annotation_opts.min_pcnt,
				       "Enter details display event filter (percent)");
			break;
		case 'K':
			top->hide_kernel_symbols = !top->hide_kernel_symbols;
			break;
		case 'q':
		case 'Q':
			printf("exiting.\n");
			if (top->dump_symtab)
				perf_session__fprintf_dsos(top->session, stderr);
			ret = false;
			break;
		case 's':
			perf_top__prompt_symbol(top, "Enter details symbol");
			break;
		case 'S':
			if (!top->sym_filter_entry)
				break;
			else {
				struct hist_entry *syme = top->sym_filter_entry;

				top->sym_filter_entry = NULL;
				__zero_source_counters(syme);
			}
			break;
		case 'U':
			top->hide_user_symbols = !top->hide_user_symbols;
			break;
		case 'z':
			top->zero = !top->zero;
			break;
		default:
			break;
	}

	return ret;
}

static void perf_top__sort_new_samples(void *arg)
{
	struct perf_top *t = arg;

	if (t->evlist->selected != NULL)
		t->sym_evsel = t->evlist->selected;

	perf_top__resort_hists(t);

	if (t->lost || t->drop)
		pr_warning("Too slow to read ring buffer (change period (-c/-F) or limit CPUs (-C)\n");
}

static void stop_top(void)
{
	session_done = 1;
	done = 1;
}

static void *display_thread_tui(void *arg)
{
	struct evsel *pos;
	struct perf_top *top = arg;
	const char *help = "For a higher level overview, try: perf top --sort comm,dso";
	struct hist_browser_timer hbt = {
		.timer		= perf_top__sort_new_samples,
		.arg		= top,
		.refresh	= top->delay_secs,
	};
	int ret;

	/* In order to read symbols from other namespaces perf to  needs to call
	 * setns(2).  This isn't permitted if the struct_fs has multiple users.
	 * unshare(2) the fs so that we may continue to setns into namespaces
	 * that we're observing.
	 */
	unshare(CLONE_FS);

	prctl(PR_SET_NAME, "perf-top-UI", 0, 0, 0);

repeat:
	perf_top__sort_new_samples(top);

	/*
	 * Initialize the uid_filter_str, in the future the TUI will allow
	 * Zooming in/out UIDs. For now just use whatever the user passed
	 * via --uid.
	 */
	evlist__for_each_entry(top->evlist, pos) {
		struct hists *hists = evsel__hists(pos);
		hists->uid_filter_str = top->record_opts.target.uid_str;
	}

	ret = evlist__tui_browse_hists(top->evlist, help, &hbt, top->min_percent,
				       &top->session->header.env, !top->record_opts.overwrite,
				       &top->annotation_opts);
	if (ret == K_RELOAD) {
		top->zero = true;
		goto repeat;
	} else
		stop_top();

	return NULL;
}

static void display_sig(int sig __maybe_unused)
{
	stop_top();
}

static void display_setup_sig(void)
{
	signal(SIGSEGV, sighandler_dump_stack);
	signal(SIGFPE, sighandler_dump_stack);
	signal(SIGINT,  display_sig);
	signal(SIGQUIT, display_sig);
	signal(SIGTERM, display_sig);
}

static void *display_thread(void *arg)
{
	struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };
	struct termios save;
	struct perf_top *top = arg;
	int delay_msecs, c;

	/* In order to read symbols from other namespaces perf to  needs to call
	 * setns(2).  This isn't permitted if the struct_fs has multiple users.
	 * unshare(2) the fs so that we may continue to setns into namespaces
	 * that we're observing.
	 */
	unshare(CLONE_FS);

	prctl(PR_SET_NAME, "perf-top-UI", 0, 0, 0);

	display_setup_sig();
	pthread__unblock_sigwinch();
repeat:
	delay_msecs = top->delay_secs * MSEC_PER_SEC;
	set_term_quiet_input(&save);
	/* trash return*/
	clearerr(stdin);
	if (poll(&stdin_poll, 1, 0) > 0)
		getc(stdin);

	while (!done) {
		perf_top__print_sym_table(top);
		/*
		 * Either timeout expired or we got an EINTR due to SIGWINCH,
		 * refresh screen in both cases.
		 */
		switch (poll(&stdin_poll, 1, delay_msecs)) {
		case 0:
			continue;
		case -1:
			if (errno == EINTR)
				continue;
			__fallthrough;
		default:
			c = getc(stdin);
			tcsetattr(0, TCSAFLUSH, &save);

			if (perf_top__handle_keypress(top, c))
				goto repeat;
			stop_top();
		}
	}

	tcsetattr(0, TCSAFLUSH, &save);
	return NULL;
}

static int hist_iter__top_callback(struct hist_entry_iter *iter,
				   struct addr_location *al, bool single,
				   void *arg)
{
	struct perf_top *top = arg;
	struct hist_entry *he = iter->he;
	struct evsel *evsel = iter->evsel;

	if (perf_hpp_list.sym && single)
		perf_top__record_precise_ip(top, he, iter->sample, evsel, al->addr);

	hist__account_cycles(iter->sample->branch_stack, al, iter->sample,
		     !(top->record_opts.branch_stack & PERF_SAMPLE_BRANCH_ANY),
		     NULL);
	return 0;
}

static void perf_event__process_sample(struct perf_tool *tool,
				       const union perf_event *event,
				       struct evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_top *top = container_of(tool, struct perf_top, tool);
	struct addr_location al;
	int err;

	if (!machine && perf_guest) {
		static struct intlist *seen;

		if (!seen)
			seen = intlist__new(NULL);

		if (!intlist__has_entry(seen, sample->pid)) {
			pr_err("Can't find guest [%d]'s kernel information\n",
				sample->pid);
			intlist__add(seen, sample->pid);
		}
		return;
	}

	if (!machine) {
		pr_err("%u unprocessable samples recorded.\r",
		       top->session->evlist->stats.nr_unprocessable_samples++);
		return;
	}

	if (event->header.misc & PERF_RECORD_MISC_EXACT_IP)
		top->exact_samples++;

	if (machine__resolve(machine, &al, sample) < 0)
		return;

	if (top->stitch_lbr)
		al.thread->lbr_stitch_enable = true;

	if (!machine->kptr_restrict_warned &&
	    symbol_conf.kptr_restrict &&
	    al.cpumode == PERF_RECORD_MISC_KERNEL) {
		if (!evlist__exclude_kernel(top->session->evlist)) {
			ui__warning(
"Kernel address maps (/proc/{kallsyms,modules}) are restricted.\n\n"
"Check /proc/sys/kernel/kptr_restrict and /proc/sys/kernel/perf_event_paranoid.\n\n"
"Kernel%s samples will not be resolved.\n",
			  al.map && map__has_symbols(al.map) ?
			  " modules" : "");
			if (use_browser <= 0)
				sleep(5);
		}
		machine->kptr_restrict_warned = true;
	}

	if (al.sym == NULL && al.map != NULL) {
		const char *msg = "Kernel samples will not be resolved.\n";
		/*
		 * As we do lazy loading of symtabs we only will know if the
		 * specified vmlinux file is invalid when we actually have a
		 * hit in kernel space and then try to load it. So if we get
		 * here and there are _no_ symbols in the DSO backing the
		 * kernel map, bail out.
		 *
		 * We may never get here, for instance, if we use -K/
		 * --hide-kernel-symbols, even if the user specifies an
		 * invalid --vmlinux ;-)
		 */
		if (!machine->kptr_restrict_warned && !top->vmlinux_warned &&
		    __map__is_kernel(al.map) && map__has_symbols(al.map)) {
			if (symbol_conf.vmlinux_name) {
				char serr[256];
				dso__strerror_load(al.map->dso, serr, sizeof(serr));
				ui__warning("The %s file can't be used: %s\n%s",
					    symbol_conf.vmlinux_name, serr, msg);
			} else {
				ui__warning("A vmlinux file was not found.\n%s",
					    msg);
			}

			if (use_browser <= 0)
				sleep(5);
			top->vmlinux_warned = true;
		}
	}

	if (al.sym == NULL || !al.sym->idle) {
		struct hists *hists = evsel__hists(evsel);
		struct hist_entry_iter iter = {
			.evsel		= evsel,
			.sample 	= sample,
			.add_entry_cb 	= hist_iter__top_callback,
		};

		if (symbol_conf.cumulate_callchain)
			iter.ops = &hist_iter_cumulative;
		else
			iter.ops = &hist_iter_normal;

		pthread_mutex_lock(&hists->lock);

		err = hist_entry_iter__add(&iter, &al, top->max_stack, top);
		if (err < 0)
			pr_err("Problem incrementing symbol period, skipping event\n");

		pthread_mutex_unlock(&hists->lock);
	}

	addr_location__put(&al);
}

static void
perf_top__process_lost(struct perf_top *top, union perf_event *event,
		       struct evsel *evsel)
{
	top->lost += event->lost.lost;
	top->lost_total += event->lost.lost;
	evsel->evlist->stats.total_lost += event->lost.lost;
}

static void
perf_top__process_lost_samples(struct perf_top *top,
			       union perf_event *event,
			       struct evsel *evsel)
{
	top->lost += event->lost_samples.lost;
	top->lost_total += event->lost_samples.lost;
	evsel->evlist->stats.total_lost_samples += event->lost_samples.lost;
}

static u64 last_timestamp;

static void perf_top__mmap_read_idx(struct perf_top *top, int idx)
{
	struct record_opts *opts = &top->record_opts;
	struct evlist *evlist = top->evlist;
	struct mmap *md;
	union perf_event *event;

	md = opts->overwrite ? &evlist->overwrite_mmap[idx] : &evlist->mmap[idx];
	if (perf_mmap__read_init(&md->core) < 0)
		return;

	while ((event = perf_mmap__read_event(&md->core)) != NULL) {
		int ret;

		ret = evlist__parse_sample_timestamp(evlist, event, &last_timestamp);
		if (ret && ret != -1)
			break;

		ret = ordered_events__queue(top->qe.in, event, last_timestamp, 0);
		if (ret)
			break;

		perf_mmap__consume(&md->core);

		if (top->qe.rotate) {
			pthread_mutex_lock(&top->qe.mutex);
			top->qe.rotate = false;
			pthread_cond_signal(&top->qe.cond);
			pthread_mutex_unlock(&top->qe.mutex);
		}
	}

	perf_mmap__read_done(&md->core);
}

static void perf_top__mmap_read(struct perf_top *top)
{
	bool overwrite = top->record_opts.overwrite;
	struct evlist *evlist = top->evlist;
	int i;

	if (overwrite)
		evlist__toggle_bkw_mmap(evlist, BKW_MMAP_DATA_PENDING);

	for (i = 0; i < top->evlist->core.nr_mmaps; i++)
		perf_top__mmap_read_idx(top, i);

	if (overwrite) {
		evlist__toggle_bkw_mmap(evlist, BKW_MMAP_EMPTY);
		evlist__toggle_bkw_mmap(evlist, BKW_MMAP_RUNNING);
	}
}

/*
 * Check per-event overwrite term.
 * perf top should support consistent term for all events.
 * - All events don't have per-event term
 *   E.g. "cpu/cpu-cycles/,cpu/instructions/"
 *   Nothing change, return 0.
 * - All events have same per-event term
 *   E.g. "cpu/cpu-cycles,no-overwrite/,cpu/instructions,no-overwrite/
 *   Using the per-event setting to replace the opts->overwrite if
 *   they are different, then return 0.
 * - Events have different per-event term
 *   E.g. "cpu/cpu-cycles,overwrite/,cpu/instructions,no-overwrite/"
 *   Return -1
 * - Some of the event set per-event term, but some not.
 *   E.g. "cpu/cpu-cycles/,cpu/instructions,no-overwrite/"
 *   Return -1
 */
static int perf_top__overwrite_check(struct perf_top *top)
{
	struct record_opts *opts = &top->record_opts;
	struct evlist *evlist = top->evlist;
	struct evsel_config_term *term;
	struct list_head *config_terms;
	struct evsel *evsel;
	int set, overwrite = -1;

	evlist__for_each_entry(evlist, evsel) {
		set = -1;
		config_terms = &evsel->config_terms;
		list_for_each_entry(term, config_terms, list) {
			if (term->type == EVSEL__CONFIG_TERM_OVERWRITE)
				set = term->val.overwrite ? 1 : 0;
		}

		/* no term for current and previous event (likely) */
		if ((overwrite < 0) && (set < 0))
			continue;

		/* has term for both current and previous event, compare */
		if ((overwrite >= 0) && (set >= 0) && (overwrite != set))
			return -1;

		/* no term for current event but has term for previous one */
		if ((overwrite >= 0) && (set < 0))
			return -1;

		/* has term for current event */
		if ((overwrite < 0) && (set >= 0)) {
			/* if it's first event, set overwrite */
			if (evsel == evlist__first(evlist))
				overwrite = set;
			else
				return -1;
		}
	}

	if ((overwrite >= 0) && (opts->overwrite != overwrite))
		opts->overwrite = overwrite;

	return 0;
}

static int perf_top_overwrite_fallback(struct perf_top *top,
				       struct evsel *evsel)
{
	struct record_opts *opts = &top->record_opts;
	struct evlist *evlist = top->evlist;
	struct evsel *counter;

	if (!opts->overwrite)
		return 0;

	/* only fall back when first event fails */
	if (evsel != evlist__first(evlist))
		return 0;

	evlist__for_each_entry(evlist, counter)
		counter->core.attr.write_backward = false;
	opts->overwrite = false;
	pr_debug2("fall back to non-overwrite mode\n");
	return 1;
}

static int perf_top__start_counters(struct perf_top *top)
{
	char msg[BUFSIZ];
	struct evsel *counter;
	struct evlist *evlist = top->evlist;
	struct record_opts *opts = &top->record_opts;

	if (perf_top__overwrite_check(top)) {
		ui__error("perf top only support consistent per-event "
			  "overwrite setting for all events\n");
		goto out_err;
	}

	evlist__config(evlist, opts, &callchain_param);

	evlist__for_each_entry(evlist, counter) {
try_again:
		if (evsel__open(counter, top->evlist->core.cpus,
				     top->evlist->core.threads) < 0) {

			/*
			 * Specially handle overwrite fall back.
			 * Because perf top is the only tool which has
			 * overwrite mode by default, support
			 * both overwrite and non-overwrite mode, and
			 * require consistent mode for all events.
			 *
			 * May move it to generic code with more tools
			 * have similar attribute.
			 */
			if (perf_missing_features.write_backward &&
			    perf_top_overwrite_fallback(top, counter))
				goto try_again;

			if (evsel__fallback(counter, errno, msg, sizeof(msg))) {
				if (verbose > 0)
					ui__warning("%s\n", msg);
				goto try_again;
			}

			evsel__open_strerror(counter, &opts->target, errno, msg, sizeof(msg));
			ui__error("%s\n", msg);
			goto out_err;
		}
	}

	if (evlist__mmap(evlist, opts->mmap_pages) < 0) {
		ui__error("Failed to mmap with %d (%s)\n",
			    errno, str_error_r(errno, msg, sizeof(msg)));
		goto out_err;
	}

	return 0;

out_err:
	return -1;
}

static int callchain_param__setup_sample_type(struct callchain_param *callchain)
{
	if (callchain->mode != CHAIN_NONE) {
		if (callchain_register_param(callchain) < 0) {
			ui__error("Can't register callchain params.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static struct ordered_events *rotate_queues(struct perf_top *top)
{
	struct ordered_events *in = top->qe.in;

	if (top->qe.in == &top->qe.data[1])
		top->qe.in = &top->qe.data[0];
	else
		top->qe.in = &top->qe.data[1];

	return in;
}

static void *process_thread(void *arg)
{
	struct perf_top *top = arg;

	while (!done) {
		struct ordered_events *out, *in = top->qe.in;

		if (!in->nr_events) {
			usleep(100);
			continue;
		}

		out = rotate_queues(top);

		pthread_mutex_lock(&top->qe.mutex);
		top->qe.rotate = true;
		pthread_cond_wait(&top->qe.cond, &top->qe.mutex);
		pthread_mutex_unlock(&top->qe.mutex);

		if (ordered_events__flush(out, OE_FLUSH__TOP))
			pr_err("failed to process events\n");
	}

	return NULL;
}

/*
 * Allow only 'top->delay_secs' seconds behind samples.
 */
static int should_drop(struct ordered_event *qevent, struct perf_top *top)
{
	union perf_event *event = qevent->event;
	u64 delay_timestamp;

	if (event->header.type != PERF_RECORD_SAMPLE)
		return false;

	delay_timestamp = qevent->timestamp + top->delay_secs * NSEC_PER_SEC;
	return delay_timestamp < last_timestamp;
}

static int deliver_event(struct ordered_events *qe,
			 struct ordered_event *qevent)
{
	struct perf_top *top = qe->data;
	struct evlist *evlist = top->evlist;
	struct perf_session *session = top->session;
	union perf_event *event = qevent->event;
	struct perf_sample sample;
	struct evsel *evsel;
	struct machine *machine;
	int ret = -1;

	if (should_drop(qevent, top)) {
		top->drop++;
		top->drop_total++;
		return 0;
	}

	ret = evlist__parse_sample(evlist, event, &sample);
	if (ret) {
		pr_err("Can't parse sample, err = %d\n", ret);
		goto next_event;
	}

	evsel = evlist__id2evsel(session->evlist, sample.id);
	assert(evsel != NULL);

	if (event->header.type == PERF_RECORD_SAMPLE) {
		if (evswitch__discard(&top->evswitch, evsel))
			return 0;
		++top->samples;
	}

	switch (sample.cpumode) {
	case PERF_RECORD_MISC_USER:
		++top->us_samples;
		if (top->hide_user_symbols)
			goto next_event;
		machine = &session->machines.host;
		break;
	case PERF_RECORD_MISC_KERNEL:
		++top->kernel_samples;
		if (top->hide_kernel_symbols)
			goto next_event;
		machine = &session->machines.host;
		break;
	case PERF_RECORD_MISC_GUEST_KERNEL:
		++top->guest_kernel_samples;
		machine = perf_session__find_machine(session,
						     sample.pid);
		break;
	case PERF_RECORD_MISC_GUEST_USER:
		++top->guest_us_samples;
		/*
		 * TODO: we don't process guest user from host side
		 * except simple counting.
		 */
		goto next_event;
	default:
		if (event->header.type == PERF_RECORD_SAMPLE)
			goto next_event;
		machine = &session->machines.host;
		break;
	}

	if (event->header.type == PERF_RECORD_SAMPLE) {
		perf_event__process_sample(&top->tool, event, evsel,
					   &sample, machine);
	} else if (event->header.type == PERF_RECORD_LOST) {
		perf_top__process_lost(top, event, evsel);
	} else if (event->header.type == PERF_RECORD_LOST_SAMPLES) {
		perf_top__process_lost_samples(top, event, evsel);
	} else if (event->header.type < PERF_RECORD_MAX) {
		events_stats__inc(&session->evlist->stats, event->header.type);
		machine__process_event(machine, event, &sample);
	} else
		++session->evlist->stats.nr_unknown_events;

	ret = 0;
next_event:
	return ret;
}

static void init_process_thread(struct perf_top *top)
{
	ordered_events__init(&top->qe.data[0], deliver_event, top);
	ordered_events__init(&top->qe.data[1], deliver_event, top);
	ordered_events__set_copy_on_queue(&top->qe.data[0], true);
	ordered_events__set_copy_on_queue(&top->qe.data[1], true);
	top->qe.in = &top->qe.data[0];
	pthread_mutex_init(&top->qe.mutex, NULL);
	pthread_cond_init(&top->qe.cond, NULL);
}

static int __cmd_top(struct perf_top *top)
{
	struct record_opts *opts = &top->record_opts;
	pthread_t thread, thread_process;
	int ret;

	if (!top->annotation_opts.objdump_path) {
		ret = perf_env__lookup_objdump(&top->session->header.env,
					       &top->annotation_opts.objdump_path);
		if (ret)
			return ret;
	}

	ret = callchain_param__setup_sample_type(&callchain_param);
	if (ret)
		return ret;

	if (perf_session__register_idle_thread(top->session) < 0)
		return ret;

	if (top->nr_threads_synthesize > 1)
		perf_set_multithreaded();

	init_process_thread(top);

	if (opts->record_namespaces)
		top->tool.namespace_events = true;
	if (opts->record_cgroup) {
#ifdef HAVE_FILE_HANDLE
		top->tool.cgroup_events = true;
#else
		pr_err("cgroup tracking is not supported.\n");
		return -1;
#endif
	}

	ret = perf_event__synthesize_bpf_events(top->session, perf_event__process,
						&top->session->machines.host,
						&top->record_opts);
	if (ret < 0)
		pr_debug("Couldn't synthesize BPF events: Pre-existing BPF programs won't have symbols resolved.\n");

	ret = perf_event__synthesize_cgroups(&top->tool, perf_event__process,
					     &top->session->machines.host);
	if (ret < 0)
		pr_debug("Couldn't synthesize cgroup events.\n");

	machine__synthesize_threads(&top->session->machines.host, &opts->target,
				    top->evlist->core.threads, false,
				    top->nr_threads_synthesize);

	if (top->nr_threads_synthesize > 1)
		perf_set_singlethreaded();

	if (perf_hpp_list.socket) {
		ret = perf_env__read_cpu_topology_map(&perf_env);
		if (ret < 0) {
			char errbuf[BUFSIZ];
			const char *err = str_error_r(-ret, errbuf, sizeof(errbuf));

			ui__error("Could not read the CPU topology map: %s\n", err);
			return ret;
		}
	}

	ret = perf_top__start_counters(top);
	if (ret)
		return ret;

	top->session->evlist = top->evlist;
	perf_session__set_id_hdr_size(top->session);

	/*
	 * When perf is starting the traced process, all the events (apart from
	 * group members) have enable_on_exec=1 set, so don't spoil it by
	 * prematurely enabling them.
	 *
	 * XXX 'top' still doesn't start workloads like record, trace, but should,
	 * so leave the check here.
	 */
        if (!target__none(&opts->target))
		evlist__enable(top->evlist);

	ret = -1;
	if (pthread_create(&thread_process, NULL, process_thread, top)) {
		ui__error("Could not create process thread.\n");
		return ret;
	}

	if (pthread_create(&thread, NULL, (use_browser > 0 ? display_thread_tui :
							    display_thread), top)) {
		ui__error("Could not create display thread.\n");
		goto out_join_thread;
	}

	if (top->realtime_prio) {
		struct sched_param param;

		param.sched_priority = top->realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			ui__error("Could not set realtime priority.\n");
			goto out_join;
		}
	}

	/* Wait for a minimal set of events before starting the snapshot */
	evlist__poll(top->evlist, 100);

	perf_top__mmap_read(top);

	while (!done) {
		u64 hits = top->samples;

		perf_top__mmap_read(top);

		if (opts->overwrite || (hits == top->samples))
			ret = evlist__poll(top->evlist, 100);

		if (resize) {
			perf_top__resize(top);
			resize = 0;
		}
	}

	ret = 0;
out_join:
	pthread_join(thread, NULL);
out_join_thread:
	pthread_cond_signal(&top->qe.cond);
	pthread_join(thread_process, NULL);
	return ret;
}

static int
callchain_opt(const struct option *opt, const char *arg, int unset)
{
	symbol_conf.use_callchain = true;
	return record_callchain_opt(opt, arg, unset);
}

static int
parse_callchain_opt(const struct option *opt, const char *arg, int unset)
{
	struct callchain_param *callchain = opt->value;

	callchain->enabled = !unset;
	callchain->record_mode = CALLCHAIN_FP;

	/*
	 * --no-call-graph
	 */
	if (unset) {
		symbol_conf.use_callchain = false;
		callchain->record_mode = CALLCHAIN_NONE;
		return 0;
	}

	return parse_callchain_top_opt(arg);
}

static int perf_top_config(const char *var, const char *value, void *cb __maybe_unused)
{
	if (!strcmp(var, "top.call-graph")) {
		var = "call-graph.record-mode";
		return perf_default_config(var, value, cb);
	}
	if (!strcmp(var, "top.children")) {
		symbol_conf.cumulate_callchain = perf_config_bool(var, value);
		return 0;
	}

	return 0;
}

static int
parse_percent_limit(const struct option *opt, const char *arg,
		    int unset __maybe_unused)
{
	struct perf_top *top = opt->value;

	top->min_percent = strtof(arg, NULL);
	return 0;
}

const char top_callchain_help[] = CALLCHAIN_RECORD_HELP CALLCHAIN_REPORT_HELP
	"\n\t\t\t\tDefault: fp,graph,0.5,caller,function";

int cmd_top(int argc, const char **argv)
{
	char errbuf[BUFSIZ];
	struct perf_top top = {
		.count_filter	     = 5,
		.delay_secs	     = 2,
		.record_opts = {
			.mmap_pages	= UINT_MAX,
			.user_freq	= UINT_MAX,
			.user_interval	= ULLONG_MAX,
			.freq		= 4000, /* 4 KHz */
			.target		= {
				.uses_mmap   = true,
			},
			/*
			 * FIXME: This will lose PERF_RECORD_MMAP and other metadata
			 * when we pause, fix that and reenable. Probably using a
			 * separate evlist with a dummy event, i.e. a non-overwrite
			 * ring buffer just for metadata events, while PERF_RECORD_SAMPLE
			 * stays in overwrite mode. -acme
			 * */
			.overwrite	= 0,
			.sample_time	= true,
			.sample_time_set = true,
		},
		.max_stack	     = sysctl__max_stack(),
		.annotation_opts     = annotation__default_options,
		.nr_threads_synthesize = UINT_MAX,
	};
	struct record_opts *opts = &top.record_opts;
	struct target *target = &opts->target;
	const struct option options[] = {
	OPT_CALLBACK('e', "event", &top.evlist, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_U64('c', "count", &opts->user_interval, "event period to sample"),
	OPT_STRING('p', "pid", &target->pid, "pid",
		    "profile events on existing process id"),
	OPT_STRING('t', "tid", &target->tid, "tid",
		    "profile events on existing thread id"),
	OPT_BOOLEAN('a', "all-cpus", &target->system_wide,
			    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &target->cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_BOOLEAN(0, "ignore-vmlinux", &symbol_conf.ignore_vmlinux,
		    "don't load vmlinux even if found"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_BOOLEAN('K', "hide_kernel_symbols", &top.hide_kernel_symbols,
		    "hide kernel symbols"),
	OPT_CALLBACK('m', "mmap-pages", &opts->mmap_pages, "pages",
		     "number of mmap data pages", evlist__parse_mmap_pages),
	OPT_INTEGER('r', "realtime", &top.realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_INTEGER('d', "delay", &top.delay_secs,
		    "number of seconds to delay between refreshes"),
	OPT_BOOLEAN('D', "dump-symtab", &top.dump_symtab,
			    "dump the symbol table used for profiling"),
	OPT_INTEGER('f', "count-filter", &top.count_filter,
		    "only display functions with more events than this"),
	OPT_BOOLEAN(0, "group", &opts->group,
			    "put the counters into a counter group"),
	OPT_BOOLEAN('i', "no-inherit", &opts->no_inherit,
		    "child tasks do not inherit counters"),
	OPT_STRING(0, "sym-annotate", &top.sym_filter, "symbol name",
		    "symbol to annotate"),
	OPT_BOOLEAN('z', "zero", &top.zero, "zero history across updates"),
	OPT_CALLBACK('F', "freq", &top.record_opts, "freq or 'max'",
		     "profile at this frequency",
		      record__parse_freq),
	OPT_INTEGER('E', "entries", &top.print_entries,
		    "display this many functions"),
	OPT_BOOLEAN('U', "hide_user_symbols", &top.hide_user_symbols,
		    "hide user symbols"),
	OPT_BOOLEAN(0, "tui", &top.use_tui, "Use the TUI interface"),
	OPT_BOOLEAN(0, "stdio", &top.use_stdio, "Use the stdio interface"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent, cpu, srcline, ..."
		   " Please refer the man page for the complete list."),
	OPT_STRING(0, "fields", &field_order, "key[,keys...]",
		   "output field(s): overhead, period, sample plus all of sort keys"),
	OPT_BOOLEAN('n', "show-nr-samples", &symbol_conf.show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_CALLBACK_NOOPT('g', NULL, &callchain_param,
			   NULL, "enables call-graph recording and display",
			   &callchain_opt),
	OPT_CALLBACK(0, "call-graph", &callchain_param,
		     "record_mode[,record_size],print_type,threshold[,print_limit],order,sort_key[,branch]",
		     top_callchain_help, &parse_callchain_opt),
	OPT_BOOLEAN(0, "children", &symbol_conf.cumulate_callchain,
		    "Accumulate callchains of children and show total overhead as well"),
	OPT_INTEGER(0, "max-stack", &top.max_stack,
		    "Set the maximum stack depth when parsing the callchain. "
		    "Default: kernel.perf_event_max_stack or " __stringify(PERF_MAX_STACK_DEPTH)),
	OPT_CALLBACK(0, "ignore-callees", NULL, "regex",
		   "ignore callees of these functions in call graphs",
		   report_parse_ignore_callees_opt),
	OPT_BOOLEAN(0, "show-total-period", &symbol_conf.show_total_period,
		    "Show a column with the sum of periods"),
	OPT_STRING(0, "dsos", &symbol_conf.dso_list_str, "dso[,dso...]",
		   "only consider symbols in these dsos"),
	OPT_STRING(0, "comms", &symbol_conf.comm_list_str, "comm[,comm...]",
		   "only consider symbols in these comms"),
	OPT_STRING(0, "symbols", &symbol_conf.sym_list_str, "symbol[,symbol...]",
		   "only consider these symbols"),
	OPT_BOOLEAN(0, "source", &top.annotation_opts.annotate_src,
		    "Interleave source code with assembly code (default)"),
	OPT_BOOLEAN(0, "asm-raw", &top.annotation_opts.show_asm_raw,
		    "Display raw encoding of assembly instructions (default)"),
	OPT_BOOLEAN(0, "demangle-kernel", &symbol_conf.demangle_kernel,
		    "Enable kernel symbol demangling"),
	OPT_BOOLEAN(0, "no-bpf-event", &top.record_opts.no_bpf_event, "do not record bpf events"),
	OPT_STRING(0, "objdump", &top.annotation_opts.objdump_path, "path",
		    "objdump binary to use for disassembly and annotations"),
	OPT_STRING('M', "disassembler-style", &top.annotation_opts.disassembler_style, "disassembler style",
		   "Specify disassembler style (e.g. -M intel for intel syntax)"),
	OPT_STRING(0, "prefix", &top.annotation_opts.prefix, "prefix",
		    "Add prefix to source file path names in programs (with --prefix-strip)"),
	OPT_STRING(0, "prefix-strip", &top.annotation_opts.prefix_strip, "N",
		    "Strip first N entries of source file path name in programs (with --prefix)"),
	OPT_STRING('u', "uid", &target->uid_str, "user", "user to profile"),
	OPT_CALLBACK(0, "percent-limit", &top, "percent",
		     "Don't show entries under that percent", parse_percent_limit),
	OPT_CALLBACK(0, "percentage", NULL, "relative|absolute",
		     "How to display percentage of filtered entries", parse_filter_percentage),
	OPT_STRING('w', "column-widths", &symbol_conf.col_width_list_str,
		   "width[,width...]",
		   "don't try to adjust column width, use these fixed values"),
	OPT_UINTEGER(0, "proc-map-timeout", &proc_map_timeout,
			"per thread proc mmap processing timeout in ms"),
	OPT_CALLBACK_NOOPT('b', "branch-any", &opts->branch_stack,
		     "branch any", "sample any taken branches",
		     parse_branch_stack),
	OPT_CALLBACK('j', "branch-filter", &opts->branch_stack,
		     "branch filter mask", "branch stack filter modes",
		     parse_branch_stack),
	OPT_BOOLEAN(0, "raw-trace", &symbol_conf.raw_trace,
		    "Show raw trace event output (do not use print fmt or plugins)"),
	OPT_BOOLEAN(0, "hierarchy", &symbol_conf.report_hierarchy,
		    "Show entries in a hierarchy"),
	OPT_BOOLEAN(0, "overwrite", &top.record_opts.overwrite,
		    "Use a backward ring buffer, default: no"),
	OPT_BOOLEAN(0, "force", &symbol_conf.force, "don't complain, do it"),
	OPT_UINTEGER(0, "num-thread-synthesize", &top.nr_threads_synthesize,
			"number of thread to run event synthesize"),
	OPT_CALLBACK('G', "cgroup", &top.evlist, "name",
		     "monitor event in cgroup name only", parse_cgroups),
	OPT_BOOLEAN(0, "namespaces", &opts->record_namespaces,
		    "Record namespaces events"),
	OPT_BOOLEAN(0, "all-cgroups", &opts->record_cgroup,
		    "Record cgroup events"),
	OPT_INTEGER(0, "group-sort-idx", &symbol_conf.group_sort_idx,
		    "Sort the output by the event at the index n in group. "
		    "If n is invalid, sort by the first event. "
		    "WARNING: should be used on grouped events."),
	OPT_BOOLEAN(0, "stitch-lbr", &top.stitch_lbr,
		    "Enable LBR callgraph stitching approach"),
#ifdef HAVE_LIBPFM
	OPT_CALLBACK(0, "pfm-events", &top.evlist, "event",
		"libpfm4 event selector. use 'perf list' to list available events",
		parse_libpfm_events_option),
#endif
	OPTS_EVSWITCH(&top.evswitch),
	OPT_END()
	};
	const char * const top_usage[] = {
		"perf top [<options>]",
		NULL
	};
	int status = hists__init();

	if (status < 0)
		return status;

	top.annotation_opts.min_pcnt = 5;
	top.annotation_opts.context  = 4;

	top.evlist = evlist__new();
	if (top.evlist == NULL)
		return -ENOMEM;

	status = perf_config(perf_top_config, &top);
	if (status)
		return status;
	/*
	 * Since the per arch annotation init routine may need the cpuid, read
	 * it here, since we are not getting this from the perf.data header.
	 */
	status = perf_env__read_cpuid(&perf_env);
	if (status) {
		/*
		 * Some arches do not provide a get_cpuid(), so just use pr_debug, otherwise
		 * warn the user explicitly.
		 */
		eprintf(status == ENOSYS ? 1 : 0, verbose,
			"Couldn't read the cpuid for this machine: %s\n",
			str_error_r(errno, errbuf, sizeof(errbuf)));
	}
	top.evlist->env = &perf_env;

	argc = parse_options(argc, argv, options, top_usage, 0);
	if (argc)
		usage_with_options(top_usage, options);

	if (annotate_check_args(&top.annotation_opts) < 0)
		goto out_delete_evlist;

	if (!top.evlist->core.nr_entries &&
	    evlist__add_default(top.evlist) < 0) {
		pr_err("Not enough memory for event selector list\n");
		goto out_delete_evlist;
	}

	status = evswitch__init(&top.evswitch, top.evlist, stderr);
	if (status)
		goto out_delete_evlist;

	if (symbol_conf.report_hierarchy) {
		/* disable incompatible options */
		symbol_conf.event_group = false;
		symbol_conf.cumulate_callchain = false;

		if (field_order) {
			pr_err("Error: --hierarchy and --fields options cannot be used together\n");
			parse_options_usage(top_usage, options, "fields", 0);
			parse_options_usage(NULL, options, "hierarchy", 0);
			goto out_delete_evlist;
		}
	}

	if (top.stitch_lbr && !(callchain_param.record_mode == CALLCHAIN_LBR)) {
		pr_err("Error: --stitch-lbr must be used with --call-graph lbr\n");
		goto out_delete_evlist;
	}

	if (nr_cgroups > 0 && opts->record_cgroup) {
		pr_err("--cgroup and --all-cgroups cannot be used together\n");
		goto out_delete_evlist;
	}

	if (opts->branch_stack && callchain_param.enabled)
		symbol_conf.show_branchflag_count = true;

	sort__mode = SORT_MODE__TOP;
	/* display thread wants entries to be collapsed in a different tree */
	perf_hpp_list.need_collapse = 1;

	if (top.use_stdio)
		use_browser = 0;
	else if (top.use_tui)
		use_browser = 1;

	setup_browser(false);

	if (setup_sorting(top.evlist) < 0) {
		if (sort_order)
			parse_options_usage(top_usage, options, "s", 1);
		if (field_order)
			parse_options_usage(sort_order ? NULL : top_usage,
					    options, "fields", 0);
		goto out_delete_evlist;
	}

	status = target__validate(target);
	if (status) {
		target__strerror(target, status, errbuf, BUFSIZ);
		ui__warning("%s\n", errbuf);
	}

	status = target__parse_uid(target);
	if (status) {
		int saved_errno = errno;

		target__strerror(target, status, errbuf, BUFSIZ);
		ui__error("%s\n", errbuf);

		status = -saved_errno;
		goto out_delete_evlist;
	}

	if (target__none(target))
		target->system_wide = true;

	if (evlist__create_maps(top.evlist, target) < 0) {
		ui__error("Couldn't create thread/CPU maps: %s\n",
			  errno == ENOENT ? "No such process" : str_error_r(errno, errbuf, sizeof(errbuf)));
		goto out_delete_evlist;
	}

	if (top.delay_secs < 1)
		top.delay_secs = 1;

	if (record_opts__config(opts)) {
		status = -EINVAL;
		goto out_delete_evlist;
	}

	top.sym_evsel = evlist__first(top.evlist);

	if (!callchain_param.enabled) {
		symbol_conf.cumulate_callchain = false;
		perf_hpp__cancel_cumulate();
	}

	if (symbol_conf.cumulate_callchain && !callchain_param.order_set)
		callchain_param.order = ORDER_CALLER;

	status = symbol__annotation_init();
	if (status < 0)
		goto out_delete_evlist;

	annotation_config__init(&top.annotation_opts);

	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);
	status = symbol__init(NULL);
	if (status < 0)
		goto out_delete_evlist;

	sort__setup_elide(stdout);

	get_term_dimensions(&top.winsize);
	if (top.print_entries == 0) {
		perf_top__update_print_entries(&top);
		signal(SIGWINCH, winch_sig);
	}

	top.session = perf_session__new(NULL, false, NULL);
	if (IS_ERR(top.session)) {
		status = PTR_ERR(top.session);
		goto out_delete_evlist;
	}

#ifdef HAVE_LIBBPF_SUPPORT
	if (!top.record_opts.no_bpf_event) {
		top.sb_evlist = evlist__new();

		if (top.sb_evlist == NULL) {
			pr_err("Couldn't create side band evlist.\n.");
			goto out_delete_evlist;
		}

		if (evlist__add_bpf_sb_event(top.sb_evlist, &perf_env)) {
			pr_err("Couldn't ask for PERF_RECORD_BPF_EVENT side band events.\n.");
			goto out_delete_evlist;
		}
	}
#endif

	if (evlist__start_sb_thread(top.sb_evlist, target)) {
		pr_debug("Couldn't start the BPF side band thread:\nBPF programs starting from now on won't be annotatable\n");
		opts->no_bpf_event = true;
	}

	status = __cmd_top(&top);

	if (!opts->no_bpf_event)
		evlist__stop_sb_thread(top.sb_evlist);

out_delete_evlist:
	evlist__delete(top.evlist);
	perf_session__delete(top.session);

	return status;
}
