/*	$OpenBSD: ypserv_db.c,v 1.32 2021/10/09 18:43:51 deraadt Exp $ */

/*
 * Copyright (c) 1994 Mats O Jansson <moj@stacken.kth.se>
 * Copyright (c) 1996 Charles D. Cranor
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

/*
 * major revision/cleanup of Mats' version
 * done by Chuck Cranor <chuck@ccrc.wustl.edu>
 * Jan 1996.
 */

#include <rpc/rpc.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <resolv.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <errno.h>
#include "yplog.h"
#include "ypdb.h"
#include "ypdef.h"
#include "ypserv.h"

LIST_HEAD(domainlist, opt_domain);	/* LIST of domains */
LIST_HEAD(maplist, opt_map);		/* LIST of maps (in a domain) */
TAILQ_HEAD(mapq, opt_map);		/* TAILQ of maps (LRU) */

struct opt_map {
	mapname map;			/* map name (malloc'd) */
	DBM	*db;			/* database */
	struct opt_domain *dom;         /* back ptr to our domain */
	int     host_lookup;            /* host lookup */
	int     secure;                 /* secure map? */
	TAILQ_ENTRY(opt_map) mapsq;   /* map queue pointers */
	LIST_ENTRY(opt_map) mapsl;      /* map list pointers */
};

struct opt_domain {
	domainname	domain;         /* domain name (malloc'd) */
	struct maplist	dmaps;          /* the domain's active maps */
	LIST_ENTRY(opt_domain) domsl;	/* global linked list of domains */
};

struct domainlist doms;			/* global list of domains */
struct mapq maps;			/* global queue of maps (LRU) */

extern int usedns;

/*
 * ypdb_init: init the queues and lists
 */
void
ypdb_init(void)
{
	LIST_INIT(&doms);
	TAILQ_INIT(&maps);
}

/*
 * yp_private:
 * Check if key is a YP private key. Return TRUE if it is and
 * ypprivate is FALSE.
 */
static int
yp_private(datum key, int ypprivate)
{
	if (ypprivate)
		return (FALSE);

	if (key.dsize == 0 || key.dptr == NULL)
		return (FALSE);

	if (key.dsize == YP_LAST_LEN &&
	    strncmp(key.dptr, YP_LAST_KEY, YP_LAST_LEN) == 0)
		return(TRUE);
	if (key.dsize == YP_INPUT_LEN &&
	    strncmp(key.dptr, YP_INPUT_KEY, YP_INPUT_LEN) == 0)
		return(TRUE);
	if (key.dsize == YP_OUTPUT_LEN &&
	    strncmp(key.dptr, YP_OUTPUT_KEY, YP_OUTPUT_LEN) == 0)
		return(TRUE);
	if (key.dsize == YP_MASTER_LEN &&
	    strncmp(key.dptr, YP_MASTER_KEY, YP_MASTER_LEN) == 0)
		return(TRUE);
	if (key.dsize == YP_DOMAIN_LEN &&
	    strncmp(key.dptr, YP_DOMAIN_KEY, YP_DOMAIN_LEN) == 0)
		return(TRUE);
	if (key.dsize == YP_INTERDOMAIN_LEN &&
	    strncmp(key.dptr, YP_INTERDOMAIN_KEY, YP_INTERDOMAIN_LEN) == 0)
		return(TRUE);
	if (key.dsize == YP_SECURE_LEN &&
	    strncmp(key.dptr, YP_SECURE_KEY, YP_SECURE_LEN) == 0)
		return(TRUE);
	return(FALSE);
}

/*
 * Close least recent used map. This routine is called when we have
 * no more file descripotors free, or we want to close all maps.
 */
static void
ypdb_close_last(void)
{
	struct opt_map *last;

	if (TAILQ_EMPTY(&maps)) {
		yplog("  ypdb_close_last: LRU list is empty!");
		return;
	}

	last = TAILQ_LAST(&maps, mapq);

	TAILQ_REMOVE(&maps, last, mapsq);	/* remove from LRU circleq */
	LIST_REMOVE(last, mapsl);		/* remove from domain list */

#ifdef DEBUG
	yplog("  ypdb_close_last: closing map %s in domain %s [db=0x%x]",
	    last->map, last->dom->domain, last->db);
#endif

	ypdb_close(last->db);			/* close DB */
	free(last->map);			/* free map name */
	free(last);				/* free map */
}

