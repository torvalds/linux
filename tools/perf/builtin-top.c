/*
 * builtin-top.c
 *
 * Builtin top command: Display a continuously updated profile of
 * any workload, CPU or specific PID.
 *
 * Copyright (C) 2008, Red Hat Inc, Ingo Molnar <mingo@redhat.com>
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

#include "util/symbol.h"
#include "util/color.h"
#include "util/util.h"
#include <linux/rbtree.h>
#include "util/parse-options.h"
#include "util/parse-events.h"

#include "util/debug.h"

#include <assert.h>
#include <fcntl.h>

#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include <errno.h>
#include <time.h>
#include <sched.h>
#include <pthread.h>

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/mman.h>

#include <linux/unistd.h>
#include <linux/types.h>

static int			fd[MAX_NR_CPUS][MAX_COUNTERS];

static int			system_wide			=  0;

static int			default_interval		= 100000;

static int			count_filter			=  5;
static int			print_entries			= 15;

static int			target_pid			= -1;
static int			inherit				=  0;
static int			profile_cpu			= -1;
static int			nr_cpus				=  0;
static unsigned int		realtime_prio			=  0;
static int			group				=  0;
static unsigned int		page_size;
static unsigned int		mmap_pages			= 16;
static int			freq				=  0;

static int			delay_secs			=  2;
static int			zero;
static int			dump_symtab;

/*
 * Source
 */

struct source_line {
	u64			eip;
	unsigned long		count[MAX_COUNTERS];
	char			*line;
	struct source_line	*next;
};

static char			*sym_filter			=  NULL;
struct sym_entry		*sym_filter_entry		=  NULL;
static int			sym_pcnt_filter			=  5;
static int			sym_counter			=  0;
static int			display_weighted		= -1;

/*
 * Symbols
 */

static u64			min_ip;
static u64			max_ip = -1ll;

struct sym_entry {
	struct rb_node		rb_node;
	struct list_head	node;
	unsigned long		count[MAX_COUNTERS];
	unsigned long		snap_count;
	double			weight;
	int			skip;
	struct source_line	*source;
	struct source_line	*lines;
	struct source_line	**lines_tail;
	pthread_mutex_t		source_lock;
};

/*
 * Source functions
 */

static void parse_source(struct sym_entry *syme)
{
	struct symbol *sym;
	struct module *module;
	struct section *section = NULL;
	FILE *file;
	char command[PATH_MAX*2];
	const char *path = vmlinux_name;
	u64 start, end, len;

	if (!syme)
		return;

	if (syme->lines) {
		pthread_mutex_lock(&syme->source_lock);
		goto out_assign;
	}

	sym = (struct symbol *)(syme + 1);
	module = sym->module;

	if (module)
		path = module->path;
	if (!path)
		return;

	start = sym->obj_start;
	if (!start)
		start = sym->start;

	if (module) {
		section = module->sections->find_section(module->sections, ".text");
		if (section)
			start -= section->vma;
	}

	end = start + sym->end - sym->start + 1;
	len = sym->end - sym->start;

	sprintf(command, "objdump --start-address=0x%016Lx --stop-address=0x%016Lx -dS %s", start, end, path);

	file = popen(command, "r");
	if (!file)
		return;

	pthread_mutex_lock(&syme->source_lock);
	syme->lines_tail = &syme->lines;
	while (!feof(file)) {
		struct source_line *src;
		size_t dummy = 0;
		char *c;

		src = malloc(sizeof(struct source_line));
		assert(src != NULL);
		memset(src, 0, sizeof(struct source_line));

		if (getline(&src->line, &dummy, file) < 0)
			break;
		if (!src->line)
			break;

		c = strchr(src->line, '\n');
		if (c)
			*c = 0;

		src->next = NULL;
		*syme->lines_tail = src;
		syme->lines_tail = &src->next;

		if (strlen(src->line)>8 && src->line[8] == ':') {
			src->eip = strtoull(src->line, NULL, 16);
			if (section)
				src->eip += section->vma;
		}
		if (strlen(src->line)>8 && src->line[16] == ':') {
			src->eip = strtoull(src->line, NULL, 16);
			if (section)
				src->eip += section->vma;
		}
	}
	pclose(file);
out_assign:
	sym_filter_entry = syme;
	pthread_mutex_unlock(&syme->source_lock);
}

