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

	TP_PROTO(const struct net *net, const struct fib6_result *res,
		 struct fib6_table *table, const struct flowi6 *flp),

	TP_ARGS(net, res, table, flp),

	TP_STRUCT__entry(
		__field(	u32,	tb_id		)
		__field(	int,	err		)
		__field(	int,	oif		)
		__field(	int,	iif		)
		__field(	__u8,	tos		)
		__field(	__u8,	scope		)
		__field(	__u8,	flags		)
		__array(	__u8,	src,	16	)
		__array(	__u8,	dst,	16	)
		__field(        u16,	sport		)
		__field(        u16,	dport		)
		__field(        u8,	proto		)
		__field(        u8,	rt_type		)
		__dynamic_array(	char,	name,	IFNAMSIZ )
		__array(		__u8,	gw,	16	 )
	),

	TP_fast_assign(
		struct in6_addr *in6;

		__entry->tb_id = table->tb6_id;
		__entry->err = ip6_rt_type_to_error(res->fib6_type);
		__entry->oif = flp->flowi6_oif;
		__entry->iif = flp->flowi6_iif;
		__entry->tos = ip6_tclass(flp->flowlabel);
		__entry->scope = flp->flowi6_scope;
		__entry->flags = flp->flowi6_flags;

		in6 = (struct in6_addr *)__entry->src;
		*in6 = flp->saddr;

		in6 = (struct in6_addr *)__entry->dst;
		*in6 = flp->daddr;

		__entry->proto = flp->flowi6_proto;
		if (__entry->proto == IPPROTO_TCP ||
		    __entry->proto == IPPROTO_UDP) {
			__entry->sport = ntohs(flp->fl6_sport);
			__entry->dport = ntohs(flp->fl6_dport);
		} else {
			__entry->sport = 0;
			__entry->dport = 0;
		}

		if (res->nh && res->nh->fib_nh_dev) {
			__assign_str(name, res->nh->fib_nh_dev);
		} else {
			__assign_str(name, "-");
		}
		if (res->f6i == net->ipv6.fib6_null_entry) {
			struct in6_addr in6_zero = {};

			in6 = (struct in6_addr *)__entry->gw;
			*in6 = in6_zero;

		} else if (res->nh) {
			in6 = (struct in6_addr *)__entry->gw;
			*in6 = res->nh->fib_nh_gw6;
		}
	),

	TP_printk("table %3u oif %d iif %d proto %u %pI6c/%u -> %pI6c/%u tos %d scope %d flags %x ==> dev %s gw %pI6c err %d",
		  __entry->tb_id, __entry->oif, __entry->iif, __entry->proto,
		  __entry->src, __entry->sport, __entry->dst, __entry->dport,
		  __entry->tos, __entry->scope, __entry->flags,
		  __get_str(name), __entry->gw, __entry->err)
);

#endif /* _TRACE_FIB6_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
