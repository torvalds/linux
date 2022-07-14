// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

static struct sockaddr_in old;

SEC("ksyscall/connect")
int BPF_KSYSCALL(handle_sys_connect, int fd, struct sockaddr_in *uservaddr, int addrlen)
{
	struct sockaddr_in new;

	bpf_probe_read_user(&old, sizeof(old), uservaddr);
	__builtin_memset(&new, 0xab, sizeof(new));
	bpf_probe_write_user(uservaddr, &new, sizeof(new));

	return 0;
}

char _license[] SEC("license") = "GPL";
