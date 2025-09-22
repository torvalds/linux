/*	$OpenBSD: database.c,v 1.38 2019/06/28 13:32:47 deraadt Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <bitstring.h>		/* for structs.h */
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>		/* for structs.h */
#include <unistd.h>

#include "pathnames.h"
#include "globals.h"
#include "macros.h"
#include "structs.h"
#include "funcs.h"

#define HASH(a,b) ((a)+(b))

static	void		process_crontab(int, const char *, const char *,
					struct stat *, cron_db *, cron_db *);

void
load_database(cron_db **db)
{
	struct stat statbuf, syscron_stat;
	cron_db *new_db, *old_db = *db;
	struct timespec mtime;
	struct dirent *dp;
	DIR *dir;
	user *u;

	/* before we start loading any data, do a stat on _PATH_CRON_SPOOL
	 * so that if anything changes as of this moment (i.e., before we've
	 * cached any of the database), we'll see the changes next time.
	 */
	if (stat(_PATH_CRON_SPOOL, &statbuf) == -1) {
		syslog(LOG_ERR, "(CRON) STAT FAILED (%s)", _PATH_CRON_SPOOL);
		return;
	}

	/* track system crontab file
	 */
	if (stat(_PATH_SYS_CRONTAB, &syscron_stat) == -1)
		timespecclear(&syscron_stat.st_mtim);

	/* hash mtime of system crontab file and crontab dir
	 */
	mtime.tv_sec =
	    HASH(statbuf.st_mtim.tv_sec, syscron_stat.st_mtim.tv_sec);
	mtime.tv_nsec =
	    HASH(statbuf.st_mtim.tv_nsec, syscron_stat.st_mtim.tv_nsec);

	/* if spooldir's mtime has not changed, we don't need to fiddle with
	 * the database.
	 */
	if (old_db != NULL && timespeccmp(&mtime, &old_db->mtime, ==))
		return;

	/* something's different.  make a new database, moving unchanged
	 * elements from the old database, reloading elements that have
	 * actually changed.  Whatever is left in the old database when
	 * we're done is chaff -- crontabs that disappeared.
	 */
	if ((new_db = malloc(sizeof(*new_db))) == NULL)
		return;
	new_db->mtime = mtime;
	TAILQ_INIT(&new_db->users);

	if (timespecisset(&syscron_stat.st_mtim)) {
		process_crontab(AT_FDCWD, "*system*", _PATH_SYS_CRONTAB,
				&syscron_stat, new_db, old_db);
	}

	/* we used to keep this dir open all the time, for the sake of
	 * efficiency.  however, we need to close it in every fork, and
	 * we fork a lot more often than the mtime of the dir changes.
	 */
	if (!(dir = opendir(_PATH_CRON_SPOOL))) {
		syslog(LOG_ERR, "(CRON) OPENDIR FAILED (%s)", _PATH_CRON_SPOOL);
		/* Restore system crontab entry as needed. */
		if (!TAILQ_EMPTY(&new_db->users) &&
		    (u = TAILQ_FIRST(&old_db->users))) {
			if (strcmp(u->name, "*system*") == 0) {
				TAILQ_REMOVE(&old_db->users, u, entries);
				free_user(u);
				TAILQ_INSERT_HEAD(&old_db->users,
				    TAILQ_FIRST(&new_db->users), entries);
			}
		}
		free(new_db);
		return;
	}

	while (NULL != (dp = readdir(dir))) {
		/* avoid file names beginning with ".".  this is good
		 * because we would otherwise waste two guaranteed calls
		 * to getpwnam() for . and .., and also because user names
		 * starting with a period are just too nasty to consider.
		 */
		if (dp->d_name[0] == '.')
			continue;

		process_crontab(dirfd(dir), dp->d_name, dp->d_name,
				&statbuf, new_db, old_db);
	}
	closedir(dir);

	/* if we don't do this, then when our children eventually call
	 * getpwnam() in do_command.c's child_process to verify MAILTO=,
	 * they will screw us up (and v-v).
	 */
	endpwent();

	/* whatever's left in the old database is now junk.
	 */
	if (old_db != NULL) {
		while ((u = TAILQ_FIRST(&old_db->users))) {
			TAILQ_REMOVE(&old_db->users, u, entries);
			free_user(u);
		}
		free(old_db);
	}

	/* overwrite the database control block with the new one.
	 */
	*db = new_db;
}

