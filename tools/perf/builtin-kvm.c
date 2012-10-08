#include "builtin.h"
#include "perf.h"

#include "util/evsel.h"
#include "util/util.h"
#include "util/cache.h"
#include "util/symbol.h"
#include "util/thread.h"
#include "util/header.h"
#include "util/session.h"

#include "util/parse-options.h"
#include "util/trace-event.h"
#include "util/debug.h"
#include "util/debugfs.h"
#include "util/tool.h"
#include "util/stat.h"

#include <sys/prctl.h>

#include <semaphore.h>
#include <pthread.h>
#include <math.h>

#include "../../arch/x86/include/asm/svm.h"
#include "../../arch/x86/include/asm/vmx.h"
#include "../../arch/x86/include/asm/kvm.h"

struct event_key {
	#define INVALID_KEY     (~0ULL)
	u64 key;
	int info;
};

struct kvm_event_stats {
	u64 time;
	struct stats stats;
};

struct kvm_event {
	struct list_head hash_entry;
	struct rb_node rb;

	struct event_key key;

	struct kvm_event_stats total;

	#define DEFAULT_VCPU_NUM 8
	int max_vcpu;
	struct kvm_event_stats *vcpu;
};

typedef int (*key_cmp_fun)(struct kvm_event*, struct kvm_event*, int);

struct kvm_event_key {
	const char *name;
	key_cmp_fun key;
};


struct perf_kvm;

struct kvm_events_ops {
	bool (*is_begin_event)(struct perf_evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key);
	bool (*is_end_event)(struct perf_evsel *evsel,
			     struct perf_sample *sample, struct event_key *key);
	void (*decode_key)(struct perf_kvm *kvm, struct event_key *key,
			   char decode[20]);
	const char *name;
};

struct exit_reasons_table {
	unsigned long exit_code;
	const char *reason;
};

#define EVENTS_BITS		12
#define EVENTS_CACHE_SIZE	(1UL << EVENTS_BITS)

struct perf_kvm {
	struct perf_tool    tool;
	struct perf_session *session;

	const char *file_name;
	const char *report_event;
	const char *sort_key;
	int trace_vcpu;

	struct exit_reasons_table *exit_reasons;
	int exit_reasons_size;
	const char *exit_reasons_isa;

	struct kvm_events_ops *events_ops;
	key_cmp_fun compare;
	struct list_head kvm_events_cache[EVENTS_CACHE_SIZE];
	u64 total_time;
	u64 total_count;

	struct rb_root result;
};


static void exit_event_get_key(struct perf_evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key)
{
	key->info = 0;
	key->key = perf_evsel__intval(evsel, sample, "exit_reason");
}

static bool kvm_exit_event(struct perf_evsel *evsel)
{
	return !strcmp(evsel->name, "kvm:kvm_exit");
}

