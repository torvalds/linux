/*
 * Process version 3 NFSACL requests.
 *
 * Copyright (C) 2002-2003 Andreas Gruenbacher <agruen@suse.de>
 */

#include "nfsd.h"
/* FIXME: nfsacl.h is a broken header */
#include <linux/nfsacl.h>
#include <linux/gfp.h>
#include "cache.h"
#include "xdr3.h"
#include "vfs.h"

#define RETURN_STATUS(st)	{ resp->status = (st); return (st); }

/*
 * NULL call.
 */
static __be32
nfsd3_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	return nfs_ok;
}

/*
 * Get the Access and/or Default ACL of a file.
 */
static __be32 nfsd3_proc_getacl(struct svc_rqst * rqstp,
		struct nfsd3_getaclargs *argp, struct nfsd3_getaclres *resp)
{
	struct posix_acl *acl;
	struct inode *inode;
	svc_fh *fh;
	__be32 nfserr = 0;

	fh = fh_copy(&resp->fh, &argp->fh);
	nfserr = fh_verify(rqstp, &resp->fh, 0, NFSD_MAY_NOP);
	if (nfserr)
		RETURN_STATUS(nfserr);

	inode = d_inode(fh->fh_dentry);

	if (argp->mask & ~NFS_ACL_MASK)
		RETURN_STATUS(nfserr_inval);
	resp->mask = argp->mask;

	if (resp->mask & (NFS_ACL|NFS_ACLCNT)) {
		acl = get_acl(inode, ACL_TYPE_ACCESS);
		if (acl == NULL) {
			/* Solaris returns the inode's minimum ACL. */
			acl = posix_acl_from_mode(inode->i_mode, GFP_KERNEL);
		}
		if (IS_ERR(acl)) {
			nfserr = nfserrno(PTR_ERR(acl));
			goto fail;
		}
		resp->acl_access = acl;
	}
	if (resp->mask & (NFS_DFACL|NFS_DFACLCNT)) {
		/* Check how Solaris handles requests for the Default ACL
		   of a non-directory! */
		acl = get_acl(inode, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl)) {
			nfserr = nfserrno(PTR_ERR(acl));
			goto fail;
		}
		resp->acl_default = acl;
	}

	/* resp->acl_{access,default} are released in nfs3svc_release_getacl. */
	RETURN_STATUS(0);

fail:
	posix_acl_release(resp->acl_access);
	posix_acl_release(resp->acl_default);
	RETURN_STATUS(nfserr);
}

/*
 * Set the Access and/or Default ACL of a file.
 */
static __be32 nfsd3_proc_setacl(struct svc_rqst * rqstp,
		struct nfsd3_setaclargs *argp,
		struct nfsd3_attrstat *resp)
{
	struct inode *inode;
	svc_fh *fh;
	__be32 nfserr = 0;
	int error;

	fh = fh_copy(&resp->fh, &argp->fh);
	nfserr = fh_verify(rqstp, &resp->fh, 0, NFSD_MAY_SATTR);
	if (nfserr)
		goto out;

	inode = d_inode(fh->fh_dentry);

	error = fh_want_write(fh);
	if (error)
		goto out_errno;

	fh_lock(fh);

	error = set_posix_acl(inode, ACL_TYPE_ACCESS, argp->acl_access);
	if (error)
		goto out_drop_lock;
	error = set_posix_acl(inode, ACL_TYPE_DEFAULT, argp->acl_default);

out_drop_lock:
	fh_unlock(fh);
	fh_drop_write(fh);
out_errno:
	nfserr = nfserrno(error);
out:
	/* argp->acl_{access,default} may have been allocated in
	   nfs3svc_decode_setaclargs. */
	posix_acl_release(argp->acl_access);
	posix_acl_release(argp->acl_default);
	RETURN_STATUS(nfserr);
}

/*
 * XDR decode functions
 */
static int nfs3svc_decode_getaclargs(struct svc_rqst *rqstp, __be32 *p,
		struct nfsd3_getaclargs *args)
{
	p = nfs3svc_decode_fh(p, &args->fh);
	if (!p)
		return 0;
	args->mask = ntohl(*p); p++;

	return xdr_argsize_check(rqstp, p);
}


static int nfs3svc_decode_setaclargs(struct svc_rqst *rqstp, __be32 *p,
		struct nfsd3_setaclargs *args)
{
	struct kvec *head = rqstp->rq_arg.head;
	unsigned int base;
	int n;

