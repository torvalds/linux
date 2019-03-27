/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by
 * Jacques A. Vidrine, Safeport Network Services, and Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#ifdef YP
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#ifdef HESIOD
#include <hesiod.h>
#endif
#include <netdb.h>
#include <nsswitch.h>
#include <pthread.h>
#include <pthread_np.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "un-namespace.h"
#include <db.h>
#include "libc_private.h"
#include "pw_scan.h"
#include "nss_tls.h"
#ifdef NS_CACHING
#include "nscache.h"
#endif

#ifndef CTASSERT
#define CTASSERT(x)		_CTASSERT(x, __LINE__)
#define _CTASSERT(x, y)		__CTASSERT(x, y)
#define __CTASSERT(x, y)	typedef char __assert_ ## y [(x) ? 1 : -1]
#endif

/* Counter as stored in /etc/pwd.db */
typedef	int		pwkeynum;

CTASSERT(MAXLOGNAME > sizeof(uid_t));
CTASSERT(MAXLOGNAME > sizeof(pwkeynum));

enum constants {
	PWD_STORAGE_INITIAL	= 1 << 10, /* 1 KByte */
	PWD_STORAGE_MAX		= 1 << 20, /* 1 MByte */
	SETPWENT		= 1,
	ENDPWENT		= 2,
	HESIOD_NAME_MAX		= 256
};

static const ns_src defaultsrc[] = {
	{ NSSRC_COMPAT, NS_SUCCESS },
	{ NULL, 0 }
};

int	__pw_match_entry(const char *, size_t, enum nss_lookup_type,
	    const char *, uid_t);
int	__pw_parse_entry(char *, size_t, struct passwd *, int, int *errnop);

union key {
	const char	*name;
	uid_t		 uid;
};

static	struct passwd *getpw(int (*fn)(union key, struct passwd *, char *,
		    size_t, struct passwd **), union key);
static	int	 wrap_getpwnam_r(union key, struct passwd *, char *,
		    size_t, struct passwd **);
static	int	 wrap_getpwuid_r(union key, struct passwd *, char *, size_t,
		    struct passwd **);
static	int	 wrap_getpwent_r(union key, struct passwd *, char *, size_t,
		    struct passwd **);

static	int	 pwdb_match_entry_v3(char *, size_t, enum nss_lookup_type,
		    const char *, uid_t);
static	int	 pwdb_parse_entry_v3(char *, size_t, struct passwd *, int *);
static	int	 pwdb_match_entry_v4(char *, size_t, enum nss_lookup_type,
		    const char *, uid_t);
static	int	 pwdb_parse_entry_v4(char *, size_t, struct passwd *, int *);


struct {
	int	(*match)(char *, size_t, enum nss_lookup_type, const char *,
		    uid_t);
	int	(*parse)(char *, size_t, struct passwd *, int *);
} pwdb_versions[] = {
	{ NULL, NULL },					/* version 0 */
	{ NULL, NULL },					/* version 1 */
	{ NULL, NULL },					/* version 2 */
	{ pwdb_match_entry_v3, pwdb_parse_entry_v3 },	/* version 3 */
	{ pwdb_match_entry_v4, pwdb_parse_entry_v4 },	/* version 4 */
};


struct files_state {
	DB		*db;
	pwkeynum	 keynum;
	int		 stayopen;
	int		 version;
};
static	void	files_endstate(void *);
NSS_TLS_HANDLING(files);
static	DB	*pwdbopen(int *);
static	void	 files_endstate(void *);
static	int	 files_setpwent(void *, void *, va_list);
static	int	 files_passwd(void *, void *, va_list);


#ifdef HESIOD
struct dns_state {
	long	counter;
};
static	void	dns_endstate(void *);
NSS_TLS_HANDLING(dns);
static	int	 dns_setpwent(void *, void *, va_list);
static	int	 dns_passwd(void *, void *, va_list);
#endif


#ifdef YP
struct nis_state {
	char	 domain[MAXHOSTNAMELEN];
	int	 done;
	char	*key;
	int	 keylen;
};
static	void	 nis_endstate(void *);
NSS_TLS_HANDLING(nis);
static	int	 nis_setpwent(void *, void *, va_list);
static	int	 nis_passwd(void *, void *, va_list);
static	int	 nis_map(char *, enum nss_lookup_type, char *, size_t, int *);
static	int	 nis_adjunct(char *, const char *, char *, size_t);
#endif


struct compat_state {
	DB		*db;
	pwkeynum	 keynum;
	int		 stayopen;
	int		 version;
	DB		*exclude;
	struct passwd	 template;
	char		*name;
	enum _compat {
		COMPAT_MODE_OFF = 0,
		COMPAT_MODE_ALL,
		COMPAT_MODE_NAME,
		COMPAT_MODE_NETGROUP
	}		 compat;
};
static	void	 compat_endstate(void *);
NSS_TLS_HANDLING(compat);
static	int	 compat_setpwent(void *, void *, va_list);
static	int	 compat_passwd(void *, void *, va_list);
static	void	 compat_clear_template(struct passwd *);
static	int	 compat_set_template(struct passwd *, struct passwd *);
static	int	 compat_use_template(struct passwd *, struct passwd *, char *,
		    size_t);
static	int	 compat_redispatch(struct compat_state *, enum nss_lookup_type,
		    enum nss_lookup_type, const char *, const char *, uid_t,
		    struct passwd *, char *, size_t, int *);

#ifdef NS_CACHING
static	int	 pwd_id_func(char *, size_t *, va_list ap, void *);
static	int	 pwd_marshal_func(char *, size_t *, void *, va_list, void *);
static	int	 pwd_unmarshal_func(char *, size_t, void *, va_list, void *);

static int
pwd_id_func(char *buffer, size_t *buffer_size, va_list ap, void *cache_mdata)
{
	char	*name;
	uid_t	uid;
	size_t	size, desired_size;
	int	res = NS_UNAVAIL;
	enum nss_lookup_type lookup_type;

	lookup_type = (enum nss_lookup_type)cache_mdata;
	switch (lookup_type) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		size = strlen(name);
		desired_size = sizeof(enum nss_lookup_type) + size + 1;
		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), name, size + 1);

		res = NS_SUCCESS;
		break;
	case nss_lt_id:
		uid = va_arg(ap, uid_t);
		desired_size = sizeof(enum nss_lookup_type) + sizeof(uid_t);
		if (desired_size > *buffer_size) {
			res = NS_RETURN;
			goto fin;
		}

		memcpy(buffer, &lookup_type, sizeof(enum nss_lookup_type));
		memcpy(buffer + sizeof(enum nss_lookup_type), &uid,
		    sizeof(uid_t));

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

