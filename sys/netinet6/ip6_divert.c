/*      $OpenBSD: ip6_divert.c,v 1.108 2025/07/08 00:47:41 jsg Exp $ */

/*
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_divert.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/icmp6.h>

#include <net/pfvar.h>

/*
 * Locks used to protect data:
 *	a	atomic
 */

struct	inpcbtable	divb6table;
struct	cpumem		*div6counters;

const struct pr_usrreqs divert6_usrreqs = {
	.pru_attach	= divert6_attach,
	.pru_detach	= divert_detach,
	.pru_bind	= divert_bind,
	.pru_shutdown	= divert_shutdown,
	.pru_send	= divert6_send,
	.pru_control	= in6_control,
	.pru_sockaddr	= in6_sockaddr,
	.pru_peeraddr	= in6_peeraddr,
};

int	divert6_output(struct inpcb *, struct mbuf *, struct mbuf *,
	    struct mbuf *);

void
divert6_init(void)
{
	in_pcbinit(&divb6table, DIVERT_HASHSIZE);
	div6counters = counters_alloc(divs_ncounters);
}

int
divert6_output(struct inpcb *inp, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	struct sockaddr_in6 *sin6;
	int error, min_hdrlen, nxt, off, dir;
	struct ip6_hdr *ip6;

	m_freem(control);

	if ((error = in6_nam2sin6(nam, &sin6)))
		goto fail;

	/* Do basic sanity checks. */
	if (m->m_pkthdr.len < sizeof(struct ip6_hdr))
		goto fail;
	if ((m = m_pullup(m, sizeof(struct ip6_hdr))) == NULL) {
		/* m_pullup() has freed the mbuf, so just return. */
		divstat_inc(divs_errors);
		return (ENOBUFS);
	}
	ip6 = mtod(m, struct ip6_hdr *);
	if ((ip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION)
		goto fail;
	if (m->m_pkthdr.len < sizeof(struct ip6_hdr) + ntohs(ip6->ip6_plen))
		goto fail;

	/*
	 * Recalculate the protocol checksum since the userspace application
	 * may have modified the packet prior to reinjection.
	 */
	off = ip6_lasthdr(m, 0, IPPROTO_IPV6, &nxt);
	if (off < sizeof(struct ip6_hdr))
		goto fail;

	dir = (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) ? PF_OUT : PF_IN);

	switch (nxt) {
	case IPPROTO_TCP:
		min_hdrlen = sizeof(struct tcphdr);
		m->m_pkthdr.csum_flags |= M_TCP_CSUM_OUT;
		break;
	case IPPROTO_UDP:
		min_hdrlen = sizeof(struct udphdr);
		m->m_pkthdr.csum_flags |= M_UDP_CSUM_OUT;
		break;
	case IPPROTO_ICMPV6:
		min_hdrlen = sizeof(struct icmp6_hdr);
		m->m_pkthdr.csum_flags |= M_ICMP_CSUM_OUT;
		break;
	default:
		min_hdrlen = 0;
		break;
	}
	if (min_hdrlen && m->m_pkthdr.len < off + min_hdrlen)
		goto fail;

	m->m_pkthdr.pf.flags |= PF_TAG_DIVERTED_PACKET;

	if (dir == PF_IN) {
		struct rtentry *rt;
		struct ifnet *ifp;

		rt = rtalloc(sin6tosa(sin6), 0, inp->inp_rtableid);
		if (!rtisvalid(rt) || !ISSET(rt->rt_flags, RTF_LOCAL)) {
			rtfree(rt);
			error = EADDRNOTAVAIL;
			goto fail;
		}
		m->m_pkthdr.ph_ifidx = rt->rt_ifidx;
		rtfree(rt);

		/*
		 * Recalculate the protocol checksum for the inbound packet
		 * since the userspace application may have modified the packet
		 * prior to reinjection.
		 */
		in6_proto_cksum_out(m, NULL);

		ifp = if_get(m->m_pkthdr.ph_ifidx);
		if (ifp == NULL) {
			error = ENETDOWN;
			goto fail;
		}
		ipv6_input(ifp, m, NULL);
		if_put(ifp);
	} else {
		m->m_pkthdr.ph_rtableid = inp->inp_rtableid;

		error = ip6_output(m, NULL, &inp->inp_route,
		    IP_ALLOWBROADCAST | IP_RAWOUTPUT, NULL, NULL);
	}

	divstat_inc(divs_opackets);
	return (error);

fail:
	divstat_inc(divs_errors);
	m_freem(m);
	return (error ? error : EINVAL);
}

void
divert6_packet(struct mbuf *m, int dir, u_int16_t divert_port)
{
	struct inpcb *inp = NULL;
	struct socket *so;
	struct sockaddr_in6 sin6;

	divstat_inc(divs_ipackets);

	if (m->m_len < sizeof(struct ip6_hdr) &&
	    (m = m_pullup(m, sizeof(struct ip6_hdr))) == NULL) {
		divstat_inc(divs_errors);
		goto bad;
	}

	mtx_enter(&divb6table.inpt_mtx);
	TAILQ_FOREACH(inp, &divb6table.inpt_queue, inp_queue) {
		if (inp->inp_lport != divert_port)
			continue;
		in_pcbref(inp);
		break;
	}
	mtx_leave(&divb6table.inpt_mtx);
	if (inp == NULL) {
		divstat_inc(divs_noport);
		goto bad;
	}

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
	sin6.sin6_len = sizeof(sin6);

	if (dir == PF_IN) {
		struct ifaddr *ifa;
		struct ifnet *ifp;

		ifp = if_get(m->m_pkthdr.ph_ifidx);
		if (ifp == NULL) {
			divstat_inc(divs_errors);
			goto bad;
		}
		TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
			if (ifa->ifa_addr->sa_family != AF_INET6)
				continue;
			sin6.sin6_addr = satosin6(ifa->ifa_addr)->sin6_addr;
			break;
		}
		if_put(ifp);
	} else {
		/*
		 * Calculate protocol checksum for outbound packet diverted
		 * to userland.  pf out rule diverts before cksum offload.
		 */
		in6_proto_cksum_out(m, NULL);
	}

	so = inp->inp_socket;
	mtx_enter(&so->so_rcv.sb_mtx);
	if (sbappendaddr(&so->so_rcv, sin6tosa(&sin6), m, NULL) == 0) {
		mtx_leave(&so->so_rcv.sb_mtx);
		divstat_inc(divs_fullsock);
		goto bad;
	}
	mtx_leave(&so->so_rcv.sb_mtx);
	sorwakeup(so);

	in_pcbunref(inp);
	return;

 bad:
	in_pcbunref(inp);
	m_freem(m);
}

int
divert6_attach(struct socket *so, int proto, int wait)
{
	int error;

	if (so->so_pcb != NULL)
		return EINVAL;
	if ((so->so_state & SS_PRIV) == 0)
		return EACCES;

	error = soreserve(so, atomic_load_int(&divert_sendspace),
	    atomic_load_int(&divert_recvspace));
	if (error)
		return (error);
	error = in_pcballoc(so, &divb6table, wait);
	if (error)
		return (error);

	return (0);
}

int
divert6_send(struct socket *so, struct mbuf *m, struct mbuf *addr,
    struct mbuf *control)
{
	struct inpcb *inp = sotoinpcb(so);

	soassertlocked(so);
	return (divert6_output(inp, m, addr, control));
}
