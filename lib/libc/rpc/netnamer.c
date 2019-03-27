/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2009, Sun Microsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice, 
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 * - Neither the name of Sun Microsystems, Inc. nor the names of its 
 *   contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)netnamer.c 1.13 91/03/11 Copyr 1986 Sun Micro";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * netname utility routines convert from unix names to network names and
 * vice-versa This module is operating system dependent! What we define here
 * will work with any unix system that has adopted the sun NIS domain
 * architecture.
 */
#include "namespace.h"
#include <sys/param.h>
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#ifdef YP
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <grp.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "un-namespace.h"

static char    *OPSYS = "unix";
#ifdef YP
static char    *NETID = "netid.byname";
#endif
static char    *NETIDFILE = "/etc/netid";

static int getnetid( char *, char * );
static int _getgroups( char *, gid_t * );

/*
 * Convert network-name into unix credential
 */
int
netname2user(char netname[MAXNETNAMELEN + 1], uid_t *uidp, gid_t *gidp,
    int *gidlenp, gid_t *gidlist)
{
	char           *p;
	int             gidlen;
	uid_t           uid;
	long		luid;
	struct passwd  *pwd;
	char            val[1024];
	char           *val1, *val2;
	char           *domain;
	int             vallen;
	int             err;

	if (getnetid(netname, val)) {
		char *res = val;

		p = strsep(&res, ":");
		if (p == NULL)
			return (0);
		*uidp = (uid_t) atol(p);
		p = strsep(&res, "\n,");
		if (p == NULL) {
			return (0);
		}
		*gidp = (gid_t) atol(p);
		for (gidlen = 0; gidlen < NGRPS; gidlen++) {
			p = strsep(&res, "\n,");
			if (p == NULL)
				break;
			gidlist[gidlen] = (gid_t) atol(p);
		}
		*gidlenp = gidlen;

		return (1);
	}
	val1 = strchr(netname, '.');
	if (val1 == NULL)
		return (0);
	if (strncmp(netname, OPSYS, (val1-netname)))
		return (0);
	val1++;
	val2 = strchr(val1, '@');
	if (val2 == NULL)
		return (0);
	vallen = val2 - val1;
	if (vallen > (1024 - 1))
		vallen = 1024 - 1;
	(void) strncpy(val, val1, 1024);
	val[vallen] = 0;

	err = __rpc_get_default_domain(&domain);	/* change to rpc */
	if (err)
		return (0);

	if (strcmp(val2 + 1, domain))
		return (0);	/* wrong domain */

	if (sscanf(val, "%ld", &luid) != 1)
		return (0);
	uid = luid;

	/* use initgroups method */
	pwd = getpwuid(uid);
	if (pwd == NULL)
		return (0);
	*uidp = pwd->pw_uid;
	*gidp = pwd->pw_gid;
	*gidlenp = _getgroups(pwd->pw_name, gidlist);
	return (1);
}

/*
 * initgroups
 */

static int
_getgroups(char *uname, gid_t groups[NGRPS])
{
	gid_t           ngroups = 0;
	struct group *grp;
	int    i;
	int    j;
	int             filter;

	setgrent();
	while ((grp = getgrent())) {
		for (i = 0; grp->gr_mem[i]; i++)
			if (!strcmp(grp->gr_mem[i], uname)) {
				if (ngroups == NGRPS) {
#ifdef DEBUG
					fprintf(stderr,
				"initgroups: %s is in too many groups\n", uname);
#endif
					goto toomany;
				}
				/* filter out duplicate group entries */
				filter = 0;
				for (j = 0; j < ngroups; j++)
					if (groups[j] == grp->gr_gid) {
						filter++;
						break;
					}
				if (!filter)
					groups[ngroups++] = grp->gr_gid;
			}
	}
toomany:
	endgrent();
	return (ngroups);
}

/*
 * Convert network-name to hostname
 */
int
netname2host(char netname[MAXNETNAMELEN + 1], char *hostname, int hostlen)
{
	int             err;
	char            valbuf[1024];
	char           *val;
	char           *val2;
	int             vallen;
	char           *domain;

	if (getnetid(netname, valbuf)) {
		val = valbuf;
		if ((*val == '0') && (val[1] == ':')) {
			(void) strncpy(hostname, val + 2, hostlen);
			return (1);
		}
	}
	val = strchr(netname, '.');
	if (val == NULL)
		return (0);
	if (strncmp(netname, OPSYS, (val - netname)))
		return (0);
	val++;
	val2 = strchr(val, '@');
	if (val2 == NULL)
		return (0);
	vallen = val2 - val;
	if (vallen > (hostlen - 1))
		vallen = hostlen - 1;
	(void) strncpy(hostname, val, vallen);
	hostname[vallen] = 0;

	err = __rpc_get_default_domain(&domain);	/* change to rpc */
	if (err)
		return (0);

	if (strcmp(val2 + 1, domain))
		return (0);	/* wrong domain */
	else
		return (1);
}

/*
 * reads the file /etc/netid looking for a + to optionally go to the
 * network information service.
 */
int
getnetid(char *key, char *ret)
{
	char            buf[1024];	/* big enough */
	char           *res;
	char           *mkey;
	char           *mval;
	FILE           *fd;
#ifdef YP
	char           *domain;
	int             err;
	char           *lookup;
	int             len;
#endif
	int rv;

	rv = 0;

	fd = fopen(NETIDFILE, "r");
	if (fd == NULL) {
#ifdef YP
		res = "+";
		goto getnetidyp;
#else
		return (0);
#endif
	}
	while (fd != NULL) {
		res = fgets(buf, sizeof(buf), fd);
		if (res == NULL) {
			rv = 0;
			goto done;
		}
		if (res[0] == '#')
			continue;
		else if (res[0] == '+') {
#ifdef YP
	getnetidyp:
			err = yp_get_default_domain(&domain);
			if (err) {
				continue;
			}
			lookup = NULL;
			err = yp_match(domain, NETID, key,
				strlen(key), &lookup, &len);
			if (err) {
#ifdef DEBUG
				fprintf(stderr, "match failed error %d\n", err);
#endif
				continue;
			}
			lookup[len] = 0;
			strcpy(ret, lookup);
			free(lookup);
			rv = 2;
			goto done;
#else	/* YP */
#ifdef DEBUG
			fprintf(stderr,
"Bad record in %s '+' -- NIS not supported in this library copy\n",
				NETIDFILE);
#endif
			continue;
#endif	/* YP */
		} else {
			mkey = strsep(&res, "\t ");
			if (mkey == NULL) {
				fprintf(stderr,
		"Bad record in %s -- %s", NETIDFILE, buf);
				continue;
			}
			do {
				mval = strsep(&res, " \t#\n");
			} while (mval != NULL && !*mval);
			if (mval == NULL) {
				fprintf(stderr,
		"Bad record in %s val problem - %s", NETIDFILE, buf);
				continue;
			}
			if (strcmp(mkey, key) == 0) {
				strcpy(ret, mval);
				rv = 1;
				goto done;
			}
		}
	}

done:
	if (fd != NULL)
		fclose(fd);
	return (rv);
}
