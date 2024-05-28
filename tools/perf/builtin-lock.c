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
#include "util/cgroup.h"
#include "util/callchain.h"
#include "util/lock-contention.h"
#include "util/bpf_skel/lock_data.h"

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
#include "util/util.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/prctl.h>
#include <semaphore.h>
#include <math.h>
#include <limits.h>
#include <ctype.h>

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

static struct hlist_head *lockhash_table;

#define __lockhashfn(key)	hash_long((unsigned long)key, LOCKHASH_BITS)
#define lockhashentry(key)	(lockhash_table + __lockhashfn((key)))

static struct rb_root		thread_stats;

static bool combine_locks;
static bool show_thread_stats;
static bool show_lock_addrs;
static bool show_lock_owner;
static bool show_lock_cgroups;
static bool use_bpf;
static unsigned long bpf_map_entries = MAX_ENTRIES;
static int max_stack_depth = CONTENTION_STACK_DEPTH;
static int stack_skip = CONTENTION_STACK_SKIP;
static int print_nr_entries = INT_MAX / 2;
static LIST_HEAD(callstack_filters);
static const char *output_name = NULL;
static FILE *lock_output;

struct callstack_filter {
	struct list_head list;
	char name[];
};

static struct lock_filter filters;

static enum lock_aggr_mode aggr_mode = LOCK_AGGR_ADDR;

static bool needs_callstack(void)
{
	return !list_empty(&callstack_filters);
}

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

	/* for CSV output */
	if (len == 0) {
		fprintf(lock_output, "%llu", nsec);
		return;
	}

	for (int i = 0; table[i].unit; i++) {
		if (nsec < table[i].base)
			continue;

		fprintf(lock_output, "%*.2f %s", len - 3, nsec / table[i].base, table[i].unit);
		return;
	}

	fprintf(lock_output, "%*llu %s", len - 3, nsec, "ns");
}

#define PRINT_KEY(member)						\
static void lock_stat_key_print_ ## member(struct lock_key *key,	\
					   struct lock_stat *ls)	\
{									\
	fprintf(lock_output, "%*llu", key->len, (unsigned long long)ls->member);\
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

struct lock_stat *lock_stat_find(u64 addr)
{
	struct hlist_head *entry = lockhashentry(addr);
	struct lock_stat *ret;

	hlist_for_each_entry(ret, entry, hash_entry) {
		if (ret->addr == addr)
			return ret;
	}
	return NULL;
}

struct lock_stat *lock_stat_findnew(u64 addr, const char *name, int flags)
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

bool match_callstack_filter(struct machine *machine, u64 *callstack)
{
	struct map *kmap;
	struct symbol *sym;
	u64 ip;
	const char *arch = perf_env__arch(machine->env);

	if (list_empty(&callstack_filters))
		return true;

	for (int i = 0; i < max_stack_depth; i++) {
		struct callstack_filter *filter;

		/*
		 * In powerpc, the callchain saved by kernel always includes
		 * first three entries as the NIP (next instruction pointer),
		 * LR (link register), and the contents of LR save area in the
		 * second stack frame. In certain scenarios its possible to have
		 * invalid kernel instruction addresses in either LR or the second
		 * stack frame's LR. In that case, kernel will store that address as
		 * zero.
		 *
		 * The below check will continue to look into callstack,
		 * incase first or second callstack index entry has 0
		 * address for powerpc.
		 */
		if (!callstack || (!callstack[i] && (strcmp(arch, "powerpc") ||
						(i != 1 && i != 2))))
			break;

		ip = callstack[i];
		sym = machine__find_kernel_symbol(machine, ip, &kmap);
		if (sym == NULL)
			continue;

		list_for_each_entry(filter, &callstack_filters, list) {
			if (strstr(sym->name, filter->name))
				return true;
		}
	}
	return false;
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

static int get_key_by_aggr_mode_simple(u64 *key, u64 addr, u32 tid)
{
	switch (aggr_mode) {
	case LOCK_AGGR_ADDR:
		*key = addr;
		break;
	case LOCK_AGGR_TASK:
		*key = tid;
		break;
	case LOCK_AGGR_CALLER:
	case LOCK_AGGR_CGROUP:
	default:
		pr_err("Invalid aggregation mode: %d\n", aggr_mode);
		return -EINVAL;
	}
	return 0;
}

static u64 callchain_id(struct evsel *evsel, struct perf_sample *sample);

static int get_key_by_aggr_mode(u64 *key, u64 addr, struct evsel *evsel,
				 struct perf_sample *sample)
{
	if (aggr_mode == LOCK_AGGR_CALLER) {
		*key = callchain_id(evsel, sample);
		return 0;
	}
	return get_key_by_aggr_mode_simple(key, addr, sample->tid);
}

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
	int ret;

	ret = get_key_by_aggr_mode_simple(&key, addr, sample->tid);
	if (ret < 0)
		return ret;

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
	int ret;

	ret = get_key_by_aggr_mode_simple(&key, addr, sample->tid);
	if (ret < 0)
		return ret;

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
	int ret;

	ret = get_key_by_aggr_mode_simple(&key, addr, sample->tid);
	if (ret < 0)
		return ret;

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
	int ret;

	ret = get_key_by_aggr_mode_simple(&key, addr, sample->tid);
	if (ret < 0)
		return ret;

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

static int get_symbol_name_offset(struct map *map, struct symbol *sym, u64 ip,
				  char *buf, int size)
{
	u64 offset;

	if (map == NULL || sym == NULL) {
		buf[0] = '\0';
		return 0;
	}

	offset = map__map_ip(map, ip) - sym->start;

	if (offset)
		return scnprintf(buf, size, "%s+%#lx", sym->name, offset);
	else
		return strlcpy(buf, sym->name, size);
}
static int lock_contention_caller(struct evsel *evsel, struct perf_sample *sample,
				  char *buf, int size)
{
	struct thread *thread;
	struct callchain_cursor *cursor;
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

	cursor = get_tls_callchain_cursor();

	/* use caller function name from the callchain */
	ret = thread__resolve_callchain(thread, cursor, evsel, sample,
					NULL, NULL, max_stack_depth);
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
		if (++skip <= stack_skip)
			goto next;

		sym = node->ms.sym;
		if (sym && !machine__is_lock_function(machine, node->ip)) {
			get_symbol_name_offset(node->ms.map, sym, node->ip,
					       buf, size);
			return 0;
		}

next:
		callchain_cursor_advance(cursor);
	}
	return -1;
}

static u64 callchain_id(struct evsel *evsel, struct perf_sample *sample)
{
	struct callchain_cursor *cursor;
	struct machine *machine = &session->machines.host;
	struct thread *thread;
	u64 hash = 0;
	int skip = 0;
	int ret;

	thread = machine__findnew_thread(machine, -1, sample->pid);
	if (thread == NULL)
		return -1;

	cursor = get_tls_callchain_cursor();
	/* use caller function name from the callchain */
	ret = thread__resolve_callchain(thread, cursor, evsel, sample,
					NULL, NULL, max_stack_depth);
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
		if (++skip <= stack_skip)
			goto next;

		if (node->ms.sym && machine__is_lock_function(machine, node->ip))
			goto next;

		hash ^= hash_long((unsigned long)node->ip, 64);

next:
		callchain_cursor_advance(cursor);
	}
	return hash;
}

