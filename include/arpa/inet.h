/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND ISC)
 *
 * ++Copyright++ 1983, 1993
 * -
 * Copyright (c) 1983, 1993
 *    The Regents of the University of California.  All rights reserved.
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
 * -
 * Portions Copyright (c) 1993 by Digital Equipment Corporation.
 * 
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies, and that
 * the name of Digital Equipment Corporation not be used in advertising or
 * publicity pertaining to distribution of the document or software without
 * specific, written prior permission.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
 * CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 * -
 * --Copyright--
 */

/*%
 *	@(#)inet.h	8.1 (Berkeley) 6/2/93
 *	$Id: inet.h,v 1.3 2005/04/27 04:56:16 sra Exp $
 * $FreeBSD$
 */

#ifndef _ARPA_INET_H_
#define	_ARPA_INET_H_

/* External definitions for functions in inet(3). */

#include <sys/cdefs.h>
#include <sys/_types.h>

/* Required for byteorder(3) functions. */
#include <machine/endian.h>

#define	INET_ADDRSTRLEN		16
#define	INET6_ADDRSTRLEN	46

#ifndef _UINT16_T_DECLARED
typedef	__uint16_t	uint16_t;
#define	_UINT16_T_DECLARED
#endif

#ifndef _UINT32_T_DECLARED
typedef	__uint32_t	uint32_t;
#define	_UINT32_T_DECLARED
#endif

#ifndef _IN_ADDR_T_DECLARED
typedef	uint32_t	in_addr_t;
#define	_IN_ADDR_T_DECLARED
#endif

#ifndef _IN_PORT_T_DECLARED
typedef	uint16_t	in_port_t;
#define	_IN_PORT_T_DECLARED
#endif

#if __BSD_VISIBLE
#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif
#endif

/*
 * XXX socklen_t is used by a POSIX.1-2001 interface, but not required by
 * POSIX.1-2001.
 */
#ifndef _SOCKLEN_T_DECLARED
typedef	__socklen_t	socklen_t;
#define	_SOCKLEN_T_DECLARED
#endif

#ifndef _STRUCT_IN_ADDR_DECLARED
struct in_addr {
	in_addr_t s_addr;
};
#define	_STRUCT_IN_ADDR_DECLARED
#endif

/* XXX all new diversions!! argh!! */
#if __BSD_VISIBLE
#define	inet_addr		__inet_addr
#define	inet_aton		__inet_aton
#define	inet_lnaof		__inet_lnaof
#define	inet_makeaddr		__inet_makeaddr
#define	inet_neta		__inet_neta
#define	inet_netof		__inet_netof
#define	inet_network		__inet_network
#define	inet_net_ntop		__inet_net_ntop
#define	inet_net_pton		__inet_net_pton
#define	inet_cidr_ntop		__inet_cidr_ntop
#define	inet_cidr_pton		__inet_cidr_pton
#define	inet_ntoa		__inet_ntoa
#define	inet_ntoa_r		__inet_ntoa_r
#define	inet_pton		__inet_pton
#define	inet_ntop		__inet_ntop
#define	inet_nsap_addr		__inet_nsap_addr
#define	inet_nsap_ntoa		__inet_nsap_ntoa
#endif /* __BSD_VISIBLE */

__BEGIN_DECLS
#ifndef _BYTEORDER_PROTOTYPED
#define	_BYTEORDER_PROTOTYPED
uint32_t	 htonl(uint32_t);
uint16_t	 htons(uint16_t);
uint32_t	 ntohl(uint32_t);
uint16_t	 ntohs(uint16_t);
#endif

in_addr_t	 inet_addr(const char *);
/*const*/ char	*inet_ntoa(struct in_addr);
const char	*inet_ntop(int, const void * __restrict, char * __restrict,
		    socklen_t);
int		 inet_pton(int, const char * __restrict, void * __restrict);

#if __BSD_VISIBLE
int		 inet_aton(const char *, struct in_addr *);
in_addr_t	 inet_lnaof(struct in_addr);
struct in_addr	 inet_makeaddr(in_addr_t, in_addr_t);
char *		 inet_neta(in_addr_t, char *, size_t);
in_addr_t	 inet_netof(struct in_addr);
in_addr_t	 inet_network(const char *);
char		*inet_net_ntop(int, const void *, int, char *, size_t);
int		 inet_net_pton(int, const char *, void *, size_t);
char		*inet_ntoa_r(struct in_addr, char *buf, socklen_t size);
char		*inet_cidr_ntop(int, const void *, int, char *, size_t);
int		 inet_cidr_pton(int, const char *, void *, int *);
unsigned	 inet_nsap_addr(const char *, unsigned char *, int);
char		*inet_nsap_ntoa(int, const unsigned char *, char *);
#endif /* __BSD_VISIBLE */
__END_DECLS

#ifndef _BYTEORDER_FUNC_DEFINED
#define	_BYTEORDER_FUNC_DEFINED
#define	htonl(x)	__htonl(x)
#define	htons(x)	__htons(x)
#define	ntohl(x)	__ntohl(x)
#define	ntohs(x)	__ntohs(x)
#endif

#endif /* !_ARPA_INET_H_ */

/*! \file */