static void __zero_source_counters(struct sym_entry *syme)
{
	int i;
	struct source_line *line;

	line = syme->lines;
	while (line) {
		for (i = 0; i < nr_counters; i++)
			line->count[i] = 0;
		line = line->next;
	}
}

static void record_precise_ip(struct sym_entry *syme, int counter, u64 ip)
{
	struct source_line *line;

	if (syme != sym_filter_entry)
		return;

	if (pthread_mutex_trylock(&syme->source_lock))
		return;

	if (!syme->source)
		goto out_unlock;

	for (line = syme->lines; line; line = line->next) {
		if (line->eip == ip) {
			line->count[counter]++;
			break;
		}
		if (line->eip > ip)
			break;
	}
out_unlock:
	pthread_mutex_unlock(&syme->source_lock);
}

static void lookup_sym_source(struct sym_entry *syme)
{
	struct symbol *symbol = (struct symbol *)(syme + 1);
	struct source_line *line;
	char pattern[PATH_MAX];
	char *idx;

	sprintf(pattern, "<%s>:", symbol->name);

	if (symbol->module) {
		idx = strstr(pattern, "\t");
		if (idx)
			*idx = 0;
	}

	pthread_mutex_lock(&syme->source_lock);
	for (line = syme->lines; line; line = line->next) {
		if (strstr(line->line, pattern)) {
			syme->source = line;
			break;
		}
	}
	pthread_mutex_unlock(&syme->source_lock);
}

static void show_lines(struct source_line *queue, int count, int total)
{
	int i;
	struct source_line *line;

	line = queue;
	for (i = 0; i < count; i++) {
		float pcnt = 100.0*(float)line->count[sym_counter]/(float)total;

		printf("%8li %4.1f%%\t%s\n", line->count[sym_counter], pcnt, line->line);
		line = line->next;
	}
}

#define TRACE_COUNT     3

static void show_details(struct sym_entry *syme)
{
	struct symbol *symbol;
	struct source_line *line;
	struct source_line *line_queue = NULL;
	int displayed = 0;
	int line_queue_count = 0, total = 0, more = 0;

	if (!syme)
		return;

	if (!syme->source)
		lookup_sym_source(syme);

	if (!syme->source)
		return;

	symbol = (struct symbol *)(syme + 1);
	printf("Showing %s for %s\n", event_name(sym_counter), symbol->name);
	printf("  Events  Pcnt (>=%d%%)\n", sym_pcnt_filter);

	pthread_mutex_lock(&syme->source_lock);
	line = syme->source;
	while (line) {
		total += line->count[sym_counter];
		line = line->next;
	}

	line = syme->source;
	while (line) {
		float pcnt = 0.0;

		if (!line_queue_count)
			line_queue = line;
		line_queue_count++;

		if (line->count[sym_counter])
			pcnt = 100.0 * line->count[sym_counter] / (float)total;
		if (pcnt >= (float)sym_pcnt_filter) {
			if (displayed <= print_entries)
				show_lines(line_queue, line_queue_count, total);
			else more++;
			displayed += line_queue_count;
			line_queue_count = 0;
			line_queue = NULL;
		} else if (line_queue_count > TRACE_COUNT) {
			line_queue = line_queue->next;
			line_queue_count--;
		}

		line->count[sym_counter] = zero ? 0 : line->count[sym_counter] * 7 / 8;
		line = line->next;
	}
	pthread_mutex_unlock(&syme->source_lock);
	if (more)
		printf("%d lines not displayed, maybe increase display entries [e]\n", more);
}

/*
 * Symbols will be added here in record_ip and will get out
 * after decayed.
 */
static LIST_HEAD(active_symbols);
static pthread_mutex_t active_symbols_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Ordering weight: count-1 * count-2 * ... / count-n
 */
static double sym_weight(const struct sym_entry *sym)
{
	double weight = sym->snap_count;
	int counter;

	if (!display_weighted)
		return weight;

	for (counter = 1; counter < nr_counters-1; counter++)
		weight *= sym->count[counter];

	weight /= (sym->count[counter] + 1);

	return weight;
}

static long			samples;
static long			userspace_samples;
static const char		CONSOLE_CLEAR[] = "[H[2J";

static void __list_insert_active_sym(struct sym_entry *syme)
{
	list_add(&syme->node, &active_symbols);
}

static void list_remove_active_sym(struct sym_entry *syme)
{
	pthread_mutex_lock(&active_symbols_lock);
	list_del_init(&syme->node);
	pthread_mutex_unlock(&active_symbols_lock);
}