user *
find_user(cron_db *db, const char *name)
{
	user *u = NULL;

	if (db != NULL) {
		TAILQ_FOREACH(u, &db->users, entries) {
			if (strcmp(u->name, name) == 0)
				break;
		}
	}
	return (u);
}

static void
process_crontab(int dfd, const char *uname, const char *fname,
		struct stat *statbuf, cron_db *new_db, cron_db *old_db)
{
	struct passwd *pw = NULL;
	FILE *crontab_fp = NULL;
	user *u, *new_u;
	mode_t tabmask, tabperm;
	int fd;

	/* Note: pw must remain NULL for system crontab (see below). */
	if (fname[0] != '/' && (pw = getpwnam(uname)) == NULL) {
		/* file doesn't have a user in passwd file.
		 */
		syslog(LOG_WARNING, "(%s) ORPHAN (no passwd entry)", uname);
		goto next_crontab;
	}

	fd = openat(dfd, fname, O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
	if (fd == -1) {
		/* crontab not accessible?
		 */
		syslog(LOG_ERR, "(%s) CAN'T OPEN (%s)", uname, fname);
		goto next_crontab;
	}
	if (!(crontab_fp = fdopen(fd, "r"))) {
		syslog(LOG_ERR, "(%s) FDOPEN (%m)", fname);
		close(fd);
		goto next_crontab;
	}

	if (fstat(fileno(crontab_fp), statbuf) == -1) {
		syslog(LOG_ERR, "(%s) FSTAT FAILED (%s)", uname, fname);
		goto next_crontab;
	}
	if (!S_ISREG(statbuf->st_mode)) {
		syslog(LOG_WARNING, "(%s) NOT REGULAR (%s)", uname, fname);
		goto next_crontab;
	}
	/* Looser permissions on system crontab. */
	tabmask = pw ? ALLPERMS : (ALLPERMS & ~(S_IWUSR|S_IRGRP|S_IROTH));
	tabperm = pw ? (S_IRUSR|S_IWUSR) : S_IRUSR;
	if ((statbuf->st_mode & tabmask) != tabperm) {
		syslog(LOG_WARNING, "(%s) BAD FILE MODE (%s)", uname, fname);
		goto next_crontab;
	}
	if (statbuf->st_uid != 0 && (pw == NULL ||
	    statbuf->st_uid != pw->pw_uid || strcmp(uname, pw->pw_name) != 0)) {
		syslog(LOG_WARNING, "(%s) WRONG FILE OWNER (%s)", uname, fname);
		goto next_crontab;
	}
	if (pw != NULL && statbuf->st_gid != cron_gid) {
		syslog(LOG_WARNING, "(%s) WRONG FILE GROUP (%s)", uname, fname);
		goto next_crontab;
	}
	if (pw != NULL && statbuf->st_nlink != 1) {
		syslog(LOG_WARNING, "(%s) BAD LINK COUNT (%s)", uname, fname);
		goto next_crontab;
	}

	u = find_user(old_db, fname);
	if (u != NULL) {
		/* if crontab has not changed since we last read it
		 * in, then we can just use our existing entry.
		 */
		if (timespeccmp(&u->mtime, &statbuf->st_mtim, ==)) {
			TAILQ_REMOVE(&old_db->users, u, entries);
			TAILQ_INSERT_TAIL(&new_db->users, u, entries);
			goto next_crontab;
		}
		syslog(LOG_INFO, "(%s) RELOAD (%s)", uname, fname);
	}

	new_u = load_user(crontab_fp, pw, fname);
	if (new_u != NULL) {
		/* Insert user into the new database and remove from old. */
		new_u->mtime = statbuf->st_mtim;
		TAILQ_INSERT_TAIL(&new_db->users, new_u, entries);
		if (u != NULL) {
			TAILQ_REMOVE(&old_db->users, u, entries);
			free_user(u);
		}
	} else if (u != NULL) {
		/* New user crontab failed to load, preserve the old one. */
		TAILQ_REMOVE(&old_db->users, u, entries);
		TAILQ_INSERT_TAIL(&new_db->users, u, entries);
	}

 next_crontab:
	if (crontab_fp != NULL) {
		fclose(crontab_fp);
	}
}
