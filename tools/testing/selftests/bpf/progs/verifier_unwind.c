// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

/*
 * llvm lowers unwinding to two insns:
 *   invoke foo unwind pad    call foo; unwind_goto pad
 *   resume                   unwind_exit
 * The tests spell them out as raw insns, so they don't depend on llvm version.
 * The argument of unwind_goto() is the number of insns to skip.
 */
#define unwind_goto(off) ".8byte %[insn_unwind_goto_" #off "];"
#define unwind_exit ".8byte %[insn_unwind_exit];"

#define __imm_unwind_goto(off) \
	__imm_insn(insn_unwind_goto_##off, \
		   BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, BPF_UNWIND_GOTO, off, 0))
#define __imm_unwind_exit \
	__imm_insn(insn_unwind_exit, \
		   BPF_RAW_INSN(BPF_JMP | BPF_EXIT, 0, BPF_UNWIND_EXIT, 0, 0))

extern void bpf_rcu_read_lock(void) __ksym;
extern void bpf_rcu_read_unlock(void) __ksym;
extern void bpf_throw(__u64 cookie) __ksym;

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} prog_array SEC(".maps");

struct timer_elem {
	struct bpf_timer t;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct timer_elem);
} timer_map SEC(".maps");

/* The verifier doesn't know what is in there */
unsigned long zero;
unsigned long one = 1;

/*
 * The verifier follows a static subprog, so it knows which way it leaves when
 * it knows the argument. To have both ways at run time the tests pass skb->len,
 * which is 64, or skb->len - 64.
 *
 * r1 == 0: returns 7, otherwise unwinds with r0 = 5
 */
