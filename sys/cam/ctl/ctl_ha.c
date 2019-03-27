/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Alexander Motin <mav@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/types.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <vm/uma.h>

#include <cam/cam.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/ctl/ctl_io.h>
#include <cam/ctl/ctl.h>
#include <cam/ctl/ctl_frontend.h>
#include <cam/ctl/ctl_util.h>
#include <cam/ctl/ctl_backend.h>
#include <cam/ctl/ctl_ioctl.h>
#include <cam/ctl/ctl_ha.h>
#include <cam/ctl/ctl_private.h>
#include <cam/ctl/ctl_debug.h>
#include <cam/ctl/ctl_error.h>

#if (__FreeBSD_version < 1100000)
struct mbufq {
	struct mbuf *head;
	struct mbuf *tail;
};

static void
mbufq_init(struct mbufq *q, int limit)
{

	q->head = q->tail = NULL;
}

static void
mbufq_drain(struct mbufq *q)
{
	struct mbuf *m;

	while ((m = q->head) != NULL) {
		q->head = m->m_nextpkt;
		m_freem(m);
	}
	q->tail = NULL;
}

static struct mbuf *
mbufq_dequeue(struct mbufq *q)
{
	struct mbuf *m;

	m = q->head;
	if (m) {
		if (q->tail == m)
			q->tail = NULL;
		q->head = m->m_nextpkt;
		m->m_nextpkt = NULL;
	}
	return (m);
}

static void
mbufq_enqueue(struct mbufq *q, struct mbuf *m)
{

	m->m_nextpkt = NULL;
	if (q->tail)
		q->tail->m_nextpkt = m;
	else
		q->head = m;
	q->tail = m;
}

static u_int
sbavail(struct sockbuf *sb)
{
	return (sb->sb_cc);
}

#if (__FreeBSD_version < 1000000)
#define	mtodo(m, o)	((void *)(((m)->m_data) + (o)))
#endif
#endif

struct ha_msg_wire {
	uint32_t	 channel;
	uint32_t	 length;
};

struct ha_dt_msg_wire {
	ctl_ha_dt_cmd	command;
	uint32_t	size;
	uint8_t		*local;
	uint8_t		*remote;
};

struct ha_softc {
	struct ctl_softc *ha_ctl_softc;
	ctl_evt_handler	 ha_handler[CTL_HA_CHAN_MAX];
	char		 ha_peer[128];
	struct sockaddr_in  ha_peer_in;
	struct socket	*ha_lso;
	struct socket	*ha_so;
	struct mbufq	 ha_sendq;
	struct mbuf	*ha_sending;
	struct mtx	 ha_lock;
	int		 ha_connect;
	int		 ha_listen;
	int		 ha_connected;
	int		 ha_receiving;
	int		 ha_wakeup;
	int		 ha_disconnect;
	int		 ha_shutdown;
	eventhandler_tag ha_shutdown_eh;
	TAILQ_HEAD(, ctl_ha_dt_req) ha_dts;
} ha_softc;

static void
ctl_ha_conn_wake(struct ha_softc *softc)
{

	mtx_lock(&softc->ha_lock);
	softc->ha_wakeup = 1;
	mtx_unlock(&softc->ha_lock);
	wakeup(&softc->ha_wakeup);
}

static int
ctl_ha_lupcall(struct socket *so, void *arg, int waitflag)
{
	struct ha_softc *softc = arg;

	ctl_ha_conn_wake(softc);
	return (SU_OK);
}

static int
ctl_ha_rupcall(struct socket *so, void *arg, int waitflag)
{
	struct ha_softc *softc = arg;

	wakeup(&softc->ha_receiving);
	return (SU_OK);
}

static int
ctl_ha_supcall(struct socket *so, void *arg, int waitflag)
{
	struct ha_softc *softc = arg;

	ctl_ha_conn_wake(softc);
	return (SU_OK);
}

static void
ctl_ha_evt(struct ha_softc *softc, ctl_ha_channel ch, ctl_ha_event evt,
    int param)
{
	int i;

	if (ch < CTL_HA_CHAN_MAX) {
		if (softc->ha_handler[ch])
			softc->ha_handler[ch](ch, evt, param);
		return;
	}
	for (i = 0; i < CTL_HA_CHAN_MAX; i++) {
		if (softc->ha_handler[i])
			softc->ha_handler[i](i, evt, param);
	}
}

