// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

void *bpf_arena_alloc_pages(void *map, void *addr, __u32 page_cnt, int node_id,
			    __u64 flags) __ksym;
void bpf_arena_free_pages(void *map, void *ptr, __u32 page_cnt) __ksym;

#ifdef __TARGET_ARCH_arm64
#define ARENA_VM_START (1ull << 32)
#else
#define ARENA_VM_START (1ull << 44)
#endif

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 4);
	__ulong(map_extra, ARENA_VM_START);
} arena SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, long long);
} hash SEC(".maps");

/* JITs that take BPF_F_ARENA_SCALAR */
#define __arena_scalar __flag(BPF_F_ARENA_SCALAR) __arch_x86_64 __arch_arm64

/* BTF FUNC records are not generated for kfuncs referenced from inline assembly */
void __kfunc_btf_root(void)
{
	bpf_arena_alloc_pages(0, 0, 0, 0, 0);
	bpf_arena_free_pages(0, 0, 0);
}

/* Tests start with r6 = address of a new page as the user space sees it, a number */

SEC("syscall")
__arena_scalar
__description("arena_scalar: load and store of every size")
__success __retval(0)
__load_if_JITed()
__naked void ld_st_sizes(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r7 = r6;					\
	r1 = 0x1122334455667788 ll;			\
	*(u64 *)(r6 + 0) = r1;				\
	*(u32 *)(r6 + 8) = r1;				\
	*(u16 *)(r6 + 12) = r1;				\
	*(u8 *)(r6 + 14) = r1;				\
	*(u64 *)(r6 + 16) = 0x1234;			\
	*(u32 *)(r6 + 24) = 0x5678;			\
	*(u16 *)(r6 + 28) = 0x9a;			\
	*(u8 *)(r6 + 30) = 0xbc;			\
	r0 = 1;						\
	r2 = *(u64 *)(r6 + 0);				\
	if r2 != r1 goto 9f;				\
	r0 = 2;						\
	r2 = *(u32 *)(r6 + 8);				\
	if r2 != 0x55667788 goto 9f;			\
	r0 = 3;						\
	r2 = *(u16 *)(r6 + 12);				\
	if r2 != 0x7788 goto 9f;			\
	r0 = 4;						\
	r2 = *(u8 *)(r6 + 14);				\
	if r2 != 0x88 goto 9f;				\
	r0 = 5;						\
	r2 = *(u64 *)(r6 + 16);				\
	if r2 != 0x1234 goto 9f;			\
	r0 = 6;						\
	r2 = *(u32 *)(r6 + 24);				\
	if r2 != 0x5678 goto 9f;			\
	r0 = 7;						\
	r2 = *(u16 *)(r6 + 28);				\
	if r2 != 0x9a goto 9f;				\
	r0 = 8;						\
	r2 = *(u8 *)(r6 + 30);				\
	if r2 != 0xbc goto 9f;				\
	/* the address is what it was */		\
	r0 = 9;						\
	if r6 != r7 goto 9f;				\
	r0 = 10;					\
	r7 >>= 32;					\
	if r7 == 0 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: load into the register that holds the address")
__success __retval(0)
__load_if_JITed()
__naked void ld_into_base(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r1 = 77;					\
	*(u64 *)(r6 + 0) = r1;				\
	r6 = *(u64 *)(r6 + 0);				\
	r0 = 1;						\
	if r6 != 77 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: sign extending load")
__success __retval(0)
__load_if_JITed()
__naked void ldsx(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r7 = r6;					\
	*(u64 *)(r6 + 0) = 0x80;			\
	.8byte %[ldsx_insn]; /* r2 = *(s8 *)(r6 + 0) */	\
	r0 = 1;						\
	if r2 != -128 goto 9f;				\
	r0 = 2;						\
	if r6 != r7 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(ldsx_insn, BPF_RAW_INSN(BPF_LDX | BPF_MEMSX | BPF_B,
					     BPF_REG_2, BPF_REG_6, 0, 0))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: address loaded from arena")
__success __retval(0)
__load_if_JITed()
__naked void ptr_chase(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	/* page[0] = &page[64]; page[64] = 5; */	\
	r1 = r6;					\
	r1 += 64;					\
	*(u64 *)(r6 + 0) = r1;				\
	*(u64 *)(r1 + 0) = 5;				\
	r2 = *(u64 *)(r6 + 0);				\
	r0 = 1;						\
	if r2 != r1 goto 9f;				\
	r3 = *(u64 *)(r2 + 0);				\
	r0 = 2;						\
	if r3 != 5 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: atomics")
__success __retval(0)
__load_if_JITed()
__naked void atomics(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r7 = r6;					\
	*(u64 *)(r6 + 8) = 1;				\
	r1 = 2;						\
	lock *(u64 *)(r6 + 8) += r1;			\
	r1 = 4;						\
	.8byte %[fetch_add_insn]; /* r1 = atomic_fetch_add((u64 *)(r6 + 8), r1) */ \
	r0 = 1;						\
	if r1 != 3 goto 9f;				\
	r1 = 8;						\
	.8byte %[xchg_insn]; /* r1 = xchg_64(r6 + 8, r1) */ \
	r0 = 2;						\
	if r1 != 7 goto 9f;				\
	r0 = 8;						\
	r1 = 16;					\
	.8byte %[cmpxchg_insn]; /* r0 = cmpxchg_64(r6 + 8, r0, r1) */ \
	r2 = r0;					\
	r0 = 3;						\
	if r2 != 8 goto 9f;				\
	r2 = *(u64 *)(r6 + 8);				\
	r0 = 4;						\
	if r2 != 16 goto 9f;				\
	r0 = 5;						\
	if r6 != r7 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(fetch_add_insn, BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH,
						   BPF_REG_6, BPF_REG_1, 8)),
	  __imm_insn(xchg_insn, BPF_ATOMIC_OP(BPF_DW, BPF_XCHG, BPF_REG_6, BPF_REG_1, 8)),
	  __imm_insn(cmpxchg_insn, BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_6, BPF_REG_1, 8))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: fetching and/or/xor")
__success __retval(0)
__load_if_JITed()
__naked void fetch_and_or_xor(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r7 = 0x11000000110 ll;				\
	*(u64 *)(r6 + 8) = r7;				\
	r0 = r6;					\
	r0 ^= 0x55;					\
	r1 = 0x1100000011 ll;				\
	.8byte %[fetch_or_dw]; /* r1 = atomic_fetch_or((u64 *)(r6 + 8), r1) */ \
	r2 = r6;					\
	r2 ^= 0x55;					\
	if r0 == r2 goto 1f;				\
	r0 = 99;					\
	goto 9f;					\
1:	r0 = 1;						\
	if r1 != r7 goto 9f;				\
	r7 = 0x11100000111 ll;				\
	r1 = 0x11000000111 ll;				\
	.8byte %[fetch_and_dw]; /* r1 = atomic_fetch_and((u64 *)(r6 + 8), r1) */ \
	r0 = 2;						\
	if r1 != r7 goto 9f;				\
	r7 = 0x11000000111 ll;				\
	r1 = 0x101 ll;					\
	.8byte %[fetch_xor_dw]; /* r1 = atomic_fetch_xor((u64 *)(r6 + 8), r1) */ \
	r0 = 3;						\
	if r1 != r7 goto 9f;				\
	r7 = 0x11000000010 ll;				\
	r2 = *(u64 *)(r6 + 8);				\
	r0 = 4;						\
	if r2 != r7 goto 9f;				\
	*(u32 *)(r6 + 16) = 0x110;			\
	r1 = 0xdead000000000011 ll;			\
	.8byte %[fetch_or_w]; /* r1 = atomic_fetch_or((u32 *)(r6 + 16), r1) */ \
	r0 = 5;						\
	if r1 != 0x110 goto 9f;				\
	r1 = 0xdead000000000101 ll;			\
	.8byte %[fetch_and_w]; /* r1 = atomic_fetch_and((u32 *)(r6 + 16), r1) */ \
	r0 = 6;						\
	if r1 != 0x111 goto 9f;				\
	r1 = 0xdead000000000011 ll;			\
	.8byte %[fetch_xor_w]; /* r1 = atomic_fetch_xor((u32 *)(r6 + 16), r1) */ \
	r0 = 7;						\
	if r1 != 0x101 goto 9f;				\
	r2 = *(u32 *)(r6 + 16);				\
	r0 = 8;						\
	if r2 != 0x110 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(fetch_or_dw, BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH,
						BPF_REG_6, BPF_REG_1, 8)),
	  __imm_insn(fetch_and_dw, BPF_ATOMIC_OP(BPF_DW, BPF_AND | BPF_FETCH,
						 BPF_REG_6, BPF_REG_1, 8)),
	  __imm_insn(fetch_xor_dw, BPF_ATOMIC_OP(BPF_DW, BPF_XOR | BPF_FETCH,
						 BPF_REG_6, BPF_REG_1, 8)),
	  __imm_insn(fetch_or_w, BPF_ATOMIC_OP(BPF_W, BPF_OR | BPF_FETCH,
					       BPF_REG_6, BPF_REG_1, 16)),
	  __imm_insn(fetch_and_w, BPF_ATOMIC_OP(BPF_W, BPF_AND | BPF_FETCH,
						BPF_REG_6, BPF_REG_1, 16)),
	  __imm_insn(fetch_xor_w, BPF_ATOMIC_OP(BPF_W, BPF_XOR | BPF_FETCH,
						BPF_REG_6, BPF_REG_1, 16))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: fetching and/or/xor with the operand in r0")
__success __retval(0)
__load_if_JITed()
__naked void fetch_and_or_xor_r0(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r7 = 0x11000000110 ll;				\
	*(u64 *)(r6 + 8) = r7;				\
	r0 = 0x1100000011 ll;				\
	.8byte %[fetch_or_dw]; /* r0 = atomic_fetch_or((u64 *)(r6 + 8), r0) */ \
	r2 = r0;					\
	r0 = 1;						\
	if r2 != r7 goto 9f;				\
	r7 = 0x11100000111 ll;				\
	r0 = 0x11000000111 ll;				\
	.8byte %[fetch_and_dw]; /* r0 = atomic_fetch_and((u64 *)(r6 + 8), r0) */ \
	r2 = r0;					\
	r0 = 2;						\
	if r2 != r7 goto 9f;				\
	r7 = 0x11000000111 ll;				\
	r0 = 0x101 ll;					\
	.8byte %[fetch_xor_dw]; /* r0 = atomic_fetch_xor((u64 *)(r6 + 8), r0) */ \
	r2 = r0;					\
	r0 = 3;						\
	if r2 != r7 goto 9f;				\
	r7 = 0x11000000010 ll;				\
	r2 = *(u64 *)(r6 + 8);				\
	r0 = 4;						\
	if r2 != r7 goto 9f;				\
	*(u32 *)(r6 + 16) = 0x110;			\
	r0 = 0xdead000000000011 ll;			\
	.8byte %[fetch_or_w]; /* r0 = atomic_fetch_or((u32 *)(r6 + 16), r0) */ \
	r2 = r0;					\
	r0 = 5;						\
	if r2 != 0x110 goto 9f;				\
	r0 = 0xdead000000000101 ll;			\
	.8byte %[fetch_and_w]; /* r0 = atomic_fetch_and((u32 *)(r6 + 16), r0) */ \
	r2 = r0;					\
	r0 = 6;						\
	if r2 != 0x111 goto 9f;				\
	r0 = 0xdead000000000011 ll;			\
	.8byte %[fetch_xor_w]; /* r0 = atomic_fetch_xor((u32 *)(r6 + 16), r0) */ \
	r2 = r0;					\
	r0 = 7;						\
	if r2 != 0x101 goto 9f;				\
	r2 = *(u32 *)(r6 + 16);				\
	r0 = 8;						\
	if r2 != 0x110 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(fetch_or_dw, BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH,
						BPF_REG_6, BPF_REG_0, 8)),
	  __imm_insn(fetch_and_dw, BPF_ATOMIC_OP(BPF_DW, BPF_AND | BPF_FETCH,
						 BPF_REG_6, BPF_REG_0, 8)),
	  __imm_insn(fetch_xor_dw, BPF_ATOMIC_OP(BPF_DW, BPF_XOR | BPF_FETCH,
						 BPF_REG_6, BPF_REG_0, 8)),
	  __imm_insn(fetch_or_w, BPF_ATOMIC_OP(BPF_W, BPF_OR | BPF_FETCH,
					       BPF_REG_6, BPF_REG_0, 16)),
	  __imm_insn(fetch_and_w, BPF_ATOMIC_OP(BPF_W, BPF_AND | BPF_FETCH,
						BPF_REG_6, BPF_REG_0, 16)),
	  __imm_insn(fetch_xor_w, BPF_ATOMIC_OP(BPF_W, BPF_XOR | BPF_FETCH,
						BPF_REG_6, BPF_REG_0, 16))
	: __clobber_all);
}

/*
 * The page is gone, so the access faults. The fetched register has to come back
 * 0 and everything else has to be left alone, r0 included. Reading the address
 * again has to give 0 as well: had the fault been recovered by the arena mapping
 * its zeroed scratch page instead of by the JIT's exception table entry, the
 * access would have succeeded and left a 1 there.
 */
SEC("syscall")
__arena_scalar
__description("arena_scalar: fetching or through a number to a freed page")
__success __retval(0)
__load_if_JITed()
__naked void fetch_or_freed_page(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	*(u64 *)(r6 + 8) = 0x55;			\
	r1 = %[arena] ll;				\
	r2 = r6;					\
	r3 = 1;						\
	call %[bpf_arena_free_pages];			\
	r0 = r6;					\
	r0 ^= 0x55;					\
	r1 = 1;						\
	.8byte %[fetch_or_dw]; /* faults: r1 = atomic_fetch_or((u64 *)(r6 + 8), r1) */ \
	r2 = r6;					\
	r2 ^= 0x55;					\
	if r0 == r2 goto 1f;				\
	r0 = 1;						\
	goto 9f;					\
1:	r0 = 2;						\
	if r1 != 0 goto 9f;				\
	r3 = *(u64 *)(r6 + 8);				\
	r0 = 3;						\
	if r3 != 0 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm(bpf_arena_free_pages),
	  __imm_insn(fetch_or_dw, BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH,
						BPF_REG_6, BPF_REG_1, 8))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: fetching or with the operand in r0 to a freed page")
__success __retval(0)
__load_if_JITed()
__naked void fetch_or_r0_freed_page(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	*(u64 *)(r6 + 8) = 0x55;			\
	r1 = %[arena] ll;				\
	r2 = r6;					\
	r3 = 1;						\
	call %[bpf_arena_free_pages];			\
	r0 = 1;						\
	.8byte %[fetch_or_dw]; /* faults: r0 = atomic_fetch_or((u64 *)(r6 + 8), r0) */ \
	r2 = r0;					\
	r0 = 1;						\
	if r2 != 0 goto 9f;				\
	r3 = *(u64 *)(r6 + 8);				\
	r0 = 2;						\
	if r3 != 0 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm(bpf_arena_free_pages),
	  __imm_insn(fetch_or_dw, BPF_ATOMIC_OP(BPF_DW, BPF_OR | BPF_FETCH,
						BPF_REG_6, BPF_REG_0, 8))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: cmpxchg through r0")
__success __retval(0)
__load_if_JITed()
__naked void cmpxchg_r0(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	/* The address is in r0. The value at the address is not equal to it. */ \
	*(u64 *)(r6 + 0) = 3;				\
	r0 = r6;					\
	r1 = 5;						\
	.8byte %[cmpxchg_insn]; /* r0 = cmpxchg_64(r0 + 0, r0, r1) */ \
	r2 = r0;					\
	r0 = 1;						\
	if r2 != 3 goto 9f;				\
	r2 = *(u64 *)(r6 + 0);				\
	r0 = 2;						\
	if r2 != 3 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(cmpxchg_insn, BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_0, BPF_REG_1, 0))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: xchg into the register that holds the address")
__success __retval(0)
__load_if_JITed()
__naked void xchg_into_base(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	*(u64 *)(r6 + 0) = 3;				\
	r1 = r6;					\
	.8byte %[xchg_insn]; /* r1 = xchg_64(r1 + 0, r1) */ \
	r0 = 1;						\
	if r1 != 3 goto 9f;				\
	r2 = *(u64 *)(r6 + 0);				\
	r0 = 2;						\
	if r2 != r6 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(xchg_insn, BPF_ATOMIC_OP(BPF_DW, BPF_XCHG, BPF_REG_1, BPF_REG_1, 0))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: xchg into the register that holds a pointer to stack")
__failure __msg("misaligned access off (0x0; 0xffffffffffffffff)+0 size 8")
__naked void xchg_into_stack_ptr(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r1 = 0;						\
	*(u64 *)(r10 - 8) = r1;				\
	r1 = r10;					\
	r1 += -8;					\
	.8byte %[xchg_insn]; /* r1 = xchg_64(r1 + 0, r1) */ \
	r0 = 0;						\
	exit;						\
"	:
	: __imm_addr(arena),
	  __imm_insn(xchg_insn, BPF_ATOMIC_OP(BPF_DW, BPF_XCHG, BPF_REG_1, BPF_REG_1, 0))
	: __clobber_all);
}

#ifdef CAN_USE_LOAD_ACQ_STORE_REL

SEC("syscall")
__arena_scalar
__description("arena_scalar: load-acquire and store-release")
__success __retval(0)
__load_if_JITed()
__naked void load_acq_store_rel(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r7 = r6;					\
	r1 = 0x1234;					\
	.8byte %[store_release_insn]; /* store_release((u64 *)(r6 + 8), r1) */ \
	.8byte %[load_acquire_insn]; /* r2 = load_acquire((u64 *)(r6 + 8)) */ \
	r0 = 1;						\
	if r2 != 0x1234 goto 9f;			\
	.8byte %[load_acquire8_insn]; /* w2 = load_acquire((u8 *)(r6 + 8)) */ \
	r0 = 2;						\
	if r2 != 0x34 goto 9f;				\
	r0 = 3;						\
	if r6 != r7 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_6, BPF_REG_1, 8)),
	  __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_2, BPF_REG_6, 8)),
	  __imm_insn(load_acquire8_insn,
		     BPF_ATOMIC_OP(BPF_B, BPF_LOAD_ACQ, BPF_REG_2, BPF_REG_6, 8))
	: __clobber_all);
}

