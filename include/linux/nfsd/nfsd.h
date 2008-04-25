/*
 * linux/include/linux/nfsd/nfsd.h
 *
 * Hodge-podge collection of knfsd-related stuff.
 * I will sort this out later.
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_NFSD_H
#define LINUX_NFSD_NFSD_H

#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/dirent.h>
#include <linux/fs.h>
#include <linux/posix_acl.h>
#include <linux/mount.h>

#include <linux/nfsd/debug.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/export.h>
#include <linux/nfsd/stats.h>
/*
 * nfsd version
 */
#define NFSD_VERSION		"0.5"
#define NFSD_SUPPORTED_MINOR_VERSION	0

/*
 * Special flags for nfsd_permission. These must be different from MAY_READ,
 * MAY_WRITE, and MAY_EXEC.
 */
#define MAY_NOP			0
#define MAY_SATTR		8
#define MAY_TRUNC		16
#define MAY_LOCK		32
#define MAY_OWNER_OVERRIDE	64
#define	MAY_LOCAL_ACCESS	128 /* IRIX doing local access check on device special file*/
#if (MAY_SATTR | MAY_TRUNC | MAY_LOCK | MAY_OWNER_OVERRIDE | MAY_LOCAL_ACCESS) & (MAY_READ | MAY_WRITE | MAY_EXEC)
# error "please use a different value for MAY_SATTR or MAY_TRUNC or MAY_LOCK or MAY_LOCAL_ACCESS or MAY_OWNER_OVERRIDE."
#endif
#define MAY_CREATE		(MAY_EXEC|MAY_WRITE)
#define MAY_REMOVE		(MAY_EXEC|MAY_WRITE|MAY_TRUNC)

/*
 * Callback function for readdir
 */
struct readdir_cd {
	__be32			err;	/* 0, nfserr, or nfserr_eof */
};
typedef int (*nfsd_dirop_t)(struct inode *, struct dentry *, int, int);

extern struct svc_program	nfsd_program;
extern struct svc_version	nfsd_version2, nfsd_version3,
				nfsd_version4;
extern struct svc_serv		*nfsd_serv;

extern struct seq_operations nfs_exports_op;

/*
 * Function prototypes.
 */
int		nfsd_svc(unsigned short port, int nrservs);
int		nfsd_dispatch(struct svc_rqst *rqstp, __be32 *statp);

int		nfsd_nrthreads(void);
int		nfsd_nrpools(void);
int		nfsd_get_nrthreads(int n, int *);
int		nfsd_set_nrthreads(int n, int *);

/* nfsd/vfs.c */
int		fh_lock_parent(struct svc_fh *, struct dentry *);
int		nfsd_racache_init(int);
void		nfsd_racache_shutdown(void);
int		nfsd_cross_mnt(struct svc_rqst *rqstp, struct dentry **dpp,
		                struct svc_export **expp);
__be32		nfsd_lookup(struct svc_rqst *, struct svc_fh *,
				const char *, unsigned int, struct svc_fh *);
__be32		 nfsd_lookup_dentry(struct svc_rqst *, struct svc_fh *,
				const char *, unsigned int,
				struct svc_export **, struct dentry **);
__be32		nfsd_setattr(struct svc_rqst *, struct svc_fh *,
				struct iattr *, int, time_t);
#ifdef CONFIG_NFSD_V4
__be32          nfsd4_set_nfs4_acl(struct svc_rqst *, struct svc_fh *,
                    struct nfs4_acl *);
int             nfsd4_get_nfs4_acl(struct svc_rqst *, struct dentry *, struct nfs4_acl **);
#endif /* CONFIG_NFSD_V4 */
__be32		nfsd_create(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				int type, dev_t rdev, struct svc_fh *res);
#ifdef CONFIG_NFSD_V3
__be32		nfsd_access(struct svc_rqst *, struct svc_fh *, u32 *, u32 *);
__be32		nfsd_create_v3(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				struct svc_fh *res, int createmode,
				u32 *verifier, int *truncp, int *created);
__be32		nfsd_commit(struct svc_rqst *, struct svc_fh *,
				loff_t, unsigned long);
