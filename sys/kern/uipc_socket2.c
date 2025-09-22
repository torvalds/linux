/*	$OpenBSD: uipc_socket2.c,v 1.186 2025/07/14 21:47:26 bluhm Exp $	*/
/*	$NetBSD: uipc_socket2.c,v 1.11 1996/02/04 02:17:55 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)uipc_socket2.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/pool.h>

/*
 * Primitive routines for operating on sockets and socket buffers
 */

u_long sb_max = SB_MAX;		/* [I] patchable */

extern struct pool mclpools[];
extern struct pool mbpool;

/*
 * Procedures to manipulate state flags of socket
 * and do appropriate wakeups.  Normal sequence from the
 * active (originating) side is that soisconnecting() is
 * called during processing of connect() call,
 * resulting in an eventual call to soisconnected() if/when the
 * connection is established.  When the connection is torn down
 * soisdisconnecting() is called during processing of disconnect() call,
 * and soisdisconnected() is called when the connection to the peer
 * is totally severed.  The semantics of these routines are such that
 * connectionless protocols can call soisconnected() and soisdisconnected()
 * only, bypassing the in-progress calls when setting up a ``connection''
 * takes no time.
 *
 * From the passive side, a socket is created with
 * two queues of sockets: so_q0 for connections in progress
 * and so_q for connections already made and awaiting user acceptance.
 * As a protocol is preparing incoming connections, it creates a socket
 * structure queued on so_q0 by calling sonewconn().  When the connection
 * is established, soisconnected() is called, and transfers the
 * socket structure to so_q, making it available to accept().
 *
 * If a socket is closed with sockets on either
 * so_q0 or so_q, these sockets are dropped.
 *
 * If higher level protocols are implemented in
 * the kernel, the wakeups done here will sometimes
 * cause software-interrupt process scheduling.
 */

void
soisconnecting(struct socket *so)
{
	soassertlocked(so);
	so->so_state &= ~(SS_ISCONNECTED|SS_ISDISCONNECTING);
	so->so_state |= SS_ISCONNECTING;
}

void
soisconnected(struct socket *so)
{
	struct socket *head = so->so_head;

	soassertlocked(so);
	so->so_state &= ~(SS_ISCONNECTING|SS_ISDISCONNECTING);
	so->so_state |= SS_ISCONNECTED;

	if (head != NULL && so->so_onq == &head->so_q0) {
		soref(head);
		sounlock(so);
		solock(head);
		solock(so);

		if (so->so_onq != &head->so_q0) {
			sounlock(head);
			sorele(head);
			return;
		}

		soqremque(so, 0);
		soqinsque(head, so, 1);
		sorwakeup(head);
		wakeup_one(&head->so_timeo);

		sounlock(head);
		sorele(head);
	} else {
		wakeup(&so->so_timeo);
		sorwakeup(so);
		sowwakeup(so);
	}
}

void
soisdisconnecting(struct socket *so)
{
	soassertlocked(so);
	so->so_state &= ~SS_ISCONNECTING;
	so->so_state |= SS_ISDISCONNECTING;

	mtx_enter(&so->so_rcv.sb_mtx);
	so->so_rcv.sb_state |= SS_CANTRCVMORE;
	mtx_leave(&so->so_rcv.sb_mtx);

	mtx_enter(&so->so_snd.sb_mtx);
	so->so_snd.sb_state |= SS_CANTSENDMORE;
	mtx_leave(&so->so_snd.sb_mtx);

	wakeup(&so->so_timeo);
	sowwakeup(so);
	sorwakeup(so);
}

void
soisdisconnected(struct socket *so)
{
	soassertlocked(so);

	mtx_enter(&so->so_rcv.sb_mtx);
	so->so_rcv.sb_state |= SS_CANTRCVMORE;
	mtx_leave(&so->so_rcv.sb_mtx);

	mtx_enter(&so->so_snd.sb_mtx);
	so->so_snd.sb_state |= SS_CANTSENDMORE;
	mtx_leave(&so->so_snd.sb_mtx);

	so->so_state &= ~(SS_ISCONNECTING|SS_ISCONNECTED|SS_ISDISCONNECTING);
	so->so_state |= SS_ISDISCONNECTED;

	wakeup(&so->so_timeo);
	sowwakeup(so);
	sorwakeup(so);
}

