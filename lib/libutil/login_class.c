/*-
 * Copyright (c) 1996 by
 * Sean Eric Fagan <sef@kithrup.com>
 * David Nugent <davidn@blaze.net.au>
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
 * High-level routines relating to use of the user capabilities database
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/mac.h>
#include <sys/resource.h>
#include <sys/rtprio.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <login_cap.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>


static struct login_res {
    const char *what;
    rlim_t (*who)(login_cap_t *, const char *, rlim_t, rlim_t);
    int why;
} resources[] = {
    { "cputime",         login_getcaptime, RLIMIT_CPU     },
    { "filesize",        login_getcapsize, RLIMIT_FSIZE   },
    { "datasize",        login_getcapsize, RLIMIT_DATA    },
    { "stacksize",       login_getcapsize, RLIMIT_STACK   },
    { "memoryuse",       login_getcapsize, RLIMIT_RSS     },
    { "memorylocked",    login_getcapsize, RLIMIT_MEMLOCK },
    { "maxproc",         login_getcapnum,  RLIMIT_NPROC   },
    { "openfiles",       login_getcapnum,  RLIMIT_NOFILE  },
    { "coredumpsize",    login_getcapsize, RLIMIT_CORE    },
    { "sbsize",          login_getcapsize, RLIMIT_SBSIZE  },
    { "vmemoryuse",      login_getcapsize, RLIMIT_VMEM    },
    { "pseudoterminals", login_getcapnum,  RLIMIT_NPTS    },
    { "swapuse",         login_getcapsize, RLIMIT_SWAP    },
    { "kqueues",         login_getcapsize, RLIMIT_KQUEUES },
    { "umtxp",           login_getcapnum,  RLIMIT_UMTXP   },
    { NULL,              0,                0              }
};


void
setclassresources(login_cap_t *lc)
{
    struct login_res *lr;

    if (lc == NULL)
	return;

    for (lr = resources; lr->what != NULL; ++lr) {
	struct rlimit	rlim;

	/*
	 * The login.conf file can have <limit>, <limit>-max, and
	 * <limit>-cur entries.
	 * What we do is get the current current- and maximum- limits.
	 * Then, we try to get an entry for <limit> from the capability,
	 * using the current and max limits we just got as the
	 * default/error values.
	 * *Then*, we try looking for <limit>-cur and <limit>-max,
	 * again using the appropriate values as the default/error
	 * conditions.
	 */

	if (getrlimit(lr->why, &rlim) != 0)
	    syslog(LOG_ERR, "getting %s resource limit: %m", lr->what);
	else {
	    char	name_cur[40];
	    char	name_max[40];
	    rlim_t	rcur = rlim.rlim_cur;
	    rlim_t	rmax = rlim.rlim_max;

	    sprintf(name_cur, "%s-cur", lr->what);
	    sprintf(name_max, "%s-max", lr->what);

	    rcur = (*lr->who)(lc, lr->what, rcur, rcur);
	    rmax = (*lr->who)(lc, lr->what, rmax, rmax);
	    rlim.rlim_cur = (*lr->who)(lc, name_cur, rcur, rcur);
	    rlim.rlim_max = (*lr->who)(lc, name_max, rmax, rmax);

	    if (setrlimit(lr->why, &rlim) == -1)
		syslog(LOG_WARNING, "set class '%s' resource limit %s: %m", lc->lc_class, lr->what);
	}
    }
}



static struct login_vars {
    const char *tag;
    const char *var;
    const char *def;
    int overwrite;
} pathvars[] = {
    { "path",           "PATH",       NULL, 1},
    { "cdpath",         "CDPATH",     NULL, 1},
    { "manpath",        "MANPATH",    NULL, 1},
    { NULL,             NULL,         NULL, 0}
}, envars[] = {
    { "lang",           "LANG",       NULL, 1},
    { "charset",        "MM_CHARSET", NULL, 1},
    { "timezone",       "TZ",         NULL, 1},
    { "term",           "TERM",       NULL, 0},
    { NULL,             NULL,         NULL, 0}
};

