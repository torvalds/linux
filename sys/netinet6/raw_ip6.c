/*	$OpenBSD: raw_ip6.c,v 1.194 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: raw_ip6.c,v 1.69 2001/03/04 15:55:44 itojun Exp $	*/

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
 * Copyright (c) 1982, 1986, 1988, 1993
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
 *	@(#)raw_ip.c	8.2 (Berkeley) 1/4/94
 */

#include "pf.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#ifdef MROUTING
#include <netinet6/ip6_mroute.h>
#endif
#include <netinet/icmp6.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet6/ip6protosw.h>
#include <netinet6/raw_ip6.h>

#if NPF > 0
#include <net/pfvar.h>
#endif

/*
 * Raw interface to IP6 protocol.
 */

struct	inpcbtable rawin6pcbtable;

struct cpumem *rip6counters;

const struct pr_usrreqs rip6_usrreqs = {
	.pru_attach	= rip6_attach,
	.pru_detach	= rip6_detach,
	.pru_bind	= rip6_bind,
	.pru_connect	= rip6_connect,
	.pru_disconnect	= rip6_disconnect,
	.pru_shutdown	= rip6_shutdown,
	.pru_send	= rip6_send,
	.pru_control	= in6_control,
	.pru_sockaddr	= in6_sockaddr,
	.pru_peeraddr	= in6_peeraddr,
};

void	rip6_sbappend(struct inpcb *, struct mbuf *, struct ip6_hdr *, int,
	    struct sockaddr_in6 *);

/*
 * Initialize raw connection block queue.
 */
void
rip6_init(void)
{
	in_pcbinit(&rawin6pcbtable, 1);
	rip6counters = counters_alloc(rip6s_ncounters);
}

int
rip6_input(struct mbuf **mp, int *offp, int proto, int af, struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct inpcb_iterator iter = { .inp_table = NULL };
	struct inpcb *inp, *last;
	struct in6_addr *key;
	struct sockaddr_in6 rip6src;
	uint8_t type;

	KASSERT(af == AF_INET6);

	if (proto == IPPROTO_ICMPV6) {
		struct icmp6_hdr *icmp6;

		icmp6 = ip6_exthdr_get(mp, *offp, sizeof(*icmp6));
		if (icmp6 == NULL)
			return IPPROTO_DONE;
		type = icmp6->icmp6_type;
	} else
		rip6stat_inc(rip6s_ipackets);

	memset(&rip6src, 0, sizeof(rip6src));
	rip6src.sin6_family = AF_INET6;
	rip6src.sin6_len = sizeof(rip6src);
	/* KAME hack: recover scopeid */
	in6_recoverscope(&rip6src, &ip6->ip6_src);

	key = &ip6->ip6_dst;
#if NPF > 0
	if (m->m_pkthdr.pf.flags & PF_TAG_DIVERTED) {
		struct pf_divert *divert;

		divert = pf_find_divert(m);
		KASSERT(divert != NULL);
		switch (divert->type) {
		case PF_DIVERT_TO:
			key = &divert->addr.v6;
			break;
		case PF_DIVERT_REPLY:
			break;
		default:
			panic("%s: unknown divert type %d, mbuf %p, divert %p",
			    __func__, divert->type, m, divert);
		}
	}
#endif
	mtx_enter(&rawin6pcbtable.inpt_mtx);
	last = inp = NULL;
	while ((inp = in_pcb_iterator(&rawin6pcbtable, inp, &iter)) != NULL) {
		KASSERT(ISSET(inp->inp_flags, INP_IPV6));

		/*
		 * Packet must not be inserted after disconnected wakeup
		 * call.  To avoid race, check again when holding receive
		 * buffer mutex.
		 */
		if (ISSET(READ_ONCE(inp->inp_socket->so_rcv.sb_state),
		    SS_CANTRCVMORE))
			continue;
		if (rtable_l2(inp->inp_rtableid) !=
		    rtable_l2(m->m_pkthdr.ph_rtableid))
			continue;

		if ((inp->inp_ipv6.ip6_nxt || proto == IPPROTO_ICMPV6) &&
		    inp->inp_ipv6.ip6_nxt != proto)
			continue;
		if (!IN6_IS_ADDR_UNSPECIFIED(&inp->inp_laddr6) &&
		    !IN6_ARE_ADDR_EQUAL(&inp->inp_laddr6, key))
			continue;
		if (!IN6_IS_ADDR_UNSPECIFIED(&inp->inp_faddr6) &&
		    !IN6_ARE_ADDR_EQUAL(&inp->inp_faddr6, &ip6->ip6_src))
			continue;
		if (proto == IPPROTO_ICMPV6 && inp->inp_icmp6filt) {
			if (ICMP6_FILTER_WILLBLOCK(type, inp->inp_icmp6filt))
				continue;
		}
		if (proto != IPPROTO_ICMPV6 && inp->inp_cksum6 != -1) {
			rip6stat_inc(rip6s_isum);
			/*
			 * Although in6_cksum() does not need the position of
			 * the checksum field for verification, enforce that it
			 * is located within the packet.  Userland has given
			 * a checksum offset, a packet too short for that is
			 * invalid.  Avoid overflow with user supplied offset.
			 */
			if (m->m_pkthdr.len < *offp + 2 ||
			    m->m_pkthdr.len - *offp - 2 < inp->inp_cksum6 ||
			    in6_cksum(m, proto, *offp,
			    m->m_pkthdr.len - *offp)) {
				rip6stat_inc(rip6s_badsum);
				continue;
			}
		}

		if (last != NULL) {
			struct mbuf *n;

			mtx_leave(&rawin6pcbtable.inpt_mtx);

			n = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (n != NULL)
				rip6_sbappend(last, n, ip6, *offp, &rip6src);
			in_pcbunref(last);

			mtx_enter(&rawin6pcbtable.inpt_mtx);
		}
		last = in_pcbref(inp);
	}
	mtx_leave(&rawin6pcbtable.inpt_mtx);

	if (last == NULL) {
		struct counters_ref ref;
		uint64_t *counters;

		if (proto != IPPROTO_ICMPV6) {
			rip6stat_inc(rip6s_nosock);
			if (m->m_flags & M_MCAST)
				rip6stat_inc(rip6s_nosockmcast);
		}
		if (proto == IPPROTO_NONE || proto == IPPROTO_ICMPV6) {
			m_freem(m);
		} else {
			int prvnxt = ip6_get_prevhdr(m, *offp);

			icmp6_error(m, ICMP6_PARAM_PROB,
			    ICMP6_PARAMPROB_NEXTHEADER, prvnxt);
		}
		counters = counters_enter(&ref, ip6counters);
		counters[ip6s_delivered]--;
		counters_leave(&ref, ip6counters);

		return IPPROTO_DONE;
	}

	rip6_sbappend(last, m, ip6, *offp, &rip6src);
	in_pcbunref(last);

	return IPPROTO_DONE;
}

void
rip6_sbappend(struct inpcb *inp, struct mbuf *m, struct ip6_hdr *ip6, int hlen,
    struct sockaddr_in6 *rip6src)
{
	struct socket *so = inp->inp_socket;
	struct mbuf *opts = NULL;
	int ret = 0;

	if (inp->inp_flags & IN6P_CONTROLOPTS)
		ip6_savecontrol(inp, m, &opts);
	/* strip intermediate headers */
	m_adj(m, hlen);

	mtx_enter(&so->so_rcv.sb_mtx);
	if (!ISSET(inp->inp_socket->so_rcv.sb_state, SS_CANTRCVMORE))
		ret = sbappendaddr(&so->so_rcv, sin6tosa(rip6src), m, opts);
	mtx_leave(&so->so_rcv.sb_mtx);

	if (ret == 0) {
		m_freem(m);
		m_freem(opts);
		rip6stat_inc(rip6s_fullsock);
	} else
		sorwakeup(so);
}

void
rip6_ctlinput(int cmd, struct sockaddr *sa, u_int rdomain, void *d)
{
	struct ip6_hdr *ip6;
	struct ip6ctlparam *ip6cp = NULL;
	struct sockaddr_in6 *sa6 = satosin6(sa);
	const struct sockaddr_in6 *sa6_src = NULL;
	void *cmdarg;
	void (*notify)(struct inpcb *, int) = in_pcbrtchange;
	int nxt;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		return;

	if ((unsigned)cmd >= PRC_NCMDS)
		return;
	if (PRC_IS_REDIRECT(cmd))
		notify = in_pcbrtchange, d = NULL;
	else if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (cmd == PRC_MSGSIZE)
		; /* special code is present, see below */
	else if (inet6ctlerrmap[cmd] == 0)
		return;

	/* if the parameter is from icmp6, decode it. */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
		ip6 = ip6cp->ip6c_ip6;
		cmdarg = ip6cp->ip6c_cmdarg;
		sa6_src = ip6cp->ip6c_src;
		nxt = ip6cp->ip6c_nxt;
	} else {
		ip6 = NULL;
		cmdarg = NULL;
		sa6_src = &sa6_any;
		nxt = -1;
	}

	if (ip6 && cmd == PRC_MSGSIZE) {
		int valid = 0;
		struct inpcb *inp;

		/*
		 * Check to see if we have a valid raw IPv6 socket
		 * corresponding to the address in the ICMPv6 message
		 * payload, and the protocol (ip6_nxt) meets the socket.
		 * XXX chase extension headers, or pass final nxt value
		 * from icmp6_notify_error()
		 */
		inp = in6_pcblookup(&rawin6pcbtable, &sa6->sin6_addr, 0,
		    &sa6_src->sin6_addr, 0, rdomain);

		if (inp && inp->inp_ipv6.ip6_nxt &&
		    inp->inp_ipv6.ip6_nxt == nxt)
			valid = 1;

		/*
		 * Depending on the value of "valid" and routing table
		 * size (mtudisc_{hi,lo}wat), we will:
		 * - recalculate the new MTU and create the
		 *   corresponding routing entry, or
		 * - ignore the MTU change notification.
		 */
		icmp6_mtudisc_update((struct ip6ctlparam *)d, valid);
		in_pcbunref(inp);

		/*
		 * regardless of if we called icmp6_mtudisc_update(),
		 * we need to call in6_pcbnotify(), to notify path
		 * MTU change to the userland (2292bis-02), because
		 * some unconnected sockets may share the same
		 * destination and want to know the path MTU.
		 */
	}

	in6_pcbnotify(&rawin6pcbtable, sa6, 0,
	    sa6_src, 0, rdomain, cmd, cmdarg, notify);
}

