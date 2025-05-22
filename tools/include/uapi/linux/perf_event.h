/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Performance events:
 *
 *    Copyright (C) 2008-2009, Thomas Gleixner <tglx@linutronix.de>
 *    Copyright (C) 2008-2011, Red Hat, Inc., Ingo Molnar
 *    Copyright (C) 2008-2011, Red Hat, Inc., Peter Zijlstra
 *
 * Data type definitions, declarations, prototypes.
 *
 *    Started by: Thomas Gleixner and Ingo Molnar
 *
 * For licencing details see kernel-base/COPYING
 */
#ifndef _UAPI_LINUX_PERF_EVENT_H
#define _UAPI_LINUX_PERF_EVENT_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <asm/byteorder.h>

/*
 * User-space ABI bits:
 */

/*
 * attr.type
 */
enum perf_type_id {
	PERF_TYPE_HARDWARE			= 0,
	PERF_TYPE_SOFTWARE			= 1,
	PERF_TYPE_TRACEPOINT			= 2,
	PERF_TYPE_HW_CACHE			= 3,
	PERF_TYPE_RAW				= 4,
	PERF_TYPE_BREAKPOINT			= 5,

	PERF_TYPE_MAX,				/* non-ABI */
};

/*
 * attr.config layout for type PERF_TYPE_HARDWARE and PERF_TYPE_HW_CACHE
 *
 * PERF_TYPE_HARDWARE:			0xEEEEEEEE000000AA
 *					AA: hardware event ID
 *					EEEEEEEE: PMU type ID
 *
 * PERF_TYPE_HW_CACHE:			0xEEEEEEEE00DDCCBB
 *					BB: hardware cache ID
 *					CC: hardware cache op ID
 *					DD: hardware cache op result ID
 *					EEEEEEEE: PMU type ID
 *
 * If the PMU type ID is 0, PERF_TYPE_RAW will be applied.
 */
#define PERF_PMU_TYPE_SHIFT			32
#define PERF_HW_EVENT_MASK			0xffffffff

/*
 * Generalized performance event event_id types, used by the
 * attr.event_id parameter of the sys_perf_event_open()
 * syscall:
 */
enum perf_hw_id {
	/*
	 * Common hardware events, generalized by the kernel:
	 */
	PERF_COUNT_HW_CPU_CYCLES		= 0,
	PERF_COUNT_HW_INSTRUCTIONS		= 1,
	PERF_COUNT_HW_CACHE_REFERENCES		= 2,
	PERF_COUNT_HW_CACHE_MISSES		= 3,
	PERF_COUNT_HW_BRANCH_INSTRUCTIONS	= 4,
	PERF_COUNT_HW_BRANCH_MISSES		= 5,
	PERF_COUNT_HW_BUS_CYCLES		= 6,
	PERF_COUNT_HW_STALLED_CYCLES_FRONTEND	= 7,
	PERF_COUNT_HW_STALLED_CYCLES_BACKEND	= 8,
	PERF_COUNT_HW_REF_CPU_CYCLES		= 9,

	PERF_COUNT_HW_MAX,			/* non-ABI */
};

/*
 * Generalized hardware cache events:
 *
 *       { L1-D, L1-I, LLC, ITLB, DTLB, BPU, NODE } x
 *       { read, write, prefetch } x
 *       { accesses, misses }
 */
enum perf_hw_cache_id {
	PERF_COUNT_HW_CACHE_L1D			= 0,
	PERF_COUNT_HW_CACHE_L1I			= 1,
	PERF_COUNT_HW_CACHE_LL			= 2,
	PERF_COUNT_HW_CACHE_DTLB		= 3,
	PERF_COUNT_HW_CACHE_ITLB		= 4,
	PERF_COUNT_HW_CACHE_BPU			= 5,
	PERF_COUNT_HW_CACHE_NODE		= 6,

	PERF_COUNT_HW_CACHE_MAX,		/* non-ABI */
};

enum perf_hw_cache_op_id {
	PERF_COUNT_HW_CACHE_OP_READ		= 0,
	PERF_COUNT_HW_CACHE_OP_WRITE		= 1,
	PERF_COUNT_HW_CACHE_OP_PREFETCH		= 2,

	PERF_COUNT_HW_CACHE_OP_MAX,		/* non-ABI */
};

enum perf_hw_cache_op_result_id {
	PERF_COUNT_HW_CACHE_RESULT_ACCESS	= 0,
	PERF_COUNT_HW_CACHE_RESULT_MISS		= 1,

	PERF_COUNT_HW_CACHE_RESULT_MAX,		/* non-ABI */
};

/*
 * Special "software" events provided by the kernel, even if the hardware
 * does not support performance events. These events measure various
 * physical and SW events of the kernel (and allow the profiling of them as
 * well):
 */
enum perf_sw_ids {
	PERF_COUNT_SW_CPU_CLOCK			= 0,
	PERF_COUNT_SW_TASK_CLOCK		= 1,
	PERF_COUNT_SW_PAGE_FAULTS		= 2,
	PERF_COUNT_SW_CONTEXT_SWITCHES		= 3,
	PERF_COUNT_SW_CPU_MIGRATIONS		= 4,
	PERF_COUNT_SW_PAGE_FAULTS_MIN		= 5,
	PERF_COUNT_SW_PAGE_FAULTS_MAJ		= 6,
	PERF_COUNT_SW_ALIGNMENT_FAULTS		= 7,
	PERF_COUNT_SW_EMULATION_FAULTS		= 8,
	PERF_COUNT_SW_DUMMY			= 9,
	PERF_COUNT_SW_BPF_OUTPUT		= 10,
	PERF_COUNT_SW_CGROUP_SWITCHES		= 11,

	PERF_COUNT_SW_MAX,			/* non-ABI */
};

/*
 * Bits that can be set in attr.sample_type to request information
 * in the overflow packets.
 */
enum perf_event_sample_format {
	PERF_SAMPLE_IP				= 1U << 0,
	PERF_SAMPLE_TID				= 1U << 1,
	PERF_SAMPLE_TIME			= 1U << 2,
	PERF_SAMPLE_ADDR			= 1U << 3,
	PERF_SAMPLE_READ			= 1U << 4,
	PERF_SAMPLE_CALLCHAIN			= 1U << 5,
	PERF_SAMPLE_ID				= 1U << 6,
	PERF_SAMPLE_CPU				= 1U << 7,
	PERF_SAMPLE_PERIOD			= 1U << 8,
	PERF_SAMPLE_STREAM_ID			= 1U << 9,
	PERF_SAMPLE_RAW				= 1U << 10,
	PERF_SAMPLE_BRANCH_STACK		= 1U << 11,
	PERF_SAMPLE_REGS_USER			= 1U << 12,
	PERF_SAMPLE_STACK_USER			= 1U << 13,
	PERF_SAMPLE_WEIGHT			= 1U << 14,
	PERF_SAMPLE_DATA_SRC			= 1U << 15,
	PERF_SAMPLE_IDENTIFIER			= 1U << 16,
	PERF_SAMPLE_TRANSACTION			= 1U << 17,
	PERF_SAMPLE_REGS_INTR			= 1U << 18,
	PERF_SAMPLE_PHYS_ADDR			= 1U << 19,
	PERF_SAMPLE_AUX				= 1U << 20,
	PERF_SAMPLE_CGROUP			= 1U << 21,
	PERF_SAMPLE_DATA_PAGE_SIZE		= 1U << 22,
	PERF_SAMPLE_CODE_PAGE_SIZE		= 1U << 23,
	PERF_SAMPLE_WEIGHT_STRUCT		= 1U << 24,

