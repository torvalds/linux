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

enum afs_fs_operation {
	afs_FS_FetchData		= 130,	/* AFS Fetch file data */
	afs_FS_FetchStatus		= 132,	/* AFS Fetch file status */
	afs_FS_StoreData		= 133,	/* AFS Store file data */
	afs_FS_StoreStatus		= 135,	/* AFS Store file status */
	afs_FS_RemoveFile		= 136,	/* AFS Remove a file */
	afs_FS_CreateFile		= 137,	/* AFS Create a file */
	afs_FS_Rename			= 138,	/* AFS Rename or move a file or directory */
	afs_FS_Symlink			= 139,	/* AFS Create a symbolic link */
	afs_FS_Link			= 140,	/* AFS Create a hard link */
	afs_FS_MakeDir			= 141,	/* AFS Create a directory */
	afs_FS_RemoveDir		= 142,	/* AFS Remove a directory */
	afs_FS_GetVolumeInfo		= 148,	/* AFS Get information about a volume */
	afs_FS_GetVolumeStatus		= 149,	/* AFS Get volume status information */
	afs_FS_GetRootVolume		= 151,	/* AFS Get root volume name */
	afs_FS_SetLock			= 156,	/* AFS Request a file lock */
	afs_FS_ExtendLock		= 157,	/* AFS Extend a file lock */
	afs_FS_ReleaseLock		= 158,	/* AFS Release a file lock */
	afs_FS_Lookup			= 161,	/* AFS lookup file in directory */
	afs_FS_InlineBulkStatus		= 65536, /* AFS Fetch multiple file statuses with errors */
	afs_FS_FetchData64		= 65537, /* AFS Fetch file data */
	afs_FS_StoreData64		= 65538, /* AFS Store file data */
	afs_FS_GiveUpAllCallBacks	= 65539, /* AFS Give up all our callbacks on a server */
	afs_FS_GetCapabilities		= 65540, /* AFS Get FS server capabilities */
};

enum afs_vl_operation {
	afs_VL_GetEntryByNameU	= 527,		/* AFS Get Vol Entry By Name operation ID */
	afs_VL_GetAddrsU	= 533,		/* AFS Get FS server addresses */
	afs_YFSVL_GetEndpoints	= 64002,	/* YFS Get FS & Vol server addresses */
	afs_VL_GetCapabilities	= 65537,	/* AFS Get VL server capabilities */
};

enum afs_edit_dir_op {
	afs_edit_dir_create,
	afs_edit_dir_create_error,
	afs_edit_dir_create_inval,
	afs_edit_dir_create_nospc,
	afs_edit_dir_delete,
	afs_edit_dir_delete_error,
	afs_edit_dir_delete_inval,
	afs_edit_dir_delete_noent,
};

