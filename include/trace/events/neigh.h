#undef TRACE_SYSTEM
#define TRACE_SYSTEM neigh

#if !defined(_TRACE_NEIGH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NEIGH_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/tracepoint.h>
#include <net/neighbour.h>

#define neigh_state_str(state)				\
	__print_symbolic(state,				\
		{ NUD_INCOMPLETE, "incomplete" },	\
		{ NUD_REACHABLE, "reachable" },		\
		{ NUD_STALE, "stale" },			\
		{ NUD_DELAY, "delay" },			\
		{ NUD_PROBE, "probe" },			\
		{ NUD_FAILED, "failed" },		\
		{ NUD_NOARP, "noarp" },			\
		{ NUD_PERMANENT, "permanent"})

TRACE_EVENT(neigh_create,

	TP_PROTO(struct neigh_table *tbl, struct net_device *dev,
		 const void *pkey, const struct neighbour *n,
		 bool exempt_from_gc),

	TP_ARGS(tbl, dev, pkey, n, exempt_from_gc),

	TP_STRUCT__entry(
		__field(u32, family)
		__string(dev, dev ? dev->name : "NULL")
		__field(int, entries)
		__field(u8, created)
		__field(u8, gc_exempt)
		__array(u8, primary_key4, 4)
		__array(u8, primary_key6, 16)
	),

	TP_fast_assign(
		__be32 *p32;

		__entry->family = tbl->family;
		__assign_str(dev);
		__entry->entries = atomic_read(&tbl->gc_entries);
		__entry->created = n != NULL;
		__entry->gc_exempt = exempt_from_gc;
		p32 = (__be32 *)__entry->primary_key4;

		if (tbl->family == AF_INET)
			*p32 = *(__be32 *)pkey;
		else
			*p32 = 0;

#if IS_ENABLED(CONFIG_IPV6)
		if (tbl->family == AF_INET6) {
			struct in6_addr *pin6;

			pin6 = (struct in6_addr *)__entry->primary_key6;
			*pin6 = *(struct in6_addr *)pkey;
		}
#endif
	),

	TP_printk("family %d dev %s entries %d primary_key4 %pI4 primary_key6 %pI6c created %d gc_exempt %d",
		  __entry->family, __get_str(dev), __entry->entries,
		  __entry->primary_key4, __entry->primary_key6,
		  __entry->created, __entry->gc_exempt)
);

TRACE_EVENT(neigh_update,

	TP_PROTO(struct neighbour *n, const u8 *lladdr, u8 new,
		 u32 flags, u32 nlmsg_pid),

	TP_ARGS(n, lladdr, new, flags, nlmsg_pid),

	TP_STRUCT__entry(
		__field(u32, family)
		__string(dev, (n->dev ? n->dev->name : "NULL"))
		__array(u8, lladdr, MAX_ADDR_LEN)
		__field(u8, lladdr_len)
		__field(u8, flags)
		__field(u8, nud_state)
		__field(u8, type)
		__field(u8, dead)
		__field(int, refcnt)
		__array(__u8, primary_key4, 4)
		__array(__u8, primary_key6, 16)
		__field(unsigned long, confirmed)
		__field(unsigned long, updated)
		__field(unsigned long, used)
		__array(u8, new_lladdr, MAX_ADDR_LEN)
		__field(u8, new_state)
		__field(u32, update_flags)
		__field(u32, pid)
	),

	TP_fast_assign(
		int lladdr_len = (n->dev ? n->dev->addr_len : MAX_ADDR_LEN);
		struct in6_addr *pin6;
		__be32 *p32;

		__entry->family = n->tbl->family;
		__assign_str(dev);
		__entry->lladdr_len = lladdr_len;
		memcpy(__entry->lladdr, n->ha, lladdr_len);
		__entry->flags = n->flags;
		__entry->nud_state = n->nud_state;
		__entry->type = n->type;
		__entry->dead = n->dead;
		__entry->refcnt = refcount_read(&n->refcnt);
		pin6 = (struct in6_addr *)__entry->primary_key6;
		p32 = (__be32 *)__entry->primary_key4;

		if (n->tbl->family == AF_INET)
			*p32 = *(__be32 *)n->primary_key;
		else
			*p32 = 0;

#if IS_ENABLED(CONFIG_IPV6)
		if (n->tbl->family == AF_INET6) {
			pin6 = (struct in6_addr *)__entry->primary_key6;
			*pin6 = *(struct in6_addr *)n->primary_key;
		} else
#endif
		{
			ipv6_addr_set_v4mapped(*p32, pin6);
		}
		__entry->confirmed = n->confirmed;
		__entry->updated = n->updated;
		__entry->used = n->used;
		if (lladdr)
			memcpy(__entry->new_lladdr, lladdr, lladdr_len);
		__entry->new_state = new;
		__entry->update_flags = flags;
		__entry->pid = nlmsg_pid;
	),

	TP_printk("family %d dev %s lladdr %s flags %02x nud_state %s type %02x "
		  "dead %d refcnt %d primary_key4 %pI4 primary_key6 %pI6c "
		  "confirmed %lu updated %lu used %lu new_lladdr %s "
		  "new_state %s update_flags %02x pid %d",
		  __entry->family, __get_str(dev),
		  __print_hex_str(__entry->lladdr, __entry->lladdr_len),
		  __entry->flags, neigh_state_str(__entry->nud_state),
		  __entry->type, __entry->dead, __entry->refcnt,
		  __entry->primary_key4, __entry->primary_key6,
		  __entry->confirmed, __entry->updated, __entry->used,
		  __print_hex_str(__entry->new_lladdr, __entry->lladdr_len),
		  neigh_state_str(__entry->new_state),
		  __entry->update_flags, __entry->pid)
);