	PERF_SAMPLE_MAX = 1U << 25,		/* non-ABI */
};

#define PERF_SAMPLE_WEIGHT_TYPE	(PERF_SAMPLE_WEIGHT | PERF_SAMPLE_WEIGHT_STRUCT)

/*
 * Values to program into branch_sample_type when PERF_SAMPLE_BRANCH is set.
 *
 * If the user does not pass priv level information via branch_sample_type,
 * the kernel uses the event's priv level. Branch and event priv levels do
 * not have to match. Branch priv level is checked for permissions.
 *
 * The branch types can be combined, however BRANCH_ANY covers all types
 * of branches and therefore it supersedes all the other types.
 */
enum perf_branch_sample_type_shift {
	PERF_SAMPLE_BRANCH_USER_SHIFT		=  0, /* user branches */
	PERF_SAMPLE_BRANCH_KERNEL_SHIFT		=  1, /* kernel branches */
	PERF_SAMPLE_BRANCH_HV_SHIFT		=  2, /* hypervisor branches */

	PERF_SAMPLE_BRANCH_ANY_SHIFT		=  3, /* any branch types */
	PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT	=  4, /* any call branch */
	PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT	=  5, /* any return branch */
	PERF_SAMPLE_BRANCH_IND_CALL_SHIFT	=  6, /* indirect calls */
	PERF_SAMPLE_BRANCH_ABORT_TX_SHIFT	=  7, /* transaction aborts */
	PERF_SAMPLE_BRANCH_IN_TX_SHIFT		=  8, /* in transaction */
	PERF_SAMPLE_BRANCH_NO_TX_SHIFT		=  9, /* not in transaction */
	PERF_SAMPLE_BRANCH_COND_SHIFT		= 10, /* conditional branches */

	PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT	= 11, /* CALL/RET stack */
	PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT	= 12, /* indirect jumps */
	PERF_SAMPLE_BRANCH_CALL_SHIFT		= 13, /* direct call */

	PERF_SAMPLE_BRANCH_NO_FLAGS_SHIFT	= 14, /* no flags */
	PERF_SAMPLE_BRANCH_NO_CYCLES_SHIFT	= 15, /* no cycles */

	PERF_SAMPLE_BRANCH_TYPE_SAVE_SHIFT	= 16, /* save branch type */

	PERF_SAMPLE_BRANCH_HW_INDEX_SHIFT	= 17, /* save low level index of raw branch records */

	PERF_SAMPLE_BRANCH_PRIV_SAVE_SHIFT	= 18, /* save privilege mode */

	PERF_SAMPLE_BRANCH_COUNTERS_SHIFT	= 19, /* save occurrences of events on a branch */

	PERF_SAMPLE_BRANCH_MAX_SHIFT		/* non-ABI */
};

enum perf_branch_sample_type {
	PERF_SAMPLE_BRANCH_USER			= 1U << PERF_SAMPLE_BRANCH_USER_SHIFT,
	PERF_SAMPLE_BRANCH_KERNEL		= 1U << PERF_SAMPLE_BRANCH_KERNEL_SHIFT,
	PERF_SAMPLE_BRANCH_HV			= 1U << PERF_SAMPLE_BRANCH_HV_SHIFT,

	PERF_SAMPLE_BRANCH_ANY			= 1U << PERF_SAMPLE_BRANCH_ANY_SHIFT,
	PERF_SAMPLE_BRANCH_ANY_CALL		= 1U << PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT,
	PERF_SAMPLE_BRANCH_ANY_RETURN		= 1U << PERF_SAMPLE_BRANCH_ANY_RETURN_SHIFT,
	PERF_SAMPLE_BRANCH_IND_CALL		= 1U << PERF_SAMPLE_BRANCH_IND_CALL_SHIFT,
	PERF_SAMPLE_BRANCH_ABORT_TX		= 1U << PERF_SAMPLE_BRANCH_ABORT_TX_SHIFT,
	PERF_SAMPLE_BRANCH_IN_TX		= 1U << PERF_SAMPLE_BRANCH_IN_TX_SHIFT,
	PERF_SAMPLE_BRANCH_NO_TX		= 1U << PERF_SAMPLE_BRANCH_NO_TX_SHIFT,
	PERF_SAMPLE_BRANCH_COND			= 1U << PERF_SAMPLE_BRANCH_COND_SHIFT,

	PERF_SAMPLE_BRANCH_CALL_STACK		= 1U << PERF_SAMPLE_BRANCH_CALL_STACK_SHIFT,
	PERF_SAMPLE_BRANCH_IND_JUMP		= 1U << PERF_SAMPLE_BRANCH_IND_JUMP_SHIFT,
	PERF_SAMPLE_BRANCH_CALL			= 1U << PERF_SAMPLE_BRANCH_CALL_SHIFT,

	PERF_SAMPLE_BRANCH_NO_FLAGS		= 1U << PERF_SAMPLE_BRANCH_NO_FLAGS_SHIFT,
	PERF_SAMPLE_BRANCH_NO_CYCLES		= 1U << PERF_SAMPLE_BRANCH_NO_CYCLES_SHIFT,

	PERF_SAMPLE_BRANCH_TYPE_SAVE		= 1U << PERF_SAMPLE_BRANCH_TYPE_SAVE_SHIFT,

	PERF_SAMPLE_BRANCH_HW_INDEX		= 1U << PERF_SAMPLE_BRANCH_HW_INDEX_SHIFT,

	PERF_SAMPLE_BRANCH_PRIV_SAVE		= 1U << PERF_SAMPLE_BRANCH_PRIV_SAVE_SHIFT,

	PERF_SAMPLE_BRANCH_COUNTERS		= 1U << PERF_SAMPLE_BRANCH_COUNTERS_SHIFT,

	PERF_SAMPLE_BRANCH_MAX			= 1U << PERF_SAMPLE_BRANCH_MAX_SHIFT,
};

/*
 * Common control flow change classifications:
 */
enum {
	PERF_BR_UNKNOWN				=  0,	/* Unknown */
	PERF_BR_COND				=  1,	/* Conditional */
	PERF_BR_UNCOND				=  2,	/* Unconditional  */
	PERF_BR_IND				=  3,	/* Indirect */
	PERF_BR_CALL				=  4,	/* Function call */
	PERF_BR_IND_CALL			=  5,	/* Indirect function call */
	PERF_BR_RET				=  6,	/* Function return */
	PERF_BR_SYSCALL				=  7,	/* Syscall */
	PERF_BR_SYSRET				=  8,	/* Syscall return */
	PERF_BR_COND_CALL			=  9,	/* Conditional function call */
	PERF_BR_COND_RET			= 10,	/* Conditional function return */
	PERF_BR_ERET				= 11,	/* Exception return */
	PERF_BR_IRQ				= 12,	/* IRQ */
	PERF_BR_SERROR				= 13,	/* System error */
	PERF_BR_NO_TX				= 14,	/* Not in transaction */
	PERF_BR_EXTEND_ABI			= 15,	/* Extend ABI */
	PERF_BR_MAX,
};

/*
 * Common branch speculation outcome classifications:
 */
enum {
	PERF_BR_SPEC_NA				= 0,	/* Not available */
	PERF_BR_SPEC_WRONG_PATH			= 1,	/* Speculative but on wrong path */
	PERF_BR_NON_SPEC_CORRECT_PATH		= 2,	/* Non-speculative but on correct path */
	PERF_BR_SPEC_CORRECT_PATH		= 3,	/* Speculative and on correct path */
	PERF_BR_SPEC_MAX,
};

