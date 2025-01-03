// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

/* From include/linux/filter.h */
#define MAX_BPF_STACK    512

#if defined(__TARGET_ARCH_x86)

struct elem {
	struct bpf_timer t;
	char pad[256];
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} array SEC(".maps");

SEC("kprobe")
__description("Private stack, single prog")
__success
__arch_x86_64
__jited("	movabsq	$0x{{.*}}, %r9")
__jited("	addq	%gs:0x{{.*}}, %r9")
__jited("	movl	$0x2a, %edi")
__jited("	movq	%rdi, -0x100(%r9)")
__naked void private_stack_single_prog(void)
{
	asm volatile ("			\
	r1 = 42;			\
	*(u64 *)(r10 - 256) = r1;	\
	r0 = 0;				\
	exit;				\
"	::: __clobber_all);
}

SEC("raw_tp")
__description("No private stack")
__success
__arch_x86_64
__jited("	subq	$0x8, %rsp")
__naked void no_private_stack_nested(void)
{
	asm volatile ("			\
	r1 = 42;			\
	*(u64 *)(r10 - 8) = r1;		\
	r0 = 0;				\
	exit;				\
"	::: __clobber_all);
}

__used
__naked static void cumulative_stack_depth_subprog(void)
{
	asm volatile ("				\
	r1 = 41;				\
	*(u64 *)(r10 - 32) = r1;		\
	call %[bpf_get_smp_processor_id];	\
	exit;					\
"	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("kprobe")
__description("Private stack, subtree > MAX_BPF_STACK")
__success
__arch_x86_64
/* private stack fp for the main prog */
__jited("	movabsq	$0x{{.*}}, %r9")
__jited("	addq	%gs:0x{{.*}}, %r9")
__jited("	movl	$0x2a, %edi")
__jited("	movq	%rdi, -0x200(%r9)")
__jited("	pushq	%r9")
__jited("	callq	0x{{.*}}")
__jited("	popq	%r9")
__jited("	xorl	%eax, %eax")
__naked void private_stack_nested_1(void)
{
	asm volatile ("				\
	r1 = 42;				\
	*(u64 *)(r10 - %[max_bpf_stack]) = r1;	\
	call cumulative_stack_depth_subprog;	\
	r0 = 0;					\
	exit;					\
"	:
	: __imm_const(max_bpf_stack, MAX_BPF_STACK)
	: __clobber_all);
}

__naked __noinline __used
static unsigned long loop_callback(void)
{
	asm volatile ("				\
	call %[bpf_get_prandom_u32];		\
	r1 = 42;				\
	*(u64 *)(r10 - 512) = r1;		\
	call cumulative_stack_depth_subprog;	\
	r0 = 0;					\
	exit;					\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_common);
}

SEC("raw_tp")
__description("Private stack, callback")
__success
__arch_x86_64
/* for func loop_callback */
__jited("func #1")
__jited("	endbr64")
__jited("	nopl	(%rax,%rax)")
__jited("	nopl	(%rax)")
__jited("	pushq	%rbp")
__jited("	movq	%rsp, %rbp")
__jited("	endbr64")
__jited("	movabsq	$0x{{.*}}, %r9")
__jited("	addq	%gs:0x{{.*}}, %r9")
__jited("	pushq	%r9")
__jited("	callq")
__jited("	popq	%r9")
__jited("	movl	$0x2a, %edi")
__jited("	movq	%rdi, -0x200(%r9)")
__jited("	pushq	%r9")
__jited("	callq")
__jited("	popq	%r9")
__naked void private_stack_callback(void)
{
	asm volatile ("			\
	r1 = 1;				\
	r2 = %[loop_callback];		\
	r3 = 0;				\
	r4 = 0;				\
	call %[bpf_loop];		\
	r0 = 0;				\
	exit;				\
"	:
	: __imm_ptr(loop_callback),
	  __imm(bpf_loop)
	: __clobber_common);
}

SEC("fentry/bpf_fentry_test9")
__description("Private stack, exception in main prog")
__success __retval(0)
__arch_x86_64
__jited("	pushq	%r9")
__jited("	callq")
__jited("	popq	%r9")
int private_stack_exception_main_prog(void)
{
	asm volatile ("			\
	r1 = 42;			\
	*(u64 *)(r10 - 512) = r1;	\
"	::: __clobber_common);

	bpf_throw(0);
	return 0;
}

__used static int subprog_exception(void)
{
	bpf_throw(0);
	return 0;
}

SEC("fentry/bpf_fentry_test9")
__description("Private stack, exception in subprog")
__success __retval(0)
__arch_x86_64
__jited("	movq	%rdi, -0x200(%r9)")
__jited("	pushq	%r9")
__jited("	callq")
__jited("	popq	%r9")
int private_stack_exception_sub_prog(void)
{
	asm volatile ("			\
	r1 = 42;			\
	*(u64 *)(r10 - 512) = r1;	\
	call subprog_exception;		\
"	::: __clobber_common);

	return 0;
}

int glob;
__noinline static void subprog2(int *val)
{
	glob += val[0] * 2;
}

__noinline static void subprog1(int *val)
{
	int tmp[64] = {};

	tmp[0] = *val;
	subprog2(tmp);
}

__noinline static int timer_cb1(void *map, int *key, struct bpf_timer *timer)
{
	subprog1(key);
	return 0;
}

__noinline static int timer_cb2(void *map, int *key, struct bpf_timer *timer)
{
	return 0;
}

SEC("fentry/bpf_fentry_test9")
__description("Private stack, async callback, not nested")
__success __retval(0)
__arch_x86_64
__jited("	movabsq	$0x{{.*}}, %r9")
int private_stack_async_callback_1(void)
{
	struct bpf_timer *arr_timer;
	int array_key = 0;

	arr_timer = bpf_map_lookup_elem(&array, &array_key);
	if (!arr_timer)
		return 0;

	bpf_timer_init(arr_timer, &array, 1);
	bpf_timer_set_callback(arr_timer, timer_cb2);
	bpf_timer_start(arr_timer, 0, 0);
	subprog1(&array_key);
	return 0;
}

SEC("fentry/bpf_fentry_test9")
__description("Private stack, async callback, potential nesting")
__success __retval(0)
__arch_x86_64
__jited("	subq	$0x100, %rsp")
int private_stack_async_callback_2(void)
{
	struct bpf_timer *arr_timer;
	int array_key = 0;

	arr_timer = bpf_map_lookup_elem(&array, &array_key);
	if (!arr_timer)
		return 0;

	bpf_timer_init(arr_timer, &array, 1);
	bpf_timer_set_callback(arr_timer, timer_cb1);
	bpf_timer_start(arr_timer, 0, 0);
	subprog1(&array_key);
	return 0;
}

#else

SEC("kprobe")
__description("private stack is not supported, use a dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
