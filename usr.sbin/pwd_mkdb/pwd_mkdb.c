/*	$OpenBSD: pwd_mkdb.c,v 1.62 2024/07/28 19:13:26 millert Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Portions Copyright (c) 1994, Jason Downs.  All rights reserved.
 * Portions Copyright (c) 1998, Todd C. Miller.  All rights reserved.
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

#include <sys/types.h>
#include <sys/stat.h>

#include <db.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))
#define _MAXBSIZE	(64 * 1024)

#define	INSECURE	1
#define	SECURE		2
#define	PERM_INSECURE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define	PERM_SECURE	(S_IRUSR|S_IWUSR)

#define FILE_SECURE	0x01
#define FILE_INSECURE	0x02
#define FILE_ORIG	0x04

#define	SHADOW_GROUP	"_shadow"

HASHINFO openinfo = {
	4096,		/* bsize */
	32,		/* ffactor */
	256,		/* nelem */
	2048 * 1024,	/* cachesize */
	NULL,		/* hash() */
	0		/* lorder */
};

static char *pname;				/* password file name */
static char *basedir;				/* dir holding master.passwd */
static int clean;				/* what to remove on cleanup */
static int hasyp;				/* are we running YP? */

void		cleanup(void);
__dead void	fatal(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
__dead void	fatalc(int, const char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
__dead void	fatalx(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
int		write_old_entry(FILE *, const struct passwd *);

void		cp(char *, char *, mode_t);
void		mv(char *, char *);
int		scan(FILE *, struct passwd *, int *);
void		usage(void);
char		*changedir(char *path, char *dir);
void		db_store(FILE *, FILE *, DB *, DB *,struct passwd *, int, char *, uid_t);

int
main(int argc, char **argv)
{
	DB *dp, *edp;
	DBT data, key;
	FILE *fp, *oldfp = NULL;
	struct stat st;
	struct passwd pwd;
	struct group *grp;
	sigset_t set;
	uid_t olduid;
	gid_t shadow;
	int ch, tfd, makeold, secureonly, flags, checkonly;
	char *username, buf[MAXIMUM(PATH_MAX, LINE_MAX * 2)];

	flags = checkonly = makeold = secureonly = 0;
	username = NULL;
	while ((ch = getopt(argc, argv, "cd:psu:v")) != -1)
		switch (ch) {
		case 'c':			/* verify only */
			checkonly = 1;
			break;
		case 'd':
			basedir = optarg;
			if (strlen(basedir) > PATH_MAX - 40)
				errx(1, "basedir too long");
			break;
		case 'p':			/* create V7 "file.orig" */
			makeold = 1;
			break;
		case 's':			/* only update spwd.db */
			secureonly = 1;
			break;
		case 'u':			/* only update this record */
			username = optarg;
			if (strlen(username) > _PW_NAME_LEN)
				errx(1, "username too long");
			break;
		case 'v':			/* backward compatible */
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 1 || (makeold && secureonly) ||
	    (username && (*username == '+' || *username == '-')))
		usage();

	if ((grp = getgrnam(SHADOW_GROUP)) == NULL)
		errx(1, "cannot find `%s' in the group database, aborting",
		    SHADOW_GROUP);
	shadow = grp->gr_gid;

	/*
	 * This could be changed to allow the user to interrupt.
	 * Probably not worth the effort.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGTSTP);
	sigaddset(&set, SIGHUP);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGQUIT);
	sigaddset(&set, SIGTERM);
	(void)sigprocmask(SIG_BLOCK, &set, (sigset_t *)NULL);

	/* We don't care what the user wants. */
	(void)umask(0);

	if (**argv != '/' && basedir == NULL)
		errx(1, "%s must be specified as an absolute path", *argv);

	if ((pname = strdup(changedir(*argv, basedir))) == NULL)
		err(1, NULL);
	/* Open the original password file */
	if (!(fp = fopen(pname, "r")))
		fatal("%s", pname);

	/* Check only if password database is valid */
	if (checkonly) {
		u_int cnt;

		for (cnt = 1; scan(fp, &pwd, &flags); ++cnt)
			;
		exit(0);
	}

	if (fstat(fileno(fp), &st) == -1)
		fatal("%s", pname);

	/* Tweak openinfo values for large passwd files. */
	if (st.st_size > (off_t)100*1024)
		openinfo.cachesize = MINIMUM(st.st_size * 20, (off_t)12*1024*1024);
	/* Estimate number of elements based on a 128-byte average entry. */
	if (st.st_size / 128 * 3 > openinfo.nelem)
		openinfo.nelem = st.st_size / 128 * 3;

	/* If only updating a single record, stash the old uid */
	if (username) {
		dp = dbopen(_PATH_MP_DB, O_RDONLY, 0, DB_HASH, NULL);
		if (dp == NULL)
			fatal(_PATH_MP_DB);
		buf[0] = _PW_KEYBYNAME;
		strlcpy(buf + 1, username, sizeof(buf) - 1);
		key.data = (u_char *)buf;
		key.size = strlen(buf + 1) + 1;
		if ((dp->get)(dp, &key, &data, 0) == 0) {
			char *p = (char *)data.data;
			/* Skip to uid field */
			while (*p++ != '\0')
				;
			while (*p++ != '\0')
				;
			memcpy(&olduid, p, sizeof(olduid));
		} else
			olduid = -1;
		(dp->close)(dp);
	}

	/* Open the temporary encrypted password database. */
	(void)snprintf(buf, sizeof(buf), "%s.tmp",
	    changedir(_PATH_SMP_DB, basedir));
	if (username) {
		cp(changedir(_PATH_SMP_DB, basedir), buf, PERM_SECURE);
		edp = dbopen(buf,
		    O_RDWR, PERM_SECURE, DB_HASH, &openinfo);
	} else {
		edp = dbopen(buf,
		    O_RDWR|O_CREAT|O_EXCL, PERM_SECURE, DB_HASH, &openinfo);
	}
	if (!edp)
		fatal("%s", buf);
	if (fchown(edp->fd(edp), -1, shadow) != 0)
		warn("%s: unable to set group to %s", _PATH_SMP_DB,
		    SHADOW_GROUP);
	else if (fchmod(edp->fd(edp), PERM_SECURE|S_IRGRP) != 0)
		warn("%s: unable to make group readable", _PATH_SMP_DB);
	clean |= FILE_SECURE;

	if (pledge("stdio rpath wpath cpath getpw fattr flock", NULL) == -1)
		err(1, "pledge");

	/* Open the temporary insecure password database. */
	if (!secureonly) {
		(void)snprintf(buf, sizeof(buf), "%s.tmp",
		    changedir(_PATH_MP_DB, basedir));
		if (username) {
			cp(changedir(_PATH_MP_DB, basedir), buf, PERM_INSECURE);
			dp = dbopen(buf, O_RDWR, PERM_INSECURE, DB_HASH,
			    &openinfo);
		} else {
			dp = dbopen(buf, O_RDWR|O_CREAT|O_EXCL, PERM_INSECURE,
			    DB_HASH, &openinfo);
		}
		if (dp == NULL)
			fatal("%s", buf);
		clean |= FILE_INSECURE;
	} else
		dp = NULL;

	/*
	 * Open file for old password file.  Minor trickiness -- don't want to
	 * change the file already existing, since someone (stupidly) might
	 * still be using this for permission checking.  So, open it first and
	 * fdopen the resulting fd.  The resulting file should be readable by
	 * everyone.
	 */
	if (makeold) {
		(void)snprintf(buf, sizeof(buf), "%s.orig", pname);
		if ((tfd = open(buf,
		    O_WRONLY|O_CREAT|O_EXCL, PERM_INSECURE)) == -1)
			fatal("%s", buf);
		if ((oldfp = fdopen(tfd, "w")) == NULL)
			fatal("%s", buf);
		clean |= FILE_ORIG;
	}

	/*
	 * The databases actually contain three copies of the original data.
	 * Each password file entry is converted into a rough approximation
	 * of a ``struct passwd'', with the strings placed inline.  This
	 * object is then stored as the data for three separate keys.  The
	 * first key * is the pw_name field prepended by the _PW_KEYBYNAME
	 * character.  The second key is the pw_uid field prepended by the
	 * _PW_KEYBYUID character.  The third key is the line number in the
	 * original file prepended by the _PW_KEYBYNUM character.  (The special
	 * characters are prepended to ensure that the keys do not collide.)
	 *
	 * If we see something go by that looks like YP, we save a special
	 * pointer record, which if YP is enabled in the C lib, will speed
	 * things up.
	 */

	/*
	 * Write the .db files.
	 * We do this three times, one per key type (for getpw{nam,uid,ent}).
	 * The first time through we also check for YP, issue warnings
	 * and save the V7 format passwd file if necessary.
	 */
	db_store(fp, oldfp, edp, dp, &pwd, _PW_KEYBYNAME, username, olduid);
	db_store(fp, oldfp, edp, dp, &pwd, _PW_KEYBYUID, username, olduid);
	db_store(fp, oldfp, edp, dp, &pwd, _PW_KEYBYNUM, username, olduid);

	/* Store YP token, if needed. */
	if (hasyp && !username) {
		key.data = (u_char *)_PW_YPTOKEN;
		key.size = strlen(_PW_YPTOKEN);
		data.data = (u_char *)NULL;
		data.size = 0;

		if ((edp->put)(edp, &key, &data, R_NOOVERWRITE) == -1)
			fatal("put");

		if (dp && (dp->put)(dp, &key, &data, R_NOOVERWRITE) == -1)
			fatal("put");
	}

	if ((edp->close)(edp))
		fatal("close edp");
	if (dp && (dp->close)(dp))
		fatal("close dp");
	if (makeold) {
		if (fclose(oldfp) == EOF)
			fatal("close old");
	}

	/* Set master.passwd permissions, in case caller forgot. */
	(void)fchmod(fileno(fp), S_IRUSR|S_IWUSR);
	if (fclose(fp) != 0)
		fatal("fclose");

	/* Install as the real password files. */
	if (!secureonly) {
		(void)snprintf(buf, sizeof(buf), "%s.tmp",
		    changedir(_PATH_MP_DB, basedir));
		mv(buf, changedir(_PATH_MP_DB, basedir));
	}
	(void)snprintf(buf, sizeof(buf), "%s.tmp",
	    changedir(_PATH_SMP_DB, basedir));
	mv(buf, changedir(_PATH_SMP_DB, basedir));
	if (makeold) {
		(void)snprintf(buf, sizeof(buf), "%s.orig", pname);
		mv(buf, changedir(_PATH_PASSWD, basedir));
	}

	/*
	 * Move the master password LAST -- chpass(1), passwd(1) and vipw(8)
	 * all use flock(2) on it to block other incarnations of themselves.
	 * The rename means that everything is unlocked, as the original file
	 * can no longer be accessed.
	 */
	mv(pname, changedir(_PATH_MASTERPASSWD, basedir));
	exit(0);
}

int
scan(FILE *fp, struct passwd *pw, int *flags)
{
	static int lcnt;
	static char line[LINE_MAX];
	char *p;

	if (fgets(line, sizeof(line), fp) == NULL)
		return (0);
	++lcnt;
	/*
	 * ``... if I swallow anything evil, put your fingers down my
	 * throat...''
	 *	-- The Who
	 */
	p = line;
	if (*p != '\0' && *(p += strlen(line) - 1) != '\n') {
		warnx("line too long");
		goto fmt;
	}
	*p = '\0';
	*flags = 0;
	if (!pw_scan(line, pw, flags)) {
		warnx("at line #%d", lcnt);
fmt:		fatalc(EFTYPE, "%s", pname);
	}

	return (1);
}

void
cp(char *from, char *to, mode_t mode)
{
	static char buf[_MAXBSIZE];
	int from_fd, rcount, to_fd, wcount;

	if ((from_fd = open(from, O_RDONLY)) == -1)
		fatal("%s", from);
	if ((to_fd = open(to, O_WRONLY|O_CREAT|O_EXCL, mode)) == -1)
		fatal("%s", to);
	while ((rcount = read(from_fd, buf, sizeof buf)) > 0) {
		wcount = write(to_fd, buf, rcount);
		if (rcount != wcount || wcount == -1)
			fatal("%s to %s", from, to);
	}
	if (rcount == -1)
		fatal("%s to %s", from, to);
	close(to_fd);
	close(from_fd);
}

void
mv(char *from, char *to)
{
	if (rename(from, to))
		fatal("%s to %s", from, to);
}

void
fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
	cleanup();
	exit(EXIT_FAILURE);
}

void
fatalc(int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnc(code, fmt, ap);
	va_end(ap);
	cleanup();
	exit(EXIT_FAILURE);
}

void
fatalx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
	cleanup();
	exit(EXIT_FAILURE);
}

void
cleanup(void)
{
	char buf[PATH_MAX];

	if (clean & FILE_ORIG) {
		(void)snprintf(buf, sizeof(buf), "%s.orig", pname);
		(void)unlink(buf);
	}
	if (clean & FILE_SECURE) {
		(void)snprintf(buf, sizeof(buf), "%s.tmp",
		    changedir(_PATH_SMP_DB, basedir));
		(void)unlink(buf);
	}
	if (clean & FILE_INSECURE) {
		(void)snprintf(buf, sizeof(buf), "%s.tmp",
		    changedir(_PATH_MP_DB, basedir));
		(void)unlink(buf);
	}
}

void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: pwd_mkdb [-c] [-p | -s] [-d directory] [-u username] file\n");
	exit(EXIT_FAILURE);
}