static void
ctl_ha_close(struct ha_softc *softc)
{
	struct socket *so = softc->ha_so;
	int report = 0;

	if (softc->ha_connected || softc->ha_disconnect) {
		softc->ha_connected = 0;
		mbufq_drain(&softc->ha_sendq);
		m_freem(softc->ha_sending);
		softc->ha_sending = NULL;
		report = 1;
	}
	if (so) {
		SOCKBUF_LOCK(&so->so_rcv);
		soupcall_clear(so, SO_RCV);
		while (softc->ha_receiving) {
			wakeup(&softc->ha_receiving);
			msleep(&softc->ha_receiving, SOCKBUF_MTX(&so->so_rcv),
			    0, "ha_rx exit", 0);
		}
		SOCKBUF_UNLOCK(&so->so_rcv);
		SOCKBUF_LOCK(&so->so_snd);
		soupcall_clear(so, SO_SND);
		SOCKBUF_UNLOCK(&so->so_snd);
		softc->ha_so = NULL;
		if (softc->ha_connect)
			pause("reconnect", hz / 2);
		soclose(so);
	}
	if (report) {
		ctl_ha_evt(softc, CTL_HA_CHAN_MAX, CTL_HA_EVT_LINK_CHANGE,
		    (softc->ha_connect || softc->ha_listen) ?
		    CTL_HA_LINK_UNKNOWN : CTL_HA_LINK_OFFLINE);
	}
}

static void
ctl_ha_lclose(struct ha_softc *softc)
{

	if (softc->ha_lso) {
		SOCKBUF_LOCK(&softc->ha_lso->so_rcv);
		soupcall_clear(softc->ha_lso, SO_RCV);
		SOCKBUF_UNLOCK(&softc->ha_lso->so_rcv);
		soclose(softc->ha_lso);
		softc->ha_lso = NULL;
	}
}

static void
ctl_ha_rx_thread(void *arg)
{
	struct ha_softc *softc = arg;
	struct socket *so = softc->ha_so;
	struct ha_msg_wire wire_hdr;
	struct uio uio;
	struct iovec iov;
	int error, flags, next;

	bzero(&wire_hdr, sizeof(wire_hdr));
	while (1) {
		if (wire_hdr.length > 0)
			next = wire_hdr.length;
		else
			next = sizeof(wire_hdr);
		SOCKBUF_LOCK(&so->so_rcv);
		while (sbavail(&so->so_rcv) < next || softc->ha_disconnect) {
			if (softc->ha_connected == 0 || softc->ha_disconnect ||
			    so->so_error ||
			    (so->so_rcv.sb_state & SBS_CANTRCVMORE)) {
				goto errout;
			}
			so->so_rcv.sb_lowat = next;
			msleep(&softc->ha_receiving, SOCKBUF_MTX(&so->so_rcv),
			    0, "-", 0);
		}
		SOCKBUF_UNLOCK(&so->so_rcv);

		if (wire_hdr.length == 0) {
			iov.iov_base = &wire_hdr;
			iov.iov_len = sizeof(wire_hdr);
			uio.uio_iov = &iov;
			uio.uio_iovcnt = 1;
			uio.uio_rw = UIO_READ;
			uio.uio_segflg = UIO_SYSSPACE;
			uio.uio_td = curthread;
			uio.uio_resid = sizeof(wire_hdr);
			flags = MSG_DONTWAIT;
			error = soreceive(softc->ha_so, NULL, &uio, NULL,
			    NULL, &flags);
			if (error != 0) {
				printf("%s: header receive error %d\n",
				    __func__, error);
				SOCKBUF_LOCK(&so->so_rcv);
				goto errout;
			}
		} else {
			ctl_ha_evt(softc, wire_hdr.channel,
			    CTL_HA_EVT_MSG_RECV, wire_hdr.length);
			wire_hdr.length = 0;
		}
	}

errout:
	softc->ha_receiving = 0;
	wakeup(&softc->ha_receiving);
	SOCKBUF_UNLOCK(&so->so_rcv);
	ctl_ha_conn_wake(softc);
	kthread_exit();
}

