/*	$OpenBSD: rtsock.c,v 1.388 2025/08/21 08:49:21 mvs Exp $	*/
/*	$NetBSD: rtsock.c,v 1.18 1996/03/29 00:32:10 cgd Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1988, 1991, 1993
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
 *	@(#)rtsock.c	8.6 (Berkeley) 2/11/95
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/pool.h>
#include <sys/protosw.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/route.h>

#include <netinet/in.h>

#ifdef MPLS
#include <netmpls/mpls.h>
#endif
#ifdef IPSEC
#include <net/if_enc.h>
#endif
#ifdef BFD
#include <net/bfd.h>
#endif

#include <sys/stdarg.h>
#include <sys/timeout.h>

#define	ROUTESNDQ	8192
#define	ROUTERCVQ	8192

const struct sockaddr route_src = { 2, PF_ROUTE, };

struct walkarg {
	int	w_op, w_arg, w_tmemsize;
	size_t	w_given, w_needed;
	caddr_t	w_where, w_tmem;
};

void	route_prinit(void);
int	route_output(struct mbuf *, struct socket *);
int	route_ctloutput(int, struct socket *, int, int, struct mbuf *);
int	route_attach(struct socket *, int, int);
int	route_detach(struct socket *);
int	route_disconnect(struct socket *);
int	route_shutdown(struct socket *);
void	route_rcvd(struct socket *);
int	route_send(struct socket *, struct mbuf *, struct mbuf *,
	    struct mbuf *);
int	route_sockaddr(struct socket *, struct mbuf *);
int	route_peeraddr(struct socket *, struct mbuf *);
void	route_input(struct mbuf *m0, struct socket *, sa_family_t);
int	route_arp_conflict(struct rtentry *, struct rt_addrinfo *);
int	route_cleargateway(struct rtentry *, void *, unsigned int);
void	rtm_senddesync_timer(void *);
void	rtm_senddesync(struct socket *);
int	rtm_sendup(struct socket *, struct mbuf *);

int	rtm_getifa(struct rt_addrinfo *, unsigned int);
int	rtm_output(struct rt_msghdr *, struct rtentry **, struct rt_addrinfo *,
	    uint8_t, unsigned int);
struct rt_msghdr *rtm_report(struct rtentry *, u_char, int, int);
struct mbuf	*rtm_msg1(int, struct rt_addrinfo *);
int		 rtm_msg2(int, int, struct rt_addrinfo *, caddr_t,
		     struct walkarg *);
int		 rtm_xaddrs(caddr_t, caddr_t, struct rt_addrinfo *);
int		 rtm_validate_proposal(struct rt_addrinfo *);
void		 rtm_setmetrics(u_long, const struct rt_metrics *,
		     struct rt_kmetrics *);
void		 rtm_getmetrics(const struct rtentry *,
		     struct rt_metrics *);

int		 sysctl_iflist(int, struct walkarg *);
int		 sysctl_ifnames(struct walkarg *);
int		 sysctl_rtable_rtstat(void *, size_t *, void *);

int		 rt_setsource(unsigned int, const struct sockaddr *);

/*
 * Locks used to protect struct members
 *       I       immutable after creation
 *       s       solock
 *
 * Lock order: rtptable.rtp_lk -> solock
 */
struct rtpcb {
	struct socket		*rop_socket;		/* [I] */

	TAILQ_ENTRY(rtpcb)	rop_list;
	struct timeout		rop_timeout;
	unsigned int		rop_msgfilter;		/* [s] */
	unsigned int		rop_flagfilter;		/* [s] */
	unsigned int		rop_flags;		/* [s] */
	u_int			rop_rtableid;		/* [s] */
	unsigned short		rop_proto;		/* [I] */
	u_char			rop_priority;		/* [s] */
};
#define	sotortpcb(so)	((struct rtpcb *)(so)->so_pcb)

struct rtptable {
	TAILQ_HEAD(, rtpcb)	rtp_list;
	struct rwlock		rtp_lk;
	unsigned int		rtp_count;
};

struct pool rtpcb_pool;
struct rtptable rtptable;

/*
 * These flags and timeout are used for indicating to userland (via a
 * RTM_DESYNC msg) when the route socket has overflowed and messages
 * have been lost.
 */
#define ROUTECB_FLAG_DESYNC	0x1	/* Route socket out of memory */
#define ROUTECB_FLAG_FLUSH	0x2	/* Wait until socket is empty before
					   queueing more packets */

#define ROUTE_DESYNC_RESEND_TIMEOUT	200	/* In ms */

void
route_prinit(void)
{
	rw_init(&rtptable.rtp_lk, "rtsock");
	TAILQ_INIT(&rtptable.rtp_list);
	pool_init(&rtpcb_pool, sizeof(struct rtpcb), 0,
	    IPL_SOFTNET, PR_WAITOK, "rtpcb", NULL);
}

int
route_attach(struct socket *so, int proto, int wait)
{
	struct rtpcb	*rop;
	int		 error;

	soassertlocked(so);

	error = soreserve(so, ROUTESNDQ, ROUTERCVQ);
	if (error)
		return (error);
	/*
	 * use the rawcb but allocate a rtpcb, this
	 * code does not care about the additional fields
	 * and works directly on the raw socket.
	 */
	rop = pool_get(&rtpcb_pool, (wait == M_WAIT ? PR_WAITOK : PR_NOWAIT) |
	    PR_ZERO);
	if (rop == NULL)
		return (ENOBUFS);
	so->so_pcb = rop;
	/* Init the timeout structure */
	timeout_set_flags(&rop->rop_timeout, rtm_senddesync_timer, so,
	    KCLOCK_NONE, TIMEOUT_PROC | TIMEOUT_MPSAFE);

	rop->rop_socket = so;
	rop->rop_proto = proto;

	rop->rop_rtableid = curproc->p_p->ps_rtableid;

	soisconnected(so);
	so->so_options |= SO_USELOOPBACK;

	/* Give up solock before taking rtp_lk for the lock ordering. */
	sounlock(so);

	rw_enter(&rtptable.rtp_lk, RW_WRITE);
	TAILQ_INSERT_TAIL(&rtptable.rtp_list, rop, rop_list);
	rtptable.rtp_count++;
	rw_exit(&rtptable.rtp_lk);

	solock(so);

	return (0);
}

int
route_detach(struct socket *so)
{
	struct rtpcb	*rop;

	soassertlocked(so);

	rop = sotortpcb(so);
	if (rop == NULL)
		return (EINVAL);

	/* Give up solock before taking rtp_lk for the lock ordering. */
	sounlock(so);

	rw_enter(&rtptable.rtp_lk, RW_WRITE);
	rtptable.rtp_count--;
	TAILQ_REMOVE(&rtptable.rtp_list, rop, rop_list);
	rw_exit(&rtptable.rtp_lk);

	/* wait for all references to drop */
	timeout_del_barrier(&rop->rop_timeout);

	solock(so);

	so->so_pcb = NULL;
	KASSERT((so->so_state & SS_NOFDREF) == 0);
	pool_put(&rtpcb_pool, rop);

	return (0);
}

int
route_disconnect(struct socket *so)
{
	soisdisconnected(so);
	return (0);
}

int
route_shutdown(struct socket *so)
{
	socantsendmore(so);
	return (0);
}

void
route_rcvd(struct socket *so)
{
	struct rtpcb *rop = sotortpcb(so);

	soassertlocked(so);

	/*
	 * If we are in a FLUSH state, check if the buffer is
	 * empty so that we can clear the flag.
	 */

	mtx_enter(&so->so_rcv.sb_mtx);
	if (((rop->rop_flags & ROUTECB_FLAG_FLUSH) != 0) &&
	    ((sbspace_locked(&so->so_rcv) == so->so_rcv.sb_hiwat)))
		rop->rop_flags &= ~ROUTECB_FLAG_FLUSH;
	mtx_leave(&so->so_rcv.sb_mtx);
}

int
route_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	int error;

	soassertlocked(so);

	if (control && control->m_len) {
		error = EOPNOTSUPP;
		goto out;
	}

	if (nam) {
		error = EISCONN;
		goto out;
	}

	error = route_output(m, so);
	m = NULL;

out:
	m_freem(control);
	m_freem(m);

	return (error);
}

int
route_sockaddr(struct socket *so, struct mbuf *nam)
{
	return (EINVAL);
}

int
route_peeraddr(struct socket *so, struct mbuf *nam)
{
	/* minimal support, just implement a fake peer address */
	memcpy(mtod(nam, caddr_t), &route_src, route_src.sa_len);
	nam->m_len = route_src.sa_len;
	return (0);
}

int
route_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
	struct rtpcb *rop = sotortpcb(so);
	int error = 0;
	unsigned int tid, prio;

	if (level != AF_ROUTE)
		return (EINVAL);

	switch (op) {
	case PRCO_SETOPT:
		switch (optname) {
		case ROUTE_MSGFILTER:
			if (m == NULL || m->m_len != sizeof(unsigned int))
				error = EINVAL;
			else
				rop->rop_msgfilter = *mtod(m, unsigned int *);
			break;
		case ROUTE_TABLEFILTER:
			if (m == NULL || m->m_len != sizeof(unsigned int)) {
				error = EINVAL;
				break;
			}
			tid = *mtod(m, unsigned int *);
			if (tid != RTABLE_ANY && !rtable_exists(tid))
				error = ENOENT;
			else
				rop->rop_rtableid = tid;
			break;
		case ROUTE_PRIOFILTER:
			if (m == NULL || m->m_len != sizeof(unsigned int)) {
				error = EINVAL;
				break;
			}
			prio = *mtod(m, unsigned int *);
			if (prio > RTP_MAX)
				error = EINVAL;
			else
				rop->rop_priority = prio;
			break;
		case ROUTE_FLAGFILTER:
			if (m == NULL || m->m_len != sizeof(unsigned int))
				error = EINVAL;
			else
				rop->rop_flagfilter = *mtod(m, unsigned int *);
			break;
		default:
			error = ENOPROTOOPT;
			break;
		}
		break;
	case PRCO_GETOPT:
		switch (optname) {
		case ROUTE_MSGFILTER:
			m->m_len = sizeof(unsigned int);
			*mtod(m, unsigned int *) = rop->rop_msgfilter;
			break;
		case ROUTE_TABLEFILTER:
			m->m_len = sizeof(unsigned int);
			*mtod(m, unsigned int *) = rop->rop_rtableid;
			break;
		case ROUTE_PRIOFILTER:
			m->m_len = sizeof(unsigned int);
			*mtod(m, unsigned int *) = rop->rop_priority;
			break;
		case ROUTE_FLAGFILTER:
			m->m_len = sizeof(unsigned int);
			*mtod(m, unsigned int *) = rop->rop_flagfilter;
			break;
		default:
			error = ENOPROTOOPT;
			break;
		}
	}
	return (error);
}