static int
pwd_marshal_func(char *buffer, size_t *buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	uid_t uid;
	struct passwd *pwd;
	char *orig_buf;
	size_t orig_buf_size;

	struct passwd new_pwd;
	size_t desired_size, size;
	char *p;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		break;
	case nss_lt_id:
		uid = va_arg(ap, uid_t);
		break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	pwd = va_arg(ap, struct passwd *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);

	desired_size = sizeof(struct passwd) + sizeof(char *) +
	    strlen(pwd->pw_name) + 1;
	if (pwd->pw_passwd != NULL)
		desired_size += strlen(pwd->pw_passwd) + 1;
	if (pwd->pw_class != NULL)
		desired_size += strlen(pwd->pw_class) + 1;
	if (pwd->pw_gecos != NULL)
		desired_size += strlen(pwd->pw_gecos) + 1;
	if (pwd->pw_dir != NULL)
		desired_size += strlen(pwd->pw_dir) + 1;
	if (pwd->pw_shell != NULL)
		desired_size += strlen(pwd->pw_shell) + 1;

	if (*buffer_size < desired_size) {
		/* this assignment is here for future use */
		*buffer_size = desired_size;
		return (NS_RETURN);
	}

	memcpy(&new_pwd, pwd, sizeof(struct passwd));
	memset(buffer, 0, desired_size);

	*buffer_size = desired_size;
	p = buffer + sizeof(struct passwd) + sizeof(char *);
	memcpy(buffer + sizeof(struct passwd), &p, sizeof(char *));

	if (new_pwd.pw_name != NULL) {
		size = strlen(new_pwd.pw_name);
		memcpy(p, new_pwd.pw_name, size);
		new_pwd.pw_name = p;
		p += size + 1;
	}

	if (new_pwd.pw_passwd != NULL) {
		size = strlen(new_pwd.pw_passwd);
		memcpy(p, new_pwd.pw_passwd, size);
		new_pwd.pw_passwd = p;
		p += size + 1;
	}

	if (new_pwd.pw_class != NULL) {
		size = strlen(new_pwd.pw_class);
		memcpy(p, new_pwd.pw_class, size);
		new_pwd.pw_class = p;
		p += size + 1;
	}

	if (new_pwd.pw_gecos != NULL) {
		size = strlen(new_pwd.pw_gecos);
		memcpy(p, new_pwd.pw_gecos, size);
		new_pwd.pw_gecos = p;
		p += size + 1;
	}

	if (new_pwd.pw_dir != NULL) {
		size = strlen(new_pwd.pw_dir);
		memcpy(p, new_pwd.pw_dir, size);
		new_pwd.pw_dir = p;
		p += size + 1;
	}

	if (new_pwd.pw_shell != NULL) {
		size = strlen(new_pwd.pw_shell);
		memcpy(p, new_pwd.pw_shell, size);
		new_pwd.pw_shell = p;
		p += size + 1;
	}

	memcpy(buffer, &new_pwd, sizeof(struct passwd));
	return (NS_SUCCESS);
}

static int
pwd_unmarshal_func(char *buffer, size_t buffer_size, void *retval, va_list ap,
    void *cache_mdata)
{
	char *name;
	uid_t uid;
	struct passwd *pwd;
	char *orig_buf;
	size_t orig_buf_size;
	int *ret_errno;

	char *p;

	switch ((enum nss_lookup_type)cache_mdata) {
	case nss_lt_name:
		name = va_arg(ap, char *);
		break;
	case nss_lt_id:
		uid = va_arg(ap, uid_t);
		break;
	case nss_lt_all:
		break;
	default:
		/* should be unreachable */
		return (NS_UNAVAIL);
	}

	pwd = va_arg(ap, struct passwd *);
	orig_buf = va_arg(ap, char *);
	orig_buf_size = va_arg(ap, size_t);
	ret_errno = va_arg(ap, int *);

	if (orig_buf_size <
	    buffer_size - sizeof(struct passwd) - sizeof(char *)) {
		*ret_errno = ERANGE;
		return (NS_RETURN);
	}

	memcpy(pwd, buffer, sizeof(struct passwd));
	memcpy(&p, buffer + sizeof(struct passwd), sizeof(char *));
	memcpy(orig_buf, buffer + sizeof(struct passwd) + sizeof(char *),
	    buffer_size - sizeof(struct passwd) - sizeof(char *));

	NS_APPLY_OFFSET(pwd->pw_name, orig_buf, p, char *);
	NS_APPLY_OFFSET(pwd->pw_passwd, orig_buf, p, char *);
	NS_APPLY_OFFSET(pwd->pw_class, orig_buf, p, char *);
	NS_APPLY_OFFSET(pwd->pw_gecos, orig_buf, p, char *);
	NS_APPLY_OFFSET(pwd->pw_dir, orig_buf, p, char *);
	NS_APPLY_OFFSET(pwd->pw_shell, orig_buf, p, char *);

	if (retval != NULL)
		*((struct passwd **)retval) = pwd;

	return (NS_SUCCESS);
}

NSS_MP_CACHE_HANDLING(passwd);
#endif /* NS_CACHING */

void
setpwent(void)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		passwd, (void *)nss_lt_all,
		NULL, NULL);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setpwent, (void *)SETPWENT },
#ifdef HESIOD
		{ NSSRC_DNS, dns_setpwent, (void *)SETPWENT },
#endif
#ifdef YP
		{ NSSRC_NIS, nis_setpwent, (void *)SETPWENT },
#endif
		{ NSSRC_COMPAT, compat_setpwent, (void *)SETPWENT },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	(void)_nsdispatch(NULL, dtab, NSDB_PASSWD, "setpwent", defaultsrc, 0);
}


int
setpassent(int stayopen)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		passwd, (void *)nss_lt_all,
		NULL, NULL);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setpwent, (void *)SETPWENT },
#ifdef HESIOD
		{ NSSRC_DNS, dns_setpwent, (void *)SETPWENT },
#endif
#ifdef YP
		{ NSSRC_NIS, nis_setpwent, (void *)SETPWENT },
#endif
		{ NSSRC_COMPAT, compat_setpwent, (void *)SETPWENT },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	(void)_nsdispatch(NULL, dtab, NSDB_PASSWD, "setpwent", defaultsrc,
	    stayopen);
	return (1);
}


void
endpwent(void)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		passwd, (void *)nss_lt_all,
		NULL, NULL);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_setpwent, (void *)ENDPWENT },
#ifdef HESIOD
		{ NSSRC_DNS, dns_setpwent, (void *)ENDPWENT },
#endif
#ifdef YP
		{ NSSRC_NIS, nis_setpwent, (void *)ENDPWENT },
#endif
		{ NSSRC_COMPAT, compat_setpwent, (void *)ENDPWENT },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	(void)_nsdispatch(NULL, dtab, NSDB_PASSWD, "endpwent", defaultsrc);
}


int
getpwent_r(struct passwd *pwd, char *buffer, size_t bufsize,
    struct passwd **result)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info = NS_MP_CACHE_INFO_INITIALIZER(
		passwd, (void *)nss_lt_all,
		pwd_marshal_func, pwd_unmarshal_func);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_passwd, (void *)nss_lt_all },
#ifdef HESIOD
		{ NSSRC_DNS, dns_passwd, (void *)nss_lt_all },
#endif
#ifdef YP
		{ NSSRC_NIS, nis_passwd, (void *)nss_lt_all },
