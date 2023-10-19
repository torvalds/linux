// SPDX-License-Identifier: GPL-2.0
/*
 * File operations used by nfsd. Some of these have been ripped from
 * other parts of the kernel because they weren't exported, others
 * are partial duplicates with added or changed functionality.
 *
 * Note that several functions dget() the dentry upon which they want
 * to act, most notably those that create directory entries. Response
 * dentry's are dput()'d if necessary in the release callback.
 * So if you notice code paths that apparently fail to dput() the
 * dentry, don't worry--they have been taken care of.
 *
 * Copyright (C) 1995-1999 Olaf Kirch <okir@monad.swb.de>
 * Zerocpy NFS support (C) 2002 Hirokazu Takahashi <taka@valinux.co.jp>
 */

#include <linux/fs.h>
#include <linux/file.h>
#include <linux/splice.h>
#include <linux/falloc.h>
#include <linux/fcntl.h>
#include <linux/namei.h>
#include <linux/delay.h>
#include <linux/fsnotify.h>
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>
#include <linux/jhash.h>
#include <linux/ima.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/exportfs.h>
#include <linux/writeback.h>
#include <linux/security.h>

#include "xdr3.h"

#ifdef CONFIG_NFSD_V4
#include "../internal.h"
#include "acl.h"
#include "idmap.h"
#include "xdr4.h"
#endif /* CONFIG_NFSD_V4 */

#include "nfsd.h"
#include "vfs.h"
#include "filecache.h"
#include "trace.h"

#define NFSDDBG_FACILITY		NFSDDBG_FILEOP

/* 
 * Called from nfsd_lookup and encode_dirent. Check if we have crossed 
 * a mount point.
 * Returns -EAGAIN or -ETIMEDOUT leaving *dpp and *expp unchanged,
 *  or nfs_ok having possibly changed *dpp and *expp
 */
int
nfsd_cross_mnt(struct svc_rqst *rqstp, struct dentry **dpp, 
		        struct svc_export **expp)
{
	struct svc_export *exp = *expp, *exp2 = NULL;
	struct dentry *dentry = *dpp;
	struct path path = {.mnt = mntget(exp->ex_path.mnt),
			    .dentry = dget(dentry)};
	int err = 0;

	err = follow_down(&path);
	if (err < 0)
		goto out;
	if (path.mnt == exp->ex_path.mnt && path.dentry == dentry &&
	    nfsd_mountpoint(dentry, exp) == 2) {
		/* This is only a mountpoint in some other namespace */
		path_put(&path);
		goto out;
	}

	exp2 = rqst_exp_get_by_name(rqstp, &path);
	if (IS_ERR(exp2)) {
		err = PTR_ERR(exp2);
		/*
		 * We normally allow NFS clients to continue
		 * "underneath" a mountpoint that is not exported.
		 * The exception is V4ROOT, where no traversal is ever
		 * allowed without an explicit export of the new
		 * directory.
		 */
		if (err == -ENOENT && !(exp->ex_flags & NFSEXP_V4ROOT))
			err = 0;
		path_put(&path);
		goto out;
	}
	if (nfsd_v4client(rqstp) ||
		(exp->ex_flags & NFSEXP_CROSSMOUNT) || EX_NOHIDE(exp2)) {
		/* successfully crossed mount point */
		/*
		 * This is subtle: path.dentry is *not* on path.mnt
		 * at this point.  The only reason we are safe is that
		 * original mnt is pinned down by exp, so we should
		 * put path *before* putting exp
		 */
		*dpp = path.dentry;
		path.dentry = dentry;
		*expp = exp2;
		exp2 = exp;
	}
	path_put(&path);
	exp_put(exp2);
out:
	return err;
}

static void follow_to_parent(struct path *path)
{
	struct dentry *dp;

	while (path->dentry == path->mnt->mnt_root && follow_up(path))
		;
	dp = dget_parent(path->dentry);
	dput(path->dentry);
	path->dentry = dp;
}

static int nfsd_lookup_parent(struct svc_rqst *rqstp, struct dentry *dparent, struct svc_export **exp, struct dentry **dentryp)
{
	struct svc_export *exp2;
	struct path path = {.mnt = mntget((*exp)->ex_path.mnt),
			    .dentry = dget(dparent)};

	follow_to_parent(&path);

	exp2 = rqst_exp_parent(rqstp, &path);
	if (PTR_ERR(exp2) == -ENOENT) {
		*dentryp = dget(dparent);
	} else if (IS_ERR(exp2)) {
		path_put(&path);
		return PTR_ERR(exp2);
	} else {
		*dentryp = dget(path.dentry);
		exp_put(*exp);
		*exp = exp2;
	}
	path_put(&path);
	return 0;
}

/*
 * For nfsd purposes, we treat V4ROOT exports as though there was an
 * export at *every* directory.
 * We return:
 * '1' if this dentry *must* be an export point,
 * '2' if it might be, if there is really a mount here, and
 * '0' if there is no chance of an export point here.
 */
int nfsd_mountpoint(struct dentry *dentry, struct svc_export *exp)
{
	if (!d_inode(dentry))
		return 0;
	if (exp->ex_flags & NFSEXP_V4ROOT)
		return 1;
	if (nfsd4_is_junction(dentry))
		return 1;
	if (d_mountpoint(dentry))
		/*
		 * Might only be a mountpoint in a different namespace,
		 * but we need to check.
		 */
		return 2;
	return 0;
}

__be32
nfsd_lookup_dentry(struct svc_rqst *rqstp, struct svc_fh *fhp,
		   const char *name, unsigned int len,
		   struct svc_export **exp_ret, struct dentry **dentry_ret)
{
	struct svc_export	*exp;
	struct dentry		*dparent;
	struct dentry		*dentry;
	int			host_err;

	dprintk("nfsd: nfsd_lookup(fh %s, %.*s)\n", SVCFH_fmt(fhp), len,name);

	dparent = fhp->fh_dentry;
	exp = exp_get(fhp->fh_export);

	/* Lookup the name, but don't follow links */
	if (isdotent(name, len)) {
		if (len==1)
			dentry = dget(dparent);
		else if (dparent != exp->ex_path.dentry)
			dentry = dget_parent(dparent);
		else if (!EX_NOHIDE(exp) && !nfsd_v4client(rqstp))
			dentry = dget(dparent); /* .. == . just like at / */
		else {
			/* checking mountpoint crossing is very different when stepping up */
			host_err = nfsd_lookup_parent(rqstp, dparent, &exp, &dentry);
			if (host_err)
				goto out_nfserr;
		}
	} else {
		dentry = lookup_one_len_unlocked(name, dparent, len);
		host_err = PTR_ERR(dentry);
		if (IS_ERR(dentry))
			goto out_nfserr;
		if (nfsd_mountpoint(dentry, exp)) {
			host_err = nfsd_cross_mnt(rqstp, &dentry, &exp);
			if (host_err) {
				dput(dentry);
				goto out_nfserr;
			}
		}
	}
	*dentry_ret = dentry;
	*exp_ret = exp;
	return 0;

out_nfserr:
	exp_put(exp);
	return nfserrno(host_err);
}

/**
 * nfsd_lookup - look up a single path component for nfsd
 *
 * @rqstp:   the request context
 * @fhp:     the file handle of the directory
 * @name:    the component name, or %NULL to look up parent
 * @len:     length of name to examine
 * @resfh:   pointer to pre-initialised filehandle to hold result.
 *
 * Look up one component of a pathname.
 * N.B. After this call _both_ fhp and resfh need an fh_put
 *
 * If the lookup would cross a mountpoint, and the mounted filesystem
 * is exported to the client with NFSEXP_NOHIDE, then the lookup is
 * accepted as it stands and the mounted directory is
 * returned. Otherwise the covered directory is returned.
 * NOTE: this mountpoint crossing is not supported properly by all
 *   clients and is explicitly disallowed for NFSv3
 *
 */
__be32
nfsd_lookup(struct svc_rqst *rqstp, struct svc_fh *fhp, const char *name,
	    unsigned int len, struct svc_fh *resfh)
{
	struct svc_export	*exp;
	struct dentry		*dentry;
	__be32 err;

	err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_EXEC);
	if (err)
		return err;
	err = nfsd_lookup_dentry(rqstp, fhp, name, len, &exp, &dentry);
	if (err)
		return err;
	err = check_nfsd_access(exp, rqstp);
	if (err)
		goto out;
	/*
	 * Note: we compose the file handle now, but as the
	 * dentry may be negative, it may need to be updated.
	 */
	err = fh_compose(resfh, exp, dentry, fhp);
	if (!err && d_really_is_negative(dentry))
		err = nfserr_noent;
out:
	dput(dentry);
	exp_put(exp);
	return err;
}

/*
 * Commit metadata changes to stable storage.
 */
static int
commit_inode_metadata(struct inode *inode)
{
	const struct export_operations *export_ops = inode->i_sb->s_export_op;

	if (export_ops->commit_metadata)
		return export_ops->commit_metadata(inode);
	return sync_inode_metadata(inode, 1);
}

static int
commit_metadata(struct svc_fh *fhp)
{
	struct inode *inode = d_inode(fhp->fh_dentry);

	if (!EX_ISSYNC(fhp->fh_export))
		return 0;
	return commit_inode_metadata(inode);
}

/*
 * Go over the attributes and take care of the small differences between
 * NFS semantics and what Linux expects.
 */
