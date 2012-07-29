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
#include "util/session.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/thread_map.h"
#include "util/top.h"
#include "util/util.h"
#include <linux/rbtree.h>
#include "util/parse-options.h"
#include "util/parse-events.h"
#include "util/cpumap.h"
#include "util/xyarray.h"
#include "util/sort.h"

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
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <sys/mman.h>

#include <linux/unistd.h>
#include <linux/types.h>

void get_term_dimensions(struct winsize *ws)
{
	char *s = getenv("LINES");

	if (s != NULL) {
		ws->ws_row = atoi(s);
		s = getenv("COLUMNS");
		if (s != NULL) {
			ws->ws_col = atoi(s);
			if (ws->ws_row && ws->ws_col)
				return;
		}
	}
#ifdef TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, ws) == 0 &&
	    ws->ws_row && ws->ws_col)
		return;
#endif
	ws->ws_row = 25;
	ws->ws_col = 80;
}

static void perf_top__update_print_entries(struct perf_top *top)
{
	if (top->print_entries > 9)
		top->print_entries -= 9;
}

static void perf_top__sig_winch(int sig __used, siginfo_t *info __used, void *arg)
{
	struct perf_top *top = arg;

	get_term_dimensions(&top->winsize);
	if (!top->print_entries
	    || (top->print_entries+4) > top->winsize.ws_row) {
		top->print_entries = top->winsize.ws_row;
	} else {
		top->print_entries += 4;
		top->winsize.ws_row = top->print_entries;
	}
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
	if (map->dso->symtab_type == SYMTAB__KALLSYMS) {
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
	struct symbol *sym;
	int err;

	if (he == NULL || he->ms.sym == NULL ||
	    ((top->sym_filter_entry == NULL ||
	      top->sym_filter_entry->ms.sym != he->ms.sym) && use_browser != 1))
		return;

	sym = he->ms.sym;
	notes = symbol__annotation(sym);

	if (pthread_mutex_trylock(&notes->lock))
		return;

	if (notes->src == NULL && symbol__alloc_hist(sym) < 0) {
		pthread_mutex_unlock(&notes->lock);
		pr_err("Not enough memory for annotating '%s' symbol!\n",
		       sym->name);
		sleep(1);
		return;
	}

	ip = he->ms.map->map_ip(he->ms.map, ip);
	err = symbol__inc_addr_samples(sym, he->ms.map, counter, ip);

	pthread_mutex_unlock(&notes->lock);

	if (err == -ERANGE && !he->ms.map->erange_warned)
		ui__warn_map_erange(he->ms.map, sym, ip);
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

	more = symbol__annotate_printf(symbol, he->ms.map, top->sym_evsel->idx,
				       0, top->sym_pcnt_filter, top->print_entries, 4);
	if (top->zero)
		symbol__annotate_zero_histogram(symbol, top->sym_evsel->idx);
	else
		symbol__annotate_decay_histogram(symbol, top->sym_evsel->idx);
	if (more != 0)
		printf("%d lines not displayed, maybe increase display entries [e]\n", more);
out_unlock:
	pthread_mutex_unlock(&notes->lock);
}

static const char		CONSOLE_CLEAR[] = "[H[2J";

static struct hist_entry *perf_evsel__add_hist_entry(struct perf_evsel *evsel,
						     struct addr_location *al,
						     struct perf_sample *sample)
{
	struct hist_entry *he;

	he = __hists__add_entry(&evsel->hists, al, NULL, sample->period);
	if (he == NULL)
		return NULL;

	hists__inc_nr_events(&evsel->hists, PERF_RECORD_SAMPLE);
	return he;
}

static void perf_top__print_sym_table(struct perf_top *top)
{
	char bf[160];
	int printed = 0;
	const int win_width = top->winsize.ws_col - 1;

	puts(CONSOLE_CLEAR);

	perf_top__header_snprintf(top, bf, sizeof(bf));
	printf("%s\n", bf);

	perf_top__reset_sample_counters(top);

	printf("%-*.*s\n", win_width, win_width, graph_dotted_line);

	if (top->sym_evsel->hists.stats.nr_lost_warned !=
	    top->sym_evsel->hists.stats.nr_events[PERF_RECORD_LOST]) {
		top->sym_evsel->hists.stats.nr_lost_warned =
			top->sym_evsel->hists.stats.nr_events[PERF_RECORD_LOST];
		color_fprintf(stdout, PERF_COLOR_RED,
			      "WARNING: LOST %d chunks, Check IO/CPU overload",
			      top->sym_evsel->hists.stats.nr_lost_warned);
		++printed;
	}

	if (top->sym_filter_entry) {
		perf_top__show_details(top);
		return;
	}

	hists__collapse_resort_threaded(&top->sym_evsel->hists);
	hists__output_resort_threaded(&top->sym_evsel->hists);
	hists__decay_entries_threaded(&top->sym_evsel->hists,
				      top->hide_user_symbols,
				      top->hide_kernel_symbols);
	hists__output_recalc_col_len(&top->sym_evsel->hists,
				     top->winsize.ws_row - 3);
	putchar('\n');
	hists__fprintf(&top->sym_evsel->hists, NULL, false, false,
		       top->winsize.ws_row - 4 - printed, win_width, stdout);
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

	next = rb_first(&top->sym_evsel->hists.entries);
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

static void perf_top__handle_keypress(struct perf_top *top, int c)
{
	if (!perf_top__key_mapped(top, c)) {
		struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };
		struct termios tc, save;

		perf_top__print_mapped_keys(top);
		fprintf(stdout, "\nEnter selection, or unmapped key to continue: ");
		fflush(stdout);

		tcgetattr(0, &save);
		tc = save;
		tc.c_lflag &= ~(ICANON | ECHO);
		tc.c_cc[VMIN] = 0;
		tc.c_cc[VTIME] = 0;
		tcsetattr(0, TCSANOW, &tc);

		poll(&stdin_poll, 1, -1);
		c = getc(stdin);

		tcsetattr(0, TCSAFLUSH, &save);
		if (!perf_top__key_mapped(top, c))
			return;
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
				perf_top__sig_winch(SIGWINCH, NULL, top);
				signal(SIGWINCH, SIG_DFL);
			}
			break;
		case 'E':
			if (top->evlist->nr_entries > 1) {
				/* Select 0 as the default event: */
				int counter = 0;

				fprintf(stderr, "\nAvailable events:");

				list_for_each_entry(top->sym_evsel, &top->evlist->entries, node)
					fprintf(stderr, "\n\t%d %s", top->sym_evsel->idx, perf_evsel__name(top->sym_evsel));

				prompt_integer(&counter, "Enter details event counter");

				if (counter >= top->evlist->nr_entries) {
					top->sym_evsel = list_entry(top->evlist->entries.next, struct perf_evsel, node);
					fprintf(stderr, "Sorry, no such event, using %s.\n", perf_evsel__name(top->sym_evsel));
					sleep(1);
					break;
				}
				list_for_each_entry(top->sym_evsel, &top->evlist->entries, node)
					if (top->sym_evsel->idx == counter)
						break;
			} else
				top->sym_evsel = list_entry(top->evlist->entries.next, struct perf_evsel, node);
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
			exit(0);
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
}

static void perf_top__sort_new_samples(void *arg)
{
	struct perf_top *t = arg;
	perf_top__reset_sample_counters(t);

	if (t->evlist->selected != NULL)
		t->sym_evsel = t->evlist->selected;

	hists__collapse_resort_threaded(&t->sym_evsel->hists);
	hists__output_resort_threaded(&t->sym_evsel->hists);
	hists__decay_entries_threaded(&t->sym_evsel->hists,
				      t->hide_user_symbols,
				      t->hide_kernel_symbols);
}

static void *display_thread_tui(void *arg)
{
	struct perf_evsel *pos;
	struct perf_top *top = arg;
	const char *help = "For a higher level overview, try: perf top --sort comm,dso";

	perf_top__sort_new_samples(top);

	/*
	 * Initialize the uid_filter_str, in the future the TUI will allow
	 * Zooming in/out UIDs. For now juse use whatever the user passed
	 * via --uid.
	 */
	list_for_each_entry(pos, &top->evlist->entries, node)
		pos->hists.uid_filter_str = top->target.uid_str;

	perf_evlist__tui_browse_hists(top->evlist, help,
				      perf_top__sort_new_samples,
				      top, top->delay_secs);

	exit_browser(0);
	exit(0);
	return NULL;
}

static void *display_thread(void *arg)
{
	struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };
	struct termios tc, save;
	struct perf_top *top = arg;
	int delay_msecs, c;

	tcgetattr(0, &save);
	tc = save;
	tc.c_lflag &= ~(ICANON | ECHO);
	tc.c_cc[VMIN] = 0;
	tc.c_cc[VTIME] = 0;

	pthread__unblock_sigwinch();
repeat:
	delay_msecs = top->delay_secs * 1000;
	tcsetattr(0, TCSANOW, &tc);
	/* trash return*/
	getc(stdin);

	while (1) {
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
			goto process_hotkey;
		}
	}