#endif
		{ NSSRC_COMPAT, compat_passwd, (void *)nss_lt_all },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int	rv, ret_errno;

	__pw_initpwd(pwd);
	ret_errno = 0;
	*result = NULL;
	rv = _nsdispatch(result, dtab, NSDB_PASSWD, "getpwent_r", defaultsrc,
	    pwd, buffer, bufsize, &ret_errno);
	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}


int
getpwnam_r(const char *name, struct passwd *pwd, char *buffer, size_t bufsize,
    struct passwd **result)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
    		NS_COMMON_CACHE_INFO_INITIALIZER(
		passwd, (void *)nss_lt_name,
		pwd_id_func, pwd_marshal_func, pwd_unmarshal_func);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_passwd, (void *)nss_lt_name },
#ifdef HESIOD
		{ NSSRC_DNS, dns_passwd, (void *)nss_lt_name },
#endif
#ifdef YP
		{ NSSRC_NIS, nis_passwd, (void *)nss_lt_name },
#endif
		{ NSSRC_COMPAT, compat_passwd, (void *)nss_lt_name },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int	rv, ret_errno;

	__pw_initpwd(pwd);
	ret_errno = 0;
	*result = NULL;
	rv = _nsdispatch(result, dtab, NSDB_PASSWD, "getpwnam_r", defaultsrc,
	    name, pwd, buffer, bufsize, &ret_errno);
	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}


int
getpwuid_r(uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize,
    struct passwd **result)
{
#ifdef NS_CACHING
	static const nss_cache_info cache_info =
    		NS_COMMON_CACHE_INFO_INITIALIZER(
		passwd, (void *)nss_lt_id,
		pwd_id_func, pwd_marshal_func, pwd_unmarshal_func);
#endif

	static const ns_dtab dtab[] = {
		{ NSSRC_FILES, files_passwd, (void *)nss_lt_id },
#ifdef HESIOD
		{ NSSRC_DNS, dns_passwd, (void *)nss_lt_id },
#endif
#ifdef YP
		{ NSSRC_NIS, nis_passwd, (void *)nss_lt_id },
#endif
		{ NSSRC_COMPAT, compat_passwd, (void *)nss_lt_id },
#ifdef NS_CACHING
		NS_CACHE_CB(&cache_info)
#endif
		{ NULL, NULL, NULL }
	};
	int	rv, ret_errno;

	__pw_initpwd(pwd);
	ret_errno = 0;
	*result = NULL;
	rv = _nsdispatch(result, dtab, NSDB_PASSWD, "getpwuid_r", defaultsrc,
	    uid, pwd, buffer, bufsize, &ret_errno);
	if (rv == NS_SUCCESS)
		return (0);
	else
		return (ret_errno);
}


static struct passwd	 pwd;
static char		*pwd_storage;
static size_t		 pwd_storage_size;


static struct passwd *
getpw(int (*fn)(union key, struct passwd *, char *, size_t, struct passwd **),
    union key key)
{
	int		 rv;
	struct passwd	*res;

	if (pwd_storage == NULL) {
		pwd_storage = malloc(PWD_STORAGE_INITIAL);
		if (pwd_storage == NULL)
			return (NULL);
		pwd_storage_size = PWD_STORAGE_INITIAL;
	}
	do {
		rv = fn(key, &pwd, pwd_storage, pwd_storage_size, &res);
		if (res == NULL && rv == ERANGE) {
			free(pwd_storage);
			if ((pwd_storage_size << 1) > PWD_STORAGE_MAX) {
				pwd_storage = NULL;
				errno = ERANGE;
				return (NULL);
			}
			pwd_storage_size <<= 1;
			pwd_storage = malloc(pwd_storage_size);
			if (pwd_storage == NULL)
				return (NULL);
		}
	} while (res == NULL && rv == ERANGE);
	if (rv != 0)
		errno = rv;
	return (res);
}


static int
wrap_getpwnam_r(union key key, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **res)
{
	return (getpwnam_r(key.name, pwd, buffer, bufsize, res));
}


static int
wrap_getpwuid_r(union key key, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **res)
{
	return (getpwuid_r(key.uid, pwd, buffer, bufsize, res));
}


static int
wrap_getpwent_r(union key key __unused, struct passwd *pwd, char *buffer,
    size_t bufsize, struct passwd **res)
{
	return (getpwent_r(pwd, buffer, bufsize, res));
}


struct passwd *
getpwnam(const char *name)
{
	union key key;

	key.name = name;
	return (getpw(wrap_getpwnam_r, key));
}


struct passwd *
getpwuid(uid_t uid)
{
	union key key;

	key.uid = uid;
	return (getpw(wrap_getpwuid_r, key));
}


struct passwd *
getpwent(void)
{
	union key key;

	key.uid = 0; /* not used */
	return (getpw(wrap_getpwent_r, key));
}


/*
 * files backend
 */
static DB *
pwdbopen(int *version)
{
	DB	*res;
	DBT	 key, entry;
	int	 rv;

	if (geteuid() != 0 ||
	    (res = dbopen(_PATH_SMP_DB, O_RDONLY, 0, DB_HASH, NULL)) == NULL)
		res = dbopen(_PATH_MP_DB, O_RDONLY, 0, DB_HASH, NULL);
	if (res == NULL)
		return (NULL);
	key.data = _PWD_VERSION_KEY;
	key.size = strlen(_PWD_VERSION_KEY);
	rv = res->get(res, &key, &entry, 0);
	if (rv == 0)
		*version = *(unsigned char *)entry.data;
	else
		*version = 3;
	if (*version < 3 ||
	    *version >= nitems(pwdb_versions)) {
		syslog(LOG_CRIT, "Unsupported password database version %d",
		    *version);
		res->close(res);
		res = NULL;
	}
	return (res);
}


static void
files_endstate(void *p)
{
	DB	*db;

	if (p == NULL)
		return;
	db = ((struct files_state *)p)->db;
	if (db != NULL)
		db->close(db);
	free(p);
}


static int
files_setpwent(void *retval, void *mdata, va_list ap)
{
	struct files_state	*st;
	int			 rv, stayopen;

	rv = files_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);
	switch ((enum constants)mdata) {
	case SETPWENT:
		stayopen = va_arg(ap, int);
		st->keynum = 0;
		if (stayopen)
			st->db = pwdbopen(&st->version);
		st->stayopen = stayopen;
		break;
	case ENDPWENT:
		if (st->db != NULL) {
			(void)st->db->close(st->db);
			st->db = NULL;
		}
		break;
	default:
		break;
	}
	return (NS_UNAVAIL);
}


