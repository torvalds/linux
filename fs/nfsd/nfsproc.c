// SPDX-License-Identifier: GPL-2.0
/*
 * Process version 2 NFS requests.
 *
 * Copyright (C) 1995-1997 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/namei.h>

#include "cache.h"
#include "xdr.h"
#include "vfs.h"

typedef struct svc_rqst	svc_rqst;
typedef struct svc_buf	svc_buf;

#define NFSDDBG_FACILITY		NFSDDBG_PROC


static __be32
nfsd_proc_null(struct svc_rqst *rqstp)
{
	return nfs_ok;
}

static __be32
nfsd_return_attrs(__be32 err, struct nfsd_attrstat *resp)
{
	if (err) return err;
	return fh_getattr(&resp->fh, &resp->stat);
}
static __be32
nfsd_return_dirop(__be32 err, struct nfsd_diropres *resp)
{
	if (err) return err;
	return fh_getattr(&resp->fh, &resp->stat);
}
/*
 * Get a file's attributes
 * N.B. After this call resp->fh needs an fh_put
 */
static __be32
nfsd_proc_getattr(struct svc_rqst *rqstp)
{
	struct nfsd_fhandle *argp = rqstp->rq_argp;
	struct nfsd_attrstat *resp = rqstp->rq_resp;
	__be32 nfserr;
	dprintk("nfsd: GETATTR  %s\n", SVCFH_fmt(&argp->fh));

	fh_copy(&resp->fh, &argp->fh);
	nfserr = fh_verify(rqstp, &resp->fh, 0,
			NFSD_MAY_NOP | NFSD_MAY_BYPASS_GSS_ON_ROOT);
	return nfsd_return_attrs(nfserr, resp);
}

/*
 * Set a file's attributes
 * N.B. After this call resp->fh needs an fh_put
 */
static __be32
nfsd_proc_setattr(struct svc_rqst *rqstp)
{
	struct nfsd_sattrargs *argp = rqstp->rq_argp;
	struct nfsd_attrstat *resp = rqstp->rq_resp;
	struct iattr *iap = &argp->attrs;
	struct svc_fh *fhp;
	__be32 nfserr;

	dprintk("nfsd: SETATTR  %s, valid=%x, size=%ld\n",
		SVCFH_fmt(&argp->fh),
		argp->attrs.ia_valid, (long) argp->attrs.ia_size);

	fhp = fh_copy(&resp->fh, &argp->fh);

	/*
	 * NFSv2 does not differentiate between "set-[ac]time-to-now"
	 * which only requires access, and "set-[ac]time-to-X" which
	 * requires ownership.
	 * So if it looks like it might be "set both to the same time which
	 * is close to now", and if setattr_prepare fails, then we
	 * convert to "set to now" instead of "set to explicit time"
	 *
	 * We only call setattr_prepare as the last test as technically
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

		nfserr = fh_verify(rqstp, fhp, 0, NFSD_MAY_NOP);
		if (nfserr)
			goto done;

		if (delta < 0)
			delta = -delta;
		if (delta < MAX_TOUCH_TIME_ERROR &&
		    setattr_prepare(fhp->fh_dentry, iap) != 0) {
			/*
			 * Turn off ATTR_[AM]TIME_SET but leave ATTR_[AM]TIME.
			 * This will cause notify_change to set these times
			 * to "now"
			 */
			iap->ia_valid &= ~BOTH_TIME_SET;
		}
	}

	nfserr = nfsd_setattr(rqstp, fhp, iap, 0, (time_t)0);
done:
	return nfsd_return_attrs(nfserr, resp);
}

/*
 * Look up a path name component
 * Note: the dentry in the resp->fh may be negative if the file
 * doesn't exist yet.
 * N.B. After this call resp->fh needs an fh_put
 */
static __be32
nfsd_proc_lookup(struct svc_rqst *rqstp)
{
	struct nfsd_diropargs *argp = rqstp->rq_argp;
	struct nfsd_diropres *resp = rqstp->rq_resp;
	__be32	nfserr;

	dprintk("nfsd: LOOKUP   %s %.*s\n",
		SVCFH_fmt(&argp->fh), argp->len, argp->name);

	fh_init(&resp->fh, NFS_FHSIZE);
	nfserr = nfsd_lookup(rqstp, &argp->fh, argp->name, argp->len,
				 &resp->fh);

	fh_put(&argp->fh);
	return nfsd_return_dirop(nfserr, resp);
}

