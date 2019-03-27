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
 * Compatibility shims for the GNU C Library-style nsswitch interface.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <errno.h>
#include <nss.h>
#include <pthread.h>
#include <pthread_np.h>
#include "un-namespace.h"
#include "libc_private.h"


struct group;
struct passwd;

static int	terminator;

#define DECLARE_TERMINATOR(x)					\
static pthread_key_t	 _term_key_##x;				\
static void							\
_term_create_##x(void)						\
{								\
	(void)_pthread_key_create(&_term_key_##x, NULL);	\
}								\
static void		*_term_main_##x;			\
static pthread_once_t	 _term_once_##x = PTHREAD_ONCE_INIT

#define SET_TERMINATOR(x, y)						\
do {									\
	if (!__isthreaded || _pthread_main_np())			\
		_term_main_##x = (y);					\
	else {								\
		(void)_pthread_once(&_term_once_##x, _term_create_##x);	\
		(void)_pthread_setspecific(_term_key_##x, y);		\
	}								\
} while (0)

#define CHECK_TERMINATOR(x)					\
(!__isthreaded || _pthread_main_np() ?				\
    (_term_main_##x) :						\
    ((void)_pthread_once(&_term_once_##x, _term_create_##x),	\
    _pthread_getspecific(_term_key_##x)))



DECLARE_TERMINATOR(group);

int __nss_compat_getgrnam_r(void *retval, void *mdata, va_list ap);
int __nss_compat_getgrgid_r(void *retval, void *mdata, va_list ap);
int __nss_compat_getgrent_r(void *retval, void *mdata, va_list ap);
int __nss_compat_setgrent(void *retval, void *mdata, va_list ap);
int __nss_compat_endgrent(void *retval, void *mdata, va_list ap);
int __nss_compat_getpwnam_r(void *retval, void *mdata, va_list ap);
int __nss_compat_getpwuid_r(void *retval, void *mdata, va_list ap);
int __nss_compat_getpwent_r(void *retval, void *mdata, va_list ap);
int __nss_compat_setpwent(void *retval, void *mdata, va_list ap);
int __nss_compat_endpwent(void *retval, void *mdata, va_list ap);

int
__nss_compat_getgrnam_r(void *retval, void *mdata, va_list ap)
{
	int (*fn)(const char *, struct group *, char *, size_t, int *);
	const char	*name;
	struct group	*grp;
	char		*buffer;
	int		*errnop, ns_status;
	size_t		 bufsize;
	enum nss_status	 nss_status;

	fn = mdata;
	name = va_arg(ap, const char *);
	grp = va_arg(ap, struct group *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	nss_status = fn(name, grp, buffer, bufsize, errnop);
	ns_status = __nss_compat_result(nss_status, *errnop);
	if (ns_status == NS_SUCCESS)
		*(struct group **)retval = grp;
	return (ns_status);
}


int
__nss_compat_getgrgid_r(void *retval, void *mdata, va_list ap)
{
	int (*fn)(gid_t, struct group *, char *, size_t, int *);
	gid_t		 gid;
	struct group	*grp;
	char		*buffer;
	int		*errnop, ns_status;
	size_t		 bufsize;
	enum nss_status	 nss_status;
	
	fn = mdata;
	gid = va_arg(ap, gid_t);
	grp = va_arg(ap, struct group *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	nss_status = fn(gid, grp, buffer, bufsize, errnop);
	ns_status = __nss_compat_result(nss_status, *errnop);
	if (ns_status == NS_SUCCESS)
		*(struct group **)retval = grp;
	return (ns_status);
}


int
__nss_compat_getgrent_r(void *retval, void *mdata, va_list ap)
{
	int (*fn)(struct group *, char *, size_t, int *);
	struct group	*grp;
	char		*buffer;
	int		*errnop, ns_status;
	size_t		 bufsize;
	enum nss_status	 nss_status;

	if (CHECK_TERMINATOR(group))
		return (NS_NOTFOUND);
	fn = mdata;
	grp = va_arg(ap, struct group *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	nss_status = fn(grp, buffer, bufsize, errnop);
	ns_status = __nss_compat_result(nss_status, *errnop);
	if (ns_status == NS_SUCCESS)
		*(struct group **)retval = grp;
	else if (ns_status != NS_RETURN)
		SET_TERMINATOR(group, &terminator);
	return (ns_status);
}


int
__nss_compat_setgrent(void *retval, void *mdata, va_list ap)
{

	SET_TERMINATOR(group, NULL);
	((int (*)(void))mdata)();
	return (NS_UNAVAIL);
}


int
__nss_compat_endgrent(void *retval, void *mdata, va_list ap)
{

	SET_TERMINATOR(group, NULL);
	((int (*)(void))mdata)();
	return (NS_UNAVAIL);
}



DECLARE_TERMINATOR(passwd);


int
__nss_compat_getpwnam_r(void *retval, void *mdata, va_list ap)
{
	int (*fn)(const char *, struct passwd *, char *, size_t, int *);
	const char	*name;
	struct passwd	*pwd;
	char		*buffer;
	int		*errnop, ns_status;
	size_t		 bufsize;
	enum nss_status	 nss_status;

	fn = mdata;
	name = va_arg(ap, const char *);
	pwd = va_arg(ap, struct passwd *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	nss_status = fn(name, pwd, buffer, bufsize, errnop);
	ns_status = __nss_compat_result(nss_status, *errnop);
	if (ns_status == NS_SUCCESS)
		*(struct passwd **)retval = pwd;
	return (ns_status);
}


int
__nss_compat_getpwuid_r(void *retval, void *mdata, va_list ap)
{
	int (*fn)(uid_t, struct passwd *, char *, size_t, int *);
	uid_t		 uid;
	struct passwd	*pwd;
	char		*buffer;
	int		*errnop, ns_status;
	size_t		 bufsize;
	enum nss_status	 nss_status;
	
	fn = mdata;
	uid = va_arg(ap, uid_t);
	pwd = va_arg(ap, struct passwd *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	nss_status = fn(uid, pwd, buffer, bufsize, errnop);
	ns_status = __nss_compat_result(nss_status, *errnop);
	if (ns_status == NS_SUCCESS)
		*(struct passwd **)retval = pwd;
	return (ns_status);
}


int
__nss_compat_getpwent_r(void *retval, void *mdata, va_list ap)
{
	int (*fn)(struct passwd *, char *, size_t, int *);
	struct passwd	*pwd;
	char		*buffer;
	int		*errnop, ns_status;
	size_t		 bufsize;
	enum nss_status	 nss_status;

	if (CHECK_TERMINATOR(passwd))
		return (NS_NOTFOUND);
	fn = mdata;
	pwd = va_arg(ap, struct passwd *);
	buffer = va_arg(ap, char *);
	bufsize = va_arg(ap, size_t);
	errnop = va_arg(ap, int *);
	nss_status = fn(pwd, buffer, bufsize, errnop);
	ns_status = __nss_compat_result(nss_status, *errnop);
	if (ns_status == NS_SUCCESS)
		*(struct passwd **)retval = pwd;
	else if (ns_status != NS_RETURN)
		SET_TERMINATOR(passwd, &terminator);
	return (ns_status);
}


int
__nss_compat_setpwent(void *retval, void *mdata, va_list ap)
{

	SET_TERMINATOR(passwd, NULL);
	((int (*)(void))mdata)();
	return (NS_UNAVAIL);
}


int
__nss_compat_endpwent(void *retval, void *mdata, va_list ap)
{

	SET_TERMINATOR(passwd, NULL);
	((int (*)(void))mdata)();
	return (NS_UNAVAIL);
}
