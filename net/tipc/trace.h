/*
 * net/tipc/trace.h: TIPC tracepoints
 *
 * Copyright (c) 2018, Ericsson AB
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "ASIS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tipc

#if !defined(_TIPC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TIPC_TRACE_H

#include <linux/tracepoint.h>
#include "core.h"
#include "link.h"
#include "socket.h"
#include "node.h"

#define SKB_LMIN	(100)
#define SKB_LMAX	(SKB_LMIN * 2)
#define LIST_LMIN	(SKB_LMIN * 3)
#define LIST_LMAX	(SKB_LMIN * 11)
#define SK_LMIN		(SKB_LMIN * 2)
#define SK_LMAX		(SKB_LMIN * 11)
#define LINK_LMIN	(SKB_LMIN)
#define LINK_LMAX	(SKB_LMIN * 16)
#define NODE_LMIN	(SKB_LMIN)
#define NODE_LMAX	(SKB_LMIN * 11)

#ifndef __TIPC_TRACE_ENUM
#define __TIPC_TRACE_ENUM
enum {
	TIPC_DUMP_NONE		= 0,

	TIPC_DUMP_TRANSMQ	= 1,
	TIPC_DUMP_BACKLOGQ	= (1 << 1),
	TIPC_DUMP_DEFERDQ	= (1 << 2),
	TIPC_DUMP_INPUTQ	= (1 << 3),
	TIPC_DUMP_WAKEUP        = (1 << 4),

	TIPC_DUMP_SK_SNDQ	= (1 << 8),
	TIPC_DUMP_SK_RCVQ	= (1 << 9),
	TIPC_DUMP_SK_BKLGQ	= (1 << 10),
	TIPC_DUMP_ALL		= 0xffffu
};
#endif

/* Link & Node FSM states: */
#define state_sym(val)							  \
	__print_symbolic(val,						  \
			{(0xe),		"ESTABLISHED"			},\
			{(0xe << 4),	"ESTABLISHING"			},\
			{(0x1 << 8),	"RESET"				},\
			{(0x2 << 12),	"RESETTING"			},\
			{(0xd << 16),	"PEER_RESET"			},\
			{(0xf << 20),	"FAILINGOVER"			},\
			{(0xc << 24),	"SYNCHING"			},\
			{(0xdd),	"SELF_DOWN_PEER_DOWN"		},\
			{(0xaa),	"SELF_UP_PEER_UP"		},\
			{(0xd1),	"SELF_DOWN_PEER_LEAVING"	},\
			{(0xac),	"SELF_UP_PEER_COMING"		},\
			{(0xca),	"SELF_COMING_PEER_UP"		},\
			{(0x1d),	"SELF_LEAVING_PEER_DOWN"	},\
			{(0xf0),	"FAILINGOVER"			},\
			{(0xcc),	"SYNCHING"			})

/* Link & Node FSM events: */
#define evt_sym(val)							  \
	__print_symbolic(val,						  \
			{(0xec1ab1e),	"ESTABLISH_EVT"			},\
			{(0x9eed0e),	"PEER_RESET_EVT"		},\
			{(0xfa110e),	"FAILURE_EVT"			},\
			{(0x10ca1d0e),	"RESET_EVT"			},\
			{(0xfa110bee),	"FAILOVER_BEGIN_EVT"		},\
			{(0xfa110ede),	"FAILOVER_END_EVT"		},\
			{(0xc1ccbee),	"SYNCH_BEGIN_EVT"		},\
			{(0xc1ccede),	"SYNCH_END_EVT"			},\
			{(0xece),	"SELF_ESTABL_CONTACT_EVT"	},\
			{(0x1ce),	"SELF_LOST_CONTACT_EVT"		},\
			{(0x9ece),	"PEER_ESTABL_CONTACT_EVT"	},\
			{(0x91ce),	"PEER_LOST_CONTACT_EVT"		},\
			{(0xfbe),	"FAILOVER_BEGIN_EVT"		},\
			{(0xfee),	"FAILOVER_END_EVT"		},\
			{(0xcbe),	"SYNCH_BEGIN_EVT"		},\
			{(0xcee),	"SYNCH_END_EVT"			})