static int
files_passwd(void *retval, void *mdata, va_list ap)
{
	char			 keybuf[MAXLOGNAME + 1];
	DBT			 key, entry;
	struct files_state	*st;
	enum nss_lookup_type	 how;
	const char		*name;
	struct passwd		*pwd;
	char			*buffer;
	size_t			 bufsize, namesize;
	uid_t			 uid;
	uint32_t		 store;
	int			 rv, stayopen = 0, *errnop;

	name = NULL;
	uid = (uid_t)-1;
	how = (enum nss_lookup_type)mdata;
	switch (how) {
	case nss_lt_name:
		name = va_arg(ap, const char *);
		keybuf[0] = _PW_KEYBYNAME;
		break;
	case nss_lt_id:
		uid = va_arg(ap, uid_t);
		keybuf[0] = _PW_KEYBYUID;
		break;
	case nss_lt_all:
		keybuf[0] = _PW_KEYBYNUM;
		break;
	default:
		rv = NS_NOTFOUND;
		goto fin;
	}
	pwd = va_arg(ap, struct passwd *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	*errnop = files_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);
	if (how == nss_lt_all && st->keynum < 0) {
		rv = NS_NOTFOUND;
		goto fin;
	}
	if (st->db == NULL &&
	    (st->db = pwdbopen(&st->version)) == NULL) {
		*errnop = errno;
		rv = NS_UNAVAIL;
		goto fin;
	}
	if (how == nss_lt_all)
		stayopen = 1;
	else
		stayopen = st->stayopen;
	key.data = keybuf;
	do {
		switch (how) {
		case nss_lt_name:
			/* MAXLOGNAME includes NUL byte, but we do not
			 * include the NUL byte in the key.
			 */
			namesize = strlcpy(&keybuf[1], name, sizeof(keybuf)-1);
			if (namesize >= sizeof(keybuf)-1) {
				*errnop = EINVAL;
				rv = NS_NOTFOUND;
				goto fin;
			}
			key.size = namesize + 1;
			break;
		case nss_lt_id:
			if (st->version < _PWD_CURRENT_VERSION) {
				memcpy(&keybuf[1], &uid, sizeof(uid));
				key.size = sizeof(uid) + 1;
			} else {
				store = htonl(uid);
				memcpy(&keybuf[1], &store, sizeof(store));
				key.size = sizeof(store) + 1;
			}
			break;
		case nss_lt_all:
			st->keynum++;
			if (st->version < _PWD_CURRENT_VERSION) {
				memcpy(&keybuf[1], &st->keynum,
				    sizeof(st->keynum));
				key.size = sizeof(st->keynum) + 1;
			} else {
				store = htonl(st->keynum);
				memcpy(&keybuf[1], &store, sizeof(store));
				key.size = sizeof(store) + 1;
			}
			break;
		}
		keybuf[0] = _PW_VERSIONED(keybuf[0], st->version);
		rv = st->db->get(st->db, &key, &entry, 0);
		if (rv < 0 || rv > 1) { /* should never return > 1 */
			*errnop = errno;
			rv = NS_UNAVAIL;
			goto fin;
		} else if (rv == 1) {
			if (how == nss_lt_all)
				st->keynum = -1;
			rv = NS_NOTFOUND;
			goto fin;
		}
		rv = pwdb_versions[st->version].match(entry.data, entry.size,
		    how, name, uid);
		if (rv != NS_SUCCESS)
			continue;
		if (entry.size > bufsize) {
			*errnop = ERANGE;
			rv = NS_RETURN;
			break;
		}
		memcpy(buffer, entry.data, entry.size);
		rv = pwdb_versions[st->version].parse(buffer, entry.size, pwd,
		    errnop);
	} while (how == nss_lt_all && !(rv & NS_TERMINATE));
fin:
	if (st->db != NULL && !stayopen) {
		(void)st->db->close(st->db);
		st->db = NULL;
	}
	if (rv == NS_SUCCESS) {
		pwd->pw_fields &= ~_PWF_SOURCE;
		pwd->pw_fields |= _PWF_FILES;
		if (retval != NULL)
			*(struct passwd **)retval = pwd;
	}
	return (rv);
}


static int
pwdb_match_entry_v3(char *entry, size_t entrysize, enum nss_lookup_type how,
    const char *name, uid_t uid)
{
	const char	*p, *eom;
	uid_t		 uid2;

	eom = &entry[entrysize];
	for (p = entry; p < eom; p++)
		if (*p == '\0')
			break;
	if (*p != '\0')
		return (NS_NOTFOUND);
	if (how == nss_lt_all)
		return (NS_SUCCESS);
	if (how == nss_lt_name)
		return (strcmp(name, entry) == 0 ? NS_SUCCESS : NS_NOTFOUND);
	for (p++; p < eom; p++)
		if (*p == '\0')
			break;
	if (*p != '\0' || (++p) + sizeof(uid) >= eom)
		return (NS_NOTFOUND);
	memcpy(&uid2, p, sizeof(uid2));
	return (uid == uid2 ? NS_SUCCESS : NS_NOTFOUND);
}


static int
pwdb_parse_entry_v3(char *buffer, size_t bufsize, struct passwd *pwd,
    int *errnop)
{
	char		*p, *eom;
	int32_t		 pw_change, pw_expire;

	/* THIS CODE MUST MATCH THAT IN pwd_mkdb. */
	p = buffer;
	eom = &buffer[bufsize];
#define STRING(field)	do {			\
		(field) = p;			\
		while (p < eom && *p != '\0')	\
			p++;			\
		if (p >= eom)			\
			return (NS_NOTFOUND);	\
		p++;				\
	} while (0)
#define SCALAR(field)	do {				\
		if (p + sizeof(field) > eom)		\
			return (NS_NOTFOUND);		\
		memcpy(&(field), p, sizeof(field));	\
		p += sizeof(field);			\
	} while (0)
	STRING(pwd->pw_name);
	STRING(pwd->pw_passwd);
	SCALAR(pwd->pw_uid);
	SCALAR(pwd->pw_gid);
	SCALAR(pw_change);
	STRING(pwd->pw_class);
	STRING(pwd->pw_gecos);
	STRING(pwd->pw_dir);
	STRING(pwd->pw_shell);
	SCALAR(pw_expire);
	SCALAR(pwd->pw_fields);
#undef STRING
#undef SCALAR
	pwd->pw_change = pw_change;
	pwd->pw_expire = pw_expire;
	return (NS_SUCCESS);
}


static int
pwdb_match_entry_v4(char *entry, size_t entrysize, enum nss_lookup_type how,
    const char *name, uid_t uid)
{
	const char	*p, *eom;
	uint32_t	 uid2;

	eom = &entry[entrysize];
	for (p = entry; p < eom; p++)
		if (*p == '\0')
			break;
	if (*p != '\0')
		return (NS_NOTFOUND);
	if (how == nss_lt_all)
		return (NS_SUCCESS);
	if (how == nss_lt_name)
		return (strcmp(name, entry) == 0 ? NS_SUCCESS : NS_NOTFOUND);
	for (p++; p < eom; p++)
		if (*p == '\0')
			break;
	if (*p != '\0' || (++p) + sizeof(uid) >= eom)
		return (NS_NOTFOUND);
	memcpy(&uid2, p, sizeof(uid2));
	uid2 = ntohl(uid2);
	return (uid == (uid_t)uid2 ? NS_SUCCESS : NS_NOTFOUND);
}


static int
pwdb_parse_entry_v4(char *buffer, size_t bufsize, struct passwd *pwd,
    int *errnop)
{
	char		*p, *eom;
	uint32_t	 n;

	/* THIS CODE MUST MATCH THAT IN pwd_mkdb. */
	p = buffer;
	eom = &buffer[bufsize];
#define STRING(field)	do {			\
		(field) = p;			\
		while (p < eom && *p != '\0')	\
			p++;			\
		if (p >= eom)			\
			return (NS_NOTFOUND);	\
		p++;				\
	} while (0)
