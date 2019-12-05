// SPDX-License-Identifier: GPL-2.0

#include <linux/ptrace.h>
#include <linux/bpf.h>

#include <netinet/in.h>

#include "bpf_helpers.h"
#include "bpf_tracing.h"

static struct sockaddr_in old;

SEC("kprobe/__sys_connect")
int handle_sys_connect(struct pt_regs *ctx)
{
	void *ptr = (void *)PT_REGS_PARM2(ctx);
	struct sockaddr_in new;

	bpf_probe_read_user(&old, sizeof(old), ptr);
	__builtin_memset(&new, 0xab, sizeof(new));
	bpf_probe_write_user(ptr, &new, sizeof(new));

	return 0;
}

char _license[] SEC("license") = "GPL";