/*
 * Generate IPv6 header and pass packet to ip6_output.
 * Tack on options user may have setup with control call.
 */
int
rip6_output(struct mbuf *m, struct socket *so, struct sockaddr *dstaddr,
    struct mbuf *control)
{
	struct in6_addr *dst;
	struct ip6_hdr *ip6;
	struct inpcb *inp;
	u_int	plen = m->m_pkthdr.len;
	int error = 0;
	struct ip6_pktopts opt, *optp = NULL;
	int type;		/* for ICMPv6 output statistics only */
	int priv = 0;
	int flags;

	inp = sotoinpcb(so);

	priv = 0;
	if ((so->so_state & SS_PRIV) != 0)
		priv = 1;
	if (control) {
		if ((error = ip6_setpktopts(control, &opt,
		    inp->inp_outputopts6,
		    priv, so->so_proto->pr_protocol)) != 0)
			goto bad;
		optp = &opt;
	} else
		optp = inp->inp_outputopts6;

	if (dstaddr->sa_family != AF_INET6) {
		error = EAFNOSUPPORT;
		goto bad;
	}
	dst = &satosin6(dstaddr)->sin6_addr;
	if (IN6_IS_ADDR_V4MAPPED(dst)) {
		error = EADDRNOTAVAIL;
		goto bad;
	}

