// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include "bpf_tracing_net.h"

char _license[] SEC("license") = "GPL";

__s32 page_size = 0;

const char cc_reno[TCP_CA_NAME_MAX] = "reno";
const char cc_cubic[TCP_CA_NAME_MAX] = "cubic";

SEC("cgroup/setsockopt")
int sockopt_qos_to_cc(struct bpf_sockopt *ctx)
{
	void *optval_end = ctx->optval_end;
	int *optval = ctx->optval;
	char buf[TCP_CA_NAME_MAX];

	if (ctx->level != SOL_IPV6 || ctx->optname != IPV6_TCLASS)
		goto out;

	if (optval + 1 > optval_end)
		return 0; /* EPERM, bounds check */

	if (bpf_getsockopt(ctx->sk, SOL_TCP, TCP_CONGESTION, &buf, sizeof(buf)))
		return 0;

	if (bpf_strncmp(buf, sizeof(buf), cc_cubic))
		return 0;

	if (*optval == 0x2d) {
		if (bpf_setsockopt(ctx->sk, SOL_TCP, TCP_CONGESTION, (void *)&cc_reno,
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