/*
 * When an attempt at a new connection is noted on a socket
 * which accepts connections, sonewconn is called.  If the
 * connection is possible (subject to space constraints, etc.)
 * then we allocate a new structure, properly linked into the
 * data structure of the original socket, and return this.
 * Connstatus may be 0 or SS_ISCONNECTED.
 */
struct socket *
sonewconn(struct socket *head, int connstatus, int wait)
{
	struct socket *so;
	int soqueue = connstatus ? 1 : 0;

	soassertlocked(head);

	if (m_pool_used() > 95)
		return (NULL);
	if (head->so_qlen + head->so_q0len > head->so_qlimit * 3)
		return (NULL);
	so = soalloc(head->so_proto, wait);
	if (so == NULL)
		return (NULL);
	so->so_type = head->so_type;
	so->so_options = head->so_options &~ SO_ACCEPTCONN;
	so->so_linger = head->so_linger;
	so->so_state = head->so_state | SS_NOFDREF;
	so->so_proto = head->so_proto;
	so->so_timeo = head->so_timeo;
	so->so_euid = head->so_euid;
	so->so_ruid = head->so_ruid;
	so->so_egid = head->so_egid;
	so->so_rgid = head->so_rgid;
	so->so_cpid = head->so_cpid;

	/*
	 * Lock order will be `head' -> `so' while these sockets are linked.
	 */
	solock_nonet(so);

	/*
	 * Inherit watermarks but those may get clamped in low mem situations.
	 */
	if (soreserve(so, head->so_snd.sb_hiwat, head->so_rcv.sb_hiwat))
		goto fail;

	mtx_enter(&head->so_snd.sb_mtx);
	so->so_snd.sb_wat = head->so_snd.sb_wat;
	so->so_snd.sb_lowat = head->so_snd.sb_lowat;
	so->so_snd.sb_timeo_nsecs = head->so_snd.sb_timeo_nsecs;
	mtx_leave(&head->so_snd.sb_mtx);

	mtx_enter(&head->so_rcv.sb_mtx);
	so->so_rcv.sb_wat = head->so_rcv.sb_wat;
	so->so_rcv.sb_lowat = head->so_rcv.sb_lowat;
	so->so_rcv.sb_timeo_nsecs = head->so_rcv.sb_timeo_nsecs;
	mtx_leave(&head->so_rcv.sb_mtx);

	sigio_copy(&so->so_sigio, &head->so_sigio);

	soqinsque(head, so, soqueue);
	if (pru_attach(so, 0, wait) != 0) {
		soqremque(so, soqueue);
		goto fail;
	}
	if (connstatus) {
		so->so_state |= connstatus;
		sorwakeup(head);
		wakeup(&head->so_timeo);
	}

	return (so);

fail:
	sounlock_nonet(so);
	sigio_free(&so->so_sigio);
	klist_free(&so->so_rcv.sb_klist);
	klist_free(&so->so_snd.sb_klist);
	pool_put(&socket_pool, so);

	return (NULL);
}

void
soqinsque(struct socket *head, struct socket *so, int q)
{
	soassertlocked(head);
	soassertlocked(so);

	KASSERT(so->so_onq == NULL);

	so->so_head = head;
	if (q == 0) {
		head->so_q0len++;
		so->so_onq = &head->so_q0;
	} else {
		head->so_qlen++;
		so->so_onq = &head->so_q;
	}
	TAILQ_INSERT_TAIL(so->so_onq, so, so_qe);
}

int
soqremque(struct socket *so, int q)
{
	struct socket *head = so->so_head;

	soassertlocked(so);
	soassertlocked(head);

	if (q == 0) {
		if (so->so_onq != &head->so_q0)
			return (0);
		head->so_q0len--;
	} else {
		if (so->so_onq != &head->so_q)
			return (0);
		head->so_qlen--;
	}
	TAILQ_REMOVE(so->so_onq, so, so_qe);
	so->so_onq = NULL;
	so->so_head = NULL;
	return (1);
}

