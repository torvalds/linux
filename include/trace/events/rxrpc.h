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

#endif /* _TRACE_RXRPC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
