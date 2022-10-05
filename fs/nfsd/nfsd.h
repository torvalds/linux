/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hodge-podge collection of knfsd-related stuff.
 * I will sort this out later.
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_NFSD_H
#define LINUX_NFSD_NFSD_H

#include <linux/types.h>
#include <linux/mount.h>

#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/sunrpc/svc.h>
#include <linux/sunrpc/svc_xprt.h>
#include <linux/sunrpc/msg_prot.h>
#include <linux/sunrpc/addr.h>

#include <uapi/linux/nfsd/debug.h>

#include "netns.h"
#include "export.h"
#include "stats.h"

#undef ifdebug
#ifdef CONFIG_SUNRPC_DEBUG
# define ifdebug(flag)		if (nfsd_debug & NFSDDBG_##flag)
#else
# define ifdebug(flag)		if (0)
#endif

/*
 * nfsd version
 */
#define NFSD_SUPPORTED_MINOR_VERSION	2
/*
 * Maximum blocksizes supported by daemon under various circumstances.
 */
#define NFSSVC_MAXBLKSIZE       RPCSVC_MAXPAYLOAD
/* NFSv2 is limited by the protocol specification, see RFC 1094 */
#define NFSSVC_MAXBLKSIZE_V2    (8*1024)


/*
 * Largest number of bytes we need to allocate for an NFS
 * call or reply.  Used to control buffer sizes.  We use
 * the length of v3 WRITE, READDIR and READDIR replies
 * which are an RPC header, up to 26 XDR units of reply
 * data, and some page data.
 *
 * Note that accuracy here doesn't matter too much as the
 * size is rounded up to a page size when allocating space.
 */
#define NFSD_BUFSIZE            ((RPC_MAX_HEADER_WITH_AUTH+26)*XDR_UNIT + NFSSVC_MAXBLKSIZE)

struct readdir_cd {
	__be32			err;	/* 0, nfserr, or nfserr_eof */
};


extern struct svc_program	nfsd_program;
extern const struct svc_version	nfsd_version2, nfsd_version3,
				nfsd_version4;
extern struct mutex		nfsd_mutex;
extern spinlock_t		nfsd_drc_lock;
extern unsigned long		nfsd_drc_max_mem;
extern unsigned long		nfsd_drc_mem_used;

extern const struct seq_operations nfs_exports_op;

/*
 * Common void argument and result helpers
 */
struct nfsd_voidargs { };
struct nfsd_voidres { };
bool		nfssvc_decode_voidarg(struct svc_rqst *rqstp,
				      struct xdr_stream *xdr);
bool		nfssvc_encode_voidres(struct svc_rqst *rqstp,
				      struct xdr_stream *xdr);

/*
 * Function prototypes.
 */
int		nfsd_svc(int nrservs, struct net *net, const struct cred *cred);
int		nfsd_dispatch(struct svc_rqst *rqstp, __be32 *statp);

int		nfsd_nrthreads(struct net *);
int		nfsd_nrpools(struct net *);
int		nfsd_get_nrthreads(int n, int *, struct net *);
int		nfsd_set_nrthreads(int n, int *, struct net *);
int		nfsd_pool_stats_open(struct inode *, struct file *);
int		nfsd_pool_stats_release(struct inode *, struct file *);
void		nfsd_shutdown_threads(struct net *net);

void		nfsd_put(struct net *net);

bool		i_am_nfsd(void);

struct nfsdfs_client {
	struct kref cl_ref;
	void (*cl_release)(struct kref *kref);
};

struct nfsdfs_client *get_nfsdfs_client(struct inode *);
struct dentry *nfsd_client_mkdir(struct nfsd_net *nn,
				 struct nfsdfs_client *ncl, u32 id,
				 const struct tree_descr *,
				 struct dentry **fdentries);
void nfsd_client_rmdir(struct dentry *dentry);


#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
#ifdef CONFIG_NFSD_V2_ACL
extern const struct svc_version nfsd_acl_version2;
#else
#define nfsd_acl_version2 NULL
#endif
#ifdef CONFIG_NFSD_V3_ACL
extern const struct svc_version nfsd_acl_version3;
#else
#define nfsd_acl_version3 NULL
#endif
#endif

