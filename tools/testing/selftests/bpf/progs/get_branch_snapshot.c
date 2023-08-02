// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u64 test1_hits = 0;
__u64 address_low = 0;
__u64 address_high = 0;
int wasted_entries = 0;
long total_entries = 0;

#define ENTRY_CNT 32
struct perf_branch_entry entries[ENTRY_CNT] = {};

static inline bool gbs_in_range(__u64 val)
{
	return (val >= address_low) && (val < address_high);
}

SEC("fexit/bpf_testmod_loop_test")
int BPF_PROG(test1, int n, int ret)
{
	long i;

	total_entries = bpf_get_branch_snapshot(entries, sizeof(entries), 0);
	total_entries /= sizeof(struct perf_branch_entry);

	for (i = 0; i < ENTRY_CNT; i++) {
		if (i >= total_entries)
			break;
		if (gbs_in_range(entries[i].from) && gbs_in_range(entries[i].to))
			test1_hits++;
		else if (!test1_hits)
			wasted_entries++;
	}
	return 0;
}
