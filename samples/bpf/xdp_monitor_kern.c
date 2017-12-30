/* XDP monitor tool, based on tracepoints
 *
 *  Copyright(c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

struct bpf_map_def SEC("maps") redirect_err_cnt = {
	.type = BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(u64),
	.max_entries = 2,
	/* TODO: have entries for all possible errno's */
};

#define XDP_UNKNOWN	XDP_REDIRECT + 1
struct bpf_map_def SEC("maps") exception_cnt = {
	.type		= BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size	= sizeof(u32),
	.value_size	= sizeof(u64),
	.max_entries	= XDP_UNKNOWN + 1,
};

/* Tracepoint format: /sys/kernel/debug/tracing/events/xdp/xdp_redirect/format
 * Code in:                kernel/include/trace/events/xdp.h
 */
struct xdp_redirect_ctx {
	u64 __pad;		// First 8 bytes are not accessible by bpf code
	int prog_id;		//	offset:8;  size:4; signed:1;
	u32 act;		//	offset:12  size:4; signed:0;
	int ifindex;		//	offset:16  size:4; signed:1;
	int err;		//	offset:20  size:4; signed:1;
	int to_ifindex;		//	offset:24  size:4; signed:1;
	u32 map_id;		//	offset:28  size:4; signed:0;
	int map_index;		//	offset:32  size:4; signed:1;
};				//	offset:36

enum {
	XDP_REDIRECT_SUCCESS = 0,
	XDP_REDIRECT_ERROR = 1
};

static __always_inline
int xdp_redirect_collect_stat(struct xdp_redirect_ctx *ctx)
{
	u32 key = XDP_REDIRECT_ERROR;
	int err = ctx->err;
	u64 *cnt;

	if (!err)
		key = XDP_REDIRECT_SUCCESS;

	cnt  = bpf_map_lookup_elem(&redirect_err_cnt, &key);
	if (!cnt)
		return 1;
	*cnt += 1;

	return 0; /* Indicate event was filtered (no further processing)*/
	/*
	 * Returning 1 here would allow e.g. a perf-record tracepoint
	 * to see and record these events, but it doesn't work well
	 * in-practice as stopping perf-record also unload this
	 * bpf_prog.  Plus, there is additional overhead of doing so.
	 */
}

SEC("tracepoint/xdp/xdp_redirect_err")
int trace_xdp_redirect_err(struct xdp_redirect_ctx *ctx)
{
	return xdp_redirect_collect_stat(ctx);
}


SEC("tracepoint/xdp/xdp_redirect_map_err")
int trace_xdp_redirect_map_err(struct xdp_redirect_ctx *ctx)
{
	return xdp_redirect_collect_stat(ctx);
}

/* Likely unloaded when prog starts */
SEC("tracepoint/xdp/xdp_redirect")
int trace_xdp_redirect(struct xdp_redirect_ctx *ctx)
{
	return xdp_redirect_collect_stat(ctx);
}

/* Likely unloaded when prog starts */
SEC("tracepoint/xdp/xdp_redirect_map")
int trace_xdp_redirect_map(struct xdp_redirect_ctx *ctx)
{
	return xdp_redirect_collect_stat(ctx);
}

/* Tracepoint format: /sys/kernel/debug/tracing/events/xdp/xdp_exception/format
 * Code in:                kernel/include/trace/events/xdp.h
 */
struct xdp_exception_ctx {
	u64 __pad;	// First 8 bytes are not accessible by bpf code
	int prog_id;	//	offset:8;  size:4; signed:1;
	u32 act;	//	offset:12; size:4; signed:0;
	int ifindex;	//	offset:16; size:4; signed:1;
};

SEC("tracepoint/xdp/xdp_exception")
int trace_xdp_exception(struct xdp_exception_ctx *ctx)
{
	u64 *cnt;;
	u32 key;

	key = ctx->act;
	if (key > XDP_REDIRECT)
		key = XDP_UNKNOWN;

	cnt = bpf_map_lookup_elem(&exception_cnt, &key);
	if (!cnt)
		return 1;
	*cnt += 1;

	return 0;
}
