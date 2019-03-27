/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 Ana Kukec <anchie@FreeBSD.org>
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
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/protosw.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/sockbuf.h>
#include <sys/socketvar.h>
#include <sys/types.h>

#include <net/route.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/ip_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/scope6_var.h>
#include <netinet6/send.h>

static MALLOC_DEFINE(M_SEND, "send", "Secure Neighbour Discovery");

/*
 * The socket used to communicate with the SeND daemon.
 */
VNET_DEFINE_STATIC(struct socket *, send_so);
#define	V_send_so	VNET(send_so)

u_long	send_sendspace	= 8 * (1024 + sizeof(struct sockaddr_send));
u_long	send_recvspace	= 9216;

struct mtx	send_mtx;
#define SEND_LOCK_INIT()	mtx_init(&send_mtx, "send_mtx", NULL, MTX_DEF)
#define SEND_LOCK()		mtx_lock(&send_mtx)
#define SEND_UNLOCK()		mtx_unlock(&send_mtx)
#define SEND_LOCK_DESTROY()     mtx_destroy(&send_mtx)

static int
send_attach(struct socket *so, int proto, struct thread *td)
{
	int error;

	SEND_LOCK();
	if (V_send_so != NULL) {
		SEND_UNLOCK();
		return (EEXIST);
	}

	error = priv_check(td, PRIV_NETINET_RAW);
	if (error) {
		SEND_UNLOCK();
		return(error);
	}

	if (proto != IPPROTO_SEND) {
		SEND_UNLOCK();
		return (EPROTONOSUPPORT);
	}
	error = soreserve(so, send_sendspace, send_recvspace);
	if (error) {
		SEND_UNLOCK();
		return(error);
	}

	V_send_so = so;
	SEND_UNLOCK();

	return (0);
}

static int
send_output(struct mbuf *m, struct ifnet *ifp, int direction)
{
	struct ip6_hdr *ip6;
	struct sockaddr_in6 dst;
	struct icmp6_hdr *icmp6;
	int icmp6len;

	/*
	 * Receive incoming (SeND-protected) or outgoing traffic
	 * (SeND-validated) from the SeND user space application.
	 */

	switch (direction) {
	case SND_IN:
		if (m->m_len < (sizeof(struct ip6_hdr) +
		    sizeof(struct icmp6_hdr))) {
			m = m_pullup(m, sizeof(struct ip6_hdr) +
			    sizeof(struct icmp6_hdr));
			if (!m)
				return (ENOBUFS);
		}

		/* Before passing off the mbuf record the proper interface. */
		m->m_pkthdr.rcvif = ifp;

		if (m->m_flags & M_PKTHDR)
			icmp6len = m->m_pkthdr.len - sizeof(struct ip6_hdr);
		else
			panic("Doh! not the first mbuf.");

		ip6 = mtod(m, struct ip6_hdr *);
		icmp6 = (struct icmp6_hdr *)(ip6 + 1);

		/*
		 * Output the packet as icmp6.c:icpm6_input() would do.
		 * The mbuf is always consumed, so we do not have to
		 * care about that.
		 */
		switch (icmp6->icmp6_type) {
		case ND_NEIGHBOR_SOLICIT:
			nd6_ns_input(m, sizeof(struct ip6_hdr), icmp6len);
			break;
		case ND_NEIGHBOR_ADVERT:
			nd6_na_input(m, sizeof(struct ip6_hdr), icmp6len);
			break;
		case ND_REDIRECT:
			icmp6_redirect_input(m, sizeof(struct ip6_hdr));
			break;
		case ND_ROUTER_SOLICIT:
			nd6_rs_input(m, sizeof(struct ip6_hdr), icmp6len);
			break;
		case ND_ROUTER_ADVERT:
			nd6_ra_input(m, sizeof(struct ip6_hdr), icmp6len);
			break;
		default:
			m_freem(m);
			return (ENOSYS);
		}
		return (0);

	case SND_OUT:
		if (m->m_len < sizeof(struct ip6_hdr)) {
			m = m_pullup(m, sizeof(struct ip6_hdr));
			if (!m)
				return (ENOBUFS);
		}
		ip6 = mtod(m, struct ip6_hdr *);
		if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst))
			m->m_flags |= M_MCAST;

		bzero(&dst, sizeof(dst));
		dst.sin6_family = AF_INET6;
		dst.sin6_len = sizeof(dst);
		dst.sin6_addr = ip6->ip6_dst;

		m_clrprotoflags(m);	/* Avoid confusing lower layers. */

		IP_PROBE(send, NULL, NULL, ip6, ifp, NULL, ip6);

		/*
		 * Output the packet as nd6.c:nd6_output_lle() would do.
		 * The mbuf is always consumed, so we do not have to care
		 * about that.
		 * XXX-BZ as we added data, what about fragmenting,
		 * if now needed?
		 */
		int error;
		error = ((*ifp->if_output)(ifp, m, (struct sockaddr *)&dst,
		    NULL));
		if (error)
			error = ENOENT;
		return (error);

	default:
		panic("%s: direction %d neither SND_IN nor SND_OUT.",
		     __func__, direction);
	}
}

