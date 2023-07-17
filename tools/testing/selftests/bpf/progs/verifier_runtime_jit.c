// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/runtime_jit.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

void dummy_prog_42_socket(void);
void dummy_prog_24_socket(void);
void dummy_prog_loop1_socket(void);
void dummy_prog_loop2_socket(void);

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 4);
	__uint(key_size, sizeof(int));
	__array(values, void (void));
} map_prog1_socket SEC(".maps") = {
	.values = {
		[0] = (void *)&dummy_prog_42_socket,
		[1] = (void *)&dummy_prog_loop1_socket,
		[2] = (void *)&dummy_prog_24_socket,
	},
};

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 8);
	__uint(key_size, sizeof(int));
	__array(values, void (void));
} map_prog2_socket SEC(".maps") = {
	.values = {
		[1] = (void *)&dummy_prog_loop2_socket,
		[2] = (void *)&dummy_prog_24_socket,
		[7] = (void *)&dummy_prog_42_socket,
	},
};

SEC("socket")
__auxiliary __auxiliary_unpriv
__naked void dummy_prog_42_socket(void)
{
	asm volatile ("r0 = 42; exit;");
}

SEC("socket")
__auxiliary __auxiliary_unpriv
__naked void dummy_prog_24_socket(void)
{
	asm volatile ("r0 = 24; exit;");
}

SEC("socket")
__auxiliary __auxiliary_unpriv
__naked void dummy_prog_loop1_socket(void)
{
	asm volatile ("			\
	r3 = 1;				\
	r2 = %[map_prog1_socket] ll;	\
	call %[bpf_tail_call];		\
	r0 = 41;			\
	exit;				\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

SEC("socket")
__auxiliary __auxiliary_unpriv
__naked void dummy_prog_loop2_socket(void)
{
	asm volatile ("			\
	r3 = 1;				\
	r2 = %[map_prog2_socket] ll;	\
	call %[bpf_tail_call];		\
	r0 = 41;			\
	exit;				\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog2_socket)
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, prog once")
__success __success_unpriv __retval(42)
__naked void call_within_bounds_prog_once(void)
{
	asm volatile ("					\
	r3 = 0;						\
	r2 = %[map_prog1_socket] ll;			\
	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, prog loop")
__success __success_unpriv __retval(41)
__naked void call_within_bounds_prog_loop(void)
{
	asm volatile ("					\
	r3 = 1;						\
	r2 = %[map_prog1_socket] ll;			\
	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, no prog")
__success __success_unpriv __retval(1)
__naked void call_within_bounds_no_prog(void)
{
	asm volatile ("					\
	r3 = 3;						\
	r2 = %[map_prog1_socket] ll;			\
	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, key 2")
__success __success_unpriv __retval(24)
__naked void call_within_bounds_key_2(void)
{
	asm volatile ("					\
	r3 = 2;						\
	r2 = %[map_prog1_socket] ll;			\
	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, key 2 / key 2, first branch")
__success __success_unpriv __retval(24)
__naked void _2_key_2_first_branch(void)
{
	asm volatile ("					\
	r0 = 13;					\
	*(u8*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	r0 = *(u8*)(r1 + %[__sk_buff_cb_0]);		\
	if r0 == 13 goto l0_%=;				\
	r3 = 2;						\
	r2 = %[map_prog1_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r3 = 2;						\
	r2 = %[map_prog1_socket] ll;			\
l1_%=:	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, key 2 / key 2, second branch")
__success __success_unpriv __retval(24)
__naked void _2_key_2_second_branch(void)
{
	asm volatile ("					\
	r0 = 14;					\
	*(u8*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	r0 = *(u8*)(r1 + %[__sk_buff_cb_0]);		\
	if r0 == 13 goto l0_%=;				\
	r3 = 2;						\
	r2 = %[map_prog1_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r3 = 2;						\
	r2 = %[map_prog1_socket] ll;			\
l1_%=:	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, key 0 / key 2, first branch")
__success __success_unpriv __retval(24)
__naked void _0_key_2_first_branch(void)
{
	asm volatile ("					\
	r0 = 13;					\
	*(u8*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	r0 = *(u8*)(r1 + %[__sk_buff_cb_0]);		\
	if r0 == 13 goto l0_%=;				\
	r3 = 0;						\
	r2 = %[map_prog1_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r3 = 2;						\
	r2 = %[map_prog1_socket] ll;			\
l1_%=:	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, key 0 / key 2, second branch")
__success __success_unpriv __retval(42)
__naked void _0_key_2_second_branch(void)
{
	asm volatile ("					\
	r0 = 14;					\
	*(u8*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	r0 = *(u8*)(r1 + %[__sk_buff_cb_0]);		\
	if r0 == 13 goto l0_%=;				\
	r3 = 0;						\
	r2 = %[map_prog1_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r3 = 2;						\
	r2 = %[map_prog1_socket] ll;			\
l1_%=:	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, different maps, first branch")
__success __failure_unpriv __msg_unpriv("tail_call abusing map_ptr")
__retval(1)
__naked void bounds_different_maps_first_branch(void)
{
	asm volatile ("					\
	r0 = 13;					\
	*(u8*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	r0 = *(u8*)(r1 + %[__sk_buff_cb_0]);		\
	if r0 == 13 goto l0_%=;				\
	r3 = 0;						\
	r2 = %[map_prog1_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r3 = 0;						\
	r2 = %[map_prog2_socket] ll;			\
l1_%=:	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket),
	  __imm_addr(map_prog2_socket),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call within bounds, different maps, second branch")
__success __failure_unpriv __msg_unpriv("tail_call abusing map_ptr")
__retval(42)
__naked void bounds_different_maps_second_branch(void)
{
	asm volatile ("					\
	r0 = 14;					\
	*(u8*)(r1 + %[__sk_buff_cb_0]) = r0;		\
	r0 = *(u8*)(r1 + %[__sk_buff_cb_0]);		\
	if r0 == 13 goto l0_%=;				\
	r3 = 0;						\
	r2 = %[map_prog1_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r3 = 0;						\
	r2 = %[map_prog2_socket] ll;			\
l1_%=:	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket),
	  __imm_addr(map_prog2_socket),
	  __imm_const(__sk_buff_cb_0, offsetof(struct __sk_buff, cb[0]))
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: tail_call out of bounds")
__success __success_unpriv __retval(2)
__naked void tail_call_out_of_bounds(void)
{
	asm volatile ("					\
	r3 = 256;					\
	r2 = %[map_prog1_socket] ll;			\
	call %[bpf_tail_call];				\
	r0 = 2;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: pass negative index to tail_call")
__success __success_unpriv __retval(2)
__naked void negative_index_to_tail_call(void)
{
	asm volatile ("					\
	r3 = -1;					\
	r2 = %[map_prog1_socket] ll;			\
	call %[bpf_tail_call];				\
	r0 = 2;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

SEC("socket")
__description("runtime/jit: pass > 32bit index to tail_call")
__success __success_unpriv __retval(42)
/* Verifier rewrite for unpriv skips tail call here. */
__retval_unpriv(2)
__naked void _32bit_index_to_tail_call(void)
{
	asm volatile ("					\
	r3 = 0x100000000 ll;				\
	r2 = %[map_prog1_socket] ll;			\
	call %[bpf_tail_call];				\
	r0 = 2;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