static bool exit_event_begin(struct perf_evsel *evsel,
			     struct perf_sample *sample, struct event_key *key)
{
	if (kvm_exit_event(evsel)) {
		exit_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static bool kvm_entry_event(struct perf_evsel *evsel)
{
	return !strcmp(evsel->name, "kvm:kvm_entry");
}

static bool exit_event_end(struct perf_evsel *evsel,
			   struct perf_sample *sample __maybe_unused,
			   struct event_key *key __maybe_unused)
{
	return kvm_entry_event(evsel);
}

static struct exit_reasons_table vmx_exit_reasons[] = {
	VMX_EXIT_REASONS
};

static struct exit_reasons_table svm_exit_reasons[] = {
	SVM_EXIT_REASONS
};

static const char *get_exit_reason(struct perf_kvm *kvm, u64 exit_code)
{
	int i = kvm->exit_reasons_size;
	struct exit_reasons_table *tbl = kvm->exit_reasons;

	while (i--) {
		if (tbl->exit_code == exit_code)
			return tbl->reason;
		tbl++;
	}

	pr_err("unknown kvm exit code:%lld on %s\n",
		(unsigned long long)exit_code, kvm->exit_reasons_isa);
	return "UNKNOWN";
}

static void exit_event_decode_key(struct perf_kvm *kvm,
				  struct event_key *key,
				  char decode[20])
{
	const char *exit_reason = get_exit_reason(kvm, key->key);

	scnprintf(decode, 20, "%s", exit_reason);
}

static struct kvm_events_ops exit_events = {
	.is_begin_event = exit_event_begin,
	.is_end_event = exit_event_end,
	.decode_key = exit_event_decode_key,
	.name = "VM-EXIT"
};

/*
 * For the mmio events, we treat:
 * the time of MMIO write: kvm_mmio(KVM_TRACE_MMIO_WRITE...) -> kvm_entry
 * the time of MMIO read: kvm_exit -> kvm_mmio(KVM_TRACE_MMIO_READ...).
 */
static void mmio_event_get_key(struct perf_evsel *evsel, struct perf_sample *sample,
			       struct event_key *key)
{
	key->key  = perf_evsel__intval(evsel, sample, "gpa");
	key->info = perf_evsel__intval(evsel, sample, "type");
}

#define KVM_TRACE_MMIO_READ_UNSATISFIED 0
#define KVM_TRACE_MMIO_READ 1
#define KVM_TRACE_MMIO_WRITE 2

static bool mmio_event_begin(struct perf_evsel *evsel,
			     struct perf_sample *sample, struct event_key *key)
{
	/* MMIO read begin event in kernel. */
	if (kvm_exit_event(evsel))
		return true;

	/* MMIO write begin event in kernel. */
	if (!strcmp(evsel->name, "kvm:kvm_mmio") &&
	    perf_evsel__intval(evsel, sample, "type") == KVM_TRACE_MMIO_WRITE) {
		mmio_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static bool mmio_event_end(struct perf_evsel *evsel, struct perf_sample *sample,
			   struct event_key *key)
{
	/* MMIO write end event in kernel. */
	if (kvm_entry_event(evsel))
		return true;

	/* MMIO read end event in kernel.*/
	if (!strcmp(evsel->name, "kvm:kvm_mmio") &&
	    perf_evsel__intval(evsel, sample, "type") == KVM_TRACE_MMIO_READ) {
		mmio_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static void mmio_event_decode_key(struct perf_kvm *kvm __maybe_unused,
				  struct event_key *key,
				  char decode[20])
{
	scnprintf(decode, 20, "%#lx:%s", (unsigned long)key->key,
				key->info == KVM_TRACE_MMIO_WRITE ? "W" : "R");
}

static struct kvm_events_ops mmio_events = {
	.is_begin_event = mmio_event_begin,
	.is_end_event = mmio_event_end,
	.decode_key = mmio_event_decode_key,
	.name = "MMIO Access"
};

 /* The time of emulation pio access is from kvm_pio to kvm_entry. */
static void ioport_event_get_key(struct perf_evsel *evsel,
				 struct perf_sample *sample,
				 struct event_key *key)
{
	key->key  = perf_evsel__intval(evsel, sample, "port");
	key->info = perf_evsel__intval(evsel, sample, "rw");
}

static bool ioport_event_begin(struct perf_evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key)
{
	if (!strcmp(evsel->name, "kvm:kvm_pio")) {
		ioport_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

static bool ioport_event_end(struct perf_evsel *evsel,
			     struct perf_sample *sample __maybe_unused,
			     struct event_key *key __maybe_unused)
{
	return kvm_entry_event(evsel);
}

static void ioport_event_decode_key(struct perf_kvm *kvm __maybe_unused,
				    struct event_key *key,
				    char decode[20])
{
	scnprintf(decode, 20, "%#llx:%s", (unsigned long long)key->key,
				key->info ? "POUT" : "PIN");
}

static struct kvm_events_ops ioport_events = {
	.is_begin_event = ioport_event_begin,
	.is_end_event = ioport_event_end,
	.decode_key = ioport_event_decode_key,
	.name = "IO Port Access"
};

static bool register_kvm_events_ops(struct perf_kvm *kvm)
{
	bool ret = true;

	if (!strcmp(kvm->report_event, "vmexit"))
		kvm->events_ops = &exit_events;
	else if (!strcmp(kvm->report_event, "mmio"))
		kvm->events_ops = &mmio_events;
	else if (!strcmp(kvm->report_event, "ioport"))
		kvm->events_ops = &ioport_events;
	else {
		pr_err("Unknown report event:%s\n", kvm->report_event);
		ret = false;
	}

	return ret;
}

struct vcpu_event_record {
	int vcpu_id;
	u64 start_time;
	struct kvm_event *last_event;
};


static void init_kvm_event_record(struct perf_kvm *kvm)
{
	unsigned int i;

	for (i = 0; i < EVENTS_CACHE_SIZE; i++)
		INIT_LIST_HEAD(&kvm->kvm_events_cache[i]);
}

static int kvm_events_hash_fn(u64 key)
{
	return key & (EVENTS_CACHE_SIZE - 1);
}

static bool kvm_event_expand(struct kvm_event *event, int vcpu_id)
{
	int old_max_vcpu = event->max_vcpu;

	if (vcpu_id < event->max_vcpu)
		return true;

	while (event->max_vcpu <= vcpu_id)
		event->max_vcpu += DEFAULT_VCPU_NUM;

	event->vcpu = realloc(event->vcpu,
			      event->max_vcpu * sizeof(*event->vcpu));
	if (!event->vcpu) {
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
	return event;
}

static struct kvm_event *find_create_kvm_event(struct perf_kvm *kvm,
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

static bool handle_begin_event(struct perf_kvm *kvm,
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

static bool handle_end_event(struct perf_kvm *kvm,
			     struct vcpu_event_record *vcpu_record,
			     struct event_key *key,
			     u64 timestamp)
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

	BUG_ON(timestamp < time_begin);

	time_diff = timestamp - time_begin;
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

		vcpu_record->vcpu_id = perf_evsel__intval(evsel, sample, "vcpu_id");
		thread->priv = vcpu_record;
	}

	return thread->priv;
}

static bool handle_kvm_event(struct perf_kvm *kvm,
			     struct thread *thread,
			     struct perf_evsel *evsel,
			     struct perf_sample *sample)
{
	struct vcpu_event_record *vcpu_record;
	struct event_key key = {.key = INVALID_KEY};

	vcpu_record = per_vcpu_record(thread, evsel, sample);
	if (!vcpu_record)
		return true;

	/* only process events for vcpus user cares about */
	if ((kvm->trace_vcpu != -1) &&
	    (kvm->trace_vcpu != vcpu_record->vcpu_id))
		return true;

	if (kvm->events_ops->is_begin_event(evsel, sample, &key))
		return handle_begin_event(kvm, vcpu_record, &key, sample->time);

	if (kvm->events_ops->is_end_event(evsel, sample, &key))
		return handle_end_event(kvm, vcpu_record, &key, sample->time);

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

#define DEF_SORT_NAME_KEY(name, compare_key)				\
	{ #name, compare_kvm_event_ ## compare_key }

static struct kvm_event_key keys[] = {
	DEF_SORT_NAME_KEY(sample, count),
	DEF_SORT_NAME_KEY(time, mean),
	{ NULL, NULL }
};

static bool select_key(struct perf_kvm *kvm)
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

static void update_total_count(struct perf_kvm *kvm, struct kvm_event *event)
{
	int vcpu = kvm->trace_vcpu;

	kvm->total_count += get_event_count(event, vcpu);
	kvm->total_time += get_event_time(event, vcpu);
}

static bool event_is_valid(struct kvm_event *event, int vcpu)
{
	return !!get_event_count(event, vcpu);
}

static void sort_result(struct perf_kvm *kvm)
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

static void print_vcpu_info(int vcpu)
{
	pr_info("Analyze events for ");

	if (vcpu == -1)
		pr_info("all VCPUs:\n\n");
	else
		pr_info("VCPU %d:\n\n", vcpu);
}

static void print_result(struct perf_kvm *kvm)
{
	char decode[20];
	struct kvm_event *event;
	int vcpu = kvm->trace_vcpu;

	pr_info("\n\n");
	print_vcpu_info(vcpu);
	pr_info("%20s ", kvm->events_ops->name);
	pr_info("%10s ", "Samples");
	pr_info("%9s ", "Samples%");

	pr_info("%9s ", "Time%");
	pr_info("%16s ", "Avg time");
	pr_info("\n\n");

	while ((event = pop_from_result(&kvm->result))) {
		u64 ecount, etime;

		ecount = get_event_count(event, vcpu);
		etime = get_event_time(event, vcpu);

		kvm->events_ops->decode_key(kvm, &event->key, decode);
		pr_info("%20s ", decode);
		pr_info("%10llu ", (unsigned long long)ecount);
		pr_info("%8.2f%% ", (double)ecount / kvm->total_count * 100);
		pr_info("%8.2f%% ", (double)etime / kvm->total_time * 100);
		pr_info("%9.2fus ( +-%7.2f%% )", (double)etime / ecount/1e3,
			kvm_event_rel_stddev(vcpu, event));
		pr_info("\n");
	}

	pr_info("\nTotal Samples:%" PRIu64 ", Total events handled time:%.2fus.\n\n",
		kvm->total_count, kvm->total_time / 1e3);
}

static int process_sample_event(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct perf_evsel *evsel,
				struct machine *machine)
{
	struct thread *thread = machine__findnew_thread(machine, sample->tid);
	struct perf_kvm *kvm = container_of(tool, struct perf_kvm, tool);

	if (thread == NULL) {
		pr_debug("problem processing %d event, skipping it.\n",
			event->header.type);
		return -1;
	}

	if (!handle_kvm_event(kvm, thread, evsel, sample))
		return -1;

	return 0;
}

static int get_cpu_isa(struct perf_session *session)
{
	char *cpuid = session->header.env.cpuid;
	int isa;

	if (strstr(cpuid, "Intel"))
		isa = 1;
	else if (strstr(cpuid, "AMD"))
		isa = 0;
	else {
		pr_err("CPU %s is not supported.\n", cpuid);
		isa = -ENOTSUP;
	}

	return isa;
}

static int read_events(struct perf_kvm *kvm)
{
	int ret;

	struct perf_tool eops = {
		.sample			= process_sample_event,
		.comm			= perf_event__process_comm,
		.ordered_samples	= true,
	};

	kvm->tool = eops;
	kvm->session = perf_session__new(kvm->file_name, O_RDONLY, 0, false,
					 &kvm->tool);
	if (!kvm->session) {
		pr_err("Initializing perf session failed\n");
		return -EINVAL;
	}

	if (!perf_session__has_traces(kvm->session, "kvm record"))
		return -EINVAL;

	/*
	 * Do not use 'isa' recorded in kvm_exit tracepoint since it is not
	 * traced in the old kernel.
	 */
	ret = get_cpu_isa(kvm->session);

	if (ret < 0)
		return ret;

	if (ret == 1) {
		kvm->exit_reasons = vmx_exit_reasons;
		kvm->exit_reasons_size = ARRAY_SIZE(vmx_exit_reasons);
		kvm->exit_reasons_isa = "VMX";
	}

	return perf_session__process_events(kvm->session, &kvm->tool);
}

static bool verify_vcpu(int vcpu)
{
	if (vcpu != -1 && vcpu < 0) {
		pr_err("Invalid vcpu:%d.\n", vcpu);
		return false;
	}

	return true;
}

static int kvm_events_report_vcpu(struct perf_kvm *kvm)
{
	int ret = -EINVAL;
	int vcpu = kvm->trace_vcpu;

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

static const char * const record_args[] = {
	"record",
	"-R",
	"-f",
	"-m", "1024",
	"-c", "1",
	"-e", "kvm:kvm_entry",
	"-e", "kvm:kvm_exit",
	"-e", "kvm:kvm_mmio",
	"-e", "kvm:kvm_pio",
};

#define STRDUP_FAIL_EXIT(s)		\
	({	char *_p;		\
	_p = strdup(s);		\
		if (!_p)		\
			return -ENOMEM;	\
		_p;			\
	})

static int kvm_events_record(struct perf_kvm *kvm, int argc, const char **argv)
{
	unsigned int rec_argc, i, j;
	const char **rec_argv;

	rec_argc = ARRAY_SIZE(record_args) + argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));

	if (rec_argv == NULL)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(record_args); i++)
		rec_argv[i] = STRDUP_FAIL_EXIT(record_args[i]);

	rec_argv[i++] = STRDUP_FAIL_EXIT("-o");
	rec_argv[i++] = STRDUP_FAIL_EXIT(kvm->file_name);

	for (j = 1; j < (unsigned int)argc; j++, i++)
		rec_argv[i] = argv[j];

	return cmd_record(i, rec_argv, NULL);
}

static int kvm_events_report(struct perf_kvm *kvm, int argc, const char **argv)
{
	const struct option kvm_events_report_options[] = {
		OPT_STRING(0, "event", &kvm->report_event, "report event",
			    "event for reporting: vmexit, mmio, ioport"),
		OPT_INTEGER(0, "vcpu", &kvm->trace_vcpu,
			    "vcpu id to report"),
		OPT_STRING('k', "key", &kvm->sort_key, "sort-key",
			    "key for sorting: sample(sort by samples number)"
			    " time (sort by avg time)"),
		OPT_END()
	};

	const char * const kvm_events_report_usage[] = {
		"perf kvm stat report [<options>]",
		NULL
	};

	symbol__init();

	if (argc) {
		argc = parse_options(argc, argv,
				     kvm_events_report_options,
				     kvm_events_report_usage, 0);
		if (argc)
			usage_with_options(kvm_events_report_usage,
					   kvm_events_report_options);
	}

	return kvm_events_report_vcpu(kvm);
}

static void print_kvm_stat_usage(void)
{
	printf("Usage: perf kvm stat <command>\n\n");

	printf("# Available commands:\n");
	printf("\trecord: record kvm events\n");
	printf("\treport: report statistical data of kvm events\n");

	printf("\nOtherwise, it is the alias of 'perf stat':\n");
}

static int kvm_cmd_stat(struct perf_kvm *kvm, int argc, const char **argv)
{
	if (argc == 1) {
		print_kvm_stat_usage();
		goto perf_stat;
	}

	if (!strncmp(argv[1], "rec", 3))
		return kvm_events_record(kvm, argc - 1, argv + 1);

	if (!strncmp(argv[1], "rep", 3))
		return kvm_events_report(kvm, argc - 1 , argv + 1);

perf_stat:
	return cmd_stat(argc, argv, NULL);
}

static int __cmd_record(struct perf_kvm *kvm, int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;

	rec_argc = argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	rec_argv[i++] = strdup("record");
	rec_argv[i++] = strdup("-o");
	rec_argv[i++] = strdup(kvm->file_name);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_record(i, rec_argv, NULL);
}

static int __cmd_report(struct perf_kvm *kvm, int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;

	rec_argc = argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	rec_argv[i++] = strdup("report");
	rec_argv[i++] = strdup("-i");
	rec_argv[i++] = strdup(kvm->file_name);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_report(i, rec_argv, NULL);
}

static int __cmd_buildid_list(struct perf_kvm *kvm, int argc, const char **argv)
{
	int rec_argc, i = 0, j;
	const char **rec_argv;

	rec_argc = argc + 2;
	rec_argv = calloc(rec_argc + 1, sizeof(char *));
	rec_argv[i++] = strdup("buildid-list");
	rec_argv[i++] = strdup("-i");
	rec_argv[i++] = strdup(kvm->file_name);
	for (j = 1; j < argc; j++, i++)
		rec_argv[i] = argv[j];

	BUG_ON(i != rec_argc);

	return cmd_buildid_list(i, rec_argv, NULL);
}

int cmd_kvm(int argc, const char **argv, const char *prefix __maybe_unused)
{
	struct perf_kvm kvm = {
		.trace_vcpu	= -1,
		.report_event	= "vmexit",
		.sort_key	= "sample",

		.exit_reasons = svm_exit_reasons,
		.exit_reasons_size = ARRAY_SIZE(svm_exit_reasons),
		.exit_reasons_isa = "SVM",
	};

	const struct option kvm_options[] = {
		OPT_STRING('i', "input", &kvm.file_name, "file",
			   "Input file name"),
		OPT_STRING('o', "output", &kvm.file_name, "file",
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
		OPT_END()
	};


	const char * const kvm_usage[] = {
		"perf kvm [<options>] {top|record|report|diff|buildid-list|stat}",
		NULL
	};

	perf_host  = 0;
	perf_guest = 1;

	argc = parse_options(argc, argv, kvm_options, kvm_usage,
			PARSE_OPT_STOP_AT_NON_OPTION);
	if (!argc)
		usage_with_options(kvm_usage, kvm_options);

	if (!perf_host)
		perf_guest = 1;

	if (!kvm.file_name) {
		if (perf_host && !perf_guest)
			kvm.file_name = strdup("perf.data.host");
		else if (!perf_host && perf_guest)
			kvm.file_name = strdup("perf.data.guest");
		else
			kvm.file_name = strdup("perf.data.kvm");

		if (!kvm.file_name) {
			pr_err("Failed to allocate memory for filename\n");
			return -ENOMEM;
		}
	}

	if (!strncmp(argv[0], "rec", 3))
		return __cmd_record(&kvm, argc, argv);
	else if (!strncmp(argv[0], "rep", 3))
		return __cmd_report(&kvm, argc, argv);
	else if (!strncmp(argv[0], "diff", 4))
		return cmd_diff(argc, argv, NULL);
	else if (!strncmp(argv[0], "top", 3))
		return cmd_top(argc, argv, NULL);
	else if (!strncmp(argv[0], "buildid-list", 12))
		return __cmd_buildid_list(&kvm, argc, argv);
	else if (!strncmp(argv[0], "stat", 4))
		return kvm_cmd_stat(&kvm, argc, argv);
	else
		usage_with_options(kvm_usage, kvm_options);

	return 0;
}
