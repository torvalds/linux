// SPDX-License-Identifier: GPL-2.0
/*
 * builtin-kwork.c
 *
 * Copyright (c) 2022  Huawei Inc,  Yang Jihong <yangjihong1@huawei.com>
 */

#include "builtin.h"

#include "util/data.h"
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/header.h"
#include "util/kwork.h"
#include "util/debug.h"
#include "util/session.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/string2.h"
#include "util/callchain.h"
#include "util/evsel_fprintf.h"
#include "util/util.h"

#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include <traceevent/event-parse.h>

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <linux/err.h>
#include <linux/time64.h>
#include <linux/zalloc.h>

/*
 * report header elements width
 */
#define PRINT_CPU_WIDTH 4
#define PRINT_COUNT_WIDTH 9
#define PRINT_RUNTIME_WIDTH 10
#define PRINT_LATENCY_WIDTH 10
#define PRINT_TIMESTAMP_WIDTH 17
#define PRINT_KWORK_NAME_WIDTH 30
#define RPINT_DECIMAL_WIDTH 3
#define PRINT_BRACKETPAIR_WIDTH 2
#define PRINT_TIME_UNIT_SEC_WIDTH 2
#define PRINT_TIME_UNIT_MESC_WIDTH 3
#define PRINT_PID_WIDTH 7
#define PRINT_TASK_NAME_WIDTH 16
#define PRINT_CPU_USAGE_WIDTH 6
#define PRINT_CPU_USAGE_DECIMAL_WIDTH 2
#define PRINT_CPU_USAGE_HIST_WIDTH 30
#define PRINT_RUNTIME_HEADER_WIDTH (PRINT_RUNTIME_WIDTH + PRINT_TIME_UNIT_MESC_WIDTH)
#define PRINT_LATENCY_HEADER_WIDTH (PRINT_LATENCY_WIDTH + PRINT_TIME_UNIT_MESC_WIDTH)
#define PRINT_TIMEHIST_CPU_WIDTH (PRINT_CPU_WIDTH + PRINT_BRACKETPAIR_WIDTH)
#define PRINT_TIMESTAMP_HEADER_WIDTH (PRINT_TIMESTAMP_WIDTH + PRINT_TIME_UNIT_SEC_WIDTH)

struct sort_dimension {
	const char      *name;
	int             (*cmp)(struct kwork_work *l, struct kwork_work *r);
	struct          list_head list;
};

static int id_cmp(struct kwork_work *l, struct kwork_work *r)
{
	if (l->cpu > r->cpu)
		return 1;
	if (l->cpu < r->cpu)
		return -1;

	if (l->id > r->id)
		return 1;
	if (l->id < r->id)
		return -1;

	return 0;
}

static int count_cmp(struct kwork_work *l, struct kwork_work *r)
{
	if (l->nr_atoms > r->nr_atoms)
		return 1;
	if (l->nr_atoms < r->nr_atoms)
		return -1;

	return 0;
}

static int runtime_cmp(struct kwork_work *l, struct kwork_work *r)
{
	if (l->total_runtime > r->total_runtime)
		return 1;
	if (l->total_runtime < r->total_runtime)
		return -1;

	return 0;
}

static int max_runtime_cmp(struct kwork_work *l, struct kwork_work *r)
{
	if (l->max_runtime > r->max_runtime)
		return 1;
	if (l->max_runtime < r->max_runtime)
		return -1;

	return 0;
}

static int avg_latency_cmp(struct kwork_work *l, struct kwork_work *r)
{
	u64 avgl, avgr;

	if (!r->nr_atoms)
		return 1;
	if (!l->nr_atoms)
		return -1;

	avgl = l->total_latency / l->nr_atoms;
	avgr = r->total_latency / r->nr_atoms;

	if (avgl > avgr)
		return 1;
	if (avgl < avgr)
		return -1;

	return 0;
}

static int max_latency_cmp(struct kwork_work *l, struct kwork_work *r)
{
	if (l->max_latency > r->max_latency)
		return 1;
	if (l->max_latency < r->max_latency)
		return -1;

	return 0;
}

static int cpu_usage_cmp(struct kwork_work *l, struct kwork_work *r)
{
	if (l->cpu_usage > r->cpu_usage)
		return 1;
	if (l->cpu_usage < r->cpu_usage)
		return -1;

	return 0;
}

static int id_or_cpu_r_cmp(struct kwork_work *l, struct kwork_work *r)
{
	if (l->id < r->id)
		return 1;
	if (l->id > r->id)
		return -1;

	if (l->id != 0)
		return 0;

	if (l->cpu < r->cpu)
		return 1;
	if (l->cpu > r->cpu)
		return -1;

	return 0;
}

static int sort_dimension__add(struct perf_kwork *kwork __maybe_unused,
			       const char *tok, struct list_head *list)
{
	size_t i;
	static struct sort_dimension max_sort_dimension = {
		.name = "max",
		.cmp  = max_runtime_cmp,
	};
	static struct sort_dimension id_sort_dimension = {
		.name = "id",
		.cmp  = id_cmp,
	};
	static struct sort_dimension runtime_sort_dimension = {
		.name = "runtime",
		.cmp  = runtime_cmp,
	};
	static struct sort_dimension count_sort_dimension = {
		.name = "count",
		.cmp  = count_cmp,
	};
	static struct sort_dimension avg_sort_dimension = {
		.name = "avg",
		.cmp  = avg_latency_cmp,
	};
	static struct sort_dimension rate_sort_dimension = {
		.name = "rate",
		.cmp  = cpu_usage_cmp,
	};
	static struct sort_dimension tid_sort_dimension = {
		.name = "tid",
		.cmp  = id_or_cpu_r_cmp,
	};
	struct sort_dimension *available_sorts[] = {
		&id_sort_dimension,
		&max_sort_dimension,
		&count_sort_dimension,
		&runtime_sort_dimension,
		&avg_sort_dimension,
		&rate_sort_dimension,
		&tid_sort_dimension,
	};

	if (kwork->report == KWORK_REPORT_LATENCY)
		max_sort_dimension.cmp = max_latency_cmp;

	for (i = 0; i < ARRAY_SIZE(available_sorts); i++) {
		if (!strcmp(available_sorts[i]->name, tok)) {
			list_add_tail(&available_sorts[i]->list, list);
			return 0;
		}
	}

	return -1;
}

static void setup_sorting(struct perf_kwork *kwork,
			  const struct option *options,
			  const char * const usage_msg[])
{
	char *tmp, *tok, *str = strdup(kwork->sort_order);

	for (tok = strtok_r(str, ", ", &tmp);
	     tok; tok = strtok_r(NULL, ", ", &tmp)) {
		if (sort_dimension__add(kwork, tok, &kwork->sort_list) < 0)
			usage_with_options_msg(usage_msg, options,
					       "Unknown --sort key: `%s'", tok);
	}

	pr_debug("Sort order: %s\n", kwork->sort_order);
	free(str);
}

static struct kwork_atom *atom_new(struct perf_kwork *kwork,
				   struct perf_sample *sample)
{
	unsigned long i;
	struct kwork_atom_page *page;
	struct kwork_atom *atom = NULL;

	list_for_each_entry(page, &kwork->atom_page_list, list) {
		if (!bitmap_full(page->bitmap, NR_ATOM_PER_PAGE)) {
			i = find_first_zero_bit(page->bitmap, NR_ATOM_PER_PAGE);
			BUG_ON(i >= NR_ATOM_PER_PAGE);
			atom = &page->atoms[i];
			goto found_atom;
		}
	}

	/*
	 * new page
	 */
	page = zalloc(sizeof(*page));
	if (page == NULL) {
		pr_err("Failed to zalloc kwork atom page\n");
		return NULL;
	}

	i = 0;
	atom = &page->atoms[0];
	list_add_tail(&page->list, &kwork->atom_page_list);

found_atom:
	__set_bit(i, page->bitmap);
	atom->time = sample->time;
	atom->prev = NULL;
	atom->page_addr = page;
	atom->bit_inpage = i;
	return atom;
}

static void atom_free(struct kwork_atom *atom)
{
	if (atom->prev != NULL)
		atom_free(atom->prev);

	__clear_bit(atom->bit_inpage,
		    ((struct kwork_atom_page *)atom->page_addr)->bitmap);
}

static void atom_del(struct kwork_atom *atom)
{
	list_del(&atom->list);
	atom_free(atom);
}

static int work_cmp(struct list_head *list,
		    struct kwork_work *l, struct kwork_work *r)
{
	int ret = 0;
	struct sort_dimension *sort;

	BUG_ON(list_empty(list));

	list_for_each_entry(sort, list, list) {
		ret = sort->cmp(l, r);
		if (ret)
			return ret;
	}

	return ret;
}

static struct kwork_work *work_search(struct rb_root_cached *root,
				      struct kwork_work *key,
				      struct list_head *sort_list)
{
	int cmp;
	struct kwork_work *work;
	struct rb_node *node = root->rb_root.rb_node;

	while (node) {
		work = container_of(node, struct kwork_work, node);
		cmp = work_cmp(sort_list, key, work);
		if (cmp > 0)
			node = node->rb_left;
		else if (cmp < 0)
			node = node->rb_right;
		else {
			if (work->name == NULL)
				work->name = key->name;
			return work;
		}
	}
	return NULL;
}

static void work_insert(struct rb_root_cached *root,
			struct kwork_work *key, struct list_head *sort_list)
{
	int cmp;
	bool leftmost = true;
	struct kwork_work *cur;
	struct rb_node **new = &(root->rb_root.rb_node), *parent = NULL;

