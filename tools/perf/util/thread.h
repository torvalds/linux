/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_THREAD_H
#define __PERF_THREAD_H

#include <linux/refcount.h>
#include <linux/list.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include "srccode.h"
#include "symbol_conf.h"
#include <strlist.h>
#include <intlist.h>
#include "rwsem.h"
#include "callchain.h"
#include <internal/rc_check.h>

struct addr_location;
struct map;
struct perf_record_namespaces;
struct thread_stack;
struct unwind_libunwind_ops;

struct lbr_stitch {
	struct list_head		lists;
	struct list_head		free_lists;
	struct perf_sample		prev_sample;
	struct callchain_cursor_node	*prev_lbr_cursor;
	unsigned int prev_lbr_cursor_size;
};

DECLARE_RC_STRUCT(thread) {
	/** @maps: mmaps associated with this thread. */
	struct maps		*maps;
	pid_t			pid_; /* Not all tools update this */
	/** @tid: thread ID number unique to a machine. */
	pid_t			tid;
	/** @ppid: parent process of the process this thread belongs to. */
	pid_t			ppid;
	int			cpu;
	int			guest_cpu; /* For QEMU thread */
	refcount_t		refcnt;
	/**
	 * @exited: Has the thread had an exit event. Such threads are usually
	 * removed from the machine's threads but some events/tools require
	 * access to dead threads.
	 */
	bool			exited;
	bool			comm_set;
	int			comm_len;
	struct list_head	namespaces_list;
	struct rw_semaphore	namespaces_lock;
	struct list_head	comm_list;
	struct rw_semaphore	comm_lock;
	u64			db_id;

	void			*priv;
	struct thread_stack	*ts;
	struct nsinfo		*nsinfo;
	struct srccode_state	srccode_state;
	bool			filter;
	int			filter_entry_depth;

	/* LBR call stack stitch */
	bool			lbr_stitch_enable;
	struct lbr_stitch	*lbr_stitch;
};

struct machine;
struct namespaces;
struct comm;

struct thread *thread__new(pid_t pid, pid_t tid);
int thread__init_maps(struct thread *thread, struct machine *machine);
void thread__delete(struct thread *thread);

void thread__set_priv_destructor(void (*destructor)(void *priv));

struct thread *thread__get(struct thread *thread);
void thread__put(struct thread *thread);

static inline void __thread__zput(struct thread **thread)
{
	thread__put(*thread);
	*thread = NULL;
}

#define thread__zput(thread) __thread__zput(&thread)

struct namespaces *thread__namespaces(struct thread *thread);
int thread__set_namespaces(struct thread *thread, u64 timestamp,
			   struct perf_record_namespaces *event);

int __thread__set_comm(struct thread *thread, const char *comm, u64 timestamp,
		       bool exec);
static inline int thread__set_comm(struct thread *thread, const char *comm,
				   u64 timestamp)
{
	return __thread__set_comm(thread, comm, timestamp, false);
}

int thread__set_comm_from_proc(struct thread *thread);

int thread__comm_len(struct thread *thread);
struct comm *thread__comm(struct thread *thread);
struct comm *thread__exec_comm(struct thread *thread);
const char *thread__comm_str(struct thread *thread);
int thread__insert_map(struct thread *thread, struct map *map);
int thread__fork(struct thread *thread, struct thread *parent, u64 timestamp, bool do_maps_clone);
size_t thread__fprintf(struct thread *thread, FILE *fp);

struct thread *thread__main_thread(struct machine *machine, struct thread *thread);

struct map *thread__find_map(struct thread *thread, u8 cpumode, u64 addr,
			     struct addr_location *al);
struct map *thread__find_map_fb(struct thread *thread, u8 cpumode, u64 addr,
				struct addr_location *al);

struct symbol *thread__find_symbol(struct thread *thread, u8 cpumode,
				   u64 addr, struct addr_location *al);
struct symbol *thread__find_symbol_fb(struct thread *thread, u8 cpumode,
				      u64 addr, struct addr_location *al);

void thread__find_cpumode_addr_location(struct thread *thread, u64 addr,
					struct addr_location *al);

int thread__memcpy(struct thread *thread, struct machine *machine,
		   void *buf, u64 ip, int len, bool *is64bit);

static inline struct maps *thread__maps(struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->maps;
}

static inline void thread__set_maps(struct thread *thread, struct maps *maps)
{
	RC_CHK_ACCESS(thread)->maps = maps;
}

static inline pid_t thread__pid(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->pid_;
}

static inline void thread__set_pid(struct thread *thread, pid_t pid_)
{
	RC_CHK_ACCESS(thread)->pid_ = pid_;
}

static inline pid_t thread__tid(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->tid;
}

static inline void thread__set_tid(struct thread *thread, pid_t tid)
{
	RC_CHK_ACCESS(thread)->tid = tid;
}

