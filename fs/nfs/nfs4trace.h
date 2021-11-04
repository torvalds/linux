/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Trond Myklebust <Trond.Myklebust@netapp.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nfs4

#if !defined(_TRACE_NFS4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NFS4_H

#include <linux/tracepoint.h>
#include <trace/events/sunrpc_base.h>

#include <trace/events/fs.h>
#include <trace/events/nfs.h>

#define show_nfs_fattr_flags(valid) \
	__print_flags((unsigned long)valid, "|", \
		{ NFS_ATTR_FATTR_TYPE, "TYPE" }, \
		{ NFS_ATTR_FATTR_MODE, "MODE" }, \
		{ NFS_ATTR_FATTR_NLINK, "NLINK" }, \
		{ NFS_ATTR_FATTR_OWNER, "OWNER" }, \
		{ NFS_ATTR_FATTR_GROUP, "GROUP" }, \
		{ NFS_ATTR_FATTR_RDEV, "RDEV" }, \
		{ NFS_ATTR_FATTR_SIZE, "SIZE" }, \
		{ NFS_ATTR_FATTR_FSID, "FSID" }, \
		{ NFS_ATTR_FATTR_FILEID, "FILEID" }, \
		{ NFS_ATTR_FATTR_ATIME, "ATIME" }, \
		{ NFS_ATTR_FATTR_MTIME, "MTIME" }, \
		{ NFS_ATTR_FATTR_CTIME, "CTIME" }, \
		{ NFS_ATTR_FATTR_CHANGE, "CHANGE" }, \
		{ NFS_ATTR_FATTR_OWNER_NAME, "OWNER_NAME" }, \
		{ NFS_ATTR_FATTR_GROUP_NAME, "GROUP_NAME" })

DECLARE_EVENT_CLASS(nfs4_clientid_event,
		TP_PROTO(
			const struct nfs_client *clp,
			int error
		),

		TP_ARGS(clp, error),

		TP_STRUCT__entry(
			__string(dstaddr, clp->cl_hostname)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			__entry->error = error < 0 ? -error : 0;
			__assign_str(dstaddr, clp->cl_hostname);
		),

		TP_printk(
			"error=%ld (%s) dstaddr=%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			__get_str(dstaddr)
		)
);
#define DEFINE_NFS4_CLIENTID_EVENT(name) \
	DEFINE_EVENT(nfs4_clientid_event, name,	 \
			TP_PROTO( \
				const struct nfs_client *clp, \
				int error \
			), \
			TP_ARGS(clp, error))
DEFINE_NFS4_CLIENTID_EVENT(nfs4_setclientid);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_setclientid_confirm);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_renew);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_renew_async);
#ifdef CONFIG_NFS_V4_1
DEFINE_NFS4_CLIENTID_EVENT(nfs4_exchange_id);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_create_session);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_destroy_session);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_destroy_clientid);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_bind_conn_to_session);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_sequence);
DEFINE_NFS4_CLIENTID_EVENT(nfs4_reclaim_complete);

TRACE_EVENT(nfs4_sequence_done,
		TP_PROTO(
			const struct nfs4_session *session,
			const struct nfs4_sequence_res *res
		),
		TP_ARGS(session, res),

		TP_STRUCT__entry(
			__field(unsigned int, session)
			__field(unsigned int, slot_nr)
			__field(unsigned int, seq_nr)
			__field(unsigned int, highest_slotid)
			__field(unsigned int, target_highest_slotid)
			__field(unsigned long, status_flags)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			const struct nfs4_slot *sr_slot = res->sr_slot;
			__entry->session = nfs_session_id_hash(&session->sess_id);
			__entry->slot_nr = sr_slot->slot_nr;
			__entry->seq_nr = sr_slot->seq_nr;
			__entry->highest_slotid = res->sr_highest_slotid;
			__entry->target_highest_slotid =
					res->sr_target_highest_slotid;
			__entry->status_flags = res->sr_status_flags;
			__entry->error = res->sr_status < 0 ?
					-res->sr_status : 0;
		),
		TP_printk(
			"error=%ld (%s) session=0x%08x slot_nr=%u seq_nr=%u "
			"highest_slotid=%u target_highest_slotid=%u "
			"status_flags=0x%lx (%s)",
			-__entry->error,
			show_nfs4_status(__entry->error),
			__entry->session,
			__entry->slot_nr,
			__entry->seq_nr,
			__entry->highest_slotid,
			__entry->target_highest_slotid,
			__entry->status_flags,
			show_nfs4_seq4_status(__entry->status_flags)
		)
);

struct cb_sequenceargs;
struct cb_sequenceres;

TRACE_EVENT(nfs4_cb_sequence,
		TP_PROTO(
			const struct cb_sequenceargs *args,
			const struct cb_sequenceres *res,
			__be32 status
		),
		TP_ARGS(args, res, status),

		TP_STRUCT__entry(
			__field(unsigned int, session)
			__field(unsigned int, slot_nr)
			__field(unsigned int, seq_nr)
			__field(unsigned int, highest_slotid)
			__field(unsigned int, cachethis)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			__entry->session = nfs_session_id_hash(&args->csa_sessionid);
			__entry->slot_nr = args->csa_slotid;
			__entry->seq_nr = args->csa_sequenceid;
			__entry->highest_slotid = args->csa_highestslotid;
			__entry->cachethis = args->csa_cachethis;
			__entry->error = be32_to_cpu(status);
		),

		TP_printk(
			"error=%ld (%s) session=0x%08x slot_nr=%u seq_nr=%u "
			"highest_slotid=%u",
			-__entry->error,
			show_nfs4_status(__entry->error),
			__entry->session,
			__entry->slot_nr,
			__entry->seq_nr,
			__entry->highest_slotid
		)
);

TRACE_EVENT(nfs4_cb_seqid_err,
		TP_PROTO(
			const struct cb_sequenceargs *args,
			__be32 status
		),
		TP_ARGS(args, status),

		TP_STRUCT__entry(
			__field(unsigned int, session)
			__field(unsigned int, slot_nr)
			__field(unsigned int, seq_nr)
			__field(unsigned int, highest_slotid)
			__field(unsigned int, cachethis)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			__entry->session = nfs_session_id_hash(&args->csa_sessionid);
			__entry->slot_nr = args->csa_slotid;
			__entry->seq_nr = args->csa_sequenceid;
			__entry->highest_slotid = args->csa_highestslotid;
			__entry->cachethis = args->csa_cachethis;
			__entry->error = be32_to_cpu(status);
		),

		TP_printk(
			"error=%ld (%s) session=0x%08x slot_nr=%u seq_nr=%u "
			"highest_slotid=%u",
			-__entry->error,
			show_nfs4_status(__entry->error),
			__entry->session,
			__entry->slot_nr,
			__entry->seq_nr,
			__entry->highest_slotid
		)
);

#endif /* CONFIG_NFS_V4_1 */

TRACE_EVENT(nfs4_setup_sequence,
		TP_PROTO(
			const struct nfs4_session *session,
			const struct nfs4_sequence_args *args
		),
		TP_ARGS(session, args),

		TP_STRUCT__entry(
			__field(unsigned int, session)
			__field(unsigned int, slot_nr)
			__field(unsigned int, seq_nr)
			__field(unsigned int, highest_used_slotid)
		),

		TP_fast_assign(
			const struct nfs4_slot *sa_slot = args->sa_slot;
			__entry->session = session ? nfs_session_id_hash(&session->sess_id) : 0;
			__entry->slot_nr = sa_slot->slot_nr;
			__entry->seq_nr = sa_slot->seq_nr;
			__entry->highest_used_slotid =
					sa_slot->table->highest_used_slotid;
		),
		TP_printk(
			"session=0x%08x slot_nr=%u seq_nr=%u "
			"highest_used_slotid=%u",
			__entry->session,
			__entry->slot_nr,
			__entry->seq_nr,
			__entry->highest_used_slotid
		)
);

