/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992/3 Theo de Raadt <deraadt@fsa.ca>
 * Copyright (c) 1998 Bill Paul <wpaul@ctr.columbia.edu>
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
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include "reentrant.h"
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/yp.h>
#include "un-namespace.h"
#include "libc_private.h"

/*
 * We have to define these here due to clashes between yp_prot.h and
 * yp.h.
 */

#define YPMATCHCACHE

#ifdef YPMATCHCACHE
struct ypmatch_ent {
        char			*ypc_map;
	keydat			ypc_key;
	valdat			ypc_val;
        time_t			ypc_expire_t;
        struct ypmatch_ent	*ypc_next;
};
#define YPLIB_MAXCACHE	5	/* At most 5 entries */
#define YPLIB_EXPIRE	5	/* Expire after 5 seconds */
#endif

struct dom_binding {
        struct dom_binding *dom_pnext;
        char dom_domain[YPMAXDOMAIN + 1];
        struct sockaddr_in dom_server_addr;
        u_short dom_server_port;
        int dom_socket;
        CLIENT *dom_client;
        u_short dom_local_port; /* now I finally know what this is for. */
        long dom_vers;
#ifdef YPMATCHCACHE
	struct ypmatch_ent *cache;
	int ypmatch_cachecnt;
#endif
};

#include <rpcsvc/ypclnt.h>

#ifndef BINDINGDIR
#define BINDINGDIR "/var/yp/binding"
#endif
#define MAX_RETRIES 20

extern bool_t xdr_domainname(), xdr_ypbind_resp();
extern bool_t xdr_ypreq_key(), xdr_ypresp_val();
extern bool_t xdr_ypreq_nokey(), xdr_ypresp_key_val();
extern bool_t xdr_ypresp_all(), xdr_ypresp_all_seq();
extern bool_t xdr_ypresp_master();

int (*ypresp_allfn)();
void *ypresp_data;

static void _yp_unbind(struct dom_binding *);
struct dom_binding *_ypbindlist;
static char _yp_domain[MAXHOSTNAMELEN];
int _yplib_timeout = 20;

static mutex_t _ypmutex = MUTEX_INITIALIZER;
#define YPLOCK()	mutex_lock(&_ypmutex);
#define YPUNLOCK()	mutex_unlock(&_ypmutex);

#ifdef YPMATCHCACHE
static void
ypmatch_cache_delete(struct dom_binding *ypdb, struct ypmatch_ent *prev,
    struct ypmatch_ent *cur)
{
	if (prev == NULL)
		ypdb->cache = cur->ypc_next;
	else
		prev->ypc_next = cur->ypc_next;

	free(cur->ypc_map);
	free(cur->ypc_key.keydat_val);
	free(cur->ypc_val.valdat_val);
	free(cur);

	ypdb->ypmatch_cachecnt--;

	return;
}

static void
ypmatch_cache_flush(struct dom_binding *ypdb)
{
	struct ypmatch_ent	*n, *c = ypdb->cache;

	while (c != NULL) {
		n = c->ypc_next;
		ypmatch_cache_delete(ypdb, NULL, c);
		c = n;
	}

	return;
}

static void
ypmatch_cache_expire(struct dom_binding *ypdb)
{
	struct ypmatch_ent	*c = ypdb->cache;
	struct ypmatch_ent	*n, *p = NULL;
	time_t			t;

	time(&t);

	while (c != NULL) {
		if (t >= c->ypc_expire_t) {
			n = c->ypc_next;
			ypmatch_cache_delete(ypdb, p, c);
			c = n;
		} else {
			p = c;
			c = c->ypc_next;
		}
	}

	return;
}