	while (*new) {
		cur = container_of(*new, struct kwork_work, node);
		parent = *new;
		cmp = work_cmp(sort_list, key, cur);

		if (cmp > 0)
			new = &((*new)->rb_left);
		else {
			new = &((*new)->rb_right);
			leftmost = false;
		}
	}

	rb_link_node(&key->node, parent, new);
	rb_insert_color_cached(&key->node, root, leftmost);
}

static struct kwork_work *work_new(struct kwork_work *key)
{
	int i;
	struct kwork_work *work = zalloc(sizeof(*work));

	if (work == NULL) {
		pr_err("Failed to zalloc kwork work\n");
		return NULL;
	}

	for (i = 0; i < KWORK_TRACE_MAX; i++)
		INIT_LIST_HEAD(&work->atom_list[i]);

	work->id = key->id;
	work->cpu = key->cpu;
	work->name = key->name;
	work->class = key->class;
	return work;
}

static struct kwork_work *work_findnew(struct rb_root_cached *root,
				       struct kwork_work *key,
				       struct list_head *sort_list)
{
	struct kwork_work *work = work_search(root, key, sort_list);

	if (work != NULL)
		return work;

	work = work_new(key);
	if (work)
		work_insert(root, work, sort_list);

	return work;
}

static void profile_update_timespan(struct perf_kwork *kwork,
				    struct perf_sample *sample)
{
	if (!kwork->summary)
		return;

	if ((kwork->timestart == 0) || (kwork->timestart > sample->time))
		kwork->timestart = sample->time;

	if (kwork->timeend < sample->time)
		kwork->timeend = sample->time;
}

static bool profile_name_match(struct perf_kwork *kwork,
			       struct kwork_work *work)
{
	if (kwork->profile_name && work->name &&
	    (strcmp(work->name, kwork->profile_name) != 0)) {
		return false;
	}

	return true;
}

static bool profile_event_match(struct perf_kwork *kwork,
				struct kwork_work *work,
				struct perf_sample *sample)
{
	int cpu = work->cpu;
	u64 time = sample->time;
	struct perf_time_interval *ptime = &kwork->ptime;

	if ((kwork->cpu_list != NULL) && !test_bit(cpu, kwork->cpu_bitmap))
		return false;

	if (((ptime->start != 0) && (ptime->start > time)) ||
	    ((ptime->end != 0) && (ptime->end < time)))
		return false;

	/*
	 * report top needs to collect the runtime of all tasks to
	 * calculate the load of each core.
	 */
	if ((kwork->report != KWORK_REPORT_TOP) &&
	    !profile_name_match(kwork, work)) {
		return false;
	}

	profile_update_timespan(kwork, sample);
	return true;
}

static int work_push_atom(struct perf_kwork *kwork,
			  struct kwork_class *class,
			  enum kwork_trace_type src_type,
			  enum kwork_trace_type dst_type,
			  struct evsel *evsel,
			  struct perf_sample *sample,
			  struct machine *machine,
			  struct kwork_work **ret_work,
			  bool overwrite)
{
	struct kwork_atom *atom, *dst_atom, *last_atom;
	struct kwork_work *work, key;

	BUG_ON(class->work_init == NULL);
	class->work_init(kwork, class, &key, src_type, evsel, sample, machine);

	atom = atom_new(kwork, sample);
	if (atom == NULL)
		return -1;

	work = work_findnew(&class->work_root, &key, &kwork->cmp_id);
	if (work == NULL) {
		atom_free(atom);
		return -1;
	}

	if (!profile_event_match(kwork, work, sample)) {
		atom_free(atom);
		return 0;
	}

	if (dst_type < KWORK_TRACE_MAX) {
		dst_atom = list_last_entry_or_null(&work->atom_list[dst_type],
						   struct kwork_atom, list);
		if (dst_atom != NULL) {
			atom->prev = dst_atom;
			list_del(&dst_atom->list);
		}
	}

	if (ret_work != NULL)
		*ret_work = work;

	if (overwrite) {
		last_atom = list_last_entry_or_null(&work->atom_list[src_type],
						    struct kwork_atom, list);
		if (last_atom) {
			atom_del(last_atom);

			kwork->nr_skipped_events[src_type]++;
			kwork->nr_skipped_events[KWORK_TRACE_MAX]++;
		}
	}

	list_add_tail(&atom->list, &work->atom_list[src_type]);

	return 0;
}

static struct kwork_atom *work_pop_atom(struct perf_kwork *kwork,
					struct kwork_class *class,
					enum kwork_trace_type src_type,
					enum kwork_trace_type dst_type,
					struct evsel *evsel,
					struct perf_sample *sample,
					struct machine *machine,
					struct kwork_work **ret_work)
{
	struct kwork_atom *atom, *src_atom;
	struct kwork_work *work, key;

	BUG_ON(class->work_init == NULL);
	class->work_init(kwork, class, &key, src_type, evsel, sample, machine);

	work = work_findnew(&class->work_root, &key, &kwork->cmp_id);
	if (ret_work != NULL)
		*ret_work = work;

	if (work == NULL)
		return NULL;

	if (!profile_event_match(kwork, work, sample))
		return NULL;

	atom = list_last_entry_or_null(&work->atom_list[dst_type],
				       struct kwork_atom, list);
	if (atom != NULL)
		return atom;

	src_atom = atom_new(kwork, sample);
	if (src_atom != NULL)
		list_add_tail(&src_atom->list, &work->atom_list[src_type]);
	else {
		if (ret_work != NULL)
			*ret_work = NULL;
	}

	return NULL;
}

static struct kwork_work *find_work_by_id(struct rb_root_cached *root,
					  u64 id, int cpu)
{
	struct rb_node *next;
	struct kwork_work *work;

	next = rb_first_cached(root);
	while (next) {
		work = rb_entry(next, struct kwork_work, node);
		if ((cpu != -1 && work->id == id && work->cpu == cpu) ||
		    (cpu == -1 && work->id == id))
			return work;

		next = rb_next(next);
	}

	return NULL;
}

static struct kwork_class *get_kwork_class(struct perf_kwork *kwork,
					   enum kwork_class_type type)
{
	struct kwork_class *class;

	list_for_each_entry(class, &kwork->class_list, list) {
		if (class->type == type)
			return class;
	}

	return NULL;
}

static void report_update_exit_event(struct kwork_work *work,
				     struct kwork_atom *atom,
				     struct perf_sample *sample)
{
	u64 delta;
	u64 exit_time = sample->time;
	u64 entry_time = atom->time;

	if ((entry_time != 0) && (exit_time >= entry_time)) {
		delta = exit_time - entry_time;
		if ((delta > work->max_runtime) ||
		    (work->max_runtime == 0)) {
			work->max_runtime = delta;
			work->max_runtime_start = entry_time;
			work->max_runtime_end = exit_time;
		}
		work->total_runtime += delta;
		work->nr_atoms++;
	}
}

static int report_entry_event(struct perf_kwork *kwork,
			      struct kwork_class *class,
			      struct evsel *evsel,
			      struct perf_sample *sample,
			      struct machine *machine)
{
	return work_push_atom(kwork, class, KWORK_TRACE_ENTRY,
			      KWORK_TRACE_MAX, evsel, sample,
			      machine, NULL, true);
}

static int report_exit_event(struct perf_kwork *kwork,
			     struct kwork_class *class,
			     struct evsel *evsel,
			     struct perf_sample *sample,
			     struct machine *machine)
{
	struct kwork_atom *atom = NULL;
	struct kwork_work *work = NULL;

	atom = work_pop_atom(kwork, class, KWORK_TRACE_EXIT,
			     KWORK_TRACE_ENTRY, evsel, sample,
			     machine, &work);
	if (work == NULL)
		return -1;

	if (atom != NULL) {
		report_update_exit_event(work, atom, sample);
		atom_del(atom);
	}

	return 0;
}

static void latency_update_entry_event(struct kwork_work *work,
				       struct kwork_atom *atom,
				       struct perf_sample *sample)
{
	u64 delta;
	u64 entry_time = sample->time;
	u64 raise_time = atom->time;

	if ((raise_time != 0) && (entry_time >= raise_time)) {
		delta = entry_time - raise_time;
		if ((delta > work->max_latency) ||
		    (work->max_latency == 0)) {
			work->max_latency = delta;
			work->max_latency_start = raise_time;
			work->max_latency_end = entry_time;
		}
		work->total_latency += delta;
		work->nr_atoms++;
	}
}

static int latency_raise_event(struct perf_kwork *kwork,
			       struct kwork_class *class,
			       struct evsel *evsel,
			       struct perf_sample *sample,
			       struct machine *machine)
{
	return work_push_atom(kwork, class, KWORK_TRACE_RAISE,
			      KWORK_TRACE_MAX, evsel, sample,
			      machine, NULL, true);
}

static int latency_entry_event(struct perf_kwork *kwork,
			       struct kwork_class *class,
			       struct evsel *evsel,
			       struct perf_sample *sample,
			       struct machine *machine)
{
	struct kwork_atom *atom = NULL;
	struct kwork_work *work = NULL;

	atom = work_pop_atom(kwork, class, KWORK_TRACE_ENTRY,
			     KWORK_TRACE_RAISE, evsel, sample,
			     machine, &work);
	if (work == NULL)
		return -1;

	if (atom != NULL) {
		latency_update_entry_event(work, atom, sample);
		atom_del(atom);
	}

	return 0;
}

static void timehist_save_callchain(struct perf_kwork *kwork,
				    struct perf_sample *sample,
				    struct evsel *evsel,
				    struct machine *machine)
{
	struct symbol *sym;
	struct thread *thread;
	struct callchain_cursor_node *node;
	struct callchain_cursor *cursor;

	if (!kwork->show_callchain || sample->callchain == NULL)
		return;

