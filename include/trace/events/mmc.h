/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmc

#if !defined(_TRACE_MMC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMC_H

#include <linux/tracepoint.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>

/*
 * Unconditional logging of mmc block erase operations,
 * including cmd, address, size
 */
DECLARE_EVENT_CLASS(mmc_blk_erase_class,
	TP_PROTO(unsigned int cmd, unsigned int addr, unsigned int size),
	TP_ARGS(cmd, addr, size),
	TP_STRUCT__entry(
		__field(unsigned int, cmd)
		__field(unsigned int, addr)
		__field(unsigned int, size)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->addr = addr;
		__entry->size = size;
	),
	TP_printk("cmd=%u,addr=0x%08x,size=0x%08x",
		  __entry->cmd, __entry->addr, __entry->size)
);

DEFINE_EVENT(mmc_blk_erase_class, mmc_blk_erase_start,
	TP_PROTO(unsigned int cmd, unsigned int addr, unsigned int size),
	TP_ARGS(cmd, addr, size));

DEFINE_EVENT(mmc_blk_erase_class, mmc_blk_erase_end,
	TP_PROTO(unsigned int cmd, unsigned int addr, unsigned int size),
	TP_ARGS(cmd, addr, size));

/*
 * Logging of start of read or write mmc block operation,
 * including cmd, address, size
 */
DECLARE_EVENT_CLASS(mmc_blk_rw_class,
	TP_PROTO(unsigned int cmd, unsigned int addr, struct mmc_data *data),
	TP_ARGS(cmd, addr, data),
	TP_STRUCT__entry(
		__field(unsigned int, cmd)
		__field(unsigned int, addr)
		__field(unsigned int, size)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->addr = addr;
		__entry->size = data->blocks;
	),
	TP_printk("cmd=%u,addr=0x%08x,size=0x%08x",
		  __entry->cmd, __entry->addr, __entry->size)
);

DEFINE_EVENT_CONDITION(mmc_blk_rw_class, mmc_blk_rw_start,
	TP_PROTO(unsigned int cmd, unsigned int addr, struct mmc_data *data),
	TP_ARGS(cmd, addr, data),
	TP_CONDITION(((cmd == MMC_READ_MULTIPLE_BLOCK) ||
		      (cmd == MMC_WRITE_MULTIPLE_BLOCK)) &&
		      data));

DEFINE_EVENT_CONDITION(mmc_blk_rw_class, mmc_blk_rw_end,
	TP_PROTO(unsigned int cmd, unsigned int addr, struct mmc_data *data),
	TP_ARGS(cmd, addr, data),
	TP_CONDITION(((cmd == MMC_READ_MULTIPLE_BLOCK) ||
		      (cmd == MMC_WRITE_MULTIPLE_BLOCK)) &&
		      data));
#endif /* _TRACE_MMC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
