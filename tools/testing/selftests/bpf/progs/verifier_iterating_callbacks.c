// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 8);
	__type(key, __u32);
	__type(value, __u64);
} map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_USER_RINGBUF);
	__uint(max_entries, 8);
} ringbuf SEC(".maps");

struct vm_area_struct;
struct bpf_map;

struct buf_context {
	char *buf;
};

struct num_context {
	__u64 i;
	__u64 j;
};

__u8 choice_arr[2] = { 0, 1 };

static int unsafe_on_2nd_iter_cb(__u32 idx, struct buf_context *ctx)
{
	if (idx == 0) {
		ctx->buf = (char *)(0xDEAD);
		return 0;
	}

	if (bpf_probe_read_user(ctx->buf, 8, (void *)(0xBADC0FFEE)))
		return 1;

	return 0;
}

SEC("?raw_tp")
__failure __msg("R1 type=scalar expected=fp")
int unsafe_on_2nd_iter(void *unused)
{
	char buf[4];
	struct buf_context loop_ctx = { .buf = buf };

	bpf_loop(100, unsafe_on_2nd_iter_cb, &loop_ctx, 0);
	return 0;
}

static int unsafe_on_zero_iter_cb(__u32 idx, struct num_context *ctx)
{
	ctx->i = 0;
	return 0;
}

SEC("?raw_tp")
__failure __msg("invalid access to map value, value_size=2 off=32 size=1")
int unsafe_on_zero_iter(void *unused)
{
	struct num_context loop_ctx = { .i = 32 };

	bpf_loop(100, unsafe_on_zero_iter_cb, &loop_ctx, 0);
	return choice_arr[loop_ctx.i];
}

static int widening_cb(__u32 idx, struct num_context *ctx)
{
	++ctx->i;
	return 0;
}

SEC("?raw_tp")
__success
int widening(void *unused)
{
	struct num_context loop_ctx = { .i = 0, .j = 1 };

	bpf_loop(100, widening_cb, &loop_ctx, 0);
	/* loop_ctx.j is not changed during callback iteration,
	 * verifier should not apply widening to it.
	 */
	return choice_arr[loop_ctx.j];
}

static int loop_detection_cb(__u32 idx, struct num_context *ctx)
{
	for (;;) {}
	return 0;
}

SEC("?raw_tp")
__failure __msg("infinite loop detected")
int loop_detection(void *unused)
{
	struct num_context loop_ctx = { .i = 0 };

	bpf_loop(100, loop_detection_cb, &loop_ctx, 0);
	return 0;
}

static __always_inline __u64 oob_state_machine(struct num_context *ctx)
{
	switch (ctx->i) {
	case 0:
		ctx->i = 1;
		break;
	case 1:
		ctx->i = 32;
		break;
	}
	return 0;
}

static __u64 for_each_map_elem_cb(struct bpf_map *map, __u32 *key, __u64 *val, void *data)
{
	return oob_state_machine(data);
}

SEC("?raw_tp")
__failure __msg("invalid access to map value, value_size=2 off=32 size=1")
int unsafe_for_each_map_elem(void *unused)
{
	struct num_context loop_ctx = { .i = 0 };

	bpf_for_each_map_elem(&map, for_each_map_elem_cb, &loop_ctx, 0);
	return choice_arr[loop_ctx.i];
}

static __u64 ringbuf_drain_cb(struct bpf_dynptr *dynptr, void *data)
{
	return oob_state_machine(data);
}

SEC("?raw_tp")
__failure __msg("invalid access to map value, value_size=2 off=32 size=1")
int unsafe_ringbuf_drain(void *unused)
{
	struct num_context loop_ctx = { .i = 0 };

	bpf_user_ringbuf_drain(&ringbuf, ringbuf_drain_cb, &loop_ctx, 0);
	return choice_arr[loop_ctx.i];
}

static __u64 find_vma_cb(struct task_struct *task, struct vm_area_struct *vma, void *data)
{
	return oob_state_machine(data);
}

SEC("?raw_tp")
__failure __msg("invalid access to map value, value_size=2 off=32 size=1")
int unsafe_find_vma(void *unused)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct num_context loop_ctx = { .i = 0 };

	bpf_find_vma(task, 0, find_vma_cb, &loop_ctx, 0);
	return choice_arr[loop_ctx.i];
}

static int iter_limit_cb(__u32 idx, struct num_context *ctx)
{
	ctx->i++;
	return 0;
}