enum afs_edit_dir_reason {
	afs_edit_dir_for_create,
	afs_edit_dir_for_link,
	afs_edit_dir_for_mkdir,
	afs_edit_dir_for_rename,
	afs_edit_dir_for_rmdir,
	afs_edit_dir_for_symlink,
	afs_edit_dir_for_unlink,
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

#define afs_fs_operations \
	EM(afs_FS_FetchData,			"FS.FetchData") \
	EM(afs_FS_FetchStatus,			"FS.FetchStatus") \
	EM(afs_FS_StoreData,			"FS.StoreData") \
	EM(afs_FS_StoreStatus,			"FS.StoreStatus") \
	EM(afs_FS_RemoveFile,			"FS.RemoveFile") \
	EM(afs_FS_CreateFile,			"FS.CreateFile") \
	EM(afs_FS_Rename,			"FS.Rename") \
	EM(afs_FS_Symlink,			"FS.Symlink") \
	EM(afs_FS_Link,				"FS.Link") \
	EM(afs_FS_MakeDir,			"FS.MakeDir") \
	EM(afs_FS_RemoveDir,			"FS.RemoveDir") \
	EM(afs_FS_GetVolumeInfo,		"FS.GetVolumeInfo") \
	EM(afs_FS_GetVolumeStatus,		"FS.GetVolumeStatus") \
	EM(afs_FS_GetRootVolume,		"FS.GetRootVolume") \
	EM(afs_FS_SetLock,			"FS.SetLock") \
	EM(afs_FS_ExtendLock,			"FS.ExtendLock") \
	EM(afs_FS_ReleaseLock,			"FS.ReleaseLock") \
	EM(afs_FS_Lookup,			"FS.Lookup") \
	EM(afs_FS_InlineBulkStatus,		"FS.InlineBulkStatus") \
	EM(afs_FS_FetchData64,			"FS.FetchData64") \
	EM(afs_FS_StoreData64,			"FS.StoreData64") \
	EM(afs_FS_GiveUpAllCallBacks,		"FS.GiveUpAllCallBacks") \
	E_(afs_FS_GetCapabilities,		"FS.GetCapabilities")

#define afs_vl_operations \
	EM(afs_VL_GetEntryByNameU,		"VL.GetEntryByNameU") \
	EM(afs_VL_GetAddrsU,			"VL.GetAddrsU") \
	EM(afs_YFSVL_GetEndpoints,		"YFSVL.GetEndpoints") \
	E_(afs_VL_GetCapabilities,		"VL.GetCapabilities")

#define afs_edit_dir_ops				  \
	EM(afs_edit_dir_create,			"create") \
	EM(afs_edit_dir_create_error,		"c_fail") \
	EM(afs_edit_dir_create_inval,		"c_invl") \
	EM(afs_edit_dir_create_nospc,		"c_nspc") \
	EM(afs_edit_dir_delete,			"delete") \
	EM(afs_edit_dir_delete_error,		"d_err ") \
	EM(afs_edit_dir_delete_inval,		"d_invl") \
	E_(afs_edit_dir_delete_noent,		"d_nent")

#define afs_edit_dir_reasons				  \
	EM(afs_edit_dir_for_create,		"Create") \
	EM(afs_edit_dir_for_link,		"Link  ") \
	EM(afs_edit_dir_for_mkdir,		"MkDir ") \
	EM(afs_edit_dir_for_rename,		"Rename") \
	EM(afs_edit_dir_for_rmdir,		"RmDir ") \
	EM(afs_edit_dir_for_symlink,		"Symlnk") \
	E_(afs_edit_dir_for_unlink,		"Unlink")


/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

afs_call_traces;
afs_fs_operations;
afs_vl_operations;
afs_edit_dir_ops;
afs_edit_dir_reasons;

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
		    __field(unsigned int,		call		)
		    __field(enum afs_call_state,	state		)
		    __field(unsigned int,		count		)
		    __field(unsigned int,		offset		)
		    __field(unsigned short,		unmarshall	)
		    __field(bool,			want_more	)
		    __field(int,			ret		)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->state	= call->state;
		    __entry->unmarshall	= call->unmarshall;
		    __entry->count	= count;
		    __entry->offset	= offset;
		    __entry->want_more	= want_more;
		    __entry->ret	= ret;
			   ),

	    TP_printk("c=%08x s=%u u=%u %u/%u wm=%u ret=%d",
		      __entry->call,
		      __entry->state, __entry->unmarshall,
		      __entry->offset, __entry->count,
		      __entry->want_more, __entry->ret)
	    );

TRACE_EVENT(afs_notify_call,
	    TP_PROTO(struct rxrpc_call *rxcall, struct afs_call *call),

	    TP_ARGS(rxcall, call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_call_state,	state		)
		    __field(unsigned short,		unmarshall	)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->state	= call->state;
		    __entry->unmarshall	= call->unmarshall;
			   ),

	    TP_printk("c=%08x s=%u u=%u",
		      __entry->call,
		      __entry->state, __entry->unmarshall)
	    );

TRACE_EVENT(afs_cb_call,
	    TP_PROTO(struct afs_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(const char *,		name		)
		    __field(u32,			op		)
			     ),

	    TP_fast_assign(
		    __entry->call	= call->debug_id;
		    __entry->name	= call->type->name;
		    __entry->op		= call->operation_ID;
			   ),

	    TP_printk("c=%08x %s o=%u",
		      __entry->call,
		      __entry->name,
		      __entry->op)
	    );

