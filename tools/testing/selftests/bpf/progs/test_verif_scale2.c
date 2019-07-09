// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include "bpf_helpers.h"
#define ATTR __attribute__((always_inline))
#include "test_jhash.h"

SEC("scale90_inline")
int balancer_ingress(struct __sk_buff *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	void *ptr;
	int ret = 0, nh_off, i = 0;

	nh_off = 14;

	/* pragma unroll doesn't work on large loops */

#define C do { \
	ptr = data + i; \
	if (ptr + nh_off > data_end) \
		break; \
	ctx->tc_index = jhash(ptr, nh_off, ctx->cb[0] + i++); \
	} while (0);
#define C30 C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;C;
	C30;C30;C30; /* 90 calls */
	return 0;
}
char _license[] SEC("license") = "GPL";
