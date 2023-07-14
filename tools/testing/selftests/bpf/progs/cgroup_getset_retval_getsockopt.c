// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2021 Google LLC.
 */

#include <errno.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__u32 invocations = 0;
__u32 assertion_error = 0;
__u32 retval_value = 0;
__u32 ctx_retval_value = 0;
__u32 page_size = 0;

SEC("cgroup/getsockopt")
int get_retval(struct bpf_sockopt *ctx)
{
	retval_value = bpf_get_retval();
	ctx_retval_value = ctx->retval;
	__sync_fetch_and_add(&invocations, 1);

	/* optval larger than PAGE_SIZE use kernel's buffer. */
	if (ctx->optlen > page_size)
		ctx->optlen = 0;

	return 1;
}

SEC("cgroup/getsockopt")
int set_eisconn(struct bpf_sockopt *ctx)
{
	__sync_fetch_and_add(&invocations, 1);

	if (bpf_set_retval(-EISCONN))
		assertion_error = 1;

	/* optval larger than PAGE_SIZE use kernel's buffer. */
	if (ctx->optlen > page_size)
		ctx->optlen = 0;

	return 1;
}

SEC("cgroup/getsockopt")
int clear_retval(struct bpf_sockopt *ctx)
{
	__sync_fetch_and_add(&invocations, 1);

	ctx->retval = 0;

	/* optval larger than PAGE_SIZE use kernel's buffer. */
	if (ctx->optlen > page_size)
		ctx->optlen = 0;

	return 1;
}
