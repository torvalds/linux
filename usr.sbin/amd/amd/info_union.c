/*
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)info_union.c	8.1 (Berkeley) 6/6/93
 *	$Id: info_union.c,v 1.9 2014/10/26 03:03:34 guenther Exp $
 */

/*
 * Get info from the system namespace
 *
 * NOTE: Cannot handle reads back through the automounter.
 * THIS WILL CAUSE A DEADLOCK!
 */

#include "am.h"


#include <dirent.h>
#define	DIRENT struct dirent

#define	UNION_PREFIX	"union:"
#define	UNION_PREFLEN	6

/*
 * No way to probe - check the map name begins with "union:"
 */
int
union_init(char *map, time_t *tp)
{
	*tp = 0;
	return strncmp(map, UNION_PREFIX, UNION_PREFLEN) == 0 ? 0 : ENOENT;
}

int
union_search(mnt_map *m, char *map, char *key, char **pval, time_t *tp)
{
	char *mapd = strdup(map + UNION_PREFLEN);
	char **v = strsplit(mapd, ':', '\"');
	char **p;

	for (p = v; p[1]; p++)
		;
	*pval = xmalloc(strlen(*p) + 5);
	snprintf(*pval, strlen(*p) + 5, "fs:=%s", *p);
	free(mapd);
	free(v);
	return 0;
}

int
union_reload(mnt_map *m, char *map, void (*fn)(mnt_map *, char *, char *))
{
	char *mapd = strdup(map + UNION_PREFLEN);
	char **v = strsplit(mapd, ':', '\"');
	char **dir;

	/*
	 * Add fake /defaults entry
	 */
	(*fn)(m, strdup("/defaults"),
	    strdup("type:=link;opts:=nounmount;sublink:=${key}"));

	for (dir = v; *dir; dir++) {
		DIR *dirp = opendir(*dir);
		DIRENT *dp;
		int dlen;

		if (!dirp) {
			plog(XLOG_USER, "Cannot read directory %s: %m", *dir);
			continue;
		}
		dlen = strlen(*dir);
#ifdef DEBUG
		dlog("Reading directory %s...", *dir);
#endif
		while ((dp = readdir(dirp))) {
			char *val;

			if (dp->d_name[0] == '.' &&
			    (dp->d_name[1] == '\0' ||
			    (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
				continue;

#ifdef DEBUG
			dlog("... gives %s", dp->d_name);
#endif
			val = xmalloc(dlen + 5);
			snprintf(val, dlen + 5, "fs:=%s", *dir);
			(*fn)(m, strdup(dp->d_name), val);
		}
		closedir(dirp);
	}

	/*
	 * Add wildcard entry
	 */
	{
		char *val = xmalloc(strlen(dir[-1]) + 5);

		snprintf(val, strlen(dir[-1]) + 5, "fs:=%s", dir[-1]);
		(*fn)(m, strdup("*"), val);
	}
	free(mapd);
	free(v);
	return 0;
}