#define SCALAR(field)	do {				\
		if (p + sizeof(n) > eom)		\
			return (NS_NOTFOUND);		\
		memcpy(&n, p, sizeof(n));		\
		(field) = ntohl(n);			\
		p += sizeof(n);				\
	} while (0)
	STRING(pwd->pw_name);
	STRING(pwd->pw_passwd);
	SCALAR(pwd->pw_uid);
	SCALAR(pwd->pw_gid);
	SCALAR(pwd->pw_change);
	STRING(pwd->pw_class);
	STRING(pwd->pw_gecos);
	STRING(pwd->pw_dir);
	STRING(pwd->pw_shell);
	SCALAR(pwd->pw_expire);
	SCALAR(pwd->pw_fields);
#undef STRING
#undef SCALAR
	return (NS_SUCCESS);
}


#ifdef HESIOD
/*
 * dns backend
 */
static void
dns_endstate(void *p)
{
	free(p);
}


static int
dns_setpwent(void *retval, void *mdata, va_list ap)
{
	struct dns_state	*st;
	int			 rv;

	rv = dns_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);
	st->counter = 0;
	return (NS_UNAVAIL);
}


static int
dns_passwd(void *retval, void *mdata, va_list ap)
{
	char			 buf[HESIOD_NAME_MAX];
	struct dns_state	*st;
	struct passwd		*pwd;
	const char		*name, *label;
	void			*ctx;
	char			*buffer, **hes;
	size_t			 bufsize, linesize;
	uid_t			 uid;
	enum nss_lookup_type	 how;
	int			 rv, *errnop;

	ctx = NULL;
	hes = NULL;
	name = NULL;
	uid = (uid_t)-1;
	how = (enum nss_lookup_type)mdata;
	switch (how) {
	case nss_lt_name:
		name = va_arg(ap, const char *);
		break;
	case nss_lt_id:
		uid = va_arg(ap, uid_t);
		break;
	case nss_lt_all:
		break;
	}
	pwd     = va_arg(ap, struct passwd *);
	buffer  = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop  = va_arg(ap, int *);
	*errnop = dns_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);
	if (hesiod_init(&ctx) != 0) {
		*errnop = errno;
		rv = NS_UNAVAIL;
		goto fin;
	}
	do {
		rv = NS_NOTFOUND;
		switch (how) {
		case nss_lt_name:
			label = name;
			break;
		case nss_lt_id:
			if (snprintf(buf, sizeof(buf), "%lu",
			    (unsigned long)uid) >= sizeof(buf))
				goto fin;
			label = buf;
			break;
		case nss_lt_all:
			if (st->counter < 0)
				goto fin;
			if (snprintf(buf, sizeof(buf), "passwd-%ld",
			    st->counter++) >= sizeof(buf))
				goto fin;
			label = buf;
			break;
		}
		hes = hesiod_resolve(ctx, label,
		    how == nss_lt_id ? "uid" : "passwd");
		if (hes == NULL) {
			if (how == nss_lt_all)
				st->counter = -1;
			if (errno != ENOENT)
				*errnop = errno;
			goto fin;
		}
		rv = __pw_match_entry(hes[0], strlen(hes[0]), how, name, uid);
		if (rv != NS_SUCCESS) {
			hesiod_free_list(ctx, hes);
			hes = NULL;
			continue;
		}
		linesize = strlcpy(buffer, hes[0], bufsize);
		if (linesize >= bufsize) {
			*errnop = ERANGE;
			rv = NS_RETURN;
			continue;
		}
		hesiod_free_list(ctx, hes);
		hes = NULL;
		rv = __pw_parse_entry(buffer, bufsize, pwd, 0, errnop);
	} while (how == nss_lt_all && !(rv & NS_TERMINATE));
fin:
	if (hes != NULL)
		hesiod_free_list(ctx, hes);
	if (ctx != NULL)
		hesiod_end(ctx);
	if (rv == NS_SUCCESS) {
		pwd->pw_fields &= ~_PWF_SOURCE;
		pwd->pw_fields |= _PWF_HESIOD;
		if (retval != NULL)
			*(struct passwd **)retval = pwd;
	}
	return (rv);
}
#endif /* HESIOD */


#ifdef YP
/*
 * nis backend
 */
static void
nis_endstate(void *p)
{
	free(((struct nis_state *)p)->key);
	free(p);
}

/*
 * Test for the presence of special FreeBSD-specific master.passwd.by*
 * maps. We do this using yp_order(). If it fails, then either the server
 * doesn't have the map, or the YPPROC_ORDER procedure isn't supported by
 * the server (Sun NIS+ servers in YP compat mode behave this way). If
 * the master.passwd.by* maps don't exist, then let the lookup routine try
 * the regular passwd.by* maps instead. If the lookup routine fails, it
 * can return an error as needed.
 */
static int
nis_map(char *domain, enum nss_lookup_type how, char *buffer, size_t bufsize,
    int *master)
{
	int	rv, order;

	*master = 0;
	if (geteuid() == 0) {
		if (snprintf(buffer, bufsize, "master.passwd.by%s",
		    (how == nss_lt_id) ? "uid" : "name") >= bufsize)
			return (NS_UNAVAIL);
		rv = yp_order(domain, buffer, &order);
		if (rv == 0) {
			*master = 1;
			return (NS_SUCCESS);
		}
	}

	if (snprintf(buffer, bufsize, "passwd.by%s",
	    (how == nss_lt_id) ? "uid" : "name") >= bufsize)
		return (NS_UNAVAIL);

	return (NS_SUCCESS);
}


static int
nis_adjunct(char *domain, const char *name, char *buffer, size_t bufsize)
{
	int	 rv;
	char	*result, *p, *q, *eor;
	int	 resultlen;

	result = NULL;
	rv = yp_match(domain, "passwd.adjunct.byname", name, strlen(name),
	    &result, &resultlen);
	if (rv != 0)
		rv = 1;
	else {
		eor = &result[resultlen];
		p = memchr(result, ':', eor - result);
		if (p != NULL && ++p < eor &&
		    (q = memchr(p, ':', eor - p)) != NULL) {
			if (q - p >= bufsize)
				rv = -1;
			else {
				memcpy(buffer, p, q - p);
				buffer[q - p] ='\0';
			}
		} else
			rv = 1;
	}
	free(result);
	return (rv);
}


static int
nis_setpwent(void *retval, void *mdata, va_list ap)
{
	struct nis_state	*st;
	int			 rv;

	rv = nis_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);
	st->done = 0;
	free(st->key);
	st->key = NULL;
	return (NS_UNAVAIL);
}