static void rb_insert_active_sym(struct rb_root *tree, struct sym_entry *se)
{
	struct rb_node **p = &tree->rb_node;
	struct rb_node *parent = NULL;
	struct sym_entry *iter;

	while (*p != NULL) {
		parent = *p;
		iter = rb_entry(parent, struct sym_entry, rb_node);

		if (se->weight > iter->weight)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&se->rb_node, parent, p);
	rb_insert_color(&se->rb_node, tree);
}

static void print_sym_table(void)
{
	int printed = 0, j;
	int counter, snap = !display_weighted ? sym_counter : 0;
	float samples_per_sec = samples/delay_secs;
	float ksamples_per_sec = (samples-userspace_samples)/delay_secs;
	float sum_ksamples = 0.0;
	struct sym_entry *syme, *n;
	struct rb_root tmp = RB_ROOT;
	struct rb_node *nd;

	samples = userspace_samples = 0;

	/* Sort the active symbols */
	pthread_mutex_lock(&active_symbols_lock);
	syme = list_entry(active_symbols.next, struct sym_entry, node);
	pthread_mutex_unlock(&active_symbols_lock);

	list_for_each_entry_safe_from(syme, n, &active_symbols, node) {
		syme->snap_count = syme->count[snap];
		if (syme->snap_count != 0) {
			syme->weight = sym_weight(syme);
			rb_insert_active_sym(&tmp, syme);
			sum_ksamples += syme->snap_count;

			for (j = 0; j < nr_counters; j++)
				syme->count[j] = zero ? 0 : syme->count[j] * 7 / 8;
		} else
			list_remove_active_sym(syme);
	}

	puts(CONSOLE_CLEAR);

	printf(
"------------------------------------------------------------------------------\n");
	printf( "   PerfTop:%8.0f irqs/sec  kernel:%4.1f%% [",
		samples_per_sec,
		100.0 - (100.0*((samples_per_sec-ksamples_per_sec)/samples_per_sec)));

	if (nr_counters == 1 || !display_weighted) {
		printf("%Ld", (u64)attrs[0].sample_period);
		if (freq)
			printf("Hz ");
		else
			printf(" ");
	}

	if (!display_weighted)
		printf("%s", event_name(sym_counter));
	else for (counter = 0; counter < nr_counters; counter++) {
		if (counter)
			printf("/");

		printf("%s", event_name(counter));
	}

	printf( "], ");

	if (target_pid != -1)
		printf(" (target_pid: %d", target_pid);
	else
		printf(" (all");

	if (profile_cpu != -1)
		printf(", cpu: %d)\n", profile_cpu);
	else {
		if (target_pid != -1)
			printf(")\n");
		else
			printf(", %d CPUs)\n", nr_cpus);
	}

	printf("------------------------------------------------------------------------------\n\n");

	if (sym_filter_entry) {
		show_details(sym_filter_entry);
		return;
	}

	if (nr_counters == 1)
		printf("             samples    pcnt");
	else
		printf("   weight    samples    pcnt");

	if (verbose)
		printf("         RIP       ");
	printf("   kernel function\n");
	printf("   %s    _______   _____",
	       nr_counters == 1 ? "      " : "______");
	if (verbose)
		printf("   ________________");
	printf("   _______________\n\n");

	for (nd = rb_first(&tmp); nd; nd = rb_next(nd)) {
		struct symbol *sym;
		double pcnt;

		syme = rb_entry(nd, struct sym_entry, rb_node);
		sym = (struct symbol *)(syme + 1);

		if (++printed > print_entries || (int)syme->snap_count < count_filter)
			continue;

		pcnt = 100.0 - (100.0 * ((sum_ksamples - syme->snap_count) /
					 sum_ksamples));

		if (nr_counters == 1 || !display_weighted)
			printf("%20.2f - ", syme->weight);
		else
			printf("%9.1f %10ld - ", syme->weight, syme->snap_count);

		percent_color_fprintf(stdout, "%4.1f%%", pcnt);
		if (verbose)
			printf(" - %016llx", sym->start);
		printf(" : %s", sym->name);
		if (sym->module)
			printf("\t[%s]", sym->module->name);
		printf("\n");
	}
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

static void prompt_symbol(struct sym_entry **target, const char *msg)
{
	char *buf = malloc(0), *p;
	struct sym_entry *syme = *target, *n, *found = NULL;
	size_t dummy = 0;

	/* zero counters of active symbol */
	if (syme) {
		pthread_mutex_lock(&syme->source_lock);
		__zero_source_counters(syme);
		*target = NULL;
		pthread_mutex_unlock(&syme->source_lock);
	}

	fprintf(stdout, "\n%s: ", msg);
	if (getline(&buf, &dummy, stdin) < 0)
		goto out_free;

	p = strchr(buf, '\n');
	if (p)
		*p = 0;

	pthread_mutex_lock(&active_symbols_lock);
	syme = list_entry(active_symbols.next, struct sym_entry, node);
	pthread_mutex_unlock(&active_symbols_lock);

	list_for_each_entry_safe_from(syme, n, &active_symbols, node) {
		struct symbol *sym = (struct symbol *)(syme + 1);

		if (!strcmp(buf, sym->name)) {
			found = syme;
			break;
		}
	}

	if (!found) {
		fprintf(stderr, "Sorry, %s is not active.\n", sym_filter);
		sleep(1);
		return;
	} else
		parse_source(found);

out_free:
	free(buf);
}

static void print_mapped_keys(void)
{
	char *name = NULL;

	if (sym_filter_entry) {
		struct symbol *sym = (struct symbol *)(sym_filter_entry+1);
		name = sym->name;
	}

	fprintf(stdout, "\nMapped keys:\n");
	fprintf(stdout, "\t[d]     display refresh delay.             \t(%d)\n", delay_secs);
	fprintf(stdout, "\t[e]     display entries (lines).           \t(%d)\n", print_entries);

	if (nr_counters > 1)
		fprintf(stdout, "\t[E]     active event counter.              \t(%s)\n", event_name(sym_counter));

	fprintf(stdout, "\t[f]     profile display filter (count).    \t(%d)\n", count_filter);

	if (vmlinux_name) {
		fprintf(stdout, "\t[F]     annotate display filter (percent). \t(%d%%)\n", sym_pcnt_filter);
		fprintf(stdout, "\t[s]     annotate symbol.                   \t(%s)\n", name?: "NULL");
		fprintf(stdout, "\t[S]     stop annotation.\n");
	}

	if (nr_counters > 1)
		fprintf(stdout, "\t[w]     toggle display weighted/count[E]r. \t(%d)\n", display_weighted ? 1 : 0);

	fprintf(stdout, "\t[z]     toggle sample zeroing.             \t(%d)\n", zero ? 1 : 0);
	fprintf(stdout, "\t[qQ]    quit.\n");
}

static int key_mapped(int c)
{
	switch (c) {
		case 'd':
		case 'e':
		case 'f':
		case 'z':
		case 'q':
		case 'Q':
			return 1;
		case 'E':
		case 'w':
			return nr_counters > 1 ? 1 : 0;
		case 'F':
		case 's':
		case 'S':
			return vmlinux_name ? 1 : 0;
		default:
			break;
	}

	return 0;
}

static void handle_keypress(int c)
{
	if (!key_mapped(c)) {
		struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };
		struct termios tc, save;

		print_mapped_keys();
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
		if (!key_mapped(c))
			return;
	}

	switch (c) {
		case 'd':
			prompt_integer(&delay_secs, "Enter display delay");
			break;
		case 'e':
			prompt_integer(&print_entries, "Enter display entries (lines)");
			break;
		case 'E':
			if (nr_counters > 1) {
				int i;

				fprintf(stderr, "\nAvailable events:");
				for (i = 0; i < nr_counters; i++)
					fprintf(stderr, "\n\t%d %s", i, event_name(i));

				prompt_integer(&sym_counter, "Enter details event counter");

				if (sym_counter >= nr_counters) {
					fprintf(stderr, "Sorry, no such event, using %s.\n", event_name(0));
					sym_counter = 0;
					sleep(1);
				}
			} else sym_counter = 0;
			break;
		case 'f':
			prompt_integer(&count_filter, "Enter display event count filter");
			break;
		case 'F':
			prompt_percent(&sym_pcnt_filter, "Enter details display event filter (percent)");
			break;
		case 'q':
		case 'Q':
			printf("exiting.\n");
			exit(0);
		case 's':
			prompt_symbol(&sym_filter_entry, "Enter details symbol");
			break;
		case 'S':
			if (!sym_filter_entry)
				break;
			else {
				struct sym_entry *syme = sym_filter_entry;

				pthread_mutex_lock(&syme->source_lock);
				sym_filter_entry = NULL;
				__zero_source_counters(syme);
				pthread_mutex_unlock(&syme->source_lock);
			}
			break;
		case 'w':
			display_weighted = ~display_weighted;
			break;
		case 'z':
			zero = ~zero;
			break;
		default:
			break;
	}
}