static void
ypmatch_cache_insert(struct dom_binding *ypdb, char *map, keydat *key,
    valdat *val)
{
	struct ypmatch_ent	*new;

	/* Do an expire run to maybe open up a slot. */
	if (ypdb->ypmatch_cachecnt)
		ypmatch_cache_expire(ypdb);

	/*
	 * If there are no slots free, then force an expire of
	 * the least recently used entry.
 	 */
	if (ypdb->ypmatch_cachecnt >= YPLIB_MAXCACHE) {
		struct ypmatch_ent	*o = NULL, *c = ypdb->cache;
		time_t			oldest = 0;

		oldest = ~oldest;

		while (c != NULL) {
			if (c->ypc_expire_t < oldest) {
				oldest = c->ypc_expire_t;
				o = c;
			}
			c = c->ypc_next;
		}

		if (o == NULL)
			return;
		o->ypc_expire_t = 0;
		ypmatch_cache_expire(ypdb);
	}

	new = malloc(sizeof(struct ypmatch_ent));
	if (new == NULL)
		return;

	new->ypc_map = strdup(map);
	if (new->ypc_map == NULL) {
		free(new);
		return;
	}
	new->ypc_key.keydat_val = malloc(key->keydat_len);
	if (new->ypc_key.keydat_val == NULL) {
		free(new->ypc_map);
		free(new);
		return;
	}
	new->ypc_val.valdat_val = malloc(val->valdat_len);
	if (new->ypc_val.valdat_val == NULL) {
		free(new->ypc_val.valdat_val);
		free(new->ypc_map);
		free(new);
		return;
	}

	new->ypc_expire_t = time(NULL) + YPLIB_EXPIRE;
	new->ypc_key.keydat_len = key->keydat_len;
	new->ypc_val.valdat_len = val->valdat_len;
	bcopy(key->keydat_val, new->ypc_key.keydat_val, key->keydat_len);
	bcopy(val->valdat_val, new->ypc_val.valdat_val, val->valdat_len);

	new->ypc_next = ypdb->cache;
	ypdb->cache = new;

	ypdb->ypmatch_cachecnt++;

	return;
}

static bool_t
ypmatch_cache_lookup(struct dom_binding *ypdb, char *map, keydat *key,
    valdat *val)
{
	struct ypmatch_ent	*c;

	ypmatch_cache_expire(ypdb);

	for (c = ypdb->cache; c != NULL; c = c->ypc_next) {
		if (strcmp(map, c->ypc_map))
			continue;
		if (key->keydat_len != c->ypc_key.keydat_len)
			continue;
		if (bcmp(key->keydat_val, c->ypc_key.keydat_val,
				key->keydat_len))
			continue;
	}

	if (c == NULL)
		return(FALSE);

	val->valdat_len = c->ypc_val.valdat_len;
	val->valdat_val = c->ypc_val.valdat_val;

	return(TRUE);
}
#endif

const char *
ypbinderr_string(int incode)
{
	static char err[80];
	switch (incode) {
	case 0:
		return ("Success");
	case YPBIND_ERR_ERR:
		return ("Internal ypbind error");
	case YPBIND_ERR_NOSERV:
		return ("Domain not bound");
	case YPBIND_ERR_RESC:
		return ("System resource allocation failure");
	}
	sprintf(err, "Unknown ypbind error: #%d\n", incode);
	return (err);
}

