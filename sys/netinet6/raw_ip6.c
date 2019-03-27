/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

/*-
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ipsec.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sx.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/in_pcb.h>

#include <netinet/icmp6.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6protosw.h>
#include <netinet6/ip6_mroute.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/ip6_var.h>
#include <netinet6/nd6.h>
#include <netinet6/raw_ip6.h>
#include <netinet6/scope6_var.h>
#include <netinet6/send.h>

#include <netipsec/ipsec_support.h>

#include <machine/stdarg.h>

#define	satosin6(sa)	((struct sockaddr_in6 *)(sa))
#define	ifatoia6(ifa)	((struct in6_ifaddr *)(ifa))

/*
 * Raw interface to IP6 protocol.
 */

VNET_DECLARE(struct inpcbhead, ripcb);
VNET_DECLARE(struct inpcbinfo, ripcbinfo);
#define	V_ripcb				VNET(ripcb)
#define	V_ripcbinfo			VNET(ripcbinfo)

extern u_long	rip_sendspace;
extern u_long	rip_recvspace;

VNET_PCPUSTAT_DEFINE(struct rip6stat, rip6stat);
VNET_PCPUSTAT_SYSINIT(rip6stat);

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(rip6stat);
#endif /* VIMAGE */

/*
 * Hooks for multicast routing. They all default to NULL, so leave them not
 * initialized and rely on BSS being set to 0.
 */

/*
 * The socket used to communicate with the multicast routing daemon.
 */
VNET_DEFINE(struct socket *, ip6_mrouter);

/*
 * The various mrouter functions.
 */
int (*ip6_mrouter_set)(struct socket *, struct sockopt *);
int (*ip6_mrouter_get)(struct socket *, struct sockopt *);
int (*ip6_mrouter_done)(void);
int (*ip6_mforward)(struct ip6_hdr *, struct ifnet *, struct mbuf *);
int (*mrt6_ioctl)(u_long, caddr_t);

/*
 * Setup generic address and protocol structures for raw_input routine, then
 * pass them along with mbuf chain.
 */