TRACE_EVENT(afs_call,
	    TP_PROTO(struct afs_call *call, enum afs_call_trace op,
		     int usage, int outstanding, const void *where),

	    TP_ARGS(call, op, usage, outstanding, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(int,			op		)
		    __field(int,			usage		)
		    __field(int,			outstanding	)
		    __field(const void *,		where		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op = op;
		    __entry->usage = usage;
		    __entry->outstanding = outstanding;
		    __entry->where = where;
			   ),

	    TP_printk("c=%08x %s u=%d o=%d sp=%pSR",
		      __entry->call,
		      __print_symbolic(__entry->op, afs_call_traces),
		      __entry->usage,
		      __entry->outstanding,
		      __entry->where)
	    );

TRACE_EVENT(afs_make_fs_call,
	    TP_PROTO(struct afs_call *call, const struct afs_fid *fid),

	    TP_ARGS(call, fid),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_fs_operation,	op		)
		    __field_struct(struct afs_fid,	fid		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op = call->operation_ID;
		    if (fid) {
			    __entry->fid = *fid;
		    } else {
			    __entry->fid.vid = 0;
			    __entry->fid.vnode = 0;
			    __entry->fid.unique = 0;
		    }
			   ),

	    TP_printk("c=%08x %06x:%06x:%06x %s",
		      __entry->call,
		      __entry->fid.vid,
		      __entry->fid.vnode,
		      __entry->fid.unique,
		      __print_symbolic(__entry->op, afs_fs_operations))
	    );

TRACE_EVENT(afs_make_vl_call,
	    TP_PROTO(struct afs_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_vl_operation,	op		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->op = call->operation_ID;
			   ),

	    TP_printk("c=%08x %s",
		      __entry->call,
		      __print_symbolic(__entry->op, afs_vl_operations))
	    );

TRACE_EVENT(afs_call_done,
	    TP_PROTO(struct afs_call *call),

	    TP_ARGS(call),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(struct rxrpc_call *,	rx_call		)
		    __field(int,			ret		)
		    __field(u32,			abort_code	)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->rx_call = call->rxcall;
		    __entry->ret = call->error;
		    __entry->abort_code = call->abort_code;
			   ),

	    TP_printk("   c=%08x ret=%d ab=%d [%p]",
		      __entry->call,
		      __entry->ret,
		      __entry->abort_code,
		      __entry->rx_call)
	    );

TRACE_EVENT(afs_send_pages,
	    TP_PROTO(struct afs_call *call, struct msghdr *msg,
		     pgoff_t first, pgoff_t last, unsigned int offset),

	    TP_ARGS(call, msg, first, last, offset),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(pgoff_t,			first		)
		    __field(pgoff_t,			last		)
		    __field(unsigned int,		nr		)
		    __field(unsigned int,		bytes		)
		    __field(unsigned int,		offset		)
		    __field(unsigned int,		flags		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->first = first;
		    __entry->last = last;
		    __entry->nr = msg->msg_iter.nr_segs;
		    __entry->bytes = msg->msg_iter.count;
		    __entry->offset = offset;
		    __entry->flags = msg->msg_flags;
			   ),

	    TP_printk(" c=%08x %lx-%lx-%lx b=%x o=%x f=%x",
		      __entry->call,
		      __entry->first, __entry->first + __entry->nr - 1, __entry->last,
		      __entry->bytes, __entry->offset,
		      __entry->flags)
	    );

TRACE_EVENT(afs_sent_pages,
	    TP_PROTO(struct afs_call *call, pgoff_t first, pgoff_t last,
		     pgoff_t cursor, int ret),

	    TP_ARGS(call, first, last, cursor, ret),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(pgoff_t,			first		)
		    __field(pgoff_t,			last		)
		    __field(pgoff_t,			cursor		)
		    __field(int,			ret		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->first = first;
		    __entry->last = last;
		    __entry->cursor = cursor;
		    __entry->ret = ret;
			   ),

	    TP_printk(" c=%08x %lx-%lx c=%lx r=%d",
		      __entry->call,
		      __entry->first, __entry->last,
		      __entry->cursor, __entry->ret)
	    );

TRACE_EVENT(afs_dir_check_failed,
	    TP_PROTO(struct afs_vnode *vnode, loff_t off, loff_t i_size),

	    TP_ARGS(vnode, off, i_size),

	    TP_STRUCT__entry(
		    __field(struct afs_vnode *,		vnode		)
		    __field(loff_t,			off		)
		    __field(loff_t,			i_size		)
			     ),

	    TP_fast_assign(
		    __entry->vnode = vnode;
		    __entry->off = off;
		    __entry->i_size = i_size;
			   ),

	    TP_printk("vn=%p %llx/%llx",
		      __entry->vnode, __entry->off, __entry->i_size)
	    );

/*
 * We use page->private to hold the amount of the page that we've written to,
 * splitting the field into two parts.  However, we need to represent a range
 * 0...PAGE_SIZE inclusive, so we can't support 64K pages on a 32-bit system.
 */
