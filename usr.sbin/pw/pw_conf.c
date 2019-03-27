/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/types.h>
#include <sys/sbuf.h>

#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "pw.h"

#define debugging 0

enum {
	_UC_NONE,
	_UC_DEFAULTPWD,
	_UC_REUSEUID,
	_UC_REUSEGID,
	_UC_NISPASSWD,
	_UC_DOTDIR,
	_UC_NEWMAIL,
	_UC_LOGFILE,
	_UC_HOMEROOT,
	_UC_HOMEMODE,
	_UC_SHELLPATH,
	_UC_SHELLS,
	_UC_DEFAULTSHELL,
	_UC_DEFAULTGROUP,
	_UC_EXTRAGROUPS,
	_UC_DEFAULTCLASS,
	_UC_MINUID,
	_UC_MAXUID,
	_UC_MINGID,
	_UC_MAXGID,
	_UC_EXPIRE,
	_UC_PASSWORD,
	_UC_FIELDS
};

static char     bourne_shell[] = "sh";

static char    *system_shells[_UC_MAXSHELLS] =
{
	bourne_shell,
	"csh",
	"tcsh"
};

static char const *booltrue[] =
{
	"yes", "true", "1", "on", NULL
};
static char const *boolfalse[] =
{
	"no", "false", "0", "off", NULL
};

static struct userconf config =
{
	0,			/* Default password for new users? (nologin) */
	0,			/* Reuse uids? */
	0,			/* Reuse gids? */
	NULL,			/* NIS version of the passwd file */
	"/usr/share/skel",	/* Where to obtain skeleton files */
	NULL,			/* Mail to send to new accounts */
	"/var/log/userlog",	/* Where to log changes */
	"/home",		/* Where to create home directory */
	_DEF_DIRMODE,		/* Home directory perms, modified by umask */
	"/bin",			/* Where shells are located */
	system_shells,		/* List of shells (first is default) */
	bourne_shell,		/* Default shell */
	NULL,			/* Default group name */
	NULL,			/* Default (additional) groups */
	NULL,			/* Default login class */
	1000, 32000,		/* Allowed range of uids */
	1000, 32000,		/* Allowed range of gids */
	0,			/* Days until account expires */
	0			/* Days until password expires */
};

static char const *comments[_UC_FIELDS] =
{
	"#\n# pw.conf - user/group configuration defaults\n#\n",
	"\n# Password for new users? no=nologin yes=loginid none=blank random=random\n",
	"\n# Reuse gaps in uid sequence? (yes or no)\n",
	"\n# Reuse gaps in gid sequence? (yes or no)\n",
	"\n# Path to the NIS passwd file (blank or 'no' for none)\n",
	"\n# Obtain default dotfiles from this directory\n",
	"\n# Mail this file to new user (/etc/newuser.msg or no)\n",
	"\n# Log add/change/remove information in this file\n",
	"\n# Root directory in which $HOME directory is created\n",
	"\n# Mode for the new $HOME directory, will be modified by umask\n",
	"\n# Colon separated list of directories containing valid shells\n",
	"\n# Comma separated list of available shells (without paths)\n",
	"\n# Default shell (without path)\n",
	"\n# Default group (leave blank for new group per user)\n",
	"\n# Extra groups for new users\n",
	"\n# Default login class for new users\n",
	"\n# Range of valid default user ids\n",
	NULL,
	"\n# Range of valid default group ids\n",
	NULL,
	"\n# Days after which account expires (0=disabled)\n",
	"\n# Days after which password expires (0=disabled)\n"
};

static char const *kwds[] =
{
	"",
	"defaultpasswd",
	"reuseuids",
	"reusegids",
	"nispasswd",
	"skeleton",
	"newmail",
	"logfile",
	"home",
	"homemode",
	"shellpath",
	"shells",
	"defaultshell",
	"defaultgroup",
	"extragroups",
	"defaultclass",
	"minuid",
	"maxuid",
	"mingid",
	"maxgid",
	"expire_days",
	"password_days",
	NULL
};

