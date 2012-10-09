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
#ifndef _LINUX_PERF_EVENT_H
#define _LINUX_PERF_EVENT_H

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
 * physical and sw events of the kernel (and allow the profiling of them as
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

	PERF_SAMPLE_MAX = 1U << 12,		/* non-ABI */
};

/*
 * values to program into branch_sample_type when PERF_SAMPLE_BRANCH is set
 *
 * If the user does not pass priv level information via branch_sample_type,
 * the kernel uses the event's priv level. Branch and event priv levels do
 * not have to match. Branch priv level is checked for permissions.
 *
 * The branch types can be combined, however BRANCH_ANY covers all types
 * of branches and therefore it supersedes all the other types.
 */
enum perf_branch_sample_type {
	PERF_SAMPLE_BRANCH_USER		= 1U << 0, /* user branches */
	PERF_SAMPLE_BRANCH_KERNEL	= 1U << 1, /* kernel branches */
	PERF_SAMPLE_BRANCH_HV		= 1U << 2, /* hypervisor branches */

	PERF_SAMPLE_BRANCH_ANY		= 1U << 3, /* any branch types */
	PERF_SAMPLE_BRANCH_ANY_CALL	= 1U << 4, /* any call branch */
	PERF_SAMPLE_BRANCH_ANY_RETURN	= 1U << 5, /* any return branch */
	PERF_SAMPLE_BRANCH_IND_CALL	= 1U << 6, /* indirect calls */

	PERF_SAMPLE_BRANCH_MAX		= 1U << 7, /* non-ABI */
};

#define PERF_SAMPLE_BRANCH_PLM_ALL \
	(PERF_SAMPLE_BRANCH_USER|\
	 PERF_SAMPLE_BRANCH_KERNEL|\
	 PERF_SAMPLE_BRANCH_HV)

/*
 * The format of the data returned by read() on a perf event fd,
 * as specified by attr.read_format:
 *
 * struct read_format {
 *	{ u64		value;
 *	  { u64		time_enabled; } && PERF_FORMAT_TOTAL_TIME_ENABLED
 *	  { u64		time_running; } && PERF_FORMAT_TOTAL_TIME_RUNNING
 *	  { u64		id;           } && PERF_FORMAT_ID
 *	} && !PERF_FORMAT_GROUP
 *
 *	{ u64		nr;
 *	  { u64		time_enabled; } && PERF_FORMAT_TOTAL_TIME_ENABLED
 *	  { u64		time_running; } && PERF_FORMAT_TOTAL_TIME_RUNNING
 *	  { u64		value;
 *	    { u64	id;           } && PERF_FORMAT_ID
 *	  }		cntr[nr];
 *	} && PERF_FORMAT_GROUP
 * };
 */
enum perf_event_read_format {
	PERF_FORMAT_TOTAL_TIME_ENABLED		= 1U << 0,
	PERF_FORMAT_TOTAL_TIME_RUNNING		= 1U << 1,
	PERF_FORMAT_ID				= 1U << 2,
	PERF_FORMAT_GROUP			= 1U << 3,

	PERF_FORMAT_MAX = 1U << 4,		/* non-ABI */
};

#define PERF_ATTR_SIZE_VER0	64	/* sizeof first published struct */
#define PERF_ATTR_SIZE_VER1	72	/* add: config2 */
#define PERF_ATTR_SIZE_VER2	80	/* add: branch_sample_type */

/*
 * Hardware event_id to monitor via a performance monitoring event:
 */
struct perf_event_attr {

	/*
	 * Major type: hardware/software/tracepoint/etc.
	 */
	__u32			type;

	/*
	 * Size of the attr structure, for fwd/bwd compat.
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

				__reserved_1   : 43;

	union {
		__u32		wakeup_events;	  /* wakeup every n events */
		__u32		wakeup_watermark; /* bytes before wakeup   */
	};

	__u32			bp_type;
	union {
		__u64		bp_addr;
		__u64		config1; /* extension of config */
	};
	union {
		__u64		bp_len;
		__u64		config2; /* extension of config1 */
	};
	__u64	branch_sample_type; /* enum branch_sample_type */
};

/*
 * Ioctls that can be done on a perf event fd:
 */
