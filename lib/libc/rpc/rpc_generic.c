/*	$NetBSD: rpc_generic.c,v 1.4 2000/09/28 09:07:04 kleink Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc. 
 */

/* #pragma ident	"@(#)rpc_generic.c	1.17	94/04/24 SMI" */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * rpc_generic.c, Miscl routines for RPC.
 *
 */

#include "namespace.h"
#include "reentrant.h"
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/rpc.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <netdb.h>
#include <netconfig.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <rpc/nettype.h>
#include "un-namespace.h"
#include "rpc_com.h"
#include "mt_misc.h"

struct handle {
	NCONF_HANDLE *nhandle;
	int nflag;		/* Whether NETPATH or NETCONFIG */
	int nettype;
};

static const struct _rpcnettype {
	const char *name;
	const int type;
} _rpctypelist[] = {
	{ "netpath", _RPC_NETPATH },
	{ "visible", _RPC_VISIBLE },
	{ "circuit_v", _RPC_CIRCUIT_V },
	{ "datagram_v", _RPC_DATAGRAM_V },
	{ "circuit_n", _RPC_CIRCUIT_N },
	{ "datagram_n", _RPC_DATAGRAM_N },
	{ "tcp", _RPC_TCP },
	{ "udp", _RPC_UDP },
	{ 0, _RPC_NONE }
};

struct netid_af {
	const char	*netid;
	int		af;
	int		protocol;
};

static const struct netid_af na_cvt[] = {
	{ "udp",  AF_INET,  IPPROTO_UDP },
	{ "tcp",  AF_INET,  IPPROTO_TCP },
#ifdef INET6
	{ "udp6", AF_INET6, IPPROTO_UDP },
	{ "tcp6", AF_INET6, IPPROTO_TCP },
#endif
	{ "local", AF_LOCAL, 0 }
};

#if 0
static char *strlocase(char *);
#endif
static int getnettype(const char *);

/*
 * Cache the result of getrlimit(), so we don't have to do an
 * expensive call every time.
 */
int
__rpc_dtbsize(void)
{
	static int tbsize;
	struct rlimit rl;

	if (tbsize) {
		return (tbsize);
	}
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		return (tbsize = (int)rl.rlim_max);
	}
	/*
	 * Something wrong.  I'll try to save face by returning a
	 * pessimistic number.
	 */
	return (32);
}


/*
 * Find the appropriate buffer size
 *
 * size - Size requested
 */
u_int
/*ARGSUSED*/
__rpc_get_t_size(int af, int proto, int size)
{
	int maxsize, defsize;

	maxsize = 256 * 1024;	/* XXX */
	switch (proto) {
	case IPPROTO_TCP:
		defsize = 64 * 1024;	/* XXX */
		break;
	case IPPROTO_UDP:
		defsize = UDPMSGSIZE;
		break;
	default:
		defsize = RPC_MAXDATASIZE;
		break;
	}
	if (size == 0)
		return defsize;

	/* Check whether the value is within the upper max limit */
	return (size > maxsize ? (u_int)maxsize : (u_int)size);
}

/*
 * Find the appropriate address buffer size
 */
u_int
__rpc_get_a_size(int af)
{
	switch (af) {
	case AF_INET:
		return sizeof (struct sockaddr_in);
#ifdef INET6
	case AF_INET6:
		return sizeof (struct sockaddr_in6);
#endif
	case AF_LOCAL:
		return sizeof (struct sockaddr_un);
	default:
		break;
	}
	return ((u_int)RPC_MAXADDRSIZE);
}

#if 0
static char *
strlocase(char *p)
{
	char *t = p;

	for (; *p; p++)
		if (isupper(*p))
			*p = tolower(*p);
	return (t);
}
#endif

/*
 * Returns the type of the network as defined in <rpc/nettype.h>
 * If nettype is NULL, it defaults to NETPATH.
 */
static int
getnettype(const char *nettype)
{
	int i;

	if ((nettype == NULL) || (nettype[0] == 0)) {
		return (_RPC_NETPATH);	/* Default */
	}

#if 0
	nettype = strlocase(nettype);
#endif
	for (i = 0; _rpctypelist[i].name; i++)
		if (strcasecmp(nettype, _rpctypelist[i].name) == 0) {
			return (_rpctypelist[i].type);
		}
	return (_rpctypelist[i].type);
}

static thread_key_t tcp_key, udp_key;
static once_t keys_once = ONCE_INITIALIZER;
static int tcp_key_error, udp_key_error;

