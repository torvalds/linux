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
#include "util/rbtree.h"
#include "util/parse-options.h"
#include "util/parse-events.h"

#include <assert.h>
#include <fcntl.h>

#include <stdio.h>

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

static u64			count_filter			=  5;
static int			print_entries			= 15;

static int			target_pid			= -1;
static int			profile_cpu			= -1;
static int			nr_cpus				=  0;
static unsigned int		realtime_prio			=  0;
static int			group				=  0;
static unsigned int		page_size;
static unsigned int		mmap_pages			= 16;
static int			freq				=  0;
static int			verbose				=  0;

static char			*sym_filter;
static unsigned long		filter_start;
static unsigned long		filter_end;

static int			delay_secs			=  2;
static int			zero;
static int			dump_symtab;

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
};

struct sym_entry		*sym_filter_entry;

struct dso			*kernel_dso;

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
	int counter;
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
		syme->snap_count = syme->count[0];
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

	if (nr_counters == 1) {
		printf("%Ld", (u64)attrs[0].sample_period);
		if (freq)
			printf("Hz ");
		else
			printf(" ");
	}

	for (counter = 0; counter < nr_counters; counter++) {
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

	if (nr_counters == 1)
		printf("             samples    pcnt");
	else
		printf("  weight     samples    pcnt");

	printf("         RIP          kernel function\n"
	       	       "  ______     _______   _____   ________________   _______________\n\n"
	);

	for (nd = rb_first(&tmp); nd; nd = rb_next(nd)) {
		struct sym_entry *syme = rb_entry(nd, struct sym_entry, rb_node);
		struct symbol *sym = (struct symbol *)(syme + 1);
		char *color = PERF_COLOR_NORMAL;
		double pcnt;

		if (++printed > print_entries || syme->snap_count < count_filter)
			continue;

		pcnt = 100.0 - (100.0 * ((sum_ksamples - syme->snap_count) /
					 sum_ksamples));

		/*
		 * We color high-overhead entries in red, mid-overhead
		 * entries in green - and keep the low overhead places
		 * normal:
		 */
		if (pcnt >= 5.0) {
			color = PERF_COLOR_RED;
		} else {
			if (pcnt >= 0.5)
				color = PERF_COLOR_GREEN;
		}

		if (nr_counters == 1)
			printf("%20.2f - ", syme->weight);
		else
			printf("%9.1f %10ld - ", syme->weight, syme->snap_count);

		color_fprintf(stdout, color, "%4.1f%%", pcnt);
		printf(" - %016llx : %s\n", sym->start, sym->name);
	}
}

static void *display_thread(void *arg)
{
	struct pollfd stdin_poll = { .fd = 0, .events = POLLIN };
	int delay_msecs = delay_secs * 1000;

	printf("PerfTop refresh period: %d seconds\n", delay_secs);

	do {
		print_sym_table();
	} while (!poll(&stdin_poll, 1, delay_msecs) == 1);

	printf("key pressed - exiting.\n");
	exit(0);

	return NULL;
}

static int symbol_filter(struct dso *self, struct symbol *sym)
{
	static int filter_match;
	struct sym_entry *syme;
	const char *name = sym->name;

	if (!strcmp(name, "_text") ||
	    !strcmp(name, "_etext") ||
	    !strcmp(name, "_sinittext") ||
	    !strncmp("init_module", name, 11) ||
	    !strncmp("cleanup_module", name, 14) ||
	    strstr(name, "_text_start") ||
	    strstr(name, "_text_end"))
		return 1;

	syme = dso__sym_priv(self, sym);
	/* Tag samples to be skipped. */
	if (!strcmp("default_idle", name) ||
	    !strcmp("cpu_idle", name) ||
	    !strcmp("enter_idle", name) ||
	    !strcmp("exit_idle", name) ||
	    !strcmp("mwait_idle", name))
		syme->skip = 1;

	if (filter_match == 1) {
		filter_end = sym->start;
		filter_match = -1;
		if (filter_end - filter_start > 10000) {
			fprintf(stderr,
				"hm, too large filter symbol <%s> - skipping.\n",
				sym_filter);
			fprintf(stderr, "symbol filter start: %016lx\n",
				filter_start);
			fprintf(stderr, "                end: %016lx\n",
				filter_end);
			filter_end = filter_start = 0;
			sym_filter = NULL;
			sleep(1);
		}
	}

	if (filter_match == 0 && sym_filter && !strcmp(name, sym_filter)) {
		filter_match = 1;
		filter_start = sym->start;
	}


	return 0;
}

static int parse_symbols(void)
{
	struct rb_node *node;
	struct symbol  *sym;

	kernel_dso = dso__new("[kernel]", sizeof(struct sym_entry));
	if (kernel_dso == NULL)
		return -1;

	if (dso__load_kernel(kernel_dso, NULL, symbol_filter, 1) != 0)
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

#define TRACE_COUNT     3

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
			pthread_mutex_lock(&active_symbols_lock);
			if (list_empty(&syme->node) || !syme->node.next)
				__list_insert_active_sym(syme);
			pthread_mutex_unlock(&active_symbols_lock);
			return;
		}
	}

	samples--;
}

static void process_event(u64 ip, int counter)
{
	samples++;

	if (ip < min_ip || ip > max_ip) {
		userspace_samples++;
		return;
	}

	record_ip(ip, counter);
}

struct mmap_data {
	int			counter;
	void			*base;
	unsigned int		mask;
	unsigned int		prev;
};

static unsigned int mmap_read_head(struct mmap_data *md)
{
	struct perf_counter_mmap_page *pc = md->base;
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
		struct ip_event {
			struct perf_event_header header;
			u64 ip;
			u32 pid, target_pid;
		};
		struct mmap_event {
			struct perf_event_header header;
			u32 pid, target_pid;
			u64 start;
			u64 len;
			u64 pgoff;
			char filename[PATH_MAX];
		};

		typedef union event_union {
			struct perf_event_header header;
			struct ip_event ip;
			struct mmap_event mmap;
		} event_t;

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

		if (event->header.misc & PERF_EVENT_MISC_OVERFLOW) {
			if (event->header.type & PERF_SAMPLE_IP)
				process_event(event->ip.ip, md->counter);
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
	struct perf_counter_attr *attr;
	unsigned int cpu;

	cpu = profile_cpu;
	if (target_pid == -1 && profile_cpu == -1)
		cpu = i;

	attr = attrs + counter;

	attr->sample_type	= PERF_SAMPLE_IP | PERF_SAMPLE_TID;
	attr->freq		= freq;

try_again:
	fd[i][counter] = sys_perf_counter_open(attr, target_pid, cpu, group_fd, 0);

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
		die("No CONFIG_PERF_COUNTERS=y kernel support configured?\n");
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
	OPT_STRING('s', "sym-filter", &sym_filter, "pattern",
		    "only display symbols matchig this pattern"),
	OPT_BOOLEAN('z', "zero", &group,
		    "zero history across updates"),
	OPT_INTEGER('F', "freq", &freq,
		    "profile at this frequency"),
	OPT_INTEGER('E', "entries", &print_entries,
		    "display this many functions"),
	OPT_BOOLEAN('v', "verbose", &verbose,
		    "be more verbose (show counter open errors, etc)"),
	OPT_END()
};

int cmd_top(int argc, const char **argv, const char *prefix)
{
	int counter;

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