enum {
	PERF_BR_NEW_FAULT_ALGN			= 0,    /* Alignment fault */
	PERF_BR_NEW_FAULT_DATA			= 1,    /* Data fault */
	PERF_BR_NEW_FAULT_INST			= 2,    /* Inst fault */
	PERF_BR_NEW_ARCH_1			= 3,    /* Architecture specific */
	PERF_BR_NEW_ARCH_2			= 4,    /* Architecture specific */
	PERF_BR_NEW_ARCH_3			= 5,    /* Architecture specific */
	PERF_BR_NEW_ARCH_4			= 6,    /* Architecture specific */
	PERF_BR_NEW_ARCH_5			= 7,    /* Architecture specific */
	PERF_BR_NEW_MAX,
};

enum {
	PERF_BR_PRIV_UNKNOWN			= 0,
	PERF_BR_PRIV_USER			= 1,
	PERF_BR_PRIV_KERNEL			= 2,
	PERF_BR_PRIV_HV				= 3,
};

#define PERF_BR_ARM64_FIQ			PERF_BR_NEW_ARCH_1
#define PERF_BR_ARM64_DEBUG_HALT		PERF_BR_NEW_ARCH_2
#define PERF_BR_ARM64_DEBUG_EXIT		PERF_BR_NEW_ARCH_3
#define PERF_BR_ARM64_DEBUG_INST		PERF_BR_NEW_ARCH_4
#define PERF_BR_ARM64_DEBUG_DATA		PERF_BR_NEW_ARCH_5

#define PERF_SAMPLE_BRANCH_PLM_ALL \
	(PERF_SAMPLE_BRANCH_USER|\
	 PERF_SAMPLE_BRANCH_KERNEL|\
	 PERF_SAMPLE_BRANCH_HV)

/*
 * Values to determine ABI of the registers dump.
 */
enum perf_sample_regs_abi {
	PERF_SAMPLE_REGS_ABI_NONE		= 0,
	PERF_SAMPLE_REGS_ABI_32			= 1,
	PERF_SAMPLE_REGS_ABI_64			= 2,
};

/*
 * Values for the memory transaction event qualifier, mostly for
 * abort events. Multiple bits can be set.
 */
enum {
	PERF_TXN_ELISION			= (1 << 0), /* From elision */
	PERF_TXN_TRANSACTION			= (1 << 1), /* From transaction */
	PERF_TXN_SYNC				= (1 << 2), /* Instruction is related */
	PERF_TXN_ASYNC				= (1 << 3), /* Instruction is not related */
	PERF_TXN_RETRY				= (1 << 4), /* Retry possible */
	PERF_TXN_CONFLICT			= (1 << 5), /* Conflict abort */
	PERF_TXN_CAPACITY_WRITE			= (1 << 6), /* Capacity write abort */
	PERF_TXN_CAPACITY_READ			= (1 << 7), /* Capacity read abort */

	PERF_TXN_MAX				= (1 << 8), /* non-ABI */

	/* Bits 32..63 are reserved for the abort code */

	PERF_TXN_ABORT_MASK			= (0xffffffffULL << 32),
	PERF_TXN_ABORT_SHIFT			= 32,
};

/*
 * The format of the data returned by read() on a perf event fd,
 * as specified by attr.read_format:
 *
 * struct read_format {
 *	{ u64		value;
 *	  { u64		time_enabled; } && PERF_FORMAT_TOTAL_TIME_ENABLED
 *	  { u64		time_running; } && PERF_FORMAT_TOTAL_TIME_RUNNING
 *	  { u64		id;           } && PERF_FORMAT_ID
 *	  { u64		lost;         } && PERF_FORMAT_LOST
 *	} && !PERF_FORMAT_GROUP
 *
 *	{ u64		nr;
 *	  { u64		time_enabled; } && PERF_FORMAT_TOTAL_TIME_ENABLED
 *	  { u64		time_running; } && PERF_FORMAT_TOTAL_TIME_RUNNING
 *	  { u64		value;
 *	    { u64	id;           } && PERF_FORMAT_ID
 *	    { u64	lost;         } && PERF_FORMAT_LOST
 *	  }		cntr[nr];
 *	} && PERF_FORMAT_GROUP
 * };
 */
enum perf_event_read_format {
	PERF_FORMAT_TOTAL_TIME_ENABLED		= 1U << 0,
	PERF_FORMAT_TOTAL_TIME_RUNNING		= 1U << 1,
	PERF_FORMAT_ID				= 1U << 2,
	PERF_FORMAT_GROUP			= 1U << 3,
	PERF_FORMAT_LOST			= 1U << 4,

	PERF_FORMAT_MAX = 1U << 5,		/* non-ABI */
};

#define PERF_ATTR_SIZE_VER0			 64	/* Size of first published 'struct perf_event_attr' */
#define PERF_ATTR_SIZE_VER1			 72	/* Add: config2 */
#define PERF_ATTR_SIZE_VER2			 80	/* Add: branch_sample_type */
#define PERF_ATTR_SIZE_VER3			 96	/* Add: sample_regs_user */
							/* Add: sample_stack_user */
#define PERF_ATTR_SIZE_VER4			104	/* Add: sample_regs_intr */
#define PERF_ATTR_SIZE_VER5			112	/* Add: aux_watermark */
#define PERF_ATTR_SIZE_VER6			120	/* Add: aux_sample_size */
#define PERF_ATTR_SIZE_VER7			128	/* Add: sig_data */
#define PERF_ATTR_SIZE_VER8			136	/* Add: config3 */

/*
 * 'struct perf_event_attr' contains various attributes that define
 * a performance event - most of them hardware related configuration
 * details, but also a lot of behavioral switches and values implemented
 * by the kernel.
 */
struct perf_event_attr {

	/*
	 * Major type: hardware/software/tracepoint/etc.
	 */
	__u32			type;

	/*
	 * Size of the attr structure, for forward/backwards compatibility.
	 */
	__u32			size;

	/*
	 * Type specific configuration information.
	 */
	__u64			config;

	union {
		__u64		sample_period;
		__u64		sample_freq;
	};

	__u64			sample_type;
	__u64			read_format;

	__u64			disabled       :  1, /* off by default        */
				inherit	       :  1, /* children inherit it   */
				pinned	       :  1, /* must always be on PMU */
				exclusive      :  1, /* only group on PMU     */
				exclude_user   :  1, /* don't count user      */
				exclude_kernel :  1, /* ditto kernel          */
				exclude_hv     :  1, /* ditto hypervisor      */
				exclude_idle   :  1, /* don't count when idle */
				mmap           :  1, /* include mmap data     */
				comm	       :  1, /* include comm data     */
				freq           :  1, /* use freq, not period  */
				inherit_stat   :  1, /* per task counts       */
				enable_on_exec :  1, /* next exec enables     */
				task           :  1, /* trace fork/exit       */
				watermark      :  1, /* wakeup_watermark      */
				/*
				 * precise_ip:
				 *
				 *  0 - SAMPLE_IP can have arbitrary skid
				 *  1 - SAMPLE_IP must have constant skid
				 *  2 - SAMPLE_IP requested to have 0 skid
				 *  3 - SAMPLE_IP must have 0 skid
				 *
				 *  See also PERF_RECORD_MISC_EXACT_IP
				 */
				precise_ip     :  2, /* skid constraint       */
				mmap_data      :  1, /* non-exec mmap data    */
				sample_id_all  :  1, /* sample_type all events */

