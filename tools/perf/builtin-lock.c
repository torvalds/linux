#include "builtin.h"
#include "perf.h"

#include "util/evlist.h"
#include "util/evsel.h"
#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"

#include "util/parse-options.h"
#include "util/trace-event.h"

#include "util/debug.h"
#include "util/session.h"
#include "util/tool.h"

#include <sys/types.h>
#include <sys/prctl.h>
#include <semaphore.h>
#include <pthread.h>
#include <math.h>
#include <limits.h>

#include <linux/list.h>
#include <linux/hash.h>

static struct perf_session *session;

/* based on kernel/lockdep.c */
#define LOCKHASH_BITS		12
#define LOCKHASH_SIZE		(1UL << LOCKHASH_BITS)

static struct list_head lockhash_table[LOCKHASH_SIZE];

#define __lockhashfn(key)	hash_long((unsigned long)key, LOCKHASH_BITS)
#define lockhashentry(key)	(lockhash_table + __lockhashfn((key)))

struct lock_stat {
	struct list_head	hash_entry;
	struct rb_node		rb;		/* used for sorting */

	/*
	 * FIXME: perf_evsel__intval() returns u64,
	 * so address of lockdep_map should be dealed as 64bit.
	 * Is there more better solution?
	 */
	void			*addr;		/* address of lockdep_map, used as ID */
	char			*name;		/* for strcpy(), we cannot use const */

	unsigned int		nr_acquire;
	unsigned int		nr_acquired;
	unsigned int		nr_contended;
	unsigned int		nr_release;

	unsigned int		nr_readlock;
	unsigned int		nr_trylock;
	/* these times are in nano sec. */
	u64			wait_time_total;
	u64			wait_time_min;
	u64			wait_time_max;

	int			discard; /* flag of blacklist */
};

/*
 * States of lock_seq_stat
 *
 * UNINITIALIZED is required for detecting first event of acquire.
 * As the nature of lock events, there is no guarantee
 * that the first event for the locks are acquire,
 * it can be acquired, contended or release.
 */
#define SEQ_STATE_UNINITIALIZED      0	       /* initial state */
#define SEQ_STATE_RELEASED	1
#define SEQ_STATE_ACQUIRING	2
#define SEQ_STATE_ACQUIRED	3
#define SEQ_STATE_READ_ACQUIRED	4
#define SEQ_STATE_CONTENDED	5

/*
 * MAX_LOCK_DEPTH
 * Imported from include/linux/sched.h.
 * Should this be synchronized?
 */
#define MAX_LOCK_DEPTH 48

/*
 * struct lock_seq_stat:
 * Place to put on state of one lock sequence
 * 1) acquire -> acquired -> release
 * 2) acquire -> contended -> acquired -> release
 * 3) acquire (with read or try) -> release
 * 4) Are there other patterns?
 */
struct lock_seq_stat {
	struct list_head        list;
	int			state;
	u64			prev_event_time;
	void                    *addr;

	int                     read_count;
};

struct thread_stat {
	struct rb_node		rb;

	u32                     tid;
	struct list_head        seq_list;
};

static struct rb_root		thread_stats;

static struct thread_stat *thread_stat_find(u32 tid)
{
	struct rb_node *node;
	struct thread_stat *st;

	node = thread_stats.rb_node;
	while (node) {
		st = container_of(node, struct thread_stat, rb);
		if (st->tid == tid)
			return st;
		else if (tid < st->tid)
			node = node->rb_left;
		else
			node = node->rb_right;
	}

	return NULL;
}

static void thread_stat_insert(struct thread_stat *new)
{
	struct rb_node **rb = &thread_stats.rb_node;
	struct rb_node *parent = NULL;
	struct thread_stat *p;

	while (*rb) {
		p = container_of(*rb, struct thread_stat, rb);
		parent = *rb;

		if (new->tid < p->tid)
			rb = &(*rb)->rb_left;
		else if (new->tid > p->tid)
			rb = &(*rb)->rb_right;
		else
			BUG_ON("inserting invalid thread_stat\n");
	}

	rb_link_node(&new->rb, parent, rb);
	rb_insert_color(&new->rb, &thread_stats);
}

static struct thread_stat *thread_stat_findnew_after_first(u32 tid)
{
	struct thread_stat *st;

	st = thread_stat_find(tid);
	if (st)
		return st;

	st = zalloc(sizeof(struct thread_stat));
	if (!st) {
		pr_err("memory allocation failed\n");
		return NULL;
	}