static u64 *get_callstack(struct perf_sample *sample, int max_stack)
{
	u64 *callstack;
	u64 i;
	int c;

	callstack = calloc(max_stack, sizeof(*callstack));
	if (callstack == NULL)
		return NULL;

	for (i = 0, c = 0; i < sample->callchain->nr && c < max_stack; i++) {
		u64 ip = sample->callchain->ips[i];

		if (ip >= PERF_CONTEXT_MAX)
			continue;

		callstack[c++] = ip;
	}
	return callstack;
}

static int report_lock_contention_begin_event(struct evsel *evsel,
					      struct perf_sample *sample)
{
	struct lock_stat *ls;
	struct thread_stat *ts;
	struct lock_seq_stat *seq;
	u64 addr = evsel__intval(evsel, sample, "lock_addr");
	unsigned int flags = evsel__intval(evsel, sample, "flags");
	u64 key;
	int i, ret;
	static bool kmap_loaded;
	struct machine *machine = &session->machines.host;
	struct map *kmap;
	struct symbol *sym;

	ret = get_key_by_aggr_mode(&key, addr, evsel, sample);
	if (ret < 0)
		return ret;

	if (!kmap_loaded) {
		unsigned long *addrs;

		/* make sure it loads the kernel map to find lock symbols */
		map__load(machine__kernel_map(machine));
		kmap_loaded = true;

		/* convert (kernel) symbols to addresses */
		for (i = 0; i < filters.nr_syms; i++) {
			sym = machine__find_kernel_symbol_by_name(machine,
								  filters.syms[i],
								  &kmap);
			if (sym == NULL) {
				pr_warning("ignore unknown symbol: %s\n",
					   filters.syms[i]);
				continue;
			}

			addrs = realloc(filters.addrs,
					(filters.nr_addrs + 1) * sizeof(*addrs));
			if (addrs == NULL) {
				pr_warning("memory allocation failure\n");
				return -ENOMEM;
			}

			addrs[filters.nr_addrs++] = map__unmap_ip(kmap, sym->start);
			filters.addrs = addrs;
		}
	}

	ls = lock_stat_find(key);
	if (!ls) {
		char buf[128];
		const char *name = "";

		switch (aggr_mode) {
		case LOCK_AGGR_ADDR:
			sym = machine__find_kernel_symbol(machine, key, &kmap);
			if (sym)
				name = sym->name;
			break;
		case LOCK_AGGR_CALLER:
			name = buf;
			if (lock_contention_caller(evsel, sample, buf, sizeof(buf)) < 0)
				name = "Unknown";
			break;
		case LOCK_AGGR_CGROUP:
		case LOCK_AGGR_TASK:
		default:
			break;
		}

		ls = lock_stat_findnew(key, name, flags);
		if (!ls)
			return -ENOMEM;
	}