TRACE_DEFINE_ENUM(NFS4CLNT_MANAGER_RUNNING);
TRACE_DEFINE_ENUM(NFS4CLNT_CHECK_LEASE);
TRACE_DEFINE_ENUM(NFS4CLNT_LEASE_EXPIRED);
TRACE_DEFINE_ENUM(NFS4CLNT_RECLAIM_REBOOT);
TRACE_DEFINE_ENUM(NFS4CLNT_RECLAIM_NOGRACE);
TRACE_DEFINE_ENUM(NFS4CLNT_DELEGRETURN);
TRACE_DEFINE_ENUM(NFS4CLNT_SESSION_RESET);
TRACE_DEFINE_ENUM(NFS4CLNT_LEASE_CONFIRM);
TRACE_DEFINE_ENUM(NFS4CLNT_SERVER_SCOPE_MISMATCH);
TRACE_DEFINE_ENUM(NFS4CLNT_PURGE_STATE);
TRACE_DEFINE_ENUM(NFS4CLNT_BIND_CONN_TO_SESSION);
TRACE_DEFINE_ENUM(NFS4CLNT_MOVED);
TRACE_DEFINE_ENUM(NFS4CLNT_LEASE_MOVED);
TRACE_DEFINE_ENUM(NFS4CLNT_DELEGATION_EXPIRED);
TRACE_DEFINE_ENUM(NFS4CLNT_RUN_MANAGER);
TRACE_DEFINE_ENUM(NFS4CLNT_RECALL_RUNNING);
TRACE_DEFINE_ENUM(NFS4CLNT_RECALL_ANY_LAYOUT_READ);
TRACE_DEFINE_ENUM(NFS4CLNT_RECALL_ANY_LAYOUT_RW);

#define show_nfs4_clp_state(state) \
	__print_flags(state, "|", \
		{ NFS4CLNT_MANAGER_RUNNING,	"MANAGER_RUNNING" }, \
		{ NFS4CLNT_CHECK_LEASE,		"CHECK_LEASE" }, \
		{ NFS4CLNT_LEASE_EXPIRED,	"LEASE_EXPIRED" }, \
		{ NFS4CLNT_RECLAIM_REBOOT,	"RECLAIM_REBOOT" }, \
		{ NFS4CLNT_RECLAIM_NOGRACE,	"RECLAIM_NOGRACE" }, \
		{ NFS4CLNT_DELEGRETURN,		"DELEGRETURN" }, \
		{ NFS4CLNT_SESSION_RESET,	"SESSION_RESET" }, \
		{ NFS4CLNT_LEASE_CONFIRM,	"LEASE_CONFIRM" }, \
		{ NFS4CLNT_SERVER_SCOPE_MISMATCH, \
						"SERVER_SCOPE_MISMATCH" }, \
		{ NFS4CLNT_PURGE_STATE,		"PURGE_STATE" }, \
		{ NFS4CLNT_BIND_CONN_TO_SESSION, \
						"BIND_CONN_TO_SESSION" }, \
		{ NFS4CLNT_MOVED,		"MOVED" }, \
		{ NFS4CLNT_LEASE_MOVED,		"LEASE_MOVED" }, \
		{ NFS4CLNT_DELEGATION_EXPIRED,	"DELEGATION_EXPIRED" }, \
		{ NFS4CLNT_RUN_MANAGER,		"RUN_MANAGER" }, \
		{ NFS4CLNT_RECALL_RUNNING,	"RECALL_RUNNING" }, \
		{ NFS4CLNT_RECALL_ANY_LAYOUT_READ, "RECALL_ANY_LAYOUT_READ" }, \
		{ NFS4CLNT_RECALL_ANY_LAYOUT_RW, "RECALL_ANY_LAYOUT_RW" })

TRACE_EVENT(nfs4_state_mgr,
		TP_PROTO(
			const struct nfs_client *clp
		),

		TP_ARGS(clp),

		TP_STRUCT__entry(
			__field(unsigned long, state)
			__string(hostname, clp->cl_hostname)
		),

		TP_fast_assign(
			__entry->state = clp->cl_state;
			__assign_str(hostname, clp->cl_hostname);
		),

		TP_printk(
			"hostname=%s clp state=%s", __get_str(hostname),
			show_nfs4_clp_state(__entry->state)
		)
)

TRACE_EVENT(nfs4_state_mgr_failed,
		TP_PROTO(
			const struct nfs_client *clp,
			const char *section,
			int status
		),

		TP_ARGS(clp, section, status),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(unsigned long, state)
			__string(hostname, clp->cl_hostname)
			__string(section, section)
		),

		TP_fast_assign(
			__entry->error = status < 0 ? -status : 0;
			__entry->state = clp->cl_state;
			__assign_str(hostname, clp->cl_hostname);
			__assign_str(section, section);
		),

		TP_printk(
			"hostname=%s clp state=%s error=%ld (%s) section=%s",
			__get_str(hostname),
			show_nfs4_clp_state(__entry->state), -__entry->error,
			show_nfs4_status(__entry->error), __get_str(section)

		)
)

TRACE_EVENT(nfs4_xdr_bad_operation,
		TP_PROTO(
			const struct xdr_stream *xdr,
			u32 op,
			u32 expected
		),

		TP_ARGS(xdr, op, expected),

		TP_STRUCT__entry(
			__field(unsigned int, task_id)
			__field(unsigned int, client_id)
			__field(u32, xid)
			__field(u32, op)
			__field(u32, expected)
		),

		TP_fast_assign(
			const struct rpc_rqst *rqstp = xdr->rqst;
			const struct rpc_task *task = rqstp->rq_task;

			__entry->task_id = task->tk_pid;
			__entry->client_id = task->tk_client->cl_clid;
			__entry->xid = be32_to_cpu(rqstp->rq_xid);
			__entry->op = op;
			__entry->expected = expected;
		),

		TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
			  " xid=0x%08x operation=%u, expected=%u",
			__entry->task_id, __entry->client_id, __entry->xid,
			__entry->op, __entry->expected
		)
);

DECLARE_EVENT_CLASS(nfs4_xdr_event,
		TP_PROTO(
			const struct xdr_stream *xdr,
			u32 op,
			u32 error
		),

		TP_ARGS(xdr, op, error),

		TP_STRUCT__entry(
			__field(unsigned int, task_id)
			__field(unsigned int, client_id)
			__field(u32, xid)
			__field(u32, op)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			const struct rpc_rqst *rqstp = xdr->rqst;
			const struct rpc_task *task = rqstp->rq_task;

			__entry->task_id = task->tk_pid;
			__entry->client_id = task->tk_client->cl_clid;
			__entry->xid = be32_to_cpu(rqstp->rq_xid);
			__entry->op = op;
			__entry->error = error;
		),

		TP_printk(SUNRPC_TRACE_TASK_SPECIFIER
			  " xid=0x%08x error=%ld (%s) operation=%u",
			__entry->task_id, __entry->client_id, __entry->xid,
			-__entry->error, show_nfs4_status(__entry->error),
			__entry->op
		)
);
#define DEFINE_NFS4_XDR_EVENT(name) \
	DEFINE_EVENT(nfs4_xdr_event, name, \
			TP_PROTO( \
				const struct xdr_stream *xdr, \
				u32 op, \
				u32 error \
			), \
			TP_ARGS(xdr, op, error))
DEFINE_NFS4_XDR_EVENT(nfs4_xdr_status);
DEFINE_NFS4_XDR_EVENT(nfs4_xdr_bad_filehandle);

DECLARE_EVENT_CLASS(nfs4_cb_error_class,
		TP_PROTO(
			__be32 xid,
			u32 cb_ident
		),

		TP_ARGS(xid, cb_ident),

		TP_STRUCT__entry(
			__field(u32, xid)
			__field(u32, cbident)
		),

		TP_fast_assign(
			__entry->xid = be32_to_cpu(xid);
			__entry->cbident = cb_ident;
		),

		TP_printk(
			"xid=0x%08x cb_ident=0x%08x",
			__entry->xid, __entry->cbident
		)
);

#define DEFINE_CB_ERROR_EVENT(name) \
	DEFINE_EVENT(nfs4_cb_error_class, nfs_cb_##name, \
			TP_PROTO( \
				__be32 xid, \
				u32 cb_ident \
			), \
			TP_ARGS(xid, cb_ident))

DEFINE_CB_ERROR_EVENT(no_clp);
DEFINE_CB_ERROR_EVENT(badprinc);

