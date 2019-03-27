/************************************************************************
* Copyright 1995 by Wietse Venema.  All rights reserved.
*
* This material was originally written and compiled by Wietse Venema at
* Eindhoven University of Technology, The Netherlands, in 1990, 1991,
* 1992, 1993, 1994 and 1995.
*
* Redistribution and use in source and binary forms are permitted
* provided that this entire copyright notice is duplicated in all such
* copies.
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

    BUGS
	This module uses strtok(3), which may cause conflicts with other
	uses of that same routine.

    AUTHOR
	Wietse Venema (wietse@wzv.win.tue.nl)
	Eindhoven University of Technology
	The Netherlands
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "login.h"
#include "pathnames.h"

static void	login_protect(const char *, char *, int, uid_t, gid_t);

#define	WSPACE		" \t\n"

/* login_fbtab - apply protections specified in /etc/fbtab or logindevperm */

void
login_fbtab(char *tty, uid_t uid, gid_t gid)
{
    FILE   *fp;
    char    buf[BUFSIZ];
    char   *devname;
    char   *cp;
    int     prot;
    const char *table;

    if ((fp = fopen(table = _PATH_FBTAB, "r")) == NULL
    && (fp = fopen(table = _PATH_LOGINDEVPERM, "r")) == NULL)
	return;

    while (fgets(buf, sizeof(buf), fp)) {
	if ((cp = strchr(buf, '#')))
	    *cp = 0;				/* strip comment */
	if ((cp = devname = strtok(buf, WSPACE)) == NULL)
	    continue;				/* empty or comment */
	if (strncmp(devname, _PATH_DEV, sizeof _PATH_DEV - 1) != 0
	       || (cp = strtok(NULL, WSPACE)) == NULL
	       || *cp != '0'
	       || sscanf(cp, "%o", &prot) == 0
	       || prot == 0
	       || (prot & 0777) != prot
	       || (cp = strtok(NULL, WSPACE)) == NULL) {
	    syslog(LOG_ERR, "%s: bad entry: %s", table, cp ? cp : "(null)");
	    continue;
	}
	if (strcmp(devname + 5, tty) == 0) {
	    for (cp = strtok(cp, ":"); cp; cp = strtok(NULL, ":")) {
		login_protect(table, cp, prot, uid, gid);
	    }
	}
    }
    fclose(fp);
}

/* login_protect - protect one device entry */

static void
login_protect(const char *table, char *pattern, int mask, uid_t uid, gid_t gid)
{
    glob_t  gl;
    char   *path;
    unsigned int     i;

    if (glob(pattern, GLOB_NOSORT, NULL, &gl) != 0)
	return;
    for (i = 0; i < gl.gl_pathc; i++) {
	path = gl.gl_pathv[i];
	/* clear flags of the device */
	if (chflags(path, 0) && errno != ENOENT && errno != EOPNOTSUPP)
	    syslog(LOG_ERR, "%s: chflags(%s): %m", table, path);
	if (chmod(path, mask) && errno != ENOENT)
	    syslog(LOG_ERR, "%s: chmod(%s): %m", table, path);
	if (chown(path, uid, gid) && errno != ENOENT)
	    syslog(LOG_ERR, "%s: chown(%s): %m", table, path);
    }
    globfree(&gl);
}
