/*	$OpenBSD: login_fbtab.c,v 1.18 2022/12/27 17:10:08 jmc Exp $	*/

/************************************************************************
* Copyright 1995 by Wietse Venema.  All rights reserved.  Some individual
* files may be covered by other copyrights.
*
* This material was originally written and compiled by Wietse Venema at
* Eindhoven University of Technology, The Netherlands, in 1990, 1991,
* 1992, 1993, 1994 and 1995.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that this entire copyright notice
* is duplicated in all such copies.
*
* This software is provided "as is" and without any expressed or implied
* warranties, including, without limitation, the implied warranties of
* merchantibility and fitness for any particular purpose.
************************************************************************/
/*
    SYNOPSIS
	void login_fbtab(tty, uid, gid)
	char *tty;
	uid_t uid;
	gid_t gid;

    DESCRIPTION
	This module implements device security as described in the
	SunOS 4.1.x fbtab(5) and SunOS 5.x logindevperm(4) manual
	pages. The program first looks for /etc/fbtab. If that file
	cannot be opened it attempts to process /etc/logindevperm.
	We expect entries with the following format:

	    Comments start with a # and extend to the end of the line.

	    Blank lines or lines with only a comment are ignored.

	    All other lines consist of three fields delimited by
	    whitespace: a login device (/dev/console), an octal
	    permission number (0600), and a ":"-delimited list of
	    devices (/dev/kbd:/dev/mouse). All device names are
	    absolute paths. A path that ends in "*" refers to all
	    directory entries except "." and "..".

	    If the tty argument (relative path) matches a login device
	    name (absolute path), the permissions of the devices in the
	    ":"-delimited list are set as specified in the second
	    field, and their ownership is changed to that of the uid
	    and gid arguments.

    DIAGNOSTICS
	Problems are reported via the syslog daemon with severity
	LOG_ERR.

    AUTHOR
	Wietse Venema (wietse@wzv.win.tue.nl)
	Eindhoven University of Technology
	The Netherlands
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <glob.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <util.h>

#define _PATH_FBTAB	"/etc/fbtab"

static void login_protect(const char *, mode_t, uid_t, gid_t);

#define	WSPACE		" \t\n"

/*
 * login_fbtab - apply protections specified in /etc/fbtab or logindevperm
 */
void
login_fbtab(const char *tty, uid_t uid, gid_t gid)
{
	FILE	*fp;
	char	*buf, *toklast, *tbuf, *devnam, *cp;
	mode_t	prot;
	size_t	len;

	if ((fp = fopen(_PATH_FBTAB, "r")) == NULL)
		return;

	tbuf = NULL;
	while ((buf = fgetln(fp, &len)) != NULL) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		else {
			if ((tbuf = malloc(len + 1)) == NULL)
				break;
			memcpy(tbuf, buf, len);
			tbuf[len] = '\0';
			buf = tbuf;
		}
		if ((cp = strchr(buf, '#')))
			*cp = '\0';	/* strip comment */
		if (buf[0] == '\0' ||
		    (cp = devnam = strtok_r(buf, WSPACE, &toklast)) == NULL)
			continue;	/* empty or comment */
		if (strncmp(devnam, _PATH_DEV, sizeof(_PATH_DEV) - 1) != 0 ||
		    (cp = strtok_r(NULL, WSPACE, &toklast)) == NULL ||
		    *cp != '0' ||
		    sscanf(cp, "%o", &prot) == 0 ||
		    prot == 0 ||
		    (prot & 0777) != prot ||
		    (cp = strtok_r(NULL, WSPACE, &toklast)) == NULL) {
			syslog(LOG_ERR, "%s: bad entry: %s", _PATH_FBTAB,
			    cp ? cp : "(null)");
			continue;
		}
		if (strcmp(devnam + sizeof(_PATH_DEV) - 1, tty) == 0) {
			for (cp = strtok_r(cp, ":", &toklast); cp != NULL;
			    cp = strtok_r(NULL, ":", &toklast))
				login_protect(cp, prot, uid, gid);
		}
	}
	free(tbuf);
	fclose(fp);
}

/*
 * login_protect - protect one device entry
 */
static void
login_protect(const char *path, mode_t mask, uid_t uid, gid_t gid)
{
	glob_t	g;
	size_t	n;
	char	*gpath;

	if (strlen(path) >= PATH_MAX) {
		errno = ENAMETOOLONG;
		syslog(LOG_ERR, "%s: %s: %m", _PATH_FBTAB, path);
		return;
	}

	if (glob(path, GLOB_NOSORT, NULL, &g) != 0) {
		if (errno != ENOENT)
			syslog(LOG_ERR, "%s: glob(%s): %m", _PATH_FBTAB, path);
		globfree(&g);
		return;
	}

	for (n = 0; n < g.gl_matchc; n++) {
		gpath = g.gl_pathv[n];

		if (chmod(gpath, mask) && errno != ENOENT)
			syslog(LOG_ERR, "%s: chmod(%s): %m", _PATH_FBTAB, gpath);
		if (chown(gpath, uid, gid) && errno != ENOENT)
			syslog(LOG_ERR, "%s: chown(%s): %m", _PATH_FBTAB, gpath);
	}

	globfree(&g);
}