DECLARE_EVENT_CLASS(nfs4_open_event,
		TP_PROTO(
			const struct nfs_open_context *ctx,
			int flags,
			int error
		),

		TP_ARGS(ctx, flags, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(unsigned long, flags)
			__field(unsigned long, fmode)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(u64, dir)
			__string(name, ctx->dentry->d_name.name)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
			__field(int, openstateid_seq)
			__field(u32, openstateid_hash)
		),

		TP_fast_assign(
			const struct nfs4_state *state = ctx->state;
			const struct inode *inode = NULL;

			__entry->error = -error;
			__entry->flags = flags;
			__entry->fmode = (__force unsigned long)ctx->mode;
			__entry->dev = ctx->dentry->d_sb->s_dev;
			if (!IS_ERR_OR_NULL(state)) {
				inode = state->inode;
				__entry->stateid_seq =
					be32_to_cpu(state->stateid.seqid);
				__entry->stateid_hash =
					nfs_stateid_hash(&state->stateid);
				__entry->openstateid_seq =
					be32_to_cpu(state->open_stateid.seqid);
				__entry->openstateid_hash =
					nfs_stateid_hash(&state->open_stateid);
			} else {
				__entry->stateid_seq = 0;
				__entry->stateid_hash = 0;
				__entry->openstateid_seq = 0;
				__entry->openstateid_hash = 0;
			}
			if (inode != NULL) {
				__entry->fileid = NFS_FILEID(inode);
				__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			} else {
				__entry->fileid = 0;
				__entry->fhandle = 0;
			}
			__entry->dir = NFS_FILEID(d_inode(ctx->dentry->d_parent));
			__assign_str(name, ctx->dentry->d_name.name);
		),

		TP_printk(
			"error=%ld (%s) flags=%lu (%s) fmode=%s "
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"name=%02x:%02x:%llu/%s stateid=%d:0x%08x "
			"openstateid=%d:0x%08x",
			 -__entry->error,
			 show_nfs4_status(__entry->error),
			 __entry->flags,
			 show_fs_fcntl_open_flags(__entry->flags),
			 show_fs_fmode_flags(__entry->fmode),
			 MAJOR(__entry->dev), MINOR(__entry->dev),
			 (unsigned long long)__entry->fileid,
			 __entry->fhandle,
			 MAJOR(__entry->dev), MINOR(__entry->dev),
			 (unsigned long long)__entry->dir,
			 __get_str(name),
			 __entry->stateid_seq, __entry->stateid_hash,
			 __entry->openstateid_seq, __entry->openstateid_hash
		)
);

#define DEFINE_NFS4_OPEN_EVENT(name) \
	DEFINE_EVENT(nfs4_open_event, name, \
			TP_PROTO( \
				const struct nfs_open_context *ctx, \
				int flags, \
				int error \
			), \
			TP_ARGS(ctx, flags, error))
DEFINE_NFS4_OPEN_EVENT(nfs4_open_reclaim);
DEFINE_NFS4_OPEN_EVENT(nfs4_open_expired);
DEFINE_NFS4_OPEN_EVENT(nfs4_open_file);

TRACE_EVENT(nfs4_cached_open,
		TP_PROTO(
			const struct nfs4_state *state
		),
		TP_ARGS(state),
		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned int, fmode)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = state->inode;

			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->fmode = (__force unsigned int)state->state;
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
		),

		TP_printk(
			"fmode=%s fileid=%02x:%02x:%llu "
			"fhandle=0x%08x stateid=%d:0x%08x",
			__entry->fmode ?  show_fs_fmode_flags(__entry->fmode) :
					  "closed",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash
		)
);

TRACE_EVENT(nfs4_close,
		TP_PROTO(
			const struct nfs4_state *state,
			const struct nfs_closeargs *args,
			const struct nfs_closeres *res,
			int error
		),

		TP_ARGS(state, args, res, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned int, fmode)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = state->inode;

			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->fmode = (__force unsigned int)state->state;
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(args->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&args->stateid);
		),

		TP_printk(
			"error=%ld (%s) fmode=%s fileid=%02x:%02x:%llu "
			"fhandle=0x%08x openstateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			__entry->fmode ?  show_fs_fmode_flags(__entry->fmode) :
					  "closed",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash
		)
);

DECLARE_EVENT_CLASS(nfs4_lock_event,
		TP_PROTO(
			const struct file_lock *request,
			const struct nfs4_state *state,
			int cmd,
			int error
		),

		TP_ARGS(request, state, cmd, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(unsigned long, cmd)
			__field(unsigned long, type)
			__field(loff_t, start)
			__field(loff_t, end)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = state->inode;

			__entry->error = error < 0 ? -error : 0;
			__entry->cmd = cmd;
			__entry->type = request->fl_type;
			__entry->start = request->fl_start;
			__entry->end = request->fl_end;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
		),

		TP_printk(
			"error=%ld (%s) cmd=%s:%s range=%lld:%lld "
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			show_fs_fcntl_cmd(__entry->cmd),
			show_fs_fcntl_lock_type(__entry->type),
			(long long)__entry->start,
			(long long)__entry->end,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash
		)
);

#define DEFINE_NFS4_LOCK_EVENT(name) \
	DEFINE_EVENT(nfs4_lock_event, name, \
			TP_PROTO( \
				const struct file_lock *request, \
				const struct nfs4_state *state, \
				int cmd, \
				int error \
			), \
			TP_ARGS(request, state, cmd, error))
DEFINE_NFS4_LOCK_EVENT(nfs4_get_lock);
DEFINE_NFS4_LOCK_EVENT(nfs4_unlock);

TRACE_EVENT(nfs4_set_lock,
		TP_PROTO(
			const struct file_lock *request,
			const struct nfs4_state *state,
			const nfs4_stateid *lockstateid,
			int cmd,
			int error
		),

		TP_ARGS(request, state, lockstateid, cmd, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(unsigned long, cmd)
			__field(unsigned long, type)
			__field(loff_t, start)
			__field(loff_t, end)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
			__field(int, lockstateid_seq)
			__field(u32, lockstateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = state->inode;

			__entry->error = error < 0 ? -error : 0;
			__entry->cmd = cmd;
			__entry->type = request->fl_type;
			__entry->start = request->fl_start;
			__entry->end = request->fl_end;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
			__entry->lockstateid_seq =
				be32_to_cpu(lockstateid->seqid);
			__entry->lockstateid_hash =
				nfs_stateid_hash(lockstateid);
		),

		TP_printk(
			"error=%ld (%s) cmd=%s:%s range=%lld:%lld "
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x lockstateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			show_fs_fcntl_cmd(__entry->cmd),
			show_fs_fcntl_lock_type(__entry->type),
			(long long)__entry->start,
			(long long)__entry->end,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash,
			__entry->lockstateid_seq, __entry->lockstateid_hash
		)
);

TRACE_DEFINE_ENUM(LK_STATE_IN_USE);
TRACE_DEFINE_ENUM(NFS_DELEGATED_STATE);
TRACE_DEFINE_ENUM(NFS_OPEN_STATE);
TRACE_DEFINE_ENUM(NFS_O_RDONLY_STATE);
TRACE_DEFINE_ENUM(NFS_O_WRONLY_STATE);
TRACE_DEFINE_ENUM(NFS_O_RDWR_STATE);
TRACE_DEFINE_ENUM(NFS_STATE_RECLAIM_REBOOT);
TRACE_DEFINE_ENUM(NFS_STATE_RECLAIM_NOGRACE);
TRACE_DEFINE_ENUM(NFS_STATE_POSIX_LOCKS);
TRACE_DEFINE_ENUM(NFS_STATE_RECOVERY_FAILED);
TRACE_DEFINE_ENUM(NFS_STATE_MAY_NOTIFY_LOCK);
TRACE_DEFINE_ENUM(NFS_STATE_CHANGE_WAIT);
TRACE_DEFINE_ENUM(NFS_CLNT_DST_SSC_COPY_STATE);
TRACE_DEFINE_ENUM(NFS_CLNT_SRC_SSC_COPY_STATE);
TRACE_DEFINE_ENUM(NFS_SRV_SSC_COPY_STATE);

#define show_nfs4_state_flags(flags) \
	__print_flags(flags, "|", \
		{ LK_STATE_IN_USE,		"IN_USE" }, \
		{ NFS_DELEGATED_STATE,		"DELEGATED" }, \
		{ NFS_OPEN_STATE,		"OPEN" }, \
		{ NFS_O_RDONLY_STATE,		"O_RDONLY" }, \
		{ NFS_O_WRONLY_STATE,		"O_WRONLY" }, \
		{ NFS_O_RDWR_STATE,		"O_RDWR" }, \
		{ NFS_STATE_RECLAIM_REBOOT,	"RECLAIM_REBOOT" }, \
		{ NFS_STATE_RECLAIM_NOGRACE,	"RECLAIM_NOGRACE" }, \
		{ NFS_STATE_POSIX_LOCKS,	"POSIX_LOCKS" }, \
		{ NFS_STATE_RECOVERY_FAILED,	"RECOVERY_FAILED" }, \
		{ NFS_STATE_MAY_NOTIFY_LOCK,	"MAY_NOTIFY_LOCK" }, \
		{ NFS_STATE_CHANGE_WAIT,	"CHANGE_WAIT" }, \
		{ NFS_CLNT_DST_SSC_COPY_STATE,	"CLNT_DST_SSC_COPY" }, \
		{ NFS_CLNT_SRC_SSC_COPY_STATE,	"CLNT_SRC_SSC_COPY" }, \
		{ NFS_SRV_SSC_COPY_STATE,	"SRV_SSC_COPY" })

#define show_nfs4_lock_flags(flags) \
	__print_flags(flags, "|", \
		{ BIT(NFS_LOCK_INITIALIZED),	"INITIALIZED" }, \
		{ BIT(NFS_LOCK_LOST),		"LOST" })

TRACE_EVENT(nfs4_state_lock_reclaim,
		TP_PROTO(
			const struct nfs4_state *state,
			const struct nfs4_lock_state *lock
		),

		TP_ARGS(state, lock),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned long, state_flags)
			__field(unsigned long, lock_flags)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = state->inode;

			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->state_flags = state->flags;
			__entry->lock_flags = lock->ls_flags;
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
		),

		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x state_flags=%s lock_flags=%s",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid, __entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash,
			show_nfs4_state_flags(__entry->state_flags),
			show_nfs4_lock_flags(__entry->lock_flags)
		)
)

