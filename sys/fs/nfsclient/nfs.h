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

#ifndef _NFSCLIENT_NFS_H_
#define	_NFSCLIENT_NFS_H_

#if defined(_KERNEL)

#ifndef NFS_TPRINTF_INITIAL_DELAY
#define	NFS_TPRINTF_INITIAL_DELAY       12
#endif

#ifndef NFS_TPRINTF_DELAY
#define	NFS_TPRINTF_DELAY               30
#endif

/*
 * Nfs version macros.
 */
#define	NFS_ISV3(v) \
	(VFSTONFS((v)->v_mount)->nm_flag & NFSMNT_NFSV3)
#define	NFS_ISV4(v) \
	(VFSTONFS((v)->v_mount)->nm_flag & NFSMNT_NFSV4)
#define	NFS_ISV34(v) \
	(VFSTONFS((v)->v_mount)->nm_flag & (NFSMNT_NFSV3 | NFSMNT_NFSV4))

#ifdef NFS_DEBUG

extern int nfs_debug;
#define	NFS_DEBUG_ASYNCIO	1 /* asynchronous i/o */
#define	NFS_DEBUG_WG		2 /* server write gathering */
#define	NFS_DEBUG_RC		4 /* server request caching */

#define	NFS_DPF(cat, args)					\
	do {							\
		if (nfs_debug & NFS_DEBUG_##cat) printf args;	\
	} while (0)

#else

#define	NFS_DPF(cat, args)

#endif

/*
 * NFS iod threads can be in one of these three states once spawned.
 * NFSIOD_NOT_AVAILABLE - Cannot be assigned an I/O operation at this time.
 * NFSIOD_AVAILABLE - Available to be assigned an I/O operation.
 * NFSIOD_CREATED_FOR_NFS_ASYNCIO - Newly created for nfs_asyncio() and
 *	will be used by the thread that called nfs_asyncio().
 */
enum nfsiod_state {
	NFSIOD_NOT_AVAILABLE = 0,
	NFSIOD_AVAILABLE = 1,
	NFSIOD_CREATED_FOR_NFS_ASYNCIO = 2,
};

/*
 * Function prototypes.
 */
int ncl_meta_setsize(struct vnode *, struct ucred *, struct thread *,
    u_quad_t);
void ncl_doio_directwrite(struct buf *);
int ncl_bioread(struct vnode *, struct uio *, int, struct ucred *);
int ncl_biowrite(struct vnode *, struct uio *, int, struct ucred *);
int ncl_vinvalbuf(struct vnode *, int, struct thread *, int);
int ncl_asyncio(struct nfsmount *, struct buf *, struct ucred *,
    struct thread *);
int ncl_doio(struct vnode *, struct buf *, struct ucred *, struct thread *,
    int);
void ncl_nhinit(void);
void ncl_nhuninit(void);
void ncl_nodelock(struct nfsnode *);
void ncl_nodeunlock(struct nfsnode *);
int ncl_getattrcache(struct vnode *, struct vattr *);
int ncl_readrpc(struct vnode *, struct uio *, struct ucred *);
int ncl_writerpc(struct vnode *, struct uio *, struct ucred *, int *, int *,
    int);
int ncl_readlinkrpc(struct vnode *, struct uio *, struct ucred *);
int ncl_readdirrpc(struct vnode *, struct uio *, struct ucred *,
    struct thread *);
int ncl_readdirplusrpc(struct vnode *, struct uio *, struct ucred *,
    struct thread *);
int ncl_writebp(struct buf *, int, struct thread *);
int ncl_commit(struct vnode *, u_quad_t, int, struct ucred *, struct thread *);
void ncl_clearcommit(struct mount *);
int ncl_fsinfo(struct nfsmount *, struct vnode *, struct ucred *,
    struct thread *);
int ncl_init(struct vfsconf *);
int ncl_uninit(struct vfsconf *);
void	ncl_nfsiodnew(void);
void	ncl_nfsiodnew_tq(__unused void *, int);

#endif	/* _KERNEL */

#endif	/* _NFSCLIENT_NFS_H_ */
