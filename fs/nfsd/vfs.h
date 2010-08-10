/*
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_VFS_H
#define LINUX_NFSD_VFS_H

#include "nfsfh.h"

/*
 * Flags for nfsd_permission
 */
#define NFSD_MAY_NOP		0
#define NFSD_MAY_EXEC		1 /* == MAY_EXEC */
#define NFSD_MAY_WRITE		2 /* == MAY_WRITE */
#define NFSD_MAY_READ		4 /* == MAY_READ */
#define NFSD_MAY_SATTR		8
#define NFSD_MAY_TRUNC		16
#define NFSD_MAY_LOCK		32
#define NFSD_MAY_OWNER_OVERRIDE	64
#define NFSD_MAY_LOCAL_ACCESS	128 /* IRIX doing local access check on device special file*/
#define NFSD_MAY_BYPASS_GSS_ON_ROOT 256
#define NFSD_MAY_NOT_BREAK_LEASE 512

#define NFSD_MAY_CREATE		(NFSD_MAY_EXEC|NFSD_MAY_WRITE)
#define NFSD_MAY_REMOVE		(NFSD_MAY_EXEC|NFSD_MAY_WRITE|NFSD_MAY_TRUNC)

/*
 * Callback function for readdir
 */
typedef int (*nfsd_dirop_t)(struct inode *, struct dentry *, int, int);

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
int nfsd_mountpoint(struct dentry *, struct svc_export *);
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
__be32 		nfsd_read(struct svc_rqst *, struct svc_fh *,
				loff_t, struct kvec *, int, unsigned long *);
__be32 		nfsd_read_file(struct svc_rqst *, struct svc_fh *, struct file *,
				loff_t, struct kvec *, int, unsigned long *);
__be32 		nfsd_write(struct svc_rqst *, struct svc_fh *,struct file *,
				loff_t, struct kvec *,int, unsigned long *, int *);
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
				struct kstatfs *, int access);

int		nfsd_notify_change(struct inode *, struct iattr *);
__be32		nfsd_permission(struct svc_rqst *, struct svc_export *,
				struct dentry *, int);
int		nfsd_sync_dir(struct dentry *dp);

#if defined(CONFIG_NFSD_V2_ACL) || defined(CONFIG_NFSD_V3_ACL)
struct posix_acl *nfsd_get_posix_acl(struct svc_fh *, int);
int nfsd_set_posix_acl(struct svc_fh *, int, struct posix_acl *);
#endif

#endif /* LINUX_NFSD_VFS_H */
