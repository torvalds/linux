/* AFS tracepoints
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
#define TRACE_SYSTEM afs

#if !defined(_TRACE_AFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_AFS_H

#include <linux/tracepoint.h>

TRACE_EVENT(afs_recv_data,
	    TP_PROTO(struct afs_call *call, unsigned count, unsigned offset,
		     bool want_more, int ret),

	    TP_ARGS(call, count, offset, want_more, ret),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	rxcall		)
		    __field(struct afs_call *,		call		)
		    __field(enum afs_call_state,	state		)
		    __field(unsigned int,		count		)
		    __field(unsigned int,		offset		)
		    __field(unsigned short,		unmarshall	)
		    __field(bool,			want_more	)
		    __field(int,			ret		)
			     ),

	    TP_fast_assign(
		    __entry->rxcall	= call->rxcall;
		    __entry->call	= call;
		    __entry->state	= call->state;
		    __entry->unmarshall	= call->unmarshall;
		    __entry->count	= count;
		    __entry->offset	= offset;
		    __entry->want_more	= want_more;
		    __entry->ret	= ret;
			   ),

	    TP_printk("c=%p ac=%p s=%u u=%u %u/%u wm=%u ret=%d",
		      __entry->rxcall,
		      __entry->call,
		      __entry->state, __entry->unmarshall,
		      __entry->offset, __entry->count,
		      __entry->want_more, __entry->ret)
	    );

TRACE_EVENT(afs_notify_call,
	    TP_PROTO(struct rxrpc_call *rxcall, struct afs_call *call),

	    TP_ARGS(rxcall, call),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	rxcall		)
		    __field(struct afs_call *,		call		)
		    __field(enum afs_call_state,	state		)
		    __field(unsigned short,		unmarshall	)
			     ),

	    TP_fast_assign(
		    __entry->rxcall	= rxcall;
		    __entry->call	= call;
		    __entry->state	= call->state;
		    __entry->unmarshall	= call->unmarshall;
			   ),

	    TP_printk("c=%p ac=%p s=%u u=%u",
		      __entry->rxcall,
		      __entry->call,
		      __entry->state, __entry->unmarshall)
	    );

TRACE_EVENT(afs_cb_call,
	    TP_PROTO(struct afs_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(struct rxrpc_call *,	rxcall		)
		    __field(struct afs_call *,		call		)
		    __field(const char *,		name		)
		    __field(u32,			op		)
			     ),

	    TP_fast_assign(
		    __entry->rxcall	= call->rxcall;
		    __entry->call	= call;
		    __entry->name	= call->type->name;
		    __entry->op		= call->operation_ID;
			   ),

	    TP_printk("c=%p ac=%p %s o=%u",
		      __entry->rxcall,
		      __entry->call,
		      __entry->name,
		      __entry->op)
	    );

#endif /* _TRACE_AFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
