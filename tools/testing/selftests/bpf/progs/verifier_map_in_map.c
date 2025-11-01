// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/map_in_map.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
	__array(values, struct {
		__uint(type, BPF_MAP_TYPE_ARRAY);
		__uint(max_entries, 1);
		__type(key, int);
		__type(value, int);
	});
} map_in_map SEC(".maps");

SEC("socket")
__description("map in map access")
__success __success_unpriv __retval(0)
__naked void map_in_map_access(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_in_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = r0;					\
	call %[bpf_map_lookup_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_in_map)
	: __clobber_all);
}

SEC("xdp")
__description("map in map state pruning")
__success __msg("processed 15 insns")
__log_level(2) __retval(0) __flag(BPF_F_TEST_STATE_FREQ)
__naked void map_in_map_state_pruning(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r6 = r10;					\
	r6 += -4;					\
	r2 = r6;					\
	r1 = %[map_in_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r2 = r6;					\
	r1 = r0;					\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l1_%=;				\
	r2 = r6;					\
	r1 = %[map_in_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l2_%=;				\
	exit;						\
l2_%=:	r2 = r6;					\
	r1 = r0;					\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l1_%=;				\
	exit;						\
l1_%=:	r0 = *(u32*)(r0 + 0);				\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_in_map)
	: __clobber_all);
}

SEC("socket")
__description("invalid inner map pointer")
__failure __msg("R1 pointer arithmetic on map_ptr prohibited")
__failure_unpriv
__naked void invalid_inner_map_pointer(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_in_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = r0;					\
	r1 += 8;					\
	call %[bpf_map_lookup_elem];			\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_in_map)
	: __clobber_all);
}

SEC("socket")
__description("forgot null checking on the inner map pointer")
__failure __msg("R1 type=map_value_or_null expected=map_ptr")
__failure_unpriv
__naked void on_the_inner_map_pointer(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_in_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = r0;					\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_in_map)
	: __clobber_all);
}

SEC("socket")
__description("map_ptr is never null")
__success
__naked void map_ptr_is_never_null(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r1 = %[map_in_map] ll;				\
	if r1 != 0 goto l0_%=;				\
	r10 = 42;					\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_in_map)
	: __clobber_all);
}

SEC("socket")
__description("map_ptr is never null inner")
__success
__naked void map_ptr_is_never_null_inner(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_in_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	if r0 != 0 goto l0_%=;				\
	r10 = 42;					\
l0_%=:  exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_in_map)
	: __clobber_all);
}

SEC("socket")
__description("map_ptr is never null inner spill fill")
__success
__naked void map_ptr_is_never_null_inner_spill_fill(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u32*)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[map_in_map] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	*(u64 *)(r10 -16) = r0;				\
	r1 = *(u64 *)(r10 -16);				\
	if r1 == 0 goto l1_%=;				\
	exit;						\
l1_%=:	r10 = 42;					\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_in_map)
	: __clobber_all);
}

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
	__array(values, struct {
		__uint(type, BPF_MAP_TYPE_RINGBUF);
		__uint(max_entries, 64 * 1024);
	});
} rb_in_map SEC(".maps");

struct rb_ctx {
	void *rb;
	struct bpf_dynptr dptr;
};

static __always_inline struct rb_ctx __rb_event_reserve(__u32 sz)
{
	struct rb_ctx rb_ctx = {};
	void *rb;
	__u32 cpu = bpf_get_smp_processor_id();
	__u32 rb_slot = cpu & 1;

	rb = bpf_map_lookup_elem(&rb_in_map, &rb_slot);
	if (!rb)
		return rb_ctx;

	rb_ctx.rb = rb;
	bpf_ringbuf_reserve_dynptr(rb, sz, 0, &rb_ctx.dptr);

	return rb_ctx;
}

static __noinline void __rb_event_submit(struct rb_ctx *ctx)
{
	if (!ctx->rb)
		return;

	/* If the verifier (incorrectly) concludes that ctx->rb can be
	 * NULL at this point, we'll get "BPF_EXIT instruction in main
	 * prog would lead to reference leak" error
	 */
	bpf_ringbuf_submit_dynptr(&ctx->dptr, 0);
}

SEC("socket")
int map_ptr_is_never_null_rb(void *ctx)
{
	struct rb_ctx event_ctx = __rb_event_reserve(256);
	__rb_event_submit(&event_ctx);
	return 0;
}

char _license[] SEC("license") = "GPL";
