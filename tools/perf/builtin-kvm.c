// SPDX-License-Identifier: GPL-2.0
#include "builtin.h"
#include "perf.h"

#include "util/build-id.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/mmap.h"
#include "util/term.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/session.h"
#include "util/intlist.h"
#include <subcmd/pager.h>
#include <subcmd/parse-options.h>
#include "util/trace-event.h"
#include "util/debug.h"
#include "util/tool.h"
#include "util/stat.h"
#include "util/synthetic-events.h"
#include "util/top.h"
#include "util/data.h"
#include "util/ordered-events.h"
#include "util/kvm-stat.h"
#include "util/util.h"
#include "ui/browsers/hists.h"
#include "ui/progress.h"
#include "ui/ui.h"
#include "util/string2.h"

#include <sys/prctl.h>
#ifdef HAVE_TIMERFD_SUPPORT
#include <sys/timerfd.h>
#endif
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <termios.h>
#include <semaphore.h>
#include <signal.h>
#include <math.h>
#include <perf/mmap.h>

#if defined(HAVE_KVM_STAT_SUPPORT) && defined(HAVE_LIBTRACEEVENT)
#define GET_EVENT_KEY(func, field)					\
static u64 get_event_ ##func(struct kvm_event *event, int vcpu)		\
{									\
	if (vcpu == -1)							\
		return event->total.field;				\
									\
	if (vcpu >= event->max_vcpu)					\
		return 0;						\
									\
	return event->vcpu[vcpu].field;					\
}

#define COMPARE_EVENT_KEY(func, field)					\
GET_EVENT_KEY(func, field)						\
static int64_t cmp_event_ ## func(struct kvm_event *one,		\
			      struct kvm_event *two, int vcpu)		\
{									\
	return get_event_ ##func(one, vcpu) -				\
	       get_event_ ##func(two, vcpu);				\
}

COMPARE_EVENT_KEY(time, time);
COMPARE_EVENT_KEY(max, stats.max);
COMPARE_EVENT_KEY(min, stats.min);
COMPARE_EVENT_KEY(count, stats.n);
COMPARE_EVENT_KEY(mean, stats.mean);

struct kvm_hists {
	struct hists		hists;
	struct perf_hpp_list	list;
};

struct kvm_dimension {
	const char *name;
	const char *header;
	int width;
	int64_t (*cmp)(struct perf_hpp_fmt *fmt, struct hist_entry *left,
		       struct hist_entry *right);
	int (*entry)(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		     struct hist_entry *he);
};

struct kvm_fmt {
	struct perf_hpp_fmt	fmt;
	struct kvm_dimension	*dim;
};

static struct kvm_hists kvm_hists;

static int64_t ev_name_cmp(struct perf_hpp_fmt *fmt __maybe_unused,
			   struct hist_entry *left,
			   struct hist_entry *right)
{
	/* Return opposite number for sorting in alphabetical order */
	return -strcmp(left->kvm_info->name, right->kvm_info->name);
}

static int fmt_width(struct perf_hpp_fmt *fmt,
		     struct perf_hpp *hpp __maybe_unused,
		     struct hists *hists __maybe_unused);

static int ev_name_entry(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			 struct hist_entry *he)
{
	int width = fmt_width(fmt, hpp, he->hists);

	return scnprintf(hpp->buf, hpp->size, "%*s", width, he->kvm_info->name);
}

static struct kvm_dimension dim_event = {
	.header		= "Event name",
	.name		= "ev_name",
	.cmp		= ev_name_cmp,
	.entry		= ev_name_entry,
	.width		= 40,
};

#define EV_METRIC_CMP(metric)						\
static int64_t ev_cmp_##metric(struct perf_hpp_fmt *fmt __maybe_unused,	\
			       struct hist_entry *left,			\
			       struct hist_entry *right)		\
{									\
	struct kvm_event *event_left;					\
	struct kvm_event *event_right;					\
	struct perf_kvm_stat *perf_kvm;					\
									\
	event_left  = container_of(left, struct kvm_event, he);		\
	event_right = container_of(right, struct kvm_event, he);	\
									\
	perf_kvm = event_left->perf_kvm;				\
	return cmp_event_##metric(event_left, event_right,		\
				  perf_kvm->trace_vcpu);		\
}

EV_METRIC_CMP(time)
EV_METRIC_CMP(count)
EV_METRIC_CMP(max)
EV_METRIC_CMP(min)
EV_METRIC_CMP(mean)

#define EV_METRIC_ENTRY(metric)						\
static int ev_entry_##metric(struct perf_hpp_fmt *fmt,			\
			     struct perf_hpp *hpp,			\
			     struct hist_entry *he)			\
{									\
	struct kvm_event *event;					\
	int width = fmt_width(fmt, hpp, he->hists);			\
	struct perf_kvm_stat *perf_kvm;					\
									\
	event = container_of(he, struct kvm_event, he);			\
	perf_kvm = event->perf_kvm;					\
	return scnprintf(hpp->buf, hpp->size, "%*lu", width,		\
		get_event_##metric(event, perf_kvm->trace_vcpu));	\
}

EV_METRIC_ENTRY(time)
EV_METRIC_ENTRY(count)
EV_METRIC_ENTRY(max)
EV_METRIC_ENTRY(min)

static struct kvm_dimension dim_time = {
	.header		= "Time (ns)",
	.name		= "time",
	.cmp		= ev_cmp_time,
	.entry		= ev_entry_time,
	.width		= 12,
};

static struct kvm_dimension dim_count = {
	.header		= "Samples",
	.name		= "sample",
	.cmp		= ev_cmp_count,
	.entry		= ev_entry_count,
	.width		= 12,
};

static struct kvm_dimension dim_max_time = {
	.header		= "Max Time (ns)",
	.name		= "max_t",
	.cmp		= ev_cmp_max,
	.entry		= ev_entry_max,
	.width		= 14,
};

static struct kvm_dimension dim_min_time = {
	.header		= "Min Time (ns)",
	.name		= "min_t",
	.cmp		= ev_cmp_min,
	.entry		= ev_entry_min,
	.width		= 14,
};

static int ev_entry_mean(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
			 struct hist_entry *he)
{
	struct kvm_event *event;
	int width = fmt_width(fmt, hpp, he->hists);
	struct perf_kvm_stat *perf_kvm;

