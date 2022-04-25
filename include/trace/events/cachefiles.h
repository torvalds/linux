/* SPDX-License-Identifier: GPL-2.0-or-later */
/* CacheFiles tracepoints
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cachefiles

#if !defined(_TRACE_CACHEFILES_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CACHEFILES_H

#include <linux/tracepoint.h>

/*
 * Define enums for tracing information.
 */
#ifndef __CACHEFILES_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __CACHEFILES_DECLARE_TRACE_ENUMS_ONCE_ONLY

enum cachefiles_obj_ref_trace {
	cachefiles_obj_get_ioreq,
	cachefiles_obj_new,
	cachefiles_obj_put_alloc_fail,
	cachefiles_obj_put_detach,
	cachefiles_obj_put_ioreq,
	cachefiles_obj_see_clean_commit,
	cachefiles_obj_see_clean_delete,
	cachefiles_obj_see_clean_drop_tmp,
	cachefiles_obj_see_lookup_cookie,
	cachefiles_obj_see_lookup_failed,
	cachefiles_obj_see_withdraw_cookie,
	cachefiles_obj_see_withdrawal,
	cachefiles_obj_get_ondemand_fd,
	cachefiles_obj_put_ondemand_fd,
};

enum fscache_why_object_killed {
	FSCACHE_OBJECT_IS_STALE,
	FSCACHE_OBJECT_IS_WEIRD,
	FSCACHE_OBJECT_INVALIDATED,
	FSCACHE_OBJECT_NO_SPACE,
	FSCACHE_OBJECT_WAS_RETIRED,
	FSCACHE_OBJECT_WAS_CULLED,
	FSCACHE_VOLUME_IS_WEIRD,
};

enum cachefiles_coherency_trace {
	cachefiles_coherency_check_aux,
	cachefiles_coherency_check_content,
	cachefiles_coherency_check_dirty,
	cachefiles_coherency_check_len,
	cachefiles_coherency_check_objsize,
	cachefiles_coherency_check_ok,
	cachefiles_coherency_check_type,
	cachefiles_coherency_check_xattr,
	cachefiles_coherency_set_fail,
	cachefiles_coherency_set_ok,
	cachefiles_coherency_vol_check_cmp,
	cachefiles_coherency_vol_check_ok,
	cachefiles_coherency_vol_check_resv,
	cachefiles_coherency_vol_check_xattr,
	cachefiles_coherency_vol_set_fail,
	cachefiles_coherency_vol_set_ok,
};

enum cachefiles_trunc_trace {
	cachefiles_trunc_dio_adjust,
	cachefiles_trunc_expand_tmpfile,
	cachefiles_trunc_shrink,
};

enum cachefiles_prepare_read_trace {
	cachefiles_trace_read_after_eof,
	cachefiles_trace_read_found_hole,
	cachefiles_trace_read_found_part,
	cachefiles_trace_read_have_data,
	cachefiles_trace_read_no_data,
	cachefiles_trace_read_no_file,
	cachefiles_trace_read_seek_error,
	cachefiles_trace_read_seek_nxio,
};

enum cachefiles_error_trace {
	cachefiles_trace_fallocate_error,
	cachefiles_trace_getxattr_error,
	cachefiles_trace_link_error,
	cachefiles_trace_lookup_error,
	cachefiles_trace_mkdir_error,
	cachefiles_trace_notify_change_error,
	cachefiles_trace_open_error,
	cachefiles_trace_read_error,
	cachefiles_trace_remxattr_error,
	cachefiles_trace_rename_error,
	cachefiles_trace_seek_error,
	cachefiles_trace_setxattr_error,
	cachefiles_trace_statfs_error,
	cachefiles_trace_tmpfile_error,
	cachefiles_trace_trunc_error,
	cachefiles_trace_unlink_error,
	cachefiles_trace_write_error,
};

#endif

/*
 * Define enum -> string mappings for display.
 */
#define cachefiles_obj_kill_traces				\
	EM(FSCACHE_OBJECT_IS_STALE,	"stale")		\
	EM(FSCACHE_OBJECT_IS_WEIRD,	"weird")		\
	EM(FSCACHE_OBJECT_INVALIDATED,	"inval")		\
	EM(FSCACHE_OBJECT_NO_SPACE,	"no_space")		\
	EM(FSCACHE_OBJECT_WAS_RETIRED,	"was_retired")		\
	EM(FSCACHE_OBJECT_WAS_CULLED,	"was_culled")		\
	E_(FSCACHE_VOLUME_IS_WEIRD,	"volume_weird")

