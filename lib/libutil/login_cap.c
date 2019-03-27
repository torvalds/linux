/*-
 * Copyright (c) 1996 by
 * Sean Eric Fagan <sef@kithrup.com>
 * David Nugent <davidn@blaze.net.au>
 * All rights reserved.
 *
 * Portions copyright (c) 1995,1997
 * Berkeley Software Design, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, is permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. This work was done expressly for inclusion into FreeBSD.  Other use
 *    is permitted provided this notation is included.
 * 4. Absolutely no warranty of function or purpose is made by the authors.
 * 5. Modifications may be freely made to this file providing the above
 *    conditions are met.
 *
 * Low-level routines relating to the user capabilities database
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/param.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <login_cap.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/*
 * allocstr()
 * Manage a single static pointer for handling a local char* buffer,
 * resizing as necessary to contain the string.
 *
 * allocarray()
 * Manage a static array for handling a group of strings, resizing
 * when necessary.
 */

static int lc_object_count = 0;

static size_t internal_stringsz = 0;
static char * internal_string = NULL;
static size_t internal_arraysz = 0;
static const char ** internal_array = NULL;

static char path_login_conf[] = _PATH_LOGIN_CONF;

static char *
allocstr(const char *str)
{
    char    *p;

    size_t sz = strlen(str) + 1;	/* realloc() only if necessary */
    if (sz <= internal_stringsz)
	p = strcpy(internal_string, str);
    else if ((p = realloc(internal_string, sz)) != NULL) {
	internal_stringsz = sz;
	internal_string = strcpy(p, str);
    }
    return p;
}


static const char **
allocarray(size_t sz)
{
    static const char    **p;

    if (sz <= internal_arraysz)
	p = internal_array;
    else if ((p = reallocarray(internal_array, sz, sizeof(char*))) != NULL) {
	internal_arraysz = sz;
	internal_array = p;
    }
    return p;
}


/*
 * arrayize()
 * Turn a simple string <str> separated by any of
 * the set of <chars> into an array.  The last element
 * of the array will be NULL, as is proper.
 * Free using freearraystr()
 */

static const char **
arrayize(const char *str, const char *chars, int *size)
{
    int	    i;
    char *ptr;
    const char *cptr;
    const char **res = NULL;

    /* count the sub-strings */
    for (i = 0, cptr = str; *cptr; i++) {
	int count = strcspn(cptr, chars);
	cptr += count;
	if (*cptr)
	    ++cptr;
    }

    /* alloc the array */
    if ((ptr = allocstr(str)) != NULL) {
	if ((res = allocarray(++i)) == NULL)
	    free((void *)(uintptr_t)(const void *)str);
	else {
	    /* now split the string */
	    i = 0;
	    while (*ptr) {
		int count = strcspn(ptr, chars);
		res[i++] = ptr;
		ptr += count;
		if (*ptr)
		    *ptr++ = '\0';
	    }
	    res[i] = NULL;
	}
    }

    if (size)
	*size = i;

    return res;
}


/*
 * login_close()
 * Frees up all resources relating to a login class
 *
 */

void
login_close(login_cap_t * lc)
{
    if (lc) {
	free(lc->lc_style);
	free(lc->lc_class);
	free(lc->lc_cap);
	free(lc);
	if (--lc_object_count == 0) {
	    free(internal_string);
	    free(internal_array);
	    internal_array = NULL;
	    internal_arraysz = 0;
	    internal_string = NULL;
	    internal_stringsz = 0;
	    cgetclose();
	}
    }
}


/*
 * login_getclassbyname()
 * Get the login class by its name.
 * If the name given is NULL or empty, the default class
 * LOGIN_DEFCLASS (i.e., "default") is fetched.
 * If the name given is LOGIN_MECLASS and
 * 'pwd' argument is non-NULL and contains an non-NULL
 * dir entry, then the file _FILE_LOGIN_CONF is picked
 * up from that directory and used before the system
 * login database. In that case the system login database
 * is looked up using LOGIN_MECLASS, too, which is a bug.
 * Return a filled-out login_cap_t structure, including
 * class name, and the capability record buffer.
 */