__naked __noinline __used
static unsigned long may_unwind(void)
{
	asm volatile (
		"if r1 != 0 goto 1f;"
		"r0 = 7;"
		"exit;"
	"1:"
		"r0 = 5;"
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

/* owns nothing: its pad only carries on */
__naked __noinline __used
static unsigned long passes_on(void)
{
	asm volatile (
		"call may_unwind;"
		unwind_goto(2)
		"r0 += 1;"
		"exit;"
		unwind_exit
		:
		: __imm_unwind_goto(2),
		  __imm_unwind_exit
	);
}

__naked __noinline __used
static unsigned long always_unwinds(void)
{
	asm volatile (
		"r0 = 5;"
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

__naked __noinline __used
static unsigned long never_unwinds(void)
{
	asm volatile (
		"r0 = 7;"
		"exit;"
	);
}

/* Both can unwind. This one returns 7. */
__naked __noinline __used
unsigned long global_returns(void)
{
	asm volatile (
		"r1 = %[zero] ll;"
		"r1 = *(u64 *)(r1 + 0);"
		"if r1 != 0 goto 1f;"
		"r0 = 7;"
		"exit;"
	"1:"
		"r0 = 5;"
		unwind_exit
		:
		: __imm_addr(zero),
		  __imm_unwind_exit
	);
}

/* This one unwinds with r0 = 5 */
__naked __noinline __used
unsigned long global_unwinds(void)
{
	asm volatile (
		"r1 = %[one] ll;"
		"r1 = *(u64 *)(r1 + 0);"
		"if r1 != 0 goto 1f;"
		"r0 = 7;"
		"exit;"
	"1:"
		"r0 = 5;"
		unwind_exit
		:
		: __imm_addr(one),
		  __imm_unwind_exit
	);
}

SEC("tc")
__description("unwind: the callee returns")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__naked int callee_returns(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"r1 -= 64;"
		"call may_unwind;"
		unwind_goto(1)
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: the callee unwinds")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(42)
__naked int callee_unwinds(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"call may_unwind;"
		unwind_goto(1)
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: return through two frames")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(8)
__naked int two_frames_return(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"r1 -= 64;"
		"call passes_on;"
		unwind_goto(1)
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

/* unwind_exit returns r0 like exit does */
SEC("tc")
__description("unwind: unwind through two frames, r0 reaches the pad")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(15)
__naked int two_frames_unwind(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"call passes_on;"
		unwind_goto(1)
		"exit;"
		"r0 += 10;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: r6-r9 and the stack are intact in the pad")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(1111)
__naked int pad_sees_callee_saved(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"r6 = 1;"
		"r7 = 10;"
		"r8 = 100;"
		"r9 = 400;"
		"*(u64 *)(r10 - 8) = 600;"
		"call passes_on;"
		unwind_goto(2)
		"r0 = 0;"
		"exit;"
		"r0 = r6;"
		"r0 += r7;"
		"r0 += r8;"
		"r0 += r9;"
		"r1 = *(u64 *)(r10 - 8);"
		"r0 += r1;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(2)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: r1-r5 are not readable in the pad")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("R3 !read_ok")
__naked int pad_reads_r3(void)
{
	asm volatile (
		"r3 = 0;"
		"call always_unwinds;"
		unwind_goto(1)
		"exit;"
		"r0 = r3;"
		"exit;"
		:
		: __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unwind_exit of the main prog is exit")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(3)
__naked int main_prog_unwinds(void)
{
	asm volatile (
		"r0 = 3;"
		unwind_exit
		:
		: __imm_unwind_exit
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unwind_exit of the main prog needs r0")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("R0 !read_ok")
__naked int main_prog_unwinds_no_r0(void)
{
	asm volatile (
		unwind_exit
		:
		: __imm_unwind_exit
		: __clobber_all
	);
}

/*
 * What follows unwind_goto is dead code. unwind_exit still has to find the pad,
 * and the cpu predicts that the callee returns: there is a trap for that.
 */
SEC("tc")
__description("unwind: the callee never returns")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(42)
__xlated("0: call pc+4")
__xlated("1: unwind_goto pc+1")
__xlated("2: goto pc-1")
__xlated("3: r0 = 42")
__xlated("4: exit")
__naked int callee_never_returns(void)
{
	asm volatile (
		"call always_unwinds;"
		unwind_goto(2)
		"r0 = 1;"
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_unwind_goto(2)
		: __clobber_all
	);
}

/* The pad is dead code and goes away together with unwind_goto */
SEC("tc")
__description("unwind: the callee never unwinds")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__xlated("0: call pc+1")
__xlated("1: exit")
__xlated("2: r0 = 7")
__xlated("3: exit")
__naked int callee_never_unwinds(void)
{
	asm volatile (
		"call never_unwinds;"
		unwind_goto(1)
		"exit;"
		"r0 = *(u64 *)(r0 + 0);"
		"exit;"
		:
		: __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: the pad drops what the frame owns")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(42)
__naked int pad_unlocks(void)
{
	asm volatile (
		"r6 = *(u32 *)(r1 + %[len]);"
		"call %[bpf_rcu_read_lock];"
		"r1 = r6;"
		"call may_unwind;"
		unwind_goto(3)
		"call %[bpf_rcu_read_unlock];"
		"r0 = 0;"
		"exit;"
		"call %[bpf_rcu_read_unlock];"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm(bpf_rcu_read_lock),
		  __imm(bpf_rcu_read_unlock),
		  __imm_unwind_goto(3)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: the pad is checked like any other code")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("in main prog cannot be used inside bpf_rcu_read_lock-ed region")
__naked int pad_does_not_unlock(void)
{
	asm volatile (
		"r6 = *(u32 *)(r1 + %[len]);"
		"call %[bpf_rcu_read_lock];"
		"r1 = r6;"
		"call may_unwind;"
		unwind_goto(3)
		"call %[bpf_rcu_read_unlock];"
		"r0 = 0;"
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm(bpf_rcu_read_lock),
		  __imm(bpf_rcu_read_unlock),
		  __imm_unwind_goto(3)
		: __clobber_all
	);
}

/* Leaves either way with the same registers */
__naked __noinline __used
static unsigned long same_regs(void)
{
	asm volatile (
		"r0 = 0;"
		"if r1 != 0 goto 1f;"
		"exit;"
	"1:"
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

/*
 * After the call the two states differ only in the way the callee left.
 * The second one must not be pruned, or nobody looks at the pad.
 */
SEC("tc")
__description("unwind: the way the callee left is a part of the state")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("R0 invalid mem access 'scalar'")
__flag(BPF_F_TEST_STATE_FREQ)
__naked int no_pruning_after_the_call(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"call same_regs;"
		unwind_goto(1)
		"exit;"
		"r0 = *(u64 *)(r0 + 0);"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

/* Returns ctx, or unwinds with a number in r0 */
__naked __noinline __used
static unsigned long ctx_or_unwind(void)
{
	asm volatile (
		"r2 = *(u32 *)(r1 + %[len]);"
		"if r2 > 100 goto 1f;"
		"r0 = r1;"
		"exit;"
	"1:"
		"r0 = 0;"
		unwind_exit
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_exit
	);
}

/*
 * unwind_exit changes its return address, so the cpu mispredicts the return and
 * runs what follows unwind_goto with r0 of unwind_exit.
 */
SEC("socket")
__description("unwind: what follows unwind_goto runs speculatively")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(50) /* 64 without the ethernet header */
__success_unpriv __retval_unpriv(50)
__caps_unpriv(CAP_BPF)
#ifdef SPEC_V1
__xlated_unpriv("unwind_goto pc+{{.*}}")
__xlated_unpriv("nospec") /* inserted to prevent `R0 invalid mem access 'scalar'` */
__xlated_unpriv("r0 = *(u32 *)(r0 +{{.*}})")
#endif
__naked int speculative_fall_through(void)
{
	asm volatile (
		"call ctx_or_unwind;"
		unwind_goto(2)
		"r0 = *(u32 *)(r0 + %[len]);"
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(2)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: a global subprog returns")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__naked int calls_global_returns(void)
{
	asm volatile (
		"call global_returns;"
		unwind_goto(1)
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: a global subprog unwinds")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(15)
__naked int calls_global_unwinds(void)
{
	asm volatile (
		"call global_unwinds;"
		unwind_goto(1)
		"exit;"
		"r0 += 10;"
		"exit;"
		:
		: __imm_unwind_goto(1)
		: __clobber_all
	);
}

/* The verifier doesn't look inside */
SEC("tc")
__description("unwind: the pad of a call of a global subprog is always checked")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("R0 invalid mem access 'scalar'")
__naked int global_pad_is_checked(void)
{
	asm volatile (
		"call global_returns;"
		unwind_goto(1)
		"exit;"
		"r0 = *(u64 *)(r0 + 0);"
		"exit;"
		:
		: __imm_unwind_goto(1)
		: __clobber_all
	);
}

__naked __noinline __used
unsigned long global_never_unwinds(void)
{
	asm volatile (
		"r0 = 7;"
		"exit;"
	);
}

SEC("tc")
__description("unwind: a global subprog that cannot unwind")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__naked int calls_global_never_unwinds(void)
{
	asm volatile (
		"call global_never_unwinds;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("unwind: no unwind_goto after a call of a global subprog")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("no unwind_goto after the call of global_returns()")
__naked int global_no_unwind_goto(void)
{
	asm volatile (
		"call global_returns;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("unwind: no unwind_goto after a call of a static subprog")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("unwind_exit, but no unwind_goto after the call at insn 1")
__naked int static_no_unwind_goto(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"call may_unwind;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len))
		: __clobber_all
	);
}

/* The verifier sees that it doesn't get to unwind_exit */
SEC("tc")
__description("unwind: no unwind_goto is fine when the callee doesn't unwind")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__naked int static_does_not_get_there(void)
{
	asm volatile (
		"r1 = 0;"
		"call may_unwind;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("unwind: callx, the callee returns")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__naked int callx_returns(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"r1 -= 64;"
		"r6 = %[may_unwind] ll;"
		"callx r6;"
		unwind_goto(1)
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_addr(may_unwind),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: callx, the callee unwinds")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(42)
__naked int callx_unwinds(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"r6 = %[may_unwind] ll;"
		"callx r6;"
		unwind_goto(1)
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_addr(may_unwind),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unwind_goto after a plain insn")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("unwind_goto at insn 1 doesn't follow a call of a subprog")
__naked int unwind_goto_after_mov(void)
{
	asm volatile (
		"r0 = 0;"
		unwind_goto(0)
		"exit;"
		:
		: __imm_unwind_goto(0)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unwind_goto is the first insn")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("unwind_goto at insn 0 doesn't follow a call of a subprog")
__naked int unwind_goto_first(void)
{
	asm volatile (
		unwind_goto(0)
		"r0 = 0;"
		"exit;"
		:
		: __imm_unwind_goto(0)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unwind_goto after a call of a helper")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("unwind_goto at insn 1 doesn't follow a call of a subprog")
__naked int unwind_goto_after_helper(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		unwind_goto(0)
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm_unwind_goto(0)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unwind_goto after a call of a kfunc")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("unwind_goto at insn 1 doesn't follow a call of a subprog")
__naked int unwind_goto_after_kfunc(void)
{
	asm volatile (
		"call %[bpf_rcu_read_lock];"
		unwind_goto(0)
		"call %[bpf_rcu_read_unlock];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_rcu_read_lock),
		  __imm(bpf_rcu_read_unlock),
		  __imm_unwind_goto(0)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: jump to unwind_goto")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("jump to unwind_goto at insn 3")
__naked int jump_to_unwind_goto(void)
{
	asm volatile (
		"r0 = 0;"
		"if r1 != 0 goto 1f;"
		"call never_unwinds;"
	"1:"
		unwind_goto(0)
		"exit;"
		:
		: __imm_unwind_goto(0)
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: reserved fields of unwind_goto")
__failure __msg("invalid may_goto imm 1")
__naked int unwind_goto_reserved(void)
{
	asm volatile (
		"call never_unwinds;"
		".8byte %[bad];"
		"exit;"
		:
		: __imm_insn(bad, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, BPF_UNWIND_GOTO, 0, 1))
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unknown kind of jcond")
__failure __msg("invalid may_goto imm 0")
__naked int jcond_unknown(void)
{
	asm volatile (
		"call never_unwinds;"
		".8byte %[bad];"
		"exit;"
		:
		: __imm_insn(bad, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, BPF_UNWIND_GOTO + 1, 0, 0))
		: __clobber_all
	);
}

SEC("tc")
__description("unwind: unknown kind of exit")
__failure __msg("BPF_EXIT uses reserved fields")
__naked int exit_unknown(void)
{
	asm volatile (
		"r0 = 0;"
		".8byte %[bad];"
		:
		: __imm_insn(bad, BPF_RAW_INSN(BPF_JMP | BPF_EXIT, 0, BPF_UNWIND_EXIT + 1, 0, 0))
		: __clobber_all
	);
}

/* r1 is the index: unwinds in the second iteration */
__naked __noinline __used
static unsigned long cb_unwinds(void)
{
	asm volatile (
		"r0 = 0;"
		"if r1 != 0 goto 1f;"
		"exit;"
	"1:"
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

SEC("tc")
__description("unwind: callback unwinds")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__failure __msg("callback cannot unwind")
__naked int callback_unwinds(void)
{
	asm volatile (
		"r1 = 3;"
		"r2 = %[cb_unwinds];"
		"r3 = 0;"
		"r4 = 0;"
		"call %[bpf_loop];"
		"exit;"
		:
		: __imm_ptr(cb_unwinds),
		  __imm(bpf_loop)
		: __clobber_common
	);
}

__naked __noinline __used
static unsigned long async_cb_unwinds(void)
{
	asm volatile (
		"r0 = 0;"
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

/* The kernel calls it. It's the top frame like the main prog: unwind_exit is exit. */
SEC("tc")
__description("unwind: async callback unwinds")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(0)
int async_callback_unwinds(void *ctx)
{
	struct timer_elem *val;
	int key = 0;

	val = bpf_map_lookup_elem(&timer_map, &key);
	if (!val)
		return 0;

	bpf_timer_init(&val->t, &timer_map, 0);
	bpf_timer_set_callback(&val->t, (void *)async_cb_unwinds);
	bpf_timer_start(&val->t, 0, 0);
	return 0;
}

/* Not naked: the argument has to have a name in BTF */
__noinline int exception_cb_unwinds(__u64 cookie)
{
	if (cookie)
		asm volatile ("r0 = 7;" unwind_exit :: __imm_unwind_exit : "r0");
	return 0;
}

SEC("tc")
__description("unwind: exception callback unwinds")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__attribute__((btf_decl_tag("exception_callback:exception_cb_unwinds")))
__success __retval(7)
int exception_callback_unwinds(struct __sk_buff *skb)
{
	bpf_throw(skb->len);
	return 0;
}

/* What it calls can unwind as long as it doesn't let it out */
__naked __noinline __used
static unsigned long cb_catches(void)
{
	asm volatile (
		"call may_unwind;"
		unwind_goto(0)
		"r0 = 0;"
		"exit;"
		:
		: __imm_unwind_goto(0)
	);
}

SEC("tc")
__description("unwind: callback catches")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(3)
__naked int callback_catches(void)
{
	asm volatile (
		"r1 = 3;"
		"r2 = %[cb_catches];"
		"r3 = 0;"
		"r4 = 0;"
		"call %[bpf_loop];"
		"exit;"
		:
		: __imm_ptr(cb_catches),
		  __imm(bpf_loop)
		: __clobber_common
	);
}

/* Both leave the subprog like exit does */
__naked __noinline __used
static unsigned long does_tail_call(void)
{
	asm volatile (
		"r6 = *(u32 *)(r1 + %[len]);"
		"r2 = %[prog_array] ll;"
		"r3 = 0;"
		"call %[bpf_tail_call];"
		"r0 = 7;"
		"if r6 == 64 goto 1f;"
		unwind_exit
	"1:"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm(bpf_tail_call),
		  __imm_addr(prog_array),
		  __imm_unwind_exit
	);
}

SEC("tc")
__description("unwind: tail call in a subprog that can unwind")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__naked int tail_call_in_subprog(void)
{
	asm volatile (
		"call does_tail_call;"
		unwind_goto(1)
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_unwind_goto(1)
		: __clobber_all
	);
}

/* The packet is shorter than that: ld_abs leaves the subprog with r0 = 0 */
__naked __noinline __used
static unsigned long does_ld_abs(void)
{
	asm volatile (
		"r6 = r1;"
		"r0 = *(u8 *)skb[1000];"
		"r0 = 5;"
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

SEC("tc")
__description("unwind: ld_abs in a subprog that can unwind")
__arch_x86_64
__arch_arm64
__load_if_JITed()
__success __retval(7)
__naked int ld_abs_in_subprog(void)
{
	asm volatile (
		"call does_ld_abs;"
		unwind_goto(2)
		"r0 += 7;"
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_unwind_goto(2)
		: __clobber_all
	);
}

/*
 * JITs don't emit anything for unwind_goto. It's an extable entry, where
 * unwind_exit looks its return address up.
 * With KASAN x86-64 JIT wraps a load in pushes, a call and pops. 'r2 = 0x1234'
 * is where the patterns start, so that they don't take that call for the one
 * of the subprog.
 */
SEC("tc")
__description("unwind: jit")
__success __retval(42)
__load_if_JITed()
__arch_x86_64
__jited("	movl	$0x1234, %esi")
__jited("...") /* call depth accounting */
__jited("	callq	0x{{.*}}")
__jited("	movl	$0x7, %eax")
__jited("...")
__jited("	movl	$0x5, %eax")
__jited("	pushq	%rax")
__jited("	pushq	%rsi")
__jited("	movq	0x8(%rbp), %rdi")
__jited("...") /* call depth accounting */
__jited("	callq	0x{{.*}}")
__jited("	movq	%rax, 0x8(%rbp)")
__jited("	popq	%rsi")
__jited("	popq	%rax")
__arch_arm64
__jited("	mov	x1, #0x1234")
__jited("	bl	{{.*}}")
__jited("	mov	x8, x0")
__jited("	mov	x8, #0x7")
__jited("...")
__jited("	mov	x8, #0x5")
__jited("	stp	x8, x1, [sp, #-0x10]!")
__jited("	ldr	x0, [x29, #0x8]")
__jited("...")
__jited("	mov	x10, x0")
__jited("	ldp	x8, x1, [sp], #0x10")
__jited("...")
__jited("	add	x30, x30, x10")
__jited("	ret")
__naked int jit(void)
{
	asm volatile (
		"r1 = *(u32 *)(r1 + %[len]);"
		"r2 = 0x1234;"
		"call may_unwind;"
		unwind_goto(2)
		"r0 = 7;"
		"exit;"
		"r0 = 42;"
		"exit;"
		:
		: __imm_const(len, offsetof(struct __sk_buff, len)),
		  __imm_unwind_goto(2)
		: __clobber_all
	);
}

/* r1 == 0: returns 7, otherwise unwinds with r0 = 5 */
__naked __noinline __used
static unsigned long may_unwind_big_stack(void)
{
	asm volatile (
		"*(u64 *)(r10 - 200) = r1;"
		"if r1 != 0 goto 1f;"
		"r0 = 7;"
		"exit;"
	"1:"
		"r0 = 5;"
		unwind_exit
		:
		: __imm_unwind_exit
	);
}

/*
 * x86-64 keeps the pointer to the private stack in r9 and saves it around a call.
 * The value that is read back in the pad is there only when r9 is restored.
 */
SEC("raw_tp")
__description("unwind: private stack, the callee unwinds")
__success __retval(47)
__load_if_JITed()
__arch_x86_64
__jited("	movl	$0x1234, %esi") /* past the load, see above */
__jited("	pushq	%r9")
__jited("...") /* call depth accounting */
__jited("	callq	0x{{.*}}")
__jited("	popq	%r9")
__jited("	jmp	L{{.*}}")
__jited("	popq	%r9")
__jited("	jmp	L{{.*}}")
__arch_arm64
__naked int private_stack_unwinds(void)
{
	asm volatile (
		"*(u64 *)(r10 - 200) = 42;"
		"r1 = %[one] ll;"
		"r1 = *(u64 *)(r1 + 0);"
		"r2 = 0x1234;"
		"call may_unwind_big_stack;"
		unwind_goto(1)
		"exit;"
		"r1 = *(u64 *)(r10 - 200);"
		"r0 += r1;"
		"exit;"
		:
		: __imm_addr(one),
		  __imm_unwind_goto(1)
		: __clobber_all
	);
}

SEC("raw_tp")
__description("unwind: private stack, the callee returns")
__success __retval(49)
__load_if_JITed()
__arch_x86_64
__arch_arm64
__naked int private_stack_returns(void)
{
	asm volatile (
		"*(u64 *)(r10 - 200) = 42;"
		"r1 = %[zero] ll;"
		"r1 = *(u64 *)(r1 + 0);"
		"call may_unwind_big_stack;"
		unwind_goto(3)
		"r1 = *(u64 *)(r10 - 200);"
		"r0 += r1;"
		"exit;"
		"r0 = 0;"
		"exit;"
		:
		: __imm_addr(zero),
		  __imm_unwind_goto(3)
		: __clobber_all
	);
}

/*
 * BTF FUNC records are not generated for kfuncs referenced
 * from inline assembly. These records are necessary for
 * libbpf to link the program. The function below is a hack
 * to ensure that BTF FUNC records are generated.
 */
void __kfunc_btf_root(void)
{
	bpf_rcu_read_lock();
	bpf_rcu_read_unlock();
}

char _license[] SEC("license") = "GPL";