static char    *
unquote(char const * str)
{
	if (str && (*str == '"' || *str == '\'')) {
		char           *p = strchr(str + 1, *str);

		if (p != NULL)
			*p = '\0';
		return (char *) (*++str ? str : NULL);
	}
	return (char *) str;
}

int
boolean_val(char const * str, int dflt)
{
	if ((str = unquote(str)) != NULL) {
		int             i;

		for (i = 0; booltrue[i]; i++)
			if (strcmp(str, booltrue[i]) == 0)
				return 1;
		for (i = 0; boolfalse[i]; i++)
			if (strcmp(str, boolfalse[i]) == 0)
				return 0;
	}
	return dflt;
}

int
passwd_val(char const * str, int dflt)
{
	if ((str = unquote(str)) != NULL) {
		int             i;

		for (i = 0; booltrue[i]; i++)
			if (strcmp(str, booltrue[i]) == 0)
				return P_YES;
		for (i = 0; boolfalse[i]; i++)
			if (strcmp(str, boolfalse[i]) == 0)
				return P_NO;

		/*
		 * Special cases for defaultpassword
		 */
		if (strcmp(str, "random") == 0)
			return P_RANDOM;
		if (strcmp(str, "none") == 0)
			return P_NONE;

		errx(1, "Invalid value for default password");
	}
	return dflt;
}

char const     *
boolean_str(int val)
{
	if (val == P_NO)
		return (boolfalse[0]);
	else if (val == P_RANDOM)
		return ("random");
	else if (val == P_NONE)
		return ("none");
	else
		return (booltrue[0]);
}

char           *
newstr(char const * p)
{
	char	*q;

	if ((p = unquote(p)) == NULL)
		return (NULL);

	if ((q = strdup(p)) == NULL)
		err(1, "strdup()");

	return (q);
}