				exclude_host   :  1, /* don't count in host   */
				exclude_guest  :  1, /* don't count in guest  */

				exclude_callchain_kernel : 1, /* exclude kernel callchains */
				exclude_callchain_user   : 1, /* exclude user callchains */
				mmap2          :  1, /* include mmap with inode data     */
				comm_exec      :  1, /* flag comm events that are due to an exec */
				use_clockid    :  1, /* use @clockid for time fields */
				context_switch :  1, /* context switch data */
				write_backward :  1, /* write ring buffer from end to beginning */
				namespaces     :  1, /* include namespaces data */
				ksymbol        :  1, /* include ksymbol events */
				bpf_event      :  1, /* include BPF events */
				aux_output     :  1, /* generate AUX records instead of events */
				cgroup         :  1, /* include cgroup events */
				text_poke      :  1, /* include text poke events */
				build_id       :  1, /* use build ID in mmap2 events */
				inherit_thread :  1, /* children only inherit if cloned with CLONE_THREAD */
				remove_on_exec :  1, /* event is removed from task on exec */
				sigtrap        :  1, /* send synchronous SIGTRAP on event */
				__reserved_1   : 26;

	union {
		__u32		wakeup_events;	  /* wake up every n events */
		__u32		wakeup_watermark; /* bytes before wakeup   */
	};

	__u32			bp_type;
	union {
		__u64		bp_addr;
		__u64		kprobe_func; /* for perf_kprobe */
		__u64		uprobe_path; /* for perf_uprobe */
		__u64		config1;     /* extension of config */
	};
	union {
		__u64		bp_len;
		__u64		kprobe_addr;  /* when kprobe_func == NULL */
		__u64		probe_offset; /* for perf_[k,u]probe */
		__u64		config2;      /* extension of config1 */
	};
	__u64	branch_sample_type; /* enum perf_branch_sample_type */

	/*
	 * Defines set of user regs to dump on samples.
	 * See asm/perf_regs.h for details.
	 */
	__u64	sample_regs_user;

	/*
	 * Defines size of the user stack to dump on samples.
	 */
	__u32	sample_stack_user;

	__s32	clockid;
	/*
	 * Defines set of regs to dump for each sample
	 * state captured on:
	 *  - precise = 0: PMU interrupt
	 *  - precise > 0: sampled instruction
	 *
	 * See asm/perf_regs.h for details.
	 */
	__u64	sample_regs_intr;

	/*
	 * Wakeup watermark for AUX area
	 */
	__u32	aux_watermark;

	/*
	 * Max number of frame pointers in a callchain, should be
	 * lower than /proc/sys/kernel/perf_event_max_stack.
	 *
	 * Max number of entries of branch stack should be lower
	 * than the hardware limit.
	 */
	__u16	sample_max_stack;

	__u16	__reserved_2;
	__u32	aux_sample_size;

	union {
		__u32	aux_action;
		struct {
			__u32	aux_start_paused :  1, /* start AUX area tracing paused */
				aux_pause        :  1, /* on overflow, pause AUX area tracing */
				aux_resume       :  1, /* on overflow, resume AUX area tracing */
				__reserved_3     : 29;
		};
	};

	/*
	 * User provided data if sigtrap=1, passed back to user via
	 * siginfo_t::si_perf_data, e.g. to permit user to identify the event.
	 * Note, siginfo_t::si_perf_data is long-sized, and sig_data will be
	 * truncated accordingly on 32 bit architectures.
	 */
	__u64	sig_data;

	__u64	config3; /* extension of config2 */
};

/*
 * Structure used by below PERF_EVENT_IOC_QUERY_BPF command
 * to query BPF programs attached to the same perf tracepoint
 * as the given perf event.
 */
struct perf_event_query_bpf {
	/*
	 * The below ids array length
	 */
	__u32	ids_len;
	/*
	 * Set by the kernel to indicate the number of
	 * available programs
	 */
	__u32	prog_cnt;
	/*
	 * User provided buffer to store program ids
	 */
	__u32	ids[];
};

/*
 * Ioctls that can be done on a perf event fd:
 */
#define PERF_EVENT_IOC_ENABLE			_IO  ('$', 0)
#define PERF_EVENT_IOC_DISABLE			_IO  ('$', 1)
#define PERF_EVENT_IOC_REFRESH			_IO  ('$', 2)
#define PERF_EVENT_IOC_RESET			_IO  ('$', 3)
#define PERF_EVENT_IOC_PERIOD			_IOW ('$', 4, __u64)
#define PERF_EVENT_IOC_SET_OUTPUT		_IO  ('$', 5)
#define PERF_EVENT_IOC_SET_FILTER		_IOW ('$', 6, char *)
#define PERF_EVENT_IOC_ID			_IOR ('$', 7, __u64 *)
#define PERF_EVENT_IOC_SET_BPF			_IOW ('$', 8, __u32)
#define PERF_EVENT_IOC_PAUSE_OUTPUT		_IOW ('$', 9, __u32)
#define PERF_EVENT_IOC_QUERY_BPF		_IOWR('$', 10, struct perf_event_query_bpf *)
#define PERF_EVENT_IOC_MODIFY_ATTRIBUTES	_IOW ('$', 11, struct perf_event_attr *)

enum perf_event_ioc_flags {
	PERF_IOC_FLAG_GROUP			= 1U << 0,
};

/*
 * Structure of the page that can be mapped via mmap
 */
struct perf_event_mmap_page {
	__u32	version;		/* version number of this structure */
	__u32	compat_version;		/* lowest version this is compat with */

	/*
	 * Bits needed to read the HW events in user-space.
	 *
	 *   u32 seq, time_mult, time_shift, index, width;
	 *   u64 count, enabled, running;
	 *   u64 cyc, time_offset;
	 *   s64 pmc = 0;
	 *
	 *   do {
	 *     seq = pc->lock;
	 *     barrier()
	 *
	 *     enabled = pc->time_enabled;
	 *     running = pc->time_running;
	 *
	 *     if (pc->cap_usr_time && enabled != running) {
	 *       cyc = rdtsc();
	 *       time_offset = pc->time_offset;
	 *       time_mult   = pc->time_mult;
	 *       time_shift  = pc->time_shift;
	 *     }
	 *
	 *     index = pc->index;
	 *     count = pc->offset;
	 *     if (pc->cap_user_rdpmc && index) {
	 *       width = pc->pmc_width;
	 *       pmc = rdpmc(index - 1);
	 *     }
	 *
	 *     barrier();
	 *   } while (pc->lock != seq);
	 *
	 * NOTE: for obvious reason this only works on self-monitoring
	 *       processes.
	 */
	__u32	lock;			/* seqlock for synchronization */
	__u32	index;			/* hardware event identifier */
	__s64	offset;			/* add to hardware event value */
	__u64	time_enabled;		/* time event active */
	__u64	time_running;		/* time event on CPU */
	union {
		__u64	capabilities;
		struct {
			__u64	cap_bit0		: 1, /* Always 0, deprecated, see commit 860f085b74e9 */
				cap_bit0_is_deprecated	: 1, /* Always 1, signals that bit 0 is zero */

				cap_user_rdpmc		: 1, /* The RDPMC instruction can be used to read counts */
				cap_user_time		: 1, /* The time_{shift,mult,offset} fields are used */
				cap_user_time_zero	: 1, /* The time_zero field is used */
				cap_user_time_short	: 1, /* the time_{cycle,mask} fields are used */
				cap_____res		: 58;
		};
	};

