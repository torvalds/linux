/*	$OpenBSD: login_cap.c,v 1.46 2022/12/27 17:10:06 jmc Exp $	*/

/*
 * Copyright (c) 2000-2004 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*-
 * Copyright (c) 1995,1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Berkeley Software Design,
 *	Inc.
 * 4. The name of Berkeley Software Design, Inc.  may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $From: login_cap.c,v 2.16 2000/03/22 17:10:55 donn Exp $
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <login_cap.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>


static	char *_authtypes[] = { LOGIN_DEFSTYLE, 0 };
static	char *expandstr(const char *, const struct passwd *, int);
static	int login_setenv(char *, char *, const struct passwd *, int);
static	int setuserenv(login_cap_t *, const struct passwd *);
static	int setuserpath(login_cap_t *, const struct passwd *);
static	u_quad_t multiply(u_quad_t, u_quad_t);
static	u_quad_t strtolimit(char *, char **, int);
static	u_quad_t strtosize(char *, char **, int);
static	int gsetrl(login_cap_t *, int, char *, int);

login_cap_t *
login_getclass(char *class)
{
	char *classfiles[] = { NULL, NULL, NULL };
	char classpath[PATH_MAX];
	login_cap_t *lc;
	int res, i = 0;

	if ((lc = calloc(1, sizeof(login_cap_t))) == NULL) {
		syslog(LOG_ERR, "%s:%d malloc: %m", __FILE__, __LINE__);
		return (0);
	}

	if (class == NULL || class[0] == '\0')
		class = LOGIN_DEFCLASS;
	else {
		res = snprintf(classpath, PATH_MAX, "%s/%s",
			_PATH_LOGIN_CONF_D, class);
		if (res >= 0 && res < PATH_MAX)
			classfiles[i++] = classpath;
	}

	classfiles[i++] = _PATH_LOGIN_CONF;
	classfiles[i] = NULL;

    	if ((lc->lc_class = strdup(class)) == NULL) {
		syslog(LOG_ERR, "%s:%d strdup: %m", __FILE__, __LINE__);
		free(lc);
		return (0);
	}

	if ((res = cgetent(&lc->lc_cap, classfiles, lc->lc_class)) != 0) {
		lc->lc_cap = 0;
		switch (res) {
		case 1: 
			syslog(LOG_ERR, "%s: couldn't resolve 'tc'",
				lc->lc_class);
			break;
		case -1:
			if ((res = open(_PATH_LOGIN_CONF, O_RDONLY)) >= 0)
				close(res);
			if (strcmp(lc->lc_class, LOGIN_DEFCLASS) == 0 &&
			    res < 0)
				return (lc);
			syslog(LOG_ERR, "%s: unknown class", lc->lc_class);
			break;
		case -2:
			/*
			 * A missing login.conf file is not an error condition.
			 * The individual routines deal reasonably with missing
			 * capabilities and use default values.
			 */
			if (errno == ENOENT)
				return (lc);
			syslog(LOG_ERR, "%s: getting class information: %m",
			    lc->lc_class);
			break;
		case -3:
			syslog(LOG_ERR, "%s: 'tc' reference loop",
				lc->lc_class);
			break;
		default:
			syslog(LOG_ERR, "%s: unexpected cgetent error",
				lc->lc_class);
			break;
		}
		free(lc->lc_class);
		free(lc);
		return (0);
	}
	return (lc);
}
DEF_WEAK(login_getclass);

