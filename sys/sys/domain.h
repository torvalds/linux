/*	$OpenBSD: domain.h,v 1.25 2024/10/26 05:39:03 jsg Exp $	*/
/*	$NetBSD: domain.h,v 1.10 1996/02/09 18:25:07 christos Exp $	*/

/*
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
 */

/*
 * Structure per communications domain.
 */

#ifndef	_SOCKLEN_T_DEFINED_
#define	_SOCKLEN_T_DEFINED_
typedef	__socklen_t	socklen_t;	/* length type for network syscalls */
#endif

/*
 * Forward structure declarations for function prototypes [sic].
 */
struct	mbuf;

struct domain {
	int	dom_family;		/* AF_xxx */
	const	char *dom_name;
	void	(*dom_init)(void);	/* initialize domain data structures */
					/* externalize access rights */
	int	(*dom_externalize)(struct mbuf *, socklen_t, int);
					/* dispose of internalized rights */
	void	(*dom_dispose)(struct mbuf *);
	const struct	protosw *dom_protosw, *dom_protoswNPROTOSW;
					/* initialize routing table */
	unsigned int	dom_sasize;	/* size of sockaddr structure */
	unsigned int	dom_rtoffset;	/* offset of the key, in bytes */
	unsigned int	dom_maxplen;	/* maximum prefix length, in bits */
};

#ifdef _KERNEL
void domaininit(void);

extern const struct domain *const domains[];
extern const struct domain inet6domain;
extern const struct domain inetdomain;
extern const struct domain mplsdomain;
extern const struct domain pfkeydomain;
extern const struct domain routedomain;
extern const struct domain unixdomain;
#endif /* _KERNEL */