static void *display_thread(void *arg __used)
{
	struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };
	struct termios tc, save;
	int delay_msecs, c;

	tcgetattr(0, &save);
	tc = save;
	tc.c_lflag &= ~(ICANON | ECHO);
	tc.c_cc[VMIN] = 0;
	tc.c_cc[VTIME] = 0;

repeat:
	delay_msecs = delay_secs * 1000;
	tcsetattr(0, TCSANOW, &tc);
	/* trash return*/
	getc(stdin);

	do {
		print_sym_table();
	} while (!poll(&stdin_poll, 1, delay_msecs) == 1);

	c = getc(stdin);
	tcsetattr(0, TCSAFLUSH, &save);

	handle_keypress(c);
	goto repeat;

	return NULL;
}

/* Tag samples to be skipped. */
static const char *skip_symbols[] = {
	"default_idle",
	"cpu_idle",
	"enter_idle",
	"exit_idle",
	"mwait_idle",
	"mwait_idle_with_hints",
	"ppc64_runlatch_off",
	"pseries_dedicated_idle_sleep",
	NULL
};

static int symbol_filter(struct dso *self, struct symbol *sym)
{
	struct sym_entry *syme;
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

	syme = dso__sym_priv(self, sym);
	pthread_mutex_init(&syme->source_lock, NULL);
	if (!sym_filter_entry && sym_filter && !strcmp(name, sym_filter))
		sym_filter_entry = syme;

	for (i = 0; skip_symbols[i]; i++) {
		if (!strcmp(skip_symbols[i], name)) {
			syme->skip = 1;
			break;
		}
	}

	return 0;
}

