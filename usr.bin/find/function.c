/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Cimarron D. Taylor of the University of California, Berkeley.
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

#if 0
static const char sccsid[] = "@(#)function.c	8.10 (Berkeley) 5/4/95";
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/acl.h>
#include <sys/wait.h>
#include <sys/mount.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fnmatch.h>
#include <fts.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "find.h"

static PLAN *palloc(OPTION *);
static long long find_parsenum(PLAN *, const char *, char *, char *);
static long long find_parsetime(PLAN *, const char *, char *);
static char *nextarg(OPTION *, char ***);

extern char **environ;

static PLAN *lastexecplus = NULL;

#define	COMPARE(a, b) do {						\
	switch (plan->flags & F_ELG_MASK) {				\
	case F_EQUAL:							\
		return (a == b);					\
	case F_LESSTHAN:						\
		return (a < b);						\
	case F_GREATER:							\
		return (a > b);						\
	default:							\
		abort();						\
	}								\
} while(0)

static PLAN *
palloc(OPTION *option)
{
	PLAN *new;

	if ((new = malloc(sizeof(PLAN))) == NULL)
		err(1, NULL);
	new->execute = option->execute;
	new->flags = option->flags;
	new->next = NULL;
	return new;
}

/*
 * find_parsenum --
 *	Parse a string of the form [+-]# and return the value.
 */
static long long
find_parsenum(PLAN *plan, const char *option, char *vp, char *endch)
{
	long long value;
	char *endchar, *str;	/* Pointer to character ending conversion. */

	/* Determine comparison from leading + or -. */
	str = vp;
	switch (*str) {
	case '+':
		++str;
		plan->flags |= F_GREATER;
		break;
	case '-':
		++str;
		plan->flags |= F_LESSTHAN;
		break;
	default:
		plan->flags |= F_EQUAL;
		break;
	}

	/*
	 * Convert the string with strtoq().  Note, if strtoq() returns zero
	 * and endchar points to the beginning of the string we know we have
	 * a syntax error.
	 */
	value = strtoq(str, &endchar, 10);
	if (value == 0 && endchar == str)
		errx(1, "%s: %s: illegal numeric value", option, vp);
	if (endchar[0] && endch == NULL)
		errx(1, "%s: %s: illegal trailing character", option, vp);
	if (endch)
		*endch = endchar[0];
	return value;
}

/*
 * find_parsetime --
 *	Parse a string of the form [+-]([0-9]+[smhdw]?)+ and return the value.
 */
static long long
find_parsetime(PLAN *plan, const char *option, char *vp)
{
	long long secs, value;
	char *str, *unit;	/* Pointer to character ending conversion. */

	/* Determine comparison from leading + or -. */
	str = vp;
	switch (*str) {
	case '+':
		++str;
		plan->flags |= F_GREATER;
		break;
	case '-':
		++str;
		plan->flags |= F_LESSTHAN;
		break;
	default:
		plan->flags |= F_EQUAL;
		break;
	}

	value = strtoq(str, &unit, 10);
	if (value == 0 && unit == str) {
		errx(1, "%s: %s: illegal time value", option, vp);
		/* NOTREACHED */
	}
	if (*unit == '\0')
		return value;

	/* Units syntax. */
	secs = 0;
	for (;;) {
		switch(*unit) {
		case 's':	/* seconds */
			secs += value;
			break;
		case 'm':	/* minutes */
			secs += value * 60;
			break;
		case 'h':	/* hours */
			secs += value * 3600;
			break;
		case 'd':	/* days */
			secs += value * 86400;
			break;
		case 'w':	/* weeks */
			secs += value * 604800;
			break;
		default:
			errx(1, "%s: %s: bad unit '%c'", option, vp, *unit);
			/* NOTREACHED */
		}
		str = unit + 1;
		if (*str == '\0')	/* EOS */
			break;
		value = strtoq(str, &unit, 10);
		if (value == 0 && unit == str) {
			errx(1, "%s: %s: illegal time value", option, vp);
			/* NOTREACHED */
		}
		if (*unit == '\0') {
			errx(1, "%s: %s: missing trailing unit", option, vp);
			/* NOTREACHED */
		}
	}
	plan->flags |= F_EXACTTIME;
	return secs;
}

/*
 * nextarg --
 *	Check that another argument still exists, return a pointer to it,
 *	and increment the argument vector pointer.
 */
static char *
nextarg(OPTION *option, char ***argvp)
{
	char *arg;

	if ((arg = **argvp) == NULL)
		errx(1, "%s: requires additional arguments", option->name);
	(*argvp)++;
	return arg;
} /* nextarg() */

/*
 * The value of n for the inode times (atime, birthtime, ctime, mtime) is a
 * range, i.e. n matches from (n - 1) to n 24 hour periods.  This interacts
 * with -n, such that "-mtime -1" would be less than 0 days, which isn't what
 * the user wanted.  Correct so that -1 is "less than 1".
 */
#define	TIME_CORRECT(p) \
	if (((p)->flags & F_ELG_MASK) == F_LESSTHAN) \
		++((p)->t_data.tv_sec);

/*
 * -[acm]min n functions --
 *
 *    True if the difference between the
 *		file access time (-amin)
 *		file birth time (-Bmin)
 *		last change of file status information (-cmin)
 *		file modification time (-mmin)
 *    and the current time is n min periods.
 */
int
f_Xmin(PLAN *plan, FTSENT *entry)
{
	if (plan->flags & F_TIME_C) {
		COMPARE((now - entry->fts_statp->st_ctime +
		    60 - 1) / 60, plan->t_data.tv_sec);
	} else if (plan->flags & F_TIME_A) {
		COMPARE((now - entry->fts_statp->st_atime +
		    60 - 1) / 60, plan->t_data.tv_sec);
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
	} else if (plan->flags & F_TIME_B) {
		COMPARE((now - entry->fts_statp->st_birthtime +
		    60 - 1) / 60, plan->t_data.tv_sec);
#endif
	} else {
		COMPARE((now - entry->fts_statp->st_mtime +
		    60 - 1) / 60, plan->t_data.tv_sec);
	}
}