#define PERF_EVENT_IOC_ENABLE		_IO ('$', 0)
#define PERF_EVENT_IOC_DISABLE		_IO ('$', 1)
#define PERF_EVENT_IOC_REFRESH		_IO ('$', 2)
#define PERF_EVENT_IOC_RESET		_IO ('$', 3)
#define PERF_EVENT_IOC_PERIOD		_IOW('$', 4, __u64)
#define PERF_EVENT_IOC_SET_OUTPUT	_IO ('$', 5)
#define PERF_EVENT_IOC_SET_FILTER	_IOW('$', 6, char *)

enum perf_event_ioc_flags {
	PERF_IOC_FLAG_GROUP		= 1U << 0,
};

/*
 * Structure of the page that can be mapped via mmap
 */
struct perf_event_mmap_page {
	__u32	version;		/* version number of this structure */
	__u32	compat_version;		/* lowest version this is compat with */

	/*
	 * Bits needed to read the hw events in user-space.
	 *
	 *   u32 seq, time_mult, time_shift, idx, width;
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
	 *     idx = pc->index;
	 *     count = pc->offset;
	 *     if (pc->cap_usr_rdpmc && idx) {
	 *       width = pc->pmc_width;
	 *       pmc = rdpmc(idx - 1);
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
	__u64	time_running;		/* time event on cpu */
	union {
		__u64	capabilities;
		__u64	cap_usr_time  : 1,
			cap_usr_rdpmc : 1,
			cap_____res   : 62;
	};

	/*
	 * If cap_usr_rdpmc this field provides the bit-width of the value
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
	 * delta since time_enabled (in ns) using rdtsc or similar.
	 *
	 *   u64 quot, rem;
	 *   u64 delta;
	 *
	 *   quot = (cyc >> time_shift);
	 *   rem = cyc & ((1 << time_shift) - 1);
	 *   delta = time_offset + quot * time_mult +
	 *              ((rem * time_mult) >> time_shift);
	 *
	 * Where time_offset,time_mult,time_shift and cyc are read in the
	 * seqcount loop described above. This delta can then be added to
	 * enabled and possible running (if idx), improving the scaling:
	 *
	 *   enabled += delta;
	 *   if (idx)
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
		 * Hole for extension of the self monitor capabilities
		 */

	__u64	__reserved[120];	/* align to 1k */

	/*
	 * Control data for the mmap() data buffer.
	 *
	 * User-space reading the @data_head value should issue an rmb(), on
	 * SMP capable platforms, after reading this value -- see
	 * perf_event_wakeup().
	 *
	 * When the mapping is PROT_WRITE the @data_tail value should be
	 * written by userspace to reflect the last read data. In this case
	 * the kernel will not over-write unread data.
	 */
	__u64   data_head;		/* head in the data section */
	__u64	data_tail;		/* user-space written tail */
};

#define PERF_RECORD_MISC_CPUMODE_MASK		(7 << 0)
#define PERF_RECORD_MISC_CPUMODE_UNKNOWN	(0 << 0)
#define PERF_RECORD_MISC_KERNEL			(1 << 0)
#define PERF_RECORD_MISC_USER			(2 << 0)
#define PERF_RECORD_MISC_HYPERVISOR		(3 << 0)
#define PERF_RECORD_MISC_GUEST_KERNEL		(4 << 0)
#define PERF_RECORD_MISC_GUEST_USER		(5 << 0)

/*
 * Indicates that the content of PERF_SAMPLE_IP points to
 * the actual instruction that triggered the event. See also
 * perf_event_attr::precise_ip.
 */
#define PERF_RECORD_MISC_EXACT_IP		(1 << 14)
/*
 * Reserve the last bit to indicate some extended misc field
 */
#define PERF_RECORD_MISC_EXT_RESERVED		(1 << 15)

struct perf_event_header {
	__u32	type;
	__u16	misc;
	__u16	size;
};

enum perf_event_type {