static void
ctl_ha_send(struct ha_softc *softc)
{
	struct socket *so = softc->ha_so;
	int error;

	while (1) {
		if (softc->ha_sending == NULL) {
			mtx_lock(&softc->ha_lock);
			softc->ha_sending = mbufq_dequeue(&softc->ha_sendq);
			mtx_unlock(&softc->ha_lock);
			if (softc->ha_sending == NULL) {
				so->so_snd.sb_lowat = so->so_snd.sb_hiwat + 1;
				break;
			}
		}
		SOCKBUF_LOCK(&so->so_snd);
		if (sbspace(&so->so_snd) < softc->ha_sending->m_pkthdr.len) {
			so->so_snd.sb_lowat = softc->ha_sending->m_pkthdr.len;
			SOCKBUF_UNLOCK(&so->so_snd);
			break;
		}
		SOCKBUF_UNLOCK(&so->so_snd);
		error = sosend(softc->ha_so, NULL, NULL, softc->ha_sending,
		    NULL, MSG_DONTWAIT, curthread);
		softc->ha_sending = NULL;
		if (error != 0) {
			printf("%s: sosend() error %d\n", __func__, error);
			return;
		}
	}
}

static void
ctl_ha_sock_setup(struct ha_softc *softc)
{
	struct sockopt opt;
	struct socket *so = softc->ha_so;
	int error, val;

	val = 1024 * 1024;
	error = soreserve(so, val, val);
	if (error)
		printf("%s: soreserve failed %d\n", __func__, error);

	SOCKBUF_LOCK(&so->so_rcv);
	so->so_rcv.sb_lowat = sizeof(struct ha_msg_wire);
	soupcall_set(so, SO_RCV, ctl_ha_rupcall, softc);
	SOCKBUF_UNLOCK(&so->so_rcv);
	SOCKBUF_LOCK(&so->so_snd);
	so->so_snd.sb_lowat = sizeof(struct ha_msg_wire);
	soupcall_set(so, SO_SND, ctl_ha_supcall, softc);
	SOCKBUF_UNLOCK(&so->so_snd);

	bzero(&opt, sizeof(struct sockopt));
	opt.sopt_dir = SOPT_SET;
	opt.sopt_level = SOL_SOCKET;
	opt.sopt_name = SO_KEEPALIVE;
	opt.sopt_val = &val;
	opt.sopt_valsize = sizeof(val);
	val = 1;
	error = sosetopt(so, &opt);
	if (error)
		printf("%s: KEEPALIVE setting failed %d\n", __func__, error);

	opt.sopt_level = IPPROTO_TCP;
	opt.sopt_name = TCP_NODELAY;
	val = 1;
	error = sosetopt(so, &opt);
	if (error)
		printf("%s: NODELAY setting failed %d\n", __func__, error);

	opt.sopt_name = TCP_KEEPINIT;
	val = 3;
	error = sosetopt(so, &opt);
	if (error)
		printf("%s: KEEPINIT setting failed %d\n", __func__, error);

	opt.sopt_name = TCP_KEEPIDLE;
	val = 1;
	error = sosetopt(so, &opt);
	if (error)
		printf("%s: KEEPIDLE setting failed %d\n", __func__, error);

	opt.sopt_name = TCP_KEEPINTVL;
	val = 1;
	error = sosetopt(so, &opt);
	if (error)
		printf("%s: KEEPINTVL setting failed %d\n", __func__, error);

	opt.sopt_name = TCP_KEEPCNT;
	val = 5;
	error = sosetopt(so, &opt);
	if (error)
		printf("%s: KEEPCNT setting failed %d\n", __func__, error);
}

static int
ctl_ha_connect(struct ha_softc *softc)
{
	struct thread *td = curthread;
	struct sockaddr_in sa;
	struct socket *so;
	int error;

	/* Create the socket */
	error = socreate(PF_INET, &so, SOCK_STREAM,
	    IPPROTO_TCP, td->td_ucred, td);
	if (error != 0) {
		printf("%s: socreate() error %d\n", __func__, error);
		return (error);
	}
	softc->ha_so = so;
	ctl_ha_sock_setup(softc);

	memcpy(&sa, &softc->ha_peer_in, sizeof(sa));
	error = soconnect(so, (struct sockaddr *)&sa, td);
	if (error != 0) {
		if (bootverbose)
			printf("%s: soconnect() error %d\n", __func__, error);
		goto out;
	}
	return (0);

out:
	ctl_ha_close(softc);
	return (error);
}

