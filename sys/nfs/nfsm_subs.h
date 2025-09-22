/*	$OpenBSD: nfsm_subs.h,v 1.49 2024/09/11 12:22:34 claudio Exp $	*/
/*	$NetBSD: nfsm_subs.h,v 1.10 1996/03/20 21:59:56 fvdl Exp $	*/

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
 *	@(#)nfsm_subs.h	8.2 (Berkeley) 3/30/95
 */


#ifndef _NFS_NFSM_SUBS_H_
#define _NFS_NFSM_SUBS_H_

struct nfsm_info {
	struct mbuf	*nmi_mreq;
	struct mbuf	*nmi_mrep;

	struct proc	*nmi_procp;	/* XXX XXX XXX */
	struct ucred	*nmi_cred;	/* XXX XXX XXX */

	/* Setting up / Tearing down. */
	struct mbuf	*nmi_md;
	struct mbuf	*nmi_mb;
	caddr_t		 nmi_dpos;

	int		 nmi_v3;  

	int		*nmi_errorp;
};

static inline void *
nfsm_dissect(struct nfsm_info *infop, int s)
{
	caddr_t ret;
	int avail, error;

	avail = mtod(infop->nmi_md, caddr_t) + infop->nmi_md->m_len -
	    infop->nmi_dpos;
	if (avail >= s) {
		ret = infop->nmi_dpos;
		infop->nmi_dpos += s;
		return ret;
	}
	error = nfsm_disct(&infop->nmi_md, &infop->nmi_dpos, s, avail, &ret);
	if (error != 0) {
		m_freem(infop->nmi_mrep);
		infop->nmi_mrep = NULL;
		*infop->nmi_errorp = error;
		return NULL;
	} else {
		return ret;
	}
}

#define nfsm_rndup(a)	(((a)+3)&(~0x3))

static inline int
nfsm_adv(struct nfsm_info *infop, int s)
{
	int avail, error;
       
	avail = mtod(infop->nmi_md, caddr_t) + infop->nmi_md->m_len -
	    infop->nmi_dpos;
	if (avail >= s) {
		infop->nmi_dpos += s;
		return 0;
	}
	error = nfs_adv(&infop->nmi_md, &infop->nmi_dpos, s, avail);
	if (error != 0) {
		m_freem(infop->nmi_mrep);
		infop->nmi_mrep = NULL;
		*infop->nmi_errorp = error;
		return error;
	}
	return 0;
}

static inline int
nfsm_postop_attr(struct nfsm_info *infop, struct vnode **vpp, int *attrflagp)
{
	uint32_t *tl;
	struct vnode *ttvp;
	int attrflag, error;

	if (infop->nmi_mrep == NULL)
		return 0;

	ttvp = *vpp;
	tl = (uint32_t *)nfsm_dissect(infop, NFSX_UNSIGNED);
	if (tl == NULL)
		return 1;	/* anything nonzero */
	attrflag = fxdr_unsigned(int, *tl);
	if (attrflag != 0) {
		error = nfs_loadattrcache(&ttvp, &infop->nmi_md,
		    &infop->nmi_dpos, NULL);
		if (error != 0) {
			m_freem(infop->nmi_mrep);
			infop->nmi_mrep = NULL;
			*infop->nmi_errorp = error;
			return error;
		}
		*vpp = ttvp;
	}
	*attrflagp = attrflag;
	return 0;
}

static inline int
nfsm_strsiz(struct nfsm_info *infop, int *lenp, int maxlen)
{
	uint32_t *tl = (uint32_t *)nfsm_dissect(infop, NFSX_UNSIGNED);
	int len;
	if (tl == NULL)
		return 1;
	len = fxdr_unsigned(int32_t, *tl);
	if (len < 0 || len > maxlen) {
		m_freem(infop->nmi_mrep);
		infop->nmi_mrep = NULL;
		*infop->nmi_errorp = EBADRPC;
		return 1;
	}
	*lenp = len;
	return 0;
}

static inline int
nfsm_mtouio(struct nfsm_info *infop, struct uio *uiop, int len)
{
	int error;

	if (len <= 0)
		return 0;

	error = nfsm_mbuftouio(&infop->nmi_md, uiop, len, &infop->nmi_dpos);
	if (error != 0) {
		m_freem(infop->nmi_mrep);
		infop->nmi_mrep = NULL;
		*infop->nmi_errorp = error;
		return error;
	}
	return 0;
}

static inline int
nfsm_strtom(struct nfsm_info *infop, char *str, size_t len, size_t maxlen)
{
	if (len > maxlen) {
		m_freem(infop->nmi_mreq);
		infop->nmi_mreq = NULL;
		*infop->nmi_errorp = ENAMETOOLONG;
		return 1;
	}
	nfsm_strtombuf(&infop->nmi_mb, str, len);
	return 0;
}

#endif
