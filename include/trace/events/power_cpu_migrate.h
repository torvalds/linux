#undef TRACE_SYSTEM
#define TRACE_SYSTEM power

#if !defined(_TRACE_POWER_CPU_MIGRATE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_POWER_CPU_MIGRATE_H

#include <linux/tracepoint.h>

#define __cpu_migrate_proto			\
	TP_PROTO(u64 timestamp,			\
		 u32 cpu_hwid)
#define __cpu_migrate_args			\
	TP_ARGS(timestamp,			\
		cpu_hwid)

DECLARE_EVENT_CLASS(cpu_migrate,

	__cpu_migrate_proto,
	__cpu_migrate_args,

	TP_STRUCT__entry(
		__field(u64,	timestamp		)
		__field(u32,	cpu_hwid		)
	),

	TP_fast_assign(
		__entry->timestamp = timestamp;
		__entry->cpu_hwid = cpu_hwid;
	),

	TP_printk("timestamp=%llu cpu_hwid=0x%08lX",
		(unsigned long long)__entry->timestamp,
		(unsigned long)__entry->cpu_hwid
	)
);

#define __define_cpu_migrate_event(name)		\
	DEFINE_EVENT(cpu_migrate, cpu_migrate_##name,	\
		__cpu_migrate_proto,			\
		__cpu_migrate_args			\
	)

__define_cpu_migrate_event(begin);
__define_cpu_migrate_event(finish);

#undef __define_cpu_migrate
#undef __cpu_migrate_proto
#undef __cpu_migrate_args

/* This file can get included multiple times, TRACE_HEADER_MULTI_READ at top */
#ifndef _PWR_CPU_MIGRATE_EVENT_AVOID_DOUBLE_DEFINING
#define _PWR_CPU_MIGRATE_EVENT_AVOID_DOUBLE_DEFINING

/*
 * Set from_phys_cpu and to_phys_cpu to CPU_MIGRATE_ALL_CPUS to indicate
 * a whole-cluster migration:
 */
#define CPU_MIGRATE_ALL_CPUS 0x80000000U
#endif

#endif /* _TRACE_POWER_CPU_MIGRATE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE power_cpu_migrate
#include <trace/define_trace.h>