void
rtm_senddesync_timer(void *xso)
{
	struct socket	*so = xso;

	solock(so);
	rtm_senddesync(so);
	sounlock(so);
}

void
rtm_senddesync(struct socket *so)
{
	struct rtpcb	*rop = sotortpcb(so);
	struct mbuf	*desync_mbuf;

	soassertlocked(so);

	/*
	 * Dying socket is disconnected by upper layer and there is
	 * no reason to send packet. Also we shouldn't reschedule
	 * timeout(9), otherwise timeout_del_barrier(9) can't help us.
	 */
	if ((so->so_state & SS_ISCONNECTED) == 0 ||
	    (so->so_rcv.sb_state & SS_CANTRCVMORE))
		return;

	/* If we are in a DESYNC state, try to send a RTM_DESYNC packet */
	if ((rop->rop_flags & ROUTECB_FLAG_DESYNC) == 0)
		return;

	/*
	 * If we fail to alloc memory or if sbappendaddr()
	 * fails, re-add timeout and try again.
	 */
	desync_mbuf = rtm_msg1(RTM_DESYNC, NULL);
	if (desync_mbuf != NULL) {
		int ret;

		mtx_enter(&so->so_rcv.sb_mtx);
		ret = sbappendaddr(&so->so_rcv, &route_src, desync_mbuf, NULL);
		mtx_leave(&so->so_rcv.sb_mtx);

		if (ret != 0) {
			rop->rop_flags &= ~ROUTECB_FLAG_DESYNC;
			sorwakeup(rop->rop_socket);
			return;
		}
		m_freem(desync_mbuf);
	}
	/* Re-add timeout to try sending msg again */
	timeout_add_msec(&rop->rop_timeout, ROUTE_DESYNC_RESEND_TIMEOUT);
}

void
route_input(struct mbuf *m0, struct socket *so0, sa_family_t sa_family)
{
	struct socket *so;
	struct rtpcb *rop;
	struct rt_msghdr *rtm;
	struct mbuf *m = m0;

	/* ensure that we can access the rtm_type via mtod() */
	if (m->m_len < offsetof(struct rt_msghdr, rtm_type) + 1) {
		m_freem(m);
		return;
	}

	rw_enter_read(&rtptable.rtp_lk);
	TAILQ_FOREACH(rop, &rtptable.rtp_list, rop_list) {
		/*
		 * If route socket is bound to an address family only send
		 * messages that match the address family. Address family
		 * agnostic messages are always sent.
		 */
		if (sa_family != AF_UNSPEC && rop->rop_proto != AF_UNSPEC &&
		    rop->rop_proto != sa_family)
			continue;


		so = rop->rop_socket;
		solock(so);

		/*
		 * Check to see if we don't want our own messages and
		 * if we can receive anything.
		 */
		if ((so0 == so && !(so0->so_options & SO_USELOOPBACK)) ||
		    !(so->so_state & SS_ISCONNECTED) ||
		    (so->so_rcv.sb_state & SS_CANTRCVMORE))
			goto next;

		/* filter messages that the process does not want */
		rtm = mtod(m, struct rt_msghdr *);
		/* but RTM_DESYNC can't be filtered */
		if (rtm->rtm_type != RTM_DESYNC) {
			if (rop->rop_msgfilter != 0 &&
			    !(rop->rop_msgfilter & (1U << rtm->rtm_type)))
				goto next;
			if (ISSET(rop->rop_flagfilter, rtm->rtm_flags))
				goto next;
		}
		switch (rtm->rtm_type) {
		case RTM_IFANNOUNCE:
		case RTM_DESYNC:
			/* no tableid */
			break;
		case RTM_RESOLVE:
		case RTM_NEWADDR:
		case RTM_DELADDR:
		case RTM_IFINFO:
		case RTM_80211INFO:
		case RTM_BFD:
			/* check against rdomain id */
			if (rop->rop_rtableid != RTABLE_ANY &&
			    rtable_l2(rop->rop_rtableid) != rtm->rtm_tableid)
				goto next;
			break;
		default:
			if (rop->rop_priority != 0 &&
			    rop->rop_priority < rtm->rtm_priority)
				goto next;
			/* check against rtable id */
			if (rop->rop_rtableid != RTABLE_ANY &&
			    rop->rop_rtableid != rtm->rtm_tableid)
				goto next;
			break;
		}

		/*
		 * Check to see if the flush flag is set. If so, don't queue
		 * any more messages until the flag is cleared.
		 */
		if ((rop->rop_flags & ROUTECB_FLAG_FLUSH) != 0)
			goto next;

		rtm_sendup(so, m);
next:
		sounlock(so);
	}
	rw_exit_read(&rtptable.rtp_lk);

	m_freem(m);
}

int
rtm_sendup(struct socket *so, struct mbuf *m0)
{
	struct rtpcb *rop = sotortpcb(so);
	struct mbuf *m;
	int send_desync = 0;

	soassertlocked(so);

	m = m_copym(m0, 0, M_COPYALL, M_NOWAIT);
	if (m == NULL)
		return (ENOMEM);

	mtx_enter(&so->so_rcv.sb_mtx);
	if (sbspace_locked(&so->so_rcv) < (2 * MSIZE) ||
	    sbappendaddr(&so->so_rcv, &route_src, m, NULL) == 0)
		send_desync = 1;
	mtx_leave(&so->so_rcv.sb_mtx);

	if (send_desync) {
		/* Flag socket as desync'ed and flush required */
		rop->rop_flags |= ROUTECB_FLAG_DESYNC | ROUTECB_FLAG_FLUSH;
		rtm_senddesync(so);
		m_freem(m);
		return (ENOBUFS);
	}

	sorwakeup(so);
	return (0);
}

