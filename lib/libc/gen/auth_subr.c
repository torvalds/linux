/*	$OpenBSD: auth_subr.c,v 1.56 2020/10/13 04:42:28 guenther Exp $	*/

/*
 * Copyright (c) 2000-2002,2004 Todd C. Miller <millert@openbsd.org>
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
 * Copyright (c) 1995,1996,1997 Berkeley Software Design, Inc.
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
 *	BSDI $From: auth_subr.c,v 2.4 1999/09/08 04:10:40 prb Exp $
 */

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <login_cap.h>

#define	MAXSPOOLSIZE	(8*1024)	/* Spool up to 8K of back info */

struct rmfiles {
	struct rmfiles	*next;
	char		*file;
};

struct authopts {
	struct authopts	*next;
	char		*opt;
};

struct authdata {
	struct	authdata *next;
	void	*ptr;
	size_t	len;
};

struct auth_session_t {
	char	*name;			/* name of use being authenticated */
	char	*style;			/* style of authentication used */
	char	*class;			/* class of user */
	char	*service;		/* type of service being performed */
	char	*challenge;		/* last challenge issued */
	int	flags;			/* see below */
	struct	passwd *pwd;		/* password entry for user */
	struct	timeval now;		/* time of authentication */

	int	state;			/* authenticated state */

	struct	rmfiles *rmlist;	/* list of files to remove on failure */
	struct	authopts *optlist;	/* list of options to scripts */ 
	struct	authdata *data;		/* additional data to send to scripts */

	char	spool[MAXSPOOLSIZE];	/* data returned from login script */
	int	index;			/* how much returned thus far */

	int	fd;			/* connection to authenticator */

	va_list	ap0;			/* argument list to auth_call */
	va_list	ap;			/* additional arguments to auth_call */
};

/*
 * Internal flags
 */
#define	AF_INTERACTIVE		0x0001	/* This is an interactive session */

/*
 * We cannot include bsd_auth.h until we define the above structures
 */
#include <bsd_auth.h>

/*
 * Internally used functions
 */
static void _add_rmlist(auth_session_t *, char *);
static void _auth_spool(auth_session_t *, int);
static void _recv_fd(auth_session_t *, int);
static char *_auth_next_arg(auth_session_t *);
/*
 * Set up a known environment for all authentication scripts.
 */
static char * const auth_environ[] = {
	"PATH=" _PATH_DEFPATH,
	"SHELL=" _PATH_BSHELL,
	NULL,
};

static char defservice[] = LOGIN_DEFSERVICE;

static va_list nilap;

/*
 * Quick one liners that only exist to keep auth_session_t opaque
 */
void	auth_setstate(auth_session_t *as, int s){ as->state = s; }
void	auth_set_va_list(auth_session_t *as, va_list ap) { va_copy(as->ap, ap); }
int	auth_getstate(auth_session_t *as)	{ return (as->state); }
struct passwd *auth_getpwd(auth_session_t *as)	{ return (as->pwd); }
DEF_WEAK(auth_setstate);
DEF_WEAK(auth_set_va_list);
DEF_WEAK(auth_getstate);
DEF_WEAK(auth_getpwd);

/*
 * Open a new BSD Authentication session with the default service
 * (which can be changed later).
 */
auth_session_t *
auth_open(void)
{
	auth_session_t *as;

	if ((as = calloc(1, sizeof(auth_session_t))) != NULL) {
		as->service = defservice;
		as->fd = -1;
	}

	return (as);
}
DEF_WEAK(auth_open);

/*
 * Clean the specified BSD Authentication session.
 */
void
auth_clean(auth_session_t *as)
{
	struct rmfiles *rm;
	struct authdata *data;

	as->state = 0;

	auth_clrenv(as);

	/*
	 * Clean out the rmlist and remove specified files
	 */
	while ((rm = as->rmlist) != NULL) {
		as->rmlist = rm->next;
		unlink(rm->file);
		free(rm);
	}

	/*
	 * Clean out data
	 */
	while ((data = as->data) != NULL) {
		if (as->data->len)
			explicit_bzero(as->data->ptr, as->data->len);
		as->data = data->next;
		free(data);
	}

	auth_setitem(as, AUTHV_ALL, NULL);

	if (as->pwd != NULL) {
		explicit_bzero(as->pwd->pw_passwd, strlen(as->pwd->pw_passwd));
		free(as->pwd);
		as->pwd = NULL;
	}

	if (as->fd != -1) {
		close(as->fd);
		as->fd = -1;
	}
}
DEF_WEAK(auth_clean);