#define cachefiles_obj_ref_traces					\
	EM(cachefiles_obj_get_ioreq,		"GET ioreq")		\
	EM(cachefiles_obj_new,			"NEW obj")		\
	EM(cachefiles_obj_put_alloc_fail,	"PUT alloc_fail")	\
	EM(cachefiles_obj_put_detach,		"PUT detach")		\
	EM(cachefiles_obj_put_ioreq,		"PUT ioreq")		\
	EM(cachefiles_obj_see_clean_commit,	"SEE clean_commit")	\
	EM(cachefiles_obj_see_clean_delete,	"SEE clean_delete")	\
	EM(cachefiles_obj_see_clean_drop_tmp,	"SEE clean_drop_tmp")	\
	EM(cachefiles_obj_see_lookup_cookie,	"SEE lookup_cookie")	\
	EM(cachefiles_obj_see_lookup_failed,	"SEE lookup_failed")	\
	EM(cachefiles_obj_see_withdraw_cookie,	"SEE withdraw_cookie")	\
	E_(cachefiles_obj_see_withdrawal,	"SEE withdrawal")

#define cachefiles_coherency_traces					\
	EM(cachefiles_coherency_check_aux,	"BAD aux ")		\
	EM(cachefiles_coherency_check_content,	"BAD cont")		\
	EM(cachefiles_coherency_check_dirty,	"BAD dirt")		\
	EM(cachefiles_coherency_check_len,	"BAD len ")		\
	EM(cachefiles_coherency_check_objsize,	"BAD osiz")		\
	EM(cachefiles_coherency_check_ok,	"OK      ")		\
	EM(cachefiles_coherency_check_type,	"BAD type")		\
	EM(cachefiles_coherency_check_xattr,	"BAD xatt")		\
	EM(cachefiles_coherency_set_fail,	"SET fail")		\
	EM(cachefiles_coherency_set_ok,		"SET ok  ")		\
	EM(cachefiles_coherency_vol_check_cmp,	"VOL BAD cmp ")		\
	EM(cachefiles_coherency_vol_check_ok,	"VOL OK      ")		\
	EM(cachefiles_coherency_vol_check_resv,	"VOL BAD resv")	\
	EM(cachefiles_coherency_vol_check_xattr,"VOL BAD xatt")		\
	EM(cachefiles_coherency_vol_set_fail,	"VOL SET fail")		\
	E_(cachefiles_coherency_vol_set_ok,	"VOL SET ok  ")

#define cachefiles_trunc_traces						\
	EM(cachefiles_trunc_dio_adjust,		"DIOADJ")		\
	EM(cachefiles_trunc_expand_tmpfile,	"EXPTMP")		\
	E_(cachefiles_trunc_shrink,		"SHRINK")

#define cachefiles_prepare_read_traces					\
	EM(cachefiles_trace_read_after_eof,	"after-eof ")		\
	EM(cachefiles_trace_read_found_hole,	"found-hole")		\
	EM(cachefiles_trace_read_found_part,	"found-part")		\
	EM(cachefiles_trace_read_have_data,	"have-data ")		\
	EM(cachefiles_trace_read_no_data,	"no-data   ")		\
	EM(cachefiles_trace_read_no_file,	"no-file   ")		\
	EM(cachefiles_trace_read_seek_error,	"seek-error")		\
	E_(cachefiles_trace_read_seek_nxio,	"seek-enxio")

#define cachefiles_error_traces						\
	EM(cachefiles_trace_fallocate_error,	"fallocate")		\
	EM(cachefiles_trace_getxattr_error,	"getxattr")		\
	EM(cachefiles_trace_link_error,		"link")			\
	EM(cachefiles_trace_lookup_error,	"lookup")		\
	EM(cachefiles_trace_mkdir_error,	"mkdir")		\
	EM(cachefiles_trace_notify_change_error, "notify_change")	\
	EM(cachefiles_trace_open_error,		"open")			\
	EM(cachefiles_trace_read_error,		"read")			\
	EM(cachefiles_trace_remxattr_error,	"remxattr")		\
	EM(cachefiles_trace_rename_error,	"rename")		\
	EM(cachefiles_trace_seek_error,		"seek")			\
	EM(cachefiles_trace_setxattr_error,	"setxattr")		\
	EM(cachefiles_trace_statfs_error,	"statfs")		\
	EM(cachefiles_trace_tmpfile_error,	"tmpfile")		\
	EM(cachefiles_trace_trunc_error,	"trunc")		\
	EM(cachefiles_trace_unlink_error,	"unlink")		\
	E_(cachefiles_trace_write_error,	"write")