struct rt_msghdr *
rtm_report(struct rtentry *rt, u_char type, int seq, int tableid)
{
	struct rt_msghdr	*rtm;
	struct rt_addrinfo	 info;
	struct sockaddr_rtlabel	 sa_rl;
	struct sockaddr_in6	 sa_mask;
#ifdef BFD
	struct sockaddr_bfd	 sa_bfd;
#endif
#ifdef MPLS
	struct sockaddr_mpls	 sa_mpls;
#endif
	struct ifnet		*ifp = NULL;
	int			 len;

	bzero(&info, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(rt);
	info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	info.rti_info[RTAX_NETMASK] = rt_plen2mask(rt, &sa_mask);
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(rt->rt_labelid, &sa_rl);
#ifdef BFD
	if (rt->rt_flags & RTF_BFD) {
		KERNEL_LOCK();
		info.rti_info[RTAX_BFD] = bfd2sa(rt, &sa_bfd);
		KERNEL_UNLOCK();
	}
#endif
#ifdef MPLS
	if (rt->rt_flags & RTF_MPLS) {
		bzero(&sa_mpls, sizeof(sa_mpls));
		sa_mpls.smpls_family = AF_MPLS;
		sa_mpls.smpls_len = sizeof(sa_mpls);
		sa_mpls.smpls_label = ((struct rt_mpls *)
		    rt->rt_llinfo)->mpls_label;
		info.rti_info[RTAX_SRC] = (struct sockaddr *)&sa_mpls;
		info.rti_mpls = ((struct rt_mpls *)
		    rt->rt_llinfo)->mpls_operation;
	}
#endif
	ifp = if_get(rt->rt_ifidx);
	if (ifp != NULL) {
		info.rti_info[RTAX_IFP] = sdltosa(ifp->if_sadl);
		info.rti_info[RTAX_IFA] = rtable_getsource(tableid,
		    info.rti_info[RTAX_DST]->sa_family);
		if (info.rti_info[RTAX_IFA] == NULL)
			info.rti_info[RTAX_IFA] = rt->rt_ifa->ifa_addr;
		if (ifp->if_flags & IFF_POINTOPOINT)
			info.rti_info[RTAX_BRD] = rt->rt_ifa->ifa_dstaddr;
	}
	if_put(ifp);
	/* RTAX_GENMASK, RTAX_AUTHOR, RTAX_SRCMASK ignored */

	/* build new route message */
	len = rtm_msg2(type, RTM_VERSION, &info, NULL, NULL);
	rtm = malloc(len, M_RTABLE, M_WAITOK | M_ZERO);

	rtm_msg2(type, RTM_VERSION, &info, (caddr_t)rtm, NULL);
	rtm->rtm_type = type;
	rtm->rtm_index = rt->rt_ifidx;
	rtm->rtm_tableid = tableid;
	rtm->rtm_priority = rt->rt_priority & RTP_MASK;
	rtm->rtm_flags = rt->rt_flags;
	rtm->rtm_pid = curproc->p_p->ps_pid;
	rtm->rtm_seq = seq;
	rtm_getmetrics(rt, &rtm->rtm_rmx);
	rtm->rtm_addrs = info.rti_addrs;
#ifdef MPLS
	rtm->rtm_mpls = info.rti_mpls;
#endif
	return rtm;
}

int
route_output(struct mbuf *m, struct socket *so)
{
	struct rt_msghdr	*rtm = NULL;
	struct rtentry		*rt = NULL;
	struct rt_addrinfo	 info;
	struct ifnet		*ifp;
	int			 len, seq, useloopback, error = 0;
	u_int			 tableid;
	u_int8_t		 prio;
	u_char			 vers, type;

	if (m == NULL || ((m->m_len < sizeof(int32_t)) &&
	    (m = m_pullup(m, sizeof(int32_t))) == NULL))
		return (ENOBUFS);
	if ((m->m_flags & M_PKTHDR) == 0)
		panic("route_output");

	useloopback = so->so_options & SO_USELOOPBACK;

	/*
	 * The socket can't be closed concurrently because the file
	 * descriptor reference is still held.
	 */

	sounlock(so);

	len = m->m_pkthdr.len;
	if (len < offsetof(struct rt_msghdr, rtm_hdrlen) +
	    sizeof(rtm->rtm_hdrlen) ||
	    len != mtod(m, struct rt_msghdr *)->rtm_msglen) {
		error = EINVAL;
		goto fail;
	}
	vers = mtod(m, struct rt_msghdr *)->rtm_version;
	switch (vers) {
	case RTM_VERSION:
		if (len < sizeof(struct rt_msghdr)) {
			error = EINVAL;
			goto fail;
		}
		if (len > RTM_MAXSIZE) {
			error = EMSGSIZE;
			goto fail;
		}
		rtm = malloc(len, M_RTABLE, M_WAITOK);
		m_copydata(m, 0, len, rtm);
		break;
	default:
		error = EPROTONOSUPPORT;
		goto fail;
	}

	/* Verify that the caller is sending an appropriate message early */
	switch (rtm->rtm_type) {
	case RTM_ADD:
	case RTM_DELETE:
	case RTM_GET:
	case RTM_CHANGE:
	case RTM_PROPOSAL:
	case RTM_SOURCE:
		break;
	default:
		error = EOPNOTSUPP;
		goto fail;
	}
	/*
	 * Verify that the header length is valid.
	 * All messages from userland start with a struct rt_msghdr.
	 */
	if (rtm->rtm_hdrlen == 0)	/* old client */
		rtm->rtm_hdrlen = sizeof(struct rt_msghdr);
	if (rtm->rtm_hdrlen < sizeof(struct rt_msghdr) ||
	    len < rtm->rtm_hdrlen) {
		error = EINVAL;
		goto fail;
	}

	rtm->rtm_pid = curproc->p_p->ps_pid;

	/*
	 * Verify that the caller has the appropriate privilege; RTM_GET
	 * is the only operation the non-superuser is allowed.
	 */
	if (rtm->rtm_type != RTM_GET && suser(curproc) != 0) {
		error = EACCES;
		goto fail;
	}
	tableid = rtm->rtm_tableid;
	if (!rtable_exists(tableid)) {
		if (rtm->rtm_type == RTM_ADD) {
			if ((error = rtable_add(tableid)) != 0)
				goto fail;
		} else {
			error = EINVAL;
			goto fail;
		}
	}

	/* Do not let userland play with kernel-only flags. */
	if ((rtm->rtm_flags & (RTF_LOCAL|RTF_BROADCAST)) != 0) {
		error = EINVAL;
		goto fail;
	}

	/* make sure that kernel-only bits are not set */
	rtm->rtm_priority &= RTP_MASK;
	rtm->rtm_flags &= ~(RTF_DONE|RTF_CLONED|RTF_CACHED);
	rtm->rtm_fmask &= RTF_FMASK;

	if (rtm->rtm_priority != 0) {
		if (rtm->rtm_priority > RTP_MAX ||
		    rtm->rtm_priority == RTP_LOCAL) {
			error = EINVAL;
			goto fail;
		}
		prio = rtm->rtm_priority;
	} else if (rtm->rtm_type != RTM_ADD)
		prio = RTP_ANY;
	else if (rtm->rtm_flags & RTF_STATIC)
		prio = 0;
	else
		prio = RTP_DEFAULT;

	bzero(&info, sizeof(info));
	info.rti_addrs = rtm->rtm_addrs;
	if ((error = rtm_xaddrs(rtm->rtm_hdrlen + (caddr_t)rtm,
	    len + (caddr_t)rtm, &info)) != 0)
		goto fail;

	info.rti_flags = rtm->rtm_flags;

	if (rtm->rtm_type != RTM_SOURCE &&
	    rtm->rtm_type != RTM_PROPOSAL &&
	    (info.rti_info[RTAX_DST] == NULL ||
	    info.rti_info[RTAX_DST]->sa_family >= AF_MAX ||
	    (info.rti_info[RTAX_GATEWAY] != NULL &&
	    info.rti_info[RTAX_GATEWAY]->sa_family >= AF_MAX) ||
	    info.rti_info[RTAX_GENMASK] != NULL)) {
		error = EINVAL;
		goto fail;
	}
#ifdef MPLS
	info.rti_mpls = rtm->rtm_mpls;
#endif

	if (info.rti_info[RTAX_GATEWAY] != NULL &&
	    info.rti_info[RTAX_GATEWAY]->sa_family == AF_LINK &&
	    (info.rti_flags & RTF_CLONING) == 0) {
		info.rti_flags |= RTF_LLINFO;
	}

	/*
	 * Validate RTM_PROPOSAL and pass it along or error out.
	 */
	if (rtm->rtm_type == RTM_PROPOSAL) {
		if (rtm_validate_proposal(&info) == -1) {
			error = EINVAL;
			goto fail;
		}
		/*
		 * If this is a solicitation proposal forward request to
		 * all interfaces. Most handlers will ignore it but at least
		 * umb(4) will send a response to this event.
		 */
		if (rtm->rtm_priority == RTP_PROPOSAL_SOLICIT) {
			NET_LOCK();
			TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
				ifp->if_rtrequest(ifp, RTM_PROPOSAL, NULL);
			}
			NET_UNLOCK();
		}
	} else if (rtm->rtm_type == RTM_SOURCE) {
		if (info.rti_info[RTAX_IFA] == NULL) {
			error = EINVAL;
			goto fail;
		}
		NET_LOCK();
		error = rt_setsource(tableid, info.rti_info[RTAX_IFA]);
		NET_UNLOCK();
		if (error)
			goto fail;
	} else {
		error = rtm_output(rtm, &rt, &info, prio, tableid);
		if (!error) {
			type = rtm->rtm_type;
			seq = rtm->rtm_seq;
			free(rtm, M_RTABLE, len);
			NET_LOCK_SHARED();
			rtm = rtm_report(rt, type, seq, tableid);
			NET_UNLOCK_SHARED();
			len = rtm->rtm_msglen;
		}
	}

	rtfree(rt);
	if (error) {
		rtm->rtm_errno = error;
	} else {
		rtm->rtm_flags |= RTF_DONE;
	}

	/*
	 * Check to see if we don't want our own messages.
	 */
	if (!useloopback) {
		if (rtptable.rtp_count == 0) {
			/* no other listener and no loopback of messages */
			goto fail;
		}
	}
	if (m_copyback(m, 0, len, rtm, M_NOWAIT)) {
		m_freem(m);
		m = NULL;
	} else if (m->m_pkthdr.len > len)
		m_adj(m, len - m->m_pkthdr.len);
	free(rtm, M_RTABLE, len);
	if (m)
		route_input(m, so, info.rti_info[RTAX_DST] ?
		    info.rti_info[RTAX_DST]->sa_family : AF_UNSPEC);
	solock(so);

	return (error);
fail:
	free(rtm, M_RTABLE, len);
	m_freem(m);
	solock(so);

	return (error);
}

int
rtm_output(struct rt_msghdr *rtm, struct rtentry **prt,
    struct rt_addrinfo *info, uint8_t prio, unsigned int tableid)
{
	struct rtentry		*rt = *prt;
	struct ifnet		*ifp = NULL;
	int			 plen, newgate = 0, error = 0;

