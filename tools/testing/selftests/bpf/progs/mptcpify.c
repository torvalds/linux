// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023, SUSE. */

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>
#include "bpf_tracing_net.h"

char _license[] SEC("license") = "GPL";

SEC("fmod_ret/update_socket_protocol")
int BPF_PROG(mptcpify, int family, int type, int protocol)
{
	if ((family == AF_INET || family == AF_INET6) &&
	    type == SOCK_STREAM &&
	    (!protocol || protocol == IPPROTO_TCP)) {
		return IPPROTO_MPTCP;
	}

	return protocol;
}
