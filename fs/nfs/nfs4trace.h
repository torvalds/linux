/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2013 Trond Myklebust <Trond.Myklebust@netapp.com>
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM nfs4

#if !defined(_TRACE_NFS4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NFS4_H

#include <linux/tracepoint.h>

TRACE_DEFINE_ENUM(EPERM);
TRACE_DEFINE_ENUM(ENOENT);
TRACE_DEFINE_ENUM(EIO);
TRACE_DEFINE_ENUM(ENXIO);
TRACE_DEFINE_ENUM(EACCES);
TRACE_DEFINE_ENUM(EEXIST);
TRACE_DEFINE_ENUM(EXDEV);
TRACE_DEFINE_ENUM(ENOTDIR);
TRACE_DEFINE_ENUM(EISDIR);
TRACE_DEFINE_ENUM(EFBIG);
TRACE_DEFINE_ENUM(ENOSPC);
TRACE_DEFINE_ENUM(EROFS);
TRACE_DEFINE_ENUM(EMLINK);
TRACE_DEFINE_ENUM(ENAMETOOLONG);
TRACE_DEFINE_ENUM(ENOTEMPTY);
TRACE_DEFINE_ENUM(EDQUOT);
TRACE_DEFINE_ENUM(ESTALE);
TRACE_DEFINE_ENUM(EBADHANDLE);
TRACE_DEFINE_ENUM(EBADCOOKIE);
TRACE_DEFINE_ENUM(ENOTSUPP);
TRACE_DEFINE_ENUM(ETOOSMALL);
TRACE_DEFINE_ENUM(EREMOTEIO);
TRACE_DEFINE_ENUM(EBADTYPE);
TRACE_DEFINE_ENUM(EAGAIN);
TRACE_DEFINE_ENUM(ELOOP);
TRACE_DEFINE_ENUM(EOPNOTSUPP);
TRACE_DEFINE_ENUM(EDEADLK);
TRACE_DEFINE_ENUM(ENOMEM);
TRACE_DEFINE_ENUM(EKEYEXPIRED);
TRACE_DEFINE_ENUM(ETIMEDOUT);
TRACE_DEFINE_ENUM(ERESTARTSYS);
TRACE_DEFINE_ENUM(ECONNREFUSED);
TRACE_DEFINE_ENUM(ECONNRESET);
TRACE_DEFINE_ENUM(ENETUNREACH);
TRACE_DEFINE_ENUM(EHOSTUNREACH);
TRACE_DEFINE_ENUM(EHOSTDOWN);
TRACE_DEFINE_ENUM(EPIPE);
TRACE_DEFINE_ENUM(EPFNOSUPPORT);
TRACE_DEFINE_ENUM(EPROTONOSUPPORT);

