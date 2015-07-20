#undef TRACE_SYSTEM
#define TRACE_SYSTEM migrate

#if !defined(_TRACE_MIGRATE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MIGRATE_H

#include <linux/tracepoint.h>

#define MIGRATE_MODE						\
	EM( MIGRATE_ASYNC,	"MIGRATE_ASYNC")		\
	EM( MIGRATE_SYNC_LIGHT,	"MIGRATE_SYNC_LIGHT")		\
	EMe(MIGRATE_SYNC,	"MIGRATE_SYNC")


#define MIGRATE_REASON						\
	EM( MR_COMPACTION,	"compaction")			\
	EM( MR_MEMORY_FAILURE,	"memory_failure")		\
	EM( MR_MEMORY_HOTPLUG,	"memory_hotplug")		\
	EM( MR_SYSCALL,		"syscall_or_cpuset")		\
	EM( MR_MEMPOLICY_MBIND,	"mempolicy_mbind")		\
	EM( MR_NUMA_MISPLACED,	"numa_misplaced")		\
	EMe(MR_CMA,		"cma")

/*
 * First define the enums in the above macros to be exported to userspace
 * via TRACE_DEFINE_ENUM().
 */
#undef EM
#undef EMe
#define EM(a, b)	TRACE_DEFINE_ENUM(a);
#define EMe(a, b)	TRACE_DEFINE_ENUM(a);

MIGRATE_MODE
MIGRATE_REASON

/*
 * Now redefine the EM() and EMe() macros to map the enums to the strings
 * that will be printed in the output.
 */
#undef EM
#undef EMe
#define EM(a, b)	{a, b},
#define EMe(a, b)	{a, b}

TRACE_EVENT(mm_migrate_pages,

	TP_PROTO(unsigned long succeeded, unsigned long failed,
		 enum migrate_mode mode, int reason),

	TP_ARGS(succeeded, failed, mode, reason),

	TP_STRUCT__entry(
		__field(	unsigned long,		succeeded)
		__field(	unsigned long,		failed)
		__field(	enum migrate_mode,	mode)
		__field(	int,			reason)
	),

	TP_fast_assign(
		__entry->succeeded	= succeeded;
		__entry->failed		= failed;
		__entry->mode		= mode;
		__entry->reason		= reason;
	),

	TP_printk("nr_succeeded=%lu nr_failed=%lu mode=%s reason=%s",
		__entry->succeeded,
		__entry->failed,
		__print_symbolic(__entry->mode, MIGRATE_MODE),
		__print_symbolic(__entry->reason, MIGRATE_REASON))
);

TRACE_EVENT(mm_numa_migrate_ratelimit,

	TP_PROTO(struct task_struct *p, int dst_nid, unsigned long nr_pages),

	TP_ARGS(p, dst_nid, nr_pages),

	TP_STRUCT__entry(
		__array(	char,		comm,	TASK_COMM_LEN)
		__field(	pid_t,		pid)
		__field(	int,		dst_nid)
		__field(	unsigned long,	nr_pages)
	),

	TP_fast_assign(
		memcpy(__entry->comm, p->comm, TASK_COMM_LEN);
		__entry->pid		= p->pid;
		__entry->dst_nid	= dst_nid;
		__entry->nr_pages	= nr_pages;
	),

	TP_printk("comm=%s pid=%d dst_nid=%d nr_pages=%lu",
		__entry->comm,
		__entry->pid,
		__entry->dst_nid,
		__entry->nr_pages)
);
#endif /* _TRACE_MIGRATE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
