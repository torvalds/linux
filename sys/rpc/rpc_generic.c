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

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>

#include <net/vnet.h>

#include <rpc/rpc.h>
#include <rpc/nettype.h>
#include <rpc/rpcsec_gss.h>

#include <rpc/rpc_com.h>

extern	u_long sb_max_adj;	/* not defined in socketvar.h */

#if __FreeBSD_version < 700000
#define strrchr rindex
#endif

/* Provide an entry point hook for the rpcsec_gss module. */
struct rpc_gss_entries	rpc_gss_entries;

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

struct rpc_createerr rpc_createerr;

/*
 * Find the appropriate buffer size
 */
u_int
/*ARGSUSED*/
__rpc_get_t_size(int af, int proto, int size)
{
	int defsize;

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
	return (size > sb_max_adj ? (u_int)sb_max_adj : (u_int)size);
}

/*
 * Find the appropriate address buffer size
 */
u_int
__rpc_get_a_size(af)
	int af;
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

/*
 * Used to ping the NULL procedure for clnt handle.
 * Returns NULL if fails, else a non-NULL pointer.
 */
void *
rpc_nullproc(clnt)
	CLIENT *clnt;
{
	struct timeval TIMEOUT = {25, 0};

	if (clnt_call(clnt, NULLPROC, (xdrproc_t) xdr_void, NULL,
		(xdrproc_t) xdr_void, NULL, TIMEOUT) != RPC_SUCCESS) {
		return (NULL);
	}
	return ((void *) clnt);
}

#endif

