#include "builtin.h"
#include "perf.h"

#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"

#include "util/parse-options.h"
#include "util/trace-event.h"

#include "util/debug.h"
#include "util/session.h"

#include <sys/types.h>
#include <sys/prctl.h>
#include <semaphore.h>
#include <pthread.h>
#include <math.h>
#include <limits.h>

#include <linux/list.h>
#include <linux/hash.h>

/* based on kernel/lockdep.c */
#define LOCKHASH_BITS		12
#define LOCKHASH_SIZE		(1UL << LOCKHASH_BITS)

static struct list_head lockhash_table[LOCKHASH_SIZE];

#define __lockhashfn(key)	hash_long((unsigned long)key, LOCKHASH_BITS)
#define lockhashentry(key)	(lockhash_table + __lockhashfn((key)))

#define LOCK_STATE_UNLOCKED	0	       /* initial state */
#define LOCK_STATE_LOCKED	1

struct lock_stat {
	struct list_head	hash_entry;
	struct rb_node		rb;		/* used for sorting */

	/*
	 * FIXME: raw_field_value() returns unsigned long long,
	 * so address of lockdep_map should be dealed as 64bit.
	 * Is there more better solution?
	 */
	void			*addr;		/* address of lockdep_map, used as ID */
	char			*name;		/* for strcpy(), we cannot use const */

	int			state;
	u64			prev_event_time; /* timestamp of previous event */

	unsigned int		nr_acquired;
	unsigned int		nr_acquire;
	unsigned int		nr_contended;
	unsigned int		nr_release;

	/* these times are in nano sec. */
	u64			wait_time_total;
	u64			wait_time_min;
	u64			wait_time_max;
};

/* build simple key function one is bigger than two */
#define SINGLE_KEY(member)						\
	static int lock_stat_key_ ## member(struct lock_stat *one,	\
					 struct lock_stat *two)		\
	{								\
		return one->member > two->member;			\
	}

SINGLE_KEY(nr_acquired)
SINGLE_KEY(nr_contended)
SINGLE_KEY(wait_time_total)
SINGLE_KEY(wait_time_min)
SINGLE_KEY(wait_time_max)

struct lock_key {
	/*
	 * name: the value for specify by user
	 * this should be simpler than raw name of member
	 * e.g. nr_acquired -> acquired, wait_time_total -> wait_total
	 */
	const char		*name;
	int			(*key)(struct lock_stat*, struct lock_stat*);
};

static const char		*sort_key = "acquired";

static int			(*compare)(struct lock_stat *, struct lock_stat *);

static struct rb_root		result;	/* place to store sorted data */

