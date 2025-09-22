/*	$OpenBSD: nfs_var.h,v 1.66 2024/09/09 03:50:14 jsg Exp $	*/
/*	$NetBSD: nfs_var.h,v 1.3 1996/02/18 11:53:54 fvdl Exp $	*/

/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * XXX needs <nfs/rpcv2.h> and <nfs/nfs.h> because of typedefs.
 */
#ifndef _NFS_NFS_VAR_H_
#define _NFS_NFS_VAR_H_

#ifdef _KERNEL

struct nfsnode;
struct sillyrename;
struct componentname;
struct nfs_diskless;
struct nfsm_info;

/* nfs_bio.c */
int nfs_bioread(struct vnode *, struct uio *, int, struct ucred *);
int nfs_write(void *);
int nfs_vinvalbuf(struct vnode *, int, struct ucred *, struct proc *);
int nfs_asyncio(struct buf *, int readahead);
int nfs_doio(struct buf *, struct proc *);

/* nfs_boot.c */
int nfs_boot_init(struct nfs_diskless *, struct proc *);

/* nfs_node.c */
void nfs_ninit(struct nfsmount *);
int nfs_nget(struct mount *, nfsfh_t *, int, struct nfsnode **);
int nfs_inactive(void *);
int nfs_reclaim(void *);

/* nfs_vnops.c */
int nfs_readlink(void *);
int nfs_readlinkrpc(struct vnode *, struct uio *, struct ucred *);
int nfs_readrpc(struct vnode *, struct uio *);
int nfs_writerpc(struct vnode *, struct uio *, int *, int *);
int nfs_removeit(struct sillyrename *);
int nfs_writebp(struct buf *, int);

#define	nfs_ioctl	((int (*)(void *))enoioctl)

/* nfs_serv.c */
int nfsrv3_access(struct nfsrv_descript *, struct nfssvc_sock *,
		       struct proc *, struct mbuf **);
int nfsrv_getattr(struct nfsrv_descript *, struct nfssvc_sock *,
		       struct proc *, struct mbuf **);
int nfsrv_setattr(struct nfsrv_descript *, struct nfssvc_sock *,
		       struct proc *, struct mbuf **);
int nfsrv_lookup(struct nfsrv_descript *, struct nfssvc_sock *,
		      struct proc *, struct mbuf **);
int nfsrv_readlink(struct nfsrv_descript *, struct nfssvc_sock *,
			struct proc *, struct mbuf **);
int nfsrv_read(struct nfsrv_descript *, struct nfssvc_sock *,
		    struct proc *, struct mbuf **);
int nfsrv_write(struct nfsrv_descript *, struct nfssvc_sock *,
		     struct proc *, struct mbuf **);
int nfsrv_create(struct nfsrv_descript *, struct nfssvc_sock *,
		      struct proc *, struct mbuf **);
int nfsrv_mknod(struct nfsrv_descript *, struct nfssvc_sock *,
		     struct proc *, struct mbuf **);
int nfsrv_remove(struct nfsrv_descript *, struct nfssvc_sock *,
		      struct proc *, struct mbuf **);
int nfsrv_rename(struct nfsrv_descript *, struct nfssvc_sock *,
		      struct proc *, struct mbuf **);
int nfsrv_link(struct nfsrv_descript *, struct nfssvc_sock *,
		    struct proc *, struct mbuf **);
int nfsrv_symlink(struct nfsrv_descript *, struct nfssvc_sock *,
		       struct proc *, struct mbuf **);
int nfsrv_mkdir(struct nfsrv_descript *, struct nfssvc_sock *,
		     struct proc *, struct mbuf **);
int nfsrv_rmdir(struct nfsrv_descript *, struct nfssvc_sock *,
		     struct proc *, struct mbuf **);
int nfsrv_readdir(struct nfsrv_descript *, struct nfssvc_sock *,
		       struct proc *, struct mbuf **);
int nfsrv_readdirplus(struct nfsrv_descript *, struct nfssvc_sock *,
			   struct proc *, struct mbuf **);
int nfsrv_commit(struct nfsrv_descript *, struct nfssvc_sock *,
		      struct proc *, struct mbuf **);
int nfsrv_statfs(struct nfsrv_descript *, struct nfssvc_sock *,
		      struct proc *, struct mbuf **);
int nfsrv_fsinfo(struct nfsrv_descript *, struct nfssvc_sock *,
		      struct proc *, struct mbuf **);
int nfsrv_pathconf(struct nfsrv_descript *, struct nfssvc_sock *,
		        struct proc *, struct mbuf **);
int nfsrv_null(struct nfsrv_descript *, struct nfssvc_sock *,
		    struct proc *, struct mbuf **);
