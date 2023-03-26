// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/helper_restricted.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct val {
	int cnt;
	struct bpf_spin_lock l;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct val);
} map_spin_lock SEC(".maps");

struct timer {
	struct bpf_timer t;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct timer);
} map_timer SEC(".maps");

SEC("kprobe")
__description("bpf_ktime_get_coarse_ns is forbidden in BPF_PROG_TYPE_KPROBE")
__failure __msg("unknown func bpf_ktime_get_coarse_ns")
__naked void in_bpf_prog_type_kprobe_1(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_coarse_ns];		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_coarse_ns)
	: __clobber_all);
}

SEC("tracepoint")
__description("bpf_ktime_get_coarse_ns is forbidden in BPF_PROG_TYPE_TRACEPOINT")
__failure __msg("unknown func bpf_ktime_get_coarse_ns")
__naked void in_bpf_prog_type_tracepoint_1(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_coarse_ns];		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_coarse_ns)
	: __clobber_all);
}

SEC("perf_event")
__description("bpf_ktime_get_coarse_ns is forbidden in BPF_PROG_TYPE_PERF_EVENT")
__failure __msg("unknown func bpf_ktime_get_coarse_ns")
__naked void bpf_prog_type_perf_event_1(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_coarse_ns];		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_coarse_ns)
	: __clobber_all);
}

SEC("raw_tracepoint")
__description("bpf_ktime_get_coarse_ns is forbidden in BPF_PROG_TYPE_RAW_TRACEPOINT")
__failure __msg("unknown func bpf_ktime_get_coarse_ns")
__naked void bpf_prog_type_raw_tracepoint_1(void)
{
	asm volatile ("					\
	call %[bpf_ktime_get_coarse_ns];		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_coarse_ns)
	: __clobber_all);
}

SEC("kprobe")
__description("bpf_timer_init isn restricted in BPF_PROG_TYPE_KPROBE")
__failure __msg("tracing progs cannot use bpf_timer yet")
__naked void in_bpf_prog_type_kprobe_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_timer] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = %[map_timer] ll;				\
	r3 = 1;						\
l0_%=:	call %[bpf_timer_init];				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_timer_init),
	  __imm_addr(map_timer)
	: __clobber_all);
}

SEC("perf_event")
__description("bpf_timer_init is forbidden in BPF_PROG_TYPE_PERF_EVENT")
__failure __msg("tracing progs cannot use bpf_timer yet")
__naked void bpf_prog_type_perf_event_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_timer] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = %[map_timer] ll;				\
	r3 = 1;						\
l0_%=:	call %[bpf_timer_init];				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_timer_init),
	  __imm_addr(map_timer)
	: __clobber_all);
}

SEC("tracepoint")
__description("bpf_timer_init is forbidden in BPF_PROG_TYPE_TRACEPOINT")
__failure __msg("tracing progs cannot use bpf_timer yet")
__naked void in_bpf_prog_type_tracepoint_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_timer] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = %[map_timer] ll;				\
	r3 = 1;						\
l0_%=:	call %[bpf_timer_init];				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_timer_init),
	  __imm_addr(map_timer)
	: __clobber_all);
}

SEC("raw_tracepoint")
__description("bpf_timer_init is forbidden in BPF_PROG_TYPE_RAW_TRACEPOINT")
__failure __msg("tracing progs cannot use bpf_timer yet")
__naked void bpf_prog_type_raw_tracepoint_2(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_timer] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	r2 = %[map_timer] ll;				\
	r3 = 1;						\
l0_%=:	call %[bpf_timer_init];				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_timer_init),
	  __imm_addr(map_timer)
	: __clobber_all);
}

SEC("kprobe")
__description("bpf_spin_lock is forbidden in BPF_PROG_TYPE_KPROBE")
__failure __msg("tracing progs cannot use bpf_spin_lock yet")
__naked void in_bpf_prog_type_kprobe_3(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_spin_lock] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	call %[bpf_spin_lock];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_spin_lock),
	  __imm_addr(map_spin_lock)
	: __clobber_all);
}

SEC("tracepoint")
__description("bpf_spin_lock is forbidden in BPF_PROG_TYPE_TRACEPOINT")
__failure __msg("tracing progs cannot use bpf_spin_lock yet")
__naked void in_bpf_prog_type_tracepoint_3(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_spin_lock] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	call %[bpf_spin_lock];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_spin_lock),
	  __imm_addr(map_spin_lock)
	: __clobber_all);
}

SEC("perf_event")
__description("bpf_spin_lock is forbidden in BPF_PROG_TYPE_PERF_EVENT")
__failure __msg("tracing progs cannot use bpf_spin_lock yet")
__naked void bpf_prog_type_perf_event_3(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_spin_lock] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	call %[bpf_spin_lock];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_spin_lock),
	  __imm_addr(map_spin_lock)
	: __clobber_all);
}

SEC("raw_tracepoint")
__description("bpf_spin_lock is forbidden in BPF_PROG_TYPE_RAW_TRACEPOINT")
__failure __msg("tracing progs cannot use bpf_spin_lock yet")
__naked void bpf_prog_type_raw_tracepoint_3(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_spin_lock] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = r0;					\
	call %[bpf_spin_lock];				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_spin_lock),
	  __imm_addr(map_spin_lock)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
