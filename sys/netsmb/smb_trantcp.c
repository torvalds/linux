/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/mchain.h>

#include <netsmb/netbios.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_tran.h>
#include <netsmb/smb_trantcp.h>
#include <netsmb/smb_subr.h>

#define M_NBDATA	M_PCB

static int smb_tcpsndbuf = NB_SNDQ - 1;
static int smb_tcprcvbuf = NB_RCVQ - 1;

SYSCTL_DECL(_net_smb);
SYSCTL_INT(_net_smb, OID_AUTO, tcpsndbuf, CTLFLAG_RW, &smb_tcpsndbuf, 0, "");
SYSCTL_INT(_net_smb, OID_AUTO, tcprcvbuf, CTLFLAG_RW, &smb_tcprcvbuf, 0, "");

#define nb_sosend(so,m,flags,td) sosend(so, NULL, 0, m, 0, flags, td)

static int  nbssn_recv(struct nbpcb *nbp, struct mbuf **mpp, int *lenp,
	u_int8_t *rpcodep, struct thread *td);
static int  smb_nbst_disconnect(struct smb_vc *vcp, struct thread *td);

static int
nb_setsockopt_int(struct socket *so, int level, int name, int val)
{
	struct sockopt sopt;
	int error;

	bzero(&sopt, sizeof(sopt));
	sopt.sopt_level = level;
	sopt.sopt_name = name;
	sopt.sopt_val = &val;
	sopt.sopt_valsize = sizeof(val);
	CURVNET_SET(so->so_vnet);
	error = sosetopt(so, &sopt);
	CURVNET_RESTORE();
	return error;
}

static int
nb_intr(struct nbpcb *nbp, struct proc *p)
{
	return 0;
}

static int
nb_upcall(struct socket *so, void *arg, int waitflag)
{
	struct nbpcb *nbp = arg;

	if (arg == NULL || nbp->nbp_selectid == NULL)
		return (SU_OK);
	wakeup(nbp->nbp_selectid);
	return (SU_OK);
}

static int
nb_sethdr(struct mbuf *m, u_int8_t type, u_int32_t len)
{
	u_int32_t *p = mtod(m, u_int32_t *);

	*p = htonl((len & 0x1FFFF) | (type << 24));
	return 0;
}

static int
nb_put_name(struct mbchain *mbp, struct sockaddr_nb *snb)
{
	int error;
	u_char seglen, *cp;

	cp = snb->snb_name;
	if (*cp == 0)
		return EINVAL;
	NBDEBUG("[%s]\n", cp);
	for (;;) {
		seglen = (*cp) + 1;
		error = mb_put_mem(mbp, cp, seglen, MB_MSYSTEM);
		if (error)
			return error;
		if (seglen == 1)
			break;
		cp += seglen;
	}
	return 0;
}

static int
nb_connect_in(struct nbpcb *nbp, struct sockaddr_in *to, struct thread *td)
{
	struct socket *so;
	int error, s;

	error = socreate(AF_INET, &so, SOCK_STREAM, IPPROTO_TCP,
	    td->td_ucred, td);
	if (error)
		return error;
	nbp->nbp_tso = so;
	SOCKBUF_LOCK(&so->so_rcv);
	soupcall_set(so, SO_RCV, nb_upcall, nbp);
	SOCKBUF_UNLOCK(&so->so_rcv);
	so->so_rcv.sb_timeo = (5 * SBT_1S);
	so->so_snd.sb_timeo = (5 * SBT_1S);
	error = soreserve(so, nbp->nbp_sndbuf, nbp->nbp_rcvbuf);
	if (error)
		goto bad;
	nb_setsockopt_int(so, SOL_SOCKET, SO_KEEPALIVE, 1);
	nb_setsockopt_int(so, IPPROTO_TCP, TCP_NODELAY, 1);
	SOCKBUF_LOCK(&so->so_rcv);
	so->so_rcv.sb_flags &= ~SB_NOINTR;
	SOCKBUF_UNLOCK(&so->so_rcv);
	SOCKBUF_LOCK(&so->so_snd);
	so->so_snd.sb_flags &= ~SB_NOINTR;
	SOCKBUF_UNLOCK(&so->so_snd);
	error = soconnect(so, (struct sockaddr*)to, td);
	if (error)
		goto bad;
	s = splnet();
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		tsleep(&so->so_timeo, PSOCK, "nbcon", 2 * hz);
		if ((so->so_state & SS_ISCONNECTING) && so->so_error == 0 &&
			(error = nb_intr(nbp, td->td_proc)) != 0) {
			so->so_state &= ~SS_ISCONNECTING;
			splx(s);
			goto bad;
		}
	}
	if (so->so_error) {
		error = so->so_error;
		so->so_error = 0;
		splx(s);
		goto bad;
	}
	splx(s);
	return 0;