	/*
	 * If perf_event_attr.sample_id_all is set then all event types will
	 * have the sample_type selected fields related to where/when
	 * (identity) an event took place (TID, TIME, ID, CPU, STREAM_ID)
	 * described in PERF_RECORD_SAMPLE below, it will be stashed just after
	 * the perf_event_header and the fields already present for the existing
	 * fields, i.e. at the end of the payload. That way a newer perf.data
	 * file will be supported by older perf tools, with these new optional
	 * fields being ignored.
	 *
	 * The MMAP events record the PROT_EXEC mappings so that we can
	 * correlate userspace IPs to code. They have the following structure:
	 *
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	u64				addr;
	 *	u64				len;
	 *	u64				pgoff;
	 *	char				filename[];
	 * };
	 */
	PERF_RECORD_MMAP			= 1,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				id;
	 *	u64				lost;
	 * };
	 */
	PERF_RECORD_LOST			= 2,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *
	 *	u32				pid, tid;
	 *	char				comm[];
	 * };
	 */
	PERF_RECORD_COMM			= 3,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, ppid;
	 *	u32				tid, ptid;
	 *	u64				time;
	 * };
	 */
	PERF_RECORD_EXIT			= 4,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u64				time;
	 *	u64				id;
	 *	u64				stream_id;
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
	 * };
	 */
	PERF_RECORD_FORK			= 7,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *	u32				pid, tid;
	 *
	 *	struct read_format		values;
	 * };
	 */
	PERF_RECORD_READ			= 8,

	/*
	 * struct {
	 *	struct perf_event_header	header;
	 *
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
	 *	{ u64 from, to, flags } lbr[nr];} && PERF_SAMPLE_BRANCH_STACK
	 * };
	 */
	PERF_RECORD_SAMPLE			= 9,

	PERF_RECORD_MAX,			/* non-ABI */
};

#define PERF_MAX_STACK_DEPTH		127

enum perf_callchain_context {
	PERF_CONTEXT_HV			= (__u64)-32,
	PERF_CONTEXT_KERNEL		= (__u64)-128,
	PERF_CONTEXT_USER		= (__u64)-512,

	PERF_CONTEXT_GUEST		= (__u64)-2048,
	PERF_CONTEXT_GUEST_KERNEL	= (__u64)-2176,
	PERF_CONTEXT_GUEST_USER		= (__u64)-2560,

	PERF_CONTEXT_MAX		= (__u64)-4095,
};

#define PERF_FLAG_FD_NO_GROUP		(1U << 0)
#define PERF_FLAG_FD_OUTPUT		(1U << 1)
#define PERF_FLAG_PID_CGROUP		(1U << 2) /* pid=cgroup id, per-cpu mode only */

#ifdef __KERNEL__
/*
 * Kernel-internal data types and definitions:
 */

#ifdef CONFIG_PERF_EVENTS
# include <linux/cgroup.h>
# include <asm/perf_event.h>
# include <asm/local64.h>
#endif

struct perf_guest_info_callbacks {
	int				(*is_in_guest)(void);
	int				(*is_user_mode)(void);
	unsigned long			(*get_guest_ip)(void);
};

#ifdef CONFIG_HAVE_HW_BREAKPOINT
#include <asm/hw_breakpoint.h>
#endif

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/hrtimer.h>
#include <linux/fs.h>
#include <linux/pid_namespace.h>
#include <linux/workqueue.h>
#include <linux/ftrace.h>
#include <linux/cpu.h>
#include <linux/irq_work.h>
#include <linux/static_key.h>
#include <linux/atomic.h>
#include <linux/sysfs.h>
#include <asm/local.h>

struct perf_callchain_entry {
	__u64				nr;
	__u64				ip[PERF_MAX_STACK_DEPTH];
};

struct perf_raw_record {
	u32				size;
	void				*data;
};

/*
 * single taken branch record layout:
 *
 *      from: source instruction (may not always be a branch insn)
 *        to: branch target
 *   mispred: branch target was mispredicted
 * predicted: branch target was predicted
 *
 * support for mispred, predicted is optional. In case it
 * is not supported mispred = predicted = 0.
 */
struct perf_branch_entry {
	__u64	from;
	__u64	to;
	__u64	mispred:1,  /* target mispredicted */
		predicted:1,/* target predicted */
		reserved:62;
};

/*
 * branch stack layout:
 *  nr: number of taken branches stored in entries[]
 *
 * Note that nr can vary from sample to sample
 * branches (to, from) are stored from most recent
 * to least recent, i.e., entries[0] contains the most
 * recent branch.
 */
struct perf_branch_stack {
	__u64				nr;
	struct perf_branch_entry	entries[0];
};

struct task_struct;

/*
 * extra PMU register associated with an event
 */
struct hw_perf_event_extra {
	u64		config;	/* register value */
	unsigned int	reg;	/* register address or index */
	int		alloc;	/* extra register already allocated */
	int		idx;	/* index in shared_regs->regs[] */
};

/**
 * struct hw_perf_event - performance event hardware details:
 */
struct hw_perf_event {
#ifdef CONFIG_PERF_EVENTS
	union {
		struct { /* hardware */
			u64		config;
			u64		last_tag;
			unsigned long	config_base;
			unsigned long	event_base;
			int		event_base_rdpmc;
			int		idx;
			int		last_cpu;

