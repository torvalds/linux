/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef LINUX_NFSD_VFS_H
#define LINUX_NFSD_VFS_H

#include <linux/fs.h>
#include <linux/posix_acl.h>
#include "nfsfh.h"

/*
 * Flags for nfsd_permission
 */
#define NFSD_MAY_NOP			0
#define NFSD_MAY_EXEC			0x001 /* == MAY_EXEC */
#define NFSD_MAY_WRITE			0x002 /* == MAY_WRITE */
#define NFSD_MAY_READ			0x004 /* == MAY_READ */
#define NFSD_MAY_SATTR			0x008
#define NFSD_MAY_TRUNC			0x010
#define NFSD_MAY_NLM			0x020 /* request is from lockd */
#define NFSD_MAY_MASK			0x03f

/* extra hints to permission and open routines: */
#define NFSD_MAY_OWNER_OVERRIDE		0x040
#define NFSD_MAY_LOCAL_ACCESS		0x080 /* for device special files */
#define NFSD_MAY_BYPASS_GSS_ON_ROOT	0x100
#define NFSD_MAY_NOT_BREAK_LEASE	0x200
#define NFSD_MAY_BYPASS_GSS		0x400
#define NFSD_MAY_READ_IF_EXEC		0x800

#define NFSD_MAY_64BIT_COOKIE		0x1000 /* 64 bit readdir cookies for >= NFSv3 */

#define NFSD_MAY_LOCALIO		0x2000 /* for tracing, reflects when localio used */

#define NFSD_MAY_CREATE		(NFSD_MAY_EXEC|NFSD_MAY_WRITE)
#define NFSD_MAY_REMOVE		(NFSD_MAY_EXEC|NFSD_MAY_WRITE|NFSD_MAY_TRUNC)

struct nfsd_access_map {
	u32		access;
	int		may;
};

struct nfsd_access_maps {
	const struct nfsd_access_map	*regular;
	const struct nfsd_access_map	*directory;
	const struct nfsd_access_map	*other;
};

struct nfsd_file;

/*
 * Callback function for readdir
 */
typedef int (*nfsd_filldir_t)(void *, const char *, int, loff_t, u64, unsigned);

struct readdir_cd {
	__be32			err;	/* nfs_ok, nfserr, or nfserr_eof */
};

/*
 * One directory entry buffered out of the underlying filesystem.
 * Buffering decouples the ->iterate_shared() call from the consumer,
 * which may call ->lookup() and so must not run inside iterate_dir().
 */
struct buffered_dirent {
	u64			ino;
	loff_t			offset;
	int			namlen;
	unsigned int		d_type;
	char			name[];
};

/*
 * Streaming directory reader.  nfsd_readdir_open() opens the directory
 * and seeks to the start cookie, nfsd_readdir_next() yields one entry
 * at a time (refilling from the filesystem as needed), and
 * nfsd_readdir_close() releases it.  The reader holds the open file, so
 * a consumer can pull entries while a reply is being encoded.
 */
struct nfsd_readdir_iter {
	struct dir_context	ctx;		/* drives iterate_dir() */
	struct file		*file;		/* open directory */
	struct svc_fh		*fhp;		/* directory file handle */
	char			*page;		/* one batch of raw entries */
	size_t			used;		/* bytes filled this batch */
	int			full;		/* batch buffer filled up */
	char			*pos;		/* next entry to yield */
	int			remaining;	/* bytes left from @pos */
	loff_t			offset;		/* resume cookie */
	int			host_err;	/* set once iterate_dir() fails */
	bool			batched;	/* a batch has been read */
	bool			eof;		/* directory exhausted */
};

/* nfsd/vfs.c */
struct nfsd_attrs {
	struct iattr		*na_iattr;	/* input */
	struct xdr_netobj	*na_seclabel;	/* input */
	struct posix_acl	*na_pacl;	/* input */
	struct posix_acl	*na_dpacl;	/* input */

	int			na_labelerr;	/* output */
	int			na_dpaclerr;	/* output */
	int			na_paclerr;	/* output */
};

static inline void nfsd_attrs_free(struct nfsd_attrs *attrs)
{
	posix_acl_release(attrs->na_pacl);
	posix_acl_release(attrs->na_dpacl);
}

static inline bool nfsd_attrs_valid(struct nfsd_attrs *attrs)
{
	struct iattr *iap = attrs->na_iattr;

	return (iap->ia_valid || (attrs->na_seclabel &&
		attrs->na_seclabel->len) ||
		attrs->na_pacl || attrs->na_dpacl);
}