static int
ctl_ha_accept(struct ha_softc *softc)
{
	struct socket *lso, *so;
	struct sockaddr *sap;
	int error;

	lso = softc->ha_lso;
	SOLISTEN_LOCK(lso);
	error = solisten_dequeue(lso, &so, 0);
	if (error == EWOULDBLOCK)
		return (error);
	if (error) {
		printf("%s: socket error %d\n", __func__, error);
		goto out;
	}

	sap = NULL;
	error = soaccept(so, &sap);
	if (error != 0) {
		printf("%s: soaccept() error %d\n", __func__, error);
		if (sap != NULL)
			free(sap, M_SONAME);
		goto out;
	}
	if (sap != NULL)
		free(sap, M_SONAME);
	softc->ha_so = so;
	ctl_ha_sock_setup(softc);
	return (0);

out:
	ctl_ha_lclose(softc);
	return (error);
}

static int
ctl_ha_listen(struct ha_softc *softc)
{
	struct thread *td = curthread;
	struct sockaddr_in sa;
	struct sockopt opt;
	int error, val;

	/* Create the socket */
	if (softc->ha_lso == NULL) {
		error = socreate(PF_INET, &softc->ha_lso, SOCK_STREAM,
		    IPPROTO_TCP, td->td_ucred, td);
		if (error != 0) {
			printf("%s: socreate() error %d\n", __func__, error);
			return (error);
		}
		bzero(&opt, sizeof(struct sockopt));
		opt.sopt_dir = SOPT_SET;
		opt.sopt_level = SOL_SOCKET;
		opt.sopt_name = SO_REUSEADDR;
		opt.sopt_val = &val;
		opt.sopt_valsize = sizeof(val);
		val = 1;
		error = sosetopt(softc->ha_lso, &opt);
		if (error) {
			printf("%s: REUSEADDR setting failed %d\n",
			    __func__, error);
		}
		bzero(&opt, sizeof(struct sockopt));
		opt.sopt_dir = SOPT_SET;
		opt.sopt_level = SOL_SOCKET;
		opt.sopt_name = SO_REUSEPORT;
		opt.sopt_val = &val;
		opt.sopt_valsize = sizeof(val);
		val = 1;
		error = sosetopt(softc->ha_lso, &opt);
		if (error) {
			printf("%s: REUSEPORT setting failed %d\n",
			    __func__, error);
		}
	}

	memcpy(&sa, &softc->ha_peer_in, sizeof(sa));
	error = sobind(softc->ha_lso, (struct sockaddr *)&sa, td);
	if (error != 0) {
		printf("%s: sobind() error %d\n", __func__, error);
		goto out;
	}
	error = solisten(softc->ha_lso, 1, td);
	if (error != 0) {
		printf("%s: solisten() error %d\n", __func__, error);
		goto out;
	}
	SOLISTEN_LOCK(softc->ha_lso);
	softc->ha_lso->so_state |= SS_NBIO;
	solisten_upcall_set(softc->ha_lso, ctl_ha_lupcall, softc);
	SOLISTEN_UNLOCK(softc->ha_lso);
	return (0);

out:
	ctl_ha_lclose(softc);
	return (error);
}