login_cap_t *
login_getclassbyname(char const *name, const struct passwd *pwd)
{
    login_cap_t	*lc;
  
    if ((lc = malloc(sizeof(login_cap_t))) != NULL) {
	int         r, me, i = 0;
	uid_t euid = 0;
	gid_t egid = 0;
	const char  *msg = NULL;
	const char  *dir;
	char	    userpath[MAXPATHLEN];

	static char *login_dbarray[] = { NULL, NULL, NULL };

	me = (name != NULL && strcmp(name, LOGIN_MECLASS) == 0);
	dir = (!me || pwd == NULL) ? NULL : pwd->pw_dir;
	/*
	 * Switch to user mode before checking/reading its ~/.login_conf
	 * - some NFSes have root read access disabled.
	 *
	 * XXX: This fails to configure additional groups.
	 */
	if (dir) {
	    euid = geteuid();
	    egid = getegid();
	    (void)setegid(pwd->pw_gid);
	    (void)seteuid(pwd->pw_uid);
	}

	if (dir && snprintf(userpath, MAXPATHLEN, "%s/%s", dir,
			    _FILE_LOGIN_CONF) < MAXPATHLEN) {
	    if (_secure_path(userpath, pwd->pw_uid, pwd->pw_gid) != -1)
		login_dbarray[i++] = userpath;
	}
	/*
	 * XXX: Why to add the system database if the class is `me'?
	 */
	if (_secure_path(path_login_conf, 0, 0) != -1)
	    login_dbarray[i++] = path_login_conf;
	login_dbarray[i] = NULL;

	memset(lc, 0, sizeof(login_cap_t));
	lc->lc_cap = lc->lc_class = lc->lc_style = NULL;

	if (name == NULL || *name == '\0')
	    name = LOGIN_DEFCLASS;

	switch (cgetent(&lc->lc_cap, login_dbarray, name)) {
	case -1:		/* Failed, entry does not exist */
	    if (me)
		break;	/* Don't retry default on 'me' */
	    if (i == 0)
	        r = -1;
	    else if ((r = open(login_dbarray[0], O_RDONLY | O_CLOEXEC)) >= 0)
	        close(r);
	    /*
	     * If there's at least one login class database,
	     * and we aren't searching for a default class
	     * then complain about a non-existent class.
	     */
	    if (r >= 0 || strcmp(name, LOGIN_DEFCLASS) != 0)
		syslog(LOG_ERR, "login_getclass: unknown class '%s'", name);
	    /* fall-back to default class */
	    name = LOGIN_DEFCLASS;
	    msg = "%s: no default/fallback class '%s'";
	    if (cgetent(&lc->lc_cap, login_dbarray, name) != 0 && r >= 0)
		break;
	    /* FALLTHROUGH - just return system defaults */
	case 0:		/* success! */
	    if ((lc->lc_class = strdup(name)) != NULL) {
		if (dir) {
		    (void)seteuid(euid);
		    (void)setegid(egid);
		}
		++lc_object_count;
		return lc;
	    }
	    msg = "%s: strdup: %m";
	    break;
	case -2:
	    msg = "%s: retrieving class information: %m";
	    break;
	case -3:
	    msg = "%s: 'tc=' reference loop '%s'";
	    break;
	case 1:
	    msg = "couldn't resolve 'tc=' reference in '%s'";
	    break;
	default:
	    msg = "%s: unexpected cgetent() error '%s': %m";
	    break;
	}
	if (dir) {
	    (void)seteuid(euid);
	    (void)setegid(egid);
	}
	if (msg != NULL)
	    syslog(LOG_ERR, msg, "login_getclass", name);
	free(lc);
    }

    return NULL;
}



/*
 * login_getclass()
 * Get the login class for the system (only) login class database.
 * Return a filled-out login_cap_t structure, including
 * class name, and the capability record buffer.
 */

login_cap_t *
login_getclass(const char *cls)
{
    return login_getclassbyname(cls, NULL);
}


