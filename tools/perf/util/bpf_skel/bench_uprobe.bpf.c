// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2023 Red Hat
#include "vmlinux.h"
#include <bpf/bpf_tracing.h>

unsigned int nr_uprobes;

SEC("uprobe")
int BPF_UPROBE(empty)
{
       return 0;
}

SEC("uprobe")
int BPF_UPROBE(trace_printk)
{
	char fmt[] = "perf bench uprobe %u";

	bpf_trace_printk(fmt, sizeof(fmt), ++nr_uprobes);
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
