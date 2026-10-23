// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* global data of the object is in arena */
char data_in_arena SEC(".arena.data");

extern const int bpf_prog_active __ksym;

/* the variable of the kernel is not in arena */
const void *kp = &bpf_prog_active;

SEC("syscall")
int ptr_to_extern(void *ctx)
{
	return *(int *)kp;
}

char _license[] SEC("license") = "GPL";
