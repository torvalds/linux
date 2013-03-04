#undef TRACE_SYSTEM
#define TRACE_SYSTEM migrate

#if !defined(_TRACE_MIGRATE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MIGRATE_H

#define MIGRATE_MODE						\
	{MIGRATE_ASYNC,		"MIGRATE_ASYNC"},		\
	{MIGRATE_SYNC_LIGHT,	"MIGRATE_SYNC_LIGHT"},		\
	{MIGRATE_SYNC,		"MIGRATE_SYNC"}		

#define MIGRATE_REASON						\
	{MR_COMPACTION,		"compaction"},			\
	{MR_MEMORY_FAILURE,	"memory_failure"},		\
	{MR_MEMORY_HOTPLUG,	"memory_hotplug"},		\
	{MR_SYSCALL,		"syscall_or_cpuset"},		\
	{MR_MEMPOLICY_MBIND,	"mempolicy_mbind"},		\
	{MR_CMA,		"cma"}

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

#endif /* _TRACE_MIGRATE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