#define DEF_KEY_LOCK(name, fn_suffix)	\
	{ #name, lock_stat_key_ ## fn_suffix }
struct lock_key keys[] = {
	DEF_KEY_LOCK(acquired, nr_acquired),
	DEF_KEY_LOCK(contended, nr_contended),
	DEF_KEY_LOCK(wait_total, wait_time_total),
	DEF_KEY_LOCK(wait_min, wait_time_min),
	DEF_KEY_LOCK(wait_max, wait_time_max),

	/* extra comparisons much complicated should be here */

	{ NULL, NULL }
};

static void select_key(void)
{
	int i;

	for (i = 0; keys[i].name; i++) {
		if (!strcmp(keys[i].name, sort_key)) {
			compare = keys[i].key;
			return;
		}
	}

	die("Unknown compare key:%s\n", sort_key);
}

static void insert_to_result(struct lock_stat *st,
			     int (*bigger)(struct lock_stat *, struct lock_stat *))
{
	struct rb_node **rb = &result.rb_node;
	struct rb_node *parent = NULL;
	struct lock_stat *p;

	while (*rb) {
		p = container_of(*rb, struct lock_stat, rb);
		parent = *rb;

		if (bigger(st, p))
			rb = &(*rb)->rb_left;
		else
			rb = &(*rb)->rb_right;
	}

	rb_link_node(&st->rb, parent, rb);
	rb_insert_color(&st->rb, &result);
}

/* returns left most element of result, and erase it */
static struct lock_stat *pop_from_result(void)
{
	struct rb_node *node = result.rb_node;

	if (!node)
		return NULL;

	while (node->rb_left)
		node = node->rb_left;

	rb_erase(node, &result);
	return container_of(node, struct lock_stat, rb);
}

static struct lock_stat *lock_stat_findnew(void *addr, const char *name)
{
	struct list_head *entry = lockhashentry(addr);
	struct lock_stat *ret, *new;

	list_for_each_entry(ret, entry, hash_entry) {
		if (ret->addr == addr)
			return ret;
	}

	new = zalloc(sizeof(struct lock_stat));
	if (!new)
		goto alloc_failed;

	new->addr = addr;
	new->name = zalloc(sizeof(char) * strlen(name) + 1);
	if (!new->name)
		goto alloc_failed;
	strcpy(new->name, name);

	/* LOCK_STATE_UNLOCKED == 0 isn't guaranteed forever */
	new->state = LOCK_STATE_UNLOCKED;
	new->wait_time_min = ULLONG_MAX;

	list_add(&new->hash_entry, entry);
	return new;

alloc_failed:
	die("memory allocation failed\n");
}

static char			const *input_name = "perf.data";

static int			profile_cpu = -1;

struct raw_event_sample {
	u32			size;
	char			data[0];
};

struct trace_acquire_event {
	void			*addr;
	const char		*name;
};

struct trace_acquired_event {
	void			*addr;
	const char		*name;
};

struct trace_contended_event {
	void			*addr;
	const char		*name;
};

struct trace_release_event {
	void			*addr;
	const char		*name;
};

struct trace_lock_handler {
	void (*acquire_event)(struct trace_acquire_event *,
			      struct event *,
			      int cpu,
			      u64 timestamp,
			      struct thread *thread);

	void (*acquired_event)(struct trace_acquired_event *,
			       struct event *,
			       int cpu,
			       u64 timestamp,
			       struct thread *thread);

	void (*contended_event)(struct trace_contended_event *,
				struct event *,
				int cpu,
				u64 timestamp,
				struct thread *thread);

	void (*release_event)(struct trace_release_event *,
			      struct event *,
			      int cpu,
			      u64 timestamp,
			      struct thread *thread);
};

static void
report_lock_acquire_event(struct trace_acquire_event *acquire_event,
			struct event *__event __used,
			int cpu __used,
			u64 timestamp,
			struct thread *thread __used)
{
	struct lock_stat *st;

	st = lock_stat_findnew(acquire_event->addr, acquire_event->name);

	switch (st->state) {
	case LOCK_STATE_UNLOCKED:
		break;
	case LOCK_STATE_LOCKED:
		break;
	default:
		BUG_ON(1);
		break;
	}

	st->prev_event_time = timestamp;
}

static void
report_lock_acquired_event(struct trace_acquired_event *acquired_event,
			 struct event *__event __used,
			 int cpu __used,
			 u64 timestamp,
			 struct thread *thread __used)
{
	struct lock_stat *st;

	st = lock_stat_findnew(acquired_event->addr, acquired_event->name);

	switch (st->state) {
	case LOCK_STATE_UNLOCKED:
		st->state = LOCK_STATE_LOCKED;
		st->nr_acquired++;
		break;
	case LOCK_STATE_LOCKED:
		break;
	default:
		BUG_ON(1);
		break;
	}

	st->prev_event_time = timestamp;
}

static void
report_lock_contended_event(struct trace_contended_event *contended_event,
			  struct event *__event __used,
			  int cpu __used,
			  u64 timestamp,
			  struct thread *thread __used)
{
	struct lock_stat *st;

	st = lock_stat_findnew(contended_event->addr, contended_event->name);

	switch (st->state) {
	case LOCK_STATE_UNLOCKED:
		break;
	case LOCK_STATE_LOCKED:
		st->nr_contended++;
		break;
	default:
		BUG_ON(1);
		break;
	}

	st->prev_event_time = timestamp;
}

static void
report_lock_release_event(struct trace_release_event *release_event,
			struct event *__event __used,
			int cpu __used,
			u64 timestamp,
			struct thread *thread __used)
{
	struct lock_stat *st;
	u64 hold_time;

	st = lock_stat_findnew(release_event->addr, release_event->name);

	switch (st->state) {
	case LOCK_STATE_UNLOCKED:
		break;
	case LOCK_STATE_LOCKED:
		st->state = LOCK_STATE_UNLOCKED;
		hold_time = timestamp - st->prev_event_time;

		if (timestamp < st->prev_event_time) {
			/* terribly, this can happen... */
			goto end;
		}

		if (st->wait_time_min > hold_time)
			st->wait_time_min = hold_time;
		if (st->wait_time_max < hold_time)
			st->wait_time_max = hold_time;
		st->wait_time_total += hold_time;

		st->nr_release++;
		break;
	default:
		BUG_ON(1);
		break;
	}

end:
	st->prev_event_time = timestamp;
}

/* lock oriented handlers */
/* TODO: handlers for CPU oriented, thread oriented */
static struct trace_lock_handler report_lock_ops  = {
	.acquire_event		= report_lock_acquire_event,
	.acquired_event		= report_lock_acquired_event,
	.contended_event	= report_lock_contended_event,
	.release_event		= report_lock_release_event,
};

static struct trace_lock_handler *trace_handler;

static void
process_lock_acquire_event(void *data,
			   struct event *event __used,
			   int cpu __used,
			   u64 timestamp __used,
			   struct thread *thread __used)
{
	struct trace_acquire_event acquire_event;
	u64 tmp;		/* this is required for casting... */

	tmp = raw_field_value(event, "lockdep_addr", data);
	memcpy(&acquire_event.addr, &tmp, sizeof(void *));
	acquire_event.name = (char *)raw_field_ptr(event, "name", data);

	if (trace_handler->acquire_event)
		trace_handler->acquire_event(&acquire_event, event, cpu, timestamp, thread);
}

static void
process_lock_acquired_event(void *data,
			    struct event *event __used,
			    int cpu __used,
			    u64 timestamp __used,
			    struct thread *thread __used)
{
	struct trace_acquired_event acquired_event;
	u64 tmp;		/* this is required for casting... */

	tmp = raw_field_value(event, "lockdep_addr", data);
	memcpy(&acquired_event.addr, &tmp, sizeof(void *));
	acquired_event.name = (char *)raw_field_ptr(event, "name", data);

	if (trace_handler->acquire_event)
		trace_handler->acquired_event(&acquired_event, event, cpu, timestamp, thread);
}

static void
process_lock_contended_event(void *data,
			     struct event *event __used,
			     int cpu __used,
			     u64 timestamp __used,
			     struct thread *thread __used)
{
	struct trace_contended_event contended_event;
	u64 tmp;		/* this is required for casting... */

	tmp = raw_field_value(event, "lockdep_addr", data);
	memcpy(&contended_event.addr, &tmp, sizeof(void *));
	contended_event.name = (char *)raw_field_ptr(event, "name", data);

	if (trace_handler->acquire_event)
		trace_handler->contended_event(&contended_event, event, cpu, timestamp, thread);
}

static void
process_lock_release_event(void *data,
			   struct event *event __used,
			   int cpu __used,
			   u64 timestamp __used,
			   struct thread *thread __used)
{
	struct trace_release_event release_event;
	u64 tmp;		/* this is required for casting... */

	tmp = raw_field_value(event, "lockdep_addr", data);
	memcpy(&release_event.addr, &tmp, sizeof(void *));
	release_event.name = (char *)raw_field_ptr(event, "name", data);

	if (trace_handler->acquire_event)
		trace_handler->release_event(&release_event, event, cpu, timestamp, thread);
}

static void
process_raw_event(void *data, int cpu,
		  u64 timestamp, struct thread *thread)
{
	struct event *event;
	int type;

	type = trace_parse_common_type(data);
	event = trace_find_event(type);

	if (!strcmp(event->name, "lock_acquire"))
		process_lock_acquire_event(data, event, cpu, timestamp, thread);
	if (!strcmp(event->name, "lock_acquired"))
		process_lock_acquired_event(data, event, cpu, timestamp, thread);
	if (!strcmp(event->name, "lock_contended"))
		process_lock_contended_event(data, event, cpu, timestamp, thread);
	if (!strcmp(event->name, "lock_release"))
		process_lock_release_event(data, event, cpu, timestamp, thread);
}

struct raw_event_queue {
	u64			timestamp;
	int			cpu;
	void			*data;
	struct thread		*thread;
	struct list_head	list;
};

static LIST_HEAD(raw_event_head);

#define FLUSH_PERIOD	(5 * NSEC_PER_SEC)

static u64 flush_limit = ULLONG_MAX;
static u64 last_flush = 0;
struct raw_event_queue *last_inserted;

static void flush_raw_event_queue(u64 limit)
{
	struct raw_event_queue *tmp, *iter;

	list_for_each_entry_safe(iter, tmp, &raw_event_head, list) {
		if (iter->timestamp > limit)
			return;

		if (iter == last_inserted)
			last_inserted = NULL;

		process_raw_event(iter->data, iter->cpu, iter->timestamp,
				  iter->thread);

		last_flush = iter->timestamp;
		list_del(&iter->list);
		free(iter->data);
		free(iter);
	}
}

static void __queue_raw_event_end(struct raw_event_queue *new)
{
	struct raw_event_queue *iter;

	list_for_each_entry_reverse(iter, &raw_event_head, list) {
		if (iter->timestamp < new->timestamp) {
			list_add(&new->list, &iter->list);
			return;
		}
	}

	list_add(&new->list, &raw_event_head);
}

static void __queue_raw_event_before(struct raw_event_queue *new,
				     struct raw_event_queue *iter)
{
	list_for_each_entry_continue_reverse(iter, &raw_event_head, list) {
		if (iter->timestamp < new->timestamp) {
			list_add(&new->list, &iter->list);
			return;
		}
	}

	list_add(&new->list, &raw_event_head);
}

static void __queue_raw_event_after(struct raw_event_queue *new,
				     struct raw_event_queue *iter)
{
	list_for_each_entry_continue(iter, &raw_event_head, list) {
		if (iter->timestamp > new->timestamp) {
			list_add_tail(&new->list, &iter->list);
			return;
		}
	}
	list_add_tail(&new->list, &raw_event_head);
}

/* The queue is ordered by time */
static void __queue_raw_event(struct raw_event_queue *new)
{
	if (!last_inserted) {
		__queue_raw_event_end(new);
		return;
	}

	/*
	 * Most of the time the current event has a timestamp
	 * very close to the last event inserted, unless we just switched
	 * to another event buffer. Having a sorting based on a list and
	 * on the last inserted event that is close to the current one is
	 * probably more efficient than an rbtree based sorting.
	 */
	if (last_inserted->timestamp >= new->timestamp)
		__queue_raw_event_before(new, last_inserted);
	else
		__queue_raw_event_after(new, last_inserted);
}

static void queue_raw_event(void *data, int raw_size, int cpu,
			    u64 timestamp, struct thread *thread)
{
	struct raw_event_queue *new;

	if (flush_limit == ULLONG_MAX)
		flush_limit = timestamp + FLUSH_PERIOD;

	if (timestamp < last_flush) {
		printf("Warning: Timestamp below last timeslice flush\n");
		return;
	}

	new = malloc(sizeof(*new));
	if (!new)
		die("Not enough memory\n");

	new->timestamp = timestamp;
	new->cpu = cpu;
	new->thread = thread;

	new->data = malloc(raw_size);
	if (!new->data)
		die("Not enough memory\n");

	memcpy(new->data, data, raw_size);

	__queue_raw_event(new);
	last_inserted = new;

	/*
	 * We want to have a slice of events covering 2 * FLUSH_PERIOD
	 * If FLUSH_PERIOD is big enough, it ensures every events that occured
	 * in the first half of the timeslice have all been buffered and there
	 * are none remaining (we need that because of the weakly ordered
	 * event recording we have). Then once we reach the 2 * FLUSH_PERIOD
	 * timeslice, we flush the first half to be gentle with the memory
	 * (the second half can still get new events in the middle, so wait
	 * another period to flush it)
	 */
	if (new->timestamp > flush_limit &&
		new->timestamp - flush_limit > FLUSH_PERIOD) {
		flush_limit += FLUSH_PERIOD;
		flush_raw_event_queue(flush_limit);
	}
}

static int process_sample_event(event_t *event, struct perf_session *session)
{
	struct thread *thread;
	struct sample_data data;

	bzero(&data, sizeof(struct sample_data));
	event__parse_sample(event, session->sample_type, &data);
	thread = perf_session__findnew(session, data.pid);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			 event->header.type);
		return -1;
	}

	dump_printf(" ... thread: %s:%d\n", thread->comm, thread->pid);

	if (profile_cpu != -1 && profile_cpu != (int) data.cpu)
		return 0;

	queue_raw_event(data.raw_data, data.raw_size, data.cpu, data.time, thread);

	return 0;
}