process_hotkey:
	c = getc(stdin);
	tcsetattr(0, TCSAFLUSH, &save);

	perf_top__handle_keypress(top, c);
	goto repeat;

	return NULL;
}

/* Tag samples to be skipped. */
static const char *skip_symbols[] = {
	"intel_idle",
	"default_idle",
	"native_safe_halt",
	"cpu_idle",
	"enter_idle",
	"exit_idle",
	"mwait_idle",
	"mwait_idle_with_hints",
	"poll_idle",
	"ppc64_runlatch_off",
	"pseries_dedicated_idle_sleep",
	NULL
};

static int symbol_filter(struct map *map __used, struct symbol *sym)
{
	const char *name = sym->name;
	int i;

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

	for (i = 0; skip_symbols[i]; i++) {
		if (!strcmp(skip_symbols[i], name)) {
			sym->ignore = true;
			break;
		}
	}

	return 0;
}

static void perf_event__process_sample(struct perf_tool *tool,
				       const union perf_event *event,
				       struct perf_evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_top *top = container_of(tool, struct perf_top, tool);
	struct symbol *parent = NULL;
	u64 ip = event->ip.ip;
	struct addr_location al;
	int err;

	if (!machine && perf_guest) {
		pr_err("Can't find guest [%d]'s kernel information\n",
			event->ip.pid);
		return;
	}

	if (!machine) {
		pr_err("%u unprocessable samples recorded.",
		       top->session->hists.stats.nr_unprocessable_samples++);
		return;
	}

	if (event->header.misc & PERF_RECORD_MISC_EXACT_IP)
		top->exact_samples++;

	if (perf_event__preprocess_sample(event, machine, &al, sample,
					  symbol_filter) < 0 ||
	    al.filtered)
		return;

	if (!top->kptr_restrict_warned &&
	    symbol_conf.kptr_restrict &&
	    al.cpumode == PERF_RECORD_MISC_KERNEL) {
		ui__warning(
"Kernel address maps (/proc/{kallsyms,modules}) are restricted.\n\n"
"Check /proc/sys/kernel/kptr_restrict.\n\n"
"Kernel%s samples will not be resolved.\n",
			  !RB_EMPTY_ROOT(&al.map->dso->symbols[MAP__FUNCTION]) ?
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
				ui__warning("The %s file can't be used.\n%s",
					    symbol_conf.vmlinux_name, msg);
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
		struct hist_entry *he;

		if ((sort__has_parent || symbol_conf.use_callchain) &&
		    sample->callchain) {
			err = machine__resolve_callchain(machine, al.thread,
							 sample->callchain, &parent);
			if (err)
				return;
		}

		he = perf_evsel__add_hist_entry(evsel, &al, sample);
		if (he == NULL) {
			pr_err("Problem incrementing symbol period, skipping event\n");
			return;
		}

		if (symbol_conf.use_callchain) {
			err = callchain_append(he->callchain, &callchain_cursor,
					       sample->period);
			if (err)
				return;
		}

		if (top->sort_has_symbols)
			perf_top__record_precise_ip(top, he, evsel->idx, ip);
	}

	return;
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
		ret = perf_session__parse_sample(session, event, &sample);
		if (ret) {
			pr_err("Can't parse sample, err = %d\n", ret);
			continue;
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
				continue;
			machine = perf_session__find_host_machine(session);
			break;
		case PERF_RECORD_MISC_KERNEL:
			++top->kernel_samples;
			if (top->hide_kernel_symbols)
				continue;
			machine = perf_session__find_host_machine(session);
			break;
		case PERF_RECORD_MISC_GUEST_KERNEL:
			++top->guest_kernel_samples;
			machine = perf_session__find_machine(session, event->ip.pid);
			break;
		case PERF_RECORD_MISC_GUEST_USER:
			++top->guest_us_samples;
			/*
			 * TODO: we don't process guest user from host side
			 * except simple counting.
			 */
			/* Fall thru */
		default:
			continue;
		}


		if (event->header.type == PERF_RECORD_SAMPLE) {
			perf_event__process_sample(&top->tool, event, evsel,
						   &sample, machine);
		} else if (event->header.type < PERF_RECORD_MAX) {
			hists__inc_nr_events(&evsel->hists, event->header.type);
			perf_event__process(&top->tool, event, &sample, machine);
		} else
			++session->hists.stats.nr_unknown_events;
	}
}

