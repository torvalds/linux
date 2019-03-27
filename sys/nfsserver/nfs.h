/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1995
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
 *	@(#)nfs.h	8.4 (Berkeley) 5/1/95
 * $FreeBSD$
 */

#ifndef _NFSSERVER_NFS_H_
#define _NFSSERVER_NFS_H_

#ifdef _KERNEL
#include "opt_nfs.h"
#endif

#include <nfs/nfssvc.h>

/*
 * Tunable constants for nfs
 */

#define NFS_TICKINTVL	10		/* Desired time for a tick (msec) */
#define NFS_HZ		(hz / nfs_ticks) /* Ticks/sec */
#define	NFS_TIMEO	(1 * NFS_HZ)	/* Default timeout = 1 second */
#define	NFS_MINTIMEO	(1 * NFS_HZ)	/* Min timeout to use */
#define	NFS_MAXTIMEO	(60 * NFS_HZ)	/* Max timeout to backoff to */
#define	NFS_MINIDEMTIMEO (5 * NFS_HZ)	/* Min timeout for non-idempotent ops*/
#define	NFS_MAXUIDHASH	64		/* Max. # of hashed uid entries/mp */
#ifndef NFS_GATHERDELAY
#define NFS_GATHERDELAY		10	/* Default write gather delay (msec) */
#endif
#ifdef _KERNEL
#define	DIRBLKSIZ	512		/* XXX we used to use ufs's DIRBLKSIZ */
#endif

/*
 * Oddballs
 */
#define NFS_SRVMAXDATA(n) \
		(((n)->nd_flag & ND_NFSV3) ? (((n)->nd_nam2) ? \
		 NFS_MAXDGRAMDATA : NFS_MAXDATA) : NFS_V2MAXDATA)

/*
 * XXX
 * The B_INVAFTERWRITE flag should be set to whatever is required by the
 * buffer cache code to say "Invalidate the block after it is written back".
 */
#define	B_INVAFTERWRITE	B_NOCACHE

/*
 * The IO_METASYNC flag should be implemented for local filesystems.
 * (Until then, it is nothin at all.)
 */
#ifndef IO_METASYNC
#define IO_METASYNC	0
#endif

/* NFS state flags XXX -Wunused */
#define	NFSRV_SNDLOCK		0x01000000  /* Send socket lock */
#define	NFSRV_WANTSND		0x02000000  /* Want above */

/*
 * Structures for the nfssvc(2) syscall.  Not that anyone but nfsd and
 * mount_nfs should ever try and use it.
 */

/*
 * Add a socket to monitor for NFS requests.
 */
struct nfsd_addsock_args {
	int	sock;		/* Socket to serve */
	caddr_t	name;		/* Client addr for connection based sockets */
	int	namelen;	/* Length of name */
};

/*
 * Start processing requests.
 */
struct nfsd_nfsd_args {
	const char *principal;	/* GSS-API service principal name */
	int	minthreads;	/* minimum service thread count */
	int	maxthreads;	/* maximum service thread count */
};

/*
 * XXX to allow amd to include nfs.h without nfsproto.h
 */
#ifdef NFS_NPROCS
#include <nfsserver/nfsrvstats.h>
#endif

/*
 * vfs.nfsrv sysctl(3) identifiers
 */
#define NFS_NFSRVSTATS	1		/* struct: struct nfsrvstats */
#define NFS_NFSPRIVPORT	2		/* int: prohibit nfs to resvports */

#ifdef _KERNEL

extern struct mtx nfsd_mtx;
#define	NFSD_LOCK_ASSERT()	mtx_assert(&nfsd_mtx, MA_OWNED)
#define	NFSD_UNLOCK_ASSERT()	mtx_assert(&nfsd_mtx, MA_NOTOWNED)
#define	NFSD_LOCK_DONTCARE()
#define	NFSD_LOCK()	mtx_lock(&nfsd_mtx)
#define	NFSD_UNLOCK()	mtx_unlock(&nfsd_mtx)

#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_NFSRVDESC);
MALLOC_DECLARE(M_NFSD);
#endif

/* Forward declarations */
struct nfssvc_sock;
struct nfsrv_descript;
struct uio;
struct vattr;
struct nameidata;

extern struct callout nfsrv_callout;
extern struct nfsrvstats nfsrvstats;

extern int	nfsrv_ticks;
extern int	nfsrvw_procrastinate;
extern int	nfsrvw_procrastinate_v3;
extern int 	nfsrv_numnfsd;

/* Various values converted to XDR form. */
extern u_int32_t nfsrv_nfs_false, nfsrv_nfs_true, nfsrv_nfs_xdrneg1,
	nfsrv_nfs_prog;
extern u_int32_t nfsrv_rpc_reply, nfsrv_rpc_msgdenied, nfsrv_rpc_mismatch,
	nfsrv_rpc_vers;
extern u_int32_t nfsrv_rpc_auth_unix, nfsrv_rpc_msgaccepted, nfsrv_rpc_call,
	nfsrv_rpc_autherr;

/* Procedure table data */
extern const int	nfsrvv2_procid[NFS_NPROCS];
extern const int	nfsrv_nfsv3_procid[NFS_NPROCS];
extern int32_t (*nfsrv3_procs[NFS_NPROCS])(struct nfsrv_descript *nd,
		    struct nfssvc_sock *slp, struct mbuf **mreqp);

/*
 * A list of nfssvc_sock structures is maintained with all the sockets
 * that require service by the nfsd.
 */
#ifndef NFS_WDELAYHASHSIZ
#define	NFS_WDELAYHASHSIZ 16	/* and with this */
#endif
#define	NWDELAYHASH(sock, f) \
	(&(sock)->ns_wdelayhashtbl[(*((u_int32_t *)(f))) % NFS_WDELAYHASHSIZ])

/*
 * This structure is used by the server for describing each request.
 */
struct nfsrv_descript {
	struct mbuf		*nd_mrep;	/* Request mbuf list */
	struct mbuf		*nd_md;		/* Current dissect mbuf */
	struct mbuf		*nd_mreq;	/* Reply mbuf list */
	struct sockaddr		*nd_nam;	/* and socket addr */
	struct sockaddr		*nd_nam2;	/* return socket addr */
	caddr_t			nd_dpos;	/* Current dissect pos */
	u_int32_t		nd_procnum;	/* RPC # */
	int			nd_stable;	/* storage type */
	int			nd_flag;	/* nd_flag */
	int			nd_repstat;	/* Reply status */
	fhandle_t		nd_fh;		/* File handle */
	struct ucred		*nd_cr;		/* Credentials */
	int			nd_credflavor;	/* Security flavor */
};

/* Bits for "nd_flag" */
#define ND_NFSV3	0x08

/*
 * Defines for WebNFS
 */

#define WEBNFS_ESC_CHAR		'%'
#define WEBNFS_SPECCHAR_START	0x80

#define WEBNFS_NATIVE_CHAR	0x80
/*
 * ..
 * Possibly more here in the future.
 */

/*
 * Macro for converting escape characters in WebNFS pathnames.
 * Should really be in libkern.
 */

#define HEXTOC(c) \
	((c) >= 'a' ? ((c) - ('a' - 10)) : \
	    ((c) >= 'A' ? ((c) - ('A' - 10)) : ((c) - '0')))
#define HEXSTRTOI(p) \
	((HEXTOC(p[0]) << 4) + HEXTOC(p[1]))

#ifdef NFS_DEBUG

extern int nfs_debug;
#define NFS_DEBUG_ASYNCIO	1 /* asynchronous i/o */
#define NFS_DEBUG_WG		2 /* server write gathering */
#define NFS_DEBUG_RC		4 /* server request caching */

#define NFS_DPF(cat, args)					\
	do {							\
		if (nfs_debug & NFS_DEBUG_##cat) printf args;	\
	} while (0)

#else

#define NFS_DPF(cat, args)

#endif

/*
 * The following flags can be passed to nfsrv_fhtovp() function.
 */
/* Leave file system busy on success. */
#define	NFSRV_FLAG_BUSY		0x01

struct mbuf *nfs_rephead(int, struct nfsrv_descript *, int, struct mbuf **,
	    caddr_t *);
void	nfsm_srvfattr(struct nfsrv_descript *, struct vattr *,
	    struct nfs_fattr *);
void	nfsm_srvwcc(struct nfsrv_descript *, int, struct vattr *, int,
	    struct vattr *, struct mbuf **, char **);
void	nfsm_srvpostopattr(struct nfsrv_descript *, int, struct vattr *,
	    struct mbuf **, char **);
int	nfs_namei(struct nameidata *, struct nfsrv_descript *, fhandle_t *,
	    int, struct nfssvc_sock *, struct sockaddr *, struct mbuf **,
	    caddr_t *, struct vnode **, int, struct vattr *, int *, int);
void	nfsm_adj(struct mbuf *, int, int);
int	nfsm_mbuftouio(struct mbuf **, struct uio *, int, caddr_t *);
void	nfsrv_init(int);
int	nfsrv_errmap(struct nfsrv_descript *, int);
void	nfsrvw_sort(gid_t *, int);

int	nfsrv3_access(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_commit(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_create(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_fhtovp(fhandle_t *, int, struct vnode **,
	    struct nfsrv_descript *, struct nfssvc_sock *, struct sockaddr *,
	    int *);
int	nfsrv_setpublicfs(struct mount *, struct netexport *,
	    struct export_args *);
int	nfs_ispublicfh(fhandle_t *);
int	nfsrv_fsinfo(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_getattr(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_link(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_lookup(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_mkdir(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_mknod(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_noop(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_null(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_pathconf(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_read(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_readdir(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_readdirplus(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_readlink(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_remove(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_rename(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_rmdir(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_setattr(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_statfs(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_symlink(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
int	nfsrv_write(struct nfsrv_descript *nfsd, struct nfssvc_sock *slp,
	    struct mbuf **mrq);
/*
 * #ifdef _SYS_SYSPROTO_H_ so that it is only defined when sysproto.h
 * has been included, so that "struct nfssvc_args" is defined.
 */
#ifdef _SYS_SYSPROTO_H_
int nfssvc_nfsserver(struct thread *, struct nfssvc_args *);
#endif
#endif	/* _KERNEL */

#endif