/*
 * Socantsendmore indicates that no more data will be sent on the
 * socket; it would normally be applied to a socket when the user
 * informs the system that no more data is to be sent, by the protocol
 * code (in case PRU_SHUTDOWN).  Socantrcvmore indicates that no more data
 * will be received, and will normally be applied to the socket by a
 * protocol when it detects that the peer will send no more data.
 * Data queued for reading in the socket may yet be read.
 */

void
socantsendmore(struct socket *so)
{
	soassertlocked(so);
	mtx_enter(&so->so_snd.sb_mtx);
	so->so_snd.sb_state |= SS_CANTSENDMORE;
	mtx_leave(&so->so_snd.sb_mtx);
	sowwakeup(so);
}

void
socantrcvmore(struct socket *so)
{
	mtx_enter(&so->so_rcv.sb_mtx);
	so->so_rcv.sb_state |= SS_CANTRCVMORE;
	mtx_leave(&so->so_rcv.sb_mtx);
	sorwakeup(so);
}

void
solock(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_LOCK();
		break;
	default:
		rw_enter_write(&so->so_lock);
		break;
	}
}

void
solock_shared(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_LOCK_SHARED();
		break;
	}
	rw_enter_write(&so->so_lock);
}

void
solock_nonet(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_ASSERT_LOCKED();
		break;
	}
	rw_enter_write(&so->so_lock);
}

int
solock_persocket(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		return 0;
	default:
		return 1;
	}
}

void
solock_pair(struct socket *so1, struct socket *so2)
{
	KASSERT(so1->so_type == so2->so_type);

	switch (so1->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_LOCK_SHARED();
		break;
	}
	if (so1 == so2) {
		rw_enter_write(&so1->so_lock);
	} else if (so1 < so2) {
		rw_enter_write(&so1->so_lock);
		rw_enter_write(&so2->so_lock);
	} else {
		rw_enter_write(&so2->so_lock);
		rw_enter_write(&so1->so_lock);
	}
}

void
sounlock(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_UNLOCK();
		break;
	default:
		rw_exit_write(&so->so_lock);
		break;
	}
}

void
sounlock_shared(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_UNLOCK_SHARED();
		break;
	}
	rw_exit_write(&so->so_lock);
}

void
sounlock_nonet(struct socket *so)
{
	rw_exit_write(&so->so_lock);
}

void
sounlock_pair(struct socket *so1, struct socket *so2)
{
	switch (so1->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_UNLOCK_SHARED();
		break;
	}
	if (so1 == so2)
		rw_exit_write(&so1->so_lock);
	else if (so1 < so2) {
		rw_exit_write(&so2->so_lock);
		rw_exit_write(&so1->so_lock);
	} else {
		rw_exit_write(&so1->so_lock);
		rw_exit_write(&so2->so_lock);
	}
}

void
soassertlocked_readonly(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		NET_ASSERT_LOCKED();
		break;
	default:
		rw_assert_wrlock(&so->so_lock);
		break;
	}
}

void
soassertlocked(struct socket *so)
{
	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		if (rw_status(&netlock) == RW_READ) {
			NET_ASSERT_LOCKED();

			if (splassert_ctl > 0 &&
			    rw_status(&so->so_lock) != RW_WRITE)
				splassert_fail(0, RW_WRITE, __func__);
		} else
			NET_ASSERT_LOCKED_EXCLUSIVE();
		break;
	default:
		rw_assert_wrlock(&so->so_lock);
		break;
	}
}

int
sosleep_nsec(struct socket *so, void *ident, int prio, const char *wmesg,
    uint64_t nsecs)
{
	int ret;

	switch (so->so_proto->pr_domain->dom_family) {
	case PF_INET:
	case PF_INET6:
		if (rw_status(&netlock) == RW_READ)
			rw_exit_write(&so->so_lock);
		ret = rwsleep_nsec(ident, &netlock, prio, wmesg, nsecs);
		if (rw_status(&netlock) == RW_READ)
			rw_enter_write(&so->so_lock);
		break;
	default:
		ret = rwsleep_nsec(ident, &so->so_lock, prio, wmesg, nsecs);
		break;
	}

	return ret;
}

void
sbmtxassertlocked(struct sockbuf *sb)
{
	MUTEX_ASSERT_LOCKED(&sb->sb_mtx);
}

/*
 * Wait for data to arrive at/drain from a socket buffer.
 */