DECLARE_EVENT_CLASS(nfs4_set_delegation_event,
		TP_PROTO(
			const struct inode *inode,
			fmode_t fmode
		),

		TP_ARGS(inode, fmode),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned int, fmode)
		),

		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->fmode = (__force unsigned int)fmode;
		),

		TP_printk(
			"fmode=%s fileid=%02x:%02x:%llu fhandle=0x%08x",
			show_fs_fmode_flags(__entry->fmode),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle
		)
);
#define DEFINE_NFS4_SET_DELEGATION_EVENT(name) \
	DEFINE_EVENT(nfs4_set_delegation_event, name, \
			TP_PROTO( \
				const struct inode *inode, \
				fmode_t fmode \
			), \
			TP_ARGS(inode, fmode))
DEFINE_NFS4_SET_DELEGATION_EVENT(nfs4_set_delegation);
DEFINE_NFS4_SET_DELEGATION_EVENT(nfs4_reclaim_delegation);

TRACE_EVENT(nfs4_delegreturn_exit,
		TP_PROTO(
			const struct nfs4_delegreturnargs *args,
			const struct nfs4_delegreturnres *res,
			int error
		),

		TP_ARGS(args, res, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			__entry->dev = res->server->s_dev;
			__entry->fhandle = nfs_fhandle_hash(args->fhandle);
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(args->stateid->seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(args->stateid);
		),

		TP_printk(
			"error=%ld (%s) dev=%02x:%02x fhandle=0x%08x "
			"stateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash
		)
);

#ifdef CONFIG_NFS_V4_1
DECLARE_EVENT_CLASS(nfs4_test_stateid_event,
		TP_PROTO(
			const struct nfs4_state *state,
			const struct nfs4_lock_state *lsp,
			int error
		),

		TP_ARGS(state, lsp, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = state->inode;

			__entry->error = error < 0 ? -error : 0;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash
		)
);

#define DEFINE_NFS4_TEST_STATEID_EVENT(name) \
	DEFINE_EVENT(nfs4_test_stateid_event, name, \
			TP_PROTO( \
				const struct nfs4_state *state, \
				const struct nfs4_lock_state *lsp, \
				int error \
			), \
			TP_ARGS(state, lsp, error))
DEFINE_NFS4_TEST_STATEID_EVENT(nfs4_test_delegation_stateid);
DEFINE_NFS4_TEST_STATEID_EVENT(nfs4_test_open_stateid);
DEFINE_NFS4_TEST_STATEID_EVENT(nfs4_test_lock_stateid);
#endif /* CONFIG_NFS_V4_1 */

DECLARE_EVENT_CLASS(nfs4_lookup_event,
		TP_PROTO(
			const struct inode *dir,
			const struct qstr *name,
			int error
		),

		TP_ARGS(dir, name, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(unsigned long, error)
			__field(u64, dir)
			__string(name, name->name)
		),

		TP_fast_assign(
			__entry->dev = dir->i_sb->s_dev;
			__entry->dir = NFS_FILEID(dir);
			__entry->error = -error;
			__assign_str(name, name->name);
		),

		TP_printk(
			"error=%ld (%s) name=%02x:%02x:%llu/%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->dir,
			__get_str(name)
		)
);

#define DEFINE_NFS4_LOOKUP_EVENT(name) \
	DEFINE_EVENT(nfs4_lookup_event, name, \
			TP_PROTO( \
				const struct inode *dir, \
				const struct qstr *name, \
				int error \
			), \
			TP_ARGS(dir, name, error))

DEFINE_NFS4_LOOKUP_EVENT(nfs4_lookup);
DEFINE_NFS4_LOOKUP_EVENT(nfs4_symlink);
DEFINE_NFS4_LOOKUP_EVENT(nfs4_mkdir);
DEFINE_NFS4_LOOKUP_EVENT(nfs4_mknod);
DEFINE_NFS4_LOOKUP_EVENT(nfs4_remove);
DEFINE_NFS4_LOOKUP_EVENT(nfs4_get_fs_locations);
DEFINE_NFS4_LOOKUP_EVENT(nfs4_secinfo);

TRACE_EVENT(nfs4_lookupp,
		TP_PROTO(
			const struct inode *inode,
			int error
		),

		TP_ARGS(inode, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u64, ino)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->ino = NFS_FILEID(inode);
			__entry->error = error < 0 ? -error : 0;
		),

		TP_printk(
			"error=%ld (%s) inode=%02x:%02x:%llu",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->ino
		)
);

TRACE_EVENT(nfs4_rename,
		TP_PROTO(
			const struct inode *olddir,
			const struct qstr *oldname,
			const struct inode *newdir,
			const struct qstr *newname,
			int error
		),

		TP_ARGS(olddir, oldname, newdir, newname, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(unsigned long, error)
			__field(u64, olddir)
			__string(oldname, oldname->name)
			__field(u64, newdir)
			__string(newname, newname->name)
		),

		TP_fast_assign(
			__entry->dev = olddir->i_sb->s_dev;
			__entry->olddir = NFS_FILEID(olddir);
			__entry->newdir = NFS_FILEID(newdir);
			__entry->error = error < 0 ? -error : 0;
			__assign_str(oldname, oldname->name);
			__assign_str(newname, newname->name);
		),

		TP_printk(
			"error=%ld (%s) oldname=%02x:%02x:%llu/%s "
			"newname=%02x:%02x:%llu/%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->olddir,
			__get_str(oldname),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->newdir,
			__get_str(newname)
		)
);

DECLARE_EVENT_CLASS(nfs4_inode_event,
		TP_PROTO(
			const struct inode *inode,
			int error
		),

		TP_ARGS(inode, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->error = error < 0 ? -error : 0;
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle
		)
);

#define DEFINE_NFS4_INODE_EVENT(name) \
	DEFINE_EVENT(nfs4_inode_event, name, \
			TP_PROTO( \
				const struct inode *inode, \
				int error \
			), \
			TP_ARGS(inode, error))

DEFINE_NFS4_INODE_EVENT(nfs4_access);
DEFINE_NFS4_INODE_EVENT(nfs4_readlink);
DEFINE_NFS4_INODE_EVENT(nfs4_readdir);
DEFINE_NFS4_INODE_EVENT(nfs4_get_acl);
DEFINE_NFS4_INODE_EVENT(nfs4_set_acl);
#ifdef CONFIG_NFS_V4_SECURITY_LABEL
DEFINE_NFS4_INODE_EVENT(nfs4_get_security_label);
DEFINE_NFS4_INODE_EVENT(nfs4_set_security_label);
#endif /* CONFIG_NFS_V4_SECURITY_LABEL */