/*
 * Close the specified BSD Authentication session.
 * Return 0 if not authenticated.
 */
int
auth_close(auth_session_t *as)
{
	struct rmfiles *rm;
	struct authopts *opt;
	struct authdata *data;
	int s;

	/*
	 * Save our return value
	 */
	s = as->state & AUTH_ALLOW;

	if (s == 0)
		as->index = 0;

	auth_setenv(as);


	/*
	 * Clean out the rmlist and remove specified files if the
	 * authentication failed
	 */
	while ((rm = as->rmlist) != NULL) {
		as->rmlist = rm->next;
		if (s == 0)
			unlink(rm->file);
		free(rm);
	}

	/*
	 * Clean out the opt list
	 */
	while ((opt = as->optlist) != NULL) {
		as->optlist = opt->next;
		free(opt);
	}

	/*
	 * Clean out data
	 */
	while ((data = as->data) != NULL) {
		if (as->data->len)
			explicit_bzero(as->data->ptr, as->data->len);
		as->data = data->next;
		free(data);
	}

	if (as->pwd != NULL) {
		explicit_bzero(as->pwd->pw_passwd, strlen(as->pwd->pw_passwd));
		free(as->pwd);
		as->pwd = NULL;
	}

	/*
	 * Clean up random variables
	 */
	if (as->service && as->service != defservice)
		free(as->service);
	free(as->challenge);
	free(as->class);
	free(as->style);
	free(as->name);

	free(as);
	return (s);
}
DEF_WEAK(auth_close);

/*
 * Request a challenge for the session.
 * The name and style must have already been specified
 */
char *
auth_challenge(auth_session_t *as)
{
	char path[PATH_MAX];
	int len;

	if (as == NULL || as->style == NULL || as->name == NULL ||
	    !_auth_validuser(as->name))
		return (NULL);

	len = snprintf(path, sizeof(path), _PATH_AUTHPROG "%s", as->style);
	if (len < 0 || len >= sizeof(path))
		return (NULL);

	as->state = 0;

	free(as->challenge);
	as->challenge = NULL;

	auth_call(as, path, as->style, "-s", "challenge", "--", as->name,
	    as->class, (char *)NULL);
	if (as->state & AUTH_CHALLENGE)
		as->challenge = auth_getvalue(as, "challenge");
	as->state = 0;
	as->index = 0;	/* toss our data */
	return (as->challenge);
}
DEF_WEAK(auth_challenge);

/*
 * Set/unset the requested environment variables.
 * Mark the variables as set so they will not be set a second time.
 * XXX - should provide a way to detect setenv() failure.
 */
void
auth_setenv(auth_session_t *as)
{
	char *line, *name;

	/*
	 * Set any environment variables we were asked for
	 */
    	for (line = as->spool; line < as->spool + as->index;) {
		if (!strncasecmp(line, BI_SETENV, sizeof(BI_SETENV)-1)) {
			if (isblank((unsigned char)line[sizeof(BI_SETENV) - 1])) {
				/* only do it once! */
				line[0] = 'd'; line[1] = 'i'; line[2] = 'd';
				line += sizeof(BI_SETENV) - 1;
				for (name = line;
				    isblank((unsigned char)*name); ++name)
					;
				for (line = name;
				    *line && !isblank((unsigned char)*line);
				    ++line)
					;
				if (*line)
					*line++ = '\0';
				for (; isblank((unsigned char)*line); ++line)
					;
				if (*line != '\0' && setenv(name, line, 1))
					warn("setenv(%s, %s)", name, line);
			}
		} else
		if (!strncasecmp(line, BI_UNSETENV, sizeof(BI_UNSETENV)-1)) {
			if (isblank((unsigned char)line[sizeof(BI_UNSETENV) - 1])) {
				/* only do it once! */
				line[2] = 'd'; line[3] = 'i'; line[4] = 'd';
				line += sizeof(BI_UNSETENV) - 1;
				for (name = line;
				    isblank((unsigned char)*name); ++name)
					;
				for (line = name;
				    *line && !isblank((unsigned char)*line);
				    ++line)
					;
				if (*line)
					*line++ = '\0';
				unsetenv(name);
			}
		}
		while (*line++)
			;
	}
}
DEF_WEAK(auth_setenv);

