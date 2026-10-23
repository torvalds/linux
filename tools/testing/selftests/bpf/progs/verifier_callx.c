// SPDX-License-Identifier: GPL-2.0
/* Tests for callx: indirect calls of bpf subprogs */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../../../include/linux/filter.h"

/* gcc doesn't support indirect calls */
#if defined(__clang__) && (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64) || \
    (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64))

#define CALLX_INSN(DST, SRC, OFF, IMM) \
	BPF_RAW_INSN(BPF_JMP | BPF_CALL | BPF_X, DST, SRC, OFF, IMM)

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, long long);
} map_array SEC(".maps");

struct val_with_lock {
	struct bpf_spin_lock lock;
	int cnt;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct val_with_lock);
} map_lock SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} map_prog SEC(".maps");

__naked __noinline __used
static unsigned long add1(void)
{
	asm volatile (
		"r0 = r1;"
		"r0 += 1;"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long add2(void)
{
	asm volatile (
		"r0 = r1;"
		"r0 += 2;"
		"exit;"
	);
}

/* apply(fn, x) { return fn(x); } */
__naked __noinline __used
static unsigned long apply(void)
{
	asm volatile (
		"r3 = r1;"
		"r1 = r2;"
		"callx r3;"
		"exit;"
	);
}

SEC("socket")
__success __retval(6)
__naked void callx_basic(void)
{
	asm volatile (
		"r1 = 5;"
		"r2 = %[add1] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(add1)
		: __clobber_all);
}

/* callx is printed by the verifier log and xlated dump */
SEC("socket")
__success __log_level(2)
__msg("(8d) callx r2")
__xlated("callx r2")
__naked void callx_disasm(void)
{
	asm volatile (
		"r1 = 5;"
		"r2 = %[add1] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(add1)
		: __clobber_all);
}

/* different callees are called by the same callx on different paths */
SEC("socket")
__success __retval(11)
__naked void callx_two_callees(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r6 &= 1;"
		"r2 = %[add1] ll;"
		"if r6 == 0 goto +2;"
		"r2 = %[add2] ll;"
		"r1 = 10;"
		"callx r2;"
		/* add1(10) - 0 or add2(10) - 1 */
		"r0 -= r6;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm_addr(add1),
		  __imm_addr(add2)
		: __clobber_all);
}

/* pointer to a function is passed as an argument */
SEC("socket")
__success __retval(45)
__naked void callx_fn_as_arg(void)
{
	asm volatile (
		"r1 = %[add2] ll;"
		"r2 = 40;"
		"call apply;"
		"r6 = r0;"
		"r1 = %[add1] ll;"
		"r2 = 2;"
		"call apply;"
		"r0 += r6;"
		"exit;"
		:
		: __imm_addr(add1),
		  __imm_addr(add2)
		: __clobber_all);
}

__naked __noinline __used
static unsigned long get_add1(void)
{
	asm volatile (
		"r0 = %[add1] ll;"
		"exit;"
		:
		: __imm_addr(add1)
		: __clobber_all);
}

/* pointer to a function is returned from a subprog and called via r0 */
SEC("socket")
__success __retval(2)
__naked void callx_r0(void)
{
	asm volatile (
		"call get_add1;"
		"r1 = 1;"
		"callx r0;"
		"exit;"
		::: __clobber_all);
}

/* pointer to a function survives spill/fill */
SEC("socket")
__success __retval(9)
__naked void callx_spill_fill(void)
{
	asm volatile (
		"r2 = %[add2] ll;"
		"*(u64 *)(r10 - 8) = r2;"
		"call %[bpf_get_prandom_u32];"
		"r1 = 7;"
		"r9 = *(u64 *)(r10 - 8);"
		"callx r9;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm_addr(add2)
		: __clobber_all);
}

__naked __noinline __used
static unsigned long clobber_callee_saved(void)
{
	asm volatile (
		"r6 = 100;"
		"r7 = 100;"
		"r8 = 100;"
		"r9 = 100;"
		"r0 = r1;"
		"exit;"
	);
}

/* r6-r9 are preserved across callx */
SEC("socket")
__success __retval(11)
__naked void callx_callee_saved_regs(void)
{
	asm volatile (
		"r6 = 1;"
		"r7 = 2;"
		"r8 = 3;"
		"r9 = 4;"
		"r1 = 1;"
		"r2 = %[clobber_callee_saved] ll;"
		"callx r2;"
		"r0 += r6;"
		"r0 += r7;"
		"r0 += r8;"
		"r0 += r9;"
		"exit;"
		:
		: __imm_addr(clobber_callee_saved)
		: __clobber_all);
}

/* r1-r5 are scratched by callx */
SEC("socket")
__failure __msg("R1 !read_ok")
__naked void callx_scratches_args(void)
{
	asm volatile (
		"r1 = 5;"
		"r2 = %[add1] ll;"
		"callx r2;"
		"r0 = r1;"
		"exit;"
		:
		: __imm_addr(add1)
		: __clobber_all);
}

/* return value of the callee is tracked */
SEC("socket")
__success __log_level(2)
__msg("to caller at 4:")
__msg("R0=6")
__naked void callx_retval_is_tracked(void)
{
	asm volatile (
		"r1 = 5;"
		"r2 = %[add1] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(add1)
		: __clobber_all);
}

__naked __noinline __used
static unsigned long write42(void)
{
	asm volatile (
		"r2 = 42;"
		"*(u64 *)(r1 + 0) = r2;"
		"r0 = 0;"
		"exit;"
	);
}

/* callee writes into the stack of the caller */
SEC("socket")
__success __retval(42)
__naked void callx_callee_writes_caller_stack(void)
{
	asm volatile (
		"r1 = 0;"
		"*(u64 *)(r10 - 8) = r1;"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = %[write42] ll;"
		"callx r2;"
		"r0 = *(u64 *)(r10 - 8);"
		"exit;"
		:
		: __imm_addr(write42)
		: __clobber_all);
}

SEC("socket")
__failure __msg("R1 has type scalar, expected func")
__naked void callx_scalar(void)
{
	asm volatile (
		"r1 = 0;"
		"callx r1;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__failure __msg("R2 !read_ok")
__naked void callx_uninit_reg(void)
{
	asm volatile (
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__failure __msg("R10 has type fp, expected func")
__naked void callx_fp(void)
{
	asm volatile (
		"callx r10;"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__failure __msg("R1 has type map_value, expected func")
__naked void callx_map_value(void)
{
	asm volatile (
		"r1 = %[map_array] ll;"
		"r2 = r10;"
		"r2 += -4;"
		"r3 = 0;"
		"*(u32 *)(r2 + 0) = r3;"
		"call %[bpf_map_lookup_elem];"
		"if r0 == 0 goto 1f;"
		"r1 = r0;"
		"callx r1;"
	"1:"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_map_lookup_elem),
		  __imm_addr(map_array)
		: __clobber_all);
}

/* the address of a subprog can't be modified before the call */
SEC("socket")
__failure __msg("dereference of modified func ptr R2 off=8 disallowed")
__naked void callx_modified_ptr(void)
{
	asm volatile (
		"r1 = 5;"
		"r2 = %[add1] ll;"
		"r2 += 8;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(add1)
		: __clobber_all);
}

SEC("socket")
__failure __msg("variable func access var_off=")
__naked void callx_variable_ptr(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r0 &= 8;"
		"r2 = %[add1] ll;"
		"r2 += r0;"
		"r1 = 5;"
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm_addr(add1)
		: __clobber_all);
}

__noinline __used
int global_add3(int x)
{
	return x + 3;
}

/* only static subprogs can be called via callx */
SEC("socket")
__failure __msg("callback function not static")
__naked void callx_global_func(void)
{
	asm volatile (
		"r1 = 5;"
		"r2 = %[global_add3] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(global_add3)
		: __clobber_all);
}

#define DEFINE_CALLX_RESERVED_FIELDS_PROG(NAME, SRC_REG, OFF, IMM)		\
	SEC("socket")								\
	__failure __msg("BPF_CALL|BPF_X uses reserved fields")			\
	__naked void callx_reserved_field_ ## NAME(void)			\
	{									\
		asm volatile (							\
			"r1 = 5;"						\
			"r2 = %[add1] ll;"					\
			".8byte %[callx_r2];"					\
			"exit;"							\
			:							\
			: __imm_addr(add1),					\
			  __imm_insn(callx_r2, CALLX_INSN(BPF_REG_2, (SRC_REG), (OFF), (IMM))) \
			: __clobber_all);					\
	}

DEFINE_CALLX_RESERVED_FIELDS_PROG(src_reg, BPF_REG_1, 0, 0)
DEFINE_CALLX_RESERVED_FIELDS_PROG(off, BPF_REG_0, 1, 0)
DEFINE_CALLX_RESERVED_FIELDS_PROG(imm, BPF_REG_0, 0, 1)

SEC("socket")
__failure __msg("unknown opcode 8e")
__naked void callx_jmp32(void)
{
	asm volatile (
		"r1 = 5;"
		"r2 = %[add1] ll;"
		".8byte %[callx32_r2];"
		"exit;"
		:
		: __imm_addr(add1),
		  __imm_insn(callx32_r2,
			     BPF_RAW_INSN(BPF_JMP32 | BPF_CALL | BPF_X, BPF_REG_2, 0, 0, 0))
		: __clobber_all);
}

/* similar to calls of static subprogs callx is allowed under a lock */
SEC("tc")
__success __retval(3)
__naked void callx_under_lock(void)
{
	asm volatile (
		"r1 = 0;"
		"*(u32 *)(r10 - 4) = r1;"
		"r2 = r10;"
		"r2 += -4;"
		"r1 = %[map_lock] ll;"
		"call %[bpf_map_lookup_elem];"
		"if r0 != 0 goto 1f;"
		"exit;"
	"1:"
		"r6 = r0;"
		"r1 = r6;"
		"call %[bpf_spin_lock];"
		"r1 = 1;"
		"r2 = %[add2] ll;"
		"callx r2;"
		"r7 = r0;"
		"r1 = r6;"
		"call %[bpf_spin_unlock];"
		"r0 = r7;"
		"exit;"
		:
		: __imm(bpf_map_lookup_elem),
		  __imm(bpf_spin_lock),
		  __imm(bpf_spin_unlock),
		  __imm_addr(map_lock),
		  __imm_addr(add2)
		: __clobber_all);
}

/* helpers are still not allowed under a lock in the callee */
__naked __noinline __used
static unsigned long call_helper(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

SEC("tc")
__failure __msg("function calls are not allowed while holding a lock")
__naked void callx_helper_under_lock(void)
{
	asm volatile (
		"r1 = 0;"
		"*(u32 *)(r10 - 4) = r1;"
		"r2 = r10;"
		"r2 += -4;"
		"r1 = %[map_lock] ll;"
		"call %[bpf_map_lookup_elem];"
		"if r0 != 0 goto 1f;"
		"exit;"
	"1:"
		"r6 = r0;"
		"r1 = r6;"
		"call %[bpf_spin_lock];"
		"r2 = %[call_helper] ll;"
		"callx r2;"
		"r1 = r6;"
		"call %[bpf_spin_unlock];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_map_lookup_elem),
		  __imm(bpf_spin_lock),
		  __imm(bpf_spin_unlock),
		  __imm_addr(map_lock),
		  __imm_addr(call_helper)
		: __clobber_all);
}

/* self(fn) { return fn(fn); } */
__naked __noinline __used
static unsigned long self(void)
{
	asm volatile (
		"callx r1;"
		"exit;"
	);
}

/* unbounded recursion is caught by the main verification pass */
SEC("socket")
__failure __msg("frames is too deep")
__naked void callx_unbounded_recursion(void)
{
	asm volatile (
		"r1 = %[self] ll;"
		"call self;"
		"exit;"
		:
		: __imm_addr(self)
		: __clobber_all);
}

/* countdown(fn, n) { return n ? fn(fn, n - 1) : 0; } */
__naked __noinline __used
static unsigned long countdown(void)
{
	asm volatile (
		"r0 = 0;"
		"if r2 == 0 goto 1f;"
		"r2 += -1;"
		"callx r1;"
	"1:"
		"exit;"
	);
}

/*
 * The depth of the recursion is known to the main verification pass,
 * but recursive calls are not allowed regardless.
 */
SEC("socket")
__failure __msg("recursive call from countdown() to countdown()")
__naked void callx_bounded_recursion(void)
{
	asm volatile (
		"r1 = %[countdown] ll;"
		"r2 = 2;"
		"call countdown;"
		"exit;"
		:
		: __imm_addr(countdown)
		: __clobber_all);
}

/* ping(fn1, fn2, n) { return n ? fn2(fn2, fn1, n - 1) : 0; } */
__naked __noinline __used
static unsigned long ping(void)
{
	asm volatile (
		"r0 = 0;"
		"if r3 == 0 goto 1f;"
		"r3 += -1;"
		"r4 = r1;"
		"r1 = r2;"
		"r2 = r4;"
		"callx r1;"
	"1:"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long pong(void)
{
	asm volatile (
		"r0 = 0;"
		"if r3 == 0 goto 1f;"
		"r3 += -1;"
		"r4 = r1;"
		"r1 = r2;"
		"r2 = r4;"
		"callx r1;"
	"1:"
		"exit;"
	);
}

SEC("socket")
__failure __msg("recursive call from")
__naked void callx_mutual_recursion(void)
{
	asm volatile (
		"r1 = %[ping] ll;"
		"r2 = %[pong] ll;"
		"r3 = 3;"
		"call ping;"
		"exit;"
		:
		: __imm_addr(ping),
		  __imm_addr(pong)
		: __clobber_all);
}

__naked __noinline __used
static unsigned long use_stack_304(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 304) = r0;"
		"exit;"
	);
}


/* Four 480-byte frames, deeper than any budget together with their caller */
__naked __noinline __used
static unsigned long use_stack_480_0(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 480) = r0;"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long use_stack_480_1(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 480) = r0;"
		"call use_stack_480_0;"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long use_stack_480_2(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 480) = r0;"
		"call use_stack_480_1;"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long use_stack_480_3(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 480) = r0;"
		"call use_stack_480_2;"
		"exit;"
	);
}

/* stack of the callee of callx is accounted */
SEC("socket")
__failure __msg("combined stack size of {{[0-9]+}} calls is")
__naked void callx_stack_depth(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 480) = r0;"
		"r2 = %[use_stack_480_3] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(use_stack_480_3)
		: __clobber_all);
}

/* apply_stack_304(fn) { char buf[304]; return fn(); } */
__naked __noinline __used
static unsigned long apply_stack_304(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 304) = r0;"
		"callx r1;"
		"exit;"
	);
}

