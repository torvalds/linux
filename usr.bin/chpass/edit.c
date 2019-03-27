/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
#ifndef lint
static char sccsid[] = "@(#)edit.c	8.3 (Berkeley) 4/2/94";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pw_scan.h>
#include <libutil.h>

#include "chpass.h"

static int display(const char *tfn, struct passwd *pw);
static struct passwd *verify(const char *tfn, struct passwd *pw);

struct passwd *
edit(const char *tfn, struct passwd *pw)
{
	struct passwd *npw;
	char *line;
	size_t len;

	if (display(tfn, pw) == -1)
		return (NULL);
	for (;;) {
		switch (pw_edit(1)) {
		case -1:
			return (NULL);
		case 0:
			return (pw_dup(pw));
		default:
			break;
		}
		if ((npw = verify(tfn, pw)) != NULL)
			return (npw);
		free(npw);
		printf("re-edit the password file? ");
		fflush(stdout);
		if ((line = fgetln(stdin, &len)) == NULL) {
			warn("fgetln()");
			return (NULL);
		}
		if (len > 0 && (*line == 'N' || *line == 'n'))
			return (NULL);
	}
}

/*
 * display --
 *	print out the file for the user to edit; strange side-effect:
 *	set conditional flag if the user gets to edit the shell.
 */
static int
display(const char *tfn, struct passwd *pw)
{
	FILE *fp;
	char *bp, *gecos, *p;

	if ((fp = fopen(tfn, "w")) == NULL) {
		warn("%s", tfn);
		return (-1);
	}

	(void)fprintf(fp,
	    "#Changing user information for %s.\n", pw->pw_name);
	if (master_mode) {
		(void)fprintf(fp, "Login: %s\n", pw->pw_name);
		(void)fprintf(fp, "Password: %s\n", pw->pw_passwd);
		(void)fprintf(fp, "Uid [#]: %lu\n", (unsigned long)pw->pw_uid);
		(void)fprintf(fp, "Gid [# or name]: %lu\n",
		    (unsigned long)pw->pw_gid);
		(void)fprintf(fp, "Change [month day year]: %s\n",
		    ttoa(pw->pw_change));
		(void)fprintf(fp, "Expire [month day year]: %s\n",
		    ttoa(pw->pw_expire));
		(void)fprintf(fp, "Class: %s\n", pw->pw_class);
		(void)fprintf(fp, "Home directory: %s\n", pw->pw_dir);
		(void)fprintf(fp, "Shell: %s\n",
		    *pw->pw_shell ? pw->pw_shell : _PATH_BSHELL);
	}
	/* Only admin can change "restricted" shells. */
#if 0
	else if (ok_shell(pw->pw_shell))
		/*
		 * Make shell a restricted field.  Ugly with a
		 * necklace, but there's not much else to do.
		 */
#else
	else if ((!list[E_SHELL].restricted && ok_shell(pw->pw_shell)) ||
	    master_mode)
		/*
		 * If change not restrict (table.c) and standard shell
		 *	OR if root, then allow editing of shell.
		 */
#endif
		(void)fprintf(fp, "Shell: %s\n",
		    *pw->pw_shell ? pw->pw_shell : _PATH_BSHELL);
	else
		list[E_SHELL].restricted = 1;

	if ((bp = gecos = strdup(pw->pw_gecos)) == NULL) {
		warn(NULL);
		fclose(fp);
		return (-1);
	}

	p = strsep(&bp, ",");
	p = strdup(p ? p : "");
	list[E_NAME].save = p;
	if (!list[E_NAME].restricted || master_mode)
	  (void)fprintf(fp, "Full Name: %s\n", p);

	p = strsep(&bp, ",");
	p = strdup(p ? p : "");
	list[E_LOCATE].save = p;
	if (!list[E_LOCATE].restricted || master_mode)
	  (void)fprintf(fp, "Office Location: %s\n", p);

	p = strsep(&bp, ",");
	p = strdup(p ? p : "");
	list[E_BPHONE].save = p;
	if (!list[E_BPHONE].restricted || master_mode)
	  (void)fprintf(fp, "Office Phone: %s\n", p);

	p = strsep(&bp, ",");
	p = strdup(p ? p : "");
	list[E_HPHONE].save = p;
	if (!list[E_HPHONE].restricted || master_mode)
	  (void)fprintf(fp, "Home Phone: %s\n", p);

	bp = strdup(bp ? bp : "");
	list[E_OTHER].save = bp;
	if (!list[E_OTHER].restricted || master_mode)
	  (void)fprintf(fp, "Other information: %s\n", bp);

	free(gecos);

	(void)fchown(fileno(fp), getuid(), getgid());
	(void)fclose(fp);
	return (0);
}

static struct passwd *
verify(const char *tfn, struct passwd *pw)
{
	struct passwd *npw;
	ENTRY *ep;
	char *buf, *p, *val;
	struct stat sb;
	FILE *fp;
	int line;
	size_t len;

	if ((pw = pw_dup(pw)) == NULL)
		return (NULL);
	if ((fp = fopen(tfn, "r")) == NULL ||
	    fstat(fileno(fp), &sb) == -1) {
		warn("%s", tfn);
		free(pw);
		return (NULL);
	}
	if (sb.st_size == 0) {
		warnx("corrupted temporary file");
		fclose(fp);
		free(pw);
		return (NULL);
	}
	val = NULL;
	for (line = 1; (buf = fgetln(fp, &len)) != NULL; ++line) {
		if (*buf == '\0' || *buf == '#')
			continue;
		while (len > 0 && isspace(buf[len - 1]))
			--len;
		for (ep = list;; ++ep) {
			if (!ep->prompt) {
				warnx("%s: unrecognized field on line %d",
				    tfn, line);
				goto bad;
			}
			if (ep->len > len)
				continue;
			if (strncasecmp(buf, ep->prompt, ep->len) != 0)
				continue;
			if (ep->restricted && !master_mode) {
				warnx("%s: you may not change the %s field",
				    tfn, ep->prompt);
				goto bad;
			}
			for (p = buf; p < buf + len && *p != ':'; ++p)
				/* nothing */ ;
			if (*p != ':') {
				warnx("%s: line %d corrupted", tfn, line);
				goto bad;
			}
			while (++p < buf + len && isspace(*p))
				/* nothing */ ;
			free(val);
			asprintf(&val, "%.*s", (int)(buf + len - p), p);
			if (val == NULL)
				goto bad;
			if (ep->except && strpbrk(val, ep->except)) {
				warnx("%s: invalid character in \"%s\" field '%s'",
				    tfn, ep->prompt, val);
				goto bad;
			}
			if ((ep->func)(val, pw, ep))
				goto bad;
			break;
		}
	}
	free(val);
	fclose(fp);

	/* Build the gecos field. */
	len = asprintf(&p, "%s,%s,%s,%s,%s", list[E_NAME].save,
	    list[E_LOCATE].save, list[E_BPHONE].save,
	    list[E_HPHONE].save, list[E_OTHER].save);
	if (p == NULL) {
		warn("asprintf()");
		free(pw);
		return (NULL);
	}
	while (len > 0 && p[len - 1] == ',')
		p[--len] = '\0';
	pw->pw_gecos = p;
	buf = pw_make(pw);
	free(pw);
	free(p);
	if (buf == NULL) {
		warn("pw_make()");
		return (NULL);
	}
	npw = pw_scan(buf, PWSCAN_WARN|PWSCAN_MASTER);
	free(buf);
	return (npw);
bad:
	free(pw);
	free(val);
	fclose(fp);
	return (NULL);
}
