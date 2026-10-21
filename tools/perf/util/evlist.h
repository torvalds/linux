/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_EVLIST_H
#define __PERF_EVLIST_H 1

#include <signal.h>

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/refcount.h>
#include <pthread.h>
#include <unistd.h>

#include <api/fd/array.h>
#include <internal/evlist.h>
#include <internal/evsel.h>
#include <internal/rc_check.h>
#include <perf/evlist.h>

#include "affinity.h"
#include "events_stats.h"
#include "evsel.h"
#include "rblist.h"

struct perf_cpu_map;
struct perf_stat_config;
struct pollfd;
struct record_opts;
struct strbuf;
struct target;
struct thread_map;

/*
 * State machine of bkw_mmap_state:
 *
 *                     .________________(forbid)_____________.
 *                     |                                     V
 * NOTREADY --(0)--> RUNNING --(1)--> DATA_PENDING --(2)--> EMPTY
 *                     ^  ^              |   ^               |
 *                     |  |__(forbid)____/   |___(forbid)___/|
 *                     |                                     |
 *                      \_________________(3)_______________/
 *
 * NOTREADY     : Backward ring buffers are not ready
 * RUNNING      : Backward ring buffers are recording
 * DATA_PENDING : We are required to collect data from backward ring buffers
 * EMPTY        : We have collected data from backward ring buffers.
 *
 * (0): Setup backward ring buffer
 * (1): Pause ring buffers for reading
 * (2): Read from ring buffers
 * (3): Resume ring buffers for recording
 */
enum bkw_mmap_state {
	BKW_MMAP_NOTREADY,
	BKW_MMAP_RUNNING,
	BKW_MMAP_DATA_PENDING,
	BKW_MMAP_EMPTY,
};

struct event_enable_timer;

DECLARE_RC_STRUCT(evlist) {
	struct perf_evlist core;
	refcount_t	 refcnt;
	bool		 enabled;
	bool		 no_affinity;
	int		 id_pos;
	int		 is_pos;
	int		 nr_br_cntr;
	u64		 combined_sample_type;
	enum bkw_mmap_state bkw_mmap_state;
	struct {
		int	cork_fd;
		pid_t	pid;
	} workload;
	struct mmap *mmap;
	struct mmap *overwrite_mmap;
	struct evsel *selected;
	struct events_stats stats;
	struct perf_session *session;
	void (*trace_event_sample_raw)(struct evlist *evlist,
				       union perf_event *event,
				       struct perf_sample *sample);
	u64		first_sample_time;
	u64		last_sample_time;
	struct {
		pthread_t		th;
		volatile int		done;
	} sb_thread;
	struct {
		int	fd;	/* control file descriptor */
		int	ack;	/* ack file descriptor for control commands */
		int	pos;	/* index at evlist core object to check signals */
	} ctl_fd;
	struct event_enable_timer *eet;
	/**
	 * @metric_events: A list of struct metric_event which each have a list
	 * of struct metric_expr.
	 */
	struct rblist	metric_events;
	/* samples with deferred_callchain would wait here. */
	struct list_head deferred_samples;
};

struct evsel_str_handler {
	const char *name;
	void	   *handler;
};

static inline struct perf_evlist *evlist__core(struct evlist *evlist)
{
	return &RC_CHK_ACCESS(evlist)->core;
}

static inline const struct perf_evlist *evlist__const_core(const struct evlist *evlist)
{
	return &RC_CHK_ACCESS(evlist)->core;
}

static inline int evlist__nr_entries(const struct evlist *evlist)
{
	return evlist__const_core(evlist)->nr_entries;
}

static inline bool evlist__enabled(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->enabled;
}

static inline void evlist__set_enabled(struct evlist *evlist, bool enabled)
{
	RC_CHK_ACCESS(evlist)->enabled = enabled;
}

static inline bool evlist__no_affinity(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->no_affinity;
}

static inline void evlist__set_no_affinity(struct evlist *evlist, bool no_affinity)
{
	RC_CHK_ACCESS(evlist)->no_affinity = no_affinity;
}

