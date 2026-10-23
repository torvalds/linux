// SPDX-License-Identifier: GPL-2.0
/* Tests for callx through pointers to functions in read-only data */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../../../include/linux/filter.h"

/* gcc doesn't support indirect calls */
#if defined(__clang__) && (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64) || \
    (defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64))

/*
 * Read-only data with pointers to functions, where the compiler puts tables
 * of functions, structures of operations and vtables. libbpf resolves
 * a pointer to the offset of the function in the program and the kernel
 * recognizes it by that value.
 */
#define DATA(SECTION, NAME, ...)				\
	".pushsection " SECTION ",@progbits;"			\
	".balign 8;"						\
	#NAME "_%=:"						\
	__VA_ARGS__						\
	".type " #NAME "_%=, @object;"				\
	".size " #NAME "_%=, .-" #NAME "_%=;"			\
	".popsection;"

#define RODATA(NAME, ...) DATA(".rodata,\"a\"", NAME, __VA_ARGS__)

#define FUNC_TABLE2(NAME, F0, F1) RODATA(NAME, ".quad " #F0 "; .quad " #F1 ";")

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

/* the second element of the table is called */
SEC("socket")
__success __retval(12)
__naked void callx_rodata_const_index(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 8);"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

/*
 * The program reads the address of a function from the data, so pointers to
 * functions in data are recognized only for programs that may leak pointers.
 * Otherwise nothing refers to the functions.
 */
SEC("socket")
__success __retval(12)
__failure_unpriv __msg_unpriv("unreachable insn")
__caps_unpriv(CAP_BPF)
__naked void callx_rodata_needs_perfmon(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 8);"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

