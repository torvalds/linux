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

char _license[] SEC("license") = "GPL";