#if PAGE_SIZE > 32768
#define AFS_PRIV_MAX	0xffffffff
#define AFS_PRIV_SHIFT	32
#else
#define AFS_PRIV_MAX	0xffff
#define AFS_PRIV_SHIFT	16
#endif

TRACE_EVENT(afs_page_dirty,
	    TP_PROTO(struct afs_vnode *vnode, const char *where,
		     pgoff_t page, unsigned long priv),

	    TP_ARGS(vnode, where, page, priv),

	    TP_STRUCT__entry(
		    __field(struct afs_vnode *,		vnode		)
		    __field(const char *,		where		)
		    __field(pgoff_t,			page		)
		    __field(unsigned long,		priv		)
			     ),

	    TP_fast_assign(
		    __entry->vnode = vnode;
		    __entry->where = where;
		    __entry->page = page;
		    __entry->priv = priv;
			   ),

	    TP_printk("vn=%p %lx %s %lu-%lu",
		      __entry->vnode, __entry->page, __entry->where,
		      __entry->priv & AFS_PRIV_MAX,
		      __entry->priv >> AFS_PRIV_SHIFT)
	    );

TRACE_EVENT(afs_call_state,
	    TP_PROTO(struct afs_call *call,
		     enum afs_call_state from,
		     enum afs_call_state to,
		     int ret, u32 remote_abort),

	    TP_ARGS(call, from, to, ret, remote_abort),

	    TP_STRUCT__entry(
		    __field(unsigned int,		call		)
		    __field(enum afs_call_state,	from		)
		    __field(enum afs_call_state,	to		)
		    __field(int,			ret		)
		    __field(u32,			abort		)
			     ),

	    TP_fast_assign(
		    __entry->call = call->debug_id;
		    __entry->from = from;
		    __entry->to = to;
		    __entry->ret = ret;
		    __entry->abort = remote_abort;
			   ),

	    TP_printk("c=%08x %u->%u r=%d ab=%d",
		      __entry->call,
		      __entry->from, __entry->to,
		      __entry->ret, __entry->abort)
	    );

TRACE_EVENT(afs_edit_dir,
	    TP_PROTO(struct afs_vnode *dvnode,
		     enum afs_edit_dir_reason why,
		     enum afs_edit_dir_op op,
		     unsigned int block,
		     unsigned int slot,
		     unsigned int f_vnode,
		     unsigned int f_unique,
		     const char *name),

	    TP_ARGS(dvnode, why, op, block, slot, f_vnode, f_unique, name),

	    TP_STRUCT__entry(
		    __field(unsigned int,		vnode		)
		    __field(unsigned int,		unique		)
		    __field(enum afs_edit_dir_reason,	why		)
		    __field(enum afs_edit_dir_op,	op		)
		    __field(unsigned int,		block		)
		    __field(unsigned short,		slot		)
		    __field(unsigned int,		f_vnode		)
		    __field(unsigned int,		f_unique	)
		    __array(char,			name, 18	)
			     ),

	    TP_fast_assign(
		    int __len = strlen(name);
		    __len = min(__len, 17);
		    __entry->vnode	= dvnode->fid.vnode;
		    __entry->unique	= dvnode->fid.unique;
		    __entry->why	= why;
		    __entry->op		= op;
		    __entry->block	= block;
		    __entry->slot	= slot;
		    __entry->f_vnode	= f_vnode;
		    __entry->f_unique	= f_unique;
		    memcpy(__entry->name, name, __len);
		    __entry->name[__len] = 0;
			   ),

	    TP_printk("d=%x:%x %s %s %u[%u] f=%x:%x %s",
		      __entry->vnode, __entry->unique,
		      __print_symbolic(__entry->why, afs_edit_dir_reasons),
		      __print_symbolic(__entry->op, afs_edit_dir_ops),
		      __entry->block, __entry->slot,
		      __entry->f_vnode, __entry->f_unique,
		      __entry->name)
	    );

TRACE_EVENT(afs_protocol_error,
	    TP_PROTO(struct afs_call *call, int error, const void *where),

	    TP_ARGS(call, error, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,	call		)
		    __field(int,		error		)
		    __field(const void *,	where		)
			     ),

	    TP_fast_assign(
		    __entry->call = call ? call->debug_id : 0;
		    __entry->error = error;
		    __entry->where = where;
			   ),

	    TP_printk("c=%08x r=%d sp=%pSR",
		      __entry->call, __entry->error, __entry->where)
	    );

#endif /* _TRACE_AFS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