DECLARE_EVENT_CLASS(neigh__update,
	TP_PROTO(struct neighbour *n, int err),
	TP_ARGS(n, err),
	TP_STRUCT__entry(
		__field(u32, family)
		__string(dev, (n->dev ? n->dev->name : "NULL"))
		__array(u8, lladdr, MAX_ADDR_LEN)
		__field(u8, lladdr_len)
		__field(u8, flags)
		__field(u8, nud_state)
		__field(u8, type)
		__field(u8, dead)
		__field(int, refcnt)
		__array(__u8, primary_key4, 4)
		__array(__u8, primary_key6, 16)
		__field(unsigned long, confirmed)
		__field(unsigned long, updated)
		__field(unsigned long, used)
		__field(u32, err)
	),

	TP_fast_assign(
		int lladdr_len = (n->dev ? n->dev->addr_len : MAX_ADDR_LEN);
		struct in6_addr *pin6;
		__be32 *p32;

		__entry->family = n->tbl->family;
		__assign_str(dev);
		__entry->lladdr_len = lladdr_len;
		memcpy(__entry->lladdr, n->ha, lladdr_len);
		__entry->flags = n->flags;
		__entry->nud_state = n->nud_state;
		__entry->type = n->type;
		__entry->dead = n->dead;
		__entry->refcnt = refcount_read(&n->refcnt);
		pin6 = (struct in6_addr *)__entry->primary_key6;
		p32 = (__be32 *)__entry->primary_key4;

		if (n->tbl->family == AF_INET)
			*p32 = *(__be32 *)n->primary_key;
		else
			*p32 = 0;

#if IS_ENABLED(CONFIG_IPV6)
		if (n->tbl->family == AF_INET6) {
			pin6 = (struct in6_addr *)__entry->primary_key6;
			*pin6 = *(struct in6_addr *)n->primary_key;
		} else
#endif
		{
			ipv6_addr_set_v4mapped(*p32, pin6);
		}

		__entry->confirmed = n->confirmed;
		__entry->updated = n->updated;
		__entry->used = n->used;
		__entry->err = err;
	),

	TP_printk("family %d dev %s lladdr %s flags %02x nud_state %s type %02x "
		  "dead %d refcnt %d primary_key4 %pI4 primary_key6 %pI6c "
		  "confirmed %lu updated %lu used %lu err %d",
		  __entry->family, __get_str(dev),
		  __print_hex_str(__entry->lladdr, __entry->lladdr_len),
		  __entry->flags, neigh_state_str(__entry->nud_state),
		  __entry->type, __entry->dead, __entry->refcnt,
		  __entry->primary_key4, __entry->primary_key6,
		  __entry->confirmed, __entry->updated, __entry->used,
		  __entry->err)
);

DEFINE_EVENT(neigh__update, neigh_update_done,
	TP_PROTO(struct neighbour *neigh, int err),
	TP_ARGS(neigh, err)
);

DEFINE_EVENT(neigh__update, neigh_timer_handler,
	TP_PROTO(struct neighbour *neigh, int err),
	TP_ARGS(neigh, err)
);

DEFINE_EVENT(neigh__update, neigh_event_send_done,
	TP_PROTO(struct neighbour *neigh, int err),
	TP_ARGS(neigh, err)
);

DEFINE_EVENT(neigh__update, neigh_event_send_dead,
	TP_PROTO(struct neighbour *neigh, int err),
	TP_ARGS(neigh, err)
);

DEFINE_EVENT(neigh__update, neigh_cleanup_and_release,
	TP_PROTO(struct neighbour *neigh, int rc),
	TP_ARGS(neigh, rc)
);

#endif /* _TRACE_NEIGH_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
