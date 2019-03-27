/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Martin Blapp
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/syslog.h>

#include <rpc/rpc.h>
#include <rpcsvc/mount.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mounttab.h"

struct mtablist *mtabhead;

static void badline(const char *field, const char *bad);

/*
 * Add an entry to PATH_MOUNTTAB for each mounted NFS filesystem,
 * so the client can notify the NFS server even after reboot.
 */
int
add_mtab(char *hostp, char *dirp)
{
	FILE *mtabfile;

	if ((mtabfile = fopen(PATH_MOUNTTAB, "a")) == NULL)
		return (0);
	else {
		fprintf(mtabfile, "%ld\t%s\t%s\n",
		    (long)time(NULL), hostp, dirp);
		fclose(mtabfile);
		return (1);
	}
}

/*
 * Read mounttab line for line and return struct mtablist.
 */
int
read_mtab(void)
{
	struct mtablist **mtabpp, *mtabp;
	char *hostp, *dirp, *cp;
	char str[STRSIZ];
	char *timep, *endp;
	time_t actiontime;
	u_long ultmp;
	FILE *mtabfile;

	if ((mtabfile = fopen(PATH_MOUNTTAB, "r")) == NULL) {
		if (errno == ENOENT)
			return (0);
		else {
			syslog(LOG_ERR, "can't open %s", PATH_MOUNTTAB);
			return (0);
		}
	}
	actiontime = 0;
	mtabpp = &mtabhead;
	while (fgets(str, STRSIZ, mtabfile) != NULL) {
		cp = str;
		errno = 0;
		if (*cp == '#' || *cp == ' ' || *cp == '\n')
			continue;
		timep = strsep(&cp, " \t\n");
		if (timep == NULL || *timep == '\0') {
			badline("time", timep);
			continue;
		}
		hostp = strsep(&cp, " \t\n");
		if (hostp == NULL || *hostp == '\0') {
			badline("host", hostp);
			continue;
		}
		dirp = strsep(&cp, " \t\n");
		if (dirp == NULL || *dirp == '\0') {
			badline("dir", dirp);
			continue;
		}
		ultmp = strtoul(timep, &endp, 10);
		if (ultmp == ULONG_MAX || *endp != '\0') {
			badline("time", timep);
			continue;
		}
		actiontime = ultmp;
		if ((mtabp = malloc(sizeof (struct mtablist))) == NULL) {
			syslog(LOG_ERR, "malloc");
			fclose(mtabfile);
			return (0);
		}
		mtabp->mtab_time = actiontime;
		memmove(mtabp->mtab_host, hostp, MNTNAMLEN);
		mtabp->mtab_host[MNTNAMLEN - 1] = '\0';
		memmove(mtabp->mtab_dirp, dirp, MNTPATHLEN);
		mtabp->mtab_dirp[MNTPATHLEN - 1] = '\0';
		mtabp->mtab_next = (struct mtablist *)NULL;
		*mtabpp = mtabp;
		mtabpp = &mtabp->mtab_next;
	}
	fclose(mtabfile);
	return (1);
}

/*
 * Rewrite PATH_MOUNTTAB from scratch and skip bad entries.
 * Unlink PATH_MOUNTAB if no entry is left.
 */
int
write_mtab(int verbose)
{
	struct mtablist *mtabp, *mp;
	FILE *mtabfile;
	int line;

	if ((mtabfile = fopen(PATH_MOUNTTAB, "w")) == NULL) {
		syslog(LOG_ERR, "can't write to %s", PATH_MOUNTTAB);
			return (0);
	}
	line = 0;
	for (mtabp = mtabhead; mtabp != NULL; mtabp = mtabp->mtab_next) {
		if (mtabp->mtab_host[0] == '\0')
			continue;
		/* Skip if a later (hence more recent) entry is identical. */
		for (mp = mtabp->mtab_next; mp != NULL; mp = mp->mtab_next)
			if (strcmp(mtabp->mtab_host, mp->mtab_host) == 0 &&
			    strcmp(mtabp->mtab_dirp, mp->mtab_dirp) == 0)
				break;
		if (mp != NULL)
			continue;

		fprintf(mtabfile, "%ld\t%s\t%s\n",
		    (long)mtabp->mtab_time, mtabp->mtab_host,
		    mtabp->mtab_dirp);
		if (verbose)
			warnx("write mounttab entry %s:%s",
			    mtabp->mtab_host, mtabp->mtab_dirp);
		line++;
	}
	fclose(mtabfile);
	if (line == 0) {
		if (unlink(PATH_MOUNTTAB) == -1) {
			syslog(LOG_ERR, "can't remove %s", PATH_MOUNTTAB);
			return (0);
		}
	}
	return (1);
}

/*
 * Mark the entries as clean where RPC calls have been done successfully.
 */
void
clean_mtab(char *hostp, char *dirp, int verbose)
{
	struct mtablist *mtabp;
	char *host;

	/* Copy hostp in case it points to an entry that we are zeroing out. */
	host = strdup(hostp);
	for (mtabp = mtabhead; mtabp != NULL; mtabp = mtabp->mtab_next) {
		if (strcmp(mtabp->mtab_host, host) != 0)
			continue;
		if (dirp != NULL && strcmp(mtabp->mtab_dirp, dirp) != 0)
			continue;

		if (verbose)
			warnx("delete mounttab entry%s %s:%s",
			    (dirp == NULL) ? " by host" : "",
			    mtabp->mtab_host, mtabp->mtab_dirp);
		bzero(mtabp->mtab_host, MNTNAMLEN);
	}
	free(host);
}

/*
 * Free struct mtablist mtab.
 */
void
free_mtab(void)
{
	struct mtablist *mtabp;

	while ((mtabp = mtabhead) != NULL) {
		mtabhead = mtabhead->mtab_next;
		free(mtabp);
	}
}

/*
 * Print bad lines to syslog.
 */
static void
badline(const char *field, const char *bad)
{
	syslog(LOG_ERR, "bad mounttab %s field '%s'", field,
	    (bad == NULL) ? "<null>" : bad);
}
