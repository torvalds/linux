/*	$OpenBSD: nfs_socket.c,v 1.156 2025/02/16 16:05:07 bluhm Exp $	*/
/*	$NetBSD: nfs_socket.c,v 1.27 1996/04/15 20:20:00 thorpej Exp $	*/

/*
 * Copyright (c) 1989, 1991, 1993, 1995
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
 *	@(#)nfs_socket.c	8.5 (Berkeley) 3/30/95
 */

/*
 * Socket operations for use by nfs
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/vnode.h>
#include <sys/protosw.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>
#include <sys/tprintf.h>
#include <sys/namei.h>
#include <sys/pool.h>
#include <sys/queue.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/xdr_subs.h>
#include <nfs/nfsmount.h>
#include <nfs/nfs_var.h>
#include <nfs/nfsm_subs.h>

/* External data, mostly RPC constants in XDR form. */
extern u_int32_t rpc_reply, rpc_msgdenied, rpc_mismatch, rpc_vers,
	rpc_auth_unix, rpc_msgaccepted, rpc_call, rpc_autherr;
extern u_int32_t nfs_prog;
extern struct nfsstats nfsstats;
extern const int nfsv3_procid[NFS_NPROCS];
extern int nfs_ticks;

extern struct pool nfsrv_descript_pl;

/*
 * There is a congestion window for outstanding rpcs maintained per mount
 * point. The cwnd size is adjusted in roughly the way that:
 * Van Jacobson, Congestion avoidance and Control, In "Proceedings of
 * SIGCOMM '88". ACM, August 1988.
 * describes for TCP. The cwnd size is chopped in half on a retransmit timeout
 * and incremented by 1/cwnd when each rpc reply is received and a full cwnd
 * of rpcs is in progress.
 * (The sent count and cwnd are scaled for integer arith.)
 * Variants of "slow start" were tried and were found to be too much of a
 * performance hit (ave. rtt 3 times larger),
 * I suspect due to the large rtt that nfs rpcs have.
 */
#define	NFS_CWNDSCALE	256
#define	NFS_MAXCWND	(NFS_CWNDSCALE * 32)
static const int nfs_backoff[8] = { 2, 4, 8, 16, 32, 64, 128, 256 };

/* RTT estimator */
static const enum nfs_rto_timers nfs_ptimers[NFS_NPROCS] = {
	NFS_DEFAULT_TIMER,	/* NULL */
	NFS_GETATTR_TIMER,	/* GETATTR */
	NFS_DEFAULT_TIMER,	/* SETATTR */
	NFS_LOOKUP_TIMER,	/* LOOKUP */
	NFS_GETATTR_TIMER,	/* ACCESS */
	NFS_READ_TIMER,		/* READLINK */
	NFS_READ_TIMER,		/* READ */
	NFS_WRITE_TIMER,	/* WRITE */
	NFS_DEFAULT_TIMER,	/* CREATE */
	NFS_DEFAULT_TIMER,	/* MKDIR */
	NFS_DEFAULT_TIMER,	/* SYMLINK */
	NFS_DEFAULT_TIMER,	/* MKNOD */
	NFS_DEFAULT_TIMER,	/* REMOVE */
	NFS_DEFAULT_TIMER,	/* RMDIR */
	NFS_DEFAULT_TIMER,	/* RENAME */
	NFS_DEFAULT_TIMER,	/* LINK */
	NFS_READ_TIMER,		/* READDIR */
	NFS_READ_TIMER,		/* READDIRPLUS */
	NFS_DEFAULT_TIMER,	/* FSSTAT */
	NFS_DEFAULT_TIMER,	/* FSINFO */
	NFS_DEFAULT_TIMER,	/* PATHCONF */
	NFS_DEFAULT_TIMER,	/* COMMIT */
	NFS_DEFAULT_TIMER,	/* NOOP */
};

void nfs_init_rtt(struct nfsmount *);
void nfs_update_rtt(struct nfsreq *);
int  nfs_estimate_rto(struct nfsmount *, u_int32_t procnum);

void nfs_realign(struct mbuf **, int);
void nfs_realign_fixup(struct mbuf *, struct mbuf *, unsigned int *);

int nfs_rcvlock(struct nfsreq *);
int nfs_receive(struct nfsreq *, struct mbuf **, struct mbuf **);
int nfs_reconnect(struct nfsreq *);
int nfs_reply(struct nfsreq *);
void nfs_msg(struct nfsreq *, char *);
void nfs_rcvunlock(int *);

int nfsrv_getstream(struct nfssvc_sock *, int);

unsigned int nfs_realign_test = 0;
unsigned int nfs_realign_count = 0;

/* Initialize the RTT estimator state for a new mount point. */
void
nfs_init_rtt(struct nfsmount *nmp)
{
	int i;

	for (i = 0; i < NFS_MAX_TIMER; i++)
		nmp->nm_srtt[i] = NFS_INITRTT;
	for (i = 0; i < NFS_MAX_TIMER; i++)
		nmp->nm_sdrtt[i] = 0;
}

/*
 * Update a mount point's RTT estimator state using data from the
 * passed-in request.
 * 
 * Use a gain of 0.125 on the mean and a gain of 0.25 on the deviation.
 *
 * NB: Since the timer resolution of NFS_HZ is so coarse, it can often
 * result in r_rtt == 0. Since r_rtt == N means that the actual RTT is
 * between N + dt and N + 2 - dt ticks, add 1 before calculating the
 * update values.
 */
void
nfs_update_rtt(struct nfsreq *rep)
{
	int t1 = rep->r_rtt + 1;
	int index = nfs_ptimers[rep->r_procnum] - 1;
	int *srtt = &rep->r_nmp->nm_srtt[index];
	int *sdrtt = &rep->r_nmp->nm_sdrtt[index];

	t1 -= *srtt >> 3;
	*srtt += t1;
	if (t1 < 0)
		t1 = -t1;
	t1 -= *sdrtt >> 2;
	*sdrtt += t1;
}

/*
 * Estimate RTO for an NFS RPC sent via an unreliable datagram.
 *
 * Use the mean and mean deviation of RTT for the appropriate type
 * of RPC for the frequent RPCs and a default for the others.
 * The justification for doing "other" this way is that these RPCs
 * happen so infrequently that timer est. would probably be stale.
 * Also, since many of these RPCs are non-idempotent, a conservative
 * timeout is desired.
 *
 * getattr, lookup - A+2D
 * read, write     - A+4D
 * other           - nm_timeo
 */
int
nfs_estimate_rto(struct nfsmount *nmp, u_int32_t procnum)
{
	enum nfs_rto_timers timer = nfs_ptimers[procnum];
	int index = timer - 1;
	int rto;

	switch (timer) {
	case NFS_GETATTR_TIMER:
	case NFS_LOOKUP_TIMER:
		rto = ((nmp->nm_srtt[index] + 3) >> 2) +
				((nmp->nm_sdrtt[index] + 1) >> 1);
		break;
	case NFS_READ_TIMER:
	case NFS_WRITE_TIMER:
		rto = ((nmp->nm_srtt[index] + 7) >> 3) +
				(nmp->nm_sdrtt[index] + 1);
		break;
	default:
		rto = nmp->nm_timeo;
		return (rto);
	}

	if (rto < NFS_MINRTO)
		rto = NFS_MINRTO;
	else if (rto > NFS_MAXRTO)
		rto = NFS_MAXRTO;

	return (rto);
}



/*
 * Initialize sockets and congestion for a new NFS connection.
 * We do not free the sockaddr if error.
 */
