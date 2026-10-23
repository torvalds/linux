// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

/*
 * Programs may use MAX_BPF_STACK_JIT (2 KiB) of stack on JITs that support
 * large stacks, combined over a call chain, with no separate limit on a
 * single frame. Interpreted programs and other JITs keep 512 bytes.
 */

SEC("socket")
__description("single frame of 2048 bytes")
__load_if_large_stack()
__success __success_unpriv __retval(42)
__naked void single_frame_2048(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -2048;					\
	r0 = 42;					\
	*(u64*)(r1 + 0) = r0;				\
	r0 = *(u64*)(r1 + 0);				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("single frame of 2048 bytes without large stack support")
__load_if_no_large_stack()
__failure __msg("invalid write to stack R1 off=-2048 size=8")
__naked void single_frame_2048_no_large_stack(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -2048;					\
	r0 = 42;					\
	*(u64*)(r1 + 0) = r0;				\
	exit;						\
"	::: __clobber_all);
}

__used __naked
static void frame_512_leaf(void)
{
	asm volatile ("					\
	r1 = 1;						\
	*(u64 *)(r10 - 512) = r1;			\
	exit;						\
"	::: __clobber_all);
}

__used __naked
static void frame_512_depth_2(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 512) = r1;			\
	call frame_512_leaf;				\
	exit;						\
"	::: __clobber_all);
}

__used __naked
static void frame_512_depth_3(void)
{
	asm volatile ("					\
	r1 = 3;						\
	*(u64 *)(r10 - 512) = r1;			\
	call frame_512_depth_2;				\
	exit;						\
"	::: __clobber_all);
}