int
rip6_input(struct mbuf **mp, int *offp, int proto)
{
	struct ifnet *ifp;
	struct mbuf *m = *mp;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct inpcb *in6p;
	struct inpcb *last = NULL;
	struct mbuf *opts = NULL;
	struct sockaddr_in6 fromsa;
	struct epoch_tracker et;

	RIP6STAT_INC(rip6s_ipackets);

	init_sin6(&fromsa, m, 0); /* general init */

	ifp = m->m_pkthdr.rcvif;

	INP_INFO_RLOCK_ET(&V_ripcbinfo, et);
	CK_LIST_FOREACH(in6p, &V_ripcb, inp_list) {
		/* XXX inp locking */
		if ((in6p->inp_vflag & INP_IPV6) == 0)
			continue;
		if (in6p->inp_ip_p &&
		    in6p->inp_ip_p != proto)
			continue;
		if (!IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_laddr) &&
		    !IN6_ARE_ADDR_EQUAL(&in6p->in6p_laddr, &ip6->ip6_dst))
			continue;
		if (!IN6_IS_ADDR_UNSPECIFIED(&in6p->in6p_faddr) &&
		    !IN6_ARE_ADDR_EQUAL(&in6p->in6p_faddr, &ip6->ip6_src))
			continue;
		if (last != NULL) {
			struct mbuf *n = m_copym(m, 0, M_COPYALL, M_NOWAIT);

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
			/*
			 * Check AH/ESP integrity.
			 */
			if (IPSEC_ENABLED(ipv6)) {
				if (n != NULL &&
				    IPSEC_CHECK_POLICY(ipv6, n, last) != 0) {
					m_freem(n);
					/* Do not inject data into pcb. */
					n = NULL;
				}
			}
#endif /* IPSEC */
			if (n) {
				if (last->inp_flags & INP_CONTROLOPTS ||
				    last->inp_socket->so_options & SO_TIMESTAMP)
					ip6_savecontrol(last, n, &opts);
				/* strip intermediate headers */
				m_adj(n, *offp);
				if (sbappendaddr(&last->inp_socket->so_rcv,
						(struct sockaddr *)&fromsa,
						 n, opts) == 0) {
					m_freem(n);
					if (opts)
						m_freem(opts);
					RIP6STAT_INC(rip6s_fullsock);
				} else
					sorwakeup(last->inp_socket);
				opts = NULL;
			}
			INP_RUNLOCK(last);
			last = NULL;
		}
		INP_RLOCK(in6p);
		if (__predict_false(in6p->inp_flags2 & INP_FREED))
			goto skip_2;
		if (jailed_without_vnet(in6p->inp_cred)) {
			/*
			 * Allow raw socket in jail to receive multicast;
			 * assume process had PRIV_NETINET_RAW at attach,
			 * and fall through into normal filter path if so.
			 */
			if (!IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) &&
			    prison_check_ip6(in6p->inp_cred,
			    &ip6->ip6_dst) != 0)
				goto skip_2;
		}
		if (in6p->in6p_cksum != -1) {
			RIP6STAT_INC(rip6s_isum);
			if (in6_cksum(m, proto, *offp,
			    m->m_pkthdr.len - *offp)) {
				RIP6STAT_INC(rip6s_badsum);
				goto skip_2;
			}
		}
		/*
		 * If this raw socket has multicast state, and we
		 * have received a multicast, check if this socket
		 * should receive it, as multicast filtering is now
		 * the responsibility of the transport layer.
		 */
		if (in6p->in6p_moptions &&
		    IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
			/*
			 * If the incoming datagram is for MLD, allow it
			 * through unconditionally to the raw socket.
			 *
			 * Use the M_RTALERT_MLD flag to check for MLD
			 * traffic without having to inspect the mbuf chain
			 * more deeply, as all MLDv1/v2 host messages MUST
			 * contain the Router Alert option.
			 *
			 * In the case of MLDv1, we may not have explicitly
			 * joined the group, and may have set IFF_ALLMULTI
			 * on the interface. im6o_mc_filter() may discard
			 * control traffic we actually need to see.
			 *
			 * Userland multicast routing daemons should continue
			 * filter the control traffic appropriately.
			 */
			int blocked;

			blocked = MCAST_PASS;
			if ((m->m_flags & M_RTALERT_MLD) == 0) {
				struct sockaddr_in6 mcaddr;

				bzero(&mcaddr, sizeof(struct sockaddr_in6));
				mcaddr.sin6_len = sizeof(struct sockaddr_in6);
				mcaddr.sin6_family = AF_INET6;
				mcaddr.sin6_addr = ip6->ip6_dst;

				blocked = im6o_mc_filter(in6p->in6p_moptions,
				    ifp,
				    (struct sockaddr *)&mcaddr,
				    (struct sockaddr *)&fromsa);
			}
			if (blocked != MCAST_PASS) {
				IP6STAT_INC(ip6s_notmember);
				goto skip_2;
			}
		}
		last = in6p;
		continue;
skip_2:
		INP_RUNLOCK(in6p);
	}
	INP_INFO_RUNLOCK_ET(&V_ripcbinfo, et);
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	/*
	 * Check AH/ESP integrity.
	 */
	if (IPSEC_ENABLED(ipv6) && last != NULL &&
	    IPSEC_CHECK_POLICY(ipv6, m, last) != 0) {
		m_freem(m);
		IP6STAT_DEC(ip6s_delivered);
		/* Do not inject data into pcb. */
		INP_RUNLOCK(last);
	} else
#endif /* IPSEC */
	if (last != NULL) {
		if (last->inp_flags & INP_CONTROLOPTS ||
		    last->inp_socket->so_options & SO_TIMESTAMP)
			ip6_savecontrol(last, m, &opts);
		/* Strip intermediate headers. */
		m_adj(m, *offp);
		if (sbappendaddr(&last->inp_socket->so_rcv,
		    (struct sockaddr *)&fromsa, m, opts) == 0) {
			m_freem(m);
			if (opts)
				m_freem(opts);
			RIP6STAT_INC(rip6s_fullsock);
		} else
			sorwakeup(last->inp_socket);
		INP_RUNLOCK(last);
	} else {
		RIP6STAT_INC(rip6s_nosock);
		if (m->m_flags & M_MCAST)
			RIP6STAT_INC(rip6s_nosockmcast);
		if (proto == IPPROTO_NONE)
			m_freem(m);
		else
			icmp6_error(m, ICMP6_PARAM_PROB,
			    ICMP6_PARAMPROB_NEXTHEADER,
			    ip6_get_prevhdr(m, *offp));
		IP6STAT_DEC(ip6s_delivered);
	}
	return (IPPROTO_DONE);
}