struct nfsd_net;

enum vers_op {NFSD_SET, NFSD_CLEAR, NFSD_TEST, NFSD_AVAIL };
int nfsd_vers(struct nfsd_net *nn, int vers, enum vers_op change);
int nfsd_minorversion(struct nfsd_net *nn, u32 minorversion, enum vers_op change);
void nfsd_reset_versions(struct nfsd_net *nn);
int nfsd_create_serv(struct net *net);

extern int nfsd_max_blksize;

static inline int nfsd_v4client(struct svc_rqst *rq)
{
	return rq->rq_prog == NFS_PROGRAM && rq->rq_vers == 4;
}
static inline struct user_namespace *
nfsd_user_namespace(const struct svc_rqst *rqstp)
{
	const struct cred *cred = rqstp->rq_xprt->xpt_cred;
	return cred ? cred->user_ns : &init_user_ns;
}

/* 
 * NFSv4 State
 */
#ifdef CONFIG_NFSD_V4
extern unsigned long max_delegations;
int nfsd4_init_slabs(void);
void nfsd4_free_slabs(void);
int nfs4_state_start(void);
int nfs4_state_start_net(struct net *net);
void nfs4_state_shutdown(void);
void nfs4_state_shutdown_net(struct net *net);
int nfs4_reset_recoverydir(char *recdir);
char * nfs4_recoverydir(void);
bool nfsd4_spo_must_allow(struct svc_rqst *rqstp);
int nfsd4_create_laundry_wq(void);
void nfsd4_destroy_laundry_wq(void);
#else
static inline int nfsd4_init_slabs(void) { return 0; }
static inline void nfsd4_free_slabs(void) { }
static inline int nfs4_state_start(void) { return 0; }
static inline int nfs4_state_start_net(struct net *net) { return 0; }
static inline void nfs4_state_shutdown(void) { }
static inline void nfs4_state_shutdown_net(struct net *net) { }
static inline int nfs4_reset_recoverydir(char *recdir) { return 0; }
static inline char * nfs4_recoverydir(void) {return NULL; }
static inline bool nfsd4_spo_must_allow(struct svc_rqst *rqstp)
{
	return false;
}
static inline int nfsd4_create_laundry_wq(void) { return 0; };
static inline void nfsd4_destroy_laundry_wq(void) {};
#endif

/*
 * lockd binding
 */
void		nfsd_lockd_init(void);
void		nfsd_lockd_shutdown(void);


/*
 * These macros provide pre-xdr'ed values for faster operation.
 */