static inline int evlist__sb_thread_done(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->sb_thread.done;
}

static inline void evlist__set_sb_thread_done(struct evlist *evlist, int done)
{
	RC_CHK_ACCESS(evlist)->sb_thread.done = done;
}

static inline pthread_t *evlist__sb_thread_th(struct evlist *evlist)
{
	return &RC_CHK_ACCESS(evlist)->sb_thread.th;
}

static inline int evlist__id_pos(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->id_pos;
}

static inline int evlist__is_pos(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->is_pos;
}

static inline struct event_enable_timer *evlist__event_enable_timer(struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->eet;
}

static inline enum bkw_mmap_state evlist__bkw_mmap_state(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->bkw_mmap_state;
}

static inline void evlist__set_bkw_mmap_state(struct evlist *evlist, enum bkw_mmap_state state)
{
	RC_CHK_ACCESS(evlist)->bkw_mmap_state = state;
}

static inline struct mmap *evlist__mmap(struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->mmap;
}

static inline struct mmap *evlist__overwrite_mmap(struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->overwrite_mmap;
}

static inline struct events_stats *evlist__stats(struct evlist *evlist)
{
	return &RC_CHK_ACCESS(evlist)->stats;
}

static inline u64 evlist__first_sample_time(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->first_sample_time;
}

static inline void evlist__set_first_sample_time(struct evlist *evlist, u64 first)
{
	RC_CHK_ACCESS(evlist)->first_sample_time = first;
}

static inline u64 evlist__last_sample_time(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->last_sample_time;
}

static inline void evlist__set_last_sample_time(struct evlist *evlist, u64 last)
{
	RC_CHK_ACCESS(evlist)->last_sample_time = last;
}

static inline int evlist__nr_br_cntr(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->nr_br_cntr;
}

static inline void evlist__set_nr_br_cntr(struct evlist *evlist, int nr)
{
	RC_CHK_ACCESS(evlist)->nr_br_cntr = nr;
}

static inline struct perf_session *evlist__session(struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->session;
}

static inline void evlist__set_session(struct evlist *evlist, struct perf_session *session)
{
	RC_CHK_ACCESS(evlist)->session = session;
}

static inline void (*evlist__trace_event_sample_raw(struct evlist *evlist))
			(struct evlist *evlist,
			 union perf_event *event,
			 struct perf_sample *sample)
{
	return RC_CHK_ACCESS(evlist)->trace_event_sample_raw;
}

static inline void evlist__set_trace_event_sample_raw(struct evlist *evlist,
						void (*fun)(struct evlist *evlist,
							union perf_event *event,
							struct perf_sample *sample))
{
	RC_CHK_ACCESS(evlist)->trace_event_sample_raw = fun;
}

static inline pid_t evlist__workload_pid(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->workload.pid;
}

static inline void evlist__set_workload_pid(struct evlist *evlist, pid_t pid)
{
	RC_CHK_ACCESS(evlist)->workload.pid = pid;
}

static inline int evlist__workload_cork_fd(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->workload.cork_fd;
}

static inline void evlist__set_workload_cork_fd(struct evlist *evlist, int cork_fd)
{
	RC_CHK_ACCESS(evlist)->workload.cork_fd = cork_fd;
}

static inline int evlist__ctl_fd_fd(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->ctl_fd.fd;
}

static inline void evlist__set_ctl_fd_fd(struct evlist *evlist, int fd)
{
	RC_CHK_ACCESS(evlist)->ctl_fd.fd = fd;
}

static inline int evlist__ctl_fd_ack(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->ctl_fd.ack;
}

static inline void evlist__set_ctl_fd_ack(struct evlist *evlist, int ack)
{
	RC_CHK_ACCESS(evlist)->ctl_fd.ack = ack;
}

static inline int evlist__ctl_fd_pos(const struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->ctl_fd.pos;
}

static inline void evlist__set_ctl_fd_pos(struct evlist *evlist, int pos)
{
	RC_CHK_ACCESS(evlist)->ctl_fd.pos = pos;
}

