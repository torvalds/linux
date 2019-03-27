/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.
 * Copyright (c) 2005 Andre Oppermann, Internet Business Solutions AG.
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
 * $FreeBSD$
 */

#ifndef _NETINET_IP_OPTIONS_H_
#define	_NETINET_IP_OPTIONS_H_

struct ipoptrt {
        struct  in_addr dst;                    /* final destination */
        char    nop;                            /* one NOP to align */
        char    srcopt[IPOPT_OFFSET + 1];       /* OPTVAL, OLEN and OFFSET */
        struct  in_addr route[MAX_IPOPTLEN/sizeof(struct in_addr)];
};

struct ipopt_tag {
	struct	m_tag tag;			/* m_tag */
	int	ip_nhops;
	struct	ipoptrt ip_srcrt;
};

VNET_DECLARE(int, ip_doopts);		/* process or ignore IP options */
#define	V_ip_doopts	VNET(ip_doopts)

int		 ip_checkrouteralert(struct mbuf *);
int		 ip_dooptions(struct mbuf *, int);
struct mbuf	*ip_insertoptions(struct mbuf *, struct mbuf *, int *);
int		 ip_optcopy(struct ip *, struct ip *);
int		 ip_pcbopts(struct inpcb *, int, struct mbuf *);
void		 ip_stripoptions(struct mbuf *);
struct mbuf	*ip_srcroute(struct mbuf *);

#endif /* !_NETINET_IP_OPTIONS_H_ */