static void
nfsd_sanitize_attrs(struct inode *inode, struct iattr *iap)
{
	/* Ignore mode updates on symlinks */
	if (S_ISLNK(inode->i_mode))
		iap->ia_valid &= ~ATTR_MODE;

	/* sanitize the mode change */
	if (iap->ia_valid & ATTR_MODE) {
		iap->ia_mode &= S_IALLUGO;
		iap->ia_mode |= (inode->i_mode & ~S_IALLUGO);
	}

	/* Revoke setuid/setgid on chown */
	if (!S_ISDIR(inode->i_mode) &&
	    ((iap->ia_valid & ATTR_UID) || (iap->ia_valid & ATTR_GID))) {
		iap->ia_valid |= ATTR_KILL_PRIV;
		if (iap->ia_valid & ATTR_MODE) {
			/* we're setting mode too, just clear the s*id bits */
			iap->ia_mode &= ~S_ISUID;
			if (iap->ia_mode & S_IXGRP)
				iap->ia_mode &= ~S_ISGID;
		} else {
			/* set ATTR_KILL_* bits and let VFS handle it */
			iap->ia_valid |= (ATTR_KILL_SUID | ATTR_KILL_SGID);
		}
	}
}

static __be32
nfsd_get_write_access(struct svc_rqst *rqstp, struct svc_fh *fhp,
		struct iattr *iap)
{
	struct inode *inode = d_inode(fhp->fh_dentry);

	if (iap->ia_size < inode->i_size) {
		__be32 err;

		err = nfsd_permission(rqstp, fhp->fh_export, fhp->fh_dentry,
				NFSD_MAY_TRUNC | NFSD_MAY_OWNER_OVERRIDE);
		if (err)
			return err;
	}
	return nfserrno(get_write_access(inode));
}

static int __nfsd_setattr(struct dentry *dentry, struct iattr *iap)
{
	int host_err;

	if (iap->ia_valid & ATTR_SIZE) {
		/*
		 * RFC5661, Section 18.30.4:
		 *   Changing the size of a file with SETATTR indirectly
		 *   changes the time_modify and change attributes.
		 *
		 * (and similar for the older RFCs)
		 */
		struct iattr size_attr = {
			.ia_valid	= ATTR_SIZE | ATTR_CTIME | ATTR_MTIME,
			.ia_size	= iap->ia_size,
		};

		if (iap->ia_size < 0)
			return -EFBIG;

		host_err = notify_change(&init_user_ns, dentry, &size_attr, NULL);
		if (host_err)
			return host_err;
		iap->ia_valid &= ~ATTR_SIZE;

		/*
		 * Avoid the additional setattr call below if the only other
		 * attribute that the client sends is the mtime, as we update
		 * it as part of the size change above.
		 */
		if ((iap->ia_valid & ~ATTR_MTIME) == 0)
			return 0;
	}

	if (!iap->ia_valid)
		return 0;

	iap->ia_valid |= ATTR_CTIME;
	return notify_change(&init_user_ns, dentry, iap, NULL);
}

/**
 * nfsd_setattr - Set various file attributes.
 * @rqstp: controlling RPC transaction
 * @fhp: filehandle of target
 * @attr: attributes to set
 * @check_guard: set to 1 if guardtime is a valid timestamp
 * @guardtime: do not act if ctime.tv_sec does not match this timestamp
 *
 * This call may adjust the contents of @attr (in particular, this
 * call may change the bits in the na_iattr.ia_valid field).
 *
 * Returns nfs_ok on success, otherwise an NFS status code is
 * returned. Caller must release @fhp by calling fh_put in either
 * case.
 */
__be32
nfsd_setattr(struct svc_rqst *rqstp, struct svc_fh *fhp,
	     struct nfsd_attrs *attr,
	     int check_guard, time64_t guardtime)
{
	struct dentry	*dentry;
	struct inode	*inode;
	struct iattr	*iap = attr->na_iattr;
	int		accmode = NFSD_MAY_SATTR;
	umode_t		ftype = 0;
	__be32		err;
	int		host_err;
	bool		get_write_count;
	bool		size_change = (iap->ia_valid & ATTR_SIZE);
	int		retries;

	if (iap->ia_valid & ATTR_SIZE) {
		accmode |= NFSD_MAY_WRITE|NFSD_MAY_OWNER_OVERRIDE;
		ftype = S_IFREG;
	}

	/*
	 * If utimes(2) and friends are called with times not NULL, we should
	 * not set NFSD_MAY_WRITE bit. Otherwise fh_verify->nfsd_permission
	 * will return EACCES, when the caller's effective UID does not match
	 * the owner of the file, and the caller is not privileged. In this
	 * situation, we should return EPERM(notify_change will return this).
	 */
	if (iap->ia_valid & (ATTR_ATIME | ATTR_MTIME)) {
		accmode |= NFSD_MAY_OWNER_OVERRIDE;
		if (!(iap->ia_valid & (ATTR_ATIME_SET | ATTR_MTIME_SET)))
			accmode |= NFSD_MAY_WRITE;
	}

	/* Callers that do fh_verify should do the fh_want_write: */
	get_write_count = !fhp->fh_dentry;

	/* Get inode */
	err = fh_verify(rqstp, fhp, ftype, accmode);
	if (err)
		return err;
	if (get_write_count) {
		host_err = fh_want_write(fhp);
		if (host_err)
			goto out;
	}

	dentry = fhp->fh_dentry;
	inode = d_inode(dentry);

	nfsd_sanitize_attrs(inode, iap);

	if (check_guard && guardtime != inode->i_ctime.tv_sec)
		return nfserr_notsync;

	/*
	 * The size case is special, it changes the file in addition to the
	 * attributes, and file systems don't expect it to be mixed with
	 * "random" attribute changes.  We thus split out the size change
	 * into a separate call to ->setattr, and do the rest as a separate
	 * setattr call.
	 */
	if (size_change) {
		err = nfsd_get_write_access(rqstp, fhp, iap);
		if (err)
			return err;
	}

	inode_lock(inode);
	for (retries = 1;;) {
		host_err = __nfsd_setattr(dentry, iap);
		if (host_err != -EAGAIN || !retries--)
			break;
		if (!nfsd_wait_for_delegreturn(rqstp, inode))
			break;
	}
	if (attr->na_seclabel && attr->na_seclabel->len)
		attr->na_labelerr = security_inode_setsecctx(dentry,
			attr->na_seclabel->data, attr->na_seclabel->len);
	if (IS_ENABLED(CONFIG_FS_POSIX_ACL) && attr->na_pacl)
		attr->na_aclerr = set_posix_acl(&init_user_ns,
						inode, ACL_TYPE_ACCESS,
						attr->na_pacl);
	if (IS_ENABLED(CONFIG_FS_POSIX_ACL) &&
	    !attr->na_aclerr && attr->na_dpacl && S_ISDIR(inode->i_mode))
		attr->na_aclerr = set_posix_acl(&init_user_ns,
						inode, ACL_TYPE_DEFAULT,
						attr->na_dpacl);
	inode_unlock(inode);
	if (size_change)
		put_write_access(inode);
out:
	if (!host_err)
		host_err = commit_metadata(fhp);
	return nfserrno(host_err);
}

#if defined(CONFIG_NFSD_V4)
/*
 * NFS junction information is stored in an extended attribute.
 */
#define NFSD_JUNCTION_XATTR_NAME	XATTR_TRUSTED_PREFIX "junction.nfs"

/**
 * nfsd4_is_junction - Test if an object could be an NFS junction
 *
 * @dentry: object to test
 *
 * Returns 1 if "dentry" appears to contain NFS junction information.
 * Otherwise 0 is returned.
 */
int nfsd4_is_junction(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (inode == NULL)
		return 0;
	if (inode->i_mode & S_IXUGO)
		return 0;
	if (!(inode->i_mode & S_ISVTX))
		return 0;
	if (vfs_getxattr(&init_user_ns, dentry, NFSD_JUNCTION_XATTR_NAME,
			 NULL, 0) <= 0)
		return 0;
	return 1;
}

static struct nfsd4_compound_state *nfsd4_get_cstate(struct svc_rqst *rqstp)
{
	return &((struct nfsd4_compoundres *)rqstp->rq_resp)->cstate;
}

__be32 nfsd4_clone_file_range(struct svc_rqst *rqstp,
		struct nfsd_file *nf_src, u64 src_pos,
		struct nfsd_file *nf_dst, u64 dst_pos,
		u64 count, bool sync)
{
	struct file *src = nf_src->nf_file;
	struct file *dst = nf_dst->nf_file;
	errseq_t since;
	loff_t cloned;
	__be32 ret = 0;

	since = READ_ONCE(dst->f_wb_err);
	cloned = vfs_clone_file_range(src, src_pos, dst, dst_pos, count, 0);
	if (cloned < 0) {
		ret = nfserrno(cloned);
		goto out_err;
	}
	if (count && cloned != count) {
		ret = nfserrno(-EINVAL);
		goto out_err;
	}
	if (sync) {
		loff_t dst_end = count ? dst_pos + count - 1 : LLONG_MAX;
		int status = vfs_fsync_range(dst, dst_pos, dst_end, 0);

		if (!status)
			status = filemap_check_wb_err(dst->f_mapping, since);
		if (!status)
			status = commit_inode_metadata(file_inode(src));
		if (status < 0) {
			struct nfsd_net *nn = net_generic(nf_dst->nf_net,
							  nfsd_net_id);

			trace_nfsd_clone_file_range_err(rqstp,
					&nfsd4_get_cstate(rqstp)->save_fh,
					src_pos,
					&nfsd4_get_cstate(rqstp)->current_fh,
					dst_pos,
					count, status);
			nfsd_reset_write_verifier(nn);
			trace_nfsd_writeverf_reset(nn, rqstp, status);
			ret = nfserrno(status);
		}
	}
out_err:
	return ret;
}