	/* want main thread for process - has maps */
	thread = machine__findnew_thread(machine, sample->pid, sample->pid);
	if (thread == NULL) {
		pr_debug("Failed to get thread for pid %d\n", sample->pid);
		return;
	}

	cursor = get_tls_callchain_cursor();

	if (thread__resolve_callchain(thread, cursor, evsel, sample,
				      NULL, NULL, kwork->max_stack + 2) != 0) {
		pr_debug("Failed to resolve callchain, skipping\n");
		goto out_put;
	}

	callchain_cursor_commit(cursor);

	while (true) {
		node = callchain_cursor_current(cursor);
		if (node == NULL)
			break;

		sym = node->ms.sym;
		if (sym) {
			if (!strcmp(sym->name, "__softirqentry_text_start") ||
			    !strcmp(sym->name, "__do_softirq"))
				sym->ignore = 1;
		}

		callchain_cursor_advance(cursor);
	}

out_put:
	thread__put(thread);
}

static void timehist_print_event(struct perf_kwork *kwork,
				 struct kwork_work *work,
				 struct kwork_atom *atom,
				 struct perf_sample *sample,
				 struct addr_location *al)
{
	char entrytime[32], exittime[32];
	char kwork_name[PRINT_KWORK_NAME_WIDTH];

	/*
	 * runtime start
	 */
	timestamp__scnprintf_usec(atom->time,
				  entrytime, sizeof(entrytime));
	printf(" %*s ", PRINT_TIMESTAMP_WIDTH, entrytime);

	/*
	 * runtime end
	 */
	timestamp__scnprintf_usec(sample->time,
				  exittime, sizeof(exittime));
	printf(" %*s ", PRINT_TIMESTAMP_WIDTH, exittime);

	/*
	 * cpu
	 */
	printf(" [%0*d] ", PRINT_CPU_WIDTH, work->cpu);

	/*
	 * kwork name
	 */
	if (work->class && work->class->work_name) {
		work->class->work_name(work, kwork_name,
				       PRINT_KWORK_NAME_WIDTH);
		printf(" %-*s ", PRINT_KWORK_NAME_WIDTH, kwork_name);
	} else
		printf(" %-*s ", PRINT_KWORK_NAME_WIDTH, "");

	/*
	 *runtime
	 */
	printf(" %*.*f ",
	       PRINT_RUNTIME_WIDTH, RPINT_DECIMAL_WIDTH,
	       (double)(sample->time - atom->time) / NSEC_PER_MSEC);

	/*
	 * delaytime
	 */
	if (atom->prev != NULL)
		printf(" %*.*f ", PRINT_LATENCY_WIDTH, RPINT_DECIMAL_WIDTH,
		       (double)(atom->time - atom->prev->time) / NSEC_PER_MSEC);
	else
		printf(" %*s ", PRINT_LATENCY_WIDTH, " ");

	/*
	 * callchain
	 */
	if (kwork->show_callchain) {
		struct callchain_cursor *cursor = get_tls_callchain_cursor();

		if (cursor == NULL)
			return;

		printf(" ");

		sample__fprintf_sym(sample, al, 0,
				    EVSEL__PRINT_SYM | EVSEL__PRINT_ONELINE |
				    EVSEL__PRINT_CALLCHAIN_ARROW |
				    EVSEL__PRINT_SKIP_IGNORED,
				    cursor, symbol_conf.bt_stop_list,
				    stdout);
	}

	printf("\n");
}

static int timehist_raise_event(struct perf_kwork *kwork,
				struct kwork_class *class,
				struct evsel *evsel,
				struct perf_sample *sample,
				struct machine *machine)
{
	return work_push_atom(kwork, class, KWORK_TRACE_RAISE,
			      KWORK_TRACE_MAX, evsel, sample,
			      machine, NULL, true);
}

static int timehist_entry_event(struct perf_kwork *kwork,
				struct kwork_class *class,
				struct evsel *evsel,
				struct perf_sample *sample,
				struct machine *machine)
{
	int ret;
	struct kwork_work *work = NULL;

	ret = work_push_atom(kwork, class, KWORK_TRACE_ENTRY,
			     KWORK_TRACE_RAISE, evsel, sample,
			     machine, &work, true);
	if (ret)
		return ret;

	if (work != NULL)
		timehist_save_callchain(kwork, sample, evsel, machine);

	return 0;
}

static int timehist_exit_event(struct perf_kwork *kwork,
			       struct kwork_class *class,
			       struct evsel *evsel,
			       struct perf_sample *sample,
			       struct machine *machine)
{
	struct kwork_atom *atom = NULL;
	struct kwork_work *work = NULL;
	struct addr_location al;
	int ret = 0;

	addr_location__init(&al);
	if (machine__resolve(machine, &al, sample) < 0) {
		pr_debug("Problem processing event, skipping it\n");
		ret = -1;
		goto out;
	}

	atom = work_pop_atom(kwork, class, KWORK_TRACE_EXIT,
			     KWORK_TRACE_ENTRY, evsel, sample,
			     machine, &work);
	if (work == NULL) {
		ret = -1;
		goto out;
	}

	if (atom != NULL) {
		work->nr_atoms++;
		timehist_print_event(kwork, work, atom, sample, &al);
		atom_del(atom);
	}

out:
	addr_location__exit(&al);
	return ret;
}

static void top_update_runtime(struct kwork_work *work,
			       struct kwork_atom *atom,
			       struct perf_sample *sample)
{
	u64 delta;
	u64 exit_time = sample->time;
	u64 entry_time = atom->time;

	if ((entry_time != 0) && (exit_time >= entry_time)) {
		delta = exit_time - entry_time;
		work->total_runtime += delta;
	}
}

static int top_entry_event(struct perf_kwork *kwork,
			   struct kwork_class *class,
			   struct evsel *evsel,
			   struct perf_sample *sample,
			   struct machine *machine)
{
	return work_push_atom(kwork, class, KWORK_TRACE_ENTRY,
			      KWORK_TRACE_MAX, evsel, sample,
			      machine, NULL, true);
}

static int top_exit_event(struct perf_kwork *kwork,
			  struct kwork_class *class,
			  struct evsel *evsel,
			  struct perf_sample *sample,
			  struct machine *machine)
{
	struct kwork_work *work, *sched_work;
	struct kwork_class *sched_class;
	struct kwork_atom *atom;

	atom = work_pop_atom(kwork, class, KWORK_TRACE_EXIT,
			     KWORK_TRACE_ENTRY, evsel, sample,
			     machine, &work);
	if (!work)
		return -1;

	if (atom) {
		sched_class = get_kwork_class(kwork, KWORK_CLASS_SCHED);
		if (sched_class) {
			sched_work = find_work_by_id(&sched_class->work_root,
						     work->id, work->cpu);
			if (sched_work)
				top_update_runtime(work, atom, sample);
		}
		atom_del(atom);
	}

	return 0;
}

static int top_sched_switch_event(struct perf_kwork *kwork,
				  struct kwork_class *class,
				  struct evsel *evsel,
				  struct perf_sample *sample,
				  struct machine *machine)
{
	struct kwork_atom *atom;
	struct kwork_work *work;

	atom = work_pop_atom(kwork, class, KWORK_TRACE_EXIT,
			     KWORK_TRACE_ENTRY, evsel, sample,
			     machine, &work);
	if (!work)
		return -1;

	if (atom) {
		top_update_runtime(work, atom, sample);
		atom_del(atom);
	}

	return top_entry_event(kwork, class, evsel, sample, machine);
}

static struct kwork_class kwork_irq;
static int process_irq_handler_entry_event(struct perf_tool *tool,
					   struct evsel *evsel,
					   struct perf_sample *sample,
					   struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->entry_event)
		return kwork->tp_handler->entry_event(kwork, &kwork_irq,
						      evsel, sample, machine);
	return 0;
}

static int process_irq_handler_exit_event(struct perf_tool *tool,
					  struct evsel *evsel,
					  struct perf_sample *sample,
					  struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->exit_event)
		return kwork->tp_handler->exit_event(kwork, &kwork_irq,
						     evsel, sample, machine);
	return 0;
}

const struct evsel_str_handler irq_tp_handlers[] = {
	{ "irq:irq_handler_entry", process_irq_handler_entry_event, },
	{ "irq:irq_handler_exit",  process_irq_handler_exit_event,  },
};

static int irq_class_init(struct kwork_class *class,
			  struct perf_session *session)
{
	if (perf_session__set_tracepoints_handlers(session, irq_tp_handlers)) {
		pr_err("Failed to set irq tracepoints handlers\n");
		return -1;
	}

	class->work_root = RB_ROOT_CACHED;
	return 0;
}

static void irq_work_init(struct perf_kwork *kwork,
			  struct kwork_class *class,
			  struct kwork_work *work,
			  enum kwork_trace_type src_type __maybe_unused,
			  struct evsel *evsel,
			  struct perf_sample *sample,
			  struct machine *machine __maybe_unused)
{
	work->class = class;
	work->cpu = sample->cpu;

	if (kwork->report == KWORK_REPORT_TOP) {
		work->id = evsel__intval_common(evsel, sample, "common_pid");
		work->name = NULL;
	} else {
		work->id = evsel__intval(evsel, sample, "irq");
		work->name = evsel__strval(evsel, sample, "name");
	}
}

static void irq_work_name(struct kwork_work *work, char *buf, int len)
{
	snprintf(buf, len, "%s:%" PRIu64 "", work->name, work->id);
}

static struct kwork_class kwork_irq = {
	.name           = "irq",
	.type           = KWORK_CLASS_IRQ,
	.nr_tracepoints = 2,
	.tp_handlers    = irq_tp_handlers,
	.class_init     = irq_class_init,
	.work_init      = irq_work_init,
	.work_name      = irq_work_name,
};