struct userconf *
read_userconfig(char const * file)
{
	FILE	*fp;
	char	*buf, *p;
	const char *errstr;
	size_t	linecap;
	ssize_t	linelen;

	buf = NULL;
	linecap = 0;

	if ((fp = fopen(file, "r")) == NULL)
		return (&config);

	while ((linelen = getline(&buf, &linecap, fp)) > 0) {
		if (*buf && (p = strtok(buf, " \t\r\n=")) != NULL && *p != '#') {
			static char const toks[] = " \t\r\n,=";
			char           *q = strtok(NULL, toks);
			int             i = 0;
			mode_t          *modeset;

			while (i < _UC_FIELDS && strcmp(p, kwds[i]) != 0)
				++i;
#if debugging
			if (i == _UC_FIELDS)
				printf("Got unknown kwd `%s' val=`%s'\n", p, q ? q : "");
			else
				printf("Got kwd[%s]=%s\n", p, q);
#endif
			switch (i) {
			case _UC_DEFAULTPWD:
				config.default_password = passwd_val(q, 1);
				break;
			case _UC_REUSEUID:
				config.reuse_uids = boolean_val(q, 0);
				break;
			case _UC_REUSEGID:
				config.reuse_gids = boolean_val(q, 0);
				break;
			case _UC_NISPASSWD:
				config.nispasswd = (q == NULL || !boolean_val(q, 1))
					? NULL : newstr(q);
				break;
			case _UC_DOTDIR:
				config.dotdir = (q == NULL || !boolean_val(q, 1))
					? NULL : newstr(q);
				break;
				case _UC_NEWMAIL:
				config.newmail = (q == NULL || !boolean_val(q, 1))
					? NULL : newstr(q);
				break;
			case _UC_LOGFILE:
				config.logfile = (q == NULL || !boolean_val(q, 1))
					? NULL : newstr(q);
				break;
			case _UC_HOMEROOT:
				config.home = (q == NULL || !boolean_val(q, 1))
					? "/home" : newstr(q);
				break;
			case _UC_HOMEMODE:
				modeset = setmode(q);
				config.homemode = (q == NULL || !boolean_val(q, 1))
					? _DEF_DIRMODE : getmode(modeset, _DEF_DIRMODE);
				free(modeset);
				break;
			case _UC_SHELLPATH:
				config.shelldir = (q == NULL || !boolean_val(q, 1))
					? "/bin" : newstr(q);
				break;
			case _UC_SHELLS:
				for (i = 0; i < _UC_MAXSHELLS && q != NULL; i++, q = strtok(NULL, toks))
					system_shells[i] = newstr(q);
				if (i > 0)
					while (i < _UC_MAXSHELLS)
						system_shells[i++] = NULL;
				break;
			case _UC_DEFAULTSHELL:
				config.shell_default = (q == NULL || !boolean_val(q, 1))
					? (char *) bourne_shell : newstr(q);
				break;
			case _UC_DEFAULTGROUP:
				q = unquote(q);
				config.default_group = (q == NULL || !boolean_val(q, 1) || GETGRNAM(q) == NULL)
					? NULL : newstr(q);
				break;
			case _UC_EXTRAGROUPS:
				while ((q = strtok(NULL, toks)) != NULL) {
					if (config.groups == NULL)
						config.groups = sl_init();
					sl_add(config.groups, newstr(q));
				}
				break;
			case _UC_DEFAULTCLASS:
				config.default_class = (q == NULL || !boolean_val(q, 1))
					? NULL : newstr(q);
				break;
			case _UC_MINUID:
				if ((q = unquote(q)) != NULL) {
					config.min_uid = strtounum(q, 0,
					    UID_MAX, &errstr);
					if (errstr)
						warnx("Invalid min_uid: '%s';"
						    " ignoring", q);
				}
				break;
			case _UC_MAXUID:
				if ((q = unquote(q)) != NULL) {
					config.max_uid = strtounum(q, 0,
					    UID_MAX, &errstr);
					if (errstr)
						warnx("Invalid max_uid: '%s';"
						    " ignoring", q);
				}
				break;
			case _UC_MINGID:
				if ((q = unquote(q)) != NULL) {
					config.min_gid = strtounum(q, 0,
					    GID_MAX, &errstr);
					if (errstr)
						warnx("Invalid min_gid: '%s';"
						    " ignoring", q);
				}
				break;
			case _UC_MAXGID:
				if ((q = unquote(q)) != NULL) {
					config.max_gid = strtounum(q, 0,
					    GID_MAX, &errstr);
					if (errstr)
						warnx("Invalid max_gid: '%s';"
						    " ignoring", q);
				}
				break;
			case _UC_EXPIRE:
				if ((q = unquote(q)) != NULL) {
					config.expire_days = strtonum(q, 0,
					    INT_MAX, &errstr);
					if (errstr)
						warnx("Invalid expire days:"
						    " '%s'; ignoring", q);
				}
				break;
			case _UC_PASSWORD:
				if ((q = unquote(q)) != NULL) {
					config.password_days = strtonum(q, 0,
					    INT_MAX, &errstr);
					if (errstr)
						warnx("Invalid password days:"
						    " '%s'; ignoring", q);
				}
				break;
			case _UC_FIELDS:
			case _UC_NONE:
				break;
			}
		}
	}
	free(buf);
	fclose(fp);

	return (&config);
}