/*
 * Close all open maps.
 */
void
ypdb_close_all(void)
{

#ifdef DEBUG
	yplog("  ypdb_close_all(): start");
#endif
	while (!TAILQ_EMPTY(&maps))
		ypdb_close_last();
#ifdef DEBUG
	yplog("  ypdb_close_all(): done");
#endif
}

/*
 * Close Database if Open/Close Optimization isn't turned on.
 */
static void
ypdb_close_db(DBM *db)
{
#ifdef DEBUG
	yplog("  ypdb_close_db(0x%x)", db);
#endif
#ifndef OPTDB
	ypdb_close_all();
#endif
}

/*
 * ypdb_open_db
 */
DBM *
ypdb_open_db(domainname domain, mapname map, ypstat *status,
    struct opt_map **map_info)
{
	char map_path[PATH_MAX];
	static char   *domain_key = YP_INTERDOMAIN_KEY;
	static char   *secure_key = YP_SECURE_KEY;
	DBM	*db;
	struct opt_domain *d = NULL;
	struct opt_map	*m = NULL;
	datum	k, v;
#ifdef OPTDB
	int	i;
#endif

	/*
	 * check for preloaded domain, map
	 */
	LIST_FOREACH(d, &doms, domsl) {
		if (strcmp(domain, d->domain) == 0)
			break;
	}

	if (d) {
		LIST_FOREACH(m, &d->dmaps, mapsl)
			if (strcmp(map, m->map) == 0)
				break;
	}

	/*
	 * map found open?
	 */
	if (m) {
#ifdef DEBUG
		yplog("  ypdb_open_db: cached open: domain=%s, map=%s, db=0x%x",
		    domain, map, m->db);
#endif
		TAILQ_REMOVE(&maps, m, mapsq);	/* adjust LRU queue */
		TAILQ_INSERT_HEAD(&maps, m, mapsq);
		*status = YP_TRUE;
		if (map_info)
			*map_info = m;
		return(m->db);
	}

	/* Check for illegal charcaters */

	if (strchr(domain, '/')) {
		*status = YP_NODOM;
		return (NULL);
	}
	if (strchr(map, '/')) {
		*status = YP_NOMAP;
		return (NULL);
	}

	/*
	 * open map
	 */
#ifdef OPTDB
	i = 0;
	while (i == 0) {
#endif
		snprintf(map_path, sizeof(map_path), "%s/%s/%s", YP_DB_PATH,
		    domain, map);
		db = ypdb_open(map_path, O_RDONLY, 0444);
#ifdef OPTDB
		if (db == NULL) {
#ifdef DEBUG
			yplog("  ypdb_open_db: errno %d (%s)",
			    errno, sys_errlist[errno]);
#endif
			if (errno == ENFILE || errno == EMFILE) {
				ypdb_close_last();
			} else {
				i = errno;
			}
		} else {
			i = 4711;
		}
	}
#endif
	*status = YP_NOMAP;		/* see note below */
	if (db == NULL) {
		if (errno == ENOENT) {
#ifdef DEBUG
			yplog("  ypdb_open_db: no map %s (domain=%s)",
			    map, domain);
#endif
			return(NULL);
		}
#ifdef DEBUG
		yplog("  ypdb_open_db: ypdb_open FAILED: map %s (domain=%s)",
		    map, domain);
#endif
		return(NULL);
	}

	/*
	 * note: status now YP_NOMAP
	 */

	if (d == NULL) {		/* allocate new domain? */
		d = malloc(sizeof(*d));
		if (d)
			d->domain = strdup(domain);
		if (d == NULL || d->domain == NULL) {
			yplog("  ypdb_open_db: MALLOC failed");
			ypdb_close(db);
			free(d);
			return(NULL);
		}
		LIST_INIT(&d->dmaps);
		LIST_INSERT_HEAD(&doms, d, domsl);
#ifdef DEBUG
		yplog("  ypdb_open_db: NEW DOMAIN %s", domain);
#endif
	}

