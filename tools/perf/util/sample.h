/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SAMPLE_H
#define __PERF_SAMPLE_H

#include <linux/perf_event.h>
#include <linux/types.h>

struct evsel;
struct machine;
struct thread;

/* number of register is bound by the number of bits in regs_dump::mask (64) */
#define PERF_SAMPLE_REGS_CACHE_SIZE (8 * sizeof(u64))

struct regs_dump {
	u64 abi;
	u64 mask;
	u64 *regs;

	/* Cached values/mask filled by first register access. */
	u64 cache_regs[PERF_SAMPLE_REGS_CACHE_SIZE];
	u64 cache_mask;
};

struct stack_dump {
	u16 offset;
	u64 size;
	char *data;
};

struct sample_read_value {
	u64 value;
	u64 id;   /* only if PERF_FORMAT_ID */
	u64 lost; /* only if PERF_FORMAT_LOST */
};

struct sample_read {
	u64 time_enabled;
	u64 time_running;
	union {
		struct {
			u64 nr;
			struct sample_read_value *values;
		} group;
		struct sample_read_value one;
	};
};

static inline size_t sample_read_value_size(u64 read_format)
{
	/* PERF_FORMAT_ID is forced for PERF_SAMPLE_READ */
	if (read_format & PERF_FORMAT_LOST)
		return sizeof(struct sample_read_value);
	else
		return offsetof(struct sample_read_value, lost);
}

static inline struct sample_read_value *next_sample_read_value(struct sample_read_value *v, u64 read_format)
{
	return (void *)v + sample_read_value_size(read_format);
}

#define sample_read_group__for_each(v, nr, rf) \
	for (int __i = 0; __i < (int)nr; v = next_sample_read_value(v, rf), __i++)

#define MAX_INSN 16

struct aux_sample {
	u64 size;
	void *data;
};

struct simd_flags {
	u8	arch:  2,	/* architecture (isa) */
		pred:  3,	/* predication */
		resv:  3;	/* reserved */
};

/* simd architecture flags */
enum simd_op_flags {
	SIMD_OP_FLAGS_ARCH_NONE = 0x0,	/* No SIMD operation */
	SIMD_OP_FLAGS_ARCH_SVE,		/* Arm SVE */
	SIMD_OP_FLAGS_ARCH_SME,		/* Arm SME */
	SIMD_OP_FLAGS_ARCH_ASE,		/* Arm Advanced SIMD */
};

/* simd predicate flags */
enum simd_pred_flags {
	SIMD_OP_FLAGS_PRED_NONE = 0x0,	/* Not available */
	SIMD_OP_FLAGS_PRED_PARTIAL,	/* partial predicate */
	SIMD_OP_FLAGS_PRED_EMPTY,	/* empty predicate */
	SIMD_OP_FLAGS_PRED_FULL,	/* full predicate */
	SIMD_OP_FLAGS_PRED_DISABLED,	/* disabled predicate */
};

/**
 * struct perf_sample
 *
 * A sample is generally filled in by evlist__parse_sample/evsel__parse_sample
 * which fills in the variables from a "union perf_event *event" which is data
 * from a perf ring buffer or perf.data file. The "event" sample is variable in
 * length as determined by the perf_event_attr (in the evsel) and details within
 * the sample event itself. A struct perf_sample avoids needing to care about
 * the variable length nature of the original event.
 *
 * To avoid being excessively large parts of the struct perf_sample are pointers
 * into the original sample event. In general the lifetime of a struct
 * perf_sample needs to be less than the "union perf_event *event" it was
 * derived from.
 *
 * The struct regs_dump user_regs and intr_regs are lazily allocated again for
 * size reasons, due to them holding a cache of looked up registers. The
 * function pair of perf_sample__init and perf_sample__exit correctly initialize
 * and clean up these values.
 */