static void perf_top__mmap_read(struct perf_top *top)
{
	int i;

	for (i = 0; i < top->evlist->nr_mmaps; i++)
		perf_top__mmap_read_idx(top, i);
}

static void perf_top__start_counters(struct perf_top *top)
{
	struct perf_evsel *counter, *first;
	struct perf_evlist *evlist = top->evlist;

	first = list_entry(evlist->entries.next, struct perf_evsel, node);

	list_for_each_entry(counter, &evlist->entries, node) {
		struct perf_event_attr *attr = &counter->attr;
		struct xyarray *group_fd = NULL;

		if (top->group && counter != first)
			group_fd = first->fd;

		attr->sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID;

		if (top->freq) {
			attr->sample_type |= PERF_SAMPLE_PERIOD;
			attr->freq	  = 1;
			attr->sample_freq = top->freq;
		}

		if (evlist->nr_entries > 1) {
			attr->sample_type |= PERF_SAMPLE_ID;
			attr->read_format |= PERF_FORMAT_ID;
		}

		if (perf_target__has_cpu(&top->target))
			attr->sample_type |= PERF_SAMPLE_CPU;

		if (symbol_conf.use_callchain)
			attr->sample_type |= PERF_SAMPLE_CALLCHAIN;

		attr->mmap = 1;
		attr->comm = 1;
		attr->inherit = top->inherit;
fallback_missing_features:
		if (top->exclude_guest_missing)
			attr->exclude_guest = attr->exclude_host = 0;
retry_sample_id:
		attr->sample_id_all = top->sample_id_all_missing ? 0 : 1;
try_again:
		if (perf_evsel__open(counter, top->evlist->cpus,
				     top->evlist->threads, top->group,
				     group_fd) < 0) {
			int err = errno;

			if (err == EPERM || err == EACCES) {
				ui__error_paranoid();
				goto out_err;
			} else if (err == EINVAL) {
				if (!top->exclude_guest_missing &&
				    (attr->exclude_guest || attr->exclude_host)) {
					pr_debug("Old kernel, cannot exclude "
						 "guest or host samples.\n");
					top->exclude_guest_missing = true;
					goto fallback_missing_features;
				} else if (!top->sample_id_all_missing) {
					/*
					 * Old kernel, no attr->sample_id_type_all field
					 */
					top->sample_id_all_missing = true;
					goto retry_sample_id;
				}
			}
			/*
			 * If it's cycles then fall back to hrtimer
			 * based cpu-clock-tick sw counter, which
			 * is always available even if no PMU support:
			 */
			if (attr->type == PERF_TYPE_HARDWARE &&
			    attr->config == PERF_COUNT_HW_CPU_CYCLES) {
				if (verbose)
					ui__warning("Cycles event not supported,\n"
						    "trying to fall back to cpu-clock-ticks\n");

				attr->type = PERF_TYPE_SOFTWARE;
				attr->config = PERF_COUNT_SW_CPU_CLOCK;
				if (counter->name) {
					free(counter->name);
					counter->name = NULL;
				}
				goto try_again;
			}

			if (err == ENOENT) {
				ui__error("The %s event is not supported.\n",
					  perf_evsel__name(counter));
				goto out_err;
			} else if (err == EMFILE) {
				ui__error("Too many events are opened.\n"
					    "Try again after reducing the number of events\n");
				goto out_err;
			}

			ui__error("The sys_perf_event_open() syscall "
				    "returned with %d (%s).  /bin/dmesg "
				    "may provide additional information.\n"
				    "No CONFIG_PERF_EVENTS=y kernel support "
				    "configured?\n", err, strerror(err));
			goto out_err;
		}
	}

	if (perf_evlist__mmap(evlist, top->mmap_pages, false) < 0) {
		ui__error("Failed to mmap with %d (%s)\n",
			    errno, strerror(errno));
		goto out_err;
	}

	return;

out_err:
	exit_browser(0);
	exit(0);
}