DECLARE_EVENT_CLASS(nfs4_inode_stateid_event,
		TP_PROTO(
			const struct inode *inode,
			const nfs4_stateid *stateid,
			int error
		),

		TP_ARGS(inode, stateid, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(stateid->seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash
		)
);

#define DEFINE_NFS4_INODE_STATEID_EVENT(name) \
	DEFINE_EVENT(nfs4_inode_stateid_event, name, \
			TP_PROTO( \
				const struct inode *inode, \
				const nfs4_stateid *stateid, \
				int error \
			), \
			TP_ARGS(inode, stateid, error))

DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_setattr);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_delegreturn);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_open_stateid_update);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_open_stateid_update_wait);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_close_stateid_update_wait);

DECLARE_EVENT_CLASS(nfs4_getattr_event,
		TP_PROTO(
			const struct nfs_server *server,
			const struct nfs_fh *fhandle,
			const struct nfs_fattr *fattr,
			int error
		),

		TP_ARGS(server, fhandle, fattr, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned int, valid)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			__entry->dev = server->s_dev;
			__entry->valid = fattr->valid;
			__entry->fhandle = nfs_fhandle_hash(fhandle);
			__entry->fileid = (fattr->valid & NFS_ATTR_FATTR_FILEID) ? fattr->fileid : 0;
			__entry->error = error < 0 ? -error : 0;
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"valid=%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			show_nfs_fattr_flags(__entry->valid)
		)
);

#define DEFINE_NFS4_GETATTR_EVENT(name) \
	DEFINE_EVENT(nfs4_getattr_event, name, \
			TP_PROTO( \
				const struct nfs_server *server, \
				const struct nfs_fh *fhandle, \
				const struct nfs_fattr *fattr, \
				int error \
			), \
			TP_ARGS(server, fhandle, fattr, error))
DEFINE_NFS4_GETATTR_EVENT(nfs4_getattr);
DEFINE_NFS4_GETATTR_EVENT(nfs4_lookup_root);
DEFINE_NFS4_GETATTR_EVENT(nfs4_fsinfo);

DECLARE_EVENT_CLASS(nfs4_inode_callback_event,
		TP_PROTO(
			const struct nfs_client *clp,
			const struct nfs_fh *fhandle,
			const struct inode *inode,
			int error
		),

		TP_ARGS(clp, fhandle, inode, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__string(dstaddr, clp ? clp->cl_hostname : "unknown")
		),

		TP_fast_assign(
			__entry->error = error < 0 ? -error : 0;
			__entry->fhandle = nfs_fhandle_hash(fhandle);
			if (!IS_ERR_OR_NULL(inode)) {
				__entry->fileid = NFS_FILEID(inode);
				__entry->dev = inode->i_sb->s_dev;
			} else {
				__entry->fileid = 0;
				__entry->dev = 0;
			}
			__assign_str(dstaddr, clp ? clp->cl_hostname : "unknown");
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"dstaddr=%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__get_str(dstaddr)
		)
);

#define DEFINE_NFS4_INODE_CALLBACK_EVENT(name) \
	DEFINE_EVENT(nfs4_inode_callback_event, name, \
			TP_PROTO( \
				const struct nfs_client *clp, \
				const struct nfs_fh *fhandle, \
				const struct inode *inode, \
				int error \
			), \
			TP_ARGS(clp, fhandle, inode, error))
DEFINE_NFS4_INODE_CALLBACK_EVENT(nfs4_cb_getattr);

