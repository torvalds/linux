// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include "builtin.h"
#include "perf.h"

#include "util/evlist.h" // for struct evsel_str_handler
#include "util/evsel.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/target.h"
#include "util/callchain.h"
#include "util/lock-contention.h"

#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include "util/trace-event.h"
#include "util/tracepoint.h"

#include "util/debug.h"
#include "util/session.h"
#include "util/tool.h"
#include "util/data.h"
#include "util/string2.h"
#include "util/map.h"

#include <sys/types.h>
#include <sys/prctl.h>
#include <semaphore.h>
#include <pthread.h>
#include <math.h>
#include <limits.h>

#include <linux/list.h>
#include <linux/hash.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include <linux/err.h>
#include <linux/stringify.h>

static struct perf_session *session;
static struct target target;

/* based on kernel/lockdep.c */
#define LOCKHASH_BITS		12
#define LOCKHASH_SIZE		(1UL << LOCKHASH_BITS)

static struct hlist_head lockhash_table[LOCKHASH_SIZE];

#define __lockhashfn(key)	hash_long((unsigned long)key, LOCKHASH_BITS)
#define lockhashentry(key)	(lockhash_table + __lockhashfn((key)))

static struct rb_root		thread_stats;

static bool combine_locks;
static bool show_thread_stats;
static bool use_bpf;
static unsigned long bpf_map_entries = 10240;

static enum {
	LOCK_AGGR_ADDR,
	LOCK_AGGR_TASK,
	LOCK_AGGR_CALLER,
} aggr_mode = LOCK_AGGR_ADDR;

static u64 sched_text_start;
static u64 sched_text_end;
static u64 lock_text_start;
static u64 lock_text_end;

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
SINGLE_KEY(avg_wait_time)
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
	/* header: the string printed on the header line */
	const char		*header;
	/* len: the printing width of the field */
	int			len;
	/* key: a pointer to function to compare two lock stats for sorting */
	int			(*key)(struct lock_stat*, struct lock_stat*);
	/* print: a pointer to function to print a given lock stats */
	void			(*print)(struct lock_key*, struct lock_stat*);
	/* list: list entry to link this */
	struct list_head	list;
};

static void lock_stat_key_print_time(unsigned long long nsec, int len)
{
	static const struct {
		float base;
		const char *unit;
	} table[] = {
		{ 1e9 * 3600, "h " },
		{ 1e9 * 60, "m " },
		{ 1e9, "s " },
		{ 1e6, "ms" },
		{ 1e3, "us" },
		{ 0, NULL },
	};

	for (int i = 0; table[i].unit; i++) {
		if (nsec < table[i].base)
			continue;

		pr_info("%*.2f %s", len - 3, nsec / table[i].base, table[i].unit);
		return;
	}

	pr_info("%*llu %s", len - 3, nsec, "ns");
}

#define PRINT_KEY(member)						\
static void lock_stat_key_print_ ## member(struct lock_key *key,	\
					   struct lock_stat *ls)	\
{									\
	pr_info("%*llu", key->len, (unsigned long long)ls->member);	\
}

#define PRINT_TIME(member)						\
static void lock_stat_key_print_ ## member(struct lock_key *key,	\
					   struct lock_stat *ls)	\
{									\
	lock_stat_key_print_time((unsigned long long)ls->member, key->len);	\
}

PRINT_KEY(nr_acquired)
PRINT_KEY(nr_contended)
PRINT_TIME(avg_wait_time)
PRINT_TIME(wait_time_total)
PRINT_TIME(wait_time_max)

static void lock_stat_key_print_wait_time_min(struct lock_key *key,
					      struct lock_stat *ls)
{
	u64 wait_time = ls->wait_time_min;

	if (wait_time == ULLONG_MAX)
		wait_time = 0;

	lock_stat_key_print_time(wait_time, key->len);
}


static const char		*sort_key = "acquired";

static int			(*compare)(struct lock_stat *, struct lock_stat *);

static struct rb_root		sorted; /* place to store intermediate data */
static struct rb_root		result;	/* place to store sorted data */

static LIST_HEAD(lock_keys);
static const char		*output_fields;

