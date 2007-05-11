/*
 * linux/fs/nfsd/nfsxdr.c
 *
 * XDR support for nfsd
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/nfs.h>
#include <linux/vfs.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/xdr.h>
#include <linux/mm.h>

#define NFSDDBG_FACILITY		NFSDDBG_XDR

/*
 * Mapping of S_IF* types to NFS file types
 */
static u32	nfs_ftypes[] = {
	NFNON,  NFCHR,  NFCHR, NFBAD,
	NFDIR,  NFBAD,  NFBLK, NFBAD,
	NFREG,  NFBAD,  NFLNK, NFBAD,
	NFSOCK, NFBAD,  NFLNK, NFBAD,
};


/*
 * XDR functions for basic NFS types
 */
static __be32 *
decode_fh(__be32 *p, struct svc_fh *fhp)
{
	fh_init(fhp, NFS_FHSIZE);
	memcpy(&fhp->fh_handle.fh_base, p, NFS_FHSIZE);
	fhp->fh_handle.fh_size = NFS_FHSIZE;

	/* FIXME: Look up export pointer here and verify
	 * Sun Secure RPC if requested */
	return p + (NFS_FHSIZE >> 2);
}

/* Helper function for NFSv2 ACL code */
__be32 *nfs2svc_decode_fh(__be32 *p, struct svc_fh *fhp)
{
	return decode_fh(p, fhp);
}

static __be32 *
encode_fh(__be32 *p, struct svc_fh *fhp)
{
	memcpy(p, &fhp->fh_handle.fh_base, NFS_FHSIZE);
	return p + (NFS_FHSIZE>> 2);
}

/*
 * Decode a file name and make sure that the path contains
 * no slashes or null bytes.
 */
static __be32 *
decode_filename(__be32 *p, char **namp, int *lenp)
{
	char		*name;
	int		i;

	if ((p = xdr_decode_string_inplace(p, namp, lenp, NFS_MAXNAMLEN)) != NULL) {
		for (i = 0, name = *namp; i < *lenp; i++, name++) {
			if (*name == '\0' || *name == '/')
				return NULL;
		}
	}

	return p;
}

static __be32 *
decode_pathname(__be32 *p, char **namp, int *lenp)
{
	char		*name;
	int		i;

	if ((p = xdr_decode_string_inplace(p, namp, lenp, NFS_MAXPATHLEN)) != NULL) {
		for (i = 0, name = *namp; i < *lenp; i++, name++) {
			if (*name == '\0')
				return NULL;
		}
	}

	return p;
}

static __be32 *
decode_sattr(__be32 *p, struct iattr *iap)
{
	u32	tmp, tmp1;

	iap->ia_valid = 0;

	/* Sun client bug compatibility check: some sun clients seem to
	 * put 0xffff in the mode field when they mean 0xffffffff.
	 * Quoting the 4.4BSD nfs server code: Nah nah nah nah na nah.
	 */
	if ((tmp = ntohl(*p++)) != (u32)-1 && tmp != 0xffff) {
		iap->ia_valid |= ATTR_MODE;
		iap->ia_mode = tmp;
	}
	if ((tmp = ntohl(*p++)) != (u32)-1) {
		iap->ia_valid |= ATTR_UID;
		iap->ia_uid = tmp;
	}
	if ((tmp = ntohl(*p++)) != (u32)-1) {
		iap->ia_valid |= ATTR_GID;
		iap->ia_gid = tmp;
	}
	if ((tmp = ntohl(*p++)) != (u32)-1) {
		iap->ia_valid |= ATTR_SIZE;
		iap->ia_size = tmp;
	}
	tmp  = ntohl(*p++); tmp1 = ntohl(*p++);
	if (tmp != (u32)-1 && tmp1 != (u32)-1) {
		iap->ia_valid |= ATTR_ATIME | ATTR_ATIME_SET;
		iap->ia_atime.tv_sec = tmp;
		iap->ia_atime.tv_nsec = tmp1 * 1000; 
	}
	tmp  = ntohl(*p++); tmp1 = ntohl(*p++);
	if (tmp != (u32)-1 && tmp1 != (u32)-1) {
		iap->ia_valid |= ATTR_MTIME | ATTR_MTIME_SET;
		iap->ia_mtime.tv_sec = tmp;
		iap->ia_mtime.tv_nsec = tmp1 * 1000; 
		/*
		 * Passing the invalid value useconds=1000000 for mtime
		 * is a Sun convention for "set both mtime and atime to
		 * current server time".  It's needed to make permissions
		 * checks for the "touch" program across v2 mounts to
		 * Solaris and Irix boxes work correctly. See description of
		 * sattr in section 6.1 of "NFS Illustrated" by
		 * Brent Callaghan, Addison-Wesley, ISBN 0-201-32750-5
		 */
		if (tmp1 == 1000000)
			iap->ia_valid &= ~(ATTR_ATIME_SET|ATTR_MTIME_SET);
	}
	return p;
}