	switch (rtm->rtm_type) {
	case RTM_ADD:
		if (info->rti_info[RTAX_GATEWAY] == NULL) {
			error = EINVAL;
			break;
		}

		rt = rtable_match(tableid, info->rti_info[RTAX_DST], NULL);
		if ((error = route_arp_conflict(rt, info))) {
			rtfree(rt);
			rt = NULL;
			break;
		}

		/*
		 * We cannot go through a delete/create/insert cycle for
		 * cached route because this can lead to races in the
		 * receive path.  Instead we update the L2 cache.
		 */
		if ((rt != NULL) && ISSET(rt->rt_flags, RTF_CACHED)) {
			ifp = if_get(rt->rt_ifidx);
			if (ifp == NULL) {
				rtfree(rt);
				rt = NULL;
				error = ESRCH;
				break;
			}

			goto change;
		}

		rtfree(rt);
		rt = NULL;

		NET_LOCK();
		if ((error = rtm_getifa(info, tableid)) != 0) {
			NET_UNLOCK();
			break;
		}
		error = rtrequest(RTM_ADD, info, prio, &rt, tableid);
		NET_UNLOCK();
		if (error == 0)
			rtm_setmetrics(rtm->rtm_inits, &rtm->rtm_rmx,
			    &rt->rt_rmx);
		break;
	case RTM_DELETE:
		rt = rtable_lookup(tableid, info->rti_info[RTAX_DST],
		    info->rti_info[RTAX_NETMASK], info->rti_info[RTAX_GATEWAY],
		    prio);
		if (rt == NULL) {
			error = ESRCH;
			break;
		}

		/*
		 * If we got multipath routes, we require users to specify
		 * a matching gateway.
		 */
		if (ISSET(rt->rt_flags, RTF_MPATH) &&
		    info->rti_info[RTAX_GATEWAY] == NULL) {
			error = ESRCH;
			break;
		}

		ifp = if_get(rt->rt_ifidx);
		if (ifp == NULL) {
			rtfree(rt);
			rt = NULL;
			error = ESRCH;
			break;
		}

		/*
		 * Invalidate the cache of automagically created and
		 * referenced L2 entries to make sure that ``rt_gwroute''
		 * pointer stays valid for other CPUs.
		 */
		if ((ISSET(rt->rt_flags, RTF_CACHED))) {
			NET_LOCK();
			ifp->if_rtrequest(ifp, RTM_INVALIDATE, rt);
			/* Reset the MTU of the gateway route. */
			rtable_walk(tableid, rt_key(rt)->sa_family, NULL,
			    route_cleargateway, rt);
			NET_UNLOCK();
			break;
		}

		/*
		 * Make sure that local routes are only modified by the
		 * kernel.
		 */
		if (ISSET(rt->rt_flags, RTF_LOCAL|RTF_BROADCAST)) {
			error = EINVAL;
			break;
		}

		rtfree(rt);
		rt = NULL;

		NET_LOCK();
		error = rtrequest_delete(info, prio, ifp, &rt, tableid);
		NET_UNLOCK();
		break;
	case RTM_CHANGE:
		rt = rtable_lookup(tableid, info->rti_info[RTAX_DST],
		    info->rti_info[RTAX_NETMASK], info->rti_info[RTAX_GATEWAY],
		    prio);
		/*
		 * If we got multipath routes, we require users to specify
		 * a matching gateway.
		 */
		if ((rt != NULL) && ISSET(rt->rt_flags, RTF_MPATH) &&
		    (info->rti_info[RTAX_GATEWAY] == NULL)) {
			rtfree(rt);
			rt = NULL;
		}

		/*
		 * If RTAX_GATEWAY is the argument we're trying to
		 * change, try to find a compatible route.
		 */
		if ((rt == NULL) && (info->rti_info[RTAX_GATEWAY] != NULL)) {
			rt = rtable_lookup(tableid, info->rti_info[RTAX_DST],
			    info->rti_info[RTAX_NETMASK], NULL, prio);
			/* Ensure we don't pick a multipath one. */
			if ((rt != NULL) && ISSET(rt->rt_flags, RTF_MPATH)) {
				rtfree(rt);
				rt = NULL;
			}
		}

		if (rt == NULL) {
			error = ESRCH;
			break;
		}

		/*
		 * Make sure that local routes are only modified by the
		 * kernel.
		 */
		if (ISSET(rt->rt_flags, RTF_LOCAL|RTF_BROADCAST)) {
			error = EINVAL;
			break;
		}

		ifp = if_get(rt->rt_ifidx);
		if (ifp == NULL) {
			rtfree(rt);
			rt = NULL;
			error = ESRCH;
			break;
		}

		/*
		 * RTM_CHANGE needs a perfect match.
		 */
		plen = rtable_satoplen(info->rti_info[RTAX_DST]->sa_family,
		    info->rti_info[RTAX_NETMASK]);
		if (rt_plen(rt) != plen) {
			error = ESRCH;
			break;
		}

		if (info->rti_info[RTAX_GATEWAY] != NULL)
			if (rt->rt_gateway == NULL ||
			    bcmp(rt->rt_gateway,
			    info->rti_info[RTAX_GATEWAY],
			    info->rti_info[RTAX_GATEWAY]->sa_len)) {
				newgate = 1;
			}
		/*
		 * Check reachable gateway before changing the route.
		 * New gateway could require new ifaddr, ifp;
		 * flags may also be different; ifp may be specified
		 * by ll sockaddr when protocol address is ambiguous.
		 */
		if (newgate || info->rti_info[RTAX_IFP] != NULL ||
		    info->rti_info[RTAX_IFA] != NULL) {
			struct ifaddr	*ifa = NULL;

			NET_LOCK();
			if ((error = rtm_getifa(info, tableid)) != 0) {
				NET_UNLOCK();
				break;
			}
			ifa = info->rti_ifa;
			if (rt->rt_ifa != ifa) {
				ifp->if_rtrequest(ifp, RTM_DELETE, rt);
				ifafree(rt->rt_ifa);

				rt->rt_ifa = ifaref(ifa);
				rt->rt_ifidx = ifa->ifa_ifp->if_index;
				/* recheck link state after ifp change */
				rt_if_linkstate_change(rt, ifa->ifa_ifp,
				    tableid);
			}
			NET_UNLOCK();
		}
change:
		if (info->rti_info[RTAX_GATEWAY] != NULL) {
			/* When updating the gateway, make sure it is valid. */
			if (!newgate && rt->rt_gateway->sa_family !=
			    info->rti_info[RTAX_GATEWAY]->sa_family) {
				error = EINVAL;
				break;
			}

			NET_LOCK();
			error = rt_setgate(rt,
			    info->rti_info[RTAX_GATEWAY], tableid);
			NET_UNLOCK();
			if (error)
				break;
		}
#ifdef MPLS
		if (rtm->rtm_flags & RTF_MPLS) {
			NET_LOCK();
			error = rt_mpls_set(rt,
			    info->rti_info[RTAX_SRC], info->rti_mpls);
			NET_UNLOCK();
			if (error)
				break;
		} else if (newgate || (rtm->rtm_fmask & RTF_MPLS)) {
			NET_LOCK();
			/* if gateway changed remove MPLS information */
			rt_mpls_clear(rt);
			NET_UNLOCK();
		}
#endif

#ifdef BFD
		if (ISSET(rtm->rtm_flags, RTF_BFD)) {
			KERNEL_LOCK();
			error = bfdset(rt);
			KERNEL_UNLOCK();
			if (error)
				break;
		} else if (!ISSET(rtm->rtm_flags, RTF_BFD) &&
		    ISSET(rtm->rtm_fmask, RTF_BFD)) {
			KERNEL_LOCK();
			bfdclear(rt);
			KERNEL_UNLOCK();
		}
#endif

		NET_LOCK();
		/* Hack to allow some flags to be toggled */
		if (rtm->rtm_fmask) {
			/* MPLS flag it is set by rt_mpls_set() */
			rtm->rtm_fmask &= ~RTF_MPLS;
			rtm->rtm_flags &= ~RTF_MPLS;
			rt->rt_flags =
			    (rt->rt_flags & ~rtm->rtm_fmask) |
			    (rtm->rtm_flags & rtm->rtm_fmask);
		}
		rtm_setmetrics(rtm->rtm_inits, &rtm->rtm_rmx, &rt->rt_rmx);

		ifp->if_rtrequest(ifp, RTM_ADD, rt);

		if (info->rti_info[RTAX_LABEL] != NULL) {
			const char *rtlabel = ((const struct sockaddr_rtlabel *)
			    info->rti_info[RTAX_LABEL])->sr_label;
			rtlabel_unref(rt->rt_labelid);
			rt->rt_labelid = rtlabel_name2id(rtlabel);
		}
		if_group_routechange(info->rti_info[RTAX_DST],
		    info->rti_info[RTAX_NETMASK]);
		rt->rt_locks &= ~(rtm->rtm_inits);
		rt->rt_locks |= (rtm->rtm_inits & rtm->rtm_rmx.rmx_locks);
		NET_UNLOCK();
		break;
	case RTM_GET:
		rt = rtable_lookup(tableid, info->rti_info[RTAX_DST],
		    info->rti_info[RTAX_NETMASK], info->rti_info[RTAX_GATEWAY],
		    prio);
		if (rt == NULL)
			error = ESRCH;
		break;
	}

	if_put(ifp);
	*prt = rt;
	return (error);
}

struct ifaddr *
ifa_ifwithroute(int flags, const struct sockaddr *dst,
    const struct sockaddr *gateway, unsigned int rtableid)
{
	struct ifaddr	*ifa;

	if ((flags & RTF_GATEWAY) == 0) {
		/*
		 * If we are adding a route to an interface,
		 * and the interface is a pt to pt link
		 * we should search for the destination
		 * as our clue to the interface.  Otherwise
		 * we can use the local address.
		 */
		ifa = NULL;
		if (flags & RTF_HOST)
			ifa = ifa_ifwithdstaddr(dst, rtableid);
		if (ifa == NULL)
			ifa = ifa_ifwithaddr(gateway, rtableid);
	} else {
		/*
		 * If we are adding a route to a remote net
		 * or host, the gateway may still be on the
		 * other end of a pt to pt link.
		 */
		ifa = ifa_ifwithdstaddr(gateway, rtableid);
	}
	if (ifa == NULL) {
		if (gateway->sa_family == AF_LINK) {
			const struct sockaddr_dl *sdl;
			struct ifnet *ifp;

			sdl = satosdl_const(gateway);
			ifp = if_get(sdl->sdl_index);
			if (ifp != NULL)
				ifa = ifaof_ifpforaddr(dst, ifp);
			if_put(ifp);
		} else {
			struct rtentry *rt;

			rt = rtalloc(gateway, RT_RESOLVE, rtable_l2(rtableid));
			if (rt != NULL)
				ifa = rt->rt_ifa;
			rtfree(rt);
		}
	}
	if (ifa == NULL)
		return (NULL);
	if (ifa->ifa_addr->sa_family != dst->sa_family) {
		struct ifaddr	*oifa = ifa;
		ifa = ifaof_ifpforaddr(dst, ifa->ifa_ifp);
		if (ifa == NULL)
			ifa = oifa;
	}
	return (ifa);
}

