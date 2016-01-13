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
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */
#include "builtin.h"

#include "perf.h"

#include "util/annotate.h"
#include "util/cache.h"
#include "util/color.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/machine.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/thread_map.h"
#include "util/top.h"
#include "util/util.h"
#include <linux/rbtree.h>
#include <subcmd/parse-options.h>
#include "util/parse-events.h"
#include "util/cpumap.h"
#include "util/xyarray.h"
#include "util/sort.h"
#include "util/intlist.h"
#include "util/parse-branch-options.h"
#include "arch/common.h"

#include "util/debug.h"

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

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/mman.h>

#include <linux/types.h>

static volatile int done;

#define HEADER_LINE_NR  5

static void perf_top__update_print_entries(struct perf_top *top)
{
	top->print_entries = top->winsize.ws_row - HEADER_LINE_NR;
}

static void perf_top__sig_winch(int sig __maybe_unused,
				siginfo_t *info __maybe_unused, void *arg)
{
	struct perf_top *top = arg;

	get_term_dimensions(&top->winsize);
	perf_top__update_print_entries(top);
}

static int perf_top__parse_source(struct perf_top *top, struct hist_entry *he)
{
	struct symbol *sym;
	struct annotation *notes;
	struct map *map;
	int err = -1;

	if (!he || !he->ms.sym)
		return -1;

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
	if (notes->src != NULL) {
		pthread_mutex_lock(&notes->lock);
		goto out_assign;
	}

	pthread_mutex_lock(&notes->lock);

	if (symbol__alloc_hist(sym) < 0) {
		pthread_mutex_unlock(&notes->lock);
		pr_err("Not enough memory for annotating '%s' symbol!\n",
		       sym->name);
		sleep(1);
		return err;
	}

	err = symbol__annotate(sym, map, 0);
	if (err == 0) {
out_assign:
		top->sym_filter_entry = he;
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
					int counter, u64 ip)
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

	err = hist_entry__inc_addr_samples(he, counter, ip);

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
	struct annotation *notes;
	struct symbol *symbol;
	int more;

	if (!he)
		return;

	symbol = he->ms.sym;
	notes = symbol__annotation(symbol);

	pthread_mutex_lock(&notes->lock);

	if (notes->src == NULL)
		goto out_unlock;

	printf("Showing %s for %s\n", perf_evsel__name(top->sym_evsel), symbol->name);
	printf("  Events  Pcnt (>=%d%%)\n", top->sym_pcnt_filter);

	more = symbol__annotate_printf(symbol, he->ms.map, top->sym_evsel,
				       0, top->sym_pcnt_filter, top->print_entries, 4);

	if (top->evlist->enabled) {
		if (top->zero)
			symbol__annotate_zero_histogram(symbol, top->sym_evsel->idx);
		else
			symbol__annotate_decay_histogram(symbol, top->sym_evsel->idx);
	}
	if (more != 0)
		printf("%d lines not displayed, maybe increase display entries [e]\n", more);
out_unlock:
	pthread_mutex_unlock(&notes->lock);
}

