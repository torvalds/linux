/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM fsi_master_gpio

#if !defined(_TRACE_FSI_MASTER_GPIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FSI_MASTER_GPIO_H

#include <linux/tracepoint.h>

TRACE_EVENT(fsi_master_gpio_in,
	TP_PROTO(const struct fsi_master_gpio *master, int bits, uint64_t msg),
	TP_ARGS(master, bits, msg),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	bits)
		__field(uint64_t, msg)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
		__entry->bits = bits;
		__entry->msg  = msg & ((1ull<<bits) - 1);
	),
	TP_printk("fsi-gpio%d => %0*llx[%d]",
		__entry->master_idx,
		(__entry->bits + 3) / 4,
		__entry->msg,
		__entry->bits
	)
);

TRACE_EVENT(fsi_master_gpio_out,
	TP_PROTO(const struct fsi_master_gpio *master, int bits, uint64_t msg),
	TP_ARGS(master, bits, msg),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	bits)
		__field(uint64_t, msg)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
		__entry->bits = bits;
		__entry->msg  = msg & ((1ull<<bits) - 1);
	),
	TP_printk("fsi-gpio%d <= %0*llx[%d]",
		__entry->master_idx,
		(__entry->bits + 3) / 4,
		__entry->msg,
		__entry->bits
	)
);

TRACE_EVENT(fsi_master_gpio_clock_zeros,
	TP_PROTO(const struct fsi_master_gpio *master, int clocks),
	TP_ARGS(master, clocks),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	clocks)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
		__entry->clocks = clocks;
	),
	TP_printk("fsi-gpio%d clock %d zeros",
		  __entry->master_idx, __entry->clocks
	)
);

TRACE_EVENT(fsi_master_gpio_break,
	TP_PROTO(const struct fsi_master_gpio *master),
	TP_ARGS(master),
	TP_STRUCT__entry(
		__field(int,	master_idx)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
	),
	TP_printk("fsi-gpio%d ----break---",
		__entry->master_idx
	)
);

TRACE_EVENT(fsi_master_gpio_crc_cmd_error,
	TP_PROTO(const struct fsi_master_gpio *master),
	TP_ARGS(master),
	TP_STRUCT__entry(
		__field(int,	master_idx)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
	),
	TP_printk("fsi-gpio%d ----CRC command retry---",
		__entry->master_idx
	)
);

TRACE_EVENT(fsi_master_gpio_crc_rsp_error,
	TP_PROTO(const struct fsi_master_gpio *master),
	TP_ARGS(master),
	TP_STRUCT__entry(
		__field(int,	master_idx)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
	),
	TP_printk("fsi-gpio%d ----CRC response---",
		__entry->master_idx
	)
);

TRACE_EVENT(fsi_master_gpio_poll_response_busy,
	TP_PROTO(const struct fsi_master_gpio *master, int busy),
	TP_ARGS(master, busy),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(int,	busy)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
		__entry->busy = busy;
	),
	TP_printk("fsi-gpio%d: device reported busy %d times",
		__entry->master_idx, __entry->busy)
);

TRACE_EVENT(fsi_master_gpio_cmd_abs_addr,
	TP_PROTO(const struct fsi_master_gpio *master, u32 addr),
	TP_ARGS(master, addr),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(u32,	addr)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
		__entry->addr = addr;
	),
	TP_printk("fsi-gpio%d: Sending ABS_ADR %06x",
		__entry->master_idx, __entry->addr)
);

TRACE_EVENT(fsi_master_gpio_cmd_rel_addr,
	TP_PROTO(const struct fsi_master_gpio *master, u32 rel_addr),
	TP_ARGS(master, rel_addr),
	TP_STRUCT__entry(
		__field(int,	master_idx)
		__field(u32,	rel_addr)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
		__entry->rel_addr = rel_addr;
	),
	TP_printk("fsi-gpio%d: Sending REL_ADR %03x",
		__entry->master_idx, __entry->rel_addr)
);

TRACE_EVENT(fsi_master_gpio_cmd_same_addr,
	TP_PROTO(const struct fsi_master_gpio *master),
	TP_ARGS(master),
	TP_STRUCT__entry(
		__field(int,	master_idx)
	),
	TP_fast_assign(
		__entry->master_idx = master->master.idx;
	),
	TP_printk("fsi-gpio%d: Sending SAME_ADR",
		__entry->master_idx)
);

#endif /* _TRACE_FSI_MASTER_GPIO_H */

#include <trace/define_trace.h>