PLAN *
c_Xmin(OPTION *option, char ***argvp)
{
	char *nmins;
	PLAN *new;

	nmins = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	new->t_data.tv_sec = find_parsenum(new, option->name, nmins, NULL);
	new->t_data.tv_nsec = 0;
	TIME_CORRECT(new);
	return new;
}

/*
 * -[acm]time n functions --
 *
 *	True if the difference between the
 *		file access time (-atime)
 *		file birth time (-Btime)
 *		last change of file status information (-ctime)
 *		file modification time (-mtime)
 *	and the current time is n 24 hour periods.
 */

int
f_Xtime(PLAN *plan, FTSENT *entry)
{
	time_t xtime;

	if (plan->flags & F_TIME_A)
		xtime = entry->fts_statp->st_atime;
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
	else if (plan->flags & F_TIME_B)
		xtime = entry->fts_statp->st_birthtime;
#endif
	else if (plan->flags & F_TIME_C)
		xtime = entry->fts_statp->st_ctime;
	else
		xtime = entry->fts_statp->st_mtime;

	if (plan->flags & F_EXACTTIME)
		COMPARE(now - xtime, plan->t_data.tv_sec);
	else
		COMPARE((now - xtime + 86400 - 1) / 86400, plan->t_data.tv_sec);
}

PLAN *
c_Xtime(OPTION *option, char ***argvp)
{
	char *value;
	PLAN *new;

	value = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	new->t_data.tv_sec = find_parsetime(new, option->name, value);
	new->t_data.tv_nsec = 0;
	if (!(new->flags & F_EXACTTIME))
		TIME_CORRECT(new);
	return new;
}

/*
 * -maxdepth/-mindepth n functions --
 *
 *        Does the same as -prune if the level of the current file is
 *        greater/less than the specified maximum/minimum depth.
 *
 *        Note that -maxdepth and -mindepth are handled specially in
 *        find_execute() so their f_* functions are set to f_always_true().
 */
PLAN *
c_mXXdepth(OPTION *option, char ***argvp)
{
	char *dstr;
	PLAN *new;

	dstr = nextarg(option, argvp);
	if (dstr[0] == '-')
		/* all other errors handled by find_parsenum() */
		errx(1, "%s: %s: value must be positive", option->name, dstr);

	new = palloc(option);
	if (option->flags & F_MAXDEPTH)
		maxdepth = find_parsenum(new, option->name, dstr, NULL);
	else
		mindepth = find_parsenum(new, option->name, dstr, NULL);
	return new;
}

#ifdef ACL_TYPE_NFS4
/*
 * -acl function --
 *
 *	Show files with EXTENDED ACL attributes.
 */
int
f_acl(PLAN *plan __unused, FTSENT *entry)
{
	acl_t facl;
	acl_type_t acl_type;
	int acl_supported = 0, ret, trivial;

	if (S_ISLNK(entry->fts_statp->st_mode))
		return 0;
	ret = pathconf(entry->fts_accpath, _PC_ACL_NFS4);
	if (ret > 0) {
		acl_supported = 1;
		acl_type = ACL_TYPE_NFS4;
	} else if (ret < 0 && errno != EINVAL) {
		warn("%s", entry->fts_accpath);
		return (0);
	}
	if (acl_supported == 0) {
		ret = pathconf(entry->fts_accpath, _PC_ACL_EXTENDED);
		if (ret > 0) {
			acl_supported = 1;
			acl_type = ACL_TYPE_ACCESS;
		} else if (ret < 0 && errno != EINVAL) {
			warn("%s", entry->fts_accpath);
			return (0);
		}
	}
	if (acl_supported == 0)
		return (0);

	facl = acl_get_file(entry->fts_accpath, acl_type);
	if (facl == NULL) {
		warn("%s", entry->fts_accpath);
		return (0);
	}
	ret = acl_is_trivial_np(facl, &trivial);
	acl_free(facl);
	if (ret) {
		warn("%s", entry->fts_accpath);
		return (0);
	}
	if (trivial)
		return (0);
	return (1);
}
#endif

PLAN *
c_acl(OPTION *option, char ***argvp __unused)
{
	ftsoptions &= ~FTS_NOSTAT;
	return (palloc(option));
}

/*
 * -delete functions --
 *
 *	True always.  Makes its best shot and continues on regardless.
 */
int
f_delete(PLAN *plan __unused, FTSENT *entry)
{
	/* ignore these from fts */
	if (strcmp(entry->fts_accpath, ".") == 0 ||
	    strcmp(entry->fts_accpath, "..") == 0)
		return 1;

	/* sanity check */
	if (isdepth == 0 ||			/* depth off */
	    (ftsoptions & FTS_NOSTAT))		/* not stat()ing */
		errx(1, "-delete: insecure options got turned on");

	if (!(ftsoptions & FTS_PHYSICAL) ||	/* physical off */
	    (ftsoptions & FTS_LOGICAL))		/* or finally, logical on */
		errx(1, "-delete: forbidden when symlinks are followed");

	/* Potentially unsafe - do not accept relative paths whatsoever */
	if (entry->fts_level > FTS_ROOTLEVEL &&
	    strchr(entry->fts_accpath, '/') != NULL)
		errx(1, "-delete: %s: relative path potentially not safe",
			entry->fts_accpath);

#if HAVE_STRUCT_STAT_ST_FLAGS
	/* Turn off user immutable bits if running as root */
	if ((entry->fts_statp->st_flags & (UF_APPEND|UF_IMMUTABLE)) &&
	    !(entry->fts_statp->st_flags & (SF_APPEND|SF_IMMUTABLE)) &&
	    geteuid() == 0)
		lchflags(entry->fts_accpath,
		       entry->fts_statp->st_flags &= ~(UF_APPEND|UF_IMMUTABLE));
#endif

	/* rmdir directories, unlink everything else */
	if (S_ISDIR(entry->fts_statp->st_mode)) {
		if (rmdir(entry->fts_accpath) < 0 && errno != ENOTEMPTY)
			warn("-delete: rmdir(%s)", entry->fts_path);
	} else {
		if (unlink(entry->fts_accpath) < 0)
			warn("-delete: unlink(%s)", entry->fts_path);
	}

	/* "succeed" */
	return 1;
}