static void perf_top__print_sym_table(struct perf_top *top)
{
	char bf[160];
	int printed = 0;
	const int win_width = top->winsize.ws_col - 1;
	struct hists *hists = evsel__hists(top->sym_evsel);

	puts(CONSOLE_CLEAR);

	perf_top__header_snprintf(top, bf, sizeof(bf));
	printf("%s\n", bf);

	perf_top__reset_sample_counters(top);

	printf("%-*.*s\n", win_width, win_width, graph_dotted_line);

	if (hists->stats.nr_lost_warned !=
	    hists->stats.nr_events[PERF_RECORD_LOST]) {
		hists->stats.nr_lost_warned =
			      hists->stats.nr_events[PERF_RECORD_LOST];
		color_fprintf(stdout, PERF_COLOR_RED,
			      "WARNING: LOST %d chunks, Check IO/CPU overload",
			      hists->stats.nr_lost_warned);
		++printed;
	}

	if (top->sym_filter_entry) {
		perf_top__show_details(top);
		return;
	}

	if (top->evlist->enabled) {
		if (top->zero) {
			hists__delete_entries(hists);
		} else {
			hists__decay_entries(hists, top->hide_user_symbols,
					     top->hide_kernel_symbols);
		}
	}

	hists__collapse_resort(hists, NULL);
	hists__output_resort(hists, NULL);

	hists__output_recalc_col_len(hists, top->print_entries - printed);
	putchar('\n');
	hists__fprintf(hists, false, top->print_entries - printed, win_width,
		       top->min_percent, stdout);
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

	next = rb_first(&hists->entries);
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

	if (top->evlist->nr_entries > 1)
		fprintf(stdout, "\t[E]     active event counter.              \t(%s)\n", perf_evsel__name(top->sym_evsel));

	fprintf(stdout, "\t[f]     profile display filter (count).    \t(%d)\n", top->count_filter);

	fprintf(stdout, "\t[F]     annotate display filter (percent). \t(%d%%)\n", top->sym_pcnt_filter);
	fprintf(stdout, "\t[s]     annotate symbol.                   \t(%s)\n", name?: "NULL");
	fprintf(stdout, "\t[S]     stop annotation.\n");

	fprintf(stdout,
		"\t[K]     hide kernel_symbols symbols.     \t(%s)\n",
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
			return top->evlist->nr_entries > 1 ? 1 : 0;
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
				struct sigaction act = {
					.sa_sigaction = perf_top__sig_winch,
					.sa_flags     = SA_SIGINFO,
				};
				perf_top__sig_winch(SIGWINCH, NULL, top);
				sigaction(SIGWINCH, &act, NULL);
			} else {
				signal(SIGWINCH, SIG_DFL);
			}
			break;
		case 'E':
			if (top->evlist->nr_entries > 1) {
				/* Select 0 as the default event: */
				int counter = 0;

				fprintf(stderr, "\nAvailable events:");

				evlist__for_each(top->evlist, top->sym_evsel)
					fprintf(stderr, "\n\t%d %s", top->sym_evsel->idx, perf_evsel__name(top->sym_evsel));

				prompt_integer(&counter, "Enter details event counter");

				if (counter >= top->evlist->nr_entries) {
					top->sym_evsel = perf_evlist__first(top->evlist);
					fprintf(stderr, "Sorry, no such event, using %s.\n", perf_evsel__name(top->sym_evsel));
					sleep(1);
					break;
				}
				evlist__for_each(top->evlist, top->sym_evsel)
					if (top->sym_evsel->idx == counter)
						break;
			} else
				top->sym_evsel = perf_evlist__first(top->evlist);
			break;
		case 'f':
			prompt_integer(&top->count_filter, "Enter display event count filter");
			break;
		case 'F':
			prompt_percent(&top->sym_pcnt_filter,
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
	struct hists *hists;

	perf_top__reset_sample_counters(t);

	if (t->evlist->selected != NULL)
		t->sym_evsel = t->evlist->selected;

	hists = evsel__hists(t->sym_evsel);

	if (t->evlist->enabled) {
		if (t->zero) {
			hists__delete_entries(hists);
		} else {
			hists__decay_entries(hists, t->hide_user_symbols,
					     t->hide_kernel_symbols);
		}
	}

	hists__collapse_resort(hists, NULL);
	hists__output_resort(hists, NULL);
}

static void *display_thread_tui(void *arg)
{
	struct perf_evsel *pos;
	struct perf_top *top = arg;
	const char *help = "For a higher level overview, try: perf top --sort comm,dso";
	struct hist_browser_timer hbt = {
		.timer		= perf_top__sort_new_samples,
		.arg		= top,
		.refresh	= top->delay_secs,
	};

	perf_top__sort_new_samples(top);

	/*
	 * Initialize the uid_filter_str, in the future the TUI will allow
	 * Zooming in/out UIDs. For now juse use whatever the user passed
	 * via --uid.
	 */
	evlist__for_each(top->evlist, pos) {
		struct hists *hists = evsel__hists(pos);
		hists->uid_filter_str = top->record_opts.target.uid_str;
	}

	perf_evlist__tui_browse_hists(top->evlist, help, &hbt,
				      top->min_percent,
				      &top->session->header.env);

	done = 1;
	return NULL;
}

static void display_sig(int sig __maybe_unused)
{
	done = 1;
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

	display_setup_sig();
	pthread__unblock_sigwinch();
repeat:
	delay_msecs = top->delay_secs * 1000;
	set_term_quiet_input(&save);
	/* trash return*/
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
			/* Fall trhu */
		default:
			c = getc(stdin);
			tcsetattr(0, TCSAFLUSH, &save);

			if (perf_top__handle_keypress(top, c))
				goto repeat;
			done = 1;
		}
	}

	tcsetattr(0, TCSAFLUSH, &save);
	return NULL;
}