static void
ctl_ha_conn_thread(void *arg)
{
	struct ha_softc *softc = arg;
	int error;

	while (1) {
		if (softc->ha_disconnect || softc->ha_shutdown) {
			ctl_ha_close(softc);
			if (softc->ha_disconnect == 2 || softc->ha_shutdown)
				ctl_ha_lclose(softc);
			softc->ha_disconnect = 0;
			if (softc->ha_shutdown)
				break;
		} else if (softc->ha_so != NULL &&
		    (softc->ha_so->so_error ||
		     softc->ha_so->so_rcv.sb_state & SBS_CANTRCVMORE))
			ctl_ha_close(softc);
		if (softc->ha_so == NULL) {
			if (softc->ha_lso != NULL)
				ctl_ha_accept(softc);
			else if (softc->ha_listen)
				ctl_ha_listen(softc);
			else if (softc->ha_connect)
				ctl_ha_connect(softc);
		}
		if (softc->ha_so != NULL) {
			if (softc->ha_connected == 0 &&
			    softc->ha_so->so_error == 0 &&
			    (softc->ha_so->so_state & SS_ISCONNECTING) == 0) {
				softc->ha_connected = 1;
				ctl_ha_evt(softc, CTL_HA_CHAN_MAX,
				    CTL_HA_EVT_LINK_CHANGE,
				    CTL_HA_LINK_ONLINE);
				softc->ha_receiving = 1;
				error = kproc_kthread_add(ctl_ha_rx_thread,
				    softc, &softc->ha_ctl_softc->ctl_proc,
				    NULL, 0, 0, "ctl", "ha_rx");
				if (error != 0) {
					printf("Error creating CTL HA rx thread!\n");
					softc->ha_receiving = 0;
					softc->ha_disconnect = 1;
				}
			}
			ctl_ha_send(softc);
		}
		mtx_lock(&softc->ha_lock);
		if (softc->ha_so != NULL &&
		    (softc->ha_so->so_error ||
		     softc->ha_so->so_rcv.sb_state & SBS_CANTRCVMORE))
			;
		else if (!softc->ha_wakeup)
			msleep(&softc->ha_wakeup, &softc->ha_lock, 0, "-", hz);
		softc->ha_wakeup = 0;
		mtx_unlock(&softc->ha_lock);
	}
	mtx_lock(&softc->ha_lock);
	softc->ha_shutdown = 2;
	wakeup(&softc->ha_wakeup);
	mtx_unlock(&softc->ha_lock);
	kthread_exit();
}

static int
ctl_ha_peer_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct ha_softc *softc = (struct ha_softc *)arg1;
	struct sockaddr_in *sa;
	int error, b1, b2, b3, b4, p, num;
	char buf[128];

	strlcpy(buf, softc->ha_peer, sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if ((error != 0) || (req->newptr == NULL) ||
	    strncmp(buf, softc->ha_peer, sizeof(buf)) == 0)
		return (error);

	sa = &softc->ha_peer_in;
	mtx_lock(&softc->ha_lock);
	if ((num = sscanf(buf, "connect %d.%d.%d.%d:%d",
	    &b1, &b2, &b3, &b4, &p)) >= 4) {
		softc->ha_connect = 1;
		softc->ha_listen = 0;
	} else if ((num = sscanf(buf, "listen %d.%d.%d.%d:%d",
	    &b1, &b2, &b3, &b4, &p)) >= 4) {
		softc->ha_connect = 0;
		softc->ha_listen = 1;
	} else {
		softc->ha_connect = 0;
		softc->ha_listen = 0;
		if (buf[0] != 0) {
			buf[0] = 0;
			error = EINVAL;
		}
	}
	strlcpy(softc->ha_peer, buf, sizeof(softc->ha_peer));
	if (softc->ha_connect || softc->ha_listen) {
		memset(sa, 0, sizeof(*sa));
		sa->sin_len = sizeof(struct sockaddr_in);
		sa->sin_family = AF_INET;
		sa->sin_port = htons((num >= 5) ? p : 999);
		sa->sin_addr.s_addr =
		    htonl((b1 << 24) + (b2 << 16) + (b3 << 8) + b4);
	}
	softc->ha_disconnect = 2;
	softc->ha_wakeup = 1;
	mtx_unlock(&softc->ha_lock);
	wakeup(&softc->ha_wakeup);
	return (error);
}

ctl_ha_status
ctl_ha_msg_register(ctl_ha_channel channel, ctl_evt_handler handler)
{
	struct ha_softc *softc = &ha_softc;

	KASSERT(channel < CTL_HA_CHAN_MAX,
	    ("Wrong CTL HA channel %d", channel));
	softc->ha_handler[channel] = handler;
	return (CTL_HA_STATUS_SUCCESS);
}