PLAN *
c_delete(OPTION *option, char ***argvp __unused)
{

	ftsoptions &= ~FTS_NOSTAT;	/* no optimise */
	isoutput = 1;			/* possible output */
	isdepth = 1;			/* -depth implied */

	/*
	 * Try to avoid the confusing error message about relative paths
	 * being potentially not safe.
	 */
	if (ftsoptions & FTS_NOCHDIR)
		errx(1, "%s: forbidden when the current directory cannot be opened",
		    "-delete");

	return palloc(option);
}


/*
 * always_true --
 *
 *	Always true, used for -maxdepth, -mindepth, -xdev, -follow, and -true
 */
int
f_always_true(PLAN *plan __unused, FTSENT *entry __unused)
{
	return 1;
}

/*
 * -depth functions --
 *
 *	With argument: True if the file is at level n.
 *	Without argument: Always true, causes descent of the directory hierarchy
 *	to be done so that all entries in a directory are acted on before the
 *	directory itself.
 */
int
f_depth(PLAN *plan, FTSENT *entry)
{
	if (plan->flags & F_DEPTH)
		COMPARE(entry->fts_level, plan->d_data);
	else
		return 1;
}

PLAN *
c_depth(OPTION *option, char ***argvp)
{
	PLAN *new;
	char *str;

	new = palloc(option);

	str = **argvp;
	if (str && !(new->flags & F_DEPTH)) {
		/* skip leading + or - */
		if (*str == '+' || *str == '-')
			str++;
		/* skip sign */
		if (*str == '+' || *str == '-')
			str++;
		if (isdigit(*str))
			new->flags |= F_DEPTH;
	}

	if (new->flags & F_DEPTH) {	/* -depth n */
		char *ndepth;

		ndepth = nextarg(option, argvp);
		new->d_data = find_parsenum(new, option->name, ndepth, NULL);
	} else {			/* -d */
		isdepth = 1;
	}

	return new;
}
 
/*
 * -empty functions --
 *
 *	True if the file or directory is empty
 */
int
f_empty(PLAN *plan __unused, FTSENT *entry)
{
	if (S_ISREG(entry->fts_statp->st_mode) &&
	    entry->fts_statp->st_size == 0)
		return 1;
	if (S_ISDIR(entry->fts_statp->st_mode)) {
		struct dirent *dp;
		int empty;
		DIR *dir;

		empty = 1;
		dir = opendir(entry->fts_accpath);
		if (dir == NULL)
			return 0;
		for (dp = readdir(dir); dp; dp = readdir(dir))
			if (dp->d_name[0] != '.' ||
			    (dp->d_name[1] != '\0' &&
			     (dp->d_name[1] != '.' || dp->d_name[2] != '\0'))) {
				empty = 0;
				break;
			}
		closedir(dir);
		return empty;
	}
	return 0;
}

PLAN *
c_empty(OPTION *option, char ***argvp __unused)
{
	ftsoptions &= ~FTS_NOSTAT;

	return palloc(option);
}

/*
 * [-exec | -execdir | -ok] utility [arg ... ] ; functions --
 *
 *	True if the executed utility returns a zero value as exit status.
 *	The end of the primary expression is delimited by a semicolon.  If
 *	"{}" occurs anywhere, it gets replaced by the current pathname,
 *	or, in the case of -execdir, the current basename (filename
 *	without leading directory prefix). For -exec and -ok,
 *	the current directory for the execution of utility is the same as
 *	the current directory when the find utility was started, whereas
 *	for -execdir, it is the directory the file resides in.
 *
 *	The primary -ok differs from -exec in that it requests affirmation
 *	of the user before executing the utility.
 */
int
f_exec(PLAN *plan, FTSENT *entry)
{
	int cnt;
	pid_t pid;
	int status;
	char *file;

	if (entry == NULL && plan->flags & F_EXECPLUS) {
		if (plan->e_ppos == plan->e_pbnum)
			return (1);
		plan->e_argv[plan->e_ppos] = NULL;
		goto doexec;
	}

	/* XXX - if file/dir ends in '/' this will not work -- can it? */
	if ((plan->flags & F_EXECDIR) && \
	    (file = strrchr(entry->fts_path, '/')))
		file++;
	else
		file = entry->fts_path;

	if (plan->flags & F_EXECPLUS) {
		if ((plan->e_argv[plan->e_ppos] = strdup(file)) == NULL)
			err(1, NULL);
		plan->e_len[plan->e_ppos] = strlen(file);
		plan->e_psize += plan->e_len[plan->e_ppos];
		if (++plan->e_ppos < plan->e_pnummax &&
		    plan->e_psize < plan->e_psizemax)
			return (1);
		plan->e_argv[plan->e_ppos] = NULL;
	} else {
		for (cnt = 0; plan->e_argv[cnt]; ++cnt)
			if (plan->e_len[cnt])
				brace_subst(plan->e_orig[cnt],
				    &plan->e_argv[cnt], file,
				    plan->e_len[cnt]);
	}

doexec:	if ((plan->flags & F_NEEDOK) && !queryuser(plan->e_argv))
		return 0;

	/* make sure find output is interspersed correctly with subprocesses */
	fflush(stdout);
	fflush(stderr);

	switch (pid = fork()) {
	case -1:
		err(1, "fork");
		/* NOTREACHED */
	case 0:
		/* change dir back from where we started */
		if (!(plan->flags & F_EXECDIR) &&
		    !(ftsoptions & FTS_NOCHDIR) && fchdir(dotfd)) {
			warn("chdir");
			_exit(1);
		}
		execvp(plan->e_argv[0], plan->e_argv);
		warn("%s", plan->e_argv[0]);
		_exit(1);
	}
	if (plan->flags & F_EXECPLUS) {
		while (--plan->e_ppos >= plan->e_pbnum)
			free(plan->e_argv[plan->e_ppos]);
		plan->e_ppos = plan->e_pbnum;
		plan->e_psize = plan->e_pbsize;
	}
	pid = waitpid(pid, &status, 0);
	if (pid != -1 && WIFEXITED(status) && !WEXITSTATUS(status))
		return (1);
	if (plan->flags & F_EXECPLUS) {
		exitstatus = 1;
		return (1);
	}
	return (0);
}

