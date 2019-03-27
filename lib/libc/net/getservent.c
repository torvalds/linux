/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)getservent.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <nsswitch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif
#include "namespace.h"
#include "reentrant.h"
#include "un-namespace.h"
#include "netdb_private.h"
#ifdef NS_CACHING
#include "nscache.h"
#endif
#include "nss_tls.h"

enum constants
{
	SETSERVENT		= 1,
	ENDSERVENT		= 2,
	SERVENT_STORAGE_INITIAL	= 1 << 10, /* 1 KByte */
	SERVENT_STORAGE_MAX	= 1 << 20, /* 1 MByte */
};

struct servent_mdata
{
	enum nss_lookup_type how;
	int compat_mode;
};

static const ns_src defaultsrc[] = {
	{ NSSRC_COMPAT, NS_SUCCESS },
	{ NULL, 0 }
};

static int servent_unpack(char *, struct servent *, char **, size_t, int *);

/* files backend declarations */
struct files_state
{
	FILE *fp;
	int stayopen;

	int compat_mode_active;
};
static void files_endstate(void *);
NSS_TLS_HANDLING(files);

static int files_servent(void *, void *, va_list);
static int files_setservent(void *, void *, va_list);

/* db backend declarations */
struct db_state
{
        DB *db;
	int stayopen;
	int keynum;
};
static void db_endstate(void *);
NSS_TLS_HANDLING(db);

static int db_servent(void *, void *, va_list);
static int db_setservent(void *, void *, va_list);

#ifdef YP
/* nis backend declarations */
static 	int 	nis_servent(void *, void *, va_list);
static 	int 	nis_setservent(void *, void *, va_list);

struct nis_state
{
	int yp_stepping;
	char yp_domain[MAXHOSTNAMELEN];
	char *yp_key;
	int yp_keylen;
};
static void nis_endstate(void *);
NSS_TLS_HANDLING(nis);

static int nis_servent(void *, void *, va_list);
static int nis_setservent(void *, void *, va_list);
#endif

/* compat backend declarations */
static int compat_setservent(void *, void *, va_list);

/* get** wrappers for get**_r functions declarations */
struct servent_state {
	struct servent serv;
	char *buffer;
	size_t bufsize;
};
static	void	servent_endstate(void *);
NSS_TLS_HANDLING(servent);

struct key {
	const char *proto;
	union {
		const char *name;
		int port;
	};
};

static int wrap_getservbyname_r(struct key, struct servent *, char *, size_t,
    struct servent **);
static int wrap_getservbyport_r(struct key, struct servent *, char *, size_t,
    struct servent **);
static int wrap_getservent_r(struct key, struct servent *, char *, size_t,
    struct servent **);
static struct servent *getserv(int (*fn)(struct key, struct servent *, char *,
    size_t, struct servent **), struct key);

#ifdef NS_CACHING
static int serv_id_func(char *, size_t *, va_list, void *);
static int serv_marshal_func(char *, size_t *, void *, va_list, void *);
static int serv_unmarshal_func(char *, size_t, void *, va_list, void *);
#endif

static int
servent_unpack(char *p, struct servent *serv, char **aliases,
    size_t aliases_size, int *errnop)
{
	char *cp, **q, *endp;
	long l;

	if (*p == '#')
		return -1;

	memset(serv, 0, sizeof(struct servent));

	cp = strpbrk(p, "#\n");
	if (cp != NULL)
		*cp = '\0';
	serv->s_name = p;

	p = strpbrk(p, " \t");
	if (p == NULL)
		return -1;
	*p++ = '\0';
	while (*p == ' ' || *p == '\t')
		p++;
	cp = strpbrk(p, ",/");
	if (cp == NULL)
		return -1;

	*cp++ = '\0';
	l = strtol(p, &endp, 10);
	if (endp == p || *endp != '\0' || l < 0 || l > USHRT_MAX)
		return -1;
	serv->s_port = htons((in_port_t)l);
	serv->s_proto = cp;

	q = serv->s_aliases = aliases;
	cp = strpbrk(cp, " \t");
	if (cp != NULL)
		*cp++ = '\0';
	while (cp && *cp) {
		if (*cp == ' ' || *cp == '\t') {
			cp++;
			continue;
		}
		if (q < &aliases[aliases_size - 1]) {
			*q++ = cp;
		} else {
			*q = NULL;
			*errnop = ERANGE;
			return -1;
		}
		cp = strpbrk(cp, " \t");
		if (cp != NULL)
			*cp++ = '\0';
	}
	*q = NULL;

	return 0;
}

