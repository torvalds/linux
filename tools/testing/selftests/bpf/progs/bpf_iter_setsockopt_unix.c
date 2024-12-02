// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates. */
#include <vmlinux.h>
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <limits.h>

#define AUTOBIND_LEN 6
char sun_path[AUTOBIND_LEN];

#define NR_CASES 5
int sndbuf_setsockopt[NR_CASES] = {-1, 0, 8192, INT_MAX / 2, INT_MAX};
int sndbuf_getsockopt[NR_CASES] = {-1, -1, -1, -1, -1};
int sndbuf_getsockopt_expected[NR_CASES];

static inline int cmpname(struct unix_sock *unix_sk)
{
	int i;

	for (i = 0; i < AUTOBIND_LEN; i++) {
		if (unix_sk->addr->name->sun_path[i] != sun_path[i])
			return -1;
	}

	return 0;
}

SEC("iter/unix")
int change_sndbuf(struct bpf_iter__unix *ctx)
{
	struct unix_sock *unix_sk = ctx->unix_sk;
	int i, err;

	if (!unix_sk || !unix_sk->addr)
		return 0;

	if (unix_sk->addr->name->sun_path[0])
		return 0;

	if (cmpname(unix_sk))
		return 0;

	for (i = 0; i < NR_CASES; i++) {
		err = bpf_setsockopt(unix_sk, SOL_SOCKET, SO_SNDBUF,
				     &sndbuf_setsockopt[i],
				     sizeof(sndbuf_setsockopt[i]));
		if (err)
			break;

		err = bpf_getsockopt(unix_sk, SOL_SOCKET, SO_SNDBUF,
				     &sndbuf_getsockopt[i],
				     sizeof(sndbuf_getsockopt[i]));
		if (err)
			break;
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