int
_yp_dobind(char *dom, struct dom_binding **ypdb)
{
	static pid_t pid = -1;
	char path[MAXPATHLEN];
	struct dom_binding *ysd, *ysd2;
	struct ypbind_resp ypbr;
	struct timeval tv;
	struct sockaddr_in clnt_sin;
	int clnt_sock, fd;
	pid_t gpid;
	CLIENT *client;
	int new = 0, r;
	int retries = 0;
	struct sockaddr_in check;
	socklen_t checklen = sizeof(struct sockaddr_in);

	/* Not allowed; bad doggie. Bad. */
	if (strchr(dom, '/') != NULL)
		return(YPERR_BADARGS);

	gpid = getpid();
	if (!(pid == -1 || pid == gpid)) {
		ysd = _ypbindlist;
		while (ysd) {
			if (ysd->dom_client != NULL)
				_yp_unbind(ysd);
			ysd2 = ysd->dom_pnext;
			free(ysd);
			ysd = ysd2;
		}
		_ypbindlist = NULL;
	}
	pid = gpid;

	if (ypdb != NULL)
		*ypdb = NULL;

	if (dom == NULL || strlen(dom) == 0)
		return (YPERR_BADARGS);

	for (ysd = _ypbindlist; ysd; ysd = ysd->dom_pnext)
		if (strcmp(dom, ysd->dom_domain) == 0)
			break;


	if (ysd == NULL) {
		ysd = (struct dom_binding *)malloc(sizeof *ysd);
		if (ysd == NULL)
			return (YPERR_RESRC);
		bzero((char *)ysd, sizeof *ysd);
		ysd->dom_socket = -1;
		ysd->dom_vers = 0;
		new = 1;
	} else {
	/* Check the socket -- may have been hosed by the caller. */
		if (_getsockname(ysd->dom_socket, (struct sockaddr *)&check,
		    &checklen) == -1 || check.sin_family != AF_INET ||
		    check.sin_port != ysd->dom_local_port) {
		/* Socket became bogus somehow... need to rebind. */
			int save, sock;

			sock = ysd->dom_socket;
			save = _dup(ysd->dom_socket);
			if (ysd->dom_client != NULL)
				clnt_destroy(ysd->dom_client);
			ysd->dom_vers = 0;
			ysd->dom_client = NULL;
			sock = _dup2(save, sock);
			_close(save);
		}
	}

again:
	retries++;
	if (retries > MAX_RETRIES) {
		if (new)
			free(ysd);
		return(YPERR_YPBIND);
	}
#ifdef BINDINGDIR
	if (ysd->dom_vers == 0) {
		/*
		 * We're trying to make a new binding: zorch the
		 * existing handle now (if any).
		 */
		if (ysd->dom_client != NULL) {
			clnt_destroy(ysd->dom_client);
			ysd->dom_client = NULL;
			ysd->dom_socket = -1;
		}
		snprintf(path, sizeof(path), "%s/%s.%d", BINDINGDIR, dom, 2);
		if ((fd = _open(path, O_RDONLY | O_CLOEXEC)) == -1) {
			/* no binding file, YP is dead. */
			/* Try to bring it back to life. */
			_close(fd);
			goto skipit;
		}
		if (_flock(fd, LOCK_EX|LOCK_NB) == -1 && errno == EWOULDBLOCK) {
			struct iovec iov[2];
			struct ypbind_resp ybr;
			u_short	ypb_port;

			iov[0].iov_base = (caddr_t)&ypb_port;
			iov[0].iov_len = sizeof ypb_port;
			iov[1].iov_base = (caddr_t)&ybr;
			iov[1].iov_len = sizeof ybr;

			r = _readv(fd, iov, 2);
			if (r != iov[0].iov_len + iov[1].iov_len) {
				_close(fd);
				ysd->dom_vers = -1;
				goto again;
			}

			bzero(&ysd->dom_server_addr, sizeof ysd->dom_server_addr);
			ysd->dom_server_addr.sin_family = AF_INET;
			ysd->dom_server_addr.sin_len = sizeof(struct sockaddr_in);
			bcopy(&ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr,
			    &ysd->dom_server_addr.sin_addr.s_addr,
			    sizeof(ysd->dom_server_addr.sin_addr.s_addr));
			bcopy(&ybr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port,
			    &ysd->dom_server_addr.sin_port,
			    sizeof(ysd->dom_server_addr.sin_port));

			ysd->dom_server_port = ysd->dom_server_addr.sin_port;
			_close(fd);
			goto gotit;
		} else {
			/* no lock on binding file, YP is dead. */
			/* Try to bring it back to life. */
			_close(fd);
			goto skipit;
		}
	}
skipit:
#endif
	if (ysd->dom_vers == -1 || ysd->dom_vers == 0) {
		/*
		 * We're trying to make a new binding: zorch the
		 * existing handle now (if any).
		 */
		if (ysd->dom_client != NULL) {
			clnt_destroy(ysd->dom_client);
			ysd->dom_client = NULL;
			ysd->dom_socket = -1;
		}
		bzero((char *)&clnt_sin, sizeof clnt_sin);
		clnt_sin.sin_family = AF_INET;
		clnt_sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		clnt_sock = RPC_ANYSOCK;
		client = clnttcp_create(&clnt_sin, YPBINDPROG, YPBINDVERS, &clnt_sock,
			0, 0);
		if (client == NULL) {
			/*
			 * These conditions indicate ypbind just isn't
			 * alive -- we probably don't want to shoot our
			 * mouth off in this case; instead generate error
			 * messages only for really exotic problems.
			 */
			if (rpc_createerr.cf_stat != RPC_PROGNOTREGISTERED &&
			   (rpc_createerr.cf_stat != RPC_SYSTEMERROR &&
			   rpc_createerr.cf_error.re_errno == ECONNREFUSED))
				clnt_pcreateerror("clnttcp_create");
			if (new)
				free(ysd);
			return (YPERR_YPBIND);
		}

		/*
		 * Check the port number -- should be < IPPORT_RESERVED.
		 * If not, it's possible someone has registered a bogus
		 * ypbind with the portmapper and is trying to trick us.
		 */
		if (ntohs(clnt_sin.sin_port) >= IPPORT_RESERVED) {
			if (client != NULL)
				clnt_destroy(client);
			if (new)
				free(ysd);
			return(YPERR_YPBIND);
		}
		tv.tv_sec = _yplib_timeout/2;
		tv.tv_usec = 0;
		r = clnt_call(client, YPBINDPROC_DOMAIN,
			(xdrproc_t)xdr_domainname, &dom,
			(xdrproc_t)xdr_ypbind_resp, &ypbr, tv);
		if (r != RPC_SUCCESS) {
			clnt_destroy(client);
			ysd->dom_vers = -1;
			if (r == RPC_PROGUNAVAIL || r == RPC_PROCUNAVAIL) {
				if (new)
					free(ysd);
				return(YPERR_YPBIND);
			}
			fprintf(stderr,
			"YP: server for domain %s not responding, retrying\n", dom);
			goto again;
		} else {
			if (ypbr.ypbind_status != YPBIND_SUCC_VAL) {
				struct timespec time_to_sleep, time_remaining;

				clnt_destroy(client);
				ysd->dom_vers = -1;

				time_to_sleep.tv_sec = _yplib_timeout/2;
				time_to_sleep.tv_nsec = 0;
				_nanosleep(&time_to_sleep,
				    &time_remaining);
				goto again;
			}
		}
		clnt_destroy(client);

		bzero((char *)&ysd->dom_server_addr, sizeof ysd->dom_server_addr);
		ysd->dom_server_addr.sin_family = AF_INET;
		bcopy(&ypbr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port,
		    &ysd->dom_server_addr.sin_port,
		    sizeof(ysd->dom_server_addr.sin_port));
		bcopy(&ypbr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_addr,
		    &ysd->dom_server_addr.sin_addr.s_addr,
		    sizeof(ysd->dom_server_addr.sin_addr.s_addr));

		/*
		 * We could do a reserved port check here too, but this
		 * could pose compatibility problems. The local ypbind is
		 * supposed to decide whether or not to trust yp servers
		 * on insecure ports. For now, we trust its judgement.
		 */
		ysd->dom_server_port =
			*(u_short *)&ypbr.ypbind_resp_u.ypbind_bindinfo.ypbind_binding_port;
gotit:
		ysd->dom_vers = YPVERS;
		strlcpy(ysd->dom_domain, dom, sizeof(ysd->dom_domain));
	}

	/* Don't rebuild the connection to the server unless we have to. */
	if (ysd->dom_client == NULL) {
		tv.tv_sec = _yplib_timeout/2;
		tv.tv_usec = 0;
		ysd->dom_socket = RPC_ANYSOCK;
		ysd->dom_client = clntudp_bufcreate(&ysd->dom_server_addr,
			YPPROG, YPVERS, tv, &ysd->dom_socket, 1280, 2304);
		if (ysd->dom_client == NULL) {
			clnt_pcreateerror("clntudp_create");
			ysd->dom_vers = -1;
			goto again;
		}
		if (_fcntl(ysd->dom_socket, F_SETFD, 1) == -1)
			perror("fcntl: F_SETFD");
		/*
		 * We want a port number associated with this socket
		 * so that we can check its authenticity later.
		 */
		checklen = sizeof(struct sockaddr_in);
		bzero((char *)&check, checklen);
		_bind(ysd->dom_socket, (struct sockaddr *)&check, checklen);
		check.sin_family = AF_INET;
		if (!_getsockname(ysd->dom_socket,
		    (struct sockaddr *)&check, &checklen)) {
			ysd->dom_local_port = check.sin_port;
		} else {
			clnt_destroy(ysd->dom_client);
			if (new)
				free(ysd);
			return(YPERR_YPBIND);
		}
	}

	if (new) {
		ysd->dom_pnext = _ypbindlist;
		_ypbindlist = ysd;
	}

	/*
	 * Set low retry timeout to realistically handle UDP packet
	 * loss for YP packet bursts.
	 */
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	clnt_control(ysd->dom_client, CLSET_RETRY_TIMEOUT, (char*)&tv);

	if (ypdb != NULL)
		*ypdb = ysd;
	return (0);
}