int
rtm_getifa(struct rt_addrinfo *info, unsigned int rtid)
{
	struct ifnet	*ifp = NULL;

	/*
	 * The "returned" `ifa' is guaranteed to be alive only if
	 * the NET_LOCK() is held.
	 */
	NET_ASSERT_LOCKED();

	/*
	 * ifp may be specified by sockaddr_dl when protocol address
	 * is ambiguous
	 */
	if (info->rti_info[RTAX_IFP] != NULL) {
		const struct sockaddr_dl *sdl;

		sdl = satosdl_const(info->rti_info[RTAX_IFP]);
		ifp = if_get(sdl->sdl_index);
	}

#ifdef IPSEC
	/*
	 * If the destination is a PF_KEY address, we'll look
	 * for the existence of a encap interface number or address
	 * in the options list of the gateway. By default, we'll return
	 * enc0.
	 */
	if (info->rti_info[RTAX_DST] &&
	    info->rti_info[RTAX_DST]->sa_family == PF_KEY)
		info->rti_ifa = enc_getifa(rtid, 0);
#endif

	if (info->rti_ifa == NULL && info->rti_info[RTAX_IFA] != NULL)
		info->rti_ifa = ifa_ifwithaddr(info->rti_info[RTAX_IFA], rtid);

	if (info->rti_ifa == NULL) {
		const struct sockaddr	*sa;

		if ((sa = info->rti_info[RTAX_IFA]) == NULL)
			if ((sa = info->rti_info[RTAX_GATEWAY]) == NULL)
				sa = info->rti_info[RTAX_DST];

		if (sa != NULL && ifp != NULL)
			info->rti_ifa = ifaof_ifpforaddr(sa, ifp);
		else if (info->rti_info[RTAX_DST] != NULL &&
		    info->rti_info[RTAX_GATEWAY] != NULL)
			info->rti_ifa = ifa_ifwithroute(info->rti_flags,
			    info->rti_info[RTAX_DST],
			    info->rti_info[RTAX_GATEWAY],
			    rtid);
		else if (sa != NULL)
			info->rti_ifa = ifa_ifwithroute(info->rti_flags,
			    sa, sa, rtid);
	}

	if_put(ifp);

	if (info->rti_ifa == NULL)
		return (ENETUNREACH);

	return (0);
}

int
route_cleargateway(struct rtentry *rt, void *arg, unsigned int rtableid)
{
	struct rtentry *nhrt = arg;

	if (ISSET(rt->rt_flags, RTF_GATEWAY) && rt->rt_gwroute == nhrt &&
	    !ISSET(rt->rt_locks, RTV_MTU))
		atomic_store_int(&rt->rt_mtu, 0);

	return (0);
}

/*
 * Check if the user request to insert an ARP entry does not conflict
 * with existing ones.
 *
 * Only two entries are allowed for a given IP address: a private one
 * (priv) and a public one (pub).
 */
int
route_arp_conflict(struct rtentry *rt, struct rt_addrinfo *info)
{
	int		 proxy = (info->rti_flags & RTF_ANNOUNCE);

	if ((info->rti_flags & RTF_LLINFO) == 0 ||
	    (info->rti_info[RTAX_DST]->sa_family != AF_INET))
		return (0);

	if (rt == NULL || !ISSET(rt->rt_flags, RTF_LLINFO))
		return (0);

	/* If the entry is cached, it can be updated. */
	if (ISSET(rt->rt_flags, RTF_CACHED))
		return (0);

	/*
	 * Same destination, not cached and both "priv" or "pub" conflict.
	 * If a second entry exists, it always conflict.
	 */
	if ((ISSET(rt->rt_flags, RTF_ANNOUNCE) == proxy) ||
	    ISSET(rt->rt_flags, RTF_MPATH))
		return (EEXIST);

	/* No conflict but an entry exist so we need to force mpath. */
	info->rti_flags |= RTF_MPATH;
	return (0);
}

void
rtm_setmetrics(u_long which, const struct rt_metrics *in,
    struct rt_kmetrics *out)
{
	int64_t expire;

	if (which & RTV_MTU)
		atomic_store_int(&out->rmx_mtu, in->rmx_mtu);
	if (which & RTV_EXPIRE) {
		expire = in->rmx_expire;
		if (expire != 0) {
			expire -= gettime();
			expire += getuptime();
		}

		out->rmx_expire = expire;
	}
}

void
rtm_getmetrics(const struct rtentry *rt, struct rt_metrics *out)
{
	const struct rt_kmetrics *in = &rt->rt_rmx;
	int64_t expire;

	expire = in->rmx_expire;
	if (expire == 0)
		expire = rt_timer_get_expire(rt);
	if (expire != 0) {
		expire -= getuptime();
		expire += gettime();
	}

	bzero(out, sizeof(*out));
	out->rmx_locks = in->rmx_locks;
	out->rmx_mtu = atomic_load_int(&in->rmx_mtu);
	out->rmx_expire = expire;
	out->rmx_pksent = in->rmx_pksent;
}

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

int
rtm_xaddrs(caddr_t cp, caddr_t cplim, struct rt_addrinfo *rtinfo)
{
	int i;

	/*
	 * Parse address bits, split address storage in chunks, and
	 * set info pointers.  Use sa_len for traversing the memory
	 * and check that we stay within in the limit.
	 */
	bzero(rtinfo->rti_info, sizeof(rtinfo->rti_info));
	for (i = 0; i < sizeof(rtinfo->rti_addrs) * 8; i++) {
		struct sockaddr *sa;

		if ((rtinfo->rti_addrs & (1U << i)) == 0)
			continue;
		if (i >= RTAX_MAX || cp + sizeof(socklen_t) > cplim)
			return (EINVAL);
		sa = (struct sockaddr *)cp;
		if (cp + sa->sa_len > cplim)
			return (EINVAL);
		rtinfo->rti_info[i] = sa;
		ADVANCE(cp, sa);
	}
	/*
	 * Check that the address family is suitable for the route address
	 * type.  Check that each address has a size that fits its family
	 * and its length is within the size.  Strings within addresses must
	 * be NUL terminated.
	 */
	for (i = 0; i < RTAX_MAX; i++) {
		const struct sockaddr *sa;
		size_t len, maxlen, size;

		sa = rtinfo->rti_info[i];
		if (sa == NULL)
			continue;
		maxlen = size = 0;
		switch (i) {
		case RTAX_DST:
		case RTAX_GATEWAY:
		case RTAX_SRC:
			switch (sa->sa_family) {
			case AF_INET:
				size = sizeof(struct sockaddr_in);
				break;
			case AF_LINK:
				size = sizeof(struct sockaddr_dl);
				break;
#ifdef INET6
			case AF_INET6:
				size = sizeof(struct sockaddr_in6);
				break;
#endif
#ifdef MPLS
			case AF_MPLS:
				size = sizeof(struct sockaddr_mpls);
				break;
#endif
			}
			break;
		case RTAX_IFP:
			if (sa->sa_family != AF_LINK)
				return (EAFNOSUPPORT);
			/*
			 * XXX Should be sizeof(struct sockaddr_dl), but
			 * route(8) has a bug and provides less memory.
			 * arp(8) has another bug and uses sizeof pointer.
			 */
			size = 4;
			break;
		case RTAX_IFA:
			switch (sa->sa_family) {
			case AF_INET:
				size = sizeof(struct sockaddr_in);
				break;
#ifdef INET6
			case AF_INET6:
				size = sizeof(struct sockaddr_in6);
				break;
#endif
			default:
				return (EAFNOSUPPORT);
			}
			break;
		case RTAX_LABEL:
			if (sa->sa_family != AF_UNSPEC)
				return (EAFNOSUPPORT);
			maxlen = RTLABEL_LEN;
			size = sizeof(struct sockaddr_rtlabel);
			break;
#ifdef BFD
		case RTAX_BFD:
			if (sa->sa_family != AF_UNSPEC)
				return (EAFNOSUPPORT);
			size = sizeof(struct sockaddr_bfd);
			break;
#endif
		case RTAX_DNS:
			/* more validation in rtm_validate_proposal */
			if (sa->sa_len > sizeof(struct sockaddr_rtdns))
				return (EINVAL);
			if (sa->sa_len < offsetof(struct sockaddr_rtdns,
			    sr_dns))
				return (EINVAL);
			switch (sa->sa_family) {
			case AF_INET:
#ifdef INET6
			case AF_INET6:
#endif
				break;
			default:
				return (EAFNOSUPPORT);
			}
			break;
		case RTAX_STATIC:
			switch (sa->sa_family) {
			case AF_INET:
#ifdef INET6
			case AF_INET6:
#endif
				break;
			default:
				return (EAFNOSUPPORT);
			}
			maxlen = RTSTATIC_LEN;
			size = sizeof(struct sockaddr_rtstatic);
			break;
		case RTAX_SEARCH:
			if (sa->sa_family != AF_UNSPEC)
				return (EAFNOSUPPORT);
			maxlen = RTSEARCH_LEN;
			size = sizeof(struct sockaddr_rtsearch);
			break;
		}
		if (size) {
			/* memory for the full struct must be provided */
			if (sa->sa_len < size)
				return (EINVAL);
		}
		if (maxlen) {
			/* this should not happen */
			if (2 + maxlen > size)
				return (EINVAL);
			/* strings must be NUL terminated within the struct */
			len = strnlen(sa->sa_data, maxlen);
			if (len >= maxlen || 2 + len >= sa->sa_len)
				return (EINVAL);
			break;
		}
	}
	return (0);
}