char *
login_getstyle(login_cap_t *lc, char *style, char *atype)
{
    	char **authtypes = _authtypes;
	char *auths, *ta;
    	char *f1 = NULL, **f2 = NULL;
	int i;

	/* Silently convert 's/key' -> 'skey' */
	if (style && strcmp(style, "s/key") == 0)
		style = "skey";

	free(lc->lc_style);
	lc->lc_style = NULL;

    	if (!atype || !(auths = login_getcapstr(lc, atype, NULL, NULL)))
		auths = login_getcapstr(lc, "auth", NULL, NULL);

	if (auths) {
		f1 = ta = auths;	/* auths malloced by login_getcapstr */
		i = 2;
		while (*ta)
			if (*ta++ == ',')
				++i;
		f2 = authtypes = calloc(sizeof(char *), i);
		if (!authtypes) {
			syslog(LOG_ERR, "malloc: %m");
			free(f1);
			return (0);
		}
		i = 0;
		while (*auths) {
			authtypes[i] = auths;
			while (*auths && *auths != ',')
				++auths;
			if (*auths)
				*auths++ = 0;
			if (!*authtypes[i])
				authtypes[i] = LOGIN_DEFSTYLE;
			++i;
		}
		authtypes[i] = 0;
	}

	if (!style)
		style = authtypes[0];
		
	while (*authtypes && strcmp(style, *authtypes))
		++authtypes;

	if (*authtypes) {
		lc->lc_style = strdup(*authtypes);
		if (lc->lc_style == NULL)
			syslog(LOG_ERR, "strdup: %m");
	}
	free(f1);
	free(f2);
	return (lc->lc_style);
}
DEF_WEAK(login_getstyle);

char *
login_getcapstr(login_cap_t *lc, char *cap, char *def, char *e)
{
	char *res = NULL, *str = e;
	int stat;

	errno = 0;

    	if (!lc->lc_cap)
		return (def);

	switch (stat = cgetstr(lc->lc_cap, cap, &res)) {
	case -1:
		str = def;
		break;
	case -2:
		syslog(LOG_ERR, "%s: getting capability %s: %m",
		    lc->lc_class, cap);
		break;
	default:
		if (stat >= 0)
			str = res;
		else
			syslog(LOG_ERR,
			    "%s: unexpected error with capability %s",
			    lc->lc_class, cap);
		break;
	}

	if (res != NULL && str != res)
		free(res);
	return(str);
}
DEF_WEAK(login_getcapstr);

quad_t
login_getcaptime(login_cap_t *lc, char *cap, quad_t def, quad_t e)
{
	char *ep;
	char *res = NULL, *sres;
	int stat;
	quad_t q, r;

	errno = 0;

    	if (!lc->lc_cap)
		return (def);

	switch (stat = cgetstr(lc->lc_cap, cap, &res)) {
	case -1:
		free(res);
		return (def);
	case -2:
		free(res);
		syslog(LOG_ERR, "%s: getting capability %s: %m",
		    lc->lc_class, cap);
		errno = ERANGE;
		return (e);
	default:
		if (stat >= 0) 
			break;
		free(res);
		syslog(LOG_ERR, "%s: unexpected error with capability %s",
		    lc->lc_class, cap);
		errno = ERANGE;
		return (e);
	}

	errno = 0;

	if (strcasecmp(res, "infinity") == 0) {
		free(res);
		return (RLIM_INFINITY);
	}

	q = 0;
	sres = res;
	while (*res) {
		r = strtoll(res, &ep, 0);
		if (!ep || ep == res ||
		    ((r == QUAD_MIN || r == QUAD_MAX) && errno == ERANGE)) {
invalid:
			syslog(LOG_ERR, "%s:%s=%s: invalid time",
			    lc->lc_class, cap, sres);
			free(sres);
			errno = ERANGE;
			return (e);
		}
		switch (*ep++) {
		case '\0':
			--ep;
			break;
		case 's': case 'S':
			break;
		case 'm': case 'M':
			r *= 60;
			break;
		case 'h': case 'H':
			r *= 60 * 60;
			break;
		case 'd': case 'D':
			r *= 60 * 60 * 24;
			break;
		case 'w': case 'W':
			r *= 60 * 60 * 24 * 7;
			break;
		case 'y': case 'Y':	/* Pretty absurd */
			r *= 60 * 60 * 24 * 365;
			break;
		default:
			goto invalid;
		}
		res = ep;
		q += r;
	}
	free(sres);
	return (q);
}
DEF_WEAK(login_getcaptime);