ctl_ha_status
ctl_ha_msg_deregister(ctl_ha_channel channel)
{
	struct ha_softc *softc = &ha_softc;

	KASSERT(channel < CTL_HA_CHAN_MAX,
	    ("Wrong CTL HA channel %d", channel));
	softc->ha_handler[channel] = NULL;
	return (CTL_HA_STATUS_SUCCESS);
}

/*
 * Receive a message of the specified size.
 */
ctl_ha_status
ctl_ha_msg_recv(ctl_ha_channel channel, void *addr, size_t len,
		int wait)
{
	struct ha_softc *softc = &ha_softc;
	struct uio uio;
	struct iovec iov;
	int error, flags;

	if (!softc->ha_connected)
		return (CTL_HA_STATUS_DISCONNECT);

	iov.iov_base = addr;
	iov.iov_len = len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_rw = UIO_READ;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_td = curthread;
	uio.uio_resid = len;
	flags = wait ? 0 : MSG_DONTWAIT;
	error = soreceive(softc->ha_so, NULL, &uio, NULL, NULL, &flags);
	if (error == 0)
		return (CTL_HA_STATUS_SUCCESS);

	/* Consider all errors fatal for HA sanity. */
	mtx_lock(&softc->ha_lock);
	if (softc->ha_connected) {
		softc->ha_disconnect = 1;
		softc->ha_wakeup = 1;
		wakeup(&softc->ha_wakeup);
	}
	mtx_unlock(&softc->ha_lock);
	return (CTL_HA_STATUS_ERROR);
}

/*
 * Send a message of the specified size.
 */
ctl_ha_status
ctl_ha_msg_send2(ctl_ha_channel channel, const void *addr, size_t len,
    const void *addr2, size_t len2, int wait)
{
	struct ha_softc *softc = &ha_softc;
	struct mbuf *mb, *newmb;
	struct ha_msg_wire hdr;
	size_t copylen, off;

	if (!softc->ha_connected)
		return (CTL_HA_STATUS_DISCONNECT);

	newmb = m_getm2(NULL, sizeof(hdr) + len + len2, wait, MT_DATA,
	    M_PKTHDR);
	if (newmb == NULL) {
		/* Consider all errors fatal for HA sanity. */
		mtx_lock(&softc->ha_lock);
		if (softc->ha_connected) {
			softc->ha_disconnect = 1;
			softc->ha_wakeup = 1;
			wakeup(&softc->ha_wakeup);
		}
		mtx_unlock(&softc->ha_lock);
		printf("%s: Can't allocate mbuf chain\n", __func__);
		return (CTL_HA_STATUS_ERROR);
	}
	hdr.channel = channel;
	hdr.length = len + len2;
	mb = newmb;
	memcpy(mtodo(mb, 0), &hdr, sizeof(hdr));
	mb->m_len += sizeof(hdr);
	off = 0;
	for (; mb != NULL && off < len; mb = mb->m_next) {
		copylen = min(M_TRAILINGSPACE(mb), len - off);
		memcpy(mtodo(mb, mb->m_len), (const char *)addr + off, copylen);
		mb->m_len += copylen;
		off += copylen;
		if (off == len)
			break;
	}
	KASSERT(off == len, ("%s: off (%zu) != len (%zu)", __func__,
	    off, len));
	off = 0;
	for (; mb != NULL && off < len2; mb = mb->m_next) {
		copylen = min(M_TRAILINGSPACE(mb), len2 - off);
		memcpy(mtodo(mb, mb->m_len), (const char *)addr2 + off, copylen);
		mb->m_len += copylen;
		off += copylen;
	}
	KASSERT(off == len2, ("%s: off (%zu) != len2 (%zu)", __func__,
	    off, len2));
	newmb->m_pkthdr.len = sizeof(hdr) + len + len2;

	mtx_lock(&softc->ha_lock);
	if (!softc->ha_connected) {
		mtx_unlock(&softc->ha_lock);
		m_freem(newmb);
		return (CTL_HA_STATUS_DISCONNECT);
	}
	mbufq_enqueue(&softc->ha_sendq, newmb);
	softc->ha_wakeup = 1;
	mtx_unlock(&softc->ha_lock);
	wakeup(&softc->ha_wakeup);
	return (CTL_HA_STATUS_SUCCESS);
}