struct mbuf *
rtm_msg1(int type, struct rt_addrinfo *rtinfo)
{
	struct rt_msghdr	*rtm;
	struct mbuf		*m;
	int			 i;
	const struct sockaddr	*sa;
	int			 len, dlen, hlen;

	switch (type) {
	case RTM_DELADDR:
	case RTM_NEWADDR:
		hlen = sizeof(struct ifa_msghdr);
		break;
	case RTM_IFINFO:
		hlen = sizeof(struct if_msghdr);
		break;
	case RTM_IFANNOUNCE:
		hlen = sizeof(struct if_announcemsghdr);
		break;
#ifdef BFD
	case RTM_BFD:
		hlen = sizeof(struct bfd_msghdr);
		break;
#endif
	case RTM_80211INFO:
		hlen = sizeof(struct if_ieee80211_msghdr);
		break;
	default:
		hlen = sizeof(struct rt_msghdr);
		break;
	}
	len = hlen;
	for (i = 0; i < RTAX_MAX; i++) {
		if (rtinfo == NULL || (sa = rtinfo->rti_info[i]) == NULL)
			continue;
		len += ROUNDUP(sa->sa_len);
	}
	if (len > MCLBYTES)
		panic("rtm_msg1");
	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (m && len > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			m = NULL;
		}
	}
	if (m == NULL)
		return (m);
	m->m_pkthdr.len = m->m_len = len;
	m->m_pkthdr.ph_ifidx = 0;
	rtm = mtod(m, struct rt_msghdr *);
	bzero(rtm, len);
	len = hlen;
	for (i = 0; i < RTAX_MAX; i++) {
		if (rtinfo == NULL || (sa = rtinfo->rti_info[i]) == NULL)
			continue;
		rtinfo->rti_addrs |= (1U << i);
		dlen = ROUNDUP(sa->sa_len);
		if (m_copyback(m, len, sa->sa_len, sa, M_NOWAIT)) {
			m_freem(m);
			return (NULL);
		}
		len += dlen;
	}
	rtm->rtm_msglen = len;
	rtm->rtm_hdrlen = hlen;
	rtm->rtm_version = RTM_VERSION;
	rtm->rtm_type = type;
	return (m);
}

int
rtm_msg2(int type, int vers, struct rt_addrinfo *rtinfo, caddr_t cp,
    struct walkarg *w)
{
	int		i;
	int		len, dlen, hlen, second_time = 0;
	caddr_t		cp0;

	rtinfo->rti_addrs = 0;
again:
	switch (type) {
	case RTM_DELADDR:
	case RTM_NEWADDR:
		len = sizeof(struct ifa_msghdr);
		break;
	case RTM_IFINFO:
		len = sizeof(struct if_msghdr);
		break;
	default:
		len = sizeof(struct rt_msghdr);
		break;
	}
	hlen = len;
	if ((cp0 = cp) != NULL)
		cp += len;
	for (i = 0; i < RTAX_MAX; i++) {
		const struct sockaddr *sa;

		if ((sa = rtinfo->rti_info[i]) == NULL)
			continue;
		rtinfo->rti_addrs |= (1U << i);
		dlen = ROUNDUP(sa->sa_len);
		if (cp) {
			bcopy(sa, cp, sa->sa_len);
			bzero(cp + sa->sa_len, dlen - sa->sa_len);
			cp += dlen;
		}
		len += dlen;
	}
	/* align message length to the next natural boundary */
	len = ALIGN(len);
	if (cp == 0 && w != NULL && !second_time) {
		w->w_needed += len;
		if (w->w_needed <= w->w_given && w->w_where) {
			if (w->w_tmemsize < len) {
				free(w->w_tmem, M_RTABLE, w->w_tmemsize);
				w->w_tmem = malloc(len, M_RTABLE,
				    M_NOWAIT | M_ZERO);
				if (w->w_tmem)
					w->w_tmemsize = len;
			}
			if (w->w_tmem) {
				cp = w->w_tmem;
				second_time = 1;
				goto again;
			} else
				w->w_where = 0;
		}
	}
	if (cp && w)		/* clear the message header */
		bzero(cp0, hlen);

	if (cp) {
		struct rt_msghdr *rtm = (struct rt_msghdr *)cp0;

		rtm->rtm_version = RTM_VERSION;
		rtm->rtm_type = type;
		rtm->rtm_msglen = len;
		rtm->rtm_hdrlen = hlen;
	}
	return (len);
}

