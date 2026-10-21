// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char LICENSE[] SEC("license") = "GPL";

void *user_ptr;
int retval;

SEC("fentry.s")
int BPF_PROG(fentry_xdp)
{
	char buff[64];

	retval = bpf_copy_from_user(buff, sizeof(buff), user_ptr);
	return 0;
}