	/*
	 * For an ICMPv6 packet, we should know its type and code
	 * to update statistics.
	 */
	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6) {
		struct icmp6_hdr *icmp6;
		if (m->m_len < sizeof(struct icmp6_hdr) &&
		    (m = m_pullup(m, sizeof(struct icmp6_hdr))) == NULL) {
			error = ENOBUFS;
			goto bad;
		}
		icmp6 = mtod(m, struct icmp6_hdr *);
		type = icmp6->icmp6_type;
	}

	M_PREPEND(m, sizeof(*ip6), M_DONTWAIT);
	if (!m) {
		error = ENOBUFS;
		goto bad;
	}
	ip6 = mtod(m, struct ip6_hdr *);

	/*
	 * Next header might not be ICMP6 but use its pseudo header anyway.
	 */
	ip6->ip6_dst = *dst;

	/* KAME hack: embed scopeid */
	if (in6_embedscope(&ip6->ip6_dst, satosin6(dstaddr),
	    optp, inp->inp_moptions6) != 0) {
		error = EINVAL;
		goto bad;
	}

	/*
	 * Source address selection.
	 */
	{
		const struct in6_addr *in6a;

		error = in6_pcbselsrc(&in6a, satosin6(dstaddr), inp, optp);
		if (error)
			goto bad;

		ip6->ip6_src = *in6a;
	}

	ip6->ip6_flow = inp->inp_flowinfo & IPV6_FLOWINFO_MASK;
	ip6->ip6_vfc  &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc  |= IPV6_VERSION;
