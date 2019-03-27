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

#ifndef _NFS_NFS_COMMON_H_
#define _NFS_NFS_COMMON_H_

extern enum vtype nv3tov_type[];
extern nfstype nfsv3_type[];

#define	vtonfsv2_mode(t, m) \
    txdr_unsigned(((t) == VFIFO) ? MAKEIMODE(VCHR, (m)) : MAKEIMODE((t), (m)))

#define	nfsv3tov_type(a)	nv3tov_type[fxdr_unsigned(u_int32_t,(a))&0x7]
#define	vtonfsv3_type(a)	txdr_unsigned(nfsv3_type[((int32_t)(a))])

int	nfs_adv(struct mbuf **, caddr_t *, int, int);
void	*nfsm_disct(struct mbuf **, caddr_t *, int, int, int);
int	nfs_realign(struct mbuf **, int);

/* ****************************** */
/* Build request/reply phase macros */

void	*nfsm_build_xx(int s, struct mbuf **mb, caddr_t *bpos);

#define	nfsm_build(c, s) \
	(c)nfsm_build_xx((s), &mb, &bpos)

/* ****************************** */
/* Interpretation phase macros */

void	*nfsm_dissect_xx(int s, struct mbuf **md, caddr_t *dpos);
void	*nfsm_dissect_xx_nonblock(int s, struct mbuf **md, caddr_t *dpos);
int	nfsm_strsiz_xx(int *s, int m, struct mbuf **md, caddr_t *dpos);
int	nfsm_adv_xx(int s, struct mbuf **md, caddr_t *dpos);

/* Error check helpers */
#define nfsm_dcheck(t1, mrep) \
do { \
	if (t1 != 0) { \
		error = t1; \
		m_freem((mrep)); \
		(mrep) = NULL; \
		goto nfsmout; \
	} \
} while (0)

#define nfsm_dcheckp(retp, mrep) \
do { \
	if (retp == NULL) { \
		error = EBADRPC; \
		m_freem((mrep)); \
		(mrep) = NULL; \
		goto nfsmout; \
	} \
} while (0)

#define	nfsm_dissect(c, s) \
({ \
	void *ret; \
	ret = nfsm_dissect_xx((s), &md, &dpos); \
	nfsm_dcheckp(ret, mrep); \
	(c)ret; \
})

#define	nfsm_dissect_nonblock(c, s) \
({ \
	void *ret; \
	ret = nfsm_dissect_xx_nonblock((s), &md, &dpos); \
	nfsm_dcheckp(ret, mrep); \
	(c)ret; \
})

#define	nfsm_strsiz(s,m) \
do { \
	int t1; \
	t1 = nfsm_strsiz_xx(&(s), (m), &md, &dpos); \
	nfsm_dcheck(t1, mrep); \
} while(0)

#define nfsm_mtouio(p,s) \
do {\
	int32_t t1 = 0; \
	if ((s) > 0) \
		t1 = nfsm_mbuftouio(&md, (p), (s), &dpos); \
	nfsm_dcheck(t1, mrep); \
} while (0)

#define nfsm_rndup(a)	(((a)+3)&(~0x3))

#define	nfsm_adv(s) \
do { \
	int t1; \
	t1 = nfsm_adv_xx((s), &md, &dpos); \
	nfsm_dcheck(t1, mrep); \
} while (0)

#ifdef __NO_STRICT_ALIGNMENT
#define nfsm_aligned(p, t)	1
#else
#define nfsm_aligned(p, t)	((((u_long)(p)) & (sizeof(t) - 1)) == 0)
#endif

#endif