TRACE_DEFINE_ENUM(NFS4_OK);
TRACE_DEFINE_ENUM(NFS4ERR_ACCESS);
TRACE_DEFINE_ENUM(NFS4ERR_ATTRNOTSUPP);
TRACE_DEFINE_ENUM(NFS4ERR_ADMIN_REVOKED);
TRACE_DEFINE_ENUM(NFS4ERR_BACK_CHAN_BUSY);
TRACE_DEFINE_ENUM(NFS4ERR_BADCHAR);
TRACE_DEFINE_ENUM(NFS4ERR_BADHANDLE);
TRACE_DEFINE_ENUM(NFS4ERR_BADIOMODE);
TRACE_DEFINE_ENUM(NFS4ERR_BADLAYOUT);
TRACE_DEFINE_ENUM(NFS4ERR_BADLABEL);
TRACE_DEFINE_ENUM(NFS4ERR_BADNAME);
TRACE_DEFINE_ENUM(NFS4ERR_BADOWNER);
TRACE_DEFINE_ENUM(NFS4ERR_BADSESSION);
TRACE_DEFINE_ENUM(NFS4ERR_BADSLOT);
TRACE_DEFINE_ENUM(NFS4ERR_BADTYPE);
TRACE_DEFINE_ENUM(NFS4ERR_BADXDR);
TRACE_DEFINE_ENUM(NFS4ERR_BAD_COOKIE);
TRACE_DEFINE_ENUM(NFS4ERR_BAD_HIGH_SLOT);
TRACE_DEFINE_ENUM(NFS4ERR_BAD_RANGE);
TRACE_DEFINE_ENUM(NFS4ERR_BAD_SEQID);
TRACE_DEFINE_ENUM(NFS4ERR_BAD_SESSION_DIGEST);
TRACE_DEFINE_ENUM(NFS4ERR_BAD_STATEID);
TRACE_DEFINE_ENUM(NFS4ERR_CB_PATH_DOWN);
TRACE_DEFINE_ENUM(NFS4ERR_CLID_INUSE);
TRACE_DEFINE_ENUM(NFS4ERR_CLIENTID_BUSY);
TRACE_DEFINE_ENUM(NFS4ERR_COMPLETE_ALREADY);
TRACE_DEFINE_ENUM(NFS4ERR_CONN_NOT_BOUND_TO_SESSION);
TRACE_DEFINE_ENUM(NFS4ERR_DEADLOCK);
TRACE_DEFINE_ENUM(NFS4ERR_DEADSESSION);
TRACE_DEFINE_ENUM(NFS4ERR_DELAY);
TRACE_DEFINE_ENUM(NFS4ERR_DELEG_ALREADY_WANTED);
TRACE_DEFINE_ENUM(NFS4ERR_DELEG_REVOKED);
TRACE_DEFINE_ENUM(NFS4ERR_DENIED);
TRACE_DEFINE_ENUM(NFS4ERR_DIRDELEG_UNAVAIL);
TRACE_DEFINE_ENUM(NFS4ERR_DQUOT);
TRACE_DEFINE_ENUM(NFS4ERR_ENCR_ALG_UNSUPP);
TRACE_DEFINE_ENUM(NFS4ERR_EXIST);
TRACE_DEFINE_ENUM(NFS4ERR_EXPIRED);
TRACE_DEFINE_ENUM(NFS4ERR_FBIG);
TRACE_DEFINE_ENUM(NFS4ERR_FHEXPIRED);
TRACE_DEFINE_ENUM(NFS4ERR_FILE_OPEN);
TRACE_DEFINE_ENUM(NFS4ERR_GRACE);
TRACE_DEFINE_ENUM(NFS4ERR_HASH_ALG_UNSUPP);
TRACE_DEFINE_ENUM(NFS4ERR_INVAL);
TRACE_DEFINE_ENUM(NFS4ERR_IO);
TRACE_DEFINE_ENUM(NFS4ERR_ISDIR);
TRACE_DEFINE_ENUM(NFS4ERR_LAYOUTTRYLATER);
TRACE_DEFINE_ENUM(NFS4ERR_LAYOUTUNAVAILABLE);
TRACE_DEFINE_ENUM(NFS4ERR_LEASE_MOVED);
TRACE_DEFINE_ENUM(NFS4ERR_LOCKED);
TRACE_DEFINE_ENUM(NFS4ERR_LOCKS_HELD);
TRACE_DEFINE_ENUM(NFS4ERR_LOCK_RANGE);
TRACE_DEFINE_ENUM(NFS4ERR_MINOR_VERS_MISMATCH);
TRACE_DEFINE_ENUM(NFS4ERR_MLINK);
TRACE_DEFINE_ENUM(NFS4ERR_MOVED);
TRACE_DEFINE_ENUM(NFS4ERR_NAMETOOLONG);
TRACE_DEFINE_ENUM(NFS4ERR_NOENT);
TRACE_DEFINE_ENUM(NFS4ERR_NOFILEHANDLE);
TRACE_DEFINE_ENUM(NFS4ERR_NOMATCHING_LAYOUT);
TRACE_DEFINE_ENUM(NFS4ERR_NOSPC);
TRACE_DEFINE_ENUM(NFS4ERR_NOTDIR);
TRACE_DEFINE_ENUM(NFS4ERR_NOTEMPTY);
TRACE_DEFINE_ENUM(NFS4ERR_NOTSUPP);
TRACE_DEFINE_ENUM(NFS4ERR_NOT_ONLY_OP);
TRACE_DEFINE_ENUM(NFS4ERR_NOT_SAME);
TRACE_DEFINE_ENUM(NFS4ERR_NO_GRACE);
TRACE_DEFINE_ENUM(NFS4ERR_NXIO);
TRACE_DEFINE_ENUM(NFS4ERR_OLD_STATEID);
TRACE_DEFINE_ENUM(NFS4ERR_OPENMODE);
TRACE_DEFINE_ENUM(NFS4ERR_OP_ILLEGAL);
TRACE_DEFINE_ENUM(NFS4ERR_OP_NOT_IN_SESSION);
TRACE_DEFINE_ENUM(NFS4ERR_PERM);
TRACE_DEFINE_ENUM(NFS4ERR_PNFS_IO_HOLE);
TRACE_DEFINE_ENUM(NFS4ERR_PNFS_NO_LAYOUT);
TRACE_DEFINE_ENUM(NFS4ERR_RECALLCONFLICT);
TRACE_DEFINE_ENUM(NFS4ERR_RECLAIM_BAD);
TRACE_DEFINE_ENUM(NFS4ERR_RECLAIM_CONFLICT);
TRACE_DEFINE_ENUM(NFS4ERR_REJECT_DELEG);
TRACE_DEFINE_ENUM(NFS4ERR_REP_TOO_BIG);
TRACE_DEFINE_ENUM(NFS4ERR_REP_TOO_BIG_TO_CACHE);
TRACE_DEFINE_ENUM(NFS4ERR_REQ_TOO_BIG);
TRACE_DEFINE_ENUM(NFS4ERR_RESOURCE);
TRACE_DEFINE_ENUM(NFS4ERR_RESTOREFH);
TRACE_DEFINE_ENUM(NFS4ERR_RETRY_UNCACHED_REP);
TRACE_DEFINE_ENUM(NFS4ERR_RETURNCONFLICT);
TRACE_DEFINE_ENUM(NFS4ERR_ROFS);
TRACE_DEFINE_ENUM(NFS4ERR_SAME);
TRACE_DEFINE_ENUM(NFS4ERR_SHARE_DENIED);
TRACE_DEFINE_ENUM(NFS4ERR_SEQUENCE_POS);
TRACE_DEFINE_ENUM(NFS4ERR_SEQ_FALSE_RETRY);
TRACE_DEFINE_ENUM(NFS4ERR_SEQ_MISORDERED);
TRACE_DEFINE_ENUM(NFS4ERR_SERVERFAULT);
TRACE_DEFINE_ENUM(NFS4ERR_STALE);
TRACE_DEFINE_ENUM(NFS4ERR_STALE_CLIENTID);
TRACE_DEFINE_ENUM(NFS4ERR_STALE_STATEID);
TRACE_DEFINE_ENUM(NFS4ERR_SYMLINK);
TRACE_DEFINE_ENUM(NFS4ERR_TOOSMALL);
TRACE_DEFINE_ENUM(NFS4ERR_TOO_MANY_OPS);
TRACE_DEFINE_ENUM(NFS4ERR_UNKNOWN_LAYOUTTYPE);
TRACE_DEFINE_ENUM(NFS4ERR_UNSAFE_COMPOUND);
TRACE_DEFINE_ENUM(NFS4ERR_WRONGSEC);
TRACE_DEFINE_ENUM(NFS4ERR_WRONG_CRED);
TRACE_DEFINE_ENUM(NFS4ERR_WRONG_TYPE);
TRACE_DEFINE_ENUM(NFS4ERR_XDEV);