/*
 * Receive a SeND message from user space to be either send out by the kernel
 * or, with SeND ICMPv6 options removed, to be further processed by the icmp6
 * input path.
 */
static int
send_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *nam,
    struct mbuf *control, struct thread *td)
{
	struct sockaddr_send *sendsrc;
	struct ifnet *ifp;
	int error;

	KASSERT(V_send_so == so, ("%s: socket %p not send socket %p",
		__func__, so, V_send_so));

	sendsrc = (struct sockaddr_send *)nam;
	ifp = ifnet_byindex_ref(sendsrc->send_ifidx);
	if (ifp == NULL) {
		error = ENETUNREACH;
		goto err;
	}

	error = send_output(m, ifp, sendsrc->send_direction);
	if_rele(ifp);
	m = NULL;

err:
	if (m != NULL)
		m_freem(m);
	return (error);
}

static void
send_close(struct socket *so)
{

	SEND_LOCK();
	if (V_send_so)
		V_send_so = NULL;
	SEND_UNLOCK();
}

/*
 * Send a SeND message to user space, that was either received and has to be
 * validated or was about to be send out and has to be handled by the SEND
 * daemon adding SeND ICMPv6 options.
 */
static int
send_input(struct mbuf *m, struct ifnet *ifp, int direction, int msglen __unused)
{
	struct ip6_hdr *ip6;
	struct sockaddr_send sendsrc;

	SEND_LOCK();
	if (V_send_so == NULL) {
		SEND_UNLOCK();
		return (-1);
	}

	/*
	 * Make sure to clear any possible internally embedded scope before
	 * passing the packet to user space for SeND cryptographic signature
	 * validation to succeed.
	 */
	ip6 = mtod(m, struct ip6_hdr *);
	in6_clearscope(&ip6->ip6_src);
	in6_clearscope(&ip6->ip6_dst);

	bzero(&sendsrc, sizeof(sendsrc));
	sendsrc.send_len = sizeof(sendsrc);
	sendsrc.send_family = AF_INET6;
	sendsrc.send_direction = direction;
	sendsrc.send_ifidx = ifp->if_index;

	/*
	 * Send incoming or outgoing traffic to user space either to be
	 * protected (outgoing) or validated (incoming) according to rfc3971.
	 */
	SOCKBUF_LOCK(&V_send_so->so_rcv);
	if (sbappendaddr_locked(&V_send_so->so_rcv,
	    (struct sockaddr *)&sendsrc, m, NULL) == 0) {
		SOCKBUF_UNLOCK(&V_send_so->so_rcv);
		/* XXX stats. */
		m_freem(m);
	} else {
		sorwakeup_locked(V_send_so);
	}

	SEND_UNLOCK();
	return (0);
}

struct pr_usrreqs send_usrreqs = {
	.pru_attach =		send_attach,
	.pru_send =		send_send,
	.pru_detach =		send_close
};
struct protosw send_protosw = {
	.pr_type =		SOCK_RAW,
	.pr_flags =		PR_ATOMIC|PR_ADDR,
	.pr_protocol =		IPPROTO_SEND,
	.pr_usrreqs =		&send_usrreqs
};

static int
send_modevent(module_t mod, int type, void *unused)
{
#ifdef __notyet__
	VNET_ITERATOR_DECL(vnet_iter);
#endif
	int error;

	switch (type) {
	case MOD_LOAD:
		SEND_LOCK_INIT();

		error = pf_proto_register(PF_INET6, &send_protosw);
		if (error != 0) {
			printf("%s:%d: MOD_LOAD pf_proto_register(): %d\n",
			   __func__, __LINE__, error);
			SEND_LOCK_DESTROY();
			break;
		}
		send_sendso_input_hook = send_input;
		break;
	case MOD_UNLOAD:
		/* Do not allow unloading w/o locking. */
		return (EBUSY);
#ifdef __notyet__
		VNET_LIST_RLOCK_NOSLEEP();
		SEND_LOCK();
		VNET_FOREACH(vnet_iter) {
			CURVNET_SET(vnet_iter);
			if (V_send_so != NULL) {
				CURVNET_RESTORE();
				SEND_UNLOCK();
				VNET_LIST_RUNLOCK_NOSLEEP();
				return (EBUSY);
			}
			CURVNET_RESTORE();
		}
		SEND_UNLOCK();
		VNET_LIST_RUNLOCK_NOSLEEP();
		error = pf_proto_unregister(PF_INET6, IPPROTO_SEND, SOCK_RAW);
		if (error == 0)
			SEND_LOCK_DESTROY();
		send_sendso_input_hook = NULL;
		break;
#endif
	default:
		error = 0;
		break;
	}

	return (error);
}

static moduledata_t sendmod = {
	"send",
	send_modevent,
	0
};

DECLARE_MODULE(send, sendmod, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY);