#endif /* CAN_USE_LOAD_ACQ_STORE_REL */

SEC("syscall")
__arena_scalar
__description("arena_scalar: number that is not an address in arena")
__success __retval(0)
__load_if_JITed()
__naked void not_in_arena(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	/* nothing is allocated: all loads read 0, stores are dropped */ \
	r6 = 0xdeadbeef00000000 ll;			\
	r0 = 1;						\
	r2 = *(u64 *)(r6 + 0);				\
	if r2 != 0 goto 9f;				\
	r0 = 2;						\
	r2 = *(u8 *)(r6 - 32768);			\
	if r2 != 0 goto 9f;				\
	r6 = 0x12345678ffffffff ll;			\
	r0 = 3;						\
	r2 = *(u64 *)(r6 + 32760);			\
	if r2 != 0 goto 9f;				\
	*(u64 *)(r6 + 32760) = 1;			\
	*(u8 *)(r6 + 32767) = r2;			\
	r1 = 1;						\
	lock *(u64 *)(r6 + 32760) += r1;		\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: store through the address of the stack as a number")
__success __retval(0)
__load_if_JITed()
__naked void stack_addr_as_number(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	*(u64 *)(r10 - 8) = 5;				\
	r6 = r10;					\
	r6 |= 0;					\
	/* r6 is a number now. The store goes to arena, not to the stack. */ \
	*(u64 *)(r6 - 8) = 7;				\
	r1 = 9;						\
	*(u64 *)(r6 - 8) = r1;				\
	lock *(u64 *)(r6 - 8) += r1;			\
	r2 = *(u64 *)(r10 - 8);				\
	r0 = 1;						\
	if r2 != 5 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: 64-bit math on the address stays 64-bit")
__success __retval(0)
__load_if_JITed()
__naked void alu64_after_access(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r2 = *(u64 *)(r6 + 0);				\
	r7 = r6;					\
	r7 += 8;					\
	r7 -= r6;					\
	r0 = 1;						\
	if r7 != 8 goto 9f;				\
	r7 = r6;					\
	r7 >>= 32;					\
	r0 = 2;						\
	if r7 == 0 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: st of an immediate changes no register")
__success __retval(0)
__load_if_JITed()
__naked void st_keeps_regs(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r0 = r6;					\
	r1 = 0x1111;					\
	r2 = 0x2222;					\
	*(u64 *)(r0 + 0) = 5;				\
	r3 = r0;					\
	r0 = 1;						\
	if r3 != r6 goto 9f;				\
	r0 = 2;						\
	if r1 != 0x1111 goto 9f;			\
	r0 = 3;						\
	if r2 != 0x2222 goto 9f;			\
	r0 = 0x3333;					\
	r1 = r6;					\
	*(u32 *)(r1 + 8) = -7;				\
	r3 = r0;					\
	r0 = 4;						\
	if r3 != 0x3333 goto 9f;			\
	r0 = 5;						\
	if r1 != r6 goto 9f;				\
	r0 = 6;						\
	r3 = *(u64 *)(r6 + 0);				\
	if r3 != 5 goto 9f;				\
	r0 = 7;						\
	r3 = *(u64 *)(r6 + 8);				\
	r4 = 0xfffffff9 ll;				\
	if r3 != r4 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: access through a number with garbage in the upper half")
__success __retval(0)
__load_if_JITed()
__naked void st_value_garbage(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r1 = 0xdeadbeef00001000 ll;			\
	*(u64 *)(r6 + 2048) = r1;			\
	r7 = *(u64 *)(r6 + 2048);			\
	r8 = *(u64 *)(r6 + 2048);			\
	r1 = 3;						\
	*(u64 *)(r8 + 0) = 1;				\
	r0 = 1;						\
	if r8 != r7 goto 9f;				\
	*(u8 *)(r8 + 1) = 1;				\
	r0 = 2;						\
	if r8 != r7 goto 9f;				\
	*(u32 *)(r8 + 4) = r1;				\
	r0 = 3;						\
	if r8 != r7 goto 9f;				\
	r2 = *(u16 *)(r8 + 2);				\
	r0 = 4;						\
	if r8 != r7 goto 9f;				\
	r0 = 5;						\
	if r1 != 3 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: access through a number without the upper half")
__success __retval(0)
__load_if_JITed()
__naked void st_value_small(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r1 = 0x1000 ll;					\
	*(u64 *)(r6 + 2048) = r1;			\
	r7 = *(u64 *)(r6 + 2048);			\
	r8 = *(u64 *)(r6 + 2048);			\
	r1 = 3;						\
	*(u64 *)(r8 + 0) = 1;				\
	r0 = 1;						\
	if r8 != r7 goto 9f;				\
	*(u8 *)(r8 + 1) = 1;				\
	r0 = 2;						\
	if r8 != r7 goto 9f;				\
	*(u32 *)(r8 + 4) = r1;				\
	r0 = 3;						\
	if r8 != r7 goto 9f;				\
	r2 = *(u16 *)(r8 + 2);				\
	r0 = 4;						\
	if r8 != r7 goto 9f;				\
	r0 = 5;						\
	if r1 != 3 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: atomics through a number that is not an address in arena")
__success __retval(0)
__load_if_JITed()
__naked void atomic_value(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r1 = 0xdeadbeef00001000 ll;			\
	*(u64 *)(r6 + 2048) = r1;			\
	r7 = *(u64 *)(r6 + 2048);			\
	r8 = *(u64 *)(r6 + 2048);			\
	r1 = 3;						\
	lock *(u64 *)(r8 + 0) += r1;			\
	r0 = 1;						\
	if r8 != r7 goto 9f;				\
	.8byte %[fetch_add_insn];			\
	r0 = 2;						\
	if r8 != r7 goto 9f;				\
	.8byte %[xchg_insn];				\
	r0 = 3;						\
	if r8 != r7 goto 9f;				\
	r0 = 0;						\
	.8byte %[cmpxchg_insn];				\
	r0 = 4;						\
	if r8 != r7 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(fetch_add_insn, BPF_ATOMIC_OP(BPF_DW, BPF_ADD | BPF_FETCH,
						   BPF_REG_8, BPF_REG_1, 0)),
	  __imm_insn(xchg_insn, BPF_ATOMIC_OP(BPF_DW, BPF_XCHG, BPF_REG_8, BPF_REG_1, 0)),
	  __imm_insn(cmpxchg_insn, BPF_ATOMIC_OP(BPF_DW, BPF_CMPXCHG, BPF_REG_8, BPF_REG_1, 0))
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: number and pointer to arena at the same insn")
__success __retval(0)
__load_if_JITed()
__naked void mixed_number_arena(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	*(u64 *)(r6 + 8) = 0x1234;			\
	/* a number that the verifier does not know, 0 at run time */ \
	r7 = *(u64 *)(r6 + 16);				\
	r8 = r6;					\
	if r7 != 0 goto 1f;				\
	.8byte %[cast_kern_insn];			\
1:	*(u8 *)(r8 + 0) = 1;				\
	*(u8 *)(r8 + 1) = r7;				\
	r2 = *(u8 *)(r8 + 1);				\
	lock *(u64 *)(r8 + 24) += r7;			\
	r0 = 0;						\
	if r7 != 0 goto 9f;				\
	/* pointer to arena only: JIT adds all 64 bits of r8 to the base */ \
	r0 = 2;						\
	r2 = *(u64 *)(r8 + 8);				\
	if r2 != 0x1234 goto 9f;			\
	r0 = 3;						\
	r2 = *(u8 *)(r6 + 0);				\
	if r2 != 1 goto 9f;				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm_insn(cast_kern_insn, BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_X,
						  BPF_REG_8, BPF_REG_8, 1, 1))
	: __clobber_all);
}

SEC("syscall")
__description("arena_scalar: no flag, no access through a number")
__failure __msg("R6 invalid mem access 'scalar'")
__naked void no_flag(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r6 = 0x100000000000 ll;				\
	r0 = *(u64 *)(r6 + 0);				\
	exit;						\
"	:
	: __imm_addr(arena)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: pointer that is NULL is an address in arena")
__success __retval(0)
__load_if_JITed()
__naked void null_ptr(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r1 = 0;						\
	*(u32 *)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[hash] ll;				\
	call %[bpf_map_lookup_elem];			\
	r1 = r0;					\
	r0 = 1;						\
	if r1 != 0 goto 9f;				\
	/* nothing is allocated: the load reads 0 */	\
	r0 = *(u64 *)(r1 + 0);				\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm_addr(hash),
	  __imm(bpf_map_lookup_elem)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: pointer that is NULL with an offset is an address in arena")
__success __retval(0)
__load_if_JITed()
__naked void null_ptr_off(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r1 = 0;						\
	*(u32 *)(r10 - 4) = r1;				\
	r2 = r10;					\
	r2 += -4;					\
	r1 = %[hash] ll;				\
	call %[bpf_map_lookup_elem];			\
	r1 = r0;					\
	r0 = 1;						\
	if r1 != 0 goto 9f;				\
	r1 += 8;					\
	*(u64 *)(r1 + 0) = 5;				\
	r0 = *(u64 *)(r1 + 0);				\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm_addr(hash),
	  __imm(bpf_map_lookup_elem)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: number that is less than a page is an address in arena")
__success __retval(0)
__load_if_JITed()
__naked void small_number(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r1 &= 0xfff;					\
	r0 = *(u64 *)(r1 + 0);				\
	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: number is not a pointer for a helper")
__failure __msg("R2 type=scalar expected=")
__naked void helper_arg(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	r1 = %[hash] ll;				\
	r2 = r6;					\
	call %[bpf_map_lookup_elem];			\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm_addr(hash),
	  __imm(bpf_arena_alloc_pages),
	  __imm(bpf_map_lookup_elem)
	: __clobber_all);
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: number and pointer to stack at the same insn")
__failure __msg("same insn cannot be used with different pointers")
__load_if_JITed()
__naked void mixed_number_stack(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = -1;					\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r6 = r0;					\
	r0 = 100;					\
	if r6 == 0 goto 9f;				\
	*(u64 *)(r10 - 8) = 0;				\
	call %[bpf_get_prandom_u32];			\
	if w0 != 0 goto 1f;				\
	r6 = r10;					\
	r6 += -8;					\
1:	r0 = *(u64 *)(r6 + 0);				\
	r0 = 0;						\
9:	exit;						\
"	:
	: __imm_addr(arena),
	  __imm(bpf_arena_alloc_pages),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

static int st_cb(__u64 idx, void *ctx)
{
	volatile long *p = *(volatile long **)ctx;

	p[idx] = 7;
	return 0;
}

SEC("syscall")
__arena_scalar
__description("arena_scalar: store through a number in a callback")
__success __retval(0)
__load_if_JITed()
int st_in_callback(void *unused)
{
	volatile long *p = bpf_arena_alloc_pages(&arena, NULL, 1, -1, 0);

	if (!p)
		return 100;
	bpf_loop(4, st_cb, &p, 0);
	return p[0] + p[3] + p[4] - 14;
}

char _license[] SEC("license") = "GPL";