__used __naked
static void frame_512_depth_4(void)
{
	asm volatile ("					\
	r1 = 4;						\
	*(u64 *)(r10 - 512) = r1;			\
	call frame_512_depth_3;				\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("four frames of 512 bytes fit the 2 KiB budget")
__load_if_large_stack()
__success __log_level(4) __msg("stack depth max 2048")
__naked void four_frames_of_512(void)
{
	asm volatile ("					\
	call frame_512_depth_4;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("five frames of 512 bytes exceed the 2 KiB budget")
__load_if_large_stack()
__failure __msg("combined stack size of 5 calls is 2560. Too large")
__naked void five_frames_of_512(void)
{
	asm volatile ("					\
	r1 = 5;						\
	*(u64 *)(r10 - 512) = r1;			\
	call frame_512_depth_4;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

__used __naked
static void frame_1536_leaf(void)
{
	asm volatile ("					\
	r1 = 1;						\
	*(u64 *)(r10 - 1536) = r1;			\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("512-byte frame calling a 1536-byte frame")
__load_if_large_stack()
__success __log_level(4) __msg("stack depth max 2048")
__naked void uneven_frames_fit(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 512) = r1;			\
	call frame_1536_leaf;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("520-byte frame calling a 1536-byte frame")
__load_if_large_stack()
__failure __msg("combined stack size of 2 calls is 2064. Too large")
__naked void uneven_frames_exceed(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 520) = r1;			\
	call frame_1536_leaf;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

#ifdef __BPF_FEATURE_MAY_GOTO
/* may_goto adds its counter below the frame; a JIT does not hold that against the budget */
SEC("socket")
__description("frame of 2048 bytes with may_goto")
__load_if_large_stack()
__success __retval(42)
__naked void frame_2048_with_may_goto(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -2048;					\
	r0 = 42;					\
	*(u32*)(r1 + 0) = r0;				\
	may_goto l0_%=;					\
	r2 = 100;					\
	l0_%=:						\
	exit;						\
"	::: __clobber_all);
}
#endif

SEC("socket")
__description("variable offset write reaching 2048 bytes deep")
__load_if_large_stack()
__success
__naked void var_off_write_to_2048(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 8;					\
	r2 = r10;					\
	r2 += -2048;					\
	r2 += r0;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("variable offset write reaching 2056 bytes deep")
__load_if_large_stack()
__failure __msg("invalid variable-offset write to stack R2")
__naked void var_off_write_to_2056(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r0 &= 8;					\
	r2 = r10;					\
	r2 += -2056;					\
	r2 += r0;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Each frame of a private stack gets the whole budget. */
__used __naked
static void priv_stack_frame_2048(void)
{
	asm volatile ("					\
	r1 = 1;						\
	*(u64 *)(r10 - 2048) = r1;			\
	exit;						\
"	::: __clobber_all);
}

SEC("kprobe")
__description("private stack: two frames of 2048 bytes")
__load_if_large_stack()
__arch_x86_64
__arch_arm64
__success __log_level(4)
__msg("stack depth max 2048")
__msg("subprog 0 (private_stack_two_frames) main {{.*}} stack 2048")
__msg("subprog 1 (priv_stack_frame_2048) static {{.*}} stack 2048")
__naked void private_stack_two_frames(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 2048) = r1;			\
	call priv_stack_frame_2048;			\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

/*
 * A tail call unwinds the frame of the program doing it, so a large main
 * frame is fine; the 256-byte rule only concerns the frames of callers of a
 * subprog that tail calls.
 */
SEC("tc")
__description("tail call from a 1 KiB frame")
__load_if_large_stack()
__success
__naked void tail_call_from_large_frame(void)
{
	asm volatile ("					\
	r2 = 42;					\
	*(u64 *)(r10 - 1024) = r2;			\
	r2 = %[jmp_table] ll;				\
	r3 = 0;						\
	call %[bpf_tail_call];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_tail_call),
	  __imm_addr(jmp_table)
	: __clobber_all);
}

/*
 * bpf_clone_redirect() can run the program again on top of its own frame,
 * ten frames deep, so a program calling it keeps the 512-byte budget. The
 * redirect that happens after the program returns does not.
 */
SEC("tc")
__description("1 KiB frame with bpf_clone_redirect keeps the 512-byte budget")
__load_if_large_stack()
__failure __msg("invalid write to stack R1 off=-1024 size=8")
__naked void clone_redirect_keeps_512(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = r10;					\
	r1 += -1024;					\
	r0 = 42;					\
	*(u64 *)(r1 + 0) = r0;				\
	r1 = r6;					\
	r2 = 1;						\
	r3 = 0;						\
	call %[bpf_clone_redirect];			\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_clone_redirect)
	: __clobber_all);
}

SEC("tc")
__description("1 KiB frame with bpf_redirect keeps the 2 KiB budget")
__load_if_large_stack()
__success
__naked void redirect_keeps_2048(void)
{
	asm volatile ("					\
	r1 = r10;					\
	r1 += -1024;					\
	r0 = 42;					\
	*(u64 *)(r1 + 0) = r0;				\
	r1 = 1;						\
	r2 = 0;						\
	call %[bpf_redirect];				\
	exit;						\
"	:
	: __imm(bpf_redirect)
	: __clobber_all);
}

/* Global subprogs are verified on their own but share the call chain budget. */
__used __naked int global_frame_1536(void)
{
	asm volatile ("					\
	r1 = 1;						\
	*(u64 *)(r10 - 1536) = r1;			\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("512-byte frame calling a 1536-byte global subprog")
__load_if_large_stack()
__success __log_level(4) __msg("stack depth max 2048")
__naked void global_subprog_fits(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 512) = r1;			\
	call global_frame_1536;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("520-byte frame calling a 1536-byte global subprog")
__load_if_large_stack()
__failure __msg("combined stack size of 2 calls is 2064. Too large")
__naked void global_subprog_exceeds(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 520) = r1;			\
	call global_frame_1536;				\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

/* Callback frames are part of the chain of the helper that calls them. */
static __naked int loop_cb_1536(void)
{
	asm volatile ("					\
	r1 = 1;						\
	*(u64 *)(r10 - 1536) = r1;			\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("512-byte frame with a 1536-byte bpf_loop callback")
__load_if_large_stack()
__success __log_level(4) __msg("stack depth max 2048")
__naked void loop_callback_fits(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 512) = r1;			\
	r1 = 1;						\
	r2 = %[loop_cb_1536];				\
	r3 = 0;						\
	r4 = 0;						\
	call %[bpf_loop];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_ptr(loop_cb_1536),
	  __imm(bpf_loop)
	: __clobber_common);
}

SEC("socket")
__description("520-byte frame with a 1536-byte bpf_loop callback")
__load_if_large_stack()
__failure __msg("combined stack size of 2 calls is 2064. Too large")
__naked void loop_callback_exceeds(void)
{
	asm volatile ("					\
	r1 = 2;						\
	*(u64 *)(r10 - 520) = r1;			\
	r1 = 1;						\
	r2 = %[loop_cb_1536];				\
	r3 = 0;						\
	r4 = 0;						\
	call %[bpf_loop];				\
	r0 = 0;						\
	exit;						\
"	:
	: __imm_ptr(loop_cb_1536),
	  __imm(bpf_loop)
	: __clobber_common);
}

char _license[] SEC("license") = "GPL";
