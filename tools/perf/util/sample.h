/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SAMPLE_H
#define __PERF_SAMPLE_H

#include <linux/perf_event.h>
#include <linux/types.h>

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
	u64	arch:1,	/* architecture (isa) */
		pred:2;	/* predication */
};

/* simd architecture flags */
#define SIMD_OP_FLAGS_ARCH_SVE		0x01	/* ARM SVE */

/* simd predicate flags */
#define SIMD_OP_FLAGS_PRED_PARTIAL	0x01	/* partial predicate */
#define SIMD_OP_FLAGS_PRED_EMPTY	0x02	/* empty predicate */

struct perf_sample {
	u64 ip;
	u32 pid, tid;
	u64 time;
	u64 addr;
	u64 id;
	u64 stream_id;
	u64 period;
	u64 weight;
	u64 transaction;
	u64 insn_cnt;
	u64 cyc_cnt;
	u32 cpu;
	u32 raw_size;
	u64 data_src;
	u64 phys_addr;
	u64 data_page_size;
	u64 code_page_size;
	u64 cgroup;
	u32 flags;
	u32 machine_pid;
	u32 vcpu;
	u16 insn_len;
	u8  cpumode;
	u16 misc;
	u16 ins_lat;
	union {
		u16 p_stage_cyc;
		u16 retire_lat;
	};
	bool no_hw_idx;		/* No hw_idx collected in branch_stack */
	char insn[MAX_INSN];
	void *raw_data;
	struct ip_callchain *callchain;
	struct branch_stack *branch_stack;
	u64 *branch_stack_cntr;
	struct regs_dump  user_regs;
	struct regs_dump  intr_regs;
	struct stack_dump user_stack;
	struct sample_read read;
	struct aux_sample aux_sample;
	struct simd_flags simd_flags;
};

/*
 * raw_data is always 4 bytes from an 8-byte boundary, so subtract 4 to get
 * 8-byte alignment.
 */
static inline void *perf_sample__synth_ptr(struct perf_sample *sample)
{
	return sample->raw_data - 4;
}

#endif /* __PERF_SAMPLE_H */