	st->tid = tid;
	INIT_LIST_HEAD(&st->seq_list);

	thread_stat_insert(st);

	return st;
}

static struct thread_stat *thread_stat_findnew_first(u32 tid);
static struct thread_stat *(*thread_stat_findnew)(u32 tid) =
	thread_stat_findnew_first;

static struct thread_stat *thread_stat_findnew_first(u32 tid)
{
	struct thread_stat *st;

	st = zalloc(sizeof(struct thread_stat));
	if (!st) {
		pr_err("memory allocation failed\n");
		return NULL;
	}
	st->tid = tid;
	INIT_LIST_HEAD(&st->seq_list);

	rb_link_node(&st->rb, NULL, &thread_stats.rb_node);
	rb_insert_color(&st->rb, &thread_stats);

	thread_stat_findnew = thread_stat_findnew_after_first;
	return st;
}

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
SINGLE_KEY(wait_time_max)

static int lock_stat_key_wait_time_min(struct lock_stat *one,
					struct lock_stat *two)
{
	u64 s1 = one->wait_time_min;
	u64 s2 = two->wait_time_min;
	if (s1 == ULLONG_MAX)
		s1 = 0;
	if (s2 == ULLONG_MAX)
		s2 = 0;
	return s1 > s2;
}

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

static int select_key(void)
{
	int i;

	for (i = 0; keys[i].name; i++) {
		if (!strcmp(keys[i].name, sort_key)) {
			compare = keys[i].key;
			return 0;
		}
	}

	pr_err("Unknown compare key: %s\n", sort_key);

	return -1;
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

	new->wait_time_min = ULLONG_MAX;

	list_add(&new->hash_entry, entry);
	return new;

alloc_failed:
	pr_err("memory allocation failed\n");
	return NULL;
}

struct trace_lock_handler {
	int (*acquire_event)(struct perf_evsel *evsel,
			     struct perf_sample *sample);

	int (*acquired_event)(struct perf_evsel *evsel,
			      struct perf_sample *sample);

	int (*contended_event)(struct perf_evsel *evsel,
			       struct perf_sample *sample);

	int (*release_event)(struct perf_evsel *evsel,
			     struct perf_sample *sample);
};

static struct lock_seq_stat *get_seq(struct thread_stat *ts, void *addr)
{
	struct lock_seq_stat *seq;

	list_for_each_entry(seq, &ts->seq_list, list) {
		if (seq->addr == addr)
			return seq;
	}

	seq = zalloc(sizeof(struct lock_seq_stat));
	if (!seq) {
		pr_err("memory allocation failed\n");
		return NULL;
	}
	seq->state = SEQ_STATE_UNINITIALIZED;
	seq->addr = addr;

	list_add(&seq->list, &ts->seq_list);
	return seq;
}

enum broken_state {
	BROKEN_ACQUIRE,
	BROKEN_ACQUIRED,
	BROKEN_CONTENDED,
	BROKEN_RELEASE,
	BROKEN_MAX,
};

static int bad_hist[BROKEN_MAX];

enum acquire_flags {
	TRY_LOCK = 1,
	READ_LOCK = 2,
};

static int report_lock_acquire_event(struct perf_evsel *evsel,
				     struct perf_sample *sample)
{
	void *addr;
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	const char *name = perf_evsel__strval(evsel, sample, "name");
	u64 tmp = perf_evsel__intval(evsel, sample, "lockdep_addr");
	int flag = perf_evsel__intval(evsel, sample, "flag");

	memcpy(&addr, &tmp, sizeof(void *));

	ls = lock_stat_findnew(addr, name);
	if (!ls)
		return -1;
	if (ls->discard)
		return 0;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -1;

	seq = get_seq(ts, addr);
	if (!seq)
		return -1;