/* the address of an element is used instead of the address of the table */
SEC("socket")
__success __retval(12)
__naked void callx_rodata_elem_addr(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"r2 = tbl_%= + 8 ll;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

/* pointers to functions are mixed with other data like in a vtable */
SEC("socket")
__success __retval(23)
__log_level(2)
__msg("r1 = *(u64 *)(r6 +0)          ; R1=7")
__msg("r2 = *(u64 *)(r6 +8)          ; R2=func()")
__naked void callx_rodata_mixed_with_data(void)
{
	asm volatile (
		RODATA(vt, ".quad 7; .quad add1; .quad 13; .quad add2;")
		"r6 = vt_%= ll;"
		"r1 = *(u64 *)(r6 + 0);"
		"r2 = *(u64 *)(r6 + 8);"
		/* add1(7) */
		"callx r2;"
		"r7 = r0;"
		"r1 = *(u64 *)(r6 + 16);"
		"r2 = *(u64 *)(r6 + 24);"
		/* add2(13) */
		"callx r2;"
		"r0 += r7;"
		"exit;"
		::: __clobber_all);
}

/*
 * Position independent code keeps constants with pointers in .data.rel.ro.
 * libbpf treats it as read-only data.
 */
SEC("socket")
__success __retval(23)
__naked void callx_data_rel_ro(void)
{
	asm volatile (
		DATA(".data.rel.ro,\"aw\"", vt, ".quad 7; .quad add1; .quad 13; .quad add2;")
		"r6 = vt_%= ll;"
		"r1 = *(u64 *)(r6 + 0);"
		"r2 = *(u64 *)(r6 + 8);"
		"callx r2;"
		"r7 = r0;"
		"r1 = *(u64 *)(r6 + 16);"
		"r2 = *(u64 *)(r6 + 24);"
		"callx r2;"
		"r0 += r7;"
		"exit;"
		::: __clobber_all);
}

/* an array of structures: the index selects one of the functions, not the data */
SEC("socket")
__success __retval(11)
__naked void callx_rodata_array_of_structs(void)
{
	asm volatile (
		RODATA(arr, ".quad add1; .quad 0x1111; .quad add2; .quad 0x2222;")
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r6 &= 1;"
		"r3 = r6;"
		"r3 <<= 4;"
		"r2 = arr_%= ll;"
		"r2 += r3;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 10;"
		"callx r2;"
		/* add1(10) - 0 or add2(10) - 1 */
		"r0 -= r6;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* and the data of the element that the index selects can be read */
SEC("socket")
__success __retval(16)
__naked void callx_rodata_array_of_structs_data(void)
{
	asm volatile (
		RODATA(arr, ".quad add1; .quad 5; .quad add2; .quad 5;")
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r6 &= 1;"
		"r3 = r6;"
		"r3 <<= 4;"
		"r7 = arr_%= ll;"
		"r7 += r3;"
		"r2 = *(u64 *)(r7 + 0);"
		"r1 = 10;"
		"callx r2;"
		/* add1(10) - 0 or add2(10) - 1 */
		"r0 -= r6;"
		"r1 = *(u64 *)(r7 + 8);"
		"r0 += r1;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* but not the data and the pointer with the same load */
SEC("socket")
__failure __msg("overlaps with a pointer to a function")
__naked void callx_rodata_array_of_structs_any(void)
{
	asm volatile (
		RODATA(arr, ".quad add1; .quad 5; .quad add2; .quad 5;")
		"call %[bpf_get_prandom_u32];"
		"r0 &= 3;"
		"r0 <<= 3;"
		"r2 = arr_%= ll;"
		"r2 += r0;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* dynamic dispatch: which vtable is used is not known until run time */
SEC("socket")
__success __retval(2)
__naked void callx_rodata_two_vtables(void)
{
	asm volatile (
		RODATA(vt_a, ".quad 1; .quad add1;")
		RODATA(vt_b, ".quad 0; .quad add2;")
		"call %[bpf_get_prandom_u32];"
		"r6 = vt_a_%= ll;"
		"r0 &= 1;"
		"if r0 == 0 goto +2;"
		"r6 = vt_b_%= ll;"
		"r1 = *(u64 *)(r6 + 0);"
		"r2 = *(u64 *)(r6 + 8);"
		/* add1(1) or add2(0) */
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* both functions are verified, either of them is called */
SEC("socket")
__success __retval(11)
__naked void callx_rodata_var_index(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r6 &= 1;"
		"r3 = r6;"
		"r3 <<= 3;"
		"r2 = tbl_%= ll;"
		"r2 += r3;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 10;"
		"callx r2;"
		/* add1(10) - 0 or add2(10) - 1 */
		"r0 -= r6;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* a table that has no symbol, the compiler generates such for a switch statement */
SEC("socket")
__success __retval(11)
__naked void callx_rodata_no_symbol(void)
{
	asm volatile (
		".pushsection .rodata,\"a\",@progbits;"
		".balign 8;"
	".Lanon_%=:"
		".quad add1;"
		".quad add2;"
		".popsection;"
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r6 &= 1;"
		"r3 = r6;"
		"r3 <<= 3;"
		"r2 = .Lanon_%= ll;"
		"r2 += r3;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 10;"
		"callx r2;"
		"r0 -= r6;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* the first instruction of the function is removed by the verifier */
__naked __noinline __used
static unsigned long nop_add1(void)
{
	asm volatile (
		"goto +0;"
		"r0 = r1;"
		"r0 += 1;"
		"exit;"
	);
}

/* the pointer follows the function when instructions are removed */
SEC("socket")
__success __retval(11)
__naked void callx_rodata_func_starts_with_nop(void)
{
	asm volatile (
		RODATA(tbl, ".quad nop_add1; .quad 0;")
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

/* can't be called with a scalar in r1 */
__naked __noinline __used
static unsigned long deref_r1(void)
{
	asm volatile (
		"r0 = *(u64 *)(r1 + 0);"
		"exit;"
	);
}

__naked __noinline __used
static unsigned long ret0(void)
{
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

/* every function that might be called is verified */
SEC("socket")
__failure __msg("R1 invalid mem access 'scalar'")
__naked void callx_rodata_all_callees_verified(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, ret0, deref_r1)
		"call %[bpf_get_prandom_u32];"
		"r0 &= 1;"
		"r0 <<= 3;"
		"r2 = tbl_%= ll;"
		"r2 += r0;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 0;"
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* a function that is never called is not verified, it's dead code */
SEC("socket")
__success __retval(0)
__naked void callx_rodata_unused_callee(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, ret0, deref_r1)
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 0;"
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

/* a pointer that is read and never called doesn't keep the function, it's NULL */
SEC("socket")
__success __retval(1)
__naked void callx_rodata_read_unused_callee(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, ret0, deref_r1)
		"r6 = tbl_%= ll;"
		"r2 = *(u64 *)(r6 + 0);"
		"r1 = 0;"
		"callx r2;"
		"r2 = *(u64 *)(r6 + 8);"
		"if r2 != 0 goto +1;"
		"r0 = 1;"
		"exit;"
		::: __clobber_all);
}

/* the index may select what is not a pointer to a function */
SEC("socket")
__failure __msg("overlaps with a pointer to a function")
__naked void callx_rodata_index_beyond_table(void)
{
	asm volatile (
		RODATA(tbl, ".quad add1; .quad add2; .quad 0x1234; .quad 0x5678;")
		"call %[bpf_get_prandom_u32];"
		"r0 &= 3;"
		"r0 <<= 3;"
		"r2 = tbl_%= ll;"
		"r2 += r0;"
		"r2 = *(u64 *)(r2 + 0);"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* the address of a function is not known until the program is jitted */
SEC("socket")
__failure __msg("read of 4 bytes at offset")
__msg("overlaps with a pointer to a function")
__naked void callx_rodata_partial_read(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"r6 = tbl_%= ll;"
		"r2 = *(u64 *)(r6 + 8);"
		"r1 = 10;"
		"callx r2;"
		"r0 = *(u32 *)(r6 + 0);"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__failure __msg("read of 8 bytes at offset")
__msg("overlaps with a pointer to a function")
__naked void callx_rodata_misaligned_read(void)
{
	asm volatile (
		RODATA(tbl, ".quad add1; .quad add2; .quad 0;")
		"r6 = tbl_%= ll;"
		"r2 = *(u64 *)(r6 + 0);"
		"r1 = 10;"
		"callx r2;"
		"r0 = *(u64 *)(r6 + 4);"
		"exit;"
		::: __clobber_all);
}

/*
 * The kernel doesn't look for pointers to functions in the data of a program
 * that doesn't have callx. It's a number there.
 */
SEC("socket")
__success
__naked void callx_rodata_no_callx(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"r2 = tbl_%= ll;"
		"r0 = *(u32 *)(r2 + 0);"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

/* the data next to a pointer is still known to the verifier */
SEC("socket")
__success __retval(0x1234)
__log_level(2)
__msg("R0=4660")
__naked void callx_rodata_data_is_const(void)
{
	asm volatile (
		RODATA(tbl, ".quad add1; .quad 0x1234;")
		"r6 = tbl_%= ll;"
		"r2 = *(u64 *)(r6 + 0);"
		"r1 = 10;"
		"callx r2;"
		"r0 = *(u64 *)(r6 + 8);"
		"exit;"
		::: __clobber_all);
}

/*
 * The map has the offset of the function until the program is jitted.
 * bpf_compute_const_regs() must not take it for a constant, otherwise
 * bpf_prune_dead_branches() turns the branch below into the wrong one.
 */
SEC("socket")
__success __retval(11)
__naked void callx_rodata_ptr_is_not_const(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 0);"
		"r0 = 0;"
		/* the offset is less, the address is greater */
		"if r2 < 0x100000 goto 1f;"
		"r1 = 10;"
		"callx r2;"
	"1:"
		"exit;"
		::: __clobber_all);
}

/* the pointer read from the data can't be modified */
SEC("socket")
__failure __msg("dereference of modified func ptr R2 off=8 disallowed")
__naked void callx_rodata_modified_ptr(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, add2)
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 0);"
		"r2 += 8;"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

__noinline __used
int global_add3(int x)
{
	return x + 3;
}

/*
 * A pointer to a global function is not recognized, it's a number. The function
 * is called directly as well, so that libbpf adds it to the program and stores
 * its offset into the table.
 */
SEC("socket")
__failure __msg("R2 has type scalar, expected func")
__naked void callx_rodata_global_func(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, add1, global_add3)
		"r1 = 1;"
		"call global_add3;"
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 8);"
		"r1 = 10;"
		"callx r2;"
		"exit;"
		::: __clobber_all);
}

/* selfcall(n) { return n ? tbl[0](n - 1) : 0; }, where tbl[0] == selfcall */
__naked __noinline __used
static unsigned long selfcall(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, selfcall, ret0)
		"r0 = 0;"
		"if r1 == 0 goto 1f;"
		"r1 += -1;"
		"r2 = tbl_%= ll;"
		"r2 = *(u64 *)(r2 + 0);"
		"callx r2;"
	"1:"
		"exit;"
		::: __clobber_all);
}

SEC("socket")
__failure __msg("recursive call from selfcall() to selfcall()")
__naked void callx_rodata_recursion(void)
{
	asm volatile (
		"r1 = 2;"
		"call selfcall;"
		"exit;"
		::: __clobber_all);
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

/* stack of all possible callees is accounted */
SEC("socket")
__failure __msg("combined stack size of {{[0-9]+}} calls is")
__naked void callx_rodata_stack_depth(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, ret0, use_stack_480_3)
		"r0 = 0;"
		"*(u64 *)(r10 - 304) = r0;"
		"call %[bpf_get_prandom_u32];"
		"r0 &= 1;"
		"r0 <<= 3;"
		"r2 = tbl_%= ll;"
		"r2 += r0;"
		"r2 = *(u64 *)(r2 + 0);"
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

/* stack of a callback that is read from the data is accounted too */
SEC("socket")
__failure __msg("combined stack size of {{[0-9]+}} calls is")
__naked void callx_rodata_callback_stack_depth(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, use_stack_480_3, ret0)
		"r0 = 0;"
		"*(u64 *)(r10 - 304) = r0;"
		"r6 = tbl_%= ll;"
		"r1 = 1;"
		"r2 = *(u64 *)(r6 + 0);"
		"r3 = 0;"
		"r4 = 0;"
		"call %[bpf_loop];"
		"r2 = *(u64 *)(r6 + 8);"
		"r1 = 0;"
		"callx r2;"
		"exit;"
		:
		: __imm(bpf_loop)
		: __clobber_all);
}

static __noinline __used int pull_data(struct __sk_buff *skb)
{
	return bpf_skb_pull_data(skb, 0);
}

static __noinline __used int calls_pull_data(struct __sk_buff *skb)
{
	return pull_data(skb) + 1;
}

__noinline __used int global_calls_pull_data(struct __sk_buff *skb)
{
	return calls_pull_data(skb) + 2;
}

/*
 * check_cfg() explores the functions that the data points to when it sees
 * the address of the data. It must not lose the effects of the functions
 * that call each other: global_calls_pull_data() changes packet data.
 */
SEC("tc")
__failure __msg("R7 invalid mem access 'scalar'")
__naked void callx_rodata_callee_effects(void)
{
	asm volatile (
		FUNC_TABLE2(tbl, pull_data, calls_pull_data)
		"r6 = r1;"
		"r7 = *(u32 *)(r6 + %[data]);"
		"r8 = *(u32 *)(r6 + %[data_end]);"
		"r2 = r7;"
		"r2 += 8;"
		"if r2 > r8 goto 1f;"
		"r1 = r6;"
		"call global_calls_pull_data;"
		"r0 = *(u8 *)(r7 + 0);"
	"1:"
		"r9 = tbl_%= ll;"
		"r2 = *(u64 *)(r9 + 8);"
		"r1 = r6;"
		"callx r2;"
		"r0 = 0;"
		"exit;"
		:
		: __imm_const(data, offsetof(struct __sk_buff, data)),
		  __imm_const(data_end, offsetof(struct __sk_buff, data_end))
		: __clobber_all);
}

/* pointers to functions in C */

typedef int (*op_fn)(int);

#define DEFINE_OP(N) static __noinline int op##N(int x) { return x * (N + 2) + N; }

DEFINE_OP(0)  DEFINE_OP(1)  DEFINE_OP(2)  DEFINE_OP(3)
DEFINE_OP(4)  DEFINE_OP(5)  DEFINE_OP(6)  DEFINE_OP(7)
DEFINE_OP(8)  DEFINE_OP(9)  DEFINE_OP(10) DEFINE_OP(11)
DEFINE_OP(12) DEFINE_OP(13) DEFINE_OP(14) DEFINE_OP(15)

static op_fn const ops[] = {
	op0, op1, op2,  op3,  op4,  op5,  op6,  op7,
	op8, op9, op10, op11, op12, op13, op14, op15,
};

int op_idx = 11;

SEC("socket")
__success __retval(50)
int callx_c_table(void *ctx)
{
	unsigned int i = op_idx;

	if (i >= sizeof(ops) / sizeof(ops[0]))
		return -1;
	/* op11(3) = 3 * 13 + 11 */
	return ops[i](3);
}

SEC("socket")
__success __retval(50)
int callx_c_table_null_check(void *ctx)
{
	unsigned int i = op_idx;
	op_fn op;

	if (i >= sizeof(ops) / sizeof(ops[0]))
		return -1;
	/* the address of a function is not known until the program is jitted */
	op = ops[i];
	if (!op)
		return -2;
	return op(3);
}

/* a structure of operations, where pointers to functions are mixed with data */
struct shape_ops {
	int id;
	op_fn area;
	long scale;
	op_fn perimeter;
};

static const struct shape_ops square_ops = { 1, op1, 10, op2 };
static const struct shape_ops circle_ops = { 2, op3, 20, op1 };

static __noinline int use_shape(const struct shape_ops *ops, int x)
{
	return ops->area(x) * ops->scale + ops->perimeter(ops->id);
}

SEC("socket")
__success __retval(433)
int callx_c_ops_mixed_with_data(void *ctx)
{
	/*
	 * square: op1(2) * 10 + op2(1) = 7 * 10 + 6 = 76
	 * circle: op3(3) * 20 + op1(2) = 18 * 20 + 7 = 367
	 * minus 10, since op_idx is 11
	 */
	return use_shape(&square_ops, 2) + use_shape(&circle_ops, 3) - (op_idx == 11 ? 10 : 0);
}

/* the ops are selected at run time */
SEC("socket")
__success __retval(367)
int callx_c_ops_selected(void *ctx)
{
	const struct shape_ops *ops = op_idx == 11 ? &circle_ops : &square_ops;

	return use_shape(ops, 3);
}

/*
 * clang-22 with -mcpu=v4 drops the lookup table it builds for this switch
 * ("Undefined temporary symbol .Lswitch.table.call_by_switch").
 * Fixed in clang-23 by llvm commit 9fc458a1d95b.
 *
 * https://github.com/llvm/llvm-project/commit/9fc458a1d95b
 */
#if __clang_major__ != 22 || __BPF_CPU_VERSION__ < 4
/* the compiler might turn the switch into a table that has no symbol */
static __noinline int call_by_switch(unsigned int idx, int x)
{
	op_fn op;

	switch (idx) {
	case 0:
		op = op8;
		break;
	case 1:
		op = op9;
		break;
	case 2:
		op = op10;
		break;
	case 3:
		op = op11;
		break;
	case 4:
		op = op12;
		break;
	case 5:
		op = op13;
		break;
	case 6:
		op = op14;
		break;
	case 7:
		op = op15;
		break;
	default:
		return -1;
	}
	return op(x);
}

SEC("socket")
__success __retval(50)
int callx_c_switch_table(void *ctx)
{
	/* op11(3) = 3 * 13 + 11 */
	return call_by_switch(op_idx - 8, 3);
}

#else

SEC("socket")
__skip("clang-22 bug with -mcpu=v4, switch table test cannot be built")
__success
int callx_c_switch_table(void *ctx)
{
	return 0;
}

#endif

/*
 * Misaligned pointers to functions are ignored, the rest of the data is
 * accessible as before. The program doesn't have callx, so libbpf doesn't
 * append the functions that .rodata points to and doesn't make a copy of
 * .rodata for it.
 */
static const struct {
	char tag;
	op_fn op;
} __attribute__((packed)) packed_ops[] = {
	{ 5, op0 },
	{ 7, op1 },
};

SEC("socket")
__success __retval(7)
int callx_c_packed_struct(void *ctx)
{
	return packed_ops[op_idx & 1].tag;
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
