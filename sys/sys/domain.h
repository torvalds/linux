/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)domain.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _SYS_DOMAIN_H_
#define _SYS_DOMAIN_H_

/*
 * Structure per communications domain.
 */

/*
 * Forward structure declarations for function prototypes [sic].
 */
struct	mbuf;
struct	ifnet;
struct	socket;

struct domain {
	int	dom_family;		/* AF_xxx */
	char	*dom_name;
	void	(*dom_init)		/* initialize domain data structures */
		(void);
	void	(*dom_destroy)		/* cleanup structures / state */
		(void);
	int	(*dom_externalize)	/* externalize access rights */
		(struct mbuf *, struct mbuf **, int);
	void	(*dom_dispose)		/* dispose of internalized rights */
		(struct socket *);
	struct	protosw *dom_protosw, *dom_protoswNPROTOSW;
	struct	domain *dom_next;
	int	(*dom_rtattach)		/* initialize routing table */
		(void **, int);
	int	(*dom_rtdetach)		/* clean up routing table */
		(void **, int);
	void	*(*dom_ifattach)(struct ifnet *);
	void	(*dom_ifdetach)(struct ifnet *, void *);
	int	(*dom_ifmtu)(struct ifnet *);
					/* af-dependent data on ifnet */
};

#ifdef _KERNEL
extern int	domain_init_status;
extern struct	domain *domains;
void		domain_add(void *);
void		domain_init(void *);
#ifdef VIMAGE
void		vnet_domain_init(void *);
void		vnet_domain_uninit(void *);
#endif

#define	DOMAIN_SET(name)						\
	SYSINIT(domain_add_ ## name, SI_SUB_PROTO_DOMAIN,		\
	    SI_ORDER_FIRST, domain_add, & name ## domain);		\
	SYSINIT(domain_init_ ## name, SI_SUB_PROTO_DOMAIN,		\
	    SI_ORDER_SECOND, domain_init, & name ## domain);
#ifdef VIMAGE
#define	VNET_DOMAIN_SET(name)						\
	SYSINIT(domain_add_ ## name, SI_SUB_PROTO_DOMAIN,		\
	    SI_ORDER_FIRST, domain_add, & name ## domain);		\
	VNET_SYSINIT(vnet_domain_init_ ## name, SI_SUB_PROTO_DOMAIN,	\
	    SI_ORDER_SECOND, vnet_domain_init, & name ## domain);	\
	VNET_SYSUNINIT(vnet_domain_uninit_ ## name,			\
	    SI_SUB_PROTO_DOMAIN, SI_ORDER_SECOND, vnet_domain_uninit,	\
	    & name ## domain)
#else /* !VIMAGE */
#define	VNET_DOMAIN_SET(name)	DOMAIN_SET(name)
#endif /* VIMAGE */

#endif /* _KERNEL */

#endif /* !_SYS_DOMAIN_H_ */
