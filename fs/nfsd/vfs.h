/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_VFS_H
#define LINUX_NFSD_VFS_H

#include "nfsfh.h"
#include "nfsd.h"

/*
 * Flags for nfsd_permission
 */
#define NFSD_MAY_NOP			0
#define NFSD_MAY_EXEC			0x001 /* == MAY_EXEC */
#define NFSD_MAY_WRITE			0x002 /* == MAY_WRITE */
#define NFSD_MAY_READ			0x004 /* == MAY_READ */
#define NFSD_MAY_SATTR			0x008
#define NFSD_MAY_TRUNC			0x010
#define NFSD_MAY_LOCK			0x020
#define NFSD_MAY_MASK			0x03f

/* extra hints to permission and open routines: */
#define NFSD_MAY_OWNER_OVERRIDE		0x040
#define NFSD_MAY_LOCAL_ACCESS		0x080 /* for device special files */
#define NFSD_MAY_BYPASS_GSS_ON_ROOT	0x100
#define NFSD_MAY_NOT_BREAK_LEASE	0x200
#define NFSD_MAY_BYPASS_GSS		0x400
#define NFSD_MAY_READ_IF_EXEC		0x800

#define NFSD_MAY_64BIT_COOKIE		0x1000 /* 64 bit readdir cookies for >= NFSv3 */

#define NFSD_MAY_CREATE		(NFSD_MAY_EXEC|NFSD_MAY_WRITE)
#define NFSD_MAY_REMOVE		(NFSD_MAY_EXEC|NFSD_MAY_WRITE|NFSD_MAY_TRUNC)

struct nfsd_file;

/*
 * Callback function for readdir
 */
typedef int (*nfsd_filldir_t)(void *, const char *, int, loff_t, u64, unsigned);

/* nfsd/vfs.c */
int		nfsd_cross_mnt(struct svc_rqst *rqstp, struct dentry **dpp,
		                struct svc_export **expp);
__be32		nfsd_lookup(struct svc_rqst *, struct svc_fh *,
				const char *, unsigned int, struct svc_fh *);
__be32		 nfsd_lookup_dentry(struct svc_rqst *, struct svc_fh *,
				const char *, unsigned int,
				struct svc_export **, struct dentry **);
__be32		nfsd_setattr(struct svc_rqst *, struct svc_fh *,
				struct iattr *, int, time64_t);
int nfsd_mountpoint(struct dentry *, struct svc_export *);
#ifdef CONFIG_NFSD_V4
__be32          nfsd4_set_nfs4_label(struct svc_rqst *, struct svc_fh *,
		    struct xdr_netobj *);
__be32		nfsd4_vfs_fallocate(struct svc_rqst *, struct svc_fh *,
				    struct file *, loff_t, loff_t, int);
__be32		nfsd4_clone_file_range(struct nfsd_file *nf_src, u64 src_pos,
				       struct nfsd_file *nf_dst, u64 dst_pos,
				       u64 count, bool sync);
#endif /* CONFIG_NFSD_V4 */
__be32		nfsd_create_locked(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				int type, dev_t rdev, struct svc_fh *res);
__be32		nfsd_create(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				int type, dev_t rdev, struct svc_fh *res);
#ifdef CONFIG_NFSD_V3
__be32		nfsd_access(struct svc_rqst *, struct svc_fh *, u32 *, u32 *);
__be32		do_nfsd_create(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct iattr *attrs,
				struct svc_fh *res, int createmode,
				u32 *verifier, bool *truncp, bool *created);
__be32		nfsd_commit(struct svc_rqst *, struct svc_fh *,
				loff_t, unsigned long, __be32 *verf);
#endif /* CONFIG_NFSD_V3 */
#ifdef CONFIG_NFSD_V4
__be32		nfsd_getxattr(struct svc_rqst *rqstp, struct svc_fh *fhp,
			    char *name, void **bufp, int *lenp);