quad_t
login_getcapnum(login_cap_t *lc, char *cap, quad_t def, quad_t e)
{
	char *ep;
	char *res = NULL;
	int stat;
	quad_t q;

	errno = 0;

    	if (!lc->lc_cap)
		return (def);

	switch (stat = cgetstr(lc->lc_cap, cap, &res)) {
	case -1:
		free(res);
		return (def);
	case -2:
		free(res);
		syslog(LOG_ERR, "%s: getting capability %s: %m",
		    lc->lc_class, cap);
		errno = ERANGE;
		return (e);
	default:
		if (stat >= 0) 
			break;
		free(res);
		syslog(LOG_ERR, "%s: unexpected error with capability %s",
		    lc->lc_class, cap);
		errno = ERANGE;
		return (e);
	}

	errno = 0;

	if (strcasecmp(res, "infinity") == 0) {
		free(res);
		return (RLIM_INFINITY);
	}

    	q = strtoll(res, &ep, 0);
	if (!ep || ep == res || ep[0] ||
	    ((q == QUAD_MIN || q == QUAD_MAX) && errno == ERANGE)) {
		syslog(LOG_ERR, "%s:%s=%s: invalid number",
		    lc->lc_class, cap, res);
		free(res);
		errno = ERANGE;
		return (e);
	}
	free(res);
	return (q);
}
DEF_WEAK(login_getcapnum);

quad_t
login_getcapsize(login_cap_t *lc, char *cap, quad_t def, quad_t e)
{
	char *ep;
	char *res = NULL;
	int stat;
	quad_t q;

	errno = 0;

    	if (!lc->lc_cap)
		return (def);

	switch (stat = cgetstr(lc->lc_cap, cap, &res)) {
	case -1:
		free(res);
		return (def);
	case -2:
		free(res);
		syslog(LOG_ERR, "%s: getting capability %s: %m",
		    lc->lc_class, cap);
		errno = ERANGE;
		return (e);
	default:
		if (stat >= 0) 
			break;
		free(res);
		syslog(LOG_ERR, "%s: unexpected error with capability %s",
		    lc->lc_class, cap);
		errno = ERANGE;
		return (e);
	}

	errno = 0;
	q = strtolimit(res, &ep, 0);
	if (!ep || ep == res || (ep[0] && ep[1]) ||
	    ((q == QUAD_MIN || q == QUAD_MAX) && errno == ERANGE)) {
		syslog(LOG_ERR, "%s:%s=%s: invalid size",
		    lc->lc_class, cap, res);
		free(res);
		errno = ERANGE;
		return (e);
	}
	free(res);
	return (q);
}
DEF_WEAK(login_getcapsize);

int
login_getcapbool(login_cap_t *lc, char *cap, u_int def)
{
    	if (!lc->lc_cap)
		return (def);

	return (cgetcap(lc->lc_cap, cap, ':') != NULL);
}
DEF_WEAK(login_getcapbool);

void
login_close(login_cap_t *lc)
{
	if (lc) {
		free(lc->lc_class);
		free(lc->lc_cap);
		free(lc->lc_style);
		free(lc);
	}
}
DEF_WEAK(login_close);

#define	CTIME	1
#define	CSIZE	2
#define	CNUMB	3

static struct {
	int	what;
	int	type;
	char *	name;
} r_list[] = {
	{ RLIMIT_CPU,		CTIME, "cputime", },
	{ RLIMIT_FSIZE,		CSIZE, "filesize", },
	{ RLIMIT_DATA,		CSIZE, "datasize", },
	{ RLIMIT_STACK,		CSIZE, "stacksize", },
	{ RLIMIT_RSS,		CSIZE, "memoryuse", },
	{ RLIMIT_MEMLOCK,	CSIZE, "memorylocked", },
	{ RLIMIT_NPROC,		CNUMB, "maxproc", },
	{ RLIMIT_NOFILE,	CNUMB, "openfiles", },
	{ RLIMIT_CORE,		CSIZE, "coredumpsize", },
#ifdef RLIMIT_VMEM
	{ RLIMIT_VMEM,		CSIZE, "vmemoryuse", },
#endif
	{ -1, 0, 0 }
};