bad:
	smb_nbst_disconnect(nbp->nbp_vc, td);
	return error;
}

static int
nbssn_rq_request(struct nbpcb *nbp, struct thread *td)
{
	struct mbchain *mbp;
	struct mdchain *mdp;
	struct mbuf *m0;
	struct timeval tv;
	struct sockaddr_in sin;
	u_short port;
	u_int8_t rpcode;
	int error, rplen;

	mbp = malloc(sizeof(struct mbchain), M_NBDATA, M_WAITOK);
	mdp = malloc(sizeof(struct mbchain), M_NBDATA, M_WAITOK);
	error = mb_init(mbp);
	if (error) {
		free(mbp, M_NBDATA);
		free(mdp, M_NBDATA);
		return error;
	}
	mb_put_uint32le(mbp, 0);
	nb_put_name(mbp, nbp->nbp_paddr);
	nb_put_name(mbp, nbp->nbp_laddr);
	nb_sethdr(mbp->mb_top, NB_SSN_REQUEST, mb_fixhdr(mbp) - 4);
	error = nb_sosend(nbp->nbp_tso, mbp->mb_top, 0, td);
	if (!error) {
		nbp->nbp_state = NBST_RQSENT;
	}
	mb_detach(mbp);
	mb_done(mbp);
	free(mbp, M_NBDATA);
	if (error) {
		free(mdp, M_NBDATA);
		return error;
	}
	TIMESPEC_TO_TIMEVAL(&tv, &nbp->nbp_timo);
	error = selsocket(nbp->nbp_tso, POLLIN, &tv, td);
	if (error == EWOULDBLOCK) {	/* Timeout */
		NBDEBUG("initial request timeout\n");
		free(mdp, M_NBDATA);
		return ETIMEDOUT;
	}
	if (error) {			/* restart or interrupt */
		free(mdp, M_NBDATA);
		return error;
	}
	error = nbssn_recv(nbp, &m0, &rplen, &rpcode, td);
	if (error) {
		NBDEBUG("recv() error %d\n", error);
		free(mdp, M_NBDATA);
		return error;
	}
	/*
	 * Process NETBIOS reply
	 */
	if (m0)
		md_initm(mdp, m0);
	error = 0;
	do {
		if (rpcode == NB_SSN_POSRESP) {
			nbp->nbp_state = NBST_SESSION;
			nbp->nbp_flags |= NBF_CONNECTED;
			break;
		}
		if (rpcode != NB_SSN_RTGRESP) {
			error = ECONNABORTED;
			break;
		}
		if (rplen != 6) {
			error = ECONNABORTED;
			break;
		}
		md_get_mem(mdp, (caddr_t)&sin.sin_addr, 4, MB_MSYSTEM);
		md_get_uint16(mdp, &port);
		sin.sin_port = port;
		nbp->nbp_state = NBST_RETARGET;
		smb_nbst_disconnect(nbp->nbp_vc, td);
		error = nb_connect_in(nbp, &sin, td);
		if (!error)
			error = nbssn_rq_request(nbp, td);
		if (error) {
			smb_nbst_disconnect(nbp->nbp_vc, td);
			break;
		}
	} while(0);
	if (m0)
		md_done(mdp);
	free(mdp, M_NBDATA);
	return error;
}

