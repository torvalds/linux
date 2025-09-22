/*	$OpenBSD: raw_ip.c,v 1.167 2025/07/08 00:47:41 jsg Exp $	*/
/*	$NetBSD: raw_ip.c,v 1.25 1996/02/18 18:58:33 christos Exp $	*/

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
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 *
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 *	This product includes software developed at the Information
 *	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/protosw.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_mroute.h>
#include <netinet/ip_var.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_icmp.h>

#include <net/pfvar.h>

#include "pf.h"

struct inpcbtable rawcbtable;

/*
 * Nominal space allocated to a raw ip socket.
 */
#define	RIPSNDQ		8192
#define	RIPRCVQ		8192

/*
 * Raw interface to IP protocol.
 */

const struct pr_usrreqs rip_usrreqs = {
	.pru_attach	= rip_attach,
	.pru_detach	= rip_detach,
	.pru_bind	= rip_bind,
	.pru_connect	= rip_connect,
	.pru_disconnect	= rip_disconnect,
	.pru_shutdown	= rip_shutdown,
	.pru_send	= rip_send,
	.pru_control	= in_control,
	.pru_sockaddr	= in_sockaddr,
	.pru_peeraddr	= in_peeraddr,
};

void    rip_sbappend(struct inpcb *, struct mbuf *, struct ip *,
	    struct sockaddr_in *);

/*
 * Initialize raw connection block q.
 */
void
rip_init(void)
{
	in_pcbinit(&rawcbtable, 1);
}

int
rip_input(struct mbuf **mp, int *offp, int proto, int af, struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct ip *ip = mtod(m, struct ip *);
	struct inpcb_iterator iter = { .inp_table = NULL };
	struct inpcb *inp, *last;
	struct in_addr *key;
	struct sockaddr_in ripsrc;

	KASSERT(af == AF_INET);

	memset(&ripsrc, 0, sizeof(ripsrc));
	ripsrc.sin_family = AF_INET;
	ripsrc.sin_len = sizeof(ripsrc);
	ripsrc.sin_addr = ip->ip_src;

	key = &ip->ip_dst;
#if NPF > 0
	if (m->m_pkthdr.pf.flags & PF_TAG_DIVERTED) {
		struct pf_divert *divert;

		divert = pf_find_divert(m);
		KASSERT(divert != NULL);
		switch (divert->type) {
		case PF_DIVERT_TO:
			key = &divert->addr.v4;
			break;
		case PF_DIVERT_REPLY:
			break;
		default:
			panic("%s: unknown divert type %d, mbuf %p, divert %p",
			    __func__, divert->type, m, divert);
		}
	}
#endif
	mtx_enter(&rawcbtable.inpt_mtx);
	last = inp = NULL;
	while ((inp = in_pcb_iterator(&rawcbtable, inp, &iter)) != NULL) {
		KASSERT(!ISSET(inp->inp_flags, INP_IPV6));

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

		if (inp->inp_ip.ip_p && inp->inp_ip.ip_p != ip->ip_p)
			continue;
		if (inp->inp_laddr.s_addr &&
		    inp->inp_laddr.s_addr != key->s_addr)
			continue;
		if (inp->inp_faddr.s_addr &&
		    inp->inp_faddr.s_addr != ip->ip_src.s_addr)
			continue;

		if (last != NULL) {
			struct mbuf *n;

			mtx_leave(&rawcbtable.inpt_mtx);

			n = m_copym(m, 0, M_COPYALL, M_NOWAIT);
			if (n != NULL)
				rip_sbappend(last, n, ip, &ripsrc);
			in_pcbunref(last);

			mtx_enter(&rawcbtable.inpt_mtx);
		}
		last = in_pcbref(inp);
	}
	mtx_leave(&rawcbtable.inpt_mtx);

	if (last == NULL) {
		struct counters_ref ref;
		uint64_t *counters;

		if (ip->ip_p == IPPROTO_ICMP) {
			m_freem(m);
		} else {
			icmp_error(m, ICMP_UNREACH, ICMP_UNREACH_PROTOCOL,
			    0, 0);
		}
		counters = counters_enter(&ref, ipcounters);
		counters[ips_noproto]++;
		counters[ips_delivered]--;
		counters_leave(&ref, ipcounters);

		return IPPROTO_DONE;
	}

	rip_sbappend(last, m, ip, &ripsrc);
	in_pcbunref(last);

	return IPPROTO_DONE;
}

