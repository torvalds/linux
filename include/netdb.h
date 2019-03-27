/*-
 * SPDX-License-Identifier: (BSD-3-Clause AND ISC)
 *
 * Copyright (c) 1980, 1983, 1988, 1993
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

/*
 *      @(#)netdb.h	8.1 (Berkeley) 6/2/93
 *      From: Id: netdb.h,v 8.9 1996/11/19 08:39:29 vixie Exp $
 * $FreeBSD$
 */

#ifndef _NETDB_H_
#define _NETDB_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef _IN_ADDR_T_DECLARED
typedef	__uint32_t	in_addr_t;
#define	_IN_ADDR_T_DECLARED
#endif

#ifndef _IN_PORT_T_DECLARED
typedef	__uint16_t	in_port_t;
#define	_IN_PORT_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef	__size_t	size_t;
#define	_SIZE_T_DECLARED
#endif

#ifndef _SOCKLEN_T_DECLARED
typedef	__socklen_t	socklen_t;
#define	_SOCKLEN_T_DECLARED
#endif

#ifndef _UINT32_T_DECLARED
typedef	__uint32_t	uint32_t;
#define	_UINT32_T_DECLARED
#endif

#ifndef _PATH_HEQUIV
# define	_PATH_HEQUIV	"/etc/hosts.equiv"
#endif
#define	_PATH_HOSTS	"/etc/hosts"
#define	_PATH_NETWORKS	"/etc/networks"
#define	_PATH_PROTOCOLS	"/etc/protocols"
#define	_PATH_SERVICES	"/etc/services"
#define	_PATH_SERVICES_DB "/var/db/services.db"

#define	h_errno (*__h_errno())

/*
 * Structures returned by network data base library.  All addresses are
 * supplied in host order, and returned in network order (suitable for
 * use in system calls).
 */
struct hostent {
	char	*h_name;	/* official name of host */
	char	**h_aliases;	/* alias list */
	int	h_addrtype;	/* host address type */
	int	h_length;	/* length of address */
	char	**h_addr_list;	/* list of addresses from name server */
#define	h_addr	h_addr_list[0]	/* address, for backward compatibility */
};

struct netent {
	char		*n_name;	/* official name of net */
	char		**n_aliases;	/* alias list */
	int		n_addrtype;	/* net address type */
	uint32_t	n_net;		/* network # */
};

struct servent {
	char	*s_name;	/* official service name */
	char	**s_aliases;	/* alias list */
	int	s_port;		/* port # */
	char	*s_proto;	/* protocol to use */
};

struct protoent {
	char	*p_name;	/* official protocol name */
	char	**p_aliases;	/* alias list */
	int	p_proto;	/* protocol # */
};

struct addrinfo {
	int	ai_flags;	/* AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST */
	int	ai_family;	/* AF_xxx */
	int	ai_socktype;	/* SOCK_xxx */
	int	ai_protocol;	/* 0 or IPPROTO_xxx for IPv4 and IPv6 */
	socklen_t ai_addrlen;	/* length of ai_addr */
	char	*ai_canonname;	/* canonical name for hostname */
	struct	sockaddr *ai_addr;	/* binary address */
	struct	addrinfo *ai_next;	/* next structure in linked list */
};

#define	IPPORT_RESERVED	1024

/*
 * Error return codes from gethostbyname() and gethostbyaddr()
 * (left in h_errno).
 */

#define	NETDB_INTERNAL	-1	/* see errno */
#define	NETDB_SUCCESS	0	/* no problem */
#define	HOST_NOT_FOUND	1 /* Authoritative Answer Host not found */
#define	TRY_AGAIN	2 /* Non-Authoritative Host not found, or SERVERFAIL */
#define	NO_RECOVERY	3 /* Non recoverable errors, FORMERR, REFUSED, NOTIMP */
#define	NO_DATA		4 /* Valid name, no data record of requested type */
#define	NO_ADDRESS	NO_DATA		/* no address, look for MX record */

/*
 * Error return codes from gai_strerror(3), see RFC 3493.
 */
#if 0
/* Obsoleted on RFC 2553bis-02 */
#define	EAI_ADDRFAMILY	 1	/* address family for hostname not supported */
#endif
#define	EAI_AGAIN	 2	/* name could not be resolved at this time */
#define	EAI_BADFLAGS	 3	/* flags parameter had an invalid value */
#define	EAI_FAIL	 4	/* non-recoverable failure in name resolution */
#define	EAI_FAMILY	 5	/* address family not recognized */
#define	EAI_MEMORY	 6	/* memory allocation failure */
#if 0
/* Obsoleted on RFC 2553bis-02 */
#define	EAI_NODATA	 7	/* no address associated with hostname */
#endif
#define	EAI_NONAME	 8	/* name does not resolve */
#define	EAI_SERVICE	 9	/* service not recognized for socket type */
#define	EAI_SOCKTYPE	10	/* intended socket type was not recognized */
#define	EAI_SYSTEM	11	/* system error returned in errno */
#define	EAI_BADHINTS	12	/* invalid value for hints */
#define	EAI_PROTOCOL	13	/* resolved protocol is unknown */
#define	EAI_OVERFLOW	14	/* argument buffer overflow */
#define	EAI_MAX		15