static int
nbssn_recvhdr(struct nbpcb *nbp, int *lenp,
	u_int8_t *rpcodep, int flags, struct thread *td)
{
	struct socket *so = nbp->nbp_tso;
	struct uio auio;
	struct iovec aio;
	u_int32_t len;
	int error;

	aio.iov_base = (caddr_t)&len;
	aio.iov_len = sizeof(len);
	auio.uio_iov = &aio;
	auio.uio_iovcnt = 1;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_offset = 0;
	auio.uio_resid = sizeof(len);
	auio.uio_td = td;
	CURVNET_SET(so->so_vnet);
	error = soreceive(so, (struct sockaddr **)NULL, &auio,
	    (struct mbuf **)NULL, (struct mbuf **)NULL, &flags);
	CURVNET_RESTORE();
	if (error)
		return error;
	if (auio.uio_resid > 0) {
		SMBSDEBUG("short reply\n");
		return EPIPE;
	}
	len = ntohl(len);
	*rpcodep = (len >> 24) & 0xFF;
	len &= 0x1ffff;
	if (len > SMB_MAXPKTLEN) {
		SMBERROR("packet too long (%d)\n", len);
		return EFBIG;
	}
	*lenp = len;
	return 0;
}

static int
nbssn_recv(struct nbpcb *nbp, struct mbuf **mpp, int *lenp,
	u_int8_t *rpcodep, struct thread *td)
{
	struct socket *so = nbp->nbp_tso;
	struct uio auio;
	struct mbuf *m, *tm, *im;
	u_int8_t rpcode;
	int len, resid;
	int error, rcvflg;

	if (so == NULL)
		return ENOTCONN;

	if (mpp)
		*mpp = NULL;
	m = NULL;
	for(;;) {
		/*
		 * Poll for a response header.
		 * If we don't have one waiting, return.
		 */
		len = 0;
		rpcode = 0;
		error = nbssn_recvhdr(nbp, &len, &rpcode, MSG_DONTWAIT, td);
		if ((so->so_state & (SS_ISDISCONNECTING | SS_ISDISCONNECTED)) ||
		    (so->so_rcv.sb_state & SBS_CANTRCVMORE)) {
			nbp->nbp_state = NBST_CLOSED;
			NBDEBUG("session closed by peer\n");
			return ECONNRESET;
		}
		if (error)
			return error;
		if (len == 0 && nbp->nbp_state != NBST_SESSION)
			break;
		/* no data, try again */
		if (rpcode == NB_SSN_KEEPALIVE)
			continue;

		/*
		 * Loop, blocking, for data following the response header.
		 *
		 * Note that we can't simply block here with MSG_WAITALL for the
		 * entire response size, as it may be larger than the TCP
		 * slow-start window that the sender employs.  This will result
		 * in the sender stalling until the delayed ACK is sent, then
		 * resuming slow-start, resulting in very poor performance.
		 *
		 * Instead, we never request more than NB_SORECEIVE_CHUNK
		 * bytes at a time, resulting in an ack being pushed by
		 * the TCP code at the completion of each call.
		 */
		resid = len;
		while (resid > 0) {
			tm = NULL;
			rcvflg = MSG_WAITALL;
			bzero(&auio, sizeof(auio));
			auio.uio_resid = min(resid, NB_SORECEIVE_CHUNK);
			auio.uio_td = td;
			resid -= auio.uio_resid;
			/*
			 * Spin until we have collected everything in
			 * this chunk.
			 */
			do {
				rcvflg = MSG_WAITALL;
				CURVNET_SET(so->so_vnet);
				error = soreceive(so, (struct sockaddr **)NULL,
				    &auio, &tm, (struct mbuf **)NULL, &rcvflg);
				CURVNET_RESTORE();
			} while (error == EWOULDBLOCK || error == EINTR ||
				 error == ERESTART);
			if (error)
				goto out;
			/* short return guarantees unhappiness */
			if (auio.uio_resid > 0) {
				SMBERROR("packet is shorter than expected\n");
				error = EPIPE;
				goto out;
			}
			/* append received chunk to previous chunk(s) */
			if (m == NULL) {
				m = tm;
			} else {
				/*
				 * Just glue the new chain on the end.
				 * Consumer will pullup as required.
				 */
				for (im = m; im->m_next != NULL; im = im->m_next)
					;
				im->m_next = tm;
			}
		}
		/* got a session/message packet? */
		if (nbp->nbp_state == NBST_SESSION &&
		    rpcode == NB_SSN_MESSAGE)
			break;
		/* drop packet and try for another */
		NBDEBUG("non-session packet %x\n", rpcode);
		if (m) {
			m_freem(m);
			m = NULL;
		}
	}

out:
	if (error) {
		if (m)
			m_freem(m);
		return error;
	}
	if (mpp)
		*mpp = m;
	else
		m_freem(m);
	*lenp = len;
	*rpcodep = rpcode;
	return 0;
}

