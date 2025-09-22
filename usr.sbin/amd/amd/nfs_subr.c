/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)nfs_subr.c	8.1 (Berkeley) 6/6/93
 *	$Id: nfs_subr.c,v 1.9 2015/01/22 03:43:58 guenther Exp $
 */

#include "am.h"

/*
 * Convert from UN*X to NFS error code
 */
#ifdef NFS_ERROR_MAPPING
NFS_ERROR_MAPPING
#define nfs_error(e) \
        ((nfsstat)((e) > NFS_LOMAP && (e) < NFS_HIMAP ? \
        nfs_errormap[(e) - NFS_LOMAP] : (e)))
#else
#define nfs_error(e) ((nfsstat)(e))
#endif /* NFS_ERROR_MAPPING */

static char *
do_readlink(am_node *mp, int *error_return, struct attrstat **attrpp)
{
	char *ln;

	/*
	 * If there is a readlink method, then use
	 * that, otherwise if a link exists use
	 * that, otherwise use the mount point.
	 */
	if (mp->am_mnt->mf_ops->readlink) {
		int retry = 0;
		mp = (*mp->am_mnt->mf_ops->readlink)(mp, &retry);
		if (mp == 0) {
			*error_return = retry;
			return 0;
		}
		/*reschedule_timeout_mp();*/
	}
	if (mp->am_link) {
		ln = mp->am_link;
	} else {
		ln = mp->am_mnt->mf_mount;
	}
	if (attrpp)
		*attrpp = &mp->am_attr;
	return ln;
}

void *
nfsproc_null_2_svc(void *argp, struct svc_req *rqstp)
{
	static char res;

	return &res;
}


struct attrstat *
nfsproc_getattr_2_svc(struct nfs_fh *argp, struct svc_req *rqstp)
{
	static struct attrstat res;
	am_node *mp;
	int retry;

#ifdef DEBUG
	Debug(D_TRACE)
		plog(XLOG_DEBUG, "gettattr:");
#endif /* DEBUG */

	mp = fh_to_mp2(argp, &retry);
	if (mp == 0) {
getattr_retry:

		if (retry < 0)
			return 0;
		res.status = nfs_error(retry);
	} else {
		struct attrstat *attrp = &mp->am_attr;
		if (mp->am_fattr.type == NFLNK) {
			/*
			 * Make sure we can read the link,
			 * and then determine the length.
			 */
			char *ln = do_readlink(mp, &retry, &attrp);
			if (ln == 0)
				goto getattr_retry;
		}
#ifdef DEBUG
		Debug(D_TRACE)
			plog(XLOG_DEBUG, "\tstat(%s), size = %d", mp->am_path, attrp->attrstat_u.attributes.size);
#endif /* DEBUG */
		mp->am_stats.s_getattr++;
		return attrp;
	}

	return &res;
}


struct attrstat *
nfsproc_setattr_2_svc(struct sattrargs *argp, struct svc_req *rqstp)
{
	static struct attrstat res;

	if (!fh_to_mp(&argp->file))
		res.status = nfs_error(ESTALE);
	else
		res.status = nfs_error(EROFS);

	return &res;
}


void *
nfsproc_root_2_svc(void *argp, struct svc_req *rqstp)
{
	static char res;

	return &res;
}


struct diropres *
nfsproc_lookup_2_svc(struct diropargs *argp, struct svc_req *rqstp)
{
	static struct diropres res;
	am_node *mp;
	int retry;

#ifdef DEBUG
	Debug(D_TRACE)
		plog(XLOG_DEBUG, "lookup:");
#endif /* DEBUG */

	mp = fh_to_mp2(&argp->dir, &retry);
	if (mp == 0) {
		if (retry < 0)
			return 0;
		res.status = nfs_error(retry);
	} else {
		int error;
		am_node *ap;
#ifdef DEBUG
		Debug(D_TRACE)
			plog(XLOG_DEBUG, "\tlookuppn(%s, %s)", mp->am_path, argp->name);
#endif /* DEBUG */
		ap = (*mp->am_mnt->mf_ops->lookuppn)(mp, argp->name, &error, VLOOK_CREATE);
		if (ap == 0) {
			if (error < 0) {
#ifdef DEBUG
				dlog("Not sending RPC reply");
#endif /* DEBUG */
				amd_stats.d_drops++;
				return 0;
			}
			res.status = nfs_error(error);
		} else {
			mp_to_fh(ap, &res.diropres_u.diropres.file);
			res.diropres_u.diropres.attributes = ap->am_fattr;
			res.status = NFS_OK;
		}
		mp->am_stats.s_lookup++;
		/*reschedule_timeout_mp();*/
	}

	return &res;
}


