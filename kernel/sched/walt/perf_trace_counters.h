/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2014, 2017, 2021, The Linux Foundation. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM perf_trace_counters

#if !defined(_PERF_TRACE_COUNTERS_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _PERF_TRACE_COUNTERS_H_

/* Ctr index for PMCNTENSET/CLR */
#define CC 0x80000000
#define C0 0x1
#define C1 0x2
#define C2 0x4
#define C3 0x8
#define C4 0x10
#define C5 0x20
#define C_ALL (CC | C0 | C1 | C2 | C3 | C4 | C5)
#define TYPE_MASK 0xFFFF
#define NUM_L1_CTRS 6
#define NUM_AMU_CTRS 3

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/tracepoint.h>

DECLARE_PER_CPU(u32, cntenset_val);
DECLARE_PER_CPU(unsigned long, previous_ccnt);
DECLARE_PER_CPU(unsigned long[NUM_L1_CTRS], previous_l1_cnts);
DECLARE_PER_CPU(unsigned long[NUM_AMU_CTRS], previous_amu_cnts);

#ifdef CREATE_TRACE_POINTS
static inline long __trace_sched_switch_state(bool preempt, struct task_struct *p)
{
	unsigned int state;

#ifdef CONFIG_SCHED_DEBUG
	BUG_ON(p != current);
#endif /* CONFIG_SCHED_DEBUG */

	/*
	 * Preemption ignores task state, therefore preempted tasks are always
	 * RUNNING (we will not have dequeued if state != RUNNING).
	 */
	if (preempt)
		return TASK_REPORT_MAX;

	/*
	 * task_state_index() uses fls() and returns a value from 0-8 range.
	 * Decrement it by 1 (except TASK_RUNNING state i.e 0) before using
	 * it for left shift operation to get the correct task->state
	 * mapping.
	 */
	state = task_state_index(p);

	return state ? (1 << (state - 1)) : state;
}
#endif /* CREATE_TRACE_POINTS */

/* Check the AMU bits to judge AMU implementation in ID_AA64PFR0_EL1 */
#define cpu_has_amu \
	cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64PFR0_EL1), ID_AA64PFR0_EL1_AMU_SHIFT)

