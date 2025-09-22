/*	$OpenBSD: nfs_srvsubs.c,v 1.2 2024/09/18 05:21:19 jsg Exp $	*/
/*	$NetBSD: nfs_subs.c,v 1.27.4.3 1996/07/08 20:34:24 jtc Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfs_subs.c	8.8 (Berkeley) 5/22/95
 */


/*
 * These functions support the nfsm_subs.h inline functions and help fiddle
 * mbuf chains for the nfs op functions. They do things such as creating the
 * rpc header and copying data between mbuf chains and uio lists.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/pool.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/xdr_subs.h>
#include <nfs/nfs_var.h>
#include <nfs/nfsm_subs.h>

#include <netinet/in.h>

/* Global vars */
extern u_int32_t nfs_false, nfs_true;
extern const nfstype nfsv2_type[9];
extern const nfstype nfsv3_type[9];

/*
 * Set up nameidata for a lookup() call and do it
 */
int
nfs_namei(struct nameidata *ndp, fhandle_t *fhp, int len,
    struct nfssvc_sock *slp, struct mbuf *nam, struct mbuf **mdp,
    caddr_t *dposp, struct vnode **retdirp, struct proc *p)
{
	int i, rem;
	struct mbuf *md;
	char *fromcp, *tocp;
	struct vnode *dp;
	int error, rdonly;
	struct componentname *cnp = &ndp->ni_cnd;

	*retdirp = NULL;
	cnp->cn_pnbuf = pool_get(&namei_pool, PR_WAITOK);
	/*
	 * Copy the name from the mbuf list to ndp->ni_pnbuf
	 * and set the various ndp fields appropriately.
	 */
	fromcp = *dposp;
	tocp = cnp->cn_pnbuf;
	md = *mdp;
	rem = mtod(md, caddr_t) + md->m_len - fromcp;
	for (i = 0; i < len; i++) {
		while (rem == 0) {
			md = md->m_next;
			if (md == NULL) {
				error = EBADRPC;
				goto out;
			}
			fromcp = mtod(md, caddr_t);
			rem = md->m_len;
		}
		if (*fromcp == '\0' || *fromcp == '/') {
			error = EACCES;
			goto out;
		}
		*tocp++ = *fromcp++;
		rem--;
	}
	*tocp = '\0';
	*mdp = md;
	*dposp = fromcp;
	len = nfsm_padlen(len);
	if (len > 0) {
		if (rem >= len)
			*dposp += len;
		else if ((error = nfs_adv(mdp, dposp, len, rem)) != 0)
			goto out;
	}
	ndp->ni_pathlen = tocp - cnp->cn_pnbuf;
	cnp->cn_nameptr = cnp->cn_pnbuf;
	/*
	 * Extract and set starting directory.
	 */
	error = nfsrv_fhtovp(fhp, 0, &dp, ndp->ni_cnd.cn_cred, slp,
	    nam, &rdonly);
	if (error)
		goto out;
	if (dp->v_type != VDIR) {
		vrele(dp);
		error = ENOTDIR;
		goto out;
	}
	vref(dp);
	*retdirp = dp;
	ndp->ni_startdir = dp;
	if (rdonly)
		cnp->cn_flags |= (NOCROSSMOUNT | RDONLY);
	else
		cnp->cn_flags |= NOCROSSMOUNT;

	/*
	 * And call lookup() to do the real work
	 */
	cnp->cn_proc = p;
	error = vfs_lookup(ndp);
	if (error)
		goto out;
	/*
	 * Check for encountering a symbolic link
	 */
	if (cnp->cn_flags & ISSYMLINK) {
		if ((cnp->cn_flags & LOCKPARENT) && ndp->ni_pathlen == 1)
			vput(ndp->ni_dvp);
		else
			vrele(ndp->ni_dvp);
		vput(ndp->ni_vp);
		ndp->ni_vp = NULL;
		error = EINVAL;
		goto out;
	}
	/*
	 * Check for saved name request
	 */
	if (cnp->cn_flags & (SAVENAME | SAVESTART)) {
		cnp->cn_flags |= HASBUF;
		return (0);
	}
out:
	pool_put(&namei_pool, cnp->cn_pnbuf);
	return (error);
}