static int
nis_passwd(void *retval, void *mdata, va_list ap)
{
	char		 map[YPMAXMAP];
	struct nis_state *st;
	struct passwd	*pwd;
	const char	*name;
	char		*buffer, *key, *result;
	size_t		 bufsize;
	uid_t		 uid;
	enum nss_lookup_type how;
	int		*errnop, keylen, resultlen, rv, master;

	name = NULL;
	uid = (uid_t)-1;
	how = (enum nss_lookup_type)mdata;
	switch (how) {
	case nss_lt_name:
		name = va_arg(ap, const char *);
		break;
	case nss_lt_id:
		uid = va_arg(ap, uid_t);
		break;
	case nss_lt_all:
		break;
	}
	pwd     = va_arg(ap, struct passwd *);
	buffer  = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop  = va_arg(ap, int *);
	*errnop = nis_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);
	if (st->domain[0] == '\0') {
		if (getdomainname(st->domain, sizeof(st->domain)) != 0) {
			*errnop = errno;
			return (NS_UNAVAIL);
		}
	}
	rv = nis_map(st->domain, how, map, sizeof(map), &master);
	if (rv != NS_SUCCESS)
		return (rv);
	result = NULL;
	do {
		rv = NS_NOTFOUND;
		switch (how) {
		case nss_lt_name:
			if (strlcpy(buffer, name, bufsize) >= bufsize)
				goto erange;
			break;
		case nss_lt_id:
			if (snprintf(buffer, bufsize, "%lu",
			    (unsigned long)uid) >= bufsize)
				goto erange;
			break;
		case nss_lt_all:
			if (st->done)
				goto fin;
			break;
		}
		result = NULL;
		if (how == nss_lt_all) {
			if (st->key == NULL)
				rv = yp_first(st->domain, map, &st->key,
				    &st->keylen, &result, &resultlen);
			else {
				key = st->key;
				keylen = st->keylen;
				st->key = NULL;
				rv = yp_next(st->domain, map, key, keylen,
				    &st->key, &st->keylen, &result,
				    &resultlen);
				free(key);
			}
			if (rv != 0) {
				free(result);
				free(st->key);
				st->key = NULL;
				if (rv == YPERR_NOMORE)
					st->done = 1;
				else
					rv = NS_UNAVAIL;
				goto fin;
			}
		} else {
			rv = yp_match(st->domain, map, buffer, strlen(buffer),
			    &result, &resultlen);
			if (rv == YPERR_KEY) {
				rv = NS_NOTFOUND;
				continue;
			} else if (rv != 0) {
				free(result);
				rv = NS_UNAVAIL;
				continue;
			}
		}
		if (resultlen >= bufsize) {
			free(result);
			goto erange;
		}
		memcpy(buffer, result, resultlen);
		buffer[resultlen] = '\0';
		free(result);
		rv = __pw_match_entry(buffer, resultlen, how, name, uid);
		if (rv == NS_SUCCESS)
			rv = __pw_parse_entry(buffer, resultlen, pwd, master,
			    errnop);
	} while (how == nss_lt_all && !(rv & NS_TERMINATE));
fin:
	if (rv == NS_SUCCESS) {
		if (strstr(pwd->pw_passwd, "##") != NULL) {
			rv = nis_adjunct(st->domain, pwd->pw_name,
			    &buffer[resultlen+1], bufsize-resultlen-1);
			if (rv < 0)
				goto erange;
			else if (rv == 0)
				pwd->pw_passwd = &buffer[resultlen+1];
		}
		pwd->pw_fields &= ~_PWF_SOURCE;
		pwd->pw_fields |= _PWF_NIS;
		if (retval != NULL)
			*(struct passwd **)retval = pwd;
		rv = NS_SUCCESS;
	}
	return (rv);
erange:
	*errnop = ERANGE;
	return (NS_RETURN);
}
#endif /* YP */


/*
 * compat backend
 */
static void
compat_clear_template(struct passwd *template)
{

	free(template->pw_passwd);
	free(template->pw_gecos);
	free(template->pw_dir);
	free(template->pw_shell);
	memset(template, 0, sizeof(*template));
}


static int
compat_set_template(struct passwd *src, struct passwd *template)
{

	compat_clear_template(template);
#ifdef PW_OVERRIDE_PASSWD
	if ((src->pw_fields & _PWF_PASSWD) &&
	    (template->pw_passwd = strdup(src->pw_passwd)) == NULL)
		goto enomem;
#endif
	if (src->pw_fields & _PWF_UID)
		template->pw_uid = src->pw_uid;
	if (src->pw_fields & _PWF_GID)
		template->pw_gid = src->pw_gid;
	if ((src->pw_fields & _PWF_GECOS) &&
	    (template->pw_gecos = strdup(src->pw_gecos)) == NULL)
		goto enomem;
	if ((src->pw_fields & _PWF_DIR) &&
	    (template->pw_dir = strdup(src->pw_dir)) == NULL)
		goto enomem;
	if ((src->pw_fields & _PWF_SHELL) &&
	    (template->pw_shell = strdup(src->pw_shell)) == NULL)
		goto enomem;
	template->pw_fields = src->pw_fields;
	return (0);
enomem:
	syslog(LOG_ERR, "getpwent memory allocation failure");
	return (-1);
}


static int
compat_use_template(struct passwd *pwd, struct passwd *template, char *buffer,
    size_t bufsize)
{
	struct passwd hold;
	char	*copy, *p, *q, *eob;
	size_t	 n;

	/* We cannot know the layout of the password fields in `buffer',
	 * so we have to copy everything.
	 */
	if (template->pw_fields == 0) /* nothing to fill-in */
		return (0);
	n = 0;
	n += pwd->pw_name != NULL ? strlen(pwd->pw_name) + 1 : 0;
	n += pwd->pw_passwd != NULL ? strlen(pwd->pw_passwd) + 1 : 0;
	n += pwd->pw_class != NULL ? strlen(pwd->pw_class) + 1 : 0;
	n += pwd->pw_gecos != NULL ? strlen(pwd->pw_gecos) + 1 : 0;
	n += pwd->pw_dir != NULL ? strlen(pwd->pw_dir) + 1 : 0;
	n += pwd->pw_shell != NULL ? strlen(pwd->pw_shell) + 1 : 0;
	copy = malloc(n);
	if (copy == NULL) {
		syslog(LOG_ERR, "getpwent memory allocation failure");
		return (ENOMEM);
	}
	p = copy;
	eob = &copy[n];
#define COPY(field) do {				\
	if (pwd->field == NULL)				\
		hold.field = NULL;			\
	else {						\
		hold.field = p;				\
		p += strlcpy(p, pwd->field, eob-p) + 1;	\
	}						\
} while (0)
	COPY(pw_name);
	COPY(pw_passwd);
	COPY(pw_class);
	COPY(pw_gecos);
	COPY(pw_dir);
	COPY(pw_shell);
#undef COPY
	p = buffer;
	eob = &buffer[bufsize];
#define COPY(field, flag) do {						 \
	q = (template->pw_fields & flag) ? template->field : hold.field; \
	if (q == NULL)							 \
		pwd->field = NULL;					 \
	else {								 \
		pwd->field = p;						 \
		if ((n = strlcpy(p, q, eob-p)) >= eob-p) {		 \
			free(copy);					 \
			return (ERANGE);				 \
		}							 \
		p += n + 1;						 \
	}								 \
} while (0)
	COPY(pw_name, 0);