int
sbwait(struct sockbuf *sb)
{
	int prio = (sb->sb_flags & SB_NOINTR) ? PSOCK : PSOCK | PCATCH;

	MUTEX_ASSERT_LOCKED(&sb->sb_mtx);

	sb->sb_flags |= SB_WAIT;
	return msleep_nsec(&sb->sb_cc, &sb->sb_mtx, prio, "sbwait",
	    sb->sb_timeo_nsecs);
}

int
sblock(struct sockbuf *sb, int flags)
{
	int rwflags = RW_WRITE, error;

	if (!(flags & SBL_NOINTR || sb->sb_flags & SB_NOINTR))
		rwflags |= RW_INTR;
	if (!(flags & SBL_WAIT))
		rwflags |= RW_NOSLEEP;

	error = rw_enter(&sb->sb_lock, rwflags);
	if (error == EBUSY)
		error = EWOULDBLOCK;

	return error;
}

void
sbunlock(struct sockbuf *sb)
{
	rw_exit(&sb->sb_lock);
}

/*
 * Wakeup processes waiting on a socket buffer.
 * Do asynchronous notification via SIGIO
 * if the socket buffer has the SB_ASYNC flag set.
 */
void
sowakeup(struct socket *so, struct sockbuf *sb)
{
	int dowakeup = 0, dopgsigio = 0;

	mtx_enter(&sb->sb_mtx);
	if (sb->sb_flags & SB_WAIT) {
		sb->sb_flags &= ~SB_WAIT;
		dowakeup = 1;
	}
	if (sb->sb_flags & SB_ASYNC)
		dopgsigio = 1;

	knote_locked(&sb->sb_klist, 0);
	mtx_leave(&sb->sb_mtx);

	if (dowakeup)
		wakeup(&sb->sb_cc);

	if (dopgsigio)
		pgsigio(&so->so_sigio, SIGIO, 0);
}

/*
 * Socket buffer (struct sockbuf) utility routines.
 *
 * Each socket contains two socket buffers: one for sending data and
 * one for receiving data.  Each buffer contains a queue of mbufs,
 * information about the number of mbufs and amount of data in the
 * queue, and other fields allowing select() statements and notification
 * on data availability to be implemented.
 *
 * Data stored in a socket buffer is maintained as a list of records.
 * Each record is a list of mbufs chained together with the m_next
 * field.  Records are chained together with the m_nextpkt field. The upper
 * level routine soreceive() expects the following conventions to be
 * observed when placing information in the receive buffer:
 *
 * 1. If the protocol requires each message be preceded by the sender's
 *    name, then a record containing that name must be present before
 *    any associated data (mbuf's must be of type MT_SONAME).
 * 2. If the protocol supports the exchange of ``access rights'' (really
 *    just additional data associated with the message), and there are
 *    ``rights'' to be received, then a record containing this data
 *    should be present (mbuf's must be of type MT_CONTROL).
 * 3. If a name or rights record exists, then it must be followed by
 *    a data record, perhaps of zero length.
 *
 * Before using a new socket structure it is first necessary to reserve
 * buffer space to the socket, by calling sbreserve().  This should commit
 * some of the available buffer space in the system buffer pool for the
 * socket (currently, it does nothing but enforce limits).  The space
 * should be released by calling sbrelease() when the socket is destroyed.
 */

int
soreserve(struct socket *so, u_long sndcc, u_long rcvcc)
{
	soassertlocked(so);

	mtx_enter(&so->so_rcv.sb_mtx);
	mtx_enter(&so->so_snd.sb_mtx);
	if (sbreserve(&so->so_snd, sndcc))
		goto bad;
	so->so_snd.sb_wat = sndcc;
	if (so->so_snd.sb_lowat == 0)
		so->so_snd.sb_lowat = MCLBYTES;
	if (so->so_snd.sb_lowat > so->so_snd.sb_hiwat)
		so->so_snd.sb_lowat = so->so_snd.sb_hiwat;
	if (sbreserve(&so->so_rcv, rcvcc))
		goto bad2;
	so->so_rcv.sb_wat = rcvcc;
	if (so->so_rcv.sb_lowat == 0)
		so->so_rcv.sb_lowat = 1;
	mtx_leave(&so->so_snd.sb_mtx);
	mtx_leave(&so->so_rcv.sb_mtx);

	return (0);
bad2:
	sbrelease(&so->so_snd);
bad:
	mtx_leave(&so->so_snd.sb_mtx);
	mtx_leave(&so->so_rcv.sb_mtx);
	return (ENOBUFS);
}