static void
keys_init(void)
{

	tcp_key_error = thr_keycreate(&tcp_key, free);
	udp_key_error = thr_keycreate(&udp_key, free);
}

/*
 * For the given nettype (tcp or udp only), return the first structure found.
 * This should be freed by calling freenetconfigent()
 */
struct netconfig *
__rpc_getconfip(const char *nettype)
{
	char *netid;
	char *netid_tcp = (char *) NULL;
	char *netid_udp = (char *) NULL;
	static char *netid_tcp_main;
	static char *netid_udp_main;
	struct netconfig *dummy;
	int main_thread;

	if ((main_thread = thr_main())) {
		netid_udp = netid_udp_main;
		netid_tcp = netid_tcp_main;
	} else {
		if (thr_once(&keys_once, keys_init) != 0 ||
		    tcp_key_error != 0 || udp_key_error != 0)
			return (NULL);
		netid_tcp = (char *)thr_getspecific(tcp_key);
		netid_udp = (char *)thr_getspecific(udp_key);
	}
	if (!netid_udp && !netid_tcp) {
		struct netconfig *nconf;
		void *confighandle;

		if (!(confighandle = setnetconfig())) {
			syslog (LOG_ERR, "rpc: failed to open " NETCONFIG);
			return (NULL);
		}
		while ((nconf = getnetconfig(confighandle)) != NULL) {
			if (strcmp(nconf->nc_protofmly, NC_INET) == 0) {
				if (strcmp(nconf->nc_proto, NC_TCP) == 0 &&
				    netid_tcp == NULL) {
					netid_tcp = strdup(nconf->nc_netid);
					if (main_thread)
						netid_tcp_main = netid_tcp;
					else
						thr_setspecific(tcp_key,
							(void *) netid_tcp);
				} else
				if (strcmp(nconf->nc_proto, NC_UDP) == 0 &&
				    netid_udp == NULL) {
					netid_udp = strdup(nconf->nc_netid);
					if (main_thread)
						netid_udp_main = netid_udp;
					else
						thr_setspecific(udp_key,
						(void *) netid_udp);
				}
			}
		}
		endnetconfig(confighandle);
	}
	if (strcmp(nettype, "udp") == 0)
		netid = netid_udp;
	else if (strcmp(nettype, "tcp") == 0)
		netid = netid_tcp;
	else {
		return (NULL);
	}
	if ((netid == NULL) || (netid[0] == 0)) {
		return (NULL);
	}
	dummy = getnetconfigent(netid);
	return (dummy);
}

/*
 * Returns the type of the nettype, which should then be used with
 * __rpc_getconf().
 */
void *
__rpc_setconf(const char *nettype)
{
	struct handle *handle;

	handle = (struct handle *) malloc(sizeof (struct handle));
	if (handle == NULL) {
		return (NULL);
	}
	switch (handle->nettype = getnettype(nettype)) {
	case _RPC_NETPATH:
	case _RPC_CIRCUIT_N:
	case _RPC_DATAGRAM_N:
		if (!(handle->nhandle = setnetpath()))
			goto failed;
		handle->nflag = TRUE;
		break;
	case _RPC_VISIBLE:
	case _RPC_CIRCUIT_V:
	case _RPC_DATAGRAM_V:
	case _RPC_TCP:
	case _RPC_UDP:
		if (!(handle->nhandle = setnetconfig())) {
		        syslog (LOG_ERR, "rpc: failed to open " NETCONFIG);
			goto failed;
		}
		handle->nflag = FALSE;
		break;
	default:
		goto failed;
	}

	return (handle);

failed:
	free(handle);
	return (NULL);
}

/*
 * Returns the next netconfig struct for the given "net" type.
 * __rpc_setconf() should have been called previously.
 */