void
rip_sbappend(struct inpcb *inp, struct mbuf *m, struct ip *ip,
    struct sockaddr_in *ripsrc)
{
	struct socket *so = inp->inp_socket;
	struct mbuf *opts = NULL;
	int ret = 0;

	if (inp->inp_flags & INP_CONTROLOPTS || so->so_options & SO_TIMESTAMP)
		ip_savecontrol(inp, &opts, ip, m);

	mtx_enter(&so->so_rcv.sb_mtx);
	if (!ISSET(inp->inp_socket->so_rcv.sb_state, SS_CANTRCVMORE))
		ret = sbappendaddr(&so->so_rcv, sintosa(ripsrc), m, opts);
	mtx_leave(&so->so_rcv.sb_mtx);

	if (ret == 0) {
		m_freem(m);
		m_freem(opts);
		ipstat_inc(ips_noproto);
	} else
		sorwakeup(so);
}

/*
 * Generate IP header and pass packet to ip_output.
 * Tack on options user may have setup with control call.
 */
int
rip_output(struct mbuf *m, struct socket *so, struct sockaddr *dstaddr,
    struct mbuf *control)
{
	struct sockaddr_in *dst = satosin(dstaddr);
	struct ip *ip;
	struct inpcb *inp;
	int flags, error;

	inp = sotoinpcb(so);
	flags = IP_ALLOWBROADCAST;

	/*
	 * If the user handed us a complete IP packet, use it.
	 * Otherwise, allocate an mbuf for a header and fill it in.
	 */
	if ((inp->inp_flags & INP_HDRINCL) == 0) {
		if ((m->m_pkthdr.len + sizeof(struct ip)) > IP_MAXPACKET) {
			m_freem(m);
			return (EMSGSIZE);
		}
		M_PREPEND(m, sizeof(struct ip), M_DONTWAIT);
		if (!m)
			return (ENOBUFS);
		ip = mtod(m, struct ip *);
		ip->ip_tos = inp->inp_ip.ip_tos;
		ip->ip_off = htons(0);
		ip->ip_p = inp->inp_ip.ip_p;
		ip->ip_len = htons(m->m_pkthdr.len);
		ip->ip_src.s_addr = INADDR_ANY;
		ip->ip_dst = dst->sin_addr;
		ip->ip_ttl = inp->inp_ip.ip_ttl ? inp->inp_ip.ip_ttl : MAXTTL;
	} else {
		if (m->m_pkthdr.len > IP_MAXPACKET) {
			m_freem(m);
			return (EMSGSIZE);
		}

		m = rip_chkhdr(m, inp->inp_options);
		if (m == NULL)
			return (EINVAL);

		ip = mtod(m, struct ip *);
		if (ip->ip_id == 0)
			ip->ip_id = htons(ip_randomid());
		dst->sin_addr = ip->ip_dst;

		/* XXX prevent ip_output from overwriting header fields */
		flags |= IP_RAWOUTPUT;
		ipstat_inc(ips_rawout);
	}

	if (ip->ip_src.s_addr == INADDR_ANY) {
		error = in_pcbselsrc(&ip->ip_src, dst, inp);
		if (error != 0)
			return (error);
	}

#ifdef INET6
	/*
	 * A thought:  Even though raw IP shouldn't be able to set IPv6
	 *             multicast options, if it does, the last parameter to
	 *             ip_output should be guarded against v6/v4 problems.
	 */
#endif
	/* force routing table */
	m->m_pkthdr.ph_rtableid = inp->inp_rtableid;

#if NPF > 0
	if (inp->inp_socket->so_state & SS_ISCONNECTED &&
	    ip->ip_p != IPPROTO_ICMP)
		pf_mbuf_link_inpcb(m, inp);
#endif