char *
changedir(char *path, char *dir)
{
	static char fixed[PATH_MAX];
	char *p;

	if (!dir)
		return (path);

	if ((p = strrchr(path, '/')) != NULL)
		path = p + 1;
	snprintf(fixed, sizeof(fixed), "%s/%s", dir, path);
	return (fixed);
}

int
write_old_entry(FILE *to, const struct passwd *pw)
{
	char gidstr[16], uidstr[16];

	if (to == NULL)
		return (0);

	/* Preserve gid/uid -1 */
	if (pw->pw_gid == (gid_t)-1)
		strlcpy(gidstr, "-1", sizeof(gidstr));
	else
		snprintf(gidstr, sizeof(gidstr), "%u", (u_int)pw->pw_gid);

	if (pw->pw_uid == -1)
		strlcpy(uidstr, "-1", sizeof(uidstr));
	else
		snprintf(uidstr, sizeof(uidstr), "%u", (u_int)pw->pw_uid);

	return (fprintf(to, "%s:*:%s:%s:%s:%s:%s\n", pw->pw_name, uidstr,
	    gidstr, pw->pw_gecos, pw->pw_dir, pw->pw_shell));
}

void
db_store(FILE *fp, FILE *oldfp, DB *edp, DB *dp, struct passwd *pw,
	 int keytype, char *username, uid_t olduid)
{
	char *p, *t, buf[LINE_MAX * 2], tbuf[_PW_BUF_LEN];
	int flags = 0, dbmode, found = 0;
	static int firsttime = 1;
	DBT data, key;
	size_t len;
	u_int cnt;

	/* If given a username just add that record to the existing db. */
	dbmode = username ? 0 : R_NOOVERWRITE;

	rewind(fp);
	data.data = (u_char *)buf;
	key.data = (u_char *)tbuf;
	for (cnt = 1; scan(fp, pw, &flags); ++cnt) {

		if (firsttime) {
			/* Look like YP? */
			if ((pw->pw_name[0] == '+') || (pw->pw_name[0] == '-'))
				hasyp++;

			/* Warn about potentially unsafe uid/gid overrides. */
			if (pw->pw_name[0] == '+') {
				if (!(flags & _PASSWORD_NOUID) && !pw->pw_uid)
					warnx("line %d: superuser override in "
					    "YP inclusion", cnt);
				if (!(flags & _PASSWORD_NOGID) && !pw->pw_gid)
					warnx("line %d: wheel override in "
					    "YP inclusion", cnt);
			}

			/* Create V7 format password file entry. */
			if (write_old_entry(oldfp, pw) == -1)
				fatal("write old");
		}

		/* Are we updating a specific record? */
		if (username) {
			if (strcmp(username, pw->pw_name) != 0)
				continue;
			found = 1;
			/* If the uid changed, remove the old record by uid. */
			if (olduid != -1 && olduid != pw->pw_uid) {
				tbuf[0] = _PW_KEYBYUID;
				memcpy(tbuf + 1, &olduid, sizeof(olduid));
				key.size = sizeof(olduid) + 1;
				(edp->del)(edp, &key, 0);
				if (dp)
					(dp->del)(dp, &key, 0);
			}
			/* XXX - should check to see if line number changed. */
		}

		/* Build the key. */
		tbuf[0] = keytype;
		switch (keytype) {
		case _PW_KEYBYNUM:
			memmove(tbuf + 1, &cnt, sizeof(cnt));
			key.size = sizeof(cnt) + 1;
			break;

		case _PW_KEYBYNAME:
			len = strlen(pw->pw_name);
			memmove(tbuf + 1, pw->pw_name, len);
			key.size = len + 1;
			break;

		case _PW_KEYBYUID:
			memmove(tbuf + 1, &pw->pw_uid, sizeof(pw->pw_uid));
			key.size = sizeof(pw->pw_uid) + 1;
			break;
		}

#define	COMPACT(e)	t = e; while ((*p++ = *t++));
		/* Create the secure record. */
		p = buf;
		COMPACT(pw->pw_name);
		COMPACT(pw->pw_passwd);
		memmove(p, &pw->pw_uid, sizeof(uid_t));
		p += sizeof(uid_t);
		memmove(p, &pw->pw_gid, sizeof(gid_t));
		p += sizeof(gid_t);
		memmove(p, &pw->pw_change, sizeof(time_t));
		p += sizeof(time_t);
		COMPACT(pw->pw_class);
		COMPACT(pw->pw_gecos);
		COMPACT(pw->pw_dir);
		COMPACT(pw->pw_shell);
		memmove(p, &pw->pw_expire, sizeof(time_t));
		p += sizeof(time_t);
		memmove(p, &flags, sizeof(int));
		p += sizeof(int);
		data.size = p - buf;

		/* getpwent() does not support entries > _PW_BUF_LEN. */
		if (data.size > _PW_BUF_LEN)
			fatalx("%s: entry too large", pw->pw_name);

		/* Write the secure record. */
		if ((edp->put)(edp, &key, &data, dbmode) == -1)
			fatal("put");

		if (dp == NULL)
			continue;

		/* Star out password to make insecure record. */
		p = buf + strlen(pw->pw_name) + 1;	/* skip pw_name */
		len = strlen(pw->pw_passwd);
		explicit_bzero(p, len);			/* zero pw_passwd */
		t = p + len + 1;			/* skip pw_passwd */
		if (len != 0)
			*p++ = '*';
		*p++ = '\0';
		memmove(p, t, data.size - (t - buf));
		data.size -= len - 1;

		/* Write the insecure record. */
		if ((dp->put)(dp, &key, &data, dbmode) == -1)
			fatal("put");
	}
	if (firsttime) {
		firsttime = 0;
		if (username && !found && olduid != -1)
			fatalx("can't find user in master.passwd");
	}
}