static int symbol_filter(struct map *map, struct symbol *sym)
{
	const char *name = sym->name;

	if (!__map__is_kernel(map))
		return 0;
	/*
	 * ppc64 uses function descriptors and appends a '.' to the
	 * start of every instruction address. Remove it.
	 */
	if (name[0] == '.')
		name++;

	if (!strcmp(name, "_text") ||
	    !strcmp(name, "_etext") ||
	    !strcmp(name, "_sinittext") ||
	    !strncmp("init_module", name, 11) ||
	    !strncmp("cleanup_module", name, 14) ||
	    strstr(name, "_text_start") ||
	    strstr(name, "_text_end"))
		return 1;

	if (symbol__is_idle(sym))
		sym->ignore = true;

	return 0;
}

static int hist_iter__top_callback(struct hist_entry_iter *iter,
				   struct addr_location *al, bool single,
				   void *arg)
{
	struct perf_top *top = arg;
	struct hist_entry *he = iter->he;
	struct perf_evsel *evsel = iter->evsel;

	if (sort__has_sym && single)
		perf_top__record_precise_ip(top, he, evsel->idx, al->addr);

	hist__account_cycles(iter->sample->branch_stack, al, iter->sample,
		     !(top->record_opts.branch_stack & PERF_SAMPLE_BRANCH_ANY));
	return 0;
}