static void
_yp_unbind(struct dom_binding *ypb)
{
	struct sockaddr_in check;
	socklen_t checklen = sizeof(struct sockaddr_in);

	if (ypb->dom_client != NULL) {
		/* Check the socket -- may have been hosed by the caller. */
		if (_getsockname(ypb->dom_socket, (struct sockaddr *)&check,
	    	&checklen) == -1 || check.sin_family != AF_INET ||
	    	check.sin_port != ypb->dom_local_port) {
			int save, sock;

			sock = ypb->dom_socket;
			save = _dup(ypb->dom_socket);
			clnt_destroy(ypb->dom_client);
			sock = _dup2(save, sock);
			_close(save);
		} else
			clnt_destroy(ypb->dom_client);
	}

	ypb->dom_client = NULL;
	ypb->dom_socket = -1;
	ypb->dom_vers = -1;
#ifdef YPMATCHCACHE
	ypmatch_cache_flush(ypb);
#endif
}

static int
yp_bind_locked(char *dom)
{
	return (_yp_dobind(dom, NULL));
}

int
yp_bind(char *dom)
{
	int r;

	YPLOCK();
	r = yp_bind_locked(dom);
	YPUNLOCK();
	return (r);
}

static void
yp_unbind_locked(char *dom)
{
	struct dom_binding *ypb, *ypbp;

	ypbp = NULL;
	for (ypb = _ypbindlist; ypb; ypb = ypb->dom_pnext) {
		if (strcmp(dom, ypb->dom_domain) == 0) {
			_yp_unbind(ypb);
			if (ypbp)
				ypbp->dom_pnext = ypb->dom_pnext;
			else
				_ypbindlist = ypb->dom_pnext;
			free(ypb);
			return;
		}
		ypbp = ypb;
	}
	return;
}

