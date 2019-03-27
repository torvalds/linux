/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * $FreeBSD$
 */

#ifndef _NFSSERVER_NFSM_SUBS_H_
#define _NFSSERVER_NFSM_SUBS_H_

#include <nfs/nfs_common.h>

#define	nfstov_mode(a)	(fxdr_unsigned(u_int32_t, (a)) & ALLPERMS)

/*
 * These macros do strange and peculiar things to mbuf chains for
 * the assistance of the nfs code. To attempt to use them for any
 * other purpose will be dangerous. (they make weird assumptions)
 */

/*
 * Now for the macros that do the simple stuff and call the functions
 * for the hard stuff.
 * These macros use several vars. declared in nfsm_reqhead and these
 * vars. must not be used elsewhere unless you are careful not to corrupt
 * them. The vars. starting with pN and tN (N=1,2,3,..) are temporaries
 * that may be used so long as the value is not expected to retained
 * after a macro.
 * I know, this is kind of dorkey, but it makes the actual op functions
 * fairly clean and deals with the mess caused by the xdr discriminating
 * unions.
 */



/* ************************************* */
/* Dissection phase macros */

int	nfsm_srvstrsiz_xx(int *s, int m, struct mbuf **md, caddr_t *dpos);
int	nfsm_srvnamesiz_xx(int *s, int m, struct mbuf **md, caddr_t *dpos);
int	nfsm_srvnamesiz0_xx(int *s, int m, struct mbuf **md, caddr_t *dpos);
int	nfsm_srvmtofh_xx(fhandle_t *f, int v3, struct mbuf **md, caddr_t *dpos);
int	nfsm_srvsattr_xx(struct vattr *a, struct mbuf **md, caddr_t *dpos);

#define	nfsm_srvstrsiz(s, m) \
do { \
	int t1; \
	t1 = nfsm_srvstrsiz_xx(&(s), (m), &md, &dpos); \
	if (t1) { \
		error = t1; \
		nfsm_reply(0); \
	} \
} while (0)

#define	nfsm_srvnamesiz(s) \
do { \
	int t1; \
	t1 = nfsm_srvnamesiz_xx(&(s), NFS_MAXNAMLEN, &md, &dpos); \
	if (t1) { \
		error = t1; \
		nfsm_reply(0); \
	} \
} while (0)

#define	nfsm_srvpathsiz(s) \
do { \
	int t1; \
	t1 = nfsm_srvnamesiz0_xx(&(s), NFS_MAXPATHLEN, &md, &dpos); \
	if (t1) { \
		error = t1; \
		nfsm_reply(0); \
	} \
} while (0)

#define nfsm_srvmtofh(f) \
do { \
	int t1; \
	t1 = nfsm_srvmtofh_xx((f), nfsd->nd_flag & ND_NFSV3, &md, &dpos); \
	if (t1) { \
		error = t1; \
		nfsm_reply(0); \
	} \
} while (0)

/* XXX why is this different? */
#define nfsm_srvsattr(a) \
do { \
	int t1; \
	t1 = nfsm_srvsattr_xx((a), &md, &dpos); \
	if (t1) { \
		error = t1; \
		m_freem(mrep); \
		mrep = NULL; \
		goto nfsmout; \
	} \
} while (0)

/* ************************************* */
/* Prepare the reply */

#define	nfsm_reply(s) \
do { \
	if (mrep != NULL) { \
		m_freem(mrep); \
		mrep = NULL; \
	} \
	mreq = nfs_rephead((s), nfsd, error, &mb, &bpos); \
	*mrq = mreq; \
	if (error == EBADRPC) { \
		error = 0; \
		goto nfsmout; \
	} \
} while (0)

#define	nfsm_writereply(s) \
do { \
	mreq = nfs_rephead((s), nfsd, error, &mb, &bpos); \
} while(0)

/* ************************************* */
/* Reply phase macros - add additional reply info */

void	nfsm_srvfhtom_xx(fhandle_t *f, int v3, struct mbuf **mb,
	    caddr_t *bpos);
void	nfsm_srvpostop_fh_xx(fhandle_t *f, struct mbuf **mb, caddr_t *bpos);
void	nfsm_clget_xx(u_int32_t **tl, struct mbuf *mb, struct mbuf **mp,
	    char **bp, char **be, caddr_t bpos);

#define nfsm_srvfhtom(f, v3) \
	nfsm_srvfhtom_xx((f), (v3), &mb, &bpos)

#define nfsm_srvpostop_fh(f) \
	nfsm_srvpostop_fh_xx((f), &mb, &bpos)

#define nfsm_srvwcc_data(br, b, ar, a) \
	nfsm_srvwcc(nfsd, (br), (b), (ar), (a), &mb, &bpos)

#define nfsm_srvpostop_attr(r, a) \
	nfsm_srvpostopattr(nfsd, (r), (a), &mb, &bpos)

#define	nfsm_srvfillattr(a, f) \
	nfsm_srvfattr(nfsd, (a), (f))

#define nfsm_clget \
	nfsm_clget_xx(&tl, mb, &mp, &bp, &be, bpos)

#endif
