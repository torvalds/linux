// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include "bpf_tracing_net.h"
#include "ksock_common.h"

char sendto_data[] = "hello from bpf sendto";
char send_data[] = "hello from bpf send";

__be16 connected_port;
__be16 sendto_port;
bool connected;
bool use_sendto_addr;
int sendto_ret = -1;
int send_ret = -1;

SEC("syscall")
int test_sendto(void *ctx)
{
	struct bpf_ksock_create_opts opts = {
		.family = AF_INET,
		.type = SOCK_DGRAM,
		.protocol = IPPROTO_UDP,
	};
	union bpf_ksock_addr addr = {};
	struct bpf_ksock *ks;
	int err = 0;

	ks = bpf_ksock_create(&opts, sizeof(opts), &err);
	if (!ks)
		return err;

	addr.sin.sin_family = AF_INET;
	addr.sin.sin_addr.s_addr = bpf_htonl(0x7f000001);
	if (connected) {
		addr.sin.sin_port = connected_port;
		err = bpf_ksock_connect(ks, &addr, sizeof(addr));
		if (err)
			goto out;
	}

	addr.sin.sin_port = sendto_port;
	if (use_sendto_addr)
		sendto_ret = bpf_ksock_sendto(ks, sendto_data, sizeof(sendto_data),
					      &addr, sizeof(addr));
	else
		sendto_ret = bpf_ksock_sendto(ks, sendto_data, sizeof(sendto_data), NULL, 0);

	/* sendto() must neither connect the socket nor change its default peer. */
	send_ret = bpf_ksock_send(ks, send_data, sizeof(send_data));
out:
	bpf_ksock_release(ks);
	return err;
}

char __license[] SEC("license") = "GPL";