void
rip6_ctlinput(int cmd, struct sockaddr *sa, void *d)
{
	struct ip6ctlparam *ip6cp = NULL;
	const struct sockaddr_in6 *sa6_src = NULL;
	void *cmdarg;
	struct inpcb *(*notify)(struct inpcb *, int) = in6_rtchange;

	if (sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(struct sockaddr_in6))
		return;

	if ((unsigned)cmd >= PRC_NCMDS)
		return;
	if (PRC_IS_REDIRECT(cmd))
		notify = in6_rtchange, d = NULL;
	else if (cmd == PRC_HOSTDEAD)
		d = NULL;
	else if (inet6ctlerrmap[cmd] == 0)
		return;

	/*
	 * If the parameter is from icmp6, decode it.
	 */
	if (d != NULL) {
		ip6cp = (struct ip6ctlparam *)d;
		cmdarg = ip6cp->ip6c_cmdarg;
		sa6_src = ip6cp->ip6c_src;
	} else {
		cmdarg = NULL;
		sa6_src = &sa6_any;
	}

	(void) in6_pcbnotify(&V_ripcbinfo, sa, 0,
	    (const struct sockaddr *)sa6_src, 0, cmd, cmdarg, notify);
}

/*
 * Generate IPv6 header and pass packet to ip6_output.  Tack on options user
 * may have setup with control call.
 */
int
rip6_output(struct mbuf *m, struct socket *so, ...)
{
	struct mbuf *control;
	struct m_tag *mtag;
	struct sockaddr_in6 *dstsock;
	struct ip6_hdr *ip6;
	struct inpcb *in6p;
	u_int	plen = m->m_pkthdr.len;
	int error = 0;
	struct ip6_pktopts opt, *optp;
	struct ifnet *oifp = NULL;
	int type = 0, code = 0;		/* for ICMPv6 output statistics only */
	int scope_ambiguous = 0;
	int use_defzone = 0;
	int hlim = 0;
	struct in6_addr in6a;
	va_list ap;

	va_start(ap, so);
	dstsock = va_arg(ap, struct sockaddr_in6 *);
	control = va_arg(ap, struct mbuf *);
	va_end(ap);

	in6p = sotoinpcb(so);
	INP_WLOCK(in6p);

	if (control != NULL) {
		if ((error = ip6_setpktopts(control, &opt,
		    in6p->in6p_outputopts, so->so_cred,
		    so->so_proto->pr_protocol)) != 0) {
			goto bad;
		}
		optp = &opt;
	} else
		optp = in6p->in6p_outputopts;

	/*
	 * Check and convert scope zone ID into internal form.
	 *
	 * XXX: we may still need to determine the zone later.
	 */
	if (!(so->so_state & SS_ISCONNECTED)) {
		if (!optp || !optp->ip6po_pktinfo ||
		    !optp->ip6po_pktinfo->ipi6_ifindex)
			use_defzone = V_ip6_use_defzone;
		if (dstsock->sin6_scope_id == 0 && !use_defzone)
			scope_ambiguous = 1;
		if ((error = sa6_embedscope(dstsock, use_defzone)) != 0)
			goto bad;
	}

	/*
	 * For an ICMPv6 packet, we should know its type and code to update
	 * statistics.
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
		code = icmp6->icmp6_code;
	}

	M_PREPEND(m, sizeof(*ip6), M_NOWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto bad;
	}
	ip6 = mtod(m, struct ip6_hdr *);

	/*
	 * Source address selection.
	 */
	error = in6_selectsrc_socket(dstsock, optp, in6p, so->so_cred,
	    scope_ambiguous, &in6a, &hlim);

	if (error)
		goto bad;
	error = prison_check_ip6(in6p->inp_cred, &in6a);
	if (error != 0)
		goto bad;
	ip6->ip6_src = in6a;

	ip6->ip6_dst = dstsock->sin6_addr;

	/*
	 * Fill in the rest of the IPv6 header fields.
	 */
	ip6->ip6_flow = (ip6->ip6_flow & ~IPV6_FLOWINFO_MASK) |
	    (in6p->inp_flow & IPV6_FLOWINFO_MASK);
	ip6->ip6_vfc = (ip6->ip6_vfc & ~IPV6_VERSION_MASK) |
	    (IPV6_VERSION & IPV6_VERSION_MASK);

	/*
	 * ip6_plen will be filled in ip6_output, so not fill it here.
	 */
	ip6->ip6_nxt = in6p->inp_ip_p;
	ip6->ip6_hlim = hlim;

	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6 ||
	    in6p->in6p_cksum != -1) {
		struct mbuf *n;
		int off;
		u_int16_t *p;

		/* Compute checksum. */
		if (so->so_proto->pr_protocol == IPPROTO_ICMPV6)
			off = offsetof(struct icmp6_hdr, icmp6_cksum);
		else
			off = in6p->in6p_cksum;
		if (plen < off + 1) {
			error = EINVAL;
			goto bad;
		}
		off += sizeof(struct ip6_hdr);

		n = m;
		while (n && n->m_len <= off) {
			off -= n->m_len;
			n = n->m_next;
		}
		if (!n)
			goto bad;
		p = (u_int16_t *)(mtod(n, caddr_t) + off);
		*p = 0;
		*p = in6_cksum(m, ip6->ip6_nxt, sizeof(*ip6), plen);
	}

	/*
	 * Send RA/RS messages to user land for protection, before sending
	 * them to rtadvd/rtsol.
	 */
	if ((send_sendso_input_hook != NULL) &&
	    so->so_proto->pr_protocol == IPPROTO_ICMPV6) {
		switch (type) {
		case ND_ROUTER_ADVERT:
		case ND_ROUTER_SOLICIT:
			mtag = m_tag_get(PACKET_TAG_ND_OUTGOING,
				sizeof(unsigned short), M_NOWAIT);
			if (mtag == NULL)
				goto bad;
			m_tag_prepend(m, mtag);
		}
	}

	error = ip6_output(m, optp, NULL, 0, in6p->in6p_moptions, &oifp, in6p);
	if (so->so_proto->pr_protocol == IPPROTO_ICMPV6) {
		if (oifp)
			icmp6_ifoutstat_inc(oifp, type, code);
		ICMP6STAT_INC(icp6s_outhist[type]);
	} else
		RIP6STAT_INC(rip6s_opackets);

	goto freectl;

 bad:
	if (m)
		m_freem(m);

 freectl:
	if (control != NULL) {
		ip6_clearpktopts(&opt, -1);
		m_freem(control);
	}
	INP_WUNLOCK(in6p);
	return (error);
}