/*
 * Flag values for getaddrinfo()
 */
#define	AI_PASSIVE	0x00000001 /* get address to use bind() */
#define	AI_CANONNAME	0x00000002 /* fill ai_canonname */
#define	AI_NUMERICHOST	0x00000004 /* prevent host name resolution */
#define	AI_NUMERICSERV	0x00000008 /* prevent service name resolution */
/* valid flags for addrinfo (not a standard def, apps should not use it) */
#define AI_MASK \
    (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV | \
    AI_ADDRCONFIG | AI_ALL | AI_V4MAPPED)

#define	AI_ALL		0x00000100 /* IPv6 and IPv4-mapped (with AI_V4MAPPED) */
#define	AI_V4MAPPED_CFG	0x00000200 /* accept IPv4-mapped if kernel supports */
#define	AI_ADDRCONFIG	0x00000400 /* only if any address is assigned */
#define	AI_V4MAPPED	0x00000800 /* accept IPv4-mapped IPv6 address */
/* special recommended flags for getipnodebyname */
#define	AI_DEFAULT	(AI_V4MAPPED_CFG | AI_ADDRCONFIG)

/*
 * Constants for getnameinfo()
 */
#define	NI_MAXHOST	1025
#define	NI_MAXSERV	32

/*
 * Flag values for getnameinfo()
 */
#define	NI_NOFQDN	0x00000001
#define	NI_NUMERICHOST	0x00000002
#define	NI_NAMEREQD	0x00000004
#define	NI_NUMERICSERV	0x00000008
#define	NI_DGRAM	0x00000010
#define	NI_NUMERICSCOPE	0x00000020

/*
 * Scope delimit character
 */
#define	SCOPE_DELIMITER	'%'

__BEGIN_DECLS
void		endhostent(void);
void		endnetent(void);
void		endprotoent(void);
void		endservent(void);
#if __BSD_VISIBLE || (__POSIX_VISIBLE && __POSIX_VISIBLE <= 200112)
struct hostent	*gethostbyaddr(const void *, socklen_t, int);
struct hostent	*gethostbyname(const char *);
#endif
struct hostent	*gethostent(void);
struct netent	*getnetbyaddr(uint32_t, int);
struct netent	*getnetbyname(const char *);
struct netent	*getnetent(void);
struct protoent	*getprotobyname(const char *);
struct protoent	*getprotobynumber(int);
struct protoent	*getprotoent(void);
struct servent	*getservbyname(const char *, const char *);
struct servent	*getservbyport(int, const char *);
struct servent	*getservent(void);
void		sethostent(int);
/* void		sethostfile(const char *); */
void		setnetent(int);
void		setprotoent(int);
int		getaddrinfo(const char *, const char *,
			    const struct addrinfo *, struct addrinfo **);
int		getnameinfo(const struct sockaddr *, socklen_t, char *,
			    size_t, char *, size_t, int);
void		freeaddrinfo(struct addrinfo *);
const char	*gai_strerror(int);
void		setservent(int);

#if __BSD_VISIBLE
void		endnetgrent(void);
void		freehostent(struct hostent *);
int		gethostbyaddr_r(const void *, socklen_t, int, struct hostent *,
    char *, size_t, struct hostent **, int *);
int		gethostbyname_r(const char *, struct hostent *, char *, size_t,
    struct hostent **, int *);
struct hostent	*gethostbyname2(const char *, int);
int		gethostbyname2_r(const char *, int, struct hostent *, char *,
    size_t, struct hostent **, int *);
int		gethostent_r(struct hostent *, char *, size_t,
    struct hostent **, int *);
struct hostent	*getipnodebyaddr(const void *, size_t, int, int *);
struct hostent	*getipnodebyname(const char *, int, int, int *);
int		getnetbyaddr_r(uint32_t, int, struct netent *, char *, size_t,
    struct netent**, int *);
int		getnetbyname_r(const char *, struct netent *, char *, size_t,
    struct netent **, int *);
int		getnetent_r(struct netent *, char *, size_t, struct netent **,
    int *);
int		getnetgrent(char **, char **, char **);
int		getnetgrent_r(char **, char **, char **, char *, size_t);
int		getprotobyname_r(const char *, struct protoent *, char *,
    size_t, struct protoent **);
int		getprotobynumber_r(int, struct protoent *, char *, size_t,
    struct protoent **);
int		getprotoent_r(struct protoent *, char *, size_t,
    struct protoent **);
int		getservbyname_r(const char *, const char *, struct servent *,
    char *, size_t, struct servent **);
int		getservbyport_r(int, const char *, struct servent *, char *,
    size_t, struct servent **);
int		getservent_r(struct servent *, char *, size_t,
    struct servent **);
void		herror(const char *);
const char	*hstrerror(int);
int		innetgr(const char *, const char *, const char *, const char *);
void		setnetgrent(const char *);
#endif


/*
 * PRIVATE functions specific to the FreeBSD implementation
 */

/* DO NOT USE THESE, THEY ARE SUBJECT TO CHANGE AND ARE NOT PORTABLE!!! */
int	* __h_errno(void);
__END_DECLS

#endif /* !_NETDB_H_ */