static int perf_top__setup_sample_type(struct perf_top *top)
{
	if (!top->sort_has_symbols) {
		if (symbol_conf.use_callchain) {
			ui__error("Selected -g but \"sym\" not present in --sort/-s.");
			return -EINVAL;
		}
	} else if (!top->dont_use_callchains && callchain_param.mode != CHAIN_NONE) {
		if (callchain_register_param(&callchain_param) < 0) {
			ui__error("Can't register callchain params.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int __cmd_top(struct perf_top *top)
{
	pthread_t thread;
	int ret;
	/*
	 * FIXME: perf_session__new should allow passing a O_MMAP, so that all this
	 * mmap reading, etc is encapsulated in it. Use O_WRONLY for now.
	 */
	top->session = perf_session__new(NULL, O_WRONLY, false, false, NULL);
	if (top->session == NULL)
		return -ENOMEM;

	ret = perf_top__setup_sample_type(top);
	if (ret)
		goto out_delete;

	if (perf_target__has_task(&top->target))
		perf_event__synthesize_thread_map(&top->tool, top->evlist->threads,
						  perf_event__process,
						  &top->session->host_machine);
	else
		perf_event__synthesize_threads(&top->tool, perf_event__process,
					       &top->session->host_machine);
	perf_top__start_counters(top);
	top->session->evlist = top->evlist;
	perf_session__update_sample_type(top->session);

	/* Wait for a minimal set of events before starting the snapshot */
	poll(top->evlist->pollfd, top->evlist->nr_fds, 100);

	perf_top__mmap_read(top);

	if (pthread_create(&thread, NULL, (use_browser > 0 ? display_thread_tui :
							    display_thread), top)) {
		ui__error("Could not create display thread.\n");
		exit(-1);
	}

	if (top->realtime_prio) {
		struct sched_param param;

		param.sched_priority = top->realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			ui__error("Could not set realtime priority.\n");
			exit(-1);
		}
	}

	while (1) {
		u64 hits = top->samples;

		perf_top__mmap_read(top);

		if (hits == top->samples)
			ret = poll(top->evlist->pollfd, top->evlist->nr_fds, 100);
	}

out_delete:
	perf_session__delete(top->session);
	top->session = NULL;

	return 0;
}

static int
parse_callchain_opt(const struct option *opt, const char *arg, int unset)
{
	struct perf_top *top = (struct perf_top *)opt->value;
	char *tok, *tok2;
	char *endptr;

	/*
	 * --no-call-graph
	 */
	if (unset) {
		top->dont_use_callchains = true;
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
	} else
		return -1;

	/* get the min percentage */
	tok = strtok(NULL, ",");
	if (!tok)
		goto setup;

	callchain_param.min_percent = strtod(tok, &endptr);
	if (tok == endptr)
		return -1;

	/* get the print limit */
	tok2 = strtok(NULL, ",");
	if (!tok2)
		goto setup;

	if (tok2[0] != 'c') {
		callchain_param.print_limit = strtod(tok2, &endptr);
		tok2 = strtok(NULL, ",");
		if (!tok2)
			goto setup;
	}

	/* get the call chain order */
	if (!strcmp(tok2, "caller"))
		callchain_param.order = ORDER_CALLER;
	else if (!strcmp(tok2, "callee"))
		callchain_param.order = ORDER_CALLEE;
	else
		return -1;
setup:
	if (callchain_register_param(&callchain_param) < 0) {
		fprintf(stderr, "Can't register callchain params\n");
		return -1;
	}
	return 0;
}

static const char * const top_usage[] = {
	"perf top [<options>]",
	NULL
};

int cmd_top(int argc, const char **argv, const char *prefix __used)
{
	struct perf_evsel *pos;
	int status;
	char errbuf[BUFSIZ];
	struct perf_top top = {
		.count_filter	     = 5,
		.delay_secs	     = 2,
		.freq		     = 4000, /* 4 KHz */
		.mmap_pages	     = 128,
		.sym_pcnt_filter     = 5,
		.target		     = {
			.uses_mmap   = true,
		},
	};
	char callchain_default_opt[] = "fractal,0.5,callee";
	const struct option options[] = {
	OPT_CALLBACK('e', "event", &top.evlist, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events_option),
	OPT_INTEGER('c', "count", &top.default_interval,
		    "event period to sample"),
	OPT_STRING('p', "pid", &top.target.pid, "pid",
		    "profile events on existing process id"),
	OPT_STRING('t', "tid", &top.target.tid, "tid",
		    "profile events on existing thread id"),
	OPT_BOOLEAN('a', "all-cpus", &top.target.system_wide,
			    "system-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &top.target.cpu_list, "cpu",
		    "list of cpus to monitor"),
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_BOOLEAN('K', "hide_kernel_symbols", &top.hide_kernel_symbols,
		    "hide kernel symbols"),
	OPT_UINTEGER('m', "mmap-pages", &top.mmap_pages, "number of mmap data pages"),
	OPT_INTEGER('r', "realtime", &top.realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_INTEGER('d', "delay", &top.delay_secs,
		    "number of seconds to delay between refreshes"),
	OPT_BOOLEAN('D', "dump-symtab", &top.dump_symtab,
			    "dump the symbol table used for profiling"),
	OPT_INTEGER('f', "count-filter", &top.count_filter,
		    "only display functions with more events than this"),
	OPT_BOOLEAN('g', "group", &top.group,
			    "put the counters into a counter group"),
	OPT_BOOLEAN('i', "inherit", &top.inherit,
		    "child tasks inherit counters"),
	OPT_STRING(0, "sym-annotate", &top.sym_filter, "symbol name",
		    "symbol to annotate"),
	OPT_BOOLEAN('z', "zero", &top.zero,
		    "zero history across updates"),
	OPT_INTEGER('F', "freq", &top.freq,
		    "profile at this frequency"),
	OPT_INTEGER('E', "entries", &top.print_entries,
		    "display this many functions"),
	OPT_BOOLEAN('U', "hide_user_symbols", &top.hide_user_symbols,
		    "hide user symbols"),
	OPT_BOOLEAN(0, "tui", &top.use_tui, "Use the TUI interface"),
	OPT_BOOLEAN(0, "stdio", &top.use_stdio, "Use the stdio interface"),
	OPT_INCR('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_STRING('s', "sort", &sort_order, "key[,key2...]",
		   "sort by key(s): pid, comm, dso, symbol, parent"),
	OPT_BOOLEAN('n', "show-nr-samples", &symbol_conf.show_nr_samples,
		    "Show a column with the number of samples"),
	OPT_CALLBACK_DEFAULT('G', "call-graph", &top, "output_type,min_percent, call_order",
		     "Display callchains using output_type (graph, flat, fractal, or none), min percent threshold and callchain order. "
		     "Default: fractal,0.5,callee", &parse_callchain_opt,
		     callchain_default_opt),
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
	OPT_STRING('M', "disassembler-style", &disassembler_style, "disassembler style",
		   "Specify disassembler style (e.g. -M intel for intel syntax)"),
	OPT_STRING('u', "uid", &top.target.uid_str, "user", "user to profile"),
	OPT_END()
	};

	top.evlist = perf_evlist__new(NULL, NULL);
	if (top.evlist == NULL)
		return -ENOMEM;

	symbol_conf.exclude_other = false;

	argc = parse_options(argc, argv, options, top_usage, 0);
	if (argc)
		usage_with_options(top_usage, options);

	if (sort_order == default_sort_order)
		sort_order = "dso,symbol";

	setup_sorting(top_usage, options);

	if (top.use_stdio)
		use_browser = 0;
	else if (top.use_tui)
		use_browser = 1;

	setup_browser(false);

	status = perf_target__validate(&top.target);
	if (status) {
		perf_target__strerror(&top.target, status, errbuf, BUFSIZ);
		ui__warning("%s", errbuf);
	}

	status = perf_target__parse_uid(&top.target);
	if (status) {
		int saved_errno = errno;

		perf_target__strerror(&top.target, status, errbuf, BUFSIZ);
		ui__error("%s", errbuf);

		status = -saved_errno;
		goto out_delete_evlist;
	}

	if (perf_target__none(&top.target))
		top.target.system_wide = true;

	if (perf_evlist__create_maps(top.evlist, &top.target) < 0)
		usage_with_options(top_usage, options);

	if (!top.evlist->nr_entries &&
	    perf_evlist__add_default(top.evlist) < 0) {
		ui__error("Not enough memory for event selector list\n");
		return -ENOMEM;
	}

	symbol_conf.nr_events = top.evlist->nr_entries;

	if (top.delay_secs < 1)
		top.delay_secs = 1;

	/*
	 * User specified count overrides default frequency.
	 */
	if (top.default_interval)
		top.freq = 0;
	else if (top.freq) {
		top.default_interval = top.freq;
	} else {
		ui__error("frequency and count are zero, aborting\n");
		exit(EXIT_FAILURE);
	}

	list_for_each_entry(pos, &top.evlist->entries, node) {
		/*
		 * Fill in the ones not specifically initialized via -c:
		 */
		if (!pos->attr.sample_period)
			pos->attr.sample_period = top.default_interval;
	}

	top.sym_evsel = list_entry(top.evlist->entries.next, struct perf_evsel, node);

	symbol_conf.priv_size = sizeof(struct annotation);

	symbol_conf.try_vmlinux_path = (symbol_conf.vmlinux_name == NULL);
	if (symbol__init() < 0)
		return -1;

	sort_entry__setup_elide(&sort_dso, symbol_conf.dso_list, "dso", stdout);
	sort_entry__setup_elide(&sort_comm, symbol_conf.comm_list, "comm", stdout);
	sort_entry__setup_elide(&sort_sym, symbol_conf.sym_list, "symbol", stdout);

	/*
	 * Avoid annotation data structures overhead when symbols aren't on the
	 * sort list.
	 */
	top.sort_has_symbols = sort_sym.list.next != NULL;

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