static int
parse_result(struct servent *serv, char *buffer, size_t bufsize,
    char *resultbuf, size_t resultbuflen, int *errnop)
{
	char **aliases;
	int aliases_size;

	if (bufsize <= resultbuflen + _ALIGNBYTES + sizeof(char *)) {
		*errnop = ERANGE;
		return (NS_RETURN);
	}
	aliases = (char **)_ALIGN(&buffer[resultbuflen + 1]);
	aliases_size = (buffer + bufsize - (char *)aliases) / sizeof(char *);
	if (aliases_size < 1) {
		*errnop = ERANGE;
		return (NS_RETURN);
	}

	memcpy(buffer, resultbuf, resultbuflen);
	buffer[resultbuflen] = '\0';

	if (servent_unpack(buffer, serv, aliases, aliases_size, errnop) != 0)
		return ((*errnop == 0) ? NS_NOTFOUND : NS_RETURN);
	return (NS_SUCCESS);
}

/* files backend implementation */
static	void
files_endstate(void *p)
{
	FILE * f;

	if (p == NULL)
		return;

	f = ((struct files_state *)p)->fp;
	if (f != NULL)
		fclose(f);

	free(p);
}

/*
 * compat structures. compat and files sources functionalities are almost
 * equal, so they all are managed by files_servent function
 */
static int
files_servent(void *retval, void *mdata, va_list ap)
{
	static const ns_src compat_src[] = {
#ifdef YP
		{ NSSRC_NIS, NS_SUCCESS },
#endif
		{ NULL, 0 }
	};
	ns_dtab compat_dtab[] = {
		{ NSSRC_DB, db_servent,
			(void *)((struct servent_mdata *)mdata)->how },
#ifdef YP
		{ NSSRC_NIS, nis_servent,
			(void *)((struct servent_mdata *)mdata)->how },
#endif
		{ NULL, NULL, NULL }
	};

	struct files_state *st;
	int rv;
	int stayopen;

	struct servent_mdata *serv_mdata;
	char *name;
	char *proto;
	int port;

	struct servent *serv;
	char *buffer;
	size_t bufsize;
	int *errnop;

	size_t linesize;
	char *line;
	char **cp;

	name = NULL;
	proto = NULL;
	serv_mdata = (struct servent_mdata *)mdata;
	switch (serv_mdata->how) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_id:
		port = va_arg(ap, int);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_all:
		break;
	default:
		return NS_NOTFOUND;
	}

	serv = va_arg(ap, struct servent *);
	buffer  = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap,int *);

	*errnop = files_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);

	if (st->fp == NULL)
		st->compat_mode_active = 0;

	if (st->fp == NULL && (st->fp = fopen(_PATH_SERVICES, "re")) == NULL) {
		*errnop = errno;
		return (NS_UNAVAIL);
	}

	if (serv_mdata->how == nss_lt_all)
		stayopen = 1;
	else {
		rewind(st->fp);
		stayopen = st->stayopen;
	}

	rv = NS_NOTFOUND;
	do {
		if (!st->compat_mode_active) {
			if ((line = fgetln(st->fp, &linesize)) == NULL) {
				*errnop = errno;
				rv = NS_RETURN;
				break;
			}

			if (*line=='+' && serv_mdata->compat_mode != 0)
				st->compat_mode_active = 1;
		}

		if (st->compat_mode_active != 0) {
			switch (serv_mdata->how) {
			case nss_lt_name:
				rv = nsdispatch(retval, compat_dtab,
				    NSDB_SERVICES_COMPAT, "getservbyname_r",
				    compat_src, name, proto, serv, buffer,
				    bufsize, errnop);
				break;
			case nss_lt_id:
				rv = nsdispatch(retval, compat_dtab,
				    NSDB_SERVICES_COMPAT, "getservbyport_r",
				    compat_src, port, proto, serv, buffer,
					bufsize, errnop);
				break;
			case nss_lt_all:
				rv = nsdispatch(retval, compat_dtab,
				    NSDB_SERVICES_COMPAT, "getservent_r",
				    compat_src, serv, buffer, bufsize, errnop);
				break;
			}

			if (!(rv & NS_TERMINATE) ||
			    serv_mdata->how != nss_lt_all)
				st->compat_mode_active = 0;

			continue;
		}

		rv = parse_result(serv, buffer, bufsize, line, linesize,
		    errnop);
		if (rv == NS_NOTFOUND)
			continue;
		if (rv == NS_RETURN)
			break;

		rv = NS_NOTFOUND;
		switch (serv_mdata->how) {
		case nss_lt_name:
			if (strcmp(name, serv->s_name) == 0)
				goto gotname;
			for (cp = serv->s_aliases; *cp; cp++)
				if (strcmp(name, *cp) == 0)
					goto gotname;

			continue;
		gotname:
			if (proto == NULL || strcmp(serv->s_proto, proto) == 0)
				rv = NS_SUCCESS;
			break;
		case nss_lt_id:
			if (port != serv->s_port)
				continue;

			if (proto == NULL || strcmp(serv->s_proto, proto) == 0)
				rv = NS_SUCCESS;
			break;
		case nss_lt_all:
			rv = NS_SUCCESS;
			break;
		}

	} while (!(rv & NS_TERMINATE));

	if (!stayopen && st->fp != NULL) {
		fclose(st->fp);
		st->fp = NULL;
	}

	if ((rv == NS_SUCCESS) && (retval != NULL))
		*(struct servent **)retval=serv;

	return (rv);
}