	switch (seq->state) {
	case SEQ_STATE_UNINITIALIZED:
	case SEQ_STATE_RELEASED:
		if (!flag) {
			seq->state = SEQ_STATE_ACQUIRING;
		} else {
			if (flag & TRY_LOCK)
				ls->nr_trylock++;
			if (flag & READ_LOCK)
				ls->nr_readlock++;
			seq->state = SEQ_STATE_READ_ACQUIRED;
			seq->read_count = 1;
			ls->nr_acquired++;
		}
		break;
	case SEQ_STATE_READ_ACQUIRED:
		if (flag & READ_LOCK) {
			seq->read_count++;
			ls->nr_acquired++;
			goto end;
		} else {
			goto broken;
		}
		break;
	case SEQ_STATE_ACQUIRED:
	case SEQ_STATE_ACQUIRING:
	case SEQ_STATE_CONTENDED:
broken:
		/* broken lock sequence, discard it */
		ls->discard = 1;
		bad_hist[BROKEN_ACQUIRE]++;
		list_del(&seq->list);
		free(seq);
		goto end;
		break;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	ls->nr_acquire++;
	seq->prev_event_time = sample->time;
end:
	return 0;
}

static int report_lock_acquired_event(struct perf_evsel *evsel,
				      struct perf_sample *sample)
{
	void *addr;
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	u64 contended_term;
	const char *name = perf_evsel__strval(evsel, sample, "name");
	u64 tmp = perf_evsel__intval(evsel, sample, "lockdep_addr");

	memcpy(&addr, &tmp, sizeof(void *));

	ls = lock_stat_findnew(addr, name);
	if (!ls)
		return -1;
	if (ls->discard)
		return 0;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -1;

	seq = get_seq(ts, addr);
	if (!seq)
		return -1;

	switch (seq->state) {
	case SEQ_STATE_UNINITIALIZED:
		/* orphan event, do nothing */
		return 0;
	case SEQ_STATE_ACQUIRING:
		break;
	case SEQ_STATE_CONTENDED:
		contended_term = sample->time - seq->prev_event_time;
		ls->wait_time_total += contended_term;
		if (contended_term < ls->wait_time_min)
			ls->wait_time_min = contended_term;
		if (ls->wait_time_max < contended_term)
			ls->wait_time_max = contended_term;
		break;
	case SEQ_STATE_RELEASED:
	case SEQ_STATE_ACQUIRED:
	case SEQ_STATE_READ_ACQUIRED:
		/* broken lock sequence, discard it */
		ls->discard = 1;
		bad_hist[BROKEN_ACQUIRED]++;
		list_del(&seq->list);
		free(seq);
		goto end;
		break;

	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	seq->state = SEQ_STATE_ACQUIRED;
	ls->nr_acquired++;
	seq->prev_event_time = sample->time;
end:
	return 0;
}

static int report_lock_contended_event(struct perf_evsel *evsel,
				       struct perf_sample *sample)
{
	void *addr;
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	const char *name = perf_evsel__strval(evsel, sample, "name");
	u64 tmp = perf_evsel__intval(evsel, sample, "lockdep_addr");

	memcpy(&addr, &tmp, sizeof(void *));

	ls = lock_stat_findnew(addr, name);
	if (!ls)
		return -1;
	if (ls->discard)
		return 0;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -1;

	seq = get_seq(ts, addr);
	if (!seq)
		return -1;

	switch (seq->state) {
	case SEQ_STATE_UNINITIALIZED:
		/* orphan event, do nothing */
		return 0;
	case SEQ_STATE_ACQUIRING:
		break;
	case SEQ_STATE_RELEASED:
	case SEQ_STATE_ACQUIRED:
	case SEQ_STATE_READ_ACQUIRED:
	case SEQ_STATE_CONTENDED:
		/* broken lock sequence, discard it */
		ls->discard = 1;
		bad_hist[BROKEN_CONTENDED]++;
		list_del(&seq->list);
		free(seq);
		goto end;
		break;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	seq->state = SEQ_STATE_CONTENDED;
	ls->nr_contended++;
	seq->prev_event_time = sample->time;
end:
	return 0;
}

static int report_lock_release_event(struct perf_evsel *evsel,
				     struct perf_sample *sample)
{
	void *addr;
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	const char *name = perf_evsel__strval(evsel, sample, "name");
	u64 tmp = perf_evsel__intval(evsel, sample, "lockdep_addr");

	memcpy(&addr, &tmp, sizeof(void *));

	ls = lock_stat_findnew(addr, name);
	if (!ls)
		return -1;
	if (ls->discard)
		return 0;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -1;

	seq = get_seq(ts, addr);
	if (!seq)
		return -1;