	/*
	 * If cap_user_rdpmc this field provides the bit-width of the value
	 * read using the rdpmc() or equivalent instruction. This can be used
	 * to sign extend the result like:
	 *
	 *   pmc <<= 64 - width;
	 *   pmc >>= 64 - width; // signed shift right
	 *   count += pmc;
	 */
	__u16	pmc_width;

	/*
	 * If cap_usr_time the below fields can be used to compute the time
	 * delta since time_enabled (in ns) using RDTSC or similar.
	 *
	 *   u64 quot, rem;
	 *   u64 delta;
	 *
	 *   quot = (cyc >> time_shift);
	 *   rem = cyc & (((u64)1 << time_shift) - 1);
	 *   delta = time_offset + quot * time_mult +
	 *              ((rem * time_mult) >> time_shift);
	 *
	 * Where time_offset,time_mult,time_shift and cyc are read in the
	 * seqcount loop described above. This delta can then be added to
	 * enabled and possible running (if index), improving the scaling:
	 *
	 *   enabled += delta;
	 *   if (index)
	 *     running += delta;
	 *
	 *   quot = count / running;
	 *   rem  = count % running;
	 *   count = quot * enabled + (rem * enabled) / running;
	 */
	__u16	time_shift;
	__u32	time_mult;
	__u64	time_offset;
	/*
	 * If cap_usr_time_zero, the hardware clock (e.g. TSC) can be calculated
	 * from sample timestamps.
	 *
	 *   time = timestamp - time_zero;
	 *   quot = time / time_mult;
	 *   rem  = time % time_mult;
	 *   cyc = (quot << time_shift) + (rem << time_shift) / time_mult;
	 *
	 * And vice versa:
	 *
	 *   quot = cyc >> time_shift;
	 *   rem  = cyc & (((u64)1 << time_shift) - 1);
	 *   timestamp = time_zero + quot * time_mult +
	 *               ((rem * time_mult) >> time_shift);
	 */
	__u64	time_zero;

	__u32	size;			/* Header size up to __reserved[] fields. */
	__u32	__reserved_1;

	/*
	 * If cap_usr_time_short, the hardware clock is less than 64bit wide
	 * and we must compute the 'cyc' value, as used by cap_usr_time, as:
	 *
	 *   cyc = time_cycles + ((cyc - time_cycles) & time_mask)
	 *
	 * NOTE: this form is explicitly chosen such that cap_usr_time_short
	 *       is a correction on top of cap_usr_time, and code that doesn't
	 *       know about cap_usr_time_short still works under the assumption
	 *       the counter doesn't wrap.
	 */
	__u64	time_cycles;
	__u64	time_mask;

		/*
		 * Hole for extension of the self monitor capabilities
		 */

	__u8	__reserved[116*8];	/* align to 1k. */

	/*
	 * Control data for the mmap() data buffer.
	 *
	 * User-space reading the @data_head value should issue an smp_rmb(),
	 * after reading this value.
	 *
	 * When the mapping is PROT_WRITE the @data_tail value should be
	 * written by user-space to reflect the last read data, after issuing
	 * an smp_mb() to separate the data read from the ->data_tail store.
	 * In this case the kernel will not over-write unread data.
	 *
	 * See perf_output_put_handle() for the data ordering.
	 *
	 * data_{offset,size} indicate the location and size of the perf record
	 * buffer within the mmapped area.
	 */
	__u64   data_head;		/* head in the data section */
	__u64	data_tail;		/* user-space written tail */
	__u64	data_offset;		/* where the buffer starts */
	__u64	data_size;		/* data buffer size */

	/*
	 * AUX area is defined by aux_{offset,size} fields that should be set
	 * by the user-space, so that
	 *
	 *   aux_offset >= data_offset + data_size
	 *
	 * prior to mmap()ing it. Size of the mmap()ed area should be aux_size.
	 *
	 * Ring buffer pointers aux_{head,tail} have the same semantics as
	 * data_{head,tail} and same ordering rules apply.
	 */
	__u64	aux_head;
	__u64	aux_tail;
	__u64	aux_offset;
	__u64	aux_size;
};

/*
 * The current state of perf_event_header::misc bits usage:
 * ('|' used bit, '-' unused bit)
 *
 *  012         CDEF
 *  |||---------||||
 *
 *  Where:
 *    0-2     CPUMODE_MASK
 *
 *    C       PROC_MAP_PARSE_TIMEOUT
 *    D       MMAP_DATA / COMM_EXEC / FORK_EXEC / SWITCH_OUT
 *    E       MMAP_BUILD_ID / EXACT_IP / SCHED_OUT_PREEMPT
 *    F       (reserved)
 */

#define PERF_RECORD_MISC_CPUMODE_MASK		(7 << 0)
#define PERF_RECORD_MISC_CPUMODE_UNKNOWN	(0 << 0)
#define PERF_RECORD_MISC_KERNEL			(1 << 0)
#define PERF_RECORD_MISC_USER			(2 << 0)
#define PERF_RECORD_MISC_HYPERVISOR		(3 << 0)
#define PERF_RECORD_MISC_GUEST_KERNEL		(4 << 0)
#define PERF_RECORD_MISC_GUEST_USER		(5 << 0)

/*
 * Indicates that /proc/PID/maps parsing are truncated by time out.
 */
#define PERF_RECORD_MISC_PROC_MAP_PARSE_TIMEOUT	(1 << 12)
/*
 * Following PERF_RECORD_MISC_* are used on different
 * events, so can reuse the same bit position:
 *
 *   PERF_RECORD_MISC_MMAP_DATA  - PERF_RECORD_MMAP* events
 *   PERF_RECORD_MISC_COMM_EXEC  - PERF_RECORD_COMM event
 *   PERF_RECORD_MISC_FORK_EXEC  - PERF_RECORD_FORK event (perf internal)
 *   PERF_RECORD_MISC_SWITCH_OUT - PERF_RECORD_SWITCH* events
 */
#define PERF_RECORD_MISC_MMAP_DATA		(1 << 13)
#define PERF_RECORD_MISC_COMM_EXEC		(1 << 13)
#define PERF_RECORD_MISC_FORK_EXEC		(1 << 13)
#define PERF_RECORD_MISC_SWITCH_OUT		(1 << 13)
/*
 * These PERF_RECORD_MISC_* flags below are safely reused
 * for the following events:
 *
 *   PERF_RECORD_MISC_EXACT_IP           - PERF_RECORD_SAMPLE of precise events
 *   PERF_RECORD_MISC_SWITCH_OUT_PREEMPT - PERF_RECORD_SWITCH* events
 *   PERF_RECORD_MISC_MMAP_BUILD_ID      - PERF_RECORD_MMAP2 event
 *
 *
 * PERF_RECORD_MISC_EXACT_IP:
 *   Indicates that the content of PERF_SAMPLE_IP points to
 *   the actual instruction that triggered the event. See also
 *   perf_event_attr::precise_ip.
 *
 * PERF_RECORD_MISC_SWITCH_OUT_PREEMPT:
 *   Indicates that thread was preempted in TASK_RUNNING state.
 *
 * PERF_RECORD_MISC_MMAP_BUILD_ID:
 *   Indicates that mmap2 event carries build ID data.
 */
#define PERF_RECORD_MISC_EXACT_IP		(1 << 14)
#define PERF_RECORD_MISC_SWITCH_OUT_PREEMPT	(1 << 14)
#define PERF_RECORD_MISC_MMAP_BUILD_ID		(1 << 14)
/*
 * Reserve the last bit to indicate some extended misc field
 */
#define PERF_RECORD_MISC_EXT_RESERVED		(1 << 15)