/*
 * A fiddled version of m_adj() that ensures null fill to a long
 * boundary and only trims off the back end
 */
void
nfsm_adj(struct mbuf *mp, int len, int nul)
{
	struct mbuf *m;
	int count, i;
	char *cp;

	/*
	 * Trim from tail.  Scan the mbuf chain,
	 * calculating its length and finding the last mbuf.
	 * If the adjustment only affects this mbuf, then just
	 * adjust and return.  Otherwise, rescan and truncate
	 * after the remaining size.
	 */
	count = 0;
	m = mp;
	for (;;) {
		count += m->m_len;
		if (m->m_next == NULL)
			break;
		m = m->m_next;
	}
	if (m->m_len > len) {
		m->m_len -= len;
		if (nul > 0) {
			cp = mtod(m, caddr_t)+m->m_len-nul;
			for (i = 0; i < nul; i++)
				*cp++ = '\0';
		}
		return;
	}
	count -= len;
	if (count < 0)
		count = 0;
	/*
	 * Correct length for chain is "count".
	 * Find the mbuf with last data, adjust its length,
	 * and toss data from remaining mbufs on chain.
	 */
	for (m = mp; m; m = m->m_next) {
		if (m->m_len >= count) {
			m->m_len = count;
			if (nul > 0) {
				cp = mtod(m, caddr_t)+m->m_len-nul;
				for (i = 0; i < nul; i++)
					*cp++ = '\0';
			}
			break;
		}
		count -= m->m_len;
	}
	for (m = m->m_next;m;m = m->m_next)
		m->m_len = 0;
}

/*
 * Make these non-inline functions, so that the kernel text size
 * doesn't get too big...
 */
void
nfsm_srvwcc(struct nfsrv_descript *nfsd, int before_ret,
    struct vattr *before_vap, int after_ret, struct vattr *after_vap,
    struct nfsm_info *info)
{
	u_int32_t *tl;

