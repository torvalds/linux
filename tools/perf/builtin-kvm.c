#include "builtin.h"
#include "perf.h"

#include "util/evsel.h"
#include "util/evlist.h"
#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/session.h"
#include "util/intlist.h"
#include "util/parse-options.h"
#include "util/trace-event.h"
#include "util/debug.h"
#include <api/fs/debugfs.h>
#include "util/tool.h"
#include "util/stat.h"
#include "util/top.h"
#include "util/data.h"

#include <sys/prctl.h>
#ifdef HAVE_TIMERFD_SUPPORT
#include <sys/timerfd.h>
#endif

#include <termios.h>
#include <semaphore.h>
#include <pthread.h>
#include <math.h>

#ifdef HAVE_KVM_STAT_SUPPORT
#include <asm/kvm_perf.h>
#include "util/kvm-stat.h"

void exit_event_get_key(struct perf_evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key)
{
	key->info = 0;
	key->key = perf_evsel__intval(evsel, sample, KVM_EXIT_REASON);
}

bool kvm_exit_event(struct perf_evsel *evsel)
{
	return !strcmp(evsel->name, KVM_EXIT_TRACE);
}

bool exit_event_begin(struct perf_evsel *evsel,
		      struct perf_sample *sample, struct event_key *key)
{
	if (kvm_exit_event(evsel)) {
		exit_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

bool kvm_entry_event(struct perf_evsel *evsel)
{
	return !strcmp(evsel->name, KVM_ENTRY_TRACE);
}

bool exit_event_end(struct perf_evsel *evsel,
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

	scnprintf(decode, DECODE_STR_LEN, "%s", exit_reason);
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


static void init_kvm_event_record(struct perf_kvm_stat *kvm)
{
	unsigned int i;

	for (i = 0; i < EVENTS_CACHE_SIZE; i++)
		INIT_LIST_HEAD(&kvm->kvm_events_cache[i]);
}

#ifdef HAVE_TIMERFD_SUPPORT
static void clear_events_cache_stats(struct list_head *kvm_events_cache)
{
	struct list_head *head;
	struct kvm_event *event;
	unsigned int i;
	int j;

	for (i = 0; i < EVENTS_CACHE_SIZE; i++) {
		head = &kvm_events_cache[i];
		list_for_each_entry(event, head, hash_entry) {
			/* reset stats for event */
			event->total.time = 0;
			init_stats(&event->total.stats);

			for (j = 0; j < event->max_vcpu; ++j) {
				event->vcpu[j].time = 0;
				init_stats(&event->vcpu[j].stats);
			}
		}
	}
}
#endif

static int kvm_events_hash_fn(u64 key)
{
	return key & (EVENTS_CACHE_SIZE - 1);
}

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

static struct kvm_event *kvm_alloc_init_event(struct event_key *key)
{
	struct kvm_event *event;

	event = zalloc(sizeof(*event));
	if (!event) {
		pr_err("Not enough memory\n");
		return NULL;
	}

	event->key = *key;
	init_stats(&event->total.stats);
	return event;
}

static struct kvm_event *find_create_kvm_event(struct perf_kvm_stat *kvm,
					       struct event_key *key)
{
	struct kvm_event *event;
	struct list_head *head;

	BUG_ON(key->key == INVALID_KEY);

	head = &kvm->kvm_events_cache[kvm_events_hash_fn(key->key)];
	list_for_each_entry(event, head, hash_entry) {
		if (event->key.key == key->key && event->key.info == key->info)
			return event;
	}

	event = kvm_alloc_init_event(key);
	if (!event)
		return NULL;

	list_add(&event->hash_entry, head);
	return event;
}

static bool handle_begin_event(struct perf_kvm_stat *kvm,
			       struct vcpu_event_record *vcpu_record,
			       struct event_key *key, u64 timestamp)
{
	struct kvm_event *event = NULL;

	if (key->key != INVALID_KEY)
		event = find_create_kvm_event(kvm, key);

	vcpu_record->last_event = event;
	vcpu_record->start_time = timestamp;
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

static bool update_kvm_event(struct kvm_event *event, int vcpu_id,
			     u64 time_diff)
{
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
			   struct perf_evsel *evsel,
			   struct perf_sample *sample,
			   struct event_key *key)
{
	struct child_event_ops *child_ops;

	child_ops = kvm->events_ops->child_ops;

	if (!child_ops)
		return false;

	for (; child_ops->name; child_ops++) {
		if (!strcmp(evsel->name, child_ops->name)) {
			child_ops->get_key(evsel, sample, key);
			return true;
		}
	}

	return false;
}

static bool handle_child_event(struct perf_kvm_stat *kvm,
			       struct vcpu_event_record *vcpu_record,
			       struct event_key *key,
			       struct perf_sample *sample __maybe_unused)
{
	struct kvm_event *event = NULL;

	if (key->key != INVALID_KEY)
		event = find_create_kvm_event(kvm, key);

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
		event = find_create_kvm_event(kvm, key);

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
		char decode[DECODE_STR_LEN];

		kvm->events_ops->decode_key(kvm, &event->key, decode);
		if (!skip_event(decode)) {
			pr_info("%" PRIu64 " VM %d, vcpu %d: %s event took %" PRIu64 "usec\n",
				 sample->time, sample->pid, vcpu_record->vcpu_id,
				 decode, time_diff/1000);
		}
	}

	return update_kvm_event(event, vcpu, time_diff);
}

static
struct vcpu_event_record *per_vcpu_record(struct thread *thread,
					  struct perf_evsel *evsel,
					  struct perf_sample *sample)
{
	/* Only kvm_entry records vcpu id. */
	if (!thread->priv && kvm_entry_event(evsel)) {
		struct vcpu_event_record *vcpu_record;

		vcpu_record = zalloc(sizeof(*vcpu_record));
		if (!vcpu_record) {
			pr_err("%s: Not enough memory\n", __func__);
			return NULL;
		}

		vcpu_record->vcpu_id = perf_evsel__intval(evsel, sample, VCPU_ID);
		thread->priv = vcpu_record;
	}

	return thread->priv;
}

static bool handle_kvm_event(struct perf_kvm_stat *kvm,
			     struct thread *thread,
			     struct perf_evsel *evsel,
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
		return handle_begin_event(kvm, vcpu_record, &key, sample->time);

	if (is_child_event(kvm, evsel, sample, &key))
		return handle_child_event(kvm, vcpu_record, &key, sample);

	if (kvm->events_ops->is_end_event(evsel, sample, &key))
		return handle_end_event(kvm, vcpu_record, &key, sample);

	return true;
}

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
static int compare_kvm_event_ ## func(struct kvm_event *one,		\
					struct kvm_event *two, int vcpu)\
{									\
	return get_event_ ##func(one, vcpu) >				\
				get_event_ ##func(two, vcpu);		\
}

GET_EVENT_KEY(time, time);
COMPARE_EVENT_KEY(count, stats.n);
COMPARE_EVENT_KEY(mean, stats.mean);
GET_EVENT_KEY(max, stats.max);
GET_EVENT_KEY(min, stats.min);

#define DEF_SORT_NAME_KEY(name, compare_key)				\
	{ #name, compare_kvm_event_ ## compare_key }

static struct kvm_event_key keys[] = {
	DEF_SORT_NAME_KEY(sample, count),
	DEF_SORT_NAME_KEY(time, mean),
	{ NULL, NULL }
};

static bool select_key(struct perf_kvm_stat *kvm)
{
	int i;

	for (i = 0; keys[i].name; i++) {
		if (!strcmp(keys[i].name, kvm->sort_key)) {
			kvm->compare = keys[i].key;
			return true;
		}
	}

	pr_err("Unknown compare key:%s\n", kvm->sort_key);
	return false;
}

static void insert_to_result(struct rb_root *result, struct kvm_event *event,
			     key_cmp_fun bigger, int vcpu)
{
	struct rb_node **rb = &result->rb_node;
	struct rb_node *parent = NULL;
	struct kvm_event *p;

	while (*rb) {
		p = container_of(*rb, struct kvm_event, rb);
		parent = *rb;

		if (bigger(event, p, vcpu))
			rb = &(*rb)->rb_left;
		else
			rb = &(*rb)->rb_right;
	}

	rb_link_node(&event->rb, parent, rb);
	rb_insert_color(&event->rb, result);
}

static void
update_total_count(struct perf_kvm_stat *kvm, struct kvm_event *event)
{
	int vcpu = kvm->trace_vcpu;

	kvm->total_count += get_event_count(event, vcpu);
	kvm->total_time += get_event_time(event, vcpu);
}

static bool event_is_valid(struct kvm_event *event, int vcpu)
{
	return !!get_event_count(event, vcpu);
}

static void sort_result(struct perf_kvm_stat *kvm)
{
	unsigned int i;
	int vcpu = kvm->trace_vcpu;
	struct kvm_event *event;

	for (i = 0; i < EVENTS_CACHE_SIZE; i++) {
		list_for_each_entry(event, &kvm->kvm_events_cache[i], hash_entry) {
			if (event_is_valid(event, vcpu)) {
				update_total_count(kvm, event);
				insert_to_result(&kvm->result, event,
						 kvm->compare, vcpu);
			}
		}
	}
}

/* returns left most element of result, and erase it */
static struct kvm_event *pop_from_result(struct rb_root *result)
{
	struct rb_node *node = rb_first(result);

	if (!node)
		return NULL;

	rb_erase(node, result);
	return container_of(node, struct kvm_event, rb);
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
	char decode[DECODE_STR_LEN];
	struct kvm_event *event;
	int vcpu = kvm->trace_vcpu;

	if (kvm->live) {
		puts(CONSOLE_CLEAR);
		show_timeofday();
	}

	pr_info("\n\n");
	print_vcpu_info(kvm);
	pr_info("%*s ", DECODE_STR_LEN, kvm->events_ops->name);
	pr_info("%10s ", "Samples");
	pr_info("%9s ", "Samples%");

	pr_info("%9s ", "Time%");
	pr_info("%11s ", "Min Time");
	pr_info("%11s ", "Max Time");
	pr_info("%16s ", "Avg time");
	pr_info("\n\n");

	while ((event = pop_from_result(&kvm->result))) {
		u64 ecount, etime, max, min;

		ecount = get_event_count(event, vcpu);
		etime = get_event_time(event, vcpu);
		max = get_event_max(event, vcpu);
		min = get_event_min(event, vcpu);

		kvm->events_ops->decode_key(kvm, &event->key, decode);
		pr_info("%*s ", DECODE_STR_LEN, decode);
		pr_info("%10llu ", (unsigned long long)ecount);
		pr_info("%8.2f%% ", (double)ecount / kvm->total_count * 100);
		pr_info("%8.2f%% ", (double)etime / kvm->total_time * 100);
		pr_info("%9.2fus ", (double)min / 1e3);
		pr_info("%9.2fus ", (double)max / 1e3);
		pr_info("%9.2fus ( +-%7.2f%% )", (double)etime / ecount/1e3,
			kvm_event_rel_stddev(vcpu, event));
		pr_info("\n");
	}

	pr_info("\nTotal Samples:%" PRIu64 ", Total events handled time:%.2fus.\n\n",
		kvm->total_count, kvm->total_time / 1e3);

	if (kvm->lost_events)
		pr_info("\nLost events: %" PRIu64 "\n\n", kvm->lost_events);
}

#ifdef HAVE_TIMERFD_SUPPORT
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
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct thread *thread;
	struct perf_kvm_stat *kvm = container_of(tool, struct perf_kvm_stat,
						 tool);

	if (skip_sample(kvm, sample))
		return 0;

	thread = machine__findnew_thread(machine, sample->pid, sample->tid);
	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (!handle_kvm_event(kvm, thread, evsel, sample))
		return -1;

	return 0;
}

static int cpu_isa_config(struct perf_kvm_stat *kvm)
{
	char buf[64], *cpuid;
	int err;

	if (kvm->live) {
		err = get_cpuid(buf, sizeof(buf));
		if (err != 0) {
			pr_err("Failed to look up CPU type\n");
			return err;
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

#ifdef HAVE_TIMERFD_SUPPORT
/* keeping the max events to a modest level to keep
 * the processing of samples per mmap smooth.
 */
#define PERF_KVM__MAX_EVENTS_PER_MMAP  25

static s64 perf_kvm__mmap_read_idx(struct perf_kvm_stat *kvm, int idx,
				   u64 *mmap_time)
{
	union perf_event *event;
	struct perf_sample sample;
	s64 n = 0;
	int err;

	*mmap_time = ULLONG_MAX;
	while ((event = perf_evlist__mmap_read(kvm->evlist, idx)) != NULL) {
		err = perf_evlist__parse_sample(kvm->evlist, event, &sample);
		if (err) {
			perf_evlist__mmap_consume(kvm->evlist, idx);
			pr_err("Failed to parse sample\n");
			return -1;
		}

		err = perf_session_queue_event(kvm->session, event, &kvm->tool, &sample, 0);
		/*
		 * FIXME: Here we can't consume the event, as perf_session_queue_event will
		 *        point to it, and it'll get possibly overwritten by the kernel.
		 */
		perf_evlist__mmap_consume(kvm->evlist, idx);

		if (err) {
			pr_err("Failed to enqueue sample: %d\n", err);
			return -1;
		}

		/* save time stamp of our first sample for this mmap */
		if (n == 0)
			*mmap_time = sample.time;

		/* limit events per mmap handled all at once */
		n++;
		if (n == PERF_KVM__MAX_EVENTS_PER_MMAP)
			break;
	}

	return n;
}

static int perf_kvm__mmap_read(struct perf_kvm_stat *kvm)
{
	int i, err, throttled = 0;
	s64 n, ntotal = 0;
	u64 flush_time = ULLONG_MAX, mmap_time;

	for (i = 0; i < kvm->evlist->nr_mmaps; i++) {
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
		kvm->session->ordered_events.next_flush = flush_time;
		err = kvm->tool.finished_round(&kvm->tool, NULL, kvm->session);
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

	/* reset counts */
	clear_events_cache_stats(kvm->kvm_events_cache);
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
	struct pollfd *pollfds = NULL;
	int nr_fds, nr_stdin, ret, err = -EINVAL;
	struct termios save;

	/* live flag must be set first */
	kvm->live = true;

	ret = cpu_isa_config(kvm);
	if (ret < 0)
		return ret;

	if (!verify_vcpu(kvm->trace_vcpu) ||
	    !select_key(kvm) ||
	    !register_kvm_events_ops(kvm)) {
		goto out;
	}

	set_term_quiet_input(&save);
	init_kvm_event_record(kvm);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	/* use pollfds -- need to add timerfd and stdin */
	nr_fds = kvm->evlist->pollfd.nr;

	/* add timer fd */
	if (perf_kvm__timerfd_create(kvm) < 0) {
		err = -1;
		goto out;
	}

	if (perf_evlist__add_pollfd(kvm->evlist, kvm->timerfd))
		goto out;

	nr_fds++;

	if (perf_evlist__add_pollfd(kvm->evlist, fileno(stdin)))
		goto out;

	nr_stdin = nr_fds;
	nr_fds++;
	if (fd_set_nonblock(fileno(stdin)) != 0)
		goto out;

	pollfds	 = kvm->evlist->pollfd.entries;

	/* everything is good - enable the events and process */
	perf_evlist__enable(kvm->evlist);

	while (!done) {
		int rc;

		rc = perf_kvm__mmap_read(kvm);
		if (rc < 0)
			break;

		err = perf_kvm__handle_timerfd(kvm);
		if (err)
			goto out;

		if (pollfds[nr_stdin].revents & POLLIN)
			done = perf_kvm__handle_stdin();

		if (!rc && !done)
			err = poll(pollfds, nr_fds, 100);
	}

	perf_evlist__disable(kvm->evlist);

	if (err == 0) {
		sort_result(kvm);
		print_result(kvm);
	}

out:
	if (kvm->timerfd >= 0)
		close(kvm->timerfd);

	tcsetattr(0, TCSAFLUSH, &save);
	return err;
}

static int kvm_live_open_events(struct perf_kvm_stat *kvm)
{
	int err, rc = -1;
	struct perf_evsel *pos;
	struct perf_evlist *evlist = kvm->evlist;
	char sbuf[STRERR_BUFSIZE];

	perf_evlist__config(evlist, &kvm->opts);

	/*
	 * Note: exclude_{guest,host} do not apply here.
	 *       This command processes KVM tracepoints from host only
	 */
	evlist__for_each(evlist, pos) {
		struct perf_event_attr *attr = &pos->attr;

		/* make sure these *are* set */
		perf_evsel__set_sample_bit(pos, TID);
		perf_evsel__set_sample_bit(pos, TIME);
		perf_evsel__set_sample_bit(pos, CPU);
		perf_evsel__set_sample_bit(pos, RAW);
		/* make sure these are *not*; want as small a sample as possible */
		perf_evsel__reset_sample_bit(pos, PERIOD);
		perf_evsel__reset_sample_bit(pos, IP);
		perf_evsel__reset_sample_bit(pos, CALLCHAIN);
		perf_evsel__reset_sample_bit(pos, ADDR);
		perf_evsel__reset_sample_bit(pos, READ);
		attr->mmap = 0;
		attr->comm = 0;
		attr->task = 0;

		attr->sample_period = 1;

		attr->watermark = 0;
		attr->wakeup_events = 1000;

		/* will enable all once we are ready */
		attr->disabled = 1;
	}

	err = perf_evlist__open(evlist);
	if (err < 0) {
		printf("Couldn't create the events: %s\n",
		       strerror_r(errno, sbuf, sizeof(sbuf)));
		goto out;
	}

	if (perf_evlist__mmap(evlist, kvm->opts.mmap_pages, false) < 0) {
		ui__error("Failed to mmap the events: %s\n",
			  strerror_r(errno, sbuf, sizeof(sbuf)));
		perf_evlist__close(evlist);
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
		.ordered_events		= true,
	};
	struct perf_data_file file = {
		.path = kvm->file_name,
		.mode = PERF_DATA_MODE_READ,
	};

	kvm->tool = eops;
	kvm->session = perf_session__new(&file, false, &kvm->tool);
	if (!kvm->session) {
		pr_err("Initializing perf session failed\n");
		return -1;
	}

	symbol__init(&kvm->session->header.env);

	if (!perf_session__has_traces(kvm->session, "kvm record"))
		return -EINVAL;

	/*
	 * Do not use 'isa' recorded in kvm_exit tracepoint since it is not
	 * traced in the old kernel.
	 */
	ret = cpu_isa_config(kvm);
	if (ret < 0)
		return ret;

	return perf_session__process_events(kvm->session, &kvm->tool);
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

	if (!select_key(kvm))
		goto exit;

	if (!register_kvm_events_ops(kvm))
		goto exit;

	init_kvm_event_record(kvm);
	setup_pager();

	ret = read_events(kvm);
	if (ret)
		goto exit;

	sort_result(kvm);
	print_result(kvm);

exit:
	return ret;
}

#define STRDUP_FAIL_EXIT(s)		\
	({	char *_p;		\
	_p = strdup(s);		\
		if (!_p)		\
			return -ENOMEM;	\
		_p;			\
	})

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
	const char * const *events_tp;
	events_tp_size = 0;

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

	return cmd_record(i, rec_argv, NULL);
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

	if (!kvm->opts.target.pid)
		kvm->opts.target.system_wide = true;

	return kvm_events_report_vcpu(kvm);
}

#ifdef HAVE_TIMERFD_SUPPORT
static struct perf_evlist *kvm_live_event_list(void)
{
	struct perf_evlist *evlist;
	char *tp, *name, *sys;
	int err = -1;
	const char * const *events_tp;

	evlist = perf_evlist__new();
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

		if (perf_evlist__add_newtp(evlist, sys, name, NULL)) {
			pr_err("Failed to add %s tracepoint to the list\n", *events_tp);
			free(tp);
			goto out;
		}

		free(tp);
	}

	err = 0;

out:
	if (err) {
		perf_evlist__delete(evlist);
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
			"number of mmap data pages",
			perf_evlist__parse_mmap_pages),
		OPT_INCR('v', "verbose", &verbose,
			"be more verbose (show counter open errors, etc)"),
		OPT_BOOLEAN('a', "all-cpus", &kvm->opts.target.system_wide,
			"system-wide collection from all CPUs"),
		OPT_UINTEGER('d', "display", &kvm->display_time,
			"time in seconds between display updates"),
		OPT_STRING(0, "event", &kvm->report_event, "report event",
			"event for reporting: vmexit, mmio, ioport"),
		OPT_INTEGER(0, "vcpu", &kvm->trace_vcpu,
			"vcpu id to report"),
		OPT_STRING('k', "key", &kvm->sort_key, "sort-key",
			"key for sorting: sample(sort by samples number)"
			" time (sort by avg time)"),
		OPT_U64(0, "duration", &kvm->duration,
			"show events other than"
			" HLT (x86 only) or Wait state (s390 only)"
			" that take longer than duration usecs"),
		OPT_END()
	};
	const char * const live_usage[] = {
		"perf kvm stat live [<options>]",
		NULL
	};
	struct perf_data_file file = {
		.mode = PERF_DATA_MODE_WRITE,
	};


	/* event handling */
	kvm->tool.sample = process_sample_event;
	kvm->tool.comm   = perf_event__process_comm;
	kvm->tool.exit   = perf_event__process_exit;
	kvm->tool.fork   = perf_event__process_fork;
	kvm->tool.lost   = process_lost_event;
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
	setup_browser(false);

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
	kvm->evlist = kvm_live_event_list();
	if (kvm->evlist == NULL) {
		err = -1;
		goto out;
	}

	symbol_conf.nr_events = kvm->evlist->nr_entries;

	if (perf_evlist__create_maps(kvm->evlist, &kvm->opts.target) < 0)
		usage_with_options(live_usage, live_options);

	/*
	 * perf session
	 */
	kvm->session = perf_session__new(&file, false, &kvm->tool);
	if (kvm->session == NULL) {
		err = -1;
		goto out;
	}
	kvm->session->evlist = kvm->evlist;
	perf_session__set_id_hdr_size(kvm->session);
	machine__synthesize_threads(&kvm->session->machines.host, &kvm->opts.target,
				    kvm->evlist->threads, false);
	err = kvm_live_open_events(kvm);
	if (err)
		goto out;

	err = kvm_events_live_report(kvm);

out:
	exit_browser(0);

	if (kvm->session)
		perf_session__delete(kvm->session);
	kvm->session = NULL;
	if (kvm->evlist)
		perf_evlist__delete(kvm->evlist);

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

	if (!strncmp(argv[1], "rec", 3))
		return kvm_events_record(&kvm, argc - 1, argv + 1);

	if (!strncmp(argv[1], "rep", 3))
		return kvm_events_report(&kvm, argc - 1 , argv + 1);

#ifdef HAVE_TIMERFD_SUPPORT
	if (!strncmp(argv[1], "live", 4))
		return kvm_events_live(&kvm, argc - 1 , argv + 1);
#endif

perf_stat:
	return cmd_stat(argc, argv, NULL);
}
#endif /* HAVE_KVM_STAT_SUPPORT */

static int __cmd_record(const char *file_name, int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;

	rec_argc = argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	rec_argv[i++] = strdup("record");
	rec_argv[i++] = strdup("-o");
	rec_argv[i++] = strdup(file_name);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_record(i, rec_argv, NULL);
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

	return cmd_report(i, rec_argv, NULL);
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

	return cmd_buildid_list(i, rec_argv, NULL);
}

int cmd_kvm(int argc, const char **argv, const char *prefix __maybe_unused)
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

	if (!strncmp(argv[0], "rec", 3))
		return __cmd_record(file_name, argc, argv);
	else if (!strncmp(argv[0], "rep", 3))
		return __cmd_report(file_name, argc, argv);
	else if (!strncmp(argv[0], "diff", 4))
		return cmd_diff(argc, argv, NULL);
	else if (!strncmp(argv[0], "top", 3))
		return cmd_top(argc, argv, NULL);
	else if (!strncmp(argv[0], "buildid-list", 12))
		return __cmd_buildid_list(file_name, argc, argv);
#ifdef HAVE_KVM_STAT_SUPPORT
	else if (!strncmp(argv[0], "stat", 4))
		return kvm_cmd_stat(file_name, argc, argv);
#endif
	else
		usage_with_options(kvm_usage, kvm_options);

	return 0;
}
