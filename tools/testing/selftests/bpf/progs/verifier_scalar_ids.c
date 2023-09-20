// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/* Check that precision marks propagate through scalar IDs.
 * Registers r{0,1,2} have the same scalar ID at the moment when r0 is
 * marked to be precise, this mark is immediately propagated to r{1,2}.
 */
SEC("socket")
__success __log_level(2)
__msg("frame0: regs=r0,r1,r2 stack= before 4: (bf) r3 = r10")
__msg("frame0: regs=r0,r1,r2 stack= before 3: (bf) r2 = r0")
__msg("frame0: regs=r0,r1 stack= before 2: (bf) r1 = r0")
__msg("frame0: regs=r0 stack= before 1: (57) r0 &= 255")
__msg("frame0: regs=r0 stack= before 0: (85) call bpf_ktime_get_ns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void precision_same_state(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r2.id */
	"r1 = r0;"
	"r2 = r0;"
	/* force r0 to be precise, this immediately marks r1 and r2 as
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

/* Same as precision_same_state, but mark propagates through state /
 * parent state boundary.
 */
SEC("socket")
__success __log_level(2)
__msg("frame0: last_idx 6 first_idx 5 subseq_idx -1")
__msg("frame0: regs=r0,r1,r2 stack= before 5: (bf) r3 = r10")
__msg("frame0: parent state regs=r0,r1,r2 stack=:")
__msg("frame0: regs=r0,r1,r2 stack= before 4: (05) goto pc+0")
__msg("frame0: regs=r0,r1,r2 stack= before 3: (bf) r2 = r0")
__msg("frame0: regs=r0,r1 stack= before 2: (bf) r1 = r0")
__msg("frame0: regs=r0 stack= before 1: (57) r0 &= 255")
__msg("frame0: parent state regs=r0 stack=:")
__msg("frame0: regs=r0 stack= before 0: (85) call bpf_ktime_get_ns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void precision_cross_state(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r2.id */
	"r1 = r0;"
	"r2 = r0;"
	/* force checkpoint */
	"goto +0;"
	/* force r0 to be precise, this immediately marks r1 and r2 as
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

/* Same as precision_same_state, but break one of the
 * links, note that r1 is absent from regs=... in __msg below.
 */
SEC("socket")
__success __log_level(2)
__msg("frame0: regs=r0,r2 stack= before 5: (bf) r3 = r10")
__msg("frame0: regs=r0,r2 stack= before 4: (b7) r1 = 0")
__msg("frame0: regs=r0,r2 stack= before 3: (bf) r2 = r0")
__msg("frame0: regs=r0 stack= before 2: (bf) r1 = r0")
__msg("frame0: regs=r0 stack= before 1: (57) r0 &= 255")
__msg("frame0: regs=r0 stack= before 0: (85) call bpf_ktime_get_ns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void precision_same_state_broken_link(void)
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
	/* force r0 to be precise, this immediately marks r1 and r2 as
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

/* Same as precision_same_state_broken_link, but with state /
 * parent state boundary.
 */
SEC("socket")
__success __log_level(2)
__msg("frame0: regs=r0,r2 stack= before 6: (bf) r3 = r10")
__msg("frame0: regs=r0,r2 stack= before 5: (b7) r1 = 0")
__msg("frame0: parent state regs=r0,r2 stack=:")
__msg("frame0: regs=r0,r1,r2 stack= before 4: (05) goto pc+0")
__msg("frame0: regs=r0,r1,r2 stack= before 3: (bf) r2 = r0")
__msg("frame0: regs=r0,r1 stack= before 2: (bf) r1 = r0")
__msg("frame0: regs=r0 stack= before 1: (57) r0 &= 255")
__msg("frame0: parent state regs=r0 stack=:")
__msg("frame0: regs=r0 stack= before 0: (85) call bpf_ktime_get_ns")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void precision_cross_state_broken_link(void)
{
	asm volatile (
	/* r0 = random number up to 0xff */
	"call %[bpf_ktime_get_ns];"
	"r0 &= 0xff;"
	/* tie r0.id == r1.id == r2.id */
	"r1 = r0;"
	"r2 = r0;"
	/* force checkpoint, although link between r1 and r{0,2} is
	 * broken by the next statement current precision tracking
	 * algorithm can't react to it and propagates mark for r1 to
	 * the parent state.
	 */
	"goto +0;"
	/* break link for r1, this is the only line that differs
	 * compared to precision_cross_state()
	 */
	"r1 = 0;"
	/* force r0 to be precise, this immediately marks r1 and r2 as
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

/* Check that precision marks propagate through scalar IDs.
 * Use the same scalar ID in multiple stack frames, check that
 * precision information is propagated up the call stack.
 */
SEC("socket")
__success __log_level(2)
__msg("11: (0f) r2 += r1")
/* Current state */
__msg("frame2: last_idx 11 first_idx 10 subseq_idx -1")
__msg("frame2: regs=r1 stack= before 10: (bf) r2 = r10")
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
__msg("frame0: regs=r0,r1,r6 stack= before 3: (bf) r6 = r0")
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
	/* force r1 to be precise, this immediately marks:
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
/* foo frame */
__msg("frame1: regs=r1 stack=-8,-16 before 9: (bf) r2 = r10")
__msg("frame1: regs=r1 stack=-8,-16 before 8: (7b) *(u64 *)(r10 -16) = r1")
__msg("frame1: regs=r1 stack=-8 before 7: (7b) *(u64 *)(r10 -8) = r1")
__msg("frame1: regs=r1 stack= before 4: (85) call pc+2")
/* main frame */
__msg("frame0: regs=r0,r1 stack=-8 before 3: (7b) *(u64 *)(r10 -8) = r1")
__msg("frame0: regs=r0,r1 stack= before 2: (bf) r1 = r0")
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
	/* force r1 to be precise, this immediately marks:
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
__msg("11: (0f) r3 += r7")
__msg("frame0: regs=r6,r7 stack= before 10: (bf) r3 = r10")
/* ... skip some insns ... */
__msg("frame0: regs=r6,r7 stack= before 3: (bf) r7 = r0")
__msg("frame0: regs=r0,r6 stack= before 2: (bf) r6 = r0")
/* r{8,9} */
__msg("12: (0f) r3 += r9")
__msg("frame0: regs=r8,r9 stack= before 11: (0f) r3 += r7")
/* ... skip some insns ... */
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
	/* force checkpoint */
	"goto +0;"
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

char _license[] SEC("license") = "GPL";