#define	nfs_ok			cpu_to_be32(NFS_OK)
#define	nfserr_perm		cpu_to_be32(NFSERR_PERM)
#define	nfserr_noent		cpu_to_be32(NFSERR_NOENT)
#define	nfserr_io		cpu_to_be32(NFSERR_IO)
#define	nfserr_nxio		cpu_to_be32(NFSERR_NXIO)
#define	nfserr_eagain		cpu_to_be32(NFSERR_EAGAIN)
#define	nfserr_acces		cpu_to_be32(NFSERR_ACCES)
#define	nfserr_exist		cpu_to_be32(NFSERR_EXIST)
#define	nfserr_xdev		cpu_to_be32(NFSERR_XDEV)
#define	nfserr_nodev		cpu_to_be32(NFSERR_NODEV)
#define	nfserr_notdir		cpu_to_be32(NFSERR_NOTDIR)
#define	nfserr_isdir		cpu_to_be32(NFSERR_ISDIR)
#define	nfserr_inval		cpu_to_be32(NFSERR_INVAL)
#define	nfserr_fbig		cpu_to_be32(NFSERR_FBIG)
#define	nfserr_nospc		cpu_to_be32(NFSERR_NOSPC)
#define	nfserr_rofs		cpu_to_be32(NFSERR_ROFS)
#define	nfserr_mlink		cpu_to_be32(NFSERR_MLINK)
#define	nfserr_opnotsupp	cpu_to_be32(NFSERR_OPNOTSUPP)
#define	nfserr_nametoolong	cpu_to_be32(NFSERR_NAMETOOLONG)
#define	nfserr_notempty		cpu_to_be32(NFSERR_NOTEMPTY)
#define	nfserr_dquot		cpu_to_be32(NFSERR_DQUOT)
#define	nfserr_stale		cpu_to_be32(NFSERR_STALE)
#define	nfserr_remote		cpu_to_be32(NFSERR_REMOTE)
#define	nfserr_wflush		cpu_to_be32(NFSERR_WFLUSH)
#define	nfserr_badhandle	cpu_to_be32(NFSERR_BADHANDLE)
#define	nfserr_notsync		cpu_to_be32(NFSERR_NOT_SYNC)
#define	nfserr_badcookie	cpu_to_be32(NFSERR_BAD_COOKIE)
#define	nfserr_notsupp		cpu_to_be32(NFSERR_NOTSUPP)
#define	nfserr_toosmall		cpu_to_be32(NFSERR_TOOSMALL)
#define	nfserr_serverfault	cpu_to_be32(NFSERR_SERVERFAULT)
#define	nfserr_badtype		cpu_to_be32(NFSERR_BADTYPE)
#define	nfserr_jukebox		cpu_to_be32(NFSERR_JUKEBOX)
#define	nfserr_denied		cpu_to_be32(NFSERR_DENIED)
#define	nfserr_deadlock		cpu_to_be32(NFSERR_DEADLOCK)
#define nfserr_expired          cpu_to_be32(NFSERR_EXPIRED)
#define	nfserr_bad_cookie	cpu_to_be32(NFSERR_BAD_COOKIE)
#define	nfserr_same		cpu_to_be32(NFSERR_SAME)
#define	nfserr_clid_inuse	cpu_to_be32(NFSERR_CLID_INUSE)
#define	nfserr_stale_clientid	cpu_to_be32(NFSERR_STALE_CLIENTID)
#define	nfserr_resource		cpu_to_be32(NFSERR_RESOURCE)
#define	nfserr_moved		cpu_to_be32(NFSERR_MOVED)
#define	nfserr_nofilehandle	cpu_to_be32(NFSERR_NOFILEHANDLE)
#define	nfserr_minor_vers_mismatch	cpu_to_be32(NFSERR_MINOR_VERS_MISMATCH)
#define nfserr_share_denied	cpu_to_be32(NFSERR_SHARE_DENIED)
#define nfserr_stale_stateid	cpu_to_be32(NFSERR_STALE_STATEID)
#define nfserr_old_stateid	cpu_to_be32(NFSERR_OLD_STATEID)
#define nfserr_bad_stateid	cpu_to_be32(NFSERR_BAD_STATEID)
#define nfserr_bad_seqid	cpu_to_be32(NFSERR_BAD_SEQID)
#define	nfserr_symlink		cpu_to_be32(NFSERR_SYMLINK)
#define	nfserr_not_same		cpu_to_be32(NFSERR_NOT_SAME)
#define nfserr_lock_range	cpu_to_be32(NFSERR_LOCK_RANGE)
#define	nfserr_restorefh	cpu_to_be32(NFSERR_RESTOREFH)
#define	nfserr_attrnotsupp	cpu_to_be32(NFSERR_ATTRNOTSUPP)
#define	nfserr_bad_xdr		cpu_to_be32(NFSERR_BAD_XDR)
#define	nfserr_openmode		cpu_to_be32(NFSERR_OPENMODE)
#define	nfserr_badowner		cpu_to_be32(NFSERR_BADOWNER)
#define	nfserr_locks_held	cpu_to_be32(NFSERR_LOCKS_HELD)
#define	nfserr_op_illegal	cpu_to_be32(NFSERR_OP_ILLEGAL)
#define	nfserr_grace		cpu_to_be32(NFSERR_GRACE)
#define	nfserr_no_grace		cpu_to_be32(NFSERR_NO_GRACE)
#define	nfserr_reclaim_bad	cpu_to_be32(NFSERR_RECLAIM_BAD)
#define	nfserr_badname		cpu_to_be32(NFSERR_BADNAME)
#define	nfserr_cb_path_down	cpu_to_be32(NFSERR_CB_PATH_DOWN)
#define	nfserr_locked		cpu_to_be32(NFSERR_LOCKED)
#define	nfserr_wrongsec		cpu_to_be32(NFSERR_WRONGSEC)
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