ssize_t nfsd_copy_file_range(struct file *src, u64 src_pos, struct file *dst,
			     u64 dst_pos, u64 count)
{
	ssize_t ret;

	/*
	 * Limit copy to 4MB to prevent indefinitely blocking an nfsd
	 * thread and client rpc slot.  The choice of 4MB is somewhat
	 * arbitrary.  We might instead base this on r/wsize, or make it
	 * tunable, or use a time instead of a byte limit, or implement
	 * asynchronous copy.  In theory a client could also recognize a
	 * limit like this and pipeline multiple COPY requests.
	 */
	count = min_t(u64, count, 1 << 22);
	ret = vfs_copy_file_range(src, src_pos, dst, dst_pos, count, 0);

	if (ret == -EOPNOTSUPP || ret == -EXDEV)
		ret = vfs_copy_file_range(src, src_pos, dst, dst_pos, count,
					  COPY_FILE_SPLICE);
	return ret;
}

__be32 nfsd4_vfs_fallocate(struct svc_rqst *rqstp, struct svc_fh *fhp,
			   struct file *file, loff_t offset, loff_t len,
			   int flags)
{
	int error;

	if (!S_ISREG(file_inode(file)->i_mode))
		return nfserr_inval;

	error = vfs_fallocate(file, flags, offset, len);
	if (!error)
		error = commit_metadata(fhp);

	return nfserrno(error);
}
#endif /* defined(CONFIG_NFSD_V4) */

/*
 * Check server access rights to a file system object
 */
struct accessmap {
	u32		access;
	int		how;
};
static struct accessmap	nfs3_regaccess[] = {
    {	NFS3_ACCESS_READ,	NFSD_MAY_READ			},
    {	NFS3_ACCESS_EXECUTE,	NFSD_MAY_EXEC			},
    {	NFS3_ACCESS_MODIFY,	NFSD_MAY_WRITE|NFSD_MAY_TRUNC	},
    {	NFS3_ACCESS_EXTEND,	NFSD_MAY_WRITE			},

#ifdef CONFIG_NFSD_V4
    {	NFS4_ACCESS_XAREAD,	NFSD_MAY_READ			},
    {	NFS4_ACCESS_XAWRITE,	NFSD_MAY_WRITE			},
    {	NFS4_ACCESS_XALIST,	NFSD_MAY_READ			},
#endif

    {	0,			0				}
};

static struct accessmap	nfs3_diraccess[] = {
    {	NFS3_ACCESS_READ,	NFSD_MAY_READ			},
    {	NFS3_ACCESS_LOOKUP,	NFSD_MAY_EXEC			},
    {	NFS3_ACCESS_MODIFY,	NFSD_MAY_EXEC|NFSD_MAY_WRITE|NFSD_MAY_TRUNC},
    {	NFS3_ACCESS_EXTEND,	NFSD_MAY_EXEC|NFSD_MAY_WRITE	},
    {	NFS3_ACCESS_DELETE,	NFSD_MAY_REMOVE			},

#ifdef CONFIG_NFSD_V4
    {	NFS4_ACCESS_XAREAD,	NFSD_MAY_READ			},
    {	NFS4_ACCESS_XAWRITE,	NFSD_MAY_WRITE			},
    {	NFS4_ACCESS_XALIST,	NFSD_MAY_READ			},
#endif

    {	0,			0				}
};

static struct accessmap	nfs3_anyaccess[] = {
	/* Some clients - Solaris 2.6 at least, make an access call
	 * to the server to check for access for things like /dev/null
	 * (which really, the server doesn't care about).  So
	 * We provide simple access checking for them, looking
	 * mainly at mode bits, and we make sure to ignore read-only
	 * filesystem checks
	 */
    {	NFS3_ACCESS_READ,	NFSD_MAY_READ			},
    {	NFS3_ACCESS_EXECUTE,	NFSD_MAY_EXEC			},
    {	NFS3_ACCESS_MODIFY,	NFSD_MAY_WRITE|NFSD_MAY_LOCAL_ACCESS	},
    {	NFS3_ACCESS_EXTEND,	NFSD_MAY_WRITE|NFSD_MAY_LOCAL_ACCESS	},

    {	0,			0				}
};

__be32
nfsd_access(struct svc_rqst *rqstp, struct svc_fh *fhp, u32 *access, u32 *supported)
{
	struct accessmap	*map;
	struct svc_export	*export;
	struct dentry		*dentry;
	u32			query, result = 0, sresult = 0;
	__be32			error;

	error = fh_verify(rqstp, fhp, 0, NFSD_MAY_NOP);
	if (error)
		goto out;

	export = fhp->fh_export;
	dentry = fhp->fh_dentry;

	if (d_is_reg(dentry))
		map = nfs3_regaccess;
	else if (d_is_dir(dentry))
		map = nfs3_diraccess;
	else
		map = nfs3_anyaccess;


	query = *access;
	for  (; map->access; map++) {
		if (map->access & query) {
			__be32 err2;

			sresult |= map->access;

			err2 = nfsd_permission(rqstp, export, dentry, map->how);
			switch (err2) {
			case nfs_ok:
				result |= map->access;
				break;
				
			/* the following error codes just mean the access was not allowed,
			 * rather than an error occurred */
			case nfserr_rofs:
			case nfserr_acces:
			case nfserr_perm:
				/* simply don't "or" in the access bit. */
				break;
			default:
				error = err2;
				goto out;
			}
		}
	}
	*access = result;
	if (supported)
		*supported = sresult;

 out:
	return error;
}

int nfsd_open_break_lease(struct inode *inode, int access)
{
	unsigned int mode;

	if (access & NFSD_MAY_NOT_BREAK_LEASE)
		return 0;
	mode = (access & NFSD_MAY_WRITE) ? O_WRONLY : O_RDONLY;
	return break_lease(inode, mode | O_NONBLOCK);
}

/*
 * Open an existing file or directory.
 * The may_flags argument indicates the type of open (read/write/lock)
 * and additional flags.
 * N.B. After this call fhp needs an fh_put
 */
static __be32
__nfsd_open(struct svc_rqst *rqstp, struct svc_fh *fhp, umode_t type,
			int may_flags, struct file **filp)
{
	struct path	path;
	struct inode	*inode;
	struct file	*file;
	int		flags = O_RDONLY|O_LARGEFILE;
	__be32		err;
	int		host_err = 0;

	path.mnt = fhp->fh_export->ex_path.mnt;
	path.dentry = fhp->fh_dentry;
	inode = d_inode(path.dentry);

	err = nfserr_perm;
	if (IS_APPEND(inode) && (may_flags & NFSD_MAY_WRITE))
		goto out;

	if (!inode->i_fop)
		goto out;

	host_err = nfsd_open_break_lease(inode, may_flags);
	if (host_err) /* NOMEM or WOULDBLOCK */
		goto out_nfserr;

	if (may_flags & NFSD_MAY_WRITE) {
		if (may_flags & NFSD_MAY_READ)
			flags = O_RDWR|O_LARGEFILE;
		else
			flags = O_WRONLY|O_LARGEFILE;
	}

	file = dentry_open(&path, flags, current_cred());
	if (IS_ERR(file)) {
		host_err = PTR_ERR(file);
		goto out_nfserr;
	}

	host_err = ima_file_check(file, may_flags);
	if (host_err) {
		fput(file);
		goto out_nfserr;
	}

	if (may_flags & NFSD_MAY_64BIT_COOKIE)
		file->f_mode |= FMODE_64BITHASH;
	else
		file->f_mode |= FMODE_32BITHASH;

	*filp = file;
out_nfserr:
	err = nfserrno(host_err);
out:
	return err;
}

__be32
nfsd_open(struct svc_rqst *rqstp, struct svc_fh *fhp, umode_t type,
		int may_flags, struct file **filp)
{
	__be32 err;
	bool retried = false;

	validate_process_creds();
	/*
	 * If we get here, then the client has already done an "open",
	 * and (hopefully) checked permission - so allow OWNER_OVERRIDE
	 * in case a chmod has now revoked permission.
	 *
	 * Arguably we should also allow the owner override for
	 * directories, but we never have and it doesn't seem to have
	 * caused anyone a problem.  If we were to change this, note
	 * also that our filldir callbacks would need a variant of
	 * lookup_one_len that doesn't check permissions.
	 */
	if (type == S_IFREG)
		may_flags |= NFSD_MAY_OWNER_OVERRIDE;
retry:
	err = fh_verify(rqstp, fhp, type, may_flags);
	if (!err) {
		err = __nfsd_open(rqstp, fhp, type, may_flags, filp);
		if (err == nfserr_stale && !retried) {
			retried = true;
			fh_put(fhp);
			goto retry;
		}
	}
	validate_process_creds();
	return err;
}

/**
 * nfsd_open_verified - Open a regular file for the filecache
 * @rqstp: RPC request
 * @fhp: NFS filehandle of the file to open
 * @may_flags: internal permission flags
 * @filp: OUT: open "struct file *"
 *
 * Returns an nfsstat value in network byte order.
 */