	if (before_ret) {
		tl = nfsm_build(&info->nmi_mb, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		tl = nfsm_build(&info->nmi_mb, 7 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		txdr_hyper(before_vap->va_size, tl);
		tl += 2;
		txdr_nfsv3time(&(before_vap->va_mtime), tl);
		tl += 2;
		txdr_nfsv3time(&(before_vap->va_ctime), tl);
	}
	nfsm_srvpostop_attr(nfsd, after_ret, after_vap, info);
}

void
nfsm_srvpostop_attr(struct nfsrv_descript *nfsd, int after_ret,
    struct vattr *after_vap, struct nfsm_info *info)
{
	u_int32_t *tl;
	struct nfs_fattr *fp;

	if (after_ret) {
		tl = nfsm_build(&info->nmi_mb, NFSX_UNSIGNED);
		*tl = nfs_false;
	} else {
		tl = nfsm_build(&info->nmi_mb, NFSX_UNSIGNED + NFSX_V3FATTR);
		*tl++ = nfs_true;
		fp = (struct nfs_fattr *)tl;
		nfsm_srvfattr(nfsd, after_vap, fp);
	}
}

void
nfsm_srvfattr(struct nfsrv_descript *nfsd, struct vattr *vap,
    struct nfs_fattr *fp)
{

	fp->fa_nlink = txdr_unsigned(vap->va_nlink);
	fp->fa_uid = txdr_unsigned(vap->va_uid);
	fp->fa_gid = txdr_unsigned(vap->va_gid);
	if (nfsd->nd_flag & ND_NFSV3) {
		fp->fa_type = vtonfsv3_type(vap->va_type);
		fp->fa_mode = vtonfsv3_mode(vap->va_mode);
		txdr_hyper(vap->va_size, &fp->fa3_size);
		txdr_hyper(vap->va_bytes, &fp->fa3_used);
		fp->fa3_rdev.specdata1 = txdr_unsigned(major(vap->va_rdev));
		fp->fa3_rdev.specdata2 = txdr_unsigned(minor(vap->va_rdev));
		fp->fa3_fsid.nfsuquad[0] = 0;
		fp->fa3_fsid.nfsuquad[1] = txdr_unsigned(vap->va_fsid);
		txdr_hyper(vap->va_fileid, &fp->fa3_fileid);
		txdr_nfsv3time(&vap->va_atime, &fp->fa3_atime);
		txdr_nfsv3time(&vap->va_mtime, &fp->fa3_mtime);
		txdr_nfsv3time(&vap->va_ctime, &fp->fa3_ctime);
	} else {
		fp->fa_type = vtonfsv2_type(vap->va_type);
		fp->fa_mode = vtonfsv2_mode(vap->va_type, vap->va_mode);
		fp->fa2_size = txdr_unsigned(vap->va_size);
		fp->fa2_blocksize = txdr_unsigned(vap->va_blocksize);
		if (vap->va_type == VFIFO)
			fp->fa2_rdev = 0xffffffff;
		else
			fp->fa2_rdev = txdr_unsigned(vap->va_rdev);
		fp->fa2_blocks = txdr_unsigned(vap->va_bytes / NFS_FABLKSIZE);
		fp->fa2_fsid = txdr_unsigned(vap->va_fsid);
		fp->fa2_fileid = txdr_unsigned((u_int32_t)vap->va_fileid);
		txdr_nfsv2time(&vap->va_atime, &fp->fa2_atime);
		txdr_nfsv2time(&vap->va_mtime, &fp->fa2_mtime);
		txdr_nfsv2time(&vap->va_ctime, &fp->fa2_ctime);
	}
}

/*
 * nfsrv_fhtovp() - convert a fh to a vnode ptr (optionally locked)
 * 	- look up fsid in mount list (if not found ret error)
 *	- get vp and export rights by calling VFS_FHTOVP() and VFS_CHECKEXP()
 *	- if cred->cr_uid == 0 or MNT_EXPORTANON set it to credanon
 *	- if not lockflag unlock it with VOP_UNLOCK()
 */
int
nfsrv_fhtovp(fhandle_t *fhp, int lockflag, struct vnode **vpp,
    struct ucred *cred, struct nfssvc_sock *slp, struct mbuf *nam,
    int *rdonlyp)
{
	struct mount *mp;
	int i;
	struct ucred *credanon;
	int error, exflags;
	struct sockaddr_in *saddr;

	*vpp = NULL;
	mp = vfs_getvfs(&fhp->fh_fsid);

	if (!mp)
		return (ESTALE);
	error = VFS_CHECKEXP(mp, nam, &exflags, &credanon);
	if (error)
		return (error);
	error = VFS_FHTOVP(mp, &fhp->fh_fid, vpp);
	if (error)
		return (error);

	saddr = mtod(nam, struct sockaddr_in *);
	if (saddr->sin_family == AF_INET &&
	    (ntohs(saddr->sin_port) >= IPPORT_RESERVED ||
	    (slp->ns_so->so_type == SOCK_STREAM && ntohs(saddr->sin_port) == 20))) {
		vput(*vpp);
		return (NFSERR_AUTHERR | AUTH_TOOWEAK);
	}

	/* Check/setup credentials. */
	if (cred->cr_uid == 0 || (exflags & MNT_EXPORTANON)) {
		cred->cr_uid = credanon->cr_uid;
		cred->cr_gid = credanon->cr_gid;
		for (i = 0; i < credanon->cr_ngroups && i < NGROUPS_MAX; i++)
			cred->cr_groups[i] = credanon->cr_groups[i];
		cred->cr_ngroups = i;
	}
	if (exflags & MNT_EXRDONLY)
		*rdonlyp = 1;
	else
		*rdonlyp = 0;
	if (!lockflag)
		VOP_UNLOCK(*vpp);

	return (0);
}

/*
 * This function compares two net addresses by family and returns non zero
 * if they are the same host, or if there is any doubt it returns 0.
 * The AF_INET family is handled as a special case so that address mbufs
 * don't need to be saved to store "struct in_addr", which is only 4 bytes.
 */
int
netaddr_match(int family, union nethostaddr *haddr, struct mbuf *nam)
{
	struct sockaddr_in *inetaddr;

	switch (family) {
	case AF_INET:
		inetaddr = mtod(nam, struct sockaddr_in *);
		if (inetaddr->sin_family == AF_INET &&
		    inetaddr->sin_addr.s_addr == haddr->had_inetaddr)
			return (1);
		break;
	default:
		break;
	}
	return (0);
}

int
nfsm_srvsattr(struct mbuf **mp, struct vattr *va, struct mbuf *mrep,
    caddr_t *dposp)
{
	struct nfsm_info	info;
	int error = 0;
	uint32_t *tl;

	info.nmi_md = *mp;
	info.nmi_dpos = *dposp;
	info.nmi_mrep = mrep;
	info.nmi_errorp = &error;

	tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
	if (tl == NULL)
		return error;
	if (*tl == nfs_true) {
		tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
		if (tl == NULL)
			return error;
		va->va_mode = nfstov_mode(*tl);
	}

	tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
	if (tl == NULL)
		return error;
	if (*tl == nfs_true) {
		tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
		if (tl == NULL)
			return error;
		va->va_uid = fxdr_unsigned(uid_t, *tl);
	}

	tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
	if (tl == NULL)
		return error;
	if (*tl == nfs_true) {
		tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
		if (tl == NULL)
			return error;
		va->va_gid = fxdr_unsigned(gid_t, *tl);
	}

	tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
	if (tl == NULL)
		return error;
	if (*tl == nfs_true) {
		tl = (uint32_t *)nfsm_dissect(&info, 2 * NFSX_UNSIGNED);
		if (tl == NULL)
			return error;
		va->va_size = fxdr_hyper(tl);
	}

	tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
	if (tl == NULL)
		return error;
	switch (fxdr_unsigned(int, *tl)) {
	case NFSV3SATTRTIME_TOCLIENT:
		va->va_vaflags |= VA_UTIMES_CHANGE;
		va->va_vaflags &= ~VA_UTIMES_NULL;
		tl = (uint32_t *)nfsm_dissect(&info, 2 * NFSX_UNSIGNED);
		if (tl == NULL)
			return error;
		fxdr_nfsv3time(tl, &va->va_atime);
		break;
	case NFSV3SATTRTIME_TOSERVER:
		va->va_vaflags |= VA_UTIMES_CHANGE;
		getnanotime(&va->va_atime);
		break;
	}

	tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
	if (tl == NULL)
		return error;
	switch (fxdr_unsigned(int, *tl)) {
	case NFSV3SATTRTIME_TOCLIENT:
		va->va_vaflags |= VA_UTIMES_CHANGE;
		va->va_vaflags &= ~VA_UTIMES_NULL;
		tl = (uint32_t *)nfsm_dissect(&info, 2 * NFSX_UNSIGNED);
		if (tl == NULL)
			return error;
		fxdr_nfsv3time(tl, &va->va_mtime);
		break;
	case NFSV3SATTRTIME_TOSERVER:
		va->va_vaflags |= VA_UTIMES_CHANGE;
		getnanotime(&va->va_mtime);
		break;
	}

	*dposp = info.nmi_dpos;
	*mp = info.nmi_md;
	return 0;
}