static int
gsetrl(login_cap_t *lc, int what, char *name, int type)
{
	struct rlimit rl;
	struct rlimit r;
	char name_cur[32];
	char name_max[32];
    	char *v;
	int len;

	/*
	 * If we have no capabilities then there is nothing to do and
	 * we can just return success.
	 */
	if (lc->lc_cap == NULL)
		return (0);

	len = snprintf(name_cur, sizeof name_cur, "%s-cur", name);
	if (len < 0 || len >= sizeof name_cur) {
		syslog(LOG_ERR, "current resource limit name too large");
		return (-1);
	}
	len = snprintf(name_max, sizeof name_max, "%s-max", name);
	if (len < 0 || len >= sizeof name_max) {
		syslog(LOG_ERR, "max resource limit name too large");
		return (-1);
	}

	if (getrlimit(what, &r)) {
		syslog(LOG_ERR, "getting resource limit: %m");
		return (-1);
	}

	/*
	 * We need to pre-fetch the 3 possible strings we will look
	 * up to see what order they come in.  If the one without
	 * the -cur or -max comes in first then we ignore any later
	 * -cur or -max entries.
	 * Note that the cgetent routines will always return failure
	 * on the entry "".  This will cause our login_get* routines
	 * to use the default entry.
	 */
	if ((v = cgetcap(lc->lc_cap, name, '=')) != NULL) {
		if (v < cgetcap(lc->lc_cap, name_cur, '='))
			name_cur[0] = '\0';
		if (v < cgetcap(lc->lc_cap, name_max, '='))
			name_max[0] = '\0';
	}

#define	RCUR	r.rlim_cur
#define	RMAX	r.rlim_max

	switch (type) {
	case CTIME:
		RCUR = (rlim_t)login_getcaptime(lc, name, RCUR, RCUR);
		RMAX = (rlim_t)login_getcaptime(lc, name, RMAX, RMAX);
		rl.rlim_cur = (rlim_t)login_getcaptime(lc, name_cur, RCUR, RCUR);
		rl.rlim_max = (rlim_t)login_getcaptime(lc, name_max, RMAX, RMAX);
		break;
	case CSIZE:
		RCUR = (rlim_t)login_getcapsize(lc, name, RCUR, RCUR);
		RMAX = (rlim_t)login_getcapsize(lc, name, RMAX, RMAX);
		rl.rlim_cur = (rlim_t)login_getcapsize(lc, name_cur, RCUR, RCUR);
		rl.rlim_max = (rlim_t)login_getcapsize(lc, name_max, RMAX, RMAX);
		break;
	case CNUMB:
		RCUR = (rlim_t)login_getcapnum(lc, name, RCUR, RCUR);
		RMAX = (rlim_t)login_getcapnum(lc, name, RMAX, RMAX);
		rl.rlim_cur = (rlim_t)login_getcapnum(lc, name_cur, RCUR, RCUR);
		rl.rlim_max = (rlim_t)login_getcapnum(lc, name_max, RMAX, RMAX);
		break;
	default:
		return (-1);
	}

	if (setrlimit(what, &rl)) {
		syslog(LOG_ERR, "%s: setting resource limit %s: %m",
		    lc->lc_class, name);
		return (-1);
	}
#undef	RCUR
#undef	RMAX
	return (0);
}

int
setclasscontext(char *class, u_int flags)
{
	int ret;
	login_cap_t *lc;

	flags &= LOGIN_SETRESOURCES | LOGIN_SETPRIORITY | LOGIN_SETUMASK |
	    LOGIN_SETPATH | LOGIN_SETRTABLE;

	lc = login_getclass(class);
	ret = lc ? setusercontext(lc, NULL, 0, flags) : -1;
	login_close(lc);
	return (ret);
}