static char *
substvar(const char * var, const struct passwd * pwd, int hlen, int pch, int nlen)
{
    char    *np = NULL;

    if (var != NULL) {
	int	tildes = 0;
	int	dollas = 0;
	char	*p;
	const char *q;

	if (pwd != NULL) {
	    for (q = var; *q != '\0'; ++q) {
		tildes += (*q == '~');
		dollas += (*q == '$');
	    }
	}

	np = malloc(strlen(var) + (dollas * nlen)
		    - dollas + (tildes * (pch+hlen))
		    - tildes + 1);

	if (np != NULL) {
	    p = strcpy(np, var);

	    if (pwd != NULL) {
		/*
		 * This loop does user username and homedir substitutions
		 * for unescaped $ (username) and ~ (homedir)
		 */
		while (*(p += strcspn(p, "~$")) != '\0') {
		    int	l = strlen(p);

		    if (p > np && *(p-1) == '\\')  /* Escaped: */
			memmove(p - 1, p, l + 1); /* Slide-out the backslash */
		    else if (*p == '~') {
			int	v = pch && *(p+1) != '/'; /* Avoid double // */
			memmove(p + hlen + v, p + 1, l);  /* Subst homedir */
			memmove(p, pwd->pw_dir, hlen);
			if (v)
			    p[hlen] = '/';
			p += hlen + v;
		    }
		    else /* if (*p == '$') */ {
			memmove(p + nlen, p + 1, l);	/* Subst username */
			memmove(p, pwd->pw_name, nlen);
			p += nlen;
		    }
		}
	    }
	}
    }

    return (np);
}


void
setclassenvironment(login_cap_t *lc, const struct passwd * pwd, int paths)
{
    struct login_vars	*vars = paths ? pathvars : envars;
    int			hlen = pwd ? strlen(pwd->pw_dir) : 0;
    int			nlen = pwd ? strlen(pwd->pw_name) : 0;
    char pch = 0;

    if (hlen && pwd->pw_dir[hlen-1] != '/')
	++pch;

    while (vars->tag != NULL) {
	const char * var = paths ? login_getpath(lc, vars->tag, NULL)
				 : login_getcapstr(lc, vars->tag, NULL, NULL);

	char * np  = substvar(var, pwd, hlen, pch, nlen);

	if (np != NULL) {
	    setenv(vars->var, np, vars->overwrite);
	    free(np);
	} else if (vars->def != NULL) {
	    setenv(vars->var, vars->def, 0);
	}
	++vars;
    }

    /*
     * If we're not processing paths, then see if there is a setenv list by
     * which the admin and/or user may set an arbitrary set of env vars.
     */
    if (!paths) {
	const char	**set_env = login_getcaplist(lc, "setenv", ",");

	if (set_env != NULL) {
	    while (*set_env != NULL) {
		char	*p = strchr(*set_env, '=');

		if (p != NULL) {  /* Discard invalid entries */
		    char	*np;

		    *p++ = '\0';
		    if ((np = substvar(p, pwd, hlen, pch, nlen)) != NULL) {
			setenv(*set_env, np, 1);
			free(np);
		    }
		}
		++set_env;
	    }
	}
    }
}


static int
list2cpuset(const char *list, cpuset_t *mask)
{
	enum { NONE, NUM, DASH } state;
	int lastnum;
	int curnum;
	const char *l;

	state = NONE;
	curnum = lastnum = 0;
	for (l = list; *l != '\0';) {
		if (isdigit(*l)) {
			curnum = atoi(l);
			if (curnum > CPU_SETSIZE)
				errx(EXIT_FAILURE,
				    "Only %d cpus supported", CPU_SETSIZE);
			while (isdigit(*l))
				l++;
			switch (state) {
			case NONE:
				lastnum = curnum;
				state = NUM;
				break;
			case DASH:
				for (; lastnum <= curnum; lastnum++)
					CPU_SET(lastnum, mask);
				state = NONE;
				break;
			case NUM:
			default:
				return (0);
			}
			continue;
		}
		switch (*l) {
		case ',':
			switch (state) {
			case NONE:
				break;
			case NUM:
				CPU_SET(curnum, mask);
				state = NONE;
				break;
			case DASH:
				return (0);
				break;
			}
			break;
		case '-':
			if (state != NUM)
				return (0);
			state = DASH;
			break;
		default:
			return (0);
		}
		l++;
	}
	switch (state) {
		case NONE:
			break;
		case NUM:
			CPU_SET(curnum, mask);
			break;
		case DASH:
			return (0);
	}
	return (1);
}