			struct hw_perf_event_extra extra_reg;
			struct hw_perf_event_extra branch_reg;
		};
		struct { /* software */
			struct hrtimer	hrtimer;
		};
#ifdef CONFIG_HAVE_HW_BREAKPOINT
		struct { /* breakpoint */
			struct arch_hw_breakpoint	info;
			struct list_head		bp_list;
			/*
			 * Crufty hack to avoid the chicken and egg
			 * problem hw_breakpoint has with context
			 * creation and event initalization.
			 */
			struct task_struct		*bp_target;
		};
#endif
	};
	int				state;
	local64_t			prev_count;
	u64				sample_period;
	u64				last_period;
	local64_t			period_left;
	u64                             interrupts_seq;
	u64				interrupts;

	u64				freq_time_stamp;
	u64				freq_count_stamp;
#endif
};

/*
 * hw_perf_event::state flags
 */
#define PERF_HES_STOPPED	0x01 /* the counter is stopped */
#define PERF_HES_UPTODATE	0x02 /* event->count up-to-date */
#define PERF_HES_ARCH		0x04

struct perf_event;

/*
 * Common implementation detail of pmu::{start,commit,cancel}_txn
 */
#define PERF_EVENT_TXN 0x1

/**
 * struct pmu - generic performance monitoring unit
 */
struct pmu {
	struct list_head		entry;

	struct device			*dev;
	const struct attribute_group	**attr_groups;
	char				*name;
	int				type;

	int * __percpu			pmu_disable_count;
	struct perf_cpu_context * __percpu pmu_cpu_context;
	int				task_ctx_nr;

	/*
	 * Fully disable/enable this PMU, can be used to protect from the PMI
	 * as well as for lazy/batch writing of the MSRs.
	 */
	void (*pmu_enable)		(struct pmu *pmu); /* optional */
	void (*pmu_disable)		(struct pmu *pmu); /* optional */

	/*
	 * Try and initialize the event for this PMU.
	 * Should return -ENOENT when the @event doesn't match this PMU.
	 */
	int (*event_init)		(struct perf_event *event);

#define PERF_EF_START	0x01		/* start the counter when adding    */
#define PERF_EF_RELOAD	0x02		/* reload the counter when starting */
#define PERF_EF_UPDATE	0x04		/* update the counter when stopping */

	/*
	 * Adds/Removes a counter to/from the PMU, can be done inside
	 * a transaction, see the ->*_txn() methods.
	 */
	int  (*add)			(struct perf_event *event, int flags);
	void (*del)			(struct perf_event *event, int flags);

	/*
	 * Starts/Stops a counter present on the PMU. The PMI handler
	 * should stop the counter when perf_event_overflow() returns
	 * !0. ->start() will be used to continue.
	 */
	void (*start)			(struct perf_event *event, int flags);
	void (*stop)			(struct perf_event *event, int flags);

	/*
	 * Updates the counter value of the event.
	 */
	void (*read)			(struct perf_event *event);

	/*
	 * Group events scheduling is treated as a transaction, add
	 * group events as a whole and perform one schedulability test.
	 * If the test fails, roll back the whole group
	 *
	 * Start the transaction, after this ->add() doesn't need to
	 * do schedulability tests.
	 */
	void (*start_txn)		(struct pmu *pmu); /* optional */
	/*
	 * If ->start_txn() disabled the ->add() schedulability test
	 * then ->commit_txn() is required to perform one. On success
	 * the transaction is closed. On error the transaction is kept
	 * open until ->cancel_txn() is called.
	 */
	int  (*commit_txn)		(struct pmu *pmu); /* optional */
	/*
	 * Will cancel the transaction, assumes ->del() is called
	 * for each successful ->add() during the transaction.
	 */
	void (*cancel_txn)		(struct pmu *pmu); /* optional */

	/*
	 * Will return the value for perf_event_mmap_page::index for this event,
	 * if no implementation is provided it will default to: event->hw.idx + 1.
	 */
	int (*event_idx)		(struct perf_event *event); /*optional */

	/*
	 * flush branch stack on context-switches (needed in cpu-wide mode)
	 */
	void (*flush_branch_stack)	(void);
};

/**
 * enum perf_event_active_state - the states of a event
 */
enum perf_event_active_state {
	PERF_EVENT_STATE_ERROR		= -2,
	PERF_EVENT_STATE_OFF		= -1,
	PERF_EVENT_STATE_INACTIVE	=  0,
	PERF_EVENT_STATE_ACTIVE		=  1,
};