/* error codes for internal use */
/* if a request fails due to kmalloc failure, it gets dropped.
 *  Client should resend eventually
 */
#define	nfserr_dropit		cpu_to_be32(30000)
/* end-of-file indicator in readdir */
#define	nfserr_eof		cpu_to_be32(30001)
/* replay detected */
#define	nfserr_replay_me	cpu_to_be32(11001)
/* nfs41 replay detected */
#define	nfserr_replay_cache	cpu_to_be32(11002)

/* Check for dir entries '.' and '..' */
#define isdotent(n, l)	(l < 3 && n[0] == '.' && (l == 1 || n[1] == '.'))

#ifdef CONFIG_NFSD_V4

/* before processing a COMPOUND operation, we have to check that there
 * is enough space in the buffer for XDR encode to succeed.  otherwise,
 * we might process an operation with side effects, and be unable to
 * tell the client that the operation succeeded.
 *
 * COMPOUND_SLACK_SPACE - this is the minimum bytes of buffer space
 * needed to encode an "ordinary" _successful_ operation.  (GETATTR,
 * READ, READDIR, and READLINK have their own buffer checks.)  if we
 * fall below this level, we fail the next operation with NFS4ERR_RESOURCE.
 *
 * COMPOUND_ERR_SLACK_SPACE - this is the minimum bytes of buffer space
 * needed to encode an operation which has failed with NFS4ERR_RESOURCE.
 * care is taken to ensure that we never fall below this level for any
 * reason.
 */
#define	COMPOUND_SLACK_SPACE		140    /* OP_GETFH */
#define COMPOUND_ERR_SLACK_SPACE	16     /* OP_SETATTR */

#define NFSD_LAUNDROMAT_MINTIMEOUT      1   /* seconds */
#define	NFSD_COURTESY_CLIENT_TIMEOUT	(24 * 60 * 60)	/* seconds */
#define	NFSD_CLIENT_MAX_TRIM_PER_RUN	128
#define	NFS4_CLIENTS_PER_GB		1024

/*
 * The following attributes are currently not supported by the NFSv4 server:
 *    ARCHIVE       (deprecated anyway)
 *    HIDDEN        (unlikely to be supported any time soon)
 *    MIMETYPE      (unlikely to be supported any time soon)
 *    QUOTA_*       (will be supported in a forthcoming patch)
 *    SYSTEM        (unlikely to be supported any time soon)
 *    TIME_BACKUP   (unlikely to be supported any time soon)
 *    TIME_CREATE   (unlikely to be supported any time soon)
 */
#define NFSD4_SUPPORTED_ATTRS_WORD0                                                         \
(FATTR4_WORD0_SUPPORTED_ATTRS   | FATTR4_WORD0_TYPE         | FATTR4_WORD0_FH_EXPIRE_TYPE   \
 | FATTR4_WORD0_CHANGE          | FATTR4_WORD0_SIZE         | FATTR4_WORD0_LINK_SUPPORT     \
 | FATTR4_WORD0_SYMLINK_SUPPORT | FATTR4_WORD0_NAMED_ATTR   | FATTR4_WORD0_FSID             \
 | FATTR4_WORD0_UNIQUE_HANDLES  | FATTR4_WORD0_LEASE_TIME   | FATTR4_WORD0_RDATTR_ERROR     \
 | FATTR4_WORD0_ACLSUPPORT      | FATTR4_WORD0_CANSETTIME   | FATTR4_WORD0_CASE_INSENSITIVE \
 | FATTR4_WORD0_CASE_PRESERVING | FATTR4_WORD0_CHOWN_RESTRICTED                             \
 | FATTR4_WORD0_FILEHANDLE      | FATTR4_WORD0_FILEID       | FATTR4_WORD0_FILES_AVAIL      \
 | FATTR4_WORD0_FILES_FREE      | FATTR4_WORD0_FILES_TOTAL  | FATTR4_WORD0_FS_LOCATIONS | FATTR4_WORD0_HOMOGENEOUS      \
 | FATTR4_WORD0_MAXFILESIZE     | FATTR4_WORD0_MAXLINK      | FATTR4_WORD0_MAXNAME          \
 | FATTR4_WORD0_MAXREAD         | FATTR4_WORD0_MAXWRITE     | FATTR4_WORD0_ACL)