	if (filters.nr_types) {
		bool found = false;

		for (i = 0; i < filters.nr_types; i++) {
			if (flags == filters.types[i]) {
				found = true;
				break;
			}
		}

		if (!found)
			return 0;
	}

	if (filters.nr_addrs) {
		bool found = false;

		for (i = 0; i < filters.nr_addrs; i++) {
			if (addr == filters.addrs[i]) {
				found = true;
				break;
			}
		}

		if (!found)
			return 0;
	}

	if (needs_callstack()) {
		u64 *callstack = get_callstack(sample, max_stack_depth);
		if (callstack == NULL)
			return -ENOMEM;

		if (!match_callstack_filter(machine, callstack)) {
			free(callstack);
			return 0;
		}

		if (ls->callstack == NULL)
			ls->callstack = callstack;
		else
			free(callstack);
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
	int ret;

	ret = get_key_by_aggr_mode(&key, addr, evsel, sample);
	if (ret < 0)
		return ret;

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

	if (quiet || total == 0 || (broken == 0 && verbose <= 0))
		return;

	fprintf(lock_output, "\n=== output for debug ===\n\n");
	fprintf(lock_output, "bad: %d, total: %d\n", bad, total);
	fprintf(lock_output, "bad rate: %.2f %%\n", (double)bad / (double)total * 100);
	fprintf(lock_output, "histogram of events caused bad sequence\n");
	for (i = 0; i < BROKEN_MAX; i++)
		fprintf(lock_output, " %10s: %d\n", name[i], bad_hist[i]);
}

/* TODO: various way to print, coloring, nano or milli sec */
static void print_result(void)
{
	struct lock_stat *st;
	struct lock_key *key;
	char cut_name[20];
	int bad, total, printed;

	if (!quiet) {
		fprintf(lock_output, "%20s ", "Name");
		list_for_each_entry(key, &lock_keys, list)
			fprintf(lock_output, "%*s ", key->len, key->header);
		fprintf(lock_output, "\n\n");
	}

	bad = total = printed = 0;
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

			fprintf(lock_output, "%20s ", name);
		} else {
			strncpy(cut_name, st->name, 16);
			cut_name[16] = '.';
			cut_name[17] = '.';
			cut_name[18] = '.';
			cut_name[19] = '\0';
			/* cut off name for saving output style */
			fprintf(lock_output, "%20s ", cut_name);
		}

		list_for_each_entry(key, &lock_keys, list) {
			key->print(key, st);
			fprintf(lock_output, " ");
		}
		fprintf(lock_output, "\n");

		if (++printed >= print_nr_entries)
			break;
	}

	print_bad_events(bad, total);
}

static bool info_threads, info_map;