/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

cachefiles_obj_kill_traces;
cachefiles_obj_ref_traces;
cachefiles_coherency_traces;
cachefiles_trunc_traces;
cachefiles_prepare_read_traces;
cachefiles_error_traces;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }


TRACE_EVENT(cachefiles_ref,
	    TP_PROTO(unsigned int object_debug_id,
		     unsigned int cookie_debug_id,
		     int usage,
		     enum cachefiles_obj_ref_trace why),

	    TP_ARGS(object_debug_id, cookie_debug_id, usage, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,			obj		)
		    __field(unsigned int,			cookie		)
		    __field(enum cachefiles_obj_ref_trace,	why		)
		    __field(int,				usage		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= object_debug_id;
		    __entry->cookie	= cookie_debug_id;
		    __entry->usage	= usage;
		    __entry->why	= why;
			   ),

	    TP_printk("c=%08x o=%08x u=%d %s",
		      __entry->cookie, __entry->obj, __entry->usage,
		      __print_symbolic(__entry->why, cachefiles_obj_ref_traces))
	    );

TRACE_EVENT(cachefiles_lookup,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct dentry *dir,
		     struct dentry *de),

	    TP_ARGS(obj, dir, de),

	    TP_STRUCT__entry(
		    __field(unsigned int,		obj	)
		    __field(short,			error	)
		    __field(unsigned long,		dino	)
		    __field(unsigned long,		ino	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->dino	= d_backing_inode(dir)->i_ino;
		    __entry->ino	= (!IS_ERR(de) && d_backing_inode(de) ?
					   d_backing_inode(de)->i_ino : 0);
		    __entry->error	= IS_ERR(de) ? PTR_ERR(de) : 0;
			   ),

	    TP_printk("o=%08x dB=%lx B=%lx e=%d",
		      __entry->obj, __entry->dino, __entry->ino, __entry->error)
	    );

TRACE_EVENT(cachefiles_mkdir,
	    TP_PROTO(struct dentry *dir, struct dentry *subdir),

	    TP_ARGS(dir, subdir),

	    TP_STRUCT__entry(
		    __field(unsigned int,			dir	)
		    __field(unsigned int,			subdir	)
			     ),

	    TP_fast_assign(
		    __entry->dir	= d_backing_inode(dir)->i_ino;
		    __entry->subdir	= d_backing_inode(subdir)->i_ino;
			   ),

	    TP_printk("dB=%x sB=%x",
		      __entry->dir,
		      __entry->subdir)
	    );

TRACE_EVENT(cachefiles_tmpfile,
	    TP_PROTO(struct cachefiles_object *obj, struct inode *backer),

	    TP_ARGS(obj, backer),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->debug_id;
		    __entry->backer	= backer->i_ino;
			   ),

	    TP_printk("o=%08x B=%x",
		      __entry->obj,
		      __entry->backer)
	    );

TRACE_EVENT(cachefiles_link,
	    TP_PROTO(struct cachefiles_object *obj, struct inode *backer),

	    TP_ARGS(obj, backer),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->debug_id;
		    __entry->backer	= backer->i_ino;
			   ),

	    TP_printk("o=%08x B=%x",
		      __entry->obj,
		      __entry->backer)
	    );