int
setusercontext(login_cap_t *lc, struct passwd *pwd, uid_t uid, u_int flags)
{
	login_cap_t *flc;
	quad_t p, rtable;
	int i;

	flc = NULL;

	if (!lc && !(flc = lc = login_getclass(pwd ? pwd->pw_class : NULL)))
		return (-1);

	/*
	 * Without the pwd entry being passed we cannot set either
	 * the group or the login.  We could complain about it.
	 */
	if (pwd == NULL)
		flags &= ~(LOGIN_SETGROUP|LOGIN_SETLOGIN);

	/*
	 * Verify that we haven't been given invalid values.
	 */
	if (flags & LOGIN_SETGROUP) {
		if (pwd->pw_gid == -1) {
			syslog(LOG_ERR, "setusercontext with invalid gid");
			login_close(flc);
			return (-1);
		}
	}
	if (flags & LOGIN_SETUSER) {
		if (uid == -1) {
			syslog(LOG_ERR, "setusercontext with invalid uid");
			login_close(flc);
			return (-1);
		}
	}

	if (flags & LOGIN_SETRESOURCES)
		for (i = 0; r_list[i].name; ++i) 
			if (gsetrl(lc, r_list[i].what, r_list[i].name,
			    r_list[i].type))
				/* XXX - call syslog()? */;

	if (flags & LOGIN_SETPRIORITY) {
		p = login_getcapnum(lc, "priority", 0, 0);

		if (setpriority(PRIO_PROCESS, 0, (int)p) == -1)
			syslog(LOG_ERR, "%s: setpriority: %m", lc->lc_class);
	}

	if (flags & LOGIN_SETUMASK) {
		p = login_getcapnum(lc, "umask", LOGIN_DEFUMASK,LOGIN_DEFUMASK);
		umask((mode_t)p);
	}

	if (flags & LOGIN_SETRTABLE) {
		rtable = login_getcapnum(lc, "rtable", -1, -1);

		if (rtable >= 0 && setrtable((int)rtable) == -1)
			syslog(LOG_ERR, "%s: setrtable: %m", lc->lc_class);
	}

	if (flags & LOGIN_SETGROUP) {
		if (setresgid(pwd->pw_gid, pwd->pw_gid, pwd->pw_gid) == -1) {
			syslog(LOG_ERR, "setresgid(%u,%u,%u): %m",
			    pwd->pw_gid, pwd->pw_gid, pwd->pw_gid);
			login_close(flc);
			return (-1);
		}

		if (initgroups(pwd->pw_name, pwd->pw_gid) == -1) {
			syslog(LOG_ERR, "initgroups(%s,%u): %m",
			    pwd->pw_name, pwd->pw_gid);
			login_close(flc);
			return (-1);
		}
	}

	if (flags & LOGIN_SETLOGIN)
		if (setlogin(pwd->pw_name) == -1) {
			syslog(LOG_ERR, "setlogin(%s) failure: %m",
			    pwd->pw_name);
			login_close(flc);
			return (-1);
		}

	if (flags & LOGIN_SETUSER) {
		if (setresuid(uid, uid, uid) == -1) {
			syslog(LOG_ERR, "setresuid(%u,%u,%u): %m",
			    uid, uid, uid);
			login_close(flc);
			return (-1);
		}
	}

	if (flags & LOGIN_SETENV) {
		if (setuserenv(lc, pwd) == -1) {
			syslog(LOG_ERR, "could not set user environment: %m");
			login_close(flc);
			return (-1);
		}
	}

	if (flags & LOGIN_SETPATH) {
		if (setuserpath(lc, pwd) == -1) {
			syslog(LOG_ERR, "could not set PATH: %m");
			login_close(flc);
			return (-1);
		}
	}

	login_close(flc);
	return (0);
}
DEF_WEAK(setusercontext);

/*
 * Look up "path" for this user in login.conf and replace whitespace
 * with ':' while expanding '~' and '$'.  Sets the PATH environment
 * variable to the result or _PATH_DEFPATH on error.
 */