/*
 * Clear out any requested environment variables.
 */
void
auth_clrenv(auth_session_t *as)
{
	char *line;

	for (line = as->spool; line < as->spool + as->index;) {
		if (!strncasecmp(line, BI_SETENV, sizeof(BI_SETENV)-1)) {
			if (isblank((unsigned char)line[sizeof(BI_SETENV) - 1])) {
				line[0] = 'i'; line[1] = 'g'; line[2] = 'n';
			}
		} else
		if (!strncasecmp(line, BI_UNSETENV, sizeof(BI_UNSETENV)-1)) {
			if (isblank((unsigned char)line[sizeof(BI_UNSETENV) - 1])) {
				line[2] = 'i'; line[3] = 'g'; line[4] = 'n';
			}
		}
		while (*line++)
			;
	}
}
DEF_WEAK(auth_clrenv);

char *
auth_getitem(auth_session_t *as, auth_item_t item)
{
	if (as != NULL) {
		switch (item) {
		case AUTHV_CHALLENGE:
			return (as->challenge);
		case AUTHV_CLASS:
			return (as->class);
		case AUTHV_NAME:
			return (as->name);
		case AUTHV_SERVICE:
			return (as->service ? as->service : defservice);
		case AUTHV_STYLE:
			return (as->style);
		case AUTHV_INTERACTIVE:
			return ((as->flags & AF_INTERACTIVE) ? "True" : NULL);
		default:
			break;
		}
	}
	return (NULL);
}
DEF_WEAK(auth_getitem);

int
auth_setitem(auth_session_t *as, auth_item_t item, char *value)
{
	if (as == NULL) {
		errno = EINVAL;
		return (-1);
	}

	switch (item) {
	case AUTHV_ALL:
		if (value != NULL) {
			errno = EINVAL;
			return (-1);
		}
		auth_setitem(as, AUTHV_CHALLENGE, NULL);
		auth_setitem(as, AUTHV_CLASS, NULL);
		auth_setitem(as, AUTHV_NAME, NULL);
		auth_setitem(as, AUTHV_SERVICE, NULL);
		auth_setitem(as, AUTHV_STYLE, NULL);
		auth_setitem(as, AUTHV_INTERACTIVE, NULL);
		return (0);

	case AUTHV_CHALLENGE:
		if (value == as->challenge)
			return (0);
		if (value != NULL && (value = strdup(value)) == NULL)
			return (-1);
		free(as->challenge);
		as->challenge = value;
		return (0);

	case AUTHV_CLASS:
		if (value == as->class)
			return (0);
		if (value != NULL && (value = strdup(value)) == NULL)
			return (-1);
		free(as->class);
		as->class = value;
		return (0);

	case AUTHV_NAME:
		if (value == as->name)
			return (0);
		if (value != NULL && !_auth_validuser(value)) {
			errno = EINVAL;
			return (-1);
		}
		if (value != NULL && (value = strdup(value)) == NULL)
			return (-1);
		free(as->name);
		as->name = value;
		return (0);

	case AUTHV_SERVICE:
		if (value == as->service)
			return (0);
		if (value == NULL || strcmp(value, defservice) == 0)
			value = defservice;
		else if ((value = strdup(value)) == NULL)
			return (-1);
		if (as->service && as->service != defservice)
			free(as->service);
		as->service = value;
		return (0);

	case AUTHV_STYLE:
		if (value == as->style)
			return (0);
		if (value == NULL || strchr(value, '/') != NULL ||
		    (value = strdup(value)) == NULL)
			return (-1);
		free(as->style);
		as->style = value;
		return (0);

	case AUTHV_INTERACTIVE:
		if (value == NULL)
			as->flags &= ~AF_INTERACTIVE;
		else
			as->flags |= ~AF_INTERACTIVE;
		return (0);

	default:
		errno = EINVAL;
		return (-1);
	}
}
DEF_WEAK(auth_setitem);

