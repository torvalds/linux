/*	$OpenBSD: remote.c,v 1.16 2006/06/06 23:24:52 deraadt Exp $	*/
/*	$NetBSD: remote.c,v 1.5 1997/04/20 00:02:45 mellon Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)remote.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: remote.c,v 1.16 2006/06/06 23:24:52 deraadt Exp $";
#endif
#endif /* not lint */

#include <stdio.h>
#include <stdlib.h>

#include "pathnames.h"
#include "tip.h"

/*
 * Attributes to be gleened from remote host description
 *   data base.
 */
static char **caps[] = {
	&AT, &DV, &CM, &CU, &EL, &IE, &OE, &PN, &PR, &DI,
	&ES, &EX, &FO, &RC, &RE, &PA
};

static char *capstrings[] = {
	"at", "dv", "cm", "cu", "el", "ie", "oe", "pn", "pr",
	"di", "es", "ex", "fo", "rc", "re", "pa", 0
};

static char	*db_array[3] = { _PATH_REMOTE, 0, 0 };

#define cgetflag(f)	(cgetcap(bp, f, ':') != NULL)

static void	getremcap(char *);

static void
getremcap(char *host)
{
	char **p, ***q, *bp, *rempath;
	int   stat;

	rempath = getenv("REMOTE");
	if (rempath != NULL) {
		if (*rempath != '/')
			/* we have an entry */
			cgetset(rempath);
		else {	/* we have a path */
			db_array[1] = rempath;
			db_array[2] = _PATH_REMOTE;
		}
	}

	if ((stat = cgetent(&bp, db_array, host)) < 0) {
		if ((DV != NULL) ||
		    (host[0] == '/' && access(DV = host, R_OK | W_OK) == 0)) {
			CU = DV;
			HO = host;
			HW = 1;
			DU = 0;
			if (!BR)
				BR = DEFBR;
			FS = DEFFS;
			return;
		}
		switch (stat) {
		case -1:
			fprintf(stderr, "%s: unknown host %s\n", __progname,
			    host);
			break;
		case -2:
			fprintf(stderr,
			    "%s: can't open host description file\n",
			    __progname);
			break;
		case -3:
			fprintf(stderr,
			    "%s: possible reference loop in host description file\n", __progname);
			break;
		}
		exit(3);
	}

	for (p = capstrings, q = caps; *p != NULL; p++, q++)
		if (**q == NULL)
			cgetstr(bp, *p, *q);
	if (!BR && (cgetnum(bp, "br", &BR) == -1))
		BR = DEFBR;
	if (!LD && (cgetnum(bp, "ld", &LD) == -1))
		LD = TTYDISC;
	if (cgetnum(bp, "fs", &FS) == -1)
		FS = DEFFS;
	if (DU < 0)
		DU = 0;
	else
		DU = cgetflag("du");
	if (DV == NOSTR) {
		fprintf(stderr, "%s: missing device spec\n", host);
		exit(3);
	}
	if (DU && CU == NOSTR)
		CU = DV;
	if (DU && PN == NOSTR) {
		fprintf(stderr, "%s: missing phone number\n", host);
		exit(3);
	}
	if (DU && AT == NOSTR) {
		fprintf(stderr, "%s: missing acu type\n", host);
		exit(3);
	}

	HD = cgetflag("hd");

	/*
	 * This effectively eliminates the "hw" attribute
	 *   from the description file
	 */
	if (!HW)
		HW = (CU == NOSTR) || (DU && equal(DV, CU));
	HO = host;
	/*
	 * see if uppercase mode should be turned on initially
	 */
	if (cgetflag("ra"))
		setboolean(value(RAISE), 1);
	if (cgetflag("ec"))
		setboolean(value(ECHOCHECK), 1);
	if (cgetflag("be"))
		setboolean(value(BEAUTIFY), 1);
	if (cgetflag("nb"))
		setboolean(value(BEAUTIFY), 0);
	if (cgetflag("sc"))
		setboolean(value(SCRIPT), 1);
	if (cgetflag("tb"))
		setboolean(value(TABEXPAND), 1);
	if (cgetflag("vb"))
		setboolean(value(VERBOSE), 1);
	if (cgetflag("nv"))
		setboolean(value(VERBOSE), 0);
	if (cgetflag("ta"))
		setboolean(value(TAND), 1);
	if (cgetflag("nt"))
		setboolean(value(TAND), 0);
	if (cgetflag("rw"))
		setboolean(value(RAWFTP), 1);
	if (cgetflag("hd"))
		setboolean(value(HALFDUPLEX), 1);
	if (cgetflag("dc"))
		setboolean(value(DC), 1);
	if (cgetflag("hf"))
		setboolean(value(HARDWAREFLOW), 1);
	if (RE == NOSTR)
		RE = (char *)"tip.record";
	if (EX == NOSTR)
		EX = (char *)"\t\n\b\f";
	if (ES != NOSTR)
		vstring("es", ES);
	if (FO != NOSTR)
		vstring("fo", FO);
	if (PR != NOSTR)
		vstring("pr", PR);
	if (RC != NOSTR)
		vstring("rc", RC);
	if (cgetnum(bp, "dl", &DL) == -1)
		DL = 0;
	if (cgetnum(bp, "cl", &CL) == -1)
		CL = 0;
	if (cgetnum(bp, "et", &ET) == -1)
		ET = 10;
}

char *
getremote(char *host)
{
	char *cp;
	static char *next;
	static int lookedup = 0;

	if (!lookedup) {
		if (host == NOSTR && (host = getenv("HOST")) == NOSTR) {
			fprintf(stderr, "%s: no host specified\n", __progname);
			exit(3);
		}
		getremcap(host);
		next = DV;
		lookedup++;
	}
	/*
	 * We return a new device each time we're called (to allow
	 *   a rotary action to be simulated)
	 */
	if (next == NOSTR)
		return (NOSTR);
	if ((cp = strchr(next, ',')) == NULL) {
		DV = next;
		next = NOSTR;
	} else {
		*cp++ = '\0';
		DV = next;
		next = cp;
	}
	return (DV);
}
