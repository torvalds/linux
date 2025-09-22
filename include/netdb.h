/*	$OpenBSD: netdb.h,v 1.33 2015/01/18 20:29:31 deraadt Exp $	*/

/*
 * ++Copyright++ 1980, 1983, 1988, 1993
 * -
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
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 Craig Metz. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any contributors
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

/*
 *      @(#)netdb.h	8.1 (Berkeley) 6/2/93
 *	$From: netdb.h,v 8.7 1996/05/09 05:59:09 vixie Exp $
 */

#ifndef _NETDB_H_
#define _NETDB_H_

#include <netinet/in.h>

#ifndef	_SOCKLEN_T_DEFINED_
#define	_SOCKLEN_T_DEFINED_
typedef	__socklen_t	socklen_t;	/* length type for network syscalls */
#endif

#define	_PATH_HEQUIV	"/etc/hosts.equiv"
#define	_PATH_HOSTS	"/etc/hosts"
#define	_PATH_NETWORKS	"/etc/networks"
#define	_PATH_PROTOCOLS	"/etc/protocols"
#define	_PATH_SERVICES	"/etc/services"

/*
 * Structures returned by network data base library.  All addresses are
 * supplied in host order, and returned in network order (suitable for
 * use in system calls).
 */
struct	hostent {
	char	*h_name;	/* official name of host */
	char	**h_aliases;	/* alias list */
	int	h_addrtype;	/* host address type */
	int	h_length;	/* length of address */
	char	**h_addr_list;	/* list of addresses from name server */
#define	h_addr	h_addr_list[0]	/* address, for backward compatibility */
};

/*
 * Assumption here is that a network number
 * fits in an in_addr_t -- probably a poor one.
 */
struct	netent {
	char		*n_name;	/* official name of net */
	char		**n_aliases;	/* alias list */
	int		n_addrtype;	/* net address type */
	in_addr_t	n_net;		/* network # */
};

struct	servent {
	char	*s_name;	/* official service name */
	char	**s_aliases;	/* alias list */
	int	s_port;		/* port # */
	char	*s_proto;	/* protocol to use */
};

struct	protoent {
	char	*p_name;	/* official protocol name */
	char	**p_aliases;	/* alias list */
	int	p_proto;	/* protocol # */
};

#if __BSD_VISIBLE || __POSIX_VISIBLE < 200809
extern int h_errno;

/*
 * Error return codes from gethostbyname() and gethostbyaddr()
 * (left in extern int h_errno).
 */

#define	NETDB_INTERNAL	-1	/* see errno */
#define	NETDB_SUCCESS	0	/* no problem */
#define	HOST_NOT_FOUND	1 /* Authoritative Answer Host not found */
#define	TRY_AGAIN	2 /* Non-Authoritative Host not found, or SERVERFAIL */
#define	NO_RECOVERY	3 /* Non recoverable errors, FORMERR, REFUSED, NOTIMP */
#define	NO_DATA		4 /* Valid name, no data record of requested type */
#define	NO_ADDRESS	NO_DATA		/* no address */
#endif /* __BSD_VISIBLE || __POSIX_VISIBLE < 200809 */

/* Values for getaddrinfo() and getnameinfo() */
#define AI_PASSIVE	1	/* socket address is intended for bind() */
#define AI_CANONNAME	2	/* request for canonical name */
#define AI_NUMERICHOST	4	/* don't ever try hostname lookup */
#define AI_EXT		8	/* enable non-portable extensions */
#define AI_NUMERICSERV	16	/* don't ever try servname lookup */
#define AI_FQDN		32	/* return the FQDN that was resolved */
#define AI_ADDRCONFIG	64	/* return configured address families only */
/* valid flags for addrinfo */
#define AI_MASK \
    (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_NUMERICSERV | AI_FQDN | \
     AI_ADDRCONFIG)

#define NI_NUMERICHOST	1	/* return the host address, not the name */
#define NI_NUMERICSERV	2	/* return the service address, not the name */
#define NI_NOFQDN	4	/* return a short name if in the local domain */
#define NI_NAMEREQD	8	/* fail if either host or service name is unknown */
#define NI_DGRAM	16	/* look up datagram service instead of stream */
/* #define NI_NUMERICSCOPE	32	 return the scope number, not the name */

#if __BSD_VISIBLE
#define NI_MAXHOST	256	/* max host name from getnameinfo (MAXHOSTNAMELEN) */
#define NI_MAXSERV	32	/* max serv. name length returned by getnameinfo */

/*
 * Scope delimit character (KAME hack)
 */
#define SCOPE_DELIMITER '%'
#endif

