/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Rick Macklem, University of Guelph
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _NFS_NFSKPIPORT_H_
#define	_NFS_NFSKPIPORT_H_
/*
 * These definitions are needed since the generic code is now using Darwin8
 * KPI stuff. (I know, seems a bit silly, but I want the code to build on
 * Darwin8 and hopefully subsequent releases from Apple.)
 */
typedef	struct mount *		mount_t;
#define	vfs_statfs(m)		(&((m)->mnt_stat))
#define	vfs_flags(m)		((m)->mnt_flag)

typedef struct vnode *		vnode_t;
#define	vnode_mount(v)		((v)->v_mount)
#define	vnode_vtype(v)		((v)->v_type)

typedef struct mbuf *		mbuf_t;
#define	mbuf_freem(m)		m_freem(m)
#define	mbuf_data(m)		mtod((m), void *)
#define	mbuf_len(m)		((m)->m_len)
#define	mbuf_next(m)		((m)->m_next)
#define	mbuf_setlen(m, l)	((m)->m_len = (l))
#define	mbuf_setnext(m, p)	((m)->m_next = (p))
#define	mbuf_pkthdr_len(m)	((m)->m_pkthdr.len)
#define	mbuf_pkthdr_setlen(m, l) ((m)->m_pkthdr.len = (l))
#define	mbuf_pkthdr_setrcvif(m, p) ((m)->m_pkthdr.rcvif = (p))

/*
 * This stuff is needed by Darwin for handling the uio structure.
 */
#define	CAST_USER_ADDR_T(a)	(a)
#define	CAST_DOWN(c, a)		((c) (a))
#define	uio_uio_resid(p)	((p)->uio_resid)
#define	uio_uio_resid_add(p, v)	((p)->uio_resid += (v))
#define	uio_uio_resid_set(p, v)	((p)->uio_resid = (v))
#define	uio_iov_base(p)		((p)->uio_iov->iov_base)
#define	uio_iov_base_add(p, v)	do {					\
	char *pp;							\
	pp = (char *)(p)->uio_iov->iov_base;				\
	pp += (v);							\
	(p)->uio_iov->iov_base = (void *)pp;				\
    } while (0)
#define	uio_iov_len(p)		((p)->uio_iov->iov_len)
#define	uio_iov_len_add(p, v)	((p)->uio_iov->iov_len += (v))

#endif	/* _NFS_NFSKPIPORT_H */
