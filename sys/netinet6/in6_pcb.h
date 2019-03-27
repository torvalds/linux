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
 *
 *	$KAME: in6_pcb.h,v 1.13 2001/02/06 09:16:53 itojun Exp $
 */

/*-
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *	@(#)in_pcb.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _NETINET6_IN6_PCB_H_
#define	_NETINET6_IN6_PCB_H_

#ifdef _KERNEL
#define	satosin6(sa)	((struct sockaddr_in6 *)(sa))
#define	sin6tosa(sin6)	((struct sockaddr *)(sin6))
#define	ifatoia6(ifa)	((struct in6_ifaddr *)(ifa))

struct	inpcbgroup *
	in6_pcbgroup_byhash(struct inpcbinfo *, u_int, uint32_t);
struct	inpcbgroup *
	in6_pcbgroup_byinpcb(struct inpcb *);
struct inpcbgroup *
	in6_pcbgroup_bymbuf(struct inpcbinfo *, struct mbuf *);
struct	inpcbgroup *
	in6_pcbgroup_bytuple(struct inpcbinfo *, const struct in6_addr *,
	    u_short, const struct in6_addr *, u_short);

void	in6_pcbpurgeif0(struct inpcbinfo *, struct ifnet *);
void	in6_losing(struct inpcb *);
int	in6_pcbbind(struct inpcb *, struct sockaddr *, struct ucred *);
int	in6_pcbconnect(struct inpcb *, struct sockaddr *, struct ucred *);
int	in6_pcbconnect_mbuf(struct inpcb *, struct sockaddr *,
	    struct ucred *, struct mbuf *);
void	in6_pcbdisconnect(struct inpcb *);
struct	inpcb *
	in6_pcblookup_local(struct inpcbinfo *,
				 struct in6_addr *, u_short, int,
				 struct ucred *);
struct	inpcb *
	in6_pcblookup(struct inpcbinfo *, struct in6_addr *,
			   u_int, struct in6_addr *, u_int, int,
			   struct ifnet *);
struct	inpcb *
	in6_pcblookup_mbuf(struct inpcbinfo *, struct in6_addr *,
			   u_int, struct in6_addr *, u_int, int,
			   struct ifnet *ifp, struct mbuf *);
void	in6_pcbnotify(struct inpcbinfo *, struct sockaddr *,
			   u_int, const struct sockaddr *, u_int, int, void *,
			   struct inpcb *(*)(struct inpcb *, int));
struct inpcb *
	in6_rtchange(struct inpcb *, int);
struct sockaddr *
	in6_sockaddr(in_port_t port, struct in6_addr *addr_p);
struct sockaddr *
	in6_v4mapsin6_sockaddr(in_port_t port, struct in_addr *addr_p);
int	in6_getpeeraddr(struct socket *so, struct sockaddr **nam);
int	in6_getsockaddr(struct socket *so, struct sockaddr **nam);
int	in6_mapped_sockaddr(struct socket *so, struct sockaddr **nam);
int	in6_mapped_peeraddr(struct socket *so, struct sockaddr **nam);
int	in6_selecthlim(struct in6pcb *, struct ifnet *);
int	in6_pcbsetport(struct in6_addr *, struct inpcb *, struct ucred *);
void	init_sin6(struct sockaddr_in6 *sin6, struct mbuf *m, int);
#endif /* _KERNEL */

#endif /* !_NETINET6_IN6_PCB_H_ */
