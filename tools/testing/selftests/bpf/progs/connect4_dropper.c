// SPDX-License-Identifier: GPL-2.0

#include <string.h>

#include <linux/stddef.h>
#include <linux/bpf.h>

#include <sys/socket.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define VERDICT_REJECT	0
#define VERDICT_PROCEED	1

SEC("cgroup/connect4")
int connect_v4_dropper(struct bpf_sock_addr *ctx)
{
	if (ctx->type != SOCK_STREAM)
		return VERDICT_PROCEED;
	if (ctx->user_port == bpf_htons(60120))
		return VERDICT_REJECT;
	return VERDICT_PROCEED;
}

char _license[] SEC("license") = "GPL";