#endif /* CONFIG_NFSD_V3 */
__be32		nfsd_open(struct svc_rqst *, struct svc_fh *, int,
				int, struct file **);
void		nfsd_close(struct file *);
__be32 		nfsd_read(struct svc_rqst *, struct svc_fh *, struct file *,
				loff_t, struct kvec *, int, unsigned long *);
__be32 		nfsd_write(struct svc_rqst *, struct svc_fh *,struct file *,
				loff_t, struct kvec *,int, unsigned long, int *);
__be32		nfsd_readlink(struct svc_rqst *, struct svc_fh *,
				char *, int *);
__be32		nfsd_symlink(struct svc_rqst *, struct svc_fh *,
				char *name, int len, char *path, int plen,
				struct svc_fh *res, struct iattr *);
__be32		nfsd_link(struct svc_rqst *, struct svc_fh *,
				char *, int, struct svc_fh *);
__be32		nfsd_rename(struct svc_rqst *,
				struct svc_fh *, char *, int,
				struct svc_fh *, char *, int);
__be32		nfsd_remove(struct svc_rqst *,
				struct svc_fh *, char *, int);
__be32		nfsd_unlink(struct svc_rqst *, struct svc_fh *, int type,
				char *name, int len);
int		nfsd_truncate(struct svc_rqst *, struct svc_fh *,
				unsigned long size);
__be32		nfsd_readdir(struct svc_rqst *, struct svc_fh *,
			     loff_t *, struct readdir_cd *, filldir_t);
__be32		nfsd_statfs(struct svc_rqst *, struct svc_fh *,
				struct kstatfs *);

int		nfsd_notify_change(struct inode *, struct iattr *);
__be32		nfsd_permission(struct svc_rqst *, struct svc_export *,
				struct dentry *, int);
int		nfsd_sync_dir(struct dentry *dp);

#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
#ifdef CONFIG_NFSD_V2_ACL
extern struct svc_version nfsd_acl_version2;
#else
#define nfsd_acl_version2 NULL
#endif
#ifdef CONFIG_NFSD_V3_ACL
extern struct svc_version nfsd_acl_version3;
#else
#define nfsd_acl_version3 NULL
#endif
struct posix_acl *nfsd_get_posix_acl(struct svc_fh *, int);
int nfsd_set_posix_acl(struct svc_fh *, int, struct posix_acl *);
#endif

enum vers_op {NFSD_SET, NFSD_CLEAR, NFSD_TEST, NFSD_AVAIL };
int nfsd_vers(int vers, enum vers_op change);
void nfsd_reset_versions(void);
int nfsd_create_serv(void);

extern int nfsd_max_blksize;

/* 
 * NFSv4 State
 */
#ifdef CONFIG_NFSD_V4
extern unsigned int max_delegations;
int nfs4_state_init(void);
void nfsd4_free_slabs(void);
void nfs4_state_start(void);
void nfs4_state_shutdown(void);
time_t nfs4_lease_time(void);
void nfs4_reset_lease(time_t leasetime);
int nfs4_reset_recoverydir(char *recdir);
#else
static inline int nfs4_state_init(void) { return 0; }
static inline void nfsd4_free_slabs(void) { }
static inline void nfs4_state_start(void) { }
static inline void nfs4_state_shutdown(void) { }
static inline time_t nfs4_lease_time(void) { return 0; }
static inline void nfs4_reset_lease(time_t leasetime) { }
static inline int nfs4_reset_recoverydir(char *recdir) { return 0; }
#endif

/*
 * lockd binding
 */
void		nfsd_lockd_init(void);
void		nfsd_lockd_shutdown(void);


/*
 * These macros provide pre-xdr'ed values for faster operation.
 */