int
write_userconfig(struct userconf *cnf, const char *file)
{
	int             fd;
	int             i, j;
	struct sbuf	*buf;
	FILE           *fp;
	char		cfgfile[MAXPATHLEN];

	if (file == NULL) {
		snprintf(cfgfile, sizeof(cfgfile), "%s/" _PW_CONF,
		    conf.etcpath);
		file = cfgfile;
	}

	if ((fd = open(file, O_CREAT|O_RDWR|O_TRUNC|O_EXLOCK, 0644)) == -1)
		return (0);

	if ((fp = fdopen(fd, "w")) == NULL) {
		close(fd);
		return (0);
	}
			
	buf = sbuf_new_auto();
	for (i = _UC_NONE; i < _UC_FIELDS; i++) {
		int             quote = 1;

		sbuf_clear(buf);
		switch (i) {
		case _UC_DEFAULTPWD:
			sbuf_cat(buf, boolean_str(cnf->default_password));
			break;
		case _UC_REUSEUID:
			sbuf_cat(buf, boolean_str(cnf->reuse_uids));
			break;
		case _UC_REUSEGID:
			sbuf_cat(buf, boolean_str(cnf->reuse_gids));
			break;
		case _UC_NISPASSWD:
			sbuf_cat(buf, cnf->nispasswd ?  cnf->nispasswd : "");
			quote = 0;
			break;
		case _UC_DOTDIR:
			sbuf_cat(buf, cnf->dotdir ?  cnf->dotdir :
			    boolean_str(0));
			break;
		case _UC_NEWMAIL:
			sbuf_cat(buf, cnf->newmail ?  cnf->newmail :
			    boolean_str(0));
			break;
		case _UC_LOGFILE:
			sbuf_cat(buf, cnf->logfile ?  cnf->logfile :
			    boolean_str(0));
			break;
		case _UC_HOMEROOT:
			sbuf_cat(buf, cnf->home);
			break;
		case _UC_HOMEMODE:
			sbuf_printf(buf, "%04o", cnf->homemode);
			quote = 0;
			break;
		case _UC_SHELLPATH:
			sbuf_cat(buf, cnf->shelldir);
			break;
		case _UC_SHELLS:
			for (j = 0; j < _UC_MAXSHELLS &&
			    system_shells[j] != NULL; j++)
				sbuf_printf(buf, "%s\"%s\"", j ?
				    "," : "", system_shells[j]);
			quote = 0;
			break;
		case _UC_DEFAULTSHELL:
			sbuf_cat(buf, cnf->shell_default ?
			    cnf->shell_default : bourne_shell);
			break;
		case _UC_DEFAULTGROUP:
			sbuf_cat(buf, cnf->default_group ?
			    cnf->default_group : "");
			break;
		case _UC_EXTRAGROUPS:
			for (j = 0; cnf->groups != NULL &&
			    j < (int)cnf->groups->sl_cur; j++)
				sbuf_printf(buf, "%s\"%s\"", j ?
				    "," : "", cnf->groups->sl_str[j]);
			quote = 0;
			break;
		case _UC_DEFAULTCLASS:
			sbuf_cat(buf, cnf->default_class ?
			    cnf->default_class : "");
			break;
		case _UC_MINUID:
			sbuf_printf(buf, "%ju", (uintmax_t)cnf->min_uid);
			quote = 0;
			break;
		case _UC_MAXUID:
			sbuf_printf(buf, "%ju", (uintmax_t)cnf->max_uid);
			quote = 0;
			break;
		case _UC_MINGID:
			sbuf_printf(buf, "%ju", (uintmax_t)cnf->min_gid);
			quote = 0;
			break;
		case _UC_MAXGID:
			sbuf_printf(buf, "%ju", (uintmax_t)cnf->max_gid);
			quote = 0;
			break;
		case _UC_EXPIRE:
			sbuf_printf(buf, "%jd", (intmax_t)cnf->expire_days);
			quote = 0;
			break;
		case _UC_PASSWORD:
			sbuf_printf(buf, "%jd", (intmax_t)cnf->password_days);
			quote = 0;
			break;
		case _UC_NONE:
			break;
		}
		sbuf_finish(buf);

		if (comments[i])
			fputs(comments[i], fp);

		if (*kwds[i]) {
			if (quote)
				fprintf(fp, "%s = \"%s\"\n", kwds[i],
				    sbuf_data(buf));
			else
				fprintf(fp, "%s = %s\n", kwds[i], sbuf_data(buf));
#if debugging
			printf("WROTE: %s = %s\n", kwds[i], sbuf_data(buf));
#endif
		}
	}
	sbuf_delete(buf);
	return (fclose(fp) != EOF);
}
