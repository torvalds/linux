#ifndef __VMLINUX_H
#define __VMLINUX_H

#include <linux/stddef.h> // for define __always_inline
#include <linux/bpf.h>
#include <linux/types.h>
#include <linux/perf_event.h>
#include <stdbool.h>

// non-UAPI kernel data structures, used in the .bpf.c BPF tool component.

// Just the fields used in these tools preserving the access index so that
// libbpf can fixup offsets with the ones used in the kernel when loading the
// BPF bytecode, if they differ from what is used here.

typedef __u8 u8;
typedef __u32 u32;
typedef __s32 s32;
typedef __u64 u64;
typedef __s64 s64;

typedef int pid_t;

typedef __s64 time64_t;

struct timespec64 {
        time64_t        tv_sec;
        long int        tv_nsec;
};

enum cgroup_subsys_id {
	perf_event_cgrp_id  = 8,
};

enum {
	HI_SOFTIRQ = 0,
	TIMER_SOFTIRQ,
	NET_TX_SOFTIRQ,
	NET_RX_SOFTIRQ,
	BLOCK_SOFTIRQ,
	IRQ_POLL_SOFTIRQ,
	TASKLET_SOFTIRQ,
	SCHED_SOFTIRQ,
	HRTIMER_SOFTIRQ,
	RCU_SOFTIRQ,    /* Preferable RCU should always be the last softirq */

	NR_SOFTIRQS
};

typedef struct {
	s64	counter;
} __attribute__((preserve_access_index)) atomic64_t;

typedef atomic64_t atomic_long_t;

struct raw_spinlock {
	int rawlock;
} __attribute__((preserve_access_index));

typedef struct raw_spinlock raw_spinlock_t;

typedef struct {
	struct raw_spinlock rlock;
} __attribute__((preserve_access_index)) spinlock_t;

struct sighand_struct {
	spinlock_t siglock;
} __attribute__((preserve_access_index));

struct rw_semaphore {
	atomic_long_t owner;
} __attribute__((preserve_access_index));

struct mutex {
	atomic_long_t owner;
} __attribute__((preserve_access_index));

struct kernfs_node {
	u64 id;
} __attribute__((preserve_access_index));

struct cgroup {
	struct kernfs_node *kn;
	int                level;
}  __attribute__((preserve_access_index));

struct cgroup_subsys_state {
	struct cgroup *cgroup;
} __attribute__((preserve_access_index));

struct css_set {
	struct cgroup_subsys_state *subsys[13];
	struct cgroup *dfl_cgrp;
} __attribute__((preserve_access_index));

struct mm_struct {
	struct rw_semaphore mmap_lock;
} __attribute__((preserve_access_index));

struct task_struct {
	unsigned int	      flags;
	struct mm_struct      *mm;
	pid_t		      pid;
	pid_t		      tgid;
	char		      comm[16];
	struct sighand_struct *sighand;
	struct css_set	      *cgroups;
} __attribute__((preserve_access_index));

struct trace_entry {
	short unsigned int type;
	unsigned char	   flags;
	unsigned char	   preempt_count;
	int		   pid;
} __attribute__((preserve_access_index));

struct trace_event_raw_irq_handler_entry {
	struct trace_entry ent;
	int		   irq;
	u32		   __data_loc_name;
	char		   __data[];
} __attribute__((preserve_access_index));

struct trace_event_raw_irq_handler_exit {
	struct trace_entry ent;
	int		   irq;
	int		   ret;
	char		   __data[];
} __attribute__((preserve_access_index));

struct trace_event_raw_softirq {
	struct trace_entry ent;
	unsigned int	   vec;
	char		   __data[];
} __attribute__((preserve_access_index));

struct trace_event_raw_workqueue_execute_start {
	struct trace_entry ent;
	void		   *work;
	void		   *function;
	char		   __data[];
} __attribute__((preserve_access_index));

struct trace_event_raw_workqueue_execute_end {
	struct trace_entry ent;
	void		   *work;
	void		   *function;
	char		  __data[];
} __attribute__((preserve_access_index));

struct trace_event_raw_workqueue_activate_work {
	struct trace_entry ent;
	void		   *work;
	char		   __data[];
} __attribute__((preserve_access_index));

struct perf_sample_data {
	u64			 addr;
	u64			 period;
	union perf_sample_weight weight;
	u64			 txn;
	union perf_mem_data_src	 data_src;
	u64			 ip;
	struct {
		u32		 pid;
		u32		 tid;
	} tid_entry;
	u64			 time;
	u64			 id;
	struct {
		u32		 cpu;
	} cpu_entry;
	u64			 phys_addr;
	u64			 cgroup;
	u64			 data_page_size;
	u64			 code_page_size;
} __attribute__((__aligned__(64))) __attribute__((preserve_access_index));

struct perf_event {
	struct perf_event	*parent;
	u64			id;
} __attribute__((preserve_access_index));

struct bpf_perf_event_data_kern {
	struct perf_sample_data *data;
	struct perf_event	*event;
} __attribute__((preserve_access_index));

/*
 * If 'struct rq' isn't defined for lock_contention.bpf.c, for the sake of
 * rq___old and rq___new, then the type for the 'runqueue' variable ends up
 * being a forward declaration (BTF_KIND_FWD) while the kernel has it defined
 * (BTF_KIND_STRUCT). The definition appears in vmlinux.h rather than
 * lock_contention.bpf.c for consistency with a generated vmlinux.h.
 */
struct rq {};

#endif // __VMLINUX_H
