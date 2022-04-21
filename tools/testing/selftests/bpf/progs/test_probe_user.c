// SPDX-License-Identifier: GPL-2.0

#include <linux/ptrace.h>
#include <linux/bpf.h>

#include <netinet/in.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

static struct sockaddr_in old;

SEC("kprobe/" SYS_PREFIX "sys_connect")
int BPF_KPROBE(handle_sys_connect)
{
#if SYSCALL_WRAPPER == 1
	struct pt_regs *real_regs;
#endif
	struct sockaddr_in new;
	void *ptr;

#if SYSCALL_WRAPPER == 0
	ptr = (void *)PT_REGS_PARM2(ctx);
#else
	real_regs = (struct pt_regs *)PT_REGS_PARM1(ctx);
	bpf_probe_read_kernel(&ptr, sizeof(ptr), &PT_REGS_PARM2(real_regs));
#endif

	bpf_probe_read_user(&old, sizeof(old), ptr);
	__builtin_memset(&new, 0xab, sizeof(new));
	bpf_probe_write_user(ptr, &new, sizeof(new));

	return 0;
}

char _license[] SEC("license") = "GPL";