struct perf_event_header {
	__u32 type;
	__u16 misc;
	__u16 size;
};

struct perf_ns_link_info {
	__u64 dev;
	__u64 ino;
};

enum {
	NET_NS_INDEX				= 0,
	UTS_NS_INDEX				= 1,
	IPC_NS_INDEX				= 2,
	PID_NS_INDEX				= 3,
	USER_NS_INDEX				= 4,
	MNT_NS_INDEX				= 5,
	CGROUP_NS_INDEX				= 6,

	NR_NAMESPACES, /* number of available namespaces */
};

enum perf_event_type {

	/*
	 * If perf_event_attr.sample_id_all is set then all event types will
	 * have the sample_type selected fields related to where/when
	 * (identity) an event took place (TID, TIME, ID, STREAM_ID, CPU,
	 * IDENTIFIER) described in PERF_RECORD_SAMPLE below, it will be stashed
	 * just after the perf_event_header and the fields already present for
	 * the existing fields, i.e. at the end of the payload. That way a newer
	 * perf.data file will be supported by older perf tools, with these new
	 * optional fields being ignored.
	 *
	 * struct sample_id {
	 *	{ u32			pid, tid; } && PERF_SAMPLE_TID
	 *	{ u64			time;     } && PERF_SAMPLE_TIME
	 *	{ u64			id;       } && PERF_SAMPLE_ID
	 *	{ u64			stream_id;} && PERF_SAMPLE_STREAM_ID
	 *	{ u32			cpu, res; } && PERF_SAMPLE_CPU
	 *	{ u64			id;	  } && PERF_SAMPLE_IDENTIFIER
	 * } && perf_event_attr::sample_id_all
	 *
	 * Note that PERF_SAMPLE_IDENTIFIER duplicates PERF_SAMPLE_ID.  The
	 * advantage of PERF_SAMPLE_IDENTIFIER is that its position is fixed
	 * relative to header.size.
	 */

	/*
	 * The MMAP events record the PROT_EXEC mappings so that we can
	 * correlate user-space IPs to code. They have the following structure:
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	u64				addr;
	 *	u64				len;
	 *	u64				pgoff;
	 *	char				filename[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_MMAP			= 1,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				id;
	 *	u64				lost;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_LOST			= 2,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	char				comm[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_COMM			= 3,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, ppid;
	 *	u32				tid, ptid;
	 *	u64				time;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_EXIT			= 4,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				time;
	 *	u64				id;
	 *	u64				stream_id;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_THROTTLE			= 5,
	PERF_RECORD_UNTHROTTLE			= 6,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, ppid;
	 *	u32				tid, ptid;
	 *	u64				time;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_FORK			= 7,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, tid;
	 *
	 *	struct read_format		values;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_READ			= 8,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	#
	 *	# Note that PERF_SAMPLE_IDENTIFIER duplicates PERF_SAMPLE_ID.
	 *	# The advantage of PERF_SAMPLE_IDENTIFIER is that its position
	 *	# is fixed relative to header.
	 *	#
	 *
	 *	{ u64			id;	  } && PERF_SAMPLE_IDENTIFIER
	 *	{ u64			ip;	  } && PERF_SAMPLE_IP
	 *	{ u32			pid, tid; } && PERF_SAMPLE_TID
	 *	{ u64			time;     } && PERF_SAMPLE_TIME
	 *	{ u64			addr;     } && PERF_SAMPLE_ADDR
	 *	{ u64			id;	  } && PERF_SAMPLE_ID
	 *	{ u64			stream_id;} && PERF_SAMPLE_STREAM_ID
	 *	{ u32			cpu, res; } && PERF_SAMPLE_CPU
	 *	{ u64			period;   } && PERF_SAMPLE_PERIOD
	 *
	 *	{ struct read_format	values;	  } && PERF_SAMPLE_READ
	 *
	 *	{ u64			nr,
	 *	  u64			ips[nr];  } && PERF_SAMPLE_CALLCHAIN
	 *
	 *	#
	 *	# The RAW record below is opaque data wrt the ABI
	 *	#
	 *	# That is, the ABI doesn't make any promises wrt to
	 *	# the stability of its content, it may vary depending
	 *	# on event, hardware, kernel version and phase of
	 *	# the moon.
	 *	#
	 *	# In other words, PERF_SAMPLE_RAW contents are not an ABI.
	 *	#
	 *
	 *	{ u32			size;
	 *	  char                  data[size];}&& PERF_SAMPLE_RAW
	 *
	 *	{ u64                   nr;
	 *	  { u64	hw_idx; } && PERF_SAMPLE_BRANCH_HW_INDEX
	 *        { u64 from, to, flags } lbr[nr];
	 *        #
	 *        # The format of the counters is decided by the
	 *        # "branch_counter_nr" and "branch_counter_width",
	 *        # which are defined in the ABI.
	 *        #
	 *        { u64 counters; } cntr[nr] && PERF_SAMPLE_BRANCH_COUNTERS
	 *      } && PERF_SAMPLE_BRANCH_STACK
	 *
	 *	{ u64			abi; # enum perf_sample_regs_abi
	 *	  u64			regs[weight(mask)]; } && PERF_SAMPLE_REGS_USER
	 *
	 *	{ u64			size;
	 *	  char			data[size];
	 *	  u64			dyn_size; } && PERF_SAMPLE_STACK_USER
	 *
	 *	{ union perf_sample_weight
	 *	 {
	 *		u64		full; && PERF_SAMPLE_WEIGHT
	 *	#if defined(__LITTLE_ENDIAN_BITFIELD)
	 *		struct {
	 *			u32	var1_dw;
	 *			u16	var2_w;
	 *			u16	var3_w;
	 *		} && PERF_SAMPLE_WEIGHT_STRUCT
	 *	#elif defined(__BIG_ENDIAN_BITFIELD)
	 *		struct {
	 *			u16	var3_w;
	 *			u16	var2_w;
	 *			u32	var1_dw;
	 *		} && PERF_SAMPLE_WEIGHT_STRUCT
	 *	#endif
	 *	 }
	 *	}
	 *	{ u64			data_src; } && PERF_SAMPLE_DATA_SRC
	 *	{ u64			transaction; } && PERF_SAMPLE_TRANSACTION
	 *	{ u64			abi; # enum perf_sample_regs_abi
	 *	  u64			regs[weight(mask)]; } && PERF_SAMPLE_REGS_INTR
	 *	{ u64			phys_addr;} && PERF_SAMPLE_PHYS_ADDR
	 *	{ u64			cgroup;} && PERF_SAMPLE_CGROUP
	 *	{ u64			data_page_size;} && PERF_SAMPLE_DATA_PAGE_SIZE
	 *	{ u64			code_page_size;} && PERF_SAMPLE_CODE_PAGE_SIZE
	 *	{ u64			size;
	 *	  char			data[size]; } && PERF_SAMPLE_AUX
	 * };
	 */
	PERF_RECORD_SAMPLE			= 9,