/*
 * Read a symlink.
 */
static __be32
nfsd_proc_readlink(struct svc_rqst *rqstp)
{
	struct nfsd_readlinkargs *argp = rqstp->rq_argp;
	struct nfsd_readlinkres *resp = rqstp->rq_resp;
	__be32	nfserr;

	dprintk("nfsd: READLINK %s\n", SVCFH_fmt(&argp->fh));

	/* Read the symlink. */
	resp->len = NFS_MAXPATHLEN;
	nfserr = nfsd_readlink(rqstp, &argp->fh, argp->buffer, &resp->len);

	fh_put(&argp->fh);
	return nfserr;
}

/*
 * Read a portion of a file.
 * N.B. After this call resp->fh needs an fh_put
 */
static __be32
nfsd_proc_read(struct svc_rqst *rqstp)
{
	struct nfsd_readargs *argp = rqstp->rq_argp;
	struct nfsd_readres *resp = rqstp->rq_resp;
	__be32	nfserr;
	u32 eof;

	dprintk("nfsd: READ    %s %d bytes at %d\n",
		SVCFH_fmt(&argp->fh),
		argp->count, argp->offset);

	/* Obtain buffer pointer for payload. 19 is 1 word for
	 * status, 17 words for fattr, and 1 word for the byte count.
	 */

	if (NFSSVC_MAXBLKSIZE_V2 < argp->count) {
		char buf[RPC_MAX_ADDRBUFLEN];
		printk(KERN_NOTICE
			"oversized read request from %s (%d bytes)\n",
				svc_print_addr(rqstp, buf, sizeof(buf)),
				argp->count);
		argp->count = NFSSVC_MAXBLKSIZE_V2;
	}
	svc_reserve_auth(rqstp, (19<<2) + argp->count + 4);

	resp->count = argp->count;
	nfserr = nfsd_read(rqstp, fh_copy(&resp->fh, &argp->fh),
				  argp->offset,
			   	  rqstp->rq_vec, argp->vlen,
				  &resp->count,
				  &eof);

	if (nfserr) return nfserr;
	return fh_getattr(&resp->fh, &resp->stat);
}

/*
 * Write data to a file
 * N.B. After this call resp->fh needs an fh_put
 */
static __be32
nfsd_proc_write(struct svc_rqst *rqstp)
{
	struct nfsd_writeargs *argp = rqstp->rq_argp;
	struct nfsd_attrstat *resp = rqstp->rq_resp;
	__be32	nfserr;
	unsigned long cnt = argp->len;
	unsigned int nvecs;

	dprintk("nfsd: WRITE    %s %d bytes at %d\n",
		SVCFH_fmt(&argp->fh),
		argp->len, argp->offset);

	nvecs = svc_fill_write_vector(rqstp, rqstp->rq_arg.pages,
				      &argp->first, cnt);
	if (!nvecs)
		return nfserr_io;
	nfserr = nfsd_write(rqstp, fh_copy(&resp->fh, &argp->fh),
			    argp->offset, rqstp->rq_vec, nvecs,
			    &cnt, NFS_DATA_SYNC);
	return nfsd_return_attrs(nfserr, resp);
}

/*
 * CREATE processing is complicated. The keyword here is `overloaded.'
 * The parent directory is kept locked between the check for existence
 * and the actual create() call in compliance with VFS protocols.
 * N.B. After this call _both_ argp->fh and resp->fh need an fh_put
 */