static int parse_symbols(void)
{
	struct rb_node *node;
	struct symbol  *sym;
	int use_modules = vmlinux_name ? 1 : 0;

	kernel_dso = dso__new("[kernel]", sizeof(struct sym_entry));
	if (kernel_dso == NULL)
		return -1;

	if (dso__load_kernel(kernel_dso, vmlinux_name, symbol_filter, verbose, use_modules) <= 0)
		goto out_delete_dso;

	node = rb_first(&kernel_dso->syms);
	sym = rb_entry(node, struct symbol, rb_node);
	min_ip = sym->start;

	node = rb_last(&kernel_dso->syms);
	sym = rb_entry(node, struct symbol, rb_node);
	max_ip = sym->end;

	if (dump_symtab)
		dso__fprintf(kernel_dso, stderr);

	return 0;

out_delete_dso:
	dso__delete(kernel_dso);
	kernel_dso = NULL;
	return -1;
}

/*
 * Binary search in the histogram table and record the hit:
 */
static void record_ip(u64 ip, int counter)
{
	struct symbol *sym = dso__find_symbol(kernel_dso, ip);

	if (sym != NULL) {
		struct sym_entry *syme = dso__sym_priv(kernel_dso, sym);

		if (!syme->skip) {
			syme->count[counter]++;
			record_precise_ip(syme, counter, ip);
			pthread_mutex_lock(&active_symbols_lock);
			if (list_empty(&syme->node) || !syme->node.next)
				__list_insert_active_sym(syme);
			pthread_mutex_unlock(&active_symbols_lock);
			return;
		}
	}

	samples--;
}

static void process_event(u64 ip, int counter, int user)
{
	samples++;

	if (user) {
		userspace_samples++;
		return;
	}

	record_ip(ip, counter);
}

struct mmap_data {
	int			counter;
	void			*base;
	int			mask;
	unsigned int		prev;
};

static unsigned int mmap_read_head(struct mmap_data *md)
{
	struct perf_event_mmap_page *pc = md->base;
	int head;

	head = pc->data_head;
	rmb();

	return head;
}

struct timeval last_read, this_read;

