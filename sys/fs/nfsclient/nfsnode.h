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

#ifndef _NFSCLIENT_NFSNODE_H_
#define	_NFSCLIENT_NFSNODE_H_

#include <sys/_task.h>

/*
 * Silly rename structure that hangs off the nfsnode until the name
 * can be removed by nfs_inactive()
 */
struct sillyrename {
	struct	task s_task;
	struct	ucred *s_cred;
	struct	vnode *s_dvp;
	long	s_namlen;
	char	s_name[32];
};

/*
 * This structure is used to save the logical directory offset to
 * NFS cookie mappings.
 * The mappings are stored in a list headed
 * by n_cookies, as required.
 * There is one mapping for each NFS_DIRBLKSIZ bytes of directory information
 * stored in increasing logical offset byte order.
 */
#define	NFSNUMCOOKIES		31

struct nfsdmap {
	LIST_ENTRY(nfsdmap)	ndm_list;
	int			ndm_eocookie;
	union {
		nfsuint64	ndmu3_cookies[NFSNUMCOOKIES];
		uint64_t	ndmu4_cookies[NFSNUMCOOKIES];
	} ndm_un1;
};

#define	ndm_cookies	ndm_un1.ndmu3_cookies
#define	ndm4_cookies	ndm_un1.ndmu4_cookies

struct nfs_accesscache {
	u_int32_t		mode;	/* ACCESS mode cache */
	uid_t			uid;	/* credentials having mode */
	time_t			stamp;	/* mode cache timestamp */
};

/*
 * The nfsnode is the nfs equivalent to ufs's inode. Any similarity
 * is purely coincidental.
 * There is a unique nfsnode allocated for each active file,
 * each current directory, each mounted-on file, text file, and the root.
 * An nfsnode is 'named' by its file handle. (nget/nfs_node.c)
 * If this structure exceeds 256 bytes (it is currently 256 using 4.4BSD-Lite
 * type definitions), file handles of > 32 bytes should probably be split out
 * into a separate malloc()'d data structure. (Reduce the size of nfsfh_t by
 * changing the definition in nfsproto.h of NFS_SMALLFH.)
 * NB: Hopefully the current order of the fields is such that everything will
 *     be well aligned and, therefore, tightly packed.
 */
struct nfsnode {
	struct mtx 		n_mtx;		/* Protects all of these members */
	struct lock		n_excl;		/* Exclusive helper for shared
						   vnode lock */
	u_quad_t		n_size;		/* Current size of file */
	u_quad_t		n_brev;		/* Modify rev when cached */
	u_quad_t		n_lrev;		/* Modify rev for lease */
	struct nfsvattr		n_vattr;	/* Vnode attribute cache */
	time_t			n_attrstamp;	/* Attr. cache timestamp */
	struct nfs_accesscache	n_accesscache[NFS_ACCESSCACHESIZE];
	struct timespec		n_mtime;	/* Prev modify time. */
	struct nfsfh		*n_fhp;		/* NFS File Handle */
	struct vnode		*n_vnode;	/* associated vnode */
	struct vnode		*n_dvp;		/* parent vnode */
	struct lockf		*n_lockf;	/* Locking record of file */
	int			n_error;	/* Save write error value */
	union {
		struct timespec	nf_atim;	/* Special file times */
		nfsuint64	nd_cookieverf;	/* Cookie verifier (dir only) */
		u_char		nd4_cookieverf[NFSX_VERF];
	} n_un1;
	union {
		struct timespec	nf_mtim;
		off_t		nd_direof;	/* Dir. EOF offset cache */
	} n_un2;
	union {
		struct sillyrename *nf_silly;	/* Ptr to silly rename struct */
		LIST_HEAD(, nfsdmap) nd_cook;	/* cookies */
	} n_un3;
	short			n_fhsize;	/* size in bytes, of fh */
	u_int32_t		n_flag;		/* Flag for locking.. */
	int			n_directio_opens;
	int                     n_directio_asyncwr;
	u_int64_t		 n_change;	/* old Change attribute */
	struct nfsv4node	*n_v4;		/* extra V4 stuff */
	struct ucred		*n_writecred;	/* Cred. for putpages */
};

#define	n_atim		n_un1.nf_atim
#define	n_mtim		n_un2.nf_mtim
#define	n_sillyrename	n_un3.nf_silly
#define	n_cookieverf	n_un1.nd_cookieverf
#define	n4_cookieverf	n_un1.nd4_cookieverf
#define	n_direofoffset	n_un2.nd_direof
#define	n_cookies	n_un3.nd_cook

/*
 * Flags for n_flag
 */
#define	NDIRCOOKIELK	0x00000001  /* Lock to serialize access to directory cookies */
#define	NFSYNCWAIT      0x00000002  /* fsync waiting for all directio async
				  writes to drain */
#define	NMODIFIED	0x00000004  /* Might have a modified buffer in bio */
#define	NWRITEERR	0x00000008  /* Flag write errors so close will know */
#define	NCREATED	0x00000010  /* Opened by nfs_create() */
#define	NTRUNCATE	0x00000020  /* Opened by nfs_setattr() */
#define	NSIZECHANGED	0x00000040  /* File size has changed: need cache inval */
#define	NNONCACHE	0x00000080  /* Node marked as noncacheable */
#define	NACC		0x00000100  /* Special file accessed */
#define	NUPD		0x00000200  /* Special file updated */
#define	NCHG		0x00000400  /* Special file times changed */
#define	NDELEGMOD	0x00000800  /* Modified delegation */
#define	NDELEGRECALL	0x00001000  /* Recall in progress */
#define	NREMOVEINPROG	0x00002000  /* Remove in progress */
#define	NREMOVEWANT	0x00004000  /* Want notification that remove is done */
#define	NLOCK		0x00008000  /* Sleep lock the node */
#define	NLOCKWANT	0x00010000  /* Want the sleep lock */
#define	NNOLAYOUT	0x00020000  /* Can't get a layout for this file */
#define	NWRITEOPENED	0x00040000  /* Has been opened for writing */
#define	NHASBEENLOCKED	0x00080000  /* Has been file locked. */
#define	NDSCOMMIT	0x00100000  /* Commit is done via the DS. */

/*
 * Convert between nfsnode pointers and vnode pointers
 */
#define	VTONFS(vp)	((struct nfsnode *)(vp)->v_data)
#define	NFSTOV(np)	((struct vnode *)(np)->n_vnode)

#define	NFS_TIMESPEC_COMPARE(T1, T2)	(((T1)->tv_sec != (T2)->tv_sec) || ((T1)->tv_nsec != (T2)->tv_nsec))

#if defined(_KERNEL)

/*
 * Prototypes for NFS vnode operations
 */
int	ncl_getpages(struct vop_getpages_args *);
int	ncl_putpages(struct vop_putpages_args *);
int	ncl_write(struct vop_write_args *);
int	ncl_inactive(struct vop_inactive_args *);
int	ncl_reclaim(struct vop_reclaim_args *);

/* other stuff */
int	ncl_removeit(struct sillyrename *, struct vnode *);
int	ncl_nget(struct mount *, u_int8_t *, int, struct nfsnode **, int);
nfsuint64 *ncl_getcookie(struct nfsnode *, off_t, int);
void	ncl_invaldir(struct vnode *);
bool	ncl_excl_start(struct vnode *);
void	ncl_excl_finish(struct vnode *, bool old_lock);
void	ncl_dircookie_lock(struct nfsnode *);
void	ncl_dircookie_unlock(struct nfsnode *);

#endif /* _KERNEL */

#endif	/* _NFSCLIENT_NFSNODE_H_ */