int
auth_setoption(auth_session_t *as, char *n, char *v)
{
	struct authopts *opt;
	size_t len = strlen(n) + strlen(v) + 2;
	int ret;

	if ((opt = malloc(sizeof(*opt) + len)) == NULL)
		return (-1);

	opt->opt = (char *)(opt + 1);

	ret = snprintf(opt->opt, len, "%s=%s", n, v);
	if (ret < 0 || ret >= len) {
		free(opt);
		errno = ENAMETOOLONG;
		return (-1);
	}
	opt->next = as->optlist;
	as->optlist = opt;
	return(0);
}
DEF_WEAK(auth_setoption);

void
auth_clroptions(auth_session_t *as)
{
	struct authopts *opt;

	while ((opt = as->optlist) != NULL) {
		as->optlist = opt->next;
		free(opt);
	}
}
DEF_WEAK(auth_clroptions);

void
auth_clroption(auth_session_t *as, char *option)
{
	struct authopts *opt, *oopt;
	size_t len;

	len = strlen(option);

	if ((opt = as->optlist) == NULL)
		return;

	if (strncmp(opt->opt, option, len) == 0 &&
	    (opt->opt[len] == '=' || opt->opt[len] == '\0')) {
		as->optlist = opt->next;
		free(opt);
		return;
	}

	while ((oopt = opt->next) != NULL) {
		if (strncmp(oopt->opt, option, len) == 0 &&
		    (oopt->opt[len] == '=' || oopt->opt[len] == '\0')) {
			opt->next = oopt->next;
			free(oopt);
			return;
		}
		opt = oopt;
	}
}
DEF_WEAK(auth_clroption);

int
auth_setdata(auth_session_t *as, void *ptr, size_t len)
{
	struct authdata *data, *dp;

	if (len <= 0)
		return (0);

	if ((data = malloc(sizeof(*data) + len)) == NULL)
		return (-1);

	data->next = NULL;
	data->len = len;
	data->ptr = data + 1;
	memcpy(data->ptr, ptr, len);

	if (as->data == NULL)
		as->data = data;
	else {
		for (dp = as->data; dp->next != NULL; dp = dp->next)
			;
		dp->next = data;
	}
	return (0);
}
DEF_WEAK(auth_setdata);

int
auth_setpwd(auth_session_t *as, struct passwd *pwd)
{
	struct passwd pwstore;
	char *instance, pwbuf[_PW_BUF_LEN];

	if (pwd == NULL && as->pwd == NULL && as->name == NULL)
		return (-1);		/* true failure */

	if (pwd == NULL) {
		/*
		 * If we were not passed in a pwd structure we need to
		 * go find one for ourself.  Always look up the username
		 * (if it is defined) in the passwd database to see if there
		 * is an entry for the user.  If not, either use the current
		 * entry or simply return a 1 which implies there is
		 * no user by that name here.  This is not a failure, just
		 * a point of information.
		 */
		if (as->name == NULL)
			return (0);
		getpwnam_r(as->name, &pwstore, pwbuf, sizeof(pwbuf), &pwd);
		if (pwd == NULL) {
			instance = strchr(as->name, '/');
			if (instance == NULL)
				return (as->pwd ? 0 : 1);
			if (strcmp(instance, "/root") == 0) {
				getpwnam_r(instance + 1, &pwstore, pwbuf,
				    sizeof(pwbuf), &pwd);
			}
			if (pwd == NULL)
				return (as->pwd ? 0 : 1);
		}
	}
	if ((pwd = pw_dup(pwd)) == NULL)
		return (-1);		/* true failure */
	if (as->pwd) {
		explicit_bzero(as->pwd->pw_passwd, strlen(as->pwd->pw_passwd));
		free(as->pwd);
	}
	as->pwd = pwd;
	return (0);
}
DEF_WEAK(auth_setpwd);

