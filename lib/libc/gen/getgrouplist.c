/*	$OpenBSD: getgrouplist.c,v 1.31 2024/11/04 21:49:26 jca Exp $ */
/*
 * Copyright (c) 2008 Ingo Schwarze <schwarze@usta.de>
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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

/*
 * get credential
 */
#include <sys/types.h>
#include <sys/limits.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>

#include <rpc/rpc.h>
#include <rpcsvc/yp.h>
#include <rpcsvc/ypclnt.h>

#ifdef YP
#define _PATH_NETID	"/etc/netid"
#define MAXLINELENGTH	1024

static int _parse_netid(char*, uid_t, gid_t*, int*, int);
static int _read_netid(const char *, uid_t, gid_t*, int*, int);

/*
 * Parse one string of the form "uid:gid[,gid[,...]]".
 * If the uid matches, add the groups to the group list and return 1.
 * If the uid does not match, return 0.
 */
static int
_parse_netid(char *netid, uid_t uid, gid_t *groups, int *ngroups,
	     int maxgroups)
{
	const char *errstr = NULL;
	char *start, *p;
	uid_t tuid;
	gid_t gid;
	int i;

	/* Check the uid. */
	p = strchr(netid, ':');
	if (!p)
		return (0);
	*p++ = '\0';
	tuid = (uid_t)strtonum(netid, 0, UID_MAX, &errstr);
	if (errstr || tuid != uid)
		return (0);

        /* Loop over the gids. */
	while (p && *p) {
		start = p;
		p = strchr(start, ',');
		if (p)
			*p++ = '\0';
		gid = (gid_t)strtonum(start, 0, GID_MAX, &errstr);
		if (errstr)
			continue;

		/* Skip this group if it is already in the list. */
		for (i = 0; i < maxgroups && i < *ngroups; i++)
			if (groups[i] == gid)
				break;

		/* Try to add this new group to the list. */
		if (i == *ngroups) {
			if (*ngroups >= maxgroups)
				(*ngroups)++;
			else
				groups[(*ngroups)++] = gid;
		}
	}
	return (1);
}

/*
 * Search /etc/netid for a particular uid and process that line.
 * See _parse_netid for details, including return values.
 */
static int
_read_netid(const char *key, uid_t uid, gid_t *groups, int *ngroups,
	    int maxgroups)
{
	FILE *fp;
	char line[MAXLINELENGTH], *p;
	int found = 0;

	fp = fopen(_PATH_NETID, "re");
	if (!fp) 
		return (0);
	while (!found && fgets(line, sizeof(line), fp)) {
		p = strchr(line, '\n');
		if (p)
			*p = '\0';
		else { /* Skip lines that are too long. */
			int ch;
			while ((ch = getc_unlocked(fp)) != '\n' && ch != EOF)
				;
			continue;
		}
		p = strchr(line, ' ');
		if (!p)
			continue;
		*p++ = '\0';
		if (strcmp(line, key))
			continue;
		found = _parse_netid(p, uid, groups, ngroups, maxgroups);
	}
	(void)fclose(fp);
	return (found);
}
#endif /* YP */

int
getgrouplist(const char *uname, gid_t agroup, gid_t *groups, int *grpcnt)
{
	int i, ngroups = 0, maxgroups = *grpcnt, bail;
	int needyp = 0, foundyp = 0;
	int *skipyp = &foundyp;
	extern struct group *_getgrent_yp(int *);
	struct group *grp;

	/*
	 * install primary group
	 */
	if (ngroups >= maxgroups)
		ngroups++;
	else
		groups[ngroups++] = agroup;

	/*
	 * Scan the group file to find additional groups.
	 */
	setgrent();
	while ((grp = _getgrent_yp(skipyp)) || foundyp) {
		if (foundyp) {
			if (foundyp > 0)
				needyp = 1;
			else
				skipyp = NULL;
			foundyp = 0;
			continue;
		}
		if (grp->gr_gid == agroup)
			continue;
		for (bail = 0, i = 0; bail == 0 && i < maxgroups &&
		    i < ngroups; i++) {
			if (groups[i] == grp->gr_gid)
				bail = 1;
		}
		if (bail)
			continue;
		for (i = 0; grp->gr_mem[i]; i++) {
			if (!strcmp(grp->gr_mem[i], uname)) {
				if (ngroups >= maxgroups)
					ngroups++;
				else
					groups[ngroups++] = grp->gr_gid;
				break;
			}
		}
	}

#ifdef YP
	/*
	 * If we were told that there is a YP marker, look at netid data.
	 */
	if (skipyp && needyp) {
		char buf[MAXLINELENGTH], *ypdata = NULL, *key;
		static char *__ypdomain;
		struct passwd pwstore;
		int ypdatalen;

		/* Construct the netid key to look up. */
		if (getpwnam_r(uname, &pwstore, buf, sizeof buf, NULL) ||
		    (!__ypdomain && yp_get_default_domain(&__ypdomain)))
			goto out;
		i = asprintf(&key, "unix.%u@%s", pwstore.pw_uid, __ypdomain);
		if (i == -1)
			goto out;

		/* First scan the static netid file. */
		if (_read_netid(key, pwstore.pw_uid, groups, &ngroups,
		    maxgroups)) {
			free(key);
			goto out;
		}

		/* Only access YP when there is no static entry. */
		if (!yp_match(__ypdomain, "netid.byname", key,
		    (int)strlen(key), &ypdata, &ypdatalen))
			_parse_netid(ypdata, pwstore.pw_uid, groups, &ngroups,
			    maxgroups);

		free(key);
		free(ypdata);
	}
#endif /* YP */

out:
	endgrent();
	*grpcnt = ngroups;
	return (ngroups > maxgroups ? -1 : 0);
}
DEF_WEAK(getgrouplist);
