// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

static long stack[256];

/*
 * KPROBE contexts
 */

__weak int kprobe_typedef_ctx_subprog(bpf_user_pt_regs_t *ctx)
{
	return bpf_get_stack(ctx, &stack, sizeof(stack), 0);
}

SEC("?kprobe")
__success
int kprobe_typedef_ctx(void *ctx)
{
	return kprobe_typedef_ctx_subprog(ctx);
}

/* s390x defines:
 *
 * typedef user_pt_regs bpf_user_pt_regs_t;
 * typedef struct { ... } user_pt_regs;
 *
 * And so "canonical" underlying struct type is anonymous.
 * So on s390x only valid ways to have PTR_TO_CTX argument in global subprogs
 * are:
 *   - bpf_user_pt_regs_t *ctx (typedef);
 *   - struct bpf_user_pt_regs_t *ctx (backwards compatible struct hack);
 *   - void *ctx __arg_ctx (arg:ctx tag)
 *
 * Other architectures also allow using underlying struct types (e.g.,
 * `struct pt_regs *ctx` for x86-64)
 */
#ifndef bpf_target_s390

#define pt_regs_struct_t typeof(*(__PT_REGS_CAST((struct pt_regs *)NULL)))

__weak int kprobe_struct_ctx_subprog(pt_regs_struct_t *ctx)
{
	return bpf_get_stack((void *)ctx, &stack, sizeof(stack), 0);
}

SEC("?kprobe")
__success
int kprobe_resolved_ctx(void *ctx)
{
	return kprobe_struct_ctx_subprog(ctx);
}

#endif

/* this is current hack to make this work on old kernels */
struct bpf_user_pt_regs_t {};

__weak int kprobe_workaround_ctx_subprog(struct bpf_user_pt_regs_t *ctx)
{
	return bpf_get_stack(ctx, &stack, sizeof(stack), 0);
}

SEC("?kprobe")
__success
int kprobe_workaround_ctx(void *ctx)
{
	return kprobe_workaround_ctx_subprog(ctx);
}

/*
 * RAW_TRACEPOINT contexts
 */

__weak int raw_tp_ctx_subprog(struct bpf_raw_tracepoint_args *ctx)
{
	return bpf_get_stack(ctx, &stack, sizeof(stack), 0);
}

SEC("?raw_tp")
__success
int raw_tp_ctx(void *ctx)
{
	return raw_tp_ctx_subprog(ctx);
}

/*
 * RAW_TRACEPOINT_WRITABLE contexts
 */

__weak int raw_tp_writable_ctx_subprog(struct bpf_raw_tracepoint_args *ctx)
{
	return bpf_get_stack(ctx, &stack, sizeof(stack), 0);
}

SEC("?raw_tp")
__success
int raw_tp_writable_ctx(void *ctx)
{
	return raw_tp_writable_ctx_subprog(ctx);
}

/*
 * PERF_EVENT contexts
 */

__weak int perf_event_ctx_subprog(struct bpf_perf_event_data *ctx)
{
	return bpf_get_stack(ctx, &stack, sizeof(stack), 0);
}

SEC("?perf_event")
__success
int perf_event_ctx(void *ctx)
{
	return perf_event_ctx_subprog(ctx);
}

/* this global subprog can be now called from many types of entry progs, each
 * with different context type
 */
__weak int subprog_ctx_tag(void *ctx __arg_ctx)
{
	return bpf_get_stack(ctx, stack, sizeof(stack), 0);
}

struct my_struct { int x; };

__weak int subprog_multi_ctx_tags(void *ctx1 __arg_ctx,
				  struct my_struct *mem,
				  void *ctx2 __arg_ctx)
{
	if (!mem)
		return 0;

	return bpf_get_stack(ctx1, stack, sizeof(stack), 0) +
	       mem->x +
	       bpf_get_stack(ctx2, stack, sizeof(stack), 0);
}

SEC("?raw_tp")
__success __log_level(2)
int arg_tag_ctx_raw_tp(void *ctx)
{
	struct my_struct x = { .x = 123 };

	return subprog_ctx_tag(ctx) + subprog_multi_ctx_tags(ctx, &x, ctx);
}

SEC("?perf_event")
__success __log_level(2)
int arg_tag_ctx_perf(void *ctx)
{
	struct my_struct x = { .x = 123 };

	return subprog_ctx_tag(ctx) + subprog_multi_ctx_tags(ctx, &x, ctx);
}

SEC("?kprobe")
__success __log_level(2)
int arg_tag_ctx_kprobe(void *ctx)
{
	struct my_struct x = { .x = 123 };

	return subprog_ctx_tag(ctx) + subprog_multi_ctx_tags(ctx, &x, ctx);
}
