/*	$OpenBSD: dev_mkdb.c,v 1.20 2023/12/24 06:35:05 gnezdo Exp $	*/

/*-
 * Copyright (c) 1990, 1993
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

#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void	usage(void);

int
main(int argc, char *argv[])
{
	FTS *fts;
	FTSENT *dp;
	char *paths[] = { ".", NULL };
	struct {
		mode_t type;
		dev_t dev;
	} bkey;
	DB *db;
	DBT data, key;
	HASHINFO info;
	int ch;
	char dbtmp[PATH_MAX], dbname[PATH_MAX];

	(void)snprintf(dbtmp, sizeof(dbtmp), "%sdev.tmp", _PATH_VARRUN);
	(void)snprintf(dbname, sizeof(dbname), "%sdev.db", _PATH_VARRUN);

	if (unveil(_PATH_DEV, "r") == -1)
		err(1, "unveil %s", _PATH_DEV);
	if (unveil(dbtmp, "rwc") == -1)
		err(1, "unveil %s", dbtmp);
	if (unveil(dbname, "wc") == -1)
		err(1, "unveil %s", dbname);
	if (pledge("stdio rpath wpath cpath flock", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "")) != -1)
		switch(ch) {
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage();

	if (chdir(_PATH_DEV))
		err(1, "%s", _PATH_DEV);

	fts = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
	if (!fts)
		err(1, "fts_open");


	bzero(&info, sizeof(info));
	info.bsize = 8192;
	db = dbopen(dbtmp, O_CREAT|O_EXLOCK|O_RDWR|O_TRUNC,
	    S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, DB_HASH, &info);
	if (db == NULL)
		err(1, "%s", dbtmp);

	/*
	 * Keys are a mode_t followed by a dev_t.  The former is the type of
	 * the file (mode & S_IFMT), the latter is the st_rdev field.  Note
	 * that the structure may contain padding, so we have to clear it
	 * out here.
	 */
	bzero(&bkey, sizeof(bkey));
	key.data = &bkey;
	key.size = sizeof(bkey);
	while ((dp = fts_read(fts))) {
		if (dp->fts_info != FTS_DEFAULT)
			continue;

		/* Create the key. */
		if (S_ISCHR(dp->fts_statp->st_mode))
			bkey.type = S_IFCHR;
		else if (S_ISBLK(dp->fts_statp->st_mode))
			bkey.type = S_IFBLK;
		else
			continue;
		bkey.dev = dp->fts_statp->st_rdev;

		/*
		 * Create the data; nul terminate the name so caller doesn't
		 * have to. strlen("./") is 2, which is stripped to remove the
		 * traversal root name.
		 */
		data.data = dp->fts_path + 2;
		data.size = dp->fts_pathlen - 2 + 1;
		if ((db->put)(db, &key, &data, 0))
			err(1, "dbput %s", dbtmp);
	}
	fts_close(fts);

	(void)(db->close)(db);
	if (rename(dbtmp, dbname))
		err(1, "rename %s to %s", dbtmp, dbname);

	return (0);
}

void
usage(void)
{

	(void)fprintf(stderr, "usage: dev_mkdb\n");
	exit(1);
}
