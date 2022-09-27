// SPDX-License-Identifier: GPL-2.0
/*
 * Process version 2 NFSACL requests.
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

#define NFSDDBG_FACILITY		NFSDDBG_PROC

/*
 * NULL call.
 */
static __be32
nfsacld_proc_null(struct svc_rqst *rqstp)
{
	return rpc_success;
}

/*
 * Get the Access and/or Default ACL of a file.
 */
static __be32 nfsacld_proc_getacl(struct svc_rqst *rqstp)
{
	struct nfsd3_getaclargs *argp = rqstp->rq_argp;
	struct nfsd3_getaclres *resp = rqstp->rq_resp;
	struct posix_acl *acl;
	struct inode *inode;
	svc_fh *fh;

	dprintk("nfsd: GETACL(2acl)   %s\n", SVCFH_fmt(&argp->fh));

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

	resp->status = fh_getattr(fh, &resp->stat);
	if (resp->status != nfs_ok)
		goto out;

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

	/* resp->acl_{access,default} are released in nfssvc_release_getacl. */
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
static __be32 nfsacld_proc_setacl(struct svc_rqst *rqstp)
{
	struct nfsd3_setaclargs *argp = rqstp->rq_argp;
	struct nfsd_attrstat *resp = rqstp->rq_resp;
	struct inode *inode;
	svc_fh *fh;
	int error;

	dprintk("nfsd: SETACL(2acl)   %s\n", SVCFH_fmt(&argp->fh));

	fh = fh_copy(&resp->fh, &argp->fh);
	resp->status = fh_verify(rqstp, &resp->fh, 0, NFSD_MAY_SATTR);
	if (resp->status != nfs_ok)
		goto out;

	inode = d_inode(fh->fh_dentry);

	error = fh_want_write(fh);
	if (error)
		goto out_errno;

	inode_lock(inode);

	error = set_posix_acl(&init_user_ns, inode, ACL_TYPE_ACCESS,
			      argp->acl_access);
	if (error)
		goto out_drop_lock;
	error = set_posix_acl(&init_user_ns, inode, ACL_TYPE_DEFAULT,
			      argp->acl_default);
	if (error)
		goto out_drop_lock;

	inode_unlock(inode);

	fh_drop_write(fh);

	resp->status = fh_getattr(fh, &resp->stat);

out:
	/* argp->acl_{access,default} may have been allocated in
	   nfssvc_decode_setaclargs. */
	posix_acl_release(argp->acl_access);
	posix_acl_release(argp->acl_default);
	return rpc_success;

out_drop_lock:
	inode_unlock(inode);
	fh_drop_write(fh);
out_errno:
	resp->status = nfserrno(error);
	goto out;
}

/*
 * Check file attributes
 */
static __be32 nfsacld_proc_getattr(struct svc_rqst *rqstp)
{
	struct nfsd_fhandle *argp = rqstp->rq_argp;
	struct nfsd_attrstat *resp = rqstp->rq_resp;

	dprintk("nfsd: GETATTR  %s\n", SVCFH_fmt(&argp->fh));

	fh_copy(&resp->fh, &argp->fh);
	resp->status = fh_verify(rqstp, &resp->fh, 0, NFSD_MAY_NOP);
	if (resp->status != nfs_ok)
		goto out;
	resp->status = fh_getattr(&resp->fh, &resp->stat);
out:
	return rpc_success;
}

/*
 * Check file access
 */
static __be32 nfsacld_proc_access(struct svc_rqst *rqstp)
{
	struct nfsd3_accessargs *argp = rqstp->rq_argp;
	struct nfsd3_accessres *resp = rqstp->rq_resp;

	dprintk("nfsd: ACCESS(2acl)   %s 0x%x\n",
			SVCFH_fmt(&argp->fh),
			argp->access);

	fh_copy(&resp->fh, &argp->fh);
	resp->access = argp->access;
	resp->status = nfsd_access(rqstp, &resp->fh, &resp->access, NULL);
	if (resp->status != nfs_ok)
		goto out;
	resp->status = fh_getattr(&resp->fh, &resp->stat);
out:
	return rpc_success;
}

/*
 * XDR decode functions
 */

static bool
nfsaclsvc_decode_getaclargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_getaclargs *argp = rqstp->rq_argp;

	if (!svcxdr_decode_fhandle(xdr, &argp->fh))
		return false;
	if (xdr_stream_decode_u32(xdr, &argp->mask) < 0)
		return false;

	return true;
}

static bool
nfsaclsvc_decode_setaclargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_setaclargs *argp = rqstp->rq_argp;

	if (!svcxdr_decode_fhandle(xdr, &argp->fh))
		return false;
	if (xdr_stream_decode_u32(xdr, &argp->mask) < 0)
		return false;
	if (argp->mask & ~NFS_ACL_MASK)
		return false;
	if (!nfs_stream_decode_acl(xdr, NULL, (argp->mask & NFS_ACL) ?
				   &argp->acl_access : NULL))
		return false;
	if (!nfs_stream_decode_acl(xdr, NULL, (argp->mask & NFS_DFACL) ?
				   &argp->acl_default : NULL))
		return false;

	return true;
}

static bool
nfsaclsvc_decode_accessargs(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_accessargs *args = rqstp->rq_argp;

	if (!svcxdr_decode_fhandle(xdr, &args->fh))
		return false;
	if (xdr_stream_decode_u32(xdr, &args->access) < 0)
		return false;

	return true;
}

/*
 * XDR encode functions
 */