int
nfs_connect(struct nfsmount *nmp, struct nfsreq *rep)
{
	struct socket *so;
	int error, rcvreserve, sndreserve;
	struct sockaddr *saddr;
	struct sockaddr_in *sin;
	struct mbuf *nam = NULL, *mopt = NULL;

	if (!(nmp->nm_sotype == SOCK_DGRAM || nmp->nm_sotype == SOCK_STREAM))
		return (EINVAL);

	nmp->nm_so = NULL;
	saddr = mtod(nmp->nm_nam, struct sockaddr *);
	error = socreate(saddr->sa_family, &nmp->nm_so, nmp->nm_sotype, 
	    nmp->nm_soproto);
	if (error) {
		nfs_disconnect(nmp);
		return (error);
	}

	/* Allocate mbufs possibly waiting before grabbing the socket lock. */
	if (nmp->nm_sotype == SOCK_STREAM || saddr->sa_family == AF_INET)
		MGET(mopt, M_WAIT, MT_SOOPTS);
	if (saddr->sa_family == AF_INET)
		MGET(nam, M_WAIT, MT_SONAME);

	so = nmp->nm_so;
	nmp->nm_soflags = so->so_proto->pr_flags;

	/*
	 * Some servers require that the client port be a reserved port number.
	 * We always allocate a reserved port, as this prevents filehandle
	 * disclosure through UDP port capture.
	 */
	if (saddr->sa_family == AF_INET) {
		int *ip;

		mopt->m_len = sizeof(int);
		ip = mtod(mopt, int *);
		*ip = IP_PORTRANGE_LOW;
		error = sosetopt(so, IPPROTO_IP, IP_PORTRANGE, mopt);
		if (error)
			goto bad;

		sin = mtod(nam, struct sockaddr_in *);
		memset(sin, 0, sizeof(*sin));
		sin->sin_len = nam->m_len = sizeof(struct sockaddr_in);
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = INADDR_ANY;
		sin->sin_port = htons(0);
		solock_shared(so);
		error = sobind(so, nam, &proc0);
		sounlock_shared(so);
		if (error)
			goto bad;

		mopt->m_len = sizeof(int);
		ip = mtod(mopt, int *);
		*ip = IP_PORTRANGE_DEFAULT;
		error = sosetopt(so, IPPROTO_IP, IP_PORTRANGE, mopt);
		if (error)
			goto bad;
	}

	/*
	 * Protocols that do not require connections may be optionally left
	 * unconnected for servers that reply from a port other than NFS_PORT.
	 */
	if (nmp->nm_flag & NFSMNT_NOCONN) {
		if (nmp->nm_soflags & PR_CONNREQUIRED) {
			error = ENOTCONN;
			goto bad;
		}
	} else {
		solock_shared(so);
		error = soconnect(so, nmp->nm_nam);
		if (error)
			goto bad_locked;

		/*
		 * Wait for the connection to complete. Cribbed from the
		 * connect system call but with the wait timing out so
		 * that interruptible mounts don't hang here for a long time.
		 */
		while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
			sosleep_nsec(so, &so->so_timeo, PSOCK, "nfscon",
			    SEC_TO_NSEC(2));
			if ((so->so_state & SS_ISCONNECTING) &&
			    so->so_error == 0 && rep &&
			    (error = nfs_sigintr(nmp, rep, rep->r_procp)) != 0){
				so->so_state &= ~SS_ISCONNECTING;
				goto bad_locked;
			}
		}
		if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			goto bad_locked;
		}
		sounlock_shared(so);
	}
	/*
	 * Always set receive timeout to detect server crash and reconnect.
	 * Otherwise, we can get stuck in soreceive forever.
	 */
	mtx_enter(&so->so_rcv.sb_mtx);
	so->so_rcv.sb_timeo_nsecs = SEC_TO_NSEC(5);
	mtx_leave(&so->so_rcv.sb_mtx);
	mtx_enter(&so->so_snd.sb_mtx);
	if (nmp->nm_flag & (NFSMNT_SOFT | NFSMNT_INT))
		so->so_snd.sb_timeo_nsecs = SEC_TO_NSEC(5);
	else
		so->so_snd.sb_timeo_nsecs = INFSLP;
	mtx_leave(&so->so_snd.sb_mtx);
	if (nmp->nm_sotype == SOCK_DGRAM) {
		sndreserve = nmp->nm_wsize + NFS_MAXPKTHDR;
		rcvreserve = (max(nmp->nm_rsize, nmp->nm_readdirsize) +
		    NFS_MAXPKTHDR) * 2;
	} else if (nmp->nm_sotype == SOCK_STREAM) {
		if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
			*mtod(mopt, int32_t *) = 1;
			mopt->m_len = sizeof(int32_t);
			sosetopt(so, SOL_SOCKET, SO_KEEPALIVE, mopt);
		}
		if (so->so_proto->pr_protocol == IPPROTO_TCP) {
			*mtod(mopt, int32_t *) = 1;
			mopt->m_len = sizeof(int32_t);
			sosetopt(so, IPPROTO_TCP, TCP_NODELAY, mopt);
		}
		sndreserve = (nmp->nm_wsize + NFS_MAXPKTHDR +
		    sizeof (u_int32_t)) * 2;
		rcvreserve = (nmp->nm_rsize + NFS_MAXPKTHDR +
		    sizeof (u_int32_t)) * 2;
	} else {
		panic("%s: nm_sotype %d", __func__, nmp->nm_sotype);
	}
	solock_shared(so);
	error = soreserve(so, sndreserve, rcvreserve);
	if (error)
		goto bad_locked;
	mtx_enter(&so->so_rcv.sb_mtx);
	so->so_rcv.sb_flags |= SB_NOINTR;
	mtx_leave(&so->so_rcv.sb_mtx);
	mtx_enter(&so->so_snd.sb_mtx);
	so->so_snd.sb_flags |= SB_NOINTR;
	mtx_leave(&so->so_snd.sb_mtx);
	sounlock_shared(so);

	m_freem(mopt);
	m_freem(nam);

	/* Initialize other non-zero congestion variables */
	nfs_init_rtt(nmp);
	nmp->nm_cwnd = NFS_MAXCWND / 2;	    /* Initial send window */
	nmp->nm_sent = 0;
	nmp->nm_timeouts = 0;
	return (0);

bad_locked:
	sounlock_shared(so);
bad:

	m_freem(mopt);
	m_freem(nam);

	nfs_disconnect(nmp);
	return (error);
}

/*
 * Reconnect routine:
 * Called when a connection is broken on a reliable protocol.
 * - clean up the old socket
 * - nfs_connect() again
 * - set R_MUSTRESEND for all outstanding requests on mount point
 * If this fails the mount point is DEAD!
 * nb: Must be called with the nfs_sndlock() set on the mount point.
 */
int
nfs_reconnect(struct nfsreq *rep)
{
	struct nfsreq *rp;
	struct nfsmount *nmp = rep->r_nmp;
	int error;

	nfs_disconnect(nmp);
	while ((error = nfs_connect(nmp, rep)) != 0) {
		if (error == EINTR || error == ERESTART)
			return (EINTR);
		tsleep_nsec(&nowake, PSOCK, "nfsrecon", SEC_TO_NSEC(1));
	}

	/*
	 * Loop through outstanding request list and fix up all requests
	 * on old socket.
	 */
	TAILQ_FOREACH(rp, &nmp->nm_reqsq, r_chain) {
		rp->r_flags |= R_MUSTRESEND;
		rp->r_rexmit = 0;
	}
	return (0);
}

/*
 * NFS disconnect. Clean up and unlink.
 */
void
nfs_disconnect(struct nfsmount *nmp)
{
	struct socket *so;

	if (nmp->nm_so) {
		so = nmp->nm_so;
		nmp->nm_so = NULL;
		soshutdown(so, SHUT_RDWR);
		soclose(so, 0);
	}
}

/*
 * This is the nfs send routine. For connection based socket types, it
 * must be called with an nfs_sndlock() on the socket.
 * "rep == NULL" indicates that it has been called from a server.
 * For the client side:
 * - return EINTR if the RPC is terminated, 0 otherwise
 * - set R_MUSTRESEND if the send fails for any reason
 * - do any cleanup required by recoverable socket errors (???)
 * For the server side:
 * - return EINTR or ERESTART if interrupted by a signal
 * - return EPIPE if a connection is lost for connection based sockets (TCP...)
 * - do any cleanup required by recoverable socket errors (???)
 */
