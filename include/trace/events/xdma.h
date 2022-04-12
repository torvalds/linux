/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xdma

#if !defined(_TRACE_XDMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_XDMA_H

#include <linux/tracepoint.h>

TRACE_EVENT(xdma_start,
	TP_PROTO(const struct aspeed_xdma *ctx, const struct aspeed_xdma_cmd *cmd),
	TP_ARGS(ctx, cmd),
	TP_STRUCT__entry(
		__field(bool,	dir_upstream)
		__field(unsigned int,	index)
		__field(__u64,	host)
		__field(__u64,	pitch)
		__field(__u64,	cmd)
	),
	TP_fast_assign(
		__entry->dir_upstream = ctx->upstream;
		__entry->index = ctx->cmd_idx;
		__entry->host = cmd->host_addr;
		__entry->pitch = cmd->pitch;
		__entry->cmd = cmd->cmd;
	),
	TP_printk("%s cmd:%u [%08llx %016llx %016llx]",
		__entry->dir_upstream ? "upstream" : "downstream",
		__entry->index,
		__entry->host,
		__entry->pitch,
		__entry->cmd
	)
);

TRACE_EVENT(xdma_irq,
	TP_PROTO(u32 sts),
	TP_ARGS(sts),
	TP_STRUCT__entry(
		__field(__u32,	status)
	),
	TP_fast_assign(
		__entry->status = sts;
	),
	TP_printk("sts:%08x",
		__entry->status
	)
);

TRACE_EVENT(xdma_reset,
	TP_PROTO(const struct aspeed_xdma *ctx),
	TP_ARGS(ctx),
	TP_STRUCT__entry(
		__field(bool,	dir_upstream)
		__field(bool,	in_progress)
	),
	TP_fast_assign(
		__entry->dir_upstream = ctx->upstream;
		__entry->in_progress =
			ctx->current_client ? ctx->current_client->in_progress : false;
	),
	TP_printk("%sin progress%s",
		__entry->in_progress ? "" : "not ",
		__entry->in_progress ? (__entry->dir_upstream ? " upstream" : " downstream") : ""
	)
);

TRACE_EVENT(xdma_perst,
	TP_PROTO(const struct aspeed_xdma *ctx),
	TP_ARGS(ctx),
	TP_STRUCT__entry(
		__field(bool,	in_reset)
	),
	TP_fast_assign(
		__entry->in_reset = ctx->in_reset;
	),
	TP_printk("%s",
		__entry->in_reset ? "in reset" : ""
	)
);

TRACE_EVENT(xdma_unmap,
	TP_PROTO(const struct aspeed_xdma_client *client),
	TP_ARGS(client),
	TP_STRUCT__entry(
		__field(__u32,	phys)
		__field(__u32,	size)
	),
	TP_fast_assign(
		__entry->phys = client->phys;
		__entry->size = client->size;
	),
	TP_printk("p:%08x s:%08x",
		__entry->phys,
		__entry->size
	)
);

TRACE_EVENT(xdma_mmap_error,
	TP_PROTO(const struct aspeed_xdma_client *client, unsigned long vm_start),
	TP_ARGS(client, vm_start),
	TP_STRUCT__entry(
		__field(__u32,	phys)
		__field(__u32,	size)
		__field(unsigned long,	vm_start)
	),
	TP_fast_assign(
		__entry->phys = client->phys;
		__entry->size = client->size;
		__entry->vm_start = vm_start;
	),
	TP_printk("p:%08x s:%08x v:%08lx",
		__entry->phys,
		__entry->size,
		__entry->vm_start
	)
);

TRACE_EVENT(xdma_mmap,
	TP_PROTO(const struct aspeed_xdma_client *client),
	TP_ARGS(client),
	TP_STRUCT__entry(
		__field(__u32,	phys)
		__field(__u32,	size)
	),
	TP_fast_assign(
		__entry->phys = client->phys;
		__entry->size = client->size;
	),
	TP_printk("p:%08x s:%08x",
		__entry->phys,
		__entry->size
	)
);

#endif /* _TRACE_XDMA_H */

#include <trace/define_trace.h>