static inline refcount_t *evlist__refcnt(struct evlist *evlist)
{
	return &RC_CHK_ACCESS(evlist)->refcnt;
}

static inline struct rblist *evlist__metric_events(struct evlist *evlist)
{
	return &RC_CHK_ACCESS(evlist)->metric_events;
}

static inline struct list_head *evlist__deferred_samples(struct evlist *evlist)
{
	return &RC_CHK_ACCESS(evlist)->deferred_samples;
}

static inline struct evsel *evlist__selected(struct evlist *evlist)
{
	return RC_CHK_ACCESS(evlist)->selected;
}

static inline void evlist__set_selected(struct evlist *evlist, struct evsel *evsel)
{
	RC_CHK_ACCESS(evlist)->selected = evsel;
}

struct evlist *evlist__new(void);
struct evlist *evlist__new_default(const struct target *target, bool sample_callchains);
struct evlist *evlist__new_dummy(void);
struct evlist *evlist__get(struct evlist *evlist);
void evlist__put(struct evlist *evlist);

void evlist__add(struct evlist *evlist, struct evsel *entry);
void evlist__remove(struct evlist *evlist, struct evsel *evsel);

int arch_evlist__cmp(const struct evsel *lhs, const struct evsel *rhs);
int arch_evlist__add_required_events(struct list_head *list);

int evlist__add_dummy(struct evlist *evlist);
struct evsel *evlist__add_aux_dummy(struct evlist *evlist, bool system_wide);
static inline struct evsel *evlist__add_dummy_on_all_cpus(struct evlist *evlist)
{
	return evlist__add_aux_dummy(evlist, true);
}
#ifdef HAVE_LIBTRACEEVENT
struct evsel *evlist__add_sched_switch(struct evlist *evlist, bool system_wide);
#endif

int evlist__add_sb_event(struct evlist *evlist, struct perf_event_attr *attr,
			 evsel__sb_cb_t cb, void *data);
void evlist__set_cb(struct evlist *evlist, evsel__sb_cb_t cb, void *data);
int evlist__start_sb_thread(struct evlist *evlist, struct target *target);
void evlist__stop_sb_thread(struct evlist *evlist);

#ifdef HAVE_LIBTRACEEVENT
int evlist__add_newtp(struct evlist *evlist, const char *sys, const char *name, void *handler);
#endif

int __evlist__set_tracepoints_handlers(struct evlist *evlist,
				       const struct evsel_str_handler *assocs,
				       size_t nr_assocs);

#define evlist__set_tracepoints_handlers(evlist, array) \
	__evlist__set_tracepoints_handlers(evlist, array, ARRAY_SIZE(array))

int evlist__set_tp_filter(struct evlist *evlist, const char *filter);
int evlist__set_tp_filter_pids(struct evlist *evlist, size_t npids, pid_t *pids);

int evlist__append_tp_filter(struct evlist *evlist, const char *filter);

int evlist__append_tp_filter_pid(struct evlist *evlist, pid_t pid);
int evlist__append_tp_filter_pids(struct evlist *evlist, size_t npids, pid_t *pids);

struct evsel *evlist__find_tracepoint_by_name(struct evlist *evlist, const char *name);

int evlist__add_pollfd(struct evlist *evlist, int fd);
int evlist__filter_pollfd(struct evlist *evlist, short revents_and_mask);

#ifdef HAVE_EVENTFD_SUPPORT
int evlist__add_wakeup_eventfd(struct evlist *evlist, int fd);
#endif

int evlist__poll(struct evlist *evlist, int timeout);

struct evsel *evlist__id2evsel(struct evlist *evlist, u64 id);
struct evsel *evlist__id2evsel_strict(struct evlist *evlist, u64 id);

struct perf_sample_id *evlist__id2sid(struct evlist *evlist, u64 id);

void evlist__toggle_bkw_mmap(struct evlist *evlist, enum bkw_mmap_state state);

