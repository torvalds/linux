/*	$FreeBSD$	*/
/*	$KAME: ip_encap.h,v 1.7 2000/03/25 07:23:37 sumikawa Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * Copyright (c) 2018 Andrey V. Elsukov <ae@FreeBSD.org>
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

#ifndef _NETINET_IP_ENCAP_H_
#define _NETINET_IP_ENCAP_H_

#ifdef _KERNEL

int	encap4_input(struct mbuf **, int *, int);
int	encap6_input(struct mbuf **, int *, int);

typedef int (*encap_lookup_t)(const struct mbuf *, int, int, void **);
typedef int (*encap_check_t)(const struct mbuf *, int, int, void *);
typedef int (*encap_input_t)(struct mbuf *, int, int, void *);
typedef void (*encap_srcaddr_t)(void *, const struct sockaddr *, int);

struct encap_config {
	int		proto;		/* protocol */
	int		min_length;	/* minimum packet length */
	int		max_hdrsize;	/* maximum header size */
	int		exact_match;	/* a packet is exactly matched */
#define	ENCAP_DRV_LOOKUP	0x7fffffff

	encap_lookup_t	lookup;
	encap_check_t	check;
	encap_input_t	input;

	void		*pad[3];
};

struct encaptab;
struct srcaddrtab;

const struct encaptab *ip_encap_attach(const struct encap_config *,
    void *arg, int mflags);
const struct encaptab *ip6_encap_attach(const struct encap_config *,
    void *arg, int mflags);

const struct srcaddrtab *ip_encap_register_srcaddr(encap_srcaddr_t,
    void *arg, int mflags);
const struct srcaddrtab *ip6_encap_register_srcaddr(encap_srcaddr_t,
    void *arg, int mflags);

int ip_encap_unregister_srcaddr(const struct srcaddrtab *);
int ip6_encap_unregister_srcaddr(const struct srcaddrtab *);
int ip_encap_detach(const struct encaptab *);
int ip6_encap_detach(const struct encaptab *);
#endif

#endif /*_NETINET_IP_ENCAP_H_*/