struct nfsd_net;

__be32		nfserrno (int errno);
void		nfsd_maybe_reset_write_verifier(struct nfsd_net *nn,
						struct svc_rqst *rqstp,
						int err);
__be32		nfsd_cross_mnt(struct svc_rqst *rqstp, struct dentry **dpp,
			       struct svc_export **expp);
__be32		nfsd_lookup(struct svc_rqst *, struct svc_fh *,
				const char *, unsigned int, struct svc_fh *);
__be32		 nfsd_lookup_dentry(struct svc_rqst *, struct svc_fh *,
				const char *, unsigned int,
				struct svc_export **, struct dentry **);
__be32		nfsd_setattr(struct svc_rqst *, struct svc_fh *,
			     struct nfsd_attrs *, const struct timespec64 *);
int nfsd_mountpoint(struct dentry *, struct svc_export *);
#ifdef CONFIG_NFSD_V4
__be32		nfsd4_vfs_fallocate(struct svc_rqst *, struct svc_fh *,
				    struct file *, loff_t, loff_t, int);
int		nfsd_clone_file_range(struct file *src, u64 src_pos,
				      struct file *dst, u64 dst_pos,
				      u64 count, errseq_t *since);
int		nfsd_clone_sync_range(struct file *src, struct file *dst,
				      u64 dst_pos, u64 count, errseq_t since);
#endif /* CONFIG_NFSD_V4 */
__be32		nfsd_create_locked(struct svc_rqst *, struct svc_fh *,
				struct nfsd_attrs *attrs, int type, dev_t rdev,
				struct svc_fh *res);
__be32		nfsd_create(struct svc_rqst *, struct svc_fh *,
				char *name, int len, struct nfsd_attrs *attrs,
				int type, dev_t rdev, struct svc_fh *res);
__be32		nfsd_access(struct svc_rqst *rqstp, struct svc_fh *fhp,
				const struct nfsd_access_maps *maps,
				u32 *access, u32 *supported);
__be32		nfsd_create_setattr(struct svc_rqst *rqstp, struct svc_fh *fhp,
				struct svc_fh *resfhp, struct nfsd_attrs *iap);
__be32		nfsd_commit(struct svc_rqst *rqst, struct svc_fh *fhp,
				struct nfsd_file *nf, u64 offset, u32 count,
				__be32 *verf);
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
int		nfsd_open_verified(struct svc_fh *fhp, umode_t type, int may_flags,
				struct file **filp);
__be32		nfsd_splice_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
				struct file *file, loff_t offset,
				unsigned long *count,
				u32 *eof);
__be32		nfsd_iter_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
				struct nfsd_file *nf, loff_t offset,
				unsigned long *count, unsigned int base,
				u32 *eof);
bool		nfsd_read_splice_ok(struct svc_rqst *rqstp);
__be32		nfsd_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
				loff_t offset, unsigned long *count,
				u32 *eof);
__be32		nfsd_write(struct svc_rqst *rqstp, struct svc_fh *fhp,
				loff_t offset, const struct xdr_buf *payload,
				unsigned long *cnt, int iocb_flags,
				__be32 *verf);
__be32		nfsd_vfs_write(struct svc_rqst *rqstp, struct svc_fh *fhp,
				struct nfsd_file *nf, loff_t offset,
				const struct xdr_buf *payload,
				unsigned long *cnt, int iocb_flags,
				__be32 *verf);
__be32		nfsd_readlink(struct svc_rqst *, struct svc_fh *,
				char *, int *);
__be32		nfsd_symlink(struct svc_rqst *, struct svc_fh *,
			     char *name, int len, char *path,
			     struct nfsd_attrs *attrs,
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
__be32		nfsd_readdir_open(struct svc_rqst *rqstp, struct svc_fh *fhp,
				  loff_t *offsetp,
				  struct nfsd_readdir_iter *iter);
struct buffered_dirent *nfsd_readdir_next(struct nfsd_readdir_iter *iter);
void		nfsd_readdir_close(struct nfsd_readdir_iter *iter);
__be32		nfsd_statfs(struct svc_rqst *, struct svc_fh *,
				struct kstatfs *, int access);
int		nfsd_get_case_info(struct dentry *dentry,
				   bool *case_insensitive,
				   bool *case_preserving);

__be32		nfsd_permission(struct svc_cred *cred, struct svc_export *exp,
				struct dentry *dentry, int acc);

void		nfsd_filp_close(struct file *fp);

#endif /* LINUX_NFSD_VFS_H */