	/*
	 * m must be NULL since we couldn't find a map.  allocate new one
	 */

	m = malloc(sizeof(*m));
	if (m)
		m->map = strdup(map);
	if (m == NULL || m->map == NULL) {
		free(m);
		yplog("  ypdb_open_db: MALLOC failed");
		ypdb_close(db);
		return(NULL);
	}
	m->db = db;
	m->dom = d;
	m->host_lookup = FALSE;
	TAILQ_INSERT_HEAD(&maps, m, mapsq);
	LIST_INSERT_HEAD(&d->dmaps, m, mapsl);
	if (strcmp(map, YP_HOSTNAME) == 0 || strcmp(map, YP_HOSTADDR) == 0) {
		if (!usedns) {
			k.dptr = domain_key;
			k.dsize = YP_INTERDOMAIN_LEN;
			v = ypdb_fetch(db, k);
			if (v.dptr)
				m->host_lookup = TRUE;
		} else
			m->host_lookup = TRUE;
	}
	m->secure = FALSE;
	k.dptr = secure_key;
	k.dsize = YP_SECURE_LEN;
	v = ypdb_fetch(db, k);
	if (v.dptr)
		m->secure = TRUE;
	*status = YP_TRUE;
	if (map_info)
		*map_info = m;
#ifdef DEBUG
	yplog("  ypdb_open_db: NEW MAP domain=%s, map=%s, hl=%d, s=%d, db=0x%x",
	    domain, map, m->host_lookup, m->secure, m->db);
#endif
	return(m->db);
}

/*
 * lookup host
 */
static ypstat
lookup_host(int nametable, int host_lookup, DBM *db, char *keystr,
    ypresp_val *result)
{
	struct hostent *host;
	struct in_addr *addr_name;
	struct in_addr addr_addr;
	static char val[BUFSIZ+1]; /* match libc */
	static char hostname[HOST_NAME_MAX+1];
	char tmpbuf[HOST_NAME_MAX+1 + 20], *v, *ptr;
	size_t len;
	int l;

	if (!host_lookup)
		return(YP_NOKEY);

	if ((_res.options & RES_INIT) == 0)
		res_init();
	bcopy("b", _res.lookups, sizeof("b"));

	if (nametable) {
		host = gethostbyname(keystr);
		if (host == NULL || host->h_addrtype != AF_INET)
			return(YP_NOKEY);
		addr_name = (struct in_addr *) *host->h_addr_list;
		v = val;
		for (; host->h_addr_list[0] != NULL; host->h_addr_list++) {
			addr_name = (struct in_addr *)host->h_addr_list[0];
			snprintf(tmpbuf, sizeof(tmpbuf), "%s %s\n",
			    inet_ntoa(*addr_name), host->h_name);
			len = strlcat(val, tmpbuf, sizeof(val));
			if (len >= sizeof(val))
				break;
			v = val + len;
		}
		result->val.valdat_val = val;
		result->val.valdat_len = v - val;
		return(YP_TRUE);
	}

	inet_aton(keystr, &addr_addr);
	host = gethostbyaddr((char *)&addr_addr, sizeof(addr_addr), AF_INET);
	if (host == NULL) return(YP_NOKEY);

	strncpy((char *)hostname, host->h_name, sizeof(hostname) - 1);
	hostname[sizeof(hostname) - 1] = '\0';
	host = gethostbyname((char *)hostname);
	if (host == NULL)
		return(YP_NOKEY);

	l = 0;
	for (; host->h_addr_list[0] != NULL; host->h_addr_list++)
		if (!bcmp(host->h_addr_list[0], &addr_addr, sizeof(addr_addr)))
			l++;
	if (l == 0) {
		yplog("lookup_host: address %s not listed for host %s\n",
		    inet_ntoa(addr_addr), hostname);
		syslog(LOG_NOTICE,
		    "ypserv: address %s not listed for host %s\n",
		    inet_ntoa(addr_addr), hostname);
		return(YP_NOKEY);
	}

	len = snprintf(val, sizeof(val), "%s %s", keystr, host->h_name);
	if (len < 0 || len >= sizeof(val))
		return(YP_YPERR);
	v = val + len;
	while ((ptr = *(host->h_aliases)) != NULL) {
		strlcat(val, " ", sizeof(val));
		len = strlcat(val, ptr, sizeof(val));
		if (len >= sizeof(val))
			break;
		v = val + len;
		host->h_aliases++;
	}
	result->val.valdat_val = val;
	result->val.valdat_len = v - val;

	return(YP_TRUE);
}