int
nfs_send(struct socket *so, struct mbuf *nam, struct mbuf *top,
    struct nfsreq *rep)
{
	struct mbuf *sendnam;
	int error, soflags, flags;

	if (rep) {
		if (rep->r_flags & R_SOFTTERM) {
			m_freem(top);
			return (EINTR);
		}
		if ((so = rep->r_nmp->nm_so) == NULL) {
			rep->r_flags |= R_MUSTRESEND;
			m_freem(top);
			return (0);
		}
		rep->r_flags &= ~R_MUSTRESEND;
		soflags = rep->r_nmp->nm_soflags;
	} else
		soflags = so->so_proto->pr_flags;
	if ((soflags & PR_CONNREQUIRED) || (so->so_state & SS_ISCONNECTED))
		sendnam = NULL;
	else
		sendnam = nam;
	flags = 0;

	error = sosend(so, sendnam, NULL, top, NULL, flags);
	if (error) {
		if (rep) {
			/*
			 * Deal with errors for the client side.
			 */
			if (rep->r_flags & R_SOFTTERM)
				error = EINTR;
			else
				rep->r_flags |= R_MUSTRESEND;
		}

		/*
		 * Handle any recoverable (soft) socket errors here. (???)
		 */
		if (error != EINTR && error != ERESTART &&
		    error != EWOULDBLOCK && error != EPIPE)
			error = 0;
	}
	return (error);
}

#ifdef NFSCLIENT
/*
 * Receive a Sun RPC Request/Reply. For SOCK_DGRAM, the work is all
 * done by soreceive(), but for SOCK_STREAM we must deal with the Record
 * Mark and consolidate the data into a new mbuf list.
 * nb: Sometimes TCP passes the data up to soreceive() in long lists of
 *     small mbufs.
 * For SOCK_STREAM we must be very careful to read an entire record once
 * we have read any of it, even if the system call has been interrupted.
 */
int
nfs_receive(struct nfsreq *rep, struct mbuf **aname, struct mbuf **mp)
{
	struct socket *so;
	struct uio auio;
	struct iovec aio;
	struct mbuf *m;
	struct mbuf *control;
	u_int32_t len;
	struct mbuf **getnam;
	int error, sotype, rcvflg;
	struct proc *p = curproc;	/* XXX */

	/*
	 * Set up arguments for soreceive()
	 */
	*mp = NULL;
	*aname = NULL;
	sotype = rep->r_nmp->nm_sotype;

	/*
	 * For reliable protocols, lock against other senders/receivers
	 * in case a reconnect is necessary.
	 * For SOCK_STREAM, first get the Record Mark to find out how much
	 * more there is to get.
	 * We must lock the socket against other receivers
	 * until we have an entire rpc request/reply.
	 */
	if (sotype != SOCK_DGRAM) {
		error = nfs_sndlock(&rep->r_nmp->nm_flag, rep);
		if (error)
			return (error);
tryagain:
		/*
		 * Check for fatal errors and resending request.
		 */
		/*
		 * Ugh: If a reconnect attempt just happened, nm_so
		 * would have changed. NULL indicates a failed
		 * attempt that has essentially shut down this
		 * mount point.
		 */
		if (rep->r_mrep || (rep->r_flags & R_SOFTTERM)) {
			nfs_sndunlock(&rep->r_nmp->nm_flag);
			return (EINTR);
		}
		so = rep->r_nmp->nm_so;
		if (!so) {
			error = nfs_reconnect(rep); 
			if (error) {
				nfs_sndunlock(&rep->r_nmp->nm_flag);
				return (error);
			}
			goto tryagain;
		}
		while (rep->r_flags & R_MUSTRESEND) {
			m = m_copym(rep->r_mreq, 0, M_COPYALL, M_WAIT);
			nfsstats.rpcretries++;
			rep->r_rtt = 0;
			rep->r_flags &= ~R_TIMING;
			error = nfs_send(so, rep->r_nmp->nm_nam, m, rep);
			if (error) {
				if (error == EINTR || error == ERESTART ||
				    (error = nfs_reconnect(rep)) != 0) {
					nfs_sndunlock(&rep->r_nmp->nm_flag);
					return (error);
				}
				goto tryagain;
			}
		}
		nfs_sndunlock(&rep->r_nmp->nm_flag);
		if (sotype == SOCK_STREAM) {
			aio.iov_base = (caddr_t) &len;
			aio.iov_len = sizeof(u_int32_t);
			auio.uio_iov = &aio;
			auio.uio_iovcnt = 1;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = UIO_READ;
			auio.uio_offset = 0;
			auio.uio_resid = sizeof(u_int32_t);
			auio.uio_procp = p;
			do {
				rcvflg = MSG_WAITALL;
				error = soreceive(so, NULL, &auio, NULL, NULL,
				    &rcvflg, 0);
				if (error == EWOULDBLOCK && rep) {
					if (rep->r_flags & R_SOFTTERM)
						return (EINTR);
					/*
					 * looks like the server died after it
					 * received the request, make sure
					 * that we will retransmit and we
					 * don't get stuck here forever.
					 */
					if (rep->r_rexmit >=
					    rep->r_nmp->nm_retry) {
						nfsstats.rpctimeouts++;
						error = EPIPE;
					}
				}
			} while (error == EWOULDBLOCK);
			if (!error && auio.uio_resid > 0) {
			    log(LOG_INFO,
				 "short receive (%zu/%zu) from nfs server %s\n",
				 sizeof(u_int32_t) - auio.uio_resid,
				 sizeof(u_int32_t),
				 rep->r_nmp->nm_mountp->mnt_stat.f_mntfromname);
			    error = EPIPE;
			}
			if (error)
				goto errout;

			len = ntohl(len) & ~0x80000000;
			/*
			 * This is SERIOUS! We are out of sync with the sender
			 * and forcing a disconnect/reconnect is all I can do.
			 */
			if (len > NFS_MAXPACKET) {
			    log(LOG_ERR, "%s (%u) from nfs server %s\n",
				"impossible packet length",
				len,
				rep->r_nmp->nm_mountp->mnt_stat.f_mntfromname);
			    error = EFBIG;
			    goto errout;
			}
			auio.uio_resid = len;
			do {
			    rcvflg = MSG_WAITALL;
			    error =  soreceive(so, NULL, &auio, mp, NULL,
			        &rcvflg, 0);
			} while (error == EWOULDBLOCK || error == EINTR ||
			    error == ERESTART);
			if (!error && auio.uio_resid > 0) {
				log(LOG_INFO, "short receive (%zu/%u) from "
				    "nfs server %s\n", len - auio.uio_resid,
				    len, rep->r_nmp->nm_mountp->
				    mnt_stat.f_mntfromname);
				error = EPIPE;
			}
		} else {
			/*
			 * NB: Since uio_resid is big, MSG_WAITALL is ignored
			 * and soreceive() will return when it has either a
			 * control msg or a data msg.
			 * We have no use for control msg., but must grab them
			 * and then throw them away so we know what is going
			 * on.
			 */
			auio.uio_resid = len = 100000000; /* Anything Big */
			auio.uio_procp = p;
			do {
				rcvflg = 0;
				error = soreceive(so, NULL, &auio, mp, &control,
				    &rcvflg, 0);
				m_freem(control);
				if (error == EWOULDBLOCK && rep) {
					if (rep->r_flags & R_SOFTTERM)
						return (EINTR);
				}
			} while (error == EWOULDBLOCK ||
			    (!error && *mp == NULL && control));
			if ((rcvflg & MSG_EOR) == 0)
				printf("Egad!!\n");
			if (!error && *mp == NULL)
				error = EPIPE;
			len -= auio.uio_resid;
		}
errout:
		if (error && error != EINTR && error != ERESTART) {
			m_freemp(mp);
			if (error != EPIPE)
				log(LOG_INFO,
				    "receive error %d from nfs server %s\n",
				    error,
				 rep->r_nmp->nm_mountp->mnt_stat.f_mntfromname);
			error = nfs_sndlock(&rep->r_nmp->nm_flag, rep);
			if (!error) {
				error = nfs_reconnect(rep);
				if (!error)
					goto tryagain;
				nfs_sndunlock(&rep->r_nmp->nm_flag);
			}
		}
	} else {
		if ((so = rep->r_nmp->nm_so) == NULL)
			return (EACCES);
		if (so->so_state & SS_ISCONNECTED)
			getnam = NULL;
		else
			getnam = aname;
		auio.uio_resid = len = 1000000;
		auio.uio_procp = p;
		do {
			rcvflg = 0;
			error = soreceive(so, getnam, &auio, mp, NULL,
			    &rcvflg, 0);
			if (error == EWOULDBLOCK &&
			    (rep->r_flags & R_SOFTTERM))
				return (EINTR);
		} while (error == EWOULDBLOCK);
		len -= auio.uio_resid;
	}
	if (error)
		m_freemp(mp);
	/*
	 * Search for any mbufs that are not a multiple of 4 bytes long
	 * or with m_data not longword aligned.
	 * These could cause pointer alignment problems, so copy them to
	 * well aligned mbufs.
	 */
	nfs_realign(mp, 5 * NFSX_UNSIGNED);
	return (error);
}