ctl_ha_status
ctl_ha_msg_send(ctl_ha_channel channel, const void *addr, size_t len,
    int wait)
{

	return (ctl_ha_msg_send2(channel, addr, len, NULL, 0, wait));
}

ctl_ha_status
ctl_ha_msg_abort(ctl_ha_channel channel)
{
	struct ha_softc *softc = &ha_softc;

	mtx_lock(&softc->ha_lock);
	softc->ha_disconnect = 1;
	softc->ha_wakeup = 1;
	mtx_unlock(&softc->ha_lock);
	wakeup(&softc->ha_wakeup);
	return (CTL_HA_STATUS_SUCCESS);
}

/*
 * Allocate a data transfer request structure.
 */
struct ctl_ha_dt_req *
ctl_dt_req_alloc(void)
{

	return (malloc(sizeof(struct ctl_ha_dt_req), M_CTL, M_WAITOK | M_ZERO));
}

/*
 * Free a data transfer request structure.
 */
void
ctl_dt_req_free(struct ctl_ha_dt_req *req)
{

	free(req, M_CTL);
}

/*
 * Issue a DMA request for a single buffer.
 */
ctl_ha_status
ctl_dt_single(struct ctl_ha_dt_req *req)
{
	struct ha_softc *softc = &ha_softc;
	struct ha_dt_msg_wire wire_dt;
	ctl_ha_status status;

	wire_dt.command = req->command;
	wire_dt.size = req->size;
	wire_dt.local = req->local;
	wire_dt.remote = req->remote;
	if (req->command == CTL_HA_DT_CMD_READ && req->callback != NULL) {
		mtx_lock(&softc->ha_lock);
		TAILQ_INSERT_TAIL(&softc->ha_dts, req, links);
		mtx_unlock(&softc->ha_lock);
		ctl_ha_msg_send(CTL_HA_CHAN_DATA, &wire_dt, sizeof(wire_dt),
		    M_WAITOK);
		return (CTL_HA_STATUS_WAIT);
	}
	if (req->command == CTL_HA_DT_CMD_READ) {
		status = ctl_ha_msg_send(CTL_HA_CHAN_DATA, &wire_dt,
		    sizeof(wire_dt), M_WAITOK);
	} else {
		status = ctl_ha_msg_send2(CTL_HA_CHAN_DATA, &wire_dt,
		    sizeof(wire_dt), req->local, req->size, M_WAITOK);
	}
	return (status);
}

static void
ctl_dt_event_handler(ctl_ha_channel channel, ctl_ha_event event, int param)
{
	struct ha_softc *softc = &ha_softc;
	struct ctl_ha_dt_req *req;
	ctl_ha_status isc_status;

	if (event == CTL_HA_EVT_MSG_RECV) {
		struct ha_dt_msg_wire wire_dt;
		uint8_t *tmp;
		int size;

		size = min(sizeof(wire_dt), param);
		isc_status = ctl_ha_msg_recv(CTL_HA_CHAN_DATA, &wire_dt,
					     size, M_WAITOK);
		if (isc_status != CTL_HA_STATUS_SUCCESS) {
			printf("%s: Error receiving message: %d\n",
			    __func__, isc_status);
			return;
		}

		if (wire_dt.command == CTL_HA_DT_CMD_READ) {
			wire_dt.command = CTL_HA_DT_CMD_WRITE;
			tmp = wire_dt.local;
			wire_dt.local = wire_dt.remote;
			wire_dt.remote = tmp;
			ctl_ha_msg_send2(CTL_HA_CHAN_DATA, &wire_dt,
			    sizeof(wire_dt), wire_dt.local, wire_dt.size,
			    M_WAITOK);
		} else if (wire_dt.command == CTL_HA_DT_CMD_WRITE) {
			isc_status = ctl_ha_msg_recv(CTL_HA_CHAN_DATA,
			    wire_dt.remote, wire_dt.size, M_WAITOK);
			mtx_lock(&softc->ha_lock);
			TAILQ_FOREACH(req, &softc->ha_dts, links) {
				if (req->local == wire_dt.remote) {
					TAILQ_REMOVE(&softc->ha_dts, req, links);
					break;
				}
			}
			mtx_unlock(&softc->ha_lock);
			if (req) {
				req->ret = isc_status;
				req->callback(req);
			}
		}
	} else if (event == CTL_HA_EVT_LINK_CHANGE) {
		CTL_DEBUG_PRINT(("%s: Link state change to %d\n", __func__,
		    param));
		if (param != CTL_HA_LINK_ONLINE) {
			mtx_lock(&softc->ha_lock);
			while ((req = TAILQ_FIRST(&softc->ha_dts)) != NULL) {
				TAILQ_REMOVE(&softc->ha_dts, req, links);
				mtx_unlock(&softc->ha_lock);
				req->ret = CTL_HA_STATUS_DISCONNECT;
				req->callback(req);
				mtx_lock(&softc->ha_lock);
			}
			mtx_unlock(&softc->ha_lock);
		}
	} else {
		printf("%s: Unknown event %d\n", __func__, event);
	}
}