int
__rpc_socket2sockinfo(struct socket *so, struct __rpc_sockinfo *sip)
{
	int type, proto;
	struct sockaddr *sa;
	sa_family_t family;
	struct sockopt opt;
	int error;

	CURVNET_SET(so->so_vnet);
	error = so->so_proto->pr_usrreqs->pru_sockaddr(so, &sa);
	CURVNET_RESTORE();
	if (error)
		return 0;

	sip->si_alen = sa->sa_len;
	family = sa->sa_family;
	free(sa, M_SONAME);

	opt.sopt_dir = SOPT_GET;
	opt.sopt_level = SOL_SOCKET;
	opt.sopt_name = SO_TYPE;
	opt.sopt_val = &type;
	opt.sopt_valsize = sizeof type;
	opt.sopt_td = NULL;
	error = sogetopt(so, &opt);
	if (error)
		return 0;

	/* XXX */
	if (family != AF_LOCAL) {
		if (type == SOCK_STREAM)
			proto = IPPROTO_TCP;
		else if (type == SOCK_DGRAM)
			proto = IPPROTO_UDP;
		else
			return 0;
	} else
		proto = 0;

	sip->si_af = family;
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

struct socket *
__rpc_nconf2socket(const struct netconfig *nconf)
{
	struct __rpc_sockinfo si;
	struct socket *so;
	int error;

	if (!__rpc_nconf2sockinfo(nconf, &si))
		return 0;

	so = NULL;
	error =  socreate(si.si_af, &so, si.si_socktype, si.si_proto,
	    curthread->td_ucred, curthread);

	if (error)
		return NULL;
	else
		return so;
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
	struct sbuf sb;
	struct sockaddr_in *sin;
	struct sockaddr_un *sun;
	char namebuf[INET_ADDRSTRLEN];
#ifdef INET6
	struct sockaddr_in6 *sin6;
	char namebuf6[INET6_ADDRSTRLEN];
#endif
	u_int16_t port;

	sbuf_new(&sb, NULL, 0, SBUF_AUTOEXTEND);

	switch (af) {
	case AF_INET:
		if (nbuf->len < sizeof(*sin))
			return NULL;
		sin = nbuf->buf;
		if (inet_ntop(af, &sin->sin_addr, namebuf, sizeof namebuf)
		    == NULL)
			return NULL;
		port = ntohs(sin->sin_port);
		if (sbuf_printf(&sb, "%s.%u.%u", namebuf,
			((uint32_t)port) >> 8,
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
		if (sbuf_printf(&sb, "%s.%u.%u", namebuf6,
			((uint32_t)port) >> 8,
			port & 0xff) < 0)
			return NULL;
		break;
#endif
	case AF_LOCAL:
		sun = nbuf->buf;
		if (sbuf_printf(&sb, "%.*s", (int)(sun->sun_len -
			    offsetof(struct sockaddr_un, sun_path)),
			sun->sun_path) < 0)
			return (NULL);
		break;
	default:
		return NULL;
	}

	sbuf_finish(&sb);
	ret = strdup(sbuf_data(&sb), M_RPC);
	sbuf_delete(&sb);

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

	addrstr = strdup(uaddr, M_RPC);
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
		portlo = (unsigned)strtol(p + 1, NULL, 10);
		*p = '\0';

		p = strrchr(addrstr, '.');
		if (p == NULL)
			goto out;
		porthi = (unsigned)strtol(p + 1, NULL, 10);
		*p = '\0';
		port = (porthi << 8) | portlo;
	}

	ret = (struct netbuf *)malloc(sizeof *ret, M_RPC, M_WAITOK);
	
	switch (af) {
	case AF_INET:
		sin = (struct sockaddr_in *)malloc(sizeof *sin, M_RPC,
		    M_WAITOK);
		memset(sin, 0, sizeof *sin);
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		if (inet_pton(AF_INET, addrstr, &sin->sin_addr) <= 0) {
			free(sin, M_RPC);
			free(ret, M_RPC);
			ret = NULL;
			goto out;
		}
		sin->sin_len = ret->maxlen = ret->len = sizeof *sin;
		ret->buf = sin;
		break;
#ifdef INET6
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)malloc(sizeof *sin6, M_RPC,
		    M_WAITOK);
		memset(sin6, 0, sizeof *sin6);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		if (inet_pton(AF_INET6, addrstr, &sin6->sin6_addr) <= 0) {
			free(sin6, M_RPC);
			free(ret, M_RPC);
			ret = NULL;
			goto out;
		}
		sin6->sin6_len = ret->maxlen = ret->len = sizeof *sin6;
		ret->buf = sin6;
		break;
#endif
	case AF_LOCAL:
		sun = (struct sockaddr_un *)malloc(sizeof *sun, M_RPC,
		    M_WAITOK);
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
	free(addrstr, M_RPC);
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

/*
 * For the given nettype (tcp or udp only), return the first structure found.
 * This should be freed by calling freenetconfigent()
 */
struct netconfig *
__rpc_getconfip(const char *nettype)
{
	char *netid;
	static char *netid_tcp = (char *) NULL;
	static char *netid_udp = (char *) NULL;
	struct netconfig *dummy;

	if (!netid_udp && !netid_tcp) {
		struct netconfig *nconf;
		void *confighandle;

		if (!(confighandle = setnetconfig())) {
			log(LOG_ERR, "rpc: failed to open " NETCONFIG);
			return (NULL);
		}
		while ((nconf = getnetconfig(confighandle)) != NULL) {
			if (strcmp(nconf->nc_protofmly, NC_INET) == 0) {
				if (strcmp(nconf->nc_proto, NC_TCP) == 0) {
					netid_tcp = strdup(nconf->nc_netid,
					    M_RPC);
				} else
				if (strcmp(nconf->nc_proto, NC_UDP) == 0) {
					netid_udp = strdup(nconf->nc_netid,
					    M_RPC);
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
 *
 * For simplicity in the kernel, we don't support the NETPATH
 * environment variable. We behave as userland would then NETPATH is
 * unset, i.e. iterate over all visible entries in netconfig.
 */
void *
__rpc_setconf(nettype)
	const char *nettype;
{
	struct handle *handle;

	handle = (struct handle *) malloc(sizeof (struct handle),
	    M_RPC, M_WAITOK);
	switch (handle->nettype = getnettype(nettype)) {
	case _RPC_NETPATH:
	case _RPC_CIRCUIT_N:
	case _RPC_DATAGRAM_N:
		if (!(handle->nhandle = setnetconfig()))
			goto failed;
		handle->nflag = TRUE;
		break;
	case _RPC_VISIBLE:
	case _RPC_CIRCUIT_V:
	case _RPC_DATAGRAM_V:
	case _RPC_TCP:
	case _RPC_UDP:
		if (!(handle->nhandle = setnetconfig())) {
		        log(LOG_ERR, "rpc: failed to open " NETCONFIG);
			goto failed;
		}
		handle->nflag = FALSE;
		break;
	default:
		goto failed;
	}

	return (handle);

failed:
	free(handle, M_RPC);
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
		if (handle->nflag) {
			nconf = getnetconfig(handle->nhandle);
			if (nconf && !(nconf->nc_flag & NC_VISIBLE))
				continue;
		} else {
			nconf = getnetconfig(handle->nhandle);
		}
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
__rpc_endconf(vhandle)
	void * vhandle;
{
	struct handle *handle;

	handle = (struct handle *) vhandle;
	if (handle == NULL) {
		return;
	}
	endnetconfig(handle->nhandle);
	free(handle, M_RPC);
}

int
__rpc_sockisbound(struct socket *so)
{
	struct sockaddr *sa;
	int error, bound;

	CURVNET_SET(so->so_vnet);
	error = so->so_proto->pr_usrreqs->pru_sockaddr(so, &sa);
	CURVNET_RESTORE();
	if (error)
		return (0);

	switch (sa->sa_family) {
		case AF_INET:
			bound = (((struct sockaddr_in *) sa)->sin_port != 0);
			break;
#ifdef INET6
		case AF_INET6:
			bound = (((struct sockaddr_in6 *) sa)->sin6_port != 0);
			break;
#endif
		case AF_LOCAL:
			/* XXX check this */
			bound = (((struct sockaddr_un *) sa)->sun_path[0] != '\0');
			break;
		default:
			bound = FALSE;
			break;
	}

	free(sa, M_SONAME);

	return bound;
}

/*
 * Implement XDR-style API for RPC call.
 */
enum clnt_stat
clnt_call_private(
	CLIENT		*cl,		/* client handle */
	struct rpc_callextra *ext,	/* call metadata */
	rpcproc_t	proc,		/* procedure number */
	xdrproc_t	xargs,		/* xdr routine for args */
	void		*argsp,		/* pointer to args */
	xdrproc_t	xresults,	/* xdr routine for results */
	void		*resultsp,	/* pointer to results */
	struct timeval	utimeout)	/* seconds to wait before giving up */
{
	XDR xdrs;
	struct mbuf *mreq;
	struct mbuf *mrep;
	enum clnt_stat stat;

	mreq = m_getcl(M_WAITOK, MT_DATA, 0);

	xdrmbuf_create(&xdrs, mreq, XDR_ENCODE);
	if (!xargs(&xdrs, argsp)) {
		m_freem(mreq);
		return (RPC_CANTENCODEARGS);
	}
	XDR_DESTROY(&xdrs);

	stat = CLNT_CALL_MBUF(cl, ext, proc, mreq, &mrep, utimeout);
	m_freem(mreq);

	if (stat == RPC_SUCCESS) {
		xdrmbuf_create(&xdrs, mrep, XDR_DECODE);
		if (!xresults(&xdrs, resultsp)) {
			XDR_DESTROY(&xdrs);
			return (RPC_CANTDECODERES);
		}
		XDR_DESTROY(&xdrs);
	}

	return (stat);
}

/*
 * Bind a socket to a privileged IP port
 */
int
bindresvport(struct socket *so, struct sockaddr *sa)
{
	int old, error, af;
	bool_t freesa = FALSE;
	struct sockaddr_in *sin;
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif
	struct sockopt opt;
	int proto, portrange, portlow;
	u_int16_t *portp;
	socklen_t salen;

	if (sa == NULL) {
		CURVNET_SET(so->so_vnet);
		error = so->so_proto->pr_usrreqs->pru_sockaddr(so, &sa);
		CURVNET_RESTORE();
		if (error)
			return (error);
		freesa = TRUE;
		af = sa->sa_family;
		salen = sa->sa_len;
		memset(sa, 0, sa->sa_len);
	} else {
		af = sa->sa_family;
		salen = sa->sa_len;
	}

	switch (af) {
	case AF_INET:
		proto = IPPROTO_IP;
		portrange = IP_PORTRANGE;
		portlow = IP_PORTRANGE_LOW;
		sin = (struct sockaddr_in *)sa;
		portp = &sin->sin_port;
		break;
#ifdef INET6
	case AF_INET6:
		proto = IPPROTO_IPV6;
		portrange = IPV6_PORTRANGE;
		portlow = IPV6_PORTRANGE_LOW;
		sin6 = (struct sockaddr_in6 *)sa;
		portp = &sin6->sin6_port;
		break;
#endif
	default:
		return (EPFNOSUPPORT);
	}

	sa->sa_family = af;
	sa->sa_len = salen;

	if (*portp == 0) {
		bzero(&opt, sizeof(opt));
		opt.sopt_dir = SOPT_GET;
		opt.sopt_level = proto;
		opt.sopt_name = portrange;
		opt.sopt_val = &old;
		opt.sopt_valsize = sizeof(old);
		error = sogetopt(so, &opt);
		if (error) {
			goto out;
		}

		opt.sopt_dir = SOPT_SET;
		opt.sopt_val = &portlow;
		error = sosetopt(so, &opt);
		if (error)
			goto out;
	}

	error = sobind(so, sa, curthread);

	if (*portp == 0) {
		if (error) {
			opt.sopt_dir = SOPT_SET;
			opt.sopt_val = &old;
			sosetopt(so, &opt);
		}
	}
out:
	if (freesa)
		free(sa, M_SONAME);

	return (error);
}

/*
 * Kernel module glue
 */
static int
krpc_modevent(module_t mod, int type, void *data)
{

	return (0);
}
static moduledata_t krpc_mod = {
	"krpc",
	krpc_modevent,
	NULL,
};
DECLARE_MODULE(krpc, krpc_mod, SI_SUB_VFS, SI_ORDER_ANY);

/* So that loader and kldload(2) can find us, wherever we are.. */
MODULE_VERSION(krpc, 1);
