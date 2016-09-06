/* AF_RXRPC tracepoints
 *
 * Copyright (C) 2016 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rxrpc

#if !defined(_TRACE_RXRPC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RXRPC_H

#include <linux/tracepoint.h>

TRACE_EVENT(rxrpc_call,
	    TP_PROTO(struct rxrpc_call *call, enum rxrpc_call_trace op,
		     int usage, int nskb,
		     const void *where, const void *aux),

	    TP_ARGS(call, op, usage, nskb, where, aux),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	call		)
		    __field(int,			op		)
		    __field(int,			usage		)
		    __field(int,			nskb		)
		    __field(const void *,		where		)
		    __field(const void *,		aux		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->nskb = nskb;
		    __entry->where = where;
		    __entry->aux = aux;
			   ),

	    TP_printk("c=%p %s u=%d s=%d p=%pSR a=%p",
		      __entry->call,
		      rxrpc_call_traces[__entry->op],
		      __entry->usage,
		      __entry->nskb,
		      __entry->where,
		      __entry->aux)
	    );

TRACE_EVENT(rxrpc_skb,
	    TP_PROTO(struct sk_buff *skb, int op, int usage, int mod_count,
		     const void *where),

	    TP_ARGS(skb, op, usage, mod_count, where),

	    TP_STRUCT__entry(
		    __field(struct sk_buff *,		skb		)
		    __field(int,			op		)
		    __field(int,			usage		)
		    __field(int,			mod_count	)
		    __field(const void *,		where		)
			     ),

	    TP_fast_assign(
		    __entry->skb = skb;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->mod_count = mod_count;
		    __entry->where = where;
			   ),

	    TP_printk("s=%p %s u=%d m=%d p=%pSR",
		      __entry->skb,
		      (__entry->op == 0 ? "NEW" :
		       __entry->op == 1 ? "SEE" :
		       __entry->op == 2 ? "GET" :
		       __entry->op == 3 ? "FRE" :
		       "PUR"),
		      __entry->usage,
		      __entry->mod_count,
		      __entry->where)
	    );

TRACE_EVENT(rxrpc_abort,
	    TP_PROTO(const char *why, u32 cid, u32 call_id, rxrpc_seq_t seq,
		     int abort_code, int error),

	    TP_ARGS(why, cid, call_id, seq, abort_code, error),

	    TP_STRUCT__entry(
		    __array(char,			why, 4		)
		    __field(u32,			cid		)
		    __field(u32,			call_id		)
		    __field(rxrpc_seq_t,		seq		)
		    __field(int,			abort_code	)
		    __field(int,			error		)
			     ),

	    TP_fast_assign(
		    memcpy(__entry->why, why, 4);
		    __entry->cid = cid;
		    __entry->call_id = call_id;
		    __entry->abort_code = abort_code;
		    __entry->error = error;
		    __entry->seq = seq;
			   ),

	    TP_printk("%08x:%08x s=%u a=%d e=%d %s",
		      __entry->cid, __entry->call_id, __entry->seq,
		      __entry->abort_code, __entry->error, __entry->why)
	    );

#endif /* _TRACE_RXRPC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