/*
 * login_getpwclass()
 * Get the login class for a given password entry from
 * the system (only) login class database.
 * If the password entry's class field is not set, or
 * the class specified does not exist, then use the
 * default of LOGIN_DEFCLASS (i.e., "default") for an unprivileged
 * user or that of LOGIN_DEFROOTCLASS (i.e., "root") for a super-user.
 * Return a filled-out login_cap_t structure, including
 * class name, and the capability record buffer.
 */

login_cap_t *
login_getpwclass(const struct passwd *pwd)
{
    const char	*cls = NULL;

    if (pwd != NULL) {
	cls = pwd->pw_class;
	if (cls == NULL || *cls == '\0')
	    cls = (pwd->pw_uid == 0) ? LOGIN_DEFROOTCLASS : LOGIN_DEFCLASS;
    }
    /*
     * XXX: pwd should be unused by login_getclassbyname() unless cls is `me',
     *      so NULL can be passed instead of pwd for more safety.
     */
    return login_getclassbyname(cls, pwd);
}


/*
 * login_getuserclass()
 * Get the `me' login class, allowing user overrides via ~/.login_conf.
 * Note that user overrides are allowed only in the `me' class.
 */

login_cap_t *
login_getuserclass(const struct passwd *pwd)
{
    return login_getclassbyname(LOGIN_MECLASS, pwd);
}


/*
 * login_getcapstr()
 * Given a login_cap entry, and a capability name, return the
 * value defined for that capability, a default if not found, or
 * an error string on error.
 */

const char *
login_getcapstr(login_cap_t *lc, const char *cap, const char *def, const char *error)
{
    char    *res;
    int	    ret;

    if (lc == NULL || cap == NULL || lc->lc_cap == NULL || *cap == '\0')
	return def;

    if ((ret = cgetstr(lc->lc_cap, cap, &res)) == -1)
	return def;
    return (ret >= 0) ? res : error;
}


/*
 * login_getcaplist()
 * Given a login_cap entry, and a capability name, return the
 * value defined for that capability split into an array of
 * strings.
 */

const char **
login_getcaplist(login_cap_t *lc, const char *cap, const char *chars)
{
    const char *lstring;

    if (chars == NULL)
	chars = ", \t";
    if ((lstring = login_getcapstr(lc, cap, NULL, NULL)) != NULL)
	return arrayize(lstring, chars, NULL);
    return NULL;
}


/*
 * login_getpath()
 * From the login_cap_t <lc>, get the capability <cap> which is
 * formatted as either a space or comma delimited list of paths
 * and append them all into a string and separate by semicolons.
 * If there is an error of any kind, return <error>.
 */

const char *
login_getpath(login_cap_t *lc, const char *cap, const char *error)
{
    const char *str;
    char *ptr;
    int count;

    str = login_getcapstr(lc, cap, NULL, NULL);
    if (str == NULL)
	return error;
    ptr = __DECONST(char *, str); /* XXXX Yes, very dodgy */
    while (*ptr) {
	count = strcspn(ptr, ", \t");
	ptr += count;
	if (*ptr)
	    *ptr++ = ':';
    }
    return str;
}


static int
isinfinite(const char *s)
{
    static const char *infs[] = {
	"infinity",
	"inf",
	"unlimited",
	"unlimit",
	"-1",
	NULL
    };
    const char **i = &infs[0];

    while (*i != NULL) {
	if (strcasecmp(s, *i) == 0)
	    return 1;
	++i;
    }
    return 0;
}


