/*
 * linux/fs/nfsd/nfsacl.c
 *
 * Process version 2 NFSACL requests.
 *
 * Copyright (C) 2002-2003 Andreas Gruenbacher <agruen@suse.de>
 */

#include <linux/sunrpc/svc.h>
#include <linux/nfs.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/nfsd/xdr.h>
#include <linux/nfsd/xdr3.h>
#include <linux/posix_acl.h>
#include <linux/nfsacl.h>

#define NFSDDBG_FACILITY		NFSDDBG_PROC
#define RETURN_STATUS(st)	{ resp->status = (st); return (st); }

/*
 * NULL call.
 */
static int
nfsacld_proc_null(struct svc_rqst *rqstp, void *argp, void *resp)
{
	return nfs_ok;
}

/*
 * Get the Access and/or Default ACL of a file.
 */
static int nfsacld_proc_getacl(struct svc_rqst * rqstp,
		struct nfsd3_getaclargs *argp, struct nfsd3_getaclres *resp)
{
	svc_fh *fh;
	struct posix_acl *acl;
	int nfserr = 0;

	dprintk("nfsd: GETACL(2acl)   %s\n", SVCFH_fmt(&argp->fh));

	fh = fh_copy(&resp->fh, &argp->fh);
	if ((nfserr = fh_verify(rqstp, &resp->fh, 0, MAY_NOP)))
		RETURN_STATUS(nfserr_inval);

	if (argp->mask & ~(NFS_ACL|NFS_ACLCNT|NFS_DFACL|NFS_DFACLCNT))
		RETURN_STATUS(nfserr_inval);
	resp->mask = argp->mask;

	if (resp->mask & (NFS_ACL|NFS_ACLCNT)) {
		acl = nfsd_get_posix_acl(fh, ACL_TYPE_ACCESS);
		if (IS_ERR(acl)) {
			int err = PTR_ERR(acl);

			if (err == -ENODATA || err == -EOPNOTSUPP)
				acl = NULL;
			else {
				nfserr = nfserrno(err);
				goto fail;
			}
		}
		if (acl == NULL) {
			/* Solaris returns the inode's minimum ACL. */

			struct inode *inode = fh->fh_dentry->d_inode;
			acl = posix_acl_from_mode(inode->i_mode, GFP_KERNEL);
		}
		resp->acl_access = acl;
	}
	if (resp->mask & (NFS_DFACL|NFS_DFACLCNT)) {
		/* Check how Solaris handles requests for the Default ACL
		   of a non-directory! */

		acl = nfsd_get_posix_acl(fh, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl)) {
			int err = PTR_ERR(acl);

			if (err == -ENODATA || err == -EOPNOTSUPP)
				acl = NULL;
			else {
				nfserr = nfserrno(err);
				goto fail;
			}
		}
		resp->acl_default = acl;
	}

	/* resp->acl_{access,default} are released in nfssvc_release_getacl. */
	RETURN_STATUS(0);

fail:
	posix_acl_release(resp->acl_access);
	posix_acl_release(resp->acl_default);
	RETURN_STATUS(nfserr);
}

/*
 * Set the Access and/or Default ACL of a file.
 */
static int nfsacld_proc_setacl(struct svc_rqst * rqstp,
		struct nfsd3_setaclargs *argp,
		struct nfsd_attrstat *resp)
{
	svc_fh *fh;
	int nfserr = 0;

	dprintk("nfsd: SETACL(2acl)   %s\n", SVCFH_fmt(&argp->fh));

	fh = fh_copy(&resp->fh, &argp->fh);
	nfserr = fh_verify(rqstp, &resp->fh, 0, MAY_NOP);

	if (!nfserr) {
		nfserr = nfserrno( nfsd_set_posix_acl(
			fh, ACL_TYPE_ACCESS, argp->acl_access) );
	}
	if (!nfserr) {
		nfserr = nfserrno( nfsd_set_posix_acl(
			fh, ACL_TYPE_DEFAULT, argp->acl_default) );
	}

	/* argp->acl_{access,default} may have been allocated in
	   nfssvc_decode_setaclargs. */
	posix_acl_release(argp->acl_access);
	posix_acl_release(argp->acl_default);
	return nfserr;
}

/*
 * Check file attributes
 */
static int nfsacld_proc_getattr(struct svc_rqst * rqstp,
		struct nfsd_fhandle *argp, struct nfsd_attrstat *resp)
{
	dprintk("nfsd: GETATTR  %s\n", SVCFH_fmt(&argp->fh));

	fh_copy(&resp->fh, &argp->fh);
	return fh_verify(rqstp, &resp->fh, 0, MAY_NOP);
}

/*
 * Check file access
 */
static int nfsacld_proc_access(struct svc_rqst *rqstp, struct nfsd3_accessargs *argp,
		struct nfsd3_accessres *resp)
{
	int nfserr;

	dprintk("nfsd: ACCESS(2acl)   %s 0x%x\n",
			SVCFH_fmt(&argp->fh),
			argp->access);

	fh_copy(&resp->fh, &argp->fh);
	resp->access = argp->access;
	nfserr = nfsd_access(rqstp, &resp->fh, &resp->access, NULL);
	return nfserr;
}

/*
 * XDR decode functions
 */
static int nfsaclsvc_decode_getaclargs(struct svc_rqst *rqstp, u32 *p,
		struct nfsd3_getaclargs *argp)
{
	if (!(p = nfs2svc_decode_fh(p, &argp->fh)))
		return 0;
	argp->mask = ntohl(*p); p++;

	return xdr_argsize_check(rqstp, p);
}