char *
auth_getvalue(auth_session_t *as, char *what)
{
	char *line, *v, *value;
	int n, len;

	len = strlen(what);

    	for (line = as->spool; line < as->spool + as->index;) {
		if (strncasecmp(line, BI_VALUE, sizeof(BI_VALUE)-1) != 0)
			goto next;
		line += sizeof(BI_VALUE) - 1;

		if (!isblank((unsigned char)*line))
			goto next;

		while (isblank((unsigned char)*++line))
			;

		if (strncmp(line, what, len) != 0 ||
		    !isblank((unsigned char)line[len]))
			goto next;
		line += len;
		while (isblank((unsigned char)*++line))
			;
		value = strdup(line);
		if (value == NULL)
			return (NULL);

		/*
		 * XXX - There should be a more standardized
		 * routine for doing this sort of thing.
		 */
		for (line = v = value; *line; ++line) {
			if (*line == '\\') {
				switch (*++line) {
				case 'r':
					*v++ = '\r';
					break;
				case 'n':
					*v++ = '\n';
					break;
				case 't':
					*v++ = '\t';
					break;
				case '0': case '1': case '2':
				case '3': case '4': case '5':
				case '6': case '7':
					n = *line - '0';
					if (isdigit((unsigned char)line[1])) {
						++line;
						n <<= 3;
						n |= *line-'0';
					}
					if (isdigit((unsigned char)line[1])) {
						++line;
						n <<= 3;
						n |= *line-'0';
					}
					break;
				default:
					*v++ = *line;
					break;
				}
			} else
				*v++ = *line;
		}
		*v = '\0';
		return (value);
next:
		while (*line++)
			;
	}
	return (NULL);
}
DEF_WEAK(auth_getvalue);

quad_t
auth_check_expire(auth_session_t *as)
{
	if (as->pwd == NULL && auth_setpwd(as, NULL) < 0) {
		as->state &= ~AUTH_ALLOW;
		as->state |= AUTH_EXPIRED;	/* XXX */
		return (-1);
	}

	if (as->pwd == NULL)
		return (0);

	if (as->pwd && (quad_t)as->pwd->pw_expire != 0) {
		if (as->now.tv_sec == 0)
			WRAP(gettimeofday)(&as->now, NULL);
		if ((quad_t)as->now.tv_sec >= (quad_t)as->pwd->pw_expire) {
			as->state &= ~AUTH_ALLOW;
			as->state |= AUTH_EXPIRED;
		}
		if ((quad_t)as->now.tv_sec == (quad_t)as->pwd->pw_expire)
			return (-1);
		return ((quad_t)as->pwd->pw_expire - (quad_t)as->now.tv_sec);
	}
	return (0);
}
DEF_WEAK(auth_check_expire);

quad_t
auth_check_change(auth_session_t *as)
{
	if (as->pwd == NULL && auth_setpwd(as, NULL) < 0) {
		as->state &= ~AUTH_ALLOW;
		as->state |= AUTH_PWEXPIRED;	/* XXX */
		return (-1);
	}

	if (as->pwd == NULL)
		return (0);

	if (as->pwd && (quad_t)as->pwd->pw_change) {
		if (as->now.tv_sec == 0)
			WRAP(gettimeofday)(&as->now, NULL);
		if (as->now.tv_sec >= (quad_t)as->pwd->pw_change) {
			as->state &= ~AUTH_ALLOW;
			as->state |= AUTH_PWEXPIRED;
		}
		if ((quad_t)as->now.tv_sec == (quad_t)as->pwd->pw_change)
			return (-1);
		return ((quad_t)as->pwd->pw_change - (quad_t)as->now.tv_sec);
	}
	return (0);
}
DEF_WEAK(auth_check_change);

/*
 * The down and dirty call to the login script
 * okay contains the default return value, typically 0 but
 * is AUTH_OKAY for approval like scripts.
 *
 * Internally additional trailing arguments can be read from as->ap
 * Options will be placed just after the first argument (not including path).
 *
 * Any data will be sent to (and freed by) the script
 */