int nfsrv_noop(struct nfsrv_descript *, struct nfssvc_sock *,
		    struct proc *, struct mbuf **);

/* nfs_socket.c */
int nfs_connect(struct nfsmount *, struct nfsreq *);
void nfs_disconnect(struct nfsmount *);
int nfs_send(struct socket *, struct mbuf *, struct mbuf *,
		  struct nfsreq *);
int nfs_request(struct vnode *, int, struct nfsm_info *);
int nfs_rephead(int, struct nfsrv_descript *, struct nfssvc_sock *, int,
		struct mbuf **, struct mbuf **);
void nfs_timer(void *);
int nfs_sigintr(struct nfsmount *, struct nfsreq *, struct proc *);
int nfs_sndlock(int *, struct nfsreq *);
void nfs_sndunlock(int *);
void nfsrv_rcv(struct socket *, caddr_t, int);
int nfsrv_dorec(struct nfssvc_sock *, struct nfsd *,
		     struct nfsrv_descript **);
void nfsrv_wakenfsd(struct nfssvc_sock *);

/* nfs_srvcache.c */
void nfsrv_initcache(void );
int nfsrv_getcache(struct nfsrv_descript *, struct nfssvc_sock *,
			struct mbuf **);
void nfsrv_updatecache(struct nfsrv_descript *, int, struct mbuf *);
void nfsrv_cleancache(void);

/* nfs_srvsubs.c */
int nfs_namei(struct nameidata *, fhandle_t *, int, struct nfssvc_sock *,
		   struct mbuf *, struct mbuf **, caddr_t *, struct vnode **,
		   struct proc *);
void nfsm_adj(struct mbuf *, int, int);
void nfsm_srvwcc(struct nfsrv_descript *, int, struct vattr *, int,
		      struct vattr *, struct nfsm_info *);
void nfsm_srvpostop_attr(struct nfsrv_descript *, int, struct vattr *,
			     struct nfsm_info *);
void nfsm_srvfattr(struct nfsrv_descript *, struct vattr *,
			struct nfs_fattr *);
int nfsrv_fhtovp(fhandle_t *, int, struct vnode **, struct ucred *,
		      struct nfssvc_sock *, struct mbuf *, int *);
int netaddr_match(int, union nethostaddr *, struct mbuf *);
int nfsm_srvsattr(struct mbuf **, struct vattr *, struct mbuf *, caddr_t *);

/* nfs_subs.c */
struct mbuf *nfsm_reqhead(int);
void nfsm_rpchead(struct nfsreq *, struct ucred *, int);
void *nfsm_build(struct mbuf **, u_int);
int nfsm_mbuftouio(struct mbuf **, struct uio *, int, caddr_t *);
void nfsm_uiotombuf(struct mbuf **, struct uio *, size_t);
void nfsm_strtombuf(struct mbuf **, void *, size_t);
void nfsm_buftombuf(struct mbuf **, void *, size_t);
int nfs_adv(struct mbuf **, caddr_t *, int, int);
int nfs_vfs_init(struct vfsconf *);
int nfs_attrtimeo(struct nfsnode *);
int nfs_loadattrcache(struct vnode **, struct mbuf **, caddr_t *,
			   struct vattr *);
int nfs_getattrcache(struct vnode *, struct vattr *);
void nfsm_v3attrbuild(struct mbuf **, struct vattr *, int);
int nfsm_disct(struct mbuf **, caddr_t *, int, int, caddr_t *);
void nfs_clearcommit(struct mount *);
int nfs_in_committed_range(struct vnode *, struct buf *);
int nfs_in_tobecommitted_range(struct vnode *, struct buf *);
void nfs_add_committed_range(struct vnode *, struct buf *);
void nfs_del_committed_range(struct vnode *, struct buf *);
void nfs_add_tobecommitted_range(struct vnode *, struct buf *);
void nfs_del_tobecommitted_range(struct vnode *, struct buf *);
void nfs_merge_commit_ranges(struct vnode *);
int nfsrv_errmap(struct nfsrv_descript *, int);
void nfsm_fhtom(struct nfsm_info *, struct vnode *, int);
void nfsm_srvfhtom(struct mbuf **, fhandle_t *, int);

/* nfs_syscalls.c */
int sys_nfssvc(struct proc *, void *, register_t *);
void nfsrv_init(int);
void nfs_getset_niothreads(int);

/* nfs_kq.c */
int  nfs_kqfilter(void *);

/* Internal NFS utility macros */
#define	mb_offset(m)	(mtod((m), caddr_t) + (m)->m_len)
#define	nfsm_padlen(s)	(nfsm_rndup(s) - (s))

#endif /* _KERNEL */
#endif /* _NFS_NFS_VAR_H_ */