TRACE_EVENT(cachefiles_unlink,
	    TP_PROTO(struct cachefiles_object *obj,
		     ino_t ino,
		     enum fscache_why_object_killed why),

	    TP_ARGS(obj, ino, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(unsigned int,		ino		)
		    __field(enum fscache_why_object_killed, why		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : UINT_MAX;
		    __entry->ino	= ino;
		    __entry->why	= why;
			   ),

	    TP_printk("o=%08x B=%x w=%s",
		      __entry->obj, __entry->ino,
		      __print_symbolic(__entry->why, cachefiles_obj_kill_traces))
	    );

TRACE_EVENT(cachefiles_rename,
	    TP_PROTO(struct cachefiles_object *obj,
		     ino_t ino,
		     enum fscache_why_object_killed why),

	    TP_ARGS(obj, ino, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(unsigned int,		ino		)
		    __field(enum fscache_why_object_killed, why		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : UINT_MAX;
		    __entry->ino	= ino;
		    __entry->why	= why;
			   ),

	    TP_printk("o=%08x B=%x w=%s",
		      __entry->obj, __entry->ino,
		      __print_symbolic(__entry->why, cachefiles_obj_kill_traces))
	    );

TRACE_EVENT(cachefiles_coherency,
	    TP_PROTO(struct cachefiles_object *obj,
		     ino_t ino,
		     enum cachefiles_content content,
		     enum cachefiles_coherency_trace why),

	    TP_ARGS(obj, ino, content, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(enum cachefiles_coherency_trace,	why	)
		    __field(enum cachefiles_content,		content	)
		    __field(u64,				ino	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->debug_id;
		    __entry->why	= why;
		    __entry->content	= content;
		    __entry->ino	= ino;
			   ),

	    TP_printk("o=%08x %s B=%llx c=%u",
		      __entry->obj,
		      __print_symbolic(__entry->why, cachefiles_coherency_traces),
		      __entry->ino,
		      __entry->content)
	    );

TRACE_EVENT(cachefiles_vol_coherency,
	    TP_PROTO(struct cachefiles_volume *volume,
		     ino_t ino,
		     enum cachefiles_coherency_trace why),

	    TP_ARGS(volume, ino, why),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,			vol	)
		    __field(enum cachefiles_coherency_trace,	why	)
		    __field(u64,				ino	)
			     ),

	    TP_fast_assign(
		    __entry->vol	= volume->vcookie->debug_id;
		    __entry->why	= why;
		    __entry->ino	= ino;
			   ),

	    TP_printk("V=%08x %s B=%llx",
		      __entry->vol,
		      __print_symbolic(__entry->why, cachefiles_coherency_traces),
		      __entry->ino)
	    );

TRACE_EVENT(cachefiles_prep_read,
	    TP_PROTO(struct netfs_io_subrequest *sreq,
		     enum netfs_io_source source,
		     enum cachefiles_prepare_read_trace why,
		     ino_t cache_inode),

	    TP_ARGS(sreq, source, why, cache_inode),

	    TP_STRUCT__entry(
		    __field(unsigned int,		rreq		)
		    __field(unsigned short,		index		)
		    __field(unsigned short,		flags		)
		    __field(enum netfs_io_source,	source		)
		    __field(enum cachefiles_prepare_read_trace,	why	)
		    __field(size_t,			len		)
		    __field(loff_t,			start		)
		    __field(unsigned int,		netfs_inode	)
		    __field(unsigned int,		cache_inode	)
			     ),

	    TP_fast_assign(
		    __entry->rreq	= sreq->rreq->debug_id;
		    __entry->index	= sreq->debug_index;
		    __entry->flags	= sreq->flags;
		    __entry->source	= source;
		    __entry->why	= why;
		    __entry->len	= sreq->len;
		    __entry->start	= sreq->start;
		    __entry->netfs_inode = sreq->rreq->inode->i_ino;
		    __entry->cache_inode = cache_inode;
			   ),

	    TP_printk("R=%08x[%u] %s %s f=%02x s=%llx %zx ni=%x B=%x",
		      __entry->rreq, __entry->index,
		      __print_symbolic(__entry->source, netfs_sreq_sources),
		      __print_symbolic(__entry->why, cachefiles_prepare_read_traces),
		      __entry->flags,
		      __entry->start, __entry->len,
		      __entry->netfs_inode, __entry->cache_inode)
	    );

TRACE_EVENT(cachefiles_read,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct inode *backer,
		     loff_t start,
		     size_t len),

	    TP_ARGS(obj, backer, start, len),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
		    __field(size_t,				len	)
		    __field(loff_t,				start	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->debug_id;
		    __entry->backer	= backer->i_ino;
		    __entry->start	= start;
		    __entry->len	= len;
			   ),

	    TP_printk("o=%08x B=%x s=%llx l=%zx",
		      __entry->obj,
		      __entry->backer,
		      __entry->start,
		      __entry->len)
	    );