/*
 * Allot mbufs to a sockbuf.
 * Attempt to scale mbmax so that mbcnt doesn't become limiting
 * if buffering efficiency is near the normal case.
 */
int
sbreserve(struct sockbuf *sb, u_long cc)
{
	sbmtxassertlocked(sb);

	if (cc == 0 || cc > sb_max)
		return (1);
	sb->sb_hiwat = cc;
	sb->sb_mbmax = max(3 * MAXMCLBYTES, cc * 8);
	if (sb->sb_lowat > sb->sb_hiwat)
		sb->sb_lowat = sb->sb_hiwat;
	return (0);
}

/*
 * In low memory situation, do not accept any greater than normal request.
 */
int
sbcheckreserve(u_long cnt, u_long defcnt)
{
	if (cnt > defcnt && sbchecklowmem())
		return (ENOBUFS);
	return (0);
}

int
sbchecklowmem(void)
{
	static int sblowmem;
	unsigned int used;

	/*
	 * m_pool_used() is thread safe.  Global variable sblowmem is updated
	 * by multiple CPUs, but most times with the same value.  And even
	 * if the value is not correct for a short time, it does not matter.
	 */
	used = m_pool_used();
	if (used < 60)
		atomic_store_int(&sblowmem, 0);
	else if (used > 80)
		atomic_store_int(&sblowmem, 1);

	return (atomic_load_int(&sblowmem));
}

/*
 * Free mbufs held by a socket, and reserved mbuf space.
 */
void
sbrelease(struct sockbuf *sb)
{

	sbflush(sb);
	sb->sb_hiwat = sb->sb_mbmax = 0;
}

/*
 * Routines to add and remove
 * data from an mbuf queue.
 *
 * The routines sbappend() or sbappendrecord() are normally called to
 * append new mbufs to a socket buffer, after checking that adequate
 * space is available, comparing the function sbspace() with the amount
 * of data to be added.  sbappendrecord() differs from sbappend() in
 * that data supplied is treated as the beginning of a new record.
 * To place a sender's address, optional access rights, and data in a
 * socket receive buffer, sbappendaddr() should be used.  To place
 * access rights and data in a socket receive buffer, sbappendrights()
 * should be used.  In either case, the new data begins a new record.
 * Note that unlike sbappend() and sbappendrecord(), these routines check
 * for the caller that there will be enough space to store the data.
 * Each fails if there is not enough space, or if it cannot find mbufs
 * to store additional information in.
 *
 * Reliable protocols may use the socket send buffer to hold data
 * awaiting acknowledgement.  Data is normally copied from a socket
 * send buffer in a protocol with m_copym for output to a peer,
 * and then removing the data from the socket buffer with sbdrop()
 * or sbdroprecord() when the data is acknowledged by the peer.
 */

#ifdef SOCKBUF_DEBUG
void
sblastrecordchk(struct sockbuf *sb, const char *where)
{
	struct mbuf *m = sb->sb_mb;

	while (m && m->m_nextpkt)
		m = m->m_nextpkt;

	if (m != sb->sb_lastrecord) {
		printf("sblastrecordchk: sb_mb %p sb_lastrecord %p last %p\n",
		    sb->sb_mb, sb->sb_lastrecord, m);
		printf("packet chain:\n");
		for (m = sb->sb_mb; m != NULL; m = m->m_nextpkt)
			printf("\t%p\n", m);
		panic("sblastrecordchk from %s", where);
	}
}

void
sblastmbufchk(struct sockbuf *sb, const char *where)
{
	struct mbuf *m = sb->sb_mb;
	struct mbuf *n;

	while (m && m->m_nextpkt)
		m = m->m_nextpkt;

	while (m && m->m_next)
		m = m->m_next;

	if (m != sb->sb_mbtail) {
		printf("sblastmbufchk: sb_mb %p sb_mbtail %p last %p\n",
		    sb->sb_mb, sb->sb_mbtail, m);
		printf("packet tree:\n");
		for (m = sb->sb_mb; m != NULL; m = m->m_nextpkt) {
			printf("\t");
			for (n = m; n != NULL; n = n->m_next)
				printf("%p ", n);
			printf("\n");
		}
		panic("sblastmbufchk from %s", where);
	}
}
#endif /* SOCKBUF_DEBUG */