SEC("?raw_tp")
__success
int bpf_loop_iter_limit_ok(void *unused)
{
	struct num_context ctx = { .i = 0 };

	bpf_loop(1, iter_limit_cb, &ctx, 0);
	return choice_arr[ctx.i];
}

SEC("?raw_tp")
__failure __msg("invalid access to map value, value_size=2 off=2 size=1")
int bpf_loop_iter_limit_overflow(void *unused)
{
	struct num_context ctx = { .i = 0 };

	bpf_loop(2, iter_limit_cb, &ctx, 0);
	return choice_arr[ctx.i];
}

static int iter_limit_level2a_cb(__u32 idx, struct num_context *ctx)
{
	ctx->i += 100;
	return 0;
}

static int iter_limit_level2b_cb(__u32 idx, struct num_context *ctx)
{
	ctx->i += 10;
	return 0;
}

static int iter_limit_level1_cb(__u32 idx, struct num_context *ctx)
{
	ctx->i += 1;
	bpf_loop(1, iter_limit_level2a_cb, ctx, 0);
	bpf_loop(1, iter_limit_level2b_cb, ctx, 0);
	return 0;
}

/* Check that path visiting every callback function once had been
 * reached by verifier. Variables 'ctx{1,2}i' below serve as flags,
 * with each decimal digit corresponding to a callback visit marker.
 */
SEC("socket")
__success __retval(111111)
int bpf_loop_iter_limit_nested(void *unused)
{
	struct num_context ctx1 = { .i = 0 };
	struct num_context ctx2 = { .i = 0 };
	__u64 a, b, c;

	bpf_loop(1, iter_limit_level1_cb, &ctx1, 0);
	bpf_loop(1, iter_limit_level1_cb, &ctx2, 0);
	a = ctx1.i;
	b = ctx2.i;
	/* Force 'ctx1.i' and 'ctx2.i' precise. */
	c = choice_arr[(a + b) % 2];
	/* This makes 'c' zero, but neither clang nor verifier know it. */
	c /= 10;
	/* Make sure that verifier does not visit 'impossible' states:
	 * enumerate all possible callback visit masks.
	 */
	if (a != 0 && a != 1 && a != 11 && a != 101 && a != 111 &&
	    b != 0 && b != 1 && b != 11 && b != 101 && b != 111)
		asm volatile ("r0 /= 0;" ::: "r0");
	return 1000 * a + b + c;
}

struct iter_limit_bug_ctx {
	__u64 a;
	__u64 b;
	__u64 c;
};

static __naked void iter_limit_bug_cb(void)
{
	/* This is the same as C code below, but written
	 * in assembly to control which branches are fall-through.
	 *
	 *   switch (bpf_get_prandom_u32()) {
	 *   case 1:  ctx->a = 42; break;
	 *   case 2:  ctx->b = 42; break;
	 *   default: ctx->c = 42; break;
	 *   }
	 */
	asm volatile (
	"r9 = r2;"
	"call %[bpf_get_prandom_u32];"
	"r1 = r0;"
	"r2 = 42;"
	"r0 = 0;"
	"if r1 == 0x1 goto 1f;"
	"if r1 == 0x2 goto 2f;"
	"*(u64 *)(r9 + 16) = r2;"
	"exit;"
	"1: *(u64 *)(r9 + 0) = r2;"
	"exit;"
	"2: *(u64 *)(r9 + 8) = r2;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all
	);
}

SEC("tc")
__failure
__flag(BPF_F_TEST_STATE_FREQ)
int iter_limit_bug(struct __sk_buff *skb)
{
	struct iter_limit_bug_ctx ctx = { 7, 7, 7 };

	bpf_loop(2, iter_limit_bug_cb, &ctx, 0);

	/* This is the same as C code below,
	 * written in assembly to guarantee checks order.
	 *
	 *   if (ctx.a == 42 && ctx.b == 42 && ctx.c == 7)
	 *     asm volatile("r1 /= 0;":::"r1");
	 */
	asm volatile (
	"r1 = *(u64 *)%[ctx_a];"
	"if r1 != 42 goto 1f;"
	"r1 = *(u64 *)%[ctx_b];"
	"if r1 != 42 goto 1f;"
	"r1 = *(u64 *)%[ctx_c];"
	"if r1 != 7 goto 1f;"
	"r1 /= 0;"
	"1:"
	:
	: [ctx_a]"m"(ctx.a),
	  [ctx_b]"m"(ctx.b),
	  [ctx_c]"m"(ctx.c)
	: "r1"
	);
	return 0;
}

char _license[] SEC("license") = "GPL";