static struct kwork_class kwork_softirq;
static int process_softirq_raise_event(struct perf_tool *tool,
				       struct evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->raise_event)
		return kwork->tp_handler->raise_event(kwork, &kwork_softirq,
						      evsel, sample, machine);

	return 0;
}

static int process_softirq_entry_event(struct perf_tool *tool,
				       struct evsel *evsel,
				       struct perf_sample *sample,
				       struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->entry_event)
		return kwork->tp_handler->entry_event(kwork, &kwork_softirq,
						      evsel, sample, machine);

	return 0;
}

static int process_softirq_exit_event(struct perf_tool *tool,
				      struct evsel *evsel,
				      struct perf_sample *sample,
				      struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->exit_event)
		return kwork->tp_handler->exit_event(kwork, &kwork_softirq,
						     evsel, sample, machine);

	return 0;
}

const struct evsel_str_handler softirq_tp_handlers[] = {
	{ "irq:softirq_raise", process_softirq_raise_event, },
	{ "irq:softirq_entry", process_softirq_entry_event, },
	{ "irq:softirq_exit",  process_softirq_exit_event,  },
};

static int softirq_class_init(struct kwork_class *class,
			      struct perf_session *session)
{
	if (perf_session__set_tracepoints_handlers(session,
						   softirq_tp_handlers)) {
		pr_err("Failed to set softirq tracepoints handlers\n");
		return -1;
	}

	class->work_root = RB_ROOT_CACHED;
	return 0;
}

static char *evsel__softirq_name(struct evsel *evsel, u64 num)
{
	char *name = NULL;
	bool found = false;
	struct tep_print_flag_sym *sym = NULL;
	struct tep_print_arg *args = evsel->tp_format->print_fmt.args;

	if ((args == NULL) || (args->next == NULL))
		return NULL;

	/* skip softirq field: "REC->vec" */
	for (sym = args->next->symbol.symbols; sym != NULL; sym = sym->next) {
		if ((eval_flag(sym->value) == (unsigned long long)num) &&
		    (strlen(sym->str) != 0)) {
			found = true;
			break;
		}
	}

	if (!found)
		return NULL;

	name = strdup(sym->str);
	if (name == NULL) {
		pr_err("Failed to copy symbol name\n");
		return NULL;
	}
	return name;
}

static void softirq_work_init(struct perf_kwork *kwork,
			      struct kwork_class *class,
			      struct kwork_work *work,
			      enum kwork_trace_type src_type __maybe_unused,
			      struct evsel *evsel,
			      struct perf_sample *sample,
			      struct machine *machine __maybe_unused)
{
	u64 num;

	work->class = class;
	work->cpu = sample->cpu;

	if (kwork->report == KWORK_REPORT_TOP) {
		work->id = evsel__intval_common(evsel, sample, "common_pid");
		work->name = NULL;
	} else {
		num = evsel__intval(evsel, sample, "vec");
		work->id = num;
		work->name = evsel__softirq_name(evsel, num);
	}
}

static void softirq_work_name(struct kwork_work *work, char *buf, int len)
{
	snprintf(buf, len, "(s)%s:%" PRIu64 "", work->name, work->id);
}

static struct kwork_class kwork_softirq = {
	.name           = "softirq",
	.type           = KWORK_CLASS_SOFTIRQ,
	.nr_tracepoints = 3,
	.tp_handlers    = softirq_tp_handlers,
	.class_init     = softirq_class_init,
	.work_init      = softirq_work_init,
	.work_name      = softirq_work_name,
};

static struct kwork_class kwork_workqueue;
static int process_workqueue_activate_work_event(struct perf_tool *tool,
						 struct evsel *evsel,
						 struct perf_sample *sample,
						 struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->raise_event)
		return kwork->tp_handler->raise_event(kwork, &kwork_workqueue,
						    evsel, sample, machine);

	return 0;
}

static int process_workqueue_execute_start_event(struct perf_tool *tool,
						 struct evsel *evsel,
						 struct perf_sample *sample,
						 struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->entry_event)
		return kwork->tp_handler->entry_event(kwork, &kwork_workqueue,
						    evsel, sample, machine);

	return 0;
}

static int process_workqueue_execute_end_event(struct perf_tool *tool,
					       struct evsel *evsel,
					       struct perf_sample *sample,
					       struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->exit_event)
		return kwork->tp_handler->exit_event(kwork, &kwork_workqueue,
						   evsel, sample, machine);

	return 0;
}

const struct evsel_str_handler workqueue_tp_handlers[] = {
	{ "workqueue:workqueue_activate_work", process_workqueue_activate_work_event, },
	{ "workqueue:workqueue_execute_start", process_workqueue_execute_start_event, },
	{ "workqueue:workqueue_execute_end",   process_workqueue_execute_end_event,   },
};

static int workqueue_class_init(struct kwork_class *class,
				struct perf_session *session)
{
	if (perf_session__set_tracepoints_handlers(session,
						   workqueue_tp_handlers)) {
		pr_err("Failed to set workqueue tracepoints handlers\n");
		return -1;
	}

	class->work_root = RB_ROOT_CACHED;
	return 0;
}

static void workqueue_work_init(struct perf_kwork *kwork __maybe_unused,
				struct kwork_class *class,
				struct kwork_work *work,
				enum kwork_trace_type src_type __maybe_unused,
				struct evsel *evsel,
				struct perf_sample *sample,
				struct machine *machine)
{
	char *modp = NULL;
	unsigned long long function_addr = evsel__intval(evsel,
							 sample, "function");

	work->class = class;
	work->cpu = sample->cpu;
	work->id = evsel__intval(evsel, sample, "work");
	work->name = function_addr == 0 ? NULL :
		machine__resolve_kernel_addr(machine, &function_addr, &modp);
}

static void workqueue_work_name(struct kwork_work *work, char *buf, int len)
{
	if (work->name != NULL)
		snprintf(buf, len, "(w)%s", work->name);
	else
		snprintf(buf, len, "(w)0x%" PRIx64, work->id);
}

static struct kwork_class kwork_workqueue = {
	.name           = "workqueue",
	.type           = KWORK_CLASS_WORKQUEUE,
	.nr_tracepoints = 3,
	.tp_handlers    = workqueue_tp_handlers,
	.class_init     = workqueue_class_init,
	.work_init      = workqueue_work_init,
	.work_name      = workqueue_work_name,
};

static struct kwork_class kwork_sched;
static int process_sched_switch_event(struct perf_tool *tool,
				      struct evsel *evsel,
				      struct perf_sample *sample,
				      struct machine *machine)
{
	struct perf_kwork *kwork = container_of(tool, struct perf_kwork, tool);

	if (kwork->tp_handler->sched_switch_event)
		return kwork->tp_handler->sched_switch_event(kwork, &kwork_sched,
							     evsel, sample, machine);
	return 0;
}

const struct evsel_str_handler sched_tp_handlers[] = {
	{ "sched:sched_switch",  process_sched_switch_event, },
};

static int sched_class_init(struct kwork_class *class,
			    struct perf_session *session)
{
	if (perf_session__set_tracepoints_handlers(session,
						   sched_tp_handlers)) {
		pr_err("Failed to set sched tracepoints handlers\n");
		return -1;
	}

	class->work_root = RB_ROOT_CACHED;
	return 0;
}

static void sched_work_init(struct perf_kwork *kwork __maybe_unused,
			    struct kwork_class *class,
			    struct kwork_work *work,
			    enum kwork_trace_type src_type,
			    struct evsel *evsel,
			    struct perf_sample *sample,
			    struct machine *machine __maybe_unused)
{
	work->class = class;
	work->cpu = sample->cpu;

	if (src_type == KWORK_TRACE_EXIT) {
		work->id = evsel__intval(evsel, sample, "prev_pid");
		work->name = strdup(evsel__strval(evsel, sample, "prev_comm"));
	} else if (src_type == KWORK_TRACE_ENTRY) {
		work->id = evsel__intval(evsel, sample, "next_pid");
		work->name = strdup(evsel__strval(evsel, sample, "next_comm"));
	}
}

static void sched_work_name(struct kwork_work *work, char *buf, int len)
{
	snprintf(buf, len, "%s", work->name);
}

static struct kwork_class kwork_sched = {
	.name		= "sched",
	.type		= KWORK_CLASS_SCHED,
	.nr_tracepoints	= ARRAY_SIZE(sched_tp_handlers),
	.tp_handlers	= sched_tp_handlers,
	.class_init	= sched_class_init,
	.work_init	= sched_work_init,
	.work_name	= sched_work_name,
};

static struct kwork_class *kwork_class_supported_list[KWORK_CLASS_MAX] = {
	[KWORK_CLASS_IRQ]       = &kwork_irq,
	[KWORK_CLASS_SOFTIRQ]   = &kwork_softirq,
	[KWORK_CLASS_WORKQUEUE] = &kwork_workqueue,
	[KWORK_CLASS_SCHED]     = &kwork_sched,
};

static void print_separator(int len)
{
	printf(" %.*s\n", len, graph_dotted_line);
}

