// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <linux/bpf.h>
#include "bpf_helpers.h"

char _license[] SEC("license") = "GPL";

struct test1 {
	ks32 a;
	ks32 ret;
};
static volatile __u64 test1_result;
SEC("fexit/bpf_fentry_test1")
int test1(struct test1 *ctx)
{
	test1_result = ctx->a == 1 && ctx->ret == 2;
	return 0;
}

struct test2 {
	ks32 a;
	ku64 b;
	ks32 ret;
};
static volatile __u64 test2_result;
SEC("fexit/bpf_fentry_test2")
int test2(struct test2 *ctx)
{
	test2_result = ctx->a == 2 && ctx->b == 3 && ctx->ret == 5;
	return 0;
}

struct test3 {
	ks8 a;
	ks32 b;
	ku64 c;
	ks32 ret;
};
static volatile __u64 test3_result;
SEC("fexit/bpf_fentry_test3")
int test3(struct test3 *ctx)
{
	test3_result = ctx->a == 4 && ctx->b == 5 && ctx->c == 6 &&
		ctx->ret == 15;
	return 0;
}

struct test4 {
	void *a;
	ks8 b;
	ks32 c;
	ku64 d;
	ks32 ret;
};
static volatile __u64 test4_result;
SEC("fexit/bpf_fentry_test4")
int test4(struct test4 *ctx)
{
	test4_result = ctx->a == (void *)7 && ctx->b == 8 && ctx->c == 9 &&
		ctx->d == 10 && ctx->ret == 34;
	return 0;
}

struct test5 {
	ku64 a;
	void *b;
	ks16 c;
	ks32 d;
	ku64 e;
	ks32 ret;
};
static volatile __u64 test5_result;
SEC("fexit/bpf_fentry_test5")
int test5(struct test5 *ctx)
{
	test5_result = ctx->a == 11 && ctx->b == (void *)12 && ctx->c == 13 &&
		ctx->d == 14 && ctx->e == 15 && ctx->ret == 65;
	return 0;
}

struct test6 {
	ku64 a;
	void *b;
	ks16 c;
	ks32 d;
	void *e;
	ks64 f;
	ks32 ret;
};
static volatile __u64 test6_result;
SEC("fexit/bpf_fentry_test6")
int test6(struct test6 *ctx)
{
	test6_result = ctx->a == 16 && ctx->b == (void *)17 && ctx->c == 18 &&
		ctx->d == 19 && ctx->e == (void *)20 && ctx->f == 21 &&
		ctx->ret == 111;
	return 0;
}