struct netconfig *
__rpc_getconf(void *vhandle)
{
	struct handle *handle;
	struct netconfig *nconf;

	handle = (struct handle *)vhandle;
	if (handle == NULL) {
		return (NULL);
	}
	for (;;) {
		if (handle->nflag)
			nconf = getnetpath(handle->nhandle);
		else
			nconf = getnetconfig(handle->nhandle);
		if (nconf == NULL)
			break;
		if ((nconf->nc_semantics != NC_TPI_CLTS) &&
			(nconf->nc_semantics != NC_TPI_COTS) &&
			(nconf->nc_semantics != NC_TPI_COTS_ORD))
			continue;
		switch (handle->nettype) {
		case _RPC_VISIBLE:
			if (!(nconf->nc_flag & NC_VISIBLE))
				continue;
			/* FALLTHROUGH */
		case _RPC_NETPATH:	/* Be happy */
			break;
		case _RPC_CIRCUIT_V:
			if (!(nconf->nc_flag & NC_VISIBLE))
				continue;
			/* FALLTHROUGH */
		case _RPC_CIRCUIT_N:
			if ((nconf->nc_semantics != NC_TPI_COTS) &&
				(nconf->nc_semantics != NC_TPI_COTS_ORD))
				continue;
			break;
		case _RPC_DATAGRAM_V:
			if (!(nconf->nc_flag & NC_VISIBLE))
				continue;
			/* FALLTHROUGH */
		case _RPC_DATAGRAM_N:
			if (nconf->nc_semantics != NC_TPI_CLTS)
				continue;
			break;
		case _RPC_TCP:
			if (((nconf->nc_semantics != NC_TPI_COTS) &&
				(nconf->nc_semantics != NC_TPI_COTS_ORD)) ||
				(strcmp(nconf->nc_protofmly, NC_INET)
#ifdef INET6
				 && strcmp(nconf->nc_protofmly, NC_INET6))
#else
				)
#endif
				||
				strcmp(nconf->nc_proto, NC_TCP))
				continue;
			break;
		case _RPC_UDP:
			if ((nconf->nc_semantics != NC_TPI_CLTS) ||
				(strcmp(nconf->nc_protofmly, NC_INET)
#ifdef INET6
				&& strcmp(nconf->nc_protofmly, NC_INET6))
#else
				)
#endif
				||
				strcmp(nconf->nc_proto, NC_UDP))
				continue;
			break;
		}
		break;
	}
	return (nconf);
}

void
__rpc_endconf(void *vhandle)
{
	struct handle *handle;

	handle = (struct handle *) vhandle;
	if (handle == NULL) {
		return;
	}
	if (handle->nflag) {
		endnetpath(handle->nhandle);
	} else {
		endnetconfig(handle->nhandle);
	}
	free(handle);
}

/*
 * Used to ping the NULL procedure for clnt handle.
 * Returns NULL if fails, else a non-NULL pointer.
 */
void *
rpc_nullproc(CLIENT *clnt)
{
	struct timeval TIMEOUT = {25, 0};

	if (clnt_call(clnt, NULLPROC, (xdrproc_t) xdr_void, NULL,
		(xdrproc_t) xdr_void, NULL, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *) clnt);
}

/*
 * Try all possible transports until
 * one succeeds in finding the netconf for the given fd.
 */
struct netconfig *
__rpcgettp(int fd)
{
	const char *netid;
	struct __rpc_sockinfo si;

	if (!__rpc_fd2sockinfo(fd, &si))
		return NULL;

	if (!__rpc_sockinfo2netid(&si, &netid))
		return NULL;

	/*LINTED const castaway*/
	return getnetconfigent((char *)netid);
}

int
__rpc_fd2sockinfo(int fd, struct __rpc_sockinfo *sip)
{
	socklen_t len;
	int type, proto;
	struct sockaddr_storage ss;

	len = sizeof ss;
	if (_getsockname(fd, (struct sockaddr *)(void *)&ss, &len) < 0)
		return 0;
	sip->si_alen = len;

	len = sizeof type;
	if (_getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) < 0)
		return 0;

	/* XXX */
	if (ss.ss_family != AF_LOCAL) {
		if (type == SOCK_STREAM)
			proto = IPPROTO_TCP;
		else if (type == SOCK_DGRAM)
			proto = IPPROTO_UDP;
		else
			return 0;
	} else
		proto = 0;

	sip->si_af = ss.ss_family;
	sip->si_proto = proto;
	sip->si_socktype = type;

	return 1;
}

/*
 * Linear search, but the number of entries is small.
 */
int
__rpc_nconf2sockinfo(const struct netconfig *nconf, struct __rpc_sockinfo *sip)
{
	int i;

	for (i = 0; i < (sizeof na_cvt) / (sizeof (struct netid_af)); i++)
		if (strcmp(na_cvt[i].netid, nconf->nc_netid) == 0 || (
		    strcmp(nconf->nc_netid, "unix") == 0 &&
		    strcmp(na_cvt[i].netid, "local") == 0)) {
			sip->si_af = na_cvt[i].af;
			sip->si_proto = na_cvt[i].protocol;
			sip->si_socktype =
			    __rpc_seman2socktype((int)nconf->nc_semantics);
			if (sip->si_socktype == -1)
				return 0;
			sip->si_alen = __rpc_get_a_size(sip->si_af);
			return 1;
		}

	return 0;
}