/*
 * c_exec, c_execdir, c_ok --
 *	build three parallel arrays, one with pointers to the strings passed
 *	on the command line, one with (possibly duplicated) pointers to the
 *	argv array, and one with integer values that are lengths of the
 *	strings, but also flags meaning that the string has to be massaged.
 */
PLAN *
c_exec(OPTION *option, char ***argvp)
{
	PLAN *new;			/* node returned */
	long argmax;
	int cnt, i;
	char **argv, **ap, **ep, *p;

	/* This would defeat -execdir's intended security. */
	if (option->flags & F_EXECDIR && ftsoptions & FTS_NOCHDIR)
		errx(1, "%s: forbidden when the current directory cannot be opened",
		    "-execdir");

	/* XXX - was in c_execdir, but seems unnecessary!?
	ftsoptions &= ~FTS_NOSTAT;
	*/
	isoutput = 1;

	/* XXX - this is a change from the previous coding */
	new = palloc(option);

	for (ap = argv = *argvp;; ++ap) {
		if (!*ap)
			errx(1,
			    "%s: no terminating \";\" or \"+\"", option->name);
		if (**ap == ';')
			break;
		if (**ap == '+' && ap != argv && strcmp(*(ap - 1), "{}") == 0) {
			new->flags |= F_EXECPLUS;
			break;
		}
	}

	if (ap == argv)
		errx(1, "%s: no command specified", option->name);

	cnt = ap - *argvp + 1;
	if (new->flags & F_EXECPLUS) {
		new->e_ppos = new->e_pbnum = cnt - 2;
		if ((argmax = sysconf(_SC_ARG_MAX)) == -1) {
			warn("sysconf(_SC_ARG_MAX)");
			argmax = _POSIX_ARG_MAX;
		}
		argmax -= 1024;
		for (ep = environ; *ep != NULL; ep++)
			argmax -= strlen(*ep) + 1 + sizeof(*ep);
		argmax -= 1 + sizeof(*ep);
		/*
		 * Ensure that -execdir ... {} + does not mix files
		 * from different directories in one invocation.
		 * Files from the same directory should be handled
		 * in one invocation but there is no code for it.
		 */
		new->e_pnummax = new->flags & F_EXECDIR ? 1 : argmax / 16;
		argmax -= sizeof(char *) * new->e_pnummax;
		if (argmax <= 0)
			errx(1, "no space for arguments");
		new->e_psizemax = argmax;
		new->e_pbsize = 0;
		cnt += new->e_pnummax + 1;
		new->e_next = lastexecplus;
		lastexecplus = new;
	}
	if ((new->e_argv = malloc(cnt * sizeof(char *))) == NULL)
		err(1, NULL);
	if ((new->e_orig = malloc(cnt * sizeof(char *))) == NULL)
		err(1, NULL);
	if ((new->e_len = malloc(cnt * sizeof(int))) == NULL)
		err(1, NULL);

	for (argv = *argvp, cnt = 0; argv < ap; ++argv, ++cnt) {
		new->e_orig[cnt] = *argv;
		if (new->flags & F_EXECPLUS)
			new->e_pbsize += strlen(*argv) + 1;
		for (p = *argv; *p; ++p)
			if (!(new->flags & F_EXECPLUS) && p[0] == '{' &&
			    p[1] == '}') {
				if ((new->e_argv[cnt] =
				    malloc(MAXPATHLEN)) == NULL)
					err(1, NULL);
				new->e_len[cnt] = MAXPATHLEN;
				break;
			}
		if (!*p) {
			new->e_argv[cnt] = *argv;
			new->e_len[cnt] = 0;
		}
	}
	if (new->flags & F_EXECPLUS) {
		new->e_psize = new->e_pbsize;
		cnt--;
		for (i = 0; i < new->e_pnummax; i++) {
			new->e_argv[cnt] = NULL;
			new->e_len[cnt] = 0;
			cnt++;
		}
		argv = ap;
		goto done;
	}
	new->e_argv[cnt] = new->e_orig[cnt] = NULL;

done:	*argvp = argv + 1;
	return new;
}

/* Finish any pending -exec ... {} + functions. */
void
finish_execplus(void)
{
	PLAN *p;

	p = lastexecplus;
	while (p != NULL) {
		(p->execute)(p, NULL);
		p = p->e_next;
	}
}

#if HAVE_STRUCT_STAT_ST_FLAGS
int
f_flags(PLAN *plan, FTSENT *entry)
{
	u_long flags;

	flags = entry->fts_statp->st_flags;
	if (plan->flags & F_ATLEAST)
		return (flags | plan->fl_flags) == flags &&
		    !(flags & plan->fl_notflags);
	else if (plan->flags & F_ANY)
		return (flags & plan->fl_flags) ||
		    (flags | plan->fl_notflags) != flags;
	else
		return flags == plan->fl_flags &&
		    !(plan->fl_flags & plan->fl_notflags);
}