#define	SBLINKRECORD(sb, m0)						\
do {									\
	if ((sb)->sb_lastrecord != NULL)				\
		(sb)->sb_lastrecord->m_nextpkt = (m0);			\
	else								\
		(sb)->sb_mb = (m0);					\
	(sb)->sb_lastrecord = (m0);					\
} while (/*CONSTCOND*/0)

/*
 * Append mbuf chain m to the last record in the
 * socket buffer sb.  The additional space associated
 * the mbuf chain is recorded in sb.  Empty mbufs are
 * discarded and mbufs are compacted where possible.
 */
void
sbappend(struct sockbuf *sb, struct mbuf *m)
{
	struct mbuf *n;

	if (m == NULL)
		return;

	sbmtxassertlocked(sb);
	SBLASTRECORDCHK(sb, "sbappend 1");

	if ((n = sb->sb_lastrecord) != NULL) {
		/*
		 * XXX Would like to simply use sb_mbtail here, but
		 * XXX I need to verify that I won't miss an EOR that
		 * XXX way.
		 */
		do {
			if (n->m_flags & M_EOR) {
				sbappendrecord(sb, m); /* XXXXXX!!!! */
				return;
			}
		} while (n->m_next && (n = n->m_next));
	} else {
		/*
		 * If this is the first record in the socket buffer, it's
		 * also the last record.
		 */
		sb->sb_lastrecord = m;
	}
	sbcompress(sb, m, n);
	SBLASTRECORDCHK(sb, "sbappend 2");
}

/*
 * This version of sbappend() should only be used when the caller
 * absolutely knows that there will never be more than one record
 * in the socket buffer, that is, a stream protocol (such as TCP).
 */
void
sbappendstream(struct sockbuf *sb, struct mbuf *m)
{
	sbmtxassertlocked(sb);
	KDASSERT(m->m_nextpkt == NULL);
	KASSERT(sb->sb_mb == sb->sb_lastrecord);

	SBLASTMBUFCHK(sb, __func__);

	sbcompress(sb, m, sb->sb_mbtail);

	sb->sb_lastrecord = sb->sb_mb;
	SBLASTRECORDCHK(sb, __func__);
}

#ifdef SOCKBUF_DEBUG
void
sbcheck(struct socket *so, struct sockbuf *sb)
{
	struct mbuf *m, *n;
	u_long len = 0, mbcnt = 0;

	for (m = sb->sb_mb; m; m = m->m_nextpkt) {
		for (n = m; n; n = n->m_next) {
			len += n->m_len;
			mbcnt += MSIZE;
			if (n->m_flags & M_EXT)
				mbcnt += n->m_ext.ext_size;
			if (m != n && n->m_nextpkt)
				panic("sbcheck nextpkt");
		}
	}
	if (len != sb->sb_cc || mbcnt != sb->sb_mbcnt) {
		printf("cc %lu != %lu || mbcnt %lu != %lu\n", len, sb->sb_cc,
		    mbcnt, sb->sb_mbcnt);
		panic("sbcheck");
	}
}
#endif

/*
 * As above, except the mbuf chain
 * begins a new record.
 */
void
sbappendrecord(struct sockbuf *sb, struct mbuf *m0)
{
	struct mbuf *m;

	sbmtxassertlocked(sb);

	if (m0 == NULL)
		return;

	/*
	 * Put the first mbuf on the queue.
	 * Note this permits zero length records.
	 */
	sballoc(sb, m0);
	SBLASTRECORDCHK(sb, "sbappendrecord 1");
	SBLINKRECORD(sb, m0);
	m = m0->m_next;
	m0->m_next = NULL;
	if (m && (m0->m_flags & M_EOR)) {
		m0->m_flags &= ~M_EOR;
		m->m_flags |= M_EOR;
	}
	sbcompress(sb, m, m0);
	SBLASTRECORDCHK(sb, "sbappendrecord 2");
}

