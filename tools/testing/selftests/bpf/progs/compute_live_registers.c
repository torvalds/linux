// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_arena_common.h"
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} test_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 1);
} arena SEC(".maps");

SEC("socket")
__log_level(2)
__msg(" 0: .......... (b7) r0 = 42")
__msg(" 1: 0......... (bf) r1 = r0")
__msg(" 2: .1........ (bf) r2 = r1")
__msg(" 3: ..2....... (bf) r3 = r2")
__msg(" 4: ...3...... (bf) r4 = r3")
__msg(" 5: ....4..... (bf) r5 = r4")
__msg(" 6: .....5.... (bf) r6 = r5")
__msg(" 7: ......6... (bf) r7 = r6")
__msg(" 8: .......7.. (bf) r8 = r7")
__msg(" 9: ........8. (bf) r9 = r8")
__msg("10: .........9 (bf) r0 = r9")
__msg("11: 0......... (95) exit")
__naked void assign_chain(void)
{
	asm volatile (
		"r0 = 42;"
		"r1 = r0;"
		"r2 = r1;"
		"r3 = r2;"
		"r4 = r3;"
		"r5 = r4;"
		"r6 = r5;"
		"r7 = r6;"
		"r8 = r7;"
		"r9 = r8;"
		"r0 = r9;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("0: .......... (b7) r1 = 7")
__msg("1: .1........ (07) r1 += 7")
__msg("2: .......... (b7) r2 = 7")
__msg("3: ..2....... (b7) r3 = 42")
__msg("4: ..23...... (0f) r2 += r3")
__msg("5: .......... (b7) r0 = 0")
__msg("6: 0......... (95) exit")
__naked void arithmetics(void)
{
	asm volatile (
		"r1 = 7;"
		"r1 += 7;"
		"r2 = 7;"
		"r3 = 42;"
		"r2 += r3;"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

#ifdef CAN_USE_BPF_ST
SEC("socket")
__log_level(2)
__msg("  1: .1........ (07) r1 += -8")
__msg("  2: .1........ (7a) *(u64 *)(r1 +0) = 7")
__msg("  3: .1........ (b7) r2 = 42")
__msg("  4: .12....... (7b) *(u64 *)(r1 +0) = r2")
__msg("  5: .12....... (7b) *(u64 *)(r1 +0) = r2")
__msg("  6: .......... (b7) r0 = 0")
__naked void store(void)
{
	asm volatile (
		"r1 = r10;"
		"r1 += -8;"
		"*(u64 *)(r1 +0) = 7;"
		"r2 = 42;"
		"*(u64 *)(r1 +0) = r2;"
		"*(u64 *)(r1 +0) = r2;"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}
#endif

SEC("socket")
__log_level(2)
__msg("1: ....4..... (07) r4 += -8")
__msg("2: ....4..... (79) r5 = *(u64 *)(r4 +0)")
__msg("3: ....45.... (07) r4 += -8")
__naked void load(void)
{
	asm volatile (
		"r4 = r10;"
		"r4 += -8;"
		"r5 = *(u64 *)(r4 +0);"
		"r4 += -8;"
		"r0 = r5;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("0: .1........ (61) r2 = *(u32 *)(r1 +0)")
__msg("1: ..2....... (d4) r2 = le64 r2")
__msg("2: ..2....... (bf) r0 = r2")
__naked void endian(void)
{
	asm volatile (
		"r2 = *(u32 *)(r1 +0);"
		"r2 = le64 r2;"
		"r0 = r2;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg(" 8: 0......... (b7) r1 = 1")
__msg(" 9: 01........ (db) r1 = atomic64_fetch_add((u64 *)(r0 +0), r1)")
__msg("10: 01........ (c3) lock *(u32 *)(r0 +0) += r1")
__msg("11: 01........ (db) r1 = atomic64_xchg((u64 *)(r0 +0), r1)")
__msg("12: 01........ (bf) r2 = r0")
__msg("13: .12....... (bf) r0 = r1")
__msg("14: 012....... (db) r0 = atomic64_cmpxchg((u64 *)(r2 +0), r0, r1)")
__naked void atomic(void)
{
	asm volatile (
		"r2 = r10;"
		"r2 += -8;"
		"r1 = 0;"
		"*(u64 *)(r2 +0) = r1;"
		"r1 = %[test_map] ll;"
		"call %[bpf_map_lookup_elem];"
		"if r0 == 0 goto 1f;"
		"r1 = 1;"
		"r1 = atomic_fetch_add((u64 *)(r0 +0), r1);"
		".8byte %[add_nofetch];" /* same as "lock *(u32 *)(r0 +0) += r1;" */
		"r1 = xchg_64(r0 + 0, r1);"
		"r2 = r0;"
		"r0 = r1;"
		"r0 = cmpxchg_64(r2 + 0, r0, r1);"
		"1: exit;"
		:
		: __imm(bpf_map_lookup_elem),
		  __imm_addr(test_map),
		  __imm_insn(add_nofetch, BPF_ATOMIC_OP(BPF_W, BPF_ADD, BPF_REG_0, BPF_REG_1, 0))
		: __clobber_all);
}

#ifdef CAN_USE_LOAD_ACQ_STORE_REL

SEC("socket")
__log_level(2)
__msg("2: .12....... (db) store_release((u64 *)(r2 -8), r1)")
__msg("3: .......... (bf) r3 = r10")
__msg("4: ...3...... (db) r4 = load_acquire((u64 *)(r3 -8))")
__naked void atomic_load_acq_store_rel(void)
{
	asm volatile (
		"r1 = 42;"
		"r2 = r10;"
		".8byte %[store_release_insn];" /* store_release((u64 *)(r2 - 8), r1); */
		"r3 = r10;"
		".8byte %[load_acquire_insn];" /* r4 = load_acquire((u64 *)(r3 + 0)); */
		"r0 = r4;"
		"exit;"
		:
		: __imm_insn(store_release_insn,
			     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_2, BPF_REG_1, -8)),
		  __imm_insn(load_acquire_insn,
			     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_4, BPF_REG_3, -8))
		: __clobber_all);
}

#endif /* CAN_USE_LOAD_ACQ_STORE_REL */

SEC("socket")
__log_level(2)
__msg("4: .12....7.. (85) call bpf_trace_printk#6")
__msg("5: 0......7.. (0f) r0 += r7")
__naked void regular_call(void)
{
	asm volatile (
		"r7 = 1;"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = 1;"
		"call %[bpf_trace_printk];"
		"r0 += r7;"
		"exit;"
		:
		: __imm(bpf_trace_printk)
		: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("2: 012....... (25) if r1 > 0x7 goto pc+1")
__msg("3: ..2....... (bf) r0 = r2")
__naked void if1(void)
{
	asm volatile (
		"r0 = 1;"
		"r2 = 2;"
		"if r1 > 0x7 goto +1;"
		"r0 = r2;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("3: 0123...... (2d) if r1 > r3 goto pc+1")
__msg("4: ..2....... (bf) r0 = r2")
__naked void if2(void)
{
	asm volatile (
		"r0 = 1;"
		"r2 = 2;"
		"r3 = 7;"
		"if r1 > r3 goto +1;"
		"r0 = r2;"
		"exit;"
		::: __clobber_all);
}

/* Verifier misses that r2 is alive if jset is not handled properly */
SEC("socket")
__log_level(2)
__msg("2: 012....... (45) if r1 & 0x7 goto pc+1")
__naked void if3_jset_bug(void)
{
	asm volatile (
		"r0 = 1;"
		"r2 = 2;"
		"if r1 & 0x7 goto +1;"
		"exit;"
		"r0 = r2;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("0: .......... (b7) r1 = 0")
__msg("1: .1........ (b7) r2 = 7")
__msg("2: .12....... (25) if r1 > 0x7 goto pc+4")
__msg("3: .12....... (07) r1 += 1")
__msg("4: .12....... (27) r2 *= 2")
__msg("5: .12....... (05) goto pc+0")
__msg("6: .12....... (05) goto pc-5")
__msg("7: .......... (b7) r0 = 0")
__msg("8: 0......... (95) exit")
__naked void loop(void)
{
	asm volatile (
		"r1 = 0;"
		"r2 = 7;"
		"if r1 > 0x7 goto +4;"
		"r1 += 1;"
		"r2 *= 2;"
		"goto +0;"
		"goto -5;"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_trace_printk)
		: __clobber_all);
}

#ifdef CAN_USE_GOTOL
SEC("socket")
__log_level(2)
__msg("2: .123...... (25) if r1 > 0x7 goto pc+2")
__msg("3: ..2....... (bf) r0 = r2")
__msg("4: 0......... (06) gotol pc+1")
__msg("5: ...3...... (bf) r0 = r3")
__msg("6: 0......... (95) exit")
__naked void gotol(void)
{
	asm volatile (
		"r2 = 42;"
		"r3 = 24;"
		"if r1 > 0x7 goto +2;"
		"r0 = r2;"
		"gotol +1;"
		"r0 = r3;"
		"exit;"
		:
		: __imm(bpf_trace_printk)
		: __clobber_all);
}
#endif

SEC("socket")
__log_level(2)
__msg("0: .......... (b7) r1 = 1")
__msg("1: .1........ (e5) may_goto pc+1")
__msg("2: .......... (05) goto pc-3")
__msg("3: .1........ (bf) r0 = r1")
__msg("4: 0......... (95) exit")
__naked void may_goto(void)
{
	asm volatile (
	"1: r1 = 1;"
	".8byte %[may_goto];"
	"goto 1b;"
	"r0 = r1;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id),
	  __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, +1 /* offset */, 0))
	: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("1: 0......... (18) r2 = 0x7")
__msg("3: 0.2....... (0f) r0 += r2")
__naked void ldimm64(void)
{
	asm volatile (
		"r0 = 0;"
		"r2 = 0x7 ll;"
		"r0 += r2;"
		"exit;"
		:
		:: __clobber_all);
}

/* No rules specific for LD_ABS/LD_IND, default behaviour kicks in */
SEC("socket")
__log_level(2)
__msg("2: 0123456789 (30) r0 = *(u8 *)skb[42]")
__msg("3: 012.456789 (0f) r7 += r0")
__msg("4: 012.456789 (b7) r3 = 42")
__msg("5: 0123456789 (50) r0 = *(u8 *)skb[r3 + 0]")
__msg("6: 0......7.. (0f) r7 += r0")
__naked void ldabs(void)
{
	asm volatile (
		"r6 = r1;"
		"r7 = 0;"
		"r0 = *(u8 *)skb[42];"
		"r7 += r0;"
		"r3 = 42;"
		".8byte %[ld_ind];" /* same as "r0 = *(u8 *)skb[r3];" */
		"r7 += r0;"
		"r0 = r7;"
		"exit;"
		:
		: __imm_insn(ld_ind, BPF_LD_IND(BPF_B, BPF_REG_3, 0))
		: __clobber_all);
}


#ifdef __BPF_FEATURE_ADDR_SPACE_CAST
SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__log_level(2)
__msg(" 6: .12345.... (85) call bpf_arena_alloc_pages")
__msg(" 7: 0......... (bf) r1 = addr_space_cast(r0, 0, 1)")
__msg(" 8: .1........ (b7) r2 = 42")
__naked void addr_space_cast(void)
{
	asm volatile (
		"r1 = %[arena] ll;"
		"r2 = 0;"
		"r3 = 1;"
		"r4 = 0;"
		"r5 = 0;"
		"call %[bpf_arena_alloc_pages];"
		"r1 = addr_space_cast(r0, 0, 1);"
		"r2 = 42;"
		"*(u64 *)(r1 +0) = r2;"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_arena_alloc_pages),
		  __imm_addr(arena)
		: __clobber_all);
}
#endif

static __used __naked int aux1(void)
{
	asm volatile (
		"r0 = r1;"
		"r0 += r2;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("0: ....45.... (b7) r1 = 1")
__msg("1: .1..45.... (b7) r2 = 2")
__msg("2: .12.45.... (b7) r3 = 3")
/* Conservative liveness for subprog parameters. */
__msg("3: .12345.... (85) call pc+2")
__msg("4: .......... (b7) r0 = 0")
__msg("5: 0......... (95) exit")
__msg("6: .12....... (bf) r0 = r1")
__msg("7: 0.2....... (0f) r0 += r2")
/* Conservative liveness for subprog return value. */
__msg("8: 0......... (95) exit")
__naked void subprog1(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"call aux1;"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* to retain debug info for BTF generation */
void kfunc_root(void)
{
	bpf_arena_alloc_pages(0, 0, 0, 0, 0);
}

char _license[] SEC("license") = "GPL";