	/*
	 * The MMAP2 records are an augmented version of MMAP, they add
	 * maj, min, ino numbers to be used to uniquely identify each mapping
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	u64				addr;
	 *	u64				len;
	 *	u64				pgoff;
	 *	union {
	 *		struct {
	 *			u32		maj;
	 *			u32		min;
	 *			u64		ino;
	 *			u64		ino_generation;
	 *		};
	 *		struct {
	 *			u8		build_id_size;
	 *			u8		__reserved_1;
	 *			u16		__reserved_2;
	 *			u8		build_id[20];
	 *		};
	 *	};
	 *	u32				prot, flags;
	 *	char				filename[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_MMAP2			= 10,

	/*
	 * Records that new data landed in the AUX buffer part.
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u64				aux_offset;
	 *	u64				aux_size;
	 *	u64				flags;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_AUX				= 11,

	/*
	 * Indicates that instruction trace has started
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid;
	 *	u32				tid;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_ITRACE_START		= 12,

	/*
	 * Records the dropped/lost sample number.
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u64				lost;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_LOST_SAMPLES		= 13,

	/*
	 * Records a context switch in or out (flagged by
	 * PERF_RECORD_MISC_SWITCH_OUT). See also
	 * PERF_RECORD_SWITCH_CPU_WIDE.
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_SWITCH			= 14,

	/*
	 * CPU-wide version of PERF_RECORD_SWITCH with next_prev_pid and
	 * next_prev_tid that are the next (switching out) or previous
	 * (switching in) pid/tid.
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				next_prev_pid;
	 *	u32				next_prev_tid;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_SWITCH_CPU_WIDE		= 15,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid;
	 *	u32				tid;
	 *	u64				nr_namespaces;
	 *	{ u64				dev, inode; } [nr_namespaces];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_NAMESPACES			= 16,

	/*
	 * Record ksymbol register/unregister events:
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				addr;
	 *	u32				len;
	 *	u16				ksym_type;
	 *	u16				flags;
	 *	char				name[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_KSYMBOL			= 17,

	/*
	 * Record BPF events:
	 *  enum perf_bpf_event_type {
	 *	PERF_BPF_EVENT_UNKNOWN		= 0,
	 *	PERF_BPF_EVENT_PROG_LOAD	= 1,
	 *	PERF_BPF_EVENT_PROG_UNLOAD	= 2,
	 *  };
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *	u16				type;
	 *	u16				flags;
	 *	u32				id;
	 *	u8				tag[BPF_TAG_SIZE];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_BPF_EVENT			= 18,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				id;
	 *	char				path[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_CGROUP			= 19,

	/*
	 * Records changes to kernel text i.e. self-modified code. 'old_len' is
	 * the number of old bytes, 'new_len' is the number of new bytes. Either
	 * 'old_len' or 'new_len' may be zero to indicate, for example, the
	 * addition or removal of a trampoline. 'bytes' contains the old bytes
	 * followed immediately by the new bytes.
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				addr;
	 *	u16				old_len;
	 *	u16				new_len;
	 *	u8				bytes[];
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_TEXT_POKE			= 20,

	/*
	 * Data written to the AUX area by hardware due to aux_output, may need
	 * to be matched to the event by an architecture-specific hardware ID.
	 * This records the hardware ID, but requires sample_id to provide the
	 * event ID. e.g. Intel PT uses this record to disambiguate PEBS-via-PT
	 * records from multiple events.
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				hw_id;
	 *	struct sample_id		sample_id;
	 * };
	 */
	PERF_RECORD_AUX_OUTPUT_HW_ID		= 21,

	PERF_RECORD_MAX,			/* non-ABI */
};

enum perf_record_ksymbol_type {
	PERF_RECORD_KSYMBOL_TYPE_UNKNOWN	= 0,
	PERF_RECORD_KSYMBOL_TYPE_BPF		= 1,
	/*
	 * Out of line code such as kprobe-replaced instructions or optimized
	 * kprobes or ftrace trampolines.
	 */
	PERF_RECORD_KSYMBOL_TYPE_OOL		= 2,
	PERF_RECORD_KSYMBOL_TYPE_MAX		/* non-ABI */
};

#define PERF_RECORD_KSYMBOL_FLAGS_UNREGISTER	(1 << 0)

enum perf_bpf_event_type {
	PERF_BPF_EVENT_UNKNOWN			= 0,
	PERF_BPF_EVENT_PROG_LOAD		= 1,
	PERF_BPF_EVENT_PROG_UNLOAD		= 2,
	PERF_BPF_EVENT_MAX,			/* non-ABI */
};

#define PERF_MAX_STACK_DEPTH			127
#define PERF_MAX_CONTEXTS_PER_STACK		  8

enum perf_callchain_context {
	PERF_CONTEXT_HV				= (__u64)-32,
	PERF_CONTEXT_KERNEL			= (__u64)-128,
	PERF_CONTEXT_USER			= (__u64)-512,

	PERF_CONTEXT_GUEST			= (__u64)-2048,
	PERF_CONTEXT_GUEST_KERNEL		= (__u64)-2176,
	PERF_CONTEXT_GUEST_USER			= (__u64)-2560,

	PERF_CONTEXT_MAX			= (__u64)-4095,
};

/**
 * PERF_RECORD_AUX::flags bits
 */
#define PERF_AUX_FLAG_TRUNCATED			0x0001	/* Record was truncated to fit */
#define PERF_AUX_FLAG_OVERWRITE			0x0002	/* Snapshot from overwrite mode */
#define PERF_AUX_FLAG_PARTIAL			0x0004	/* Record contains gaps */
#define PERF_AUX_FLAG_COLLISION			0x0008	/* Sample collided with another */
#define PERF_AUX_FLAG_PMU_FORMAT_TYPE_MASK	0xff00	/* PMU specific trace format type */

/* CoreSight PMU AUX buffer formats */
#define PERF_AUX_FLAG_CORESIGHT_FORMAT_CORESIGHT 0x0000 /* Default for backward compatibility */
#define PERF_AUX_FLAG_CORESIGHT_FORMAT_RAW	 0x0100 /* Raw format of the source */

#define PERF_FLAG_FD_NO_GROUP			(1UL << 0)
#define PERF_FLAG_FD_OUTPUT			(1UL << 1)
#define PERF_FLAG_PID_CGROUP			(1UL << 2) /* pid=cgroup ID, per-CPU mode only */
#define PERF_FLAG_FD_CLOEXEC			(1UL << 3) /* O_CLOEXEC */

#if defined(__LITTLE_ENDIAN_BITFIELD)
union perf_mem_data_src {
	__u64 val;
	struct {
		__u64   mem_op      :  5, /* Type of opcode */
			mem_lvl     : 14, /* Memory hierarchy level */
			mem_snoop   :  5, /* Snoop mode */
			mem_lock    :  2, /* Lock instr */
			mem_dtlb    :  7, /* TLB access */
			mem_lvl_num :  4, /* Memory hierarchy level number */
			mem_remote  :  1, /* Remote */
			mem_snoopx  :  2, /* Snoop mode, ext */
			mem_blk     :  3, /* Access blocked */
			mem_hops    :  3, /* Hop level */
			mem_rsvd    : 18;
	};
};
#elif defined(__BIG_ENDIAN_BITFIELD)
union perf_mem_data_src {
	__u64 val;
	struct {
		__u64	mem_rsvd    : 18,
			mem_hops    :  3, /* Hop level */
			mem_blk     :  3, /* Access blocked */
			mem_snoopx  :  2, /* Snoop mode, ext */
			mem_remote  :  1, /* Remote */
			mem_lvl_num :  4, /* Memory hierarchy level number */
			mem_dtlb    :  7, /* TLB access */
			mem_lock    :  2, /* Lock instr */
			mem_snoop   :  5, /* Snoop mode */
			mem_lvl     : 14, /* Memory hierarchy level */
			mem_op      :  5; /* Type of opcode */
	};
};
#else
# error "Unknown endianness"
#endif

