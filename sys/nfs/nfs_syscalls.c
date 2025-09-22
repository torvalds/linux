/*	$OpenBSD: nfs_syscalls.c,v 1.130 2025/03/27 23:30:54 tedu Exp $	*/
/*	$NetBSD: nfs_syscalls.c,v 1.19 1996/02/18 11:53:52 fvdl Exp $	*/

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
 *	@(#)nfs_syscalls.c	8.5 (Berkeley) 3/30/95
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/pool.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/filedesc.h>
#include <sys/signalvar.h>
#include <sys/kthread.h>
#include <sys/queue.h>

#include <sys/syscallargs.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsrvcache.h>
#include <nfs/nfsnode.h>
#include <nfs/nfs_var.h>

/* Global defs. */
extern int nfs_numasync;
extern struct nfsstats nfsstats;
struct nfssvc_sock *nfs_udpsock;
int nfsd_waiting = 0;

#ifdef NFSSERVER
struct pool nfsrv_descript_pl;

int nfsrv_getslp(struct nfsd *nfsd);

static int nfs_numnfsd = 0;
static int (*const nfsrv3_procs[NFS_NPROCS])(struct nfsrv_descript *,
    struct nfssvc_sock *, struct proc *, struct mbuf **) = {
	nfsrv_null,
	nfsrv_getattr,
	nfsrv_setattr,
	nfsrv_lookup,
	nfsrv3_access,
	nfsrv_readlink,
	nfsrv_read,
	nfsrv_write,
	nfsrv_create,
	nfsrv_mkdir,
	nfsrv_symlink,
	nfsrv_mknod,
	nfsrv_remove,
	nfsrv_rmdir,
	nfsrv_rename,
	nfsrv_link,
	nfsrv_readdir,
	nfsrv_readdirplus,
	nfsrv_statfs,
	nfsrv_fsinfo,
	nfsrv_pathconf,
	nfsrv_commit,
	nfsrv_noop
};
#endif

TAILQ_HEAD(, nfssvc_sock) nfssvc_sockhead;
struct nfsdhead nfsd_head;

int nfssvc_sockhead_flag;
#define	SLP_INIT	0x01	/* NFS data undergoing initialization */
#define	SLP_WANTINIT	0x02	/* thread waiting on NFS initialization */
int nfsd_head_flag;

#ifdef NFSCLIENT
struct proc *nfs_asyncdaemon[NFS_MAXASYNCDAEMON];
int nfs_niothreads = -1;
#endif

int nfssvc_addsock(struct file *, struct mbuf *);
int nfssvc_nfsd(struct nfsd *);
void nfsrv_slpderef(struct nfssvc_sock *);
void nfsrv_zapsock(struct nfssvc_sock *);
void nfssvc_iod(void *);

/*
 * NFS server pseudo system call for the nfsd's
 * Based on the flag value it either:
 * - adds a socket to the selection list
 * - remains in the kernel as an nfsd
 */
int
sys_nfssvc(struct proc *p, void *v, register_t *retval)
{
	int error = 0;
#ifdef NFSSERVER
	struct sys_nfssvc_args /* {
		syscallarg(int) flag;
		syscallarg(caddr_t) argp;
	} */ *uap = v;
	int flags = SCARG(uap, flag);
	struct file *fp;
	struct mbuf *nam;
	struct nfsd_args nfsdarg;
	struct nfsd_srvargs nfsd_srvargs, *nsd = &nfsd_srvargs;
	struct nfsd *nfsd;
#endif

	/* Must be super user */
	error = suser(p);
	if (error)
		return (error);

#ifndef NFSSERVER
	error = ENOSYS;
#else

	while (nfssvc_sockhead_flag & SLP_INIT) {
		nfssvc_sockhead_flag |= SLP_WANTINIT;
		tsleep_nsec(&nfssvc_sockhead, PSOCK, "nfsd init", INFSLP);
	}

	switch (flags) {
	case NFSSVC_ADDSOCK:
		error = copyin(SCARG(uap, argp), &nfsdarg, sizeof(nfsdarg));
		if (error)
			return (error);

		error = getsock(p, nfsdarg.sock, &fp);
		if (error)
			return (error);

		/*
		 * Get the client address for connected sockets.
		 */
		if (nfsdarg.name == NULL || nfsdarg.namelen == 0)
			nam = NULL;
		else {
			error = sockargs(&nam, nfsdarg.name, nfsdarg.namelen,
				MT_SONAME);
			if (error) {
				FRELE(fp, p);
				return (error);
			}
		}
		error = nfssvc_addsock(fp, nam);
		FRELE(fp, p);
		break;
	case NFSSVC_NFSD:
		error = copyin(SCARG(uap, argp), nsd, sizeof(*nsd));
		if (error)
			return (error);

		nfsd = malloc(sizeof(*nfsd), M_NFSD, M_WAITOK|M_ZERO);
		nfsd->nfsd_procp = p;
		nfsd->nfsd_slp = NULL;

		error = nfssvc_nfsd(nfsd);
		break;
	default:
		error = EINVAL;
		break;
	}

	if (error == EINTR || error == ERESTART)
		error = 0;
#endif	/* !NFSSERVER */

	return (error);
}

#ifdef NFSSERVER
/*
 * Adds a socket to the list for servicing by nfsds.
 */
int
nfssvc_addsock(struct file *fp, struct mbuf *mynam)
{
	struct mbuf *m;
	int siz;
	struct nfssvc_sock *slp;
	struct socket *so;
	struct nfssvc_sock *tslp;
	int error;

	so = (struct socket *)fp->f_data;
	tslp = NULL;
	/*
	 * Add it to the list, as required.
	 */
	if (so->so_proto->pr_protocol == IPPROTO_UDP) {
		tslp = nfs_udpsock;
		if (tslp->ns_flag & SLP_VALID) {
			m_freem(mynam);
			return (EPERM);
		}
	}
	/*
	 * Allow only IPv4 UDP and TCP sockets.
	 */
	if ((so->so_type != SOCK_STREAM && so->so_type != SOCK_DGRAM) || 
	    so->so_proto->pr_domain->dom_family != AF_INET) {
		m_freem(mynam);
		return (EINVAL);
	}

	if (so->so_type == SOCK_STREAM)
		siz = NFS_MAXPACKET + sizeof (u_long);
	else
		siz = NFS_MAXPACKET;
	solock_shared(so);
	error = soreserve(so, siz, siz); 
	sounlock_shared(so);
	if (error) {
		m_freem(mynam);
		return (error);
	}

	/*
	 * Set protocol specific options { for now TCP only } and
	 * reserve some space. For datagram sockets, this can get called
	 * repeatedly for the same socket, but that isn't harmful.
	 */
	if (so->so_type == SOCK_STREAM) {
		MGET(m, M_WAIT, MT_SOOPTS);
		*mtod(m, int32_t *) = 1;
		m->m_len = sizeof(int32_t);
		sosetopt(so, SOL_SOCKET, SO_KEEPALIVE, m);
		m_freem(m);
	}
	if (so->so_proto->pr_domain->dom_family == AF_INET &&
	    so->so_proto->pr_protocol == IPPROTO_TCP) {
		MGET(m, M_WAIT, MT_SOOPTS);
		*mtod(m, int32_t *) = 1;
		m->m_len = sizeof(int32_t);
		sosetopt(so, IPPROTO_TCP, TCP_NODELAY, m);
		m_freem(m);
	}
	solock_shared(so);
	mtx_enter(&so->so_rcv.sb_mtx);
	so->so_rcv.sb_flags &= ~SB_NOINTR;
	so->so_rcv.sb_timeo_nsecs = INFSLP;
	mtx_leave(&so->so_rcv.sb_mtx);
	mtx_enter(&so->so_snd.sb_mtx);
	so->so_snd.sb_flags &= ~SB_NOINTR;
	so->so_snd.sb_timeo_nsecs = INFSLP;
	mtx_leave(&so->so_snd.sb_mtx);
	sounlock_shared(so);
	if (tslp)
		slp = tslp;
	else {
		slp = malloc(sizeof(*slp), M_NFSSVC, M_WAITOK|M_ZERO);
		TAILQ_INSERT_TAIL(&nfssvc_sockhead, slp, ns_chain);
	}
	slp->ns_so = so;
	slp->ns_nam = mynam;
	FREF(fp);
	slp->ns_fp = fp;
	so->so_upcallarg = (caddr_t)slp;
	so->so_upcall = nfsrv_rcv;
	slp->ns_flag = (SLP_VALID | SLP_NEEDQ);
	nfsrv_wakenfsd(slp);
	return (0);
}

static inline int
nfssvc_checknam(struct mbuf *nam)
{
	struct sockaddr_in *sin;

	if (nam == NULL ||
	    in_nam2sin(nam, &sin) != 0 ||
	    ntohs(sin->sin_port) >= IPPORT_RESERVED) {
		return -1;
	}
	return 0;
}

/*
 * Called by nfssvc() for nfsds. Just loops around servicing rpc requests
 * until it is killed by a signal.
 */
int
nfssvc_nfsd(struct nfsd *nfsd)
{
	struct mbuf *m;
	int siz;
	struct nfssvc_sock *slp;
	struct socket *so;
	int *solockp;
	struct nfsrv_descript *nd = NULL;
	struct mbuf *mreq;
	int error = 0, cacherep, sotype;

	cacherep = RC_DOIT;

	TAILQ_INSERT_TAIL(&nfsd_head, nfsd, nfsd_chain);
	nfs_numnfsd++;

	/* Loop getting rpc requests until SIGKILL. */
loop:
	if (!ISSET(nfsd->nfsd_flag, NFSD_REQINPROG)) {

		/* attach an nfssvc_sock to nfsd */
		error = nfsrv_getslp(nfsd);
		if (error)
			goto done;

		slp = nfsd->nfsd_slp;

		if (ISSET(slp->ns_flag, SLP_VALID)) {
			if ((slp->ns_flag & (SLP_DISCONN | SLP_NEEDQ)) ==
			    SLP_NEEDQ) {
				CLR(slp->ns_flag, SLP_NEEDQ);
				nfs_sndlock(&slp->ns_solock, NULL);
				nfsrv_rcv(slp->ns_so, (caddr_t)slp, M_WAIT);
				nfs_sndunlock(&slp->ns_solock);
			}
			if (ISSET(slp->ns_flag, SLP_DISCONN))
				nfsrv_zapsock(slp);

			error = nfsrv_dorec(slp, nfsd, &nd);
			SET(nfsd->nfsd_flag, NFSD_REQINPROG);
		}
	} else {
		error = 0;
		slp = nfsd->nfsd_slp;
	}

	if (error || !ISSET(slp->ns_flag, SLP_VALID)) {
		if (nd != NULL) {
			pool_put(&nfsrv_descript_pl, nd);
			nd = NULL;
		}
		nfsd->nfsd_slp = NULL;
		CLR(nfsd->nfsd_flag, NFSD_REQINPROG);
		nfsrv_slpderef(slp);
		goto loop;
	}

	so = slp->ns_so;
	sotype = so->so_type;
	if (ISSET(so->so_proto->pr_flags, PR_CONNREQUIRED))
		solockp = &slp->ns_solock;
	else
		solockp = NULL;

	if (nd) {
		if (nd->nd_nam2)
			nd->nd_nam = nd->nd_nam2;
		else
			nd->nd_nam = slp->ns_nam;
	}

	cacherep = nfsrv_getcache(nd, slp, &mreq);
	switch (cacherep) {
	case RC_DOIT:
		/*
		 * Unless this is a null request (server ping), make
		 * sure that the client is using a reserved source port.
		 */
		if (nd->nd_procnum != 0 && nfssvc_checknam(nd->nd_nam) == -1) {
			/* drop it */
			m_freem(nd->nd_mrep);
			m_freem(nd->nd_nam2);
			break;
		}
		error = (*(nfsrv3_procs[nd->nd_procnum]))(nd, slp, nfsd->nfsd_procp, &mreq);
		if (mreq == NULL) {
			if (nd != NULL) {
				m_freem(nd->nd_nam2);
				m_freem(nd->nd_mrep);
			}
			break;
		}
		if (error) {
			nfsstats.srv_errs++;
			nfsrv_updatecache(nd, 0, mreq);
			m_freem(nd->nd_nam2);
			break;
		}
		nfsstats.srvrpccnt[nd->nd_procnum]++;
		nfsrv_updatecache(nd, 1, mreq);
		nd->nd_mrep = NULL;

		/* FALLTHROUGH */
	case RC_REPLY:
		m = mreq;
		siz = 0;
		while (m) {
			siz += m->m_len;
			m = m->m_next;
		}

		if (siz <= 0 || siz > NFS_MAXPACKET)
			panic("bad nfs svc reply, siz = %i", siz);

		m = mreq;
		m->m_pkthdr.len = siz;
		m->m_pkthdr.ph_ifidx = 0;

		/* For stream protocols, prepend a Sun RPC Record Mark. */
		if (sotype == SOCK_STREAM) {
			M_PREPEND(m, NFSX_UNSIGNED, M_WAIT);
			*mtod(m, u_int32_t *) = htonl(0x80000000 | siz);
		}

		if (solockp)
			nfs_sndlock(solockp, NULL);

		if (ISSET(slp->ns_flag, SLP_VALID))
		    error = nfs_send(so, nd->nd_nam2, m, NULL);
		else {
		    error = EPIPE;
		    m_freem(m);
		}
		m_freem(nd->nd_nam2);
		m_freem(nd->nd_mrep);
		if (error == EPIPE)
			nfsrv_zapsock(slp);
		if (solockp)
			nfs_sndunlock(solockp);
		if (error == EINTR || error == ERESTART) {
			pool_put(&nfsrv_descript_pl, nd);
			nfsrv_slpderef(slp);
			goto done;
		}
		break;
	case RC_DROPIT:
		m_freem(nd->nd_mrep);
		m_freem(nd->nd_nam2);
		break;
	}

	if (nd) {
		pool_put(&nfsrv_descript_pl, nd);
		nd = NULL;
	}

	if (nfsrv_dorec(slp, nfsd, &nd)) {
		nfsd->nfsd_flag &= ~NFSD_REQINPROG;
		nfsd->nfsd_slp = NULL;
		nfsrv_slpderef(slp);
	}
	goto loop;

done:
	TAILQ_REMOVE(&nfsd_head, nfsd, nfsd_chain);
	free(nfsd, M_NFSD, sizeof(*nfsd));
	if (--nfs_numnfsd == 0)
		nfsrv_init(1);	/* Reinitialize everything */
	return (error);
}

/*
 * Shut down a socket associated with an nfssvc_sock structure.
 * Should be called with the send lock set, if required.
 * The trick here is to increment the sref at the start, so that the nfsds
 * will stop using it and clear ns_flag at the end so that it will not be
 * reassigned during cleanup.
 */
void
nfsrv_zapsock(struct nfssvc_sock *slp)
{
	struct socket *so;
	struct file *fp;
	struct mbuf *m, *n;

	slp->ns_flag &= ~SLP_ALLFLAGS;
	fp = slp->ns_fp;
	if (fp) {
		FREF(fp);
		slp->ns_fp = NULL;
		so = slp->ns_so;
		so->so_upcall = NULL;
		soshutdown(so, SHUT_RDWR);
		closef(fp, NULL);
		if (slp->ns_nam)
	    		m = m_free(slp->ns_nam);
		m_freem(slp->ns_raw);
		m = slp->ns_rec;
		while (m) {
			n = m->m_nextpkt;
			m_freem(m);
			m = n;
		}
	}
}

/*
 * Dereference a server socket structure. If it has no more references and
 * is no longer valid, you can throw it away.
 */
void
nfsrv_slpderef(struct nfssvc_sock *slp)
{
	if (--(slp->ns_sref) == 0 && (slp->ns_flag & SLP_VALID) == 0) {
		TAILQ_REMOVE(&nfssvc_sockhead, slp, ns_chain);
		free(slp, M_NFSSVC, sizeof(*slp));
	}
}

/*
 * Initialize the data structures for the server.
 * Handshake with any new nfsds starting up to avoid any chance of
 * corruption.
 */
void
nfsrv_init(int terminating)
{
	struct nfssvc_sock *slp, *nslp;

	if (nfssvc_sockhead_flag & SLP_INIT)
		panic("nfsd init");
	nfssvc_sockhead_flag |= SLP_INIT;
	if (terminating) {
		for (slp = TAILQ_FIRST(&nfssvc_sockhead); slp != NULL;
		    slp = nslp) {
			nslp = TAILQ_NEXT(slp, ns_chain);
			if (slp->ns_flag & SLP_VALID)
				nfsrv_zapsock(slp);
			TAILQ_REMOVE(&nfssvc_sockhead, slp, ns_chain);
			free(slp, M_NFSSVC, sizeof(*slp));
		}
		nfsrv_cleancache();	/* And clear out server cache */
	}

	TAILQ_INIT(&nfssvc_sockhead);
	nfssvc_sockhead_flag &= ~SLP_INIT;
	if (nfssvc_sockhead_flag & SLP_WANTINIT) {
		nfssvc_sockhead_flag &= ~SLP_WANTINIT;
		wakeup((caddr_t)&nfssvc_sockhead);
	}

	TAILQ_INIT(&nfsd_head);
	nfsd_head_flag &= ~NFSD_CHECKSLP;

	nfs_udpsock =  malloc(sizeof(*nfs_udpsock), M_NFSSVC,
	    M_WAITOK|M_ZERO);
	TAILQ_INSERT_HEAD(&nfssvc_sockhead, nfs_udpsock, ns_chain);

	if (!terminating) {
		pool_init(&nfsrv_descript_pl, sizeof(struct nfsrv_descript),
		    0, IPL_NONE, PR_WAITOK, "ndscpl", NULL);
	}
}
#endif /* NFSSERVER */

#ifdef NFSCLIENT
/*
 * Asynchronous I/O threads for client nfs.
 * They do read-ahead and write-behind operations on the block I/O cache.
 * Never returns unless it fails or gets killed.
 */
void
nfssvc_iod(void *arg)
{
	struct proc *p = curproc;
	struct buf *bp, *nbp;
	int i, myiod;
	struct vnode *vp;
	int error = 0, s, bufcount;

	bufcount = MIN(256, bcstats.kvaslots / 8);
	bufcount = MIN(bufcount, bcstats.numbufs / 8);

	/* Assign my position or return error if too many already running. */
	myiod = -1;
	for (i = 0; i < NFS_MAXASYNCDAEMON; i++) {
		if (nfs_asyncdaemon[i] == NULL) {
			myiod = i;
			break;
		}
	}
	if (myiod == -1)
		kthread_exit(EBUSY);

	nfs_asyncdaemon[myiod] = p;
	nfs_numasync++;

	/* Upper limit on how many bufs we'll queue up for this iod. */
	if (nfs_bufqmax > bcstats.kvaslots / 4) {
		nfs_bufqmax = bcstats.kvaslots / 4;
		bufcount = 0;
	} 
	if (nfs_bufqmax > bcstats.numbufs / 4) {
		nfs_bufqmax = bcstats.numbufs / 4;
		bufcount = 0;
	}

	nfs_bufqmax += bufcount;
	wakeup(&nfs_bufqlen); /* wake up anyone waiting for room to enqueue IO */

	/* Just loop around doin our stuff until SIGKILL. */
	for (;;) {
	    while (TAILQ_FIRST(&nfs_bufq) == NULL && error == 0) {
		    error = tsleep_nsec(&nfs_bufq,
			PWAIT | PCATCH, "nfsidl", INFSLP);
	    }
	    while ((bp = TAILQ_FIRST(&nfs_bufq)) != NULL) {
		/* Take one off the front of the list */
		TAILQ_REMOVE(&nfs_bufq, bp, b_freelist);
		nfs_bufqlen--;
		wakeup_one(&nfs_bufqlen);
		if (bp->b_flags & B_READ)
		    (void) nfs_doio(bp, NULL);
		else do {
		    /*
		     * Look for a delayed write for the same vnode, so I can do 
		     * it now. We must grab it before calling nfs_doio() to
		     * avoid any risk of the vnode getting vclean()'d while
		     * we are doing the write rpc.
		     */
		    vp = bp->b_vp;
		    s = splbio();
		    LIST_FOREACH(nbp, &vp->v_dirtyblkhd, b_vnbufs) {
			if ((nbp->b_flags &
			    (B_BUSY|B_DELWRI|B_NEEDCOMMIT|B_NOCACHE))!=B_DELWRI)
			    continue;
			nbp->b_flags |= B_ASYNC;
			bufcache_take(nbp);
			buf_acquire(nbp);
			break;
		    }
		    /*
		     * For the delayed write, do the first part of nfs_bwrite()
		     * up to, but not including nfs_strategy().
		     */
		    if (nbp) {
			nbp->b_flags &= ~(B_READ|B_DONE|B_ERROR);
			buf_undirty(nbp);
			nbp->b_vp->v_numoutput++;
		    }
		    splx(s);

		    (void) nfs_doio(bp, NULL);
		} while ((bp = nbp) != NULL);
	    }
	    if (error) {
		nfs_asyncdaemon[myiod] = NULL;
		nfs_numasync--;
		nfs_bufqmax -= bufcount;
		kthread_exit(error);
	    }
	}
}

void
nfs_getset_niothreads(int set)
{
	int i, have, start;
	
	for (have = 0, i = 0; i < NFS_MAXASYNCDAEMON; i++)
		if (nfs_asyncdaemon[i] != NULL)
			have++;

	if (set) {
		/* clamp to sane range */
		nfs_niothreads = max(0, min(nfs_niothreads, NFS_MAXASYNCDAEMON));

		start = nfs_niothreads - have;

		while (start > 0) {
			kthread_create(nfssvc_iod, NULL, NULL, "nfsio");
			start--;
		}

		for (i = 0; (start < 0) && (i < NFS_MAXASYNCDAEMON); i++)
			if (nfs_asyncdaemon[i] != NULL) {
				psignal(nfs_asyncdaemon[i], SIGKILL);
				start++;
			}
	} else {
		if (nfs_niothreads >= 0)
			nfs_niothreads = have;
	}
}
#endif /* NFSCLIENT */

#ifdef NFSSERVER
/*
 * Find an nfssrv_sock for nfsd, sleeping if needed.
 */
int
nfsrv_getslp(struct nfsd *nfsd)
{
	struct nfssvc_sock *slp;
	int error;

again:
	while (nfsd->nfsd_slp == NULL &&
	    (nfsd_head_flag & NFSD_CHECKSLP) == 0) {
		nfsd->nfsd_flag |= NFSD_WAITING;
		nfsd_waiting++;
		error = tsleep_nsec(nfsd, PSOCK | PCATCH, "nfsd", INFSLP);
		nfsd_waiting--;
		if (error)
			return (error);
	}

	if (nfsd->nfsd_slp == NULL &&
	    (nfsd_head_flag & NFSD_CHECKSLP) != 0) {
		TAILQ_FOREACH(slp, &nfssvc_sockhead, ns_chain) {
			if ((slp->ns_flag & (SLP_VALID | SLP_DOREC)) ==
			    (SLP_VALID | SLP_DOREC)) {
				slp->ns_flag &= ~SLP_DOREC;
				slp->ns_sref++;
				nfsd->nfsd_slp = slp;
				break;
			}
		}
		if (slp == NULL)
			nfsd_head_flag &= ~NFSD_CHECKSLP;
	}

	if (nfsd->nfsd_slp == NULL)
		goto again;

	return (0);
}
#endif /* NFSSERVER */