void
setclasscpumask(login_cap_t *lc)
{
	const char *maskstr;
	cpuset_t maskset;
	cpusetid_t setid;

	maskstr = login_getcapstr(lc, "cpumask", NULL, NULL);
	CPU_ZERO(&maskset);
	if (maskstr == NULL)
		return;
	if (strcasecmp("default", maskstr) == 0)
		return;
	if (!list2cpuset(maskstr, &maskset)) {
		syslog(LOG_WARNING,
		    "list2cpuset(%s) invalid mask specification", maskstr);
		return;
	}

	if (cpuset(&setid) != 0) {
		syslog(LOG_ERR, "cpuset(): %s", strerror(errno));
		return;
	}

	if (cpuset_setaffinity(CPU_LEVEL_CPUSET, CPU_WHICH_PID, -1,
	    sizeof(maskset), &maskset) != 0)
		syslog(LOG_ERR, "cpuset_setaffinity(%s): %s", maskstr,
		    strerror(errno));
}


/*
 * setclasscontext()
 *
 * For the login class <class>, set various class context values
 * (limits, mainly) to the values for that class.  Which values are
 * set are controlled by <flags> -- see <login_class.h> for the
 * possible values.
 *
 * setclasscontext() can only set resources, priority, and umask.
 */

int
setclasscontext(const char *classname, unsigned int flags)
{
    int		rc;
    login_cap_t *lc;

    lc = login_getclassbyname(classname, NULL);

    flags &= LOGIN_SETRESOURCES | LOGIN_SETPRIORITY |
	    LOGIN_SETUMASK | LOGIN_SETPATH;

    rc = lc ? setusercontext(lc, NULL, 0, flags) : -1;
    login_close(lc);
    return (rc);
}



/*
 * Private function which takes care of processing
 */

static mode_t
setlogincontext(login_cap_t *lc, const struct passwd *pwd,
		mode_t mymask, unsigned long flags)
{
    if (lc) {
	/* Set resources */
	if (flags & LOGIN_SETRESOURCES)
	    setclassresources(lc);
	/* See if there's a umask override */
	if (flags & LOGIN_SETUMASK)
	    mymask = (mode_t)login_getcapnum(lc, "umask", mymask, mymask);
	/* Set paths */
	if (flags & LOGIN_SETPATH)
	    setclassenvironment(lc, pwd, 1);
	/* Set environment */
	if (flags & LOGIN_SETENV)
	    setclassenvironment(lc, pwd, 0);
	/* Set cpu affinity */
	if (flags & LOGIN_SETCPUMASK)
	    setclasscpumask(lc);
    }
    return (mymask);
}



/*
 * setusercontext()
 *
 * Given a login class <lc> and a user in <pwd>, with a uid <uid>,
 * set the context as in setclasscontext().  <flags> controls which
 * values are set.
 *
 * The difference between setclasscontext() and setusercontext() is
 * that the former sets things up for an already-existing process,
 * while the latter sets things up from a root context.  Such as might
 * be called from login(1).
 *
 */

