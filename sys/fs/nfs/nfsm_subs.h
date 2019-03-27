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
 * $FreeBSD$
 */

#ifndef _NFS_NFSM_SUBS_H_
#define	_NFS_NFSM_SUBS_H_


/*
 * These macros do strange and peculiar things to mbuf chains for
 * the assistance of the nfs code. To attempt to use them for any
 * other purpose will be dangerous. (they make weird assumptions)
 */

#ifndef APPLE
/*
 * First define what the actual subs. return
 */
#define	NFSM_DATAP(m, s)	(m)->m_data += (s)

/*
 * Now for the macros that do the simple stuff and call the functions
 * for the hard stuff.
 * They use fields in struct nfsrv_descript to handle the mbuf queues.
 * Replace most of the macro with an inline function, to minimize
 * the machine code. The inline functions in lower case can be called
 * directly, bypassing the macro.
 */
static __inline void *
nfsm_build(struct nfsrv_descript *nd, int siz)
{
	void *retp;
	struct mbuf *mb2;

	if (siz > M_TRAILINGSPACE(nd->nd_mb)) {
		NFSMCLGET(mb2, M_NOWAIT);
		if (siz > MLEN)
			panic("build > MLEN");
		mbuf_setlen(mb2, 0);
		nd->nd_bpos = NFSMTOD(mb2, caddr_t);
		nd->nd_mb->m_next = mb2;
		nd->nd_mb = mb2;
	}
	retp = (void *)(nd->nd_bpos);
	nd->nd_mb->m_len += siz;
	nd->nd_bpos += siz;
	return (retp);
}

#define	NFSM_BUILD(a, c, s)	((a) = (c)nfsm_build(nd, (s)))

static __inline void *
nfsm_dissect(struct nfsrv_descript *nd, int siz)
{
	int tt1; 
	void *retp;

	tt1 = NFSMTOD(nd->nd_md, caddr_t) + nd->nd_md->m_len - nd->nd_dpos; 
	if (tt1 >= siz) { 
		retp = (void *)nd->nd_dpos; 
		nd->nd_dpos += siz; 
	} else { 
		retp = nfsm_dissct(nd, siz, M_WAITOK); 
	}
	return (retp);
}

static __inline void *
nfsm_dissect_nonblock(struct nfsrv_descript *nd, int siz)
{
	int tt1; 
	void *retp;

	tt1 = NFSMTOD(nd->nd_md, caddr_t) + nd->nd_md->m_len - nd->nd_dpos; 
	if (tt1 >= siz) { 
		retp = (void *)nd->nd_dpos; 
		nd->nd_dpos += siz; 
	} else { 
		retp = nfsm_dissct(nd, siz, M_NOWAIT); 
	}
	return (retp);
}

#define	NFSM_DISSECT(a, c, s) 						\
	do {								\
		(a) = (c)nfsm_dissect(nd, (s));	 			\
		if ((a) == NULL) { 					\
			error = EBADRPC; 				\
			goto nfsmout; 					\
		}							\
	} while (0)

#define	NFSM_DISSECT_NONBLOCK(a, c, s) 					\
	do {								\
		(a) = (c)nfsm_dissect_nonblock(nd, (s));		\
		if ((a) == NULL) { 					\
			error = EBADRPC; 				\
			goto nfsmout; 					\
		}							\
	} while (0)
#endif	/* !APPLE */

#define	NFSM_STRSIZ(s, m)  						\
	do {								\
		tl = (u_int32_t *)nfsm_dissect(nd, NFSX_UNSIGNED);	\
		if (!tl || ((s) = fxdr_unsigned(int32_t, *tl)) > (m)) { \
			error = EBADRPC; 				\
			goto nfsmout; 					\
		}							\
	} while (0)

#define	NFSM_RNDUP(a)	(((a)+3)&(~0x3))

#endif	/* _NFS_NFSM_SUBS_H_ */
