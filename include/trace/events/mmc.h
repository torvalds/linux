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

/*
 * Logging of start of req(sbc) and req done of mmc  operation,
 * including cmd, args, size, resp, etc.
 */
DECLARE_EVENT_CLASS(start_req,
	TP_PROTO(const char * host, unsigned int cmd,
	         unsigned int arg, unsigned int flags,
		 unsigned int blksz, unsigned int blks),
	TP_ARGS(host, cmd, arg, flags, blksz, blks),

	TP_STRUCT__entry(
	    __string(host, host)
	    __field(unsigned int, cmd   )
	    __field(unsigned int, arg )
	    __field(unsigned int, flags )
	    __field(unsigned int, blksz )
	    __field(unsigned int, blks )
	   ),

	TP_fast_assign(
	    __assign_str(host, host);
	    __entry->cmd = cmd;
	    __entry->arg = arg;
	    __entry->flags = flags;
	    __entry->blksz = blksz;
	    __entry->blks = blks;
	),

	TP_printk("host=%s CMD%u arg=%08x flags=%08x blksz=%05x blks=%03x",
	       __get_str(host), __entry->cmd,
	      __entry->arg, __entry->flags,
	      __entry->blksz, __entry->blks )
);

DEFINE_EVENT(start_req, mmc_start_req_cmd,
	TP_PROTO(const char *host, unsigned int cmd,
	     unsigned int arg, unsigned int flags,
	     unsigned int blksz, unsigned int blks),
	TP_ARGS(host, cmd, arg, flags, blksz, blks)
);

DEFINE_EVENT(start_req, mmc_start_req_sbc,
	TP_PROTO(const char *host, unsigned int cmd,
	     unsigned int arg, unsigned int flags,
	     unsigned int blksz, unsigned int blks),
	TP_ARGS(host, cmd, arg, flags, blksz, blks)
);


DECLARE_EVENT_CLASS(req_done,
	TP_PROTO(const char *host, unsigned int cmd,
		int err, unsigned int resp1,
		unsigned int resp2, unsigned int resp3,
		unsigned int resp4),
	TP_ARGS(host, cmd, err, resp1, resp2, resp3, resp4),

	TP_STRUCT__entry(
	    __string(host, host)
	    __field(unsigned int, cmd   )
	    __field(         int, err )
	    __field(unsigned int, resp1 )
	    __field(unsigned int, resp2 )
	    __field(unsigned int, resp3 )
	    __field(unsigned int, resp4 )
	   ),

	TP_fast_assign(
	    __assign_str(host, host);
	    __entry->cmd = cmd;
	    __entry->err = err;
	    __entry->resp1 = resp1;
	    __entry->resp2 = resp2;
	    __entry->resp3 = resp3;
	    __entry->resp4 = resp4;
	),

	TP_printk("host=%s CMD%u err=%08x resp1=%08x resp2=%08x resp3=%08x resp4=%08x",
		__get_str(host), __entry->cmd,
		__entry->err, __entry->resp1,
		__entry->resp2, __entry->resp3,
		__entry->resp4 )
);

DEFINE_EVENT(req_done, mmc_req_done,
	TP_PROTO(const char *host, unsigned int cmd,
		int err, unsigned int resp1,
		unsigned int resp2, unsigned int resp3,
		unsigned int resp4),
	TP_ARGS(host, cmd, err, resp1, resp2, resp3, resp4)
);
#endif /* _TRACE_MMC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
