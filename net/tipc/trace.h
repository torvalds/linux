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

int tipc_skb_dump(struct sk_buff *skb, bool more, char *buf);
int tipc_list_dump(struct sk_buff_head *list, bool more, char *buf);
int tipc_sk_dump(struct sock *sk, u16 dqueues, char *buf);
int tipc_link_dump(struct tipc_link *l, u16 dqueues, char *buf);
int tipc_node_dump(struct tipc_node *n, bool more, char *buf);

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

#define DEFINE_SK_EVENT(name) \
DEFINE_EVENT(tipc_sk_class, name, \
	TP_PROTO(struct sock *sk, struct sk_buff *skb, u16 dqueues, \
		 const char *header), \
	TP_ARGS(sk, skb, dqueues, header))
DEFINE_SK_EVENT(tipc_sk_dump);

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

#endif /* _TIPC_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