static int
files_setservent(void *retval, void *mdata, va_list ap)
{
	struct files_state *st;
	int rv;
	int f;

	rv = files_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);

	switch ((enum constants)mdata) {
	case SETSERVENT:
		f = va_arg(ap,int);
		if (st->fp == NULL)
			st->fp = fopen(_PATH_SERVICES, "re");
		else
			rewind(st->fp);
		st->stayopen |= f;
		break;
	case ENDSERVENT:
		if (st->fp != NULL) {
			fclose(st->fp);
			st->fp = NULL;
		}
		st->stayopen = 0;
		break;
	default:
		break;
	}

	st->compat_mode_active = 0;
	return (NS_UNAVAIL);
}

/* db backend implementation */
static	void
db_endstate(void *p)
{
	DB *db;

	if (p == NULL)
		return;

	db = ((struct db_state *)p)->db;
	if (db != NULL)
		db->close(db);

	free(p);
}

static int
db_servent(void *retval, void *mdata, va_list ap)
{
	char buf[BUFSIZ];
	DBT key, data, *result;
	DB *db;

	struct db_state *st;
	int rv;
	int stayopen;

	enum nss_lookup_type how;
	char *name;
	char *proto;
	int port;

	struct servent *serv;
	char *buffer;
	size_t bufsize;
	int *errnop;

	name = NULL;
	proto = NULL;
	how = (enum nss_lookup_type)mdata;
	switch (how) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_id:
		port = va_arg(ap, int);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_all:
		break;
	default:
		return NS_NOTFOUND;
	}

	serv = va_arg(ap, struct servent *);
	buffer  = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap,int *);

	*errnop = db_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);

	if (how == nss_lt_all && st->keynum < 0)
		return (NS_NOTFOUND);

	if (st->db == NULL) {
		st->db = dbopen(_PATH_SERVICES_DB, O_RDONLY, 0, DB_HASH, NULL);
		if (st->db == NULL) {
			*errnop = errno;
			return (NS_UNAVAIL);
		}
	}

	stayopen = (how == nss_lt_all) ? 1 : st->stayopen;
	db = st->db;

	do {
		switch (how) {
		case nss_lt_name:
			key.data = buf;
			if (proto == NULL)
				key.size = snprintf(buf, sizeof(buf),
				    "\376%s", name);
			else
				key.size = snprintf(buf, sizeof(buf),
				    "\376%s/%s", name, proto);
			key.size++;
			if (db->get(db, &key, &data, 0) != 0 ||
			    db->get(db, &data, &key, 0) != 0) {
				rv = NS_NOTFOUND;
				goto db_fin;
			}
			result = &key;
			break;
		case nss_lt_id:
			key.data = buf;
			port = htons(port);
			if (proto == NULL)
				key.size = snprintf(buf, sizeof(buf),
				    "\377%d", port);
			else
				key.size = snprintf(buf, sizeof(buf),
				    "\377%d/%s", port, proto);
			key.size++;
			if (db->get(db, &key, &data, 0) != 0 ||
			    db->get(db, &data, &key, 0) != 0) {
				rv = NS_NOTFOUND;
				goto db_fin;
			}
			result = &key;
			break;
		case nss_lt_all:
			key.data = buf;
			key.size = snprintf(buf, sizeof(buf), "%d",
			    st->keynum++);
			key.size++;
			if (db->get(db, &key, &data, 0) != 0) {
				st->keynum = -1;
				rv = NS_NOTFOUND;
				goto db_fin;
			}
			result = &data;
			break;
		}

		rv = parse_result(serv, buffer, bufsize, result->data,
		    result->size - 1, errnop);

	} while (!(rv & NS_TERMINATE) && how == nss_lt_all);