static int report_print_work(struct perf_kwork *kwork, struct kwork_work *work)
{
	int ret = 0;
	char kwork_name[PRINT_KWORK_NAME_WIDTH];
	char max_runtime_start[32], max_runtime_end[32];
	char max_latency_start[32], max_latency_end[32];

	printf(" ");

	/*
	 * kwork name
	 */
	if (work->class && work->class->work_name) {
		work->class->work_name(work, kwork_name,
				       PRINT_KWORK_NAME_WIDTH);
		ret += printf(" %-*s |", PRINT_KWORK_NAME_WIDTH, kwork_name);
	} else {
		ret += printf(" %-*s |", PRINT_KWORK_NAME_WIDTH, "");
	}

	/*
	 * cpu
	 */
	ret += printf(" %0*d |", PRINT_CPU_WIDTH, work->cpu);

	/*
	 * total runtime
	 */
	if (kwork->report == KWORK_REPORT_RUNTIME) {
		ret += printf(" %*.*f ms |",
			      PRINT_RUNTIME_WIDTH, RPINT_DECIMAL_WIDTH,
			      (double)work->total_runtime / NSEC_PER_MSEC);
	} else if (kwork->report == KWORK_REPORT_LATENCY) { // avg delay
		ret += printf(" %*.*f ms |",
			      PRINT_LATENCY_WIDTH, RPINT_DECIMAL_WIDTH,
			      (double)work->total_latency /
			      work->nr_atoms / NSEC_PER_MSEC);
	}

	/*
	 * count
	 */
	ret += printf(" %*" PRIu64 " |", PRINT_COUNT_WIDTH, work->nr_atoms);

	/*
	 * max runtime, max runtime start, max runtime end
	 */
	if (kwork->report == KWORK_REPORT_RUNTIME) {
		timestamp__scnprintf_usec(work->max_runtime_start,
					  max_runtime_start,
					  sizeof(max_runtime_start));
		timestamp__scnprintf_usec(work->max_runtime_end,
					  max_runtime_end,
					  sizeof(max_runtime_end));
		ret += printf(" %*.*f ms | %*s s | %*s s |",
			      PRINT_RUNTIME_WIDTH, RPINT_DECIMAL_WIDTH,
			      (double)work->max_runtime / NSEC_PER_MSEC,
			      PRINT_TIMESTAMP_WIDTH, max_runtime_start,
			      PRINT_TIMESTAMP_WIDTH, max_runtime_end);
	}
	/*
	 * max delay, max delay start, max delay end
	 */
	else if (kwork->report == KWORK_REPORT_LATENCY) {
		timestamp__scnprintf_usec(work->max_latency_start,
					  max_latency_start,
					  sizeof(max_latency_start));
		timestamp__scnprintf_usec(work->max_latency_end,
					  max_latency_end,
					  sizeof(max_latency_end));
		ret += printf(" %*.*f ms | %*s s | %*s s |",
			      PRINT_LATENCY_WIDTH, RPINT_DECIMAL_WIDTH,
			      (double)work->max_latency / NSEC_PER_MSEC,
			      PRINT_TIMESTAMP_WIDTH, max_latency_start,
			      PRINT_TIMESTAMP_WIDTH, max_latency_end);
	}

	printf("\n");
	return ret;
}

static int report_print_header(struct perf_kwork *kwork)
{
	int ret;

	printf("\n ");
	ret = printf(" %-*s | %-*s |",
		     PRINT_KWORK_NAME_WIDTH, "Kwork Name",
		     PRINT_CPU_WIDTH, "Cpu");

	if (kwork->report == KWORK_REPORT_RUNTIME) {
		ret += printf(" %-*s |",
			      PRINT_RUNTIME_HEADER_WIDTH, "Total Runtime");
	} else if (kwork->report == KWORK_REPORT_LATENCY) {
		ret += printf(" %-*s |",
			      PRINT_LATENCY_HEADER_WIDTH, "Avg delay");
	}

	ret += printf(" %-*s |", PRINT_COUNT_WIDTH, "Count");

	if (kwork->report == KWORK_REPORT_RUNTIME) {
		ret += printf(" %-*s | %-*s | %-*s |",
			      PRINT_RUNTIME_HEADER_WIDTH, "Max runtime",
			      PRINT_TIMESTAMP_HEADER_WIDTH, "Max runtime start",
			      PRINT_TIMESTAMP_HEADER_WIDTH, "Max runtime end");
	} else if (kwork->report == KWORK_REPORT_LATENCY) {
		ret += printf(" %-*s | %-*s | %-*s |",
			      PRINT_LATENCY_HEADER_WIDTH, "Max delay",
			      PRINT_TIMESTAMP_HEADER_WIDTH, "Max delay start",
			      PRINT_TIMESTAMP_HEADER_WIDTH, "Max delay end");
	}

	printf("\n");
	print_separator(ret);
	return ret;
}

static void timehist_print_header(void)
{
	/*
	 * header row
	 */
	printf(" %-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n",
	       PRINT_TIMESTAMP_WIDTH, "Runtime start",
	       PRINT_TIMESTAMP_WIDTH, "Runtime end",
	       PRINT_TIMEHIST_CPU_WIDTH, "Cpu",
	       PRINT_KWORK_NAME_WIDTH, "Kwork name",
	       PRINT_RUNTIME_WIDTH, "Runtime",
	       PRINT_RUNTIME_WIDTH, "Delaytime");

	/*
	 * units row
	 */
	printf(" %-*s  %-*s  %-*s  %-*s  %-*s  %-*s\n",
	       PRINT_TIMESTAMP_WIDTH, "",
	       PRINT_TIMESTAMP_WIDTH, "",
	       PRINT_TIMEHIST_CPU_WIDTH, "",
	       PRINT_KWORK_NAME_WIDTH, "(TYPE)NAME:NUM",
	       PRINT_RUNTIME_WIDTH, "(msec)",
	       PRINT_RUNTIME_WIDTH, "(msec)");

	/*
	 * separator
	 */
	printf(" %.*s  %.*s  %.*s  %.*s  %.*s  %.*s\n",
	       PRINT_TIMESTAMP_WIDTH, graph_dotted_line,
	       PRINT_TIMESTAMP_WIDTH, graph_dotted_line,
	       PRINT_TIMEHIST_CPU_WIDTH, graph_dotted_line,
	       PRINT_KWORK_NAME_WIDTH, graph_dotted_line,
	       PRINT_RUNTIME_WIDTH, graph_dotted_line,
	       PRINT_RUNTIME_WIDTH, graph_dotted_line);
}

static void print_summary(struct perf_kwork *kwork)
{
	u64 time = kwork->timeend - kwork->timestart;

	printf("  Total count            : %9" PRIu64 "\n", kwork->all_count);
	printf("  Total runtime   (msec) : %9.3f (%.3f%% load average)\n",
	       (double)kwork->all_runtime / NSEC_PER_MSEC,
	       time == 0 ? 0 : (double)kwork->all_runtime / time);
	printf("  Total time span (msec) : %9.3f\n",
	       (double)time / NSEC_PER_MSEC);
}

static unsigned long long nr_list_entry(struct list_head *head)
{
	struct list_head *pos;
	unsigned long long n = 0;

	list_for_each(pos, head)
		n++;

	return n;
}

static void print_skipped_events(struct perf_kwork *kwork)
{
	int i;
	const char *const kwork_event_str[] = {
		[KWORK_TRACE_RAISE] = "raise",
		[KWORK_TRACE_ENTRY] = "entry",
		[KWORK_TRACE_EXIT]  = "exit",
	};

	if ((kwork->nr_skipped_events[KWORK_TRACE_MAX] != 0) &&
	    (kwork->nr_events != 0)) {
		printf("  INFO: %.3f%% skipped events (%" PRIu64 " including ",
		       (double)kwork->nr_skipped_events[KWORK_TRACE_MAX] /
		       (double)kwork->nr_events * 100.0,
		       kwork->nr_skipped_events[KWORK_TRACE_MAX]);

		for (i = 0; i < KWORK_TRACE_MAX; i++) {
			printf("%" PRIu64 " %s%s",
			       kwork->nr_skipped_events[i],
			       kwork_event_str[i],
			       (i == KWORK_TRACE_MAX - 1) ? ")\n" : ", ");
		}
	}

	if (verbose > 0)
		printf("  INFO: use %lld atom pages\n",
		       nr_list_entry(&kwork->atom_page_list));
}

static void print_bad_events(struct perf_kwork *kwork)
{
	if ((kwork->nr_lost_events != 0) && (kwork->nr_events != 0)) {
		printf("  INFO: %.3f%% lost events (%ld out of %ld, in %ld chunks)\n",
		       (double)kwork->nr_lost_events /
		       (double)kwork->nr_events * 100.0,
		       kwork->nr_lost_events, kwork->nr_events,
		       kwork->nr_lost_chunks);
	}
}

const char *graph_load = "||||||||||||||||||||||||||||||||||||||||||||||||";
const char *graph_idle = "                                                ";
static void top_print_per_cpu_load(struct perf_kwork *kwork)
{
	int i, load_width;
	u64 total, load, load_ratio;
	struct kwork_top_stat *stat = &kwork->top_stat;

	for (i = 0; i < MAX_NR_CPUS; i++) {
		total = stat->cpus_runtime[i].total;
		load = stat->cpus_runtime[i].load;
		if (test_bit(i, stat->all_cpus_bitmap) && total) {
			load_ratio = load * 10000 / total;
			load_width = PRINT_CPU_USAGE_HIST_WIDTH *
				load_ratio / 10000;

			printf("%%Cpu%-*d[%.*s%.*s %*.*f%%]\n",
			       PRINT_CPU_WIDTH, i,
			       load_width, graph_load,
			       PRINT_CPU_USAGE_HIST_WIDTH - load_width,
			       graph_idle,
			       PRINT_CPU_USAGE_WIDTH,
			       PRINT_CPU_USAGE_DECIMAL_WIDTH,
			       (double)load_ratio / 100);
		}
	}
}