void evlist__mmap_consume(struct evlist *evlist, int idx);

int evlist__open(struct evlist *evlist);
void evlist__close(struct evlist *evlist);

struct callchain_param;

void evlist__set_id_pos(struct evlist *evlist);
void evlist__config(struct evlist *evlist, struct record_opts *opts, struct callchain_param *callchain);
int record_opts__config(struct record_opts *opts);

int evlist__prepare_workload(struct evlist *evlist, struct target *target,
			     const char *argv[], bool pipe_output,
			     void (*exec_error)(int signo, siginfo_t *info, void *ucontext));
int evlist__start_workload(struct evlist *evlist);
void evlist__cancel_workload(struct evlist *evlist);

struct option;

int __evlist__parse_mmap_pages(unsigned int *mmap_pages, const char *str);
int evlist__parse_mmap_pages(const struct option *opt, const char *str, int unset);

unsigned long perf_event_mlock_kb_in_pages(void);

int evlist__mmap_ex(struct evlist *evlist, unsigned int pages,
			 unsigned int auxtrace_pages,
			 bool auxtrace_overwrite, int nr_cblocks,
			 int affinity, int flush, int comp_level);
int evlist__do_mmap(struct evlist *evlist, unsigned int pages);
void evlist__do_munmap(struct evlist *evlist);

size_t evlist__mmap_size(unsigned long pages);

void evlist__disable(struct evlist *evlist);
void evlist__enable(struct evlist *evlist);
void evlist__toggle_enable(struct evlist *evlist);
void evlist__disable_evsel(struct evlist *evlist, char *evsel_name);
void evlist__enable_evsel(struct evlist *evlist, char *evsel_name);
void evlist__disable_non_dummy(struct evlist *evlist);
void evlist__enable_non_dummy(struct evlist *evlist);

int evlist__create_maps(struct evlist *evlist, struct target *target);
int evlist__apply_filters(struct evlist *evlist, struct evsel **err_evsel,
			  struct target *target);

u64 __evlist__combined_sample_type(struct evlist *evlist);
u64 evlist__combined_sample_type(struct evlist *evlist);
u64 evlist__combined_branch_type(struct evlist *evlist);
void evlist__update_br_cntr(struct evlist *evlist);
bool evlist__sample_id_all(struct evlist *evlist);
u16 evlist__id_hdr_size(struct evlist *evlist);

int evlist__parse_sample(struct evlist *evlist, union perf_event *event, struct perf_sample *sample);
int evlist__parse_sample_timestamp(struct evlist *evlist, union perf_event *event, u64 *timestamp);

bool evlist__valid_sample_type(struct evlist *evlist);
bool evlist__valid_sample_id_all(struct evlist *evlist);
bool evlist__valid_read_format(struct evlist *evlist);

void evlist__splice_list_tail(struct evlist *evlist, struct list_head *list);

static inline bool evlist__empty(struct evlist *evlist)
{
	return list_empty(&evlist__core(evlist)->entries);
}

static inline struct evsel *evlist__first(struct evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__first(evlist__core(evlist));

	return container_of(evsel, struct evsel, core);
}

static inline struct evsel *evlist__last(struct evlist *evlist)
{
	struct perf_evsel *evsel = perf_evlist__last(evlist__core(evlist));

	return container_of(evsel, struct evsel, core);
}

static inline int evlist__nr_groups(struct evlist *evlist)
{
	return perf_evlist__nr_groups(evlist__core(evlist));
}

int evlist__strerror_open(struct evlist *evlist, int err, char *buf, size_t size);
int evlist__strerror_mmap(struct evlist *evlist, int err, char *buf, size_t size);

bool evlist__can_select_event(struct evlist *evlist, const char *str);
void evlist__to_front(struct evlist *evlist, struct evsel *move_evsel);