int
__rpc_nconf2fd(const struct netconfig *nconf)
{
	struct __rpc_sockinfo si;

	if (!__rpc_nconf2sockinfo(nconf, &si))
		return 0;

	return _socket(si.si_af, si.si_socktype, si.si_proto);
}

int
__rpc_sockinfo2netid(struct __rpc_sockinfo *sip, const char **netid)
{
	int i;
	struct netconfig *nconf;

	nconf = getnetconfigent("local");

	for (i = 0; i < (sizeof na_cvt) / (sizeof (struct netid_af)); i++) {
		if (na_cvt[i].af == sip->si_af &&
		    na_cvt[i].protocol == sip->si_proto) {
			if (strcmp(na_cvt[i].netid, "local") == 0 && nconf == NULL) {
				if (netid)
					*netid = "unix";
			} else {
				if (netid)
					*netid = na_cvt[i].netid;
			}
			if (nconf != NULL)
				freenetconfigent(nconf);
			return 1;
		}
	}
	if (nconf != NULL)
		freenetconfigent(nconf);

	return 0;
}

char *
taddr2uaddr(const struct netconfig *nconf, const struct netbuf *nbuf)
{
	struct __rpc_sockinfo si;

	if (!__rpc_nconf2sockinfo(nconf, &si))
		return NULL;
	return __rpc_taddr2uaddr_af(si.si_af, nbuf);
}

struct netbuf *
uaddr2taddr(const struct netconfig *nconf, const char *uaddr)
{
	struct __rpc_sockinfo si;
	
	if (!__rpc_nconf2sockinfo(nconf, &si))
		return NULL;
	return __rpc_uaddr2taddr_af(si.si_af, uaddr);
}

char *
__rpc_taddr2uaddr_af(int af, const struct netbuf *nbuf)
{
	char *ret;
	struct sockaddr_in *sin;
	struct sockaddr_un *sun;
	char namebuf[INET_ADDRSTRLEN];
#ifdef INET6
	struct sockaddr_in6 *sin6;
	char namebuf6[INET6_ADDRSTRLEN];
#endif
	u_int16_t port;

	switch (af) {
	case AF_INET:
		if (nbuf->len < sizeof(*sin))
			return NULL;
		sin = nbuf->buf;
		if (inet_ntop(af, &sin->sin_addr, namebuf, sizeof namebuf)
		    == NULL)
			return NULL;
		port = ntohs(sin->sin_port);
		if (asprintf(&ret, "%s.%u.%u", namebuf, ((u_int32_t)port) >> 8,
		    port & 0xff) < 0)
			return NULL;
		break;
#ifdef INET6
	case AF_INET6:
		if (nbuf->len < sizeof(*sin6))
			return NULL;
		sin6 = nbuf->buf;
		if (inet_ntop(af, &sin6->sin6_addr, namebuf6, sizeof namebuf6)
		    == NULL)
			return NULL;
		port = ntohs(sin6->sin6_port);
		if (asprintf(&ret, "%s.%u.%u", namebuf6, ((u_int32_t)port) >> 8,
		    port & 0xff) < 0)
			return NULL;
		break;
#endif
	case AF_LOCAL:
		sun = nbuf->buf;
		if (asprintf(&ret, "%.*s", (int)(sun->sun_len -
		    offsetof(struct sockaddr_un, sun_path)),
		    sun->sun_path) < 0)
			return (NULL);
		break;
	default:
		return NULL;
	}

	return ret;
}

