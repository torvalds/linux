// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/map_ptr_mixing.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct test_val);
} map_array_48b SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct test_val);
} map_hash_48b SEC(".maps");

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

SEC("tc")
__description("calls: two calls returning different map pointers for lookup (hash, array)")
__success __retval(1)
__naked void pointers_for_lookup_hash_array(void)
{
	asm volatile ("					\
	/* main prog */					\
	if r1 != 0 goto l0_%=;				\
	call pointers_for_lookup_hash_array__1;		\
	goto l1_%=;					\
l0_%=:	call pointers_for_lookup_hash_array__2;		\
l1_%=:	r1 = r0;					\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
	r0 = 1;						\
l2_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void pointers_for_lookup_hash_array__1(void)
{
	asm volatile ("					\
	r0 = %[map_hash_48b] ll;			\
	exit;						\
"	:
	: __imm_addr(map_hash_48b)
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void pointers_for_lookup_hash_array__2(void)
{
	asm volatile ("					\
	r0 = %[map_array_48b] ll;			\
	exit;						\
"	:
	: __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("tc")
__description("calls: two calls returning different map pointers for lookup (hash, map in map)")
__failure __msg("only read from bpf_array is supported")
__naked void lookup_hash_map_in_map(void)
{
	asm volatile ("					\
	/* main prog */					\
	if r1 != 0 goto l0_%=;				\
	call lookup_hash_map_in_map__1;			\
	goto l1_%=;					\
l0_%=:	call lookup_hash_map_in_map__2;			\
l1_%=:	r1 = r0;					\
	r2 = 0;						\
	*(u64*)(r10 - 8) = r2;				\
	r2 = r10;					\
	r2 += -8;					\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
	r0 = 1;						\
l2_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void lookup_hash_map_in_map__1(void)
{
	asm volatile ("					\
	r0 = %[map_array_48b] ll;			\
	exit;						\
"	:
	: __imm_addr(map_array_48b)
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void lookup_hash_map_in_map__2(void)
{
	asm volatile ("					\
	r0 = %[map_in_map] ll;				\
	exit;						\
"	:
	: __imm_addr(map_in_map)
	: __clobber_all);
}

SEC("socket")
__description("cond: two branches returning different map pointers for lookup (tail, tail)")
__success __failure_unpriv __msg_unpriv("tail_call abusing map_ptr")
__retval(42)
__naked void pointers_for_lookup_tail_tail_1(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	if r6 != 0 goto l0_%=;				\
	r2 = %[map_prog2_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r2 = %[map_prog1_socket] ll;			\
l1_%=:	r3 = 7;						\
	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog1_socket),
	  __imm_addr(map_prog2_socket),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("socket")
__description("cond: two branches returning same map pointers for lookup (tail, tail)")
__success __success_unpriv __retval(42)
__naked void pointers_for_lookup_tail_tail_2(void)
{
	asm volatile ("					\
	r6 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	if r6 == 0 goto l0_%=;				\
	r2 = %[map_prog2_socket] ll;			\
	goto l1_%=;					\
l0_%=:	r2 = %[map_prog2_socket] ll;			\
l1_%=:	r3 = 7;						\
	call %[bpf_tail_call];				\
	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(map_prog2_socket),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
