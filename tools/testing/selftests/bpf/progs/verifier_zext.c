// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include <bpf_arena_common.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE | BPF_F_NO_USER_CONV);
	__uint(max_entries, 1);
} arena SEC(".maps");

extern long bpf_kfunc_call_test4(signed char a, short b, int c, long d) __ksym;

/* to retain debug info for BTF generation */
void __kfunc_btf_root(void)
{
	bpf_kfunc_call_test4(0, 0, 0, 0);
	bpf_arena_alloc_pages(0, 0, 0, 0, 0);
	bpf_rdonly_cast(0, 0);
}

SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__flag(BPF_F_TEST_RND_HI32)
__success __retval(0)
__naked void zext_lost_across_checkpoint(void)
{
	asm volatile ("									\
	call %[bpf_ktime_get_ns];							\
	r8 = r0;									\
	r6 = 0xdeadbeefcafebabe ll;	/* inject some value for r6's upper half */	\
	if r8 != 0 goto 1f;		/* fall-through cached first, branch pruned */	\
	r6 = 32;			/* full 64-bit def */				\
	goto 2f;									\
1:	w6 = 32;			/* 32-bit def, zext mark lost */		\
2:	r0 = r6;			/* buggy verifier believed upper 32 bits are 0 */ \
					/* and thus did not zero extended w6 = 32. */	\
	r0 >>= 32;									\
	exit;										\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* 32-bit ALU result read as 64-bit -> zext */
SEC("socket")
__success __log_level(2)
__msg("w1 = w0{{ +}}; zext")
__naked void zext_alu32_hi_used(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* 32-bit ALU result read only as 32-bit -> no zext */
SEC("socket")
__success __log_level(2)
__not_msg("; zext")
__naked void no_zext_alu32_hi_unused(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;		/* MOV */		\
	w2 = w1;					\
	w2 += w1;		/* ALU32, BPF_X */	\
	w2 += 1;		/* ALU32, BPF_K */	\
	w2 = w2;		/* keep w2 alive for previous instruction */ \
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* 64-bit definition is never zero extended */
SEC("socket")
__success __log_level(2)
__not_msg("r1 = r0{{.*}}; zext")
__naked void no_zext_mov64(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Narrow load result read as 64-bit -> zext */
SEC("socket")
__success __log_level(2)
__msg("r1 = *(u32 *)(r10 -8){{ +}}; zext")
__naked void zext_narrow_load_hi_used(void)
{
	asm volatile ("					\
	r0 = 0;						\
	*(u64 *)(r10 - 8) = r0;				\
	r1 = *(u32 *)(r10 - 8);				\
	r0 = r1;					\
	exit;						\
"	::: __clobber_all);
}

/* 32-bit atomic fetch result read as 64-bit -> zext */
SEC("socket")
__success __log_level(2)
__msg("r1 = atomic_fetch_add((u32 *)(r10 -8), r1){{ +}}; zext")
__naked void zext_atomic_fetch32_hi_used(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64 *)(r10 - 8) = r1;				\
	w1 = 1;						\
	.8byte %[fetch_add32];				\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_insn(fetch_add32,
		     BPF_ATOMIC_OP(BPF_W, BPF_ADD | BPF_FETCH, BPF_REG_10, BPF_REG_1, -8))
	: __clobber_all);
}

/* 32-bit atomic cmpxchg result (r0) read as 64-bit -> zext */
SEC("socket")
__success __log_level(2)
__msg("r0 = atomic_cmpxchg((u32 *)(r10 -8), r0, r1){{ +}}; zext")
__naked void zext_cmpxchg32_hi_used(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64 *)(r10 - 8) = r1;				\
	w0 = 0;						\
	w1 = 1;						\
	.8byte %[cmpxchg32];				\
	r2 = r0;					\
	r0 = r2;					\
	exit;						\
"	:
	: __imm_insn(cmpxchg32,
		     BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_10, BPF_REG_1, -8))
	: __clobber_all);
}

/* 32-bit def before a branch, upper half used on one branch -> zext */
SEC("socket")
__success __log_level(2)
__msg("w6 = 32{{ +}}; zext")
__naked void zext_cfg_hi_used_one_branch(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w6 = 32;					\
	if r0 == 0 goto 1f;				\
	r0 = r6;					\
	exit;						\
1:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* r1's upper half is dead, so 'w1 = 1' must NOT be marked for zero extension. */
SEC("socket")
__success __log_level(2)
__not_msg("w1 = 1{{.*}}; zext")
__naked void no_zext_other_reg_hi_used(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	r6 <<= 32;					\
	w1 = 1;						\
	r0 = r6;					\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* LD_ABS defines r0; when r0 is read as 64-bit it must be zero extended */
SEC("socket")
__success __log_level(2)
__msg("r0 = *(u8 *)skb[0]{{.*}}; zext")
__naked void zext_ld_abs_hi_used(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r0 = *(u8 *)skb[0];				\
	r7 = r0;					\
	r0 = r7;					\
	exit;						\
"	::: __clobber_all);
}

/* Helper parameters are read as 64-bit (call_use_mask() fallback) */
SEC("socket")
__success __log_level(2)
__msg("w2 = 1{{ +}}; zext")
__naked void helper_param_read_as_64bit(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -8;					\
	w2 = 1;						\
	call %[bpf_trace_printk];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_trace_printk)
	: __clobber_all);
}