ctl_ha_status
ctl_ha_msg_init(struct ctl_softc *ctl_softc)
{
	struct ha_softc *softc = &ha_softc;
	int error;

	softc->ha_ctl_softc = ctl_softc;
	mtx_init(&softc->ha_lock, "CTL HA mutex", NULL, MTX_DEF);
	mbufq_init(&softc->ha_sendq, INT_MAX);
	TAILQ_INIT(&softc->ha_dts);
	error = kproc_kthread_add(ctl_ha_conn_thread, softc,
	    &ctl_softc->ctl_proc, NULL, 0, 0, "ctl", "ha_tx");
	if (error != 0) {
		printf("error creating CTL HA connection thread!\n");
		mtx_destroy(&softc->ha_lock);
		return (CTL_HA_STATUS_ERROR);
	}
	softc->ha_shutdown_eh = EVENTHANDLER_REGISTER(shutdown_pre_sync,
	    ctl_ha_msg_shutdown, ctl_softc, SHUTDOWN_PRI_FIRST);
	SYSCTL_ADD_PROC(&ctl_softc->sysctl_ctx,
	    SYSCTL_CHILDREN(ctl_softc->sysctl_tree),
	    OID_AUTO, "ha_peer", CTLTYPE_STRING | CTLFLAG_RWTUN,
	    softc, 0, ctl_ha_peer_sysctl, "A", "HA peer connection method");

	if (ctl_ha_msg_register(CTL_HA_CHAN_DATA, ctl_dt_event_handler)
	    != CTL_HA_STATUS_SUCCESS) {
		printf("%s: ctl_ha_msg_register failed.\n", __func__);
	}

	return (CTL_HA_STATUS_SUCCESS);
};

void
ctl_ha_msg_shutdown(struct ctl_softc *ctl_softc)
{
	struct ha_softc *softc = &ha_softc;

	/* Disconnect and shutdown threads. */
	mtx_lock(&softc->ha_lock);
	if (softc->ha_shutdown < 2) {
		softc->ha_shutdown = 1;
		softc->ha_wakeup = 1;
		wakeup(&softc->ha_wakeup);
		while (softc->ha_shutdown < 2 && !SCHEDULER_STOPPED()) {
			msleep(&softc->ha_wakeup, &softc->ha_lock, 0,
			    "shutdown", hz);
		}
	}
	mtx_unlock(&softc->ha_lock);
};

ctl_ha_status
ctl_ha_msg_destroy(struct ctl_softc *ctl_softc)
{
	struct ha_softc *softc = &ha_softc;

	if (softc->ha_shutdown_eh != NULL) {
		EVENTHANDLER_DEREGISTER(shutdown_pre_sync,
		    softc->ha_shutdown_eh);
		softc->ha_shutdown_eh = NULL;
	}

	ctl_ha_msg_shutdown(ctl_softc);	/* Just in case. */

	if (ctl_ha_msg_deregister(CTL_HA_CHAN_DATA) != CTL_HA_STATUS_SUCCESS)
		printf("%s: ctl_ha_msg_deregister failed.\n", __func__);

	mtx_destroy(&softc->ha_lock);
	return (CTL_HA_STATUS_SUCCESS);
};
