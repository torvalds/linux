/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fib6

#if !defined(_TRACE_FIB6_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FIB6_H

#include <linux/in6.h>
#include <net/flow.h>
#include <net/ip6_fib.h>
#include <linux/tracepoint.h>

TRACE_EVENT(fib6_table_lookup,

	TP_PROTO(const struct net *net, const struct rt6_info *rt,
		 struct fib6_table *table, const struct flowi6 *flp),

	TP_ARGS(net, rt, table, flp),

	TP_STRUCT__entry(
		__field(	u32,	tb_id		)

		__field(	int,	oif		)
		__field(	int,	iif		)
		__field(	__u8,	tos		)
		__field(	__u8,	scope		)
		__field(	__u8,	flags		)
		__array(	__u8,	src,	16	)
		__array(	__u8,	dst,	16	)

		__dynamic_array(	char,	name,	IFNAMSIZ )
		__array(		__u8,	gw,	16	 )
	),

	TP_fast_assign(
		struct in6_addr *in6;

		__entry->tb_id = table->tb6_id;
		__entry->oif = flp->flowi6_oif;
		__entry->iif = flp->flowi6_iif;
		__entry->tos = ip6_tclass(flp->flowlabel);
		__entry->scope = flp->flowi6_scope;
		__entry->flags = flp->flowi6_flags;

		in6 = (struct in6_addr *)__entry->src;
		*in6 = flp->saddr;

		in6 = (struct in6_addr *)__entry->dst;
		*in6 = flp->daddr;

		if (rt->rt6i_idev) {
			__assign_str(name, rt->rt6i_idev->dev->name);
		} else {
			__assign_str(name, "");
		}
		if (rt == net->ipv6.ip6_null_entry) {
			struct in6_addr in6_zero = {};

			in6 = (struct in6_addr *)__entry->gw;
			*in6 = in6_zero;

		} else if (rt) {
			in6 = (struct in6_addr *)__entry->gw;
			*in6 = rt->rt6i_gateway;
		}
	),

	TP_printk("table %3u oif %d iif %d src %pI6c dst %pI6c tos %d scope %d flags %x ==> dev %s gw %pI6c",
		  __entry->tb_id, __entry->oif, __entry->iif,
		  __entry->src, __entry->dst, __entry->tos, __entry->scope,
		  __entry->flags, __get_str(name), __entry->gw)
);

#endif /* _TRACE_FIB6_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