TRACE_EVENT(sched_switch_with_ctrs,

		TP_PROTO(bool preempt,
			struct task_struct *prev,
			struct task_struct *next),

		TP_ARGS(preempt, prev, next),

		TP_STRUCT__entry(
			__field(pid_t, prev_pid)
			__field(pid_t, next_pid)
			__array(char, prev_comm, TASK_COMM_LEN)
			__array(char, next_comm, TASK_COMM_LEN)
			__field(long, prev_state)
			__field(unsigned long, cctr)
			__field(unsigned long, ctr0)
			__field(unsigned long, ctr1)
			__field(unsigned long, ctr2)
			__field(unsigned long, ctr3)
			__field(unsigned long, ctr4)
			__field(unsigned long, ctr5)
			__field(unsigned long, amu0)
			__field(unsigned long, amu1)
			__field(unsigned long, amu2)
		),

		TP_fast_assign(
			u32 cpu = smp_processor_id();
			u32 i;
			u32 cnten_val;
			unsigned long total_ccnt = 0;
			unsigned long total_cnt = 0;
			unsigned long amu_cnt = 0;
			unsigned long delta_l1_cnts[NUM_L1_CTRS] = {0};
			unsigned long delta_amu_cnts[NUM_AMU_CTRS] = {0};

			memcpy(__entry->prev_comm, prev->comm, TASK_COMM_LEN);
			memcpy(__entry->next_comm, next->comm, TASK_COMM_LEN);
			__entry->prev_state	= __trace_sched_switch_state(preempt, prev);
			__entry->prev_pid	= prev->pid;
			__entry->next_pid	= next->pid;

			cnten_val = per_cpu(cntenset_val, cpu);

			if (cnten_val & CC) {
				/* Read value */
				total_ccnt = read_sysreg(pmccntr_el0);
				__entry->cctr = total_ccnt -
					per_cpu(previous_ccnt, cpu);
				per_cpu(previous_ccnt, cpu) = total_ccnt;
			}
			for (i = 0; i < NUM_L1_CTRS; i++) {
				if (cnten_val & (1 << i)) {
					/* Select */
					write_sysreg(i, pmselr_el0);
					isb();
					/* Read value */
					total_cnt = read_sysreg(pmxevcntr_el0);
					delta_l1_cnts[i] = total_cnt -
					  per_cpu(previous_l1_cnts[i], cpu);
					per_cpu(previous_l1_cnts[i], cpu) =
						total_cnt;
				} else
					delta_l1_cnts[i] = 0;
			}

			if (IS_ENABLED(CONFIG_ARM64_AMU_EXTN) && cpu_has_amu > 0) {
				amu_cnt = read_sysreg_s(SYS_AMEVCNTR0_CORE_EL0);
				delta_amu_cnts[0] = amu_cnt -
					per_cpu(previous_amu_cnts[0], cpu);
				per_cpu(previous_amu_cnts[0], cpu) = amu_cnt;

				amu_cnt = read_sysreg_s(SYS_AMEVCNTR0_INST_RET_EL0);
				delta_amu_cnts[1] = amu_cnt -
					per_cpu(previous_amu_cnts[1], cpu);
				per_cpu(previous_amu_cnts[1], cpu) = amu_cnt;

				amu_cnt = read_sysreg_s(SYS_AMEVCNTR0_MEM_STALL);
				delta_amu_cnts[2] = amu_cnt -
					per_cpu(previous_amu_cnts[2], cpu);
				per_cpu(previous_amu_cnts[2], cpu) = amu_cnt;
			}

			__entry->ctr0 = delta_l1_cnts[0];
			__entry->ctr1 = delta_l1_cnts[1];
			__entry->ctr2 = delta_l1_cnts[2];
			__entry->ctr3 = delta_l1_cnts[3];
			__entry->ctr4 = delta_l1_cnts[4];
			__entry->ctr5 = delta_l1_cnts[5];
			__entry->amu0 = delta_amu_cnts[0];
			__entry->amu1 = delta_amu_cnts[1];
			__entry->amu2 = delta_amu_cnts[2];
		),

		TP_printk("prev_comm=%s prev_pid=%d prev_state=%s%s ==> next_comm=%s next_pid=%d CCNTR=%u CTR0=%u CTR1=%u CTR2=%u CTR3=%u CTR4=%u CTR5=%u, CYC: %lu, INST: %lu, STALL: %lu",
			__entry->prev_comm, __entry->prev_pid,

			(__entry->prev_state & (TASK_REPORT_MAX - 1)) ?
			  __print_flags(__entry->prev_state & (TASK_REPORT_MAX - 1), "|",
					{ TASK_INTERRUPTIBLE, "S" },
					{ TASK_UNINTERRUPTIBLE, "D" },
					{ __TASK_STOPPED, "T" },
					{ __TASK_TRACED, "t" },
					{ EXIT_DEAD, "X" },
					{ EXIT_ZOMBIE, "Z" },
					{ TASK_PARKED, "P" },
					{ TASK_DEAD, "I" }) :
			"R",

			__entry->prev_state & TASK_REPORT_MAX ? "+" : "",
			__entry->next_comm,
			__entry->next_pid,
			__entry->cctr,
			__entry->ctr0, __entry->ctr1,
			__entry->ctr2, __entry->ctr3,
			__entry->ctr4, __entry->ctr5,
			__entry->amu0, __entry->amu1,
			__entry->amu2)
);

TRACE_EVENT(sched_switch_ctrs_cfg,

		TP_PROTO(int cpu),

		TP_ARGS(cpu),

		TP_STRUCT__entry(
			__field(int, cpu)
			__field(unsigned long, ctr0)
			__field(unsigned long, ctr1)
			__field(unsigned long, ctr2)
			__field(unsigned long, ctr3)
			__field(unsigned long, ctr4)
			__field(unsigned long, ctr5)
		),

		TP_fast_assign(
			u32 i;
			u32 cnten_val;
			u32 ctr_type[NUM_L1_CTRS] = {0};

			cnten_val = per_cpu(cntenset_val, cpu);

			for (i = 0; i < NUM_L1_CTRS; i++) {
				if (cnten_val & (1 << i)) {
					/* Select */
					write_sysreg(i, pmselr_el0);
					isb();
					/* Read type */
					ctr_type[i] = read_sysreg(pmxevtyper_el0)
								& TYPE_MASK;
				} else
					ctr_type[i] = 0;
			}

			__entry->cpu  = cpu;
			__entry->ctr0 = ctr_type[0];
			__entry->ctr1 = ctr_type[1];
			__entry->ctr2 = ctr_type[2];
			__entry->ctr3 = ctr_type[3];
			__entry->ctr4 = ctr_type[4];
			__entry->ctr5 = ctr_type[5];
		),

		TP_printk("cpu=%d CTR0=%lu CTR1=%lu CTR2=%lu CTR3=%lu CTR4=%lu CTR5=%lu",
				__entry->cpu,
				__entry->ctr0, __entry->ctr1,
				__entry->ctr2, __entry->ctr3,
				__entry->ctr4, __entry->ctr5)
);

#endif
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../kernel/sched/walt

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE perf_trace_counters
#include <trace/define_trace.h>
