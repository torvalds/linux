/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2005 The FreeBSD Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

#ifndef _NETDB_PRIVATE_H_
#define _NETDB_PRIVATE_H_

#include <stdio.h>				/* XXX: for FILE */

#define	NETDB_THREAD_ALLOC(name)					\
static struct name name;						\
static thread_key_t name##_key;						\
static once_t name##_init_once = ONCE_INITIALIZER;			\
static int name##_thr_keycreated = 0;					\
\
static void name##_free(void *);					\
\
static void								\
name##_keycreate(void)							\
{									\
	name##_thr_keycreated =						\
	    (thr_keycreate(&name##_key, name##_free) == 0);		\
}									\
\
struct name *								\
__##name##_init(void)							\
{									\
	struct name *he;						\
									\
	if (thr_main() != 0)						\
		return (&name);						\
	if (thr_once(&name##_init_once, name##_keycreate) != 0 ||	\
	    !name##_thr_keycreated)					\
		return (NULL);						\
	if ((he = thr_getspecific(name##_key)) != NULL)			\
		return (he);						\
	if ((he = calloc(1, sizeof(*he))) == NULL)			\
		return (NULL);						\
	if (thr_setspecific(name##_key, he) == 0)			\
		return (he);						\
	free(he);							\
	return (NULL);							\
}

#define	_MAXALIASES	35
#define	_MAXLINELEN	1024
#define	_MAXADDRS	35
#define	_HOSTBUFSIZE	(8 * 1024)
#define	_NETBUFSIZE	1025

struct hostent_data {
	uint32_t host_addr[4];			/* IPv4 or IPv6 */
	char *h_addr_ptrs[_MAXADDRS + 1];
	char *host_aliases[_MAXALIASES];
	char hostbuf[_HOSTBUFSIZE];
	FILE *hostf;
	int stayopen;
#ifdef YP
	char *yp_domain;
#endif
};

struct netent_data {
	char *net_aliases[_MAXALIASES];
	char netbuf[_NETBUFSIZE];
	FILE *netf;
	int stayopen;
#ifdef YP
	char *yp_domain;
#endif
};

struct protoent_data {
	FILE *fp;
	char *aliases[_MAXALIASES];
	int stayopen;
	char line[_MAXLINELEN + 1];
};

struct hostdata {
	struct hostent host;
	char data[sizeof(struct hostent_data)];
};

struct netdata {
	struct netent net;
	char data[sizeof(struct netent_data)];
};

struct protodata {
	struct protoent proto;
	char data[sizeof(struct protoent_data)];
};

struct hostdata *__hostdata_init(void);
struct hostent *__hostent_init(void);
struct hostent_data *__hostent_data_init(void);
struct netdata *__netdata_init(void);
struct netent_data *__netent_data_init(void);
struct protodata *__protodata_init(void);
struct protoent_data *__protoent_data_init(void);
int __copy_hostent(struct hostent *, struct hostent *, char *, size_t);
int __copy_netent(struct netent *, struct netent *, char *, size_t);
int __copy_protoent(struct protoent *, struct protoent *, char *, size_t);

void __endprotoent_p(struct protoent_data *);
int __getprotoent_p(struct protoent *, struct protoent_data *);
void __setprotoent_p(int, struct protoent_data *);
void _endhostdnsent(void);
void _endhosthtent(struct hostent_data *);
void _endnetdnsent(void);
void _endnethtent(struct netent_data *);
void _map_v4v6_address(const char *, char *);
void _map_v4v6_hostent(struct hostent *, char **, char *);
void _sethostdnsent(int);
void _sethosthtent(int, struct hostent_data *);
void _setnetdnsent(int);
void _setnethtent(int, struct netent_data *);

struct hostent *__dns_getanswer(const char *, int, const char *, int);
int _dns_gethostbyaddr(void *, void *, va_list);
int _dns_gethostbyname(void *, void *, va_list);
int _dns_getnetbyaddr(void *, void *, va_list);
int _dns_getnetbyname(void *, void *, va_list);
int _ht_gethostbyaddr(void *, void *, va_list);
int _ht_gethostbyname(void *, void *, va_list);
int _ht_getnetbyaddr(void *, void *, va_list);
int _ht_getnetbyname(void *, void *, va_list);
int _nis_gethostbyaddr(void *, void *, va_list);
int _nis_gethostbyname(void *, void *, va_list);
int _nis_getnetbyaddr(void *, void *, va_list);
int _nis_getnetbyname(void *, void *, va_list);
#ifdef NS_CACHING
int __proto_id_func(char *, size_t *, va_list, void *);
int __proto_marshal_func(char *, size_t *, void *, va_list, void *);
int __proto_unmarshal_func(char *, size_t, void *, va_list, void *);
#endif

#endif /* _NETDB_PRIVATE_H_ */