struct file;
struct perf_sample_data;

typedef void (*perf_overflow_handler_t)(struct perf_event *,
					struct perf_sample_data *,
					struct pt_regs *regs);

enum perf_group_flag {
	PERF_GROUP_SOFTWARE		= 0x1,
};

#define SWEVENT_HLIST_BITS		8
#define SWEVENT_HLIST_SIZE		(1 << SWEVENT_HLIST_BITS)

struct swevent_hlist {
	struct hlist_head		heads[SWEVENT_HLIST_SIZE];
	struct rcu_head			rcu_head;
};

#define PERF_ATTACH_CONTEXT	0x01
#define PERF_ATTACH_GROUP	0x02
#define PERF_ATTACH_TASK	0x04

#ifdef CONFIG_CGROUP_PERF
/*
 * perf_cgroup_info keeps track of time_enabled for a cgroup.
 * This is a per-cpu dynamically allocated data structure.
 */
struct perf_cgroup_info {
	u64				time;
	u64				timestamp;
};

struct perf_cgroup {
	struct				cgroup_subsys_state css;
	struct				perf_cgroup_info *info;	/* timing info, one per cpu */
};
#endif

struct ring_buffer;

/**
 * struct perf_event - performance event kernel representation:
 */
struct perf_event {
#ifdef CONFIG_PERF_EVENTS
	struct list_head		group_entry;
	struct list_head		event_entry;
	struct list_head		sibling_list;
	struct hlist_node		hlist_entry;
	int				nr_siblings;
	int				group_flags;
	struct perf_event		*group_leader;
	struct pmu			*pmu;

	enum perf_event_active_state	state;
	unsigned int			attach_state;
	local64_t			count;
	atomic64_t			child_count;

	/*
	 * These are the total time in nanoseconds that the event
	 * has been enabled (i.e. eligible to run, and the task has
	 * been scheduled in, if this is a per-task event)
	 * and running (scheduled onto the CPU), respectively.
	 *
	 * They are computed from tstamp_enabled, tstamp_running and
	 * tstamp_stopped when the event is in INACTIVE or ACTIVE state.
	 */
	u64				total_time_enabled;
	u64				total_time_running;

	/*
	 * These are timestamps used for computing total_time_enabled
	 * and total_time_running when the event is in INACTIVE or
	 * ACTIVE state, measured in nanoseconds from an arbitrary point
	 * in time.
	 * tstamp_enabled: the notional time when the event was enabled
	 * tstamp_running: the notional time when the event was scheduled on
	 * tstamp_stopped: in INACTIVE state, the notional time when the
	 *	event was scheduled off.
	 */
	u64				tstamp_enabled;
	u64				tstamp_running;
	u64				tstamp_stopped;

	/*
	 * timestamp shadows the actual context timing but it can
	 * be safely used in NMI interrupt context. It reflects the
	 * context time as it was when the event was last scheduled in.
	 *
	 * ctx_time already accounts for ctx->timestamp. Therefore to
	 * compute ctx_time for a sample, simply add perf_clock().
	 */
	u64				shadow_ctx_time;

	struct perf_event_attr		attr;
	u16				header_size;
	u16				id_header_size;
	u16				read_size;
	struct hw_perf_event		hw;

	struct perf_event_context	*ctx;
	struct file			*filp;

	/*
	 * These accumulate total time (in nanoseconds) that children
	 * events have been enabled and running, respectively.
	 */
	atomic64_t			child_total_time_enabled;
	atomic64_t			child_total_time_running;

	/*
	 * Protect attach/detach and child_list:
	 */
	struct mutex			child_mutex;
	struct list_head		child_list;
	struct perf_event		*parent;

	int				oncpu;
	int				cpu;

	struct list_head		owner_entry;
	struct task_struct		*owner;

	/* mmap bits */
	struct mutex			mmap_mutex;
	atomic_t			mmap_count;
	int				mmap_locked;
	struct user_struct		*mmap_user;
	struct ring_buffer		*rb;
	struct list_head		rb_entry;

	/* poll related */
	wait_queue_head_t		waitq;
	struct fasync_struct		*fasync;

	/* delayed work for NMIs and such */
	int				pending_wakeup;
	int				pending_kill;
	int				pending_disable;
	struct irq_work			pending;

	atomic_t			event_limit;

	void (*destroy)(struct perf_event *);
	struct rcu_head			rcu_head;

	struct pid_namespace		*ns;
	u64				id;