	error = ip_output(m, inp->inp_options, &inp->inp_route, flags,
	    inp->inp_moptions, &inp->inp_seclevel, 0);
	return (error);
}

struct mbuf *
rip_chkhdr(struct mbuf *m, struct mbuf *options)
{
	struct ip *ip;
	int hlen, opt, optlen, cnt;
	u_char *cp;

	if (m->m_pkthdr.len < sizeof(struct ip)) {
		m_freem(m);
		return NULL;
	}

	m = m_pullup(m, sizeof (struct ip));
	if (m == NULL)
		return NULL;

	ip = mtod(m, struct ip *);
	hlen = ip->ip_hl << 2;

	/* Don't allow packet length sizes that will crash. */
	if (hlen < sizeof (struct ip) ||
	    ntohs(ip->ip_len) < hlen ||
	    ntohs(ip->ip_len) != m->m_pkthdr.len) {
		m_freem(m);
		return NULL;
	}
	m = m_pullup(m, hlen);
	if (m == NULL)
		return NULL;

	ip = mtod(m, struct ip *);

	if (ip->ip_v != IPVERSION) {
		m_freem(m);
		return NULL;
	}

	/*
	 * Don't allow both user specified and setsockopt options.
	 * If options are present verify them.
	 */
	if (hlen != sizeof(struct ip)) {
		if (options) {
			m_freem(m);
			return NULL;
		} else {
			cp = (u_char *)(ip + 1);
			cnt = hlen - sizeof(struct ip);
			for (; cnt > 0; cnt -= optlen, cp += optlen) {
				opt = cp[IPOPT_OPTVAL];
				if (opt == IPOPT_EOL)
					break;
				if (opt == IPOPT_NOP)
					optlen = 1;
				else {
					if (cnt < IPOPT_OLEN + sizeof(*cp)) {
						m_freem(m);
						return NULL;
					}
					optlen = cp[IPOPT_OLEN];
					if (optlen < IPOPT_OLEN + sizeof(*cp) ||
					    optlen > cnt) {
						m_freem(m);
						return NULL;
					}
				}
			}
		}
	}

	return m;
}

/*
 * Raw IP socket option processing.
 */
int
rip_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
	struct inpcb *inp = sotoinpcb(so);
	int error;

	if (level != IPPROTO_IP)
		return (EINVAL);

	switch (optname) {

	case IP_HDRINCL:
		error = 0;
		if (op == PRCO_SETOPT) {
			if (m == NULL || m->m_len < sizeof (int))
				error = EINVAL;
			else if (*mtod(m, int *))
				inp->inp_flags |= INP_HDRINCL;
			else
				inp->inp_flags &= ~INP_HDRINCL;
		} else {
			m->m_len = sizeof(int);
			*mtod(m, int *) = inp->inp_flags & INP_HDRINCL;
		}
		return (error);

	case MRT_INIT:
	case MRT_DONE:
	case MRT_ADD_VIF:
	case MRT_DEL_VIF:
	case MRT_ADD_MFC:
	case MRT_DEL_MFC:
	case MRT_VERSION:
	case MRT_ASSERT:
	case MRT_API_SUPPORT:
	case MRT_API_CONFIG:
#ifdef MROUTING
		switch (op) {
		case PRCO_SETOPT:
			error = ip_mrouter_set(so, optname, m);
			break;
		case PRCO_GETOPT:
			error = ip_mrouter_get(so, optname, m);
			break;
		default:
			error = EINVAL;
			break;
		}
		return (error);
#else
		return (EOPNOTSUPP);
#endif
	}
	return (ip_ctloutput(op, so, level, optname, m));
}

u_long	rip_sendspace = RIPSNDQ;
u_long	rip_recvspace = RIPRCVQ;

