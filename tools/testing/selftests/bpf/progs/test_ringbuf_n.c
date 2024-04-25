// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2024 Andrea Righi <andrea.righi@canonical.com>

#include <linux/bpf.h>
#include <sched.h>
#include <unistd.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

#define TASK_COMM_LEN 16

struct sample {
	int pid;
	long value;
	char comm[16];
};

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf SEC(".maps");

int pid = 0;
long value = 0;

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int test_ringbuf_n(void *ctx)
{
	int cur_pid = bpf_get_current_pid_tgid() >> 32;
	struct sample *sample;

	if (cur_pid != pid)
		return 0;

	sample = bpf_ringbuf_reserve(&ringbuf, sizeof(*sample), 0);
	if (!sample)
		return 0;

	sample->pid = pid;
	sample->value = value;
	bpf_get_current_comm(sample->comm, sizeof(sample->comm));

	bpf_ringbuf_submit(sample, 0);

	return 0;
}