static void top_print_cpu_usage(struct perf_kwork *kwork)
{
	struct kwork_top_stat *stat = &kwork->top_stat;
	u64 idle_time = stat->cpus_runtime[MAX_NR_CPUS].idle;
	u64 hardirq_time = stat->cpus_runtime[MAX_NR_CPUS].irq;
	u64 softirq_time = stat->cpus_runtime[MAX_NR_CPUS].softirq;
	int cpus_nr = bitmap_weight(stat->all_cpus_bitmap, MAX_NR_CPUS);
	u64 cpus_total_time = stat->cpus_runtime[MAX_NR_CPUS].total;

	printf("Total  : %*.*f ms, %d cpus\n",
	       PRINT_RUNTIME_WIDTH, RPINT_DECIMAL_WIDTH,
	       (double)cpus_total_time / NSEC_PER_MSEC,
	       cpus_nr);

	printf("%%Cpu(s): %*.*f%% id, %*.*f%% hi, %*.*f%% si\n",
	       PRINT_CPU_USAGE_WIDTH, PRINT_CPU_USAGE_DECIMAL_WIDTH,
	       cpus_total_time ? (double)idle_time * 100 / cpus_total_time : 0,

	       PRINT_CPU_USAGE_WIDTH, PRINT_CPU_USAGE_DECIMAL_WIDTH,
	       cpus_total_time ? (double)hardirq_time * 100 / cpus_total_time : 0,

	       PRINT_CPU_USAGE_WIDTH, PRINT_CPU_USAGE_DECIMAL_WIDTH,
	       cpus_total_time ? (double)softirq_time * 100 / cpus_total_time : 0);

	top_print_per_cpu_load(kwork);
}

static void top_print_header(struct perf_kwork *kwork __maybe_unused)
{
	int ret;

	printf("\n ");
	ret = printf(" %*s %s%*s%s %*s  %*s  %-*s",
		     PRINT_PID_WIDTH, "PID",

		     kwork->use_bpf ? " " : "",
		     kwork->use_bpf ? PRINT_PID_WIDTH : 0,
		     kwork->use_bpf ? "SPID" : "",
		     kwork->use_bpf ? " " : "",

		     PRINT_CPU_USAGE_WIDTH, "%CPU",
		     PRINT_RUNTIME_HEADER_WIDTH + RPINT_DECIMAL_WIDTH, "RUNTIME",
		     PRINT_TASK_NAME_WIDTH, "COMMAND");
	printf("\n ");
	print_separator(ret);
}

static int top_print_work(struct perf_kwork *kwork __maybe_unused, struct kwork_work *work)
{
	int ret = 0;

	printf(" ");

	/*
	 * pid
	 */
	ret += printf(" %*" PRIu64 " ", PRINT_PID_WIDTH, work->id);

	/*
	 * tgid
	 */
	if (kwork->use_bpf)
		ret += printf(" %*d ", PRINT_PID_WIDTH, work->tgid);

	/*
	 * cpu usage
	 */
	ret += printf(" %*.*f ",
		      PRINT_CPU_USAGE_WIDTH, PRINT_CPU_USAGE_DECIMAL_WIDTH,
		      (double)work->cpu_usage / 100);

	/*
	 * total runtime
	 */
	ret += printf(" %*.*f ms ",
		      PRINT_RUNTIME_WIDTH + RPINT_DECIMAL_WIDTH, RPINT_DECIMAL_WIDTH,
		      (double)work->total_runtime / NSEC_PER_MSEC);

	/*
	 * command
	 */
	if (kwork->use_bpf)
		ret += printf(" %s%s%s",
			      work->is_kthread ? "[" : "",
			      work->name,
			      work->is_kthread ? "]" : "");
	else
		ret += printf(" %-*s", PRINT_TASK_NAME_WIDTH, work->name);

	printf("\n");
	return ret;
}

static void work_sort(struct perf_kwork *kwork,
		      struct kwork_class *class, struct rb_root_cached *root)
{
	struct rb_node *node;
	struct kwork_work *data;

	pr_debug("Sorting %s ...\n", class->name);
	for (;;) {
		node = rb_first_cached(root);
		if (!node)
			break;

		rb_erase_cached(node, root);
		data = rb_entry(node, struct kwork_work, node);
		work_insert(&kwork->sorted_work_root,
			       data, &kwork->sort_list);
	}
}

static void perf_kwork__sort(struct perf_kwork *kwork)
{
	struct kwork_class *class;

	list_for_each_entry(class, &kwork->class_list, list)
		work_sort(kwork, class, &class->work_root);
}

static int perf_kwork__check_config(struct perf_kwork *kwork,
				    struct perf_session *session)
{
	int ret;
	struct evsel *evsel;
	struct kwork_class *class;

	static struct trace_kwork_handler report_ops = {
		.entry_event = report_entry_event,
		.exit_event  = report_exit_event,
	};
	static struct trace_kwork_handler latency_ops = {
		.raise_event = latency_raise_event,
		.entry_event = latency_entry_event,
	};
	static struct trace_kwork_handler timehist_ops = {
		.raise_event = timehist_raise_event,
		.entry_event = timehist_entry_event,
		.exit_event  = timehist_exit_event,
	};
	static struct trace_kwork_handler top_ops = {
		.entry_event        = timehist_entry_event,
		.exit_event         = top_exit_event,
		.sched_switch_event = top_sched_switch_event,
	};

	switch (kwork->report) {
	case KWORK_REPORT_RUNTIME:
		kwork->tp_handler = &report_ops;
		break;
	case KWORK_REPORT_LATENCY:
		kwork->tp_handler = &latency_ops;
		break;
	case KWORK_REPORT_TIMEHIST:
		kwork->tp_handler = &timehist_ops;
		break;
	case KWORK_REPORT_TOP:
		kwork->tp_handler = &top_ops;
		break;
	default:
		pr_debug("Invalid report type %d\n", kwork->report);
		return -1;
	}

	list_for_each_entry(class, &kwork->class_list, list)
		if ((class->class_init != NULL) &&
		    (class->class_init(class, session) != 0))
			return -1;

	if (kwork->cpu_list != NULL) {
		ret = perf_session__cpu_bitmap(session,
					       kwork->cpu_list,
					       kwork->cpu_bitmap);
		if (ret < 0) {
			pr_err("Invalid cpu bitmap\n");
			return -1;
		}
	}

	if (kwork->time_str != NULL) {
		ret = perf_time__parse_str(&kwork->ptime, kwork->time_str);
		if (ret != 0) {
			pr_err("Invalid time span\n");
			return -1;
		}
	}

	list_for_each_entry(evsel, &session->evlist->core.entries, core.node) {
		if (kwork->show_callchain && !evsel__has_callchain(evsel)) {
			pr_debug("Samples do not have callchains\n");
			kwork->show_callchain = 0;
			symbol_conf.use_callchain = 0;
		}
	}

	return 0;
}

static int perf_kwork__read_events(struct perf_kwork *kwork)
{
	int ret = -1;
	struct perf_session *session = NULL;

	struct perf_data data = {
		.path  = input_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = kwork->force,
	};

	session = perf_session__new(&data, &kwork->tool);
	if (IS_ERR(session)) {
		pr_debug("Error creating perf session\n");
		return PTR_ERR(session);
	}

	symbol__init(&session->header.env);

	if (perf_kwork__check_config(kwork, session) != 0)
		goto out_delete;

	if (session->tevent.pevent &&
	    tep_set_function_resolver(session->tevent.pevent,
				      machine__resolve_kernel_addr,
				      &session->machines.host) < 0) {
		pr_err("Failed to set libtraceevent function resolver\n");
		goto out_delete;
	}

	if (kwork->report == KWORK_REPORT_TIMEHIST)
		timehist_print_header();

	ret = perf_session__process_events(session);
	if (ret) {
		pr_debug("Failed to process events, error %d\n", ret);
		goto out_delete;
	}

	kwork->nr_events      = session->evlist->stats.nr_events[0];
	kwork->nr_lost_events = session->evlist->stats.total_lost;
	kwork->nr_lost_chunks = session->evlist->stats.nr_events[PERF_RECORD_LOST];

out_delete:
	perf_session__delete(session);
	return ret;
}

static void process_skipped_events(struct perf_kwork *kwork,
				   struct kwork_work *work)
{
	int i;
	unsigned long long count;

	for (i = 0; i < KWORK_TRACE_MAX; i++) {
		count = nr_list_entry(&work->atom_list[i]);
		kwork->nr_skipped_events[i] += count;
		kwork->nr_skipped_events[KWORK_TRACE_MAX] += count;
	}
}

struct kwork_work *perf_kwork_add_work(struct perf_kwork *kwork,
				       struct kwork_class *class,
				       struct kwork_work *key)
{
	struct kwork_work *work = NULL;

	work = work_new(key);
	if (work == NULL)
		return NULL;

	work_insert(&class->work_root, work, &kwork->cmp_id);
	return work;
}

static void sig_handler(int sig)
{
	/*
	 * Simply capture termination signal so that
	 * the program can continue after pause returns
	 */
	pr_debug("Capture signal %d\n", sig);
}

static int perf_kwork__report_bpf(struct perf_kwork *kwork)
{
	int ret;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	ret = perf_kwork__trace_prepare_bpf(kwork);
	if (ret)
		return -1;

	printf("Starting trace, Hit <Ctrl+C> to stop and report\n");

	perf_kwork__trace_start();

	/*
	 * a simple pause, wait here for stop signal
	 */
	pause();

	perf_kwork__trace_finish();

	perf_kwork__report_read_bpf(kwork);

	perf_kwork__report_cleanup_bpf();

	return 0;
}