/**
 * __evlist__for_each_entry - iterate thru all the evsels
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry(list, evsel) \
        list_for_each_entry(evsel, list, core.node)

/**
 * evlist__for_each_entry - iterate thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_entry(evlist, evsel) \
	__evlist__for_each_entry(&evlist__core(evlist)->entries, evsel)

/**
 * __evlist__for_each_entry_continue - continue iteration thru all the evsels
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry_continue(list, evsel) \
        list_for_each_entry_continue(evsel, list, core.node)

/**
 * evlist__for_each_entry_continue - continue iteration thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_entry_continue(evlist, evsel) \
	__evlist__for_each_entry_continue(&evlist__core(evlist)->entries, evsel)

/**
 * __evlist__for_each_entry_from - continue iteration from @evsel (included)
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry_from(list, evsel) \
	list_for_each_entry_from(evsel, list, core.node)

/**
 * evlist__for_each_entry_from - continue iteration from @evsel (included)
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_entry_from(evlist, evsel) \
	__evlist__for_each_entry_from(&evlist__core(evlist)->entries, evsel)

/**
 * __evlist__for_each_entry_reverse - iterate thru all the evsels in reverse order
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry_reverse(list, evsel) \
        list_for_each_entry_reverse(evsel, list, core.node)

/**
 * evlist__for_each_entry_reverse - iterate thru all the evsels in reverse order
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_entry_reverse(evlist, evsel) \
	__evlist__for_each_entry_reverse(&evlist__core(evlist)->entries, evsel)

/**
 * __evlist__for_each_entry_safe - safely iterate thru all the evsels
 * @list: list_head instance to iterate
 * @tmp: struct evsel temp iterator
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry_safe(list, tmp, evsel) \
        list_for_each_entry_safe(evsel, tmp, list, core.node)

/**
 * evlist__for_each_entry_safe - safely iterate thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 * @tmp: struct evsel temp iterator
 */
#define evlist__for_each_entry_safe(evlist, tmp, evsel) \
	__evlist__for_each_entry_safe(&evlist__core(evlist)->entries, tmp, evsel)

/** Iterator state for evlist__for_each_cpu */
struct evlist_cpu_iterator {
	/** The list being iterated through. */
	struct evlist *container;
	/** The current evsel of the iterator. */
	struct evsel *evsel;
	/** The CPU map index corresponding to the evsel->core.cpus for the current CPU. */
	int cpu_map_idx;
	/**
	 * The CPU map index corresponding to evlist->core.all_cpus for the
	 * current CPU.  Distinct from cpu_map_idx as the evsel's cpu map may
	 * contain fewer entries.
	 */
	int evlist_cpu_map_idx;
	/** The number of CPU map entries in evlist->core.all_cpus. */
	int evlist_cpu_map_nr;
	/** The current CPU of the iterator. */
	struct perf_cpu cpu;
	/** If present, used to set the affinity when switching between CPUs. */
	struct affinity *affinity;
	/** Maybe be used to hold affinity state prior to iterating. */
	struct affinity saved_affinity;
};

/**
 * evlist__for_each_cpu - without affinity, iterate over the evlist. With
 *                        affinity, iterate over all CPUs and then the evlist
 *                        for each evsel on that CPU. When switching between
 *                        CPUs the affinity is set to the CPU to avoid IPIs
 *                        during syscalls. The affinity is set up and removed
 *                        automatically, if the loop is broken a call to
 *                        evlist_cpu_iterator__exit is necessary.
 * @evlist_cpu_itr: the iterator instance.
 * @evlist: evlist instance to iterate.
 */
#define evlist__for_each_cpu(evlist_cpu_itr, evlist)			\
	for (evlist_cpu_iterator__init(&(evlist_cpu_itr), evlist);	\
	     !evlist_cpu_iterator__end(&evlist_cpu_itr);		\
	     evlist_cpu_iterator__next(&evlist_cpu_itr))

/** Setup an iterator set to the first CPU/evsel of evlist. */
void evlist_cpu_iterator__init(struct evlist_cpu_iterator *itr, struct evlist *evlist);
/**
 * Cleans up the iterator, automatically done by evlist_cpu_iterator__next when
 * the end of the list is reached. Multiple calls are safe.
 */
