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

#endif /* _TRACE_FSI_MASTER_GPIO_H */

#include <trace/define_trace.h>
