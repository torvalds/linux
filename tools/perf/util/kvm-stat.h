/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_KVM_STAT_H
#define __PERF_KVM_STAT_H

#include "tool.h"
#include "sort.h"
#include "stat.h"
#include "symbol.h"
#include "record.h"

#include <errno.h>
#include <stdlib.h>
#include <linux/zalloc.h>

#define KVM_EVENT_NAME_LEN	40

struct evsel;
struct evlist;
struct perf_session;

struct event_key {
	#define INVALID_KEY     (~0ULL)
	u64 key;
	int info;
	struct exit_reasons_table *exit_reasons;
};

struct kvm_info {
	char name[KVM_EVENT_NAME_LEN];
	refcount_t refcnt;
};

struct kvm_event_stats {
	u64 time;
	struct stats stats;
};

struct perf_kvm_stat;

struct kvm_event {
	struct list_head hash_entry;

	struct perf_kvm_stat *perf_kvm;
	struct event_key key;

	struct kvm_event_stats total;

	#define DEFAULT_VCPU_NUM 8
	int max_vcpu;
	struct kvm_event_stats *vcpu;

	struct hist_entry he;
};

struct child_event_ops {
	void (*get_key)(struct evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key);
	const char *name;
};

struct kvm_events_ops {
	bool (*is_begin_event)(struct evsel *evsel,
			       struct perf_sample *sample,
			       struct event_key *key);
	bool (*is_end_event)(struct evsel *evsel,
			     struct perf_sample *sample, struct event_key *key);
	const struct child_event_ops *child_ops;
	void (*decode_key)(struct perf_kvm_stat *kvm, struct event_key *key,
			   char *decode);
	const char *name;
};

struct exit_reasons_table {
	unsigned long exit_code;
	const char *reason;
};

struct perf_kvm_stat {
	struct perf_tool    tool;
	struct record_opts  opts;
	struct evlist  *evlist;
	struct perf_session *session;

	const char *file_name;
	const char *report_event;
	const char *sort_key;
	int trace_vcpu;

	/* Used when process events */
	struct addr_location al;

	struct exit_reasons_table *exit_reasons;
	const char *exit_reasons_isa;

	const struct kvm_events_ops *events_ops;

	u64 total_time;
	u64 total_count;
	u64 lost_events;
	u64 duration;

	struct intlist *pid_list;

	int timerfd;
	unsigned int display_time;
	bool live;
	bool force;
	bool use_stdio;
};

struct kvm_reg_events_ops {
	const char *name;
	const struct kvm_events_ops *ops;
};

#ifdef HAVE_LIBTRACEEVENT

void exit_event_get_key(struct evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key);
bool exit_event_begin(struct evsel *evsel,
		      struct perf_sample *sample,
		      struct event_key *key);
bool exit_event_end(struct evsel *evsel,
		    struct perf_sample *sample,
		    struct event_key *key);
void exit_event_decode_key(struct perf_kvm_stat *kvm,
			   struct event_key *key,
			   char *decode);

bool kvm_exit_event(struct evsel *evsel);
bool kvm_entry_event(struct evsel *evsel);

#define define_exit_reasons_table(name, symbols)	\
	static struct exit_reasons_table name[] = {	\
		symbols, { -1, NULL }			\
	}

/*
 * arch specific callbacks and data structures
 */
int setup_kvm_events_tp(struct perf_kvm_stat *kvm, uint16_t e_machine);
int __setup_kvm_events_tp_powerpc(struct perf_kvm_stat *kvm);

int cpu_isa_init(struct perf_kvm_stat *kvm, uint16_t e_machine, const char *cpuid);
int __cpu_isa_init_arm64(struct perf_kvm_stat *kvm);
int __cpu_isa_init_loongarch(struct perf_kvm_stat *kvm);
int __cpu_isa_init_powerpc(struct perf_kvm_stat *kvm);
int __cpu_isa_init_riscv(struct perf_kvm_stat *kvm);
int __cpu_isa_init_s390(struct perf_kvm_stat *kvm, const char *cpuid);
int __cpu_isa_init_x86(struct perf_kvm_stat *kvm, const char *cpuid);

const char *vcpu_id_str(uint16_t e_machine);
const char *kvm_exit_reason(uint16_t e_machine);
const char *kvm_entry_trace(uint16_t e_machine);
const char *kvm_exit_trace(uint16_t e_machine);

const char * const *kvm_events_tp(uint16_t e_machine);
const char * const *__kvm_events_tp_arm64(void);
const char * const *__kvm_events_tp_loongarch(void);
const char * const *__kvm_events_tp_powerpc(void);
const char * const *__kvm_events_tp_riscv(void);
const char * const *__kvm_events_tp_s390(void);
const char * const *__kvm_events_tp_x86(void);

const struct kvm_reg_events_ops *kvm_reg_events_ops(uint16_t e_machine);
const struct kvm_reg_events_ops *__kvm_reg_events_ops_arm64(void);
const struct kvm_reg_events_ops *__kvm_reg_events_ops_loongarch(void);
const struct kvm_reg_events_ops *__kvm_reg_events_ops_powerpc(void);
const struct kvm_reg_events_ops *__kvm_reg_events_ops_riscv(void);
const struct kvm_reg_events_ops *__kvm_reg_events_ops_s390(void);
const struct kvm_reg_events_ops *__kvm_reg_events_ops_x86(void);

const char * const *kvm_skip_events(uint16_t e_machine);
const char * const *__kvm_skip_events_arm64(void);
const char * const *__kvm_skip_events_loongarch(void);
const char * const *__kvm_skip_events_powerpc(void);
const char * const *__kvm_skip_events_riscv(void);
const char * const *__kvm_skip_events_s390(void);
const char * const *__kvm_skip_events_x86(void);

int kvm_add_default_arch_event(uint16_t e_machine, int *argc, const char **argv);
int __kvm_add_default_arch_event_powerpc(int *argc, const char **argv);
int __kvm_add_default_arch_event_x86(int *argc, const char **argv);

#else /* !HAVE_LIBTRACEEVENT */

static inline int kvm_add_default_arch_event(uint16_t e_machine __maybe_unused,
					     int *argc __maybe_unused,
					     const char **argv __maybe_unused)
{
	return 0;
}

#endif /* HAVE_LIBTRACEEVENT */

static inline struct kvm_info *kvm_info__get(struct kvm_info *ki)
{
	if (ki)
		refcount_inc(&ki->refcnt);
	return ki;
}

static inline void kvm_info__put(struct kvm_info *ki)
{
	if (ki && refcount_dec_and_test(&ki->refcnt))
		free(ki);
}

static inline void __kvm_info__zput(struct kvm_info **ki)
{
	kvm_info__put(*ki);
	*ki = NULL;
}

#define kvm_info__zput(ki) __kvm_info__zput(&ki)

static inline struct kvm_info *kvm_info__new(void)
{
	struct kvm_info *ki;

	ki = zalloc(sizeof(*ki));
	if (ki)
		refcount_set(&ki->refcnt, 1);

	return ki;
}

#define STRDUP_FAIL_EXIT(s)		\
	({	char *_p;		\
		_p = strdup(s);		\
		if (!_p) {		\
			ret = -ENOMEM;	\
			goto EXIT;	\
		}			\
		_p;			\
	})

#endif /* __PERF_KVM_STAT_H */
