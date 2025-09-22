/*	$OpenBSD: kvm_mkdb.c,v 1.33 2021/10/24 21:24:18 deraadt Exp $	*/

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
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>

#include "extern.h"

__dead void usage(void);
int kvm_mkdb(int, const char *, char *, char *, gid_t, int);

HASHINFO openinfo = {
	4096,		/* bsize */
	128,		/* ffactor */
	1024,		/* nelem */
	2048 * 1024,	/* cachesize */
	NULL,		/* hash() */
	0		/* lorder */
};

int
main(int argc, char *argv[])
{
	struct rlimit rl;
	struct group *gr;
	gid_t kvm_gid = -1;
	int fd, rval, ch, verbose = 0;
	char *nlistpath, *nlistname;
	char dbdir[PATH_MAX];

	if (pledge("stdio rpath wpath cpath fattr getpw flock id unveil", NULL) == -1)
		err(1, "pledge");

	/* Try to use the kmem group to be able to fchown() in kvm_mkdb(). */
	if ((gr = getgrnam("kmem")) == NULL) {
		warn("can't find kmem group");
	} else {
		kvm_gid = gr->gr_gid;
		if (setresgid(kvm_gid, kvm_gid, kvm_gid) == -1)
			err(1, "setresgid");
	}

	/* Increase our data size to the max if we can. */
	if (getrlimit(RLIMIT_DATA, &rl) == 0) {
		rl.rlim_cur = rl.rlim_max;
		if (setrlimit(RLIMIT_DATA, &rl) == -1)
			warn("can't set rlimit data size");
	}

	if (pledge("stdio rpath wpath cpath fattr flock unveil", NULL) == -1)
		err(1, "pledge");

	strlcpy(dbdir, _PATH_VARDB, sizeof(dbdir));
	while ((ch = getopt(argc, argv, "vo:")) != -1)
		switch (ch) {
		case 'v':
			verbose = 1;
			break;
		case 'o':
			rval = strlcpy(dbdir, optarg, sizeof(dbdir));
			if (rval == 0 || rval + 1 >= sizeof(dbdir))
				errx(1, "Invalid directory");
			/* Make sure there is a '/' at the end of the path */
			if (dbdir[strlen(dbdir) - 1] != '/')
				strlcat(dbdir, "/", sizeof(dbdir));
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	if (argc > 0) {
		if (unveil(argv[0], "r") == -1)
			err(1, "unveil %s", argv[0]);
	} else {
		if (unveil(_PATH_UNIX, "r") == -1)
			err(1, "unveil %s", _PATH_UNIX);
		if (unveil(_PATH_KSYMS, "r") == -1)
			err(1, "unveil %s", _PATH_KSYMS);
	}
	if (unveil(dbdir, "rwc") == -1)
		err(1, "unveil %s", dbdir);
	if (pledge("stdio rpath wpath cpath fattr flock", NULL) == -1)
		err(1, "pledge");

	/* If no kernel specified use _PATH_KSYMS and fall back to _PATH_UNIX */
	if (argc > 0) {
		nlistpath = argv[0];
		nlistname = basename(nlistpath);
		if ((fd = open(nlistpath, O_RDONLY)) == -1)
			err(1, "can't open %s", nlistpath);
		rval = kvm_mkdb(fd, dbdir, nlistpath, nlistname, kvm_gid,
		    verbose);
	} else {
		nlistname = basename(_PATH_UNIX);
		if ((fd = open((nlistpath = _PATH_KSYMS), O_RDONLY)) == -1 ||
		    (rval = kvm_mkdb(fd, dbdir, nlistpath, nlistname, kvm_gid,
		    verbose)) != 0) {
			if (fd == -1) 
				warnx("can't open %s", _PATH_KSYMS);
			else
				warnx("will try again using %s instead", _PATH_UNIX);
			if ((fd = open((nlistpath = _PATH_UNIX), O_RDONLY)) == -1)
				err(1, "can't open %s", nlistpath);
			rval = kvm_mkdb(fd, dbdir, nlistpath, nlistname,
			    kvm_gid, verbose);
		}
	}
	exit(rval);
}

int
kvm_mkdb(int fd, const char *dbdir, char *nlistpath, char *nlistname, gid_t gid, 
    int verbose)
{
	DB *db;
	char dbtemp[PATH_MAX], dbname[PATH_MAX];
	int r;

	r = snprintf(dbtemp, sizeof(dbtemp), "%skvm_%s.tmp",
	    dbdir, nlistname);
	if (r < 0 || r >= sizeof(dbtemp)) {
		warnx("Directory name too long");
		return (1);
	}
	r = snprintf(dbname, sizeof(dbname), "%skvm_%s.db",
	    dbdir, nlistname);
	if (r < 0 || r >= sizeof(dbtemp)) {
		warnx("Directory name too long");
		return (1);
	}

	/* If the existing db file matches the currently running kernel, exit */
	if (testdb(dbname)) {
		if (verbose)
			warnx("%s already up to date", dbname);
		return(0);
	} else if (verbose)
		warnx("rebuilding %s", dbname);

	(void)umask(0);
	db = dbopen(dbtemp, O_CREAT | O_EXLOCK | O_TRUNC | O_RDWR,
	    S_IRUSR | S_IWUSR | S_IRGRP, DB_HASH, &openinfo);
	if (db == NULL) {
		warn("can't dbopen %s", dbtemp);
		return(1);
	}

	if (gid != -1 && fchown(db->fd(db), -1, gid) == -1) {
		warn("can't chown %s", dbtemp);
		(void)unlink(dbtemp);
		return(1);
	}

	if (create_knlist(nlistpath, fd, db) != 0) {
		warn("cannot determine executable type of %s", nlistpath);
		(void)unlink(dbtemp);
		return(1);
	}
	if (db->close(db)) {
		warn("can't dbclose %s", dbtemp);
		(void)unlink(dbtemp);
		return(1);
	}

	if (rename(dbtemp, dbname)) {
		warn("rename %s to %s", dbtemp, dbname);
		(void)unlink(dbtemp);
		return(1);
	}

	return(0);
}

__dead void
usage(void)
{
	(void)fprintf(stderr, "usage: kvm_mkdb [-v] [-o directory] [file]\n");
	exit(1);
}