/*
 * Raw IPv6 socket option processing.
 */
int
rip6_ctloutput(struct socket *so, struct sockopt *sopt)
{
	struct inpcb *inp;
	int error;

	if (sopt->sopt_level == IPPROTO_ICMPV6)
		/*
		 * XXX: is it better to call icmp6_ctloutput() directly
		 * from protosw?
		 */
		return (icmp6_ctloutput(so, sopt));
	else if (sopt->sopt_level != IPPROTO_IPV6) {
		if (sopt->sopt_level == SOL_SOCKET &&
		    sopt->sopt_name == SO_SETFIB) {
			inp = sotoinpcb(so);
			INP_WLOCK(inp);
			inp->inp_inc.inc_fibnum = so->so_fibnum;
			INP_WUNLOCK(inp);
			return (0);
		}
		return (EINVAL);
	}

	error = 0;

	switch (sopt->sopt_dir) {
	case SOPT_GET:
		switch (sopt->sopt_name) {
		case MRT6_INIT:
		case MRT6_DONE:
		case MRT6_ADD_MIF:
		case MRT6_DEL_MIF:
		case MRT6_ADD_MFC:
		case MRT6_DEL_MFC:
		case MRT6_PIM:
			error = ip6_mrouter_get ?  ip6_mrouter_get(so, sopt) :
			    EOPNOTSUPP;
			break;
		case IPV6_CHECKSUM:
			error = ip6_raw_ctloutput(so, sopt);
			break;
		default:
			error = ip6_ctloutput(so, sopt);
			break;
		}
		break;

	case SOPT_SET:
		switch (sopt->sopt_name) {
		case MRT6_INIT:
		case MRT6_DONE:
		case MRT6_ADD_MIF:
		case MRT6_DEL_MIF:
		case MRT6_ADD_MFC:
		case MRT6_DEL_MFC:
		case MRT6_PIM:
			error = ip6_mrouter_set ?  ip6_mrouter_set(so, sopt) :
			    EOPNOTSUPP;
			break;
		case IPV6_CHECKSUM:
			error = ip6_raw_ctloutput(so, sopt);
			break;
		default:
			error = ip6_ctloutput(so, sopt);
			break;
		}
		break;
	}

	return (error);
}