/* Bearer, net device events: */
#define dev_evt_sym(val)						  \
	__print_symbolic(val,						  \
			{(NETDEV_CHANGE),	"NETDEV_CHANGE"		},\
			{(NETDEV_GOING_DOWN),	"NETDEV_GOING_DOWN"	},\
			{(NETDEV_UP),		"NETDEV_UP"		},\
			{(NETDEV_CHANGEMTU),	"NETDEV_CHANGEMTU"	},\
			{(NETDEV_CHANGEADDR),	"NETDEV_CHANGEADDR"	},\
			{(NETDEV_UNREGISTER),	"NETDEV_UNREGISTER"	},\
			{(NETDEV_CHANGENAME),	"NETDEV_CHANGENAME"	})

extern unsigned long sysctl_tipc_sk_filter[5] __read_mostly;

int tipc_skb_dump(struct sk_buff *skb, bool more, char *buf);
int tipc_list_dump(struct sk_buff_head *list, bool more, char *buf);
int tipc_sk_dump(struct sock *sk, u16 dqueues, char *buf);
int tipc_link_dump(struct tipc_link *l, u16 dqueues, char *buf);
int tipc_node_dump(struct tipc_node *n, bool more, char *buf);
bool tipc_sk_filtering(struct sock *sk);

DECLARE_EVENT_CLASS(tipc_skb_class,

	TP_PROTO(struct sk_buff *skb, bool more, const char *header),

	TP_ARGS(skb, more, header),

	TP_STRUCT__entry(
		__string(header, header)
		__dynamic_array(char, buf, (more) ? SKB_LMAX : SKB_LMIN)
	),

	TP_fast_assign(
		__assign_str(header, header);
		tipc_skb_dump(skb, more, __get_str(buf));
	),

	TP_printk("%s\n%s", __get_str(header), __get_str(buf))
)

#define DEFINE_SKB_EVENT(name) \
DEFINE_EVENT(tipc_skb_class, name, \
	TP_PROTO(struct sk_buff *skb, bool more, const char *header), \
	TP_ARGS(skb, more, header))
DEFINE_SKB_EVENT(tipc_skb_dump);
DEFINE_SKB_EVENT(tipc_proto_build);
DEFINE_SKB_EVENT(tipc_proto_rcv);

DECLARE_EVENT_CLASS(tipc_list_class,

	TP_PROTO(struct sk_buff_head *list, bool more, const char *header),

	TP_ARGS(list, more, header),

	TP_STRUCT__entry(
		__string(header, header)
		__dynamic_array(char, buf, (more) ? LIST_LMAX : LIST_LMIN)
	),

	TP_fast_assign(
		__assign_str(header, header);
		tipc_list_dump(list, more, __get_str(buf));
	),

	TP_printk("%s\n%s", __get_str(header), __get_str(buf))
);

#define DEFINE_LIST_EVENT(name) \
DEFINE_EVENT(tipc_list_class, name, \
	TP_PROTO(struct sk_buff_head *list, bool more, const char *header), \
	TP_ARGS(list, more, header))
DEFINE_LIST_EVENT(tipc_list_dump);

DECLARE_EVENT_CLASS(tipc_sk_class,

	TP_PROTO(struct sock *sk, struct sk_buff *skb, u16 dqueues,
		 const char *header),

	TP_ARGS(sk, skb, dqueues, header),

	TP_STRUCT__entry(
		__string(header, header)
		__field(u32, portid)
		__dynamic_array(char, buf, (dqueues) ? SK_LMAX : SK_LMIN)
		__dynamic_array(char, skb_buf, (skb) ? SKB_LMIN : 1)
	),

	TP_fast_assign(
		__assign_str(header, header);
		__entry->portid = tipc_sock_get_portid(sk);
		tipc_sk_dump(sk, dqueues, __get_str(buf));
		if (skb)
			tipc_skb_dump(skb, false, __get_str(skb_buf));
		else
			*(__get_str(skb_buf)) = '\0';
	),

	TP_printk("<%u> %s\n%s%s", __entry->portid, __get_str(header),
		  __get_str(skb_buf), __get_str(buf))
);

#define DEFINE_SK_EVENT_FILTER(name) \
DEFINE_EVENT_CONDITION(tipc_sk_class, name, \
	TP_PROTO(struct sock *sk, struct sk_buff *skb, u16 dqueues, \
		 const char *header), \
	TP_ARGS(sk, skb, dqueues, header), \
	TP_CONDITION(tipc_sk_filtering(sk)))