TRACE_EVENT(cachefiles_write,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct inode *backer,
		     loff_t start,
		     size_t len),

	    TP_ARGS(obj, backer, start, len),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
		    __field(size_t,				len	)
		    __field(loff_t,				start	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->debug_id;
		    __entry->backer	= backer->i_ino;
		    __entry->start	= start;
		    __entry->len	= len;
			   ),

	    TP_printk("o=%08x B=%x s=%llx l=%zx",
		      __entry->obj,
		      __entry->backer,
		      __entry->start,
		      __entry->len)
	    );

TRACE_EVENT(cachefiles_trunc,
	    TP_PROTO(struct cachefiles_object *obj, struct inode *backer,
		     loff_t from, loff_t to, enum cachefiles_trunc_trace why),

	    TP_ARGS(obj, backer, from, to, why),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
		    __field(enum cachefiles_trunc_trace,	why	)
		    __field(loff_t,				from	)
		    __field(loff_t,				to	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj->debug_id;
		    __entry->backer	= backer->i_ino;
		    __entry->from	= from;
		    __entry->to		= to;
		    __entry->why	= why;
			   ),

	    TP_printk("o=%08x B=%x %s l=%llx->%llx",
		      __entry->obj,
		      __entry->backer,
		      __print_symbolic(__entry->why, cachefiles_trunc_traces),
		      __entry->from,
		      __entry->to)
	    );

TRACE_EVENT(cachefiles_mark_active,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct inode *inode),

	    TP_ARGS(obj, inode),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(ino_t,			inode		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->inode	= inode->i_ino;
			   ),

	    TP_printk("o=%08x B=%lx",
		      __entry->obj, __entry->inode)
	    );

TRACE_EVENT(cachefiles_mark_failed,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct inode *inode),

	    TP_ARGS(obj, inode),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(ino_t,			inode		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->inode	= inode->i_ino;
			   ),

	    TP_printk("o=%08x B=%lx",
		      __entry->obj, __entry->inode)
	    );

TRACE_EVENT(cachefiles_mark_inactive,
	    TP_PROTO(struct cachefiles_object *obj,
		     struct inode *inode),

	    TP_ARGS(obj, inode),

	    /* Note that obj may be NULL */
	    TP_STRUCT__entry(
		    __field(unsigned int,		obj		)
		    __field(ino_t,			inode		)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->inode	= inode->i_ino;
			   ),

	    TP_printk("o=%08x B=%lx",
		      __entry->obj, __entry->inode)
	    );

TRACE_EVENT(cachefiles_vfs_error,
	    TP_PROTO(struct cachefiles_object *obj, struct inode *backer,
		     int error, enum cachefiles_error_trace where),

	    TP_ARGS(obj, backer, error, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
		    __field(enum cachefiles_error_trace,	where	)
		    __field(short,				error	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->backer	= backer->i_ino;
		    __entry->error	= error;
		    __entry->where	= where;
			   ),

	    TP_printk("o=%08x B=%x %s e=%d",
		      __entry->obj,
		      __entry->backer,
		      __print_symbolic(__entry->where, cachefiles_error_traces),
		      __entry->error)
	    );

TRACE_EVENT(cachefiles_io_error,
	    TP_PROTO(struct cachefiles_object *obj, struct inode *backer,
		     int error, enum cachefiles_error_trace where),

	    TP_ARGS(obj, backer, error, where),

	    TP_STRUCT__entry(
		    __field(unsigned int,			obj	)
		    __field(unsigned int,			backer	)
		    __field(enum cachefiles_error_trace,	where	)
		    __field(short,				error	)
			     ),

	    TP_fast_assign(
		    __entry->obj	= obj ? obj->debug_id : 0;
		    __entry->backer	= backer->i_ino;
		    __entry->error	= error;
		    __entry->where	= where;
			   ),

	    TP_printk("o=%08x B=%x %s e=%d",
		      __entry->obj,
		      __entry->backer,
		      __print_symbolic(__entry->where, cachefiles_error_traces),
		      __entry->error)
	    );

#endif /* _TRACE_CACHEFILES_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
