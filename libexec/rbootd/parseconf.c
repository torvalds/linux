/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1992 The University of Utah and the Center
 *	for Software Science (CSS).
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Center for Software Science of the University of Utah Computer
 * Science Department.  CSS requests users of this software to return
 * to css-dist@cs.utah.edu any improvements that they make and grant
 * CSS redistribution rights.
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
 *
 *	from: @(#)parseconf.c	8.1 (Berkeley) 6/4/93
 *
 * From: Utah Hdr: parseconf.c 3.1 92/07/06
 * Author: Jeff Forys, University of Utah CSS
 */

#ifndef lint
#if 0
static const char sccsid[] = "@(#)parseconf.c	8.1 (Berkeley) 6/4/93";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "defs.h"

/*
**  ParseConfig -- parse the config file into linked list of clients.
**
**	Parameters:
**		None.
**
**	Returns:
**		1 on success, 0 otherwise.
**
**	Side Effects:
**		- Linked list of clients will be (re)allocated.
**
**	Warnings:
**		- GetBootFiles() must be called before this routine
**		  to create a linked list of default boot files.
*/
int
ParseConfig(void)
{
	FILE *fp;
	CLIENT *client;
	u_int8_t *addr;
	char line[C_LINELEN];
	char *cp, *bcp;
	int i, j;
	int omask, linecnt = 0;

	if (BootAny)				/* ignore config file */
		return(1);

	FreeClients();				/* delete old list of clients */

	if ((fp = fopen(ConfigFile, "r")) == NULL) {
		syslog(LOG_ERR, "ParseConfig: can't open config file (%s)",
		       ConfigFile);
		return(0);
	}

	/*
	 *  We've got to block SIGHUP to prevent reconfiguration while
	 *  dealing with the linked list of Clients.  This can be done
	 *  when actually linking the new client into the list, but
	 *  this could have unexpected results if the server was HUP'd
	 *  whilst reconfiguring.  Hence, it is done here.
	 */
	omask = sigblock(sigmask(SIGHUP));

	/*
	 *  GETSTR positions `bcp' at the start of the current token,
	 *  and null terminates it.  `cp' is positioned at the start
	 *  of the next token.  spaces & commas are separators.
	 */
#define GETSTR	while (isspace(*cp) || *cp == ',') cp++;	\
		bcp = cp;					\
		while (*cp && *cp!=',' && !isspace(*cp)) cp++;	\
		if (*cp) *cp++ = '\0'

	/*
	 *  For each line, parse it into a new CLIENT struct.
	 */
	while (fgets(line, C_LINELEN, fp) != NULL) {
		linecnt++;				/* line counter */

		if (*line == '\0' || *line == '#')	/* ignore comment */
			continue;

		if ((cp = strchr(line,'#')) != NULL)	/* trash comments */
			*cp = '\0';

		cp = line;				/* init `cp' */
		GETSTR;					/* get RMP addr */
		if (bcp == cp)				/* all delimiters */
			continue;

		/*
		 *  Get an RMP address from a string.  Abort on failure.
		 */
		if ((addr = ParseAddr(bcp)) == NULL) {
			syslog(LOG_ERR,
			       "ParseConfig: line %d: can't parse <%s>",
			       linecnt, bcp);
			continue;
		}

		if ((client = NewClient(addr)) == NULL)	/* alloc new client */
			continue;

		GETSTR;					/* get first file */

		/*
		 *  If no boot files are spec'd, use the default list.
		 *  Otherwise, validate each file (`bcp') against the
		 *  list of boot-able files.
		 */
		i = 0;
		if (bcp == cp)				/* no files spec'd */
			for (; i < C_MAXFILE && BootFiles[i] != NULL; i++)
				client->files[i] = BootFiles[i];
		else {
			do {
				/*
				 *  For each boot file spec'd, make sure it's
				 *  in our list.  If so, include a pointer to
				 *  it in the CLIENT's list of boot files.
				 */
				for (j = 0; ; j++) {
					if (j==C_MAXFILE||BootFiles[j]==NULL) {
						syslog(LOG_ERR, "ParseConfig: line %d: no boot file (%s)",
						       linecnt, bcp);
						break;
					}
					if (STREQN(BootFiles[j], bcp)) {
						if (i < C_MAXFILE)
							client->files[i++] =
							    BootFiles[j];
						else
							syslog(LOG_ERR, "ParseConfig: line %d: too many boot files (%s)",
							       linecnt, bcp);
						break;
					}
				}
				GETSTR;			/* get next file */
			} while (bcp != cp);

			/*
			 *  Restricted list of boot files were spec'd,
			 *  however, none of them were found.  Since we
			 *  apparently can't let them boot "just anything",
			 *  the entire record is invalidated.
			 */
			if (i == 0) {
				FreeClient(client);
				continue;
			}
		}

		/*
		 *  Link this client into the linked list of clients.
		 *  SIGHUP has already been blocked.
		 */
		if (Clients)
			client->next = Clients;
		Clients = client;
	}

	(void) fclose(fp);				/* close config file */

	(void) sigsetmask(omask);			/* reset signal mask */

	return(1);					/* return success */
}

/*
**  ParseAddr -- Parse a string containing an RMP address.
**
**	This routine is fairly liberal at parsing an RMP address.  The
**	address must contain 6 octets consisting of between 0 and 2 hex
**	chars (upper/lower case) separated by colons.  If two colons are
**	together (e.g. "::", the octet between them is recorded as being
**	zero.  Hence, the following addrs are all valid and parse to the
**	same thing:
**
**		08:00:09:00:66:ad	8::9:0:66:AD	8::9::66:aD
**
**	For clarity, an RMP address is really an Ethernet address, but
**	since the HP boot code uses IEEE 802.3, it's really an IEEE
**	802.3 address.  Of course, all of these are identical.
**
**	Parameters:
**		str - string representation of an RMP address.
**
**	Returns:
**		pointer to a static array of RMP_ADDRLEN bytes.
**
**	Side Effects:
**		None.
**
**	Warnings:
**		- The return value points to a static buffer; it must
**		  be copied if it's to be saved.
*/
u_int8_t *
ParseAddr(char *str)
{
	static u_int8_t addr[RMP_ADDRLEN];
	char *cp;
	unsigned i;
	int part, subpart;

	memset((char *)&addr[0], 0, RMP_ADDRLEN);	/* zero static buffer */

	part = subpart = 0;
	for (cp = str; *cp; cp++) {
		/*
		 *  A colon (`:') must be used to delimit each octet.
		 */
		if (*cp == ':') {
			if (++part == RMP_ADDRLEN)	/* too many parts */
				return(NULL);
			subpart = 0;
			continue;
		}

		/*
		 *  Convert hex character to an integer.
		 */
		if (isdigit(*cp))
			i = *cp - '0';
		else {
			i = (isupper(*cp)? tolower(*cp): *cp) - 'a' + 10;
			if (i < 10 || i > 15)		/* not a hex char */
				return(NULL);
		}

		if (subpart++) {
			if (subpart > 2)		/* too many hex chars */
				return(NULL);
			addr[part] <<= 4;
		}
		addr[part] |= i;
	}

	if (part != (RMP_ADDRLEN-1))			/* too few parts */
		return(NULL);

	return(&addr[0]);
}

/*
**  GetBootFiles -- record list of files in current (boot) directory.
**
**	Parameters:
**		None.
**
**	Returns:
**		Number of boot files on success, 0 on failure.
**
**	Side Effects:
**		Strings in `BootFiles' are freed/allocated.
**
**	Warnings:
**		- After this routine is called, ParseConfig() must be
**		  called to re-order it's list of boot file pointers.
*/
int
GetBootFiles(void)
{
	DIR *dfd;
	struct stat statb;
	struct dirent *dp;
	int i;

	/*
	 *  Free the current list of boot files.
	 */
	for (i = 0; i < C_MAXFILE && BootFiles[i] != NULL; i++) {
		FreeStr(BootFiles[i]);
		BootFiles[i] = NULL;
	}

	/*
	 *  Open current directory to read boot file names.
	 */
	if ((dfd = opendir(".")) == NULL) {	/* open BootDir */
		syslog(LOG_ERR, "GetBootFiles: can't open directory (%s)\n",
		       BootDir);
		return(0);
	}

	/*
	 *  Read each boot file name and allocate space for it in the
	 *  list of boot files (BootFiles).  All boot files read after
	 *  C_MAXFILE will be ignored.
	 */
	i = 0;
	for (dp = readdir(dfd); dp != NULL; dp = readdir(dfd)) {
		if (stat(dp->d_name, &statb) < 0 ||
		    (statb.st_mode & S_IFMT) != S_IFREG)
			continue;
		if (i == C_MAXFILE)
			syslog(LOG_ERR,
			       "GetBootFiles: too many boot files (%s ignored)",
			       dp->d_name);
		else if ((BootFiles[i] = NewStr(dp->d_name)) != NULL)
			i++;
	}

	(void) closedir(dfd);			/* close BootDir */

	if (i == 0)				/* can't find any boot files */
		syslog(LOG_ERR, "GetBootFiles: no boot files (%s)\n", BootDir);

	return(i);
}
