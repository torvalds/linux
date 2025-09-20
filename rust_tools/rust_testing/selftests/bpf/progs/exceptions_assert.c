// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <limits.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

#define check_assert(type, op, name, value)				\
	SEC("?tc")							\
	__log_level(2) __failure					\
	int check_assert_##name(void *ctx)				\
	{								\
		type num = bpf_ktime_get_ns();				\
		bpf_assert(bpf_cmp_unlikely(num, op, value));		\
		return *(u64 *)num;					\
	}

__msg(": R0_w=0xffffffff80000000")
check_assert(s64, ==, eq_int_min, INT_MIN);
__msg(": R0_w=0x7fffffff")
check_assert(s64, ==, eq_int_max, INT_MAX);
__msg(": R0_w=0")
check_assert(s64, ==, eq_zero, 0);
__msg(": R0_w=0x8000000000000000 R1_w=0x8000000000000000")
check_assert(s64, ==, eq_llong_min, LLONG_MIN);
__msg(": R0_w=0x7fffffffffffffff R1_w=0x7fffffffffffffff")
check_assert(s64, ==, eq_llong_max, LLONG_MAX);

__msg(": R0_w=scalar(id=1,smax=0x7ffffffe)")
check_assert(s64, <, lt_pos, INT_MAX);
__msg(": R0_w=scalar(id=1,smax=-1,umin=0x8000000000000000,var_off=(0x8000000000000000; 0x7fffffffffffffff))")
check_assert(s64, <, lt_zero, 0);
__msg(": R0_w=scalar(id=1,smax=0xffffffff7fffffff")
check_assert(s64, <, lt_neg, INT_MIN);

__msg(": R0_w=scalar(id=1,smax=0x7fffffff)")
check_assert(s64, <=, le_pos, INT_MAX);
__msg(": R0_w=scalar(id=1,smax=0)")
check_assert(s64, <=, le_zero, 0);
__msg(": R0_w=scalar(id=1,smax=0xffffffff80000000")
check_assert(s64, <=, le_neg, INT_MIN);

__msg(": R0_w=scalar(id=1,smin=umin=0x80000000,umax=0x7fffffffffffffff,var_off=(0x0; 0x7fffffffffffffff))")
check_assert(s64, >, gt_pos, INT_MAX);
__msg(": R0_w=scalar(id=1,smin=umin=1,umax=0x7fffffffffffffff,var_off=(0x0; 0x7fffffffffffffff))")
check_assert(s64, >, gt_zero, 0);
__msg(": R0_w=scalar(id=1,smin=0xffffffff80000001")
check_assert(s64, >, gt_neg, INT_MIN);

__msg(": R0_w=scalar(id=1,smin=umin=0x7fffffff,umax=0x7fffffffffffffff,var_off=(0x0; 0x7fffffffffffffff))")
check_assert(s64, >=, ge_pos, INT_MAX);
__msg(": R0_w=scalar(id=1,smin=0,umax=0x7fffffffffffffff,var_off=(0x0; 0x7fffffffffffffff))")
check_assert(s64, >=, ge_zero, 0);
__msg(": R0_w=scalar(id=1,smin=0xffffffff80000000")
check_assert(s64, >=, ge_neg, INT_MIN);

SEC("?tc")
__log_level(2) __failure
__msg(": R0=0 R1=ctx() R2=scalar(smin=0xffffffff80000002,smax=smax32=0x7ffffffd,smin32=0x80000002) R10=fp0")
int check_assert_range_s64(struct __sk_buff *ctx)
{
	struct bpf_sock *sk = ctx->sk;
	s64 num;

	_Static_assert(_Generic((sk->rx_queue_mapping), s32: 1, default: 0), "type match");
	if (!sk)
		return 0;
	num = sk->rx_queue_mapping;
	bpf_assert_range(num, INT_MIN + 2, INT_MAX - 2);
	return *((u8 *)ctx + num);
}

SEC("?tc")
__log_level(2) __failure
__msg(": R1=ctx() R2=scalar(smin=umin=smin32=umin32=4096,smax=umax=smax32=umax32=8192,var_off=(0x0; 0x3fff))")
int check_assert_range_u64(struct __sk_buff *ctx)
{
	u64 num = ctx->len;

	bpf_assert_range(num, 4096, 8192);
	return *((u8 *)ctx + num);
}

SEC("?tc")
__log_level(2) __failure
__msg(": R0=0 R1=ctx() R2=4096 R10=fp0")
int check_assert_single_range_s64(struct __sk_buff *ctx)
{
	struct bpf_sock *sk = ctx->sk;
	s64 num;

	_Static_assert(_Generic((sk->rx_queue_mapping), s32: 1, default: 0), "type match");
	if (!sk)
		return 0;
	num = sk->rx_queue_mapping;

	bpf_assert_range(num, 4096, 4096);
	return *((u8 *)ctx + num);
}

SEC("?tc")
__log_level(2) __failure
__msg(": R1=ctx() R2=4096 R10=fp0")
int check_assert_single_range_u64(struct __sk_buff *ctx)
{
	u64 num = ctx->len;

	bpf_assert_range(num, 4096, 4096);
	return *((u8 *)ctx + num);
}

SEC("?tc")
__log_level(2) __failure
__msg(": R1=pkt(off=64,r=64) R2=pkt_end() R6=pkt(r=64) R10=fp0")
int check_assert_generic(struct __sk_buff *ctx)
{
	u8 *data_end = (void *)(long)ctx->data_end;
	u8 *data = (void *)(long)ctx->data;

	bpf_assert(data + 64 <= data_end);
	return data[128];
}

SEC("?fentry/bpf_check")
__failure __msg("At program exit the register R1 has smin=64 smax=64")
int check_assert_with_return(void *ctx)
{
	bpf_assert_with(!ctx, 64);
	return 0;
}

char _license[] SEC("license") = "GPL";