int
setusercontext(login_cap_t *lc, const struct passwd *pwd, uid_t uid, unsigned int flags)
{
    rlim_t	p;
    mode_t	mymask;
    login_cap_t *llc = NULL;
    struct sigaction sa, prevsa;
    struct rtprio rtp;
    int error;

    if (lc == NULL) {
	if (pwd != NULL && (lc = login_getpwclass(pwd)) != NULL)
	    llc = lc; /* free this when we're done */
    }

    if (flags & LOGIN_SETPATH)
	pathvars[0].def = uid ? _PATH_DEFPATH : _PATH_STDPATH;

    /* we need a passwd entry to set these */
    if (pwd == NULL)
	flags &= ~(LOGIN_SETGROUP | LOGIN_SETLOGIN | LOGIN_SETMAC);

    /* Set the process priority */
    if (flags & LOGIN_SETPRIORITY) {
	p = login_getcapnum(lc, "priority", LOGIN_DEFPRI, LOGIN_DEFPRI);

	if (p > PRIO_MAX) {
	    rtp.type = RTP_PRIO_IDLE;
	    p -= PRIO_MAX + 1;
	    rtp.prio = p > RTP_PRIO_MAX ? RTP_PRIO_MAX : p;
	    if (rtprio(RTP_SET, 0, &rtp))
		syslog(LOG_WARNING, "rtprio '%s' (%s): %m",
		    pwd ? pwd->pw_name : "-",
		    lc ? lc->lc_class : LOGIN_DEFCLASS);
	} else if (p < PRIO_MIN) {
	    rtp.type = RTP_PRIO_REALTIME;
	    p -= PRIO_MIN - RTP_PRIO_MAX;
	    rtp.prio = p < RTP_PRIO_MIN ? RTP_PRIO_MIN : p;
	    if (rtprio(RTP_SET, 0, &rtp))
		syslog(LOG_WARNING, "rtprio '%s' (%s): %m",
		    pwd ? pwd->pw_name : "-",
		    lc ? lc->lc_class : LOGIN_DEFCLASS);
	} else {
	    if (setpriority(PRIO_PROCESS, 0, (int)p) != 0)
		syslog(LOG_WARNING, "setpriority '%s' (%s): %m",
		    pwd ? pwd->pw_name : "-",
		    lc ? lc->lc_class : LOGIN_DEFCLASS);
	}
    }

    /* Setup the user's group permissions */
    if (flags & LOGIN_SETGROUP) {
	if (setgid(pwd->pw_gid) != 0) {
	    syslog(LOG_ERR, "setgid(%lu): %m", (u_long)pwd->pw_gid);
	    login_close(llc);
	    return (-1);
	}
	if (initgroups(pwd->pw_name, pwd->pw_gid) == -1) {
	    syslog(LOG_ERR, "initgroups(%s,%lu): %m", pwd->pw_name,
		   (u_long)pwd->pw_gid);
	    login_close(llc);
	    return (-1);
	}
    }

    /* Set up the user's MAC label. */
    if ((flags & LOGIN_SETMAC) && mac_is_present(NULL) == 1) {
	const char *label_string;
	mac_t label;

	label_string = login_getcapstr(lc, "label", NULL, NULL);
	if (label_string != NULL) {
	    if (mac_from_text(&label, label_string) == -1) {
		syslog(LOG_ERR, "mac_from_text('%s') for %s: %m",
		    pwd->pw_name, label_string);
		return (-1);
	    }
	    if (mac_set_proc(label) == -1)
		error = errno;
	    else
		error = 0;
	    mac_free(label);
	    if (error != 0) {
		syslog(LOG_ERR, "mac_set_proc('%s') for %s: %s",
		    label_string, pwd->pw_name, strerror(error));
		return (-1);
	    }
	}
    }

    /* Set the sessions login */
    if ((flags & LOGIN_SETLOGIN) && setlogin(pwd->pw_name) != 0) {
	syslog(LOG_ERR, "setlogin(%s): %m", pwd->pw_name);
	login_close(llc);
	return (-1);
    }

    /* Inform the kernel about current login class */
    if (lc != NULL && lc->lc_class != NULL && (flags & LOGIN_SETLOGINCLASS)) {
	/*
	 * XXX: This is a workaround to fail gracefully in case the kernel
	 *      does not support setloginclass(2).
	 */
	bzero(&sa, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sigfillset(&sa.sa_mask);
	sigaction(SIGSYS, &sa, &prevsa);
	error = setloginclass(lc->lc_class);
	sigaction(SIGSYS, &prevsa, NULL);
	if (error != 0) {
	    syslog(LOG_ERR, "setloginclass(%s): %m", lc->lc_class);
#ifdef notyet
	    login_close(llc);
	    return (-1);
#endif
	}
    }

    mymask = (flags & LOGIN_SETUMASK) ? umask(LOGIN_DEFUMASK) : 0;
    mymask = setlogincontext(lc, pwd, mymask, flags);
    login_close(llc);

    /* This needs to be done after anything that needs root privs */
    if ((flags & LOGIN_SETUSER) && setuid(uid) != 0) {
	syslog(LOG_ERR, "setuid(%lu): %m", (u_long)uid);
	return (-1);	/* Paranoia again */
    }

    /*
     * Now, we repeat some of the above for the user's private entries
     */
    if (getuid() == uid && (lc = login_getuserclass(pwd)) != NULL) {
	mymask = setlogincontext(lc, pwd, mymask, flags);
	login_close(lc);
    }

    /* Finally, set any umask we've found */
    if (flags & LOGIN_SETUMASK)
	umask(mymask);

    return (0);
}
