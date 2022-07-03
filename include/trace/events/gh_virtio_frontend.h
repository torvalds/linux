/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gh_virtio_frontend

#if !defined(_TRACE_GH_VIRTIO_FRONTEND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GH_VIRTIO_FRONTEND_H

#include <linux/tracepoint.h>

TRACE_EVENT(virtio_mmio_vm_notify,

	TP_PROTO(unsigned int dev_index, unsigned int qindex),

	TP_ARGS(dev_index, qindex),

	TP_STRUCT__entry(
		__field(unsigned int,	dev_index)
		__field(unsigned int,	qindex)
	),

	TP_fast_assign(
		__entry->dev_index	= dev_index;
		__entry->qindex		= qindex;
	),

	TP_printk("virtio%u qindex %u", __entry->dev_index, __entry->qindex)
);

TRACE_EVENT(virtio_mmio_vm_interrupt,

	TP_PROTO(unsigned int dev_index, unsigned int status),

	TP_ARGS(dev_index, status),

	TP_STRUCT__entry(
		__field(unsigned int,	dev_index)
		__field(unsigned int,	status)
	),

	TP_fast_assign(
		__entry->dev_index	= dev_index;
		__entry->status		= status;
	),

	TP_printk("virtio%u status %x", __entry->dev_index, __entry->status)
);

TRACE_EVENT(virtio_vring_split_add,

	TP_PROTO(unsigned int dev_index, int qhead, unsigned int avail_idx_shadow,
				unsigned int descs_used, unsigned int num_free),

	TP_ARGS(dev_index, qhead, avail_idx_shadow, descs_used, num_free),

	TP_STRUCT__entry(
		__field(unsigned int,	dev_index)
		__field(unsigned int,	qhead)
		__field(unsigned int,	avail_idx_shadow)
		__field(unsigned int,	descs_used)
		__field(unsigned int,	num_free)
	),

	TP_fast_assign(
		__entry->dev_index		= dev_index;
		__entry->qhead			= qhead;
		__entry->avail_idx_shadow	= avail_idx_shadow;
		__entry->descs_used		= descs_used;
		__entry->num_free		= num_free;
	),

	TP_printk("virtio%u qhead %u avail_idx_shadow %u descs_used %u num_free %u",
		__entry->dev_index, __entry->qhead, __entry->avail_idx_shadow,
		__entry->descs_used, __entry->num_free)
);

TRACE_EVENT(virtio_detach_buf,

	TP_PROTO(unsigned int dev_index, unsigned int free_head, unsigned int num_free),

	TP_ARGS(dev_index, free_head, num_free),

	TP_STRUCT__entry(
		__field(unsigned int,	dev_index)
		__field(unsigned int,	free_head)
		__field(unsigned int,	num_free)
	),

	TP_fast_assign(
		__entry->dev_index	= dev_index;
		__entry->free_head	= free_head;
		__entry->num_free	= num_free;
	),

	TP_printk("virtio%u free_head %u num_free %u", __entry->dev_index,
				__entry->free_head, __entry->num_free)
);

TRACE_EVENT(virtio_get_buf_ctx_split,

	TP_PROTO(unsigned int dev_index, unsigned int last_used_idx, unsigned int ring_used_idx),

	TP_ARGS(dev_index, last_used_idx, ring_used_idx),

	TP_STRUCT__entry(
		__field(unsigned int,	dev_index)
		__field(unsigned int,	last_used_idx)
		__field(unsigned int,	ring_used_idx)
	),

	TP_fast_assign(
		__entry->dev_index	= dev_index;
		__entry->last_used_idx	= last_used_idx;
		__entry->ring_used_idx	= ring_used_idx;
	),

	TP_printk("virtio%u last_used_idx %u ring_used_idx %u", __entry->dev_index,
				__entry->last_used_idx, __entry->ring_used_idx)
);


TRACE_EVENT(virtio_block_done,

	TP_PROTO(unsigned int dev_index, unsigned int req_op, unsigned int sector),

	TP_ARGS(dev_index, req_op, sector),

	TP_STRUCT__entry(
		__field(unsigned int,	dev_index)
		__field(unsigned int,	req_op)
		__field(unsigned int,	sector)
	),

	TP_fast_assign(
		__entry->dev_index	= dev_index;
		__entry->req_op		= req_op;
		__entry->sector		= sector;
	),

	TP_printk("virtio%u req_op %u sector %u", __entry->dev_index,
				__entry->req_op, __entry->sector)
);

TRACE_EVENT(virtio_block_submit,

	TP_PROTO(unsigned int dev_index, unsigned int type, unsigned int sector,
				unsigned int ioprio, int err, unsigned int num),

	TP_ARGS(dev_index, type, sector, ioprio, err, num),

	TP_STRUCT__entry(
		__field(unsigned int,	dev_index)
		__field(unsigned int,	type)
		__field(unsigned int,	sector)
		__field(unsigned int,	ioprio)
		__field(int,	err)
		__field(unsigned int,	num)
	),

	TP_fast_assign(
		__entry->dev_index	= dev_index;
		__entry->type		= type;
		__entry->sector		= sector;
		__entry->ioprio		= ioprio;
		__entry->err		= err;
		__entry->num		= num;
	),

	TP_printk("virtio%u type %x sector %u ioprio %u num %d err %d",
			__entry->dev_index, __entry->type, __entry->sector,
			__entry->ioprio, __entry->num, __entry->err)
);

#endif /* _TRACE_GH_VIRTIO_FRONTEND_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