static __used __naked int subprog_reads_arg_as_64bit(void)
{
	asm volatile ("					\
	r0 = r1;					\
	exit;						\
"	::: __clobber_all);
}

/* subprogram parameters are conservatively read as 64-bit */
SEC("socket")
__success __log_level(2)
__msg("w1 = w0{{ +}}; zext")
__naked void subprog_param_read_as_64bit(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	call subprog_reads_arg_as_64bit;		\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* kfunc parameters are zero extended */
SEC("tc")
__success __log_level(2)
__msg("w1 = 1{{ +}}; zext")
__msg("w2 = 1{{ +}}; zext")
__msg("w3 = 1{{ +}}; zext")
__msg("w4 = 1{{ +}}; zext")
__naked void kfunc_param_read_per_btf(void)
{
	asm volatile ("					\
	w1 = 1;						\
	w2 = 1;						\
	w3 = 1;						\
	w4 = 1;						\
	call bpf_kfunc_call_test4;			\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__success __log_level(2)
__not_msg("; zext")
__naked void alu32_and_32bit_conditional(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if w1 > 42 goto 1f;		/* BPF_K */	\
	w2 = 28;					\
	if w2 > w1 goto 1f;		/* BPF_X */	\
	r0 = 0;						\
1:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success __log_level(2)
__msg("w1 = w0{{ +}}; zext")
__naked void alu32_and_64bit_conditional(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	w1 = w0;					\
	if r1 > 42 goto 1f;		/* BPF_K */	\
	r2 = 28;					\
	if r2 > r1 goto 1f;		/* BPF_X */	\
	r0 = 0;						\
1:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__success __log_level(2)
__not_msg("; zext")
__naked void alu64_and_conditionals(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	if w1 > 42 goto 1f;		/* BPF_K */	\
	if r1 > 42 goto 1f;		/* BPF_K */	\
	r2 = 28;					\
	if w2 > w1 goto 1f;		/* BPF_X */	\
	if r2 > r1 goto 1f;		/* BPF_X */	\
	r0 = 0;						\
1:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

#ifdef __BPF_FEATURE_ADDR_SPACE_CAST

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__arch_s390x
__xlated("7: w1 = w0")
__xlated("8: w1 = w1")
__xlated("9: w1 += 8")
__xlated("10: w1 = w1")
__xlated("11: w2 = w1")
__xlated("12: w2 = w2")
__xlated("13: *(u64 *)(r1 +0) = r2")
__naked void arena_ptr(void)
{
	asm volatile ("					\
	r1 = %[arena] ll;				\
	r2 = 0;						\
	r3 = 1;						\
	r4 = 0;						\
	r5 = 0;						\
	call %[bpf_arena_alloc_pages];			\
	r1 = addr_space_cast(r0, 0, 1);		/* needs zext */ \
	r1 += 8;				/* needs zext */ \
	r2 = addr_space_cast(r1, 1, 0);		/* needs zext because of BPF_F_NO_USER_CONV */ \
	*(u64 *)(r1 +0) = r2;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_arena_alloc_pages),
	  __imm_addr(arena)
	: __clobber_all);
}

/*
 * Result of a 32-bit cmpxchg is always explicitly zero extended.
 * Check that this holds for arenas (BPF_PROBE_ATOMIC instruction flavor).
 */
SEC("socket")
__success
__xlated("probe r0 = atomic_cmpxchg((u32 *)(r1 +0), r0, r2)")
__xlated("w0 = w0")
__naked void zext_arena_cmpxchg32(void)
{
	asm volatile ("					\
	r9 = %[arena] ll;	/* associate the arena with the program */ \
	r1 = 0;						\
	r1 = addr_space_cast(r1, 0, 1);			\
	r0 = 0;						\
	r2 = 0;						\
	.8byte %[cmpxchg32];				\
	r0 >>= 32;		/* make the upper half live */ \
	exit;						\
"	:
	: __imm_addr(arena),
	  __imm_insn(cmpxchg32,
		     BPF_ATOMIC_OP(BPF_W, BPF_CMPXCHG, BPF_REG_1, BPF_REG_2, 0))
	: __clobber_all);
}

#endif

/* Check if probe mem loads keep their zero extension. */
SEC("socket")
__success __log_level(2)
__arch_s390x
__xlated("3: r1 = *(u64 *)(r0 +0)")
__xlated("4: r2 = *(u32 *)(r0 +0)")
__xlated("5: w2 = w2")
__xlated("6: r3 = *(u16 *)(r0 +0)")
__xlated("7: w3 = w3")
__xlated("8: r4 = *(u8 *)(r0 +0)")
__xlated("9: w4 = w4")
__naked void probe_mem(void)
{
	asm volatile ("					\
	r1 = 0;						\
	r2 = 0;						\
	call %[bpf_rdonly_cast];			\
	r1 = *(u64 *)(r0 + 0);	/* BPF_PROBE_MEM */	\
	r2 = *(u32 *)(r0 + 0);	/* BPF_PROBE_MEM */	\
	r3 = *(u16 *)(r0 + 0);	/* BPF_PROBE_MEM */	\
	r4 = *(u8 *)(r0 + 0);	/* BPF_PROBE_MEM */	\
	r0 = r1;		/* make the registers used */ \
	r0 += r2;					\
	r0 += r3;					\
	r0 += r4;					\
1:	exit;						\
"	:
	: __imm(bpf_rdonly_cast)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
