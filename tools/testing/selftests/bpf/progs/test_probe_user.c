// SPDX-License-Identifier: GPL-2.0

#include <linux/ptrace.h>
#include <linux/bpf.h>

#include <netinet/in.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_trace_helpers.h"

static struct sockaddr_in old;

SEC("kprobe/__sys_connect")
int BPF_KPROBE(handle_sys_connect)
{
	void *ptr = (void *)PT_REGS_PARM2(ctx);
	struct sockaddr_in new;

	bpf_probe_read_user(&old, sizeof(old), ptr);
	__builtin_memset(&new, 0xab, sizeof(new));
	bpf_probe_write_user(ptr, &new, sizeof(new));

	return 0;
}

char _license[] SEC("license") = "GPL";