db_fin:
	if (!stayopen && st->db != NULL) {
		db->close(db);
		st->db = NULL;
	}

	if (rv == NS_SUCCESS && retval != NULL)
		*(struct servent **)retval = serv;

	return (rv);
}

static int
db_setservent(void *retval, void *mdata, va_list ap)
{
	DB *db;
	struct db_state *st;
	int rv;
	int f;

	rv = db_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);

	switch ((enum constants)mdata) {
	case SETSERVENT:
		f = va_arg(ap, int);
		st->stayopen |= f;
		st->keynum = 0;
		break;
	case ENDSERVENT:
		db = st->db;
		if (db != NULL) {
			db->close(db);
			st->db = NULL;
		}
		st->stayopen = 0;
		break;
	default:
		break;
	}

	return (NS_UNAVAIL);
}

/* nis backend implementation */
#ifdef YP
static 	void
nis_endstate(void *p)
{
	if (p == NULL)
		return;

	free(((struct nis_state *)p)->yp_key);
	free(p);
}

static int
nis_servent(void *retval, void *mdata, va_list ap)
{
	char *resultbuf, *lastkey;
	int resultbuflen;
	char buf[YPMAXRECORD + 2];

	struct nis_state *st;
	int rv;

	enum nss_lookup_type how;
	char *name;
	char *proto;
	int port;

	struct servent *serv;
	char *buffer;
	size_t bufsize;
	int *errnop;

	name = NULL;
	proto = NULL;
	how = (enum nss_lookup_type)mdata;
	switch (how) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_id:
		port = va_arg(ap, int);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_all:
		break;
	default:
		return NS_NOTFOUND;
	}

	serv = va_arg(ap, struct servent *);
	buffer  = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);

	*errnop = nis_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);

	if (st->yp_domain[0] == '\0') {
		if (getdomainname(st->yp_domain, sizeof st->yp_domain)) {
			*errnop = errno;
			return (NS_UNAVAIL);
		}
	}

	do {
		switch (how) {
		case nss_lt_name:
			snprintf(buf, sizeof(buf), "%s/%s", name, proto);
			if (yp_match(st->yp_domain, "services.byname", buf,
			    strlen(buf), &resultbuf, &resultbuflen)) {
				rv = NS_NOTFOUND;
				goto fin;
			}
			break;
		case nss_lt_id:
			snprintf(buf, sizeof(buf), "%d/%s", ntohs(port),
			    proto);

			/*
			 * We have to be a little flexible
			 * here. Ideally you're supposed to have both
			 * a services.byname and a services.byport
			 * map, but some systems have only
			 * services.byname. FreeBSD cheats a little by
			 * putting the services.byport information in
			 * the same map as services.byname so that
			 * either case will work. We allow for both
			 * possibilities here: if there is no
			 * services.byport map, we try services.byname
			 * instead.
			 */
			rv = yp_match(st->yp_domain, "services.byport", buf,
			    strlen(buf), &resultbuf, &resultbuflen);
			if (rv) {
				if (rv == YPERR_MAP) {
					if (yp_match(st->yp_domain,
					    "services.byname", buf,
					    strlen(buf), &resultbuf,
					    &resultbuflen)) {
						rv = NS_NOTFOUND;
						goto fin;
					}
				} else {
					rv = NS_NOTFOUND;
					goto fin;
				}
			}

			break;
		case nss_lt_all:
			if (!st->yp_stepping) {
				free(st->yp_key);
				rv = yp_first(st->yp_domain, "services.byname",
				    &st->yp_key, &st->yp_keylen, &resultbuf,
				    &resultbuflen);
				if (rv) {
					rv = NS_NOTFOUND;
					goto fin;
				}
				st->yp_stepping = 1;
			} else {
				lastkey = st->yp_key;
				rv = yp_next(st->yp_domain, "services.byname",
				    st->yp_key, st->yp_keylen, &st->yp_key,
				    &st->yp_keylen, &resultbuf, &resultbuflen);
				free(lastkey);
				if (rv) {
					st->yp_stepping = 0;
					rv = NS_NOTFOUND;
					goto fin;
				}
			}
			break;
		}

		rv = parse_result(serv, buffer, bufsize, resultbuf,
		    resultbuflen, errnop);
		free(resultbuf);

	} while (!(rv & NS_TERMINATE) && how == nss_lt_all);