__be32
nfsd_open_verified(struct svc_rqst *rqstp, struct svc_fh *fhp, int may_flags,
		   struct file **filp)
{
	__be32 err;

	validate_process_creds();
	err = __nfsd_open(rqstp, fhp, S_IFREG, may_flags, filp);
	validate_process_creds();
	return err;
}

/*
 * Grab and keep cached pages associated with a file in the svc_rqst
 * so that they can be passed to the network sendmsg/sendpage routines
 * directly. They will be released after the sending has completed.
 */
static int
nfsd_splice_actor(struct pipe_inode_info *pipe, struct pipe_buffer *buf,
		  struct splice_desc *sd)
{
	struct svc_rqst *rqstp = sd->u.data;
	struct page *page = buf->page;	// may be a compound one
	unsigned offset = buf->offset;
	struct page *last_page;

	last_page = page + (offset + sd->len - 1) / PAGE_SIZE;
	for (page += offset / PAGE_SIZE; page <= last_page; page++) {
		/*
		 * Skip page replacement when extending the contents
		 * of the current page.
		 */
		if (page == *(rqstp->rq_next_page - 1))
			continue;
		svc_rqst_replace_page(rqstp, page);
	}
	if (rqstp->rq_res.page_len == 0)	// first call
		rqstp->rq_res.page_base = offset % PAGE_SIZE;
	rqstp->rq_res.page_len += sd->len;
	return sd->len;
}

static int nfsd_direct_splice_actor(struct pipe_inode_info *pipe,
				    struct splice_desc *sd)
{
	return __splice_from_pipe(pipe, sd, nfsd_splice_actor);
}

static u32 nfsd_eof_on_read(struct file *file, loff_t offset, ssize_t len,
		size_t expected)
{
	if (expected != 0 && len == 0)
		return 1;
	if (offset+len >= i_size_read(file_inode(file)))
		return 1;
	return 0;
}

static __be32 nfsd_finish_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
			       struct file *file, loff_t offset,
			       unsigned long *count, u32 *eof, ssize_t host_err)
{
	if (host_err >= 0) {
		nfsd_stats_io_read_add(fhp->fh_export, host_err);
		*eof = nfsd_eof_on_read(file, offset, host_err, *count);
		*count = host_err;
		fsnotify_access(file);
		trace_nfsd_read_io_done(rqstp, fhp, offset, *count);
		return 0;
	} else {
		trace_nfsd_read_err(rqstp, fhp, offset, host_err);
		return nfserrno(host_err);
	}
}

__be32 nfsd_splice_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
			struct file *file, loff_t offset, unsigned long *count,
			u32 *eof)
{
	struct splice_desc sd = {
		.len		= 0,
		.total_len	= *count,
		.pos		= offset,
		.u.data		= rqstp,
	};
	ssize_t host_err;

	trace_nfsd_read_splice(rqstp, fhp, offset, *count);
	rqstp->rq_next_page = rqstp->rq_respages + 1;
	host_err = splice_direct_to_actor(file, &sd, nfsd_direct_splice_actor);
	return nfsd_finish_read(rqstp, fhp, file, offset, count, eof, host_err);
}

__be32 nfsd_readv(struct svc_rqst *rqstp, struct svc_fh *fhp,
		  struct file *file, loff_t offset,
		  struct kvec *vec, int vlen, unsigned long *count,
		  u32 *eof)
{
	struct iov_iter iter;
	loff_t ppos = offset;
	ssize_t host_err;

	trace_nfsd_read_vector(rqstp, fhp, offset, *count);
	iov_iter_kvec(&iter, ITER_DEST, vec, vlen, *count);
	host_err = vfs_iter_read(file, &iter, &ppos, 0);
	return nfsd_finish_read(rqstp, fhp, file, offset, count, eof, host_err);
}

/*
 * Gathered writes: If another process is currently writing to the file,
 * there's a high chance this is another nfsd (triggered by a bulk write
 * from a client's biod). Rather than syncing the file with each write
 * request, we sleep for 10 msec.
 *
 * I don't know if this roughly approximates C. Juszak's idea of
 * gathered writes, but it's a nice and simple solution (IMHO), and it
 * seems to work:-)
 *
 * Note: we do this only in the NFSv2 case, since v3 and higher have a
 * better tool (separate unstable writes and commits) for solving this
 * problem.
 */
static int wait_for_concurrent_writes(struct file *file)
{
	struct inode *inode = file_inode(file);
	static ino_t last_ino;
	static dev_t last_dev;
	int err = 0;

	if (atomic_read(&inode->i_writecount) > 1
	    || (last_ino == inode->i_ino && last_dev == inode->i_sb->s_dev)) {
		dprintk("nfsd: write defer %d\n", task_pid_nr(current));
		msleep(10);
		dprintk("nfsd: write resume %d\n", task_pid_nr(current));
	}

	if (inode->i_state & I_DIRTY) {
		dprintk("nfsd: write sync %d\n", task_pid_nr(current));
		err = vfs_fsync(file, 0);
	}
	last_ino = inode->i_ino;
	last_dev = inode->i_sb->s_dev;
	return err;
}

__be32
nfsd_vfs_write(struct svc_rqst *rqstp, struct svc_fh *fhp, struct nfsd_file *nf,
				loff_t offset, struct kvec *vec, int vlen,
				unsigned long *cnt, int stable,
				__be32 *verf)
{
	struct nfsd_net		*nn = net_generic(SVC_NET(rqstp), nfsd_net_id);
	struct file		*file = nf->nf_file;
	struct super_block	*sb = file_inode(file)->i_sb;
	struct svc_export	*exp;
	struct iov_iter		iter;
	errseq_t		since;
	__be32			nfserr;
	int			host_err;
	int			use_wgather;
	loff_t			pos = offset;
	unsigned long		exp_op_flags = 0;
	unsigned int		pflags = current->flags;
	rwf_t			flags = 0;
	bool			restore_flags = false;

	trace_nfsd_write_opened(rqstp, fhp, offset, *cnt);

	if (sb->s_export_op)
		exp_op_flags = sb->s_export_op->flags;

	if (test_bit(RQ_LOCAL, &rqstp->rq_flags) &&
	    !(exp_op_flags & EXPORT_OP_REMOTE_FS)) {
		/*
		 * We want throttling in balance_dirty_pages()
		 * and shrink_inactive_list() to only consider
		 * the backingdev we are writing to, so that nfs to
		 * localhost doesn't cause nfsd to lock up due to all
		 * the client's dirty pages or its congested queue.
		 */
		current->flags |= PF_LOCAL_THROTTLE;
		restore_flags = true;
	}

	exp = fhp->fh_export;
	use_wgather = (rqstp->rq_vers == 2) && EX_WGATHER(exp);

	if (!EX_ISSYNC(exp))
		stable = NFS_UNSTABLE;

	if (stable && !use_wgather)
		flags |= RWF_SYNC;

	iov_iter_kvec(&iter, ITER_SOURCE, vec, vlen, *cnt);
	since = READ_ONCE(file->f_wb_err);
	if (verf)
		nfsd_copy_write_verifier(verf, nn);
	file_start_write(file);
	host_err = vfs_iter_write(file, &iter, &pos, flags);
	file_end_write(file);
	if (host_err < 0) {
		nfsd_reset_write_verifier(nn);
		trace_nfsd_writeverf_reset(nn, rqstp, host_err);
		goto out_nfserr;
	}
	*cnt = host_err;
	nfsd_stats_io_write_add(exp, *cnt);
	fsnotify_modify(file);
	host_err = filemap_check_wb_err(file->f_mapping, since);
	if (host_err < 0)
		goto out_nfserr;

	if (stable && use_wgather) {
		host_err = wait_for_concurrent_writes(file);
		if (host_err < 0) {
			nfsd_reset_write_verifier(nn);
			trace_nfsd_writeverf_reset(nn, rqstp, host_err);
		}
	}

out_nfserr:
	if (host_err >= 0) {
		trace_nfsd_write_io_done(rqstp, fhp, offset, *cnt);
		nfserr = nfs_ok;
	} else {
		trace_nfsd_write_err(rqstp, fhp, offset, host_err);
		nfserr = nfserrno(host_err);
	}
	if (restore_flags)
		current_restore_flags(pflags, PF_LOCAL_THROTTLE);
	return nfserr;
}

/*
 * Read data from a file. count must contain the requested read count
 * on entry. On return, *count contains the number of bytes actually read.
 * N.B. After this call fhp needs an fh_put
 */
__be32 nfsd_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
	loff_t offset, struct kvec *vec, int vlen, unsigned long *count,
	u32 *eof)
{
	struct nfsd_file	*nf;
	struct file *file;
	__be32 err;

	trace_nfsd_read_start(rqstp, fhp, offset, *count);
	err = nfsd_file_acquire_gc(rqstp, fhp, NFSD_MAY_READ, &nf);
	if (err)
		return err;

	file = nf->nf_file;
	if (file->f_op->splice_read && test_bit(RQ_SPLICE_OK, &rqstp->rq_flags))
		err = nfsd_splice_read(rqstp, fhp, file, offset, count, eof);
	else
		err = nfsd_readv(rqstp, fhp, file, offset, vec, vlen, count, eof);

	nfsd_file_put(nf);

	trace_nfsd_read_done(rqstp, fhp, offset, *count);

	return err;
}