void evlist_cpu_iterator__exit(struct evlist_cpu_iterator *itr);
/** Move to next element in iterator, updating CPU, evsel and the affinity. */
void evlist_cpu_iterator__next(struct evlist_cpu_iterator *evlist_cpu_itr);
/** Returns true when iterator is at the end of the CPUs and evlist. */
static inline bool evlist_cpu_iterator__end(const struct evlist_cpu_iterator *evlist_cpu_itr)
{
	return evlist_cpu_itr->evlist_cpu_map_idx >= evlist_cpu_itr->evlist_cpu_map_nr;
}

struct evsel *evlist__get_tracking_event(struct evlist *evlist);
void evlist__set_tracking_event(struct evlist *evlist, struct evsel *tracking_evsel);
struct evsel *evlist__findnew_tracking_event(struct evlist *evlist, bool system_wide);

struct evsel *evlist__find_evsel_by_str(struct evlist *evlist, const char *str);

struct evsel *evlist__event2evsel(struct evlist *evlist, union perf_event *event);

bool evlist__exclude_kernel(struct evlist *evlist);

void evlist__force_leader(struct evlist *evlist);

struct evsel *evlist__reset_weak_group(struct evlist *evlist, struct evsel *evsel, bool close);

#define EVLIST_CTL_CMD_ENABLE_TAG  "enable"
#define EVLIST_CTL_CMD_DISABLE_TAG "disable"
#define EVLIST_CTL_CMD_ACK_TAG     "ack\n"
#define EVLIST_CTL_CMD_SNAPSHOT_TAG "snapshot"
#define EVLIST_CTL_CMD_EVLIST_TAG "evlist"
#define EVLIST_CTL_CMD_STOP_TAG "stop"
#define EVLIST_CTL_CMD_PING_TAG "ping"

#define EVLIST_CTL_CMD_MAX_LEN 64

enum evlist_ctl_cmd {
	EVLIST_CTL_CMD_UNSUPPORTED = 0,
	EVLIST_CTL_CMD_ENABLE,
	EVLIST_CTL_CMD_DISABLE,
	EVLIST_CTL_CMD_ACK,
	EVLIST_CTL_CMD_SNAPSHOT,
	EVLIST_CTL_CMD_EVLIST,
	EVLIST_CTL_CMD_STOP,
	EVLIST_CTL_CMD_PING,
};

int evlist__parse_control(const char *str, int *ctl_fd, int *ctl_fd_ack, bool *ctl_fd_close);
void evlist__close_control(int ctl_fd, int ctl_fd_ack, bool *ctl_fd_close);
int evlist__initialize_ctlfd(struct evlist *evlist, int ctl_fd, int ctl_fd_ack);
int evlist__finalize_ctlfd(struct evlist *evlist);
bool evlist__ctlfd_initialized(struct evlist *evlist);
int evlist__ctlfd_process(struct evlist *evlist, enum evlist_ctl_cmd *cmd);
int evlist__ctlfd_ack(struct evlist *evlist);

#define EVLIST_ENABLED_MSG "Events enabled\n"
#define EVLIST_DISABLED_MSG "Events disabled\n"

int evlist__parse_event_enable_time(struct evlist *evlist, struct record_opts *opts,
				    const char *str, int unset);
int event_enable_timer__start(struct event_enable_timer *eet);
int event_enable_timer__process(struct event_enable_timer *eet);

struct evsel *evlist__find_evsel(struct evlist *evlist, int idx);

void evlist__format_evsels(struct evlist *evlist, struct strbuf *sb, size_t max_length);
void evlist__check_mem_load_aux(struct evlist *evlist);
void evlist__warn_user_requested_cpus(struct evlist *evlist, const char *cpu_list);
void evlist__uniquify_evsel_names(struct evlist *evlist, const struct perf_stat_config *config);
bool evlist__has_bpf_output(struct evlist *evlist);
bool evlist__needs_bpf_sb_event(struct evlist *evlist);

#endif /* __PERF_EVLIST_H */