fin:
	if (rv == NS_SUCCESS && retval != NULL)
		*(struct servent **)retval = serv;

	return (rv);
}

static int
nis_setservent(void *result, void *mdata, va_list ap)
{
	struct nis_state *st;
	int rv;

	rv = nis_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);

	switch ((enum constants)mdata) {
	case SETSERVENT:
	case ENDSERVENT:
		free(st->yp_key);
		st->yp_key = NULL;
		st->yp_stepping = 0;
		break;
	default:
		break;
	}

	return (NS_UNAVAIL);
}
#endif

/* compat backend implementation */
static int
compat_setservent(void *retval, void *mdata, va_list ap)
{
	static const ns_src compat_src[] = {
#ifdef YP
		{ NSSRC_NIS, NS_SUCCESS },
#endif
		{ NULL, 0 }
	};
	ns_dtab compat_dtab[] = {
		{ NSSRC_DB, db_setservent, mdata },
#ifdef YP
		{ NSSRC_NIS, nis_setservent, mdata },
#endif
		{ NULL, NULL, NULL }
	};
	int f;

	(void)files_setservent(retval, mdata, ap);

	switch ((enum constants)mdata) {
	case SETSERVENT:
		f = va_arg(ap,int);
		(void)nsdispatch(retval, compat_dtab, NSDB_SERVICES_COMPAT,
		    "setservent", compat_src, f);
		break;
	case ENDSERVENT:
		(void)nsdispatch(retval, compat_dtab, NSDB_SERVICES_COMPAT,
		    "endservent", compat_src);
		break;
	default:
		break;
	}

	return (NS_UNAVAIL);
}

