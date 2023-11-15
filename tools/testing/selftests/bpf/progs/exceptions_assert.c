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
	int check_assert_##op##_##name(void *ctx)			\
	{								\
		type num = bpf_ktime_get_ns();				\
		bpf_assert_##op(num, value);				\
		return *(u64 *)num;					\
	}

__msg(": R0_w=-2147483648 R10=fp0")
check_assert(s64, eq, int_min, INT_MIN);
__msg(": R0_w=2147483647 R10=fp0")
check_assert(s64, eq, int_max, INT_MAX);
__msg(": R0_w=0 R10=fp0")
check_assert(s64, eq, zero, 0);
__msg(": R0_w=-9223372036854775808 R1_w=-9223372036854775808 R10=fp0")
check_assert(s64, eq, llong_min, LLONG_MIN);
__msg(": R0_w=9223372036854775807 R1_w=9223372036854775807 R10=fp0")
check_assert(s64, eq, llong_max, LLONG_MAX);

__msg(": R0_w=scalar(smax=2147483646) R10=fp0")
check_assert(s64, lt, pos, INT_MAX);
__msg(": R0_w=scalar(smax=-1,umin=9223372036854775808,var_off=(0x8000000000000000; 0x7fffffffffffffff))")
check_assert(s64, lt, zero, 0);
__msg(": R0_w=scalar(smax=-2147483649,umin=9223372036854775808,umax=18446744071562067967,var_off=(0x8000000000000000; 0x7fffffffffffffff))")
check_assert(s64, lt, neg, INT_MIN);

__msg(": R0_w=scalar(smax=2147483647) R10=fp0")
check_assert(s64, le, pos, INT_MAX);
__msg(": R0_w=scalar(smax=0) R10=fp0")
check_assert(s64, le, zero, 0);
__msg(": R0_w=scalar(smax=-2147483648,umin=9223372036854775808,umax=18446744071562067968,var_off=(0x8000000000000000; 0x7fffffffffffffff))")
check_assert(s64, le, neg, INT_MIN);

__msg(": R0_w=scalar(smin=umin=2147483648,umax=9223372036854775807,var_off=(0x0; 0x7fffffffffffffff))")
check_assert(s64, gt, pos, INT_MAX);
__msg(": R0_w=scalar(smin=umin=1,umax=9223372036854775807,var_off=(0x0; 0x7fffffffffffffff))")
check_assert(s64, gt, zero, 0);
__msg(": R0_w=scalar(smin=-2147483647) R10=fp0")
check_assert(s64, gt, neg, INT_MIN);

__msg(": R0_w=scalar(smin=umin=2147483647,umax=9223372036854775807,var_off=(0x0; 0x7fffffffffffffff))")
check_assert(s64, ge, pos, INT_MAX);
__msg(": R0_w=scalar(smin=0,umax=9223372036854775807,var_off=(0x0; 0x7fffffffffffffff)) R10=fp0")
check_assert(s64, ge, zero, 0);
__msg(": R0_w=scalar(smin=-2147483648) R10=fp0")
check_assert(s64, ge, neg, INT_MIN);

SEC("?tc")
__log_level(2) __failure
__msg(": R0=0 R1=ctx(off=0,imm=0) R2=scalar(smin=smin32=-2147483646,smax=smax32=2147483645) R10=fp0")
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
__msg(": R1=ctx(off=0,imm=0) R2=scalar(smin=umin=smin32=umin32=4096,smax=umax=smax32=umax32=8192,var_off=(0x0; 0x3fff))")
int check_assert_range_u64(struct __sk_buff *ctx)
{
	u64 num = ctx->len;

	bpf_assert_range(num, 4096, 8192);
	return *((u8 *)ctx + num);
}

SEC("?tc")
__log_level(2) __failure
__msg(": R0=0 R1=ctx(off=0,imm=0) R2=4096 R10=fp0")
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
__msg(": R1=ctx(off=0,imm=0) R2=4096 R10=fp0")
int check_assert_single_range_u64(struct __sk_buff *ctx)
{
	u64 num = ctx->len;

	bpf_assert_range(num, 4096, 4096);
	return *((u8 *)ctx + num);
}

SEC("?tc")
__log_level(2) __failure
__msg(": R1=pkt(off=64,r=64,imm=0) R2=pkt_end(off=0,imm=0) R6=pkt(off=0,r=64,imm=0) R10=fp0")
int check_assert_generic(struct __sk_buff *ctx)
{
	u8 *data_end = (void *)(long)ctx->data_end;
	u8 *data = (void *)(long)ctx->data;

	bpf_assert(data + 64 <= data_end);
	return data[128];
}

SEC("?fentry/bpf_check")
__failure __msg("At program exit the register R0 has value (0x40; 0x0)")
int check_assert_with_return(void *ctx)
{
	bpf_assert_with(!ctx, 64);
	return 0;
}

char _license[] SEC("license") = "GPL";