__be32		nfsd_listxattr(struct svc_rqst *rqstp, struct svc_fh *fhp,
			    char **bufp, int *lenp);
__be32		nfsd_removexattr(struct svc_rqst *rqstp, struct svc_fh *fhp,
			    char *name);
__be32		nfsd_setxattr(struct svc_rqst *rqstp, struct svc_fh *fhp,
			    char *name, void *buf, u32 len, u32 flags);
#endif
int 		nfsd_open_break_lease(struct inode *, int);
__be32		nfsd_open(struct svc_rqst *, struct svc_fh *, umode_t,
				int, struct file **);
__be32		nfsd_open_verified(struct svc_rqst *, struct svc_fh *, umode_t,
				int, struct file **);
__be32		nfsd_splice_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
				struct file *file, loff_t offset,
				unsigned long *count,
				u32 *eof);
__be32		nfsd_readv(struct svc_rqst *rqstp, struct svc_fh *fhp,
				struct file *file, loff_t offset,
				struct kvec *vec, int vlen,
				unsigned long *count,
				u32 *eof);
__be32 		nfsd_read(struct svc_rqst *, struct svc_fh *,
				loff_t, struct kvec *, int, unsigned long *,
				u32 *eof);
__be32 		nfsd_write(struct svc_rqst *, struct svc_fh *, loff_t,
				struct kvec *, int, unsigned long *,
				int stable, __be32 *verf);
__be32		nfsd_vfs_write(struct svc_rqst *rqstp, struct svc_fh *fhp,
				struct nfsd_file *nf, loff_t offset,
				struct kvec *vec, int vlen, unsigned long *cnt,
				int stable, __be32 *verf);
__be32		nfsd_readlink(struct svc_rqst *, struct svc_fh *,
				char *, int *);
__be32		nfsd_symlink(struct svc_rqst *, struct svc_fh *,
				char *name, int len, char *path,
				struct svc_fh *res);
__be32		nfsd_link(struct svc_rqst *, struct svc_fh *,
				char *, int, struct svc_fh *);
ssize_t		nfsd_copy_file_range(struct file *, u64,
				     struct file *, u64, u64);
__be32		nfsd_rename(struct svc_rqst *,
				struct svc_fh *, char *, int,
				struct svc_fh *, char *, int);
__be32		nfsd_unlink(struct svc_rqst *, struct svc_fh *, int type,
				char *name, int len);
__be32		nfsd_readdir(struct svc_rqst *, struct svc_fh *,
			     loff_t *, struct readdir_cd *, nfsd_filldir_t);
__be32		nfsd_statfs(struct svc_rqst *, struct svc_fh *,
				struct kstatfs *, int access);

__be32		nfsd_permission(struct svc_rqst *, struct svc_export *,
				struct dentry *, int);

static inline int fh_want_write(struct svc_fh *fh)
{
	int ret;

	if (fh->fh_want_write)
		return 0;
	ret = mnt_want_write(fh->fh_export->ex_path.mnt);
	if (!ret)
		fh->fh_want_write = true;
	return ret;
}

static inline void fh_drop_write(struct svc_fh *fh)
{
	if (fh->fh_want_write) {
		fh->fh_want_write = false;
		mnt_drop_write(fh->fh_export->ex_path.mnt);
	}
}

static inline __be32 fh_getattr(struct svc_fh *fh, struct kstat *stat)
{
	struct path p = {.mnt = fh->fh_export->ex_path.mnt,
			 .dentry = fh->fh_dentry};
	return nfserrno(vfs_getattr(&p, stat, STATX_BASIC_STATS,
				    AT_STATX_SYNC_AS_STAT));
}

static inline int nfsd_create_is_exclusive(int createmode)
{
	return createmode == NFS3_CREATE_EXCLUSIVE
	       || createmode == NFS4_CREATE_EXCLUSIVE4_1;
}

#endif /* LINUX_NFSD_VFS_H */