/*
 * Append address and data, and optionally, control (ancillary) data
 * to the receive queue of a socket.  If present,
 * m0 must include a packet header with total length.
 * Returns 0 if no space in sockbuf or insufficient mbufs.
 */
int
sbappendaddr(struct sockbuf *sb, const struct sockaddr *asa, struct mbuf *m0,
    struct mbuf *control)
{
	struct mbuf *m, *n, *nlast;
	int space = asa->sa_len;

	sbmtxassertlocked(sb);

	if (m0 && (m0->m_flags & M_PKTHDR) == 0)
		panic("sbappendaddr");
	if (m0)
		space += m0->m_pkthdr.len;
	for (n = control; n; n = n->m_next) {
		space += n->m_len;
		if (n->m_next == NULL)	/* keep pointer to last control buf */
			break;
	}
	if (space > sbspace_locked(sb))
		return (0);
	if (asa->sa_len > MLEN)
		return (0);
	MGET(m, M_DONTWAIT, MT_SONAME);
	if (m == NULL)
		return (0);
	m->m_len = asa->sa_len;
	memcpy(mtod(m, caddr_t), asa, asa->sa_len);
	if (n)
		n->m_next = m0;		/* concatenate data to control */
	else
		control = m0;
	m->m_next = control;

	SBLASTRECORDCHK(sb, "sbappendaddr 1");

	for (n = m; n->m_next != NULL; n = n->m_next)
		sballoc(sb, n);
	sballoc(sb, n);
	nlast = n;
	SBLINKRECORD(sb, m);

	sb->sb_mbtail = nlast;
	SBLASTMBUFCHK(sb, "sbappendaddr");

	SBLASTRECORDCHK(sb, "sbappendaddr 2");

	return (1);
}

int
sbappendcontrol(struct sockbuf *sb, struct mbuf *m0, struct mbuf *control)
{
	struct mbuf *m, *mlast, *n;
	int eor = 0, space = 0;

	sbmtxassertlocked(sb);

	if (control == NULL)
		panic("sbappendcontrol");
	for (m = control; ; m = m->m_next) {
		space += m->m_len;
		if (m->m_next == NULL)
			break;
	}
	n = m;			/* save pointer to last control buffer */
	for (m = m0; m; m = m->m_next) {
		space += m->m_len;
		eor |= m->m_flags & M_EOR;
		if (eor) {
			if (m->m_next == NULL)
				m->m_flags |= M_EOR;
			else
				m->m_flags &= ~M_EOR;
		}
	}
	if (space > sbspace_locked(sb))
		return (0);
	n->m_next = m0;			/* concatenate data to control */

	SBLASTRECORDCHK(sb, "sbappendcontrol 1");

	for (m = control; m->m_next != NULL; m = m->m_next)
		sballoc(sb, m);
	sballoc(sb, m);
	mlast = m;
	SBLINKRECORD(sb, control);

	sb->sb_mbtail = mlast;
	SBLASTMBUFCHK(sb, "sbappendcontrol");

	SBLASTRECORDCHK(sb, "sbappendcontrol 2");

	return (1);
}

/*
 * Compress mbuf chain m into the socket
 * buffer sb following mbuf n.  If n
 * is null, the buffer is presumed empty.
 */
void
sbcompress(struct sockbuf *sb, struct mbuf *m, struct mbuf *n)
{
	int eor = 0;
	struct mbuf *o;

	while (m) {
		eor |= m->m_flags & M_EOR;
		if (m->m_len == 0 &&
		    (eor == 0 ||
		    (((o = m->m_next) || (o = n)) &&
		    o->m_type == m->m_type))) {
			if (sb->sb_lastrecord == m)
				sb->sb_lastrecord = m->m_next;
			m = m_free(m);
			continue;
		}
		if (n && (n->m_flags & M_EOR) == 0 &&
		    /* m_trailingspace() checks buffer writeability */
		    m->m_len <= ((n->m_flags & M_EXT)? n->m_ext.ext_size :
		       MCLBYTES) / 4 && /* XXX Don't copy too much */
		    m->m_len <= m_trailingspace(n) &&
		    n->m_type == m->m_type) {
			memcpy(mtod(n, caddr_t) + n->m_len, mtod(m, caddr_t),
			    m->m_len);
			n->m_len += m->m_len;
			sb->sb_cc += m->m_len;
			if (m->m_type != MT_CONTROL && m->m_type != MT_SONAME)
				sb->sb_datacc += m->m_len;
			m = m_free(m);
			continue;
		}
		if (n)
			n->m_next = m;
		else
			sb->sb_mb = m;
		sb->sb_mbtail = m;
		sballoc(sb, m);
		n = m;
		m->m_flags &= ~M_EOR;
		m = m->m_next;
		n->m_next = NULL;
	}
	if (eor) {
		if (n)
			n->m_flags |= eor;
		else
			printf("semi-panic: sbcompress");
	}
	SBLASTMBUFCHK(sb, __func__);
}