	event = container_of(he, struct kvm_event, he);
	perf_kvm = event->perf_kvm;
	return scnprintf(hpp->buf, hpp->size, "%*lu", width,
			 get_event_mean(event, perf_kvm->trace_vcpu));
}

static struct kvm_dimension dim_mean_time = {
	.header		= "Mean Time (ns)",
	.name		= "mean_t",
	.cmp		= ev_cmp_mean,
	.entry		= ev_entry_mean,
	.width		= 14,
};

#define PERC_STR(__s, __v)				\
({							\
	scnprintf(__s, sizeof(__s), "%.2F%%", __v);	\
	__s;						\
})

static double percent(u64 st, u64 tot)
{
	return tot ? 100. * (double) st / (double) tot : 0;
}

#define EV_METRIC_PERCENT(metric)					\
static int ev_percent_##metric(struct hist_entry *he)			\
{									\
	struct kvm_event *event;					\
	struct perf_kvm_stat *perf_kvm;					\
									\
	event = container_of(he, struct kvm_event, he);			\
	perf_kvm = event->perf_kvm;					\
									\
	return percent(get_event_##metric(event, perf_kvm->trace_vcpu),	\
		       perf_kvm->total_##metric);			\
}

EV_METRIC_PERCENT(time)
EV_METRIC_PERCENT(count)

static int ev_entry_time_precent(struct perf_hpp_fmt *fmt,
				 struct perf_hpp *hpp,
				 struct hist_entry *he)
{
	int width = fmt_width(fmt, hpp, he->hists);
	double per;
	char buf[10];

	per = ev_percent_time(he);
	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int64_t
ev_cmp_time_precent(struct perf_hpp_fmt *fmt __maybe_unused,
		    struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = ev_percent_time(left);
	per_right = ev_percent_time(right);

	return per_left - per_right;
}

static struct kvm_dimension dim_time_percent = {
	.header		= "Time%",
	.name		= "percent_time",
	.cmp		= ev_cmp_time_precent,
	.entry		= ev_entry_time_precent,
	.width		= 12,
};

static int ev_entry_count_precent(struct perf_hpp_fmt *fmt,
				  struct perf_hpp *hpp,
				  struct hist_entry *he)
{
	int width = fmt_width(fmt, hpp, he->hists);
	double per;
	char buf[10];

	per = ev_percent_count(he);
	return scnprintf(hpp->buf, hpp->size, "%*s", width, PERC_STR(buf, per));
}

static int64_t
ev_cmp_count_precent(struct perf_hpp_fmt *fmt __maybe_unused,
		     struct hist_entry *left, struct hist_entry *right)
{
	double per_left;
	double per_right;

	per_left  = ev_percent_count(left);
	per_right = ev_percent_count(right);

	return per_left - per_right;
}

static struct kvm_dimension dim_count_percent = {
	.header		= "Sample%",
	.name		= "percent_sample",
	.cmp		= ev_cmp_count_precent,
	.entry		= ev_entry_count_precent,
	.width		= 12,
};

static struct kvm_dimension *dimensions[] = {
	&dim_event,
	&dim_time,
	&dim_time_percent,
	&dim_count,
	&dim_count_percent,
	&dim_max_time,
	&dim_min_time,
	&dim_mean_time,
	NULL,
};

static int fmt_width(struct perf_hpp_fmt *fmt,
		     struct perf_hpp *hpp __maybe_unused,
		     struct hists *hists __maybe_unused)
{
	struct kvm_fmt *kvm_fmt;

	kvm_fmt = container_of(fmt, struct kvm_fmt, fmt);
	return kvm_fmt->dim->width;
}

static int fmt_header(struct perf_hpp_fmt *fmt, struct perf_hpp *hpp,
		      struct hists *hists, int line __maybe_unused,
		      int *span __maybe_unused)
{
	struct kvm_fmt *kvm_fmt;
	struct kvm_dimension *dim;
	int width = fmt_width(fmt, hpp, hists);

	kvm_fmt = container_of(fmt, struct kvm_fmt, fmt);
	dim = kvm_fmt->dim;

	return scnprintf(hpp->buf, hpp->size, "%*s", width, dim->header);
}

static bool fmt_equal(struct perf_hpp_fmt *a, struct perf_hpp_fmt *b)
{
	struct kvm_fmt *kvm_fmt_a = container_of(a, struct kvm_fmt, fmt);
	struct kvm_fmt *kvm_fmt_b = container_of(b, struct kvm_fmt, fmt);

	return kvm_fmt_a->dim == kvm_fmt_b->dim;
}

static void fmt_free(struct perf_hpp_fmt *fmt)
{
	struct kvm_fmt *kvm_fmt;

	kvm_fmt = container_of(fmt, struct kvm_fmt, fmt);
	free(kvm_fmt);
}

static struct kvm_dimension *get_dimension(const char *name)
{
	unsigned int i;

	for (i = 0; dimensions[i] != NULL; i++) {
		if (!strcmp(dimensions[i]->name, name))
			return dimensions[i];
	}

	return NULL;
}

static struct kvm_fmt *get_format(const char *name)
{
	struct kvm_dimension *dim = get_dimension(name);
	struct kvm_fmt *kvm_fmt;
	struct perf_hpp_fmt *fmt;

	if (!dim)
		return NULL;

	kvm_fmt = zalloc(sizeof(*kvm_fmt));
	if (!kvm_fmt)
		return NULL;

	kvm_fmt->dim = dim;

	fmt = &kvm_fmt->fmt;
	INIT_LIST_HEAD(&fmt->list);
	INIT_LIST_HEAD(&fmt->sort_list);
	fmt->cmp	= dim->cmp;
	fmt->sort	= dim->cmp;
	fmt->color	= NULL;
	fmt->entry	= dim->entry;
	fmt->header	= fmt_header;
	fmt->width	= fmt_width;
	fmt->collapse	= dim->cmp;
	fmt->equal	= fmt_equal;
	fmt->free	= fmt_free;

	return kvm_fmt;
}

static int kvm_hists__init_output(struct perf_hpp_list *hpp_list, char *name)
{
	struct kvm_fmt *kvm_fmt = get_format(name);

	if (!kvm_fmt) {
		pr_warning("Fail to find format for output field %s.\n", name);
		return -EINVAL;
	}

	perf_hpp_list__column_register(hpp_list, &kvm_fmt->fmt);
	return 0;
}

static int kvm_hists__init_sort(struct perf_hpp_list *hpp_list, char *name)
{
	struct kvm_fmt *kvm_fmt = get_format(name);

	if (!kvm_fmt) {
		pr_warning("Fail to find format for sorting %s.\n", name);
		return -EINVAL;
	}

	perf_hpp_list__register_sort_field(hpp_list, &kvm_fmt->fmt);
	return 0;
}

static int kvm_hpp_list__init(char *list,
			      struct perf_hpp_list *hpp_list,
			      int (*fn)(struct perf_hpp_list *hpp_list,
					char *name))
{
	char *tmp, *tok;
	int ret;

	if (!list || !fn)
		return 0;

	for (tok = strtok_r(list, ", ", &tmp); tok;
	     tok = strtok_r(NULL, ", ", &tmp)) {
		ret = fn(hpp_list, tok);
		if (!ret)
			continue;

		/* Handle errors */
		if (ret == -EINVAL)
			pr_err("Invalid field key: '%s'", tok);
		else if (ret == -ESRCH)
			pr_err("Unknown field key: '%s'", tok);
		else
			pr_err("Fail to initialize for field key: '%s'", tok);

		break;
	}

	return ret;
}

static int kvm_hpp_list__parse(struct perf_hpp_list *hpp_list,
			       const char *output_, const char *sort_)
{
	char *output = output_ ? strdup(output_) : NULL;
	char *sort = sort_ ? strdup(sort_) : NULL;
	int ret;

	ret = kvm_hpp_list__init(output, hpp_list, kvm_hists__init_output);
	if (ret)
		goto out;

	ret = kvm_hpp_list__init(sort, hpp_list, kvm_hists__init_sort);
	if (ret)
		goto out;

	/* Copy sort keys to output fields */
	perf_hpp__setup_output_field(hpp_list);

	/* and then copy output fields to sort keys */
	perf_hpp__append_sort_keys(hpp_list);
out:
	free(output);
	free(sort);
	return ret;
}

static int kvm_hists__init(void)
{
	kvm_hists.list.nr_header_lines = 1;
	__hists__init(&kvm_hists.hists, &kvm_hists.list);
	perf_hpp_list__init(&kvm_hists.list);
	return kvm_hpp_list__parse(&kvm_hists.list, NULL, "ev_name");
}

static int kvm_hists__reinit(const char *output, const char *sort)
{
	perf_hpp__reset_output_field(&kvm_hists.list);
	return kvm_hpp_list__parse(&kvm_hists.list, output, sort);
}
static void print_result(struct perf_kvm_stat *kvm);

#ifdef HAVE_SLANG_SUPPORT
static void kvm_browser__update_nr_entries(struct hist_browser *hb)
{
	struct rb_node *nd = rb_first_cached(&hb->hists->entries);
	u64 nr_entries = 0;

	for (; nd; nd = rb_next(nd)) {
		struct hist_entry *he = rb_entry(nd, struct hist_entry,
						 rb_node);

		if (!he->filtered)
			nr_entries++;
	}

	hb->nr_non_filtered_entries = nr_entries;
}

static int kvm_browser__title(struct hist_browser *browser,
			      char *buf, size_t size)
{
	scnprintf(buf, size, "KVM event statistics (%lu entries)",
		  browser->nr_non_filtered_entries);
	return 0;
}

static struct hist_browser*
perf_kvm_browser__new(struct hists *hists)
{
	struct hist_browser *browser = hist_browser__new(hists);

	if (browser)
		browser->title = kvm_browser__title;

	return browser;
}

static int kvm__hists_browse(struct hists *hists)
{
	struct hist_browser *browser;
	int key = -1;

	browser = perf_kvm_browser__new(hists);
	if (browser == NULL)
		return -1;

	/* reset abort key so that it can get Ctrl-C as a key */
	SLang_reset_tty();
	SLang_init_tty(0, 0, 0);

	kvm_browser__update_nr_entries(browser);

	while (1) {
		key = hist_browser__run(browser, "? - help", true, 0);

		switch (key) {
		case 'q':
			goto out;
		default:
			break;
		}
	}

out:
	hist_browser__delete(browser);
	return 0;
}

static void kvm_display(struct perf_kvm_stat *kvm)
{
	if (!use_browser)
		print_result(kvm);
	else
		kvm__hists_browse(&kvm_hists.hists);
}

#else

static void kvm_display(struct perf_kvm_stat *kvm)
{
	use_browser = 0;
	print_result(kvm);
}

#endif /* HAVE_SLANG_SUPPORT */

#endif // defined(HAVE_KVM_STAT_SUPPORT) && defined(HAVE_LIBTRACEEVENT)

static const char *get_filename_for_perf_kvm(void)
{
	const char *filename;

	if (perf_host && !perf_guest)
		filename = strdup("perf.data.host");
	else if (!perf_host && perf_guest)
		filename = strdup("perf.data.guest");
	else
		filename = strdup("perf.data.kvm");

	return filename;
}

#if defined(HAVE_KVM_STAT_SUPPORT) && defined(HAVE_LIBTRACEEVENT)

void exit_event_get_key(struct evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key)
{
	key->info = 0;
	key->key  = evsel__intval(evsel, sample, kvm_exit_reason);
}

bool kvm_exit_event(struct evsel *evsel)
{
	return evsel__name_is(evsel, kvm_exit_trace);
}

bool exit_event_begin(struct evsel *evsel,
		      struct perf_sample *sample, struct event_key *key)
{
	if (kvm_exit_event(evsel)) {
		exit_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

bool kvm_entry_event(struct evsel *evsel)
{
	return evsel__name_is(evsel, kvm_entry_trace);
}

bool exit_event_end(struct evsel *evsel,
		    struct perf_sample *sample __maybe_unused,
		    struct event_key *key __maybe_unused)
{
	return kvm_entry_event(evsel);
}

static const char *get_exit_reason(struct perf_kvm_stat *kvm,
				   struct exit_reasons_table *tbl,
				   u64 exit_code)
{
	while (tbl->reason != NULL) {
		if (tbl->exit_code == exit_code)
			return tbl->reason;
		tbl++;
	}

	pr_err("unknown kvm exit code:%lld on %s\n",
		(unsigned long long)exit_code, kvm->exit_reasons_isa);
	return "UNKNOWN";
}

void exit_event_decode_key(struct perf_kvm_stat *kvm,
			   struct event_key *key,
			   char *decode)
{
	const char *exit_reason = get_exit_reason(kvm, key->exit_reasons,
						  key->key);

	scnprintf(decode, KVM_EVENT_NAME_LEN, "%s", exit_reason);
}

static bool register_kvm_events_ops(struct perf_kvm_stat *kvm)
{
	struct kvm_reg_events_ops *events_ops = kvm_reg_events_ops;

	for (events_ops = kvm_reg_events_ops; events_ops->name; events_ops++) {
		if (!strcmp(events_ops->name, kvm->report_event)) {
			kvm->events_ops = events_ops->ops;
			return true;
		}
	}

	return false;
}

struct vcpu_event_record {
	int vcpu_id;
	u64 start_time;
	struct kvm_event *last_event;
};

#ifdef HAVE_TIMERFD_SUPPORT
static void clear_events_cache_stats(void)
{
	struct rb_root_cached *root;
	struct rb_node *nd;
	struct kvm_event *event;
	int i;

	if (hists__has(&kvm_hists.hists, need_collapse))
		root = &kvm_hists.hists.entries_collapsed;
	else
		root = kvm_hists.hists.entries_in;

	for (nd = rb_first_cached(root); nd; nd = rb_next(nd)) {
		struct hist_entry *he;

		he = rb_entry(nd, struct hist_entry, rb_node_in);
		event = container_of(he, struct kvm_event, he);

		/* reset stats for event */
		event->total.time = 0;
		init_stats(&event->total.stats);

		for (i = 0; i < event->max_vcpu; ++i) {
			event->vcpu[i].time = 0;
			init_stats(&event->vcpu[i].stats);
		}
	}
}
#endif

static bool kvm_event_expand(struct kvm_event *event, int vcpu_id)
{
	int old_max_vcpu = event->max_vcpu;
	void *prev;

	if (vcpu_id < event->max_vcpu)
		return true;

	while (event->max_vcpu <= vcpu_id)
		event->max_vcpu += DEFAULT_VCPU_NUM;

	prev = event->vcpu;
	event->vcpu = realloc(event->vcpu,
			      event->max_vcpu * sizeof(*event->vcpu));
	if (!event->vcpu) {
		free(prev);
		pr_err("Not enough memory\n");
		return false;
	}

	memset(event->vcpu + old_max_vcpu, 0,
	       (event->max_vcpu - old_max_vcpu) * sizeof(*event->vcpu));
	return true;
}

static void *kvm_he_zalloc(size_t size)
{
	struct kvm_event *kvm_ev;

	kvm_ev = zalloc(size + sizeof(*kvm_ev));
	if (!kvm_ev)
		return NULL;

	init_stats(&kvm_ev->total.stats);
	hists__inc_nr_samples(&kvm_hists.hists, 0);
	return &kvm_ev->he;
}

static void kvm_he_free(void *he)
{
	struct kvm_event *kvm_ev;

	kvm_ev = container_of(he, struct kvm_event, he);
	free(kvm_ev);
}

static struct hist_entry_ops kvm_ev_entry_ops = {
	.new	= kvm_he_zalloc,
	.free	= kvm_he_free,
};

static struct kvm_event *find_create_kvm_event(struct perf_kvm_stat *kvm,
					       struct event_key *key,
					       struct perf_sample *sample)
{
	struct kvm_event *event;
	struct hist_entry *he;
	struct kvm_info *ki;

	BUG_ON(key->key == INVALID_KEY);

	ki = kvm_info__new();
	if (!ki) {
		pr_err("Failed to allocate kvm info\n");
		return NULL;
	}

	kvm->events_ops->decode_key(kvm, key, ki->name);
	he = hists__add_entry_ops(&kvm_hists.hists, &kvm_ev_entry_ops,
				  &kvm->al, NULL, NULL, NULL, ki, sample, true);
	if (he == NULL) {
		pr_err("Failed to allocate hist entry\n");
		free(ki);
		return NULL;
	}

	event = container_of(he, struct kvm_event, he);
	if (!event->perf_kvm) {
		event->perf_kvm = kvm;
		event->key = *key;
	}

	return event;
}

static bool handle_begin_event(struct perf_kvm_stat *kvm,
			       struct vcpu_event_record *vcpu_record,
			       struct event_key *key,
			       struct perf_sample *sample)
{
	struct kvm_event *event = NULL;

	if (key->key != INVALID_KEY)
		event = find_create_kvm_event(kvm, key, sample);

	vcpu_record->last_event = event;
	vcpu_record->start_time = sample->time;
	return true;
}

static void
kvm_update_event_stats(struct kvm_event_stats *kvm_stats, u64 time_diff)
{
	kvm_stats->time += time_diff;
	update_stats(&kvm_stats->stats, time_diff);
}

static double kvm_event_rel_stddev(int vcpu_id, struct kvm_event *event)
{
	struct kvm_event_stats *kvm_stats = &event->total;

	if (vcpu_id != -1)
		kvm_stats = &event->vcpu[vcpu_id];

	return rel_stddev_stats(stddev_stats(&kvm_stats->stats),
				avg_stats(&kvm_stats->stats));
}

static bool update_kvm_event(struct perf_kvm_stat *kvm,
			     struct kvm_event *event, int vcpu_id,
			     u64 time_diff)
{
	/* Update overall statistics */
	kvm->total_count++;
	kvm->total_time += time_diff;

	if (vcpu_id == -1) {
		kvm_update_event_stats(&event->total, time_diff);
		return true;
	}

	if (!kvm_event_expand(event, vcpu_id))
		return false;

	kvm_update_event_stats(&event->vcpu[vcpu_id], time_diff);
	return true;
}

static bool is_child_event(struct perf_kvm_stat *kvm,
			   struct evsel *evsel,
			   struct perf_sample *sample,
			   struct event_key *key)
{
	struct child_event_ops *child_ops;

	child_ops = kvm->events_ops->child_ops;

	if (!child_ops)
		return false;

	for (; child_ops->name; child_ops++) {
		if (evsel__name_is(evsel, child_ops->name)) {
			child_ops->get_key(evsel, sample, key);
			return true;
		}
	}

	return false;
}

static bool handle_child_event(struct perf_kvm_stat *kvm,
			       struct vcpu_event_record *vcpu_record,
			       struct event_key *key,
			       struct perf_sample *sample)
{
	struct kvm_event *event = NULL;

	if (key->key != INVALID_KEY)
		event = find_create_kvm_event(kvm, key, sample);

	vcpu_record->last_event = event;

	return true;
}

static bool skip_event(const char *event)
{
	const char * const *skip_events;

	for (skip_events = kvm_skip_events; *skip_events; skip_events++)
		if (!strcmp(event, *skip_events))
			return true;

	return false;
}

static bool handle_end_event(struct perf_kvm_stat *kvm,
			     struct vcpu_event_record *vcpu_record,
			     struct event_key *key,
			     struct perf_sample *sample)
{
	struct kvm_event *event;
	u64 time_begin, time_diff;
	int vcpu;

	if (kvm->trace_vcpu == -1)
		vcpu = -1;
	else
		vcpu = vcpu_record->vcpu_id;

	event = vcpu_record->last_event;
	time_begin = vcpu_record->start_time;

	/* The begin event is not caught. */
	if (!time_begin)
		return true;

	/*
	 * In some case, the 'begin event' only records the start timestamp,
	 * the actual event is recognized in the 'end event' (e.g. mmio-event).
	 */

	/* Both begin and end events did not get the key. */
	if (!event && key->key == INVALID_KEY)
		return true;

	if (!event)
		event = find_create_kvm_event(kvm, key, sample);

	if (!event)
		return false;

	vcpu_record->last_event = NULL;
	vcpu_record->start_time = 0;

	/* seems to happen once in a while during live mode */
	if (sample->time < time_begin) {
		pr_debug("End time before begin time; skipping event.\n");
		return true;
	}

	time_diff = sample->time - time_begin;

	if (kvm->duration && time_diff > kvm->duration) {
		char decode[KVM_EVENT_NAME_LEN];

		kvm->events_ops->decode_key(kvm, &event->key, decode);
		if (!skip_event(decode)) {
			pr_info("%" PRIu64 " VM %d, vcpu %d: %s event took %" PRIu64 "usec\n",
				 sample->time, sample->pid, vcpu_record->vcpu_id,
				 decode, time_diff / NSEC_PER_USEC);
		}
	}

	return update_kvm_event(kvm, event, vcpu, time_diff);
}

static
struct vcpu_event_record *per_vcpu_record(struct thread *thread,
					  struct evsel *evsel,
					  struct perf_sample *sample)
{
	/* Only kvm_entry records vcpu id. */
	if (!thread__priv(thread) && kvm_entry_event(evsel)) {
		struct vcpu_event_record *vcpu_record;

		vcpu_record = zalloc(sizeof(*vcpu_record));
		if (!vcpu_record) {
			pr_err("%s: Not enough memory\n", __func__);
			return NULL;
		}

		vcpu_record->vcpu_id = evsel__intval(evsel, sample, vcpu_id_str);
		thread__set_priv(thread, vcpu_record);
	}

	return thread__priv(thread);
}

static bool handle_kvm_event(struct perf_kvm_stat *kvm,
			     struct thread *thread,
			     struct evsel *evsel,
			     struct perf_sample *sample)
{
	struct vcpu_event_record *vcpu_record;
	struct event_key key = { .key = INVALID_KEY,
				 .exit_reasons = kvm->exit_reasons };

	vcpu_record = per_vcpu_record(thread, evsel, sample);
	if (!vcpu_record)
		return true;

	/* only process events for vcpus user cares about */
	if ((kvm->trace_vcpu != -1) &&
	    (kvm->trace_vcpu != vcpu_record->vcpu_id))
		return true;

	if (kvm->events_ops->is_begin_event(evsel, sample, &key))
		return handle_begin_event(kvm, vcpu_record, &key, sample);

	if (is_child_event(kvm, evsel, sample, &key))
		return handle_child_event(kvm, vcpu_record, &key, sample);

	if (kvm->events_ops->is_end_event(evsel, sample, &key))
		return handle_end_event(kvm, vcpu_record, &key, sample);

	return true;
}

static bool is_valid_key(struct perf_kvm_stat *kvm)
{
	static const char *key_array[] = {
		"ev_name", "sample", "time", "max_t", "min_t", "mean_t",
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(key_array); i++)
		if (!strcmp(key_array[i], kvm->sort_key))
			return true;

	pr_err("Unsupported sort key: %s\n", kvm->sort_key);
	return false;
}

static bool event_is_valid(struct kvm_event *event, int vcpu)
{
	return !!get_event_count(event, vcpu);
}

static int filter_cb(struct hist_entry *he, void *arg __maybe_unused)
{
	struct kvm_event *event;
	struct perf_kvm_stat *perf_kvm;

	event = container_of(he, struct kvm_event, he);
	perf_kvm = event->perf_kvm;
	if (!event_is_valid(event, perf_kvm->trace_vcpu))
		he->filtered = 1;
	else
		he->filtered = 0;
	return 0;
}

static void sort_result(struct perf_kvm_stat *kvm)
{
	struct ui_progress prog;
	const char *output_columns = "ev_name,sample,percent_sample,"
				     "time,percent_time,max_t,min_t,mean_t";

	kvm_hists__reinit(output_columns, kvm->sort_key);
	ui_progress__init(&prog, kvm_hists.hists.nr_entries, "Sorting...");
	hists__collapse_resort(&kvm_hists.hists, NULL);
	hists__output_resort_cb(&kvm_hists.hists, NULL, filter_cb);
	ui_progress__finish();
}

static void print_vcpu_info(struct perf_kvm_stat *kvm)
{
	int vcpu = kvm->trace_vcpu;

	pr_info("Analyze events for ");

	if (kvm->opts.target.system_wide)
		pr_info("all VMs, ");
	else if (kvm->opts.target.pid)
		pr_info("pid(s) %s, ", kvm->opts.target.pid);
	else
		pr_info("dazed and confused on what is monitored, ");

	if (vcpu == -1)
		pr_info("all VCPUs:\n\n");
	else
		pr_info("VCPU %d:\n\n", vcpu);
}

static void show_timeofday(void)
{
	char date[64];
	struct timeval tv;
	struct tm ltime;

	gettimeofday(&tv, NULL);
	if (localtime_r(&tv.tv_sec, &ltime)) {
		strftime(date, sizeof(date), "%H:%M:%S", &ltime);
		pr_info("%s.%06ld", date, tv.tv_usec);
	} else
		pr_info("00:00:00.000000");

	return;
}

static void print_result(struct perf_kvm_stat *kvm)
{
	char decode[KVM_EVENT_NAME_LEN];
	struct kvm_event *event;
	int vcpu = kvm->trace_vcpu;
	struct rb_node *nd;

	if (kvm->live) {
		puts(CONSOLE_CLEAR);
		show_timeofday();
	}

	pr_info("\n\n");
	print_vcpu_info(kvm);
	pr_info("%*s ", KVM_EVENT_NAME_LEN, kvm->events_ops->name);
	pr_info("%10s ", "Samples");
	pr_info("%9s ", "Samples%");

	pr_info("%9s ", "Time%");
	pr_info("%11s ", "Min Time");
	pr_info("%11s ", "Max Time");
	pr_info("%16s ", "Avg time");
	pr_info("\n\n");

	for (nd = rb_first_cached(&kvm_hists.hists.entries); nd; nd = rb_next(nd)) {
		struct hist_entry *he;
		u64 ecount, etime, max, min;

		he = rb_entry(nd, struct hist_entry, rb_node);
		if (he->filtered)
			continue;

		event = container_of(he, struct kvm_event, he);
		ecount = get_event_count(event, vcpu);
		etime = get_event_time(event, vcpu);
		max = get_event_max(event, vcpu);
		min = get_event_min(event, vcpu);

		kvm->events_ops->decode_key(kvm, &event->key, decode);
		pr_info("%*s ", KVM_EVENT_NAME_LEN, decode);
		pr_info("%10llu ", (unsigned long long)ecount);
		pr_info("%8.2f%% ", (double)ecount / kvm->total_count * 100);
		pr_info("%8.2f%% ", (double)etime / kvm->total_time * 100);
		pr_info("%9.2fus ", (double)min / NSEC_PER_USEC);
		pr_info("%9.2fus ", (double)max / NSEC_PER_USEC);
		pr_info("%9.2fus ( +-%7.2f%% )", (double)etime / ecount / NSEC_PER_USEC,
			kvm_event_rel_stddev(vcpu, event));
		pr_info("\n");
	}

	pr_info("\nTotal Samples:%" PRIu64 ", Total events handled time:%.2fus.\n\n",
		kvm->total_count, kvm->total_time / (double)NSEC_PER_USEC);

	if (kvm->lost_events)
		pr_info("\nLost events: %" PRIu64 "\n\n", kvm->lost_events);
}

#if defined(HAVE_TIMERFD_SUPPORT) && defined(HAVE_LIBTRACEEVENT)
static int process_lost_event(struct perf_tool *tool,
			      union perf_event *event __maybe_unused,
			      struct perf_sample *sample __maybe_unused,
			      struct machine *machine __maybe_unused)
{
	struct perf_kvm_stat *kvm = container_of(tool, struct perf_kvm_stat, tool);

	kvm->lost_events++;
	return 0;
}
#endif

static bool skip_sample(struct perf_kvm_stat *kvm,
			struct perf_sample *sample)
{
	if (kvm->pid_list && intlist__find(kvm->pid_list, sample->pid) == NULL)
		return true;

	return false;
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct evsel *evsel,
				struct machine *machine)
{
	int err = 0;
	struct thread *thread;
	struct perf_kvm_stat *kvm = container_of(tool, struct perf_kvm_stat,
						 tool);

	if (skip_sample(kvm, sample))
		return 0;

	if (machine__resolve(machine, &kvm->al, sample) < 0) {
		pr_warning("Fail to resolve address location, skip sample.\n");
		return 0;
	}

	thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (!handle_kvm_event(kvm, thread, evsel, sample))
		err = -1;

	thread__put(thread);
	return err;
}

static int cpu_isa_config(struct perf_kvm_stat *kvm)
{
	char buf[128], *cpuid;
	int err;

	if (kvm->live) {
		err = get_cpuid(buf, sizeof(buf));
		if (err != 0) {
			pr_err("Failed to look up CPU type: %s\n",
			       str_error_r(err, buf, sizeof(buf)));
			return -err;
		}
		cpuid = buf;
	} else
		cpuid = kvm->session->header.env.cpuid;

	if (!cpuid) {
		pr_err("Failed to look up CPU type\n");
		return -EINVAL;
	}

	err = cpu_isa_init(kvm, cpuid);
	if (err == -ENOTSUP)
		pr_err("CPU %s is not supported.\n", cpuid);

	return err;
}

static bool verify_vcpu(int vcpu)
{
	if (vcpu != -1 && vcpu < 0) {
		pr_err("Invalid vcpu:%d.\n", vcpu);
		return false;
	}

	return true;
}

#if defined(HAVE_TIMERFD_SUPPORT) && defined(HAVE_LIBTRACEEVENT)
/* keeping the max events to a modest level to keep
 * the processing of samples per mmap smooth.
 */
#define PERF_KVM__MAX_EVENTS_PER_MMAP  25

static s64 perf_kvm__mmap_read_idx(struct perf_kvm_stat *kvm, int idx,
				   u64 *mmap_time)
{
	struct evlist *evlist = kvm->evlist;
	union perf_event *event;
	struct mmap *md;
	u64 timestamp;
	s64 n = 0;
	int err;

	*mmap_time = ULLONG_MAX;
	md = &evlist->mmap[idx];
	err = perf_mmap__read_init(&md->core);
	if (err < 0)
		return (err == -EAGAIN) ? 0 : -1;

	while ((event = perf_mmap__read_event(&md->core)) != NULL) {
		err = evlist__parse_sample_timestamp(evlist, event, &timestamp);
		if (err) {
			perf_mmap__consume(&md->core);
			pr_err("Failed to parse sample\n");
			return -1;
		}

		err = perf_session__queue_event(kvm->session, event, timestamp, 0, NULL);
		/*
		 * FIXME: Here we can't consume the event, as perf_session__queue_event will
		 *        point to it, and it'll get possibly overwritten by the kernel.
		 */
		perf_mmap__consume(&md->core);

		if (err) {
			pr_err("Failed to enqueue sample: %d\n", err);
			return -1;
		}

		/* save time stamp of our first sample for this mmap */
		if (n == 0)
			*mmap_time = timestamp;

		/* limit events per mmap handled all at once */
		n++;
		if (n == PERF_KVM__MAX_EVENTS_PER_MMAP)
			break;
	}

	perf_mmap__read_done(&md->core);
	return n;
}

static int perf_kvm__mmap_read(struct perf_kvm_stat *kvm)
{
	int i, err, throttled = 0;
	s64 n, ntotal = 0;
	u64 flush_time = ULLONG_MAX, mmap_time;

	for (i = 0; i < kvm->evlist->core.nr_mmaps; i++) {
		n = perf_kvm__mmap_read_idx(kvm, i, &mmap_time);
		if (n < 0)
			return -1;

		/* flush time is going to be the minimum of all the individual
		 * mmap times. Essentially, we flush all the samples queued up
		 * from the last pass under our minimal start time -- that leaves
		 * a very small race for samples to come in with a lower timestamp.
		 * The ioctl to return the perf_clock timestamp should close the
		 * race entirely.
		 */
		if (mmap_time < flush_time)
			flush_time = mmap_time;

		ntotal += n;
		if (n == PERF_KVM__MAX_EVENTS_PER_MMAP)
			throttled = 1;
	}

	/* flush queue after each round in which we processed events */
	if (ntotal) {
		struct ordered_events *oe = &kvm->session->ordered_events;

		oe->next_flush = flush_time;
		err = ordered_events__flush(oe, OE_FLUSH__ROUND);
		if (err) {
			if (kvm->lost_events)
				pr_info("\nLost events: %" PRIu64 "\n\n",
					kvm->lost_events);
			return err;
		}
	}

	return throttled;
}

static volatile int done;

static void sig_handler(int sig __maybe_unused)
{
	done = 1;
}

static int perf_kvm__timerfd_create(struct perf_kvm_stat *kvm)
{
	struct itimerspec new_value;
	int rc = -1;

	kvm->timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	if (kvm->timerfd < 0) {
		pr_err("timerfd_create failed\n");
		goto out;
	}

	new_value.it_value.tv_sec = kvm->display_time;
	new_value.it_value.tv_nsec = 0;
	new_value.it_interval.tv_sec = kvm->display_time;
	new_value.it_interval.tv_nsec = 0;

	if (timerfd_settime(kvm->timerfd, 0, &new_value, NULL) != 0) {
		pr_err("timerfd_settime failed: %d\n", errno);
		close(kvm->timerfd);
		goto out;
	}

	rc = 0;
out:
	return rc;
}

static int perf_kvm__handle_timerfd(struct perf_kvm_stat *kvm)
{
	uint64_t c;
	int rc;

	rc = read(kvm->timerfd, &c, sizeof(uint64_t));
	if (rc < 0) {
		if (errno == EAGAIN)
			return 0;

		pr_err("Failed to read timer fd: %d\n", errno);
		return -1;
	}

	if (rc != sizeof(uint64_t)) {
		pr_err("Error reading timer fd - invalid size returned\n");
		return -1;
	}

	if (c != 1)
		pr_debug("Missed timer beats: %" PRIu64 "\n", c-1);

	/* update display */
	sort_result(kvm);
	print_result(kvm);

	/* Reset sort list to "ev_name" */
	kvm_hists__reinit(NULL, "ev_name");

	/* reset counts */
	clear_events_cache_stats();
	kvm->total_count = 0;
	kvm->total_time = 0;
	kvm->lost_events = 0;

	return 0;
}

static int fd_set_nonblock(int fd)
{
	long arg = 0;

	arg = fcntl(fd, F_GETFL);
	if (arg < 0) {
		pr_err("Failed to get current flags for fd %d\n", fd);
		return -1;
	}

	if (fcntl(fd, F_SETFL, arg | O_NONBLOCK) < 0) {
		pr_err("Failed to set non-block option on fd %d\n", fd);
		return -1;
	}

	return 0;
}

static int perf_kvm__handle_stdin(void)
{
	int c;

	c = getc(stdin);
	if (c == 'q')
		return 1;

	return 0;
}

static int kvm_events_live_report(struct perf_kvm_stat *kvm)
{
	int nr_stdin, ret, err = -EINVAL;
	struct termios save;

	/* live flag must be set first */
	kvm->live = true;

	ret = cpu_isa_config(kvm);
	if (ret < 0)
		return ret;

	if (!verify_vcpu(kvm->trace_vcpu) ||
	    !is_valid_key(kvm) ||
	    !register_kvm_events_ops(kvm)) {
		goto out;
	}

	set_term_quiet_input(&save);

	kvm_hists__init();

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	/* add timer fd */
	if (perf_kvm__timerfd_create(kvm) < 0) {
		err = -1;
		goto out;
	}

	if (evlist__add_pollfd(kvm->evlist, kvm->timerfd) < 0)
		goto out;

	nr_stdin = evlist__add_pollfd(kvm->evlist, fileno(stdin));
	if (nr_stdin < 0)
		goto out;

	if (fd_set_nonblock(fileno(stdin)) != 0)
		goto out;

	/* everything is good - enable the events and process */
	evlist__enable(kvm->evlist);

	while (!done) {
		struct fdarray *fda = &kvm->evlist->core.pollfd;
		int rc;

		rc = perf_kvm__mmap_read(kvm);
		if (rc < 0)
			break;

		err = perf_kvm__handle_timerfd(kvm);
		if (err)
			goto out;

		if (fda->entries[nr_stdin].revents & POLLIN)
			done = perf_kvm__handle_stdin();

		if (!rc && !done)
			err = evlist__poll(kvm->evlist, 100);
	}

	evlist__disable(kvm->evlist);

	if (err == 0) {
		sort_result(kvm);
		print_result(kvm);
	}

out:
	hists__delete_entries(&kvm_hists.hists);

	if (kvm->timerfd >= 0)
		close(kvm->timerfd);

	tcsetattr(0, TCSAFLUSH, &save);
	return err;
}

static int kvm_live_open_events(struct perf_kvm_stat *kvm)
{
	int err, rc = -1;
	struct evsel *pos;
	struct evlist *evlist = kvm->evlist;
	char sbuf[STRERR_BUFSIZE];

	evlist__config(evlist, &kvm->opts, NULL);

	/*
	 * Note: exclude_{guest,host} do not apply here.
	 *       This command processes KVM tracepoints from host only
	 */
	evlist__for_each_entry(evlist, pos) {
		struct perf_event_attr *attr = &pos->core.attr;

		/* make sure these *are* set */
		evsel__set_sample_bit(pos, TID);
		evsel__set_sample_bit(pos, TIME);
		evsel__set_sample_bit(pos, CPU);
		evsel__set_sample_bit(pos, RAW);
		/* make sure these are *not*; want as small a sample as possible */
		evsel__reset_sample_bit(pos, PERIOD);
		evsel__reset_sample_bit(pos, IP);
		evsel__reset_sample_bit(pos, CALLCHAIN);
		evsel__reset_sample_bit(pos, ADDR);
		evsel__reset_sample_bit(pos, READ);
		attr->mmap = 0;
		attr->comm = 0;
		attr->task = 0;

		attr->sample_period = 1;

		attr->watermark = 0;
		attr->wakeup_events = 1000;

		/* will enable all once we are ready */
		attr->disabled = 1;
	}

	err = evlist__open(evlist);
	if (err < 0) {
		printf("Couldn't create the events: %s\n",
		       str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out;
	}

	if (evlist__mmap(evlist, kvm->opts.mmap_pages) < 0) {
		ui__error("Failed to mmap the events: %s\n",
			  str_error_r(errno, sbuf, sizeof(sbuf)));
		evlist__close(evlist);
		goto out;
	}

	rc = 0;

out:
	return rc;
}
#endif

static int read_events(struct perf_kvm_stat *kvm)
{
	int ret;

	struct perf_tool eops = {
		.sample			= process_sample_event,
		.comm			= perf_event__process_comm,
		.namespaces		= perf_event__process_namespaces,
		.ordered_events		= true,
	};
	struct perf_data file = {
		.path  = kvm->file_name,
		.mode  = PERF_DATA_MODE_READ,
		.force = kvm->force,
	};

	kvm->tool = eops;
	kvm->session = perf_session__new(&file, &kvm->tool);
	if (IS_ERR(kvm->session)) {
		pr_err("Initializing perf session failed\n");
		return PTR_ERR(kvm->session);
	}

	symbol__init(&kvm->session->header.env);

	if (!perf_session__has_traces(kvm->session, "kvm record")) {
		ret = -EINVAL;
		goto out_delete;
	}

	/*
	 * Do not use 'isa' recorded in kvm_exit tracepoint since it is not
	 * traced in the old kernel.
	 */
	ret = cpu_isa_config(kvm);
	if (ret < 0)
		goto out_delete;

	ret = perf_session__process_events(kvm->session);

out_delete:
	perf_session__delete(kvm->session);
	return ret;
}

static int parse_target_str(struct perf_kvm_stat *kvm)
{
	if (kvm->opts.target.pid) {
		kvm->pid_list = intlist__new(kvm->opts.target.pid);
		if (kvm->pid_list == NULL) {
			pr_err("Error parsing process id string\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int kvm_events_report_vcpu(struct perf_kvm_stat *kvm)
{
	int ret = -EINVAL;
	int vcpu = kvm->trace_vcpu;

	if (parse_target_str(kvm) != 0)
		goto exit;

	if (!verify_vcpu(vcpu))
		goto exit;

	if (!is_valid_key(kvm))
		goto exit;

	if (!register_kvm_events_ops(kvm))
		goto exit;

	if (kvm->use_stdio) {
		use_browser = 0;
		setup_pager();
	} else {
		use_browser = 1;
	}

	setup_browser(false);

	kvm_hists__init();

	ret = read_events(kvm);
	if (ret)
		goto exit;

	sort_result(kvm);
	kvm_display(kvm);

exit:
	hists__delete_entries(&kvm_hists.hists);
	return ret;
}

#define STRDUP_FAIL_EXIT(s)		\
	({	char *_p;		\
	_p = strdup(s);		\
		if (!_p)		\
			return -ENOMEM;	\
		_p;			\
	})

int __weak setup_kvm_events_tp(struct perf_kvm_stat *kvm __maybe_unused)
{
	return 0;
}

static int
kvm_events_record(struct perf_kvm_stat *kvm, int argc, const char **argv)
{
	unsigned int rec_argc, i, j, events_tp_size;
	const char **rec_argv;
	const char * const record_args[] = {
		"record",
		"-R",
		"-m", "1024",
		"-c", "1",
	};
	const char * const kvm_stat_record_usage[] = {
		"perf kvm stat record [<options>]",
		NULL
	};
	const char * const *events_tp;
	int ret;

	events_tp_size = 0;
	ret = setup_kvm_events_tp(kvm);
	if (ret < 0) {
		pr_err("Unable to setup the kvm tracepoints\n");
		return ret;
	}

	for (events_tp = kvm_events_tp; *events_tp; events_tp++)
		events_tp_size++;

	rec_argc = ARRAY_SIZE(record_args) + argc + 2 +
		   2 * events_tp_size;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = STRDUP_FAIL_EXIT(record_args[i]);

	for (j = 0; j < events_tp_size; j++) {
		rec_argv[i++] = "-e";
		rec_argv[i++] = STRDUP_FAIL_EXIT(kvm_events_tp[j]);
	}

	rec_argv[i++] = STRDUP_FAIL_EXIT("-o");
	rec_argv[i++] = STRDUP_FAIL_EXIT(kvm->file_name);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	set_option_flag(record_options, 'e', "event", PARSE_OPT_HIDDEN);
	set_option_flag(record_options, 0, "filter", PARSE_OPT_HIDDEN);
	set_option_flag(record_options, 'R', "raw-samples", PARSE_OPT_HIDDEN);

	set_option_flag(record_options, 'F', "freq", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 0, "group", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'g', NULL, PARSE_OPT_DISABLED);
	set_option_flag(record_options, 0, "call-graph", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'd', "data", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'T', "timestamp", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'P', "period", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'n', "no-samples", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'N', "no-buildid-cache", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'B', "no-buildid", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'G', "cgroup", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'b', "branch-any", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'j', "branch-filter", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 'W', "weight", PARSE_OPT_DISABLED);
	set_option_flag(record_options, 0, "transaction", PARSE_OPT_DISABLED);

	record_usage = kvm_stat_record_usage;
	return cmd_record(i, rec_argv);
}

static int
kvm_events_report(struct perf_kvm_stat *kvm, int argc, const char **argv)
{
	const struct option kvm_events_report_options[] = {
		OPT_STRING(0, "event", &kvm->report_event, "report event",
			   "event for reporting: vmexit, "
			   "mmio (x86 only), ioport (x86 only)"),
		OPT_INTEGER(0, "vcpu", &kvm->trace_vcpu,
			    "vcpu id to report"),
		OPT_STRING('k', "key", &kvm->sort_key, "sort-key",
			    "key for sorting: sample(sort by samples number)"
			    " time (sort by avg time)"),
		OPT_STRING('p', "pid", &kvm->opts.target.pid, "pid",
			   "analyze events only for given process id(s)"),
		OPT_BOOLEAN('f', "force", &kvm->force, "don't complain, do it"),
		OPT_BOOLEAN(0, "stdio", &kvm->use_stdio, "use the stdio interface"),
		OPT_END()
	};

	const char * const kvm_events_report_usage[] = {
		"perf kvm stat report [<options>]",
		NULL
	};

	if (argc) {
		argc = parse_options(argc, argv,
				     kvm_events_report_options,
				     kvm_events_report_usage, 0);
		if (argc)
			usage_with_options(kvm_events_report_usage,
					   kvm_events_report_options);
	}

#ifndef HAVE_SLANG_SUPPORT
	kvm->use_stdio = true;
#endif

	if (!kvm->opts.target.pid)
		kvm->opts.target.system_wide = true;

	return kvm_events_report_vcpu(kvm);
}

#if defined(HAVE_TIMERFD_SUPPORT) && defined(HAVE_LIBTRACEEVENT)
static struct evlist *kvm_live_event_list(void)
{
	struct evlist *evlist;
	char *tp, *name, *sys;
	int err = -1;
	const char * const *events_tp;

	evlist = evlist__new();
	if (evlist == NULL)
		return NULL;

	for (events_tp = kvm_events_tp; *events_tp; events_tp++) {

		tp = strdup(*events_tp);
		if (tp == NULL)
			goto out;

		/* split tracepoint into subsystem and name */
		sys = tp;
		name = strchr(tp, ':');
		if (name == NULL) {
			pr_err("Error parsing %s tracepoint: subsystem delimiter not found\n",
			       *events_tp);
			free(tp);
			goto out;
		}
		*name = '\0';
		name++;

		if (evlist__add_newtp(evlist, sys, name, NULL)) {
			pr_err("Failed to add %s tracepoint to the list\n", *events_tp);
			free(tp);
			goto out;
		}

		free(tp);
	}

	err = 0;

out:
	if (err) {
		evlist__delete(evlist);
		evlist = NULL;
	}

	return evlist;
}

static int kvm_events_live(struct perf_kvm_stat *kvm,
			   int argc, const char **argv)
{
	char errbuf[BUFSIZ];
	int err;

	const struct option live_options[] = {
		OPT_STRING('p', "pid", &kvm->opts.target.pid, "pid",
			"record events on existing process id"),
		OPT_CALLBACK('m', "mmap-pages", &kvm->opts.mmap_pages, "pages",
			"number of mmap data pages", evlist__parse_mmap_pages),
		OPT_INCR('v', "verbose", &verbose,
			"be more verbose (show counter open errors, etc)"),
		OPT_BOOLEAN('a', "all-cpus", &kvm->opts.target.system_wide,
			"system-wide collection from all CPUs"),
		OPT_UINTEGER('d', "display", &kvm->display_time,
			"time in seconds between display updates"),
		OPT_STRING(0, "event", &kvm->report_event, "report event",
			"event for reporting: "
			"vmexit, mmio (x86 only), ioport (x86 only)"),
		OPT_INTEGER(0, "vcpu", &kvm->trace_vcpu,
			"vcpu id to report"),
		OPT_STRING('k', "key", &kvm->sort_key, "sort-key",
			"key for sorting: sample(sort by samples number)"
			" time (sort by avg time)"),
		OPT_U64(0, "duration", &kvm->duration,
			"show events other than"
			" HLT (x86 only) or Wait state (s390 only)"
			" that take longer than duration usecs"),
		OPT_UINTEGER(0, "proc-map-timeout", &proc_map_timeout,
				"per thread proc mmap processing timeout in ms"),
		OPT_END()
	};
	const char * const live_usage[] = {
		"perf kvm stat live [<options>]",
		NULL
	};
	struct perf_data data = {
		.mode = PERF_DATA_MODE_WRITE,
	};


	/* event handling */
	kvm->tool.sample = process_sample_event;
	kvm->tool.comm   = perf_event__process_comm;
	kvm->tool.exit   = perf_event__process_exit;
	kvm->tool.fork   = perf_event__process_fork;
	kvm->tool.lost   = process_lost_event;
	kvm->tool.namespaces  = perf_event__process_namespaces;
	kvm->tool.ordered_events = true;
	perf_tool__fill_defaults(&kvm->tool);

	/* set defaults */
	kvm->display_time = 1;
	kvm->opts.user_interval = 1;
	kvm->opts.mmap_pages = 512;
	kvm->opts.target.uses_mmap = false;
	kvm->opts.target.uid_str = NULL;
	kvm->opts.target.uid = UINT_MAX;

	symbol__init(NULL);
	disable_buildid_cache();

	use_browser = 0;

	if (argc) {
		argc = parse_options(argc, argv, live_options,
				     live_usage, 0);
		if (argc)
			usage_with_options(live_usage, live_options);
	}

	kvm->duration *= NSEC_PER_USEC;   /* convert usec to nsec */

	/*
	 * target related setups
	 */
	err = target__validate(&kvm->opts.target);
	if (err) {
		target__strerror(&kvm->opts.target, err, errbuf, BUFSIZ);
		ui__warning("%s", errbuf);
	}

	if (target__none(&kvm->opts.target))
		kvm->opts.target.system_wide = true;


	/*
	 * generate the event list
	 */
	err = setup_kvm_events_tp(kvm);
	if (err < 0) {
		pr_err("Unable to setup the kvm tracepoints\n");
		return err;
	}

	kvm->evlist = kvm_live_event_list();
	if (kvm->evlist == NULL) {
		err = -1;
		goto out;
	}

	if (evlist__create_maps(kvm->evlist, &kvm->opts.target) < 0)
		usage_with_options(live_usage, live_options);

	/*
	 * perf session
	 */
	kvm->session = perf_session__new(&data, &kvm->tool);
	if (IS_ERR(kvm->session)) {
		err = PTR_ERR(kvm->session);
		goto out;
	}
	kvm->session->evlist = kvm->evlist;
	perf_session__set_id_hdr_size(kvm->session);
	ordered_events__set_copy_on_queue(&kvm->session->ordered_events, true);
	machine__synthesize_threads(&kvm->session->machines.host, &kvm->opts.target,
				    kvm->evlist->core.threads, true, false, 1);
	err = kvm_live_open_events(kvm);
	if (err)
		goto out;

	err = kvm_events_live_report(kvm);

out:
	perf_session__delete(kvm->session);
	kvm->session = NULL;
	evlist__delete(kvm->evlist);

	return err;
}
#endif

static void print_kvm_stat_usage(void)
{
	printf("Usage: perf kvm stat <command>\n\n");

	printf("# Available commands:\n");
	printf("\trecord: record kvm events\n");
	printf("\treport: report statistical data of kvm events\n");
	printf("\tlive:   live reporting of statistical data of kvm events\n");

	printf("\nOtherwise, it is the alias of 'perf stat':\n");
}

static int kvm_cmd_stat(const char *file_name, int argc, const char **argv)
{
	struct perf_kvm_stat kvm = {
		.file_name = file_name,

		.trace_vcpu	= -1,
		.report_event	= "vmexit",
		.sort_key	= "sample",

	};

	if (argc == 1) {
		print_kvm_stat_usage();
		goto perf_stat;
	}

	if (strlen(argv[1]) > 2 && strstarts("record", argv[1]))
		return kvm_events_record(&kvm, argc - 1, argv + 1);

	if (strlen(argv[1]) > 2 && strstarts("report", argv[1]))
		return kvm_events_report(&kvm, argc - 1 , argv + 1);

#if defined(HAVE_TIMERFD_SUPPORT) && defined(HAVE_LIBTRACEEVENT)
	if (!strncmp(argv[1], "live", 4))
		return kvm_events_live(&kvm, argc - 1 , argv + 1);
#endif

perf_stat:
	return cmd_stat(argc, argv);
}
#endif /* HAVE_KVM_STAT_SUPPORT */

int __weak kvm_add_default_arch_event(int *argc __maybe_unused,
					const char **argv __maybe_unused)
{
	return 0;
}

static int __cmd_record(const char *file_name, int argc, const char **argv)
{
	int rec_argc, i = 0, j, ret;
	const char **rec_argv;

	ret = kvm_add_default_arch_event(&argc, argv);
	if (ret)
		return -EINVAL;

	rec_argc = argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	rec_argv[i++] = strdup("record");
	rec_argv[i++] = strdup("-o");
	rec_argv[i++] = strdup(file_name);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_record(i, rec_argv);
}

static int __cmd_report(const char *file_name, int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;

	rec_argc = argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	rec_argv[i++] = strdup("report");
	rec_argv[i++] = strdup("-i");
	rec_argv[i++] = strdup(file_name);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_report(i, rec_argv);
}

static int
__cmd_buildid_list(const char *file_name, int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;

	rec_argc = argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	rec_argv[i++] = strdup("buildid-list");
	rec_argv[i++] = strdup("-i");
	rec_argv[i++] = strdup(file_name);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_buildid_list(i, rec_argv);
}

int cmd_kvm(int argc, const char **argv)
{
	const char *file_name = NULL;
	const struct option kvm_options[] = {
		OPT_STRING('i', "input", &file_name, "file",
			   "Input file name"),
		OPT_STRING('o', "output", &file_name, "file",
			   "Output file name"),
		OPT_BOOLEAN(0, "guest", &perf_guest,
			    "Collect guest os data"),
		OPT_BOOLEAN(0, "host", &perf_host,
			    "Collect host os data"),
		OPT_STRING(0, "guestmount", &symbol_conf.guestmount, "directory",
			   "guest mount directory under which every guest os"
			   " instance has a subdir"),
		OPT_STRING(0, "guestvmlinux", &symbol_conf.default_guest_vmlinux_name,
			   "file", "file saving guest os vmlinux"),
		OPT_STRING(0, "guestkallsyms", &symbol_conf.default_guest_kallsyms,
			   "file", "file saving guest os /proc/kallsyms"),
		OPT_STRING(0, "guestmodules", &symbol_conf.default_guest_modules,
			   "file", "file saving guest os /proc/modules"),
		OPT_BOOLEAN(0, "guest-code", &symbol_conf.guest_code,
			    "Guest code can be found in hypervisor process"),
		OPT_INCR('v', "verbose", &verbose,
			    "be more verbose (show counter open errors, etc)"),
		OPT_END()
	};

	const char *const kvm_subcommands[] = { "top", "record", "report", "diff",
						"buildid-list", "stat", NULL };
	const char *kvm_usage[] = { NULL, NULL };

	perf_host  = 0;
	perf_guest = 1;

	argc = parse_options_subcommand(argc, argv, kvm_options, kvm_subcommands, kvm_usage,
					PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(kvm_usage, kvm_options);

	if (!perf_host)
		perf_guest = 1;

	if (!file_name) {
		file_name = get_filename_for_perf_kvm();

		if (!file_name) {
			pr_err("Failed to allocate memory for filename\n");
			return -ENOMEM;
		}
	}

	if (strlen(argv[0]) > 2 && strstarts("record", argv[0]))
		return __cmd_record(file_name, argc, argv);
	else if (strlen(argv[0]) > 2 && strstarts("report", argv[0]))
		return __cmd_report(file_name, argc, argv);
	else if (strlen(argv[0]) > 2 && strstarts("diff", argv[0]))
		return cmd_diff(argc, argv);
	else if (!strcmp(argv[0], "top"))
		return cmd_top(argc, argv);
	else if (strlen(argv[0]) > 2 && strstarts("buildid-list", argv[0]))
		return __cmd_buildid_list(file_name, argc, argv);
#if defined(HAVE_KVM_STAT_SUPPORT) && defined(HAVE_LIBTRACEEVENT)
	else if (strlen(argv[0]) > 2 && strstarts("stat", argv[0]))
		return kvm_cmd_stat(file_name, argc, argv);
#endif
	else
		usage_with_options(kvm_usage, kvm_options);

	return 0;
}