DECLARE_EVENT_CLASS(nfs4_inode_stateid_callback_event,
		TP_PROTO(
			const struct nfs_client *clp,
			const struct nfs_fh *fhandle,
			const struct inode *inode,
			const nfs4_stateid *stateid,
			int error
		),

		TP_ARGS(clp, fhandle, inode, stateid, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__string(dstaddr, clp ? clp->cl_hostname : "unknown")
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			__entry->error = error < 0 ? -error : 0;
			__entry->fhandle = nfs_fhandle_hash(fhandle);
			if (!IS_ERR_OR_NULL(inode)) {
				__entry->fileid = NFS_FILEID(inode);
				__entry->dev = inode->i_sb->s_dev;
			} else {
				__entry->fileid = 0;
				__entry->dev = 0;
			}
			__assign_str(dstaddr, clp ? clp->cl_hostname : "unknown");
			__entry->stateid_seq =
				be32_to_cpu(stateid->seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x dstaddr=%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash,
			__get_str(dstaddr)
		)
);

#define DEFINE_NFS4_INODE_STATEID_CALLBACK_EVENT(name) \
	DEFINE_EVENT(nfs4_inode_stateid_callback_event, name, \
			TP_PROTO( \
				const struct nfs_client *clp, \
				const struct nfs_fh *fhandle, \
				const struct inode *inode, \
				const nfs4_stateid *stateid, \
				int error \
			), \
			TP_ARGS(clp, fhandle, inode, stateid, error))
DEFINE_NFS4_INODE_STATEID_CALLBACK_EVENT(nfs4_cb_recall);
DEFINE_NFS4_INODE_STATEID_CALLBACK_EVENT(nfs4_cb_layoutrecall_file);

DECLARE_EVENT_CLASS(nfs4_idmap_event,
		TP_PROTO(
			const char *name,
			int len,
			u32 id,
			int error
		),

		TP_ARGS(name, len, id, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(u32, id)
			__dynamic_array(char, name, len > 0 ? len + 1 : 1)
		),

		TP_fast_assign(
			if (len < 0)
				len = 0;
			__entry->error = error < 0 ? error : 0;
			__entry->id = id;
			memcpy(__get_str(name), name, len);
			__get_str(name)[len] = 0;
		),

		TP_printk(
			"error=%ld (%s) id=%u name=%s",
			-__entry->error, show_nfs4_status(__entry->error),
			__entry->id,
			__get_str(name)
		)
);
#define DEFINE_NFS4_IDMAP_EVENT(name) \
	DEFINE_EVENT(nfs4_idmap_event, name, \
			TP_PROTO( \
				const char *name, \
				int len, \
				u32 id, \
				int error \
			), \
			TP_ARGS(name, len, id, error))
DEFINE_NFS4_IDMAP_EVENT(nfs4_map_name_to_uid);
DEFINE_NFS4_IDMAP_EVENT(nfs4_map_group_to_gid);
DEFINE_NFS4_IDMAP_EVENT(nfs4_map_uid_to_name);
DEFINE_NFS4_IDMAP_EVENT(nfs4_map_gid_to_group);

#ifdef CONFIG_NFS_V4_1
#define NFS4_LSEG_LAYOUT_STATEID_HASH(lseg) \
	(lseg ? nfs_stateid_hash(&lseg->pls_layout->plh_stateid) : 0)
#else
#define NFS4_LSEG_LAYOUT_STATEID_HASH(lseg) (0)
#endif

DECLARE_EVENT_CLASS(nfs4_read_event,
		TP_PROTO(
			const struct nfs_pgio_header *hdr,
			int error
		),

		TP_ARGS(hdr, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(loff_t, offset)
			__field(u32, arg_count)
			__field(u32, res_count)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
			__field(int, layoutstateid_seq)
			__field(u32, layoutstateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = hdr->inode;
			const struct nfs_inode *nfsi = NFS_I(inode);
			const struct nfs_fh *fh = hdr->args.fh ?
						  hdr->args.fh : &nfsi->fh;
			const struct nfs4_state *state =
				hdr->args.context->state;
			const struct pnfs_layout_segment *lseg = hdr->lseg;

			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(fh);
			__entry->offset = hdr->args.offset;
			__entry->arg_count = hdr->args.count;
			__entry->res_count = hdr->res.count;
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
			__entry->layoutstateid_seq = lseg ? lseg->pls_seq : 0;
			__entry->layoutstateid_hash =
				NFS4_LSEG_LAYOUT_STATEID_HASH(lseg);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%u res=%u stateid=%d:0x%08x "
			"layoutstateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			(long long)__entry->offset,
			__entry->arg_count, __entry->res_count,
			__entry->stateid_seq, __entry->stateid_hash,
			__entry->layoutstateid_seq, __entry->layoutstateid_hash
		)
);
#define DEFINE_NFS4_READ_EVENT(name) \
	DEFINE_EVENT(nfs4_read_event, name, \
			TP_PROTO( \
				const struct nfs_pgio_header *hdr, \
				int error \
			), \
			TP_ARGS(hdr, error))
DEFINE_NFS4_READ_EVENT(nfs4_read);
#ifdef CONFIG_NFS_V4_1
DEFINE_NFS4_READ_EVENT(nfs4_pnfs_read);
#endif /* CONFIG_NFS_V4_1 */

DECLARE_EVENT_CLASS(nfs4_write_event,
		TP_PROTO(
			const struct nfs_pgio_header *hdr,
			int error
		),

		TP_ARGS(hdr, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(loff_t, offset)
			__field(u32, arg_count)
			__field(u32, res_count)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
			__field(int, layoutstateid_seq)
			__field(u32, layoutstateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = hdr->inode;
			const struct nfs_inode *nfsi = NFS_I(inode);
			const struct nfs_fh *fh = hdr->args.fh ?
						  hdr->args.fh : &nfsi->fh;
			const struct nfs4_state *state =
				hdr->args.context->state;
			const struct pnfs_layout_segment *lseg = hdr->lseg;

			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(fh);
			__entry->offset = hdr->args.offset;
			__entry->arg_count = hdr->args.count;
			__entry->res_count = hdr->res.count;
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
			__entry->layoutstateid_seq = lseg ? lseg->pls_seq : 0;
			__entry->layoutstateid_hash =
				NFS4_LSEG_LAYOUT_STATEID_HASH(lseg);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%u res=%u stateid=%d:0x%08x "
			"layoutstateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			(long long)__entry->offset,
			__entry->arg_count, __entry->res_count,
			__entry->stateid_seq, __entry->stateid_hash,
			__entry->layoutstateid_seq, __entry->layoutstateid_hash
		)
);

#define DEFINE_NFS4_WRITE_EVENT(name) \
	DEFINE_EVENT(nfs4_write_event, name, \
			TP_PROTO( \
				const struct nfs_pgio_header *hdr, \
				int error \
			), \
			TP_ARGS(hdr, error))
DEFINE_NFS4_WRITE_EVENT(nfs4_write);
#ifdef CONFIG_NFS_V4_1
DEFINE_NFS4_WRITE_EVENT(nfs4_pnfs_write);
#endif /* CONFIG_NFS_V4_1 */

DECLARE_EVENT_CLASS(nfs4_commit_event,
		TP_PROTO(
			const struct nfs_commit_data *data,
			int error
		),

		TP_ARGS(data, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(unsigned long, error)
			__field(loff_t, offset)
			__field(u32, count)
			__field(int, layoutstateid_seq)
			__field(u32, layoutstateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = data->inode;
			const struct nfs_inode *nfsi = NFS_I(inode);
			const struct nfs_fh *fh = data->args.fh ?
						  data->args.fh : &nfsi->fh;
			const struct pnfs_layout_segment *lseg = data->lseg;

			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = nfsi->fileid;
			__entry->fhandle = nfs_fhandle_hash(fh);
			__entry->offset = data->args.offset;
			__entry->count = data->args.count;
			__entry->error = error < 0 ? -error : 0;
			__entry->layoutstateid_seq = lseg ? lseg->pls_seq : 0;
			__entry->layoutstateid_hash =
				NFS4_LSEG_LAYOUT_STATEID_HASH(lseg);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%u layoutstateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			(long long)__entry->offset,
			__entry->count,
			__entry->layoutstateid_seq, __entry->layoutstateid_hash
		)
);
#define DEFINE_NFS4_COMMIT_EVENT(name) \
	DEFINE_EVENT(nfs4_commit_event, name, \
			TP_PROTO( \
				const struct nfs_commit_data *data, \
				int error \
			), \
			TP_ARGS(data, error))
DEFINE_NFS4_COMMIT_EVENT(nfs4_commit);
#ifdef CONFIG_NFS_V4_1
DEFINE_NFS4_COMMIT_EVENT(nfs4_pnfs_commit_ds);

TRACE_EVENT(nfs4_layoutget,
		TP_PROTO(
			const struct nfs_open_context *ctx,
			const struct pnfs_layout_range *args,
			const struct pnfs_layout_range *res,
			const nfs4_stateid *layout_stateid,
			int error
		),

		TP_ARGS(ctx, args, res, layout_stateid, error),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(u32, iomode)
			__field(u64, offset)
			__field(u64, count)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
			__field(int, layoutstateid_seq)
			__field(u32, layoutstateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = d_inode(ctx->dentry);
			const struct nfs4_state *state = ctx->state;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->iomode = args->iomode;
			__entry->offset = args->offset;
			__entry->count = args->length;
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
			if (!error) {
				__entry->layoutstateid_seq =
				be32_to_cpu(layout_stateid->seqid);
				__entry->layoutstateid_hash =
				nfs_stateid_hash(layout_stateid);
			} else {
				__entry->layoutstateid_seq = 0;
				__entry->layoutstateid_hash = 0;
			}
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"iomode=%s offset=%llu count=%llu stateid=%d:0x%08x "
			"layoutstateid=%d:0x%08x",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			show_pnfs_layout_iomode(__entry->iomode),
			(unsigned long long)__entry->offset,
			(unsigned long long)__entry->count,
			__entry->stateid_seq, __entry->stateid_hash,
			__entry->layoutstateid_seq, __entry->layoutstateid_hash
		)
);

DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_layoutcommit);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_layoutreturn);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_layoutreturn_on_close);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_layouterror);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_layoutstats);

TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_UNKNOWN);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_NO_PNFS);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_RD_ZEROLEN);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_MDSTHRESH);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_NOMEM);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_BULK_RECALL);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_IO_TEST_FAIL);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_FOUND_CACHED);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_RETURN);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_BLOCKED);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_INVALID_OPEN);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_RETRY);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_SEND_LAYOUTGET);
TRACE_DEFINE_ENUM(PNFS_UPDATE_LAYOUT_EXIT);

#define show_pnfs_update_layout_reason(reason)				\
	__print_symbolic(reason,					\
		{ PNFS_UPDATE_LAYOUT_UNKNOWN, "unknown" },		\
		{ PNFS_UPDATE_LAYOUT_NO_PNFS, "no pnfs" },		\
		{ PNFS_UPDATE_LAYOUT_RD_ZEROLEN, "read+zerolen" },	\
		{ PNFS_UPDATE_LAYOUT_MDSTHRESH, "mdsthresh" },		\
		{ PNFS_UPDATE_LAYOUT_NOMEM, "nomem" },			\
		{ PNFS_UPDATE_LAYOUT_BULK_RECALL, "bulk recall" },	\
		{ PNFS_UPDATE_LAYOUT_IO_TEST_FAIL, "io test fail" },	\
		{ PNFS_UPDATE_LAYOUT_FOUND_CACHED, "found cached" },	\
		{ PNFS_UPDATE_LAYOUT_RETURN, "layoutreturn" },		\
		{ PNFS_UPDATE_LAYOUT_BLOCKED, "layouts blocked" },	\
		{ PNFS_UPDATE_LAYOUT_INVALID_OPEN, "invalid open" },	\
		{ PNFS_UPDATE_LAYOUT_RETRY, "retrying" },	\
		{ PNFS_UPDATE_LAYOUT_SEND_LAYOUTGET, "sent layoutget" }, \
		{ PNFS_UPDATE_LAYOUT_EXIT, "exit" })