PLAN *
c_flags(OPTION *option, char ***argvp)
{
	char *flags_str;
	PLAN *new;
	u_long flags, notflags;

	flags_str = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);

	if (*flags_str == '-') {
		new->flags |= F_ATLEAST;
		flags_str++;
	} else if (*flags_str == '+') {
		new->flags |= F_ANY;
		flags_str++;
	}
	if (strtofflags(&flags_str, &flags, &notflags) == 1)
		errx(1, "%s: %s: illegal flags string", option->name, flags_str);

	new->fl_flags = flags;
	new->fl_notflags = notflags;
	return new;
}
#endif

/*
 * -follow functions --
 *
 *	Always true, causes symbolic links to be followed on a global
 *	basis.
 */
PLAN *
c_follow(OPTION *option, char ***argvp __unused)
{
	ftsoptions &= ~FTS_PHYSICAL;
	ftsoptions |= FTS_LOGICAL;

	return palloc(option);
}

#if HAVE_STRUCT_STATFS_F_FSTYPENAME
/*
 * -fstype functions --
 *
 *	True if the file is of a certain type.
 */
int
f_fstype(PLAN *plan, FTSENT *entry)
{
	static dev_t curdev;	/* need a guaranteed illegal dev value */
	static int first = 1;
	struct statfs sb;
	static int val_flags;
	static char fstype[sizeof(sb.f_fstypename)];
	char *p, save[2] = {0,0};

	if ((plan->flags & F_MTMASK) == F_MTUNKNOWN)
		return 0;

	/* Only check when we cross mount point. */
	if (first || curdev != entry->fts_statp->st_dev) {
		curdev = entry->fts_statp->st_dev;

		/*
		 * Statfs follows symlinks; find wants the link's filesystem,
		 * not where it points.
		 */
		if (entry->fts_info == FTS_SL ||
		    entry->fts_info == FTS_SLNONE) {
			if ((p = strrchr(entry->fts_accpath, '/')) != NULL)
				++p;
			else
				p = entry->fts_accpath;
			save[0] = p[0];
			p[0] = '.';
			save[1] = p[1];
			p[1] = '\0';
		} else
			p = NULL;

		if (statfs(entry->fts_accpath, &sb)) {
			if (!ignore_readdir_race || errno != ENOENT) {
				warn("statfs: %s", entry->fts_accpath);
				exitstatus = 1;
			}
			return 0;
		}

		if (p) {
			p[0] = save[0];
			p[1] = save[1];
		}

		first = 0;

		/*
		 * Further tests may need both of these values, so
		 * always copy both of them.
		 */
		val_flags = sb.f_flags;
		strlcpy(fstype, sb.f_fstypename, sizeof(fstype));
	}
	switch (plan->flags & F_MTMASK) {
	case F_MTFLAG:
		return val_flags & plan->mt_data;
	case F_MTTYPE:
		return (strncmp(fstype, plan->c_data, sizeof(fstype)) == 0);
	default:
		abort();
	}
}

PLAN *
c_fstype(OPTION *option, char ***argvp)
{
	char *fsname;
	PLAN *new;

	fsname = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	switch (*fsname) {
	case 'l':
		if (!strcmp(fsname, "local")) {
			new->flags |= F_MTFLAG;
			new->mt_data = MNT_LOCAL;
			return new;
		}
		break;
	case 'r':
		if (!strcmp(fsname, "rdonly")) {
			new->flags |= F_MTFLAG;
			new->mt_data = MNT_RDONLY;
			return new;
		}
		break;
	}

	new->flags |= F_MTTYPE;
	new->c_data = fsname;
	return new;
}
#endif

/*
 * -group gname functions --
 *
 *	True if the file belongs to the group gname.  If gname is numeric and
 *	an equivalent of the getgrnam() function does not return a valid group
 *	name, gname is taken as a group ID.
 */
int
f_group(PLAN *plan, FTSENT *entry)
{
	COMPARE(entry->fts_statp->st_gid, plan->g_data);
}

PLAN *
c_group(OPTION *option, char ***argvp)
{
	char *gname;
	PLAN *new;
	struct group *g;
	gid_t gid;

	gname = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	g = getgrnam(gname);
	if (g == NULL) {
		char* cp = gname;
		if (gname[0] == '-' || gname[0] == '+')
			gname++;
		gid = atoi(gname);
		if (gid == 0 && gname[0] != '0')
			errx(1, "%s: %s: no such group", option->name, gname);
		gid = find_parsenum(new, option->name, cp, NULL);
	} else
		gid = g->gr_gid;

	new->g_data = gid;
	return new;
}

/*
 * -ignore_readdir_race functions --
 *
 *	Always true. Ignore errors which occur if a file or a directory
 *	in a starting point gets deleted between reading the name and calling
 *	stat on it while find is traversing the starting point.
 */

PLAN *
c_ignore_readdir_race(OPTION *option, char ***argvp __unused)
{
	if (strcmp(option->name, "-ignore_readdir_race") == 0)
		ignore_readdir_race = 1;
	else
		ignore_readdir_race = 0;

	return palloc(option);
}

/*
 * -inum n functions --
 *
 *	True if the file has inode # n.
 */
int
f_inum(PLAN *plan, FTSENT *entry)
{
	COMPARE(entry->fts_statp->st_ino, plan->i_data);
}

PLAN *
c_inum(OPTION *option, char ***argvp)
{
	char *inum_str;
	PLAN *new;

	inum_str = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	new->i_data = find_parsenum(new, option->name, inum_str, NULL);
	return new;
}

/*
 * -samefile FN
 *
 *	True if the file has the same inode (eg hard link) FN
 */

/* f_samefile is just f_inum */
PLAN *
c_samefile(OPTION *option, char ***argvp)
{
	char *fn;
	PLAN *new;
	struct stat sb;
	int error;

	fn = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	if (ftsoptions & FTS_PHYSICAL)
		error = lstat(fn, &sb);
	else
		error = stat(fn, &sb);
	if (error != 0)
		err(1, "%s", fn);
	new->i_data = sb.st_ino;
	return new;
}

/*
 * -links n functions --
 *
 *	True if the file has n links.
 */