/*
 * SMB transport interface
 */
static int
smb_nbst_create(struct smb_vc *vcp, struct thread *td)
{
	struct nbpcb *nbp;

	nbp = malloc(sizeof *nbp, M_NBDATA, M_WAITOK);
	bzero(nbp, sizeof *nbp);
	nbp->nbp_timo.tv_sec = 15;	/* XXX: sysctl ? */
	nbp->nbp_state = NBST_CLOSED;
	nbp->nbp_vc = vcp;
	nbp->nbp_sndbuf = smb_tcpsndbuf;
	nbp->nbp_rcvbuf = smb_tcprcvbuf;
	vcp->vc_tdata = nbp;
	return 0;
}

static int
smb_nbst_done(struct smb_vc *vcp, struct thread *td)
{
	struct nbpcb *nbp = vcp->vc_tdata;

	if (nbp == NULL)
		return ENOTCONN;
	smb_nbst_disconnect(vcp, td);
	if (nbp->nbp_laddr)
		free(nbp->nbp_laddr, M_SONAME);
	if (nbp->nbp_paddr)
		free(nbp->nbp_paddr, M_SONAME);
	free(nbp, M_NBDATA);
	return 0;
}

static int
smb_nbst_bind(struct smb_vc *vcp, struct sockaddr *sap, struct thread *td)
{
	struct nbpcb *nbp = vcp->vc_tdata;
	struct sockaddr_nb *snb;
	int error, slen;

	NBDEBUG("\n");
	error = EINVAL;
	do {
		if (nbp->nbp_flags & NBF_LOCADDR)
			break;
		/*
		 * It is possible to create NETBIOS name in the kernel,
		 * but nothing prevents us to do it in the user space.
		 */
		if (sap == NULL)
			break;
		slen = sap->sa_len;
		if (slen < NB_MINSALEN)
			break;
		snb = (struct sockaddr_nb*)sodupsockaddr(sap, M_WAITOK);
		if (snb == NULL) {
			error = ENOMEM;
			break;
		}
		nbp->nbp_laddr = snb;
		nbp->nbp_flags |= NBF_LOCADDR;
		error = 0;
	} while(0);
	return error;
}

static int
smb_nbst_connect(struct smb_vc *vcp, struct sockaddr *sap, struct thread *td)
{
	struct nbpcb *nbp = vcp->vc_tdata;
	struct sockaddr_in sin;
	struct sockaddr_nb *snb;
	struct timespec ts1, ts2;
	int error, slen;

	NBDEBUG("\n");
	if (nbp->nbp_tso != NULL)
		return EISCONN;
	if (nbp->nbp_laddr == NULL)
		return EINVAL;
	slen = sap->sa_len;
	if (slen < NB_MINSALEN)
		return EINVAL;
	if (nbp->nbp_paddr) {
		free(nbp->nbp_paddr, M_SONAME);
		nbp->nbp_paddr = NULL;
	}
	snb = (struct sockaddr_nb*)sodupsockaddr(sap, M_WAITOK);
	if (snb == NULL)
		return ENOMEM;
	nbp->nbp_paddr = snb;
	sin = snb->snb_addrin;
	getnanotime(&ts1);
	error = nb_connect_in(nbp, &sin, td);
	if (error)
		return error;
	getnanotime(&ts2);
	timespecsub(&ts2, &ts1, &ts2);
	if (ts2.tv_sec == 0) {
		ts2.tv_sec = 1;
		ts2.tv_nsec = 0;
	}
	timespecadd(&ts2, &ts2, &nbp->nbp_timo);
	timespecadd(&nbp->nbp_timo, &ts2, &nbp->nbp_timo);
	timespecadd(&nbp->nbp_timo, &ts2, &nbp->nbp_timo);	/*  * 4 */
	error = nbssn_rq_request(nbp, td);
	if (error)
		smb_nbst_disconnect(vcp, td);
	return error;
}