int
auth_call(auth_session_t *as, char *path, ...)
{
	char *line;
	struct authdata *data;
	struct authopts *opt;
	pid_t pid;
	int status;
	int okay;
	int pfd[2];
	int argc;
	char *argv[64];		/* 64 args should be more than enough */
#define	Nargc	(sizeof(argv)/sizeof(argv[0]))

	va_start(as->ap0, path);

	argc = 0;
	if ((argv[argc] = _auth_next_arg(as)) != NULL)
		++argc;

	if (as->fd != -1) {
		argv[argc++] = "-v";
		argv[argc++] = "fd=4";		/* AUTH_FD, see below */
	}
	/* XXX - fail if out of space in argv */
	for (opt = as->optlist; opt != NULL; opt = opt->next) {
		if (argc < Nargc - 2) {
			argv[argc++] = "-v";
			argv[argc++] = opt->opt;
		} else {
			syslog(LOG_ERR, "too many authentication options");
			goto fail;
		}
	}
	while (argc < Nargc - 1 && (argv[argc] = _auth_next_arg(as)))
		++argc;

	if (argc >= Nargc - 1 && _auth_next_arg(as)) {
		if (memcmp(&nilap, &(as->ap0), sizeof(nilap)) != 0) {
			va_end(as->ap0);
			explicit_bzero(&(as->ap0), sizeof(as->ap0));
		}
		if (memcmp(&nilap, &(as->ap), sizeof(nilap)) != 0) {
			va_end(as->ap);
			explicit_bzero(&(as->ap), sizeof(as->ap));
		}
		syslog(LOG_ERR, "too many arguments");
		goto fail;
	}

	argv[argc] = NULL;

	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, pfd) == -1) {
		syslog(LOG_ERR, "unable to create backchannel %m");
		warnx("internal resource failure");
		goto fail;
	}

	switch (pid = fork()) {
	case -1:
		syslog(LOG_ERR, "%s: %m", path);
		warnx("internal resource failure");
		close(pfd[0]);
		close(pfd[1]);
		goto fail;
	case 0:
#define	COMM_FD	3
#define	AUTH_FD	4
		if (dup2(pfd[1], COMM_FD) == -1)
			err(1, "dup of backchannel");
		if (as->fd != -1) {
			if (dup2(as->fd, AUTH_FD) == -1)
				err(1, "dup of auth fd");
			closefrom(AUTH_FD + 1);
		} else
			closefrom(COMM_FD + 1);
		execve(path, argv, auth_environ);
		syslog(LOG_ERR, "%s: %m", path);
		err(1, "%s", path);
	default:
		close(pfd[1]);
		if (as->fd != -1) {
			close(as->fd);		/* so child has only ref */
			as->fd = -1;
		}
		while ((data = as->data) != NULL) {
			as->data = data->next;
			if (data->len > 0) {
				write(pfd[0], data->ptr, data->len);
				explicit_bzero(data->ptr, data->len);
			}
			free(data);
		}
		as->index = 0;
		_auth_spool(as, pfd[0]);
		close(pfd[0]);
		do {
			if (waitpid(pid, &status, 0) != -1) {
				if (!WIFEXITED(status))
					goto fail;
				break;
			}
			/*
			 * could get ECHILD if it was waited for by
			 * another thread or from a signal handler
			 */
		} while (errno == EINTR);
	}

	/*
	 * Now scan the spooled data
	 * It is easier to wait for all the data before starting
	 * to scan it.
	 */
    	for (line = as->spool; line < as->spool + as->index;) {
		if (!strncasecmp(line, BI_REJECT, sizeof(BI_REJECT)-1)) {
			line += sizeof(BI_REJECT) - 1;
			if (!*line || *line == ' ' || *line == '\t') {
				while (*line == ' ' || *line == '\t')
					++line;
				if (!strcasecmp(line, "silent")) {
					as->state = AUTH_SILENT;
					break;
				}
				if (!strcasecmp(line, "challenge")) {
					as->state  = AUTH_CHALLENGE;
					break;
				}
				if (!strcasecmp(line, "expired")) {
					as->state  = AUTH_EXPIRED;
					break;
				}
				if (!strcasecmp(line, "pwexpired")) {
					as->state  = AUTH_PWEXPIRED;
					break;
				}
			}
			break;
		} else if (!strncasecmp(line, BI_AUTH, sizeof(BI_AUTH)-1)) {
			line += sizeof(BI_AUTH) - 1;
			if (!*line || *line == ' ' || *line == '\t') {
				while (*line == ' ' || *line == '\t')
					++line;
				if (*line == '\0')
					as->state |= AUTH_OKAY;
				else if (!strcasecmp(line, "root"))
					as->state |= AUTH_ROOTOKAY;
				else if (!strcasecmp(line, "secure"))
					as->state |= AUTH_SECURE;
			}
		} else if (!strncasecmp(line, BI_REMOVE, sizeof(BI_REMOVE)-1)) {
			line += sizeof(BI_REMOVE) - 1;
			while (*line == ' ' || *line == '\t')
				++line;
			if (*line)
				_add_rmlist(as, line);
		}
		while (*line++)
			;
	}

	if (WEXITSTATUS(status))
		as->state &= ~AUTH_ALLOW;

	okay = as->state & AUTH_ALLOW;

	if (!okay)
		auth_clrenv(as);

	if (0) {
fail:
		auth_clrenv(as);
		as->state = 0;
		okay = -1;
	}

	while ((data = as->data) != NULL) {
		as->data = data->next;
		free(data);
	}

	if (memcmp(&nilap, &(as->ap0), sizeof(nilap)) != 0) {
		va_end(as->ap0);
		explicit_bzero(&(as->ap0), sizeof(as->ap0));
	}

	if (memcmp(&nilap, &(as->ap), sizeof(nilap)) != 0) {
		va_end(as->ap);
		explicit_bzero(&(as->ap), sizeof(as->ap));
	}
	return (okay);
}
DEF_WEAK(auth_call);