static void mmap_read_counter(struct mmap_data *md)
{
	unsigned int head = mmap_read_head(md);
	unsigned int old = md->prev;
	unsigned char *data = md->base + page_size;
	int diff;

	gettimeofday(&this_read, NULL);

	/*
	 * If we're further behind than half the buffer, there's a chance
	 * the writer will bite our tail and mess up the samples under us.
	 *
	 * If we somehow ended up ahead of the head, we got messed up.
	 *
	 * In either case, truncate and restart at head.
	 */
	diff = head - old;
	if (diff > md->mask / 2 || diff < 0) {
		struct timeval iv;
		unsigned long msecs;

		timersub(&this_read, &last_read, &iv);
		msecs = iv.tv_sec*1000 + iv.tv_usec/1000;

		fprintf(stderr, "WARNING: failed to keep up with mmap data."
				"  Last read %lu msecs ago.\n", msecs);

		/*
		 * head points to a known good entry, start there.
		 */
		old = head;
	}

	last_read = this_read;

	for (; old != head;) {
		event_t *event = (event_t *)&data[old & md->mask];

		event_t event_copy;

		size_t size = event->header.size;

		/*
		 * Event straddles the mmap boundary -- header should always
		 * be inside due to u64 alignment of output.
		 */
		if ((old & md->mask) + size != ((old + size) & md->mask)) {
			unsigned int offset = old;
			unsigned int len = min(sizeof(*event), size), cpy;
			void *dst = &event_copy;

			do {
				cpy = min(md->mask + 1 - (offset & md->mask), len);
				memcpy(dst, &data[offset & md->mask], cpy);
				offset += cpy;
				dst += cpy;
				len -= cpy;
			} while (len);

			event = &event_copy;
		}

		old += size;

		if (event->header.type == PERF_RECORD_SAMPLE) {
			int user =
	(event->header.misc & PERF_RECORD_MISC_CPUMODE_MASK) == PERF_RECORD_MISC_USER;
			process_event(event->ip.ip, md->counter, user);
		}
	}

	md->prev = old;
}

static struct pollfd event_array[MAX_NR_CPUS * MAX_COUNTERS];
static struct mmap_data mmap_array[MAX_NR_CPUS][MAX_COUNTERS];

static void mmap_read(void)
{
	int i, counter;

	for (i = 0; i < nr_cpus; i++) {
		for (counter = 0; counter < nr_counters; counter++)
			mmap_read_counter(&mmap_array[i][counter]);
	}
}

int nr_poll;
int group_fd;

static void start_counter(int i, int counter)
{
	struct perf_event_attr *attr;
	int cpu;

	cpu = profile_cpu;
	if (target_pid == -1 && profile_cpu == -1)
		cpu = i;

	attr = attrs + counter;

	attr->sample_type	= PERF_SAMPLE_IP | PERF_SAMPLE_TID;
	attr->freq		= freq;
	attr->inherit		= (cpu < 0) && inherit;

try_again:
	fd[i][counter] = sys_perf_event_open(attr, target_pid, cpu, group_fd, 0);

	if (fd[i][counter] < 0) {
		int err = errno;

		if (err == EPERM)
			die("No permission - are you root?\n");
		/*
		 * If it's cycles then fall back to hrtimer
		 * based cpu-clock-tick sw counter, which
		 * is always available even if no PMU support:
		 */
		if (attr->type == PERF_TYPE_HARDWARE
			&& attr->config == PERF_COUNT_HW_CPU_CYCLES) {

			if (verbose)
				warning(" ... trying to fall back to cpu-clock-ticks\n");

			attr->type = PERF_TYPE_SOFTWARE;
			attr->config = PERF_COUNT_SW_CPU_CLOCK;
			goto try_again;
		}
		printf("\n");
		error("perfcounter syscall returned with %d (%s)\n",
			fd[i][counter], strerror(err));
		die("No CONFIG_PERF_EVENTS=y kernel support configured?\n");
		exit(-1);
	}
	assert(fd[i][counter] >= 0);
	fcntl(fd[i][counter], F_SETFL, O_NONBLOCK);

	/*
	 * First counter acts as the group leader:
	 */
	if (group && group_fd == -1)
		group_fd = fd[i][counter];

	event_array[nr_poll].fd = fd[i][counter];
	event_array[nr_poll].events = POLLIN;
	nr_poll++;

	mmap_array[i][counter].counter = counter;
	mmap_array[i][counter].prev = 0;
	mmap_array[i][counter].mask = mmap_pages*page_size - 1;
	mmap_array[i][counter].base = mmap(NULL, (mmap_pages+1)*page_size,
			PROT_READ, MAP_SHARED, fd[i][counter], 0);
	if (mmap_array[i][counter].base == MAP_FAILED)
		die("failed to mmap with %d (%s)\n", errno, strerror(errno));
}

