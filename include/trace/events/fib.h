/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fib

#if !defined(_TRACE_FIB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FIB_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/ip_fib.h>
#include <linux/tracepoint.h>

TRACE_EVENT(fib_table_lookup,

	TP_PROTO(u32 tb_id, const struct flowi4 *flp,
		 const struct fib_nh *nh, int err),

	TP_ARGS(tb_id, flp, nh, err),

	TP_STRUCT__entry(
		__field(	u32,	tb_id		)
		__field(	int,	err		)
		__field(	int,	oif		)
		__field(	int,	iif		)
		__field(	u8,	proto		)
		__field(	__u8,	tos		)
		__field(	__u8,	scope		)
		__field(	__u8,	flags		)
		__array(	__u8,	src,	4	)
		__array(	__u8,	dst,	4	)
		__array(	__u8,	gw,	4	)
		__array(	__u8,	saddr,	4	)
		__field(	u16,	sport		)
		__field(	u16,	dport		)
		__dynamic_array(char,  name,   IFNAMSIZ )
	),

	TP_fast_assign(
		__be32 *p32;

		__entry->tb_id = tb_id;
		__entry->err = err;
		__entry->oif = flp->flowi4_oif;
		__entry->iif = flp->flowi4_iif;
		__entry->tos = flp->flowi4_tos;
		__entry->scope = flp->flowi4_scope;
		__entry->flags = flp->flowi4_flags;

		p32 = (__be32 *) __entry->src;
		*p32 = flp->saddr;

		p32 = (__be32 *) __entry->dst;
		*p32 = flp->daddr;

		__entry->proto = flp->flowi4_proto;
		if (__entry->proto == IPPROTO_TCP ||
		    __entry->proto == IPPROTO_UDP) {
			__entry->sport = ntohs(flp->fl4_sport);
			__entry->dport = ntohs(flp->fl4_dport);
		} else {
			__entry->sport = 0;
			__entry->dport = 0;
		}

		if (nh) {
			p32 = (__be32 *) __entry->saddr;
			*p32 = nh->nh_saddr;

			p32 = (__be32 *) __entry->gw;
			*p32 = nh->nh_gw;

			__assign_str(name, nh->nh_dev ? nh->nh_dev->name : "-");
		} else {
			p32 = (__be32 *) __entry->saddr;
			*p32 = 0;

			p32 = (__be32 *) __entry->gw;
			*p32 = 0;

			__assign_str(name, "-");
		}
	),

	TP_printk("table %u oif %d iif %d proto %u %pI4/%u -> %pI4/%u tos %d scope %d flags %x ==> dev %s gw %pI4 src %pI4 err %d",
		  __entry->tb_id, __entry->oif, __entry->iif, __entry->proto,
		  __entry->src, __entry->sport, __entry->dst, __entry->dport,
		  __entry->tos, __entry->scope, __entry->flags,
		  __get_str(name), __entry->gw, __entry->saddr, __entry->err)
);
#endif /* _TRACE_FIB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
