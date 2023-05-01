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