static int
rip6_attach(struct socket *so, int proto, struct thread *td)
{
	struct inpcb *inp;
	struct icmp6_filter *filter;
	int error;

	inp = sotoinpcb(so);
	KASSERT(inp == NULL, ("rip6_attach: inp != NULL"));

	error = priv_check(td, PRIV_NETINET_RAW);
	if (error)
		return (error);
	error = soreserve(so, rip_sendspace, rip_recvspace);
	if (error)
		return (error);
	filter = malloc(sizeof(struct icmp6_filter), M_PCB, M_NOWAIT);
	if (filter == NULL)
		return (ENOMEM);
	INP_INFO_WLOCK(&V_ripcbinfo);
	error = in_pcballoc(so, &V_ripcbinfo);
	if (error) {
		INP_INFO_WUNLOCK(&V_ripcbinfo);
		free(filter, M_PCB);
		return (error);
	}
	inp = (struct inpcb *)so->so_pcb;
	INP_INFO_WUNLOCK(&V_ripcbinfo);
	inp->inp_vflag |= INP_IPV6;
	inp->inp_ip_p = (long)proto;
	inp->in6p_hops = -1;	/* use kernel default */
	inp->in6p_cksum = -1;
	inp->in6p_icmp6filt = filter;
	ICMP6_FILTER_SETPASSALL(inp->in6p_icmp6filt);
	INP_WUNLOCK(inp);
	return (0);
}

static void
rip6_detach(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_detach: inp == NULL"));

	if (so == V_ip6_mrouter && ip6_mrouter_done)
		ip6_mrouter_done();
	/* xxx: RSVP */
	INP_INFO_WLOCK(&V_ripcbinfo);
	INP_WLOCK(inp);
	free(inp->in6p_icmp6filt, M_PCB);
	in_pcbdetach(inp);
	in_pcbfree(inp);
	INP_INFO_WUNLOCK(&V_ripcbinfo);
}

/* XXXRW: This can't ever be called. */
static void
rip6_abort(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_abort: inp == NULL"));

	soisdisconnected(so);
}

static void
rip6_close(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_close: inp == NULL"));

	soisdisconnected(so);
}

static int
rip6_disconnect(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_disconnect: inp == NULL"));

	if ((so->so_state & SS_ISCONNECTED) == 0)
		return (ENOTCONN);
	inp->in6p_faddr = in6addr_any;
	rip6_abort(so);
	return (0);
}

static int
rip6_bind(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct epoch_tracker et;
	struct inpcb *inp;
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *)nam;
	struct ifaddr *ifa = NULL;
	int error = 0;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_bind: inp == NULL"));

	if (nam->sa_len != sizeof(*addr))
		return (EINVAL);
	if ((error = prison_check_ip6(td->td_ucred, &addr->sin6_addr)) != 0)
		return (error);
	if (CK_STAILQ_EMPTY(&V_ifnet) || addr->sin6_family != AF_INET6)
		return (EADDRNOTAVAIL);
	if ((error = sa6_embedscope(addr, V_ip6_use_defzone)) != 0)
		return (error);

	NET_EPOCH_ENTER(et);
	if (!IN6_IS_ADDR_UNSPECIFIED(&addr->sin6_addr) &&
	    (ifa = ifa_ifwithaddr((struct sockaddr *)addr)) == NULL) {
		NET_EPOCH_EXIT(et);
		return (EADDRNOTAVAIL);
	}
	if (ifa != NULL &&
	    ((struct in6_ifaddr *)ifa)->ia6_flags &
	    (IN6_IFF_ANYCAST|IN6_IFF_NOTREADY|
	     IN6_IFF_DETACHED|IN6_IFF_DEPRECATED)) {
		NET_EPOCH_EXIT(et);
		return (EADDRNOTAVAIL);
	}
	NET_EPOCH_EXIT(et);
	INP_INFO_WLOCK(&V_ripcbinfo);
	INP_WLOCK(inp);
	inp->in6p_laddr = addr->sin6_addr;
	INP_WUNLOCK(inp);
	INP_INFO_WUNLOCK(&V_ripcbinfo);
	return (0);
}