struct netbuf *
__rpc_uaddr2taddr_af(int af, const char *uaddr)
{
	struct netbuf *ret = NULL;
	char *addrstr, *p;
	unsigned port, portlo, porthi;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif
	struct sockaddr_un *sun;

	port = 0;
	sin = NULL;

	if (uaddr == NULL)
		return NULL;

	addrstr = strdup(uaddr);
	if (addrstr == NULL)
		return NULL;

	/*
	 * AF_LOCAL addresses are expected to be absolute
	 * pathnames, anything else will be AF_INET or AF_INET6.
	 */
	if (*addrstr != '/') {
		p = strrchr(addrstr, '.');
		if (p == NULL)
			goto out;
		portlo = (unsigned)atoi(p + 1);
		*p = '\0';

		p = strrchr(addrstr, '.');
		if (p == NULL)
			goto out;
		porthi = (unsigned)atoi(p + 1);
		*p = '\0';
		port = (porthi << 8) | portlo;
	}

	ret = (struct netbuf *)malloc(sizeof *ret);
	if (ret == NULL)
		goto out;
	
	switch (af) {
	case AF_INET:
		sin = (struct sockaddr_in *)malloc(sizeof *sin);
		if (sin == NULL)
			goto out;
		memset(sin, 0, sizeof *sin);
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		if (inet_pton(AF_INET, addrstr, &sin->sin_addr) <= 0) {
			free(sin);
			free(ret);
			ret = NULL;
			goto out;
		}
		sin->sin_len = ret->maxlen = ret->len = sizeof *sin;
		ret->buf = sin;
		break;
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)malloc(sizeof *sin6);
		if (sin6 == NULL)
			goto out;
		memset(sin6, 0, sizeof *sin6);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		if (inet_pton(AF_INET6, addrstr, &sin6->sin6_addr) <= 0) {
			free(sin6);
			free(ret);
			ret = NULL;
			goto out;
		}
		sin6->sin6_len = ret->maxlen = ret->len = sizeof *sin6;
		ret->buf = sin6;
		break;
#endif
	case AF_LOCAL:
		sun = (struct sockaddr_un *)malloc(sizeof *sun);
		if (sun == NULL)
			goto out;
		memset(sun, 0, sizeof *sun);
		sun->sun_family = AF_LOCAL;
		strncpy(sun->sun_path, addrstr, sizeof(sun->sun_path) - 1);
		ret->len = ret->maxlen = sun->sun_len = SUN_LEN(sun);
		ret->buf = sun;
		break;
	default:
		break;
	}
out:
	free(addrstr);
	return ret;
}

int
__rpc_seman2socktype(int semantics)
{
	switch (semantics) {
	case NC_TPI_CLTS:
		return SOCK_DGRAM;
	case NC_TPI_COTS_ORD:
		return SOCK_STREAM;
	case NC_TPI_RAW:
		return SOCK_RAW;
	default:
		break;
	}

	return -1;
}

int
__rpc_socktype2seman(int socktype)
{
	switch (socktype) {
	case SOCK_DGRAM:
		return NC_TPI_CLTS;
	case SOCK_STREAM:
		return NC_TPI_COTS_ORD;
	case SOCK_RAW:
		return NC_TPI_RAW;
	default:
		break;
	}

	return -1;
}

/*
 * XXXX - IPv6 scope IDs can't be handled in universal addresses.
 * Here, we compare the original server address to that of the RPC
 * service we just received back from a call to rpcbind on the remote
 * machine. If they are both "link local" or "site local", copy
 * the scope id of the server address over to the service address.
 */
int
__rpc_fixup_addr(struct netbuf *new, const struct netbuf *svc)
{
#ifdef INET6
	struct sockaddr *sa_new, *sa_svc;
	struct sockaddr_in6 *sin6_new, *sin6_svc;

	sa_svc = (struct sockaddr *)svc->buf;
	sa_new = (struct sockaddr *)new->buf;

	if (sa_new->sa_family == sa_svc->sa_family &&
	    sa_new->sa_family == AF_INET6) {
		sin6_new = (struct sockaddr_in6 *)new->buf;
		sin6_svc = (struct sockaddr_in6 *)svc->buf;

		if ((IN6_IS_ADDR_LINKLOCAL(&sin6_new->sin6_addr) &&
		     IN6_IS_ADDR_LINKLOCAL(&sin6_svc->sin6_addr)) ||
		    (IN6_IS_ADDR_SITELOCAL(&sin6_new->sin6_addr) &&
		     IN6_IS_ADDR_SITELOCAL(&sin6_svc->sin6_addr))) {
			sin6_new->sin6_scope_id = sin6_svc->sin6_scope_id;
		}
	}
#endif
	return 1;
}

int
__rpc_sockisbound(int fd)
{
	struct sockaddr_storage ss;
	socklen_t slen;

	slen = sizeof (struct sockaddr_storage);
	if (_getsockname(fd, (struct sockaddr *)(void *)&ss, &slen) < 0)
		return 0;

	switch (ss.ss_family) {
		case AF_INET:
			return (((struct sockaddr_in *)
			    (void *)&ss)->sin_port != 0);
#ifdef INET6
		case AF_INET6:
			return (((struct sockaddr_in6 *)
			    (void *)&ss)->sin6_port != 0);
#endif
		case AF_LOCAL:
			/* XXX check this */
			return (((struct sockaddr_un *)
			    (void *)&ss)->sun_path[0] != '\0');
		default:
			break;
	}

	return 0;
}