struct readlinkres *
nfsproc_readlink_2_svc(struct nfs_fh *argp, struct svc_req *rqstp)
{
	static struct readlinkres res;
	am_node *mp;
	int retry;

#ifdef DEBUG
	Debug(D_TRACE)
		plog(XLOG_DEBUG, "readlink:");
#endif /* DEBUG */

	mp = fh_to_mp2(argp, &retry);
	if (mp == 0) {
readlink_retry:
		if (retry < 0)
			return 0;
		res.status = nfs_error(retry);
	} else {
		char *ln = do_readlink(mp, &retry, (struct attrstat **) 0);
		if (ln == 0)
			goto readlink_retry;
		res.status = NFS_OK;
#ifdef DEBUG
		Debug(D_TRACE)
			if (ln)
				plog(XLOG_DEBUG, "\treadlink(%s) = %s", mp->am_path, ln);
#endif /* DEBUG */
		res.readlinkres_u.data = ln;
		mp->am_stats.s_readlink++;
	}

	return &res;
}


struct readres *
nfsproc_read_2_svc(struct readargs *argp, struct svc_req *rqstp)
{
	static struct readres res;

	bzero(&res, sizeof(res));

	res.status = nfs_error(EACCES);

	return &res;
}


void *
nfsproc_writecache_2_svc(void *argp, struct svc_req *rqstp)
{
	static char res;

	return &res;
}


struct attrstat *
nfsproc_write_2_svc(writeargs *argp, struct svc_req *rqstp)
{
	static struct attrstat res;

	if (!fh_to_mp(&argp->file))
		res.status = nfs_error(ESTALE);
	else
		res.status = nfs_error(EROFS);

	return &res;
}


struct diropres *
nfsproc_create_2_svc(createargs *argp, struct svc_req *rqstp)
{
	static struct diropres res;

	if (!fh_to_mp(&argp->where.dir))
		res.status = nfs_error(ESTALE);
	else
		res.status = nfs_error(EROFS);

	return &res;
}


static nfsstat *
unlink_or_rmdir(struct diropargs *argp, struct svc_req *rqstp,
    int unlinkp)
{
	static nfsstat res;
	int retry;
	/*mntfs *mf;*/
	am_node *mp = fh_to_mp3(&argp->dir, &retry, VLOOK_DELETE);

	if (mp == 0) {
		if (retry < 0)
			return 0;
		res = nfs_error(retry);
		goto out;
	}
	/*mf = mp->am_mnt;*/
	if (mp->am_fattr.type != NFDIR) {
		res = nfs_error(ENOTDIR);
		goto out;
	}
#ifdef DEBUG
	Debug(D_TRACE)
		plog(XLOG_DEBUG, "\tremove(%s, %s)", mp->am_path, argp->name);
#endif /* DEBUG */
	mp = (*mp->am_mnt->mf_ops->lookuppn)(mp, argp->name, &retry, VLOOK_DELETE);
	if (mp == 0) {
		/*
		 * Ignore retries...
		 */
		if (retry < 0)
			retry = 0;
		/*
		 * Usual NFS workaround...
		 */
		else if (retry == ENOENT)
			retry = 0;
		res = nfs_error(retry);
	} else {
		forcibly_timeout_mp(mp);
		res = NFS_OK;
	}

out:
	return &res;
}