/* Type of memory opcode: */
#define PERF_MEM_OP_NA				0x0001 /* Not available */
#define PERF_MEM_OP_LOAD			0x0002 /* Load instruction */
#define PERF_MEM_OP_STORE			0x0004 /* Store instruction */
#define PERF_MEM_OP_PFETCH			0x0008 /* Prefetch */
#define PERF_MEM_OP_EXEC			0x0010 /* Code (execution) */
#define PERF_MEM_OP_SHIFT			0

/*
 * The PERF_MEM_LVL_* namespace is being deprecated to some extent in
 * favour of newer composite PERF_MEM_{LVLNUM_,REMOTE_,SNOOPX_} fields.
 * We support this namespace in order to not break defined ABIs.
 *
 * Memory hierarchy (memory level, hit or miss)
 */
#define PERF_MEM_LVL_NA				0x0001 /* Not available */
#define PERF_MEM_LVL_HIT			0x0002 /* Hit level */
#define PERF_MEM_LVL_MISS			0x0004 /* Miss level  */
#define PERF_MEM_LVL_L1				0x0008 /* L1 */
#define PERF_MEM_LVL_LFB			0x0010 /* Line Fill Buffer */
#define PERF_MEM_LVL_L2				0x0020 /* L2 */
#define PERF_MEM_LVL_L3				0x0040 /* L3 */
#define PERF_MEM_LVL_LOC_RAM			0x0080 /* Local DRAM */
#define PERF_MEM_LVL_REM_RAM1			0x0100 /* Remote DRAM (1 hop) */
#define PERF_MEM_LVL_REM_RAM2			0x0200 /* Remote DRAM (2 hops) */
#define PERF_MEM_LVL_REM_CCE1			0x0400 /* Remote Cache (1 hop) */
#define PERF_MEM_LVL_REM_CCE2			0x0800 /* Remote Cache (2 hops) */
#define PERF_MEM_LVL_IO				0x1000 /* I/O memory */
#define PERF_MEM_LVL_UNC			0x2000 /* Uncached memory */
#define PERF_MEM_LVL_SHIFT			5

#define PERF_MEM_REMOTE_REMOTE			0x0001 /* Remote */
#define PERF_MEM_REMOTE_SHIFT			37

#define PERF_MEM_LVLNUM_L1			0x0001 /* L1 */
#define PERF_MEM_LVLNUM_L2			0x0002 /* L2 */
#define PERF_MEM_LVLNUM_L3			0x0003 /* L3 */
#define PERF_MEM_LVLNUM_L4			0x0004 /* L4 */
#define PERF_MEM_LVLNUM_L2_MHB			0x0005 /* L2 Miss Handling Buffer */
#define PERF_MEM_LVLNUM_MSC			0x0006 /* Memory-side Cache */
/* 0x007 available */
#define PERF_MEM_LVLNUM_UNC			0x0008 /* Uncached */
#define PERF_MEM_LVLNUM_CXL			0x0009 /* CXL */
#define PERF_MEM_LVLNUM_IO			0x000a /* I/O */
#define PERF_MEM_LVLNUM_ANY_CACHE		0x000b /* Any cache */
#define PERF_MEM_LVLNUM_LFB			0x000c /* LFB / L1 Miss Handling Buffer */
#define PERF_MEM_LVLNUM_RAM			0x000d /* RAM */
#define PERF_MEM_LVLNUM_PMEM			0x000e /* PMEM */
#define PERF_MEM_LVLNUM_NA			0x000f /* N/A */

#define PERF_MEM_LVLNUM_SHIFT			33

/* Snoop mode */
#define PERF_MEM_SNOOP_NA			0x0001 /* Not available */
#define PERF_MEM_SNOOP_NONE			0x0002 /* No snoop */
#define PERF_MEM_SNOOP_HIT			0x0004 /* Snoop hit */
#define PERF_MEM_SNOOP_MISS			0x0008 /* Snoop miss */
#define PERF_MEM_SNOOP_HITM			0x0010 /* Snoop hit modified */
#define PERF_MEM_SNOOP_SHIFT			19

#define PERF_MEM_SNOOPX_FWD			0x0001 /* Forward */
#define PERF_MEM_SNOOPX_PEER			0x0002 /* Transfer from peer */
#define PERF_MEM_SNOOPX_SHIFT			38

/* Locked instruction */
#define PERF_MEM_LOCK_NA			0x0001 /* Not available */
#define PERF_MEM_LOCK_LOCKED			0x0002 /* Locked transaction */
#define PERF_MEM_LOCK_SHIFT			24

/* TLB access */
#define PERF_MEM_TLB_NA				0x0001 /* Not available */
#define PERF_MEM_TLB_HIT			0x0002 /* Hit level */
#define PERF_MEM_TLB_MISS			0x0004 /* Miss level */
#define PERF_MEM_TLB_L1				0x0008 /* L1 */
#define PERF_MEM_TLB_L2				0x0010 /* L2 */
#define PERF_MEM_TLB_WK				0x0020 /* Hardware Walker*/
#define PERF_MEM_TLB_OS				0x0040 /* OS fault handler */
#define PERF_MEM_TLB_SHIFT			26

/* Access blocked */
#define PERF_MEM_BLK_NA				0x0001 /* Not available */
#define PERF_MEM_BLK_DATA			0x0002 /* Data could not be forwarded */
#define PERF_MEM_BLK_ADDR			0x0004 /* Address conflict */
#define PERF_MEM_BLK_SHIFT			40

/* Hop level */
#define PERF_MEM_HOPS_0				0x0001 /* Remote core, same node */
#define PERF_MEM_HOPS_1				0x0002 /* Remote node, same socket */
#define PERF_MEM_HOPS_2				0x0003 /* Remote socket, same board */
#define PERF_MEM_HOPS_3				0x0004 /* Remote board */
/* 5-7 available */
#define PERF_MEM_HOPS_SHIFT			43

#define PERF_MEM_S(a, s) \
	(((__u64)PERF_MEM_##a##_##s) << PERF_MEM_##a##_SHIFT)

/*
 * Layout of single taken branch records:
 *
 *      from: source instruction (may not always be a branch insn)
 *        to: branch target
 *   mispred: branch target was mispredicted
 * predicted: branch target was predicted
 *
 * support for mispred, predicted is optional. In case it
 * is not supported mispred = predicted = 0.
 *
 *     in_tx: running in a hardware transaction
 *     abort: aborting a hardware transaction
 *    cycles: cycles from last branch (or 0 if not supported)
 *      type: branch type
 *      spec: branch speculation info (or 0 if not supported)
 */
struct perf_branch_entry {
	__u64	from;
	__u64	to;
	__u64	mispred   :  1, /* target mispredicted */
		predicted :  1, /* target predicted */
		in_tx     :  1, /* in transaction */
		abort     :  1, /* transaction abort */
		cycles    : 16, /* cycle count to last branch */
		type      :  4, /* branch type */
		spec      :  2, /* branch speculation info */
		new_type  :  4, /* additional branch type */
		priv      :  3, /* privilege level */
		reserved  : 31;
};

/* Size of used info bits in struct perf_branch_entry */
#define PERF_BRANCH_ENTRY_INFO_BITS_MAX		33

union perf_sample_weight {
	__u64	      full;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	struct {
		__u32 var1_dw;
		__u16 var2_w;
		__u16 var3_w;
	};
#elif defined(__BIG_ENDIAN_BITFIELD)
	struct {
		__u16 var3_w;
		__u16 var2_w;
		__u32 var1_dw;
	};
#else
# error "Unknown endianness"
#endif
};

#endif /* _UAPI_LINUX_PERF_EVENT_H */