#define NFSD4_SUPPORTED_ATTRS_WORD1                                                         \
(FATTR4_WORD1_MODE              | FATTR4_WORD1_NO_TRUNC     | FATTR4_WORD1_NUMLINKS         \
 | FATTR4_WORD1_OWNER	        | FATTR4_WORD1_OWNER_GROUP  | FATTR4_WORD1_RAWDEV           \
 | FATTR4_WORD1_SPACE_AVAIL     | FATTR4_WORD1_SPACE_FREE   | FATTR4_WORD1_SPACE_TOTAL      \
 | FATTR4_WORD1_SPACE_USED      | FATTR4_WORD1_TIME_ACCESS  | FATTR4_WORD1_TIME_ACCESS_SET  \
 | FATTR4_WORD1_TIME_DELTA      | FATTR4_WORD1_TIME_METADATA   | FATTR4_WORD1_TIME_CREATE      \
 | FATTR4_WORD1_TIME_MODIFY     | FATTR4_WORD1_TIME_MODIFY_SET | FATTR4_WORD1_MOUNTED_ON_FILEID)

#define NFSD4_SUPPORTED_ATTRS_WORD2 0

/* 4.1 */
#ifdef CONFIG_NFSD_PNFS
#define PNFSD_SUPPORTED_ATTRS_WORD1	FATTR4_WORD1_FS_LAYOUT_TYPES
#define PNFSD_SUPPORTED_ATTRS_WORD2 \
(FATTR4_WORD2_LAYOUT_BLKSIZE	| FATTR4_WORD2_LAYOUT_TYPES)
#else
#define PNFSD_SUPPORTED_ATTRS_WORD1	0
#define PNFSD_SUPPORTED_ATTRS_WORD2	0
#endif /* CONFIG_NFSD_PNFS */

#define NFSD4_1_SUPPORTED_ATTRS_WORD0 \
	NFSD4_SUPPORTED_ATTRS_WORD0

#define NFSD4_1_SUPPORTED_ATTRS_WORD1 \
	(NFSD4_SUPPORTED_ATTRS_WORD1	| PNFSD_SUPPORTED_ATTRS_WORD1)

#define NFSD4_1_SUPPORTED_ATTRS_WORD2 \
	(NFSD4_SUPPORTED_ATTRS_WORD2	| PNFSD_SUPPORTED_ATTRS_WORD2 | \
	 FATTR4_WORD2_SUPPATTR_EXCLCREAT)

/* 4.2 */
#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
#define NFSD4_2_SECURITY_ATTRS		FATTR4_WORD2_SECURITY_LABEL
#else
#define NFSD4_2_SECURITY_ATTRS		0
#endif

#define NFSD4_2_SUPPORTED_ATTRS_WORD2 \
	(NFSD4_1_SUPPORTED_ATTRS_WORD2 | \
	FATTR4_WORD2_MODE_UMASK | \
	NFSD4_2_SECURITY_ATTRS | \
	FATTR4_WORD2_XATTR_SUPPORT)

extern const u32 nfsd_suppattrs[3][3];