/*
 * Write data to a file.
 * The stable flag requests synchronous writes.
 * N.B. After this call fhp needs an fh_put
 */
__be32
nfsd_write(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t offset,
	   struct kvec *vec, int vlen, unsigned long *cnt, int stable,
	   __be32 *verf)
{
	struct nfsd_file *nf;
	__be32 err;

	trace_nfsd_write_start(rqstp, fhp, offset, *cnt);

	err = nfsd_file_acquire_gc(rqstp, fhp, NFSD_MAY_WRITE, &nf);
	if (err)
		goto out;

	err = nfsd_vfs_write(rqstp, fhp, nf, offset, vec,
			vlen, cnt, stable, verf);
	nfsd_file_put(nf);
out:
	trace_nfsd_write_done(rqstp, fhp, offset, *cnt);
	return err;
}

/**
 * nfsd_commit - Commit pending writes to stable storage
 * @rqstp: RPC request being processed
 * @fhp: NFS filehandle
 * @nf: target file
 * @offset: raw offset from beginning of file
 * @count: raw count of bytes to sync
 * @verf: filled in with the server's current write verifier
 *
 * Note: we guarantee that data that lies within the range specified
 * by the 'offset' and 'count' parameters will be synced. The server
 * is permitted to sync data that lies outside this range at the
 * same time.
 *
 * Unfortunately we cannot lock the file to make sure we return full WCC
 * data to the client, as locking happens lower down in the filesystem.
 *
 * Return values:
 *   An nfsstat value in network byte order.
 */
__be32
nfsd_commit(struct svc_rqst *rqstp, struct svc_fh *fhp, struct nfsd_file *nf,
	    u64 offset, u32 count, __be32 *verf)
{
	__be32			err = nfs_ok;
	u64			maxbytes;
	loff_t			start, end;
	struct nfsd_net		*nn;

	/*
	 * Convert the client-provided (offset, count) range to a
	 * (start, end) range. If the client-provided range falls
	 * outside the maximum file size of the underlying FS,
	 * clamp the sync range appropriately.
	 */
	start = 0;
	end = LLONG_MAX;
	maxbytes = (u64)fhp->fh_dentry->d_sb->s_maxbytes;
	if (offset < maxbytes) {
		start = offset;
		if (count && (offset + count - 1 < maxbytes))
			end = offset + count - 1;
	}

	nn = net_generic(nf->nf_net, nfsd_net_id);
	if (EX_ISSYNC(fhp->fh_export)) {
		errseq_t since = READ_ONCE(nf->nf_file->f_wb_err);
		int err2;

		err2 = vfs_fsync_range(nf->nf_file, start, end, 0);
		switch (err2) {
		case 0:
			nfsd_copy_write_verifier(verf, nn);
			err2 = filemap_check_wb_err(nf->nf_file->f_mapping,
						    since);
			err = nfserrno(err2);
			break;
		case -EINVAL:
			err = nfserr_notsupp;
			break;
		default:
			nfsd_reset_write_verifier(nn);
			trace_nfsd_writeverf_reset(nn, rqstp, err2);
			err = nfserrno(err2);
		}
	} else
		nfsd_copy_write_verifier(verf, nn);

	return err;
}

/**
 * nfsd_create_setattr - Set a created file's attributes
 * @rqstp: RPC transaction being executed
 * @fhp: NFS filehandle of parent directory
 * @resfhp: NFS filehandle of new object
 * @attrs: requested attributes of new object
 *
 * Returns nfs_ok on success, or an nfsstat in network byte order.
 */
__be32
nfsd_create_setattr(struct svc_rqst *rqstp, struct svc_fh *fhp,
		    struct svc_fh *resfhp, struct nfsd_attrs *attrs)
{
	struct iattr *iap = attrs->na_iattr;
	__be32 status;

	/*
	 * Mode has already been set by file creation.
	 */
	iap->ia_valid &= ~ATTR_MODE;

	/*
	 * Setting uid/gid works only for root.  Irix appears to
	 * send along the gid on create when it tries to implement
	 * setgid directories via NFS:
	 */
	if (!uid_eq(current_fsuid(), GLOBAL_ROOT_UID))
		iap->ia_valid &= ~(ATTR_UID|ATTR_GID);

	/*
	 * Callers expect new file metadata to be committed even
	 * if the attributes have not changed.
	 */
	if (iap->ia_valid)
		status = nfsd_setattr(rqstp, resfhp, attrs, 0, (time64_t)0);
	else
		status = nfserrno(commit_metadata(resfhp));

	/*
	 * Transactional filesystems had a chance to commit changes
	 * for both parent and child simultaneously making the
	 * following commit_metadata a noop in many cases.
	 */
	if (!status)
		status = nfserrno(commit_metadata(fhp));

	/*
	 * Update the new filehandle to pick up the new attributes.
	 */
	if (!status)
		status = fh_update(resfhp);

	return status;
}

/* HPUX client sometimes creates a file in mode 000, and sets size to 0.
 * setting size to 0 may fail for some specific file systems by the permission
 * checking which requires WRITE permission but the mode is 000.
 * we ignore the resizing(to 0) on the just new created file, since the size is
 * 0 after file created.
 *
 * call this only after vfs_create() is called.
 * */
static void
nfsd_check_ignore_resizing(struct iattr *iap)
{
	if ((iap->ia_valid & ATTR_SIZE) && (iap->ia_size == 0))
		iap->ia_valid &= ~ATTR_SIZE;
}

/* The parent directory should already be locked: */
__be32
nfsd_create_locked(struct svc_rqst *rqstp, struct svc_fh *fhp,
		   struct nfsd_attrs *attrs,
		   int type, dev_t rdev, struct svc_fh *resfhp)
{
	struct dentry	*dentry, *dchild;
	struct inode	*dirp;
	struct iattr	*iap = attrs->na_iattr;
	__be32		err;
	int		host_err;

	dentry = fhp->fh_dentry;
	dirp = d_inode(dentry);

	dchild = dget(resfhp->fh_dentry);
	err = nfsd_permission(rqstp, fhp->fh_export, dentry, NFSD_MAY_CREATE);
	if (err)
		goto out;

	if (!(iap->ia_valid & ATTR_MODE))
		iap->ia_mode = 0;
	iap->ia_mode = (iap->ia_mode & S_IALLUGO) | type;

	if (!IS_POSIXACL(dirp))
		iap->ia_mode &= ~current_umask();

	err = 0;
	host_err = 0;
	switch (type) {
	case S_IFREG:
		host_err = vfs_create(&init_user_ns, dirp, dchild, iap->ia_mode, true);
		if (!host_err)
			nfsd_check_ignore_resizing(iap);
		break;
	case S_IFDIR:
		host_err = vfs_mkdir(&init_user_ns, dirp, dchild, iap->ia_mode);
		if (!host_err && unlikely(d_unhashed(dchild))) {
			struct dentry *d;
			d = lookup_one_len(dchild->d_name.name,
					   dchild->d_parent,
					   dchild->d_name.len);
			if (IS_ERR(d)) {
				host_err = PTR_ERR(d);
				break;
			}
			if (unlikely(d_is_negative(d))) {
				dput(d);
				err = nfserr_serverfault;
				goto out;
			}
			dput(resfhp->fh_dentry);
			resfhp->fh_dentry = dget(d);
			err = fh_update(resfhp);
			dput(dchild);
			dchild = d;
			if (err)
				goto out;
		}
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		host_err = vfs_mknod(&init_user_ns, dirp, dchild,
				     iap->ia_mode, rdev);
		break;
	default:
		printk(KERN_WARNING "nfsd: bad file type %o in nfsd_create\n",
		       type);
		host_err = -EINVAL;
	}
	if (host_err < 0)
		goto out_nfserr;

	err = nfsd_create_setattr(rqstp, fhp, resfhp, attrs);

out:
	dput(dchild);
	return err;

out_nfserr:
	err = nfserrno(host_err);
	goto out;
}

/*
 * Create a filesystem object (regular, directory, special).
 * Note that the parent directory is left locked.
 *
 * N.B. Every call to nfsd_create needs an fh_put for _both_ fhp and resfhp
 */
__be32
nfsd_create(struct svc_rqst *rqstp, struct svc_fh *fhp,
	    char *fname, int flen, struct nfsd_attrs *attrs,
	    int type, dev_t rdev, struct svc_fh *resfhp)
{
	struct dentry	*dentry, *dchild = NULL;
	__be32		err;
	int		host_err;

	if (isdotent(fname, flen))
		return nfserr_exist;

	err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_NOP);
	if (err)
		return err;

	dentry = fhp->fh_dentry;

	host_err = fh_want_write(fhp);
	if (host_err)
		return nfserrno(host_err);

	inode_lock_nested(dentry->d_inode, I_MUTEX_PARENT);
	dchild = lookup_one_len(fname, dentry, flen);
	host_err = PTR_ERR(dchild);
	if (IS_ERR(dchild)) {
		err = nfserrno(host_err);
		goto out_unlock;
	}
	err = fh_compose(resfhp, fhp->fh_export, dchild, fhp);
	/*
	 * We unconditionally drop our ref to dchild as fh_compose will have
	 * already grabbed its own ref for it.
	 */
	dput(dchild);
	if (err)
		goto out_unlock;
	fh_fill_pre_attrs(fhp);
	err = nfsd_create_locked(rqstp, fhp, attrs, type, rdev, resfhp);
	fh_fill_post_attrs(fhp);