static __be32
nfsd_proc_create(struct svc_rqst *rqstp)
{
	struct nfsd_createargs *argp = rqstp->rq_argp;
	struct nfsd_diropres *resp = rqstp->rq_resp;
	svc_fh		*dirfhp = &argp->fh;
	svc_fh		*newfhp = &resp->fh;
	struct iattr	*attr = &argp->attrs;
	struct inode	*inode;
	struct dentry	*dchild;
	int		type, mode;
	__be32		nfserr;
	int		hosterr;
	dev_t		rdev = 0, wanted = new_decode_dev(attr->ia_size);

	dprintk("nfsd: CREATE   %s %.*s\n",
		SVCFH_fmt(dirfhp), argp->len, argp->name);

	/* First verify the parent file handle */
	nfserr = fh_verify(rqstp, dirfhp, S_IFDIR, NFSD_MAY_EXEC);
	if (nfserr)
		goto done; /* must fh_put dirfhp even on error */

	/* Check for NFSD_MAY_WRITE in nfsd_create if necessary */

	nfserr = nfserr_exist;
	if (isdotent(argp->name, argp->len))
		goto done;
	hosterr = fh_want_write(dirfhp);
	if (hosterr) {
		nfserr = nfserrno(hosterr);
		goto done;
	}

	fh_lock_nested(dirfhp, I_MUTEX_PARENT);
	dchild = lookup_one_len(argp->name, dirfhp->fh_dentry, argp->len);
	if (IS_ERR(dchild)) {
		nfserr = nfserrno(PTR_ERR(dchild));
		goto out_unlock;
	}
	fh_init(newfhp, NFS_FHSIZE);
	nfserr = fh_compose(newfhp, dirfhp->fh_export, dchild, dirfhp);
	if (!nfserr && d_really_is_negative(dchild))
		nfserr = nfserr_noent;
	dput(dchild);
	if (nfserr) {
		if (nfserr != nfserr_noent)
			goto out_unlock;
		/*
		 * If the new file handle wasn't verified, we can't tell
		 * whether the file exists or not. Time to bail ...
		 */
		nfserr = nfserr_acces;
		if (!newfhp->fh_dentry) {
			printk(KERN_WARNING 
				"nfsd_proc_create: file handle not verified\n");
			goto out_unlock;
		}
	}

	inode = d_inode(newfhp->fh_dentry);

	/* Unfudge the mode bits */
	if (attr->ia_valid & ATTR_MODE) {
		type = attr->ia_mode & S_IFMT;
		mode = attr->ia_mode & ~S_IFMT;
		if (!type) {
			/* no type, so if target exists, assume same as that,
			 * else assume a file */
			if (inode) {
				type = inode->i_mode & S_IFMT;
				switch(type) {
				case S_IFCHR:
				case S_IFBLK:
					/* reserve rdev for later checking */
					rdev = inode->i_rdev;
					attr->ia_valid |= ATTR_SIZE;

					/* FALLTHROUGH */
				case S_IFIFO:
					/* this is probably a permission check..
					 * at least IRIX implements perm checking on
					 *   echo thing > device-special-file-or-pipe
					 * by doing a CREATE with type==0
					 */
					nfserr = nfsd_permission(rqstp,
								 newfhp->fh_export,
								 newfhp->fh_dentry,
								 NFSD_MAY_WRITE|NFSD_MAY_LOCAL_ACCESS);
					if (nfserr && nfserr != nfserr_rofs)
						goto out_unlock;
				}
			} else
				type = S_IFREG;
		}
	} else if (inode) {
		type = inode->i_mode & S_IFMT;
		mode = inode->i_mode & ~S_IFMT;
	} else {
		type = S_IFREG;
		mode = 0;	/* ??? */
	}

	attr->ia_valid |= ATTR_MODE;
	attr->ia_mode = mode;

	/* Special treatment for non-regular files according to the
	 * gospel of sun micro
	 */
	if (type != S_IFREG) {
		if (type != S_IFBLK && type != S_IFCHR) {
			rdev = 0;
		} else if (type == S_IFCHR && !(attr->ia_valid & ATTR_SIZE)) {
			/* If you think you've seen the worst, grok this. */
			type = S_IFIFO;
		} else {
			/* Okay, char or block special */
			if (!rdev)
				rdev = wanted;
		}

		/* we've used the SIZE information, so discard it */
		attr->ia_valid &= ~ATTR_SIZE;

		/* Make sure the type and device matches */
		nfserr = nfserr_exist;
		if (inode && type != (inode->i_mode & S_IFMT))
			goto out_unlock;
	}

	nfserr = 0;
	if (!inode) {
		/* File doesn't exist. Create it and set attrs */
		nfserr = nfsd_create_locked(rqstp, dirfhp, argp->name,
					argp->len, attr, type, rdev, newfhp);
	} else if (type == S_IFREG) {
		dprintk("nfsd:   existing %s, valid=%x, size=%ld\n",
			argp->name, attr->ia_valid, (long) attr->ia_size);
		/* File already exists. We ignore all attributes except
		 * size, so that creat() behaves exactly like
		 * open(..., O_CREAT|O_TRUNC|O_WRONLY).
		 */
		attr->ia_valid &= ATTR_SIZE;
		if (attr->ia_valid)
			nfserr = nfsd_setattr(rqstp, newfhp, attr, 0, (time_t)0);
	}

out_unlock:
	/* We don't really need to unlock, as fh_put does it. */
	fh_unlock(dirfhp);
	fh_drop_write(dirfhp);
done:
	fh_put(dirfhp);
	return nfsd_return_dirop(nfserr, resp);
}