#ifdef PW_OVERRIDE_PASSWD
	COPY(pw_passwd, _PWF_PASSWD);
#else
	COPY(pw_passwd, 0);
#endif
	COPY(pw_class, 0);
	COPY(pw_gecos, _PWF_GECOS);
	COPY(pw_dir, _PWF_DIR);
	COPY(pw_shell, _PWF_SHELL);
#undef COPY
#define COPY(field, flag) do {			\
	if (template->pw_fields & flag)		\
		pwd->field = template->field;	\
} while (0)
	COPY(pw_uid, _PWF_UID);
	COPY(pw_gid, _PWF_GID);
#undef COPY
	free(copy);
	return (0);
}


static int
compat_exclude(const char *name, DB **db)
{
	DBT	key, data;

	if (*db == NULL &&
	    (*db = dbopen(NULL, O_RDWR, 600, DB_HASH, 0)) == NULL)
		return (errno);
	key.size = strlen(name);
	key.data = (char *)name;
	data.size = 0;
	data.data = NULL;

	if ((*db)->put(*db, &key, &data, 0) == -1)
		return (errno);
	return (0);
}


static int
compat_is_excluded(const char *name, DB *db)
{
	DBT	key, data;

	if (db == NULL)
		return (0);
	key.size = strlen(name);
	key.data = (char *)name;
	return (db->get(db, &key, &data, 0) == 0);
}


static int
compat_redispatch(struct compat_state *st, enum nss_lookup_type how,
    enum nss_lookup_type lookup_how, const char *name, const char *lookup_name,
    uid_t uid, struct passwd *pwd, char *buffer, size_t bufsize, int *errnop)
{
	static const ns_src compatsrc[] = {
#ifdef YP
		{ NSSRC_NIS, NS_SUCCESS },
#endif
		{ NULL, 0 }
	};
	ns_dtab dtab[] = {
#ifdef YP
		{ NSSRC_NIS, nis_passwd, NULL },
#endif
#ifdef HESIOD
		{ NSSRC_DNS, dns_passwd, NULL },
#endif
		{ NULL, NULL, NULL }
	};
	void		*discard;
	int		 e, i, rv;

	for (i = 0; i < (int)(nitems(dtab) - 1); i++)
		dtab[i].mdata = (void *)lookup_how;
more:
	__pw_initpwd(pwd);
	switch (lookup_how) {
	case nss_lt_all:
		rv = _nsdispatch(&discard, dtab, NSDB_PASSWD_COMPAT,
		    "getpwent_r", compatsrc, pwd, buffer, bufsize,
		    errnop);
		break;
	case nss_lt_id:
		rv = _nsdispatch(&discard, dtab, NSDB_PASSWD_COMPAT,
		    "getpwuid_r", compatsrc, uid, pwd, buffer,
		    bufsize, errnop);
		break;
	case nss_lt_name:
		rv = _nsdispatch(&discard, dtab, NSDB_PASSWD_COMPAT,
		    "getpwnam_r", compatsrc, lookup_name, pwd, buffer,
		    bufsize, errnop);
		break;
	default:
		return (NS_UNAVAIL);
	}
	if (rv != NS_SUCCESS)
		return (rv);
	if (compat_is_excluded(pwd->pw_name, st->exclude)) {
		if (how == nss_lt_all)
			goto more;
		return (NS_NOTFOUND);
	}
	e = compat_use_template(pwd, &st->template, buffer, bufsize);
	if (e != 0) {
		*errnop = e;
		if (e == ERANGE)
			return (NS_RETURN);
		else
			return (NS_UNAVAIL);
	}
	switch (how) {
	case nss_lt_name:
		if (strcmp(name, pwd->pw_name) != 0)
			return (NS_NOTFOUND);
		break;
	case nss_lt_id:
		if (uid != pwd->pw_uid)
			return (NS_NOTFOUND);
		break;
	default:
		break;
	}
	return (NS_SUCCESS);
}


static void
compat_endstate(void *p)
{
	struct compat_state *st;

	if (p == NULL)
		return;
	st = (struct compat_state *)p;
	if (st->db != NULL)
		st->db->close(st->db);
	if (st->exclude != NULL)
		st->exclude->close(st->exclude);
	compat_clear_template(&st->template);
	free(p);
}


static int
compat_setpwent(void *retval, void *mdata, va_list ap)
{
	static const ns_src compatsrc[] = {
#ifdef YP
		{ NSSRC_NIS, NS_SUCCESS },
#endif
		{ NULL, 0 }
	};
	ns_dtab dtab[] = {
#ifdef YP
		{ NSSRC_NIS, nis_setpwent, NULL },
#endif
#ifdef HESIOD
		{ NSSRC_DNS, dns_setpwent, NULL },
#endif
		{ NULL, NULL, NULL }
	};
	struct compat_state	*st;
	int			 rv, stayopen;

#define set_setent(x, y) do {	 				\
	int i;							\
	for (i = 0; i < (int)(nitems(x) - 1); i++)		\
		x[i].mdata = (void *)y;				\
} while (0)

	rv = compat_getstate(&st);
	if (rv != 0)
		return (NS_UNAVAIL);
	switch ((enum constants)mdata) {
	case SETPWENT:
		stayopen = va_arg(ap, int);
		st->keynum = 0;
		if (stayopen)
			st->db = pwdbopen(&st->version);
		st->stayopen = stayopen;
		set_setent(dtab, mdata);
		(void)_nsdispatch(NULL, dtab, NSDB_PASSWD_COMPAT, "setpwent",
		    compatsrc, 0);
		break;
	case ENDPWENT:
		if (st->db != NULL) {
			(void)st->db->close(st->db);
			st->db = NULL;
		}
		set_setent(dtab, mdata);
		(void)_nsdispatch(NULL, dtab, NSDB_PASSWD_COMPAT, "endpwent",
		    compatsrc, 0);
		break;
	default:
		break;
	}
	return (NS_UNAVAIL);
#undef set_setent
}