static inline pid_t thread__ppid(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->ppid;
}

static inline void thread__set_ppid(struct thread *thread, pid_t ppid)
{
	RC_CHK_ACCESS(thread)->ppid = ppid;
}

static inline int thread__cpu(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->cpu;
}

static inline void thread__set_cpu(struct thread *thread, int cpu)
{
	RC_CHK_ACCESS(thread)->cpu = cpu;
}

static inline int thread__guest_cpu(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->guest_cpu;
}

static inline void thread__set_guest_cpu(struct thread *thread, int guest_cpu)
{
	RC_CHK_ACCESS(thread)->guest_cpu = guest_cpu;
}

static inline refcount_t *thread__refcnt(struct thread *thread)
{
	return &RC_CHK_ACCESS(thread)->refcnt;
}

static inline void thread__set_exited(struct thread *thread, bool exited)
{
	RC_CHK_ACCESS(thread)->exited = exited;
}

static inline bool thread__comm_set(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->comm_set;
}

static inline void thread__set_comm_set(struct thread *thread, bool set)
{
	RC_CHK_ACCESS(thread)->comm_set = set;
}

static inline int thread__var_comm_len(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->comm_len;
}

static inline void thread__set_comm_len(struct thread *thread, int len)
{
	RC_CHK_ACCESS(thread)->comm_len = len;
}

static inline struct list_head *thread__namespaces_list(struct thread *thread)
{
	return &RC_CHK_ACCESS(thread)->namespaces_list;
}

static inline int thread__namespaces_list_empty(const struct thread *thread)
{
	return list_empty(&RC_CHK_ACCESS(thread)->namespaces_list);
}

static inline struct rw_semaphore *thread__namespaces_lock(struct thread *thread)
{
	return &RC_CHK_ACCESS(thread)->namespaces_lock;
}

static inline struct list_head *thread__comm_list(struct thread *thread)
{
	return &RC_CHK_ACCESS(thread)->comm_list;
}

static inline struct rw_semaphore *thread__comm_lock(struct thread *thread)
{
	return &RC_CHK_ACCESS(thread)->comm_lock;
}

static inline u64 thread__db_id(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->db_id;
}

static inline void thread__set_db_id(struct thread *thread, u64 db_id)
{
	RC_CHK_ACCESS(thread)->db_id = db_id;
}

static inline void *thread__priv(struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->priv;
}

static inline void thread__set_priv(struct thread *thread, void *p)
{
	RC_CHK_ACCESS(thread)->priv = p;
}

static inline struct thread_stack *thread__ts(struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->ts;
}

static inline void thread__set_ts(struct thread *thread, struct thread_stack *ts)
{
	RC_CHK_ACCESS(thread)->ts = ts;
}

static inline struct nsinfo *thread__nsinfo(struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->nsinfo;
}

static inline struct srccode_state *thread__srccode_state(struct thread *thread)
{
	return &RC_CHK_ACCESS(thread)->srccode_state;
}

static inline bool thread__filter(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->filter;
}

static inline void thread__set_filter(struct thread *thread, bool filter)
{
	RC_CHK_ACCESS(thread)->filter = filter;
}

static inline int thread__filter_entry_depth(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->filter_entry_depth;
}

static inline void thread__set_filter_entry_depth(struct thread *thread, int depth)
{
	RC_CHK_ACCESS(thread)->filter_entry_depth = depth;
}

static inline bool thread__lbr_stitch_enable(const struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->lbr_stitch_enable;
}

static inline void thread__set_lbr_stitch_enable(struct thread *thread, bool en)
{
	RC_CHK_ACCESS(thread)->lbr_stitch_enable = en;
}

static inline struct lbr_stitch	*thread__lbr_stitch(struct thread *thread)
{
	return RC_CHK_ACCESS(thread)->lbr_stitch;
}

static inline void thread__set_lbr_stitch(struct thread *thread, struct lbr_stitch *lbrs)
{
	RC_CHK_ACCESS(thread)->lbr_stitch = lbrs;
}

static inline bool thread__is_filtered(struct thread *thread)
{
	if (symbol_conf.comm_list &&
	    !strlist__has_entry(symbol_conf.comm_list, thread__comm_str(thread))) {
		return true;
	}

	if (symbol_conf.pid_list &&
	    !intlist__has_entry(symbol_conf.pid_list, thread__pid(thread))) {
		return true;
	}

	if (symbol_conf.tid_list &&
	    !intlist__has_entry(symbol_conf.tid_list, thread__tid(thread))) {
		return true;
	}

	return false;
}

void thread__free_stitch_list(struct thread *thread);

void thread__resolve(struct thread *thread, struct addr_location *al,
		     struct perf_sample *sample);

#endif	/* __PERF_THREAD_H */