static __be32
nfsd_proc_remove(struct svc_rqst *rqstp)
{
	struct nfsd_diropargs *argp = rqstp->rq_argp;
	__be32	nfserr;

	dprintk("nfsd: REMOVE   %s %.*s\n", SVCFH_fmt(&argp->fh),
		argp->len, argp->name);

	/* Unlink. -SIFDIR means file must not be a directory */
	nfserr = nfsd_unlink(rqstp, &argp->fh, -S_IFDIR, argp->name, argp->len);
	fh_put(&argp->fh);
	return nfserr;
}

static __be32
nfsd_proc_rename(struct svc_rqst *rqstp)
{
	struct nfsd_renameargs *argp = rqstp->rq_argp;
	__be32	nfserr;

	dprintk("nfsd: RENAME   %s %.*s -> \n",
		SVCFH_fmt(&argp->ffh), argp->flen, argp->fname);
	dprintk("nfsd:        ->  %s %.*s\n",
		SVCFH_fmt(&argp->tfh), argp->tlen, argp->tname);

	nfserr = nfsd_rename(rqstp, &argp->ffh, argp->fname, argp->flen,
				    &argp->tfh, argp->tname, argp->tlen);
	fh_put(&argp->ffh);
	fh_put(&argp->tfh);
	return nfserr;
}

static __be32
nfsd_proc_link(struct svc_rqst *rqstp)
{
	struct nfsd_linkargs *argp = rqstp->rq_argp;
	__be32	nfserr;

	dprintk("nfsd: LINK     %s ->\n",
		SVCFH_fmt(&argp->ffh));
	dprintk("nfsd:    %s %.*s\n",
		SVCFH_fmt(&argp->tfh),
		argp->tlen,
		argp->tname);

	nfserr = nfsd_link(rqstp, &argp->tfh, argp->tname, argp->tlen,
				  &argp->ffh);
	fh_put(&argp->ffh);
	fh_put(&argp->tfh);
	return nfserr;
}

static __be32
nfsd_proc_symlink(struct svc_rqst *rqstp)
{
	struct nfsd_symlinkargs *argp = rqstp->rq_argp;
	struct svc_fh	newfh;
	__be32		nfserr;

	if (argp->tlen > NFS_MAXPATHLEN)
		return nfserr_nametoolong;

	argp->tname = svc_fill_symlink_pathname(rqstp, &argp->first,
						page_address(rqstp->rq_arg.pages[0]),
						argp->tlen);
	if (IS_ERR(argp->tname))
		return nfserrno(PTR_ERR(argp->tname));

	dprintk("nfsd: SYMLINK  %s %.*s -> %.*s\n",
		SVCFH_fmt(&argp->ffh), argp->flen, argp->fname,
		argp->tlen, argp->tname);

	fh_init(&newfh, NFS_FHSIZE);
	nfserr = nfsd_symlink(rqstp, &argp->ffh, argp->fname, argp->flen,
						 argp->tname, &newfh);

	kfree(argp->tname);
	fh_put(&argp->ffh);
	fh_put(&newfh);
	return nfserr;
}

