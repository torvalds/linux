#undef TRACE_SYSTEM
#define TRACE_SYSTEM fib

#if !defined(_TRACE_FIB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_FIB_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/ip_fib.h>
#include <linux/tracepoint.h>

TRACE_EVENT(fib_table_lookup,

	TP_PROTO(u32 tb_id, const struct flowi4 *flp),

	TP_ARGS(tb_id, flp),

	TP_STRUCT__entry(
		__field(	u32,	tb_id		)
		__field(	int,	oif		)
		__field(	int,	iif		)
		__field(	__u8,	tos		)
		__field(	__u8,	scope		)
		__field(	__u8,	flags		)
		__array(	__u8,	src,	4	)
		__array(	__u8,	dst,	4	)
	),

	TP_fast_assign(
		__be32 *p32;

		__entry->tb_id = tb_id;
		__entry->oif = flp->flowi4_oif;
		__entry->iif = flp->flowi4_iif;
		__entry->tos = flp->flowi4_tos;
		__entry->scope = flp->flowi4_scope;
		__entry->flags = flp->flowi4_flags;

		p32 = (__be32 *) __entry->src;
		*p32 = flp->saddr;

		p32 = (__be32 *) __entry->dst;
		*p32 = flp->daddr;
	),

	TP_printk("table %u oif %d iif %d src %pI4 dst %pI4 tos %d scope %d flags %x",
		  __entry->tb_id, __entry->oif, __entry->iif,
		  __entry->src, __entry->dst, __entry->tos, __entry->scope,
		  __entry->flags)
);

TRACE_EVENT(fib_table_lookup_nh,

	TP_PROTO(const struct fib_nh *nh),

	TP_ARGS(nh),

	TP_STRUCT__entry(
		__string(	name,	nh->nh_dev->name)
		__field(	int,	oif		)
		__array(	__u8,	src,	4	)
	),

	TP_fast_assign(
		__be32 *p32 = (__be32 *) __entry->src;

		__assign_str(name, nh->nh_dev ? nh->nh_dev->name : "not set");
		__entry->oif = nh->nh_oif;
		*p32 = nh->nh_saddr;
	),

	TP_printk("nexthop dev %s oif %d src %pI4",
		  __get_str(name), __entry->oif, __entry->src)
);

TRACE_EVENT(fib_validate_source,

	TP_PROTO(const struct net_device *dev, const struct flowi4 *flp),

	TP_ARGS(dev, flp),

	TP_STRUCT__entry(
		__string(	name,	dev->name	)
		__field(	int,	oif		)
		__field(	int,	iif		)
		__field(	__u8,	tos		)
		__array(	__u8,	src,	4	)
		__array(	__u8,	dst,	4	)
	),

	TP_fast_assign(
		__be32 *p32;

		__assign_str(name, dev ? dev->name : "not set");
		__entry->oif = flp->flowi4_oif;
		__entry->iif = flp->flowi4_iif;
		__entry->tos = flp->flowi4_tos;

		p32 = (__be32 *) __entry->src;
		*p32 = flp->saddr;

		p32 = (__be32 *) __entry->dst;
		*p32 = flp->daddr;
	),

	TP_printk("dev %s oif %d iif %d tos %d src %pI4 dst %pI4",
		  __get_str(name), __entry->oif, __entry->iif, __entry->tos,
		  __entry->src, __entry->dst)
);
#endif /* _TRACE_FIB_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