/*
 * The address of use_stack_480_3() is taken by the main prog that doesn't
 * use stack, but it is called from apply_stack_304().
 */
SEC("socket")
__failure __msg("combined stack size of {{[0-9]+}} calls is")
__naked void callx_stack_depth_nested(void)
{
	asm volatile (
		"r1 = %[use_stack_480_3] ll;"
		"call apply_stack_304;"
		"exit;"
		:
		: __imm_addr(use_stack_480_3)
		: __clobber_all);
}

SEC("socket")
__success __retval(0)
__naked void callx_stack_depth_ok(void)
{
	asm volatile (
		"r0 = 0;"
		"*(u64 *)(r10 - 200) = r0;"
		"r2 = %[use_stack_304] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(use_stack_304)
		: __clobber_all);
}

__naked __noinline __used
static unsigned long do_tail_call(void)
{
	asm volatile (
		"r2 = %[map_prog] ll;"
		"r3 = 0;"
		"call %[bpf_tail_call];"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_tail_call),
		  __imm_addr(map_prog)
		: __clobber_all);
}

SEC("socket")
__failure __msg("tail_calls are not allowed in functions called via callx")
__naked void callx_tail_call_in_callee(void)
{
	asm volatile (
		"r2 = %[do_tail_call] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm_addr(do_tail_call)
		: __clobber_all);
}