	switch (seq->state) {
	case SEQ_STATE_UNINITIALIZED:
		goto end;
		break;
	case SEQ_STATE_ACQUIRED:
		break;
	case SEQ_STATE_READ_ACQUIRED:
		seq->read_count--;
		BUG_ON(seq->read_count < 0);
		if (!seq->read_count) {
			ls->nr_release++;
			goto end;
		}
		break;
	case SEQ_STATE_ACQUIRING:
	case SEQ_STATE_CONTENDED:
	case SEQ_STATE_RELEASED:
		/* broken lock sequence, discard it */
		ls->discard = 1;
		bad_hist[BROKEN_RELEASE]++;
		goto free_seq;
		break;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	ls->nr_release++;
free_seq:
	list_del(&seq->list);
	free(seq);
end:
	return 0;
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

static int perf_evsel__process_lock_acquire(struct perf_evsel *evsel,
					     struct perf_sample *sample)
{
	if (trace_handler->acquire_event)
		return trace_handler->acquire_event(evsel, sample);
	return 0;
}

static int perf_evsel__process_lock_acquired(struct perf_evsel *evsel,
					      struct perf_sample *sample)
{
	if (trace_handler->acquired_event)
		return trace_handler->acquired_event(evsel, sample);
	return 0;
}

static int perf_evsel__process_lock_contended(struct perf_evsel *evsel,
					      struct perf_sample *sample)
{
	if (trace_handler->contended_event)
		return trace_handler->contended_event(evsel, sample);
	return 0;
}

static int perf_evsel__process_lock_release(struct perf_evsel *evsel,
					    struct perf_sample *sample)
{
	if (trace_handler->release_event)
		return trace_handler->release_event(evsel, sample);
	return 0;
}

static void print_bad_events(int bad, int total)
{
	/* Output for debug, this have to be removed */
	int i;
	const char *name[4] =
		{ "acquire", "acquired", "contended", "release" };

	pr_info("\n=== output for debug===\n\n");
	pr_info("bad: %d, total: %d\n", bad, total);
	pr_info("bad rate: %f %%\n", (double)bad / (double)total * 100);
	pr_info("histogram of events caused bad sequence\n");
	for (i = 0; i < BROKEN_MAX; i++)
		pr_info(" %10s: %d\n", name[i], bad_hist[i]);
}

/* TODO: various way to print, coloring, nano or milli sec */
static void print_result(void)
{
	struct lock_stat *st;
	char cut_name[20];
	int bad, total;

	pr_info("%20s ", "Name");
	pr_info("%10s ", "acquired");
	pr_info("%10s ", "contended");

	pr_info("%15s ", "total wait (ns)");
	pr_info("%15s ", "max wait (ns)");
	pr_info("%15s ", "min wait (ns)");

	pr_info("\n\n");

	bad = total = 0;
	while ((st = pop_from_result())) {
		total++;
		if (st->discard) {
			bad++;
			continue;
		}
		bzero(cut_name, 20);

		if (strlen(st->name) < 16) {
			/* output raw name */
			pr_info("%20s ", st->name);
		} else {
			strncpy(cut_name, st->name, 16);
			cut_name[16] = '.';
			cut_name[17] = '.';
			cut_name[18] = '.';
			cut_name[19] = '\0';
			/* cut off name for saving output style */
			pr_info("%20s ", cut_name);
		}

		pr_info("%10u ", st->nr_acquired);
		pr_info("%10u ", st->nr_contended);

		pr_info("%15" PRIu64 " ", st->wait_time_total);
		pr_info("%15" PRIu64 " ", st->wait_time_max);
		pr_info("%15" PRIu64 " ", st->wait_time_min == ULLONG_MAX ?
		       0 : st->wait_time_min);
		pr_info("\n");
	}

	print_bad_events(bad, total);
}

static bool info_threads, info_map;

static void dump_threads(void)
{
	struct thread_stat *st;
	struct rb_node *node;
	struct thread *t;

	pr_info("%10s: comm\n", "Thread ID");

	node = rb_first(&thread_stats);
	while (node) {
		st = container_of(node, struct thread_stat, rb);
		t = perf_session__findnew(session, st->tid);
		pr_info("%10d: %s\n", st->tid, t->comm);
		node = rb_next(node);
	};
}

static void dump_map(void)
{
	unsigned int i;
	struct lock_stat *st;

	pr_info("Address of instance: name of class\n");
	for (i = 0; i < LOCKHASH_SIZE; i++) {
		list_for_each_entry(st, &lockhash_table[i], hash_entry) {
			pr_info(" %p: %s\n", st->addr, st->name);
		}
	}
}

static int dump_info(void)
{
	int rc = 0;

	if (info_threads)
		dump_threads();
	else if (info_map)
		dump_map();
	else {
		rc = -1;
		pr_err("Unknown type of information\n");
	}

	return rc;
}

typedef int (*tracepoint_handler)(struct perf_evsel *evsel,
				  struct perf_sample *sample);

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine, sample->pid,
							sample->tid);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (evsel->handler.func != NULL) {
		tracepoint_handler f = evsel->handler.func;
		return f(evsel, sample);
	}