int
f_links(PLAN *plan, FTSENT *entry)
{
	COMPARE(entry->fts_statp->st_nlink, plan->l_data);
}

PLAN *
c_links(OPTION *option, char ***argvp)
{
	char *nlinks;
	PLAN *new;

	nlinks = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	new->l_data = (nlink_t)find_parsenum(new, option->name, nlinks, NULL);
	return new;
}

/*
 * -ls functions --
 *
 *	Always true - prints the current entry to stdout in "ls" format.
 */
int
f_ls(PLAN *plan __unused, FTSENT *entry)
{
	printlong(entry->fts_path, entry->fts_accpath, entry->fts_statp);
	return 1;
}

PLAN *
c_ls(OPTION *option, char ***argvp __unused)
{
	ftsoptions &= ~FTS_NOSTAT;
	isoutput = 1;

	return palloc(option);
}

/*
 * -name functions --
 *
 *	True if the basename of the filename being examined
 *	matches pattern using Pattern Matching Notation S3.14
 */
int
f_name(PLAN *plan, FTSENT *entry)
{
	char fn[PATH_MAX];
	const char *name;
	ssize_t len;

	if (plan->flags & F_LINK) {
		/*
		 * The below test both avoids obviously useless readlink()
		 * calls and ensures that symlinks with existent target do
		 * not match if symlinks are being followed.
		 * Assumption: fts will stat all symlinks that are to be
		 * followed and will return the stat information.
		 */
		if (entry->fts_info != FTS_NSOK && entry->fts_info != FTS_SL &&
		    entry->fts_info != FTS_SLNONE)
			return 0;
		len = readlink(entry->fts_accpath, fn, sizeof(fn) - 1);
		if (len == -1)
			return 0;
		fn[len] = '\0';
		name = fn;
	} else
		name = entry->fts_name;
	return !fnmatch(plan->c_data, name,
	    plan->flags & F_IGNCASE ? FNM_CASEFOLD : 0);
}

PLAN *
c_name(OPTION *option, char ***argvp)
{
	char *pattern;
	PLAN *new;

	pattern = nextarg(option, argvp);
	new = palloc(option);
	new->c_data = pattern;
	return new;
}

/*
 * -newer file functions --
 *
 *	True if the current file has been modified more recently
 *	then the modification time of the file named by the pathname
 *	file.
 */
int
f_newer(PLAN *plan, FTSENT *entry)
{
	struct timespec ft;

	if (plan->flags & F_TIME_C)
		ft = entry->fts_statp->st_ctim;
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
	else if (plan->flags & F_TIME_A)
		ft = entry->fts_statp->st_atim;
	else if (plan->flags & F_TIME_B)
		ft = entry->fts_statp->st_birthtim;
#endif
	else
		ft = entry->fts_statp->st_mtim;
	return (ft.tv_sec > plan->t_data.tv_sec ||
	    (ft.tv_sec == plan->t_data.tv_sec &&
	    ft.tv_nsec > plan->t_data.tv_nsec));
}

PLAN *
c_newer(OPTION *option, char ***argvp)
{
	char *fn_or_tspec;
	PLAN *new;
	struct stat sb;
	int error;

	fn_or_tspec = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	/* compare against what */
	if (option->flags & F_TIME2_T) {
		new->t_data.tv_sec = get_date(fn_or_tspec);
		if (new->t_data.tv_sec == (time_t) -1)
			errx(1, "Can't parse date/time: %s", fn_or_tspec);
		/* Use the seconds only in the comparison. */
		new->t_data.tv_nsec = 999999999;
	} else {
		if (ftsoptions & FTS_PHYSICAL)
			error = lstat(fn_or_tspec, &sb);
		else
			error = stat(fn_or_tspec, &sb);
		if (error != 0)
			err(1, "%s", fn_or_tspec);
		if (option->flags & F_TIME2_C)
			new->t_data = sb.st_ctim;
		else if (option->flags & F_TIME2_A)
			new->t_data = sb.st_atim;
#if HAVE_STRUCT_STAT_ST_BIRTHTIME
		else if (option->flags & F_TIME2_B)
			new->t_data = sb.st_birthtim;
#endif
		else
			new->t_data = sb.st_mtim;
	}
	return new;
}

/*
 * -nogroup functions --
 *
 *	True if file belongs to a user ID for which the equivalent
 *	of the getgrnam() 9.2.1 [POSIX.1] function returns NULL.
 */
int
f_nogroup(PLAN *plan __unused, FTSENT *entry)
{
	return group_from_gid(entry->fts_statp->st_gid, 1) == NULL;
}

PLAN *
c_nogroup(OPTION *option, char ***argvp __unused)
{
	ftsoptions &= ~FTS_NOSTAT;

	return palloc(option);
}

/*
 * -nouser functions --
 *
 *	True if file belongs to a user ID for which the equivalent
 *	of the getpwuid() 9.2.2 [POSIX.1] function returns NULL.
 */
int
f_nouser(PLAN *plan __unused, FTSENT *entry)
{
	return user_from_uid(entry->fts_statp->st_uid, 1) == NULL;
}

PLAN *
c_nouser(OPTION *option, char ***argvp __unused)
{
	ftsoptions &= ~FTS_NOSTAT;

	return palloc(option);
}

/*
 * -path functions --
 *
 *	True if the path of the filename being examined
 *	matches pattern using Pattern Matching Notation S3.14
 */
int
f_path(PLAN *plan, FTSENT *entry)
{
	return !fnmatch(plan->c_data, entry->fts_path,
	    plan->flags & F_IGNCASE ? FNM_CASEFOLD : 0);
}

/* c_path is the same as c_name */

/*
 * -perm functions --
 *
 *	The mode argument is used to represent file mode bits.  If it starts
 *	with a leading digit, it's treated as an octal mode, otherwise as a
 *	symbolic mode.
 */