static int
compat_passwd(void *retval, void *mdata, va_list ap)
{
	char			 keybuf[MAXLOGNAME + 1];
	DBT			 key, entry;
	struct compat_state	*st;
	enum nss_lookup_type	 how;
	const char		*name;
	struct passwd		*pwd;
	char			*buffer, *pw_name;
	char			*host, *user, *domain;
	size_t			 bufsize;
	uid_t			 uid;
	uint32_t		 store;
	int			 rv, from_compat, stayopen, *errnop;

	from_compat = 0;
	name = NULL;
	uid = (uid_t)-1;
	how = (enum nss_lookup_type)mdata;
	switch (how) {
	case nss_lt_name:
		name = va_arg(ap, const char *);
		break;
	case nss_lt_id:
		uid = va_arg(ap, uid_t);
		break;
	case nss_lt_all:
		break;
	default:
		rv = NS_NOTFOUND;
		goto fin;
	}
	pwd = va_arg(ap, struct passwd *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	*errnop = compat_getstate(&st);
	if (*errnop != 0)
		return (NS_UNAVAIL);
	if (how == nss_lt_all && st->keynum < 0) {
		rv = NS_NOTFOUND;
		goto fin;
	}
	if (st->db == NULL &&
	    (st->db = pwdbopen(&st->version)) == NULL) {
		*errnop = errno;
		rv = NS_UNAVAIL;
		goto fin;
	}
	if (how == nss_lt_all) {
		if (st->keynum < 0) {
			rv = NS_NOTFOUND;
			goto fin;
		}
		stayopen = 1;
	} else {
		st->keynum = 0;
		stayopen = st->stayopen;
	}
docompat:
	rv = NS_NOTFOUND;
	switch (st->compat) {
	case COMPAT_MODE_ALL:
		rv = compat_redispatch(st, how, how, name, name, uid, pwd,
		    buffer, bufsize, errnop);
		if (rv != NS_SUCCESS)
			st->compat = COMPAT_MODE_OFF;
		break;
	case COMPAT_MODE_NETGROUP:
		/* XXX getnetgrent is not thread-safe. */
		do {
			rv = getnetgrent(&host, &user, &domain);
			if (rv == 0) {
				endnetgrent();
				st->compat = COMPAT_MODE_OFF;
				rv = NS_NOTFOUND;
				continue;
			} else if (user == NULL || user[0] == '\0')
				continue;
			rv = compat_redispatch(st, how, nss_lt_name, name,
			    user, uid, pwd, buffer, bufsize, errnop);
		} while (st->compat == COMPAT_MODE_NETGROUP &&
		    !(rv & NS_TERMINATE));
		break;
	case COMPAT_MODE_NAME:
		rv = compat_redispatch(st, how, nss_lt_name, name, st->name,
		    uid, pwd, buffer, bufsize, errnop);
		free(st->name);
		st->name = NULL;
		st->compat = COMPAT_MODE_OFF;
		break;
	default:
		break;
	}
	if (rv & NS_TERMINATE) {
		from_compat = 1;
		goto fin;
	}
	key.data = keybuf;
	rv = NS_NOTFOUND;
	while (st->keynum >= 0) {
		st->keynum++;
		if (st->version < _PWD_CURRENT_VERSION) {
			memcpy(&keybuf[1], &st->keynum, sizeof(st->keynum));
			key.size = sizeof(st->keynum) + 1;
		} else {
			store = htonl(st->keynum);
			memcpy(&keybuf[1], &store, sizeof(store));
			key.size = sizeof(store) + 1;
		}
		keybuf[0] = _PW_VERSIONED(_PW_KEYBYNUM, st->version);
		rv = st->db->get(st->db, &key, &entry, 0);
		if (rv < 0 || rv > 1) { /* should never return > 1 */
			*errnop = errno;
			rv = NS_UNAVAIL;
			goto fin;
		} else if (rv == 1) {
			st->keynum = -1;
			rv = NS_NOTFOUND;
			goto fin;
		}
		pw_name = (char *)entry.data;
		switch (pw_name[0]) {
		case '+':
			switch (pw_name[1]) {
			case '\0':
				st->compat = COMPAT_MODE_ALL;
				break;
			case '@':
				setnetgrent(&pw_name[2]);
				st->compat = COMPAT_MODE_NETGROUP;
				break;
			default:
				st->name = strdup(&pw_name[1]);
				if (st->name == NULL) {
					syslog(LOG_ERR,
					 "getpwent memory allocation failure");
					*errnop = ENOMEM;
					rv = NS_UNAVAIL;
					break;
				}
				st->compat = COMPAT_MODE_NAME;
			}
			if (entry.size > bufsize) {
				*errnop = ERANGE;
				rv = NS_RETURN;
				goto fin;
			}
			memcpy(buffer, entry.data, entry.size);
			rv = pwdb_versions[st->version].parse(buffer,
			    entry.size, pwd, errnop);
			if (rv != NS_SUCCESS)
				;
			else if (compat_set_template(pwd, &st->template) < 0) {
				*errnop = ENOMEM;
				rv = NS_UNAVAIL;
				goto fin;
			}
			goto docompat;
		case '-':
			switch (pw_name[1]) {
			case '\0':
				/* XXX Maybe syslog warning */
				continue;
			case '@':
				setnetgrent(&pw_name[2]);
				while (getnetgrent(&host, &user, &domain) !=
				    0) {
					if (user != NULL && user[0] != '\0')
						compat_exclude(user,
						    &st->exclude);
				}
				endnetgrent();
				continue;
			default:
				compat_exclude(&pw_name[1], &st->exclude);
				continue;
			}
			break;
		default:
			break;
		}
		if (compat_is_excluded((char *)entry.data, st->exclude))
			continue;
		rv = pwdb_versions[st->version].match(entry.data, entry.size,
		    how, name, uid);
		if (rv == NS_RETURN)
			break;
		else if (rv != NS_SUCCESS)
			continue;
		if (entry.size > bufsize) {
			*errnop = ERANGE;
			rv = NS_RETURN;
			break;
		}
		memcpy(buffer, entry.data, entry.size);
		rv = pwdb_versions[st->version].parse(buffer, entry.size, pwd,
		    errnop);
		if (rv & NS_TERMINATE)
			break;
	}
fin:
	if (st->db != NULL && !stayopen) {
		(void)st->db->close(st->db);
		st->db = NULL;
	}
	if (rv == NS_SUCCESS) {
		if (!from_compat) {
			pwd->pw_fields &= ~_PWF_SOURCE;
			pwd->pw_fields |= _PWF_FILES;
		}
		if (retval != NULL)
			*(struct passwd **)retval = pwd;
	}
	return (rv);
}


/*
 * common passwd line matching and parsing
 */
int
__pw_match_entry(const char *entry, size_t entrysize, enum nss_lookup_type how,
    const char *name, uid_t uid)
{
	const char	*p, *eom;
	char		*q;
	size_t		 len;
	unsigned long	 m;

	eom = entry + entrysize;
	for (p = entry; p < eom; p++)
		if (*p == ':')
			break;
	if (*p != ':')
		return (NS_NOTFOUND);
	if (how == nss_lt_all)
		return (NS_SUCCESS);
	if (how == nss_lt_name) {
		len = strlen(name);
		if (len == (p - entry) && memcmp(name, entry, len) == 0)
			return (NS_SUCCESS);
		else
			return (NS_NOTFOUND);
	}
	for (p++; p < eom; p++)
		if (*p == ':')
			break;
	if (*p != ':')
		return (NS_NOTFOUND);
	m = strtoul(++p, &q, 10);
	if (q[0] != ':' || (uid_t)m != uid)
		return (NS_NOTFOUND);
	else
		return (NS_SUCCESS);
}


/* XXX buffer must be NUL-terminated.  errnop is not set correctly. */
int
__pw_parse_entry(char *buffer, size_t bufsize __unused, struct passwd *pwd,
    int master, int *errnop __unused)
{

	if (__pw_scan(buffer, pwd, master ? _PWSCAN_MASTER : 0) == 0)
		return (NS_NOTFOUND);
	else
		return (NS_SUCCESS);
}