static inline __be32 nfsd4_set_netaddr(struct sockaddr *addr,
				    struct nfs42_netaddr *netaddr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
	unsigned int port;
	size_t ret_addr, ret_port;

	switch (addr->sa_family) {
	case AF_INET:
		port = ntohs(sin->sin_port);
		sprintf(netaddr->netid, "tcp");
		netaddr->netid_len = 3;
		break;
	case AF_INET6:
		port = ntohs(sin6->sin6_port);
		sprintf(netaddr->netid, "tcp6");
		netaddr->netid_len = 4;
		break;
	default:
		return nfserr_inval;
	}
	ret_addr = rpc_ntop(addr, netaddr->addr, sizeof(netaddr->addr));
	ret_port = snprintf(netaddr->addr + ret_addr,
			    RPCBIND_MAXUADDRLEN + 1 - ret_addr,
			    ".%u.%u", port >> 8, port & 0xff);
	WARN_ON(ret_port >= RPCBIND_MAXUADDRLEN + 1 - ret_addr);
	netaddr->addr_len = ret_addr + ret_port;
	return 0;
}

static inline bool bmval_is_subset(const u32 *bm1, const u32 *bm2)
{
	return !((bm1[0] & ~bm2[0]) ||
	         (bm1[1] & ~bm2[1]) ||
		 (bm1[2] & ~bm2[2]));
}

static inline bool nfsd_attrs_supported(u32 minorversion, const u32 *bmval)
{
	return bmval_is_subset(bmval, nfsd_suppattrs[minorversion]);
}

/* These will return ERR_INVAL if specified in GETATTR or READDIR. */
#define NFSD_WRITEONLY_ATTRS_WORD1 \
	(FATTR4_WORD1_TIME_ACCESS_SET   | FATTR4_WORD1_TIME_MODIFY_SET)

/*
 * These are the only attrs allowed in CREATE/OPEN/SETATTR. Don't add
 * a writeable attribute here without also adding code to parse it to
 * nfsd4_decode_fattr().
 */
#define NFSD_WRITEABLE_ATTRS_WORD0 \
	(FATTR4_WORD0_SIZE | FATTR4_WORD0_ACL)
#define NFSD_WRITEABLE_ATTRS_WORD1 \
	(FATTR4_WORD1_MODE | FATTR4_WORD1_OWNER | FATTR4_WORD1_OWNER_GROUP \
	| FATTR4_WORD1_TIME_ACCESS_SET | FATTR4_WORD1_TIME_CREATE \
	| FATTR4_WORD1_TIME_MODIFY_SET)
#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
#define MAYBE_FATTR4_WORD2_SECURITY_LABEL \
	FATTR4_WORD2_SECURITY_LABEL
#else
#define MAYBE_FATTR4_WORD2_SECURITY_LABEL 0
#endif
#define NFSD_WRITEABLE_ATTRS_WORD2 \
	(FATTR4_WORD2_MODE_UMASK \
	| MAYBE_FATTR4_WORD2_SECURITY_LABEL)

#define NFSD_SUPPATTR_EXCLCREAT_WORD0 \
	NFSD_WRITEABLE_ATTRS_WORD0
/*
 * we currently store the exclusive create verifier in the v_{a,m}time
 * attributes so the client can't set these at create time using EXCLUSIVE4_1
 */
#define NFSD_SUPPATTR_EXCLCREAT_WORD1 \
	(NFSD_WRITEABLE_ATTRS_WORD1 & \
	 ~(FATTR4_WORD1_TIME_ACCESS_SET | FATTR4_WORD1_TIME_MODIFY_SET))
#define NFSD_SUPPATTR_EXCLCREAT_WORD2 \
	NFSD_WRITEABLE_ATTRS_WORD2

extern int nfsd4_is_junction(struct dentry *dentry);
extern int register_cld_notifier(void);
extern void unregister_cld_notifier(void);
#ifdef CONFIG_NFSD_V4_2_INTER_SSC
extern void nfsd4_ssc_init_umount_work(struct nfsd_net *nn);
#endif

extern void nfsd4_init_leases_net(struct nfsd_net *nn);

#else /* CONFIG_NFSD_V4 */
static inline int nfsd4_is_junction(struct dentry *dentry)
{
	return 0;
}

static inline void nfsd4_init_leases_net(struct nfsd_net *nn) {};

#define register_cld_notifier() 0
#define unregister_cld_notifier() do { } while(0)

#endif /* CONFIG_NFSD_V4 */

#endif /* LINUX_NFSD_NFSD_H */