static __be32 *
encode_fattr(struct svc_rqst *rqstp, __be32 *p, struct svc_fh *fhp,
	     struct kstat *stat)
{
	struct dentry	*dentry = fhp->fh_dentry;
	int type;
	struct timespec time;
	u32 f;

	type = (stat->mode & S_IFMT);

	*p++ = htonl(nfs_ftypes[type >> 12]);
	*p++ = htonl((u32) stat->mode);
	*p++ = htonl((u32) stat->nlink);
	*p++ = htonl((u32) nfsd_ruid(rqstp, stat->uid));
	*p++ = htonl((u32) nfsd_rgid(rqstp, stat->gid));

	if (S_ISLNK(type) && stat->size > NFS_MAXPATHLEN) {
		*p++ = htonl(NFS_MAXPATHLEN);
	} else {
		*p++ = htonl((u32) stat->size);
	}
	*p++ = htonl((u32) stat->blksize);
	if (S_ISCHR(type) || S_ISBLK(type))
		*p++ = htonl(new_encode_dev(stat->rdev));
	else
		*p++ = htonl(0xffffffff);
	*p++ = htonl((u32) stat->blocks);
	switch (fsid_source(fhp)) {
	default:
	case FSIDSOURCE_DEV:
		*p++ = htonl(new_encode_dev(stat->dev));
		break;
	case FSIDSOURCE_FSID:
		*p++ = htonl((u32) fhp->fh_export->ex_fsid);
		break;
	case FSIDSOURCE_UUID:
		f = ((u32*)fhp->fh_export->ex_uuid)[0];
		f ^= ((u32*)fhp->fh_export->ex_uuid)[1];
		f ^= ((u32*)fhp->fh_export->ex_uuid)[2];
		f ^= ((u32*)fhp->fh_export->ex_uuid)[3];
		*p++ = htonl(f);
		break;
	}
	*p++ = htonl((u32) stat->ino);
	*p++ = htonl((u32) stat->atime.tv_sec);
	*p++ = htonl(stat->atime.tv_nsec ? stat->atime.tv_nsec / 1000 : 0);
	lease_get_mtime(dentry->d_inode, &time); 
	*p++ = htonl((u32) time.tv_sec);
	*p++ = htonl(time.tv_nsec ? time.tv_nsec / 1000 : 0); 
	*p++ = htonl((u32) stat->ctime.tv_sec);
	*p++ = htonl(stat->ctime.tv_nsec ? stat->ctime.tv_nsec / 1000 : 0);

	return p;
}

/* Helper function for NFSv2 ACL code */
__be32 *nfs2svc_encode_fattr(struct svc_rqst *rqstp, __be32 *p, struct svc_fh *fhp)
{
	struct kstat stat;
	vfs_getattr(fhp->fh_export->ex_mnt, fhp->fh_dentry, &stat);
	return encode_fattr(rqstp, p, fhp, &stat);
}

/*
 * XDR decode functions
 */