void
rtm_send(struct rtentry *rt, int cmd, int error, unsigned int rtableid)
{
	struct rt_addrinfo	 info;
	struct ifnet		*ifp;
	struct sockaddr_rtlabel	 sa_rl;
	struct sockaddr_in6	 sa_mask;

	memset(&info, 0, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(rt);
	info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	if (!ISSET(rt->rt_flags, RTF_HOST))
		info.rti_info[RTAX_NETMASK] = rt_plen2mask(rt, &sa_mask);
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(rt->rt_labelid, &sa_rl);
	ifp = if_get(rt->rt_ifidx);
	if (ifp != NULL) {
		info.rti_info[RTAX_IFP] = sdltosa(ifp->if_sadl);
		info.rti_info[RTAX_IFA] = rtable_getsource(rtableid,
		    info.rti_info[RTAX_DST]->sa_family);
		if (info.rti_info[RTAX_IFA] == NULL)
			info.rti_info[RTAX_IFA] = rt->rt_ifa->ifa_addr;
	}

	rtm_miss(cmd, &info, rt->rt_flags, rt->rt_priority, rt->rt_ifidx, error,
	    rtableid);
	if_put(ifp);
}

/*
 * This routine is called to generate a message from the routing
 * socket indicating that a redirect has occurred, a routing lookup
 * has failed, or that a protocol has detected timeouts to a particular
 * destination.
 */
void
rtm_miss(int type, struct rt_addrinfo *rtinfo, int flags, uint8_t prio,
    u_int ifidx, int error, u_int tableid)
{
	struct rt_msghdr	*rtm;
	struct mbuf		*m;
	const struct sockaddr	*sa = rtinfo->rti_info[RTAX_DST];

	if (rtptable.rtp_count == 0)
		return;
	m = rtm_msg1(type, rtinfo);
	if (m == NULL)
		return;
	rtm = mtod(m, struct rt_msghdr *);
	rtm->rtm_flags = RTF_DONE | flags;
	rtm->rtm_priority = prio;
	rtm->rtm_errno = error;
	rtm->rtm_tableid = tableid;
	rtm->rtm_addrs = rtinfo->rti_addrs;
	rtm->rtm_index = ifidx;
	route_input(m, NULL, sa ? sa->sa_family : AF_UNSPEC);
}

/*
 * This routine is called to generate a message from the routing
 * socket indicating that the status of a network interface has changed.
 */
void
rtm_ifchg(struct ifnet *ifp)
{
	struct rt_addrinfo	 info;
	struct if_msghdr	*ifm;
	struct mbuf		*m;

	if (rtptable.rtp_count == 0)
		return;
	memset(&info, 0, sizeof(info));
	info.rti_info[RTAX_IFP] = sdltosa(ifp->if_sadl);
	m = rtm_msg1(RTM_IFINFO, &info);
	if (m == NULL)
		return;
	ifm = mtod(m, struct if_msghdr *);
	ifm->ifm_index = ifp->if_index;
	ifm->ifm_tableid = ifp->if_rdomain;
	ifm->ifm_flags = ifp->if_flags;
	ifm->ifm_xflags = ifp->if_xflags;
	if_getdata(ifp, &ifm->ifm_data);
	ifm->ifm_addrs = info.rti_addrs;
	route_input(m, NULL, AF_UNSPEC);
}

/*
 * This is called to generate messages from the routing socket
 * indicating a network interface has had addresses associated with it.
 * if we ever reverse the logic and replace messages TO the routing
 * socket indicate a request to configure interfaces, then it will
 * be unnecessary as the routing socket will automatically generate
 * copies of it.
 */
void
rtm_addr(int cmd, struct ifaddr *ifa)
{
	struct ifnet		*ifp = ifa->ifa_ifp;
	struct mbuf		*m;
	struct rt_addrinfo	 info;
	struct ifa_msghdr	*ifam;

	if (rtptable.rtp_count == 0)
		return;

	memset(&info, 0, sizeof(info));
	info.rti_info[RTAX_IFA] = ifa->ifa_addr;
	info.rti_info[RTAX_IFP] = sdltosa(ifp->if_sadl);
	info.rti_info[RTAX_NETMASK] = ifa->ifa_netmask;
	info.rti_info[RTAX_BRD] = ifa->ifa_dstaddr;
	if ((m = rtm_msg1(cmd, &info)) == NULL)
		return;
	ifam = mtod(m, struct ifa_msghdr *);
	ifam->ifam_index = ifp->if_index;
	ifam->ifam_metric = ifa->ifa_metric;
	ifam->ifam_flags = ifa->ifa_flags;
	ifam->ifam_addrs = info.rti_addrs;
	ifam->ifam_tableid = ifp->if_rdomain;

	route_input(m, NULL,
	    ifa->ifa_addr ? ifa->ifa_addr->sa_family : AF_UNSPEC);
}

/*
 * This is called to generate routing socket messages indicating
 * network interface arrival and departure.
 */
void
rtm_ifannounce(struct ifnet *ifp, int what)
{
	struct if_announcemsghdr	*ifan;
	struct mbuf			*m;

	if (rtptable.rtp_count == 0)
		return;
	m = rtm_msg1(RTM_IFANNOUNCE, NULL);
	if (m == NULL)
		return;
	ifan = mtod(m, struct if_announcemsghdr *);
	ifan->ifan_index = ifp->if_index;
	strlcpy(ifan->ifan_name, ifp->if_xname, sizeof(ifan->ifan_name));
	ifan->ifan_what = what;
	route_input(m, NULL, AF_UNSPEC);
}

#ifdef BFD
/*
 * This is used to generate routing socket messages indicating
 * the state of a BFD session.
 */
void
rtm_bfd(struct bfd_config *bfd)
{
	struct bfd_msghdr	*bfdm;
	struct sockaddr_bfd	 sa_bfd;
	struct mbuf		*m;
	struct rt_addrinfo	 info;

	if (rtptable.rtp_count == 0)
		return;
	memset(&info, 0, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(bfd->bc_rt);
	info.rti_info[RTAX_IFA] = bfd->bc_rt->rt_ifa->ifa_addr;

	m = rtm_msg1(RTM_BFD, &info);
	if (m == NULL)
		return;
	bfdm = mtod(m, struct bfd_msghdr *);
	bfdm->bm_addrs = info.rti_addrs;

	KERNEL_ASSERT_LOCKED();
	bfd2sa(bfd->bc_rt, &sa_bfd);
	memcpy(&bfdm->bm_sa, &sa_bfd, sizeof(sa_bfd));

	route_input(m, NULL, info.rti_info[RTAX_DST]->sa_family);
}
#endif /* BFD */

/*
 * This is used to generate routing socket messages indicating
 * the state of an ieee80211 interface.
 */
void
rtm_80211info(struct ifnet *ifp, struct if_ieee80211_data *ifie)
{
	struct if_ieee80211_msghdr	*ifim;
	struct mbuf			*m;

	if (rtptable.rtp_count == 0)
		return;
	m = rtm_msg1(RTM_80211INFO, NULL);
	if (m == NULL)
		return;
	ifim = mtod(m, struct if_ieee80211_msghdr *);
	ifim->ifim_index = ifp->if_index;
	ifim->ifim_tableid = ifp->if_rdomain;

	memcpy(&ifim->ifim_ifie, ifie, sizeof(ifim->ifim_ifie));
	route_input(m, NULL, AF_UNSPEC);
}

/*
 * This is used to generate routing socket messages indicating
 * the address selection proposal from an interface.
 */
void
rtm_proposal(struct ifnet *ifp, struct rt_addrinfo *rtinfo, int flags,
    uint8_t prio)
{
	struct rt_msghdr	*rtm;
	struct mbuf		*m;

	m = rtm_msg1(RTM_PROPOSAL, rtinfo);
	if (m == NULL)
		return;
	rtm = mtod(m, struct rt_msghdr *);
	rtm->rtm_flags = RTF_DONE | flags;
	rtm->rtm_priority = prio;
	rtm->rtm_tableid = ifp->if_rdomain;
	rtm->rtm_index = ifp->if_index;
	rtm->rtm_addrs = rtinfo->rti_addrs;

	route_input(m, NULL, rtinfo->rti_info[RTAX_DNS]->sa_family);
}

/*
 * This is used in dumping the kernel table via sysctl().
 */
int
sysctl_dumpentry(const struct rtentry *rt, void *v, unsigned int id)
{
	struct walkarg		*w = v;
	int			 error = 0, size;
	struct rt_addrinfo	 info;
	struct ifnet		*ifp;
#ifdef BFD
	struct sockaddr_bfd	 sa_bfd;
#endif
#ifdef MPLS
	struct sockaddr_mpls	 sa_mpls;
#endif
	struct sockaddr_rtlabel	 sa_rl;
	struct sockaddr_in6	 sa_mask;

	if (w->w_op == NET_RT_FLAGS && !(rt->rt_flags & w->w_arg))
		return 0;
	if (w->w_op == NET_RT_DUMP && w->w_arg) {
		u_int8_t prio = w->w_arg & RTP_MASK;
		if (w->w_arg < 0) {
			prio = (-w->w_arg) & RTP_MASK;
			/* Show all routes that are not this priority */
			if (prio == (rt->rt_priority & RTP_MASK))
				return 0;
		} else {
			if (prio != (rt->rt_priority & RTP_MASK) &&
			    prio != RTP_ANY)
				return 0;
		}
	}
	bzero(&info, sizeof(info));
	info.rti_info[RTAX_DST] = rt_key(rt);
	info.rti_info[RTAX_GATEWAY] = rt->rt_gateway;
	info.rti_info[RTAX_NETMASK] = rt_plen2mask(rt, &sa_mask);
	ifp = if_get(rt->rt_ifidx);
	if (ifp != NULL) {
		info.rti_info[RTAX_IFP] = sdltosa(ifp->if_sadl);
		info.rti_info[RTAX_IFA] =
		    rtable_getsource(id, info.rti_info[RTAX_DST]->sa_family);
		if (info.rti_info[RTAX_IFA] == NULL)
			info.rti_info[RTAX_IFA] = rt->rt_ifa->ifa_addr;
		if (ifp->if_flags & IFF_POINTOPOINT)
			info.rti_info[RTAX_BRD] = rt->rt_ifa->ifa_dstaddr;
	}
	if_put(ifp);
	info.rti_info[RTAX_LABEL] = rtlabel_id2sa(rt->rt_labelid, &sa_rl);
#ifdef BFD
	if (rt->rt_flags & RTF_BFD) {
		KERNEL_ASSERT_LOCKED();
		info.rti_info[RTAX_BFD] = bfd2sa(rt, &sa_bfd);
	}
#endif
#ifdef MPLS
	if (rt->rt_flags & RTF_MPLS) {
		bzero(&sa_mpls, sizeof(sa_mpls));
		sa_mpls.smpls_family = AF_MPLS;
		sa_mpls.smpls_len = sizeof(sa_mpls);
		sa_mpls.smpls_label = ((struct rt_mpls *)
		    rt->rt_llinfo)->mpls_label;
		info.rti_info[RTAX_SRC] = (struct sockaddr *)&sa_mpls;
		info.rti_mpls = ((struct rt_mpls *)
		    rt->rt_llinfo)->mpls_operation;
	}
#endif

	size = rtm_msg2(RTM_GET, RTM_VERSION, &info, NULL, w);
	if (w->w_where && w->w_tmem && w->w_needed <= w->w_given) {
		struct rt_msghdr *rtm = (struct rt_msghdr *)w->w_tmem;

		rtm->rtm_pid = curproc->p_p->ps_pid;
		rtm->rtm_flags = RTF_DONE | rt->rt_flags;
		rtm->rtm_priority = rt->rt_priority & RTP_MASK;
		rtm_getmetrics(rt, &rtm->rtm_rmx);
		/* Do not account the routing table's reference. */
		rtm->rtm_rmx.rmx_refcnt = refcnt_read(&rt->rt_refcnt) - 1;
		rtm->rtm_index = rt->rt_ifidx;
		rtm->rtm_addrs = info.rti_addrs;
		rtm->rtm_tableid = id;
#ifdef MPLS
		rtm->rtm_mpls = info.rti_mpls;
#endif
		if ((error = copyout(rtm, w->w_where, size)) != 0)
			w->w_where = NULL;
		else
			w->w_where += size;
	}
	return (error);
}

#ifndef SMALL_KERNEL
int
sysctl_rtable_rtstat(void *oldp, size_t *oldlenp, void *newp)
{
	extern struct cpumem *rtcounters;
	uint64_t counters[rts_ncounters];
	struct rtstat rtstat;
	uint32_t *words = (uint32_t *)&rtstat;
	int i;

	CTASSERT(sizeof(rtstat) == (nitems(counters) * sizeof(uint32_t)));
	memset(&rtstat, 0, sizeof rtstat);
	counters_read(rtcounters, counters, nitems(counters), NULL);

	for (i = 0; i < nitems(counters); i++)
		words[i] = (uint32_t)counters[i];

	return (sysctl_rdstruct(oldp, oldlenp, newp, &rtstat, sizeof(rtstat)));
}
#endif /* SMALL_KERNEL */

int
sysctl_iflist(int af, struct walkarg *w)
{
	struct ifnet		*ifp;
	struct ifaddr		*ifa;
	struct rt_addrinfo	 info;
	int			 len, error = 0;

	bzero(&info, sizeof(info));
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (w->w_arg && w->w_arg != ifp->if_index)
			continue;
		/* Copy the link-layer address first */
		info.rti_info[RTAX_IFP] = sdltosa(ifp->if_sadl);
		len = rtm_msg2(RTM_IFINFO, RTM_VERSION, &info, 0, w);
		if (w->w_where && w->w_tmem && w->w_needed <= w->w_given) {
			struct if_msghdr *ifm;

			ifm = (struct if_msghdr *)w->w_tmem;
			ifm->ifm_index = ifp->if_index;
			ifm->ifm_tableid = ifp->if_rdomain;
			ifm->ifm_flags = ifp->if_flags;
			if_getdata(ifp, &ifm->ifm_data);
			ifm->ifm_addrs = info.rti_addrs;
			error = copyout(ifm, w->w_where, len);
			if (error)
				return (error);
			w->w_where += len;
		}
		info.rti_info[RTAX_IFP] = NULL;
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			KASSERT(ifa->ifa_addr->sa_family != AF_LINK);
			if (af && af != ifa->ifa_addr->sa_family)
				continue;
			info.rti_info[RTAX_IFA] = ifa->ifa_addr;
			info.rti_info[RTAX_NETMASK] = ifa->ifa_netmask;
			info.rti_info[RTAX_BRD] = ifa->ifa_dstaddr;
			len = rtm_msg2(RTM_NEWADDR, RTM_VERSION, &info, 0, w);
			if (w->w_where && w->w_tmem &&
			    w->w_needed <= w->w_given) {
				struct ifa_msghdr *ifam;

				ifam = (struct ifa_msghdr *)w->w_tmem;
				ifam->ifam_index = ifa->ifa_ifp->if_index;
				ifam->ifam_flags = ifa->ifa_flags;
				ifam->ifam_metric = ifa->ifa_metric;
				ifam->ifam_addrs = info.rti_addrs;
				error = copyout(w->w_tmem, w->w_where, len);
				if (error)
					return (error);
				w->w_where += len;
			}
		}
		info.rti_info[RTAX_IFA] = info.rti_info[RTAX_NETMASK] =
		    info.rti_info[RTAX_BRD] = NULL;
	}
	return (0);
}