void
yp_unbind(char *dom)
{
	YPLOCK();
	yp_unbind_locked(dom);
	YPUNLOCK();
}

int
yp_match(char *indomain, char *inmap, const char *inkey, int inkeylen,
    char **outval, int *outvallen)
{
	struct dom_binding *ysd;
	struct ypresp_val yprv;
	struct timeval tv;
	struct ypreq_key yprk;
	int r;
	int retries = 0;
	*outval = NULL;
	*outvallen = 0;

	/* Sanity check */

	if (inkey == NULL || !strlen(inkey) || inkeylen <= 0 ||
	    inmap == NULL || !strlen(inmap) ||
	    indomain == NULL || !strlen(indomain))
		return (YPERR_BADARGS);

	YPLOCK();
	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return(YPERR_DOMAIN);
	}

	yprk.domain = indomain;
	yprk.map = inmap;
	yprk.key.keydat_val = (char *)inkey;
	yprk.key.keydat_len = inkeylen;

#ifdef YPMATCHCACHE
	if (ypmatch_cache_lookup(ysd, yprk.map, &yprk.key, &yprv.val) == TRUE) {
/*
	if (!strcmp(_yp_domain, indomain) && ypmatch_find(inmap, inkey,
	    inkeylen, &yprv.val.valdat_val, &yprv.val.valdat_len)) {
*/
		*outvallen = yprv.val.valdat_len;
		*outval = (char *)malloc(*outvallen+1);
		if (*outval == NULL) {
			_yp_unbind(ysd);
			*outvallen = 0;
			YPUNLOCK();
			return (YPERR_RESRC);
		}
		bcopy(yprv.val.valdat_val, *outval, *outvallen);
		(*outval)[*outvallen] = '\0';
		YPUNLOCK();
		return (0);
	}
	_yp_unbind(ysd);
#endif

again:
	if (retries > MAX_RETRIES) {
		YPUNLOCK();
		return (YPERR_RPC);
	}

	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return (YPERR_DOMAIN);
	}

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	bzero((char *)&yprv, sizeof yprv);

	r = clnt_call(ysd->dom_client, YPPROC_MATCH,
		(xdrproc_t)xdr_ypreq_key, &yprk,
		(xdrproc_t)xdr_ypresp_val, &yprv, tv);
	if (r != RPC_SUCCESS) {
		clnt_perror(ysd->dom_client, "yp_match: clnt_call");
		_yp_unbind(ysd);
		retries++;
		goto again;
	}

	if (!(r = ypprot_err(yprv.stat))) {
		*outvallen = yprv.val.valdat_len;
		*outval = (char *)malloc(*outvallen+1);
		if (*outval == NULL) {
			_yp_unbind(ysd);
			*outvallen = 0;
			xdr_free((xdrproc_t)xdr_ypresp_val, &yprv);
			YPUNLOCK();
			return (YPERR_RESRC);
		}
		bcopy(yprv.val.valdat_val, *outval, *outvallen);
		(*outval)[*outvallen] = '\0';
#ifdef YPMATCHCACHE
		ypmatch_cache_insert(ysd, yprk.map, &yprk.key, &yprv.val);
#endif
	}

	xdr_free((xdrproc_t)xdr_ypresp_val, &yprv);
	YPUNLOCK();
	return (r);
}

static int
yp_get_default_domain_locked(char **domp)
{
	*domp = NULL;
	if (_yp_domain[0] == '\0')
		if (getdomainname(_yp_domain, sizeof _yp_domain))
			return (YPERR_NODOM);
	*domp = _yp_domain;
	return (0);
}

int
yp_get_default_domain(char **domp)
{
	int r;

	YPLOCK();
	r = yp_get_default_domain_locked(domp);
	YPUNLOCK();
	return (r);
}