/* TODO: various way to print, coloring, nano or milli sec */
static void print_result(void)
{
	struct lock_stat *st;
	char cut_name[20];

	printf("%18s ", "ID");
	printf("%20s ", "Name");
	printf("%10s ", "acquired");
	printf("%10s ", "contended");

	printf("%15s ", "total wait (ns)");
	printf("%15s ", "max wait (ns)");
	printf("%15s ", "min wait (ns)");

	printf("\n\n");

	while ((st = pop_from_result())) {
		bzero(cut_name, 20);

		printf("%p ", st->addr);

		if (strlen(st->name) < 16) {
			/* output raw name */
			printf("%20s ", st->name);
		} else {
			strncpy(cut_name, st->name, 16);
			cut_name[16] = '.';
			cut_name[17] = '.';
			cut_name[18] = '.';
			cut_name[19] = '\0';
			/* cut off name for saving output style */
			printf("%20s ", cut_name);
		}

		printf("%10u ", st->nr_acquired);
		printf("%10u ", st->nr_contended);

		printf("%15llu ", st->wait_time_total);
		printf("%15llu ", st->wait_time_max);
		printf("%15llu ", st->wait_time_min == ULLONG_MAX ?
		       0 : st->wait_time_min);
		printf("\n");
	}
}

static void dump_map(void)
{
	unsigned int i;
	struct lock_stat *st;

	for (i = 0; i < LOCKHASH_SIZE; i++) {
		list_for_each_entry(st, &lockhash_table[i], hash_entry) {
			printf("%p: %s\n", st->addr, st->name);
		}
	}
}

