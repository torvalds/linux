// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/value_ptr_arith.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <errno.h>
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

struct other_val {
	long long foo;
	long long bar;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct other_val);
} map_hash_16b SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct test_val);
} map_hash_48b SEC(".maps");

SEC("socket")
__description("map access: known scalar += value_ptr unknown vs const")
__success __failure_unpriv
__msg_unpriv("R1 tried to add from different maps, paths or scalars")
__retval(1)
__naked void value_ptr_unknown_vs_const(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r4 = *(u8*)(r0 + 0);				\
	if r4 == 1 goto l3_%=;				\
	r1 = 6;						\
	r1 = -r1;					\
	r1 &= 0x7;					\
	goto l4_%=;					\
l3_%=:	r1 = 3;						\
l4_%=:	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr const vs unknown")
__success __failure_unpriv
__msg_unpriv("R1 tried to add from different maps, paths or scalars")
__retval(1)
__naked void value_ptr_const_vs_unknown(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r4 = *(u8*)(r0 + 0);				\
	if r4 == 1 goto l3_%=;				\
	r1 = 3;						\
	goto l4_%=;					\
l3_%=:	r1 = 6;						\
	r1 = -r1;					\
	r1 &= 0x7;					\
l4_%=:	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr const vs const (ne)")
__success __failure_unpriv
__msg_unpriv("R1 tried to add from different maps, paths or scalars")
__retval(1)
__naked void ptr_const_vs_const_ne(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r4 = *(u8*)(r0 + 0);				\
	if r4 == 1 goto l3_%=;				\
	r1 = 3;						\
	goto l4_%=;					\
l3_%=:	r1 = 5;						\
l4_%=:	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr const vs const (eq)")
__success __success_unpriv __retval(1)
__naked void ptr_const_vs_const_eq(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r4 = *(u8*)(r0 + 0);				\
	if r4 == 1 goto l3_%=;				\
	r1 = 5;						\
	goto l4_%=;					\
l3_%=:	r1 = 5;						\
l4_%=:	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr unknown vs unknown (eq)")
__success __success_unpriv __retval(1)
__naked void ptr_unknown_vs_unknown_eq(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r4 = *(u8*)(r0 + 0);				\
	if r4 == 1 goto l3_%=;				\
	r1 = 6;						\
	r1 = -r1;					\
	r1 &= 0x7;					\
	goto l4_%=;					\
l3_%=:	r1 = 6;						\
	r1 = -r1;					\
	r1 &= 0x7;					\
l4_%=:	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr unknown vs unknown (lt)")
__success __failure_unpriv
__msg_unpriv("R1 tried to add from different maps, paths or scalars")
__retval(1)
__naked void ptr_unknown_vs_unknown_lt(void)
{
	asm volatile ("					\
	r8 = r1;					\
	call %[bpf_get_prandom_u32];			\
	r9 = r0;					\
	r1 = r8;					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r4 = *(u8*)(r0 + 0);				\
	if r4 == 1 goto l3_%=;				\
	r1 = 6;						\
	r1 = r9;					\
	r1 &= 0x3;					\
	goto l4_%=;					\
l3_%=:	r1 = 6;						\
	r1 = r9;					\
	r1 &= 0x7;					\
l4_%=:	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len)),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr unknown vs unknown (gt)")
__success __failure_unpriv
__msg_unpriv("R1 tried to add from different maps, paths or scalars")
__retval(1)
__naked void ptr_unknown_vs_unknown_gt(void)
{
	asm volatile ("					\
	r8 = r1;					\
	call %[bpf_get_prandom_u32];			\
	r9 = r0;					\
	r1 = r8;					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r4 = *(u8*)(r0 + 0);				\
	if r4 == 1 goto l3_%=;				\
	r1 = 6;						\
	r1 = r9;					\
	r1 &= 0x7;					\
	goto l4_%=;					\
l3_%=:	r1 = 6;						\
	r1 = r9;					\
	r1 &= 0x3;					\
l4_%=:	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len)),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr from different maps")
__success __success_unpriv __retval(1)
__naked void value_ptr_from_different_maps(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r1 = 4;						\
	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= known scalar from different maps")
__success __failure_unpriv
__msg_unpriv("R0 min value is outside of the allowed memory range")
__retval(1)
__naked void known_scalar_from_different_maps(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_16b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r1 = 4;						\
	r0 -= r1;					\
	r0 += r1;					\
	r0 = *(u8*)(r0 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_16b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr from different maps, but same value properties")
__success __success_unpriv __retval(1)
__naked void maps_but_same_value_properties(void)
{
	asm volatile ("					\
	r0 = *(u32*)(r1 + %[__sk_buff_len]);		\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	if r0 == 1 goto l0_%=;				\
	r1 = %[map_hash_48b] ll;			\
	if r0 != 1 goto l1_%=;				\
l0_%=:	r1 = %[map_array_48b] ll;			\
l1_%=:	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r1 = 4;						\
	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l2_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_addr(map_hash_48b),
	  __imm_const(__sk_buff_len, offsetof(struct __sk_buff, len))
	: __clobber_all);
}

SEC("socket")
__description("map access: mixing value pointer and scalar, 1")
__success __failure_unpriv
__msg_unpriv("R2 tried to add from different maps, paths or scalars, pointer arithmetic with it prohibited for !root")
__retval(0)
__naked void value_pointer_and_scalar_1(void)
{
	asm volatile ("					\
	/* load map value pointer into r0 and r2 */	\
	r0 = 1;						\
	r1 = %[map_array_48b] ll;			\
	r2 = r10;					\
	r2 += -16;					\
	r6 = 0;						\
	*(u64*)(r10 - 16) = r6;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	/* load some number from the map into r1 */	\
	r1 = *(u8*)(r0 + 0);				\
	/* depending on r1, branch: */			\
	if r1 != 0 goto l1_%=;				\
	/* branch A */					\
	r2 = r0;					\
	r3 = 0;						\
	goto l2_%=;					\
l1_%=:	/* branch B */					\
	r2 = 0;						\
	r3 = 0x100000;					\
l2_%=:	/* common instruction */			\
	r2 += r3;					\
	/* depending on r1, branch: */			\
	if r1 != 0 goto l3_%=;				\
	/* branch A */					\
	goto l4_%=;					\
l3_%=:	/* branch B */					\
	r0 = 0x13371337;				\
	/* verifier follows fall-through */		\
	/* unpriv: nospec (inserted to prevent `R2 pointer comparison prohibited`) */\
	if r2 != 0x100000 goto l4_%=;			\
	r0 = 0;						\
	exit;						\
l4_%=:	/* fake-dead code; targeted from branch A to	\
	 * prevent dead code sanitization		\
	 */						\
	r0 = *(u8*)(r0 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: mixing value pointer and scalar, 2")
__success __failure_unpriv
__msg_unpriv("R2 tried to add from different maps, paths or scalars, pointer arithmetic with it prohibited for !root")
__retval(0)
__naked void value_pointer_and_scalar_2(void)
{
	asm volatile ("					\
	/* load map value pointer into r0 and r2 */	\
	r0 = 1;						\
	r1 = %[map_array_48b] ll;			\
	r2 = r10;					\
	r2 += -16;					\
	r6 = 0;						\
	*(u64*)(r10 - 16) = r6;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	/* load some number from the map into r1 */	\
	r1 = *(u8*)(r0 + 0);				\
	/* depending on r1, branch: */			\
	if r1 == 0 goto l1_%=;				\
	/* branch A */					\
	r2 = 0;						\
	r3 = 0x100000;					\
	goto l2_%=;					\
l1_%=:	/* branch B */					\
	r2 = r0;					\
	r3 = 0;						\
l2_%=:	/* common instruction */			\
	r2 += r3;					\
	/* depending on r1, branch: */			\
	if r1 != 0 goto l3_%=;				\
	/* branch A */					\
	goto l4_%=;					\
l3_%=:	/* branch B */					\
	r0 = 0x13371337;				\
	/* verifier follows fall-through */		\
	if r2 != 0x100000 goto l4_%=;			\
	r0 = 0;						\
	exit;						\
l4_%=:	/* fake-dead code; targeted from branch A to	\
	 * prevent dead code sanitization, rejected	\
	 * via branch B however				\
	 */						\
	/* unpriv: nospec (inserted to prevent `R0 invalid mem access 'scalar'`) */\
	r0 = *(u8*)(r0 + 0);				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("sanitation: alu with different scalars 1")
__success __success_unpriv __retval(0x100000)
__naked void alu_with_different_scalars_1(void)
{
	asm volatile ("					\
	r0 = 1;						\
	r1 = %[map_array_48b] ll;			\
	r2 = r10;					\
	r2 += -16;					\
	r6 = 0;						\
	*(u64*)(r10 - 16) = r6;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r1 = *(u32*)(r0 + 0);				\
	if r1 == 0 goto l1_%=;				\
	r2 = 0;						\
	r3 = 0x100000;					\
	goto l2_%=;					\
l1_%=:	r2 = 42;					\
	r3 = 0x100001;					\
l2_%=:	r2 += r3;					\
	r0 = r2;					\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("sanitation: alu with different scalars 2")
__success __success_unpriv __retval(0)
__naked void alu_with_different_scalars_2(void)
{
	asm volatile ("					\
	r0 = 1;						\
	r1 = %[map_array_48b] ll;			\
	r6 = r1;					\
	r2 = r10;					\
	r2 += -16;					\
	r7 = 0;						\
	*(u64*)(r10 - 16) = r7;				\
	call %[bpf_map_delete_elem];			\
	r7 = r0;					\
	r1 = r6;					\
	r2 = r10;					\
	r2 += -16;					\
	call %[bpf_map_delete_elem];			\
	r6 = r0;					\
	r8 = r6;					\
	r8 += r7;					\
	r0 = r8;					\
	r0 += %[einval];				\
	r0 += %[einval];				\
	exit;						\
"	:
	: __imm(bpf_map_delete_elem),
	  __imm_addr(map_array_48b),
	  __imm_const(einval, EINVAL)
	: __clobber_all);
}

SEC("socket")
__description("sanitation: alu with different scalars 3")
__success __success_unpriv __retval(0)
__naked void alu_with_different_scalars_3(void)
{
	asm volatile ("					\
	r0 = %[einval];					\
	r0 *= -1;					\
	r7 = r0;					\
	r0 = %[einval];					\
	r0 *= -1;					\
	r6 = r0;					\
	r8 = r6;					\
	r8 += r7;					\
	r0 = r8;					\
	r0 += %[einval];				\
	r0 += %[einval];				\
	exit;						\
"	:
	: __imm_const(einval, EINVAL)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, upper oob arith, test 1")
__success __failure_unpriv
__msg_unpriv("R0 pointer arithmetic of map value goes out of range")
__retval(1)
__naked void upper_oob_arith_test_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 48;					\
	r0 += r1;					\
	r0 -= r1;					\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, upper oob arith, test 2")
__success __failure_unpriv
__msg_unpriv("R0 pointer arithmetic of map value goes out of range")
__retval(1)
__naked void upper_oob_arith_test_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 49;					\
	r0 += r1;					\
	r0 -= r1;					\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, upper oob arith, test 3")
__success __success_unpriv __retval(1)
__naked void upper_oob_arith_test_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 47;					\
	r0 += r1;					\
	r0 -= r1;					\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= known scalar, lower oob arith, test 1")
__failure __msg("R0 min value is outside of the allowed memory range")
__failure_unpriv
__msg_unpriv("R0 pointer arithmetic of map value goes out of range")
__naked void lower_oob_arith_test_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 47;					\
	r0 += r1;					\
	r1 = 48;					\
	r0 -= r1;					\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= known scalar, lower oob arith, test 2")
__success __failure_unpriv
__msg_unpriv("R0 pointer arithmetic of map value goes out of range")
__retval(1)
__naked void lower_oob_arith_test_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 47;					\
	r0 += r1;					\
	r1 = 48;					\
	r0 -= r1;					\
	r1 = 1;						\
	r0 += r1;					\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= known scalar, lower oob arith, test 3")
__success __success_unpriv __retval(1)
__naked void lower_oob_arith_test_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 47;					\
	r0 += r1;					\
	r1 = 47;					\
	r0 -= r1;					\
	r0 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar += value_ptr")
__success __success_unpriv __retval(1)
__naked void access_known_scalar_value_ptr_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 4;						\
	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, 1")
__success __success_unpriv __retval(1)
__naked void value_ptr_known_scalar_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 4;						\
	r0 += r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, 2")
__failure __msg("invalid access to map value")
__failure_unpriv
__naked void value_ptr_known_scalar_2_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 49;					\
	r0 += r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, 3")
__failure __msg("invalid access to map value")
__failure_unpriv
__naked void value_ptr_known_scalar_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = -1;					\
	r0 += r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, 4")
__success __success_unpriv __retval(1)
__naked void value_ptr_known_scalar_4(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 5;						\
	r0 += r1;					\
	r1 = -2;					\
	r0 += r1;					\
	r1 = -1;					\
	r0 += r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, 5")
__success __success_unpriv __retval(0xabcdef12)
__naked void value_ptr_known_scalar_5(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = %[__imm_0];				\
	r1 += r0;					\
	r0 = *(u32*)(r1 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_const(__imm_0, (6 + 1) * sizeof(int))
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += known scalar, 6")
__success __success_unpriv __retval(0xabcdef12)
__naked void value_ptr_known_scalar_6(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = %[__imm_0];				\
	r0 += r1;					\
	r1 = %[__imm_1];				\
	r0 += r1;					\
	r0 = *(u32*)(r0 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b),
	  __imm_const(__imm_0, (3 + 1) * sizeof(int)),
	  __imm_const(__imm_1, 3 * sizeof(int))
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += N, value_ptr -= N known scalar")
__success __success_unpriv __retval(0x12345678)
__naked void value_ptr_n_known_scalar(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	w1 = 0x12345678;				\
	*(u32*)(r0 + 0) = r1;				\
	r0 += 2;					\
	r1 = 2;						\
	r0 -= r1;					\
	r0 = *(u32*)(r0 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: unknown scalar += value_ptr, 1")
__success __success_unpriv __retval(1)
__naked void unknown_scalar_value_ptr_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	r1 &= 0xf;					\
	r1 += r0;					\
	r0 = *(u8*)(r1 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: unknown scalar += value_ptr, 2")
__success __success_unpriv __retval(0xabcdef12) __flag(BPF_F_ANY_ALIGNMENT)
__naked void unknown_scalar_value_ptr_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	r1 &= 31;					\
	r1 += r0;					\
	r0 = *(u32*)(r1 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: unknown scalar += value_ptr, 3")
__success __failure_unpriv
__msg_unpriv("R0 pointer arithmetic of map value goes out of range")
__retval(0xabcdef12) __flag(BPF_F_ANY_ALIGNMENT)
__naked void unknown_scalar_value_ptr_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = -1;					\
	r0 += r1;					\
	r1 = 1;						\
	r0 += r1;					\
	r1 = *(u32*)(r0 + 0);				\
	r1 &= 31;					\
	r1 += r0;					\
	r0 = *(u32*)(r1 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: unknown scalar += value_ptr, 4")
__failure __msg("R1 max value is outside of the allowed memory range")
__msg_unpriv("R1 pointer arithmetic of map value goes out of range")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void unknown_scalar_value_ptr_4(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 19;					\
	r0 += r1;					\
	r1 = *(u32*)(r0 + 0);				\
	r1 &= 31;					\
	r1 += r0;					\
	r0 = *(u32*)(r1 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += unknown scalar, 1")
__success __success_unpriv __retval(1)
__naked void value_ptr_unknown_scalar_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	r1 &= 0xf;					\
	r0 += r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += unknown scalar, 2")
__success __success_unpriv __retval(0xabcdef12) __flag(BPF_F_ANY_ALIGNMENT)
__naked void value_ptr_unknown_scalar_2_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u32*)(r0 + 0);				\
	r1 &= 31;					\
	r0 += r1;					\
	r0 = *(u32*)(r0 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += unknown scalar, 3")
__success __success_unpriv __retval(1)
__naked void value_ptr_unknown_scalar_3(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u64*)(r0 + 0);				\
	r2 = *(u64*)(r0 + 8);				\
	r3 = *(u64*)(r0 + 16);				\
	r1 &= 0xf;					\
	r3 &= 1;					\
	r3 |= 1;					\
	if r2 > r3 goto l0_%=;				\
	r0 += r3;					\
	r0 = *(u8*)(r0 + 0);				\
	r0 = 1;						\
l1_%=:	exit;						\
l0_%=:	r0 = 2;						\
	goto l1_%=;					\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr += value_ptr")
__failure __msg("R0 pointer += pointer prohibited")
__failure_unpriv
__naked void access_value_ptr_value_ptr_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 += r0;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: known scalar -= value_ptr")
__failure __msg("R1 tried to subtract pointer from scalar")
__failure_unpriv
__naked void access_known_scalar_value_ptr_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 4;						\
	r1 -= r0;					\
	r0 = *(u8*)(r1 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= known scalar")
__failure __msg("R0 min value is outside of the allowed memory range")
__failure_unpriv
__naked void access_value_ptr_known_scalar(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 4;						\
	r0 -= r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= known scalar, 2")
__success __success_unpriv __retval(1)
__naked void value_ptr_known_scalar_2_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = 6;						\
	r2 = 4;						\
	r0 += r1;					\
	r0 -= r2;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: unknown scalar -= value_ptr")
__failure __msg("R1 tried to subtract pointer from scalar")
__failure_unpriv
__naked void access_unknown_scalar_value_ptr(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	r1 &= 0xf;					\
	r1 -= r0;					\
	r0 = *(u8*)(r1 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= unknown scalar")
__failure __msg("R0 min value is negative")
__failure_unpriv
__naked void access_value_ptr_unknown_scalar(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	r1 &= 0xf;					\
	r0 -= r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= unknown scalar, 2")
__success __success_unpriv
__retval(1)
#ifdef SPEC_V1
__xlated_unpriv("r1 &= 7")
__xlated_unpriv("nospec") /* inserted to prevent `R0 pointer arithmetic of map value goes out of range` */
__xlated_unpriv("r0 -= r1")
#endif
__naked void value_ptr_unknown_scalar_2_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u8*)(r0 + 0);				\
	r1 &= 0xf;					\
	r1 |= 0x7;					\
	r0 += r1;					\
	r1 = *(u8*)(r0 + 0);				\
	r1 &= 0x7;					\
	r0 -= r1;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: value_ptr -= value_ptr")
__failure __msg("R0 invalid mem access 'scalar'")
__msg_unpriv("R0 pointer -= pointer prohibited")
__naked void access_value_ptr_value_ptr_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 -= r0;					\
	r1 = *(u8*)(r0 + 0);				\
l0_%=:	r0 = 1;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("socket")
__description("map access: trying to leak tainted dst reg")
__failure __msg("math between map_value pointer and 4294967295 is not allowed")
__failure_unpriv
__naked void to_leak_tainted_dst_reg(void)
{
	asm volatile ("					\
	r0 = 0;						\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_array_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	exit;						\
l0_%=:	r2 = r0;					\
	w1 = 0xFFFFFFFF;				\
	w1 = w1;					\
	r2 -= r1;					\
	*(u64*)(r0 + 0) = r2;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_array_48b)
	: __clobber_all);
}

SEC("tc")
__description("32bit pkt_ptr -= scalar")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void _32bit_pkt_ptr_scalar(void)
{
	asm volatile ("					\
	r8 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r7 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r6 = r7;					\
	r6 += 40;					\
	if r6 > r8 goto l0_%=;				\
	w4 = w7;					\
	w6 -= w4;					\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

SEC("tc")
__description("32bit scalar -= pkt_ptr")
__success __retval(0) __flag(BPF_F_ANY_ALIGNMENT)
__naked void _32bit_scalar_pkt_ptr(void)
{
	asm volatile ("					\
	r8 = *(u32*)(r1 + %[__sk_buff_data_end]);	\
	r7 = *(u32*)(r1 + %[__sk_buff_data]);		\
	r6 = r7;					\
	r6 += 40;					\
	if r6 > r8 goto l0_%=;				\
	w4 = w6;					\
	w4 -= w7;					\
l0_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
	  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