static int
rip6_connect(struct socket *so, struct sockaddr *nam, struct thread *td)
{
	struct inpcb *inp;
	struct sockaddr_in6 *addr = (struct sockaddr_in6 *)nam;
	struct in6_addr in6a;
	int error = 0, scope_ambiguous = 0;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_connect: inp == NULL"));

	if (nam->sa_len != sizeof(*addr))
		return (EINVAL);
	if (CK_STAILQ_EMPTY(&V_ifnet))
		return (EADDRNOTAVAIL);
	if (addr->sin6_family != AF_INET6)
		return (EAFNOSUPPORT);

	/*
	 * Application should provide a proper zone ID or the use of default
	 * zone IDs should be enabled.  Unfortunately, some applications do
	 * not behave as it should, so we need a workaround.  Even if an
	 * appropriate ID is not determined, we'll see if we can determine
	 * the outgoing interface.  If we can, determine the zone ID based on
	 * the interface below.
	 */
	if (addr->sin6_scope_id == 0 && !V_ip6_use_defzone)
		scope_ambiguous = 1;
	if ((error = sa6_embedscope(addr, V_ip6_use_defzone)) != 0)
		return (error);

	INP_INFO_WLOCK(&V_ripcbinfo);
	INP_WLOCK(inp);
	/* Source address selection. XXX: need pcblookup? */
	error = in6_selectsrc_socket(addr, inp->in6p_outputopts,
	    inp, so->so_cred, scope_ambiguous, &in6a, NULL);
	if (error) {
		INP_WUNLOCK(inp);
		INP_INFO_WUNLOCK(&V_ripcbinfo);
		return (error);
	}

	inp->in6p_faddr = addr->sin6_addr;
	inp->in6p_laddr = in6a;
	soisconnected(so);
	INP_WUNLOCK(inp);
	INP_INFO_WUNLOCK(&V_ripcbinfo);
	return (0);
}

static int
rip6_shutdown(struct socket *so)
{
	struct inpcb *inp;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_shutdown: inp == NULL"));

	INP_WLOCK(inp);
	socantsendmore(so);
	INP_WUNLOCK(inp);
	return (0);
}

static int
rip6_send(struct socket *so, int flags, struct mbuf *m, struct sockaddr *nam,
    struct mbuf *control, struct thread *td)
{
	struct inpcb *inp;
	struct sockaddr_in6 tmp;
	struct sockaddr_in6 *dst;
	int ret;

	inp = sotoinpcb(so);
	KASSERT(inp != NULL, ("rip6_send: inp == NULL"));

	/* Always copy sockaddr to avoid overwrites. */
	/* Unlocked read. */
	if (so->so_state & SS_ISCONNECTED) {
		if (nam) {
			m_freem(m);
			return (EISCONN);
		}
		/* XXX */
		bzero(&tmp, sizeof(tmp));
		tmp.sin6_family = AF_INET6;
		tmp.sin6_len = sizeof(struct sockaddr_in6);
		INP_RLOCK(inp);
		bcopy(&inp->in6p_faddr, &tmp.sin6_addr,
		    sizeof(struct in6_addr));
		INP_RUNLOCK(inp);
		dst = &tmp;
	} else {
		if (nam == NULL) {
			m_freem(m);
			return (ENOTCONN);
		}
		if (nam->sa_len != sizeof(struct sockaddr_in6)) {
			m_freem(m);
			return (EINVAL);
		}
		tmp = *(struct sockaddr_in6 *)nam;
		dst = &tmp;

		if (dst->sin6_family == AF_UNSPEC) {
			/*
			 * XXX: we allow this case for backward
			 * compatibility to buggy applications that
			 * rely on old (and wrong) kernel behavior.
			 */
			log(LOG_INFO, "rip6 SEND: address family is "
			    "unspec. Assume AF_INET6\n");
			dst->sin6_family = AF_INET6;
		} else if (dst->sin6_family != AF_INET6) {
			m_freem(m);
			return(EAFNOSUPPORT);
		}
	}
	ret = rip6_output(m, so, dst, control);
	return (ret);
}

struct pr_usrreqs rip6_usrreqs = {
	.pru_abort =		rip6_abort,
	.pru_attach =		rip6_attach,
	.pru_bind =		rip6_bind,
	.pru_connect =		rip6_connect,
	.pru_control =		in6_control,
	.pru_detach =		rip6_detach,
	.pru_disconnect =	rip6_disconnect,
	.pru_peeraddr =		in6_getpeeraddr,
	.pru_send =		rip6_send,
	.pru_shutdown =		rip6_shutdown,
	.pru_sockaddr =		in6_getsockaddr,
	.pru_close =		rip6_close,
};
