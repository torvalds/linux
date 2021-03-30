// SPDX-License-Identifier: GPL-2.0
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

/*
 * NULL call.
 */
static __be32
nfsd3_proc_null(struct svc_rqst *rqstp)
{
	return rpc_success;
}

/*
 * Get the Access and/or Default ACL of a file.
 */
static __be32 nfsd3_proc_getacl(struct svc_rqst *rqstp)
{
	struct nfsd3_getaclargs *argp = rqstp->rq_argp;
	struct nfsd3_getaclres *resp = rqstp->rq_resp;
	struct posix_acl *acl;
	struct inode *inode;
	svc_fh *fh;

	fh = fh_copy(&resp->fh, &argp->fh);
	resp->status = fh_verify(rqstp, &resp->fh, 0, NFSD_MAY_NOP);
	if (resp->status != nfs_ok)
		goto out;

	inode = d_inode(fh->fh_dentry);

	if (argp->mask & ~NFS_ACL_MASK) {
		resp->status = nfserr_inval;
		goto out;
	}
	resp->mask = argp->mask;

	if (resp->mask & (NFS_ACL|NFS_ACLCNT)) {
		acl = get_acl(inode, ACL_TYPE_ACCESS);
		if (acl == NULL) {
			/* Solaris returns the inode's minimum ACL. */
			acl = posix_acl_from_mode(inode->i_mode, GFP_KERNEL);
		}
		if (IS_ERR(acl)) {
			resp->status = nfserrno(PTR_ERR(acl));
			goto fail;
		}
		resp->acl_access = acl;
	}
	if (resp->mask & (NFS_DFACL|NFS_DFACLCNT)) {
		/* Check how Solaris handles requests for the Default ACL
		   of a non-directory! */
		acl = get_acl(inode, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl)) {
			resp->status = nfserrno(PTR_ERR(acl));
			goto fail;
		}
		resp->acl_default = acl;
	}

	/* resp->acl_{access,default} are released in nfs3svc_release_getacl. */
out:
	return rpc_success;

fail:
	posix_acl_release(resp->acl_access);
	posix_acl_release(resp->acl_default);
	goto out;
}

/*
 * Set the Access and/or Default ACL of a file.
 */
static __be32 nfsd3_proc_setacl(struct svc_rqst *rqstp)
{
	struct nfsd3_setaclargs *argp = rqstp->rq_argp;
	struct nfsd3_attrstat *resp = rqstp->rq_resp;
	struct inode *inode;
	svc_fh *fh;
	int error;

	fh = fh_copy(&resp->fh, &argp->fh);
	resp->status = fh_verify(rqstp, &resp->fh, 0, NFSD_MAY_SATTR);
	if (resp->status != nfs_ok)
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
	resp->status = nfserrno(error);
out:
	/* argp->acl_{access,default} may have been allocated in
	   nfs3svc_decode_setaclargs. */
	posix_acl_release(argp->acl_access);
	posix_acl_release(argp->acl_default);
	return rpc_success;
}

/*
 * XDR decode functions
 */

static int nfs3svc_decode_getaclargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd3_getaclargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_nfs_fh3(xdr, &args->fh))
		return 0;
	if (xdr_stream_decode_u32(xdr, &args->mask) < 0)
		return 0;

	return 1;
}

static int nfs3svc_decode_setaclargs(struct svc_rqst *rqstp, __be32 *p)
{
	struct xdr_stream *xdr = &rqstp->rq_arg_stream;
	struct nfsd3_setaclargs *argp = rqstp->rq_argp;

	if (!svcxdr_decode_nfs_fh3(xdr, &argp->fh))
		return 0;
	if (xdr_stream_decode_u32(xdr, &argp->mask) < 0)
		return 0;
	if (argp->mask & ~NFS_ACL_MASK)
		return 0;
	if (!nfs_stream_decode_acl(xdr, NULL, (argp->mask & NFS_ACL) ?
				   &argp->acl_access : NULL))
		return 0;
	if (!nfs_stream_decode_acl(xdr, NULL, (argp->mask & NFS_DFACL) ?
				   &argp->acl_default : NULL))
		return 0;

	return 1;
}

/*
 * XDR encode functions
 */

/* GETACL */
static int nfs3svc_encode_getaclres(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd3_getaclres *resp = rqstp->rq_resp;
	struct dentry *dentry = resp->fh.fh_dentry;

	*p++ = resp->status;
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
static int nfs3svc_encode_setaclres(struct svc_rqst *rqstp, __be32 *p)
{
	struct nfsd3_attrstat *resp = rqstp->rq_resp;

	*p++ = resp->status;
	p = nfs3svc_encode_post_op_attr(rqstp, p, &resp->fh);
	return xdr_ressize_check(rqstp, p);
}

/*
 * XDR release functions
 */
static void nfs3svc_release_getacl(struct svc_rqst *rqstp)
{
	struct nfsd3_getaclres *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
	posix_acl_release(resp->acl_access);
	posix_acl_release(resp->acl_default);
}

struct nfsd3_voidargs { int dummy; };

#define ST 1		/* status*/
#define AT 21		/* attributes */
#define pAT (1+AT)	/* post attributes - conditional */
#define ACL (1+NFS_ACL_MAX_ENTRIES*3)  /* Access Control List */

static const struct svc_procedure nfsd_acl_procedures3[3] = {
	[ACLPROC3_NULL] = {
		.pc_func = nfsd3_proc_null,
		.pc_decode = nfssvc_decode_voidarg,
		.pc_encode = nfssvc_encode_voidres,
		.pc_argsize = sizeof(struct nfsd_voidargs),
		.pc_ressize = sizeof(struct nfsd_voidres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST,
		.pc_name = "NULL",
	},
	[ACLPROC3_GETACL] = {
		.pc_func = nfsd3_proc_getacl,
		.pc_decode = nfs3svc_decode_getaclargs,
		.pc_encode = nfs3svc_encode_getaclres,
		.pc_release = nfs3svc_release_getacl,
		.pc_argsize = sizeof(struct nfsd3_getaclargs),
		.pc_ressize = sizeof(struct nfsd3_getaclres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+1+2*(1+ACL),
		.pc_name = "GETACL",
	},
	[ACLPROC3_SETACL] = {
		.pc_func = nfsd3_proc_setacl,
		.pc_decode = nfs3svc_decode_setaclargs,
		.pc_encode = nfs3svc_encode_setaclres,
		.pc_release = nfs3svc_release_fhandle,
		.pc_argsize = sizeof(struct nfsd3_setaclargs),
		.pc_ressize = sizeof(struct nfsd3_attrstat),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+pAT,
		.pc_name = "SETACL",
	},
};

static unsigned int nfsd_acl_count3[ARRAY_SIZE(nfsd_acl_procedures3)];
const struct svc_version nfsd_acl_version3 = {
	.vs_vers	= 3,
	.vs_nproc	= 3,
	.vs_proc	= nfsd_acl_procedures3,
	.vs_count	= nfsd_acl_count3,
	.vs_dispatch	= nfsd_dispatch,
	.vs_xdrsize	= NFS3_SVC_XDRSIZE,
};