static struct perf_event_ops eops = {
	.sample			= process_sample_event,
	.comm			= event__process_comm,
};

static struct perf_session *session;

static int read_events(void)
{
	session = perf_session__new(input_name, O_RDONLY, 0);
	if (!session)
		die("Initializing perf session failed\n");

	return perf_session__process_events(session, &eops);
}

static void sort_result(void)
{
	unsigned int i;
	struct lock_stat *st;

	for (i = 0; i < LOCKHASH_SIZE; i++) {
		list_for_each_entry(st, &lockhash_table[i], hash_entry) {
			insert_to_result(st, compare);
		}
	}
}

static void __cmd_report(void)
{
	setup_pager();
	select_key();
	read_events();
	flush_raw_event_queue(ULLONG_MAX);
	sort_result();
	print_result();
}

static const char * const report_usage[] = {
	"perf lock report [<options>]",
	NULL
};

static const struct option report_options[] = {
	OPT_STRING('k', "key", &sort_key, "acquired",
		    "key for sorting"),
	/* TODO: type */
	OPT_END()
};

static const char * const lock_usage[] = {
	"perf lock [<options>] {record|trace|report}",
	NULL
};

static const struct option lock_options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_BOOLEAN('v', "verbose", &verbose, "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace, "dump raw trace in ASCII"),
	OPT_END()
};