static void perf_event__process_sample(struct perf_tool *tool,
				       const union perf_event *event,
				       struct perf_evsel *evsel,
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

	if (perf_event__preprocess_sample(event, machine, &al, sample) < 0)
		return;

	if (!top->kptr_restrict_warned &&
	    symbol_conf.kptr_restrict &&
	    al.cpumode == PERF_RECORD_MISC_KERNEL) {
		ui__warning(
"Kernel address maps (/proc/{kallsyms,modules}) are restricted.\n\n"
"Check /proc/sys/kernel/kptr_restrict.\n\n"
"Kernel%s samples will not be resolved.\n",
			  al.map && !RB_EMPTY_ROOT(&al.map->dso->symbols[MAP__FUNCTION]) ?
			  " modules" : "");
		if (use_browser <= 0)
			sleep(5);
		top->kptr_restrict_warned = true;
	}

	if (al.sym == NULL) {
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
		if (!top->kptr_restrict_warned && !top->vmlinux_warned &&
		    al.map == machine->vmlinux_maps[MAP__FUNCTION] &&
		    RB_EMPTY_ROOT(&al.map->dso->symbols[MAP__FUNCTION])) {
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

	if (al.sym == NULL || !al.sym->ignore) {
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

static void perf_top__mmap_read_idx(struct perf_top *top, int idx)
{
	struct perf_sample sample;
	struct perf_evsel *evsel;
	struct perf_session *session = top->session;
	union perf_event *event;
	struct machine *machine;
	u8 origin;
	int ret;

	while ((event = perf_evlist__mmap_read(top->evlist, idx)) != NULL) {
		ret = perf_evlist__parse_sample(top->evlist, event, &sample);
		if (ret) {
			pr_err("Can't parse sample, err = %d\n", ret);
			goto next_event;
		}

		evsel = perf_evlist__id2evsel(session->evlist, sample.id);
		assert(evsel != NULL);

		origin = event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK;

		if (event->header.type == PERF_RECORD_SAMPLE)
			++top->samples;

		switch (origin) {
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
		} else if (event->header.type < PERF_RECORD_MAX) {
			hists__inc_nr_events(evsel__hists(evsel), event->header.type);
			machine__process_event(machine, event, &sample);
		} else
			++session->evlist->stats.nr_unknown_events;
next_event:
		perf_evlist__mmap_consume(top->evlist, idx);
	}
}

static void perf_top__mmap_read(struct perf_top *top)
{
	int i;

	for (i = 0; i < top->evlist->nr_mmaps; i++)
		perf_top__mmap_read_idx(top, i);
}

static int perf_top__start_counters(struct perf_top *top)
{
	char msg[512];
	struct perf_evsel *counter;
	struct perf_evlist *evlist = top->evlist;
	struct record_opts *opts = &top->record_opts;

	perf_evlist__config(evlist, opts);

	evlist__for_each(evlist, counter) {
try_again:
		if (perf_evsel__open(counter, top->evlist->cpus,
				     top->evlist->threads) < 0) {
			if (perf_evsel__fallback(counter, errno, msg, sizeof(msg))) {
				if (verbose)
					ui__warning("%s\n", msg);
				goto try_again;
			}

			perf_evsel__open_strerror(counter, &opts->target,
						  errno, msg, sizeof(msg));
			ui__error("%s\n", msg);
			goto out_err;
		}
	}

	if (perf_evlist__mmap(evlist, opts->mmap_pages, false) < 0) {
		ui__error("Failed to mmap with %d (%s)\n",
			    errno, strerror_r(errno, msg, sizeof(msg)));
		goto out_err;
	}

	return 0;

out_err:
	return -1;
}

static int perf_top__setup_sample_type(struct perf_top *top __maybe_unused)
{
	if (!sort__has_sym) {
		if (symbol_conf.use_callchain) {
			ui__error("Selected -g but \"sym\" not present in --sort/-s.");
			return -EINVAL;
		}
	} else if (callchain_param.mode != CHAIN_NONE) {
		if (callchain_register_param(&callchain_param) < 0) {
			ui__error("Can't register callchain params.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int __cmd_top(struct perf_top *top)
{
	struct record_opts *opts = &top->record_opts;
	pthread_t thread;
	int ret;

	top->session = perf_session__new(NULL, false, NULL);
	if (top->session == NULL)
		return -1;

	machines__set_symbol_filter(&top->session->machines, symbol_filter);

	if (!objdump_path) {
		ret = perf_env__lookup_objdump(&top->session->header.env);
		if (ret)
			goto out_delete;
	}

	ret = perf_top__setup_sample_type(top);
	if (ret)
		goto out_delete;

	if (perf_session__register_idle_thread(top->session) < 0)
		goto out_delete;

	machine__synthesize_threads(&top->session->machines.host, &opts->target,
				    top->evlist->threads, false, opts->proc_map_timeout);

	if (sort__has_socket) {
		ret = perf_env__read_cpu_topology_map(&perf_env);
		if (ret < 0)
			goto out_err_cpu_topo;
	}

	ret = perf_top__start_counters(top);
	if (ret)
		goto out_delete;

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
                perf_evlist__enable(top->evlist);

	/* Wait for a minimal set of events before starting the snapshot */
	perf_evlist__poll(top->evlist, 100);

	perf_top__mmap_read(top);

	ret = -1;
	if (pthread_create(&thread, NULL, (use_browser > 0 ? display_thread_tui :
							    display_thread), top)) {
		ui__error("Could not create display thread.\n");
		goto out_delete;
	}

	if (top->realtime_prio) {
		struct sched_param param;

		param.sched_priority = top->realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			ui__error("Could not set realtime priority.\n");
			goto out_join;
		}
	}

	while (!done) {
		u64 hits = top->samples;

		perf_top__mmap_read(top);

		if (hits == top->samples)
			ret = perf_evlist__poll(top->evlist, 100);
	}

	ret = 0;
out_join:
	pthread_join(thread, NULL);
out_delete:
	perf_session__delete(top->session);
	top->session = NULL;

	return ret;

out_err_cpu_topo: {
	char errbuf[BUFSIZ];
	const char *err = strerror_r(-ret, errbuf, sizeof(errbuf));

	ui__error("Could not read the CPU topology map: %s\n", err);
	goto out_delete;
}
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
	struct record_opts *record = (struct record_opts *)opt->value;

	record->callgraph_set = true;
	callchain_param.enabled = !unset;
	callchain_param.record_mode = CALLCHAIN_FP;

	/*
	 * --no-call-graph
	 */
	if (unset) {
		symbol_conf.use_callchain = false;
		callchain_param.record_mode = CALLCHAIN_NONE;
		return 0;
	}

	return parse_callchain_top_opt(arg);
}

static int perf_top_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "top.call-graph"))
		var = "call-graph.record-mode"; /* fall-through */
	if (!strcmp(var, "top.children")) {
		symbol_conf.cumulate_callchain = perf_config_bool(var, value);
		return 0;
	}

	return perf_default_config(var, value, cb);
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

int cmd_top(int argc, const char **argv, const char *prefix __maybe_unused)
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
			.proc_map_timeout    = 500,
		},
		.max_stack	     = PERF_MAX_STACK_DEPTH,
		.sym_pcnt_filter     = 5,
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
	OPT_BOOLEAN('K', "hide_kernel_symbols", &top.hide_kernel_symbols,
		    "hide kernel symbols"),
	OPT_CALLBACK('m', "mmap-pages", &opts->mmap_pages, "pages",
		     "number of mmap data pages",
		     perf_evlist__parse_mmap_pages),
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
	OPT_UINTEGER('F', "freq", &opts->user_freq, "profile at this frequency"),
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
	OPT_CALLBACK_NOOPT('g', NULL, &top.record_opts,
			   NULL, "enables call-graph recording and display",
			   &callchain_opt),
	OPT_CALLBACK(0, "call-graph", &top.record_opts,
		     "record_mode[,record_size],print_type,threshold[,print_limit],order,sort_key[,branch]",
		     top_callchain_help, &parse_callchain_opt),
	OPT_BOOLEAN(0, "children", &symbol_conf.cumulate_callchain,
		    "Accumulate callchains of children and show total overhead as well"),
	OPT_INTEGER(0, "max-stack", &top.max_stack,
		    "Set the maximum stack depth when parsing the callchain. "
		    "Default: " __stringify(PERF_MAX_STACK_DEPTH)),
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
	OPT_BOOLEAN(0, "source", &symbol_conf.annotate_src,
		    "Interleave source code with assembly code (default)"),
	OPT_BOOLEAN(0, "asm-raw", &symbol_conf.annotate_asm_raw,
		    "Display raw encoding of assembly instructions (default)"),
	OPT_BOOLEAN(0, "demangle-kernel", &symbol_conf.demangle_kernel,
		    "Enable kernel symbol demangling"),
	OPT_STRING(0, "objdump", &objdump_path, "path",
		    "objdump binary to use for disassembly and annotations"),
	OPT_STRING('M', "disassembler-style", &disassembler_style, "disassembler style",
		   "Specify disassembler style (e.g. -M intel for intel syntax)"),
	OPT_STRING('u', "uid", &target->uid_str, "user", "user to profile"),
	OPT_CALLBACK(0, "percent-limit", &top, "percent",
		     "Don't show entries under that percent", parse_percent_limit),
	OPT_CALLBACK(0, "percentage", NULL, "relative|absolute",
		     "How to display percentage of filtered entries", parse_filter_percentage),
	OPT_STRING('w', "column-widths", &symbol_conf.col_width_list_str,
		   "width[,width...]",
		   "don't try to adjust column width, use these fixed values"),
	OPT_UINTEGER(0, "proc-map-timeout", &opts->proc_map_timeout,
			"per thread proc mmap processing timeout in ms"),
	OPT_CALLBACK_NOOPT('b', "branch-any", &opts->branch_stack,
		     "branch any", "sample any taken branches",
		     parse_branch_stack),
	OPT_CALLBACK('j', "branch-filter", &opts->branch_stack,
		     "branch filter mask", "branch stack filter modes",
		     parse_branch_stack),
	OPT_BOOLEAN(0, "raw-trace", &symbol_conf.raw_trace,
		    "Show raw trace event output (do not use print fmt or plugins)"),
	OPT_END()
	};
	const char * const top_usage[] = {
		"perf top [<options>]",
		NULL
	};
	int status = hists__init();

	if (status < 0)
		return status;

	top.evlist = perf_evlist__new();
	if (top.evlist == NULL)
		return -ENOMEM;

	perf_config(perf_top_config, &top);

	argc = parse_options(argc, argv, options, top_usage, 0);
	if (argc)
		usage_with_options(top_usage, options);

	if (!top.evlist->nr_entries &&
	    perf_evlist__add_default(top.evlist) < 0) {
		pr_err("Not enough memory for event selector list\n");
		goto out_delete_evlist;
	}

	sort__mode = SORT_MODE__TOP;
	/* display thread wants entries to be collapsed in a different tree */
	sort__need_collapse = 1;

	if (setup_sorting(top.evlist) < 0) {
		if (sort_order)
			parse_options_usage(top_usage, options, "s", 1);
		if (field_order)
			parse_options_usage(sort_order ? NULL : top_usage,
					    options, "fields", 0);
		goto out_delete_evlist;
	}

	if (top.use_stdio)
		use_browser = 0;
	else if (top.use_tui)
		use_browser = 1;

	setup_browser(false);

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

	if (perf_evlist__create_maps(top.evlist, target) < 0) {
		ui__error("Couldn't create thread/CPU maps: %s\n",
			  errno == ENOENT ? "No such process" : strerror_r(errno, errbuf, sizeof(errbuf)));
		goto out_delete_evlist;
	}

	symbol_conf.nr_events = top.evlist->nr_entries;

	if (top.delay_secs < 1)
		top.delay_secs = 1;

	if (record_opts__config(opts)) {
		status = -EINVAL;
		goto out_delete_evlist;
	}

	top.sym_evsel = perf_evlist__first(top.evlist);

	if (!symbol_conf.use_callchain) {
		symbol_conf.cumulate_callchain = false;
		perf_hpp__cancel_cumulate();
	}

	if (symbol_conf.cumulate_callchain && !callchain_param.order_set)
		callchain_param.order = ORDER_CALLER;

	symbol_conf.priv_size = sizeof(struct annotation);

	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);
	if (symbol__init(NULL) < 0)
		return -1;

	sort__setup_elide(stdout);

	get_term_dimensions(&top.winsize);
	if (top.print_entries == 0) {
		struct sigaction act = {
			.sa_sigaction = perf_top__sig_winch,
			.sa_flags     = SA_SIGINFO,
		};
		perf_top__update_print_entries(&top);
		sigaction(SIGWINCH, &act, NULL);
	}

	status = __cmd_top(&top);

out_delete_evlist:
	perf_evlist__delete(top.evlist);

	return status;
}
