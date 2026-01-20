/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Ceph filesystem support module tracepoints
 *
 * Copyright (C) 2025 IONOS SE. All Rights Reserved.
 * Written by Max Kellermann (max.kellermann@ionos.com)
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ceph

#if !defined(_TRACE_CEPH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CEPH_H

#include <linux/tracepoint.h>

#define ceph_mdsc_suspend_reasons						\
	EM(ceph_mdsc_suspend_reason_no_mdsmap,		"no-mdsmap")		\
	EM(ceph_mdsc_suspend_reason_no_active_mds,	"no-active-mds")	\
	EM(ceph_mdsc_suspend_reason_rejected,		"rejected")		\
	E_(ceph_mdsc_suspend_reason_session,		"session")

#ifndef __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY
#define __NETFS_DECLARE_TRACE_ENUMS_ONCE_ONLY

#undef EM
#undef E_
#define EM(a, b) a,
#define E_(a, b) a

enum ceph_mdsc_suspend_reason { ceph_mdsc_suspend_reasons } __mode(byte);

#endif

/*
 * Export enum symbols via userspace.
 */
#undef EM
#undef E_
#define EM(a, b) TRACE_DEFINE_ENUM(a);
#define E_(a, b) TRACE_DEFINE_ENUM(a);

ceph_mdsc_suspend_reasons;

/*
 * Now redefine the EM() and E_() macros to map the enums to the strings that
 * will be printed in the output.
 */
#undef EM
#undef E_
#define EM(a, b)	{ a, b },
#define E_(a, b)	{ a, b }

TRACE_EVENT(ceph_mdsc_submit_request,
	TP_PROTO(struct ceph_mds_client *mdsc,
		 struct ceph_mds_request *req),

	TP_ARGS(mdsc, req),

	TP_STRUCT__entry(
		__field(u64,	tid)
		__field(int,	op)
		__field(u64,	ino)
		__field(u64,	snap)
	),

	TP_fast_assign(
		struct inode *inode;

		__entry->tid = req->r_tid;
		__entry->op = req->r_op;

		inode = req->r_inode;
		if (inode == NULL && req->r_dentry)
			inode = d_inode(req->r_dentry);

		if (inode) {
			__entry->ino = ceph_ino(inode);
			__entry->snap = ceph_snap(inode);
		} else {
			__entry->ino = __entry->snap = 0;
		}
	),

	TP_printk("R=%llu op=%s ino=%llx,%llx",
		  __entry->tid,
		  ceph_mds_op_name(__entry->op),
		  __entry->ino, __entry->snap)
);

TRACE_EVENT(ceph_mdsc_suspend_request,
	TP_PROTO(struct ceph_mds_client *mdsc,
		     struct ceph_mds_session *session,
		     struct ceph_mds_request *req,
		     enum ceph_mdsc_suspend_reason reason),

	TP_ARGS(mdsc, session, req, reason),

	TP_STRUCT__entry(
		__field(u64,				tid)
		__field(int,				op)
		__field(int,				mds)
		__field(enum ceph_mdsc_suspend_reason,	reason)
	),

	TP_fast_assign(
		__entry->tid = req->r_tid;
		__entry->op = req->r_op;
		__entry->mds = session ? session->s_mds : -1;
		__entry->reason = reason;
	),

	TP_printk("R=%llu op=%s reason=%s",
		  __entry->tid,
		  ceph_mds_op_name(__entry->op),
		  __print_symbolic(__entry->reason, ceph_mdsc_suspend_reasons))
);

TRACE_EVENT(ceph_mdsc_resume_request,
	TP_PROTO(struct ceph_mds_client *mdsc,
		 struct ceph_mds_request *req),

	TP_ARGS(mdsc, req),

	TP_STRUCT__entry(
		__field(u64,				tid)
		__field(int,				op)
	),

	TP_fast_assign(
		__entry->tid = req->r_tid;
		__entry->op = req->r_op;
	),

	TP_printk("R=%llu op=%s",
		  __entry->tid,
		  ceph_mds_op_name(__entry->op))
);

TRACE_EVENT(ceph_mdsc_send_request,
	TP_PROTO(struct ceph_mds_session *session,
		 struct ceph_mds_request *req),

	TP_ARGS(session, req),

	TP_STRUCT__entry(
		__field(u64,		tid)
		__field(int,		op)
		__field(int,		mds)
	),

	TP_fast_assign(
		__entry->tid = req->r_tid;
		__entry->op = req->r_op;
		__entry->mds = session->s_mds;
	),

	TP_printk("R=%llu op=%s mds=%d",
		  __entry->tid,
		  ceph_mds_op_name(__entry->op),
		  __entry->mds)
);

TRACE_EVENT(ceph_mdsc_complete_request,
	TP_PROTO(struct ceph_mds_client *mdsc,
		     struct ceph_mds_request *req),

	TP_ARGS(mdsc, req),

	TP_STRUCT__entry(
		__field(u64,			tid)
		__field(int,			op)
		__field(int,			err)
		__field(unsigned long,		latency_ns)
	),

	TP_fast_assign(
		__entry->tid = req->r_tid;
		__entry->op = req->r_op;
		__entry->err = req->r_err;
		__entry->latency_ns = req->r_end_latency - req->r_start_latency;
	),

	TP_printk("R=%llu op=%s err=%d latency_ns=%lu",
		  __entry->tid,
		  ceph_mds_op_name(__entry->op),
		  __entry->err,
		  __entry->latency_ns)
);

TRACE_EVENT(ceph_handle_caps,
	TP_PROTO(struct ceph_mds_client *mdsc,
		 struct ceph_mds_session *session,
		 int op,
		 const struct ceph_vino *vino,
		 struct ceph_inode_info *inode,
		 u32 seq, u32 mseq, u32 issue_seq),

	TP_ARGS(mdsc, session, op, vino, inode, seq, mseq, issue_seq),

	TP_STRUCT__entry(
		__field(int,	mds)
		__field(int,	op)
		__field(u64,	ino)
		__field(u64,	snap)
		__field(u32,	seq)
		__field(u32,	mseq)
		__field(u32,	issue_seq)
	),

	TP_fast_assign(
		__entry->mds = session->s_mds;
		__entry->op = op;
		__entry->ino = vino->ino;
		__entry->snap = vino->snap;
		__entry->seq = seq;
		__entry->mseq = mseq;
		__entry->issue_seq = issue_seq;
	),

	TP_printk("mds=%d op=%s vino=%llx.%llx seq=%u iseq=%u mseq=%u",
		  __entry->mds,
		  ceph_cap_op_name(__entry->op),
		  __entry->ino,
		  __entry->snap,
		  __entry->seq,
		  __entry->issue_seq,
		  __entry->mseq)
);

#undef EM
#undef E_
#endif /* _TRACE_CEPH_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