out_unlock:
	inode_unlock(dentry->d_inode);
	return err;
}

/*
 * Read a symlink. On entry, *lenp must contain the maximum path length that
 * fits into the buffer. On return, it contains the true length.
 * N.B. After this call fhp needs an fh_put
 */
__be32
nfsd_readlink(struct svc_rqst *rqstp, struct svc_fh *fhp, char *buf, int *lenp)
{
	__be32		err;
	const char *link;
	struct path path;
	DEFINE_DELAYED_CALL(done);
	int len;

	err = fh_verify(rqstp, fhp, S_IFLNK, NFSD_MAY_NOP);
	if (unlikely(err))
		return err;

	path.mnt = fhp->fh_export->ex_path.mnt;
	path.dentry = fhp->fh_dentry;

	if (unlikely(!d_is_symlink(path.dentry)))
		return nfserr_inval;

	touch_atime(&path);

	link = vfs_get_link(path.dentry, &done);
	if (IS_ERR(link))
		return nfserrno(PTR_ERR(link));

	len = strlen(link);
	if (len < *lenp)
		*lenp = len;
	memcpy(buf, link, *lenp);
	do_delayed_call(&done);
	return 0;
}

/**
 * nfsd_symlink - Create a symlink and look up its inode
 * @rqstp: RPC transaction being executed
 * @fhp: NFS filehandle of parent directory
 * @fname: filename of the new symlink
 * @flen: length of @fname
 * @path: content of the new symlink (NUL-terminated)
 * @attrs: requested attributes of new object
 * @resfhp: NFS filehandle of new object
 *
 * N.B. After this call _both_ fhp and resfhp need an fh_put
 *
 * Returns nfs_ok on success, or an nfsstat in network byte order.
 */
__be32
nfsd_symlink(struct svc_rqst *rqstp, struct svc_fh *fhp,
	     char *fname, int flen,
	     char *path, struct nfsd_attrs *attrs,
	     struct svc_fh *resfhp)
{
	struct dentry	*dentry, *dnew;
	__be32		err, cerr;
	int		host_err;

	err = nfserr_noent;
	if (!flen || path[0] == '\0')
		goto out;
	err = nfserr_exist;
	if (isdotent(fname, flen))
		goto out;

	err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_CREATE);
	if (err)
		goto out;

	host_err = fh_want_write(fhp);
	if (host_err) {
		err = nfserrno(host_err);
		goto out;
	}

	dentry = fhp->fh_dentry;
	inode_lock_nested(dentry->d_inode, I_MUTEX_PARENT);
	dnew = lookup_one_len(fname, dentry, flen);
	if (IS_ERR(dnew)) {
		err = nfserrno(PTR_ERR(dnew));
		inode_unlock(dentry->d_inode);
		goto out_drop_write;
	}
	fh_fill_pre_attrs(fhp);
	host_err = vfs_symlink(&init_user_ns, d_inode(dentry), dnew, path);
	err = nfserrno(host_err);
	cerr = fh_compose(resfhp, fhp->fh_export, dnew, fhp);
	if (!err)
		nfsd_create_setattr(rqstp, fhp, resfhp, attrs);
	fh_fill_post_attrs(fhp);
	inode_unlock(dentry->d_inode);
	if (!err)
		err = nfserrno(commit_metadata(fhp));
	dput(dnew);
	if (err==0) err = cerr;
out_drop_write:
	fh_drop_write(fhp);
out:
	return err;
}

/*
 * Create a hardlink
 * N.B. After this call _both_ ffhp and tfhp need an fh_put
 */
__be32
nfsd_link(struct svc_rqst *rqstp, struct svc_fh *ffhp,
				char *name, int len, struct svc_fh *tfhp)
{
	struct dentry	*ddir, *dnew, *dold;
	struct inode	*dirp;
	__be32		err;
	int		host_err;

	err = fh_verify(rqstp, ffhp, S_IFDIR, NFSD_MAY_CREATE);
	if (err)
		goto out;
	err = fh_verify(rqstp, tfhp, 0, NFSD_MAY_NOP);
	if (err)
		goto out;
	err = nfserr_isdir;
	if (d_is_dir(tfhp->fh_dentry))
		goto out;
	err = nfserr_perm;
	if (!len)
		goto out;
	err = nfserr_exist;
	if (isdotent(name, len))
		goto out;

	host_err = fh_want_write(tfhp);
	if (host_err) {
		err = nfserrno(host_err);
		goto out;
	}

	ddir = ffhp->fh_dentry;
	dirp = d_inode(ddir);
	inode_lock_nested(dirp, I_MUTEX_PARENT);

	dnew = lookup_one_len(name, ddir, len);
	if (IS_ERR(dnew)) {
		err = nfserrno(PTR_ERR(dnew));
		goto out_unlock;
	}

	dold = tfhp->fh_dentry;

	err = nfserr_noent;
	if (d_really_is_negative(dold))
		goto out_dput;
	fh_fill_pre_attrs(ffhp);
	host_err = vfs_link(dold, &init_user_ns, dirp, dnew, NULL);
	fh_fill_post_attrs(ffhp);
	inode_unlock(dirp);
	if (!host_err) {
		err = nfserrno(commit_metadata(ffhp));
		if (!err)
			err = nfserrno(commit_metadata(tfhp));
	} else {
		if (host_err == -EXDEV && rqstp->rq_vers == 2)
			err = nfserr_acces;
		else
			err = nfserrno(host_err);
	}
	dput(dnew);
out_drop_write:
	fh_drop_write(tfhp);
out:
	return err;

out_dput:
	dput(dnew);
out_unlock:
	inode_unlock(dirp);
	goto out_drop_write;
}

static void
nfsd_close_cached_files(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	if (inode && S_ISREG(inode->i_mode))
		nfsd_file_close_inode_sync(inode);
}

static bool
nfsd_has_cached_files(struct dentry *dentry)
{
	bool		ret = false;
	struct inode *inode = d_inode(dentry);

	if (inode && S_ISREG(inode->i_mode))
		ret = nfsd_file_is_cached(inode);
	return ret;
}

/*
 * Rename a file
 * N.B. After this call _both_ ffhp and tfhp need an fh_put
 */
__be32
nfsd_rename(struct svc_rqst *rqstp, struct svc_fh *ffhp, char *fname, int flen,
			    struct svc_fh *tfhp, char *tname, int tlen)
{
	struct dentry	*fdentry, *tdentry, *odentry, *ndentry, *trap;
	struct inode	*fdir, *tdir;
	__be32		err;
	int		host_err;
	bool		close_cached = false;

	err = fh_verify(rqstp, ffhp, S_IFDIR, NFSD_MAY_REMOVE);
	if (err)
		goto out;
	err = fh_verify(rqstp, tfhp, S_IFDIR, NFSD_MAY_CREATE);
	if (err)
		goto out;

	fdentry = ffhp->fh_dentry;
	fdir = d_inode(fdentry);

	tdentry = tfhp->fh_dentry;
	tdir = d_inode(tdentry);

	err = nfserr_perm;
	if (!flen || isdotent(fname, flen) || !tlen || isdotent(tname, tlen))
		goto out;

retry:
	host_err = fh_want_write(ffhp);
	if (host_err) {
		err = nfserrno(host_err);
		goto out;
	}

	trap = lock_rename(tdentry, fdentry);
	fh_fill_pre_attrs(ffhp);
	fh_fill_pre_attrs(tfhp);

	odentry = lookup_one_len(fname, fdentry, flen);
	host_err = PTR_ERR(odentry);
	if (IS_ERR(odentry))
		goto out_nfserr;

	host_err = -ENOENT;
	if (d_really_is_negative(odentry))
		goto out_dput_old;
	host_err = -EINVAL;
	if (odentry == trap)
		goto out_dput_old;

	ndentry = lookup_one_len(tname, tdentry, tlen);
	host_err = PTR_ERR(ndentry);
	if (IS_ERR(ndentry))
		goto out_dput_old;
	host_err = -ENOTEMPTY;
	if (ndentry == trap)
		goto out_dput_new;

	host_err = -EXDEV;
	if (ffhp->fh_export->ex_path.mnt != tfhp->fh_export->ex_path.mnt)
		goto out_dput_new;
	if (ffhp->fh_export->ex_path.dentry != tfhp->fh_export->ex_path.dentry)
		goto out_dput_new;

	if ((ndentry->d_sb->s_export_op->flags & EXPORT_OP_CLOSE_BEFORE_UNLINK) &&
	    nfsd_has_cached_files(ndentry)) {
		close_cached = true;
		goto out_dput_old;
	} else {
		struct renamedata rd = {
			.old_mnt_userns	= &init_user_ns,
			.old_dir	= fdir,
			.old_dentry	= odentry,
			.new_mnt_userns	= &init_user_ns,
			.new_dir	= tdir,
			.new_dentry	= ndentry,
		};
		int retries;

		for (retries = 1;;) {
			host_err = vfs_rename(&rd);
			if (host_err != -EAGAIN || !retries--)
				break;
			if (!nfsd_wait_for_delegreturn(rqstp, d_inode(odentry)))
				break;
		}
		if (!host_err) {
			host_err = commit_metadata(tfhp);
			if (!host_err)
				host_err = commit_metadata(ffhp);
		}
	}
 out_dput_new:
	dput(ndentry);
 out_dput_old:
	dput(odentry);
 out_nfserr:
	err = nfserrno(host_err);