DEFINE_SK_EVENT_FILTER(tipc_sk_dump);
DEFINE_SK_EVENT_FILTER(tipc_sk_create);
DEFINE_SK_EVENT_FILTER(tipc_sk_sendmcast);
DEFINE_SK_EVENT_FILTER(tipc_sk_sendmsg);
DEFINE_SK_EVENT_FILTER(tipc_sk_sendstream);
DEFINE_SK_EVENT_FILTER(tipc_sk_poll);
DEFINE_SK_EVENT_FILTER(tipc_sk_filter_rcv);
DEFINE_SK_EVENT_FILTER(tipc_sk_advance_rx);
DEFINE_SK_EVENT_FILTER(tipc_sk_rej_msg);
DEFINE_SK_EVENT_FILTER(tipc_sk_drop_msg);
DEFINE_SK_EVENT_FILTER(tipc_sk_release);
DEFINE_SK_EVENT_FILTER(tipc_sk_shutdown);

#define DEFINE_SK_EVENT_FILTER_COND(name, cond) \
DEFINE_EVENT_CONDITION(tipc_sk_class, name, \
	TP_PROTO(struct sock *sk, struct sk_buff *skb, u16 dqueues, \
		 const char *header), \
	TP_ARGS(sk, skb, dqueues, header), \
	TP_CONDITION(tipc_sk_filtering(sk) && (cond)))
DEFINE_SK_EVENT_FILTER_COND(tipc_sk_overlimit1, tipc_sk_overlimit1(sk, skb));
DEFINE_SK_EVENT_FILTER_COND(tipc_sk_overlimit2, tipc_sk_overlimit2(sk, skb));

DECLARE_EVENT_CLASS(tipc_link_class,

	TP_PROTO(struct tipc_link *l, u16 dqueues, const char *header),

	TP_ARGS(l, dqueues, header),

	TP_STRUCT__entry(
		__string(header, header)
		__array(char, name, TIPC_MAX_LINK_NAME)
		__dynamic_array(char, buf, (dqueues) ? LINK_LMAX : LINK_LMIN)
	),

	TP_fast_assign(
		__assign_str(header, header);
		tipc_link_name_ext(l, __entry->name);
		tipc_link_dump(l, dqueues, __get_str(buf));
	),

	TP_printk("<%s> %s\n%s", __entry->name, __get_str(header),
		  __get_str(buf))
);

#define DEFINE_LINK_EVENT(name) \
DEFINE_EVENT(tipc_link_class, name, \
	TP_PROTO(struct tipc_link *l, u16 dqueues, const char *header), \
	TP_ARGS(l, dqueues, header))
DEFINE_LINK_EVENT(tipc_link_dump);
DEFINE_LINK_EVENT(tipc_link_conges);
DEFINE_LINK_EVENT(tipc_link_timeout);
DEFINE_LINK_EVENT(tipc_link_reset);

#define DEFINE_LINK_EVENT_COND(name, cond) \
DEFINE_EVENT_CONDITION(tipc_link_class, name, \
	TP_PROTO(struct tipc_link *l, u16 dqueues, const char *header), \
	TP_ARGS(l, dqueues, header), \
	TP_CONDITION(cond))
DEFINE_LINK_EVENT_COND(tipc_link_too_silent, tipc_link_too_silent(l));

DECLARE_EVENT_CLASS(tipc_link_transmq_class,

	TP_PROTO(struct tipc_link *r, u16 f, u16 t, struct sk_buff_head *tq),

	TP_ARGS(r, f, t, tq),

	TP_STRUCT__entry(
		__array(char, name, TIPC_MAX_LINK_NAME)
		__field(u16, from)
		__field(u16, to)
		__field(u32, len)
		__field(u16, fseqno)
		__field(u16, lseqno)
	),

	TP_fast_assign(
		tipc_link_name_ext(r, __entry->name);
		__entry->from = f;
		__entry->to = t;
		__entry->len = skb_queue_len(tq);
		__entry->fseqno = msg_seqno(buf_msg(skb_peek(tq)));
		__entry->lseqno = msg_seqno(buf_msg(skb_peek_tail(tq)));
	),

	TP_printk("<%s> retrans req: [%u-%u] transmq: %u [%u-%u]\n",
		  __entry->name, __entry->from, __entry->to,
		  __entry->len, __entry->fseqno, __entry->lseqno)
);