#ifdef NS_CACHING
static int
serv_id_func(char *buffer, size_t *buffer_size, va_list ap, void *cache_mdata)
{
	char *name;
	char *proto;
	int port;

	size_t desired_size, size, size2;
	enum nss_lookup_type lookup_type;
	int res = NS_UNAVAIL;

	lookup_type = (enum nss_lookup_type)cache_mdata;
	switch (lookup_type) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		proto = va_arg(ap, char *);

		size = strlen(name);
		desired_size = sizeof(enum nss_lookup_type) + size + 1;
		if (proto != NULL) {
			size2 = strlen(proto);
			desired_size += size2 + 1;
		} else
			size2 = 0;

		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), name, size + 1);

		if (proto != NULL)
			memcpy(buffer + sizeof(enum nss_lookup_type) + size + 1,
			    proto, size2 + 1);

		res = NS_SUCCESS;
		break;
	case nss_lt_id:
		port = va_arg(ap, int);
		proto = va_arg(ap, char *);

		desired_size = sizeof(enum nss_lookup_type) + sizeof(int);
		if (proto != NULL) {
			size = strlen(proto);
			desired_size += size + 1;
		} else
			size = 0;

		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), &port,
		    sizeof(int));

		if (proto != NULL)
			memcpy(buffer + sizeof(enum nss_lookup_type) +
			    sizeof(int), proto, size + 1);

		res = NS_SUCCESS;
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

fin:
	*buffer_size = desired_size;
	return (res);
}

int
serv_marshal_func(char *buffer, size_t *buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	char *proto;
	int port;
	struct servent *serv;
	char *orig_buf;
	size_t orig_buf_size;

	struct servent new_serv;
	size_t desired_size;
	char **alias;
	char *p;
	size_t size;
	size_t aliases_size;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_id:
		port = va_arg(ap, int);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	serv = va_arg(ap, struct servent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);

	desired_size = _ALIGNBYTES + sizeof(struct servent) + sizeof(char *);
	if (serv->s_name != NULL)
		desired_size += strlen(serv->s_name) + 1;
	if (serv->s_proto != NULL)
		desired_size += strlen(serv->s_proto) + 1;

	aliases_size = 0;
	if (serv->s_aliases != NULL) {
		for (alias = serv->s_aliases; *alias; ++alias) {
			desired_size += strlen(*alias) + 1;
			++aliases_size;
		}

		desired_size += _ALIGNBYTES +
		    sizeof(char *) * (aliases_size + 1);
	}

	if (*buffer_size < desired_size) {
		/* this assignment is here for future use */
		*buffer_size = desired_size;
		return (NS_RETURN);
	}

	memcpy(&new_serv, serv, sizeof(struct servent));
	memset(buffer, 0, desired_size);

	*buffer_size = desired_size;
	p = buffer + sizeof(struct servent) + sizeof(char *);
	memcpy(buffer + sizeof(struct servent), &p, sizeof(char *));
	p = (char *)_ALIGN(p);

	if (new_serv.s_name != NULL) {
		size = strlen(new_serv.s_name);
		memcpy(p, new_serv.s_name, size);
		new_serv.s_name = p;
		p += size + 1;
	}

	if (new_serv.s_proto != NULL) {
		size = strlen(new_serv.s_proto);
		memcpy(p, new_serv.s_proto, size);
		new_serv.s_proto = p;
		p += size + 1;
	}

	if (new_serv.s_aliases != NULL) {
		p = (char *)_ALIGN(p);
		memcpy(p, new_serv.s_aliases, sizeof(char *) * aliases_size);
		new_serv.s_aliases = (char **)p;
		p += sizeof(char *) * (aliases_size + 1);

		for (alias = new_serv.s_aliases; *alias; ++alias) {
			size = strlen(*alias);
			memcpy(p, *alias, size);
			*alias = p;
			p += size + 1;
		}
	}

	memcpy(buffer, &new_serv, sizeof(struct servent));
	return (NS_SUCCESS);
}

