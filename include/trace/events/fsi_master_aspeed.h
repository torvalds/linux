/* SPDX-License-Identifier: GPL-2.0-or-later */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM fsi_master_aspeed

#if !defined(_TRACE_FSI_MASTER_ASPEED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FSI_MASTER_ASPEED_H

#include <linux/tracepoint.h>

TRACE_EVENT(fsi_master_aspeed_opb_read,
	TP_PROTO(uint32_t addr, size_t size, uint32_t result, uint32_t status, uint32_t irq_status),
	TP_ARGS(addr, size, result, status, irq_status),
	TP_STRUCT__entry(
		__field(uint32_t,  addr)
		__field(size_t,    size)
		__field(uint32_t,  result)
		__field(uint32_t,  status)
		__field(uint32_t,  irq_status)
		),
	TP_fast_assign(
		__entry->addr = addr;
		__entry->size = size;
		__entry->result = result;
		__entry->status = status;
		__entry->irq_status = irq_status;
		),
	TP_printk("addr %08x size %zu: result %08x sts: %08x irq_sts: %08x",
		__entry->addr, __entry->size, __entry->result,
		__entry->status, __entry->irq_status
	   )
);

TRACE_EVENT(fsi_master_aspeed_opb_write,
	TP_PROTO(uint32_t addr, uint32_t val, size_t size, uint32_t status, uint32_t irq_status),
	TP_ARGS(addr, val, size, status, irq_status),
	TP_STRUCT__entry(
		__field(uint32_t,    addr)
		__field(uint32_t,    val)
		__field(size_t,    size)
		__field(uint32_t,  status)
		__field(uint32_t,  irq_status)
		),
	TP_fast_assign(
		__entry->addr = addr;
		__entry->val = val;
		__entry->size = size;
		__entry->status = status;
		__entry->irq_status = irq_status;
		),
	TP_printk("addr %08x val %08x size %zu status: %08x irq_sts: %08x",
		__entry->addr, __entry->val, __entry->size,
		__entry->status, __entry->irq_status
		)
	);

TRACE_EVENT(fsi_master_aspeed_opb_error,
	TP_PROTO(uint32_t mresp0, uint32_t mstap0, uint32_t mesrb0),
	TP_ARGS(mresp0, mstap0, mesrb0),
	TP_STRUCT__entry(
		__field(uint32_t,  mresp0)
		__field(uint32_t,  mstap0)
		__field(uint32_t,  mesrb0)
		),
	TP_fast_assign(
		__entry->mresp0 = mresp0;
		__entry->mstap0 = mstap0;
		__entry->mesrb0 = mesrb0;
		),
	TP_printk("mresp0 %08x mstap0 %08x mesrb0 %08x",
		__entry->mresp0, __entry->mstap0, __entry->mesrb0
		)
	);

#endif

#include <trace/define_trace.h>