DEFINE_EVENT(tipc_link_transmq_class, tipc_link_retrans,
	TP_PROTO(struct tipc_link *r, u16 f, u16 t, struct sk_buff_head *tq),
	TP_ARGS(r, f, t, tq)
);

DEFINE_EVENT_PRINT(tipc_link_transmq_class, tipc_link_bc_ack,
	TP_PROTO(struct tipc_link *r, u16 f, u16 t, struct sk_buff_head *tq),
	TP_ARGS(r, f, t, tq),
	TP_printk("<%s> acked: [%u-%u] transmq: %u [%u-%u]\n",
		  __entry->name, __entry->from, __entry->to,
		  __entry->len, __entry->fseqno, __entry->lseqno)
);

DECLARE_EVENT_CLASS(tipc_node_class,

	TP_PROTO(struct tipc_node *n, bool more, const char *header),

	TP_ARGS(n, more, header),

	TP_STRUCT__entry(
		__string(header, header)
		__field(u32, addr)
		__dynamic_array(char, buf, (more) ? NODE_LMAX : NODE_LMIN)
	),

	TP_fast_assign(
		__assign_str(header, header);
		__entry->addr = tipc_node_get_addr(n);
		tipc_node_dump(n, more, __get_str(buf));
	),

	TP_printk("<%x> %s\n%s", __entry->addr, __get_str(header),
		  __get_str(buf))
);

#define DEFINE_NODE_EVENT(name) \
DEFINE_EVENT(tipc_node_class, name, \
	TP_PROTO(struct tipc_node *n, bool more, const char *header), \
	TP_ARGS(n, more, header))
DEFINE_NODE_EVENT(tipc_node_dump);
DEFINE_NODE_EVENT(tipc_node_create);
DEFINE_NODE_EVENT(tipc_node_delete);
DEFINE_NODE_EVENT(tipc_node_lost_contact);
DEFINE_NODE_EVENT(tipc_node_timeout);
DEFINE_NODE_EVENT(tipc_node_link_up);
DEFINE_NODE_EVENT(tipc_node_link_down);
DEFINE_NODE_EVENT(tipc_node_reset_links);
DEFINE_NODE_EVENT(tipc_node_check_state);

DECLARE_EVENT_CLASS(tipc_fsm_class,

	TP_PROTO(const char *name, u32 os, u32 ns, int evt),

	TP_ARGS(name, os, ns, evt),

	TP_STRUCT__entry(
		__string(name, name)
		__field(u32, os)
		__field(u32, ns)
		__field(u32, evt)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->os = os;
		__entry->ns = ns;
		__entry->evt = evt;
	),

	TP_printk("<%s> %s--(%s)->%s\n", __get_str(name),
		  state_sym(__entry->os), evt_sym(__entry->evt),
		  state_sym(__entry->ns))
);

#define DEFINE_FSM_EVENT(fsm_name) \
DEFINE_EVENT(tipc_fsm_class, fsm_name, \
	TP_PROTO(const char *name, u32 os, u32 ns, int evt), \
	TP_ARGS(name, os, ns, evt))
DEFINE_FSM_EVENT(tipc_link_fsm);
DEFINE_FSM_EVENT(tipc_node_fsm);

TRACE_EVENT(tipc_l2_device_event,

	TP_PROTO(struct net_device *dev, struct tipc_bearer *b,
		 unsigned long evt),

	TP_ARGS(dev, b, evt),

	TP_STRUCT__entry(
		__string(dev_name, dev->name)
		__string(b_name, b->name)
		__field(unsigned long, evt)
		__field(u8, b_up)
		__field(u8, carrier)
		__field(u8, oper)
	),

	TP_fast_assign(
		__assign_str(dev_name, dev->name);
		__assign_str(b_name, b->name);
		__entry->evt = evt;
		__entry->b_up = test_bit(0, &b->up);
		__entry->carrier = netif_carrier_ok(dev);
		__entry->oper = netif_oper_up(dev);
	),

	TP_printk("%s on: <%s>/<%s> oper: %s carrier: %s bearer: %s\n",
		  dev_evt_sym(__entry->evt), __get_str(dev_name),
		  __get_str(b_name), (__entry->oper) ? "up" : "down",
		  (__entry->carrier) ? "ok" : "notok",
		  (__entry->b_up) ? "up" : "down")
);

#endif /* _TIPC_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