	p = nfs3svc_decode_fh(p, &args->fh);
	if (!p)
		return 0;
	args->mask = ntohl(*p++);
	if (args->mask & ~NFS_ACL_MASK ||
	    !xdr_argsize_check(rqstp, p))
		return 0;

	base = (char *)p - (char *)head->iov_base;
	n = nfsacl_decode(&rqstp->rq_arg, base, NULL,
			  (args->mask & NFS_ACL) ?
			  &args->acl_access : NULL);
	if (n > 0)
		n = nfsacl_decode(&rqstp->rq_arg, base + n, NULL,
				  (args->mask & NFS_DFACL) ?
				  &args->acl_default : NULL);
	return (n > 0);
}

/*
 * XDR encode functions
 */

/* GETACL */
static int nfs3svc_encode_getaclres(struct svc_rqst *rqstp, __be32 *p,
		struct nfsd3_getaclres *resp)
{
	struct dentry *dentry = resp->fh.fh_dentry;

	p = nfs3svc_encode_post_op_attr(rqstp, p, &resp->fh);
	if (resp->status == 0 && dentry && d_really_is_positive(dentry)) {
		struct inode *inode = d_inode(dentry);
		struct kvec *head = rqstp->rq_res.head;
		unsigned int base;
		int n;
		int w;

		*p++ = htonl(resp->mask);
		if (!xdr_ressize_check(rqstp, p))
			return 0;
		base = (char *)p - (char *)head->iov_base;

		rqstp->rq_res.page_len = w = nfsacl_size(
			(resp->mask & NFS_ACL)   ? resp->acl_access  : NULL,
			(resp->mask & NFS_DFACL) ? resp->acl_default : NULL);
		while (w > 0) {
			if (!*(rqstp->rq_next_page++))
				return 0;
			w -= PAGE_SIZE;
		}

		n = nfsacl_encode(&rqstp->rq_res, base, inode,
				  resp->acl_access,
				  resp->mask & NFS_ACL, 0);
		if (n > 0)
			n = nfsacl_encode(&rqstp->rq_res, base + n, inode,
					  resp->acl_default,
					  resp->mask & NFS_DFACL,
					  NFS_ACL_DEFAULT);
		if (n <= 0)
			return 0;
	} else
		if (!xdr_ressize_check(rqstp, p))
			return 0;

	return 1;
}

/* SETACL */
static int nfs3svc_encode_setaclres(struct svc_rqst *rqstp, __be32 *p,
		struct nfsd3_attrstat *resp)
{
	p = nfs3svc_encode_post_op_attr(rqstp, p, &resp->fh);

	return xdr_ressize_check(rqstp, p);
}

/*
 * XDR release functions
 */
static int nfs3svc_release_getacl(struct svc_rqst *rqstp, __be32 *p,
		struct nfsd3_getaclres *resp)
{
	fh_put(&resp->fh);
	posix_acl_release(resp->acl_access);
	posix_acl_release(resp->acl_default);
	return 1;
}

#define nfs3svc_decode_voidargs		NULL
#define nfs3svc_release_void		NULL
#define nfsd3_setaclres			nfsd3_attrstat
#define nfsd3_voidres			nfsd3_voidargs
struct nfsd3_voidargs { int dummy; };

#define PROC(name, argt, rest, relt, cache, respsize)	\
 { (svc_procfunc) nfsd3_proc_##name,		\
   (kxdrproc_t) nfs3svc_decode_##argt##args,	\
   (kxdrproc_t) nfs3svc_encode_##rest##res,	\
   (kxdrproc_t) nfs3svc_release_##relt,		\
   sizeof(struct nfsd3_##argt##args),		\
   sizeof(struct nfsd3_##rest##res),		\
   0,						\
   cache,					\
   respsize,					\
 }

#define ST 1		/* status*/
#define AT 21		/* attributes */
#define pAT (1+AT)	/* post attributes - conditional */
#define ACL (1+NFS_ACL_MAX_ENTRIES*3)  /* Access Control List */

static struct svc_procedure		nfsd_acl_procedures3[] = {
  PROC(null,	void,		void,		void,	  RC_NOCACHE, ST),
  PROC(getacl,	getacl,		getacl,		getacl,	  RC_NOCACHE, ST+1+2*(1+ACL)),
  PROC(setacl,	setacl,		setacl,		fhandle,  RC_NOCACHE, ST+pAT),
};

struct svc_version	nfsd_acl_version3 = {
		.vs_vers	= 3,
		.vs_nproc	= 3,
		.vs_proc	= nfsd_acl_procedures3,
		.vs_dispatch	= nfsd_dispatch,
		.vs_xdrsize	= NFS3_SVC_XDRSIZE,
};