#define show_nfsv4_errors(error) \
	__print_symbolic(error, \
		{ NFS4_OK, "OK" }, \
		/* Mapped by nfs4_stat_to_errno() */ \
		{ EPERM, "EPERM" }, \
		{ ENOENT, "ENOENT" }, \
		{ EIO, "EIO" }, \
		{ ENXIO, "ENXIO" }, \
		{ EACCES, "EACCES" }, \
		{ EEXIST, "EEXIST" }, \
		{ EXDEV, "EXDEV" }, \
		{ ENOTDIR, "ENOTDIR" }, \
		{ EISDIR, "EISDIR" }, \
		{ EFBIG, "EFBIG" }, \
		{ ENOSPC, "ENOSPC" }, \
		{ EROFS, "EROFS" }, \
		{ EMLINK, "EMLINK" }, \
		{ ENAMETOOLONG, "ENAMETOOLONG" }, \
		{ ENOTEMPTY, "ENOTEMPTY" }, \
		{ EDQUOT, "EDQUOT" }, \
		{ ESTALE, "ESTALE" }, \
		{ EBADHANDLE, "EBADHANDLE" }, \
		{ EBADCOOKIE, "EBADCOOKIE" }, \
		{ ENOTSUPP, "ENOTSUPP" }, \
		{ ETOOSMALL, "ETOOSMALL" }, \
		{ EREMOTEIO, "EREMOTEIO" }, \
		{ EBADTYPE, "EBADTYPE" }, \
		{ EAGAIN, "EAGAIN" }, \
		{ ELOOP, "ELOOP" }, \
		{ EOPNOTSUPP, "EOPNOTSUPP" }, \
		{ EDEADLK, "EDEADLK" }, \
		/* RPC errors */ \
		{ ENOMEM, "ENOMEM" }, \
		{ EKEYEXPIRED, "EKEYEXPIRED" }, \
		{ ETIMEDOUT, "ETIMEDOUT" }, \
		{ ERESTARTSYS, "ERESTARTSYS" }, \
		{ ECONNREFUSED, "ECONNREFUSED" }, \
		{ ECONNRESET, "ECONNRESET" }, \
		{ ENETUNREACH, "ENETUNREACH" }, \
		{ EHOSTUNREACH, "EHOSTUNREACH" }, \
		{ EHOSTDOWN, "EHOSTDOWN" }, \
		{ EPIPE, "EPIPE" }, \
		{ EPFNOSUPPORT, "EPFNOSUPPORT" }, \
		{ EPROTONOSUPPORT, "EPROTONOSUPPORT" }, \
		/* NFSv4 native errors */ \
		{ NFS4ERR_ACCESS, "ACCESS" }, \
		{ NFS4ERR_ATTRNOTSUPP, "ATTRNOTSUPP" }, \
		{ NFS4ERR_ADMIN_REVOKED, "ADMIN_REVOKED" }, \
		{ NFS4ERR_BACK_CHAN_BUSY, "BACK_CHAN_BUSY" }, \
		{ NFS4ERR_BADCHAR, "BADCHAR" }, \
		{ NFS4ERR_BADHANDLE, "BADHANDLE" }, \
		{ NFS4ERR_BADIOMODE, "BADIOMODE" }, \
		{ NFS4ERR_BADLAYOUT, "BADLAYOUT" }, \
		{ NFS4ERR_BADLABEL, "BADLABEL" }, \
		{ NFS4ERR_BADNAME, "BADNAME" }, \
		{ NFS4ERR_BADOWNER, "BADOWNER" }, \
		{ NFS4ERR_BADSESSION, "BADSESSION" }, \
		{ NFS4ERR_BADSLOT, "BADSLOT" }, \
		{ NFS4ERR_BADTYPE, "BADTYPE" }, \
		{ NFS4ERR_BADXDR, "BADXDR" }, \
		{ NFS4ERR_BAD_COOKIE, "BAD_COOKIE" }, \
		{ NFS4ERR_BAD_HIGH_SLOT, "BAD_HIGH_SLOT" }, \
		{ NFS4ERR_BAD_RANGE, "BAD_RANGE" }, \
		{ NFS4ERR_BAD_SEQID, "BAD_SEQID" }, \
		{ NFS4ERR_BAD_SESSION_DIGEST, "BAD_SESSION_DIGEST" }, \
		{ NFS4ERR_BAD_STATEID, "BAD_STATEID" }, \
		{ NFS4ERR_CB_PATH_DOWN, "CB_PATH_DOWN" }, \
		{ NFS4ERR_CLID_INUSE, "CLID_INUSE" }, \
		{ NFS4ERR_CLIENTID_BUSY, "CLIENTID_BUSY" }, \
		{ NFS4ERR_COMPLETE_ALREADY, "COMPLETE_ALREADY" }, \
		{ NFS4ERR_CONN_NOT_BOUND_TO_SESSION, \
			"CONN_NOT_BOUND_TO_SESSION" }, \
		{ NFS4ERR_DEADLOCK, "DEADLOCK" }, \
		{ NFS4ERR_DEADSESSION, "DEAD_SESSION" }, \
		{ NFS4ERR_DELAY, "DELAY" }, \
		{ NFS4ERR_DELEG_ALREADY_WANTED, \
			"DELEG_ALREADY_WANTED" }, \
		{ NFS4ERR_DELEG_REVOKED, "DELEG_REVOKED" }, \
		{ NFS4ERR_DENIED, "DENIED" }, \
		{ NFS4ERR_DIRDELEG_UNAVAIL, "DIRDELEG_UNAVAIL" }, \
		{ NFS4ERR_DQUOT, "DQUOT" }, \
		{ NFS4ERR_ENCR_ALG_UNSUPP, "ENCR_ALG_UNSUPP" }, \
		{ NFS4ERR_EXIST, "EXIST" }, \
		{ NFS4ERR_EXPIRED, "EXPIRED" }, \
		{ NFS4ERR_FBIG, "FBIG" }, \
		{ NFS4ERR_FHEXPIRED, "FHEXPIRED" }, \
		{ NFS4ERR_FILE_OPEN, "FILE_OPEN" }, \
		{ NFS4ERR_GRACE, "GRACE" }, \
		{ NFS4ERR_HASH_ALG_UNSUPP, "HASH_ALG_UNSUPP" }, \
		{ NFS4ERR_INVAL, "INVAL" }, \
		{ NFS4ERR_IO, "IO" }, \
		{ NFS4ERR_ISDIR, "ISDIR" }, \
		{ NFS4ERR_LAYOUTTRYLATER, "LAYOUTTRYLATER" }, \
		{ NFS4ERR_LAYOUTUNAVAILABLE, "LAYOUTUNAVAILABLE" }, \
		{ NFS4ERR_LEASE_MOVED, "LEASE_MOVED" }, \
		{ NFS4ERR_LOCKED, "LOCKED" }, \
		{ NFS4ERR_LOCKS_HELD, "LOCKS_HELD" }, \
		{ NFS4ERR_LOCK_RANGE, "LOCK_RANGE" }, \
		{ NFS4ERR_MINOR_VERS_MISMATCH, "MINOR_VERS_MISMATCH" }, \
		{ NFS4ERR_MLINK, "MLINK" }, \
		{ NFS4ERR_MOVED, "MOVED" }, \
		{ NFS4ERR_NAMETOOLONG, "NAMETOOLONG" }, \
		{ NFS4ERR_NOENT, "NOENT" }, \
		{ NFS4ERR_NOFILEHANDLE, "NOFILEHANDLE" }, \
		{ NFS4ERR_NOMATCHING_LAYOUT, "NOMATCHING_LAYOUT" }, \
		{ NFS4ERR_NOSPC, "NOSPC" }, \
		{ NFS4ERR_NOTDIR, "NOTDIR" }, \
		{ NFS4ERR_NOTEMPTY, "NOTEMPTY" }, \
		{ NFS4ERR_NOTSUPP, "NOTSUPP" }, \
		{ NFS4ERR_NOT_ONLY_OP, "NOT_ONLY_OP" }, \
		{ NFS4ERR_NOT_SAME, "NOT_SAME" }, \
		{ NFS4ERR_NO_GRACE, "NO_GRACE" }, \
		{ NFS4ERR_NXIO, "NXIO" }, \
		{ NFS4ERR_OLD_STATEID, "OLD_STATEID" }, \
		{ NFS4ERR_OPENMODE, "OPENMODE" }, \
		{ NFS4ERR_OP_ILLEGAL, "OP_ILLEGAL" }, \
		{ NFS4ERR_OP_NOT_IN_SESSION, "OP_NOT_IN_SESSION" }, \
		{ NFS4ERR_PERM, "PERM" }, \
		{ NFS4ERR_PNFS_IO_HOLE, "PNFS_IO_HOLE" }, \
		{ NFS4ERR_PNFS_NO_LAYOUT, "PNFS_NO_LAYOUT" }, \
		{ NFS4ERR_RECALLCONFLICT, "RECALLCONFLICT" }, \
		{ NFS4ERR_RECLAIM_BAD, "RECLAIM_BAD" }, \
		{ NFS4ERR_RECLAIM_CONFLICT, "RECLAIM_CONFLICT" }, \
		{ NFS4ERR_REJECT_DELEG, "REJECT_DELEG" }, \
		{ NFS4ERR_REP_TOO_BIG, "REP_TOO_BIG" }, \
		{ NFS4ERR_REP_TOO_BIG_TO_CACHE, \
			"REP_TOO_BIG_TO_CACHE" }, \
		{ NFS4ERR_REQ_TOO_BIG, "REQ_TOO_BIG" }, \
		{ NFS4ERR_RESOURCE, "RESOURCE" }, \
		{ NFS4ERR_RESTOREFH, "RESTOREFH" }, \
		{ NFS4ERR_RETRY_UNCACHED_REP, "RETRY_UNCACHED_REP" }, \
		{ NFS4ERR_RETURNCONFLICT, "RETURNCONFLICT" }, \
		{ NFS4ERR_ROFS, "ROFS" }, \
		{ NFS4ERR_SAME, "SAME" }, \
		{ NFS4ERR_SHARE_DENIED, "SHARE_DENIED" }, \
		{ NFS4ERR_SEQUENCE_POS, "SEQUENCE_POS" }, \
		{ NFS4ERR_SEQ_FALSE_RETRY, "SEQ_FALSE_RETRY" }, \
		{ NFS4ERR_SEQ_MISORDERED, "SEQ_MISORDERED" }, \
		{ NFS4ERR_SERVERFAULT, "SERVERFAULT" }, \
		{ NFS4ERR_STALE, "STALE" }, \
		{ NFS4ERR_STALE_CLIENTID, "STALE_CLIENTID" }, \
		{ NFS4ERR_STALE_STATEID, "STALE_STATEID" }, \
		{ NFS4ERR_SYMLINK, "SYMLINK" }, \
		{ NFS4ERR_TOOSMALL, "TOOSMALL" }, \
		{ NFS4ERR_TOO_MANY_OPS, "TOO_MANY_OPS" }, \
		{ NFS4ERR_UNKNOWN_LAYOUTTYPE, "UNKNOWN_LAYOUTTYPE" }, \
		{ NFS4ERR_UNSAFE_COMPOUND, "UNSAFE_COMPOUND" }, \
		{ NFS4ERR_WRONGSEC, "WRONGSEC" }, \
		{ NFS4ERR_WRONG_CRED, "WRONG_CRED" }, \
		{ NFS4ERR_WRONG_TYPE, "WRONG_TYPE" }, \
		{ NFS4ERR_XDEV, "XDEV" })