int
yp_first(char *indomain, char *inmap, char **outkey, int *outkeylen,
    char **outval, int *outvallen)
{
	struct ypresp_key_val yprkv;
	struct ypreq_nokey yprnk;
	struct dom_binding *ysd;
	struct timeval tv;
	int r;
	int retries = 0;
	/* Sanity check */

	if (indomain == NULL || !strlen(indomain) ||
	    inmap == NULL || !strlen(inmap))
		return (YPERR_BADARGS);

	*outkey = *outval = NULL;
	*outkeylen = *outvallen = 0;

	YPLOCK();
again:
	if (retries > MAX_RETRIES) {
		YPUNLOCK();
		return (YPERR_RPC);
	}

	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return (YPERR_DOMAIN);
	}

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	yprnk.domain = indomain;
	yprnk.map = inmap;
	bzero((char *)&yprkv, sizeof yprkv);

	r = clnt_call(ysd->dom_client, YPPROC_FIRST,
		(xdrproc_t)xdr_ypreq_nokey, &yprnk,
		(xdrproc_t)xdr_ypresp_key_val, &yprkv, tv);
	if (r != RPC_SUCCESS) {
		clnt_perror(ysd->dom_client, "yp_first: clnt_call");
		_yp_unbind(ysd);
		retries++;
		goto again;
	}
	if (!(r = ypprot_err(yprkv.stat))) {
		*outkeylen = yprkv.key.keydat_len;
		*outkey = (char *)malloc(*outkeylen+1);
		if (*outkey == NULL) {
			_yp_unbind(ysd);
			*outkeylen = 0;
			xdr_free((xdrproc_t)xdr_ypresp_key_val, &yprkv);
			YPUNLOCK();
			return (YPERR_RESRC);
		}
		bcopy(yprkv.key.keydat_val, *outkey, *outkeylen);
		(*outkey)[*outkeylen] = '\0';
		*outvallen = yprkv.val.valdat_len;
		*outval = (char *)malloc(*outvallen+1);
		if (*outval == NULL) {
			free(*outkey);
			_yp_unbind(ysd);
			*outkeylen = *outvallen = 0;
			xdr_free((xdrproc_t)xdr_ypresp_key_val, &yprkv);
			YPUNLOCK();
			return (YPERR_RESRC);
		}
		bcopy(yprkv.val.valdat_val, *outval, *outvallen);
		(*outval)[*outvallen] = '\0';
	}

	xdr_free((xdrproc_t)xdr_ypresp_key_val, &yprkv);
	YPUNLOCK();
	return (r);
}

int
yp_next(char *indomain, char *inmap, char *inkey, int inkeylen,
    char **outkey, int *outkeylen, char **outval, int *outvallen)
{
	struct ypresp_key_val yprkv;
	struct ypreq_key yprk;
	struct dom_binding *ysd;
	struct timeval tv;
	int r;
	int retries = 0;
	/* Sanity check */

	if (inkey == NULL || !strlen(inkey) || inkeylen <= 0 ||
	    inmap == NULL || !strlen(inmap) ||
	    indomain == NULL || !strlen(indomain))
		return (YPERR_BADARGS);

	*outkey = *outval = NULL;
	*outkeylen = *outvallen = 0;

	YPLOCK();
again:
	if (retries > MAX_RETRIES) {
		YPUNLOCK();
		return (YPERR_RPC);
	}

	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return (YPERR_DOMAIN);
	}

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	yprk.domain = indomain;
	yprk.map = inmap;
	yprk.key.keydat_val = inkey;
	yprk.key.keydat_len = inkeylen;
	bzero((char *)&yprkv, sizeof yprkv);

	r = clnt_call(ysd->dom_client, YPPROC_NEXT,
		(xdrproc_t)xdr_ypreq_key, &yprk,
		(xdrproc_t)xdr_ypresp_key_val, &yprkv, tv);
	if (r != RPC_SUCCESS) {
		clnt_perror(ysd->dom_client, "yp_next: clnt_call");
		_yp_unbind(ysd);
		retries++;
		goto again;
	}
	if (!(r = ypprot_err(yprkv.stat))) {
		*outkeylen = yprkv.key.keydat_len;
		*outkey = (char *)malloc(*outkeylen+1);
		if (*outkey == NULL) {
			_yp_unbind(ysd);
			*outkeylen = 0;
			xdr_free((xdrproc_t)xdr_ypresp_key_val, &yprkv);
			YPUNLOCK();
			return (YPERR_RESRC);
		}
		bcopy(yprkv.key.keydat_val, *outkey, *outkeylen);
		(*outkey)[*outkeylen] = '\0';
		*outvallen = yprkv.val.valdat_len;
		*outval = (char *)malloc(*outvallen+1);
		if (*outval == NULL) {
			free(*outkey);
			_yp_unbind(ysd);
			*outkeylen = *outvallen = 0;
			xdr_free((xdrproc_t)xdr_ypresp_key_val, &yprkv);
			YPUNLOCK();
			return (YPERR_RESRC);
		}
		bcopy(yprkv.val.valdat_val, *outval, *outvallen);
		(*outval)[*outvallen] = '\0';
	}

	xdr_free((xdrproc_t)xdr_ypresp_key_val, &yprkv);
	YPUNLOCK();
	return (r);
}