static void
_recv_fd(auth_session_t *as, int fd)
{
	struct msghdr msg;
	struct cmsghdr *cmp;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);
	if (recvmsg(fd, &msg, 0) == -1)
		syslog(LOG_ERR, "recvmsg: %m");
	else if (msg.msg_flags & MSG_TRUNC)
		syslog(LOG_ERR, "message truncated");
	else if (msg.msg_flags & MSG_CTRUNC)
		syslog(LOG_ERR, "control message truncated");
	else if ((cmp = CMSG_FIRSTHDR(&msg)) == NULL)
		syslog(LOG_ERR, "missing control message");
	else {
		if (cmp->cmsg_level != SOL_SOCKET)
			syslog(LOG_ERR, "unexpected cmsg_level %d",
			    cmp->cmsg_level);
		else if (cmp->cmsg_type != SCM_RIGHTS)
			syslog(LOG_ERR, "unexpected cmsg_type %d",
			    cmp->cmsg_type);
		else if (cmp->cmsg_len != CMSG_LEN(sizeof(int)))
			syslog(LOG_ERR, "bad cmsg_len %d",
			    cmp->cmsg_len);
		else {
			if (as->fd != -1)
				close(as->fd);
			as->fd = *(int *)CMSG_DATA(cmp);
		}
	}
}

static void
_auth_spool(auth_session_t *as, int fd)
{
	ssize_t r;
	char *b, *s;

	for (s = as->spool + as->index; as->index < sizeof(as->spool) - 1; ) {
		r = read(fd, as->spool + as->index,
		    sizeof(as->spool) - as->index);
		if (r <= 0) {
			as->spool[as->index] = '\0';
			return;
		}
		b = as->spool + as->index;
		as->index += r;
		/*
		 * Convert newlines into NULs to allow easy scanning of the
		 * file and receive an fd if there is a BI_FDPASS message.
		 * XXX - checking for BI_FDPASS here is annoying but
		 *       we need to avoid the read() slurping in control data.
		 */
		while (r-- > 0) {
			if (*b++ == '\n') {
				b[-1] = '\0';
				if (strcasecmp(s, BI_FDPASS) == 0)
					_recv_fd(as, fd);
				s = b;
			}
		}
	}

	syslog(LOG_ERR, "Overflowed backchannel spool buffer");
	errx(1, "System error in authentication program");
}

static void
_add_rmlist(auth_session_t *as, char *file)
{
	struct rmfiles *rm;
	size_t i = strlen(file) + 1;

	// XXX should rangecheck i since we are about to add?

	if ((rm = malloc(sizeof(struct rmfiles) + i)) == NULL) {
		syslog(LOG_ERR, "Failed to allocate rmfiles: %m");
		return;
	}
	rm->file = (char *)(rm + 1);
	rm->next = as->rmlist;
	strlcpy(rm->file, file, i);
	as->rmlist = rm;
}

static char *
_auth_next_arg(auth_session_t *as)
{
	char *arg;

	if (memcmp(&nilap, &(as->ap0), sizeof(nilap)) != 0) {
		if ((arg = va_arg(as->ap0, char *)) != NULL)
			return (arg);
		va_end(as->ap0);
		explicit_bzero(&(as->ap0), sizeof(as->ap0));
	}
	if (memcmp(&nilap, &(as->ap), sizeof(nilap)) != 0) {
		if ((arg = va_arg(as->ap, char *)) != NULL)
			return (arg);
		va_end(as->ap);
		explicit_bzero(&(as->ap), sizeof(as->ap));
	}
	return (NULL);
}