#define show_open_flags(flags) \
	__print_flags(flags, "|", \
		{ O_CREAT, "O_CREAT" }, \
		{ O_EXCL, "O_EXCL" }, \
		{ O_TRUNC, "O_TRUNC" }, \
		{ O_DIRECT, "O_DIRECT" })

#define show_fmode_flags(mode) \
	__print_flags(mode, "|", \
		{ ((__force unsigned long)FMODE_READ), "READ" }, \
		{ ((__force unsigned long)FMODE_WRITE), "WRITE" }, \
		{ ((__force unsigned long)FMODE_EXEC), "EXEC" })

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
			__entry->error = error;
			__assign_str(dstaddr, clp->cl_hostname);
		),

		TP_printk(
			"error=%ld (%s) dstaddr=%s",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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

#define show_nfs4_sequence_status_flags(status) \
	__print_flags((unsigned long)status, "|", \
		{ SEQ4_STATUS_CB_PATH_DOWN, "CB_PATH_DOWN" }, \
		{ SEQ4_STATUS_CB_GSS_CONTEXTS_EXPIRING, \
			"CB_GSS_CONTEXTS_EXPIRING" }, \
		{ SEQ4_STATUS_CB_GSS_CONTEXTS_EXPIRED, \
			"CB_GSS_CONTEXTS_EXPIRED" }, \
		{ SEQ4_STATUS_EXPIRED_ALL_STATE_REVOKED, \
			"EXPIRED_ALL_STATE_REVOKED" }, \
		{ SEQ4_STATUS_EXPIRED_SOME_STATE_REVOKED, \
			"EXPIRED_SOME_STATE_REVOKED" }, \
		{ SEQ4_STATUS_ADMIN_STATE_REVOKED, \
			"ADMIN_STATE_REVOKED" }, \
		{ SEQ4_STATUS_RECALLABLE_STATE_REVOKED,	 \
			"RECALLABLE_STATE_REVOKED" }, \
		{ SEQ4_STATUS_LEASE_MOVED, "LEASE_MOVED" }, \
		{ SEQ4_STATUS_RESTART_RECLAIM_NEEDED, \
			"RESTART_RECLAIM_NEEDED" }, \
		{ SEQ4_STATUS_CB_PATH_DOWN_SESSION, \
			"CB_PATH_DOWN_SESSION" }, \
		{ SEQ4_STATUS_BACKCHANNEL_FAULT, \
			"BACKCHANNEL_FAULT" })

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
			__field(unsigned int, status_flags)
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
			__entry->error = res->sr_status;
		),
		TP_printk(
			"error=%ld (%s) session=0x%08x slot_nr=%u seq_nr=%u "
			"highest_slotid=%u target_highest_slotid=%u "
			"status_flags=%u (%s)",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
			__entry->session,
			__entry->slot_nr,
			__entry->seq_nr,
			__entry->highest_slotid,
			__entry->target_highest_slotid,
			__entry->status_flags,
			show_nfs4_sequence_status_flags(__entry->status_flags)
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
			show_nfsv4_errors(__entry->error),
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
			show_nfsv4_errors(__entry->error),
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

TRACE_EVENT(nfs4_xdr_status,
		TP_PROTO(
			const struct xdr_stream *xdr,
			u32 op,
			int error
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

		TP_printk(
			"task:%u@%d xid=0x%08x error=%ld (%s) operation=%u",
			__entry->task_id, __entry->client_id, __entry->xid,
			-__entry->error, show_nfsv4_errors(__entry->error),
			__entry->op
		)
);

DECLARE_EVENT_CLASS(nfs4_open_event,
		TP_PROTO(
			const struct nfs_open_context *ctx,
			int flags,
			int error
		),

		TP_ARGS(ctx, flags, error),

		TP_STRUCT__entry(
			__field(unsigned long, error)
			__field(unsigned int, flags)
			__field(unsigned int, fmode)
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
			__entry->fmode = (__force unsigned int)ctx->mode;
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
			"error=%ld (%s) flags=%d (%s) fmode=%s "
			"fileid=%02x:%02x:%llu fhandle=0x%08x "
			"name=%02x:%02x:%llu/%s stateid=%d:0x%08x "
			"openstateid=%d:0x%08x",
			 -__entry->error,
			 show_nfsv4_errors(__entry->error),
			 __entry->flags,
			 show_open_flags(__entry->flags),
			 show_fmode_flags(__entry->fmode),
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
			__entry->fmode ?  show_fmode_flags(__entry->fmode) :
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
			__entry->error = error;
			__entry->stateid_seq =
				be32_to_cpu(args->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&args->stateid);
		),

		TP_printk(
			"error=%ld (%s) fmode=%s fileid=%02x:%02x:%llu "
			"fhandle=0x%08x openstateid=%d:0x%08x",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
			__entry->fmode ?  show_fmode_flags(__entry->fmode) :
					  "closed",
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash
		)
);

TRACE_DEFINE_ENUM(F_GETLK);
TRACE_DEFINE_ENUM(F_SETLK);
TRACE_DEFINE_ENUM(F_SETLKW);
TRACE_DEFINE_ENUM(F_RDLCK);
TRACE_DEFINE_ENUM(F_WRLCK);
TRACE_DEFINE_ENUM(F_UNLCK);

#define show_lock_cmd(type) \
	__print_symbolic((int)type, \
		{ F_GETLK, "GETLK" }, \
		{ F_SETLK, "SETLK" }, \
		{ F_SETLKW, "SETLKW" })
#define show_lock_type(type) \
	__print_symbolic((int)type, \
		{ F_RDLCK, "RDLCK" }, \
		{ F_WRLCK, "WRLCK" }, \
		{ F_UNLCK, "UNLCK" })

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
			__field(int, cmd)
			__field(char, type)
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

			__entry->error = error;
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
			show_nfsv4_errors(__entry->error),
			show_lock_cmd(__entry->cmd),
			show_lock_type(__entry->type),
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
			__field(int, cmd)
			__field(char, type)
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

			__entry->error = error;
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
			show_nfsv4_errors(__entry->error),
			show_lock_cmd(__entry->cmd),
			show_lock_type(__entry->type),
			(long long)__entry->start,
			(long long)__entry->end,
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			__entry->stateid_seq, __entry->stateid_hash,
			__entry->lockstateid_seq, __entry->lockstateid_hash
		)
);

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
			show_fmode_flags(__entry->fmode),
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
			__entry->error = error;
			__entry->stateid_seq =
				be32_to_cpu(args->stateid->seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(args->stateid);
		),

		TP_printk(
			"error=%ld (%s) dev=%02x:%02x fhandle=0x%08x "
			"stateid=%d:0x%08x",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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

			__entry->error = error;
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
			show_nfsv4_errors(__entry->error),
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
			show_nfsv4_errors(__entry->error),
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
			__entry->error = error;
		),

		TP_printk(
			"error=%ld (%s) inode=%02x:%02x:%llu",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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
			__entry->error = error;
			__assign_str(oldname, oldname->name);
			__assign_str(newname, newname->name);
		),

		TP_printk(
			"error=%ld (%s) oldname=%02x:%02x:%llu/%s "
			"newname=%02x:%02x:%llu/%s",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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
			show_nfsv4_errors(__entry->error),
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
			__entry->error = error;
			__entry->stateid_seq =
				be32_to_cpu(stateid->seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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
			__entry->error = error;
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"valid=%s",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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
			__entry->error = error;
			__entry->fhandle = nfs_fhandle_hash(fhandle);
			if (!IS_ERR_OR_NULL(inode)) {
				__entry->fileid = NFS_FILEID(inode);
				__entry->dev = inode->i_sb->s_dev;
			} else {
				__entry->fileid = 0;
				__entry->dev = 0;
			}
			__assign_str(dstaddr, clp ? clp->cl_hostname : "unknown")
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"dstaddr=%s",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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
			__entry->error = error;
			__entry->fhandle = nfs_fhandle_hash(fhandle);
			if (!IS_ERR_OR_NULL(inode)) {
				__entry->fileid = NFS_FILEID(inode);
				__entry->dev = inode->i_sb->s_dev;
			} else {
				__entry->fileid = 0;
				__entry->dev = 0;
			}
			__assign_str(dstaddr, clp ? clp->cl_hostname : "unknown")
			__entry->stateid_seq =
				be32_to_cpu(stateid->seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"stateid=%d:0x%08x dstaddr=%s",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
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
			-__entry->error, show_nfsv4_errors(__entry->error),
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
			__field(size_t, count)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = hdr->inode;
			const struct nfs4_state *state =
				hdr->args.context->state;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->offset = hdr->args.offset;
			__entry->count = hdr->args.count;
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%zu stateid=%d:0x%08x",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			(long long)__entry->offset,
			__entry->count,
			__entry->stateid_seq, __entry->stateid_hash
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
			__field(size_t, count)
			__field(unsigned long, error)
			__field(int, stateid_seq)
			__field(u32, stateid_hash)
		),

		TP_fast_assign(
			const struct inode *inode = hdr->inode;
			const struct nfs4_state *state =
				hdr->args.context->state;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->offset = hdr->args.offset;
			__entry->count = hdr->args.count;
			__entry->error = error < 0 ? -error : 0;
			__entry->stateid_seq =
				be32_to_cpu(state->stateid.seqid);
			__entry->stateid_hash =
				nfs_stateid_hash(&state->stateid);
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%zu stateid=%d:0x%08x",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			(long long)__entry->offset,
			__entry->count,
			__entry->stateid_seq, __entry->stateid_hash
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
			__field(loff_t, offset)
			__field(size_t, count)
			__field(unsigned long, error)
		),

		TP_fast_assign(
			const struct inode *inode = data->inode;
			__entry->dev = inode->i_sb->s_dev;
			__entry->fileid = NFS_FILEID(inode);
			__entry->fhandle = nfs_fhandle_hash(NFS_FH(inode));
			__entry->offset = data->args.offset;
			__entry->count = data->args.count;
			__entry->error = error;
		),

		TP_printk(
			"error=%ld (%s) fileid=%02x:%02x:%llu fhandle=0x%08x "
			"offset=%lld count=%zu",
			-__entry->error,
			show_nfsv4_errors(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			(long long)__entry->offset,
			__entry->count
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

TRACE_DEFINE_ENUM(IOMODE_READ);
TRACE_DEFINE_ENUM(IOMODE_RW);
TRACE_DEFINE_ENUM(IOMODE_ANY);

#define show_pnfs_iomode(iomode) \
	__print_symbolic(iomode, \
		{ IOMODE_READ, "READ" }, \
		{ IOMODE_RW, "RW" }, \
		{ IOMODE_ANY, "ANY" })

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
			__entry->error = error;
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
			show_nfsv4_errors(__entry->error),
			MAJOR(__entry->dev), MINOR(__entry->dev),
			(unsigned long long)__entry->fileid,
			__entry->fhandle,
			show_pnfs_iomode(__entry->iomode),
			(unsigned long long)__entry->offset,
			(unsigned long long)__entry->count,
			__entry->stateid_seq, __entry->stateid_hash,
			__entry->layoutstateid_seq, __entry->layoutstateid_hash
		)
);

DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_layoutcommit);
DEFINE_NFS4_INODE_STATEID_EVENT(nfs4_layoutreturn);
DEFINE_NFS4_INODE_EVENT(nfs4_layoutreturn_on_close);

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
			show_pnfs_iomode(__entry->iomode),
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
			show_pnfs_iomode(__entry->iomode),
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

#endif /* CONFIG_NFS_V4_1 */

#endif /* _TRACE_NFS4_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE nfs4trace
/* This part must be outside protection */
#include <trace/define_trace.h>
