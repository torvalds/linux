#ifndef __PERF_KVM_STAT_H
#define __PERF_KVM_STAT_H

#include "../perf.h"
#include "evsel.h"
#include "evlist.h"
#include "session.h"
#include "tool.h"
#include "stat.h"

struct event_key {
	#define INVALID_KEY     (~0ULL)
	u64 key;
	int info;
	struct exit_reasons_table *exit_reasons;
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

struct perf_kvm_stat;

struct child_event_ops {
	void (*get_key)(struct perf_evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key);
	const char *name;
};

struct kvm_events_ops {
	bool (*is_begin_event)(struct perf_evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key);
	bool (*is_end_event)(struct perf_evsel *evsel,
			     struct perf_sample *sample, struct event_key *key);
	struct child_event_ops *child_ops;
	void (*decode_key)(struct perf_kvm_stat *kvm, struct event_key *key,
			   char *decode);
	const char *name;
};

struct exit_reasons_table {
	unsigned long exit_code;
	const char *reason;
};

#define EVENTS_BITS		12
#define EVENTS_CACHE_SIZE	(1UL << EVENTS_BITS)

struct perf_kvm_stat {
	struct perf_tool    tool;
	struct record_opts  opts;
	struct perf_evlist  *evlist;
	struct perf_session *session;

	const char *file_name;
	const char *report_event;
	const char *sort_key;
	int trace_vcpu;

	struct exit_reasons_table *exit_reasons;
	const char *exit_reasons_isa;

	struct kvm_events_ops *events_ops;
	key_cmp_fun compare;
	struct list_head kvm_events_cache[EVENTS_CACHE_SIZE];

	u64 total_time;
	u64 total_count;
	u64 lost_events;
	u64 duration;

	struct intlist *pid_list;

	struct rb_root result;

	int timerfd;
	unsigned int display_time;
	bool live;
};

struct kvm_reg_events_ops {
	const char *name;
	struct kvm_events_ops *ops;
};

void exit_event_get_key(struct perf_evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key);
bool exit_event_begin(struct perf_evsel *evsel,
		      struct perf_sample *sample,
		      struct event_key *key);
bool exit_event_end(struct perf_evsel *evsel,
		    struct perf_sample *sample,
		    struct event_key *key);
void exit_event_decode_key(struct perf_kvm_stat *kvm,
			   struct event_key *key,
			   char *decode);

bool kvm_exit_event(struct perf_evsel *evsel);
bool kvm_entry_event(struct perf_evsel *evsel);

#define define_exit_reasons_table(name, symbols)	\
	static struct exit_reasons_table name[] = {	\
		symbols, { -1, NULL }			\
	}

/*
 * arch specific callbacks and data structures
 */
int cpu_isa_init(struct perf_kvm_stat *kvm, const char *cpuid);

extern const char * const kvm_events_tp[];
extern struct kvm_reg_events_ops kvm_reg_events_ops[];
extern const char * const kvm_skip_events[];

#endif /* __PERF_KVM_STAT_H */
