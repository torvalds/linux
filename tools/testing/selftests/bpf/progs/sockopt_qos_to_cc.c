// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <string.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_tcp_helpers.h"

char _license[] SEC("license") = "GPL";

__u32 page_size = 0;

SEC("cgroup/setsockopt")
int sockopt_qos_to_cc(struct bpf_sockopt *ctx)
{
	void *optval_end = ctx->optval_end;
	int *optval = ctx->optval;
	char buf[TCP_CA_NAME_MAX];
	char cc_reno[TCP_CA_NAME_MAX] = "reno";
	char cc_cubic[TCP_CA_NAME_MAX] = "cubic";

	if (ctx->level != SOL_IPV6 || ctx->optname != IPV6_TCLASS)
		goto out;

	if (optval + 1 > optval_end)
		return 0; /* EPERM, bounds check */

	if (bpf_getsockopt(ctx->sk, SOL_TCP, TCP_CONGESTION, &buf, sizeof(buf)))
		return 0;

	if (!tcp_cc_eq(buf, cc_cubic))
		return 0;

	if (*optval == 0x2d) {
		if (bpf_setsockopt(ctx->sk, SOL_TCP, TCP_CONGESTION, &cc_reno,
				sizeof(cc_reno)))
			return 0;
	}
	return 1;

out:
	/* optval larger than PAGE_SIZE use kernel's buffer. */
	if (ctx->optlen > page_size)
		ctx->optlen = 0;
	return 1;
}