int
f_perm(PLAN *plan, FTSENT *entry)
{
	mode_t mode;

	mode = entry->fts_statp->st_mode &
	    (S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO);
	if (plan->flags & F_ATLEAST)
		return (plan->m_data | mode) == mode;
	else if (plan->flags & F_ANY)
		return (mode & plan->m_data);
	else
		return mode == plan->m_data;
	/* NOTREACHED */
}

PLAN *
c_perm(OPTION *option, char ***argvp)
{
	char *perm;
	PLAN *new;
	mode_t *set;

	perm = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);

	if (*perm == '-') {
		new->flags |= F_ATLEAST;
		++perm;
	} else if (*perm == '+') {
		new->flags |= F_ANY;
		++perm;
	}

	if ((set = setmode(perm)) == NULL)
		errx(1, "%s: %s: illegal mode string", option->name, perm);

	new->m_data = getmode(set, 0);
	free(set);
	return new;
}

/*
 * -print functions --
 *
 *	Always true, causes the current pathname to be written to
 *	standard output.
 */
int
f_print(PLAN *plan __unused, FTSENT *entry)
{
	(void)puts(entry->fts_path);
	return 1;
}

PLAN *
c_print(OPTION *option, char ***argvp __unused)
{
	isoutput = 1;

	return palloc(option);
}

/*
 * -print0 functions --
 *
 *	Always true, causes the current pathname to be written to
 *	standard output followed by a NUL character
 */
int
f_print0(PLAN *plan __unused, FTSENT *entry)
{
	fputs(entry->fts_path, stdout);
	fputc('\0', stdout);
	return 1;
}

/* c_print0 is the same as c_print */

/*
 * -prune functions --
 *
 *	Prune a portion of the hierarchy.
 */
int
f_prune(PLAN *plan __unused, FTSENT *entry)
{
	if (fts_set(tree, entry, FTS_SKIP))
		err(1, "%s", entry->fts_path);
	return 1;
}

/* c_prune == c_simple */

/*
 * -regex functions --
 *
 *	True if the whole path of the file matches pattern using
 *	regular expression.
 */
int
f_regex(PLAN *plan, FTSENT *entry)
{
	char *str;
	int len;
	regex_t *pre;
	regmatch_t pmatch;
	int errcode;
	char errbuf[LINE_MAX];
	int matched;

	pre = plan->re_data;
	str = entry->fts_path;
	len = strlen(str);
	matched = 0;

	pmatch.rm_so = 0;
	pmatch.rm_eo = len;

	errcode = regexec(pre, str, 1, &pmatch, REG_STARTEND);

	if (errcode != 0 && errcode != REG_NOMATCH) {
		regerror(errcode, pre, errbuf, sizeof errbuf);
		errx(1, "%s: %s",
		     plan->flags & F_IGNCASE ? "-iregex" : "-regex", errbuf);
	}

	if (errcode == 0 && pmatch.rm_so == 0 && pmatch.rm_eo == len)
		matched = 1;

	return matched;
}

PLAN *
c_regex(OPTION *option, char ***argvp)
{
	PLAN *new;
	char *pattern;
	regex_t *pre;
	int errcode;
	char errbuf[LINE_MAX];

	if ((pre = malloc(sizeof(regex_t))) == NULL)
		err(1, NULL);

	pattern = nextarg(option, argvp);

	if ((errcode = regcomp(pre, pattern,
	    regexp_flags | (option->flags & F_IGNCASE ? REG_ICASE : 0))) != 0) {
		regerror(errcode, pre, errbuf, sizeof errbuf);
		errx(1, "%s: %s: %s",
		     option->flags & F_IGNCASE ? "-iregex" : "-regex",
		     pattern, errbuf);
	}

	new = palloc(option);
	new->re_data = pre;

	return new;
}

/* c_simple covers c_prune, c_openparen, c_closeparen, c_not, c_or, c_true, c_false */

PLAN *
c_simple(OPTION *option, char ***argvp __unused)
{
	return palloc(option);
}

/*
 * -size n[c] functions --
 *
 *	True if the file size in bytes, divided by an implementation defined
 *	value and rounded up to the next integer, is n.  If n is followed by
 *      one of c k M G T P, the size is in bytes, kilobytes,
 *      megabytes, gigabytes, terabytes or petabytes respectively.
 */
#define	FIND_SIZE	512
static int divsize = 1;

int
f_size(PLAN *plan, FTSENT *entry)
{
	off_t size;

	size = divsize ? (entry->fts_statp->st_size + FIND_SIZE - 1) /
	    FIND_SIZE : entry->fts_statp->st_size;
	COMPARE(size, plan->o_data);
}

PLAN *
c_size(OPTION *option, char ***argvp)
{
	char *size_str;
	PLAN *new;
	char endch;
	off_t scale;

	size_str = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	endch = 'c';
	new->o_data = find_parsenum(new, option->name, size_str, &endch);
	if (endch != '\0') {
		divsize = 0;

		switch (endch) {
		case 'c':                       /* characters */
			scale = 0x1LL;
			break;
		case 'k':                       /* kilobytes 1<<10 */
			scale = 0x400LL;
			break;
		case 'M':                       /* megabytes 1<<20 */
			scale = 0x100000LL;
			break;
		case 'G':                       /* gigabytes 1<<30 */
			scale = 0x40000000LL;
			break;
		case 'T':                       /* terabytes 1<<40 */
			scale = 0x10000000000LL;
			break;
		case 'P':                       /* petabytes 1<<50 */
			scale = 0x4000000000000LL;
			break;
		default:
			errx(1, "%s: %s: illegal trailing character",
				option->name, size_str);
			break;
		}
		if (new->o_data > QUAD_MAX / scale)
			errx(1, "%s: %s: value too large",
				option->name, size_str);
		new->o_data *= scale;
	}
	return new;
}

/*
 * -sparse functions --
 *
 *      Check if a file is sparse by finding if it occupies fewer blocks
 *      than we expect based on its size.
 */