static int
setuserpath(login_cap_t *lc, const struct passwd *pwd)
{
	char *path = NULL, *opath = NULL, *op, *np;
	int len, error;

	/*
	 * If we have no capabilities then set _PATH_DEFPATH.
	 */
	if (lc->lc_cap == NULL)
		goto setit;

	if ((len = cgetustr(lc->lc_cap, "path", &opath)) <= 0)
		goto setit;

	if ((path = malloc(len + 1)) == NULL)
		goto setit;

	/* Convert opath from space-separated to colon-separated path. */
	for (op = opath, np = path; *op != '\0'; ) {
		switch (*op) {
		case ' ':
		case '\t':
			/*
			 * Collapse consecutive spaces and trim any space
			 * at the very end.
			 */
			do {
				op++;
			} while (*op == ' ' || *op == '\t');
			if (*op != '\0')
				*np++ = ':';
			break;
		case '\\':
			/* check for escaped whitespace */
			if (*(op + 1) == ' ' || *(op + 1) == '\t')
				*np++ = *op++;
			/* FALLTHROUGH */
		default:
			*np++ = *op++;
			break;
		}
		
	}
	*np = '\0';
setit:
	error = login_setenv("PATH", path ? path : _PATH_DEFPATH, pwd, 1);
	free(opath);
	free(path);
	return (error);
}

/*
 * Look up "setenv" for this user in login.conf and set the comma-separated
 * list of environment variables, expanding '~' and '$'.
 */
static int
setuserenv(login_cap_t *lc, const struct passwd *pwd)
{
	char *beg, *end, *ep, *list, *value;
	int len, error;

	/*
	 * If we have no capabilities then there is nothing to do and
	 * we can just return success.
	 */
	if (lc->lc_cap == NULL)
		return (0);

	if ((len = cgetustr(lc->lc_cap, "setenv", &list)) <= 0)
		return (0);

	for (beg = end = list, ep = list + len + 1; end < ep; end++) {
		switch (*end) {
		case '\\':
			if (*(end + 1) == ',')
				end++;	/* skip escaped comma */
			continue;
		case ',':
		case '\0':
			*end = '\0';
			if (beg == end) {
				beg++;
				continue;
			}
			break;
		default:
			continue;
		}

		if ((value = strchr(beg, '=')) != NULL)
			*value++ = '\0';
		else
			value = "";
		if ((error = login_setenv(beg, value, pwd, 0)) != 0) {
			free(list);
			return (error);
		}
		beg = end + 1;
	}
	free(list);
	return (0);
}

/*
 * Set an environment variable, substituting for ~ and $
 */
static int
login_setenv(char *name, char *ovalue, const struct passwd *pwd, int ispath)
{
	char *value = NULL;
	int error;

	if (*ovalue != '\0')
		value = expandstr(ovalue, pwd, ispath);
	error = setenv(name, value ? value : ovalue, 1);
	free(value);
	return (error);
}

/*
 * Convert an expression of the following forms
 * 	1) A number.
 *	2) A number followed by a b (mult by 512).
 *	3) A number followed by a k (mult by 1024).
 *	5) A number followed by a m (mult by 1024 * 1024).
 *	6) A number followed by a g (mult by 1024 * 1024 * 1024).
 *	7) A number followed by a t (mult by 1024 * 1024 * 1024 * 1024).
 *	8) Two or more numbers (with/without k,b,m,g, or t).
 *	   separated by x (also * for backwards compatibility), specifying
 *	   the product of the indicated values.
 */
