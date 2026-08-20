// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"
#include "ksock_common.h"

char send_data[11] = "dummy data";

SEC("lsm.s/socket_sendmsg")
__description("bpf_ksock_send is rejected from socket_sendmsg LSM hook")
__failure __msg("calling kernel function bpf_ksock_send is not allowed")
int BPF_PROG(ksock_socket_sendmsg, struct socket *sock, struct msghdr *msg,
	     int size, int ret)
{
	struct __ksock_ctx_value *v;
	struct bpf_ksock *ks;

	v = ksock_ctx_value_lookup();
	if (!v)
		return ret;

	ks = bpf_kptr_xchg(&v->ctx, NULL);
	if (!ks)
		return ret;

	bpf_ksock_send(ks, send_data, sizeof(send_data));
	bpf_ksock_release(ks);

	return ret;
}

char __license[] SEC("license") = "GPL";