/*
 * Implement receipt of reply on a socket.
 * We must search through the list of received datagrams matching them
 * with outstanding requests using the xid, until ours is found.
 */
int
nfs_reply(struct nfsreq *myrep)
{
	struct nfsreq *rep;
	struct nfsmount *nmp = myrep->r_nmp;
	struct nfsm_info	info;
	struct mbuf *nam;
	u_int32_t rxid, *tl;
	int error;

	/*
	 * Loop around until we get our own reply
	 */
	for (;;) {
		/*
		 * Lock against other receivers so that I don't get stuck in
		 * sbwait() after someone else has received my reply for me.
		 * Also necessary for connection based protocols to avoid
		 * race conditions during a reconnect.
		 */
		error = nfs_rcvlock(myrep);
		if (error)
			return (error == EALREADY ? 0 : error);

		/*
		 * Get the next Rpc reply off the socket
		 */
		error = nfs_receive(myrep, &nam, &info.nmi_mrep);
		nfs_rcvunlock(&nmp->nm_flag);
		if (error) {

			/*
			 * Ignore routing errors on connectionless protocols??
			 */
			if (NFSIGNORE_SOERROR(nmp->nm_soflags, error)) {
				if (nmp->nm_so)
					nmp->nm_so->so_error = 0;
				continue;
			}
			return (error);
		}
		m_freem(nam);
	
		/*
		 * Get the xid and check that it is an rpc reply
		 */
		info.nmi_md = info.nmi_mrep;
		info.nmi_dpos = mtod(info.nmi_md, caddr_t);
		info.nmi_errorp = &error;
		tl = (uint32_t *)nfsm_dissect(&info, 2 * NFSX_UNSIGNED);
		if (tl == NULL)
			goto nfsmout;
		rxid = *tl++;
		if (*tl != rpc_reply) {
			nfsstats.rpcinvalid++;
			m_freem(info.nmi_mrep);
nfsmout:
			continue;
		}

		/*
		 * Loop through the request list to match up the reply
		 * Iff no match, just drop the datagram
		 */
		TAILQ_FOREACH(rep, &nmp->nm_reqsq, r_chain) {
			if (rep->r_mrep == NULL && rxid == rep->r_xid) {
				/* Found it.. */
				rep->r_mrep = info.nmi_mrep;
				rep->r_md = info.nmi_md;
				rep->r_dpos = info.nmi_dpos;

				/*
				 * Update congestion window.
				 * Do the additive increase of
				 * one rpc/rtt.
				 */
				if (nmp->nm_cwnd <= nmp->nm_sent) {
					nmp->nm_cwnd +=
					   (NFS_CWNDSCALE * NFS_CWNDSCALE +
					   (nmp->nm_cwnd >> 1)) / nmp->nm_cwnd;
					if (nmp->nm_cwnd > NFS_MAXCWND)
						nmp->nm_cwnd = NFS_MAXCWND;
				}
				rep->r_flags &= ~R_SENT;
				nmp->nm_sent -= NFS_CWNDSCALE;

				if (rep->r_flags & R_TIMING)
					nfs_update_rtt(rep);

				nmp->nm_timeouts = 0;
				break;
			}
		}
		/*
		 * If not matched to a request, drop it.
		 * If it's mine, get out.
		 */
		if (rep == 0) {
			nfsstats.rpcunexpected++;
			m_freem(info.nmi_mrep);
		} else if (rep == myrep) {
			if (rep->r_mrep == NULL)
				panic("nfsreply nil");
			return (0);
		}
	}
}

/*
 * nfs_request - goes something like this
 *	- fill in request struct
 *	- links it into list
 *	- calls nfs_send() for first transmit
 *	- calls nfs_receive() to get reply
 *	- break down rpc header and return with nfs reply pointed to
 *	  by mrep or error
 * nb: always frees up mreq mbuf list
 */