static int perf_kwork__report(struct perf_kwork *kwork)
{
	int ret;
	struct rb_node *next;
	struct kwork_work *work;

	if (kwork->use_bpf)
		ret = perf_kwork__report_bpf(kwork);
	else
		ret = perf_kwork__read_events(kwork);

	if (ret != 0)
		return -1;

	perf_kwork__sort(kwork);

	setup_pager();

	ret = report_print_header(kwork);
	next = rb_first_cached(&kwork->sorted_work_root);
	while (next) {
		work = rb_entry(next, struct kwork_work, node);
		process_skipped_events(kwork, work);

		if (work->nr_atoms != 0) {
			report_print_work(kwork, work);
			if (kwork->summary) {
				kwork->all_runtime += work->total_runtime;
				kwork->all_count += work->nr_atoms;
			}
		}
		next = rb_next(next);
	}
	print_separator(ret);

	if (kwork->summary) {
		print_summary(kwork);
		print_separator(ret);
	}

	print_bad_events(kwork);
	print_skipped_events(kwork);
	printf("\n");

	return 0;
}

typedef int (*tracepoint_handler)(struct perf_tool *tool,
				  struct evsel *evsel,
				  struct perf_sample *sample,
				  struct machine *machine);

static int perf_kwork__process_tracepoint_sample(struct perf_tool *tool,
						 union perf_event *event __maybe_unused,
						 struct perf_sample *sample,
						 struct evsel *evsel,
						 struct machine *machine)
{
	int err = 0;

	if (evsel->handler != NULL) {
		tracepoint_handler f = evsel->handler;

		err = f(tool, evsel, sample, machine);
	}

	return err;
}

static int perf_kwork__timehist(struct perf_kwork *kwork)
{
	/*
	 * event handlers for timehist option
	 */
	kwork->tool.comm	 = perf_event__process_comm;
	kwork->tool.exit	 = perf_event__process_exit;
	kwork->tool.fork	 = perf_event__process_fork;
	kwork->tool.attr	 = perf_event__process_attr;
	kwork->tool.tracing_data = perf_event__process_tracing_data;
	kwork->tool.build_id	 = perf_event__process_build_id;
	kwork->tool.ordered_events = true;
	kwork->tool.ordering_requires_timestamps = true;
	symbol_conf.use_callchain = kwork->show_callchain;

	if (symbol__validate_sym_arguments()) {
		pr_err("Failed to validate sym arguments\n");
		return -1;
	}

	setup_pager();

	return perf_kwork__read_events(kwork);
}

static void top_calc_total_runtime(struct perf_kwork *kwork)
{
	struct kwork_class *class;
	struct kwork_work *work;
	struct rb_node *next;
	struct kwork_top_stat *stat = &kwork->top_stat;

	class = get_kwork_class(kwork, KWORK_CLASS_SCHED);
	if (!class)
		return;

	next = rb_first_cached(&class->work_root);
	while (next) {
		work = rb_entry(next, struct kwork_work, node);
		BUG_ON(work->cpu >= MAX_NR_CPUS);
		stat->cpus_runtime[work->cpu].total += work->total_runtime;
		stat->cpus_runtime[MAX_NR_CPUS].total += work->total_runtime;
		next = rb_next(next);
	}
}

static void top_calc_idle_time(struct perf_kwork *kwork,
				struct kwork_work *work)
{
	struct kwork_top_stat *stat = &kwork->top_stat;

	if (work->id == 0) {
		stat->cpus_runtime[work->cpu].idle += work->total_runtime;
		stat->cpus_runtime[MAX_NR_CPUS].idle += work->total_runtime;
	}
}

static void top_calc_irq_runtime(struct perf_kwork *kwork,
				 enum kwork_class_type type,
				 struct kwork_work *work)
{
	struct kwork_top_stat *stat = &kwork->top_stat;

	if (type == KWORK_CLASS_IRQ) {
		stat->cpus_runtime[work->cpu].irq += work->total_runtime;
		stat->cpus_runtime[MAX_NR_CPUS].irq += work->total_runtime;
	} else if (type == KWORK_CLASS_SOFTIRQ) {
		stat->cpus_runtime[work->cpu].softirq += work->total_runtime;
		stat->cpus_runtime[MAX_NR_CPUS].softirq += work->total_runtime;
	}
}

static void top_subtract_irq_runtime(struct perf_kwork *kwork,
				     struct kwork_work *work)
{
	struct kwork_class *class;
	struct kwork_work *data;
	unsigned int i;
	int irq_class_list[] = {KWORK_CLASS_IRQ, KWORK_CLASS_SOFTIRQ};

	for (i = 0; i < ARRAY_SIZE(irq_class_list); i++) {
		class = get_kwork_class(kwork, irq_class_list[i]);
		if (!class)
			continue;

		data = find_work_by_id(&class->work_root,
				       work->id, work->cpu);
		if (!data)
			continue;

		if (work->total_runtime > data->total_runtime) {
			work->total_runtime -= data->total_runtime;
			top_calc_irq_runtime(kwork, irq_class_list[i], data);
		}
	}
}

static void top_calc_cpu_usage(struct perf_kwork *kwork)
{
	struct kwork_class *class;
	struct kwork_work *work;
	struct rb_node *next;
	struct kwork_top_stat *stat = &kwork->top_stat;

	class = get_kwork_class(kwork, KWORK_CLASS_SCHED);
	if (!class)
		return;

	next = rb_first_cached(&class->work_root);
	while (next) {
		work = rb_entry(next, struct kwork_work, node);

		if (work->total_runtime == 0)
			goto next;

		__set_bit(work->cpu, stat->all_cpus_bitmap);

		top_subtract_irq_runtime(kwork, work);

		work->cpu_usage = work->total_runtime * 10000 /
			stat->cpus_runtime[work->cpu].total;

		top_calc_idle_time(kwork, work);
next:
		next = rb_next(next);
	}
}

static void top_calc_load_runtime(struct perf_kwork *kwork,
				  struct kwork_work *work)
{
	struct kwork_top_stat *stat = &kwork->top_stat;

	if (work->id != 0) {
		stat->cpus_runtime[work->cpu].load += work->total_runtime;
		stat->cpus_runtime[MAX_NR_CPUS].load += work->total_runtime;
	}
}

static void top_merge_tasks(struct perf_kwork *kwork)
{
	struct kwork_work *merged_work, *data;
	struct kwork_class *class;
	struct rb_node *node;
	int cpu;
	struct rb_root_cached merged_root = RB_ROOT_CACHED;

	class = get_kwork_class(kwork, KWORK_CLASS_SCHED);
	if (!class)
		return;

	for (;;) {
		node = rb_first_cached(&class->work_root);
		if (!node)
			break;

		rb_erase_cached(node, &class->work_root);
		data = rb_entry(node, struct kwork_work, node);

		if (!profile_name_match(kwork, data))
			continue;

		cpu = data->cpu;
		merged_work = find_work_by_id(&merged_root, data->id,
					      data->id == 0 ? cpu : -1);
		if (!merged_work) {
			work_insert(&merged_root, data, &kwork->cmp_id);
		} else {
			merged_work->total_runtime += data->total_runtime;
			merged_work->cpu_usage += data->cpu_usage;
		}

		top_calc_load_runtime(kwork, data);
	}

	work_sort(kwork, class, &merged_root);
}

static void perf_kwork__top_report(struct perf_kwork *kwork)
{
	struct kwork_work *work;
	struct rb_node *next;

	printf("\n");

	top_print_cpu_usage(kwork);
	top_print_header(kwork);
	next = rb_first_cached(&kwork->sorted_work_root);
	while (next) {
		work = rb_entry(next, struct kwork_work, node);
		process_skipped_events(kwork, work);

		if (work->total_runtime == 0)
			goto next;

		top_print_work(kwork, work);

next:
		next = rb_next(next);
	}

	printf("\n");
}

static int perf_kwork__top_bpf(struct perf_kwork *kwork)
{
	int ret;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	ret = perf_kwork__top_prepare_bpf(kwork);
	if (ret)
		return -1;

	printf("Starting trace, Hit <Ctrl+C> to stop and report\n");

	perf_kwork__top_start();

	/*
	 * a simple pause, wait here for stop signal
	 */
	pause();

	perf_kwork__top_finish();

	perf_kwork__top_read_bpf(kwork);

	perf_kwork__top_cleanup_bpf();

	return 0;

}

static int perf_kwork__top(struct perf_kwork *kwork)
{
	struct __top_cpus_runtime *cpus_runtime;
	int ret = 0;

	cpus_runtime = zalloc(sizeof(struct __top_cpus_runtime) * (MAX_NR_CPUS + 1));
	if (!cpus_runtime)
		return -1;

	kwork->top_stat.cpus_runtime = cpus_runtime;
	bitmap_zero(kwork->top_stat.all_cpus_bitmap, MAX_NR_CPUS);

	if (kwork->use_bpf)
		ret = perf_kwork__top_bpf(kwork);
	else
		ret = perf_kwork__read_events(kwork);

	if (ret)
		goto out;

	top_calc_total_runtime(kwork);
	top_calc_cpu_usage(kwork);
	top_merge_tasks(kwork);

	setup_pager();

	perf_kwork__top_report(kwork);

out:
	zfree(&kwork->top_stat.cpus_runtime);
	return ret;
}

static void setup_event_list(struct perf_kwork *kwork,
			     const struct option *options,
			     const char * const usage_msg[])
{
	int i;
	struct kwork_class *class;
	char *tmp, *tok, *str;

	/*
	 * set default events list if not specified
	 */
	if (kwork->event_list_str == NULL)
		kwork->event_list_str = "irq, softirq, workqueue";

	str = strdup(kwork->event_list_str);
	for (tok = strtok_r(str, ", ", &tmp);
	     tok; tok = strtok_r(NULL, ", ", &tmp)) {
		for (i = 0; i < KWORK_CLASS_MAX; i++) {
			class = kwork_class_supported_list[i];
			if (strcmp(tok, class->name) == 0) {
				list_add_tail(&class->list, &kwork->class_list);
				break;
			}
		}
		if (i == KWORK_CLASS_MAX) {
			usage_with_options_msg(usage_msg, options,
					       "Unknown --event key: `%s'", tok);
		}
	}
	free(str);

	pr_debug("Config event list:");
	list_for_each_entry(class, &kwork->class_list, list)
		pr_debug(" %s", class->name);
	pr_debug("\n");
}

