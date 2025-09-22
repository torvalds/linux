/*	$OpenBSD: udp6_output.c,v 1.67 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: udp6_output.c,v 1.21 2001/02/07 11:51:54 itojun Exp $	*/

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
 * Copyright (c) 1982, 1986, 1989, 1993
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
 */

#include "pf.h"
#include "stoeplitz.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_var.h>
#if NPF > 0
#include <net/pfvar.h>
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_pcb.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>

/*
 * UDP protocol implementation.
 * Per RFC 768, August, 1980.
 */
int
udp6_output(struct inpcb *inp, struct mbuf *m, struct mbuf *addr6,
	struct mbuf *control)
{
	u_int32_t ulen = m->m_pkthdr.len;
	u_int32_t plen = sizeof(struct udphdr) + ulen;
	int error = 0, priv = 0, hlen, flags;
	struct ip6_hdr *ip6;
	struct udphdr *udp6;
	const struct in6_addr *laddr, *faddr;
	struct ip6_pktopts *optp, opt;
	struct sockaddr_in6 tmp, valid;
	struct proc *p = curproc;	/* XXX */
	u_short fport;

	if ((inp->inp_socket->so_state & SS_PRIV) != 0)
		priv = 1;
	if (control) {
		if ((error = ip6_setpktopts(control, &opt,
		    inp->inp_outputopts6, priv, IPPROTO_UDP)) != 0)
			goto release;
		optp = &opt;
	} else
		optp = inp->inp_outputopts6;

	if (addr6) {
		struct sockaddr_in6 *sin6;

		if ((error = in6_nam2sin6(addr6, &sin6)))
			goto release;
		if (sin6->sin6_port == 0) {
			error = EADDRNOTAVAIL;
			goto release;
		}
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			error = EADDRNOTAVAIL;
			goto release;
		}
		if (!IN6_IS_ADDR_UNSPECIFIED(&inp->inp_faddr6)) {
			error = EISCONN;
			goto release;
		}

		/* protect *sin6 from overwrites */
		tmp = *sin6;
		sin6 = &tmp;

		faddr = &sin6->sin6_addr;
		fport = sin6->sin6_port; /* allow 0 port */

		/* KAME hack: embed scopeid */
		if (in6_embedscope(&sin6->sin6_addr, sin6,
		    inp->inp_outputopts6, inp->inp_moptions6) != 0) {
			error = EINVAL;
			goto release;
		}

		error = in6_pcbselsrc(&laddr, sin6, inp, optp);
		if (error)
			goto release;

		if (inp->inp_lport == 0){
			error = in_pcbbind(inp, NULL, p);
			if (error)
				goto release;
		}

		if (!IN6_IS_ADDR_UNSPECIFIED(&inp->inp_laddr6) &&
		    !IN6_ARE_ADDR_EQUAL(&inp->inp_laddr6, laddr)) {
			valid.sin6_addr = *laddr;
			valid.sin6_port = inp->inp_lport;
			valid.sin6_scope_id = 0;
			valid.sin6_family = AF_INET6;
			valid.sin6_len = sizeof(valid);
			error = in6_pcbaddrisavail(inp, &valid, 0, p);
			if (error)
				goto release;
		}
	} else {
		if (IN6_IS_ADDR_UNSPECIFIED(&inp->inp_faddr6)) {
			error = ENOTCONN;
			goto release;
		}
		laddr = &inp->inp_laddr6;
		faddr = &inp->inp_faddr6;
		fport = inp->inp_fport;
	}

	hlen = sizeof(struct ip6_hdr);

	/*
	 * Calculate data length and get a mbuf
	 * for UDP and IP6 headers.
	 */
	M_PREPEND(m, hlen + sizeof(struct udphdr), M_DONTWAIT);
	if (m == NULL) {
		error = ENOBUFS;
		goto releaseopt;
	}

	/*
	 * Stuff checksum and output datagram.
	 */
	udp6 = (struct udphdr *)(mtod(m, caddr_t) + hlen);
	udp6->uh_sport = inp->inp_lport; /* lport is always set in the PCB */
	udp6->uh_dport = fport;
	if (plen <= 0xffff)
		udp6->uh_ulen = htons((u_short)plen);
	else
		udp6->uh_ulen = 0;
	udp6->uh_sum = 0;

	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow	= inp->inp_flowinfo & IPV6_FLOWINFO_MASK;
	ip6->ip6_vfc	&= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc	|= IPV6_VERSION;
#if 0	/* ip6_plen will be filled in ip6_output. */
	ip6->ip6_plen	= htons((u_short)plen);
#endif
	ip6->ip6_nxt	= IPPROTO_UDP;
	ip6->ip6_hlim	= in6_selecthlim(inp);
	ip6->ip6_src	= *laddr;
	ip6->ip6_dst	= *faddr;

	m->m_pkthdr.csum_flags |= M_UDP_CSUM_OUT;

	flags = 0;
	if (inp->inp_flags & IN6P_MINMTU)
		flags |= IPV6_MINMTU;

	udpstat_inc(udps_opackets);

	/* force routing table */
	m->m_pkthdr.ph_rtableid = inp->inp_rtableid;

	if (inp->inp_socket->so_state & SS_ISCONNECTED) {
#if NPF > 0
		pf_mbuf_link_inpcb(m, inp);
#endif
#if NSTOEPLITZ > 0
		m->m_pkthdr.ph_flowid = inp->inp_flowid;
		SET(m->m_pkthdr.csum_flags, M_FLOWID);
#endif
	}

	error = ip6_output(m, optp, &inp->inp_route,
	    flags, inp->inp_moptions6, &inp->inp_seclevel);
	goto releaseopt;

release:
	m_freem(m);

releaseopt:
	if (control) {
		ip6_clearpktopts(&opt, -1);
		m_freem(control);
	}
	return (error);
}