static int
smb_nbst_disconnect(struct smb_vc *vcp, struct thread *td)
{
	struct nbpcb *nbp = vcp->vc_tdata;
	struct socket *so;

	if (nbp == NULL || nbp->nbp_tso == NULL)
		return ENOTCONN;
	if ((so = nbp->nbp_tso) != NULL) {
		nbp->nbp_flags &= ~NBF_CONNECTED;
		nbp->nbp_tso = (struct socket *)NULL;
		soshutdown(so, 2);
		soclose(so);
	}
	if (nbp->nbp_state != NBST_RETARGET) {
		nbp->nbp_state = NBST_CLOSED;
	}
	return 0;
}

static int
smb_nbst_send(struct smb_vc *vcp, struct mbuf *m0, struct thread *td)
{
	struct nbpcb *nbp = vcp->vc_tdata;
	int error;

	if (nbp->nbp_state != NBST_SESSION) {
		error = ENOTCONN;
		goto abort;
	}
	M_PREPEND(m0, 4, M_WAITOK);
	nb_sethdr(m0, NB_SSN_MESSAGE, m_fixhdr(m0) - 4);
	error = nb_sosend(nbp->nbp_tso, m0, 0, td);
	return error;
abort:
	if (m0)
		m_freem(m0);
	return error;
}


static int
smb_nbst_recv(struct smb_vc *vcp, struct mbuf **mpp, struct thread *td)
{
	struct nbpcb *nbp = vcp->vc_tdata;
	u_int8_t rpcode;
	int error, rplen;

	nbp->nbp_flags |= NBF_RECVLOCK;
	error = nbssn_recv(nbp, mpp, &rplen, &rpcode, td);
	nbp->nbp_flags &= ~NBF_RECVLOCK;
	return error;
}

static void
smb_nbst_timo(struct smb_vc *vcp)
{
	return;
}

static void
smb_nbst_intr(struct smb_vc *vcp)
{
	struct nbpcb *nbp = vcp->vc_tdata;

	if (nbp == NULL || nbp->nbp_tso == NULL)
		return;
	sorwakeup(nbp->nbp_tso);
	sowwakeup(nbp->nbp_tso);
}

static int
smb_nbst_getparam(struct smb_vc *vcp, int param, void *data)
{
	struct nbpcb *nbp = vcp->vc_tdata;

	switch (param) {
	    case SMBTP_SNDSZ:
		*(int*)data = nbp->nbp_sndbuf;
		break;
	    case SMBTP_RCVSZ:
		*(int*)data = nbp->nbp_rcvbuf;
		break;
	    case SMBTP_TIMEOUT:
		*(struct timespec*)data = nbp->nbp_timo;
		break;
	    default:
		return EINVAL;
	}
	return 0;
}

static int
smb_nbst_setparam(struct smb_vc *vcp, int param, void *data)
{
	struct nbpcb *nbp = vcp->vc_tdata;

	switch (param) {
	    case SMBTP_SELECTID:
		nbp->nbp_selectid = data;
		break;
	    default:
		return EINVAL;
	}
	return 0;
}

/*
 * Check for fatal errors
 */
static int
smb_nbst_fatal(struct smb_vc *vcp, int error)
{
	switch (error) {
	    case ENOTCONN:
	    case ENETRESET:
	    case ECONNABORTED:
		return 1;
	}
	return 0;
}


struct smb_tran_desc smb_tran_nbtcp_desc = {
	SMBT_NBTCP,
	smb_nbst_create, smb_nbst_done,
	smb_nbst_bind, smb_nbst_connect, smb_nbst_disconnect,
	smb_nbst_send, smb_nbst_recv,
	smb_nbst_timo, smb_nbst_intr,
	smb_nbst_getparam, smb_nbst_setparam,
	smb_nbst_fatal
};