int
f_sparse(PLAN *plan __unused, FTSENT *entry)
{
	off_t expected_blocks;

	expected_blocks = (entry->fts_statp->st_size + 511) / 512;
	return entry->fts_statp->st_blocks < expected_blocks;
}

PLAN *
c_sparse(OPTION *option, char ***argvp __unused)
{
	ftsoptions &= ~FTS_NOSTAT;

	return palloc(option);
}

/*
 * -type c functions --
 *
 *	True if the type of the file is c, where c is b, c, d, p, f or w
 *	for block special file, character special file, directory, FIFO,
 *	regular file or whiteout respectively.
 */
int
f_type(PLAN *plan, FTSENT *entry)
{
	if (plan->m_data == S_IFDIR)
		return (entry->fts_info == FTS_D || entry->fts_info == FTS_DC ||
		    entry->fts_info == FTS_DNR || entry->fts_info == FTS_DOT ||
		    entry->fts_info == FTS_DP);
	else
		return (entry->fts_statp->st_mode & S_IFMT) == plan->m_data;
}

PLAN *
c_type(OPTION *option, char ***argvp)
{
	char *typestring;
	PLAN *new;
	mode_t  mask;

	typestring = nextarg(option, argvp);
	if (typestring[0] != 'd')
		ftsoptions &= ~FTS_NOSTAT;

	switch (typestring[0]) {
	case 'b':
		mask = S_IFBLK;
		break;
	case 'c':
		mask = S_IFCHR;
		break;
	case 'd':
		mask = S_IFDIR;
		break;
	case 'f':
		mask = S_IFREG;
		break;
	case 'l':
		mask = S_IFLNK;
		break;
	case 'p':
		mask = S_IFIFO;
		break;
	case 's':
		mask = S_IFSOCK;
		break;
#if defined(FTS_WHITEOUT) && defined(S_IFWHT)
	case 'w':
		mask = S_IFWHT;
		ftsoptions |= FTS_WHITEOUT;
		break;
#endif /* FTS_WHITEOUT */
	default:
		errx(1, "%s: %s: unknown type", option->name, typestring);
	}

	new = palloc(option);
	new->m_data = mask;
	return new;
}

/*
 * -user uname functions --
 *
 *	True if the file belongs to the user uname.  If uname is numeric and
 *	an equivalent of the getpwnam() S9.2.2 [POSIX.1] function does not
 *	return a valid user name, uname is taken as a user ID.
 */
int
f_user(PLAN *plan, FTSENT *entry)
{
	COMPARE(entry->fts_statp->st_uid, plan->u_data);
}

PLAN *
c_user(OPTION *option, char ***argvp)
{
	char *username;
	PLAN *new;
	struct passwd *p;
	uid_t uid;

	username = nextarg(option, argvp);
	ftsoptions &= ~FTS_NOSTAT;

	new = palloc(option);
	p = getpwnam(username);
	if (p == NULL) {
		char* cp = username;
		if( username[0] == '-' || username[0] == '+' )
			username++;
		uid = atoi(username);
		if (uid == 0 && username[0] != '0')
			errx(1, "%s: %s: no such user", option->name, username);
		uid = find_parsenum(new, option->name, cp, NULL);
	} else
		uid = p->pw_uid;

	new->u_data = uid;
	return new;
}

/*
 * -xdev functions --
 *
 *	Always true, causes find not to descend past directories that have a
 *	different device ID (st_dev, see stat() S5.6.2 [POSIX.1])
 */
PLAN *
c_xdev(OPTION *option, char ***argvp __unused)
{
	ftsoptions |= FTS_XDEV;

	return palloc(option);
}

/*
 * ( expression ) functions --
 *
 *	True if expression is true.
 */
int
f_expr(PLAN *plan, FTSENT *entry)
{
	PLAN *p;
	int state = 0;

	for (p = plan->p_data[0];
	    p && (state = (p->execute)(p, entry)); p = p->next);
	return state;
}

/*
 * f_openparen and f_closeparen nodes are temporary place markers.  They are
 * eliminated during phase 2 of find_formplan() --- the '(' node is converted
 * to a f_expr node containing the expression and the ')' node is discarded.
 * The functions themselves are only used as constants.
 */

int
f_openparen(PLAN *plan __unused, FTSENT *entry __unused)
{
	abort();
}

int
f_closeparen(PLAN *plan __unused, FTSENT *entry __unused)
{
	abort();
}

/* c_openparen == c_simple */
/* c_closeparen == c_simple */

/*
 * AND operator. Since AND is implicit, no node is allocated.
 */
PLAN *
c_and(OPTION *option __unused, char ***argvp __unused)
{
	return NULL;
}

/*
 * ! expression functions --
 *
 *	Negation of a primary; the unary NOT operator.
 */
int
f_not(PLAN *plan, FTSENT *entry)
{
	PLAN *p;
	int state = 0;

	for (p = plan->p_data[0];
	    p && (state = (p->execute)(p, entry)); p = p->next);
	return !state;
}

/* c_not == c_simple */

/*
 * expression -o expression functions --
 *
 *	Alternation of primaries; the OR operator.  The second expression is
 * not evaluated if the first expression is true.
 */
int
f_or(PLAN *plan, FTSENT *entry)
{
	PLAN *p;
	int state = 0;

	for (p = plan->p_data[0];
	    p && (state = (p->execute)(p, entry)); p = p->next);

	if (state)
		return 1;

	for (p = plan->p_data[1];
	    p && (state = (p->execute)(p, entry)); p = p->next);
	return state;
}

/* c_or == c_simple */

/*
 * -false
 *
 *	Always false.
 */
int
f_false(PLAN *plan __unused, FTSENT *entry __unused)
{
	return 0;
}

/* c_false == c_simple */

/*
 * -quit
 *
 *	Exits the program
 */
int
f_quit(PLAN *plan __unused, FTSENT *entry __unused)
{
	finish_execplus();
	exit(exitstatus);
}

/* c_quit == c_simple */