/* tail call in the caller of callx is fine */
SEC("socket")
__success __retval(3)
__naked void callx_tail_call_in_caller(void)
{
	asm volatile (
		"r6 = r1;"
		"r1 = 2;"
		"r2 = %[add1] ll;"
		"callx r2;"
		"r7 = r0;"
		"r1 = r6;"
		"r2 = %[map_prog] ll;"
		"r3 = 0;"
		"call %[bpf_tail_call];"
		"r0 = r7;"
		"exit;"
		:
		: __imm(bpf_tail_call),
		  __imm_addr(map_prog),
		  __imm_addr(add1)
		: __clobber_all);
}

/* read_idx(p) { return ((char *)map_value)[*p]; } */
__naked __noinline __used
static unsigned long read_idx(void)
{
	asm volatile (
		"r6 = *(u64 *)(r1 + 0);"
		"r1 = 0;"
		"*(u32 *)(r10 - 4) = r1;"
		"r2 = r10;"
		"r2 += -4;"
		"r1 = %[map_array] ll;"
		"call %[bpf_map_lookup_elem];"
		"if r0 == 0 goto 1f;"
		"r0 += r6;"
		"r0 = *(u8 *)(r0 + 0);"
	"1:"
		"exit;"
		:
		: __imm(bpf_map_lookup_elem),
		  __imm_addr(map_array)
		: __clobber_all);
}