ypresp_val
ypdb_get_record(domainname domain, mapname map, keydat key, int ypprivate)
{
	static	ypresp_val res;
	static	char keystr[YPMAXRECORD+1];
	DBM	*db;
	datum	k, v;
	int	host_lookup = 0, hn;
	struct opt_map *map_info = NULL;

	bzero(&res, sizeof(res));

	db = ypdb_open_db(domain, map, &res.stat, &map_info);
	if (!db || res.stat < 0)
		return(res);
	if (map_info)
		host_lookup = map_info->host_lookup;

	k.dptr = key.keydat_val;
	k.dsize = key.keydat_len;

	if (yp_private(k, ypprivate)) {
		res.stat = YP_NOKEY;
		goto done;
	}

	v = ypdb_fetch(db, k);

	if (v.dptr == NULL) {
		res.stat = YP_NOKEY;
		if ((hn = strcmp(map, YP_HOSTNAME)) != 0 &&
		    strcmp(map, YP_HOSTADDR) != 0)
			goto done;
		/* note: lookup_host needs null terminated string */
		strncpy(keystr, key.keydat_val, key.keydat_len);
		keystr[key.keydat_len] = '\0';
		res.stat = lookup_host((hn == 0) ? TRUE : FALSE,
		    host_lookup, db, keystr, &res);
	} else {
		res.val.valdat_val = v.dptr;
		res.val.valdat_len = v.dsize;
	}

done:
	ypdb_close_db(db);
	return(res);
}

ypresp_key_val
ypdb_get_first(domainname domain, mapname map, int ypprivate)
{
	static ypresp_key_val res;
	DBM	*db;
	datum	k, v;

	bzero(&res, sizeof(res));

	db = ypdb_open_db(domain, map, &res.stat, NULL);

	if (res.stat >= 0) {
		k = ypdb_firstkey(db);

		while (yp_private(k, ypprivate))
			k = ypdb_nextkey(db);

		if (k.dptr == NULL) {
			res.stat = YP_NOKEY;
		} else {
			res.key.keydat_val = k.dptr;
			res.key.keydat_len = k.dsize;
			v = ypdb_fetch(db, k);
			if (v.dptr == NULL) {
				res.stat = YP_NOKEY;
			} else {
				res.val.valdat_val = v.dptr;
				res.val.valdat_len = v.dsize;
			}
		}
	}
	ypdb_close_db(db);
	return (res);
}

ypresp_key_val
ypdb_get_next(domainname domain, mapname map, keydat key, int ypprivate)
{
	static ypresp_key_val res;
	DBM	*db;
	datum	k, v, n;

	bzero(&res, sizeof(res));
	db = ypdb_open_db(domain, map, &res.stat, NULL);

	if (res.stat >= 0) {
		n.dptr = key.keydat_val;
		n.dsize = key.keydat_len;
		v.dptr = NULL;
		v.dsize = 0;
		k.dptr = NULL;
		k.dsize = 0;

		n = ypdb_setkey(db, n);

		if (n.dptr != NULL)
			k = ypdb_nextkey(db);
		else
			k.dptr = NULL;

		if (k.dptr != NULL) {
			while (yp_private(k, ypprivate))
				k = ypdb_nextkey(db);
		}

		if (k.dptr == NULL) {
			res.stat = YP_NOMORE;
		} else {
			res.key.keydat_val = k.dptr;
			res.key.keydat_len = k.dsize;
			v = ypdb_fetch(db, k);
			if (v.dptr == NULL) {
				res.stat = YP_NOMORE;
			} else {
				res.val.valdat_val = v.dptr;
				res.val.valdat_len = v.dsize;
			}
		}
	}
	ypdb_close_db(db);
	return (res);
}