/*
 * Make directory. This operation is not idempotent.
 * N.B. After this call resp->fh needs an fh_put
 */
static __be32
nfsd_proc_mkdir(struct svc_rqst *rqstp)
{
	struct nfsd_createargs *argp = rqstp->rq_argp;
	struct nfsd_diropres *resp = rqstp->rq_resp;
	__be32	nfserr;

	dprintk("nfsd: MKDIR    %s %.*s\n", SVCFH_fmt(&argp->fh), argp->len, argp->name);

	if (resp->fh.fh_dentry) {
		printk(KERN_WARNING
			"nfsd_proc_mkdir: response already verified??\n");
	}

	argp->attrs.ia_valid &= ~ATTR_SIZE;
	fh_init(&resp->fh, NFS_FHSIZE);
	nfserr = nfsd_create(rqstp, &argp->fh, argp->name, argp->len,
				    &argp->attrs, S_IFDIR, 0, &resp->fh);
	fh_put(&argp->fh);
	return nfsd_return_dirop(nfserr, resp);
}

/*
 * Remove a directory
 */
static __be32
nfsd_proc_rmdir(struct svc_rqst *rqstp)
{
	struct nfsd_diropargs *argp = rqstp->rq_argp;
	__be32	nfserr;

	dprintk("nfsd: RMDIR    %s %.*s\n", SVCFH_fmt(&argp->fh), argp->len, argp->name);

	nfserr = nfsd_unlink(rqstp, &argp->fh, S_IFDIR, argp->name, argp->len);
	fh_put(&argp->fh);
	return nfserr;
}

/*
 * Read a portion of a directory.
 */
static __be32
nfsd_proc_readdir(struct svc_rqst *rqstp)
{
	struct nfsd_readdirargs *argp = rqstp->rq_argp;
	struct nfsd_readdirres *resp = rqstp->rq_resp;
	int		count;
	__be32		nfserr;
	loff_t		offset;

	dprintk("nfsd: READDIR  %s %d bytes at %d\n",
		SVCFH_fmt(&argp->fh),		
		argp->count, argp->cookie);

	/* Shrink to the client read size */
	count = (argp->count >> 2) - 2;

	/* Make sure we've room for the NULL ptr & eof flag */
	count -= 2;
	if (count < 0)
		count = 0;

	resp->buffer = argp->buffer;
	resp->offset = NULL;
	resp->buflen = count;
	resp->common.err = nfs_ok;
	/* Read directory and encode entries on the fly */
	offset = argp->cookie;
	nfserr = nfsd_readdir(rqstp, &argp->fh, &offset, 
			      &resp->common, nfssvc_encode_entry);

	resp->count = resp->buffer - argp->buffer;
	if (resp->offset)
		*resp->offset = htonl(offset);

	fh_put(&argp->fh);
	return nfserr;
}

/*
 * Get file system info
 */
static __be32
nfsd_proc_statfs(struct svc_rqst *rqstp)
{
	struct nfsd_fhandle *argp = rqstp->rq_argp;
	struct nfsd_statfsres *resp = rqstp->rq_resp;
	__be32	nfserr;

	dprintk("nfsd: STATFS   %s\n", SVCFH_fmt(&argp->fh));

	nfserr = nfsd_statfs(rqstp, &argp->fh, &resp->stats,
			NFSD_MAY_BYPASS_GSS_ON_ROOT);
	fh_put(&argp->fh);
	return nfserr;
}

/*
 * NFSv2 Server procedures.
 * Only the results of non-idempotent operations are cached.
 */
struct nfsd_void { int dummy; };

#define ST 1		/* status */
#define FH 8		/* filehandle */
#define	AT 18		/* attributes */