/*
 * Stack slots of the caller that might be read by the callee of callx
 * have to be considered alive at the checkpoints before callx and
 * inside of the callee. Otherwise the state with fp-8 == 1000 is pruned
 * and out of bounds access in read_idx() goes unnoticed.
 */
SEC("socket")
__failure __msg("invalid access to map value, value_size=8 off=1000 size=1")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void callx_callee_reads_caller_stack(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r1 = 1000;"
		"*(u64 *)(r10 - 8) = r1;"
		"if r0 == 0 goto 1f;"
		"r1 = 0;"
		"*(u64 *)(r10 - 8) = r1;"
	"1:"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = %[read_idx] ll;"
		"callx r2;"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm_addr(read_idx)
		: __clobber_all);
}

/* in bounds access in read_idx() is fine */
SEC("socket")
__success __retval(0)
__flag(BPF_F_TEST_STATE_FREQ)
__naked void callx_callee_reads_caller_stack_ok(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r1 = 7;"
		"*(u64 *)(r10 - 8) = r1;"
		"if r0 == 0 goto 1f;"
		"r1 = 0;"
		"*(u64 *)(r10 - 8) = r1;"
	"1:"
		"r1 = r10;"
		"r1 += -8;"
		"r2 = %[read_idx] ll;"
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm_addr(read_idx)
		: __clobber_all);
}

