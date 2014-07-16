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
#include <linux/fcntl.h>
#include <linux/namei.h>
#include <linux/delay.h>
#include <linux/fsnotify.h>
#include <linux/posix_acl_xattr.h>
#include <linux/xattr.h>
#include <linux/jhash.h>
#include <linux/ima.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/exportfs.h>
#include <linux/writeback.h>
#include <linux/security.h>

#ifdef CONFIG_NFSD_V3
#include "xdr3.h"
#endif /* CONFIG_NFSD_V3 */

#ifdef CONFIG_NFSD_V4
#include "acl.h"
#include "idmap.h"
#endif /* CONFIG_NFSD_V4 */

#include "nfsd.h"
#include "vfs.h"

#define NFSDDBG_FACILITY		NFSDDBG_FILEOP


/*
 * This is a cache of readahead params that help us choose the proper
 * readahead strategy. Initially, we set all readahead parameters to 0
 * and let the VFS handle things.
 * If you increase the number of cached files very much, you'll need to
 * add a hash table here.
 */
struct raparms {
	struct raparms		*p_next;
	unsigned int		p_count;
	ino_t			p_ino;
	dev_t			p_dev;
	int			p_set;
	struct file_ra_state	p_ra;
	unsigned int		p_hindex;
};

struct raparm_hbucket {
	struct raparms		*pb_head;
	spinlock_t		pb_lock;
} ____cacheline_aligned_in_smp;

#define RAPARM_HASH_BITS	4
#define RAPARM_HASH_SIZE	(1<<RAPARM_HASH_BITS)
#define RAPARM_HASH_MASK	(RAPARM_HASH_SIZE-1)
static struct raparm_hbucket	raparm_hash[RAPARM_HASH_SIZE];

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
 */
