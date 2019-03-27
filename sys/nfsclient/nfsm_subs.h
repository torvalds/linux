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

#ifndef _NFSCLIENT_NFSM_SUBS_H_
#define _NFSCLIENT_NFSM_SUBS_H_

#include <nfs/nfs_common.h>

#define	nfsv2tov_type(a)	nv2tov_type[fxdr_unsigned(u_int32_t,(a))&0x7]

struct ucred;
struct vnode;

/*
 * These macros do strange and peculiar things to mbuf chains for
 * the assistance of the nfs code. To attempt to use them for any
 * other purpose will be dangerous. (they make weird assumptions)
 */

/*
 * First define what the actual subs. return
 */
u_int32_t nfs_xid_gen(void);

/* *********************************** */
/* Request generation phase macros */

int	nfsm_fhtom_xx(struct vnode *v, int v3, struct mbuf **mb,
	    caddr_t *bpos);
void	nfsm_v3attrbuild_xx(struct vattr *va, int full, struct mbuf **mb,
	    caddr_t *bpos);
int	nfsm_strtom_xx(const char *a, int s, int m, struct mbuf **mb,
	    caddr_t *bpos);

#define nfsm_bcheck(t1, mreq) \
do { \
	if (t1) { \
		error = t1; \
		m_freem(mreq); \
		goto nfsmout; \
	} \
} while (0)

#define nfsm_fhtom(v, v3) \
do { \
	int32_t t1; \
	t1 = nfsm_fhtom_xx((v), (v3), &mb, &bpos); \
	nfsm_bcheck(t1, mreq); \
} while (0)

/* If full is true, set all fields, otherwise just set mode and time fields */
#define nfsm_v3attrbuild(a, full) \
	nfsm_v3attrbuild_xx(a, full, &mb, &bpos)

#define nfsm_uiotom(p, s) \
do { \
	int t1; \
	t1 = nfsm_uiotombuf((p), &mb, (s), &bpos); \
	nfsm_bcheck(t1, mreq); \
} while (0)

#define	nfsm_strtom(a, s, m) \
do { \
	int t1; \
	t1 = nfsm_strtom_xx((a), (s), (m), &mb, &bpos); \
	nfsm_bcheck(t1, mreq); \
} while (0)

/* *********************************** */
/* Send the request */

#define	nfsm_request(v, t, p, c) \
do { \
	sigset_t oldset; \
	nfs_set_sigmask(p, &oldset); \
	error = nfs_request((v), mreq, (t), (p), (c), &mrep, &md, &dpos); \
	nfs_restore_sigmask(p, &oldset); \
	if (error != 0) { \
		if (error & NFSERR_RETERR) \
			error &= ~NFSERR_RETERR; \
		else \
			goto nfsmout; \
	} \
} while (0)

/* *********************************** */
/* Reply interpretation phase macros */

int	nfsm_mtofh_xx(struct vnode *d, struct vnode **v, int v3, int *f,
	    struct mbuf **md, caddr_t *dpos);
int	nfsm_getfh_xx(nfsfh_t **f, int *s, int v3, struct mbuf **md,
	    caddr_t *dpos);
int	nfsm_loadattr_xx(struct vnode **v, struct vattr *va, struct mbuf **md,
	    caddr_t *dpos);
int	nfsm_postop_attr_xx(struct vnode **v, int *f, struct vattr *va,
	    struct mbuf **md, caddr_t *dpos);
int	nfsm_wcc_data_xx(struct vnode **v, int *f, struct mbuf **md,
	    caddr_t *dpos);

#define nfsm_mtofh(d, v, v3, f) \
do { \
	int32_t t1; \
	t1 = nfsm_mtofh_xx((d), &(v), (v3), &(f), &md, &dpos); \
	nfsm_dcheck(t1, mrep); \
} while (0)

#define nfsm_getfh(f, s, v3) \
do { \
	int32_t t1; \
	t1 = nfsm_getfh_xx(&(f), &(s), (v3), &md, &dpos); \
	nfsm_dcheck(t1, mrep); \
} while (0)

#define	nfsm_loadattr(v, a) \
do { \
	int32_t t1; \
	t1 = nfsm_loadattr_xx(&v, a, &md, &dpos); \
	nfsm_dcheck(t1, mrep); \
} while (0)

#define	nfsm_postop_attr(v, f) \
do { \
	int32_t t1; \
	t1 = nfsm_postop_attr_xx(&v, &f, NULL, &md, &dpos);	\
	nfsm_dcheck(t1, mrep); \
} while (0)

#define	nfsm_postop_attr_va(v, f, va)		\
do { \
	int32_t t1; \
	t1 = nfsm_postop_attr_xx(&v, &f, va, &md, &dpos);	\
	nfsm_dcheck(t1, mrep); \
} while (0)

/* Used as (f) for nfsm_wcc_data() */
#define NFSV3_WCCRATTR	0
#define NFSV3_WCCCHK	1

#define	nfsm_wcc_data(v, f) \
do { \
	int32_t t1; \
	t1 = nfsm_wcc_data_xx(&v, &f, &md, &dpos); \
	nfsm_dcheck(t1, mrep); \
} while (0)

#endif