	perf_overflow_handler_t		overflow_handler;
	void				*overflow_handler_context;

#ifdef CONFIG_EVENT_TRACING
	struct ftrace_event_call	*tp_event;
	struct event_filter		*filter;
#ifdef CONFIG_FUNCTION_TRACER
	struct ftrace_ops               ftrace_ops;
#endif
#endif

#ifdef CONFIG_CGROUP_PERF
	struct perf_cgroup		*cgrp; /* cgroup event is attach to */
	int				cgrp_defer_enabled;
#endif

#endif /* CONFIG_PERF_EVENTS */
};

enum perf_event_context_type {
	task_context,
	cpu_context,
};

/**
 * struct perf_event_context - event context structure
 *
 * Used as a container for task events and CPU events as well:
 */
struct perf_event_context {
	struct pmu			*pmu;
	enum perf_event_context_type	type;
	/*
	 * Protect the states of the events in the list,
	 * nr_active, and the list:
	 */
	raw_spinlock_t			lock;
	/*
	 * Protect the list of events.  Locking either mutex or lock
	 * is sufficient to ensure the list doesn't change; to change
	 * the list you need to lock both the mutex and the spinlock.
	 */
	struct mutex			mutex;

	struct list_head		pinned_groups;
	struct list_head		flexible_groups;
	struct list_head		event_list;
	int				nr_events;
	int				nr_active;
	int				is_active;
	int				nr_stat;
	int				nr_freq;
	int				rotate_disable;
	atomic_t			refcount;
	struct task_struct		*task;

	/*
	 * Context clock, runs when context enabled.
	 */
	u64				time;
	u64				timestamp;

	/*
	 * These fields let us detect when two contexts have both
	 * been cloned (inherited) from a common ancestor.
	 */
	struct perf_event_context	*parent_ctx;
	u64				parent_gen;
	u64				generation;
	int				pin_count;
	int				nr_cgroups;	 /* cgroup evts */
	int				nr_branch_stack; /* branch_stack evt */
	struct rcu_head			rcu_head;
};

/*
 * Number of contexts where an event can trigger:
 *	task, softirq, hardirq, nmi.
 */
#define PERF_NR_CONTEXTS	4

/**
 * struct perf_event_cpu_context - per cpu event context structure
 */
struct perf_cpu_context {
	struct perf_event_context	ctx;
	struct perf_event_context	*task_ctx;
	int				active_oncpu;
	int				exclusive;
	struct list_head		rotation_list;
	int				jiffies_interval;
	struct pmu			*active_pmu;
	struct perf_cgroup		*cgrp;
};

struct perf_output_handle {
	struct perf_event		*event;
	struct ring_buffer		*rb;
	unsigned long			wakeup;
	unsigned long			size;
	void				*addr;
	int				page;
};

#ifdef CONFIG_PERF_EVENTS

extern int perf_pmu_register(struct pmu *pmu, char *name, int type);
extern void perf_pmu_unregister(struct pmu *pmu);

extern int perf_num_counters(void);
extern const char *perf_pmu_name(void);
extern void __perf_event_task_sched_in(struct task_struct *prev,
				       struct task_struct *task);
extern void __perf_event_task_sched_out(struct task_struct *prev,
					struct task_struct *next);
extern int perf_event_init_task(struct task_struct *child);
extern void perf_event_exit_task(struct task_struct *child);
extern void perf_event_free_task(struct task_struct *task);
extern void perf_event_delayed_put(struct task_struct *task);
extern void perf_event_print_debug(void);
extern void perf_pmu_disable(struct pmu *pmu);
extern void perf_pmu_enable(struct pmu *pmu);
extern int perf_event_task_disable(void);
extern int perf_event_task_enable(void);
extern int perf_event_refresh(struct perf_event *event, int refresh);
extern void perf_event_update_userpage(struct perf_event *event);
extern int perf_event_release_kernel(struct perf_event *event);
extern struct perf_event *
perf_event_create_kernel_counter(struct perf_event_attr *attr,
				int cpu,
				struct task_struct *task,
				perf_overflow_handler_t callback,
				void *context);
extern void perf_pmu_migrate_context(struct pmu *pmu,
				int src_cpu, int dst_cpu);
extern u64 perf_event_read_value(struct perf_event *event,
				 u64 *enabled, u64 *running);


struct perf_sample_data {
	u64				type;