static int perf_kwork__record(struct perf_kwork *kwork,
			      int argc, const char **argv)
{
	const char **rec_argv;
	unsigned int rec_argc, i, j;
	struct kwork_class *class;

	const char *const record_args[] = {
		"record",
		"-a",
		"-R",
		"-m", "1024",
		"-c", "1",
	};

	rec_argc = ARRAY_SIZE(record_args) + argc - 1;

	list_for_each_entry(class, &kwork->class_list, list)
		rec_argc += 2 * class->nr_tracepoints;

	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = strdup(record_args[i]);

	list_for_each_entry(class, &kwork->class_list, list) {
		for (j = 0; j < class->nr_tracepoints; j++) {
			rec_argv[i++] = strdup("-e");
			rec_argv[i++] = strdup(class->tp_handlers[j].name);
		}
	}

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	pr_debug("record comm: ");
	for (j = 0; j < rec_argc; j++)
		pr_debug("%s ", rec_argv[j]);
	pr_debug("\n");

	return cmd_record(i, rec_argv);
}

int cmd_kwork(int argc, const char **argv)
{
	static struct perf_kwork kwork = {
		.class_list          = LIST_HEAD_INIT(kwork.class_list),
		.tool = {
			.mmap		= perf_event__process_mmap,
			.mmap2		= perf_event__process_mmap2,
			.sample		= perf_kwork__process_tracepoint_sample,
			.ordered_events = true,
		},
		.atom_page_list      = LIST_HEAD_INIT(kwork.atom_page_list),
		.sort_list           = LIST_HEAD_INIT(kwork.sort_list),
		.cmp_id              = LIST_HEAD_INIT(kwork.cmp_id),
		.sorted_work_root    = RB_ROOT_CACHED,
		.tp_handler          = NULL,
		.profile_name        = NULL,
		.cpu_list            = NULL,
		.time_str            = NULL,
		.force               = false,
		.event_list_str      = NULL,
		.summary             = false,
		.sort_order          = NULL,
		.show_callchain      = false,
		.max_stack           = 5,
		.timestart           = 0,
		.timeend             = 0,
		.nr_events           = 0,
		.nr_lost_chunks      = 0,
		.nr_lost_events      = 0,
		.all_runtime         = 0,
		.all_count           = 0,
		.nr_skipped_events   = { 0 },
	};
	static const char default_report_sort_order[] = "runtime, max, count";
	static const char default_latency_sort_order[] = "avg, max, count";
	static const char default_top_sort_order[] = "rate, runtime";
	const struct option kwork_options[] = {
	OPT_INCR('v', "verbose", &verbose,
		 "be more verbose (show symbol address, etc)"),
	OPT_BOOLEAN('D', "dump-raw-trace", &dump_trace,
		    "dump raw trace in ASCII"),
	OPT_STRING('k', "kwork", &kwork.event_list_str, "kwork",
		   "list of kwork to profile (irq, softirq, workqueue, sched, etc)"),
	OPT_BOOLEAN('f', "force", &kwork.force, "don't complain, do it"),
	OPT_END()
	};
	const struct option report_options[] = {
	OPT_STRING('s', "sort", &kwork.sort_order, "key[,key2...]",
		   "sort by key(s): runtime, max, count"),
	OPT_STRING('C', "cpu", &kwork.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_STRING('n', "name", &kwork.profile_name, "name",
		   "event name to profile"),
	OPT_STRING(0, "time", &kwork.time_str, "str",
		   "Time span for analysis (start,stop)"),
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
	OPT_BOOLEAN('S', "with-summary", &kwork.summary,
		    "Show summary with statistics"),
#ifdef HAVE_BPF_SKEL
	OPT_BOOLEAN('b', "use-bpf", &kwork.use_bpf,
		    "Use BPF to measure kwork runtime"),
#endif
	OPT_PARENT(kwork_options)
	};
	const struct option latency_options[] = {
	OPT_STRING('s', "sort", &kwork.sort_order, "key[,key2...]",
		   "sort by key(s): avg, max, count"),
	OPT_STRING('C', "cpu", &kwork.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_STRING('n', "name", &kwork.profile_name, "name",
		   "event name to profile"),
	OPT_STRING(0, "time", &kwork.time_str, "str",
		   "Time span for analysis (start,stop)"),
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
#ifdef HAVE_BPF_SKEL
	OPT_BOOLEAN('b', "use-bpf", &kwork.use_bpf,
		    "Use BPF to measure kwork latency"),
#endif
	OPT_PARENT(kwork_options)
	};
	const struct option timehist_options[] = {
	OPT_STRING('k', "vmlinux", &symbol_conf.vmlinux_name,
		   "file", "vmlinux pathname"),
	OPT_STRING(0, "kallsyms", &symbol_conf.kallsyms_name,
		   "file", "kallsyms pathname"),
	OPT_BOOLEAN('g', "call-graph", &kwork.show_callchain,
		    "Display call chains if present"),
	OPT_UINTEGER(0, "max-stack", &kwork.max_stack,
		   "Maximum number of functions to display backtrace."),
	OPT_STRING(0, "symfs", &symbol_conf.symfs, "directory",
		    "Look for files with symbols relative to this directory"),
	OPT_STRING(0, "time", &kwork.time_str, "str",
		   "Time span for analysis (start,stop)"),
	OPT_STRING('C', "cpu", &kwork.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_STRING('n', "name", &kwork.profile_name, "name",
		   "event name to profile"),
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
	OPT_PARENT(kwork_options)
	};
	const struct option top_options[] = {
	OPT_STRING('s', "sort", &kwork.sort_order, "key[,key2...]",
		   "sort by key(s): rate, runtime, tid"),
	OPT_STRING('C', "cpu", &kwork.cpu_list, "cpu",
		   "list of cpus to profile"),
	OPT_STRING('n', "name", &kwork.profile_name, "name",
		   "event name to profile"),
	OPT_STRING(0, "time", &kwork.time_str, "str",
		   "Time span for analysis (start,stop)"),
	OPT_STRING('i', "input", &input_name, "file",
		   "input file name"),
#ifdef HAVE_BPF_SKEL
	OPT_BOOLEAN('b', "use-bpf", &kwork.use_bpf,
		    "Use BPF to measure task cpu usage"),
#endif
	OPT_PARENT(kwork_options)
	};
	const char *kwork_usage[] = {
		NULL,
		NULL
	};
	const char * const report_usage[] = {
		"perf kwork report [<options>]",
		NULL
	};
	const char * const latency_usage[] = {
		"perf kwork latency [<options>]",
		NULL
	};
	const char * const timehist_usage[] = {
		"perf kwork timehist [<options>]",
		NULL
	};
	const char * const top_usage[] = {
		"perf kwork top [<options>]",
		NULL
	};
	const char *const kwork_subcommands[] = {
		"record", "report", "latency", "timehist", "top", NULL
	};

	argc = parse_options_subcommand(argc, argv, kwork_options,
					kwork_subcommands, kwork_usage,
					PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(kwork_usage, kwork_options);

	sort_dimension__add(&kwork, "id", &kwork.cmp_id);

	if (strlen(argv[0]) > 2 && strstarts("record", argv[0])) {
		setup_event_list(&kwork, kwork_options, kwork_usage);
		return perf_kwork__record(&kwork, argc, argv);
	} else if (strlen(argv[0]) > 2 && strstarts("report", argv[0])) {
		kwork.sort_order = default_report_sort_order;
		if (argc > 1) {
			argc = parse_options(argc, argv, report_options, report_usage, 0);
			if (argc)
				usage_with_options(report_usage, report_options);
		}
		kwork.report = KWORK_REPORT_RUNTIME;
		setup_sorting(&kwork, report_options, report_usage);
		setup_event_list(&kwork, kwork_options, kwork_usage);
		return perf_kwork__report(&kwork);
	} else if (strlen(argv[0]) > 2 && strstarts("latency", argv[0])) {
		kwork.sort_order = default_latency_sort_order;
		if (argc > 1) {
			argc = parse_options(argc, argv, latency_options, latency_usage, 0);
			if (argc)
				usage_with_options(latency_usage, latency_options);
		}
		kwork.report = KWORK_REPORT_LATENCY;
		setup_sorting(&kwork, latency_options, latency_usage);
		setup_event_list(&kwork, kwork_options, kwork_usage);
		return perf_kwork__report(&kwork);
	} else if (strlen(argv[0]) > 2 && strstarts("timehist", argv[0])) {
		if (argc > 1) {
			argc = parse_options(argc, argv, timehist_options, timehist_usage, 0);
			if (argc)
				usage_with_options(timehist_usage, timehist_options);
		}
		kwork.report = KWORK_REPORT_TIMEHIST;
		setup_event_list(&kwork, kwork_options, kwork_usage);
		return perf_kwork__timehist(&kwork);
	} else if (strlen(argv[0]) > 2 && strstarts("top", argv[0])) {
		kwork.sort_order = default_top_sort_order;
		if (argc > 1) {
			argc = parse_options(argc, argv, top_options, top_usage, 0);
			if (argc)
				usage_with_options(top_usage, top_options);
		}
		kwork.report = KWORK_REPORT_TOP;
		if (!kwork.event_list_str)
			kwork.event_list_str = "sched, irq, softirq";
		setup_event_list(&kwork, kwork_options, kwork_usage);
		setup_sorting(&kwork, top_options, top_usage);
		return perf_kwork__top(&kwork);
	} else
		usage_with_options(kwork_usage, kwork_options);

	return 0;
}
