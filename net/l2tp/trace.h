/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM l2tp

#if !defined(_TRACE_L2TP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_L2TP_H

#include <linux/tracepoint.h>
#include <linux/l2tp.h>
#include "l2tp_core.h"

#define encap_type_name(e) { L2TP_ENCAPTYPE_##e, #e }
#define show_encap_type_name(val) \
	__print_symbolic(val, \
			encap_type_name(UDP), \
			encap_type_name(IP))

#define pw_type_name(p) { L2TP_PWTYPE_##p, #p }
#define show_pw_type_name(val) \
	__print_symbolic(val, \
	pw_type_name(ETH_VLAN), \
	pw_type_name(ETH), \
	pw_type_name(PPP), \
	pw_type_name(PPP_AC), \
	pw_type_name(IP))

DECLARE_EVENT_CLASS(tunnel_only_evt,
	TP_PROTO(struct l2tp_tunnel *tunnel),
	TP_ARGS(tunnel),
	TP_STRUCT__entry(
		__array(char, name, L2TP_TUNNEL_NAME_MAX)
	),
	TP_fast_assign(
		memcpy(__entry->name, tunnel->name, L2TP_TUNNEL_NAME_MAX);
	),
	TP_printk("%s", __entry->name)
);

DECLARE_EVENT_CLASS(session_only_evt,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session),
	TP_STRUCT__entry(
		__array(char, name, L2TP_SESSION_NAME_MAX)
	),
	TP_fast_assign(
		memcpy(__entry->name, session->name, L2TP_SESSION_NAME_MAX);
	),
	TP_printk("%s", __entry->name)
);

TRACE_EVENT(register_tunnel,
	TP_PROTO(struct l2tp_tunnel *tunnel),
	TP_ARGS(tunnel),
	TP_STRUCT__entry(
		__array(char, name, L2TP_TUNNEL_NAME_MAX)
		__field(int, fd)
		__field(u32, tid)
		__field(u32, ptid)
		__field(int, version)
		__field(enum l2tp_encap_type, encap)
	),
	TP_fast_assign(
		memcpy(__entry->name, tunnel->name, L2TP_TUNNEL_NAME_MAX);
		__entry->fd = tunnel->fd;
		__entry->tid = tunnel->tunnel_id;
		__entry->ptid = tunnel->peer_tunnel_id;
		__entry->version = tunnel->version;
		__entry->encap = tunnel->encap;
	),
	TP_printk("%s: type=%s encap=%s version=L2TPv%d tid=%u ptid=%u fd=%d",
		__entry->name,
		__entry->fd > 0 ? "managed" : "unmanaged",
		show_encap_type_name(__entry->encap),
		__entry->version,
		__entry->tid,
		__entry->ptid,
		__entry->fd)
);

DEFINE_EVENT(tunnel_only_evt, delete_tunnel,
	TP_PROTO(struct l2tp_tunnel *tunnel),
	TP_ARGS(tunnel)
);

DEFINE_EVENT(tunnel_only_evt, free_tunnel,
	TP_PROTO(struct l2tp_tunnel *tunnel),
	TP_ARGS(tunnel)
);

TRACE_EVENT(register_session,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session),
	TP_STRUCT__entry(
		__array(char, name, L2TP_SESSION_NAME_MAX)
		__field(u32, tid)
		__field(u32, ptid)
		__field(u32, sid)
		__field(u32, psid)
		__field(enum l2tp_pwtype, pwtype)
	),
	TP_fast_assign(
		memcpy(__entry->name, session->name, L2TP_SESSION_NAME_MAX);
		__entry->tid = session->tunnel ? session->tunnel->tunnel_id : 0;
		__entry->ptid = session->tunnel ? session->tunnel->peer_tunnel_id : 0;
		__entry->sid = session->session_id;
		__entry->psid = session->peer_session_id;
		__entry->pwtype = session->pwtype;
	),
	TP_printk("%s: pseudowire=%s sid=%u psid=%u tid=%u ptid=%u",
		__entry->name,
		show_pw_type_name(__entry->pwtype),
		__entry->sid,
		__entry->psid,
		__entry->sid,
		__entry->psid)
);

DEFINE_EVENT(session_only_evt, delete_session,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session)
);

DEFINE_EVENT(session_only_evt, free_session,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session)
);

DEFINE_EVENT(session_only_evt, session_seqnum_lns_enable,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session)
);

DEFINE_EVENT(session_only_evt, session_seqnum_lns_disable,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session)
);

DECLARE_EVENT_CLASS(session_seqnum_evt,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session),
	TP_STRUCT__entry(
		__array(char, name, L2TP_SESSION_NAME_MAX)
		__field(u32, ns)
		__field(u32, nr)
	),
	TP_fast_assign(
		memcpy(__entry->name, session->name, L2TP_SESSION_NAME_MAX);
		__entry->ns = session->ns;
		__entry->nr = session->nr;
	),
	TP_printk("%s: ns=%u nr=%u",
		__entry->name,
		__entry->ns,
		__entry->nr)
);

DEFINE_EVENT(session_seqnum_evt, session_seqnum_update,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session)
);

DEFINE_EVENT(session_seqnum_evt, session_seqnum_reset,
	TP_PROTO(struct l2tp_session *session),
	TP_ARGS(session)
);

DECLARE_EVENT_CLASS(session_pkt_discard_evt,
	TP_PROTO(struct l2tp_session *session, u32 pkt_ns),
	TP_ARGS(session, pkt_ns),
	TP_STRUCT__entry(
		__array(char, name, L2TP_SESSION_NAME_MAX)
		__field(u32, pkt_ns)
		__field(u32, my_nr)
		__field(u32, reorder_q_len)
	),
	TP_fast_assign(
		memcpy(__entry->name, session->name, L2TP_SESSION_NAME_MAX);
		__entry->pkt_ns = pkt_ns,
		__entry->my_nr = session->nr;
		__entry->reorder_q_len = skb_queue_len(&session->reorder_q);
	),
	TP_printk("%s: pkt_ns=%u my_nr=%u reorder_q_len=%u",
		__entry->name,
		__entry->pkt_ns,
		__entry->my_nr,
		__entry->reorder_q_len)
);

DEFINE_EVENT(session_pkt_discard_evt, session_pkt_expired,
	TP_PROTO(struct l2tp_session *session, u32 pkt_ns),
	TP_ARGS(session, pkt_ns)
);

DEFINE_EVENT(session_pkt_discard_evt, session_pkt_outside_rx_window,
	TP_PROTO(struct l2tp_session *session, u32 pkt_ns),
	TP_ARGS(session, pkt_ns)
);

DEFINE_EVENT(session_pkt_discard_evt, session_pkt_oos,
	TP_PROTO(struct l2tp_session *session, u32 pkt_ns),
	TP_ARGS(session, pkt_ns)
);

#endif /* _TRACE_L2TP_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