#define EAI_BADFLAGS	-1	/* invalid value for ai_flags */
#define EAI_NONAME	-2	/* name or service is not known */
#define EAI_AGAIN	-3	/* temporary failure in name resolution */
#define EAI_FAIL	-4	/* non-recoverable failure in name resolution */
#define EAI_NODATA	-5	/* no address associated with name */
#define EAI_FAMILY	-6	/* ai_family not supported */
#define EAI_SOCKTYPE	-7	/* ai_socktype not supported */
#define EAI_SERVICE	-8	/* service not supported for ai_socktype */
#define EAI_ADDRFAMILY	-9	/* address family for name not supported */
#define EAI_MEMORY	-10	/* memory allocation failure */
#define EAI_SYSTEM	-11	/* system error (code indicated in errno) */
#define EAI_BADHINTS	-12	/* invalid value for hints */
#define EAI_PROTOCOL	-13	/* resolved protocol is unknown */
#define EAI_OVERFLOW	-14	/* argument buffer overflow */

struct addrinfo {
	int ai_flags;		/* input flags */
	int ai_family;		/* protocol family for socket */
	int ai_socktype;	/* socket type */
	int ai_protocol;	/* protocol for socket */
	socklen_t ai_addrlen;	/* length of socket-address */
	struct sockaddr *ai_addr; /* socket-address for socket */
	char *ai_canonname;	/* canonical name for service location (iff req) */
	struct addrinfo *ai_next; /* pointer to next in list */
};
 
#if __BSD_VISIBLE
/*
 * Flags for getrrsetbyname()
 */
#define RRSET_VALIDATED		1

/*
 * Return codes for getrrsetbyname()
 */
#define ERRSET_SUCCESS		0
#define ERRSET_NOMEMORY		1
#define ERRSET_FAIL		2
#define ERRSET_INVAL		3
#define ERRSET_NONAME		4
#define ERRSET_NODATA		5

/*
 * Structures used by getrrsetbyname() and freerrset()
 */
struct rdatainfo {
	unsigned int		rdi_length;	/* length of data */
	unsigned char		*rdi_data;	/* record data */
};

struct rrsetinfo {
	unsigned int		rri_flags;	/* RRSET_VALIDATED ... */
	unsigned int		rri_rdclass;	/* class number */
	unsigned int		rri_rdtype;	/* RR type number */
	unsigned int		rri_ttl;	/* time to live */
	unsigned int		rri_nrdatas;	/* size of rdatas array */
	unsigned int		rri_nsigs;	/* size of sigs array */
	char			*rri_name;	/* canonical name */
	struct rdatainfo	*rri_rdatas;	/* individual records */
	struct rdatainfo	*rri_sigs;	/* individual signatures */
};

struct servent_data {
	void *fp;
	char **aliases;
	int maxaliases;
	int stayopen;
	char *line;
};

struct protoent_data {
	void *fp;
	char **aliases;
	int maxaliases;
	int stayopen;
	char *line;
};
#endif

__BEGIN_DECLS
void		endhostent(void);
void		endnetent(void);
void		endprotoent(void);
void		endservent(void);
#if __BSD_VISIBLE || __POSIX_VISIBLE < 200809
struct hostent	*gethostbyaddr(const void *, socklen_t, int);
struct hostent	*gethostbyname(const char *);
#endif
#if __BSD_VISIBLE
struct hostent	*gethostbyname2(const char *, int);
#endif
struct hostent	*gethostent(void);
struct netent	*getnetbyaddr(in_addr_t, int);
struct netent	*getnetbyname(const char *);
struct netent	*getnetent(void);
struct protoent	*getprotobyname(const char *);
struct protoent	*getprotobynumber(int);
struct protoent	*getprotoent(void);
struct servent	*getservbyname(const char *, const char *);
struct servent	*getservbyport(int, const char *);
struct servent	*getservent(void);
#if __BSD_VISIBLE
void		herror(const char *);
const char	*hstrerror(int);
#endif
void		sethostent(int);
/* void		sethostfile(const char *); */
void		setnetent(int);
void		setprotoent(int);
void		setservent(int);

#if __BSD_VISIBLE
void		endprotoent_r(struct protoent_data *);
void		endservent_r(struct servent_data *);
int		getprotobyname_r(const char *, struct protoent *,
		    struct protoent_data *);
int		getprotobynumber_r(int, struct protoent *,
		    struct protoent_data *);
int		getservbyname_r(const char *, const char *, struct servent *,
		    struct servent_data *);
int		getservbyport_r(int, const char *, struct servent *,
		    struct servent_data *);
int		getservent_r(struct servent *, struct servent_data *);
int		getprotoent_r(struct protoent *, struct protoent_data *);
void		setprotoent_r(int, struct protoent_data *);
void		setservent_r(int, struct servent_data *);
#endif

int		getaddrinfo(const char *, const char *,
		    const struct addrinfo *, struct addrinfo **);
void		freeaddrinfo(struct addrinfo *);
int		getnameinfo(const struct sockaddr *, socklen_t,
		    char *, size_t, char *, size_t, int);
const char	*gai_strerror(int);

#if __BSD_VISIBLE
int		getrrsetbyname(const char *, unsigned int, unsigned int, unsigned int, struct rrsetinfo **);
void		freerrset(struct rrsetinfo *);
#endif
__END_DECLS

#endif /* !_NETDB_H_ */