static u_quad_t
rmultiply(u_quad_t n1, u_quad_t n2)
{
    u_quad_t	m, r;
    int		b1, b2;

    static int bpw = 0;

    /* Handle simple cases */
    if (n1 == 0 || n2 == 0)
	return 0;
    if (n1 == 1)
	return n2;
    if (n2 == 1)
	return n1;

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
     * First check the magnitude of each number. If the sum of the
     * magnatude is way to high, reject the number. (If this test
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
     * then adding in residual amout will cause an overflow.
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
 * login_getcaptime()
 * From the login_cap_t <lc>, get the capability <cap>, which is
 * formatted as a time (e.g., "<cap>=10h3m2s").  If <cap> is not
 * present in <lc>, return <def>; if there is an error of some kind,
 * return <error>.
 */

rlim_t
login_getcaptime(login_cap_t *lc, const char *cap, rlim_t def, rlim_t error)
{
    char    *res, *ep, *oval;
    int	    r;
    rlim_t  tot;

    errno = 0;
    if (lc == NULL || lc->lc_cap == NULL)
	return def;

    /*
     * Look for <cap> in lc_cap.
     * If it's not there (-1), return <def>.
     * If there's an error, return <error>.
     */

    if ((r = cgetstr(lc->lc_cap, cap, &res)) == -1)
	return def;
    else if (r < 0) {
	errno = ERANGE;
	return error;
    }

    /* "inf" and "infinity" are special cases */
    if (isinfinite(res))
	return RLIM_INFINITY;

    /*
     * Now go through the string, turning something like 1h2m3s into
     * an integral value.  Whee.
     */

    errno = 0;
    tot = 0;
    oval = res;
    while (*res) {
	rlim_t tim = strtoq(res, &ep, 0);
	rlim_t mult = 1;

	if (ep == NULL || ep == res || errno != 0) {
	invalid:
	    syslog(LOG_WARNING, "login_getcaptime: class '%s' bad value %s=%s",
		   lc->lc_class, cap, oval);
	    errno = ERANGE;
	    return error;
	}
	/* Look for suffixes */
	switch (*ep++) {
	case 0:
	    ep--;
	    break;	/* end of string */
	case 's': case 'S':	/* seconds */
	    break;
	case 'm': case 'M':	/* minutes */
	    mult = 60;
	    break;
	case 'h': case 'H':	/* hours */
	    mult = 60L * 60L;
	    break;
	case 'd': case 'D':	/* days */
	    mult = 60L * 60L * 24L;
	    break;
	case 'w': case 'W':	/* weeks */
	    mult = 60L * 60L * 24L * 7L;
	    break;
	case 'y': case 'Y':	/* 365-day years */
	    mult = 60L * 60L * 24L * 365L;
	    break;
	default:
	    goto invalid;
	}
	res = ep;
	tot += rmultiply(tim, mult);
	if (errno)
	    goto invalid;
    }

    return tot;
}


/*
 * login_getcapnum()
 * From the login_cap_t <lc>, extract the numerical value <cap>.
 * If it is not present, return <def> for a default, and return
 * <error> if there is an error.
 * Like login_getcaptime(), only it only converts to a number, not
 * to a time; "infinity" and "inf" are 'special.'
 */

rlim_t
login_getcapnum(login_cap_t *lc, const char *cap, rlim_t def, rlim_t error)
{
    char    *ep, *res;
    int	    r;
    rlim_t  val;

    if (lc == NULL || lc->lc_cap == NULL)
	return def;

    /*
     * For BSDI compatibility, try for the tag=<val> first
     */
    if ((r = cgetstr(lc->lc_cap, cap, &res)) == -1) {
	long	lval;
	/* string capability not present, so try for tag#<val> as numeric */
	if ((r = cgetnum(lc->lc_cap, cap, &lval)) == -1)
	    return def; /* Not there, so return default */
	else if (r >= 0)
	    return (rlim_t)lval;
    }

    if (r < 0) {
	errno = ERANGE;
	return error;
    }

    if (isinfinite(res))
	return RLIM_INFINITY;

    errno = 0;
    val = strtoq(res, &ep, 0);
    if (ep == NULL || ep == res || errno != 0) {
	syslog(LOG_WARNING, "login_getcapnum: class '%s' bad value %s=%s",
	       lc->lc_class, cap, res);
	errno = ERANGE;
	return error;
    }

    return val;
}



/*
 * login_getcapsize()
 * From the login_cap_t <lc>, extract the capability <cap>, which is
 * formatted as a size (e.g., "<cap>=10M"); it can also be "infinity".
 * If not present, return <def>, or <error> if there is an error of
 * some sort.
 */

rlim_t
login_getcapsize(login_cap_t *lc, const char *cap, rlim_t def, rlim_t error)
{
    char    *ep, *res, *oval;
    int	    r;
    rlim_t  tot;

    if (lc == NULL || lc->lc_cap == NULL)
	return def;

    if ((r = cgetstr(lc->lc_cap, cap, &res)) == -1)
	return def;
    else if (r < 0) {
	errno = ERANGE;
	return error;
    }

    if (isinfinite(res))
	return RLIM_INFINITY;

    errno = 0;
    tot = 0;
    oval = res;
    while (*res) {
	rlim_t siz = strtoq(res, &ep, 0);
	rlim_t mult = 1;

	if (ep == NULL || ep == res || errno != 0) {
	invalid:
	    syslog(LOG_WARNING, "login_getcapsize: class '%s' bad value %s=%s",
		   lc->lc_class, cap, oval);
	    errno = ERANGE;
	    return error;
	}
	switch (*ep++) {
	case 0:	/* end of string */
	    ep--;
	    break;
	case 'b': case 'B':	/* 512-byte blocks */
	    mult = 512;
	    break;
	case 'k': case 'K':	/* 1024-byte Kilobytes */
	    mult = 1024;
	    break;
	case 'm': case 'M':	/* 1024-k kbytes */
	    mult = 1024 * 1024;
	    break;
	case 'g': case 'G':	/* 1Gbyte */
	    mult = 1024 * 1024 * 1024;
	    break;
	case 't': case 'T':	/* 1TBte */
	    mult = 1024LL * 1024LL * 1024LL * 1024LL;
	    break;
	default:
	    goto invalid;
	}
	res = ep;
	tot += rmultiply(siz, mult);
	if (errno)
	    goto invalid;
    }

    return tot;
}


/*
 * login_getcapbool()
 * From the login_cap_t <lc>, check for the existence of the capability
 * of <cap>.  Return <def> if <lc>->lc_cap is NULL, otherwise return
 * the whether or not <cap> exists there.
 */

int
login_getcapbool(login_cap_t *lc, const char *cap, int def)
{
    if (lc == NULL || lc->lc_cap == NULL)
	return def;
    return (cgetcap(lc->lc_cap, cap, ':') != NULL);
}


/*
 * login_getstyle()
 * Given a login_cap entry <lc>, and optionally a type of auth <auth>,
 * and optionally a style <style>, find the style that best suits these
 * rules:
 *	1.  If <auth> is non-null, look for an "auth-<auth>=" string
 *	in the capability; if not present, default to "auth=".
 *	2.  If there is no auth list found from (1), default to
 *	"passwd" as an authorization list.
 *	3.  If <style> is non-null, look for <style> in the list of
 *	authorization methods found from (2); if <style> is NULL, default
 *	to LOGIN_DEFSTYLE ("passwd").
 *	4.  If the chosen style is found in the chosen list of authorization
 *	methods, return that; otherwise, return NULL.
 * E.g.:
 *     login_getstyle(lc, NULL, "ftp");
 *     login_getstyle(lc, "login", NULL);
 *     login_getstyle(lc, "skey", "network");
 */

const char *
login_getstyle(login_cap_t *lc, const char *style, const char *auth)
{
    int	    i;
    const char **authtypes = NULL;
    char    *auths= NULL;
    char    realauth[64];

    static const char *defauthtypes[] = { LOGIN_DEFSTYLE, NULL };

    if (auth != NULL && *auth != '\0') {
	if (snprintf(realauth, sizeof realauth, "auth-%s", auth) < (int)sizeof(realauth))
	    authtypes = login_getcaplist(lc, realauth, NULL);
    }

    if (authtypes == NULL)
	authtypes = login_getcaplist(lc, "auth", NULL);

    if (authtypes == NULL)
	authtypes = defauthtypes;

    /*
     * We have at least one authtype now; auths is a comma-separated
     * (or space-separated) list of authentication types.  We have to
     * convert from this to an array of char*'s; authtypes then gets this.
     */
    i = 0;
    if (style != NULL && *style != '\0') {
	while (authtypes[i] != NULL && strcmp(style, authtypes[i]) != 0)
	    i++;
    }

    lc->lc_style = NULL;
    if (authtypes[i] != NULL && (auths = strdup(authtypes[i])) != NULL)
	lc->lc_style = auths;

    if (lc->lc_style != NULL)
	lc->lc_style = strdup(lc->lc_style);

    return lc->lc_style;
}