/* GETACL */
static bool
nfsaclsvc_encode_getaclres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_getaclres *resp = rqstp->rq_resp;
	struct dentry *dentry = resp->fh.fh_dentry;
	struct inode *inode;
	int w;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return false;

	if (dentry == NULL || d_really_is_negative(dentry))
		return true;
	inode = d_inode(dentry);

	if (!svcxdr_encode_fattr(rqstp, xdr, &resp->fh, &resp->stat))
		return false;
	if (xdr_stream_encode_u32(xdr, resp->mask) < 0)
		return false;

	rqstp->rq_res.page_len = w = nfsacl_size(
		(resp->mask & NFS_ACL)   ? resp->acl_access  : NULL,
		(resp->mask & NFS_DFACL) ? resp->acl_default : NULL);
	while (w > 0) {
		if (!*(rqstp->rq_next_page++))
			return true;
		w -= PAGE_SIZE;
	}

	if (!nfs_stream_encode_acl(xdr, inode, resp->acl_access,
				   resp->mask & NFS_ACL, 0))
		return false;
	if (!nfs_stream_encode_acl(xdr, inode, resp->acl_default,
				   resp->mask & NFS_DFACL, NFS_ACL_DEFAULT))
		return false;

	return true;
}

/* ACCESS */
static bool
nfsaclsvc_encode_accessres(struct svc_rqst *rqstp, struct xdr_stream *xdr)
{
	struct nfsd3_accessres *resp = rqstp->rq_resp;

	if (!svcxdr_encode_stat(xdr, resp->status))
		return false;
	switch (resp->status) {
	case nfs_ok:
		if (!svcxdr_encode_fattr(rqstp, xdr, &resp->fh, &resp->stat))
			return false;
		if (xdr_stream_encode_u32(xdr, resp->access) < 0)
			return false;
		break;
	}

	return true;
}

/*
 * XDR release functions
 */
static void nfsaclsvc_release_getacl(struct svc_rqst *rqstp)
{
	struct nfsd3_getaclres *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
	posix_acl_release(resp->acl_access);
	posix_acl_release(resp->acl_default);
}

static void nfsaclsvc_release_access(struct svc_rqst *rqstp)
{
	struct nfsd3_accessres *resp = rqstp->rq_resp;

	fh_put(&resp->fh);
}

struct nfsd3_voidargs { int dummy; };

#define ST 1		/* status*/
#define AT 21		/* attributes */
#define pAT (1+AT)	/* post attributes - conditional */
#define ACL (1+NFS_ACL_MAX_ENTRIES*3)  /* Access Control List */

static const struct svc_procedure nfsd_acl_procedures2[5] = {
	[ACLPROC2_NULL] = {
		.pc_func = nfsacld_proc_null,
		.pc_decode = nfssvc_decode_voidarg,
		.pc_encode = nfssvc_encode_voidres,
		.pc_argsize = sizeof(struct nfsd_voidargs),
		.pc_ressize = sizeof(struct nfsd_voidres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST,
		.pc_name = "NULL",
	},
	[ACLPROC2_GETACL] = {
		.pc_func = nfsacld_proc_getacl,
		.pc_decode = nfsaclsvc_decode_getaclargs,
		.pc_encode = nfsaclsvc_encode_getaclres,
		.pc_release = nfsaclsvc_release_getacl,
		.pc_argsize = sizeof(struct nfsd3_getaclargs),
		.pc_ressize = sizeof(struct nfsd3_getaclres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+1+2*(1+ACL),
		.pc_name = "GETACL",
	},
	[ACLPROC2_SETACL] = {
		.pc_func = nfsacld_proc_setacl,
		.pc_decode = nfsaclsvc_decode_setaclargs,
		.pc_encode = nfssvc_encode_attrstatres,
		.pc_release = nfssvc_release_attrstat,
		.pc_argsize = sizeof(struct nfsd3_setaclargs),
		.pc_ressize = sizeof(struct nfsd_attrstat),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+AT,
		.pc_name = "SETACL",
	},
	[ACLPROC2_GETATTR] = {
		.pc_func = nfsacld_proc_getattr,
		.pc_decode = nfssvc_decode_fhandleargs,
		.pc_encode = nfssvc_encode_attrstatres,
		.pc_release = nfssvc_release_attrstat,
		.pc_argsize = sizeof(struct nfsd_fhandle),
		.pc_ressize = sizeof(struct nfsd_attrstat),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+AT,
		.pc_name = "GETATTR",
	},
	[ACLPROC2_ACCESS] = {
		.pc_func = nfsacld_proc_access,
		.pc_decode = nfsaclsvc_decode_accessargs,
		.pc_encode = nfsaclsvc_encode_accessres,
		.pc_release = nfsaclsvc_release_access,
		.pc_argsize = sizeof(struct nfsd3_accessargs),
		.pc_ressize = sizeof(struct nfsd3_accessres),
		.pc_cachetype = RC_NOCACHE,
		.pc_xdrressize = ST+AT+1,
		.pc_name = "SETATTR",
	},
};

static unsigned int nfsd_acl_count2[ARRAY_SIZE(nfsd_acl_procedures2)];
const struct svc_version nfsd_acl_version2 = {
	.vs_vers	= 2,
	.vs_nproc	= 5,
	.vs_proc	= nfsd_acl_procedures2,
	.vs_count	= nfsd_acl_count2,
	.vs_dispatch	= nfsd_dispatch,
	.vs_xdrsize	= NFS3_SVC_XDRSIZE,
};