int
serv_unmarshal_func(char *buffer, size_t buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	char *proto;
	int port;
	struct servent *serv;
	char *orig_buf;
	char *p;
	char **alias;
	size_t orig_buf_size;
	int *ret_errno;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_id:
		port = va_arg(ap, int);
		proto = va_arg(ap, char *);
		break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	serv = va_arg(ap, struct servent *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);
	ret_errno = va_arg(ap, int *);

	if (orig_buf_size <
	    buffer_size - sizeof(struct servent) - sizeof(char *)) {
		*ret_errno = ERANGE;
		return (NS_RETURN);
	}

	memcpy(serv, buffer, sizeof(struct servent));
	memcpy(&p, buffer + sizeof(struct servent), sizeof(char *));

	orig_buf = (char *)_ALIGN(orig_buf);
	memcpy(orig_buf, buffer + sizeof(struct servent) + sizeof(char *) +
	    (_ALIGN(p) - (size_t)p),
	    buffer_size - sizeof(struct servent) - sizeof(char *) -
	    (_ALIGN(p) - (size_t)p));
	p = (char *)_ALIGN(p);

	NS_APPLY_OFFSET(serv->s_name, orig_buf, p, char *);
	NS_APPLY_OFFSET(serv->s_proto, orig_buf, p, char *);
	if (serv->s_aliases != NULL) {
		NS_APPLY_OFFSET(serv->s_aliases, orig_buf, p, char **);

		for (alias = serv->s_aliases; *alias; ++alias)
			NS_APPLY_OFFSET(*alias, orig_buf, p, char *);
	}

	if (retval != NULL)
		*((struct servent **)retval) = serv;
	return (NS_SUCCESS);
}

NSS_MP_CACHE_HANDLING(services);
#endif /* NS_CACHING */

/* get**_r functions implementation */
int
getservbyname_r(const char *name, const char *proto, struct servent *serv,
    char *buffer, size_t bufsize, struct servent **result)
{
	static const struct servent_mdata mdata = { nss_lt_name, 0 };
	static const struct servent_mdata compat_mdata = { nss_lt_name, 1 };
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
	NS_COMMON_CACHE_INFO_INITIALIZER(
		services, (void *)nss_lt_name,
		serv_id_func, serv_marshal_func, serv_unmarshal_func);
#endif /* NS_CACHING */
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_servent, (void *)&mdata },
		{ NSSRC_DB, db_servent, (void *)nss_lt_name },
#ifdef YP
		{ NSSRC_NIS, nis_servent, (void *)nss_lt_name },
#endif
		{ NSSRC_COMPAT, files_servent, (void *)&compat_mdata },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int	rv, ret_errno;

	ret_errno = 0;
	*result = NULL;
	rv = nsdispatch(result, dtab, NSDB_SERVICES, "getservbyname_r",
	    defaultsrc, name, proto, serv, buffer, bufsize, &ret_errno);

	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}

int
getservbyport_r(int port, const char *proto, struct servent *serv,
    char *buffer, size_t bufsize, struct servent **result)
{
	static const struct servent_mdata mdata = { nss_lt_id, 0 };
	static const struct servent_mdata compat_mdata = { nss_lt_id, 1 };
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
	NS_COMMON_CACHE_INFO_INITIALIZER(
		services, (void *)nss_lt_id,
		serv_id_func, serv_marshal_func, serv_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_servent, (void *)&mdata },
		{ NSSRC_DB, db_servent, (void *)nss_lt_id },
#ifdef YP
		{ NSSRC_NIS, nis_servent, (void *)nss_lt_id },
#endif
		{ NSSRC_COMPAT, files_servent, (void *)&compat_mdata },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int rv, ret_errno;

	ret_errno = 0;
	*result = NULL;
	rv = nsdispatch(result, dtab, NSDB_SERVICES, "getservbyport_r",
	    defaultsrc, port, proto, serv, buffer, bufsize, &ret_errno);

	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}

int
getservent_r(struct servent *serv, char *buffer, size_t bufsize,
    struct servent **result)
{
	static const struct servent_mdata mdata = { nss_lt_all, 0 };
	static const struct servent_mdata compat_mdata = { nss_lt_all, 1 };
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		services, (void *)nss_lt_all,
		serv_marshal_func, serv_unmarshal_func);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_servent, (void *)&mdata },
		{ NSSRC_DB, db_servent, (void *)nss_lt_all },
#ifdef YP
		{ NSSRC_NIS, nis_servent, (void *)nss_lt_all },