static
u_quad_t
strtosize(char *str, char **endptr, int radix)
{
	u_quad_t num, num2;
	char *expr, *expr2;

	errno = 0;
	num = strtoull(str, &expr, radix);
	if (errno || expr == str) {
		if (endptr)
			*endptr = expr;
		return (num);
	}

	switch(*expr) {
	case 'b': case 'B':
		num = multiply(num, (u_quad_t)512);
		++expr;
		break;
	case 'k': case 'K':
		num = multiply(num, (u_quad_t)1024);
		++expr;
		break;
	case 'm': case 'M':
		num = multiply(num, (u_quad_t)1024 * 1024);
		++expr;
		break;
	case 'g': case 'G':
		num = multiply(num, (u_quad_t)1024 * 1024 * 1024);
		++expr;
		break;
	case 't': case 'T':
		num = multiply(num, (u_quad_t)1024 * 1024);
		num = multiply(num, (u_quad_t)1024 * 1024);
		++expr;
		break;
	}

	if (errno)
		goto erange;

	switch(*expr) {
	case '*':			/* Backward compatible. */
	case 'x':
		num2 = strtosize(expr+1, &expr2, radix);
		if (errno) {
			expr = expr2;
			goto erange;
		}

		if (expr2 == expr + 1) {
			if (endptr)
				*endptr = expr;
			return (num);
		}
		expr = expr2;
		num = multiply(num, num2);
		if (errno)
			goto erange;
		break;
	}
	if (endptr)
		*endptr = expr;
	return (num);
erange:
	if (endptr)
		*endptr = expr;
	errno = ERANGE;
	return (UQUAD_MAX);
}

static
u_quad_t
strtolimit(char *str, char **endptr, int radix)
{
	if (strcasecmp(str, "infinity") == 0 || strcasecmp(str, "inf") == 0) {
		if (endptr)
			*endptr = str + strlen(str);
		return ((u_quad_t)RLIM_INFINITY);
	}
	return (strtosize(str, endptr, radix));
}

static u_quad_t
multiply(u_quad_t n1, u_quad_t n2)
{
	static int bpw = 0;
	u_quad_t m;
	u_quad_t r;
	int b1, b2;

	/*
	 * Get rid of the simple cases
	 */
	if (n1 == 0 || n2 == 0)
		return (0);
	if (n1 == 1)
		return (n2);
	if (n2 == 1)
		return (n1);

	/*
	 * sizeof() returns number of bytes needed for storage.
	 * This may be different from the actual number of useful bits.
	 */
	if (!bpw) {
		bpw = sizeof(u_quad_t) * 8;
		while (((u_quad_t)1 << (bpw-1)) == 0)
			--bpw;
	}

	/*
	 * First check the magnitude of each number.  If the sum of the
	 * magnitude is way to high, reject the number.  (If this test
	 * is not done then the first multiply below may overflow.)
	 */
	for (b1 = bpw; (((u_quad_t)1 << (b1-1)) & n1) == 0; --b1)
		; 
	for (b2 = bpw; (((u_quad_t)1 << (b2-1)) & n2) == 0; --b2)
		; 
	if (b1 + b2 - 2 > bpw) {
		errno = ERANGE;
		return (UQUAD_MAX);
	}

	/*
	 * Decompose the multiplication to be:
	 * h1 = n1 & ~1
	 * h2 = n2 & ~1
	 * l1 = n1 & 1
	 * l2 = n2 & 1
	 * (h1 + l1) * (h2 + l2)
	 * (h1 * h2) + (h1 * l2) + (l1 * h2) + (l1 * l2)
	 *
	 * Since h1 && h2 do not have the low bit set, we can then say:
	 *
	 * (h1>>1 * h2>>1 * 4) + ...
	 *
	 * So if (h1>>1 * h2>>1) > (1<<(bpw - 2)) then the result will
	 * overflow.
	 *
	 * Finally, if MAX - ((h1 * l2) + (l1 * h2) + (l1 * l2)) < (h1*h2)
	 * then adding in residual amount will cause an overflow.
	 */

	m = (n1 >> 1) * (n2 >> 1);

	if (m >= ((u_quad_t)1 << (bpw-2))) {
		errno = ERANGE;
		return (UQUAD_MAX);
	}

	m *= 4;

	r = (n1 & n2 & 1)
	  + (n2 & 1) * (n1 & ~(u_quad_t)1)
	  + (n1 & 1) * (n2 & ~(u_quad_t)1);

	if ((u_quad_t)(m + r) < m) {
		errno = ERANGE;
		return (UQUAD_MAX);
	}
	m += r;

	return (m);
}