nfsstat *
nfsproc_remove_2_svc(struct diropargs *argp, struct svc_req *rqstp)
{
	return unlink_or_rmdir(argp, rqstp, TRUE);
}

nfsstat *
nfsproc_rename_2_svc(renameargs *argp, struct svc_req *rqstp)
{
	static nfsstat res;
	if (!fh_to_mp(&argp->from.dir) || !fh_to_mp(&argp->to.dir))
		res = nfs_error(ESTALE);
	/*
	 * If the kernel is doing clever things with referenced files
	 * then let it pretend...
	 */
	else if (strncmp(argp->to.name, ".nfs", 4) == 0)
		res = NFS_OK;
	/*
	 * otherwise a failure
	 */
	else
		res = nfs_error(EROFS);
	return &res;
}


nfsstat *
nfsproc_link_2_svc(linkargs *argp, struct svc_req *rqstp)
{
	static nfsstat res;
	if (!fh_to_mp(&argp->from) || !fh_to_mp(&argp->to.dir))
		res = nfs_error(ESTALE);
	else
		res = nfs_error(EROFS);

	return &res;
}


nfsstat *
nfsproc_symlink_2_svc(symlinkargs *argp, struct svc_req *rqstp)
{
	static nfsstat res;
	if (!fh_to_mp(&argp->from.dir))
		res = nfs_error(ESTALE);
	else
		res = nfs_error(EROFS);

	return &res;
}


struct diropres *
nfsproc_mkdir_2_svc(createargs *argp, struct svc_req *rqstp)
{
	static struct diropres res;
	if (!fh_to_mp(&argp->where.dir))
		res.status = nfs_error(ESTALE);
	else
		res.status = nfs_error(EROFS);

	return &res;
}


nfsstat *
nfsproc_rmdir_2_svc(struct diropargs *argp, struct svc_req *rqstp)
{
	return unlink_or_rmdir(argp, rqstp, FALSE);
}


struct readdirres *
nfsproc_readdir_2_svc(readdirargs *argp, struct svc_req *rqstp)
{
	static readdirres res;
	static entry e_res[MAX_READDIR_ENTRIES];
	am_node *mp;
	int retry;

#ifdef DEBUG
	Debug(D_TRACE)
		plog(XLOG_DEBUG, "readdir:");
#endif /* DEBUG */

	mp = fh_to_mp2(&argp->dir, &retry);
	if (mp == 0) {
		if (retry < 0)
			return 0;
		res.status = nfs_error(retry);
	} else {
#ifdef DEBUG
		Debug(D_TRACE)
			plog(XLOG_DEBUG, "\treaddir(%s)", mp->am_path);
#endif /* DEBUG */
		res.status = nfs_error((*mp->am_mnt->mf_ops->readdir)(mp, argp->cookie,
					&res.readdirres_u.reply, e_res, argp->count));
		mp->am_stats.s_readdir++;
	}

	return &res;
}

struct statfsres *
nfsproc_statfs_2_svc(struct nfs_fh *argp, struct svc_req *rqstp)
{
	static statfsres res;
	am_node *mp;
	int retry;

#ifdef DEBUG
	Debug(D_TRACE)
		plog(XLOG_DEBUG, "statfs:");
#endif /* DEBUG */

	mp = fh_to_mp2(argp, &retry);
	if (mp == 0) {
		if (retry < 0)
			return 0;
		res.status = nfs_error(retry);
	} else {
		statfsokres *fp;
#ifdef DEBUG
		Debug(D_TRACE)
			plog(XLOG_DEBUG, "\tstat_fs(%s)", mp->am_path);
#endif /* DEBUG */
		/*
		 * just return faked up file system information
		 */

		fp = &res.statfsres_u.reply;

		fp->tsize = 1024;
		fp->bsize = 4096;
		fp->blocks = 0;
		fp->bfree = 0;
		fp->bavail = 0;

		res.status = NFS_OK;
		mp->am_stats.s_statfs++;
	}

	return &res;
}