#endif
		{ NSSRC_COMPAT, files_servent, (void *)&compat_mdata },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int rv, ret_errno;

	ret_errno = 0;
	*result = NULL;
	rv = nsdispatch(result, dtab, NSDB_SERVICES, "getservent_r",
	    defaultsrc, serv, buffer, bufsize, &ret_errno);

	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}

void
setservent(int stayopen)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		services, (void *)nss_lt_all,
		NULL, NULL);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setservent, (void *)SETSERVENT },
		{ NSSRC_DB, db_setservent, (void *)SETSERVENT },
#ifdef YP
		{ NSSRC_NIS, nis_setservent, (void *)SETSERVENT },
#endif
		{ NSSRC_COMPAT, compat_setservent, (void *)SETSERVENT },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};

	(void)nsdispatch(NULL, dtab, NSDB_SERVICES, "setservent", defaultsrc,
	    stayopen);
}

void
endservent(void)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		services, (void *)nss_lt_all,
		NULL, NULL);
#endif
	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setservent, (void *)ENDSERVENT },
		{ NSSRC_DB, db_setservent, (void *)ENDSERVENT },
#ifdef YP
		{ NSSRC_NIS, nis_setservent, (void *)ENDSERVENT },
#endif
		{ NSSRC_COMPAT, compat_setservent, (void *)ENDSERVENT },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};

	(void)nsdispatch(NULL, dtab, NSDB_SERVICES, "endservent", defaultsrc);
}

/* get** wrappers for get**_r functions implementation */
static void
servent_endstate(void *p)
{
	if (p == NULL)
		return;

	free(((struct servent_state *)p)->buffer);
	free(p);
}

static int
wrap_getservbyname_r(struct key key, struct servent *serv, char *buffer,
    size_t bufsize, struct servent **res)
{
	return (getservbyname_r(key.name, key.proto, serv, buffer, bufsize,
	    res));
}

static int
wrap_getservbyport_r(struct key key, struct servent *serv, char *buffer,
    size_t bufsize, struct servent **res)
{
	return (getservbyport_r(key.port, key.proto, serv, buffer, bufsize,
	    res));
}

static	int
wrap_getservent_r(struct key key, struct servent *serv, char *buffer,
    size_t bufsize, struct servent **res)
{
	return (getservent_r(serv, buffer, bufsize, res));
}

static struct servent *
getserv(int (*fn)(struct key, struct servent *, char *, size_t,
    struct servent **), struct key key)
{
	int rv;
	struct servent *res;
	struct servent_state * st;

	rv = servent_getstate(&st);
	if (rv != 0) {
		errno = rv;
		return NULL;
	}

	if (st->buffer == NULL) {
		st->buffer = malloc(SERVENT_STORAGE_INITIAL);
		if (st->buffer == NULL)
			return (NULL);
		st->bufsize = SERVENT_STORAGE_INITIAL;
	}
	do {
		rv = fn(key, &st->serv, st->buffer, st->bufsize, &res);
		if (res == NULL && rv == ERANGE) {
			free(st->buffer);
			if ((st->bufsize << 1) > SERVENT_STORAGE_MAX) {
				st->buffer = NULL;
				errno = ERANGE;
				return (NULL);
			}
			st->bufsize <<= 1;
			st->buffer = malloc(st->bufsize);
			if (st->buffer == NULL)
				return (NULL);
		}
	} while (res == NULL && rv == ERANGE);
	if (rv != 0)
		errno = rv;

	return (res);
}

struct servent *
getservbyname(const char *name, const char *proto)
{
	struct key key;

	key.name = name;
	key.proto = proto;

	return (getserv(wrap_getservbyname_r, key));
}

struct servent *
getservbyport(int port, const char *proto)
{
	struct key key;

	key.port = port;
	key.proto = proto;

	return (getserv(wrap_getservbyport_r, key));
}

struct servent *
getservent(void)
{
	struct key key;

	key.proto = NULL;
	key.port = 0;

	return (getserv(wrap_getservent_r, key));
}