/*
 * Free all mbufs in a sockbuf.
 * Check that all resources are reclaimed.
 */
void
sbflush(struct sockbuf *sb)
{
	rw_assert_unlocked(&sb->sb_lock);

	while (sb->sb_mbcnt)
		sbdrop(sb, (int)sb->sb_cc);

	KASSERT(sb->sb_cc == 0);
	KASSERT(sb->sb_datacc == 0);
	KASSERT(sb->sb_mb == NULL);
	KASSERT(sb->sb_mbtail == NULL);
	KASSERT(sb->sb_lastrecord == NULL);
}

/*
 * Drop data from (the front of) a sockbuf.
 */
void
sbdrop(struct sockbuf *sb, int len)
{
	struct mbuf *m, *mn;
	struct mbuf *next;

	sbmtxassertlocked(sb);

	next = (m = sb->sb_mb) ? m->m_nextpkt : NULL;
	while (len > 0) {
		if (m == NULL) {
			if (next == NULL)
				panic("sbdrop");
			m = next;
			next = m->m_nextpkt;
			continue;
		}
		if (m->m_len > len) {
			m->m_len -= len;
			m->m_data += len;
			sb->sb_cc -= len;
			if (m->m_type != MT_CONTROL && m->m_type != MT_SONAME)
				sb->sb_datacc -= len;
			break;
		}
		len -= m->m_len;
		sbfree(sb, m);
		mn = m_free(m);
		m = mn;
	}
	while (m && m->m_len == 0) {
		sbfree(sb, m);
		mn = m_free(m);
		m = mn;
	}
	if (m) {
		sb->sb_mb = m;
		m->m_nextpkt = next;
	} else
		sb->sb_mb = next;
	/*
	 * First part is an inline SB_EMPTY_FIXUP().  Second part
	 * makes sure sb_lastrecord is up-to-date if we dropped
	 * part of the last record.
	 */
	m = sb->sb_mb;
	if (m == NULL) {
		sb->sb_mbtail = NULL;
		sb->sb_lastrecord = NULL;
	} else if (m->m_nextpkt == NULL)
		sb->sb_lastrecord = m;
}

/*
 * Drop a record off the front of a sockbuf
 * and move the next record to the front.
 */
void
sbdroprecord(struct sockbuf *sb)
{
	struct mbuf *m, *mn;

	m = sb->sb_mb;
	if (m) {
		sb->sb_mb = m->m_nextpkt;
		do {
			sbfree(sb, m);
			mn = m_free(m);
		} while ((m = mn) != NULL);
	}
	SB_EMPTY_FIXUP(sb);
}

/*
 * Create a "control" mbuf containing the specified data
 * with the specified type for presentation on a socket buffer.
 */
struct mbuf *
sbcreatecontrol(const void *p, size_t size, int type, int level)
{
	struct cmsghdr *cp;
	struct mbuf *m;

	if (CMSG_SPACE(size) > MCLBYTES) {
		printf("sbcreatecontrol: message too large %zu\n", size);
		return (NULL);
	}

	if ((m = m_get(M_DONTWAIT, MT_CONTROL)) == NULL)
		return (NULL);
	if (CMSG_SPACE(size) > MLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			return NULL;
		}
	}
	cp = mtod(m, struct cmsghdr *);
	memset(cp, 0, CMSG_SPACE(size));
	memcpy(CMSG_DATA(cp), p, size);
	m->m_len = CMSG_SPACE(size);
	cp->cmsg_len = CMSG_LEN(size);
	cp->cmsg_level = level;
	cp->cmsg_type = type;
	return (m);
}