static const struct svc_procedure nfsd_procedures2[18] = {
	[NFSPROC_NULL] = {
		.pc_func = nfsd_proc_null,
		.pc_decode = nfssvc_decode_void,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_void),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST,
	},
	[NFSPROC_GETATTR] = {
		.pc_func = nfsd_proc_getattr,
		.pc_decode = nfssvc_decode_fhandle,
		.pc_encode = nfssvc_encode_attrstat,
		.pc_release = nfssvc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd_fhandle),
		.pc_ressize = sizeof(struct nfsd_attrstat),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+AT,
	},
	[NFSPROC_SETATTR] = {
		.pc_func = nfsd_proc_setattr,
		.pc_decode = nfssvc_decode_sattrargs,
		.pc_encode = nfssvc_encode_attrstat,
		.pc_release = nfssvc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd_sattrargs),
		.pc_ressize = sizeof(struct nfsd_attrstat),
		.pc_cachetype = RC_REPLBUFF,
		.pc_xdrressize = ST+AT,
	},
	[NFSPROC_ROOT] = {
		.pc_decode = nfssvc_decode_void,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_void),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST,
	},
	[NFSPROC_LOOKUP] = {
		.pc_func = nfsd_proc_lookup,
		.pc_decode = nfssvc_decode_diropargs,
		.pc_encode = nfssvc_encode_diropres,
		.pc_release = nfssvc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd_diropargs),
		.pc_ressize = sizeof(struct nfsd_diropres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+FH+AT,
	},
	[NFSPROC_READLINK] = {
		.pc_func = nfsd_proc_readlink,
		.pc_decode = nfssvc_decode_readlinkargs,
		.pc_encode = nfssvc_encode_readlinkres,
		.pc_argsize = sizeof(struct nfsd_readlinkargs),
		.pc_ressize = sizeof(struct nfsd_readlinkres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+1+NFS_MAXPATHLEN/4,
	},
	[NFSPROC_READ] = {
		.pc_func = nfsd_proc_read,
		.pc_decode = nfssvc_decode_readargs,
		.pc_encode = nfssvc_encode_readres,
		.pc_release = nfssvc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd_readargs),
		.pc_ressize = sizeof(struct nfsd_readres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+AT+1+NFSSVC_MAXBLKSIZE_V2/4,
	},
	[NFSPROC_WRITECACHE] = {
		.pc_decode = nfssvc_decode_void,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_void),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST,
	},
	[NFSPROC_WRITE] = {
		.pc_func = nfsd_proc_write,
		.pc_decode = nfssvc_decode_writeargs,
		.pc_encode = nfssvc_encode_attrstat,
		.pc_release = nfssvc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd_writeargs),
		.pc_ressize = sizeof(struct nfsd_attrstat),
		.pc_cachetype = RC_REPLBUFF,
		.pc_xdrressize = ST+AT,
	},
	[NFSPROC_CREATE] = {
		.pc_func = nfsd_proc_create,
		.pc_decode = nfssvc_decode_createargs,
		.pc_encode = nfssvc_encode_diropres,
		.pc_release = nfssvc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd_createargs),
		.pc_ressize = sizeof(struct nfsd_diropres),
		.pc_cachetype = RC_REPLBUFF,
		.pc_xdrressize = ST+FH+AT,
	},
	[NFSPROC_REMOVE] = {
		.pc_func = nfsd_proc_remove,
		.pc_decode = nfssvc_decode_diropargs,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_diropargs),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_REPLSTAT,
		.pc_xdrressize = ST,
	},
	[NFSPROC_RENAME] = {
		.pc_func = nfsd_proc_rename,
		.pc_decode = nfssvc_decode_renameargs,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_renameargs),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_REPLSTAT,
		.pc_xdrressize = ST,
	},
	[NFSPROC_LINK] = {
		.pc_func = nfsd_proc_link,
		.pc_decode = nfssvc_decode_linkargs,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_linkargs),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_REPLSTAT,
		.pc_xdrressize = ST,
	},
	[NFSPROC_SYMLINK] = {
		.pc_func = nfsd_proc_symlink,
		.pc_decode = nfssvc_decode_symlinkargs,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_symlinkargs),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_REPLSTAT,
		.pc_xdrressize = ST,
	},
	[NFSPROC_MKDIR] = {
		.pc_func = nfsd_proc_mkdir,
		.pc_decode = nfssvc_decode_createargs,
		.pc_encode = nfssvc_encode_diropres,
		.pc_release = nfssvc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd_createargs),
		.pc_ressize = sizeof(struct nfsd_diropres),
		.pc_cachetype = RC_REPLBUFF,
		.pc_xdrressize = ST+FH+AT,
	},
	[NFSPROC_RMDIR] = {
		.pc_func = nfsd_proc_rmdir,
		.pc_decode = nfssvc_decode_diropargs,
		.pc_encode = nfssvc_encode_void,
		.pc_argsize = sizeof(struct nfsd_diropargs),
		.pc_ressize = sizeof(struct nfsd_void),
		.pc_cachetype = RC_REPLSTAT,
		.pc_xdrressize = ST,
	},
	[NFSPROC_READDIR] = {
		.pc_func = nfsd_proc_readdir,
		.pc_decode = nfssvc_decode_readdirargs,
		.pc_encode = nfssvc_encode_readdirres,
		.pc_argsize = sizeof(struct nfsd_readdirargs),
		.pc_ressize = sizeof(struct nfsd_readdirres),
		.pc_cachetype = RC_NOCACHE,
	},
	[NFSPROC_STATFS] = {
		.pc_func = nfsd_proc_statfs,
		.pc_decode = nfssvc_decode_fhandle,
		.pc_encode = nfssvc_encode_statfsres,
		.pc_argsize = sizeof(struct nfsd_fhandle),
		.pc_ressize = sizeof(struct nfsd_statfsres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+5,
	},
};


