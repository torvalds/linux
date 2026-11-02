/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Pre-xdr'ed nfsd error values and nfsd-internal error codes.
 *
 * Separated from nfsd.h so that nfsd.h itself does not have to pull
 * in <linux/nfs4.h>: the NFS4ERR_* values used below are the only
 * reason that include was needed.
 */

#ifndef LINUX_NFSD_NFSERR_H
#define LINUX_NFSD_NFSERR_H

#include <linux/sunrpc/xdrgen/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>

/*
 * These macros provide pre-xdr'ed values for faster operation.
 */
#define	nfs_ok				cpu_to_be32(NFS_OK)
#define	nfserr_perm			cpu_to_be32(NFSERR_PERM)
#define	nfserr_noent			cpu_to_be32(NFSERR_NOENT)
#define	nfserr_io			cpu_to_be32(NFSERR_IO)
#define	nfserr_nxio			cpu_to_be32(NFSERR_NXIO)
#define	nfserr_acces			cpu_to_be32(NFSERR_ACCES)
#define	nfserr_exist			cpu_to_be32(NFSERR_EXIST)
#define	nfserr_xdev			cpu_to_be32(NFS3ERR_XDEV)
#define	nfserr_nodev			cpu_to_be32(NFSERR_NODEV)
#define	nfserr_notdir			cpu_to_be32(NFSERR_NOTDIR)
#define	nfserr_isdir			cpu_to_be32(NFSERR_ISDIR)
#define	nfserr_inval			cpu_to_be32(NFS3ERR_INVAL)
#define	nfserr_fbig			cpu_to_be32(NFSERR_FBIG)
#define	nfserr_nospc			cpu_to_be32(NFSERR_NOSPC)
#define	nfserr_rofs			cpu_to_be32(NFSERR_ROFS)
#define	nfserr_mlink			cpu_to_be32(NFS3ERR_MLINK)
#define	nfserr_nametoolong		cpu_to_be32(NFSERR_NAMETOOLONG)
#define	nfserr_notempty			cpu_to_be32(NFSERR_NOTEMPTY)
#define	nfserr_dquot			cpu_to_be32(NFSERR_DQUOT)
#define	nfserr_stale			cpu_to_be32(NFSERR_STALE)
#define	nfserr_remote			cpu_to_be32(NFS3ERR_REMOTE)
#define	nfserr_wflush			cpu_to_be32(NFSERR_WFLUSH)
#define	nfserr_badhandle		cpu_to_be32(NFS3ERR_BADHANDLE)
#define	nfserr_notsync			cpu_to_be32(NFS3ERR_NOT_SYNC)
#define	nfserr_badcookie		cpu_to_be32(NFS3ERR_BAD_COOKIE)
#define	nfserr_notsupp			cpu_to_be32(NFS3ERR_NOTSUPP)
#define	nfserr_toosmall			cpu_to_be32(NFS3ERR_TOOSMALL)
#define	nfserr_serverfault		cpu_to_be32(NFS3ERR_SERVERFAULT)
#define	nfserr_badtype			cpu_to_be32(NFS3ERR_BADTYPE)
#define	nfserr_jukebox			cpu_to_be32(NFS3ERR_JUKEBOX)
#define nfserr_delay			cpu_to_be32(NFS4ERR_DELAY)
#define	nfserr_same			cpu_to_be32(NFS4ERR_SAME)
#define	nfserr_denied			cpu_to_be32(NFS4ERR_DENIED)
#define	nfserr_deadlock			cpu_to_be32(NFS4ERR_DEADLOCK)
#define	nfserr_expired			cpu_to_be32(NFS4ERR_EXPIRED)
#define	nfserr_bad_cookie		cpu_to_be32(NFS4ERR_BAD_COOKIE)
#define	nfserr_clid_inuse		cpu_to_be32(NFS4ERR_CLID_INUSE)
#define	nfserr_stale_clientid		cpu_to_be32(NFS4ERR_STALE_CLIENTID)
#define	nfserr_resource			cpu_to_be32(NFS4ERR_RESOURCE)
#define	nfserr_moved			cpu_to_be32(NFS4ERR_MOVED)
#define	nfserr_nofilehandle		cpu_to_be32(NFS4ERR_NOFILEHANDLE)
#define	nfserr_minor_vers_mismatch	cpu_to_be32(NFS4ERR_MINOR_VERS_MISMATCH)
#define nfserr_share_denied		cpu_to_be32(NFS4ERR_SHARE_DENIED)
#define nfserr_stale_stateid		cpu_to_be32(NFS4ERR_STALE_STATEID)
#define nfserr_old_stateid		cpu_to_be32(NFS4ERR_OLD_STATEID)
#define nfserr_bad_stateid		cpu_to_be32(NFS4ERR_BAD_STATEID)
#define nfserr_bad_seqid		cpu_to_be32(NFS4ERR_BAD_SEQID)
#define	nfserr_symlink			cpu_to_be32(NFS4ERR_SYMLINK)
#define	nfserr_not_same			cpu_to_be32(NFS4ERR_NOT_SAME)
#define nfserr_lock_range		cpu_to_be32(NFS4ERR_LOCK_RANGE)
#define	nfserr_restorefh		cpu_to_be32(NFS4ERR_RESTOREFH)
#define	nfserr_attrnotsupp		cpu_to_be32(NFS4ERR_ATTRNOTSUPP)
#define	nfserr_bad_xdr			cpu_to_be32(NFS4ERR_BADXDR)
#define	nfserr_openmode			cpu_to_be32(NFS4ERR_OPENMODE)
#define	nfserr_badowner			cpu_to_be32(NFS4ERR_BADOWNER)
#define	nfserr_locks_held		cpu_to_be32(NFS4ERR_LOCKS_HELD)
#define	nfserr_op_illegal		cpu_to_be32(NFS4ERR_OP_ILLEGAL)
#define	nfserr_grace			cpu_to_be32(NFS4ERR_GRACE)
#define	nfserr_no_grace			cpu_to_be32(NFS4ERR_NO_GRACE)
#define	nfserr_reclaim_bad		cpu_to_be32(NFS4ERR_RECLAIM_BAD)
#define	nfserr_badname			cpu_to_be32(NFS4ERR_BADNAME)
#define	nfserr_admin_revoked		cpu_to_be32(NFS4ERR_ADMIN_REVOKED)
#define	nfserr_cb_path_down		cpu_to_be32(NFS4ERR_CB_PATH_DOWN)
#define	nfserr_locked			cpu_to_be32(NFS4ERR_LOCKED)
#define	nfserr_wrongsec			cpu_to_be32(NFS4ERR_WRONGSEC)
#define nfserr_badiomode		cpu_to_be32(NFS4ERR_BADIOMODE)
#define nfserr_badlayout		cpu_to_be32(NFS4ERR_BADLAYOUT)
#define nfserr_bad_session_digest	cpu_to_be32(NFS4ERR_BAD_SESSION_DIGEST)
#define nfserr_badsession		cpu_to_be32(NFS4ERR_BADSESSION)
#define nfserr_badslot			cpu_to_be32(NFS4ERR_BADSLOT)
#define nfserr_complete_already		cpu_to_be32(NFS4ERR_COMPLETE_ALREADY)
#define nfserr_conn_not_bound_to_session cpu_to_be32(NFS4ERR_CONN_NOT_BOUND_TO_SESSION)
#define nfserr_deleg_already_wanted	cpu_to_be32(NFS4ERR_DELEG_ALREADY_WANTED)
#define nfserr_back_chan_busy		cpu_to_be32(NFS4ERR_BACK_CHAN_BUSY)
#define nfserr_layouttrylater		cpu_to_be32(NFS4ERR_LAYOUTTRYLATER)
#define nfserr_layoutunavailable	cpu_to_be32(NFS4ERR_LAYOUTUNAVAILABLE)
#define nfserr_nomatching_layout	cpu_to_be32(NFS4ERR_NOMATCHING_LAYOUT)
#define nfserr_recallconflict		cpu_to_be32(NFS4ERR_RECALLCONFLICT)
#define nfserr_unknown_layouttype	cpu_to_be32(NFS4ERR_UNKNOWN_LAYOUTTYPE)
#define nfserr_seq_misordered		cpu_to_be32(NFS4ERR_SEQ_MISORDERED)
#define nfserr_sequence_pos		cpu_to_be32(NFS4ERR_SEQUENCE_POS)
#define nfserr_req_too_big		cpu_to_be32(NFS4ERR_REQ_TOO_BIG)
#define nfserr_rep_too_big		cpu_to_be32(NFS4ERR_REP_TOO_BIG)
#define nfserr_rep_too_big_to_cache	cpu_to_be32(NFS4ERR_REP_TOO_BIG_TO_CACHE)
#define nfserr_retry_uncached_rep	cpu_to_be32(NFS4ERR_RETRY_UNCACHED_REP)
#define nfserr_unsafe_compound		cpu_to_be32(NFS4ERR_UNSAFE_COMPOUND)
#define nfserr_too_many_ops		cpu_to_be32(NFS4ERR_TOO_MANY_OPS)
#define nfserr_op_not_in_session	cpu_to_be32(NFS4ERR_OP_NOT_IN_SESSION)
#define nfserr_hash_alg_unsupp		cpu_to_be32(NFS4ERR_HASH_ALG_UNSUPP)
#define nfserr_clientid_busy		cpu_to_be32(NFS4ERR_CLIENTID_BUSY)
#define nfserr_pnfs_io_hole		cpu_to_be32(NFS4ERR_PNFS_IO_HOLE)
#define nfserr_seq_false_retry		cpu_to_be32(NFS4ERR_SEQ_FALSE_RETRY)
#define nfserr_bad_high_slot		cpu_to_be32(NFS4ERR_BAD_HIGH_SLOT)
#define nfserr_deadsession		cpu_to_be32(NFS4ERR_DEADSESSION)
#define nfserr_encr_alg_unsupp		cpu_to_be32(NFS4ERR_ENCR_ALG_UNSUPP)
#define nfserr_pnfs_no_layout		cpu_to_be32(NFS4ERR_PNFS_NO_LAYOUT)
#define nfserr_not_only_op		cpu_to_be32(NFS4ERR_NOT_ONLY_OP)
#define nfserr_wrong_cred		cpu_to_be32(NFS4ERR_WRONG_CRED)
#define nfserr_wrong_type		cpu_to_be32(NFS4ERR_WRONG_TYPE)
#define nfserr_dirdeleg_unavail		cpu_to_be32(NFS4ERR_DIRDELEG_UNAVAIL)
#define nfserr_reject_deleg		cpu_to_be32(NFS4ERR_REJECT_DELEG)
#define nfserr_returnconflict		cpu_to_be32(NFS4ERR_RETURNCONFLICT)
#define nfserr_deleg_revoked		cpu_to_be32(NFS4ERR_DELEG_REVOKED)
#define nfserr_partner_notsupp		cpu_to_be32(NFS4ERR_PARTNER_NOTSUPP)
#define nfserr_partner_no_auth		cpu_to_be32(NFS4ERR_PARTNER_NO_AUTH)
#define nfserr_union_notsupp		cpu_to_be32(NFS4ERR_UNION_NOTSUPP)
#define nfserr_offload_denied		cpu_to_be32(NFS4ERR_OFFLOAD_DENIED)
#define nfserr_wrong_lfs		cpu_to_be32(NFS4ERR_WRONG_LFS)
#define nfserr_badlabel			cpu_to_be32(NFS4ERR_BADLABEL)
#define nfserr_file_open		cpu_to_be32(NFS4ERR_FILE_OPEN)
#define nfserr_xattr2big		cpu_to_be32(NFS4ERR_XATTR2BIG)
#define nfserr_noxattr			cpu_to_be32(NFS4ERR_NOXATTR)

/*
 * Error codes for internal use.  These are based at an impossible
 * nfsstat4 value so that, once converted to be32, they cannot conflict
 * with any value defined by the protocol (compare the nlm__int__* codes
 * in fs/lockd/lockd.h).
 */
enum {
/* end-of-file indicator in readdir */
	NFSERR_EOF = 30000,
#define	nfserr_eof		cpu_to_be32(NFSERR_EOF)

/* replay detected */
	NFSERR_REPLAY_ME,
#define	nfserr_replay_me	cpu_to_be32(NFSERR_REPLAY_ME)

/* nfs41 replay detected */
	NFSERR_REPLAY_CACHE,
#define	nfserr_replay_cache	cpu_to_be32(NFSERR_REPLAY_CACHE)

/* symlink found where dir expected - handled differently to
 * other symlink found errors by NFSv3.
 */
	NFSERR_SYMLINK_NOT_DIR,
#define	nfserr_symlink_not_dir	cpu_to_be32(NFSERR_SYMLINK_NOT_DIR)
};

#endif /* LINUX_NFSD_NFSERR_H */
