// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "xdp_metadata.h"
#include "bpf_kfuncs.h"

int arr[1];
int unkn_idx;
const volatile bool call_dead_subprog = false;

__noinline long global_bad(void)
{
	return arr[unkn_idx]; /* BOOM */
}

__noinline long global_good(void)
{
	return arr[0];
}

__noinline long global_calls_bad(void)
{
	return global_good() + global_bad() /* does BOOM indirectly */;
}

__noinline long global_calls_good_only(void)
{
	return global_good();
}

__noinline long global_dead(void)
{
	return arr[0] * 2;
}

SEC("?raw_tp")
__success __log_level(2)
/* main prog is validated completely first */
__msg("('global_calls_good_only') is global and assumed valid.")
/* eventually global_good() is transitively validated as well */
__msg("Validating global_good() func")
__msg("('global_good') is safe for any args that match its prototype")
int chained_global_func_calls_success(void)
{
	int sum = 0;

	if (call_dead_subprog)
		sum += global_dead();
	return global_calls_good_only() + sum;
}

SEC("?raw_tp")
__failure __log_level(2)
/* main prog validated successfully first */
__msg("('global_calls_bad') is global and assumed valid.")
/* eventually we validate global_bad() and fail */
__msg("Validating global_bad() func")
__msg("math between map_value pointer and register") /* BOOM */
int chained_global_func_calls_bad(void)
{
	return global_calls_bad();
}

/* do out of bounds access forcing verifier to fail verification if this
 * global func is called
 */
__noinline int global_unsupp(const int *mem)
{
	if (!mem)
		return 0;
	return mem[100]; /* BOOM */
}

const volatile bool skip_unsupp_global = true;

SEC("?raw_tp")
__success
int guarded_unsupp_global_called(void)
{
	if (!skip_unsupp_global)
		return global_unsupp(NULL);
	return 0;
}

SEC("?raw_tp")
__failure __log_level(2)
__msg("Func#1 ('global_unsupp') is global and assumed valid.")
__msg("Validating global_unsupp() func#1...")
__msg("value is outside of the allowed memory range")
int unguarded_unsupp_global_called(void)
{
	int x = 0;

	return global_unsupp(&x);
}

long stack[128];

__weak int subprog_nullable_ptr_bad(int *p)
{
	return (*p) * 2; /* bad, missing null check */
}

SEC("?raw_tp")
__failure __log_level(2)
__msg("invalid mem access 'mem_or_null'")
int arg_tag_nullable_ptr_fail(void *ctx)
{
	int x = 42;

	return subprog_nullable_ptr_bad(&x);
}

__noinline __weak int subprog_nonnull_ptr_good(int *p1 __arg_nonnull, int *p2 __arg_nonnull)
{
	return (*p1) * (*p2); /* good, no need for NULL checks */
}

int x = 47;

SEC("?raw_tp")
__success __log_level(2)
int arg_tag_nonnull_ptr_good(void *ctx)
{
	int y = 74;

	return subprog_nonnull_ptr_good(&x, &y);
}

/* this global subprog can be now called from many types of entry progs, each
 * with different context type
 */
__weak int subprog_ctx_tag(void *ctx __arg_ctx)
{
	return bpf_get_stack(ctx, stack, sizeof(stack), 0);
}

SEC("?raw_tp")
__success __log_level(2)
int arg_tag_ctx_raw_tp(void *ctx)
{
	return subprog_ctx_tag(ctx);
}

SEC("?tp")
__success __log_level(2)
int arg_tag_ctx_tp(void *ctx)
{
	return subprog_ctx_tag(ctx);
}

SEC("?kprobe")
__success __log_level(2)
int arg_tag_ctx_kprobe(void *ctx)
{
	return subprog_ctx_tag(ctx);
}

__weak int subprog_dynptr(struct bpf_dynptr *dptr)
{
	long *d, t, buf[1] = {};

	d = bpf_dynptr_data(dptr, 0, sizeof(long));
	if (!d)
		return 0;

	t = *d + 1;

	d = bpf_dynptr_slice(dptr, 0, &buf, sizeof(long));
	if (!d)
		return t;

	t = *d + 2;

	return t;
}

SEC("?xdp")
__success __log_level(2)
int arg_tag_dynptr(struct xdp_md *ctx)
{
	struct bpf_dynptr dptr;

	bpf_dynptr_from_xdp(ctx, 0, &dptr);

	return subprog_dynptr(&dptr);
}

char _license[] SEC("license") = "GPL";