int
yp_all(char *indomain, char *inmap, struct ypall_callback *incallback)
{
	struct ypreq_nokey yprnk;
	struct dom_binding *ysd;
	struct timeval tv;
	struct sockaddr_in clnt_sin;
	CLIENT *clnt;
	u_long status, savstat;
	int clnt_sock;
	int retries = 0;
	/* Sanity check */

	if (indomain == NULL || !strlen(indomain) ||
	    inmap == NULL || !strlen(inmap))
		return (YPERR_BADARGS);

	YPLOCK();
again:
	if (retries > MAX_RETRIES) {
		YPUNLOCK();
		return (YPERR_RPC);
	}

	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return (YPERR_DOMAIN);
	}

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	/* YPPROC_ALL manufactures its own channel to ypserv using TCP */

	clnt_sock = RPC_ANYSOCK;
	clnt_sin = ysd->dom_server_addr;
	clnt_sin.sin_port = 0;
	clnt = clnttcp_create(&clnt_sin, YPPROG, YPVERS, &clnt_sock, 0, 0);
	if (clnt == NULL) {
		YPUNLOCK();
		printf("clnttcp_create failed\n");
		return (YPERR_PMAP);
	}

	yprnk.domain = indomain;
	yprnk.map = inmap;
	ypresp_allfn = incallback->foreach;
	ypresp_data = (void *)incallback->data;

	if (clnt_call(clnt, YPPROC_ALL,
		(xdrproc_t)xdr_ypreq_nokey, &yprnk,
		(xdrproc_t)xdr_ypresp_all_seq, &status, tv) != RPC_SUCCESS) {
			clnt_perror(clnt, "yp_all: clnt_call");
			clnt_destroy(clnt);
			_yp_unbind(ysd);
			retries++;
			goto again;
	}

	clnt_destroy(clnt);
	savstat = status;
	xdr_free((xdrproc_t)xdr_ypresp_all_seq, &status);	/* not really needed... */
	YPUNLOCK();
	if (savstat != YP_NOMORE)
		return (ypprot_err(savstat));
	return (0);
}

int
yp_order(char *indomain, char *inmap, int *outorder)
{
 	struct dom_binding *ysd;
	struct ypresp_order ypro;
	struct ypreq_nokey yprnk;
	struct timeval tv;
	int r;

	/* Sanity check */

	if (indomain == NULL || !strlen(indomain) ||
	    inmap == NULL || !strlen(inmap))
		return (YPERR_BADARGS);

	YPLOCK();
again:
	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return (YPERR_DOMAIN);
	}

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	yprnk.domain = indomain;
	yprnk.map = inmap;

	bzero((char *)(char *)&ypro, sizeof ypro);

	r = clnt_call(ysd->dom_client, YPPROC_ORDER,
		(xdrproc_t)xdr_ypreq_nokey, &yprnk,
		(xdrproc_t)xdr_ypresp_order, &ypro, tv);

	/*
	 * NIS+ in YP compat mode doesn't support the YPPROC_ORDER
	 * procedure.
	 */
	if (r == RPC_PROCUNAVAIL) {
		YPUNLOCK();
		return(YPERR_YPERR);
	}

	if (r != RPC_SUCCESS) {
		clnt_perror(ysd->dom_client, "yp_order: clnt_call");
		_yp_unbind(ysd);
		goto again;
	}

	if (!(r = ypprot_err(ypro.stat))) {
		*outorder = ypro.ordernum;
	}

	xdr_free((xdrproc_t)xdr_ypresp_order, &ypro);
	YPUNLOCK();
	return (r);
}