int
rip_attach(struct socket *so, int proto, int wait)
{
	struct inpcb *inp;
	int error;

	if (so->so_pcb)
		panic("rip_attach");
	if ((so->so_state & SS_PRIV) == 0)
		return EACCES;
	if (proto < 0 || proto >= IPPROTO_MAX)
		return EPROTONOSUPPORT;

	if ((error = soreserve(so, rip_sendspace, rip_recvspace)))
		return error;
	if ((error = in_pcballoc(so, &rawcbtable, wait)))
		return error;
	inp = sotoinpcb(so);
	inp->inp_ip.ip_p = proto;
	return 0;
}

int
rip_detach(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);

	soassertlocked(so);

	if (inp == NULL)
		return (EINVAL);

#ifdef MROUTING
	if (so == ip_mrouter[inp->inp_rtableid])
		ip_mrouter_done(so);
#endif
	in_pcbdetach(inp);

	return (0);
}

int
rip_bind(struct socket *so, struct mbuf *nam, struct proc *p)
{
	struct inpcb *inp = sotoinpcb(so);
	struct sockaddr_in *addr;
	int error;

	soassertlocked(so);

	if ((error = in_nam2sin(nam, &addr)))
		return (error);

	if (!((so->so_options & SO_BINDANY) ||
	    addr->sin_addr.s_addr == INADDR_ANY ||
	    addr->sin_addr.s_addr == INADDR_BROADCAST ||
	    in_broadcast(addr->sin_addr, inp->inp_rtableid) ||
	    ifa_ifwithaddr(sintosa(addr), inp->inp_rtableid)))
		return (EADDRNOTAVAIL);

	mtx_enter(&rawcbtable.inpt_mtx);
	inp->inp_laddr = addr->sin_addr;
	mtx_leave(&rawcbtable.inpt_mtx);

	return (0);
}

int
rip_connect(struct socket *so, struct mbuf *nam)
{
	struct inpcb *inp = sotoinpcb(so);
	struct sockaddr_in *addr;
	int error;

	soassertlocked(so);

	if ((error = in_nam2sin(nam, &addr)))
		return (error);

	mtx_enter(&rawcbtable.inpt_mtx);
	inp->inp_faddr = addr->sin_addr;
	mtx_leave(&rawcbtable.inpt_mtx);
	soisconnected(so);

	return (0);
}

int
rip_disconnect(struct socket *so)
{
	struct inpcb *inp = sotoinpcb(so);

	soassertlocked(so);

	if ((so->so_state & SS_ISCONNECTED) == 0)
		return (ENOTCONN);

	soisdisconnected(so);
	mtx_enter(&rawcbtable.inpt_mtx);
	inp->inp_faddr.s_addr = INADDR_ANY;
	mtx_leave(&rawcbtable.inpt_mtx);

	return (0);
}

int
rip_shutdown(struct socket *so)
{
	/*
	 * Mark the connection as being incapable of further input.
	 */

	soassertlocked(so);
	socantsendmore(so);

	return (0);
}

int
rip_send(struct socket *so, struct mbuf *m, struct mbuf *nam,
    struct mbuf *control)
{
	struct inpcb *inp = sotoinpcb(so);
	struct sockaddr_in dst;
	int error;

	soassertlocked(so);

	/*
	 * Ship a packet out.  The appropriate raw output
	 * routine handles any massaging necessary.
	 */
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(dst);
	if (so->so_state & SS_ISCONNECTED) {
		if (nam) {
			error = EISCONN;
			goto out;
		}
		dst.sin_addr = inp->inp_faddr;
	} else {
		struct sockaddr_in *addr;

		if (nam == NULL) {
			error = ENOTCONN;
			goto out;
		}
		if ((error = in_nam2sin(nam, &addr)))
			goto out;
		dst.sin_addr = addr->sin_addr;
	}
#ifdef IPSEC
	/* XXX Find an IPsec TDB */
#endif
	error = rip_output(m, so, sintosa(&dst), NULL);
	m = NULL;

out:
	m_freem(control);
	m_freem(m);

	return (error);
}