#if 0				/* ip6_plen will be filled in ip6_output. */
	ip6->ip6_plen  = htons((u_short)plen);
#endif
	ip6->ip6_nxt   = inp->inp_ipv6.ip6_nxt;
	ip6->ip6_hlim = in6_selecthlim(inp);

	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6 ||
	    inp->inp_cksum6 != -1) {
		struct mbuf *n;
		int off;
		u_int16_t *sump;
		int sumoff;

		/* compute checksum */
		if (so->so_proto->pr_protocol == IPPROTO_ICMPV6)
			off = offsetof(struct icmp6_hdr, icmp6_cksum);
		else
			off = inp->inp_cksum6;
		if (plen < 2 || plen - 2 < off) {
			error = EINVAL;
			goto bad;
		}
		off += sizeof(struct ip6_hdr);

		n = m_pulldown(m, off, sizeof(*sump), &sumoff);
		if (n == NULL) {
			m = NULL;
			error = ENOBUFS;
			goto bad;
		}
		sump = (u_int16_t *)(mtod(n, caddr_t) + sumoff);
		*sump = 0;
		*sump = in6_cksum(m, ip6->ip6_nxt, sizeof(*ip6), plen);
	}

	flags = 0;
	if (inp->inp_flags & IN6P_MINMTU)
		flags |= IPV6_MINMTU;

	/* force routing table */
	m->m_pkthdr.ph_rtableid = inp->inp_rtableid;

#if NPF > 0
	if (inp->inp_socket->so_state & SS_ISCONNECTED &&
	    so->so_proto->pr_protocol != IPPROTO_ICMPV6)
		pf_mbuf_link_inpcb(m, inp);
#endif

	error = ip6_output(m, optp, &inp->inp_route, flags,
	    inp->inp_moptions6, &inp->inp_seclevel);
	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6) {
		icmp6stat_inc(icp6s_outhist + type);
	} else
		rip6stat_inc(rip6s_opackets);

	goto freectl;

 bad:
	m_freem(m);

 freectl:
	if (control) {
		ip6_clearpktopts(&opt, -1);
		m_freem(control);
	}
	return (error);
}

/*
 * Raw IPv6 socket option processing.
 */
int
rip6_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
#ifdef MROUTING
	int error;
#endif

	switch (level) {
	case IPPROTO_IPV6:
		switch (optname) {
#ifdef MROUTING
		case MRT6_INIT:
		case MRT6_DONE:
		case MRT6_ADD_MIF:
		case MRT6_DEL_MIF:
		case MRT6_ADD_MFC:
		case MRT6_DEL_MFC:
			if (op == PRCO_SETOPT) {
				error = ip6_mrouter_set(optname, so, m);
			} else if (op == PRCO_GETOPT)
				error = ip6_mrouter_get(optname, so, m);
			else
				error = EINVAL;
			return (error);
#endif
		case IPV6_CHECKSUM:
			return (ip6_raw_ctloutput(op, so, level, optname, m));
		default:
			return (ip6_ctloutput(op, so, level, optname, m));
		}

	case IPPROTO_ICMPV6:
		/*
		 * XXX: is it better to call icmp6_ctloutput() directly
		 * from protosw?
		 */
		return (icmp6_ctloutput(op, so, level, optname, m));

	default:
		return EINVAL;
	}
}

extern	u_long rip6_sendspace;
extern	u_long rip6_recvspace;

int
rip6_attach(struct socket *so, int proto, int wait)
{
	struct inpcb *inp;
	int error;

	if (so->so_pcb)
		panic("%s", __func__);
	if ((so->so_state & SS_PRIV) == 0)
		return (EACCES);
	if (proto < 0 || proto >= IPPROTO_MAX)
		return EPROTONOSUPPORT;

	if ((error = soreserve(so, rip6_sendspace, rip6_recvspace)))
		return error;
	if ((error = in_pcballoc(so, &rawin6pcbtable, wait)))
		return error;

	inp = sotoinpcb(so);
	inp->inp_ipv6.ip6_nxt = proto;
	inp->inp_cksum6 = -1;

	inp->inp_icmp6filt = malloc(sizeof(struct icmp6_filter), M_PCB,
	    wait == M_WAIT ? M_WAITOK : M_NOWAIT);
	if (inp->inp_icmp6filt == NULL) {
		in_pcbdetach(inp);
		return ENOMEM;
	}
	ICMP6_FILTER_SETPASSALL(inp->inp_icmp6filt);
	return 0;
}

