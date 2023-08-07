// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2023 Google
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#include "sample-filter.h"

/* BPF map that will be filled by user space */
struct filters {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct perf_bpf_filter_entry);
	__uint(max_entries, MAX_FILTERS);
} filters SEC(".maps");

int dropped;

void *bpf_cast_to_kern_ctx(void *) __ksym;

/* new kernel perf_sample_data definition */
struct perf_sample_data___new {
	__u64 sample_flags;
} __attribute__((preserve_access_index));

/* new kernel perf_mem_data_src definition */
union perf_mem_data_src___new {
	__u64 val;
	struct {
		__u64   mem_op:5,	/* type of opcode */
			mem_lvl:14,	/* memory hierarchy level */
			mem_snoop:5,	/* snoop mode */
			mem_lock:2,	/* lock instr */
			mem_dtlb:7,	/* tlb access */
			mem_lvl_num:4,	/* memory hierarchy level number */
			mem_remote:1,   /* remote */
			mem_snoopx:2,	/* snoop mode, ext */
			mem_blk:3,	/* access blocked */
			mem_hops:3,	/* hop level */
			mem_rsvd:18;
	};
};

/* helper function to return the given perf sample data */
static inline __u64 perf_get_sample(struct bpf_perf_event_data_kern *kctx,
				    struct perf_bpf_filter_entry *entry)
{
	struct perf_sample_data___new *data = (void *)kctx->data;

	if (!bpf_core_field_exists(data->sample_flags) ||
	    (data->sample_flags & entry->flags) == 0)
		return 0;

	switch (entry->flags) {
	case PERF_SAMPLE_IP:
		return kctx->data->ip;
	case PERF_SAMPLE_ID:
		return kctx->data->id;
	case PERF_SAMPLE_TID:
		if (entry->part)
			return kctx->data->tid_entry.pid;
		else
			return kctx->data->tid_entry.tid;
	case PERF_SAMPLE_CPU:
		return kctx->data->cpu_entry.cpu;
	case PERF_SAMPLE_TIME:
		return kctx->data->time;
	case PERF_SAMPLE_ADDR:
		return kctx->data->addr;
	case PERF_SAMPLE_PERIOD:
		return kctx->data->period;
	case PERF_SAMPLE_TRANSACTION:
		return kctx->data->txn;
	case PERF_SAMPLE_WEIGHT_STRUCT:
		if (entry->part == 1)
			return kctx->data->weight.var1_dw;
		if (entry->part == 2)
			return kctx->data->weight.var2_w;
		if (entry->part == 3)
			return kctx->data->weight.var3_w;
		/* fall through */
	case PERF_SAMPLE_WEIGHT:
		return kctx->data->weight.full;
	case PERF_SAMPLE_PHYS_ADDR:
		return kctx->data->phys_addr;
	case PERF_SAMPLE_CODE_PAGE_SIZE:
		return kctx->data->code_page_size;
	case PERF_SAMPLE_DATA_PAGE_SIZE:
		return kctx->data->data_page_size;
	case PERF_SAMPLE_DATA_SRC:
		if (entry->part == 1)
			return kctx->data->data_src.mem_op;
		if (entry->part == 2)
			return kctx->data->data_src.mem_lvl_num;
		if (entry->part == 3) {
			__u32 snoop = kctx->data->data_src.mem_snoop;
			__u32 snoopx = kctx->data->data_src.mem_snoopx;

			return (snoopx << 5) | snoop;
		}
		if (entry->part == 4)
			return kctx->data->data_src.mem_remote;
		if (entry->part == 5)
			return kctx->data->data_src.mem_lock;
		if (entry->part == 6)
			return kctx->data->data_src.mem_dtlb;
		if (entry->part == 7)
			return kctx->data->data_src.mem_blk;
		if (entry->part == 8) {
			union perf_mem_data_src___new *data = (void *)&kctx->data->data_src;

			if (bpf_core_field_exists(data->mem_hops))
				return data->mem_hops;

			return 0;
		}
		/* return the whole word */
		return kctx->data->data_src.val;
	default:
		break;
	}
	return 0;
}

#define CHECK_RESULT(data, op, val)			\
	if (!(data op val)) {				\
		if (!in_group)				\
			goto drop;			\
	} else if (in_group) {				\
		group_result = 1;			\
	}

/* BPF program to be called from perf event overflow handler */
SEC("perf_event")
int perf_sample_filter(void *ctx)
{
	struct bpf_perf_event_data_kern *kctx;
	struct perf_bpf_filter_entry *entry;
	__u64 sample_data;
	int in_group = 0;
	int group_result = 0;
	int i;

	kctx = bpf_cast_to_kern_ctx(ctx);

	for (i = 0; i < MAX_FILTERS; i++) {
		int key = i; /* needed for verifier :( */

		entry = bpf_map_lookup_elem(&filters, &key);
		if (entry == NULL)
			break;
		sample_data = perf_get_sample(kctx, entry);

		switch (entry->op) {
		case PBF_OP_EQ:
			CHECK_RESULT(sample_data, ==, entry->value)
			break;
		case PBF_OP_NEQ:
			CHECK_RESULT(sample_data, !=, entry->value)
			break;
		case PBF_OP_GT:
			CHECK_RESULT(sample_data, >, entry->value)
			break;
		case PBF_OP_GE:
			CHECK_RESULT(sample_data, >=, entry->value)
			break;
		case PBF_OP_LT:
			CHECK_RESULT(sample_data, <, entry->value)
			break;
		case PBF_OP_LE:
			CHECK_RESULT(sample_data, <=, entry->value)
			break;
		case PBF_OP_AND:
			CHECK_RESULT(sample_data, &, entry->value)
			break;
		case PBF_OP_GROUP_BEGIN:
			in_group = 1;
			group_result = 0;
			break;
		case PBF_OP_GROUP_END:
			if (group_result == 0)
				goto drop;
			in_group = 0;
			break;
		}
	}
	/* generate sample data */
	return 1;

drop:
	__sync_fetch_and_add(&dropped, 1);
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