int
nfs_request(struct vnode *vp, int procnum, struct nfsm_info *infop)
{
	struct mbuf *m;
	u_int32_t *tl;
	struct nfsmount *nmp;
	int i, error = 0;
	int trylater_delay;
	struct nfsreq *rep;
	struct nfsm_info info;

	rep = pool_get(&nfsreqpl, PR_WAITOK);
	rep->r_nmp = VFSTONFS(vp->v_mount);
	rep->r_vp = vp;
	rep->r_procp = infop->nmi_procp;
	rep->r_procnum = procnum;

	/* empty mbuf for AUTH_UNIX header */
	rep->r_mreq = m_gethdr(M_WAIT, MT_DATA);
	rep->r_mreq->m_next = infop->nmi_mreq;
	rep->r_mreq->m_len = 0;
	m_calchdrlen(rep->r_mreq);

	trylater_delay = NFS_MINTIMEO;

	nmp = rep->r_nmp;

	/* Get the RPC header with authorization. */
	nfsm_rpchead(rep, infop->nmi_cred, RPCAUTH_UNIX);
	m = rep->r_mreq;

	/*
	 * For stream protocols, insert a Sun RPC Record Mark.
	 */
	if (nmp->nm_sotype == SOCK_STREAM) {
		M_PREPEND(m, NFSX_UNSIGNED, M_WAIT);
		*mtod(m, u_int32_t *) = htonl(0x80000000 |
		    (m->m_pkthdr.len - NFSX_UNSIGNED));
	}

tryagain:
	rep->r_rtt = rep->r_rexmit = 0;
	if (nfs_ptimers[rep->r_procnum] != NFS_DEFAULT_TIMER)
		rep->r_flags = R_TIMING;
	else
		rep->r_flags = 0;
	rep->r_mrep = NULL;

	/*
	 * Do the client side RPC.
	 */
	nfsstats.rpcrequests++;
	/*
	 * Chain request into list of outstanding requests. Be sure
	 * to put it LAST so timer finds oldest requests first.
	 */
	if (TAILQ_EMPTY(&nmp->nm_reqsq))
		timeout_add(&nmp->nm_rtimeout, nfs_ticks);
	TAILQ_INSERT_TAIL(&nmp->nm_reqsq, rep, r_chain);

	/*
	 * If backing off another request or avoiding congestion, don't
	 * send this one now but let timer do it. If not timing a request,
	 * do it now.
	 */
	if (nmp->nm_so && (nmp->nm_sotype != SOCK_DGRAM ||
		(nmp->nm_flag & NFSMNT_DUMBTIMR) ||
		nmp->nm_sent < nmp->nm_cwnd)) {
		if (nmp->nm_soflags & PR_CONNREQUIRED)
			error = nfs_sndlock(&nmp->nm_flag, rep);
		if (!error) {
			error = nfs_send(nmp->nm_so, nmp->nm_nam,
			    m_copym(m, 0, M_COPYALL, M_WAIT), rep);
			if (nmp->nm_soflags & PR_CONNREQUIRED)
				nfs_sndunlock(&nmp->nm_flag);
		}
		if (!error && (rep->r_flags & R_MUSTRESEND) == 0) {
			nmp->nm_sent += NFS_CWNDSCALE;
			rep->r_flags |= R_SENT;
		}
	} else {
		rep->r_rtt = -1;
	}

	/*
	 * Wait for the reply from our send or the timer's.
	 */
	if (!error || error == EPIPE)
		error = nfs_reply(rep);

	/*
	 * RPC done, unlink the request.
	 */
	TAILQ_REMOVE(&nmp->nm_reqsq, rep, r_chain);
	if (TAILQ_EMPTY(&nmp->nm_reqsq))
		timeout_del(&nmp->nm_rtimeout);

	/*
	 * Decrement the outstanding request count.
	 */
	if (rep->r_flags & R_SENT) {
		rep->r_flags &= ~R_SENT;	/* paranoia */
		nmp->nm_sent -= NFS_CWNDSCALE;
	}

	/*
	 * If there was a successful reply and a tprintf msg.
	 * tprintf a response.
	 */
	if (!error && (rep->r_flags & R_TPRINTFMSG))
		nfs_msg(rep, "is alive again");
	info.nmi_mrep = rep->r_mrep;
	info.nmi_md = rep->r_md;
	info.nmi_dpos = rep->r_dpos;
	info.nmi_errorp = &error;
	if (error) {
		infop->nmi_mrep = NULL;
		goto nfsmout1;
	}

	/*
	 * break down the rpc header and check if ok
	 */
	tl = (uint32_t *)nfsm_dissect(&info, 3 * NFSX_UNSIGNED);
	if (tl == NULL)
		goto nfsmout;
	if (*tl++ == rpc_msgdenied) {
		if (*tl == rpc_mismatch)
			error = EOPNOTSUPP;
		else
			error = EACCES;	/* Should be EAUTH. */
		infop->nmi_mrep = NULL;
		goto nfsmout1;
	}

	/*
	 * Since we only support RPCAUTH_UNIX atm we step over the
	 * reply verifier type, and in the (error) case that there really
	 * is any data in it, we advance over it.
	 */
	tl++;			/* Step over verifier type */
	i = fxdr_unsigned(int32_t, *tl);
	if (i > 0) {
		/* Should not happen */
		if (nfsm_adv(&info, nfsm_rndup(i)) != 0)
			goto nfsmout;
	}

	tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
	if (tl == NULL)
		goto nfsmout;
	/* 0 == ok */
	if (*tl == 0) {
		tl = (uint32_t *)nfsm_dissect(&info, NFSX_UNSIGNED);
		if (tl == NULL)
			goto nfsmout;
		if (*tl != 0) {
			error = fxdr_unsigned(int, *tl);
			if ((nmp->nm_flag & NFSMNT_NFSV3) &&
			    error == NFSERR_TRYLATER) {
				m_freem(info.nmi_mrep);
				info.nmi_mrep = NULL;
				error = 0;
				tsleep_nsec(&nowake, PSOCK, "nfsretry",
				    SEC_TO_NSEC(trylater_delay));
				trylater_delay *= NFS_TIMEOUTMUL;
				if (trylater_delay > NFS_MAXTIMEO)
					trylater_delay = NFS_MAXTIMEO;

				goto tryagain;
			}

			/*
			 * If the File Handle was stale, invalidate the
			 * lookup cache, just in case.
			 */
			if (error == ESTALE)
				cache_purge(rep->r_vp);
		}
		goto nfsmout;
	}

	error = EPROTONOSUPPORT;

nfsmout:
	infop->nmi_mrep = info.nmi_mrep;
	infop->nmi_md = info.nmi_md;
	infop->nmi_dpos = info.nmi_dpos;
nfsmout1:
	m_freem(rep->r_mreq);
	pool_put(&nfsreqpl, rep);
	return (error);
}
#endif /* NFSCLIENT */

/*
 * Generate the rpc reply header
 * siz arg. is used to decide if adding a cluster is worthwhile
 */