int nfsd_mountpoint(struct dentry *dentry, struct svc_export *exp)
{
	if (d_mountpoint(dentry))
		return 1;
	if (nfsd4_is_junction(dentry))
		return 1;
	if (!(exp->ex_flags & NFSEXP_V4ROOT))
		return 0;
	return dentry->d_inode != NULL;
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
	exp  = fhp->fh_export;
	exp_get(exp);

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
		/*
		 * In the nfsd4_open() case, this may be held across
		 * subsequent open and delegation acquisition which may
		 * need to take the child's i_mutex:
		 */
		fh_lock_nested(fhp, I_MUTEX_PARENT);
		dentry = lookup_one_len(name, dparent, len);
		host_err = PTR_ERR(dentry);
		if (IS_ERR(dentry))
			goto out_nfserr;
		/*
		 * check if we have crossed a mount point ...
		 */
		if (nfsd_mountpoint(dentry, exp)) {
			if ((host_err = nfsd_cross_mnt(rqstp, &dentry, &exp))) {
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

/*
 * Look up one component of a pathname.
 * N.B. After this call _both_ fhp and resfh need an fh_put
 *
 * If the lookup would cross a mountpoint, and the mounted filesystem
 * is exported to the client with NFSEXP_NOHIDE, then the lookup is
 * accepted as it stands and the mounted directory is
 * returned. Otherwise the covered directory is returned.
 * NOTE: this mountpoint crossing is not supported properly by all
 *   clients and is explicitly disallowed for NFSv3
 *      NeilBrown <neilb@cse.unsw.edu.au>
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
	if (!err && !dentry->d_inode)
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
commit_metadata(struct svc_fh *fhp)
{
	struct inode *inode = fhp->fh_dentry->d_inode;
	const struct export_operations *export_ops = inode->i_sb->s_export_op;

	if (!EX_ISSYNC(fhp->fh_export))
		return 0;

	if (export_ops->commit_metadata)
		return export_ops->commit_metadata(inode);
	return sync_inode_metadata(inode, 1);
}

/*
 * Go over the attributes and take care of the small differences between
 * NFS semantics and what Linux expects.
 */
static void
nfsd_sanitize_attrs(struct inode *inode, struct iattr *iap)
{
	/*
	 * NFSv2 does not differentiate between "set-[ac]time-to-now"
	 * which only requires access, and "set-[ac]time-to-X" which
	 * requires ownership.
	 * So if it looks like it might be "set both to the same time which
	 * is close to now", and if inode_change_ok fails, then we
	 * convert to "set to now" instead of "set to explicit time"
	 *
	 * We only call inode_change_ok as the last test as technically
	 * it is not an interface that we should be using.
	 */
#define BOTH_TIME_SET (ATTR_ATIME_SET | ATTR_MTIME_SET)
#define	MAX_TOUCH_TIME_ERROR (30*60)
	if ((iap->ia_valid & BOTH_TIME_SET) == BOTH_TIME_SET &&
	    iap->ia_mtime.tv_sec == iap->ia_atime.tv_sec) {
		/*
		 * Looks probable.
		 *
		 * Now just make sure time is in the right ballpark.
		 * Solaris, at least, doesn't seem to care what the time
		 * request is.  We require it be within 30 minutes of now.
		 */
		time_t delta = iap->ia_atime.tv_sec - get_seconds();
		if (delta < 0)
			delta = -delta;
		if (delta < MAX_TOUCH_TIME_ERROR &&
		    inode_change_ok(inode, iap) != 0) {
			/*
			 * Turn off ATTR_[AM]TIME_SET but leave ATTR_[AM]TIME.
			 * This will cause notify_change to set these times
			 * to "now"
			 */
			iap->ia_valid &= ~BOTH_TIME_SET;
		}
	}

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
	struct inode *inode = fhp->fh_dentry->d_inode;
	int host_err;

	if (iap->ia_size < inode->i_size) {
		__be32 err;

		err = nfsd_permission(rqstp, fhp->fh_export, fhp->fh_dentry,
				NFSD_MAY_TRUNC | NFSD_MAY_OWNER_OVERRIDE);
		if (err)
			return err;
	}

	host_err = get_write_access(inode);
	if (host_err)
		goto out_nfserrno;

	host_err = locks_verify_truncate(inode, NULL, iap->ia_size);
	if (host_err)
		goto out_put_write_access;
	return 0;

out_put_write_access:
	put_write_access(inode);
out_nfserrno:
	return nfserrno(host_err);
}

/*
 * Set various file attributes.  After this call fhp needs an fh_put.
 */
__be32
nfsd_setattr(struct svc_rqst *rqstp, struct svc_fh *fhp, struct iattr *iap,
	     int check_guard, time_t guardtime)
{
	struct dentry	*dentry;
	struct inode	*inode;
	int		accmode = NFSD_MAY_SATTR;
	umode_t		ftype = 0;
	__be32		err;
	int		host_err;
	bool		get_write_count;
	int		size_change = 0;

	if (iap->ia_valid & (ATTR_ATIME | ATTR_MTIME | ATTR_SIZE))
		accmode |= NFSD_MAY_WRITE|NFSD_MAY_OWNER_OVERRIDE;
	if (iap->ia_valid & ATTR_SIZE)
		ftype = S_IFREG;

	/* Callers that do fh_verify should do the fh_want_write: */
	get_write_count = !fhp->fh_dentry;

	/* Get inode */
	err = fh_verify(rqstp, fhp, ftype, accmode);
	if (err)
		goto out;
	if (get_write_count) {
		host_err = fh_want_write(fhp);
		if (host_err)
			return nfserrno(host_err);
	}

	dentry = fhp->fh_dentry;
	inode = dentry->d_inode;

	/* Ignore any mode updates on symlinks */
	if (S_ISLNK(inode->i_mode))
		iap->ia_valid &= ~ATTR_MODE;

	if (!iap->ia_valid)
		goto out;

	nfsd_sanitize_attrs(inode, iap);

	/*
	 * The size case is special, it changes the file in addition to the
	 * attributes.
	 */
	if (iap->ia_valid & ATTR_SIZE) {
		err = nfsd_get_write_access(rqstp, fhp, iap);
		if (err)
			goto out;
		size_change = 1;
	}

	iap->ia_valid |= ATTR_CTIME;

	if (check_guard && guardtime != inode->i_ctime.tv_sec) {
		err = nfserr_notsync;
		goto out_put_write_access;
	}

	fh_lock(fhp);
	host_err = notify_change(dentry, iap, NULL);
	fh_unlock(fhp);
	err = nfserrno(host_err);

out_put_write_access:
	if (size_change)
		put_write_access(inode);
	if (!err)
		commit_metadata(fhp);
out:
	return err;
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
	struct inode *inode = dentry->d_inode;

	if (inode == NULL)
		return 0;
	if (inode->i_mode & S_IXUGO)
		return 0;
	if (!(inode->i_mode & S_ISVTX))
		return 0;
	if (vfs_getxattr(dentry, NFSD_JUNCTION_XATTR_NAME, NULL, 0) <= 0)
		return 0;
	return 1;
}
#ifdef CONFIG_NFSD_V4_SECURITY_LABEL
__be32 nfsd4_set_nfs4_label(struct svc_rqst *rqstp, struct svc_fh *fhp,
		struct xdr_netobj *label)
{
	__be32 error;
	int host_error;
	struct dentry *dentry;

	error = fh_verify(rqstp, fhp, 0 /* S_IFREG */, NFSD_MAY_SATTR);
	if (error)
		return error;

	dentry = fhp->fh_dentry;

	mutex_lock(&dentry->d_inode->i_mutex);
	host_error = security_inode_setsecctx(dentry, label->data, label->len);
	mutex_unlock(&dentry->d_inode->i_mutex);
	return nfserrno(host_error);
}
#else
__be32 nfsd4_set_nfs4_label(struct svc_rqst *rqstp, struct svc_fh *fhp,
		struct xdr_netobj *label)
{
	return nfserr_notsupp;
}
#endif

#endif /* defined(CONFIG_NFSD_V4) */

#ifdef CONFIG_NFSD_V3
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

    {	0,			0				}
};

static struct accessmap	nfs3_diraccess[] = {
    {	NFS3_ACCESS_READ,	NFSD_MAY_READ			},
    {	NFS3_ACCESS_LOOKUP,	NFSD_MAY_EXEC			},
    {	NFS3_ACCESS_MODIFY,	NFSD_MAY_EXEC|NFSD_MAY_WRITE|NFSD_MAY_TRUNC},
    {	NFS3_ACCESS_EXTEND,	NFSD_MAY_EXEC|NFSD_MAY_WRITE	},
    {	NFS3_ACCESS_DELETE,	NFSD_MAY_REMOVE			},

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

	if (S_ISREG(dentry->d_inode->i_mode))
		map = nfs3_regaccess;
	else if (S_ISDIR(dentry->d_inode->i_mode))
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
#endif /* CONFIG_NFSD_V3 */

static int nfsd_open_break_lease(struct inode *inode, int access)
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
__be32
nfsd_open(struct svc_rqst *rqstp, struct svc_fh *fhp, umode_t type,
			int may_flags, struct file **filp)
{
	struct path	path;
	struct inode	*inode;
	int		flags = O_RDONLY|O_LARGEFILE;
	__be32		err;
	int		host_err = 0;

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
	err = fh_verify(rqstp, fhp, type, may_flags);
	if (err)
		goto out;

	path.mnt = fhp->fh_export->ex_path.mnt;
	path.dentry = fhp->fh_dentry;
	inode = path.dentry->d_inode;

	/* Disallow write access to files with the append-only bit set
	 * or any access when mandatory locking enabled
	 */
	err = nfserr_perm;
	if (IS_APPEND(inode) && (may_flags & NFSD_MAY_WRITE))
		goto out;
	/*
	 * We must ignore files (but only files) which might have mandatory
	 * locks on them because there is no way to know if the accesser has
	 * the lock.
	 */
	if (S_ISREG((inode)->i_mode) && mandatory_lock(inode))
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
	*filp = dentry_open(&path, flags, current_cred());
	if (IS_ERR(*filp)) {
		host_err = PTR_ERR(*filp);
		*filp = NULL;
	} else {
		host_err = ima_file_check(*filp, may_flags);

		if (may_flags & NFSD_MAY_64BIT_COOKIE)
			(*filp)->f_mode |= FMODE_64BITHASH;
		else
			(*filp)->f_mode |= FMODE_32BITHASH;
	}

out_nfserr:
	err = nfserrno(host_err);
out:
	validate_process_creds();
	return err;
}

/*
 * Close a file.
 */
void
nfsd_close(struct file *filp)
{
	fput(filp);
}

/*
 * Obtain the readahead parameters for the file
 * specified by (dev, ino).
 */

static inline struct raparms *
nfsd_get_raparms(dev_t dev, ino_t ino)
{
	struct raparms	*ra, **rap, **frap = NULL;
	int depth = 0;
	unsigned int hash;
	struct raparm_hbucket *rab;

	hash = jhash_2words(dev, ino, 0xfeedbeef) & RAPARM_HASH_MASK;
	rab = &raparm_hash[hash];

	spin_lock(&rab->pb_lock);
	for (rap = &rab->pb_head; (ra = *rap); rap = &ra->p_next) {
		if (ra->p_ino == ino && ra->p_dev == dev)
			goto found;
		depth++;
		if (ra->p_count == 0)
			frap = rap;
	}
	depth = nfsdstats.ra_size;
	if (!frap) {	
		spin_unlock(&rab->pb_lock);
		return NULL;
	}
	rap = frap;
	ra = *frap;
	ra->p_dev = dev;
	ra->p_ino = ino;
	ra->p_set = 0;
	ra->p_hindex = hash;
found:
	if (rap != &rab->pb_head) {
		*rap = ra->p_next;
		ra->p_next   = rab->pb_head;
		rab->pb_head = ra;
	}
	ra->p_count++;
	nfsdstats.ra_depth[depth*10/nfsdstats.ra_size]++;
	spin_unlock(&rab->pb_lock);
	return ra;
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
	struct page **pp = rqstp->rq_next_page;
	struct page *page = buf->page;
	size_t size;

	size = sd->len;

	if (rqstp->rq_res.page_len == 0) {
		get_page(page);
		put_page(*rqstp->rq_next_page);
		*(rqstp->rq_next_page++) = page;
		rqstp->rq_res.page_base = buf->offset;
		rqstp->rq_res.page_len = size;
	} else if (page != pp[-1]) {
		get_page(page);
		if (*rqstp->rq_next_page)
			put_page(*rqstp->rq_next_page);
		*(rqstp->rq_next_page++) = page;
		rqstp->rq_res.page_len += size;
	} else
		rqstp->rq_res.page_len += size;

	return size;
}

static int nfsd_direct_splice_actor(struct pipe_inode_info *pipe,
				    struct splice_desc *sd)
{
	return __splice_from_pipe(pipe, sd, nfsd_splice_actor);
}

__be32 nfsd_finish_read(struct file *file, unsigned long *count, int host_err)
{
	if (host_err >= 0) {
		nfsdstats.io_read += host_err;
		*count = host_err;
		fsnotify_access(file);
		return 0;
	} else 
		return nfserrno(host_err);
}

int nfsd_splice_read(struct svc_rqst *rqstp,
		     struct file *file, loff_t offset, unsigned long *count)
{
	struct splice_desc sd = {
		.len		= 0,
		.total_len	= *count,
		.pos		= offset,
		.u.data		= rqstp,
	};
	int host_err;

	rqstp->rq_next_page = rqstp->rq_respages + 1;
	host_err = splice_direct_to_actor(file, &sd, nfsd_direct_splice_actor);
	return nfsd_finish_read(file, count, host_err);
}

int nfsd_readv(struct file *file, loff_t offset, struct kvec *vec, int vlen,
		unsigned long *count)
{
	mm_segment_t oldfs;
	int host_err;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	host_err = vfs_readv(file, (struct iovec __user *)vec, vlen, &offset);
	set_fs(oldfs);
	return nfsd_finish_read(file, count, host_err);
}

static __be32
nfsd_vfs_read(struct svc_rqst *rqstp, struct file *file,
	      loff_t offset, struct kvec *vec, int vlen, unsigned long *count)
{
	if (file->f_op->splice_read && rqstp->rq_splice_ok)
		return nfsd_splice_read(rqstp, file, offset, count);
	else
		return nfsd_readv(file, offset, vec, vlen, count);
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

static __be32
nfsd_vfs_write(struct svc_rqst *rqstp, struct svc_fh *fhp, struct file *file,
				loff_t offset, struct kvec *vec, int vlen,
				unsigned long *cnt, int *stablep)
{
	struct svc_export	*exp;
	struct dentry		*dentry;
	struct inode		*inode;
	mm_segment_t		oldfs;
	__be32			err = 0;
	int			host_err;
	int			stable = *stablep;
	int			use_wgather;
	loff_t			pos = offset;
	unsigned int		pflags = current->flags;

	if (rqstp->rq_local)
		/*
		 * We want less throttling in balance_dirty_pages()
		 * and shrink_inactive_list() so that nfs to
		 * localhost doesn't cause nfsd to lock up due to all
		 * the client's dirty pages or its congested queue.
		 */
		current->flags |= PF_LESS_THROTTLE;

	dentry = file->f_path.dentry;
	inode = dentry->d_inode;
	exp   = fhp->fh_export;

	use_wgather = (rqstp->rq_vers == 2) && EX_WGATHER(exp);

	if (!EX_ISSYNC(exp))
		stable = 0;

	/* Write the data. */
	oldfs = get_fs(); set_fs(KERNEL_DS);
	host_err = vfs_writev(file, (struct iovec __user *)vec, vlen, &pos);
	set_fs(oldfs);
	if (host_err < 0)
		goto out_nfserr;
	*cnt = host_err;
	nfsdstats.io_write += host_err;
	fsnotify_modify(file);

	if (stable) {
		if (use_wgather)
			host_err = wait_for_concurrent_writes(file);
		else
			host_err = vfs_fsync_range(file, offset, offset+*cnt, 0);
	}

out_nfserr:
	dprintk("nfsd: write complete host_err=%d\n", host_err);
	if (host_err >= 0)
		err = 0;
	else
		err = nfserrno(host_err);
	if (rqstp->rq_local)
		tsk_restore_flags(current, pflags, PF_LESS_THROTTLE);
	return err;
}

__be32 nfsd_get_tmp_read_open(struct svc_rqst *rqstp, struct svc_fh *fhp,
		struct file **file, struct raparms **ra)
{
	struct inode *inode;
	__be32 err;

	err = nfsd_open(rqstp, fhp, S_IFREG, NFSD_MAY_READ, file);
	if (err)
		return err;

	inode = file_inode(*file);

	/* Get readahead parameters */
	*ra = nfsd_get_raparms(inode->i_sb->s_dev, inode->i_ino);

	if (*ra && (*ra)->p_set)
		(*file)->f_ra = (*ra)->p_ra;
	return nfs_ok;
}

void nfsd_put_tmp_read_open(struct file *file, struct raparms *ra)
{
	/* Write back readahead params */
	if (ra) {
		struct raparm_hbucket *rab = &raparm_hash[ra->p_hindex];
		spin_lock(&rab->pb_lock);
		ra->p_ra = file->f_ra;
		ra->p_set = 1;
		ra->p_count--;
		spin_unlock(&rab->pb_lock);
	}
	nfsd_close(file);
}

/*
 * Read data from a file. count must contain the requested read count
 * on entry. On return, *count contains the number of bytes actually read.
 * N.B. After this call fhp needs an fh_put
 */
__be32 nfsd_read(struct svc_rqst *rqstp, struct svc_fh *fhp,
	loff_t offset, struct kvec *vec, int vlen, unsigned long *count)
{
	struct file *file;
	struct raparms	*ra;
	__be32 err;

	err = nfsd_get_tmp_read_open(rqstp, fhp, &file, &ra);
	if (err)
		return err;

	err = nfsd_vfs_read(rqstp, file, offset, vec, vlen, count);

	nfsd_put_tmp_read_open(file, ra);

	return err;
}

/*
 * Write data to a file.
 * The stable flag requests synchronous writes.
 * N.B. After this call fhp needs an fh_put
 */
__be32
nfsd_write(struct svc_rqst *rqstp, struct svc_fh *fhp, struct file *file,
		loff_t offset, struct kvec *vec, int vlen, unsigned long *cnt,
		int *stablep)
{
	__be32			err = 0;

	if (file) {
		err = nfsd_permission(rqstp, fhp->fh_export, fhp->fh_dentry,
				NFSD_MAY_WRITE|NFSD_MAY_OWNER_OVERRIDE);
		if (err)
			goto out;
		err = nfsd_vfs_write(rqstp, fhp, file, offset, vec, vlen, cnt,
				stablep);
	} else {
		err = nfsd_open(rqstp, fhp, S_IFREG, NFSD_MAY_WRITE, &file);
		if (err)
			goto out;

		if (cnt)
			err = nfsd_vfs_write(rqstp, fhp, file, offset, vec, vlen,
					     cnt, stablep);
		nfsd_close(file);
	}
out:
	return err;
}

#ifdef CONFIG_NFSD_V3
/*
 * Commit all pending writes to stable storage.
 *
 * Note: we only guarantee that data that lies within the range specified
 * by the 'offset' and 'count' parameters will be synced.
 *
 * Unfortunately we cannot lock the file to make sure we return full WCC
 * data to the client, as locking happens lower down in the filesystem.
 */
__be32
nfsd_commit(struct svc_rqst *rqstp, struct svc_fh *fhp,
               loff_t offset, unsigned long count)
{
	struct file	*file;
	loff_t		end = LLONG_MAX;
	__be32		err = nfserr_inval;

	if (offset < 0)
		goto out;
	if (count != 0) {
		end = offset + (loff_t)count - 1;
		if (end < offset)
			goto out;
	}

	err = nfsd_open(rqstp, fhp, S_IFREG,
			NFSD_MAY_WRITE|NFSD_MAY_NOT_BREAK_LEASE, &file);
	if (err)
		goto out;
	if (EX_ISSYNC(fhp->fh_export)) {
		int err2 = vfs_fsync_range(file, offset, end, 0);

		if (err2 != -EINVAL)
			err = nfserrno(err2);
		else
			err = nfserr_notsupp;
	}

	nfsd_close(file);
out:
	return err;
}
#endif /* CONFIG_NFSD_V3 */

static __be32
nfsd_create_setattr(struct svc_rqst *rqstp, struct svc_fh *resfhp,
			struct iattr *iap)
{
	/*
	 * Mode has already been set earlier in create:
	 */
	iap->ia_valid &= ~ATTR_MODE;
	/*
	 * Setting uid/gid works only for root.  Irix appears to
	 * send along the gid on create when it tries to implement
	 * setgid directories via NFS:
	 */
	if (!uid_eq(current_fsuid(), GLOBAL_ROOT_UID))
		iap->ia_valid &= ~(ATTR_UID|ATTR_GID);
	if (iap->ia_valid)
		return nfsd_setattr(rqstp, resfhp, iap, 0, (time_t)0);
	return 0;
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

/*
 * Create a file (regular, directory, device, fifo); UNIX sockets 
 * not yet implemented.
 * If the response fh has been verified, the parent directory should
 * already be locked. Note that the parent directory is left locked.
 *
 * N.B. Every call to nfsd_create needs an fh_put for _both_ fhp and resfhp
 */
__be32
nfsd_create(struct svc_rqst *rqstp, struct svc_fh *fhp,
		char *fname, int flen, struct iattr *iap,
		int type, dev_t rdev, struct svc_fh *resfhp)
{
	struct dentry	*dentry, *dchild = NULL;
	struct inode	*dirp;
	__be32		err;
	__be32		err2;
	int		host_err;

	err = nfserr_perm;
	if (!flen)
		goto out;
	err = nfserr_exist;
	if (isdotent(fname, flen))
		goto out;

	err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_CREATE);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	dirp = dentry->d_inode;

	err = nfserr_notdir;
	if (!dirp->i_op->lookup)
		goto out;
	/*
	 * Check whether the response file handle has been verified yet.
	 * If it has, the parent directory should already be locked.
	 */
	if (!resfhp->fh_dentry) {
		host_err = fh_want_write(fhp);
		if (host_err)
			goto out_nfserr;

		/* called from nfsd_proc_mkdir, or possibly nfsd3_proc_create */
		fh_lock_nested(fhp, I_MUTEX_PARENT);
		dchild = lookup_one_len(fname, dentry, flen);
		host_err = PTR_ERR(dchild);
		if (IS_ERR(dchild))
			goto out_nfserr;
		err = fh_compose(resfhp, fhp->fh_export, dchild, fhp);
		if (err)
			goto out;
	} else {
		/* called from nfsd_proc_create */
		dchild = dget(resfhp->fh_dentry);
		if (!fhp->fh_locked) {
			/* not actually possible */
			printk(KERN_ERR
				"nfsd_create: parent %pd2 not locked!\n",
				dentry);
			err = nfserr_io;
			goto out;
		}
	}
	/*
	 * Make sure the child dentry is still negative ...
	 */
	err = nfserr_exist;
	if (dchild->d_inode) {
		dprintk("nfsd_create: dentry %pd/%pd not negative!\n",
			dentry, dchild);
		goto out; 
	}

	if (!(iap->ia_valid & ATTR_MODE))
		iap->ia_mode = 0;
	iap->ia_mode = (iap->ia_mode & S_IALLUGO) | type;

	err = nfserr_inval;
	if (!S_ISREG(type) && !S_ISDIR(type) && !special_file(type)) {
		printk(KERN_WARNING "nfsd: bad file type %o in nfsd_create\n",
		       type);
		goto out;
	}

	/*
	 * Get the dir op function pointer.
	 */
	err = 0;
	host_err = 0;
	switch (type) {
	case S_IFREG:
		host_err = vfs_create(dirp, dchild, iap->ia_mode, true);
		if (!host_err)
			nfsd_check_ignore_resizing(iap);
		break;
	case S_IFDIR:
		host_err = vfs_mkdir(dirp, dchild, iap->ia_mode);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		host_err = vfs_mknod(dirp, dchild, iap->ia_mode, rdev);
		break;
	}
	if (host_err < 0)
		goto out_nfserr;

	err = nfsd_create_setattr(rqstp, resfhp, iap);

	/*
	 * nfsd_setattr already committed the child.  Transactional filesystems
	 * had a chance to commit changes for both parent and child
	 * simultaneously making the following commit_metadata a noop.
	 */
	err2 = nfserrno(commit_metadata(fhp));
	if (err2)
		err = err2;
	/*
	 * Update the file handle to get the new inode info.
	 */
	if (!err)
		err = fh_update(resfhp);
out:
	if (dchild && !IS_ERR(dchild))
		dput(dchild);
	return err;

out_nfserr:
	err = nfserrno(host_err);
	goto out;
}

#ifdef CONFIG_NFSD_V3

static inline int nfsd_create_is_exclusive(int createmode)
{
	return createmode == NFS3_CREATE_EXCLUSIVE
	       || createmode == NFS4_CREATE_EXCLUSIVE4_1;
}

/*
 * NFSv3 and NFSv4 version of nfsd_create
 */
__be32
do_nfsd_create(struct svc_rqst *rqstp, struct svc_fh *fhp,
		char *fname, int flen, struct iattr *iap,
		struct svc_fh *resfhp, int createmode, u32 *verifier,
	        bool *truncp, bool *created)
{
	struct dentry	*dentry, *dchild = NULL;
	struct inode	*dirp;
	__be32		err;
	int		host_err;
	__u32		v_mtime=0, v_atime=0;

	err = nfserr_perm;
	if (!flen)
		goto out;
	err = nfserr_exist;
	if (isdotent(fname, flen))
		goto out;
	if (!(iap->ia_valid & ATTR_MODE))
		iap->ia_mode = 0;
	err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_EXEC);
	if (err)
		goto out;

	dentry = fhp->fh_dentry;
	dirp = dentry->d_inode;

	/* Get all the sanity checks out of the way before
	 * we lock the parent. */
	err = nfserr_notdir;
	if (!dirp->i_op->lookup)
		goto out;

	host_err = fh_want_write(fhp);
	if (host_err)
		goto out_nfserr;

	fh_lock_nested(fhp, I_MUTEX_PARENT);

	/*
	 * Compose the response file handle.
	 */
	dchild = lookup_one_len(fname, dentry, flen);
	host_err = PTR_ERR(dchild);
	if (IS_ERR(dchild))
		goto out_nfserr;

	/* If file doesn't exist, check for permissions to create one */
	if (!dchild->d_inode) {
		err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_CREATE);
		if (err)
			goto out;
	}

	err = fh_compose(resfhp, fhp->fh_export, dchild, fhp);
	if (err)
		goto out;

	if (nfsd_create_is_exclusive(createmode)) {
		/* solaris7 gets confused (bugid 4218508) if these have
		 * the high bit set, so just clear the high bits. If this is
		 * ever changed to use different attrs for storing the
		 * verifier, then do_open_lookup() will also need to be fixed
		 * accordingly.
		 */
		v_mtime = verifier[0]&0x7fffffff;
		v_atime = verifier[1]&0x7fffffff;
	}
	
	if (dchild->d_inode) {
		err = 0;

		switch (createmode) {
		case NFS3_CREATE_UNCHECKED:
			if (! S_ISREG(dchild->d_inode->i_mode))
				goto out;
			else if (truncp) {
				/* in nfsv4, we need to treat this case a little
				 * differently.  we don't want to truncate the
				 * file now; this would be wrong if the OPEN
				 * fails for some other reason.  furthermore,
				 * if the size is nonzero, we should ignore it
				 * according to spec!
				 */
				*truncp = (iap->ia_valid & ATTR_SIZE) && !iap->ia_size;
			}
			else {
				iap->ia_valid &= ATTR_SIZE;
				goto set_attr;
			}
			break;
		case NFS3_CREATE_EXCLUSIVE:
			if (   dchild->d_inode->i_mtime.tv_sec == v_mtime
			    && dchild->d_inode->i_atime.tv_sec == v_atime
			    && dchild->d_inode->i_size  == 0 ) {
				if (created)
					*created = 1;
				break;
			}
		case NFS4_CREATE_EXCLUSIVE4_1:
			if (   dchild->d_inode->i_mtime.tv_sec == v_mtime
			    && dchild->d_inode->i_atime.tv_sec == v_atime
			    && dchild->d_inode->i_size  == 0 ) {
				if (created)
					*created = 1;
				goto set_attr;
			}
			 /* fallthru */
		case NFS3_CREATE_GUARDED:
			err = nfserr_exist;
		}
		fh_drop_write(fhp);
		goto out;
	}

	host_err = vfs_create(dirp, dchild, iap->ia_mode, true);
	if (host_err < 0) {
		fh_drop_write(fhp);
		goto out_nfserr;
	}
	if (created)
		*created = 1;

	nfsd_check_ignore_resizing(iap);

	if (nfsd_create_is_exclusive(createmode)) {
		/* Cram the verifier into atime/mtime */
		iap->ia_valid = ATTR_MTIME|ATTR_ATIME
			| ATTR_MTIME_SET|ATTR_ATIME_SET;
		/* XXX someone who knows this better please fix it for nsec */ 
		iap->ia_mtime.tv_sec = v_mtime;
		iap->ia_atime.tv_sec = v_atime;
		iap->ia_mtime.tv_nsec = 0;
		iap->ia_atime.tv_nsec = 0;
	}

 set_attr:
	err = nfsd_create_setattr(rqstp, resfhp, iap);

	/*
	 * nfsd_setattr already committed the child (and possibly also the parent).
	 */
	if (!err)
		err = nfserrno(commit_metadata(fhp));

	/*
	 * Update the filehandle to get the new inode info.
	 */
	if (!err)
		err = fh_update(resfhp);

 out:
	fh_unlock(fhp);
	if (dchild && !IS_ERR(dchild))
		dput(dchild);
	fh_drop_write(fhp);
 	return err;
 
 out_nfserr:
	err = nfserrno(host_err);
	goto out;
}
#endif /* CONFIG_NFSD_V3 */

/*
 * Read a symlink. On entry, *lenp must contain the maximum path length that
 * fits into the buffer. On return, it contains the true length.
 * N.B. After this call fhp needs an fh_put
 */
__be32
nfsd_readlink(struct svc_rqst *rqstp, struct svc_fh *fhp, char *buf, int *lenp)
{
	struct inode	*inode;
	mm_segment_t	oldfs;
	__be32		err;
	int		host_err;
	struct path path;

	err = fh_verify(rqstp, fhp, S_IFLNK, NFSD_MAY_NOP);
	if (err)
		goto out;

	path.mnt = fhp->fh_export->ex_path.mnt;
	path.dentry = fhp->fh_dentry;
	inode = path.dentry->d_inode;

	err = nfserr_inval;
	if (!inode->i_op->readlink)
		goto out;

	touch_atime(&path);
	/* N.B. Why does this call need a get_fs()??
	 * Remove the set_fs and watch the fireworks:-) --okir
	 */

	oldfs = get_fs(); set_fs(KERNEL_DS);
	host_err = inode->i_op->readlink(path.dentry, (char __user *)buf, *lenp);
	set_fs(oldfs);

	if (host_err < 0)
		goto out_nfserr;
	*lenp = host_err;
	err = 0;
out:
	return err;

out_nfserr:
	err = nfserrno(host_err);
	goto out;
}

/*
 * Create a symlink and look up its inode
 * N.B. After this call _both_ fhp and resfhp need an fh_put
 */
__be32
nfsd_symlink(struct svc_rqst *rqstp, struct svc_fh *fhp,
				char *fname, int flen,
				char *path,  int plen,
				struct svc_fh *resfhp,
				struct iattr *iap)
{
	struct dentry	*dentry, *dnew;
	__be32		err, cerr;
	int		host_err;

	err = nfserr_noent;
	if (!flen || !plen)
		goto out;
	err = nfserr_exist;
	if (isdotent(fname, flen))
		goto out;

	err = fh_verify(rqstp, fhp, S_IFDIR, NFSD_MAY_CREATE);
	if (err)
		goto out;

	host_err = fh_want_write(fhp);
	if (host_err)
		goto out_nfserr;

	fh_lock(fhp);
	dentry = fhp->fh_dentry;
	dnew = lookup_one_len(fname, dentry, flen);
	host_err = PTR_ERR(dnew);
	if (IS_ERR(dnew))
		goto out_nfserr;

	if (unlikely(path[plen] != 0)) {
		char *path_alloced = kmalloc(plen+1, GFP_KERNEL);
		if (path_alloced == NULL)
			host_err = -ENOMEM;
		else {
			strncpy(path_alloced, path, plen);
			path_alloced[plen] = 0;
			host_err = vfs_symlink(dentry->d_inode, dnew, path_alloced);
			kfree(path_alloced);
		}
	} else
		host_err = vfs_symlink(dentry->d_inode, dnew, path);
	err = nfserrno(host_err);
	if (!err)
		err = nfserrno(commit_metadata(fhp));
	fh_unlock(fhp);

	fh_drop_write(fhp);

	cerr = fh_compose(resfhp, fhp->fh_export, dnew, fhp);
	dput(dnew);
	if (err==0) err = cerr;
out:
	return err;

out_nfserr:
	err = nfserrno(host_err);
	goto out;
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
	if (S_ISDIR(tfhp->fh_dentry->d_inode->i_mode))
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

	fh_lock_nested(ffhp, I_MUTEX_PARENT);
	ddir = ffhp->fh_dentry;
	dirp = ddir->d_inode;

	dnew = lookup_one_len(name, ddir, len);
	host_err = PTR_ERR(dnew);
	if (IS_ERR(dnew))
		goto out_nfserr;

	dold = tfhp->fh_dentry;

	err = nfserr_noent;
	if (!dold->d_inode)
		goto out_dput;
	host_err = vfs_link(dold, dirp, dnew, NULL);
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
out_dput:
	dput(dnew);
out_unlock:
	fh_unlock(ffhp);
	fh_drop_write(tfhp);
out:
	return err;

out_nfserr:
	err = nfserrno(host_err);
	goto out_unlock;
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

	err = fh_verify(rqstp, ffhp, S_IFDIR, NFSD_MAY_REMOVE);
	if (err)
		goto out;
	err = fh_verify(rqstp, tfhp, S_IFDIR, NFSD_MAY_CREATE);
	if (err)
		goto out;

	fdentry = ffhp->fh_dentry;
	fdir = fdentry->d_inode;

	tdentry = tfhp->fh_dentry;
	tdir = tdentry->d_inode;

	err = nfserr_perm;
	if (!flen || isdotent(fname, flen) || !tlen || isdotent(tname, tlen))
		goto out;

	host_err = fh_want_write(ffhp);
	if (host_err) {
		err = nfserrno(host_err);
		goto out;
	}

	/* cannot use fh_lock as we need deadlock protective ordering
	 * so do it by hand */
	trap = lock_rename(tdentry, fdentry);
	ffhp->fh_locked = tfhp->fh_locked = 1;
	fill_pre_wcc(ffhp);
	fill_pre_wcc(tfhp);

	odentry = lookup_one_len(fname, fdentry, flen);
	host_err = PTR_ERR(odentry);
	if (IS_ERR(odentry))
		goto out_nfserr;

	host_err = -ENOENT;
	if (!odentry->d_inode)
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

	host_err = vfs_rename(fdir, odentry, tdir, ndentry, NULL, 0);
	if (!host_err) {
		host_err = commit_metadata(tfhp);
		if (!host_err)
			host_err = commit_metadata(ffhp);
	}
 out_dput_new:
	dput(ndentry);
 out_dput_old:
	dput(odentry);
 out_nfserr:
	err = nfserrno(host_err);
	/*
	 * We cannot rely on fh_unlock on the two filehandles,
	 * as that would do the wrong thing if the two directories
	 * were the same, so again we do it by hand.
	 */
	fill_post_wcc(ffhp);
	fill_post_wcc(tfhp);
	unlock_rename(tdentry, fdentry);
	ffhp->fh_locked = tfhp->fh_locked = 0;
	fh_drop_write(ffhp);

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

	fh_lock_nested(fhp, I_MUTEX_PARENT);
	dentry = fhp->fh_dentry;
	dirp = dentry->d_inode;

	rdentry = lookup_one_len(fname, dentry, flen);
	host_err = PTR_ERR(rdentry);
	if (IS_ERR(rdentry))
		goto out_nfserr;

	if (!rdentry->d_inode) {
		dput(rdentry);
		err = nfserr_noent;
		goto out;
	}

	if (!type)
		type = rdentry->d_inode->i_mode & S_IFMT;

	if (type != S_IFDIR)
		host_err = vfs_unlink(dirp, rdentry, NULL);
	else
		host_err = vfs_rmdir(dirp, rdentry);
	if (!host_err)
		host_err = commit_metadata(fhp);
	dput(rdentry);

out_nfserr:
	err = nfserrno(host_err);
out:
	return err;
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

static int nfsd_buffered_filldir(void *__buf, const char *name, int namlen,
				 loff_t offset, u64 ino, unsigned int d_type)
{
	struct readdir_data *buf = __buf;
	struct buffered_dirent *de = (void *)(buf->dirent + buf->used);
	unsigned int reclen;

	reclen = ALIGN(sizeof(struct buffered_dirent) + namlen, sizeof(u64));
	if (buf->used + reclen > PAGE_SIZE) {
		buf->full = 1;
		return -EINVAL;
	}

	de->namlen = namlen;
	de->offset = offset;
	de->ino = ino;
	de->d_type = d_type;
	memcpy(de->name, name, namlen);
	buf->used += reclen;

	return 0;
}

static __be32 nfsd_buffered_readdir(struct file *file, filldir_t func,
				    struct readdir_cd *cdp, loff_t *offsetp)
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
		struct inode *dir_inode = file_inode(file);
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

		/*
		 * Various filldir functions may end up calling back into
		 * lookup_one_len() and the file system's ->lookup() method.
		 * These expect i_mutex to be held, as it would within readdir.
		 */
		host_err = mutex_lock_killable(&dir_inode->i_mutex);
		if (host_err)
			break;

		de = (struct buffered_dirent *)buf.dirent;
		while (size > 0) {
			offset = de->offset;

			if (func(cdp, de->name, de->namlen, de->offset,
				 de->ino, de->d_type))
				break;

			if (cdp->err != nfs_ok)
				break;

			reclen = ALIGN(sizeof(*de) + de->namlen,
				       sizeof(u64));
			size -= reclen;
			de = (struct buffered_dirent *)((char *)de + reclen);
		}
		mutex_unlock(&dir_inode->i_mutex);
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
	     struct readdir_cd *cdp, filldir_t func)
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

	err = nfsd_buffered_readdir(file, func, cdp, offsetp);

	if (err == nfserr_eof || err == nfserr_toosmall)
		err = nfs_ok; /* can still be found in ->err */
out_close:
	nfsd_close(file);
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

/*
 * Check for a user's access permissions to this inode.
 */
__be32
nfsd_permission(struct svc_rqst *rqstp, struct svc_export *exp,
					struct dentry *dentry, int acc)
{
	struct inode	*inode = dentry->d_inode;
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
	err = inode_permission(inode, acc & (MAY_READ|MAY_WRITE|MAY_EXEC));

	/* Allow read access to binaries even when mode 111 */
	if (err == -EACCES && S_ISREG(inode->i_mode) &&
	     (acc == (NFSD_MAY_READ | NFSD_MAY_OWNER_OVERRIDE) ||
	      acc == (NFSD_MAY_READ | NFSD_MAY_READ_IF_EXEC)))
		err = inode_permission(inode, MAY_EXEC);

	return err? nfserrno(err) : 0;
}

void
nfsd_racache_shutdown(void)
{
	struct raparms *raparm, *last_raparm;
	unsigned int i;

	dprintk("nfsd: freeing readahead buffers.\n");

	for (i = 0; i < RAPARM_HASH_SIZE; i++) {
		raparm = raparm_hash[i].pb_head;
		while(raparm) {
			last_raparm = raparm;
			raparm = raparm->p_next;
			kfree(last_raparm);
		}
		raparm_hash[i].pb_head = NULL;
	}
}
/*
 * Initialize readahead param cache
 */
int
nfsd_racache_init(int cache_size)
{
	int	i;
	int	j = 0;
	int	nperbucket;
	struct raparms **raparm = NULL;


	if (raparm_hash[0].pb_head)
		return 0;
	nperbucket = DIV_ROUND_UP(cache_size, RAPARM_HASH_SIZE);
	if (nperbucket < 2)
		nperbucket = 2;
	cache_size = nperbucket * RAPARM_HASH_SIZE;

	dprintk("nfsd: allocating %d readahead buffers.\n", cache_size);

	for (i = 0; i < RAPARM_HASH_SIZE; i++) {
		spin_lock_init(&raparm_hash[i].pb_lock);

		raparm = &raparm_hash[i].pb_head;
		for (j = 0; j < nperbucket; j++) {
			*raparm = kzalloc(sizeof(struct raparms), GFP_KERNEL);
			if (!*raparm)
				goto out_nomem;
			raparm = &(*raparm)->p_next;
		}
		*raparm = NULL;
	}

	nfsdstats.ra_size = cache_size;
	return 0;

out_nomem:
	dprintk("nfsd: kmalloc failed, freeing readahead buffers\n");
	nfsd_racache_shutdown();
	return -ENOMEM;
}