int
rip6_detach(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);

	soassertlocked(so);

	if (inp == NULL)
		panic("%s", __func__);
#ifdef MROUTING
	if (so == ip6_mrouter[inp->inp_rtableid])
		ip6_mrouter_done(so);
#endif
	free(inp->inp_icmp6filt, M_PCB, sizeof(struct icmp6_filter));
	inp->inp_icmp6filt = NULL;

	in_pcbdetach(inp);

	return (0);
}

int
rip6_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct inpcb *inp = sotoinpcb(so);
	struct sockaddr_in6 *addr;
	int error;

	soassertlocked(so);

	if ((error = in6_nam2sin6(nam, &addr)))
		return (error);

	/*
	 * Make sure to not enter in_pcblookup_local(), local ports
	 * are non-sensical for raw sockets.
	 */
	addr->sin6_port = 0;

	if ((error = in6_pcbaddrisavail(inp, addr, 0, p)))
		return (error);

	mtx_enter(&rawin6pcbtable.inpt_mtx);
	inp->inp_laddr6 = addr->sin6_addr;
	mtx_leave(&rawin6pcbtable.inpt_mtx);

	return (0);
}

int
rip6_connect(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp = sotoinpcb(so);
	struct sockaddr_in6 *addr;
	const struct in6_addr *in6a;
	int error;

	soassertlocked(so);

	if ((error = in6_nam2sin6(nam, &addr)))
		return (error);

	/* Source address selection. XXX: need pcblookup? */
	error = in6_pcbselsrc(&in6a, addr, inp, inp->inp_outputopts6);
	if (error)
		return (error);

	mtx_enter(&rawin6pcbtable.inpt_mtx);
	inp->inp_laddr6 = *in6a;
	inp->inp_faddr6 = addr->sin6_addr;
	mtx_leave(&rawin6pcbtable.inpt_mtx);
	soisconnected(so);

	return (0);
}

int
rip6_disconnect(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);

	soassertlocked(so);

	if ((so->so_state & SS_ISCONNECTED) == 0)
		return (ENOTCONN);

	soisdisconnected(so);
	mtx_enter(&rawin6pcbtable.inpt_mtx);
	inp->inp_faddr6 = in6addr_any;
	mtx_leave(&rawin6pcbtable.inpt_mtx);

	return (0);
}

int
rip6_shutdown(struct socket *so)
{
	/*
	 * Mark the connection as being incapable of further input.
	 */
	soassertlocked(so);
	socantsendmore(so);
	return (0);
}

int
rip6_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
	struct mbuf *control)
{
	struct inpcb *inp = sotoinpcb(so);
	struct sockaddr_in6 dst;
	int error;

	soassertlocked(so);

	/*
	 * Ship a packet out. The appropriate raw output
	 * routine handles any messaging necessary.
	 */

	/* always copy sockaddr to avoid overwrites */
	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(dst);
	if (so->so_state & SS_ISCONNECTED) {
		if (nam) {
			error = EISCONN;
			goto out;
		}
		dst.sin6_addr = inp->inp_faddr6;
	} else {
		struct sockaddr_in6 *addr6;

		if (nam == NULL) {
			error = ENOTCONN;
			goto out;
		}
		if ((error = in6_nam2sin6(nam, &addr6)))
			goto out;
		dst.sin6_addr = addr6->sin6_addr;
		dst.sin6_scope_id = addr6->sin6_scope_id;
	}
	error = rip6_output(m, so, sin6tosa(&dst), control);
	control = NULL;
	m = NULL;

out:
	m_freem(control);
	m_freem(m);

	return (error);
}

#ifndef SMALL_KERNEL
int
rip6_sysctl_rip6stat(void *oldp, size_t *oldplen, void *newp)
{
	struct rip6stat rip6stat;

	CTASSERT(sizeof(rip6stat) == rip6s_ncounters * sizeof(uint64_t));
	counters_read(rip6counters, (uint64_t *)&rip6stat, rip6s_ncounters,
	    NULL);

	return (sysctl_rdstruct(oldp, oldplen, newp,
	    &rip6stat, sizeof(rip6stat)));
}

int
rip6_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return ENOTDIR;

	switch (name[0]) {
	case RIPV6CTL_STATS:
		return (rip6_sysctl_rip6stat(oldp, oldlenp, newp));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}
#endif /* SMALL_KERNEL */
