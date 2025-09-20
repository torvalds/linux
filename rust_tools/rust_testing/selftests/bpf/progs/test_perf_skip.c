// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

uintptr_t ip;

SEC("perf_event")
int handler(struct bpf_perf_event_data *data)
{
	/* Skip events that have the correct ip. */
	return ip != PT_REGS_IP(&data->regs);
}

char _license[] SEC("license") = "GPL";