int
nfs_rephead(int siz, struct nfsrv_descript *nd, struct nfssvc_sock *slp,
    int err, struct mbuf **mrq, struct mbuf **mbp)
{
	u_int32_t *tl;
	struct mbuf *mreq;
	struct mbuf *mb;

	MGETHDR(mreq, M_WAIT, MT_DATA);
	mb = mreq;
	/*
	 * If this is a big reply, use a cluster else
	 * try and leave leading space for the lower level headers.
	 */
	siz += RPC_REPLYSIZ;
	if (siz >= MHLEN - max_hdr) {
		MCLGET(mreq, M_WAIT);
	} else
		mreq->m_data += max_hdr;
	tl = mtod(mreq, u_int32_t *);
	mreq->m_len = 6 * NFSX_UNSIGNED;
	*tl++ = txdr_unsigned(nd->nd_retxid);
	*tl++ = rpc_reply;
	if (err == ERPCMISMATCH || (err & NFSERR_AUTHERR)) {
		*tl++ = rpc_msgdenied;
		if (err & NFSERR_AUTHERR) {
			*tl++ = rpc_autherr;
			*tl = txdr_unsigned(err & ~NFSERR_AUTHERR);
			mreq->m_len -= NFSX_UNSIGNED;
		} else {
			*tl++ = rpc_mismatch;
			*tl++ = txdr_unsigned(RPC_VER2);
			*tl = txdr_unsigned(RPC_VER2);
		}
	} else {
		*tl++ = rpc_msgaccepted;

		/* AUTH_UNIX requires RPCAUTH_NULL. */
		*tl++ = 0;
		*tl++ = 0;

		switch (err) {
		case EPROGUNAVAIL:
			*tl = txdr_unsigned(RPC_PROGUNAVAIL);
			break;
		case EPROGMISMATCH:
			*tl = txdr_unsigned(RPC_PROGMISMATCH);
			tl = nfsm_build(&mb, 2 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(NFS_VER2);
			*tl = txdr_unsigned(NFS_VER3);
			break;
		case EPROCUNAVAIL:
			*tl = txdr_unsigned(RPC_PROCUNAVAIL);
			break;
		case EBADRPC:
			*tl = txdr_unsigned(RPC_GARBAGE);
			break;
		default:
			*tl = 0;
			if (err != NFSERR_RETVOID) {
				tl = nfsm_build(&mb, NFSX_UNSIGNED);
				if (err)
				    *tl = txdr_unsigned(nfsrv_errmap(nd, err));
				else
				    *tl = 0;
			}
			break;
		}
	}

	*mrq = mreq;
	if (mbp != NULL)
		*mbp = mb;
	if (err != 0 && err != NFSERR_RETVOID)
		nfsstats.srvrpc_errs++;
	return (0);
}

/*
 * nfs timer routine
 * Scan the nfsreq list and retransmit any requests that have timed out.
 */
void
nfs_timer(void *arg)
{
	struct nfsmount *nmp = arg;
	struct nfsreq *rep;
	struct mbuf *m;
	struct socket *so;
	int timeo, error;

	NET_LOCK();
	TAILQ_FOREACH(rep, &nmp->nm_reqsq, r_chain) {
		if (rep->r_mrep || (rep->r_flags & R_SOFTTERM))
			continue;
		if (nfs_sigintr(nmp, rep, rep->r_procp)) {
			rep->r_flags |= R_SOFTTERM;
			continue;
		}
		if (rep->r_rtt >= 0) {
			rep->r_rtt++;
			if (nmp->nm_flag & NFSMNT_DUMBTIMR)
				timeo = nmp->nm_timeo;
			else
				timeo = nfs_estimate_rto(nmp, rep->r_procnum);
			if (nmp->nm_timeouts > 0)
				timeo *= nfs_backoff[nmp->nm_timeouts - 1];
			if (rep->r_rtt <= timeo)
				continue;
			if (nmp->nm_timeouts < nitems(nfs_backoff))
				nmp->nm_timeouts++;
		}

		/* Check for server not responding. */
		if ((rep->r_flags & R_TPRINTFMSG) == 0 && rep->r_rexmit > 4) {
			nfs_msg(rep, "not responding");
			rep->r_flags |= R_TPRINTFMSG;
		}
		if (rep->r_rexmit >= nmp->nm_retry) {	/* too many */
			nfsstats.rpctimeouts++;
			rep->r_flags |= R_SOFTTERM;
			continue;
		}
		if (nmp->nm_sotype != SOCK_DGRAM) {
			if (++rep->r_rexmit > NFS_MAXREXMIT)
				rep->r_rexmit = NFS_MAXREXMIT;
			continue;
		}

		if ((so = nmp->nm_so) == NULL)
			continue;

		/*
		 * If there is enough space and the window allows..
		 *	Resend it
		 * Set r_rtt to -1 in case we fail to send it now.
		 */
		rep->r_rtt = -1;
		if (sbspace(&so->so_snd) >= rep->r_mreq->m_pkthdr.len &&
		   ((nmp->nm_flag & NFSMNT_DUMBTIMR) ||
		    (rep->r_flags & R_SENT) ||
		    nmp->nm_sent < nmp->nm_cwnd) &&
		   (m = m_copym(rep->r_mreq, 0, M_COPYALL, M_DONTWAIT))){
			if ((nmp->nm_flag & NFSMNT_NOCONN) == 0)
				error = pru_send(so, m, NULL, NULL);
			else
				error = pru_send(so, m, nmp->nm_nam, NULL);
			if (error) {
				if (NFSIGNORE_SOERROR(nmp->nm_soflags, error))
					so->so_error = 0;
			} else {
				/*
				 * Iff first send, start timing
				 * else turn timing off, backoff timer
				 * and divide congestion window by 2.
				 */
				if (rep->r_flags & R_SENT) {
					rep->r_flags &= ~R_TIMING;
					if (++rep->r_rexmit > NFS_MAXREXMIT)
						rep->r_rexmit = NFS_MAXREXMIT;
					nmp->nm_cwnd >>= 1;
					if (nmp->nm_cwnd < NFS_CWNDSCALE)
						nmp->nm_cwnd = NFS_CWNDSCALE;
					nfsstats.rpcretries++;
				} else {
					rep->r_flags |= R_SENT;
					nmp->nm_sent += NFS_CWNDSCALE;
				}
				rep->r_rtt = 0;
			}
		}
	}
	NET_UNLOCK();
	timeout_add(&nmp->nm_rtimeout, nfs_ticks);
}

/*
 * Test for a termination condition pending on the process.
 * This is used for NFSMNT_INT mounts.
 */
int
nfs_sigintr(struct nfsmount *nmp, struct nfsreq *rep, struct proc *p)
{

	if (rep && (rep->r_flags & R_SOFTTERM))
		return (EINTR);
	if (!(nmp->nm_flag & NFSMNT_INT))
		return (0);
	if (p && (SIGPENDING(p) & ~p->p_p->ps_sigacts->ps_sigignore &
	    NFSINT_SIGMASK))
		return (EINTR);
	return (0);
}

/*
 * Lock a socket against others.
 * Necessary for STREAM sockets to ensure you get an entire rpc request/reply
 * and also to avoid race conditions between the processes with nfs requests
 * in progress when a reconnect is necessary.
 */
int
nfs_sndlock(int *flagp, struct nfsreq *rep)
{
	uint64_t slptimeo = INFSLP;
	struct proc *p;
	int slpflag = 0;

	if (rep) {
		p = rep->r_procp;
		if (rep->r_nmp->nm_flag & NFSMNT_INT)
			slpflag = PCATCH;
	} else
		p = NULL;
	while (*flagp & NFSMNT_SNDLOCK) {
		if (rep && nfs_sigintr(rep->r_nmp, rep, p))
			return (EINTR);
		*flagp |= NFSMNT_WANTSND;
		tsleep_nsec(flagp, slpflag | (PZERO - 1), "nfsndlck", slptimeo);
		if (slpflag == PCATCH) {
			slpflag = 0;
			slptimeo = SEC_TO_NSEC(2);
		}
	}
	*flagp |= NFSMNT_SNDLOCK;
	return (0);
}

/*
 * Unlock the stream socket for others.
 */
void
nfs_sndunlock(int *flagp)
{

	if ((*flagp & NFSMNT_SNDLOCK) == 0)
		panic("nfs sndunlock");
	*flagp &= ~NFSMNT_SNDLOCK;
	if (*flagp & NFSMNT_WANTSND) {
		*flagp &= ~NFSMNT_WANTSND;
		wakeup((caddr_t)flagp);
	}
}

int
nfs_rcvlock(struct nfsreq *rep)
{
	uint64_t slptimeo = INFSLP;
	int *flagp = &rep->r_nmp->nm_flag;
	int slpflag;

	if (*flagp & NFSMNT_INT)
		slpflag = PCATCH;
	else
		slpflag = 0;

	while (*flagp & NFSMNT_RCVLOCK) {
		if (nfs_sigintr(rep->r_nmp, rep, rep->r_procp))
			return (EINTR);
		*flagp |= NFSMNT_WANTRCV;
		tsleep_nsec(flagp, slpflag | (PZERO - 1), "nfsrcvlk", slptimeo);
		if (rep->r_mrep != NULL) {
			/*
			 * Don't take the lock if our reply has been received
			 * while we where sleeping.
			 */
			 return (EALREADY);
		}
		if (slpflag == PCATCH) {
			slpflag = 0;
			slptimeo = SEC_TO_NSEC(2);
		}
	}
	*flagp |= NFSMNT_RCVLOCK;
	return (0);
}

/*
 * Unlock the stream socket for others.
 */
void
nfs_rcvunlock(int *flagp)
{

	if ((*flagp & NFSMNT_RCVLOCK) == 0)
		panic("nfs rcvunlock");
	*flagp &= ~NFSMNT_RCVLOCK;
	if (*flagp & NFSMNT_WANTRCV) {
		*flagp &= ~NFSMNT_WANTRCV;
		wakeup(flagp);
	}
}

/*
 * Auxiliary routine to align the length of mbuf copies made with m_copyback().
 */
void
nfs_realign_fixup(struct mbuf *m, struct mbuf *n, unsigned int *off)
{
	size_t padding;

	/*
	 * The maximum number of bytes that m_copyback() places in a mbuf is
	 * always an aligned quantity, so realign happens at the chain's tail.
	 */
	while (n->m_next != NULL)
		n = n->m_next;

	/*
	 * Pad from the next elements in the source chain. Loop until the
	 * destination chain is aligned, or the end of the source is reached.
	 */
	do {
		m = m->m_next;
		if (m == NULL)
			return;

		padding = min(ALIGN(n->m_len) - n->m_len, m->m_len);
		if (padding > m_trailingspace(n))
			panic("nfs_realign_fixup: no memory to pad to");

		bcopy(mtod(m, void *), mtod(n, char *) + n->m_len, padding);

		n->m_len += padding;
		m_adj(m, padding);
		*off += padding;

	} while (!ALIGNED_POINTER(n->m_len, void *));
}

/*
 * The NFS RPC parsing code uses the data address and the length of mbuf
 * structures to calculate on-memory addresses. This function makes sure these
 * parameters are correctly aligned.
 */
void
nfs_realign(struct mbuf **pm, int hsiz)
{
	struct mbuf *m;
	struct mbuf *n = NULL;
	unsigned int off = 0;

	++nfs_realign_test;
	while ((m = *pm) != NULL) {
		if (!ALIGNED_POINTER(m->m_data, void *) ||
		    !ALIGNED_POINTER(m->m_len,  void *)) {
			MGET(n, M_WAIT, MT_DATA);
#define ALIGN_POINTER(n) ((u_int)(((n) + sizeof(void *)) & ~sizeof(void *)))
			if (ALIGN_POINTER(m->m_len) >= MINCLSIZE) {
				MCLGET(n, M_WAIT);
			}
			n->m_len = 0;
			break;
		}
		pm = &m->m_next;
	}
	/*
	 * If n is non-NULL, loop on m copying data, then replace the
	 * portion of the chain that had to be realigned.
	 */
	if (n != NULL) {
		++nfs_realign_count;
		while (m) {
			m_copyback(n, off, m->m_len, mtod(m, caddr_t), M_WAIT);

			/*
			 * If an unaligned amount of memory was copied, fix up
			 * the last mbuf created by m_copyback().
			 */
			if (!ALIGNED_POINTER(m->m_len, void *))
				nfs_realign_fixup(m, n, &off);

			off += m->m_len;
			m = m->m_next;
		}
		m_freemp(pm);
		*pm = n;
	}
}


/*
 * Parse an RPC request
 * - verify it
 * - fill in the cred struct.
 */
int
nfs_getreq(struct nfsrv_descript *nd, struct nfsd *nfsd, int has_header)
{
	int len, i;
	u_int32_t *tl;
	u_int32_t nfsvers, auth_type;
	int error = 0;
	struct nfsm_info info;

	info.nmi_mrep = nd->nd_mrep;
	info.nmi_md = nd->nd_md;
	info.nmi_dpos = nd->nd_dpos;
	info.nmi_errorp = &error;
	if (has_header) {
		tl = (uint32_t *)nfsm_dissect(&info, 10 * NFSX_UNSIGNED);
		if (tl == NULL)
			goto nfsmout;
		nd->nd_retxid = fxdr_unsigned(u_int32_t, *tl++);
		if (*tl++ != rpc_call) {
			m_freem(info.nmi_mrep);
			return (EBADRPC);
		}
	} else {
		tl = (uint32_t *)nfsm_dissect(&info, 8 * NFSX_UNSIGNED);
		if (tl == NULL)
			goto nfsmout;
	}
	nd->nd_repstat = 0;
	nd->nd_flag = 0;
	if (*tl++ != rpc_vers) {
		nd->nd_repstat = ERPCMISMATCH;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	if (*tl != nfs_prog) {
		nd->nd_repstat = EPROGUNAVAIL;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	tl++;
	nfsvers = fxdr_unsigned(u_int32_t, *tl++);
	if (nfsvers != NFS_VER2 && nfsvers != NFS_VER3) {
		nd->nd_repstat = EPROGMISMATCH;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	if (nfsvers == NFS_VER3)
		nd->nd_flag = ND_NFSV3;
	nd->nd_procnum = fxdr_unsigned(u_int32_t, *tl++);
	if (nd->nd_procnum == NFSPROC_NULL)
		return (0);
	if (nd->nd_procnum >= NFS_NPROCS ||
		(nd->nd_procnum > NFSPROC_COMMIT) ||
		(!nd->nd_flag && nd->nd_procnum > NFSV2PROC_STATFS)) {
		nd->nd_repstat = EPROCUNAVAIL;
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}
	if ((nd->nd_flag & ND_NFSV3) == 0)
		nd->nd_procnum = nfsv3_procid[nd->nd_procnum];
	auth_type = *tl++;
	len = fxdr_unsigned(int, *tl++);
	if (len < 0 || len > RPCAUTH_MAXSIZ) {
		m_freem(info.nmi_mrep);
		return (EBADRPC);
	}

	/* Handle auth_unix */
	if (auth_type == rpc_auth_unix) {
		len = fxdr_unsigned(int, *++tl);
		if (len < 0 || len > NFS_MAXNAMLEN) {
			m_freem(info.nmi_mrep);
			return (EBADRPC);
		}
		if (nfsm_adv(&info, nfsm_rndup(len)) != 0)
			goto nfsmout;
		tl = (uint32_t *)nfsm_dissect(&info, 3 * NFSX_UNSIGNED);
		if (tl == NULL)
			goto nfsmout;
		memset(&nd->nd_cr, 0, sizeof (struct ucred));
		refcnt_init(&nd->nd_cr.cr_refcnt);
		nd->nd_cr.cr_uid = fxdr_unsigned(uid_t, *tl++);
		nd->nd_cr.cr_gid = fxdr_unsigned(gid_t, *tl++);
		len = fxdr_unsigned(int, *tl);
		if (len < 0 || len > RPCAUTH_UNIXGIDS) {
			m_freem(info.nmi_mrep);
			return (EBADRPC);
		}
		tl = (uint32_t *)
		    nfsm_dissect(&info, (len + 2) * NFSX_UNSIGNED);
		if (tl == NULL)
			goto nfsmout;
		for (i = 0; i < len; i++) {
			if (i < NGROUPS_MAX)
				nd->nd_cr.cr_groups[i] =
				    fxdr_unsigned(gid_t, *tl++);
			else
				tl++;
		}
		nd->nd_cr.cr_ngroups = (len > NGROUPS_MAX) ? NGROUPS_MAX : len;
		len = fxdr_unsigned(int, *++tl);
		if (len < 0 || len > RPCAUTH_MAXSIZ) {
			m_freem(info.nmi_mrep);
			return (EBADRPC);
		}
		if (len > 0) {
			if (nfsm_adv(&info, nfsm_rndup(len)) != 0)
				goto nfsmout;
		}
	} else {
		nd->nd_repstat = (NFSERR_AUTHERR | AUTH_REJECTCRED);
		nd->nd_procnum = NFSPROC_NOOP;
		return (0);
	}

	nd->nd_md = info.nmi_md;
	nd->nd_dpos = info.nmi_dpos;
	return (0);
nfsmout:
	return (error);
}

void
nfs_msg(struct nfsreq *rep, char *msg)
{
	tpr_t tpr;

	if (rep->r_procp)
		tpr = tprintf_open(rep->r_procp);
	else
		tpr = NULL;

	tprintf(tpr, "nfs server %s: %s\n",
	    rep->r_nmp->nm_mountp->mnt_stat.f_mntfromname, msg);
	tprintf_close(tpr);
}

#ifdef NFSSERVER
/*
 * Socket upcall routine for the nfsd sockets.
 * The caddr_t arg is a pointer to the "struct nfssvc_sock".
 * Essentially do as much as possible non-blocking, else punt and it will
 * be called with M_WAIT from an nfsd.
 */
void
nfsrv_rcv(struct socket *so, caddr_t arg, int waitflag)
{
	struct nfssvc_sock *slp = (struct nfssvc_sock *)arg;
	struct mbuf *m;
	struct mbuf *mp, *nam;
	struct uio auio;
	int flags, error;

	KERNEL_LOCK();

	if ((slp->ns_flag & SLP_VALID) == 0)
		goto out;

	/* Defer soreceive() to an nfsd. */
	if (waitflag == M_DONTWAIT) {
		slp->ns_flag |= SLP_NEEDQ;
		goto dorecs;
	}

	auio.uio_procp = NULL;
	if (so->so_type == SOCK_STREAM) {
		/*
		 * Do soreceive().
		 */
		auio.uio_resid = 1000000000;
		flags = MSG_DONTWAIT;
		error = soreceive(so, NULL, &auio, &mp, NULL,
		    &flags, 0);
		if (error || mp == NULL) {
			if (error == EWOULDBLOCK)
				slp->ns_flag |= SLP_NEEDQ;
			else
				slp->ns_flag |= SLP_DISCONN;
			goto dorecs;
		}
		m = mp;
		if (slp->ns_rawend) {
			slp->ns_rawend->m_next = m;
			slp->ns_cc += 1000000000 - auio.uio_resid;
		} else {
			slp->ns_raw = m;
			slp->ns_cc = 1000000000 - auio.uio_resid;
		}
		while (m->m_next)
			m = m->m_next;
		slp->ns_rawend = m;

		/*
		 * Now try and parse record(s) out of the raw stream data.
		 */
		error = nfsrv_getstream(slp, waitflag);
		if (error) {
			if (error == EPERM)
				slp->ns_flag |= SLP_DISCONN;
			else
				slp->ns_flag |= SLP_NEEDQ;
		}
	} else {
		do {
			auio.uio_resid = 1000000000;
			flags = MSG_DONTWAIT;
			error = soreceive(so, &nam, &auio, &mp,
			    NULL, &flags, 0);
			if (mp) {
				m = nam;
				m->m_next = mp;
				if (slp->ns_recend)
					slp->ns_recend->m_nextpkt = m;
				else
					slp->ns_rec = m;
				slp->ns_recend = m;
				m->m_nextpkt = NULL;
			}
			if (error) {
				if ((so->so_proto->pr_flags & PR_CONNREQUIRED)
					&& error != EWOULDBLOCK) {
					slp->ns_flag |= SLP_DISCONN;
					goto dorecs;
				}
			}
		} while (mp);
	}

	/*
	 * Now try and process the request records, non-blocking.
	 */
dorecs:
	if (waitflag == M_DONTWAIT &&
		(slp->ns_rec || (slp->ns_flag & (SLP_NEEDQ | SLP_DISCONN))))
		nfsrv_wakenfsd(slp);

out:
	KERNEL_UNLOCK();
}

/*
 * Try and extract an RPC request from the mbuf data list received on a
 * stream socket. The "waitflag" argument indicates whether or not it
 * can sleep.
 */
int
nfsrv_getstream(struct nfssvc_sock *slp, int waitflag)
{
	struct mbuf *m, **mpp;
	char *cp1, *cp2;
	int len;
	struct mbuf *om, *m2, *recm;
	u_int32_t recmark;

	if (slp->ns_flag & SLP_GETSTREAM)
		return (0);
	slp->ns_flag |= SLP_GETSTREAM;
	for (;;) {
		if (slp->ns_reclen == 0) {
			if (slp->ns_cc < NFSX_UNSIGNED) {
				slp->ns_flag &= ~SLP_GETSTREAM;
				return (0);
			}
			m = slp->ns_raw;
			if (m->m_len >= NFSX_UNSIGNED) {
				bcopy(mtod(m, caddr_t), &recmark,
				    NFSX_UNSIGNED);
				m->m_data += NFSX_UNSIGNED;
				m->m_len -= NFSX_UNSIGNED;
			} else {
				cp1 = (caddr_t)&recmark;
				cp2 = mtod(m, caddr_t);
				while (cp1 < ((caddr_t)&recmark) + NFSX_UNSIGNED) {
					while (m->m_len == 0) {
						m = m->m_next;
						cp2 = mtod(m, caddr_t);
					}
					*cp1++ = *cp2++;
					m->m_data++;
					m->m_len--;
				}
			}
			slp->ns_cc -= NFSX_UNSIGNED;
			recmark = ntohl(recmark);
			slp->ns_reclen = recmark & ~0x80000000;
			if (recmark & 0x80000000)
				slp->ns_flag |= SLP_LASTFRAG;
			else
				slp->ns_flag &= ~SLP_LASTFRAG;
			if (slp->ns_reclen > NFS_MAXPACKET) {
				slp->ns_flag &= ~SLP_GETSTREAM;
				return (EPERM);
			}
		}

		/*
		 * Now get the record part.
		 */
		recm = NULL;
		if (slp->ns_cc == slp->ns_reclen) {
			recm = slp->ns_raw;
			slp->ns_raw = slp->ns_rawend = NULL;
			slp->ns_cc = slp->ns_reclen = 0;
		} else if (slp->ns_cc > slp->ns_reclen) {
			len = 0;
			m = slp->ns_raw;
			om = NULL;
			while (len < slp->ns_reclen) {
				if ((len + m->m_len) > slp->ns_reclen) {
					m2 = m_copym(m, 0, slp->ns_reclen - len,
					    waitflag);
					if (m2) {
						if (om) {
							om->m_next = m2;
							recm = slp->ns_raw;
						} else
							recm = m2;
						m->m_data += slp->ns_reclen-len;
						m->m_len -= slp->ns_reclen-len;
						len = slp->ns_reclen;
					} else {
						slp->ns_flag &= ~SLP_GETSTREAM;
						return (EWOULDBLOCK);
					}
				} else if ((len + m->m_len) == slp->ns_reclen) {
					om = m;
					len += m->m_len;
					m = m->m_next;
					recm = slp->ns_raw;
					om->m_next = NULL;
				} else {
					om = m;
					len += m->m_len;
					m = m->m_next;
				}
			}
			slp->ns_raw = m;
			slp->ns_cc -= len;
			slp->ns_reclen = 0;
		} else {
			slp->ns_flag &= ~SLP_GETSTREAM;
			return (0);
		}

		/*
		 * Accumulate the fragments into a record.
		 */
		mpp = &slp->ns_frag;
		while (*mpp)
			mpp = &((*mpp)->m_next);
		*mpp = recm;
		if (slp->ns_flag & SLP_LASTFRAG) {
			if (slp->ns_recend)
			    slp->ns_recend->m_nextpkt = slp->ns_frag;
			else
			    slp->ns_rec = slp->ns_frag;
			slp->ns_recend = slp->ns_frag;
			slp->ns_frag = NULL;
		}
	}
}

/*
 * Parse an RPC header.
 */
int
nfsrv_dorec(struct nfssvc_sock *slp, struct nfsd *nfsd,
    struct nfsrv_descript **ndp)
{
	struct mbuf *m, *nam;
	struct nfsrv_descript *nd;
	int error;

	*ndp = NULL;
	if ((slp->ns_flag & SLP_VALID) == 0 ||
	    (m = slp->ns_rec) == NULL)
		return (ENOBUFS);
	slp->ns_rec = m->m_nextpkt;
	if (slp->ns_rec)
		m->m_nextpkt = NULL;
	else
		slp->ns_recend = NULL;
	if (m->m_type == MT_SONAME) {
		nam = m;
		m = m->m_next;
		nam->m_next = NULL;
	} else
		nam = NULL;
	nd = pool_get(&nfsrv_descript_pl, PR_WAITOK);
	nfs_realign(&m, 10 * NFSX_UNSIGNED);
	nd->nd_md = nd->nd_mrep = m;
	nd->nd_nam2 = nam;
	nd->nd_dpos = mtod(m, caddr_t);
	error = nfs_getreq(nd, nfsd, 1);
	if (error) {
		m_freem(nam);
		pool_put(&nfsrv_descript_pl, nd);
		return (error);
	}
	*ndp = nd;
	nfsd->nfsd_nd = nd;
	return (0);
}


/*
 * Search for a sleeping nfsd and wake it up.
 * SIDE EFFECT: If none found, set NFSD_CHECKSLP flag, so that one of the
 * running nfsds will go look for the work in the nfssvc_sock list.
 */
void
nfsrv_wakenfsd(struct nfssvc_sock *slp)
{
	struct nfsd	*nfsd;

	if ((slp->ns_flag & SLP_VALID) == 0)
		return;

	TAILQ_FOREACH(nfsd, &nfsd_head, nfsd_chain) {
		if (nfsd->nfsd_flag & NFSD_WAITING) {
			nfsd->nfsd_flag &= ~NFSD_WAITING;
			if (nfsd->nfsd_slp)
				panic("nfsd wakeup");
			slp->ns_sref++;
			nfsd->nfsd_slp = slp;
			wakeup_one(nfsd);
			return;
		}
	}

	slp->ns_flag |= SLP_DOREC;
	nfsd_head_flag |= NFSD_CHECKSLP;
}
#endif /* NFSSERVER */