#define	nfs_ok			__constant_htonl(NFS_OK)
#define	nfserr_perm		__constant_htonl(NFSERR_PERM)
#define	nfserr_noent		__constant_htonl(NFSERR_NOENT)
#define	nfserr_io		__constant_htonl(NFSERR_IO)
#define	nfserr_nxio		__constant_htonl(NFSERR_NXIO)
#define	nfserr_eagain		__constant_htonl(NFSERR_EAGAIN)
#define	nfserr_acces		__constant_htonl(NFSERR_ACCES)
#define	nfserr_exist		__constant_htonl(NFSERR_EXIST)
#define	nfserr_xdev		__constant_htonl(NFSERR_XDEV)
#define	nfserr_nodev		__constant_htonl(NFSERR_NODEV)
#define	nfserr_notdir		__constant_htonl(NFSERR_NOTDIR)
#define	nfserr_isdir		__constant_htonl(NFSERR_ISDIR)
#define	nfserr_inval		__constant_htonl(NFSERR_INVAL)
#define	nfserr_fbig		__constant_htonl(NFSERR_FBIG)
#define	nfserr_nospc		__constant_htonl(NFSERR_NOSPC)
#define	nfserr_rofs		__constant_htonl(NFSERR_ROFS)
#define	nfserr_mlink		__constant_htonl(NFSERR_MLINK)
#define	nfserr_opnotsupp	__constant_htonl(NFSERR_OPNOTSUPP)
#define	nfserr_nametoolong	__constant_htonl(NFSERR_NAMETOOLONG)
#define	nfserr_notempty		__constant_htonl(NFSERR_NOTEMPTY)
#define	nfserr_dquot		__constant_htonl(NFSERR_DQUOT)
#define	nfserr_stale		__constant_htonl(NFSERR_STALE)
#define	nfserr_remote		__constant_htonl(NFSERR_REMOTE)
#define	nfserr_wflush		__constant_htonl(NFSERR_WFLUSH)
#define	nfserr_badhandle	__constant_htonl(NFSERR_BADHANDLE)
#define	nfserr_notsync		__constant_htonl(NFSERR_NOT_SYNC)
#define	nfserr_badcookie	__constant_htonl(NFSERR_BAD_COOKIE)
#define	nfserr_notsupp		__constant_htonl(NFSERR_NOTSUPP)
#define	nfserr_toosmall		__constant_htonl(NFSERR_TOOSMALL)
#define	nfserr_serverfault	__constant_htonl(NFSERR_SERVERFAULT)
#define	nfserr_badtype		__constant_htonl(NFSERR_BADTYPE)
#define	nfserr_jukebox		__constant_htonl(NFSERR_JUKEBOX)
#define	nfserr_denied		__constant_htonl(NFSERR_DENIED)
#define	nfserr_deadlock		__constant_htonl(NFSERR_DEADLOCK)
#define nfserr_expired          __constant_htonl(NFSERR_EXPIRED)
#define	nfserr_bad_cookie	__constant_htonl(NFSERR_BAD_COOKIE)
#define	nfserr_same		__constant_htonl(NFSERR_SAME)
#define	nfserr_clid_inuse	__constant_htonl(NFSERR_CLID_INUSE)
#define	nfserr_stale_clientid	__constant_htonl(NFSERR_STALE_CLIENTID)
#define	nfserr_resource		__constant_htonl(NFSERR_RESOURCE)
#define	nfserr_moved		__constant_htonl(NFSERR_MOVED)
#define	nfserr_nofilehandle	__constant_htonl(NFSERR_NOFILEHANDLE)
#define	nfserr_minor_vers_mismatch	__constant_htonl(NFSERR_MINOR_VERS_MISMATCH)
#define nfserr_share_denied	__constant_htonl(NFSERR_SHARE_DENIED)
#define nfserr_stale_stateid	__constant_htonl(NFSERR_STALE_STATEID)
#define nfserr_old_stateid	__constant_htonl(NFSERR_OLD_STATEID)
#define nfserr_bad_stateid	__constant_htonl(NFSERR_BAD_STATEID)
#define nfserr_bad_seqid	__constant_htonl(NFSERR_BAD_SEQID)
#define	nfserr_symlink		__constant_htonl(NFSERR_SYMLINK)
#define	nfserr_not_same		__constant_htonl(NFSERR_NOT_SAME)
#define	nfserr_restorefh	__constant_htonl(NFSERR_RESTOREFH)
#define	nfserr_attrnotsupp	__constant_htonl(NFSERR_ATTRNOTSUPP)
#define	nfserr_bad_xdr		__constant_htonl(NFSERR_BAD_XDR)
#define	nfserr_openmode		__constant_htonl(NFSERR_OPENMODE)
#define	nfserr_locks_held	__constant_htonl(NFSERR_LOCKS_HELD)
#define	nfserr_op_illegal	__constant_htonl(NFSERR_OP_ILLEGAL)
#define	nfserr_grace		__constant_htonl(NFSERR_GRACE)
#define	nfserr_no_grace		__constant_htonl(NFSERR_NO_GRACE)
#define	nfserr_reclaim_bad	__constant_htonl(NFSERR_RECLAIM_BAD)
#define	nfserr_badname		__constant_htonl(NFSERR_BADNAME)
#define	nfserr_cb_path_down	__constant_htonl(NFSERR_CB_PATH_DOWN)
#define	nfserr_locked		__constant_htonl(NFSERR_LOCKED)
#define	nfserr_wrongsec		__constant_htonl(NFSERR_WRONGSEC)
#define	nfserr_replay_me	__constant_htonl(NFSERR_REPLAY_ME)