ypresp_order
ypdb_get_order(domainname domain, mapname map)
{
	static ypresp_order res;
	static char   *order_key = YP_LAST_KEY;
	char   order[MAX_LAST_LEN+1];
	DBM	*db;
	datum	k, v;

	bzero(&res, sizeof(res));
	db = ypdb_open_db(domain, map, &res.stat, NULL);

	if (res.stat >= 0) {
		k.dptr = order_key;
		k.dsize = YP_LAST_LEN;

		v = ypdb_fetch(db, k);
		if (v.dptr == NULL) {
			res.stat = YP_NOKEY;
		} else {
			strncpy(order, v.dptr, v.dsize);
			order[v.dsize] = '\0';
			res.ordernum = (u_int32_t)atol(order);
		}
	}
	ypdb_close_db(db);
	return (res);
}

ypresp_master
ypdb_get_master(domainname domain, mapname map)
{
	static ypresp_master res;
	static char   *master_key = YP_MASTER_KEY;
	static char   master[MAX_MASTER_LEN+1];
	DBM	*db;
	datum	k, v;

	bzero(&res, sizeof(res));
	db = ypdb_open_db(domain, map, &res.stat, NULL);

	if (res.stat >= 0) {
		k.dptr = master_key;
		k.dsize = YP_MASTER_LEN;

		v = ypdb_fetch(db, k);
		if (v.dptr == NULL) {
			res.stat = YP_NOKEY;
		} else {
			strncpy(master, v.dptr, v.dsize);
			master[v.dsize] = '\0';
			res.peer = (peername) &master;
		}
	}
	ypdb_close_db(db);
	return (res);
}

bool_t
ypdb_xdr_get_all(XDR *xdrs, ypreq_nokey *req)
{
	static ypresp_all resp;
	DBM	*db;
	datum	k, v;

	bzero(&resp, sizeof(resp));

	/*
	 * open db, and advance past any private keys we may see
	 */
	db = ypdb_open_db(req->domain, req->map,
	    &resp.ypresp_all_u.val.stat, NULL);
	if (!db || resp.ypresp_all_u.val.stat < 0)
		return(FALSE);
	k = ypdb_firstkey(db);
	while (yp_private(k, FALSE))
		k = ypdb_nextkey(db);

	while (1) {
		if (k.dptr == NULL)
			break;

		v = ypdb_fetch(db, k);
		if (v.dptr == NULL)
			break;

		resp.more = TRUE;
		resp.ypresp_all_u.val.stat = YP_TRUE;
		resp.ypresp_all_u.val.key.keydat_val = k.dptr;
		resp.ypresp_all_u.val.key.keydat_len = k.dsize;
		resp.ypresp_all_u.val.val.valdat_val = v.dptr;
		resp.ypresp_all_u.val.val.valdat_len = v.dsize;

		if (!xdr_ypresp_all(xdrs, &resp)) {
#ifdef DEBUG
			yplog("  ypdb_xdr_get_all: xdr_ypresp_all failed");
#endif
			return(FALSE);
		}

		/* advance past private keys */
		k = ypdb_nextkey(db);
		while (yp_private(k, FALSE))
			k = ypdb_nextkey(db);
	}

	bzero(&resp, sizeof(resp));
	resp.ypresp_all_u.val.stat = YP_NOKEY;
	resp.more = FALSE;

	if (!xdr_ypresp_all(xdrs, &resp)) {
#ifdef DEBUG
		yplog("  ypdb_xdr_get_all: final xdr_ypresp_all failed");
#endif
		return(FALSE);
	}
	ypdb_close_db(db);
	return (TRUE);
}

int
ypdb_secure(domainname domain, mapname map)
{
	static	ypresp_val res;
	DBM	*db;
	int	secure;
	struct opt_map *map_info = NULL;

	bzero(&res, sizeof(res));
	secure = FALSE;

	db = ypdb_open_db(domain, map, &res.stat, &map_info);
	if (!db || res.stat < 0)
		return(secure);			/* ? */
	if (map_info)
		secure = map_info->secure;

	ypdb_close_db(db);
	return(secure);
}