static void dump_threads(void)
{
	struct thread_stat *st;
	struct rb_node *node;
	struct thread *t;

	fprintf(lock_output, "%10s: comm\n", "Thread ID");

	node = rb_first(&thread_stats);
	while (node) {
		st = container_of(node, struct thread_stat, rb);
		t = perf_session__findnew(session, st->tid);
		fprintf(lock_output, "%10d: %s\n", st->tid, thread__comm_str(t));
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

	fprintf(lock_output, "Address of instance: name of class\n");
	for (i = 0; i < LOCKHASH_SIZE; i++) {
		hlist_for_each_entry(st, &lockhash_table[i], hash_entry) {
			insert_to_result(st, compare_maps);
		}
	}

	while ((st = pop_from_result()))
		fprintf(lock_output, " %#llx: %s\n", (unsigned long long)st->addr, st->name);
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

static int process_event_update(struct perf_tool *tool,
				union perf_event *event,
				struct evlist **pevlist)
{
	int ret;

	ret = perf_event__process_event_update(tool, event, pevlist);
	if (ret < 0)
		return ret;

	/* this can return -EEXIST since we call it for each evsel */
	perf_session__set_tracepoints_handlers(session, lock_tracepoints);
	perf_session__set_tracepoints_handlers(session, contention_tracepoints);
	return 0;
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

static const struct {
	unsigned int flags;
	const char *str;
	const char *name;
} lock_type_table[] = {
	{ 0,				"semaphore",	"semaphore" },
	{ LCB_F_SPIN,			"spinlock",	"spinlock" },
	{ LCB_F_SPIN | LCB_F_READ,	"rwlock:R",	"rwlock" },
	{ LCB_F_SPIN | LCB_F_WRITE,	"rwlock:W",	"rwlock" },
	{ LCB_F_READ,			"rwsem:R",	"rwsem" },
	{ LCB_F_WRITE,			"rwsem:W",	"rwsem" },
	{ LCB_F_RT,			"rt-mutex",	"rt-mutex" },
	{ LCB_F_RT | LCB_F_READ,	"rwlock-rt:R",	"rwlock-rt" },
	{ LCB_F_RT | LCB_F_WRITE,	"rwlock-rt:W",	"rwlock-rt" },
	{ LCB_F_PERCPU | LCB_F_READ,	"pcpu-sem:R",	"percpu-rwsem" },
	{ LCB_F_PERCPU | LCB_F_WRITE,	"pcpu-sem:W",	"percpu-rwsem" },
	{ LCB_F_MUTEX,			"mutex",	"mutex" },
	{ LCB_F_MUTEX | LCB_F_SPIN,	"mutex",	"mutex" },
	/* alias for get_type_flag() */
	{ LCB_F_MUTEX | LCB_F_SPIN,	"mutex-spin",	"mutex" },
};

static const char *get_type_str(unsigned int flags)
{
	flags &= LCB_F_MAX_FLAGS - 1;

	for (unsigned int i = 0; i < ARRAY_SIZE(lock_type_table); i++) {
		if (lock_type_table[i].flags == flags)
			return lock_type_table[i].str;
	}
	return "unknown";
}

static const char *get_type_name(unsigned int flags)
{
	flags &= LCB_F_MAX_FLAGS - 1;

	for (unsigned int i = 0; i < ARRAY_SIZE(lock_type_table); i++) {
		if (lock_type_table[i].flags == flags)
			return lock_type_table[i].name;
	}
	return "unknown";
}

static unsigned int get_type_flag(const char *str)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(lock_type_table); i++) {
		if (!strcmp(lock_type_table[i].name, str))
			return lock_type_table[i].flags;
	}
	for (unsigned int i = 0; i < ARRAY_SIZE(lock_type_table); i++) {
		if (!strcmp(lock_type_table[i].str, str))
			return lock_type_table[i].flags;
	}
	return UINT_MAX;
}

static void lock_filter_finish(void)
{
	zfree(&filters.types);
	filters.nr_types = 0;

	zfree(&filters.addrs);
	filters.nr_addrs = 0;

	for (int i = 0; i < filters.nr_syms; i++)
		free(filters.syms[i]);

	zfree(&filters.syms);
	filters.nr_syms = 0;

	zfree(&filters.cgrps);
	filters.nr_cgrps = 0;
}

static void sort_contention_result(void)
{
	sort_result();
}

static void print_header_stdio(void)
{
	struct lock_key *key;

	list_for_each_entry(key, &lock_keys, list)
		fprintf(lock_output, "%*s ", key->len, key->header);

	switch (aggr_mode) {
	case LOCK_AGGR_TASK:
		fprintf(lock_output, "  %10s   %s\n\n", "pid",
			show_lock_owner ? "owner" : "comm");
		break;
	case LOCK_AGGR_CALLER:
		fprintf(lock_output, "  %10s   %s\n\n", "type", "caller");
		break;
	case LOCK_AGGR_ADDR:
		fprintf(lock_output, "  %16s   %s\n\n", "address", "symbol");
		break;
	case LOCK_AGGR_CGROUP:
		fprintf(lock_output, "  %s\n\n", "cgroup");
		break;
	default:
		break;
	}
}

static void print_header_csv(const char *sep)
{
	struct lock_key *key;

	fprintf(lock_output, "# output: ");
	list_for_each_entry(key, &lock_keys, list)
		fprintf(lock_output, "%s%s ", key->header, sep);

	switch (aggr_mode) {
	case LOCK_AGGR_TASK:
		fprintf(lock_output, "%s%s %s\n", "pid", sep,
			show_lock_owner ? "owner" : "comm");
		break;
	case LOCK_AGGR_CALLER:
		fprintf(lock_output, "%s%s %s", "type", sep, "caller");
		if (verbose > 0)
			fprintf(lock_output, "%s %s", sep, "stacktrace");
		fprintf(lock_output, "\n");
		break;
	case LOCK_AGGR_ADDR:
		fprintf(lock_output, "%s%s %s%s %s\n", "address", sep, "symbol", sep, "type");
		break;
	case LOCK_AGGR_CGROUP:
		fprintf(lock_output, "%s\n", "cgroup");
		break;
	default:
		break;
	}
}

static void print_header(void)
{
	if (!quiet) {
		if (symbol_conf.field_sep)
			print_header_csv(symbol_conf.field_sep);
		else
			print_header_stdio();
	}
}

static void print_lock_stat_stdio(struct lock_contention *con, struct lock_stat *st)
{
	struct lock_key *key;
	struct thread *t;
	int pid;

	list_for_each_entry(key, &lock_keys, list) {
		key->print(key, st);
		fprintf(lock_output, " ");
	}

	switch (aggr_mode) {
	case LOCK_AGGR_CALLER:
		fprintf(lock_output, "  %10s   %s\n", get_type_str(st->flags), st->name);
		break;
	case LOCK_AGGR_TASK:
		pid = st->addr;
		t = perf_session__findnew(session, pid);
		fprintf(lock_output, "  %10d   %s\n",
			pid, pid == -1 ? "Unknown" : thread__comm_str(t));
		break;
	case LOCK_AGGR_ADDR:
		fprintf(lock_output, "  %016llx   %s (%s)\n", (unsigned long long)st->addr,
			st->name, get_type_name(st->flags));
		break;
	case LOCK_AGGR_CGROUP:
		fprintf(lock_output, "  %s\n", st->name);
		break;
	default:
		break;
	}

	if (aggr_mode == LOCK_AGGR_CALLER && verbose > 0) {
		struct map *kmap;
		struct symbol *sym;
		char buf[128];
		u64 ip;

		for (int i = 0; i < max_stack_depth; i++) {
			if (!st->callstack || !st->callstack[i])
				break;

			ip = st->callstack[i];
			sym = machine__find_kernel_symbol(con->machine, ip, &kmap);
			get_symbol_name_offset(kmap, sym, ip, buf, sizeof(buf));
			fprintf(lock_output, "\t\t\t%#lx  %s\n", (unsigned long)ip, buf);
		}
	}
}

static void print_lock_stat_csv(struct lock_contention *con, struct lock_stat *st,
				const char *sep)
{
	struct lock_key *key;
	struct thread *t;
	int pid;

	list_for_each_entry(key, &lock_keys, list) {
		key->print(key, st);
		fprintf(lock_output, "%s ", sep);
	}

	switch (aggr_mode) {
	case LOCK_AGGR_CALLER:
		fprintf(lock_output, "%s%s %s", get_type_str(st->flags), sep, st->name);
		if (verbose <= 0)
			fprintf(lock_output, "\n");
		break;
	case LOCK_AGGR_TASK:
		pid = st->addr;
		t = perf_session__findnew(session, pid);
		fprintf(lock_output, "%d%s %s\n", pid, sep,
			pid == -1 ? "Unknown" : thread__comm_str(t));
		break;
	case LOCK_AGGR_ADDR:
		fprintf(lock_output, "%llx%s %s%s %s\n", (unsigned long long)st->addr, sep,
			st->name, sep, get_type_name(st->flags));
		break;
	case LOCK_AGGR_CGROUP:
		fprintf(lock_output, "%s\n",st->name);
		break;
	default:
		break;
	}

	if (aggr_mode == LOCK_AGGR_CALLER && verbose > 0) {
		struct map *kmap;
		struct symbol *sym;
		char buf[128];
		u64 ip;

		for (int i = 0; i < max_stack_depth; i++) {
			if (!st->callstack || !st->callstack[i])
				break;

			ip = st->callstack[i];
			sym = machine__find_kernel_symbol(con->machine, ip, &kmap);
			get_symbol_name_offset(kmap, sym, ip, buf, sizeof(buf));
			fprintf(lock_output, "%s %#lx %s", i ? ":" : sep, (unsigned long) ip, buf);
		}
		fprintf(lock_output, "\n");
	}
}

static void print_lock_stat(struct lock_contention *con, struct lock_stat *st)
{
	if (symbol_conf.field_sep)
		print_lock_stat_csv(con, st, symbol_conf.field_sep);
	else
		print_lock_stat_stdio(con, st);
}

static void print_footer_stdio(int total, int bad, struct lock_contention_fails *fails)
{
	/* Output for debug, this have to be removed */
	int broken = fails->task + fails->stack + fails->time + fails->data;

	if (!use_bpf)
		print_bad_events(bad, total);

	if (quiet || total == 0 || (broken == 0 && verbose <= 0))
		return;

	total += broken;
	fprintf(lock_output, "\n=== output for debug ===\n\n");
	fprintf(lock_output, "bad: %d, total: %d\n", broken, total);
	fprintf(lock_output, "bad rate: %.2f %%\n", 100.0 * broken / total);

	fprintf(lock_output, "histogram of failure reasons\n");
	fprintf(lock_output, " %10s: %d\n", "task", fails->task);
	fprintf(lock_output, " %10s: %d\n", "stack", fails->stack);
	fprintf(lock_output, " %10s: %d\n", "time", fails->time);
	fprintf(lock_output, " %10s: %d\n", "data", fails->data);
}

static void print_footer_csv(int total, int bad, struct lock_contention_fails *fails,
			     const char *sep)
{
	/* Output for debug, this have to be removed */
	if (use_bpf)
		bad = fails->task + fails->stack + fails->time + fails->data;

	if (quiet || total == 0 || (bad == 0 && verbose <= 0))
		return;

	total += bad;
	fprintf(lock_output, "# debug: total=%d%s bad=%d", total, sep, bad);

	if (use_bpf) {
		fprintf(lock_output, "%s bad_%s=%d", sep, "task", fails->task);
		fprintf(lock_output, "%s bad_%s=%d", sep, "stack", fails->stack);
		fprintf(lock_output, "%s bad_%s=%d", sep, "time", fails->time);
		fprintf(lock_output, "%s bad_%s=%d", sep, "data", fails->data);
	} else {
		int i;
		const char *name[4] = { "acquire", "acquired", "contended", "release" };

		for (i = 0; i < BROKEN_MAX; i++)
			fprintf(lock_output, "%s bad_%s=%d", sep, name[i], bad_hist[i]);
	}
	fprintf(lock_output, "\n");
}

static void print_footer(int total, int bad, struct lock_contention_fails *fails)
{
	if (symbol_conf.field_sep)
		print_footer_csv(total, bad, fails, symbol_conf.field_sep);
	else
		print_footer_stdio(total, bad, fails);
}

static void print_contention_result(struct lock_contention *con)
{
	struct lock_stat *st;
	int bad, total, printed;

	if (!quiet)
		print_header();

	bad = total = printed = 0;

	while ((st = pop_from_result())) {
		total += use_bpf ? st->nr_contended : 1;
		if (st->broken)
			bad++;

		if (!st->wait_time_total)
			continue;

		print_lock_stat(con, st);

		if (++printed >= print_nr_entries)
			break;
	}

	if (print_nr_entries) {
		/* update the total/bad stats */
		while ((st = pop_from_result())) {
			total += use_bpf ? st->nr_contended : 1;
			if (st->broken)
				bad++;
		}
	}
	/* some entries are collected but hidden by the callstack filter */
	total += con->nr_filtered;

	print_footer(total, bad, &con->fails);
}

static bool force;

static int __cmd_report(bool display_info)
{
	int err = -EINVAL;
	struct perf_tool eops = {
		.attr		 = perf_event__process_attr,
		.event_update	 = process_event_update,
		.sample		 = process_sample_event,
		.comm		 = perf_event__process_comm,
		.mmap		 = perf_event__process_mmap,
		.namespaces	 = perf_event__process_namespaces,
		.tracing_data	 = perf_event__process_tracing_data,
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

	symbol_conf.allow_aliases = true;
	symbol__init(&session->header.env);

	if (!data.is_pipe) {
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

static int check_lock_contention_options(const struct option *options,
					 const char * const *usage)

{
	if (show_thread_stats && show_lock_addrs) {
		pr_err("Cannot use thread and addr mode together\n");
		parse_options_usage(usage, options, "threads", 0);
		parse_options_usage(NULL, options, "lock-addr", 0);
		return -1;
	}

	if (show_lock_owner && !use_bpf) {
		pr_err("Lock owners are available only with BPF\n");
		parse_options_usage(usage, options, "lock-owner", 0);
		parse_options_usage(NULL, options, "use-bpf", 0);
		return -1;
	}

	if (show_lock_owner && show_lock_addrs) {
		pr_err("Cannot use owner and addr mode together\n");
		parse_options_usage(usage, options, "lock-owner", 0);
		parse_options_usage(NULL, options, "lock-addr", 0);
		return -1;
	}

	if (show_lock_cgroups && !use_bpf) {
		pr_err("Cgroups are available only with BPF\n");
		parse_options_usage(usage, options, "lock-cgroup", 0);
		parse_options_usage(NULL, options, "use-bpf", 0);
		return -1;
	}

	if (show_lock_cgroups && show_lock_addrs) {
		pr_err("Cannot use cgroup and addr mode together\n");
		parse_options_usage(usage, options, "lock-cgroup", 0);
		parse_options_usage(NULL, options, "lock-addr", 0);
		return -1;
	}

	if (show_lock_cgroups && show_thread_stats) {
		pr_err("Cannot use cgroup and thread mode together\n");
		parse_options_usage(usage, options, "lock-cgroup", 0);
		parse_options_usage(NULL, options, "threads", 0);
		return -1;
	}

	if (symbol_conf.field_sep) {
		if (strstr(symbol_conf.field_sep, ":") || /* part of type flags */
		    strstr(symbol_conf.field_sep, "+") || /* part of caller offset */
		    strstr(symbol_conf.field_sep, ".")) { /* can be in a symbol name */
			pr_err("Cannot use the separator that is already used\n");
			parse_options_usage(usage, options, "x", 1);
			return -1;
		}
	}

	if (show_lock_owner)
		show_thread_stats = true;

	return 0;
}

static int __cmd_contention(int argc, const char **argv)
{
	int err = -EINVAL;
	struct perf_tool eops = {
		.attr		 = perf_event__process_attr,
		.event_update	 = process_event_update,
		.sample		 = process_sample_event,
		.comm		 = perf_event__process_comm,
		.mmap		 = perf_event__process_mmap,
		.tracing_data	 = perf_event__process_tracing_data,
		.ordered_events	 = true,
	};
	struct perf_data data = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = force,
	};
	struct lock_contention con = {
		.target = &target,
		.map_nr_entries = bpf_map_entries,
		.max_stack = max_stack_depth,
		.stack_skip = stack_skip,
		.filters = &filters,
		.save_callstack = needs_callstack(),
		.owner = show_lock_owner,
		.cgroups = RB_ROOT,
	};

	lockhash_table = calloc(LOCKHASH_SIZE, sizeof(*lockhash_table));
	if (!lockhash_table)
		return -ENOMEM;

	con.result = &lockhash_table[0];

	session = perf_session__new(use_bpf ? NULL : &data, &eops);
	if (IS_ERR(session)) {
		pr_err("Initializing perf session failed\n");
		err = PTR_ERR(session);
		session = NULL;
		goto out_delete;
	}

	con.machine = &session->machines.host;

	con.aggr_mode = aggr_mode = show_thread_stats ? LOCK_AGGR_TASK :
		show_lock_addrs ? LOCK_AGGR_ADDR :
		show_lock_cgroups ? LOCK_AGGR_CGROUP : LOCK_AGGR_CALLER;

	if (con.aggr_mode == LOCK_AGGR_CALLER)
		con.save_callstack = true;

	symbol_conf.allow_aliases = true;
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
	} else if (!data.is_pipe) {
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

	if (symbol_conf.field_sep) {
		int i;
		struct lock_key *keys = contention_keys;

		/* do not align output in CSV format */
		for (i = 0; keys[i].name; i++)
			keys[i].len = 0;
	}

	if (use_bpf) {
		lock_contention_start();
		if (argc)
			evlist__start_workload(con.evlist);

		/* wait for signal */
		pause();

		lock_contention_stop();
		lock_contention_read(&con);
	} else {
		err = perf_session__process_events(session);
		if (err)
			goto out_delete;
	}

	setup_pager();

	sort_contention_result();
	print_contention_result(&con);

out_delete:
	lock_filter_finish();
	evlist__delete(con.evlist);
	lock_contention_finish(&con);
	perf_session__delete(session);
	zfree(&lockhash_table);
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
		rec_argv[i] = record_args[i];

	for (j = 0; j < nr_tracepoints; j++) {
		rec_argv[i++] = "-e";
		rec_argv[i++] = has_lock_stat
			? lock_tracepoints[j].name
			: contention_tracepoints[j].name;
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

static int parse_max_stack(const struct option *opt, const char *str,
			   int unset __maybe_unused)
{
	unsigned long *len = (unsigned long *)opt->value;
	long val;
	char *endptr;

	errno = 0;
	val = strtol(str, &endptr, 0);
	if (*endptr != '\0' || errno != 0) {
		pr_err("invalid max stack depth: %s\n", str);
		return -1;
	}

	if (val < 0 || val > sysctl__max_stack()) {
		pr_err("invalid max stack depth: %ld\n", val);
		return -1;
	}

	*len = val;
	return 0;
}

static bool add_lock_type(unsigned int flags)
{
	unsigned int *tmp;

	tmp = realloc(filters.types, (filters.nr_types + 1) * sizeof(*filters.types));
	if (tmp == NULL)
		return false;

	tmp[filters.nr_types++] = flags;
	filters.types = tmp;
	return true;
}

static int parse_lock_type(const struct option *opt __maybe_unused, const char *str,
			   int unset __maybe_unused)
{
	char *s, *tmp, *tok;
	int ret = 0;

	s = strdup(str);
	if (s == NULL)
		return -1;

	for (tok = strtok_r(s, ", ", &tmp); tok; tok = strtok_r(NULL, ", ", &tmp)) {
		unsigned int flags = get_type_flag(tok);

		if (flags == -1U) {
			pr_err("Unknown lock flags: %s\n", tok);
			ret = -1;
			break;
		}

		if (!add_lock_type(flags)) {
			ret = -1;
			break;
		}
	}

	free(s);
	return ret;
}

static bool add_lock_addr(unsigned long addr)
{
	unsigned long *tmp;

	tmp = realloc(filters.addrs, (filters.nr_addrs + 1) * sizeof(*filters.addrs));
	if (tmp == NULL) {
		pr_err("Memory allocation failure\n");
		return false;
	}

	tmp[filters.nr_addrs++] = addr;
	filters.addrs = tmp;
	return true;
}

static bool add_lock_sym(char *name)
{
	char **tmp;
	char *sym = strdup(name);

	if (sym == NULL) {
		pr_err("Memory allocation failure\n");
		return false;
	}

	tmp = realloc(filters.syms, (filters.nr_syms + 1) * sizeof(*filters.syms));
	if (tmp == NULL) {
		pr_err("Memory allocation failure\n");
		free(sym);
		return false;
	}

	tmp[filters.nr_syms++] = sym;
	filters.syms = tmp;
	return true;
}

static int parse_lock_addr(const struct option *opt __maybe_unused, const char *str,
			   int unset __maybe_unused)
{
	char *s, *tmp, *tok;
	int ret = 0;
	u64 addr;

	s = strdup(str);
	if (s == NULL)
		return -1;

	for (tok = strtok_r(s, ", ", &tmp); tok; tok = strtok_r(NULL, ", ", &tmp)) {
		char *end;

		addr = strtoul(tok, &end, 16);
		if (*end == '\0') {
			if (!add_lock_addr(addr)) {
				ret = -1;
				break;
			}
			continue;
		}

		/*
		 * At this moment, we don't have kernel symbols.  Save the symbols
		 * in a separate list and resolve them to addresses later.
		 */
		if (!add_lock_sym(tok)) {
			ret = -1;
			break;
		}
	}

	free(s);
	return ret;
}

static int parse_call_stack(const struct option *opt __maybe_unused, const char *str,
			   int unset __maybe_unused)
{
	char *s, *tmp, *tok;
	int ret = 0;

	s = strdup(str);
	if (s == NULL)
		return -1;

	for (tok = strtok_r(s, ", ", &tmp); tok; tok = strtok_r(NULL, ", ", &tmp)) {
		struct callstack_filter *entry;

		entry = malloc(sizeof(*entry) + strlen(tok) + 1);
		if (entry == NULL) {
			pr_err("Memory allocation failure\n");
			free(s);
			return -1;
		}

		strcpy(entry->name, tok);
		list_add_tail(&entry->list, &callstack_filters);
	}

	free(s);
	return ret;
}

static int parse_output(const struct option *opt __maybe_unused, const char *str,
			int unset __maybe_unused)
{
	const char **name = (const char **)opt->value;

	if (str == NULL)
		return -1;

	lock_output = fopen(str, "w");
	if (lock_output == NULL) {
		pr_err("Cannot open %s\n", str);
		return -1;
	}

	*name = str;
	return 0;
}

static bool add_lock_cgroup(char *name)
{
	u64 *tmp;
	struct cgroup *cgrp;

	cgrp = cgroup__new(name, /*do_open=*/false);
	if (cgrp == NULL) {
		pr_err("Failed to create cgroup: %s\n", name);
		return false;
	}

	if (read_cgroup_id(cgrp) < 0) {
		pr_err("Failed to read cgroup id for %s\n", name);
		cgroup__put(cgrp);
		return false;
	}

	tmp = realloc(filters.cgrps, (filters.nr_cgrps + 1) * sizeof(*filters.cgrps));
	if (tmp == NULL) {
		pr_err("Memory allocation failure\n");
		return false;
	}

	tmp[filters.nr_cgrps++] = cgrp->id;
	filters.cgrps = tmp;
	cgroup__put(cgrp);
	return true;
}

static int parse_cgroup_filter(const struct option *opt __maybe_unused, const char *str,
			       int unset __maybe_unused)
{
	char *s, *tmp, *tok;
	int ret = 0;

	s = strdup(str);
	if (s == NULL)
		return -1;

	for (tok = strtok_r(s, ", ", &tmp); tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (!add_lock_cgroup(tok)) {
			ret = -1;
			break;
		}
	}

	free(s);
	return ret;
}

int cmd_lock(int argc, const char **argv)
{
	const struct option lock_options[] = {
	OPT_STRING('i', "input", &input_name, "file", "input file name"),
	OPT_CALLBACK(0, "output", &output_name, "file", "output file name", parse_output),
	OPT_INCR('v', "verbose", &verbose, "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace, "dump raw trace in ASCII"),
	OPT_BOOLEAN('f', "force", &force, "don't complain, do it"),
	OPT_STRING(0, "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_BOOLEAN('q', "quiet", &quiet, "Do not show any warnings or messages"),
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
	OPT_INTEGER('E', "entries", &print_nr_entries, "display this many functions"),
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
	OPT_CALLBACK('M', "map-nr-entries", &bpf_map_entries, "num",
		     "Max number of BPF map entries", parse_map_entry),
	OPT_CALLBACK(0, "max-stack", &max_stack_depth, "num",
		     "Set the maximum stack depth when collecting lock contention, "
		     "Default: " __stringify(CONTENTION_STACK_DEPTH), parse_max_stack),
	OPT_INTEGER(0, "stack-skip", &stack_skip,
		    "Set the number of stack depth to skip when finding a lock caller, "
		    "Default: " __stringify(CONTENTION_STACK_SKIP)),
	OPT_INTEGER('E', "entries", &print_nr_entries, "display this many functions"),
	OPT_BOOLEAN('l', "lock-addr", &show_lock_addrs, "show lock stats by address"),
	OPT_CALLBACK('Y', "type-filter", NULL, "FLAGS",
		     "Filter specific type of locks", parse_lock_type),
	OPT_CALLBACK('L', "lock-filter", NULL, "ADDRS/NAMES",
		     "Filter specific address/symbol of locks", parse_lock_addr),
	OPT_CALLBACK('S', "callstack-filter", NULL, "NAMES",
		     "Filter specific function in the callstack", parse_call_stack),
	OPT_BOOLEAN('o', "lock-owner", &show_lock_owner, "show lock owners instead of waiters"),
	OPT_STRING_NOEMPTY('x', "field-separator", &symbol_conf.field_sep, "separator",
		   "print result in CSV format with custom separator"),
	OPT_BOOLEAN(0, "lock-cgroup", &show_lock_cgroups, "show lock stats by cgroup"),
	OPT_CALLBACK('G', "cgroup-filter", NULL, "CGROUPS",
		     "Filter specific cgroups", parse_cgroup_filter),
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

	lockhash_table = calloc(LOCKHASH_SIZE, sizeof(*lockhash_table));
	if (!lockhash_table)
		return -ENOMEM;

	for (i = 0; i < LOCKHASH_SIZE; i++)
		INIT_HLIST_HEAD(lockhash_table + i);

	lock_output = stderr;
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
		rc = cmd_script(argc, argv);
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

		if (check_lock_contention_options(contention_options,
						  contention_usage) < 0)
			return -1;

		rc = __cmd_contention(argc, argv);
	} else {
		usage_with_options(lock_usage, lock_options);
	}

	zfree(&lockhash_table);
	return rc;
}