#define DEF_KEY_LOCK(name, header, fn_suffix, len)			\
	{ #name, header, len, lock_stat_key_ ## fn_suffix, lock_stat_key_print_ ## fn_suffix, {} }
static struct lock_key report_keys[] = {
	DEF_KEY_LOCK(acquired, "acquired", nr_acquired, 10),
	DEF_KEY_LOCK(contended, "contended", nr_contended, 10),
	DEF_KEY_LOCK(avg_wait, "avg wait", avg_wait_time, 12),
	DEF_KEY_LOCK(wait_total, "total wait", wait_time_total, 12),
	DEF_KEY_LOCK(wait_max, "max wait", wait_time_max, 12),
	DEF_KEY_LOCK(wait_min, "min wait", wait_time_min, 12),

	/* extra comparisons much complicated should be here */
	{ }
};

static struct lock_key contention_keys[] = {
	DEF_KEY_LOCK(contended, "contended", nr_contended, 10),
	DEF_KEY_LOCK(wait_total, "total wait", wait_time_total, 12),
	DEF_KEY_LOCK(wait_max, "max wait", wait_time_max, 12),
	DEF_KEY_LOCK(wait_min, "min wait", wait_time_min, 12),
	DEF_KEY_LOCK(avg_wait, "avg wait", avg_wait_time, 12),

	/* extra comparisons much complicated should be here */
	{ }
};

static int select_key(bool contention)
{
	int i;
	struct lock_key *keys = report_keys;

	if (contention)
		keys = contention_keys;

	for (i = 0; keys[i].name; i++) {
		if (!strcmp(keys[i].name, sort_key)) {
			compare = keys[i].key;

			/* selected key should be in the output fields */
			if (list_empty(&keys[i].list))
				list_add_tail(&keys[i].list, &lock_keys);

			return 0;
		}
	}

	pr_err("Unknown compare key: %s\n", sort_key);
	return -1;
}

static int add_output_field(bool contention, char *name)
{
	int i;
	struct lock_key *keys = report_keys;

	if (contention)
		keys = contention_keys;

	for (i = 0; keys[i].name; i++) {
		if (strcmp(keys[i].name, name))
			continue;

		/* prevent double link */
		if (list_empty(&keys[i].list))
			list_add_tail(&keys[i].list, &lock_keys);

		return 0;
	}

	pr_err("Unknown output field: %s\n", name);
	return -1;
}

static int setup_output_field(bool contention, const char *str)
{
	char *tok, *tmp, *orig;
	int i, ret = 0;
	struct lock_key *keys = report_keys;

	if (contention)
		keys = contention_keys;

	/* no output field given: use all of them */
	if (str == NULL) {
		for (i = 0; keys[i].name; i++)
			list_add_tail(&keys[i].list, &lock_keys);
		return 0;
	}

	for (i = 0; keys[i].name; i++)
		INIT_LIST_HEAD(&keys[i].list);

	orig = tmp = strdup(str);
	if (orig == NULL)
		return -ENOMEM;

	while ((tok = strsep(&tmp, ",")) != NULL){
		ret = add_output_field(contention, tok);
		if (ret < 0)
			break;
	}
	free(orig);

	return ret;
}

static void combine_lock_stats(struct lock_stat *st)
{
	struct rb_node **rb = &sorted.rb_node;
	struct rb_node *parent = NULL;
	struct lock_stat *p;
	int ret;

	while (*rb) {
		p = container_of(*rb, struct lock_stat, rb);
		parent = *rb;

		if (st->name && p->name)
			ret = strcmp(st->name, p->name);
		else
			ret = !!st->name - !!p->name;

		if (ret == 0) {
			p->nr_acquired += st->nr_acquired;
			p->nr_contended += st->nr_contended;
			p->wait_time_total += st->wait_time_total;

			if (p->nr_contended)
				p->avg_wait_time = p->wait_time_total / p->nr_contended;

			if (p->wait_time_min > st->wait_time_min)
				p->wait_time_min = st->wait_time_min;
			if (p->wait_time_max < st->wait_time_max)
				p->wait_time_max = st->wait_time_max;

			p->broken |= st->broken;
			st->combined = 1;
			return;
		}

		if (ret < 0)
			rb = &(*rb)->rb_left;
		else
			rb = &(*rb)->rb_right;
	}

	rb_link_node(&st->rb, parent, rb);
	rb_insert_color(&st->rb, &sorted);
}

static void insert_to_result(struct lock_stat *st,
			     int (*bigger)(struct lock_stat *, struct lock_stat *))
{
	struct rb_node **rb = &result.rb_node;
	struct rb_node *parent = NULL;
	struct lock_stat *p;

	if (combine_locks && st->combined)
		return;

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

static struct lock_stat *lock_stat_find(u64 addr)
{
	struct hlist_head *entry = lockhashentry(addr);
	struct lock_stat *ret;

	hlist_for_each_entry(ret, entry, hash_entry) {
		if (ret->addr == addr)
			return ret;
	}
	return NULL;
}

static struct lock_stat *lock_stat_findnew(u64 addr, const char *name, int flags)
{
	struct hlist_head *entry = lockhashentry(addr);
	struct lock_stat *ret, *new;

	hlist_for_each_entry(ret, entry, hash_entry) {
		if (ret->addr == addr)
			return ret;
	}

	new = zalloc(sizeof(struct lock_stat));
	if (!new)
		goto alloc_failed;

	new->addr = addr;
	new->name = strdup(name);
	if (!new->name) {
		free(new);
		goto alloc_failed;
	}

	new->flags = flags;
	new->wait_time_min = ULLONG_MAX;

	hlist_add_head(&new->hash_entry, entry);
	return new;

alloc_failed:
	pr_err("memory allocation failed\n");
	return NULL;
}

struct trace_lock_handler {
	/* it's used on CONFIG_LOCKDEP */
	int (*acquire_event)(struct evsel *evsel,
			     struct perf_sample *sample);

	/* it's used on CONFIG_LOCKDEP && CONFIG_LOCK_STAT */
	int (*acquired_event)(struct evsel *evsel,
			      struct perf_sample *sample);

	/* it's used on CONFIG_LOCKDEP && CONFIG_LOCK_STAT */
	int (*contended_event)(struct evsel *evsel,
			       struct perf_sample *sample);

	/* it's used on CONFIG_LOCKDEP */
	int (*release_event)(struct evsel *evsel,
			     struct perf_sample *sample);

	/* it's used when CONFIG_LOCKDEP is off */
	int (*contention_begin_event)(struct evsel *evsel,
				      struct perf_sample *sample);

	/* it's used when CONFIG_LOCKDEP is off */
	int (*contention_end_event)(struct evsel *evsel,
				    struct perf_sample *sample);
};

static struct lock_seq_stat *get_seq(struct thread_stat *ts, u64 addr)
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

static int report_lock_acquire_event(struct evsel *evsel,
				     struct perf_sample *sample)
{
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	const char *name = evsel__strval(evsel, sample, "name");
	u64 addr = evsel__intval(evsel, sample, "lockdep_addr");
	int flag = evsel__intval(evsel, sample, "flags");
	u64 key;

	switch (aggr_mode) {
	case LOCK_AGGR_ADDR:
		key = addr;
		break;
	case LOCK_AGGR_TASK:
		key = sample->tid;
		break;
	case LOCK_AGGR_CALLER:
	default:
		pr_err("Invalid aggregation mode: %d\n", aggr_mode);
		return -EINVAL;
	}

	ls = lock_stat_findnew(key, name, 0);
	if (!ls)
		return -ENOMEM;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -ENOMEM;

	seq = get_seq(ts, addr);
	if (!seq)
		return -ENOMEM;

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
		/* broken lock sequence */
		if (!ls->broken) {
			ls->broken = 1;
			bad_hist[BROKEN_ACQUIRE]++;
		}
		list_del_init(&seq->list);
		free(seq);
		goto end;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	ls->nr_acquire++;
	seq->prev_event_time = sample->time;
end:
	return 0;
}

static int report_lock_acquired_event(struct evsel *evsel,
				      struct perf_sample *sample)
{
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	u64 contended_term;
	const char *name = evsel__strval(evsel, sample, "name");
	u64 addr = evsel__intval(evsel, sample, "lockdep_addr");
	u64 key;

	switch (aggr_mode) {
	case LOCK_AGGR_ADDR:
		key = addr;
		break;
	case LOCK_AGGR_TASK:
		key = sample->tid;
		break;
	case LOCK_AGGR_CALLER:
	default:
		pr_err("Invalid aggregation mode: %d\n", aggr_mode);
		return -EINVAL;
	}

	ls = lock_stat_findnew(key, name, 0);
	if (!ls)
		return -ENOMEM;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -ENOMEM;

	seq = get_seq(ts, addr);
	if (!seq)
		return -ENOMEM;

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
		/* broken lock sequence */
		if (!ls->broken) {
			ls->broken = 1;
			bad_hist[BROKEN_ACQUIRED]++;
		}
		list_del_init(&seq->list);
		free(seq);
		goto end;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	seq->state = SEQ_STATE_ACQUIRED;
	ls->nr_acquired++;
	ls->avg_wait_time = ls->nr_contended ? ls->wait_time_total/ls->nr_contended : 0;
	seq->prev_event_time = sample->time;
end:
	return 0;
}

static int report_lock_contended_event(struct evsel *evsel,
				       struct perf_sample *sample)
{
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	const char *name = evsel__strval(evsel, sample, "name");
	u64 addr = evsel__intval(evsel, sample, "lockdep_addr");
	u64 key;

	switch (aggr_mode) {
	case LOCK_AGGR_ADDR:
		key = addr;
		break;
	case LOCK_AGGR_TASK:
		key = sample->tid;
		break;
	case LOCK_AGGR_CALLER:
	default:
		pr_err("Invalid aggregation mode: %d\n", aggr_mode);
		return -EINVAL;
	}

	ls = lock_stat_findnew(key, name, 0);
	if (!ls)
		return -ENOMEM;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -ENOMEM;

	seq = get_seq(ts, addr);
	if (!seq)
		return -ENOMEM;

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
		/* broken lock sequence */
		if (!ls->broken) {
			ls->broken = 1;
			bad_hist[BROKEN_CONTENDED]++;
		}
		list_del_init(&seq->list);
		free(seq);
		goto end;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	seq->state = SEQ_STATE_CONTENDED;
	ls->nr_contended++;
	ls->avg_wait_time = ls->wait_time_total/ls->nr_contended;
	seq->prev_event_time = sample->time;
end:
	return 0;
}

static int report_lock_release_event(struct evsel *evsel,
				     struct perf_sample *sample)
{
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	const char *name = evsel__strval(evsel, sample, "name");
	u64 addr = evsel__intval(evsel, sample, "lockdep_addr");
	u64 key;

	switch (aggr_mode) {
	case LOCK_AGGR_ADDR:
		key = addr;
		break;
	case LOCK_AGGR_TASK:
		key = sample->tid;
		break;
	case LOCK_AGGR_CALLER:
	default:
		pr_err("Invalid aggregation mode: %d\n", aggr_mode);
		return -EINVAL;
	}

	ls = lock_stat_findnew(key, name, 0);
	if (!ls)
		return -ENOMEM;

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -ENOMEM;

	seq = get_seq(ts, addr);
	if (!seq)
		return -ENOMEM;

	switch (seq->state) {
	case SEQ_STATE_UNINITIALIZED:
		goto end;
	case SEQ_STATE_ACQUIRED:
		break;
	case SEQ_STATE_READ_ACQUIRED:
		seq->read_count--;
		BUG_ON(seq->read_count < 0);
		if (seq->read_count) {
			ls->nr_release++;
			goto end;
		}
		break;
	case SEQ_STATE_ACQUIRING:
	case SEQ_STATE_CONTENDED:
	case SEQ_STATE_RELEASED:
		/* broken lock sequence */
		if (!ls->broken) {
			ls->broken = 1;
			bad_hist[BROKEN_RELEASE]++;
		}
		goto free_seq;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	ls->nr_release++;
free_seq:
	list_del_init(&seq->list);
	free(seq);
end:
	return 0;
}

bool is_lock_function(struct machine *machine, u64 addr)
{
	if (!sched_text_start) {
		struct map *kmap;
		struct symbol *sym;

		sym = machine__find_kernel_symbol_by_name(machine,
							  "__sched_text_start",
							  &kmap);
		if (!sym) {
			/* to avoid retry */
			sched_text_start = 1;
			return false;
		}

		sched_text_start = kmap->unmap_ip(kmap, sym->start);

		/* should not fail from here */
		sym = machine__find_kernel_symbol_by_name(machine,
							  "__sched_text_end",
							  &kmap);
		sched_text_end = kmap->unmap_ip(kmap, sym->start);

		sym = machine__find_kernel_symbol_by_name(machine,
							  "__lock_text_start",
							  &kmap);
		lock_text_start = kmap->unmap_ip(kmap, sym->start);

		sym = machine__find_kernel_symbol_by_name(machine,
							  "__lock_text_end",
							  &kmap);
		lock_text_end = kmap->unmap_ip(kmap, sym->start);
	}

	/* failed to get kernel symbols */
	if (sched_text_start == 1)
		return false;

	/* mutex and rwsem functions are in sched text section */
	if (sched_text_start <= addr && addr < sched_text_end)
		return true;

	/* spinlock functions are in lock text section */
	if (lock_text_start <= addr && addr < lock_text_end)
		return true;

	return false;
}

static int lock_contention_caller(struct evsel *evsel, struct perf_sample *sample,
				  char *buf, int size)
{
	struct thread *thread;
	struct callchain_cursor *cursor = &callchain_cursor;
	struct machine *machine = &session->machines.host;
	struct symbol *sym;
	int skip = 0;
	int ret;

	/* lock names will be replaced to task name later */
	if (show_thread_stats)
		return -1;

	thread = machine__findnew_thread(machine, -1, sample->pid);
	if (thread == NULL)
		return -1;

	/* use caller function name from the callchain */
	ret = thread__resolve_callchain(thread, cursor, evsel, sample,
					NULL, NULL, CONTENTION_STACK_DEPTH);
	if (ret != 0) {
		thread__put(thread);
		return -1;
	}

	callchain_cursor_commit(cursor);
	thread__put(thread);

	while (true) {
		struct callchain_cursor_node *node;

		node = callchain_cursor_current(cursor);
		if (node == NULL)
			break;

		/* skip first few entries - for lock functions */
		if (++skip <= CONTENTION_STACK_SKIP)
			goto next;

		sym = node->ms.sym;
		if (sym && !is_lock_function(machine, node->ip)) {
			struct map *map = node->ms.map;
			u64 offset;

			offset = map->map_ip(map, node->ip) - sym->start;

			if (offset)
				scnprintf(buf, size, "%s+%#lx", sym->name, offset);
			else
				strlcpy(buf, sym->name, size);
			return 0;
		}

next:
		callchain_cursor_advance(cursor);
	}
	return -1;
}

static u64 callchain_id(struct evsel *evsel, struct perf_sample *sample)
{
	struct callchain_cursor *cursor = &callchain_cursor;
	struct machine *machine = &session->machines.host;
	struct thread *thread;
	u64 hash = 0;
	int skip = 0;
	int ret;

	thread = machine__findnew_thread(machine, -1, sample->pid);
	if (thread == NULL)
		return -1;

	/* use caller function name from the callchain */
	ret = thread__resolve_callchain(thread, cursor, evsel, sample,
					NULL, NULL, CONTENTION_STACK_DEPTH);
	thread__put(thread);

	if (ret != 0)
		return -1;

	callchain_cursor_commit(cursor);

	while (true) {
		struct callchain_cursor_node *node;

		node = callchain_cursor_current(cursor);
		if (node == NULL)
			break;

		/* skip first few entries - for lock functions */
		if (++skip <= CONTENTION_STACK_SKIP)
			goto next;

		if (node->ms.sym && is_lock_function(machine, node->ip))
			goto next;

		hash ^= hash_long((unsigned long)node->ip, 64);

next:
		callchain_cursor_advance(cursor);
	}
	return hash;
}

static int report_lock_contention_begin_event(struct evsel *evsel,
					      struct perf_sample *sample)
{
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	u64 addr = evsel__intval(evsel, sample, "lock_addr");
	u64 key;

	switch (aggr_mode) {
	case LOCK_AGGR_ADDR:
		key = addr;
		break;
	case LOCK_AGGR_TASK:
		key = sample->tid;
		break;
	case LOCK_AGGR_CALLER:
		key = callchain_id(evsel, sample);
		break;
	default:
		pr_err("Invalid aggregation mode: %d\n", aggr_mode);
		return -EINVAL;
	}

	ls = lock_stat_find(key);
	if (!ls) {
		char buf[128];
		const char *caller = buf;
		unsigned int flags = evsel__intval(evsel, sample, "flags");

		if (lock_contention_caller(evsel, sample, buf, sizeof(buf)) < 0)
			caller = "Unknown";

		ls = lock_stat_findnew(key, caller, flags);
		if (!ls)
			return -ENOMEM;
	}

	ts = thread_stat_findnew(sample->tid);
	if (!ts)
		return -ENOMEM;

	seq = get_seq(ts, addr);
	if (!seq)
		return -ENOMEM;

	switch (seq->state) {
	case SEQ_STATE_UNINITIALIZED:
	case SEQ_STATE_ACQUIRED:
		break;
	case SEQ_STATE_CONTENDED:
		/*
		 * It can have nested contention begin with mutex spinning,
		 * then we would use the original contention begin event and
		 * ignore the second one.
		 */
		goto end;
	case SEQ_STATE_ACQUIRING:
	case SEQ_STATE_READ_ACQUIRED:
	case SEQ_STATE_RELEASED:
		/* broken lock sequence */
		if (!ls->broken) {
			ls->broken = 1;
			bad_hist[BROKEN_CONTENDED]++;
		}
		list_del_init(&seq->list);
		free(seq);
		goto end;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	if (seq->state != SEQ_STATE_CONTENDED) {
		seq->state = SEQ_STATE_CONTENDED;
		seq->prev_event_time = sample->time;
		ls->nr_contended++;
	}
end:
	return 0;
}

static int report_lock_contention_end_event(struct evsel *evsel,
					    struct perf_sample *sample)
{
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	u64 contended_term;
	u64 addr = evsel__intval(evsel, sample, "lock_addr");
	u64 key;

	switch (aggr_mode) {
	case LOCK_AGGR_ADDR:
		key = addr;
		break;
	case LOCK_AGGR_TASK:
		key = sample->tid;
		break;
	case LOCK_AGGR_CALLER:
		key = callchain_id(evsel, sample);
		break;
	default:
		pr_err("Invalid aggregation mode: %d\n", aggr_mode);
		return -EINVAL;
	}

	ls = lock_stat_find(key);
	if (!ls)
		return 0;

	ts = thread_stat_find(sample->tid);
	if (!ts)
		return 0;

	seq = get_seq(ts, addr);
	if (!seq)
		return -ENOMEM;

	switch (seq->state) {
	case SEQ_STATE_UNINITIALIZED:
		goto end;
	case SEQ_STATE_CONTENDED:
		contended_term = sample->time - seq->prev_event_time;
		ls->wait_time_total += contended_term;
		if (contended_term < ls->wait_time_min)
			ls->wait_time_min = contended_term;
		if (ls->wait_time_max < contended_term)
			ls->wait_time_max = contended_term;
		break;
	case SEQ_STATE_ACQUIRING:
	case SEQ_STATE_ACQUIRED:
	case SEQ_STATE_READ_ACQUIRED:
	case SEQ_STATE_RELEASED:
		/* broken lock sequence */
		if (!ls->broken) {
			ls->broken = 1;
			bad_hist[BROKEN_ACQUIRED]++;
		}
		list_del_init(&seq->list);
		free(seq);
		goto end;
	default:
		BUG_ON("Unknown state of lock sequence found!\n");
		break;
	}

	seq->state = SEQ_STATE_ACQUIRED;
	ls->nr_acquired++;
	ls->avg_wait_time = ls->wait_time_total/ls->nr_acquired;
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
	.contention_begin_event	= report_lock_contention_begin_event,
	.contention_end_event	= report_lock_contention_end_event,
};

static struct trace_lock_handler contention_lock_ops  = {
	.contention_begin_event	= report_lock_contention_begin_event,
	.contention_end_event	= report_lock_contention_end_event,
};


static struct trace_lock_handler *trace_handler;

static int evsel__process_lock_acquire(struct evsel *evsel, struct perf_sample *sample)
{
	if (trace_handler->acquire_event)
		return trace_handler->acquire_event(evsel, sample);
	return 0;
}

static int evsel__process_lock_acquired(struct evsel *evsel, struct perf_sample *sample)
{
	if (trace_handler->acquired_event)
		return trace_handler->acquired_event(evsel, sample);
	return 0;
}

static int evsel__process_lock_contended(struct evsel *evsel, struct perf_sample *sample)
{
	if (trace_handler->contended_event)
		return trace_handler->contended_event(evsel, sample);
	return 0;
}

static int evsel__process_lock_release(struct evsel *evsel, struct perf_sample *sample)
{
	if (trace_handler->release_event)
		return trace_handler->release_event(evsel, sample);
	return 0;
}

static int evsel__process_contention_begin(struct evsel *evsel, struct perf_sample *sample)
{
	if (trace_handler->contention_begin_event)
		return trace_handler->contention_begin_event(evsel, sample);
	return 0;
}

static int evsel__process_contention_end(struct evsel *evsel, struct perf_sample *sample)
{
	if (trace_handler->contention_end_event)
		return trace_handler->contention_end_event(evsel, sample);
	return 0;
}

static void print_bad_events(int bad, int total)
{
	/* Output for debug, this have to be removed */
	int i;
	int broken = 0;
	const char *name[4] =
		{ "acquire", "acquired", "contended", "release" };

	for (i = 0; i < BROKEN_MAX; i++)
		broken += bad_hist[i];

	if (broken == 0 && !verbose)
		return;

	pr_info("\n=== output for debug===\n\n");
	pr_info("bad: %d, total: %d\n", bad, total);
	pr_info("bad rate: %.2f %%\n", (double)bad / (double)total * 100);
	pr_info("histogram of events caused bad sequence\n");
	for (i = 0; i < BROKEN_MAX; i++)
		pr_info(" %10s: %d\n", name[i], bad_hist[i]);
}

/* TODO: various way to print, coloring, nano or milli sec */
static void print_result(void)
{
	struct lock_stat *st;
	struct lock_key *key;
	char cut_name[20];
	int bad, total;

	pr_info("%20s ", "Name");
	list_for_each_entry(key, &lock_keys, list)
		pr_info("%*s ", key->len, key->header);
	pr_info("\n\n");

	bad = total = 0;
	while ((st = pop_from_result())) {
		total++;
		if (st->broken)
			bad++;
		if (!st->nr_acquired)
			continue;

		bzero(cut_name, 20);

		if (strlen(st->name) < 20) {
			/* output raw name */
			const char *name = st->name;

			if (show_thread_stats) {
				struct thread *t;

				/* st->addr contains tid of thread */
				t = perf_session__findnew(session, st->addr);
				name = thread__comm_str(t);
			}

			pr_info("%20s ", name);
		} else {
			strncpy(cut_name, st->name, 16);
			cut_name[16] = '.';
			cut_name[17] = '.';
			cut_name[18] = '.';
			cut_name[19] = '\0';
			/* cut off name for saving output style */
			pr_info("%20s ", cut_name);
		}

		list_for_each_entry(key, &lock_keys, list) {
			key->print(key, st);
			pr_info(" ");
		}
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
		pr_info("%10d: %s\n", st->tid, thread__comm_str(t));
		node = rb_next(node);
		thread__put(t);
	}
}

static int compare_maps(struct lock_stat *a, struct lock_stat *b)
{
	int ret;

	if (a->name && b->name)
		ret = strcmp(a->name, b->name);
	else
		ret = !!a->name - !!b->name;

	if (!ret)
		return a->addr < b->addr;
	else
		return ret < 0;
}

static void dump_map(void)
{
	unsigned int i;
	struct lock_stat *st;

	pr_info("Address of instance: name of class\n");
	for (i = 0; i < LOCKHASH_SIZE; i++) {
		hlist_for_each_entry(st, &lockhash_table[i], hash_entry) {
			insert_to_result(st, compare_maps);
		}
	}

	while ((st = pop_from_result()))
		pr_info(" %#llx: %s\n", (unsigned long long)st->addr, st->name);
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

typedef int (*tracepoint_handler)(struct evsel *evsel,
				  struct perf_sample *sample);

static int process_sample_event(struct perf_tool *tool __maybe_unused,
				union perf_event *event,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine)
{
	int err = 0;
	struct thread *thread = machine__findnew_thread(machine, sample->pid,
							sample->tid);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (evsel->handler != NULL) {
		tracepoint_handler f = evsel->handler;
		err = f(evsel, sample);
	}

	thread__put(thread);

	return err;
}

static void combine_result(void)
{
	unsigned int i;
	struct lock_stat *st;

	if (!combine_locks)
		return;

	for (i = 0; i < LOCKHASH_SIZE; i++) {
		hlist_for_each_entry(st, &lockhash_table[i], hash_entry) {
			combine_lock_stats(st);
		}
	}
}

static void sort_result(void)
{
	unsigned int i;
	struct lock_stat *st;

	for (i = 0; i < LOCKHASH_SIZE; i++) {
		hlist_for_each_entry(st, &lockhash_table[i], hash_entry) {
			insert_to_result(st, compare);
		}
	}
}

static const char *get_type_str(struct lock_stat *st)
{
	static const struct {
		unsigned int flags;
		const char *name;
	} table[] = {
		{ 0,				"semaphore" },
		{ LCB_F_SPIN,			"spinlock" },
		{ LCB_F_SPIN | LCB_F_READ,	"rwlock:R" },
		{ LCB_F_SPIN | LCB_F_WRITE,	"rwlock:W"},
		{ LCB_F_READ,			"rwsem:R" },
		{ LCB_F_WRITE,			"rwsem:W" },
		{ LCB_F_RT,			"rtmutex" },
		{ LCB_F_RT | LCB_F_READ,	"rwlock-rt:R" },
		{ LCB_F_RT | LCB_F_WRITE,	"rwlock-rt:W"},
		{ LCB_F_PERCPU | LCB_F_READ,	"pcpu-sem:R" },
		{ LCB_F_PERCPU | LCB_F_WRITE,	"pcpu-sem:W" },
		{ LCB_F_MUTEX,			"mutex" },
		{ LCB_F_MUTEX | LCB_F_SPIN,	"mutex" },
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(table); i++) {
		if (table[i].flags == st->flags)
			return table[i].name;
	}
	return "unknown";
}

static void sort_contention_result(void)
{
	sort_result();
}

static void print_contention_result(void)
{
	struct lock_stat *st;
	struct lock_key *key;
	int bad, total;

	list_for_each_entry(key, &lock_keys, list)
		pr_info("%*s ", key->len, key->header);

	if (show_thread_stats)
		pr_info("  %10s   %s\n\n", "pid", "comm");
	else
		pr_info("  %10s   %s\n\n", "type", "caller");

	bad = total = 0;
	if (use_bpf)
		bad = bad_hist[BROKEN_CONTENDED];

	while ((st = pop_from_result())) {
		total += use_bpf ? st->nr_contended : 1;
		if (st->broken)
			bad++;

		list_for_each_entry(key, &lock_keys, list) {
			key->print(key, st);
			pr_info(" ");
		}

		if (show_thread_stats) {
			struct thread *t;
			int pid = st->addr;

			/* st->addr contains tid of thread */
			t = perf_session__findnew(session, pid);
			pr_info("  %10d   %s\n", pid, thread__comm_str(t));
			continue;
		}

		pr_info("  %10s   %s\n", get_type_str(st), st->name);
	}

	print_bad_events(bad, total);
}

static const struct evsel_str_handler lock_tracepoints[] = {
	{ "lock:lock_acquire",	 evsel__process_lock_acquire,   }, /* CONFIG_LOCKDEP */
	{ "lock:lock_acquired",	 evsel__process_lock_acquired,  }, /* CONFIG_LOCKDEP, CONFIG_LOCK_STAT */
	{ "lock:lock_contended", evsel__process_lock_contended, }, /* CONFIG_LOCKDEP, CONFIG_LOCK_STAT */
	{ "lock:lock_release",	 evsel__process_lock_release,   }, /* CONFIG_LOCKDEP */
};

static const struct evsel_str_handler contention_tracepoints[] = {
	{ "lock:contention_begin", evsel__process_contention_begin, },
	{ "lock:contention_end",   evsel__process_contention_end,   },
};

static bool force;

static int __cmd_report(bool display_info)
{
	int err = -EINVAL;
	struct perf_tool eops = {
		.sample		 = process_sample_event,
		.comm		 = perf_event__process_comm,
		.mmap		 = perf_event__process_mmap,
		.namespaces	 = perf_event__process_namespaces,
		.ordered_events	 = true,
	};
	struct perf_data data = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = force,
	};

	session = perf_session__new(&data, &eops);
	if (IS_ERR(session)) {
		pr_err("Initializing perf session failed\n");
		return PTR_ERR(session);
	}

	/* for lock function check */
	symbol_conf.sort_by_name = true;
	symbol__init(&session->header.env);

	if (!perf_session__has_traces(session, "lock record"))
		goto out_delete;

	if (perf_session__set_tracepoints_handlers(session, lock_tracepoints)) {
		pr_err("Initializing perf session tracepoint handlers failed\n");
		goto out_delete;
	}

	if (perf_session__set_tracepoints_handlers(session, contention_tracepoints)) {
		pr_err("Initializing perf session tracepoint handlers failed\n");
		goto out_delete;
	}

	if (setup_output_field(false, output_fields))
		goto out_delete;

	if (select_key(false))
		goto out_delete;

	if (show_thread_stats)
		aggr_mode = LOCK_AGGR_TASK;

	err = perf_session__process_events(session);
	if (err)
		goto out_delete;

	setup_pager();
	if (display_info) /* used for info subcommand */
		err = dump_info();
	else {
		combine_result();
		sort_result();
		print_result();
	}

out_delete:
	perf_session__delete(session);
	return err;
}

static void sighandler(int sig __maybe_unused)
{
}

static int __cmd_contention(int argc, const char **argv)
{
	int err = -EINVAL;
	struct perf_tool eops = {
		.sample		 = process_sample_event,
		.comm		 = perf_event__process_comm,
		.mmap		 = perf_event__process_mmap,
		.ordered_events	 = true,
	};
	struct perf_data data = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = force,
	};
	struct lock_contention con = {
		.target = &target,
		.result = &lockhash_table[0],
		.map_nr_entries = bpf_map_entries,
	};

	session = perf_session__new(use_bpf ? NULL : &data, &eops);
	if (IS_ERR(session)) {
		pr_err("Initializing perf session failed\n");
		return PTR_ERR(session);
	}

	/* for lock function check */
	symbol_conf.sort_by_name = true;
	symbol__init(&session->header.env);

	if (use_bpf) {
		err = target__validate(&target);
		if (err) {
			char errbuf[512];

			target__strerror(&target, err, errbuf, 512);
			pr_err("%s\n", errbuf);
			goto out_delete;
		}

		signal(SIGINT, sighandler);
		signal(SIGCHLD, sighandler);
		signal(SIGTERM, sighandler);

		con.machine = &session->machines.host;

		con.evlist = evlist__new();
		if (con.evlist == NULL) {
			err = -ENOMEM;
			goto out_delete;
		}

		err = evlist__create_maps(con.evlist, &target);
		if (err < 0)
			goto out_delete;

		if (argc) {
			err = evlist__prepare_workload(con.evlist, &target,
						       argv, false, NULL);
			if (err < 0)
				goto out_delete;
		}

		if (lock_contention_prepare(&con) < 0) {
			pr_err("lock contention BPF setup failed\n");
			goto out_delete;
		}
	} else {
		if (!perf_session__has_traces(session, "lock record"))
			goto out_delete;

		if (!evlist__find_evsel_by_str(session->evlist,
					       "lock:contention_begin")) {
			pr_err("lock contention evsel not found\n");
			goto out_delete;
		}

		if (perf_session__set_tracepoints_handlers(session,
						contention_tracepoints)) {
			pr_err("Initializing perf session tracepoint handlers failed\n");
			goto out_delete;
		}
	}

	if (setup_output_field(true, output_fields))
		goto out_delete;

	if (select_key(true))
		goto out_delete;

	if (show_thread_stats)
		aggr_mode = LOCK_AGGR_TASK;
	else
		aggr_mode = LOCK_AGGR_CALLER;

	if (use_bpf) {
		lock_contention_start();
		if (argc)
			evlist__start_workload(con.evlist);

		/* wait for signal */
		pause();

		lock_contention_stop();
		lock_contention_read(&con);

		/* abuse bad hist stats for lost entries */
		bad_hist[BROKEN_CONTENDED] = con.lost;
	} else {
		err = perf_session__process_events(session);
		if (err)
			goto out_delete;
	}

	setup_pager();

	sort_contention_result();
	print_contention_result();

out_delete:
	evlist__delete(con.evlist);
	lock_contention_finish();
	perf_session__delete(session);
	return err;
}


static int __cmd_record(int argc, const char **argv)
{
	const char *record_args[] = {
		"record", "-R", "-m", "1024", "-c", "1", "--synth", "task",
	};
	const char *callgraph_args[] = {
		"--call-graph", "fp," __stringify(CONTENTION_STACK_DEPTH),
	};
	unsigned int rec_argc, i, j, ret;
	unsigned int nr_tracepoints;
	unsigned int nr_callgraph_args = 0;
	const char **rec_argv;
	bool has_lock_stat = true;

	for (i = 0; i < ARRAY_SIZE(lock_tracepoints); i++) {
		if (!is_valid_tracepoint(lock_tracepoints[i].name)) {
			pr_debug("tracepoint %s is not enabled. "
				 "Are CONFIG_LOCKDEP and CONFIG_LOCK_STAT enabled?\n",
				 lock_tracepoints[i].name);
			has_lock_stat = false;
			break;
		}
	}

	if (has_lock_stat)
		goto setup_args;

	for (i = 0; i < ARRAY_SIZE(contention_tracepoints); i++) {
		if (!is_valid_tracepoint(contention_tracepoints[i].name)) {
			pr_err("tracepoint %s is not enabled.\n",
			       contention_tracepoints[i].name);
			return 1;
		}
	}

	nr_callgraph_args = ARRAY_SIZE(callgraph_args);

setup_args:
	rec_argc = ARRAY_SIZE(record_args) + nr_callgraph_args + argc - 1;

	if (has_lock_stat)
		nr_tracepoints = ARRAY_SIZE(lock_tracepoints);
	else
		nr_tracepoints = ARRAY_SIZE(contention_tracepoints);

	/* factor of 2 is for -e in front of each tracepoint */
	rec_argc += 2 * nr_tracepoints;

	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (!rec_argv)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	for (j = 0; j < nr_tracepoints; j++) {
		const char *ev_name;

		if (has_lock_stat)
			ev_name = strdup(lock_tracepoints[j].name);
		else
			ev_name = strdup(contention_tracepoints[j].name);

		if (!ev_name)
			return -ENOMEM;

		rec_argv[i++] = "-e";
		rec_argv[i++] = ev_name;
	}

	for (j = 0; j < nr_callgraph_args; j++, i++)
		rec_argv[i] = callgraph_args[j];

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	ret = cmd_record(i, rec_argv);
	free(rec_argv);
	return ret;
}

static int parse_map_entry(const struct option *opt, const char *str,
			    int unset __maybe_unused)
{
	unsigned long *len = (unsigned long *)opt->value;
	unsigned long val;
	char *endptr;

	errno = 0;
	val = strtoul(str, &endptr, 0);
	if (*endptr != '\0' || errno != 0) {
		pr_err("invalid BPF map length: %s\n", str);
		return -1;
	}

	*len = val;
	return 0;
}

int cmd_lock(int argc, const char **argv)
{
	const struct option lock_options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_INCR('v', "verbose", &verbose, "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace, "dump raw trace in ASCII"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_STRING(0, "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_END()
	};

	const struct option info_options[] = {
	OPT_BOOLEAN('t', "threads", &info_threads,
		    "dump thread list in perf.data"),
	OPT_BOOLEAN('m', "map", &info_map,
		    "map of lock instances (address:name table)"),
	OPT_PARENT(lock_options)
	};

	const struct option report_options[] = {
	OPT_STRING('k', "key", &sort_key, "acquired",
		    "key for sorting (acquired / contended / avg_wait / wait_total / wait_max / wait_min)"),
	OPT_STRING('F', "field", &output_fields, NULL,
		    "output fields (acquired / contended / avg_wait / wait_total / wait_max / wait_min)"),
	/* TODO: type */
	OPT_BOOLEAN('c', "combine-locks", &combine_locks,
		    "combine locks in the same class"),
	OPT_BOOLEAN('t', "threads", &show_thread_stats,
		    "show per-thread lock stats"),
	OPT_PARENT(lock_options)
	};

	struct option contention_options[] = {
	OPT_STRING('k', "key", &sort_key, "wait_total",
		    "key for sorting (contended / wait_total / wait_max / wait_min / avg_wait)"),
	OPT_STRING('F', "field", &output_fields, "contended,wait_total,wait_max,avg_wait",
		    "output fields (contended / wait_total / wait_max / wait_min / avg_wait)"),
	OPT_BOOLEAN('t', "threads", &show_thread_stats,
		    "show per-thread lock stats"),
	OPT_BOOLEAN('b', "use-bpf", &use_bpf, "use BPF program to collect lock contention stats"),
	OPT_BOOLEAN('a', "all-cpus", &target.system_wide,
		    "System-wide collection from all CPUs"),
	OPT_STRING('C', "cpu", &target.cpu_list, "cpu",
		    "List of cpus to monitor"),
	OPT_STRING('p', "pid", &target.pid, "pid",
		   "Trace on existing process id"),
	OPT_STRING(0, "tid", &target.tid, "tid",
		   "Trace on existing thread id (exclusive to --pid)"),
	OPT_CALLBACK(0, "map-nr-entries", &bpf_map_entries, "num",
		     "Max number of BPF map entries", parse_map_entry),
	OPT_PARENT(lock_options)
	};

	const char * const info_usage[] = {
		"perf lock info [<options>]",
		NULL
	};
	const char *const lock_subcommands[] = { "record", "report", "script",
						 "info", "contention", NULL };
	const char *lock_usage[] = {
		NULL,
		NULL
	};
	const char * const report_usage[] = {
		"perf lock report [<options>]",
		NULL
	};
	const char * const contention_usage[] = {
		"perf lock contention [<options>]",
		NULL
	};
	unsigned int i;
	int rc = 0;

	for (i = 0; i < LOCKHASH_SIZE; i++)
		INIT_HLIST_HEAD(lockhash_table + i);

	argc = parse_options_subcommand(argc, argv, lock_options, lock_subcommands,
					lock_usage, PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(lock_usage, lock_options);

	if (strlen(argv[0]) > 2 && strstarts("record", argv[0])) {
		return __cmd_record(argc, argv);
	} else if (strlen(argv[0]) > 2 && strstarts("report", argv[0])) {
		trace_handler = &report_lock_ops;
		if (argc) {
			argc = parse_options(argc, argv,
					     report_options, report_usage, 0);
			if (argc)
				usage_with_options(report_usage, report_options);
		}
		rc = __cmd_report(false);
	} else if (!strcmp(argv[0], "script")) {
		/* Aliased to 'perf script' */
		return cmd_script(argc, argv);
	} else if (!strcmp(argv[0], "info")) {
		if (argc) {
			argc = parse_options(argc, argv,
					     info_options, info_usage, 0);
			if (argc)
				usage_with_options(info_usage, info_options);
		}
		/* recycling report_lock_ops */
		trace_handler = &report_lock_ops;
		rc = __cmd_report(true);
	} else if (strlen(argv[0]) > 2 && strstarts("contention", argv[0])) {
		trace_handler = &contention_lock_ops;
		sort_key = "wait_total";
		output_fields = "contended,wait_total,wait_max,avg_wait";

#ifndef HAVE_BPF_SKEL
		set_option_nobuild(contention_options, 'b', "use-bpf",
				   "no BUILD_BPF_SKEL=1", false);
#endif
		if (argc) {
			argc = parse_options(argc, argv, contention_options,
					     contention_usage, 0);
		}
		rc = __cmd_contention(argc, argv);
	} else {
		usage_with_options(lock_usage, lock_options);
	}

	return rc;
}