	return 0;
}

static const struct perf_evsel_str_handler lock_tracepoints[] = {
	{ "lock:lock_acquire",	 perf_evsel__process_lock_acquire,   }, /* CONFIG_LOCKDEP */
	{ "lock:lock_acquired",	 perf_evsel__process_lock_acquired,  }, /* CONFIG_LOCKDEP, CONFIG_LOCK_STAT */
	{ "lock:lock_contended", perf_evsel__process_lock_contended, }, /* CONFIG_LOCKDEP, CONFIG_LOCK_STAT */
	{ "lock:lock_release",	 perf_evsel__process_lock_release,   }, /* CONFIG_LOCKDEP */
};

static int read_events(void)
{
	struct perf_tool eops = {
		.sample		 = process_sample_event,
		.comm		 = perf_event__process_comm,
		.ordered_samples = true,
	};
	session = perf_session__new(input_name, O_RDONLY, 0, false, &eops);
	if (!session) {
		pr_err("Initializing perf session failed\n");
		return -1;
	}

	if (perf_session__set_tracepoints_handlers(session, lock_tracepoints)) {
		pr_err("Initializing perf session tracepoint handlers failed\n");
		return -1;
	}

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

static int __cmd_report(void)
{
	setup_pager();

	if ((select_key() != 0) ||
	    (read_events() != 0))
		return -1;

	sort_result();
	print_result();

	return 0;
}

static int __cmd_record(int argc, const char **argv)
{
	const char *record_args[] = {
		"record", "-R", "-m", "1024", "-c", "1",
	};
	unsigned int rec_argc, i, j;
	const char **rec_argv;

	for (i = 0; i < ARRAY_SIZE(lock_tracepoints); i++) {
		if (!is_valid_tracepoint(lock_tracepoints[i].name)) {
				pr_err("tracepoint %s is not enabled. "
				       "Are CONFIG_LOCKDEP and CONFIG_LOCK_STAT enabled?\n",
				       lock_tracepoints[i].name);
				return 1;
		}
	}

	rec_argc = ARRAY_SIZE(record_args) + argc - 1;
	/* factor of 2 is for -e in front of each tracepoint */
	rec_argc += 2 * ARRAY_SIZE(lock_tracepoints);

	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	for (j = 0; j < ARRAY_SIZE(lock_tracepoints); j++) {
		rec_argv[i++] = "-e";
		rec_argv[i++] = strdup(lock_tracepoints[j].name);
	}

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_record(i, rec_argv, NULL);
}

int cmd_lock(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const struct option info_options[] = {
	OPT_BOOLEAN('t', "threads", &info_threads,
		    "dump thread list in perf.data"),
	OPT_BOOLEAN('m', "map", &info_map,
		    "map of lock instances (address:name table)"),
	OPT_END()
	};
	const struct option lock_options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace, "dump raw trace in ASCII"),
	OPT_END()
	};
	const struct option report_options[] = {
	OPT_STRING('k', "key", &sort_key, "acquired",
		    "key for sorting (acquired / contended / wait_total / wait_max / wait_min)"),
	/* TODO: type */
	OPT_END()
	};
	const char * const info_usage[] = {
		"perf lock info [<options>]",
		NULL
	};
	const char * const lock_usage[] = {
		"perf lock [<options>] {record|report|script|info}",
		NULL
	};
	const char * const report_usage[] = {
		"perf lock report [<options>]",
		NULL
	};
	unsigned int i;
	int rc = 0;

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
	} else if (!strcmp(argv[0], "script")) {
		/* Aliased to 'perf script' */
		return cmd_script(argc, argv, prefix);
	} else if (!strcmp(argv[0], "info")) {
		if (argc) {
			argc = parse_options(argc, argv,
					     info_options, info_usage, 0);
			if (argc)
				usage_with_options(info_usage, info_options);
		}
		/* recycling report_lock_ops */
		trace_handler = &report_lock_ops;
		setup_pager();
		if (read_events() != 0)
			rc = -1;
		else
			rc = dump_info();
	} else {
		usage_with_options(lock_usage, lock_options);
	}

	return rc;
}