/* same as above, but the pointer to the stack is passed through one more frame */
SEC("socket")
__failure __msg("invalid access to map value, value_size=8 off=1000 size=1")
__flag(BPF_F_TEST_STATE_FREQ)
__naked void callx_callee_reads_caller_stack_nested(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r1 = 1000;"
		"*(u64 *)(r10 - 8) = r1;"
		"if r0 == 0 goto 1f;"
		"r1 = 0;"
		"*(u64 *)(r10 - 8) = r1;"
	"1:"
		"r1 = %[read_idx] ll;"
		"r2 = r10;"
		"r2 += -8;"
		"call apply;"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm_addr(read_idx)
		: __clobber_all);
}

/* registers that are constant before callx are not constant after it */
SEC("socket")
__success __retval(1)
__naked void callx_clobbers_const_regs(void)
{
	asm volatile (
		"r0 = 0;"
		"r1 = 0;"
		"r2 = %[add1] ll;"
		"callx r2;"
		/* dead branch pruning must not assume that r0 is still 0 */
		"if r0 == 0 goto 1f;"
		"r0 = 1;"
		"exit;"
	"1:"
		"r0 = 2;"
		"exit;"
		:
		: __imm_addr(add1)
		: __clobber_all);
}

/* scalar argument passed through callx is tracked precisely */
__naked __noinline __used
static unsigned long identity(void)
{
	asm volatile (
		"r0 = r1;"
		"exit;"
	);
}