struct perf_sample {
	/** @evsel: Backward reference to the evsel used when constructing the sample. */
	struct evsel *evsel;
	/** @ip: The sample event PERF_SAMPLE_IP value. */
	u64 ip;
	/** @pid: The sample event PERF_SAMPLE_TID pid value. */
	u32 pid;
	/** @tid: The sample event PERF_SAMPLE_TID tid value. */
	u32 tid;
	/** @time: The sample event PERF_SAMPLE_TIME value. */
	u64 time;
	/** @addr: The sample event PERF_SAMPLE_ADDR value. */
	u64 addr;
	/** @id: The sample event PERF_SAMPLE_ID or PERF_SAMPLE_IDENTIFIER value. */
	u64 id;
	/** @stream_id: The sample event PERF_SAMPLE_STREAM_ID value. */
	u64 stream_id;
	/** @period: The sample event PERF_SAMPLE_PERIOD value. */
	u64 period;
	/** @weight: Data determined by PERF_SAMPLE_WEIGHT or PERF_SAMPLE_WEIGHT_STRUCT. */
	u64 weight;
	/** @transaction: The sample event PERF_SAMPLE_TRANSACTION value. */
	u64 transaction;
	/** @insn_cnt: Filled in and used by intel-pt. */
	u64 insn_cnt;
	/** @cyc_cnt: Filled in and used by intel-pt. */
	u64 cyc_cnt;
	/** @cpu: The sample event PERF_SAMPLE_CPU value. */
	u32 cpu;
	/**
	 * @raw_size: The size in bytes of raw data from PERF_SAMPLE_RAW. For
	 *            alignment reasons this should always be sizeof(u32)
	 *            followed by a multiple of sizeof(u64).
	 */
	u32 raw_size;
	/** @data_src: The sample event PERF_SAMPLE_DATA_SRC value. */
	u64 data_src;
	/** @phys_addr: The sample event PERF_SAMPLE_PHYS_ADDR value. */
	u64 phys_addr;
	/** @data_page_size: The sample event PERF_SAMPLE_DATA_PAGE_SIZE value. */
	u64 data_page_size;
	/** @code_page_size: The sample event PERF_SAMPLE_CODE_PAGE_SIZE value. */
	u64 code_page_size;
	/** @cgroup: The sample event PERF_SAMPLE_CGROUP value. */
	u64 cgroup;
	/** @flags: Extra flag data from auxiliary events like intel-pt. */
	u32 flags;
	/** @machine_pid: The guest machine pid derived from the sample id. */
	u32 machine_pid;
	/** @vcpu: The guest machine vcpu derived from the sample id. */
	u32 vcpu;
	/**
	 * @insn_len: Instruction length from auxiliary events like
	 *            intel-pt. The instruction itself is held in insn.
	 */
	u16 insn_len;
	/** @misc: The entire struct perf_event_header misc variable. */
	u16 misc;
	/**
	 * @ins_lat: Instruction latency information from weight2 in
	 *           PERF_SAMPLE_WEIGHT_STRUCT or auxiliary events like
	 *           intel-pt.
	 */
	u16 ins_lat;
	/**
	 * @weight3: From PERF_SAMPLE_WEIGHT_STRUCT. On x86 holds retire_lat, on
	 *           powerpc holds p_stage_cyc.
	 */
	u16 weight3;
	/**
	 * @cpumode: The cpumode from struct perf_event_header misc variable
	 *           masked with CPUMODE_MASK. Gives user, kernel and hypervisor
	 *           information.
	 */
	u8  cpumode;
	/**
	 * @no_hw_idx: For PERF_SAMPLE_BRANCH_STACK, true when
	 *             PERF_SAMPLE_BRANCH_HW_INDEX isn't set.
	 */
	bool no_hw_idx;
	/**
	 * @deferred_callchain: When processing PERF_SAMPLE_CALLCHAIN a deferred
	 *                      user callchain marker was encountered.
	 */
	bool deferred_callchain;
	/**
	 * @merged_callchain: A synthesized merged callchain that is allocated
	 *                    and needs freeing.
	 */
	bool merged_callchain;
	/**
	 * @deferred_cookie: Identifier of the deferred callchain in the later
	 *                   PERF_RECORD_CALLCHAIN_DEFERRED event.
	 */
	u64 deferred_cookie;
	/** @insn: A copy of the sampled instruction filled in by perf_sample__fetch_insn. */
	char insn[MAX_INSN];
	/** @raw_data: Pointer into the original event for PERF_SAMPLE_RAW data. */
	void *raw_data;
	/**
	 * @callchain: Pointer into the original event for PERF_SAMPLE_CALLCHAIN
	 *             data. For deferred callchains this may be a copy that
	 *             needs freeing, see sample__merge_deferred_callchain.
	 */
	struct ip_callchain *callchain;
	/** @branch_stack: Pointer into the original event for PERF_SAMPLE_BRANCH_STACK data. */
	struct branch_stack *branch_stack;
	/**
	 * @branch_stack_cntr: Pointer into the original event for
	 *                     PERF_SAMPLE_BRANCH_COUNTERS data.
	 */
	u64 *branch_stack_cntr;
	/** @user_regs: Values and pointers into the sample for PERF_SAMPLE_REGS_USER. */
	struct regs_dump  *user_regs;
	/** @intr_regs: Values and pointers into the sample for PERF_SAMPLE_REGS_INTR. */
	struct regs_dump  *intr_regs;
	/** @user_stack: Size and pointer into the sample for PERF_SAMPLE_STACK_USER. */
	struct stack_dump user_stack;
	/**
	 * @read: The sample event PERF_SAMPLE_READ counter values. The valid
	 *        values depend on the attr.read_format PERF_FORMAT_ values.
	 */
	struct sample_read read;
	/**
	 * @aux_sample: Similar to raw data but with a 64-bit size and
	 *              alignment, PERF_SAMPLE_AUX data.
	 */
	struct aux_sample aux_sample;
	/** @simd_flags: SIMD flag information from ARM SPE auxiliary events. */
	struct simd_flags simd_flags;
};

void perf_sample__init(struct perf_sample *sample, bool all);
void perf_sample__exit(struct perf_sample *sample);
struct regs_dump *perf_sample__user_regs(struct perf_sample *sample);
struct regs_dump *perf_sample__intr_regs(struct perf_sample *sample);

void perf_sample__fetch_insn(struct perf_sample *sample,
			     struct thread *thread,
			     struct machine *machine);

/*
 * raw_data is always 4 bytes from an 8-byte boundary, so subtract 4 to get
 * 8-byte alignment.
 */
static inline void *perf_sample__synth_ptr(struct perf_sample *sample)
{
	return sample->raw_data - 4;
}

#endif /* __PERF_SAMPLE_H */