int
nfssvc_decode_void(struct svc_rqst *rqstp, __be32 *p, void *dummy)
{
	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_fhandle(struct svc_rqst *rqstp, __be32 *p, struct nfsd_fhandle *args)
{
	if (!(p = decode_fh(p, &args->fh)))
		return 0;
	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_sattrargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_sattrargs *args)
{
	p = decode_fh(p, &args->fh);
	if (!p)
		return 0;
	p = decode_sattr(p, &args->attrs);

	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_diropargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_diropargs *args)
{
	if (!(p = decode_fh(p, &args->fh))
	 || !(p = decode_filename(p, &args->name, &args->len)))
		return 0;

	 return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_readargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_readargs *args)
{
	unsigned int len;
	int v,pn;
	if (!(p = decode_fh(p, &args->fh)))
		return 0;

	args->offset    = ntohl(*p++);
	len = args->count     = ntohl(*p++);
	p++; /* totalcount - unused */

	if (len > NFSSVC_MAXBLKSIZE_V2)
		len = NFSSVC_MAXBLKSIZE_V2;

	/* set up somewhere to store response.
	 * We take pages, put them on reslist and include in iovec
	 */
	v=0;
	while (len > 0) {
		pn = rqstp->rq_resused++;
		rqstp->rq_vec[v].iov_base = page_address(rqstp->rq_respages[pn]);
		rqstp->rq_vec[v].iov_len = len < PAGE_SIZE?len:PAGE_SIZE;
		len -= rqstp->rq_vec[v].iov_len;
		v++;
	}
	args->vlen = v;
	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_writeargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_writeargs *args)
{
	unsigned int len, hdr, dlen;
	int v;

	if (!(p = decode_fh(p, &args->fh)))
		return 0;

	p++;				/* beginoffset */
	args->offset = ntohl(*p++);	/* offset */
	p++;				/* totalcount */
	len = args->len = ntohl(*p++);
	/*
	 * The protocol specifies a maximum of 8192 bytes.
	 */
	if (len > NFSSVC_MAXBLKSIZE_V2)
		return 0;

	/*
	 * Check to make sure that we got the right number of
	 * bytes.
	 */
	hdr = (void*)p - rqstp->rq_arg.head[0].iov_base;
	dlen = rqstp->rq_arg.head[0].iov_len + rqstp->rq_arg.page_len
		- hdr;

	/*
	 * Round the length of the data which was specified up to
	 * the next multiple of XDR units and then compare that
	 * against the length which was actually received.
	 */
	if (dlen != XDR_QUADLEN(len)*4)
		return 0;

	rqstp->rq_vec[0].iov_base = (void*)p;
	rqstp->rq_vec[0].iov_len = rqstp->rq_arg.head[0].iov_len - hdr;
	v = 0;
	while (len > rqstp->rq_vec[v].iov_len) {
		len -= rqstp->rq_vec[v].iov_len;
		v++;
		rqstp->rq_vec[v].iov_base = page_address(rqstp->rq_pages[v]);
		rqstp->rq_vec[v].iov_len = PAGE_SIZE;
	}
	rqstp->rq_vec[v].iov_len = len;
	args->vlen = v + 1;
	return 1;
}

int
nfssvc_decode_createargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_createargs *args)
{
	if (   !(p = decode_fh(p, &args->fh))
	    || !(p = decode_filename(p, &args->name, &args->len)))
		return 0;
	p = decode_sattr(p, &args->attrs);

	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_renameargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_renameargs *args)
{
	if (!(p = decode_fh(p, &args->ffh))
	 || !(p = decode_filename(p, &args->fname, &args->flen))
	 || !(p = decode_fh(p, &args->tfh))
	 || !(p = decode_filename(p, &args->tname, &args->tlen)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_readlinkargs(struct svc_rqst *rqstp, __be32 *p, struct nfsd_readlinkargs *args)
{
	if (!(p = decode_fh(p, &args->fh)))
		return 0;
	args->buffer = page_address(rqstp->rq_respages[rqstp->rq_resused++]);

	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_linkargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_linkargs *args)
{
	if (!(p = decode_fh(p, &args->ffh))
	 || !(p = decode_fh(p, &args->tfh))
	 || !(p = decode_filename(p, &args->tname, &args->tlen)))
		return 0;

	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_symlinkargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_symlinkargs *args)
{
	if (   !(p = decode_fh(p, &args->ffh))
	    || !(p = decode_filename(p, &args->fname, &args->flen))
	    || !(p = decode_pathname(p, &args->tname, &args->tlen)))
		return 0;
	p = decode_sattr(p, &args->attrs);

	return xdr_argsize_check(rqstp, p);
}

int
nfssvc_decode_readdirargs(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_readdirargs *args)
{
	if (!(p = decode_fh(p, &args->fh)))
		return 0;
	args->cookie = ntohl(*p++);
	args->count  = ntohl(*p++);
	if (args->count > PAGE_SIZE)
		args->count = PAGE_SIZE;

	args->buffer = page_address(rqstp->rq_respages[rqstp->rq_resused++]);

	return xdr_argsize_check(rqstp, p);
}

/*
 * XDR encode functions
 */
int
nfssvc_encode_void(struct svc_rqst *rqstp, __be32 *p, void *dummy)
{
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_attrstat(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_attrstat *resp)
{
	p = encode_fattr(rqstp, p, &resp->fh, &resp->stat);
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_diropres(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_diropres *resp)
{
	p = encode_fh(p, &resp->fh);
	p = encode_fattr(rqstp, p, &resp->fh, &resp->stat);
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_readlinkres(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_readlinkres *resp)
{
	*p++ = htonl(resp->len);
	xdr_ressize_check(rqstp, p);
	rqstp->rq_res.page_len = resp->len;
	if (resp->len & 3) {
		/* need to pad the tail */
		rqstp->rq_res.tail[0].iov_base = p;
		*p = 0;
		rqstp->rq_res.tail[0].iov_len = 4 - (resp->len&3);
	}
	return 1;
}

int
nfssvc_encode_readres(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_readres *resp)
{
	p = encode_fattr(rqstp, p, &resp->fh, &resp->stat);
	*p++ = htonl(resp->count);
	xdr_ressize_check(rqstp, p);

	/* now update rqstp->rq_res to reflect data aswell */
	rqstp->rq_res.page_len = resp->count;
	if (resp->count & 3) {
		/* need to pad the tail */
		rqstp->rq_res.tail[0].iov_base = p;
		*p = 0;
		rqstp->rq_res.tail[0].iov_len = 4 - (resp->count&3);
	}
	return 1;
}

int
nfssvc_encode_readdirres(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_readdirres *resp)
{
	xdr_ressize_check(rqstp, p);
	p = resp->buffer;
	*p++ = 0;			/* no more entries */
	*p++ = htonl((resp->common.err == nfserr_eof));
	rqstp->rq_res.page_len = (((unsigned long)p-1) & ~PAGE_MASK)+1;

	return 1;
}

int
nfssvc_encode_statfsres(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_statfsres *resp)
{
	struct kstatfs	*stat = &resp->stats;

	*p++ = htonl(NFSSVC_MAXBLKSIZE_V2);	/* max transfer size */
	*p++ = htonl(stat->f_bsize);
	*p++ = htonl(stat->f_blocks);
	*p++ = htonl(stat->f_bfree);
	*p++ = htonl(stat->f_bavail);
	return xdr_ressize_check(rqstp, p);
}

int
nfssvc_encode_entry(void *ccdv, const char *name,
		    int namlen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct readdir_cd *ccd = ccdv;
	struct nfsd_readdirres *cd = container_of(ccd, struct nfsd_readdirres, common);
	__be32	*p = cd->buffer;
	int	buflen, slen;

	/*
	dprintk("nfsd: entry(%.*s off %ld ino %ld)\n",
			namlen, name, offset, ino);
	 */

	if (offset > ~((u32) 0)) {
		cd->common.err = nfserr_fbig;
		return -EINVAL;
	}
	if (cd->offset)
		*cd->offset = htonl(offset);
	if (namlen > NFS2_MAXNAMLEN)
		namlen = NFS2_MAXNAMLEN;/* truncate filename */

	slen = XDR_QUADLEN(namlen);
	if ((buflen = cd->buflen - slen - 4) < 0) {
		cd->common.err = nfserr_toosmall;
		return -EINVAL;
	}
	*p++ = xdr_one;				/* mark entry present */
	*p++ = htonl((u32) ino);		/* file id */
	p    = xdr_encode_array(p, name, namlen);/* name length & name */
	cd->offset = p;			/* remember pointer */
	*p++ = htonl(~0U);		/* offset of next entry */

	cd->buflen = buflen;
	cd->buffer = p;
	cd->common.err = nfs_ok;
	return 0;
}

/*
 * XDR release functions
 */
int
nfssvc_release_fhandle(struct svc_rqst *rqstp, __be32 *p,
					struct nfsd_fhandle *resp)
{
	fh_put(&resp->fh);
	return 1;
}
