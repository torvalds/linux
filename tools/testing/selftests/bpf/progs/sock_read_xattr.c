// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Christian Brauner */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

char value[16];
int read_ret = -1;
__u32 monitored_pid = 0;

static __always_inline void read_xattr(struct socket *sock)
{
	struct bpf_dynptr value_ptr;

	bpf_dynptr_from_mem(value, sizeof(value), 0, &value_ptr);
	bpf_sock_read_xattr(sock, "user.bpf_test", &value_ptr);
}

SEC("lsm.s/socket_connect")
__success
int BPF_PROG(trusted_sock_ptr_sleepable, struct socket *sock)
{
	read_xattr(sock);
	return 0;
}

SEC("lsm/socket_connect")
__success
int BPF_PROG(trusted_sock_ptr_non_sleepable, struct socket *sock)
{
	read_xattr(sock);
	return 0;
}

SEC("lsm.s/socket_connect")
__success
int BPF_PROG(read_sock_xattr, struct socket *sock)
{
	struct bpf_dynptr value_ptr;
	__u32 pid = bpf_get_current_pid_tgid() >> 32;

	if (pid != monitored_pid)
		return 0;

	bpf_dynptr_from_mem(value, sizeof(value), 0, &value_ptr);
	read_ret = bpf_sock_read_xattr(sock, "user.bpf_test", &value_ptr);
	return 0;
}