	u64				ip;
	struct {
		u32	pid;
		u32	tid;
	}				tid_entry;
	u64				time;
	u64				addr;
	u64				id;
	u64				stream_id;
	struct {
		u32	cpu;
		u32	reserved;
	}				cpu_entry;
	u64				period;
	struct perf_callchain_entry	*callchain;
	struct perf_raw_record		*raw;
	struct perf_branch_stack	*br_stack;
};

static inline void perf_sample_data_init(struct perf_sample_data *data,
					 u64 addr, u64 period)
{
	/* remaining struct members initialized in perf_prepare_sample() */
	data->addr = addr;
	data->raw  = NULL;
	data->br_stack = NULL;
	data->period	= period;
}

extern void perf_output_sample(struct perf_output_handle *handle,
			       struct perf_event_header *header,
			       struct perf_sample_data *data,
			       struct perf_event *event);
extern void perf_prepare_sample(struct perf_event_header *header,
				struct perf_sample_data *data,
				struct perf_event *event,
				struct pt_regs *regs);

extern int perf_event_overflow(struct perf_event *event,
				 struct perf_sample_data *data,
				 struct pt_regs *regs);

static inline bool is_sampling_event(struct perf_event *event)
{
	return event->attr.sample_period != 0;
}

/*
 * Return 1 for a software event, 0 for a hardware event
 */
static inline int is_software_event(struct perf_event *event)
{
	return event->pmu->task_ctx_nr == perf_sw_context;
}

extern struct static_key perf_swevent_enabled[PERF_COUNT_SW_MAX];

extern void __perf_sw_event(u32, u64, struct pt_regs *, u64);

#ifndef perf_arch_fetch_caller_regs
static inline void perf_arch_fetch_caller_regs(struct pt_regs *regs, unsigned long ip) { }
#endif

/*
 * Take a snapshot of the regs. Skip ip and frame pointer to
 * the nth caller. We only need a few of the regs:
 * - ip for PERF_SAMPLE_IP
 * - cs for user_mode() tests
 * - bp for callchains
 * - eflags, for future purposes, just in case
 */
static inline void perf_fetch_caller_regs(struct pt_regs *regs)
{
	memset(regs, 0, sizeof(*regs));

	perf_arch_fetch_caller_regs(regs, CALLER_ADDR0);
}

static __always_inline void
perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr)
{
	struct pt_regs hot_regs;

	if (static_key_false(&perf_swevent_enabled[event_id])) {
		if (!regs) {
			perf_fetch_caller_regs(&hot_regs);
			regs = &hot_regs;
		}
		__perf_sw_event(event_id, nr, regs, addr);
	}
}

extern struct static_key_deferred perf_sched_events;

static inline void perf_event_task_sched_in(struct task_struct *prev,
					    struct task_struct *task)
{
	if (static_key_false(&perf_sched_events.key))
		__perf_event_task_sched_in(prev, task);
}

static inline void perf_event_task_sched_out(struct task_struct *prev,
					     struct task_struct *next)
{
	perf_sw_event(PERF_COUNT_SW_CONTEXT_SWITCHES, 1, NULL, 0);

	if (static_key_false(&perf_sched_events.key))
		__perf_event_task_sched_out(prev, next);
}

extern void perf_event_mmap(struct vm_area_struct *vma);
extern struct perf_guest_info_callbacks *perf_guest_cbs;
extern int perf_register_guest_info_callbacks(struct perf_guest_info_callbacks *callbacks);
extern int perf_unregister_guest_info_callbacks(struct perf_guest_info_callbacks *callbacks);

extern void perf_event_comm(struct task_struct *tsk);
extern void perf_event_fork(struct task_struct *tsk);

/* Callchains */
DECLARE_PER_CPU(struct perf_callchain_entry, perf_callchain_entry);

extern void perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs);
extern void perf_callchain_kernel(struct perf_callchain_entry *entry, struct pt_regs *regs);

static inline void perf_callchain_store(struct perf_callchain_entry *entry, u64 ip)
{
	if (entry->nr < PERF_MAX_STACK_DEPTH)
		entry->ip[entry->nr++] = ip;
}

extern int sysctl_perf_event_paranoid;
extern int sysctl_perf_event_mlock;
extern int sysctl_perf_event_sample_rate;

extern int perf_proc_update_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos);

static inline bool perf_paranoid_tracepoint_raw(void)
{
	return sysctl_perf_event_paranoid > -1;
}

static inline bool perf_paranoid_cpu(void)
{
	return sysctl_perf_event_paranoid > 0;
}

static inline bool perf_paranoid_kernel(void)
{
	return sysctl_perf_event_paranoid > 1;
}