/* error codes for internal use */
/* if a request fails due to kmalloc failure, it gets dropped.
 *  Client should resend eventually
 */
#define	nfserr_dropit		__constant_htonl(30000)
/* end-of-file indicator in readdir */
#define	nfserr_eof		__constant_htonl(30001)

/* Check for dir entries '.' and '..' */
#define isdotent(n, l)	(l < 3 && n[0] == '.' && (l == 1 || n[1] == '.'))

/*
 * Time of server startup
 */
extern struct timeval	nfssvc_boot;

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
#define COMPOUND_ERR_SLACK_SPACE	12     /* OP_SETATTR */

#define NFSD_LEASE_TIME                 (nfs4_lease_time())
#define NFSD_LAUNDROMAT_MINTIMEOUT      10   /* seconds */

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
#define NFSD_SUPPORTED_ATTRS_WORD0                                                          \
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

#define NFSD_SUPPORTED_ATTRS_WORD1                                                          \
(FATTR4_WORD1_MODE              | FATTR4_WORD1_NO_TRUNC     | FATTR4_WORD1_NUMLINKS         \
 | FATTR4_WORD1_OWNER	        | FATTR4_WORD1_OWNER_GROUP  | FATTR4_WORD1_RAWDEV           \
 | FATTR4_WORD1_SPACE_AVAIL     | FATTR4_WORD1_SPACE_FREE   | FATTR4_WORD1_SPACE_TOTAL      \
 | FATTR4_WORD1_SPACE_USED      | FATTR4_WORD1_TIME_ACCESS  | FATTR4_WORD1_TIME_ACCESS_SET  \
 | FATTR4_WORD1_TIME_DELTA   | FATTR4_WORD1_TIME_METADATA    \
 | FATTR4_WORD1_TIME_MODIFY     | FATTR4_WORD1_TIME_MODIFY_SET | FATTR4_WORD1_MOUNTED_ON_FILEID)

/* These will return ERR_INVAL if specified in GETATTR or READDIR. */
#define NFSD_WRITEONLY_ATTRS_WORD1							    \
(FATTR4_WORD1_TIME_ACCESS_SET   | FATTR4_WORD1_TIME_MODIFY_SET)

/* These are the only attrs allowed in CREATE/OPEN/SETATTR. */
#define NFSD_WRITEABLE_ATTRS_WORD0                                                          \
(FATTR4_WORD0_SIZE              | FATTR4_WORD0_ACL                                         )
#define NFSD_WRITEABLE_ATTRS_WORD1                                                          \
(FATTR4_WORD1_MODE              | FATTR4_WORD1_OWNER         | FATTR4_WORD1_OWNER_GROUP     \
 | FATTR4_WORD1_TIME_ACCESS_SET | FATTR4_WORD1_TIME_MODIFY_SET)

#endif /* CONFIG_NFSD_V4 */

#endif /* LINUX_NFSD_NFSD_H */