static int nfsaclsvc_decode_setaclargs(struct svc_rqst *rqstp, u32 *p,
		struct nfsd3_setaclargs *argp)
{
	struct kvec *head = rqstp->rq_arg.head;
	unsigned int base;
	int n;

	if (!(p = nfs2svc_decode_fh(p, &argp->fh)))
		return 0;
	argp->mask = ntohl(*p++);
	if (argp->mask & ~(NFS_ACL|NFS_ACLCNT|NFS_DFACL|NFS_DFACLCNT) ||
	    !xdr_argsize_check(rqstp, p))
		return 0;

	base = (char *)p - (char *)head->iov_base;
	n = nfsacl_decode(&rqstp->rq_arg, base, NULL,
			  (argp->mask & NFS_ACL) ?
			  &argp->acl_access : NULL);
	if (n > 0)
		n = nfsacl_decode(&rqstp->rq_arg, base + n, NULL,
				  (argp->mask & NFS_DFACL) ?
				  &argp->acl_default : NULL);
	return (n > 0);
}

static int nfsaclsvc_decode_fhandleargs(struct svc_rqst *rqstp, u32 *p,
		struct nfsd_fhandle *argp)
{
	if (!(p = nfs2svc_decode_fh(p, &argp->fh)))
		return 0;
	return xdr_argsize_check(rqstp, p);
}

static int nfsaclsvc_decode_accessargs(struct svc_rqst *rqstp, u32 *p,
		struct nfsd3_accessargs *argp)
{
	if (!(p = nfs2svc_decode_fh(p, &argp->fh)))
		return 0;
	argp->access = ntohl(*p++);

	return xdr_argsize_check(rqstp, p);
}

/*
 * XDR encode functions
 */

/* GETACL */
static int nfsaclsvc_encode_getaclres(struct svc_rqst *rqstp, u32 *p,
		struct nfsd3_getaclres *resp)
{
	struct dentry *dentry = resp->fh.fh_dentry;
	struct inode *inode = dentry->d_inode;
	int w = nfsacl_size(
		(resp->mask & NFS_ACL)   ? resp->acl_access  : NULL,
		(resp->mask & NFS_DFACL) ? resp->acl_default : NULL);
	struct kvec *head = rqstp->rq_res.head;
	unsigned int base;
	int n;

	if (dentry == NULL || dentry->d_inode == NULL)
		return 0;
	inode = dentry->d_inode;

	p = nfs2svc_encode_fattr(rqstp, p, &resp->fh);
	*p++ = htonl(resp->mask);
	if (!xdr_ressize_check(rqstp, p))
		return 0;
	base = (char *)p - (char *)head->iov_base;

	rqstp->rq_res.page_len = w;
	while (w > 0) {
		if (!svc_take_res_page(rqstp))
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
	return 1;
}

static int nfsaclsvc_encode_attrstatres(struct svc_rqst *rqstp, u32 *p,
		struct nfsd_attrstat *resp)
{
	p = nfs2svc_encode_fattr(rqstp, p, &resp->fh);
	return xdr_ressize_check(rqstp, p);
}

/* ACCESS */
static int nfsaclsvc_encode_accessres(struct svc_rqst *rqstp, u32 *p,
		struct nfsd3_accessres *resp)
{
	p = nfs2svc_encode_fattr(rqstp, p, &resp->fh);
	*p++ = htonl(resp->access);
	return xdr_ressize_check(rqstp, p);
}

/*
 * XDR release functions
 */
static int nfsaclsvc_release_getacl(struct svc_rqst *rqstp, u32 *p,
		struct nfsd3_getaclres *resp)
{
	fh_put(&resp->fh);
	posix_acl_release(resp->acl_access);
	posix_acl_release(resp->acl_default);
	return 1;
}

static int nfsaclsvc_release_fhandle(struct svc_rqst *rqstp, u32 *p,
		struct nfsd_fhandle *resp)
{
	fh_put(&resp->fh);
	return 1;
}

#define nfsaclsvc_decode_voidargs	NULL
#define nfsaclsvc_encode_voidres	NULL
#define nfsaclsvc_release_void		NULL
#define nfsd3_fhandleargs	nfsd_fhandle
#define nfsd3_attrstatres	nfsd_attrstat
#define nfsd3_voidres		nfsd3_voidargs
struct nfsd3_voidargs { int dummy; };

#define PROC(name, argt, rest, relt, cache, respsize)	\
 { (svc_procfunc) nfsacld_proc_##name,		\
   (kxdrproc_t) nfsaclsvc_decode_##argt##args,	\
   (kxdrproc_t) nfsaclsvc_encode_##rest##res,	\
   (kxdrproc_t) nfsaclsvc_release_##relt,		\
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

static struct svc_procedure		nfsd_acl_procedures2[] = {
  PROC(null,	void,		void,		void,	  RC_NOCACHE, ST),
  PROC(getacl,	getacl,		getacl,		getacl,	  RC_NOCACHE, ST+1+2*(1+ACL)),
  PROC(setacl,	setacl,		attrstat,	fhandle,  RC_NOCACHE, ST+AT),
  PROC(getattr, fhandle,	attrstat,	fhandle,  RC_NOCACHE, ST+AT),
  PROC(access,	access,		access,		fhandle,  RC_NOCACHE, ST+AT+1),
};

struct svc_version	nfsd_acl_version2 = {
		.vs_vers	= 2,
		.vs_nproc	= 5,
		.vs_proc	= nfsd_acl_procedures2,
		.vs_dispatch	= nfsd_dispatch,
		.vs_xdrsize	= NFS3_SVC_XDRSIZE,
};