TRACE_EVENT(pnfs_update_layout,
		TP_PROTO(struct inode *inode,
			loff_t pos,
			u64 count,
			enum pnfs_iomode iomode,
			struct pnfs_layout_hdr *lo,
			struct pnfs_layout_segment *lseg,
			enum pnfs_update_layout_reason reason
		),
		TP_ARGS(inode, pos, count, iomode, lo, lseg, reason),
		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u64, fileid)
			__field(u32, fhandle)
			__field(loff_t, pos)
			__field(u64, count)
			__field(enum pnfs_iomode, iomode)
			__field(int, layoutstateid_seq)
			__field(u32, layoutstateid_hash)
			__field(long, lseg)
			__field(enum pnfs_update_layout_reason, reason)
		),
		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->pos = pos;
			__entry->count = count;
			__entry->iomode = iomode;
			__entry->reason = reason;
			if (lo != NULL) {
				__entry->layoutstateid_seq =
				be32_to_cpu(lo->plh_stateid.seqid);
				__entry->layoutstateid_hash =
				nfs_stateid_hash(&lo->plh_stateid);
			} else {
				__entry->layoutstateid_seq = 0;
				__entry->layoutstateid_hash = 0;
			}
			__entry->lseg = (long)lseg;
		),
		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"iomode=%s pos=%llu count=%llu "
			"layoutstateid=%d:0x%08x lseg=0x%lx (%s)",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			show_pnfs_layout_iomode(__entry->iomode),
			(unsigned long long)__entry->pos,
			(unsigned long long)__entry->count,
			__entry->layoutstateid_seq, __entry->layoutstateid_hash,
			__entry->lseg,
			show_pnfs_update_layout_reason(__entry->reason)
		)
);

DECLARE_EVENT_CLASS(pnfs_layout_event,
		TP_PROTO(struct inode *inode,
			loff_t pos,
			u64 count,
			enum pnfs_iomode iomode,
			struct pnfs_layout_hdr *lo,
			struct pnfs_layout_segment *lseg
		),
		TP_ARGS(inode, pos, count, iomode, lo, lseg),
		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(u64, fileid)
			__field(u32, fhandle)
			__field(loff_t, pos)
			__field(u64, count)
			__field(enum pnfs_iomode, iomode)
			__field(int, layoutstateid_seq)
			__field(u32, layoutstateid_hash)
			__field(long, lseg)
		),
		TP_fast_assign(
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->pos = pos;
			__entry->count = count;
			__entry->iomode = iomode;
			if (lo != NULL) {
				__entry->layoutstateid_seq =
				be32_to_cpu(lo->plh_stateid.seqid);
				__entry->layoutstateid_hash =
				nfs_stateid_hash(&lo->plh_stateid);
			} else {
				__entry->layoutstateid_seq = 0;
				__entry->layoutstateid_hash = 0;
			}
			__entry->lseg = (long)lseg;
		),
		TP_printk(
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"iomode=%s pos=%llu count=%llu "
			"layoutstateid=%d:0x%08x lseg=0x%lx",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			show_pnfs_layout_iomode(__entry->iomode),
			(unsigned long long)__entry->pos,
			(unsigned long long)__entry->count,
			__entry->layoutstateid_seq, __entry->layoutstateid_hash,
			__entry->lseg
		)
);

#define DEFINE_PNFS_LAYOUT_EVENT(name) \
	DEFINE_EVENT(pnfs_layout_event, name, \
		TP_PROTO(struct inode *inode, \
			loff_t pos, \
			u64 count, \
			enum pnfs_iomode iomode, \
			struct pnfs_layout_hdr *lo, \
			struct pnfs_layout_segment *lseg \
		), \
		TP_ARGS(inode, pos, count, iomode, lo, lseg))

DEFINE_PNFS_LAYOUT_EVENT(pnfs_mds_fallback_pg_init_read);
DEFINE_PNFS_LAYOUT_EVENT(pnfs_mds_fallback_pg_init_write);
DEFINE_PNFS_LAYOUT_EVENT(pnfs_mds_fallback_pg_get_mirror_count);
DEFINE_PNFS_LAYOUT_EVENT(pnfs_mds_fallback_read_done);
DEFINE_PNFS_LAYOUT_EVENT(pnfs_mds_fallback_write_done);
DEFINE_PNFS_LAYOUT_EVENT(pnfs_mds_fallback_read_pagelist);
DEFINE_PNFS_LAYOUT_EVENT(pnfs_mds_fallback_write_pagelist);

DECLARE_EVENT_CLASS(nfs4_deviceid_event,
		TP_PROTO(
			const struct nfs_client *clp,
			const struct nfs4_deviceid *deviceid
		),

		TP_ARGS(clp, deviceid),

		TP_STRUCT__entry(
			__string(dstaddr, clp->cl_hostname)
			__array(unsigned char, deviceid, NFS4_DEVICEID4_SIZE)
		),

		TP_fast_assign(
			__assign_str(dstaddr, clp->cl_hostname);
			memcpy(__entry->deviceid, deviceid->data,
			       NFS4_DEVICEID4_SIZE);
		),

		TP_printk(
			"deviceid=%s, dstaddr=%s",
			__print_hex(__entry->deviceid, NFS4_DEVICEID4_SIZE),
			__get_str(dstaddr)
		)
);
#define DEFINE_PNFS_DEVICEID_EVENT(name) \
	DEFINE_EVENT(nfs4_deviceid_event, name, \
			TP_PROTO(const struct nfs_client *clp, \
				const struct nfs4_deviceid *deviceid \
			), \
			TP_ARGS(clp, deviceid))
DEFINE_PNFS_DEVICEID_EVENT(nfs4_deviceid_free);

DECLARE_EVENT_CLASS(nfs4_deviceid_status,
		TP_PROTO(
			const struct nfs_server *server,
			const struct nfs4_deviceid *deviceid,
			int status
		),

		TP_ARGS(server, deviceid, status),

		TP_STRUCT__entry(
			__field(dev_t, dev)
			__field(int, status)
			__string(dstaddr, server->nfs_client->cl_hostname)
			__array(unsigned char, deviceid, NFS4_DEVICEID4_SIZE)
		),

		TP_fast_assign(
			__entry->dev = server->s_dev;
			__entry->status = status;
			__assign_str(dstaddr, server->nfs_client->cl_hostname);
			memcpy(__entry->deviceid, deviceid->data,
			       NFS4_DEVICEID4_SIZE);
		),

		TP_printk(
			"dev=%02x:%02x: deviceid=%s, dstaddr=%s, status=%d",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			__print_hex(__entry->deviceid, NFS4_DEVICEID4_SIZE),
			__get_str(dstaddr),
			__entry->status
		)
);
#define DEFINE_PNFS_DEVICEID_STATUS(name) \
	DEFINE_EVENT(nfs4_deviceid_status, name, \
			TP_PROTO(const struct nfs_server *server, \
				const struct nfs4_deviceid *deviceid, \
				int status \
			), \
			TP_ARGS(server, deviceid, status))
DEFINE_PNFS_DEVICEID_STATUS(nfs4_getdeviceinfo);
DEFINE_PNFS_DEVICEID_STATUS(nfs4_find_deviceid);

DECLARE_EVENT_CLASS(nfs4_flexfiles_io_event,
		TP_PROTO(
			const struct nfs_pgio_header *hdr
		),

		TP_ARGS(hdr),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(loff_t, offset)
			__field(u32, count)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
			__string(dstaddr, hdr->ds_clp ?
				rpc_peeraddr2str(hdr->ds_clp->cl_rpcclient,
					RPC_DISPLAY_ADDR) : "unknown")
		),

		TP_fast_assign(
			const struct inode *inode = hdr->inode;

			__entry->error = hdr->res.op_status;
			__entry->fhandle = nfs_fhandle_hash(hdr->args.fh);
			__entry->fileid = NFS_FILEID(inode);
			__entry->dev = inode->i_sb->s_dev;
			__entry->offset = hdr->args.offset;
			__entry->count = hdr->args.count;
			__entry->stateid_seq =
				be32_to_cpu(hdr->args.stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&hdr->args.stateid);
			__assign_str(dstaddr, hdr->ds_clp ?
				rpc_peeraddr2str(hdr->ds_clp->cl_rpcclient,
					RPC_DISPLAY_ADDR) : "unknown");
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%llu count=%u stateid=%d:0x%08x dstaddr=%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->count,
			__entry->stateid_seq, __entry->stateid_hash,
			__get_str(dstaddr)
		)
);

#define DEFINE_NFS4_FLEXFILES_IO_EVENT(name) \
	DEFINE_EVENT(nfs4_flexfiles_io_event, name, \
			TP_PROTO( \
				const struct nfs_pgio_header *hdr \
			), \
			TP_ARGS(hdr))
DEFINE_NFS4_FLEXFILES_IO_EVENT(ff_layout_read_error);
DEFINE_NFS4_FLEXFILES_IO_EVENT(ff_layout_write_error);