	if (!close_cached) {
		fh_fill_post_attrs(ffhp);
		fh_fill_post_attrs(tfhp);
	}
	unlock_rename(tdentry, fdentry);
	fh_drop_write(ffhp);

	/*
	 * If the target dentry has cached open files, then we need to try to
	 * close them prior to doing the rename. Flushing delayed fput
	 * shouldn't be done with locks held however, so we delay it until this
	 * point and then reattempt the whole shebang.
	 */
	if (close_cached) {
		close_cached = false;
		nfsd_close_cached_files(ndentry);
		dput(ndentry);
		goto retry;
	}
out:
	return err;
}

/*
 * Unlink a file or directory
 * N.B. After this call fhp needs an fh_put
 */
__be32
nfsd_unlink(struct svc_rqst *rqstp, struct svc_fh *fhp, int type,
				char *fname, int flen)
{
	struct dentry	*dentry, *rdentry;
	struct inode	*dirp;
	struct inode	*rinode;
	__be32		err;
	int		host_err;

	err = nfserr_acces;
	if (!flen || isdotent(fname, flen))
		goto out;
	err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_REMOVE);
	if (err)
		goto out;

	host_err = fh_want_write(fhp);
	if (host_err)
		goto out_nfserr;

	dentry = fhp->fh_dentry;
	dirp = d_inode(dentry);
	inode_lock_nested(dirp, I_MUTEX_PARENT);

	rdentry = lookup_one_len(fname, dentry, flen);
	host_err = PTR_ERR(rdentry);
	if (IS_ERR(rdentry))
		goto out_unlock;

	if (d_really_is_negative(rdentry)) {
		dput(rdentry);
		host_err = -ENOENT;
		goto out_unlock;
	}
	rinode = d_inode(rdentry);
	ihold(rinode);

	if (!type)
		type = d_inode(rdentry)->i_mode & S_IFMT;

	fh_fill_pre_attrs(fhp);
	if (type != S_IFDIR) {
		int retries;

		if (rdentry->d_sb->s_export_op->flags & EXPORT_OP_CLOSE_BEFORE_UNLINK)
			nfsd_close_cached_files(rdentry);

		for (retries = 1;;) {
			host_err = vfs_unlink(&init_user_ns, dirp, rdentry, NULL);
			if (host_err != -EAGAIN || !retries--)
				break;
			if (!nfsd_wait_for_delegreturn(rqstp, rinode))
				break;
		}
	} else {
		host_err = vfs_rmdir(&init_user_ns, dirp, rdentry);
	}
	fh_fill_post_attrs(fhp);

	inode_unlock(dirp);
	if (!host_err)
		host_err = commit_metadata(fhp);
	dput(rdentry);
	iput(rinode);    /* truncate the inode here */

out_drop_write:
	fh_drop_write(fhp);
out_nfserr:
	if (host_err == -EBUSY) {
		/* name is mounted-on. There is no perfect
		 * error status.
		 */
		if (nfsd_v4client(rqstp))
			err = nfserr_file_open;
		else
			err = nfserr_acces;
	} else {
		err = nfserrno(host_err);
	}
out:
	return err;
out_unlock:
	inode_unlock(dirp);
	goto out_drop_write;
}

/*
 * We do this buffering because we must not call back into the file
 * system's ->lookup() method from the filldir callback. That may well
 * deadlock a number of file systems.
 *
 * This is based heavily on the implementation of same in XFS.
 */
struct buffered_dirent {
	u64		ino;
	loff_t		offset;
	int		namlen;
	unsigned int	d_type;
	char		name[];
};

struct readdir_data {
	struct dir_context ctx;
	char		*dirent;
	size_t		used;
	int		full;
};

static bool nfsd_buffered_filldir(struct dir_context *ctx, const char *name,
				 int namlen, loff_t offset, u64 ino,
				 unsigned int d_type)
{
	struct readdir_data *buf =
		container_of(ctx, struct readdir_data, ctx);
	struct buffered_dirent *de = (void *)(buf->dirent + buf->used);
	unsigned int reclen;

	reclen = ALIGN(sizeof(struct buffered_dirent) + namlen, sizeof(u64));
	if (buf->used + reclen > PAGE_SIZE) {
		buf->full = 1;
		return false;
	}

	de->namlen = namlen;
	de->offset = offset;
	de->ino = ino;
	de->d_type = d_type;
	memcpy(de->name, name, namlen);
	buf->used += reclen;

	return true;
}

static __be32 nfsd_buffered_readdir(struct file *file, struct svc_fh *fhp,
				    nfsd_filldir_t func, struct readdir_cd *cdp,
				    loff_t *offsetp)
{
	struct buffered_dirent *de;
	int host_err;
	int size;
	loff_t offset;
	struct readdir_data buf = {
		.ctx.actor = nfsd_buffered_filldir,
		.dirent = (void *)__get_free_page(GFP_KERNEL)
	};

	if (!buf.dirent)
		return nfserrno(-ENOMEM);

	offset = *offsetp;

	while (1) {
		unsigned int reclen;

		cdp->err = nfserr_eof; /* will be cleared on successful read */
		buf.used = 0;
		buf.full = 0;

		host_err = iterate_dir(file, &buf.ctx);
		if (buf.full)
			host_err = 0;

		if (host_err < 0)
			break;

		size = buf.used;

		if (!size)
			break;

		de = (struct buffered_dirent *)buf.dirent;
		while (size > 0) {
			offset = de->offset;

			if (func(cdp, de->name, de->namlen, de->offset,
				 de->ino, de->d_type))
				break;

			if (cdp->err != nfs_ok)
				break;

			trace_nfsd_dirent(fhp, de->ino, de->name, de->namlen);

			reclen = ALIGN(sizeof(*de) + de->namlen,
				       sizeof(u64));
			size -= reclen;
			de = (struct buffered_dirent *)((char *)de + reclen);
		}
		if (size > 0) /* We bailed out early */
			break;

		offset = vfs_llseek(file, 0, SEEK_CUR);
	}

	free_page((unsigned long)(buf.dirent));

	if (host_err)
		return nfserrno(host_err);

	*offsetp = offset;
	return cdp->err;
}

/*
 * Read entries from a directory.
 * The  NFSv3/4 verifier we ignore for now.
 */
__be32
nfsd_readdir(struct svc_rqst *rqstp, struct svc_fh *fhp, loff_t *offsetp, 
	     struct readdir_cd *cdp, nfsd_filldir_t func)
{
	__be32		err;
	struct file	*file;
	loff_t		offset = *offsetp;
	int             may_flags = NFSD_MAY_READ;

	/* NFSv2 only supports 32 bit cookies */
	if (rqstp->rq_vers > 2)
		may_flags |= NFSD_MAY_64BIT_COOKIE;

	err = nfsd_open(rqstp, fhp, S_IFDIR, may_flags, &file);
	if (err)
		goto out;

	offset = vfs_llseek(file, offset, SEEK_SET);
	if (offset < 0) {
		err = nfserrno((int)offset);
		goto out_close;
	}

	err = nfsd_buffered_readdir(file, fhp, func, cdp, offsetp);

	if (err == nfserr_eof || err == nfserr_toosmall)
		err = nfs_ok; /* can still be found in ->err */
out_close:
	fput(file);
out:
	return err;
}

/*
 * Get file system stats
 * N.B. After this call fhp needs an fh_put
 */
__be32
nfsd_statfs(struct svc_rqst *rqstp, struct svc_fh *fhp, struct kstatfs *stat, int access)
{
	__be32 err;

	err = fh_verify(rqstp, fhp, 0, NFSD_MAY_NOP | access);
	if (!err) {
		struct path path = {
			.mnt	= fhp->fh_export->ex_path.mnt,
			.dentry	= fhp->fh_dentry,
		};
		if (vfs_statfs(&path, stat))
			err = nfserr_io;
	}
	return err;
}

static int exp_rdonly(struct svc_rqst *rqstp, struct svc_export *exp)
{
	return nfsexp_flags(rqstp, exp) & NFSEXP_READONLY;
}

#ifdef CONFIG_NFSD_V4
/*
 * Helper function to translate error numbers. In the case of xattr operations,
 * some error codes need to be translated outside of the standard translations.
 *
 * ENODATA needs to be translated to nfserr_noxattr.
 * E2BIG to nfserr_xattr2big.
 *
 * Additionally, vfs_listxattr can return -ERANGE. This means that the
 * file has too many extended attributes to retrieve inside an
 * XATTR_LIST_MAX sized buffer. This is a bug in the xattr implementation:
 * filesystems will allow the adding of extended attributes until they hit
 * their own internal limit. This limit may be larger than XATTR_LIST_MAX.
 * So, at that point, the attributes are present and valid, but can't
 * be retrieved using listxattr, since the upper level xattr code enforces
 * the XATTR_LIST_MAX limit.
 *
 * This bug means that we need to deal with listxattr returning -ERANGE. The
 * best mapping is to return TOOSMALL.
 */
static __be32
nfsd_xattr_errno(int err)
{
	switch (err) {
	case -ENODATA:
		return nfserr_noxattr;
	case -E2BIG:
		return nfserr_xattr2big;
	case -ERANGE:
		return nfserr_toosmall;
	}
	return nfserrno(err);
}