int
sysctl_ifnames(struct walkarg *w)
{
	struct if_nameindex_msg ifn;
	struct ifnet *ifp;
	int error = 0;

	/* XXX ignore tableid for now */
	TAILQ_FOREACH(ifp, &ifnetlist, if_list) {
		if (w->w_arg && w->w_arg != ifp->if_index)
			continue;
		w->w_needed += sizeof(ifn);
		if (w->w_where && w->w_needed <= w->w_given) {

			memset(&ifn, 0, sizeof(ifn));
			ifn.if_index = ifp->if_index;
			strlcpy(ifn.if_name, ifp->if_xname,
			    sizeof(ifn.if_name));
			error = copyout(&ifn, w->w_where, sizeof(ifn));
			if (error)
				return (error);
			w->w_where += sizeof(ifn);
		}
	}

	return (0);
}

int
sysctl_source(int af, u_int tableid, struct walkarg *w)
{
	union {
		struct sockaddr_in in;
#ifdef INET6
		struct sockaddr_in6 in6;
#endif
	}		 buf;
	struct sockaddr	*sa;
	int		 size, error = 0;

	NET_LOCK_SHARED();
	if ((sa = rtable_getsource(tableid, af)) != NULL) {
		switch (sa->sa_family) {
		case AF_INET:
			size = sizeof(struct sockaddr_in);
			break;
#ifdef INET6
		case AF_INET6:
			size = sizeof(struct sockaddr_in6);
			break;
#endif
		default:
			sa = NULL;
			break;
		}

	}
	if (sa != NULL)
		memcpy(&buf, sa, size);
	NET_UNLOCK_SHARED();

	if (sa != NULL) {
		w->w_needed += size;
		if (w->w_where && w->w_needed <= w->w_given) {
			if ((error = copyout(&buf, w->w_where, size)))
				return (error);
			w->w_where += size;
		}
	}
	return (0);
}

int
sysctl_rtable(int *name, u_int namelen, void *where, size_t *given, void *new,
    size_t newlen)
{
	int			 i, error = EINVAL;
	u_char			 af;
	struct walkarg		 w;
	u_int			 tableid = 0;

	if (new)
		return (EPERM);
	if (namelen < 3 || namelen > 4)
		return (EINVAL);
	af = name[0];
	bzero(&w, sizeof(w));
	w.w_where = where;
	w.w_given = *given;
	w.w_op = name[1];
	w.w_arg = name[2];

	if (namelen == 4) {
		tableid = name[3];
		if (!rtable_exists(tableid))
			return (ENOENT);
	} else
		tableid = curproc->p_p->ps_rtableid;

	switch (w.w_op) {
	case NET_RT_DUMP:
	case NET_RT_FLAGS:
		NET_LOCK_SHARED();
		for (i = 1; i <= AF_MAX; i++) {
			if (af != 0 && af != i)
				continue;

			error = rtable_read(tableid, i, sysctl_dumpentry, &w);
			if (error == EAFNOSUPPORT)
				error = 0;
			if (error)
				break;
		}
		NET_UNLOCK_SHARED();
		break;
#ifndef SMALL_KERNEL
	case NET_RT_STATS:
		return (sysctl_rtable_rtstat(where, given, new));
	case NET_RT_TABLE:
		tableid = w.w_arg;
		if (rtable_exists(tableid)) {
			struct rt_tableinfo	 tableinfo;

			memset(&tableinfo, 0, sizeof tableinfo);
			tableinfo.rti_tableid = tableid;
			tableinfo.rti_domainid = rtable_l2(tableid);
			error = sysctl_rdstruct(where, given, new,
			    &tableinfo, sizeof(tableinfo));
			return (error);
		} else
			return (ENOENT);
#endif /* SMALL_KERNEL */
	case NET_RT_SOURCE:
		tableid = w.w_arg;
		if (!rtable_exists(tableid))
			return (ENOENT);
		for (i = 1; i <= AF_MAX; i++) {
			if (af != 0 && af != i)
				continue;

			error = sysctl_source(i, tableid, &w);
			if (error == EAFNOSUPPORT)
				error = 0;
			if (error)
				break;
		}
		break;
	case NET_RT_IFLIST:
		NET_LOCK_SHARED();
		error = sysctl_iflist(af, &w);
		NET_UNLOCK_SHARED();
		break;

	case NET_RT_IFNAMES:
		NET_LOCK_SHARED();
		error = sysctl_ifnames(&w);
		NET_UNLOCK_SHARED();
		break;
	}
	free(w.w_tmem, M_RTABLE, w.w_tmemsize);
	if (where) {
		*given = w.w_where - (caddr_t)where;
		if (w.w_needed > w.w_given)
			return (ENOMEM);
	} else if (w.w_needed == 0) {
		*given = 0;
	} else {
		*given = roundup(w.w_needed + MAX(w.w_needed / 10, 1024),
		    PAGE_SIZE);
	}
	return (error);
}

int
rtm_validate_proposal(struct rt_addrinfo *info)
{
	if (info->rti_addrs & ~(RTA_NETMASK | RTA_IFA | RTA_DNS | RTA_STATIC |
	    RTA_SEARCH)) {
		return -1;
	}

	if (ISSET(info->rti_addrs, RTA_NETMASK)) {
		const struct sockaddr *sa = info->rti_info[RTAX_NETMASK];
		if (sa == NULL)
			return -1;
		switch (sa->sa_family) {
		case AF_INET:
			if (sa->sa_len != sizeof(struct sockaddr_in))
				return -1;
			break;
		case AF_INET6:
			if (sa->sa_len != sizeof(struct sockaddr_in6))
				return -1;
			break;
		default:
			return -1;
		}
	}

	if (ISSET(info->rti_addrs, RTA_IFA)) {
		const struct sockaddr *sa = info->rti_info[RTAX_IFA];
		if (sa == NULL)
			return -1;
		switch (sa->sa_family) {
		case AF_INET:
			if (sa->sa_len != sizeof(struct sockaddr_in))
				return -1;
			break;
		case AF_INET6:
			if (sa->sa_len != sizeof(struct sockaddr_in6))
				return -1;
			break;
		default:
			return -1;
		}
	}

	if (ISSET(info->rti_addrs, RTA_DNS)) {
		const struct sockaddr_rtdns *rtdns =
		    (const struct sockaddr_rtdns *)info->rti_info[RTAX_DNS];
		if (rtdns == NULL)
			return -1;
		if (rtdns->sr_len > sizeof(*rtdns))
			return -1;
		if (rtdns->sr_len < offsetof(struct sockaddr_rtdns, sr_dns))
			return -1;
		switch (rtdns->sr_family) {
		case AF_INET:
			if ((rtdns->sr_len - offsetof(struct sockaddr_rtdns,
			    sr_dns)) % sizeof(struct in_addr) != 0)
				return -1;
			break;
#ifdef INET6
		case AF_INET6:
			if ((rtdns->sr_len - offsetof(struct sockaddr_rtdns,
			    sr_dns)) % sizeof(struct in6_addr) != 0)
				return -1;
			break;
#endif
		default:
			return -1;
		}
	}

	if (ISSET(info->rti_addrs, RTA_STATIC)) {
		const struct sockaddr_rtstatic *rtstatic = (const struct
		    sockaddr_rtstatic *)info->rti_info[RTAX_STATIC];
		if (rtstatic == NULL)
			return -1;
		if (rtstatic->sr_len > sizeof(*rtstatic))
			return -1;
		if (rtstatic->sr_len <=
		    offsetof(struct sockaddr_rtstatic, sr_static))
			return -1;
	}

	if (ISSET(info->rti_addrs, RTA_SEARCH)) {
		const struct sockaddr_rtsearch *rtsearch = (const struct
		    sockaddr_rtsearch *)info->rti_info[RTAX_SEARCH];
		if (rtsearch == NULL)
			return -1;
		if (rtsearch->sr_len > sizeof(*rtsearch))
			return -1;
		if (rtsearch->sr_len <=
		    offsetof(struct sockaddr_rtsearch, sr_search))
			return -1;
	}

	return 0;
}

int
rt_setsource(unsigned int rtableid, const struct sockaddr *src)
{
	struct ifaddr	*ifa;
	/*
	 * If source address is 0.0.0.0 or ::
	 * use automatic source selection
	 */
	switch(src->sa_family) {
	case AF_INET:
		if(satosin_const(src)->sin_addr.s_addr == INADDR_ANY) {
			rtable_setsource(rtableid, AF_INET, NULL);
			return (0);
		}
		break;
#ifdef INET6
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&satosin6_const(src)->sin6_addr)) {
			rtable_setsource(rtableid, AF_INET6, NULL);
			return (0);
		}
		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}

	/*
	 * Check if source address is assigned to an interface in the
	 * same rdomain
	 */
	if ((ifa = ifa_ifwithaddr(src, rtableid)) == NULL)
		return (EINVAL);

	return rtable_setsource(rtableid, src->sa_family, ifa->ifa_addr);
}

/*
 * Definitions of protocols supported in the ROUTE domain.
 */

const struct pr_usrreqs route_usrreqs = {
	.pru_attach	= route_attach,
	.pru_detach	= route_detach,
	.pru_disconnect	= route_disconnect,
	.pru_shutdown	= route_shutdown,
	.pru_rcvd	= route_rcvd,
	.pru_send	= route_send,
	.pru_sockaddr	= route_sockaddr,
	.pru_peeraddr	= route_peeraddr,
};

const struct protosw routesw[] = {
{
  .pr_type	= SOCK_RAW,
  .pr_domain	= &routedomain,
  .pr_flags	= PR_ATOMIC|PR_ADDR|PR_WANTRCVD,
  .pr_ctloutput	= route_ctloutput,
  .pr_usrreqs	= &route_usrreqs,
  .pr_init	= route_prinit,
  .pr_sysctl	= sysctl_rtable
}
};

const struct domain routedomain = {
  .dom_family = PF_ROUTE,
  .dom_name = "route",
  .dom_init = route_init,
  .dom_protosw = routesw,
  .dom_protoswNPROTOSW = &routesw[nitems(routesw)]
};