TRACE_EVENT(ff_layout_commit_error,
		TP_PROTO(
			const struct nfs_commit_data *data
		),

		TP_ARGS(data),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(loff_t, offset)
			__field(u32, count)
			__string(dstaddr, data->ds_clp ?
				rpc_peeraddr2str(data->ds_clp->cl_rpcclient,
					RPC_DISPLAY_ADDR) : "unknown")
		),

		TP_fast_assign(
			const struct inode *inode = data->inode;

			__entry->error = data->res.op_status;
			__entry->fhandle = nfs_fhandle_hash(data->args.fh);
			__entry->fileid = NFS_FILEID(inode);
			__entry->dev = inode->i_sb->s_dev;
			__entry->offset = data->args.offset;
			__entry->count = data->args.count;
			__assign_str(dstaddr, data->ds_clp ?
				rpc_peeraddr2str(data->ds_clp->cl_rpcclient,
					RPC_DISPLAY_ADDR) : "unknown");
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%llu count=%u dstaddr=%s",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->offset, __entry->count,
			__get_str(dstaddr)
		)
);

TRACE_DEFINE_ENUM(NFS4_CONTENT_DATA);
TRACE_DEFINE_ENUM(NFS4_CONTENT_HOLE);

#define show_llseek_mode(what)			\
	__print_symbolic(what,			\
		{ NFS4_CONTENT_DATA, "DATA" },		\
		{ NFS4_CONTENT_HOLE, "HOLE" })

#ifdef CONFIG_NFS_V4_2
TRACE_EVENT(nfs4_llseek,
		TP_PROTO(
			const struct inode *inode,
			const struct nfs42_seek_args *args,
			const struct nfs42_seek_res *res,
			int error
		),

		TP_ARGS(inode, args, res, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(u32, fhandle)
			__field(u32, fileid)
			__field(dev_t, dev)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
			__field(loff_t, offset_s)
			__field(u32, what)
			__field(loff_t, offset_r)
			__field(u32, eof)
		),

		TP_fast_assign(
			const struct nfs_inode *nfsi = NFS_I(inode);
			const struct nfs_fh *fh = args->sa_fh;

			__entry->fileid = nfsi->fileid;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fhandle = nfs_fhandle_hash(fh);
			__entry->offset_s = args->sa_offset;
			__entry->stateid_seq =
				be32_to_cpu(args->sa_stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&args->sa_stateid);
			__entry->what = args->sa_what;
			if (error) {
				__entry->error = -error;
				__entry->offset_r = 0;
				__entry->eof = 0;
			} else {
				__entry->error = 0;
				__entry->offset_r = res->sr_offset;
				__entry->eof = res->sr_eof;
			}
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x offset_s=%llu what=%s "
			"offset_r=%llu eof=%u",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash,
			__entry->offset_s,
			show_llseek_mode(__entry->what),
			__entry->offset_r,
			__entry->eof
		)
);

DECLARE_EVENT_CLASS(nfs4_sparse_event,
		TP_PROTO(
			const struct inode *inode,
			const struct nfs42_falloc_args *args,
			int error
		),

		TP_ARGS(inode, args, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(loff_t, offset)
			__field(loff_t, len)
			__field(dev_t, dev)
			__field(u32, fhandle)
			__field(u64, fileid)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			__entry->error = error < 0 ? -error : 0;
			__entry->offset = args->falloc_offset;
			__entry->len = args->falloc_length;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->stateid_seq =
				be32_to_cpu(args->falloc_stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&args->falloc_stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x offset=%llu len=%llu",
			-__entry->error,
			show_nfs4_status(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash,
			(long long)__entry->offset,
			(long long)__entry->len
		)
);
#define DEFINE_NFS4_SPARSE_EVENT(name) \
	DEFINE_EVENT(nfs4_sparse_event, name, \
			TP_PROTO( \
				const struct inode *inode, \
				const struct nfs42_falloc_args *args, \
				int error \
			), \
			TP_ARGS(inode, args, error))
DEFINE_NFS4_SPARSE_EVENT(nfs4_fallocate);
DEFINE_NFS4_SPARSE_EVENT(nfs4_deallocate);

TRACE_EVENT(nfs4_copy,
		TP_PROTO(
			const struct inode *src_inode,
			const struct inode *dst_inode,
			const struct nfs42_copy_args *args,
			const struct nfs42_copy_res *res,
			const struct nl4_server *nss,
			int error
		),

		TP_ARGS(src_inode, dst_inode, args, res, nss, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(u32, src_fhandle)
			__field(u32, src_fileid)
			__field(u32, dst_fhandle)
			__field(u32, dst_fileid)
			__field(dev_t, src_dev)
			__field(dev_t, dst_dev)
			__field(int, src_stateid_seq)
			__field(u32, src_stateid_hash)
			__field(int, dst_stateid_seq)
			__field(u32, dst_stateid_hash)
			__field(loff_t, src_offset)
			__field(loff_t, dst_offset)
			__field(bool, sync)
			__field(loff_t, len)
			__field(int, res_stateid_seq)
			__field(u32, res_stateid_hash)
			__field(loff_t, res_count)
			__field(bool, res_sync)
			__field(bool, res_cons)
			__field(bool, intra)
		),

		TP_fast_assign(
			const struct nfs_inode *src_nfsi = NFS_I(src_inode);
			const struct nfs_inode *dst_nfsi = NFS_I(dst_inode);

			__entry->src_fileid = src_nfsi->fileid;
			__entry->src_dev = src_inode->i_sb->s_dev;
			__entry->src_fhandle = nfs_fhandle_hash(args->src_fh);
			__entry->src_offset = args->src_pos;
			__entry->dst_fileid = dst_nfsi->fileid;
			__entry->dst_dev = dst_inode->i_sb->s_dev;
			__entry->dst_fhandle = nfs_fhandle_hash(args->dst_fh);
			__entry->dst_offset = args->dst_pos;
			__entry->len = args->count;
			__entry->sync = args->sync;
			__entry->src_stateid_seq =
				be32_to_cpu(args->src_stateid.seqid);
			__entry->src_stateid_hash =
				nfs_stateid_hash(&args->src_stateid);
			__entry->dst_stateid_seq =
				be32_to_cpu(args->dst_stateid.seqid);
			__entry->dst_stateid_hash =
				nfs_stateid_hash(&args->dst_stateid);
			__entry->intra = nss ? 0 : 1;
			if (error) {
				__entry->error = -error;
				__entry->res_stateid_seq = 0;
				__entry->res_stateid_hash = 0;
				__entry->res_count = 0;
				__entry->res_sync = 0;
				__entry->res_cons = 0;
			} else {
				__entry->error = 0;
				__entry->res_stateid_seq =
					be32_to_cpu(res->write_res.stateid.seqid);
				__entry->res_stateid_hash =
					nfs_stateid_hash(&res->write_res.stateid);
				__entry->res_count = res->write_res.count;
				__entry->res_sync = res->synchronous;
				__entry->res_cons = res->consecutive;
			}
		),

		TP_printk(
			"error=%ld (%s) intra=%d src_fileid=%02x:%02x:%llu "
			"src_fhandle=0x%08x dst_fileid=%02x:%02x:%llu "
			"dst_fhandle=0x%08x src_stateid=%d:0x%08x "
			"dst_stateid=%d:0x%08x src_offset=%llu dst_offset=%llu "
			"len=%llu sync=%d cb_stateid=%d:0x%08x res_sync=%d "
			"res_cons=%d res_count=%llu",
			-__entry->error,
			show_nfs4_status(__entry->error),
			__entry->intra,
			MAJOR(__entry->src_dev), MINOR(__entry->src_dev),
			(unsigned long long)__entry->src_fileid,
			__entry->src_fhandle,
			MAJOR(__entry->dst_dev), MINOR(__entry->dst_dev),
			(unsigned long long)__entry->dst_fileid,
			__entry->dst_fhandle,
			__entry->src_stateid_seq, __entry->src_stateid_hash,
			__entry->dst_stateid_seq, __entry->dst_stateid_hash,
			__entry->src_offset,
			__entry->dst_offset,
			__entry->len,
			__entry->sync,
			__entry->res_stateid_seq, __entry->res_stateid_hash,
			__entry->res_sync,
			__entry->res_cons,
			__entry->res_count
		)
);
#endif /* CONFIG_NFS_V4_2 */

#endif /* CONFIG_NFS_V4_1 */

#endif /* _TRACE_NFS4_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE nfs4trace
/* This part must be outside protection */
#include <trace/define_trace.h>
