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

/*
 * Define enums for tracing information.
 */
#ifndef __AFS_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __AFS_DECLARE_TRACE_ENUMS_ONCE_ONLY

enum afs_call_trace {
	afs_call_trace_alloc,
	afs_call_trace_free,
	afs_call_trace_put,
	afs_call_trace_wake,
	afs_call_trace_work,
};

#endif /* end __AFS_DECLARE_TRACE_ENUMS_ONCE_ONLY */

/*
 * Declare tracing information enums and their string mappings for display.
 */
#define afs_call_traces \
	EM(afs_call_trace_alloc,		"ALLOC") \
	EM(afs_call_trace_free,			"FREE ") \
	EM(afs_call_trace_put,			"PUT  ") \
	EM(afs_call_trace_wake,			"WAKE ") \
	E_(afs_call_trace_work,			"WORK ")

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

afs_call_traces;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }

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

TRACE_EVENT(afs_call,
	    TP_PROTO(struct afs_call *call, enum afs_call_trace op,
		     int usage, int outstanding, const void *where),

	    TP_ARGS(call, op, usage, outstanding, where),

	    TP_STRUCT__entry(
		    __field(struct afs_call *,		call		)
		    __field(int,			op		)
		    __field(int,			usage		)
		    __field(int,			outstanding	)
		    __field(const void *,		where		)
			     ),

	    TP_fast_assign(
		    __entry->call = call;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->outstanding = outstanding;
		    __entry->where = where;
			   ),

	    TP_printk("c=%p %s u=%d o=%d sp=%pSR",
		      __entry->call,
		      __print_symbolic(__entry->op, afs_call_traces),
		      __entry->usage,
		      __entry->outstanding,
		      __entry->where)
	    );

#endif /* _TRACE_AFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
