/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mdio

#if !defined(_TRACE_MDIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MDIO_H

#include <linux/tracepoint.h>

TRACE_EVENT_CONDITION(mdio_access,

	TP_PROTO(struct mii_bus *bus, char read,
		 u8 addr, unsigned regnum, u16 val, int err),

	TP_ARGS(bus, read, addr, regnum, val, err),

	TP_CONDITION(err >= 0),

	TP_STRUCT__entry(
		__array(char, busid, MII_BUS_ID_SIZE)
		__field(char, read)
		__field(u8, addr)
		__field(u16, val)
		__field(unsigned, regnum)
	),

	TP_fast_assign(
		strscpy(__entry->busid, bus->id, MII_BUS_ID_SIZE);
		__entry->read = read;
		__entry->addr = addr;
		__entry->regnum = regnum;
		__entry->val = val;
	),

	TP_printk("%s %-5s phy:0x%02hhx reg:0x%02x val:0x%04hx",
		  __entry->busid, __entry->read ? "read" : "write",
		  __entry->addr, __entry->regnum, __entry->val)
);

#endif /* if !defined(_TRACE_MDIO_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