/*
 * Check whether or not a tilde in a string should be expanded.
 * We only do expansion for things like "~", "~/...", ~me", "~me/...".
 * Additionally, for paths the tilde must be a the beginning.
 */
#define tilde_valid(s, b, u, l, ip) \
    ((!(ip) || (s) == (b) || (s)[-1] == ':') && \
    ((s)[1] == '/' || (s)[1] == '\0' || \
    (strncmp((s)+1, u, l) == 0 && ((s)[l+1] == '/' || (s)[l+1] == '\0'))))

/*
 * Make a copy of a string, expanding '~' to the user's homedir, '$' to the
 * login name and other escape sequences as per cgetstr(3).
 */
static char *
expandstr(const char *ostr, const struct passwd *pwd, int ispath)
{
	size_t n, olen, nlen, ulen, dlen;
	const char *ep, *eo, *op;
	char *nstr, *np;
	int ch;

	if (pwd != NULL) {
		ulen = strlen(pwd->pw_name);
		dlen = strlen(pwd->pw_dir);
	}

	/* calculate the size of the new string */
	olen = nlen = strlen(ostr);
	for (op = ostr, ep = ostr + olen; op < ep; op++) {
		switch (*op) {
		case '~':
			if (pwd == NULL ||
			    !tilde_valid(op, ostr, pwd->pw_name, ulen, ispath))
				break;
			if (op[1] != '/' && op[1] != '\0') {
				op += ulen;	/* ~username */
				nlen = nlen - ulen - 1 + dlen;
			} else
				nlen += dlen - 1;
			break;
		case '$':
			if (pwd != NULL)
				nlen += ulen - 1;
			break;
		case '^':
			/* control char */
			if (*++op != '\0')
				nlen--;
			break;
		case '\\':
			if (op[1] == '\0')
				break;
			/*
			 * Byte in octal notation (\123) or an escaped char (\t)
			 */
			eo = op + 4;
			do {
				op++;
				nlen--;
			} while (op < eo && *op >= '0' && *op <= '7');
			break;
		}
	}
	if ((np = nstr = malloc(++nlen)) == NULL)
		return (NULL);

	for (op = ostr, ep = ostr + olen; op < ep; op++) {
		switch ((ch = *op)) {
		case '~':
			if (pwd == NULL ||
			    !tilde_valid(op, ostr, pwd->pw_name, ulen, ispath))
				break;
			if (op[1] != '/' && op[1] != '\0')
				op += ulen;	/* ~username */
			strlcpy(np, pwd->pw_dir, nlen);
			nlen -= dlen;
			np += dlen;
			continue;
		case '$':
			if (pwd == NULL)
				break;
			strlcpy(np, pwd->pw_name, nlen);
			nlen -= ulen;
			np += ulen;
			continue;
		case '^':
			if (op[1] != '\0')
				ch = *++op & 037;
			break;
		case '\\':
			if (op[1] == '\0')
				break;
			switch(*++op) {
			case '0': case '1': case '2': case '3':
			case '4': case '5': case '6': case '7':
				/* byte in octal up to 3 digits long */
				ch = 0;
				n = 3;
				do {
					ch = ch * 8 + (*op++ - '0');
				} while (--n && *op >= '0' && *op <= '7');
				break;
			case 'b': case 'B':
				ch = '\b';
				break;
			case 't': case 'T':
				ch = '\t';
				break;
			case 'n': case 'N':
				ch = '\n';
				break;
			case 'f': case 'F':
				ch = '\f';
				break;
			case 'r': case 'R':
				ch = '\r';
				break;
			case 'e': case 'E':
				ch = '\033';
				break;
			case 'c': case 'C':
				ch = ':';
				break;
			default:
				ch = *op;
				break;
			}
			break;
		}
		*np++ = ch;
		nlen--;
	}
	*np = '\0';
	return (nstr);
}