/*
 * Retrieve the specified user extended attribute. To avoid always
 * having to allocate the maximum size (since we are not getting
 * a maximum size from the RPC), do a probe + alloc. Hold a reader
 * lock on i_rwsem to prevent the extended attribute from changing
 * size while we're doing this.
 */
__be32
nfsd_getxattr(struct svc_rqst *rqstp, struct svc_fh *fhp, char *name,
	      void **bufp, int *lenp)
{
	ssize_t len;
	__be32 err;
	char *buf;
	struct inode *inode;
	struct dentry *dentry;

	err = fh_verify(rqstp, fhp, 0, NFSD_MAY_READ);
	if (err)
		return err;

	err = nfs_ok;
	dentry = fhp->fh_dentry;
	inode = d_inode(dentry);

	inode_lock_shared(inode);

	len = vfs_getxattr(&init_user_ns, dentry, name, NULL, 0);

	/*
	 * Zero-length attribute, just return.
	 */
	if (len == 0) {
		*bufp = NULL;
		*lenp = 0;
		goto out;
	}

	if (len < 0) {
		err = nfsd_xattr_errno(len);
		goto out;
	}

	if (len > *lenp) {
		err = nfserr_toosmall;
		goto out;
	}

	buf = kvmalloc(len, GFP_KERNEL | GFP_NOFS);
	if (buf == NULL) {
		err = nfserr_jukebox;
		goto out;
	}

	len = vfs_getxattr(&init_user_ns, dentry, name, buf, len);
	if (len <= 0) {
		kvfree(buf);
		buf = NULL;
		err = nfsd_xattr_errno(len);
	}

	*lenp = len;
	*bufp = buf;

out:
	inode_unlock_shared(inode);

	return err;
}

/*
 * Retrieve the xattr names. Since we can't know how many are
 * user extended attributes, we must get all attributes here,
 * and have the XDR encode filter out the "user." ones.
 *
 * While this could always just allocate an XATTR_LIST_MAX
 * buffer, that's a waste, so do a probe + allocate. To
 * avoid any changes between the probe and allocate, wrap
 * this in inode_lock.
 */
__be32
nfsd_listxattr(struct svc_rqst *rqstp, struct svc_fh *fhp, char **bufp,
	       int *lenp)
{
	ssize_t len;
	__be32 err;
	char *buf;
	struct inode *inode;
	struct dentry *dentry;

	err = fh_verify(rqstp, fhp, 0, NFSD_MAY_READ);
	if (err)
		return err;

	dentry = fhp->fh_dentry;
	inode = d_inode(dentry);
	*lenp = 0;

	inode_lock_shared(inode);

	len = vfs_listxattr(dentry, NULL, 0);
	if (len <= 0) {
		err = nfsd_xattr_errno(len);
		goto out;
	}

	if (len > XATTR_LIST_MAX) {
		err = nfserr_xattr2big;
		goto out;
	}

	/*
	 * We're holding i_rwsem - use GFP_NOFS.
	 */
	buf = kvmalloc(len, GFP_KERNEL | GFP_NOFS);
	if (buf == NULL) {
		err = nfserr_jukebox;
		goto out;
	}

	len = vfs_listxattr(dentry, buf, len);
	if (len <= 0) {
		kvfree(buf);
		err = nfsd_xattr_errno(len);
		goto out;
	}

	*lenp = len;
	*bufp = buf;

	err = nfs_ok;
out:
	inode_unlock_shared(inode);

	return err;
}

/**
 * nfsd_removexattr - Remove an extended attribute
 * @rqstp: RPC transaction being executed
 * @fhp: NFS filehandle of object with xattr to remove
 * @name: name of xattr to remove (NUL-terminate)
 *
 * Pass in a NULL pointer for delegated_inode, and let the client deal
 * with NFS4ERR_DELAY (same as with e.g. setattr and remove).
 *
 * Returns nfs_ok on success, or an nfsstat in network byte order.
 */
__be32
nfsd_removexattr(struct svc_rqst *rqstp, struct svc_fh *fhp, char *name)
{
	__be32 err;
	int ret;

	err = fh_verify(rqstp, fhp, 0, NFSD_MAY_WRITE);
	if (err)
		return err;

	ret = fh_want_write(fhp);
	if (ret)
		return nfserrno(ret);

	inode_lock(fhp->fh_dentry->d_inode);
	fh_fill_pre_attrs(fhp);

	ret = __vfs_removexattr_locked(&init_user_ns, fhp->fh_dentry,
				       name, NULL);

	fh_fill_post_attrs(fhp);
	inode_unlock(fhp->fh_dentry->d_inode);
	fh_drop_write(fhp);

	return nfsd_xattr_errno(ret);
}

__be32
nfsd_setxattr(struct svc_rqst *rqstp, struct svc_fh *fhp, char *name,
	      void *buf, u32 len, u32 flags)
{
	__be32 err;
	int ret;

	err = fh_verify(rqstp, fhp, 0, NFSD_MAY_WRITE);
	if (err)
		return err;

	ret = fh_want_write(fhp);
	if (ret)
		return nfserrno(ret);
	inode_lock(fhp->fh_dentry->d_inode);
	fh_fill_pre_attrs(fhp);

	ret = __vfs_setxattr_locked(&init_user_ns, fhp->fh_dentry, name, buf,
				    len, flags, NULL);
	fh_fill_post_attrs(fhp);
	inode_unlock(fhp->fh_dentry->d_inode);
	fh_drop_write(fhp);

	return nfsd_xattr_errno(ret);
}
#endif

/*
 * Check for a user's access permissions to this inode.
 */
__be32
nfsd_permission(struct svc_rqst *rqstp, struct svc_export *exp,
					struct dentry *dentry, int acc)
{
	struct inode	*inode = d_inode(dentry);
	int		err;

	if ((acc & NFSD_MAY_MASK) == NFSD_MAY_NOP)
		return 0;
#if 0
	dprintk("nfsd: permission 0x%x%s%s%s%s%s%s%s mode 0%o%s%s%s\n",
		acc,
		(acc & NFSD_MAY_READ)?	" read"  : "",
		(acc & NFSD_MAY_WRITE)?	" write" : "",
		(acc & NFSD_MAY_EXEC)?	" exec"  : "",
		(acc & NFSD_MAY_SATTR)?	" sattr" : "",
		(acc & NFSD_MAY_TRUNC)?	" trunc" : "",
		(acc & NFSD_MAY_LOCK)?	" lock"  : "",
		(acc & NFSD_MAY_OWNER_OVERRIDE)? " owneroverride" : "",
		inode->i_mode,
		IS_IMMUTABLE(inode)?	" immut" : "",
		IS_APPEND(inode)?	" append" : "",
		__mnt_is_readonly(exp->ex_path.mnt)?	" ro" : "");
	dprintk("      owner %d/%d user %d/%d\n",
		inode->i_uid, inode->i_gid, current_fsuid(), current_fsgid());
#endif

	/* Normally we reject any write/sattr etc access on a read-only file
	 * system.  But if it is IRIX doing check on write-access for a 
	 * device special file, we ignore rofs.
	 */
	if (!(acc & NFSD_MAY_LOCAL_ACCESS))
		if (acc & (NFSD_MAY_WRITE | NFSD_MAY_SATTR | NFSD_MAY_TRUNC)) {
			if (exp_rdonly(rqstp, exp) ||
			    __mnt_is_readonly(exp->ex_path.mnt))
				return nfserr_rofs;
			if (/* (acc & NFSD_MAY_WRITE) && */ IS_IMMUTABLE(inode))
				return nfserr_perm;
		}
	if ((acc & NFSD_MAY_TRUNC) && IS_APPEND(inode))
		return nfserr_perm;

	if (acc & NFSD_MAY_LOCK) {
		/* If we cannot rely on authentication in NLM requests,
		 * just allow locks, otherwise require read permission, or
		 * ownership
		 */
		if (exp->ex_flags & NFSEXP_NOAUTHNLM)
			return 0;
		else
			acc = NFSD_MAY_READ | NFSD_MAY_OWNER_OVERRIDE;
	}
	/*
	 * The file owner always gets access permission for accesses that
	 * would normally be checked at open time. This is to make
	 * file access work even when the client has done a fchmod(fd, 0).
	 *
	 * However, `cp foo bar' should fail nevertheless when bar is
	 * readonly. A sensible way to do this might be to reject all
	 * attempts to truncate a read-only file, because a creat() call
	 * always implies file truncation.
	 * ... but this isn't really fair.  A process may reasonably call
	 * ftruncate on an open file descriptor on a file with perm 000.
	 * We must trust the client to do permission checking - using "ACCESS"
	 * with NFSv3.
	 */
	if ((acc & NFSD_MAY_OWNER_OVERRIDE) &&
	    uid_eq(inode->i_uid, current_fsuid()))
		return 0;

	/* This assumes  NFSD_MAY_{READ,WRITE,EXEC} == MAY_{READ,WRITE,EXEC} */
	err = inode_permission(&init_user_ns, inode,
			       acc & (MAY_READ | MAY_WRITE | MAY_EXEC));

	/* Allow read access to binaries even when mode 111 */
	if (err == -EACCES && S_ISREG(inode->i_mode) &&
	     (acc == (NFSD_MAY_READ | NFSD_MAY_OWNER_OVERRIDE) ||
	      acc == (NFSD_MAY_READ | NFSD_MAY_READ_IF_EXEC)))
		err = inode_permission(&init_user_ns, inode, MAY_EXEC);

	return err? nfserrno(err) : 0;
}
