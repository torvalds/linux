/*	$OpenBSD: devname.c,v 1.14 2024/01/22 17:22:58 deraadt Exp $ */
/*
 * Copyright (c) 1989, 1993
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

#include <sys/stat.h>
#include <sys/types.h>

#include <db.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

static char *
devname_nodb(dev_t dev, mode_t type)
{
	static char buf[NAME_MAX + 1];
	char *name = NULL;
	struct dirent *dp;
	struct stat sb;
	DIR *dirp;

	if ((dirp = opendir(_PATH_DEV)) == NULL)
		return (NULL);
	while ((dp = readdir(dirp)) != NULL) {
		if (dp->d_type != DT_UNKNOWN && DTTOIF(dp->d_type) != type)
			continue;
		if (fstatat(dirfd(dirp), dp->d_name, &sb, AT_SYMLINK_NOFOLLOW)
		    || sb.st_rdev != dev || (sb.st_mode & S_IFMT) != type)
			continue;
		strlcpy(buf, dp->d_name, sizeof(buf));
		name = buf;
		break;
	}
	closedir(dirp);
	return (name);
}

/*
 * Keys in dev.db are a mode_t followed by a dev_t.  The former is the
 * type of the file (mode & S_IFMT), the latter is the st_rdev field.
 * Note that the structure may contain padding.
 */
char *
devname(dev_t dev, mode_t type)
{
	static DB *db;
	static bool failure;
	struct {
		mode_t type;
		dev_t dev;
	} bkey;
	DBT data, key;
	char *name = NULL;

	if (!db && !failure) {
		if (!(db = __hash_open(_PATH_DEVDB, O_RDONLY, 0, NULL, 0)))
			failure = true;
	}
	if (!failure) {
		/* Be sure to clear any padding that may be found in bkey.  */
		memset(&bkey, 0, sizeof(bkey));
		bkey.dev = dev;
		bkey.type = type;
		key.data = &bkey;
		key.size = sizeof(bkey);
		if ((db->get)(db, &key, &data, 0) == 0)
			name = data.data;
	} else {
		name = devname_nodb(dev, type);
	}
	return (name ? name : "??");
}
DEF_WEAK(devname);