static const char *record_args[] = {
	"record",
	"-a",
	"-R",
	"-f",
	"-m", "1024",
	"-c", "1",
	"-e", "lock:lock_acquire:r",
	"-e", "lock:lock_acquired:r",
	"-e", "lock:lock_contended:r",
	"-e", "lock:lock_release:r",
};

static int __cmd_record(int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;

	rec_argc = ARRAY_SIZE(record_args) + argc - 1;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_record(i, rec_argv, NULL);
}

int cmd_lock(int argc, const char **argv, const char *prefix __used)
{
	unsigned int i;

	symbol__init();
	for (i = 0; i < LOCKHASH_SIZE; i++)
		INIT_LIST_HEAD(lockhash_table + i);

	argc = parse_options(argc, argv, lock_options, lock_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(lock_usage, lock_options);

	if (!strncmp(argv[0], "rec", 3)) {
		return __cmd_record(argc, argv);
	} else if (!strncmp(argv[0], "report", 6)) {
		trace_handler = &report_lock_ops;
		if (argc) {
			argc = parse_options(argc, argv,
					     report_options, report_usage, 0);
			if (argc)
				usage_with_options(report_usage, report_options);
		}
		__cmd_report();
	} else if (!strcmp(argv[0], "trace")) {
		/* Aliased to 'perf trace' */
		return cmd_trace(argc, argv, prefix);
	} else if (!strcmp(argv[0], "map")) {
		/* recycling report_lock_ops */
		trace_handler = &report_lock_ops;
		setup_pager();
		read_events();
		dump_map();
	} else {
		usage_with_options(lock_usage, lock_options);
	}

	return 0;
}