static int __cmd_top(void)
{
	pthread_t thread;
	int i, counter;
	int ret;

	for (i = 0; i < nr_cpus; i++) {
		group_fd = -1;
		for (counter = 0; counter < nr_counters; counter++)
			start_counter(i, counter);
	}

	/* Wait for a minimal set of events before starting the snapshot */
	poll(event_array, nr_poll, 100);

	mmap_read();

	if (pthread_create(&thread, NULL, display_thread, NULL)) {
		printf("Could not create display thread.\n");
		exit(-1);
	}

	if (realtime_prio) {
		struct sched_param param;

		param.sched_priority = realtime_prio;
		if (sched_setscheduler(0, SCHED_FIFO, &param)) {
			printf("Could not set realtime priority.\n");
			exit(-1);
		}
	}

	while (1) {
		int hits = samples;

		mmap_read();

		if (hits == samples)
			ret = poll(event_array, nr_poll, 100);
	}

	return 0;
}

static const char * const top_usage[] = {
	"perf top [<options>]",
	NULL
};

static const struct option options[] = {
	OPT_CALLBACK('e', "event", NULL, "event",
		     "event selector. use 'perf list' to list available events",
		     parse_events),
	OPT_INTEGER('c', "count", &default_interval,
		    "event period to sample"),
	OPT_INTEGER('p', "pid", &target_pid,
		    "profile events on existing pid"),
	OPT_BOOLEAN('a', "all-cpus", &system_wide,
			    "system-wide collection from all CPUs"),
	OPT_INTEGER('C', "CPU", &profile_cpu,
		    "CPU to profile on"),
	OPT_STRING('k', "vmlinux", &vmlinux_name, "file", "vmlinux pathname"),
	OPT_INTEGER('m', "mmap-pages", &mmap_pages,
		    "number of mmap data pages"),
	OPT_INTEGER('r', "realtime", &realtime_prio,
		    "collect data with this RT SCHED_FIFO priority"),
	OPT_INTEGER('d', "delay", &delay_secs,
		    "number of seconds to delay between refreshes"),
	OPT_BOOLEAN('D', "dump-symtab", &dump_symtab,
			    "dump the symbol table used for profiling"),
	OPT_INTEGER('f', "count-filter", &count_filter,
		    "only display functions with more events than this"),
	OPT_BOOLEAN('g', "group", &group,
			    "put the counters into a counter group"),
	OPT_BOOLEAN('i', "inherit", &inherit,
		    "child tasks inherit counters"),
	OPT_STRING('s', "sym-annotate", &sym_filter, "symbol name",
		    "symbol to annotate - requires -k option"),
	OPT_BOOLEAN('z', "zero", &zero,
		    "zero history across updates"),
	OPT_INTEGER('F', "freq", &freq,
		    "profile at this frequency"),
	OPT_INTEGER('E', "entries", &print_entries,
		    "display this many functions"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_END()
};

int cmd_top(int argc, const char **argv, const char *prefix __used)
{
	int counter;

	symbol__init();

	page_size = sysconf(_SC_PAGE_SIZE);

	argc = parse_options(argc, argv, options, top_usage, 0);
	if (argc)
		usage_with_options(top_usage, options);

	if (freq) {
		default_interval = freq;
		freq = 1;
	}

	/* CPU and PID are mutually exclusive */
	if (target_pid != -1 && profile_cpu != -1) {
		printf("WARNING: PID switch overriding CPU\n");
		sleep(1);
		profile_cpu = -1;
	}

	if (!nr_counters)
		nr_counters = 1;

	if (delay_secs < 1)
		delay_secs = 1;

	parse_symbols();
	parse_source(sym_filter_entry);

	/*
	 * Fill in the ones not specifically initialized via -c:
	 */
	for (counter = 0; counter < nr_counters; counter++) {
		if (attrs[counter].sample_period)
			continue;

		attrs[counter].sample_period = default_interval;
	}

	nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	assert(nr_cpus <= MAX_NR_CPUS);
	assert(nr_cpus >= 0);

	if (target_pid != -1 || profile_cpu != -1)
		nr_cpus = 1;

	return __cmd_top();
}