int
yp_master(char *indomain, char *inmap, char **outname)
{
	struct dom_binding *ysd;
	struct ypresp_master yprm;
	struct ypreq_nokey yprnk;
	struct timeval tv;
	int r;

	/* Sanity check */

	if (indomain == NULL || !strlen(indomain) ||
	    inmap == NULL || !strlen(inmap))
		return (YPERR_BADARGS);
	YPLOCK();
again:
	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return (YPERR_DOMAIN);
	}

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	yprnk.domain = indomain;
	yprnk.map = inmap;

	bzero((char *)&yprm, sizeof yprm);

	r = clnt_call(ysd->dom_client, YPPROC_MASTER,
		(xdrproc_t)xdr_ypreq_nokey, &yprnk,
		(xdrproc_t)xdr_ypresp_master, &yprm, tv);
	if (r != RPC_SUCCESS) {
		clnt_perror(ysd->dom_client, "yp_master: clnt_call");
		_yp_unbind(ysd);
		goto again;
	}

	if (!(r = ypprot_err(yprm.stat))) {
		*outname = (char *)strdup(yprm.peer);
	}

	xdr_free((xdrproc_t)xdr_ypresp_master, &yprm);
	YPUNLOCK();
	return (r);
}

int
yp_maplist(char *indomain, struct ypmaplist **outmaplist)
{
	struct dom_binding *ysd;
	struct ypresp_maplist ypml;
	struct timeval tv;
	int r;

	/* Sanity check */

	if (indomain == NULL || !strlen(indomain))
		return (YPERR_BADARGS);

	YPLOCK();
again:
	if (_yp_dobind(indomain, &ysd) != 0) {
		YPUNLOCK();
		return (YPERR_DOMAIN);
	}

	tv.tv_sec = _yplib_timeout;
	tv.tv_usec = 0;

	bzero((char *)&ypml, sizeof ypml);

	r = clnt_call(ysd->dom_client, YPPROC_MAPLIST,
		(xdrproc_t)xdr_domainname, &indomain,
		(xdrproc_t)xdr_ypresp_maplist, &ypml,tv);
	if (r != RPC_SUCCESS) {
		clnt_perror(ysd->dom_client, "yp_maplist: clnt_call");
		_yp_unbind(ysd);
		goto again;
	}
	if (!(r = ypprot_err(ypml.stat))) {
		*outmaplist = ypml.maps;
	}

	/* NO: xdr_free((xdrproc_t)xdr_ypresp_maplist, &ypml);*/
	YPUNLOCK();
	return (r);
}

const char *
yperr_string(int incode)
{
	static char err[80];

	switch (incode) {
	case 0:
		return ("Success");
	case YPERR_BADARGS:
		return ("Request arguments bad");
	case YPERR_RPC:
		return ("RPC failure");
	case YPERR_DOMAIN:
		return ("Can't bind to server which serves this domain");
	case YPERR_MAP:
		return ("No such map in server's domain");
	case YPERR_KEY:
		return ("No such key in map");
	case YPERR_YPERR:
		return ("YP server error");
	case YPERR_RESRC:
		return ("Local resource allocation failure");
	case YPERR_NOMORE:
		return ("No more records in map database");
	case YPERR_PMAP:
		return ("Can't communicate with portmapper");
	case YPERR_YPBIND:
		return ("Can't communicate with ypbind");
	case YPERR_YPSERV:
		return ("Can't communicate with ypserv");
	case YPERR_NODOM:
		return ("Local domain name not set");
	case YPERR_BADDB:
		return ("Server data base is bad");
	case YPERR_VERS:
		return ("YP server version mismatch - server can't supply service.");
	case YPERR_ACCESS:
		return ("Access violation");
	case YPERR_BUSY:
		return ("Database is busy");
	}
	sprintf(err, "YP unknown error %d\n", incode);
	return (err);
}

int
ypprot_err(unsigned int incode)
{
	switch (incode) {
	case YP_TRUE:
		return (0);
	case YP_FALSE:
		return (YPERR_YPBIND);
	case YP_NOMORE:
		return (YPERR_NOMORE);
	case YP_NOMAP:
		return (YPERR_MAP);
	case YP_NODOM:
		return (YPERR_DOMAIN);
	case YP_NOKEY:
		return (YPERR_KEY);
	case YP_BADOP:
		return (YPERR_YPERR);
	case YP_BADDB:
		return (YPERR_BADDB);
	case YP_YPERR:
		return (YPERR_YPERR);
	case YP_BADARGS:
		return (YPERR_BADARGS);
	case YP_VERS:
		return (YPERR_VERS);
	}
	return (YPERR_YPERR);
}

int
_yp_check(char **dom)
{
	char *unused;

	YPLOCK();
	if (_yp_domain[0]=='\0')
		if (yp_get_default_domain_locked(&unused)) {
			YPUNLOCK();
			return (0);
		}

	if (dom)
		*dom = _yp_domain;

	if (yp_bind_locked(_yp_domain) == 0) {
		yp_unbind_locked(_yp_domain);
		YPUNLOCK();
		return (1);
	}
	YPUNLOCK();
	return (0);
}
