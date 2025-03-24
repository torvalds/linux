// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* Check that precision marks propagate through scalar IDs.
 * Registers r{0,1,2} have the same scalar ID.
 * Range information is propagated for scalars sharing same ID.
 * Check that precision mark for r0 causes precision marks for r{1,2}
 * when range information is propagated for 'if <reg> <op> <const>' insn.
 */
SEC("socket")
__success __log_level(2)
/* first 'if' branch */
__msg("6: (0f) r3 += r0")
__msg("frame0: regs=r0 stack= before 4: (25) if r1 > 0x7 goto pc+0")
__msg("frame0: parent state regs=r0,r1,r2 stack=:")
__msg("frame0: regs=r0,r1,r2 stack= before 3: (bf) r2 = r0")
/* second 'if' branch */
__msg("from 4 to 5: ")
__msg("6: (0f) r3 += r0")
__msg("frame0: regs=r0 stack= before 5: (bf) r3 = r10")
__msg("frame0: regs=r0 stack= before 4: (25) if r1 > 0x7 goto pc+0")
/* parent state already has r{0,1,2} as precise */
__msg("frame0: parent state regs= stack=:")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void linked_regs_bpf_k(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r2.id */
	"r1 = r0;"
	"r2 = r0;"
	"if r1 > 7 goto +0;"
	/* force r0 to be precise, this eventually marks r1 and r2 as
	 * precise as well because of shared IDs
	 */
	"r3 = r10;"
	"r3 += r0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Registers r{0,1,2} share same ID when 'if r1 > ...' insn is processed,
 * check that verifier marks r{1,2} as precise while backtracking
 * 'if r1 > ...' with r0 already marked.
 */
SEC("socket")
__success __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("frame0: regs=r0 stack= before 5: (2d) if r1 > r3 goto pc+0")
__msg("frame0: parent state regs=r0,r1,r2,r3 stack=:")
__msg("frame0: regs=r0,r1,r2,r3 stack= before 4: (b7) r3 = 7")
__naked void linked_regs_bpf_x_src(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r2.id */
	"r1 = r0;"
	"r2 = r0;"
	"r3 = 7;"
	"if r1 > r3 goto +0;"
	/* force r0 to be precise, this eventually marks r1 and r2 as
	 * precise as well because of shared IDs
	 */
	"r4 = r10;"
	"r4 += r0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Registers r{0,1,2} share same ID when 'if r1 > r3' insn is processed,
 * check that verifier marks r{0,1,2} as precise while backtracking
 * 'if r1 > r3' with r3 already marked.
 */
SEC("socket")
__success __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("frame0: regs=r3 stack= before 5: (2d) if r1 > r3 goto pc+0")
__msg("frame0: parent state regs=r0,r1,r2,r3 stack=:")
__msg("frame0: regs=r0,r1,r2,r3 stack= before 4: (b7) r3 = 7")
__naked void linked_regs_bpf_x_dst(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r2.id */
	"r1 = r0;"
	"r2 = r0;"
	"r3 = 7;"
	"if r1 > r3 goto +0;"
	/* force r0 to be precise, this eventually marks r1 and r2 as
	 * precise as well because of shared IDs
	 */
	"r4 = r10;"
	"r4 += r3;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Same as linked_regs_bpf_k, but break one of the
 * links, note that r1 is absent from regs=... in __msg below.
 */
SEC("socket")
__success __log_level(2)
__msg("7: (0f) r3 += r0")
__msg("frame0: regs=r0 stack= before 6: (bf) r3 = r10")
__msg("frame0: parent state regs=r0 stack=:")
__msg("frame0: regs=r0 stack= before 5: (25) if r0 > 0x7 goto pc+0")
__msg("frame0: parent state regs=r0,r2 stack=:")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void linked_regs_broken_link(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r2.id */
	"r1 = r0;"
	"r2 = r0;"
	/* break link for r1, this is the only line that differs
	 * compared to the previous test
	 */
	"r1 = 0;"
	"if r0 > 7 goto +0;"
	/* force r0 to be precise,
	 * this eventually marks r2 as precise because of shared IDs
	 */
	"r3 = r10;"
	"r3 += r0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Check that precision marks propagate through scalar IDs.
 * Use the same scalar ID in multiple stack frames, check that
 * precision information is propagated up the call stack.
 */
SEC("socket")
__success __log_level(2)
__msg("12: (0f) r2 += r1")
/* Current state */
__msg("frame2: last_idx 12 first_idx 11 subseq_idx -1 ")
__msg("frame2: regs=r1 stack= before 11: (bf) r2 = r10")
__msg("frame2: parent state regs=r1 stack=")
__msg("frame1: parent state regs= stack=")
__msg("frame0: parent state regs= stack=")
/* Parent state */
__msg("frame2: last_idx 10 first_idx 10 subseq_idx 11 ")
__msg("frame2: regs=r1 stack= before 10: (25) if r1 > 0x7 goto pc+0")
__msg("frame2: parent state regs=r1 stack=")
/* frame1.r{6,7} are marked because mark_precise_scalar_ids()
 * looks for all registers with frame2.r1.id in the current state
 */
__msg("frame1: parent state regs=r6,r7 stack=")
__msg("frame0: parent state regs=r6 stack=")
/* Parent state */
__msg("frame2: last_idx 8 first_idx 8 subseq_idx 10")
__msg("frame2: regs=r1 stack= before 8: (85) call pc+1")
/* frame1.r1 is marked because of backtracking of call instruction */
__msg("frame1: parent state regs=r1,r6,r7 stack=")
__msg("frame0: parent state regs=r6 stack=")
/* Parent state */
__msg("frame1: last_idx 7 first_idx 6 subseq_idx 8")
__msg("frame1: regs=r1,r6,r7 stack= before 7: (bf) r7 = r1")
__msg("frame1: regs=r1,r6 stack= before 6: (bf) r6 = r1")
__msg("frame1: parent state regs=r1 stack=")
__msg("frame0: parent state regs=r6 stack=")
/* Parent state */
__msg("frame1: last_idx 4 first_idx 4 subseq_idx 6")
__msg("frame1: regs=r1 stack= before 4: (85) call pc+1")
__msg("frame0: parent state regs=r1,r6 stack=")
/* Parent state */
__msg("frame0: last_idx 3 first_idx 1 subseq_idx 4")
__msg("frame0: regs=r1,r6 stack= before 3: (bf) r6 = r0")
__msg("frame0: regs=r0,r1 stack= before 2: (bf) r1 = r0")
__msg("frame0: regs=r0 stack= before 1: (57) r0 &= 255")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void precision_many_frames(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r6.id */
	"r1 = r0;"
	"r6 = r0;"
	"call precision_many_frames__foo;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

static __naked __noinline __used
void precision_many_frames__foo(void)
{
	asm volatile (
	/* conflate one of the register numbers (r6) with outer frame,
	 * to verify that those are tracked independently
	 */
	"r6 = r1;"
	"r7 = r1;"
	"call precision_many_frames__bar;"
	"exit"
	::: __clobber_all);
}

static __naked __noinline __used
void precision_many_frames__bar(void)
{
	asm volatile (
	"if r1 > 7 goto +0;"
	/* force r1 to be precise, this eventually marks:
	 * - bar frame r1
	 * - foo frame r{1,6,7}
	 * - main frame r{1,6}
	 */
	"r2 = r10;"
	"r2 += r1;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Check that scalars with the same IDs are marked precise on stack as
 * well as in registers.
 */
SEC("socket")
__success __log_level(2)
__msg("11: (0f) r2 += r1")
/* foo frame */
__msg("frame1: regs=r1 stack= before 10: (bf) r2 = r10")
__msg("frame1: regs=r1 stack= before 9: (25) if r1 > 0x7 goto pc+0")
__msg("frame1: regs=r1 stack=-8,-16 before 8: (7b) *(u64 *)(r10 -16) = r1")
__msg("frame1: regs=r1 stack=-8 before 7: (7b) *(u64 *)(r10 -8) = r1")
__msg("frame1: regs=r1 stack= before 4: (85) call pc+2")
/* main frame */
__msg("frame0: regs=r1 stack=-8 before 3: (7b) *(u64 *)(r10 -8) = r1")
__msg("frame0: regs=r1 stack= before 2: (bf) r1 = r0")
__msg("frame0: regs=r0 stack= before 1: (57) r0 &= 255")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void precision_stack(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == fp[-8].id */
	"r1 = r0;"
	"*(u64*)(r10 - 8) = r1;"
	"call precision_stack__foo;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

static __naked __noinline __used
void precision_stack__foo(void)
{
	asm volatile (
	/* conflate one of the register numbers (r6) with outer frame,
	 * to verify that those are tracked independently
	 */
	"*(u64*)(r10 - 8) = r1;"
	"*(u64*)(r10 - 16) = r1;"
	"if r1 > 7 goto +0;"
	/* force r1 to be precise, this eventually marks:
	 * - foo frame r1,fp{-8,-16}
	 * - main frame r1,fp{-8}
	 */
	"r2 = r10;"
	"r2 += r1;"
	"exit"
	::: __clobber_all);
}

/* Use two separate scalar IDs to check that these are propagated
 * independently.
 */
SEC("socket")
__success __log_level(2)
/* r{6,7} */
__msg("12: (0f) r3 += r7")
__msg("frame0: regs=r7 stack= before 11: (bf) r3 = r10")
__msg("frame0: regs=r7 stack= before 9: (25) if r7 > 0x7 goto pc+0")
/* ... skip some insns ... */
__msg("frame0: regs=r6,r7 stack= before 3: (bf) r7 = r0")
__msg("frame0: regs=r0,r6 stack= before 2: (bf) r6 = r0")
/* r{8,9} */
__msg("13: (0f) r3 += r9")
__msg("frame0: regs=r9 stack= before 12: (0f) r3 += r7")
/* ... skip some insns ... */
__msg("frame0: regs=r9 stack= before 10: (25) if r9 > 0x7 goto pc+0")
__msg("frame0: regs=r8,r9 stack= before 7: (bf) r9 = r0")
__msg("frame0: regs=r0,r8 stack= before 6: (bf) r8 = r0")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void precision_two_ids(void)
{
	asm volatile (
	/* r6 = random number up to 0xff
	 * r6.id == r7.id
	 */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	"r6 = r0;"
	"r7 = r0;"
	/* same, but for r{8,9} */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	"r8 = r0;"
	"r9 = r0;"
	/* clear r0 id */
	"r0 = 0;"
	/* propagate equal scalars precision */
	"if r7 > 7 goto +0;"
	"if r9 > 7 goto +0;"
	"r3 = r10;"
	/* force r7 to be precise, this also marks r6 */
	"r3 += r7;"
	/* force r9 to be precise, this also marks r8 */
	"r3 += r9;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__success __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
/* check thar r0 and r6 have different IDs after 'if',
 * collect_linked_regs() can't tie more than 6 registers for a single insn.
 */
__msg("8: (25) if r0 > 0x7 goto pc+0         ; R0=scalar(id=1")
__msg("9: (bf) r6 = r6                       ; R6_w=scalar(id=2")
/* check that r{0-5} are marked precise after 'if' */
__msg("frame0: regs=r0 stack= before 8: (25) if r0 > 0x7 goto pc+0")
__msg("frame0: parent state regs=r0,r1,r2,r3,r4,r5 stack=:")
__naked void linked_regs_too_many_regs(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r{0-6} IDs */
	"r1 = r0;"
	"r2 = r0;"
	"r3 = r0;"
	"r4 = r0;"
	"r5 = r0;"
	"r6 = r0;"
	/* propagate range for r{0-6} */
	"if r0 > 7 goto +0;"
	/* make r6 appear in the log */
	"r6 = r6;"
	/* force r0 to be precise,
	 * this would cause r{0-4} to be precise because of shared IDs
	 */
	"r7 = r10;"
	"r7 += r0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
__failure __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("regs=r7 stack= before 5: (3d) if r8 >= r0")
__msg("parent state regs=r0,r7,r8")
__msg("regs=r0,r7,r8 stack= before 4: (25) if r0 > 0x1")
__msg("div by zero")
__naked void linked_regs_broken_link_2(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r7 = r0;"
	"r8 = r0;"
	"call %[bpf_get_prandom_u32];"
	"if r0 > 1 goto +0;"
	/* r7.id == r8.id,
	 * thus r7 precision implies r8 precision,
	 * which implies r0 precision because of the conditional below.
	 */
	"if r8 >= r0 goto 1f;"
	/* break id relation between r7 and r8 */
	"r8 += r8;"
	/* make r7 precise */
	"if r7 == 0 goto 1f;"
	"r0 /= 0;"
"1:"
	"r0 = 42;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Check that mark_chain_precision() for one of the conditional jump
 * operands does not trigger equal scalars precision propagation.
 */
SEC("socket")
__success __log_level(2)
__msg("3: (25) if r1 > 0x100 goto pc+0")
__msg("frame0: regs=r1 stack= before 2: (bf) r1 = r0")
__naked void cjmp_no_linked_regs_trigger(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id */
	"r1 = r0;"
	/* the jump below would be predicted, thus r1 would be marked precise,
	 * this should not imply precision mark for r0
	 */
	"if r1 > 256 goto +0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Verify that check_ids() is used by regsafe() for scalars.
 *
 * r9 = ... some pointer with range X ...
 * r6 = ... unbound scalar ID=a ...
 * r7 = ... unbound scalar ID=b ...
 * if (r6 > r7) goto +1
 * r7 = r6
 * if (r7 > X) goto exit
 * r9 += r6
 * ... access memory using r9 ...
 *
 * The memory access is safe only if r7 is bounded,
 * which is true for one branch and not true for another.
 */
SEC("socket")
__failure __msg("register with unbounded min value")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void check_ids_in_regsafe(void)
{
	asm volatile (
	/* Bump allocated stack */
	"r1 = 0;"
	"*(u64*)(r10 - 8) = r1;"
	/* r9 = pointer to stack */
	"r9 = r10;"
	"r9 += -8;"
	/* r7 = ktime_get_ns() */
	"call %[bpf_ktime_get_ns];"
	"r7 = r0;"
	/* r6 = ktime_get_ns() */
	"call %[bpf_ktime_get_ns];"
	"r6 = r0;"
	/* if r6 > r7 is an unpredictable jump */
	"if r6 > r7 goto l1_%=;"
	"r7 = r6;"
"l1_%=:"
	/* if r7 > 4 ...; transfers range to r6 on one execution path
	 * but does not transfer on another
	 */
	"if r7 > 4 goto l2_%=;"
	/* Access memory at r9[r6], r6 is not always bounded */
	"r9 += r6;"
	"r0 = *(u8*)(r9 + 0);"
"l2_%=:"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Similar to check_ids_in_regsafe.
 * The l0 could be reached in two states:
 *
 *   (1) r6{.id=A}, r7{.id=A}, r8{.id=B}
 *   (2) r6{.id=B}, r7{.id=A}, r8{.id=B}
 *
 * Where (2) is not safe, as "r7 > 4" check won't propagate range for it.
 * This example would be considered safe without changes to
 * mark_chain_precision() to track scalar values with equal IDs.
 */
SEC("socket")
__failure __msg("register with unbounded min value")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void check_ids_in_regsafe_2(void)
{
	asm volatile (
	/* Bump allocated stack */
	"r1 = 0;"
	"*(u64*)(r10 - 8) = r1;"
	/* r9 = pointer to stack */
	"r9 = r10;"
	"r9 += -8;"
	/* r8 = ktime_get_ns() */
	"call %[bpf_ktime_get_ns];"
	"r8 = r0;"
	/* r7 = ktime_get_ns() */
	"call %[bpf_ktime_get_ns];"
	"r7 = r0;"
	/* r6 = ktime_get_ns() */
	"call %[bpf_ktime_get_ns];"
	"r6 = r0;"
	/* scratch .id from r0 */
	"r0 = 0;"
	/* if r6 > r7 is an unpredictable jump */
	"if r6 > r7 goto l1_%=;"
	/* tie r6 and r7 .id */
	"r6 = r7;"
"l0_%=:"
	/* if r7 > 4 exit(0) */
	"if r7 > 4 goto l2_%=;"
	/* Access memory at r9[r6] */
	"r9 += r6;"
	"r0 = *(u8*)(r9 + 0);"
"l2_%=:"
	"r0 = 0;"
	"exit;"
"l1_%=:"
	/* tie r6 and r8 .id */
	"r6 = r8;"
	"goto l0_%=;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Check that scalar IDs *are not* generated on register to register
 * assignments if source register is a constant.
 *
 * If such IDs *are* generated the 'l1' below would be reached in
 * two states:
 *
 *   (1) r1{.id=A}, r2{.id=A}
 *   (2) r1{.id=C}, r2{.id=C}
 *
 * Thus forcing 'if r1 == r2' verification twice.
 */
SEC("socket")
__success __log_level(2)
__msg("11: (1d) if r3 == r4 goto pc+0")
__msg("frame 0: propagating r3,r4")
__msg("11: safe")
__msg("processed 15 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void no_scalar_id_for_const(void)
{
	asm volatile (
	"call %[bpf_ktime_get_ns];"
	/* unpredictable jump */
	"if r0 > 7 goto l0_%=;"
	/* possibly generate same scalar ids for r3 and r4 */
	"r1 = 0;"
	"r1 = r1;"
	"r3 = r1;"
	"r4 = r1;"
	"goto l1_%=;"
"l0_%=:"
	/* possibly generate different scalar ids for r3 and r4 */
	"r1 = 0;"
	"r2 = 0;"
	"r3 = r1;"
	"r4 = r2;"
"l1_%=:"
	/* predictable jump, marks r3 and r4 precise */
	"if r3 == r4 goto +0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Same as no_scalar_id_for_const() but for 32-bit values */
SEC("socket")
__success __log_level(2)
__msg("11: (1e) if w3 == w4 goto pc+0")
__msg("frame 0: propagating r3,r4")
__msg("11: safe")
__msg("processed 15 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void no_scalar_id_for_const32(void)
{
	asm volatile (
	"call %[bpf_ktime_get_ns];"
	/* unpredictable jump */
	"if r0 > 7 goto l0_%=;"
	/* possibly generate same scalar ids for r3 and r4 */
	"w1 = 0;"
	"w1 = w1;"
	"w3 = w1;"
	"w4 = w1;"
	"goto l1_%=;"
"l0_%=:"
	/* possibly generate different scalar ids for r3 and r4 */
	"w1 = 0;"
	"w2 = 0;"
	"w3 = w1;"
	"w4 = w2;"
"l1_%=:"
	/* predictable jump, marks r1 and r2 precise */
	"if w3 == w4 goto +0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Check that unique scalar IDs are ignored when new verifier state is
 * compared to cached verifier state. For this test:
 * - cached state has no id on r1
 * - new state has a unique id on r1
 */
SEC("socket")
__success __log_level(2)
__msg("6: (25) if r6 > 0x7 goto pc+1")
__msg("7: (57) r1 &= 255")
__msg("8: (bf) r2 = r10")
__msg("from 6 to 8: safe")
__msg("processed 12 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void ignore_unique_scalar_ids_cur(void)
{
	asm volatile (
	"call %[bpf_ktime_get_ns];"
	"r6 = r0;"
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* r1.id == r0.id */
	"r1 = r0;"
	/* make r1.id unique */
	"r0 = 0;"
	"if r6 > 7 goto l0_%=;"
	/* clear r1 id, but keep the range compatible */
	"r1 &= 0xff;"
"l0_%=:"
	/* get here in two states:
	 * - first: r1 has no id (cached state)
	 * - second: r1 has a unique id (should be considered equivalent)
	 */
	"r2 = r10;"
	"r2 += r1;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Check that unique scalar IDs are ignored when new verifier state is
 * compared to cached verifier state. For this test:
 * - cached state has a unique id on r1
 * - new state has no id on r1
 */
SEC("socket")
__success __log_level(2)
__msg("6: (25) if r6 > 0x7 goto pc+1")
__msg("7: (05) goto pc+1")
__msg("9: (bf) r2 = r10")
__msg("9: safe")
__msg("processed 13 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void ignore_unique_scalar_ids_old(void)
{
	asm volatile (
	"call %[bpf_ktime_get_ns];"
	"r6 = r0;"
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* r1.id == r0.id */
	"r1 = r0;"
	/* make r1.id unique */
	"r0 = 0;"
	"if r6 > 7 goto l1_%=;"
	"goto l0_%=;"
"l1_%=:"
	/* clear r1 id, but keep the range compatible */
	"r1 &= 0xff;"
"l0_%=:"
	/* get here in two states:
	 * - first: r1 has a unique id (cached state)
	 * - second: r1 has no id (should be considered equivalent)
	 */
	"r2 = r10;"
	"r2 += r1;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Check that two different scalar IDs in a verified state can't be
 * mapped to the same scalar ID in current state.
 */
SEC("socket")
__success __log_level(2)
/* The exit instruction should be reachable from two states,
 * use two matches and "processed .. insns" to ensure this.
 */
__msg("13: (95) exit")
__msg("13: (95) exit")
__msg("processed 18 insns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void two_old_ids_one_cur_id(void)
{
	asm volatile (
	/* Give unique scalar IDs to r{6,7} */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	"r6 = r0;"
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	"r7 = r0;"
	"r0 = 0;"
	/* Maybe make r{6,7} IDs identical */
	"if r6 > r7 goto l0_%=;"
	"goto l1_%=;"
"l0_%=:"
	"r6 = r7;"
"l1_%=:"
	/* Mark r{6,7} precise.
	 * Get here in two states:
	 * - first:  r6{.id=A}, r7{.id=B} (cached state)
	 * - second: r6{.id=A}, r7{.id=A}
	 * Currently we don't want to consider such states equivalent.
	 * Thus "exit;" would be verified twice.
	 */
	"r2 = r10;"
	"r2 += r6;"
	"r2 += r7;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

SEC("socket")
/* Note the flag, see verifier.c:opt_subreg_zext_lo32_rnd_hi32() */
__flag(BPF_F_TEST_RND_HI32)
__success
/* This test was added because of a bug in verifier.c:sync_linked_regs(),
 * upon range propagation it destroyed subreg_def marks for registers.
 * The subreg_def mark is used to decide whether zero extension instructions
 * are needed when register is read. When BPF_F_TEST_RND_HI32 is set it
 * also causes generation of statements to randomize upper halves of
 * read registers.
 *
 * The test is written in a way to return an upper half of a register
 * that is affected by range propagation and must have it's subreg_def
 * preserved. This gives a return value of 0 and leads to undefined
 * return value if subreg_def mark is not preserved.
 */
__retval(0)
/* Check that verifier believes r1/r0 are zero at exit */
__log_level(2)
__msg("4: (77) r1 >>= 32                     ; R1_w=0")
__msg("5: (bf) r0 = r1                       ; R0_w=0 R1_w=0")
__msg("6: (95) exit")
__msg("from 3 to 4")
__msg("4: (77) r1 >>= 32                     ; R1_w=0")
__msg("5: (bf) r0 = r1                       ; R0_w=0 R1_w=0")
__msg("6: (95) exit")
/* Verify that statements to randomize upper half of r1 had not been
 * generated.
 */
__xlated("call unknown")
__xlated("r0 &= 2147483647")
__xlated("w1 = w0")
/* This is how disasm.c prints BPF_ZEXT_REG at the moment, x86 and arm
 * are the only CI archs that do not need zero extension for subregs.
 */
#if !defined(__TARGET_ARCH_x86) && !defined(__TARGET_ARCH_arm64)
__xlated("w1 = w1")
#endif
__xlated("if w0 < 0xa goto pc+0")
__xlated("r1 >>= 32")
__xlated("r0 = r1")
__xlated("exit")
__naked void linked_regs_and_subreg_def(void)
{
	asm volatile (
	"call %[bpf_ktime_get_ns];"
	/* make sure r0 is in 32-bit range, otherwise w1 = w0 won't
	 * assign same IDs to registers.
	 */
	"r0 &= 0x7fffffff;"
	/* link w1 and w0 via ID */
	"w1 = w0;"
	/* 'if' statement propagates range info from w0 to w1,
	 * but should not affect w1->subreg_def property.
	 */
	"if w0 < 10 goto +0;"
	/* r1 is read here, on archs that require subreg zero
	 * extension this would cause zext patch generation.
	 */
	"r1 >>= 32;"
	"r0 = r1;"
	"exit;"
	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