extern void perf_event_init(void);
extern void perf_tp_event(u64 addr, u64 count, void *record,
			  int entry_size, struct pt_regs *regs,
			  struct hlist_head *head, int rctx,
			  struct task_struct *task);
extern void perf_bp_event(struct perf_event *event, void *data);

#ifndef perf_misc_flags
# define perf_misc_flags(regs) \
		(user_mode(regs) ? PERF_RECORD_MISC_USER : PERF_RECORD_MISC_KERNEL)
# define perf_instruction_pointer(regs)	instruction_pointer(regs)
#endif

static inline bool has_branch_stack(struct perf_event *event)
{
	return event->attr.sample_type & PERF_SAMPLE_BRANCH_STACK;
}

extern int perf_output_begin(struct perf_output_handle *handle,
			     struct perf_event *event, unsigned int size);
extern void perf_output_end(struct perf_output_handle *handle);
extern void perf_output_copy(struct perf_output_handle *handle,
			     const void *buf, unsigned int len);
extern int perf_swevent_get_recursion_context(void);
extern void perf_swevent_put_recursion_context(int rctx);
extern void perf_event_enable(struct perf_event *event);
extern void perf_event_disable(struct perf_event *event);
extern void perf_event_task_tick(void);
#else
static inline void
perf_event_task_sched_in(struct task_struct *prev,
			 struct task_struct *task)			{ }
static inline void
perf_event_task_sched_out(struct task_struct *prev,
			  struct task_struct *next)			{ }
static inline int perf_event_init_task(struct task_struct *child)	{ return 0; }
static inline void perf_event_exit_task(struct task_struct *child)	{ }
static inline void perf_event_free_task(struct task_struct *task)	{ }
static inline void perf_event_delayed_put(struct task_struct *task)	{ }
static inline void perf_event_print_debug(void)				{ }
static inline int perf_event_task_disable(void)				{ return -EINVAL; }
static inline int perf_event_task_enable(void)				{ return -EINVAL; }
static inline int perf_event_refresh(struct perf_event *event, int refresh)
{
	return -EINVAL;
}

static inline void
perf_sw_event(u32 event_id, u64 nr, struct pt_regs *regs, u64 addr)	{ }
static inline void
perf_bp_event(struct perf_event *event, void *data)			{ }

static inline int perf_register_guest_info_callbacks
(struct perf_guest_info_callbacks *callbacks)				{ return 0; }
static inline int perf_unregister_guest_info_callbacks
(struct perf_guest_info_callbacks *callbacks)				{ return 0; }

static inline void perf_event_mmap(struct vm_area_struct *vma)		{ }
static inline void perf_event_comm(struct task_struct *tsk)		{ }
static inline void perf_event_fork(struct task_struct *tsk)		{ }
static inline void perf_event_init(void)				{ }
static inline int  perf_swevent_get_recursion_context(void)		{ return -1; }
static inline void perf_swevent_put_recursion_context(int rctx)		{ }
static inline void perf_event_enable(struct perf_event *event)		{ }
static inline void perf_event_disable(struct perf_event *event)		{ }
static inline void perf_event_task_tick(void)				{ }
#endif

#define perf_output_put(handle, x) perf_output_copy((handle), &(x), sizeof(x))

/*
 * This has to have a higher priority than migration_notifier in sched.c.
 */
#define perf_cpu_notifier(fn)						\
do {									\
	static struct notifier_block fn##_nb __cpuinitdata =		\
		{ .notifier_call = fn, .priority = CPU_PRI_PERF };	\
	fn(&fn##_nb, (unsigned long)CPU_UP_PREPARE,			\
		(void *)(unsigned long)smp_processor_id());		\
	fn(&fn##_nb, (unsigned long)CPU_STARTING,			\
		(void *)(unsigned long)smp_processor_id());		\
	fn(&fn##_nb, (unsigned long)CPU_ONLINE,				\
		(void *)(unsigned long)smp_processor_id());		\
	register_cpu_notifier(&fn##_nb);				\
} while (0)


#define PMU_FORMAT_ATTR(_name, _format)					\
static ssize_t								\
_name##_show(struct device *dev,					\
			       struct device_attribute *attr,		\
			       char *page)				\
{									\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);			\
	return sprintf(page, _format "\n");				\
}									\
									\
static struct device_attribute format_attr_##_name = __ATTR_RO(_name)

#endif /* __KERNEL__ */
#endif /* _LINUX_PERF_EVENT_H */