long long vals[] SEC(".data.vals") = {1, 2, 3, 4};

SEC("socket")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r0 stack= before 12: (95) exit")
__msg("mark_precise: frame1: regs=r0 stack= before 11: (bf) r0 = r1")
__msg("mark_precise: frame1: regs=r1 stack= before 4: (8d) callx r2")
__msg("mark_precise: frame0: regs=r1 stack= before 3: (bf) r1 = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 3")
__retval(4)
__naked void callx_precision(void)
{
	asm volatile (
		"r2 = %[identity] ll;"
		"r6 = 3;"
		"r1 = r6;"
		"callx r2;"
		"r0 *= 8;"
		"r1 = %[vals] ll;"
		"r1 += r0;"
		"r0 = *(u64 *)(r1 + 0);"
		"exit;"
		:
		: __imm_addr(identity),
		  __imm_addr(vals)
		: __clobber_all);
}

/*
 * The address of a function that is never called is valid. The function is
 * the last one in the program.
 */
SEC("socket")
__success __retval(6)
__naked void callx_addr_of_dead_func(void)
{
	asm volatile (
		"r6 = %[add1] ll;"
		"r7 = %[add2] ll;"
		"r1 = 5;"
		"callx r6;"
		"if r7 != r6 goto +1;"
		"r0 = 0;"
		"exit;"
		:
		: __imm_addr(add1), __imm_addr(add2)
		: __clobber_all);
}

/* and when it's followed by another function */
SEC("socket")
__success __retval(7)
__naked void callx_addr_of_dead_func_first(void)
{
	asm volatile (
		"r7 = %[add1] ll;"
		"r6 = %[add2] ll;"
		"r1 = 5;"
		"callx r6;"
		"if r7 != r6 goto +1;"
		"r0 = 0;"
		"exit;"
		:
		: __imm_addr(add1), __imm_addr(add2)
		: __clobber_all);
}

/* the same without callx */
SEC("socket")
__success __retval(6)
__naked void call_addr_of_dead_func(void)
{
	asm volatile (
		"r1 = 5;"
		"call add1;"
		"r7 = %[add2] ll;"
		"exit;"
		:
		: __imm_addr(add2)
		: __clobber_all);
}

/* function pointers in C */

typedef int (*op_fn)(int);

static __noinline int mul3(int x)
{
	return x * 3;
}

static __noinline int sub7(int x)
{
	return x - 7;
}

static __noinline int apply_op(op_fn op, int x)
{
	return op(x);
}

SEC("socket")
__success __retval(36)
int callx_c_fn_as_arg(void *ctx)
{
	/* (5 * 3) + (28 - 7) */
	return apply_op(mul3, 5) + apply_op(sub7, 28);
}

SEC("socket")
__success __retval(30)
int callx_c_select(void *ctx)
{
	__u32 rnd = bpf_get_prandom_u32() & 1;
	op_fn op = rnd ? mul3 : sub7;
	int x = rnd ? 10 : 37;

	/* 10 * 3 or 37 - 7 */
	return op(x);
}

struct ops {
	op_fn first;
	op_fn second;
	int bias;
};

static __noinline int run_ops(const struct ops *ops, int x)
{
	return ops->second(ops->first(x)) + ops->bias;
}

SEC("socket")
__success __retval(100)
int callx_c_ops_on_stack(void *ctx)
{
	struct ops a, b;

	/* avoid an initializer with function pointers in .rodata */
	a.first = mul3;
	a.second = sub7;
	a.bias = 10;
	b.first = sub7;
	b.second = mul3;
	b.bias = 1;

	/* ((7 * 3) - 7 + 10) + ((32 - 7) * 3 + 1) */
	return run_ops(&a, 7) + run_ops(&b, 32);
}

#else

SEC("socket")
__success
int dummy(void *ctx)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