static unsigned int nfsd_count2[ARRAY_SIZE(nfsd_procedures2)];
const struct svc_version nfsd_version2 = {
	.vs_vers	= 2,
	.vs_nproc	= 18,
	.vs_proc	= nfsd_procedures2,
	.vs_count	= nfsd_count2,
	.vs_dispatch	= nfsd_dispatch,
	.vs_xdrsize	= NFS2_SVC_XDRSIZE,
};

/*
 * Map errnos to NFS errnos.
 */
__be32
nfserrno (int errno)
{
	static struct {
		__be32	nfserr;
		int	syserr;
	} nfs_errtbl[] = {
		{ nfs_ok, 0 },
		{ nfserr_perm, -EPERM },
		{ nfserr_noent, -ENOENT },
		{ nfserr_io, -EIO },
		{ nfserr_nxio, -ENXIO },
		{ nfserr_fbig, -E2BIG },
		{ nfserr_acces, -EACCES },
		{ nfserr_exist, -EEXIST },
		{ nfserr_xdev, -EXDEV },
		{ nfserr_mlink, -EMLINK },
		{ nfserr_nodev, -ENODEV },
		{ nfserr_notdir, -ENOTDIR },
		{ nfserr_isdir, -EISDIR },
		{ nfserr_inval, -EINVAL },
		{ nfserr_fbig, -EFBIG },
		{ nfserr_nospc, -ENOSPC },
		{ nfserr_rofs, -EROFS },
		{ nfserr_mlink, -EMLINK },
		{ nfserr_nametoolong, -ENAMETOOLONG },
		{ nfserr_notempty, -ENOTEMPTY },
#ifdef EDQUOT
		{ nfserr_dquot, -EDQUOT },
#endif
		{ nfserr_stale, -ESTALE },
		{ nfserr_jukebox, -ETIMEDOUT },
		{ nfserr_jukebox, -ERESTARTSYS },
		{ nfserr_jukebox, -EAGAIN },
		{ nfserr_jukebox, -EWOULDBLOCK },
		{ nfserr_jukebox, -ENOMEM },
		{ nfserr_io, -ETXTBSY },
		{ nfserr_notsupp, -EOPNOTSUPP },
		{ nfserr_toosmall, -ETOOSMALL },
		{ nfserr_serverfault, -ESERVERFAULT },
		{ nfserr_serverfault, -ENFILE },
		{ nfserr_io, -EUCLEAN },
		{ nfserr_perm, -ENOKEY },
	};
	int	i;

	for (i = 0; i < ARRAY_SIZE(nfs_errtbl); i++) {
		if (nfs_errtbl[i].syserr == errno)
			return nfs_errtbl[i].nfserr;
	}
	WARN_ONCE(1, "nfsd: non-standard errno: %d\n", errno);
	return nfserr_io;
}

