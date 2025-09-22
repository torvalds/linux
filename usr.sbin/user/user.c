/* $OpenBSD: user.c,v 1.132 2025/02/27 01:32:55 millert Exp $ */
/* $NetBSD: user.c,v 1.69 2003/04/14 17:40:07 agc Exp $ */

/*
 * Copyright (c) 1999 Alistair G. Crooks.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <login_cap.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <util.h>

#include "usermgmt.h"


/* this struct describes a uid range */
typedef struct range_t {
	uid_t	r_from;		/* low uid */
	uid_t	r_to;		/* high uid */
} range_t;

/* this struct encapsulates the user information */
typedef struct user_t {
	int		u_flags;		/* see below */
	uid_t		u_uid;			/* uid of user */
	char	       *u_password;		/* encrypted password */
	char	       *u_comment;		/* comment field */
	char	       *u_home;		/* home directory */
	char	       *u_primgrp;		/* primary group */
	int		u_groupc;		/* # of secondary groups */
	const char     *u_groupv[NGROUPS_MAX];	/* secondary groups */
	char	       *u_shell;		/* user's shell */
	char	       *u_basedir;		/* base directory for home */
	char	       *u_expire;		/* when account will expire */
	char	       *u_inactive;		/* when password will expire */
	char	       *u_skeldir;		/* directory for startup files */
	char	       *u_class;		/* login class */
	unsigned int	u_rsize;		/* size of range array */
	unsigned int	u_rc;			/* # of ranges */
	range_t	       *u_rv;			/* the ranges */
	unsigned int	u_defrc;		/* # of ranges in defaults */
	int		u_preserve;		/* preserve uids on deletion */
} user_t;

/* flags for which fields of the user_t replace the passwd entry */
enum {
	F_COMMENT	= 0x0001,
	F_DUPUID	= 0x0002,
	F_EXPIRE	= 0x0004,
	F_GROUP		= 0x0008,
	F_HOMEDIR	= 0x0010,
	F_MKDIR		= 0x0020,
	F_INACTIVE	= 0x0040,
	F_PASSWORD	= 0x0080,
	F_SECGROUP	= 0x0100,
	F_SHELL		= 0x0200,
	F_UID		= 0x0400,
	F_USERNAME	= 0x0800,
	F_CLASS		= 0x1000,
	F_SETSECGROUP	= 0x4000,
	F_ACCTLOCK	= 0x8000,
	F_ACCTUNLOCK	= 0x10000
};

/* flags for runas() */
enum {
	RUNAS_DUP_DEVNULL	= 0x01,
	RUNAS_IGNORE_EXITVAL	= 0x02
};

#define CONFFILE		"/etc/usermgmt.conf"
#define _PATH_NONEXISTENT	"/nonexistent"

#ifndef DEF_GROUP
#define DEF_GROUP	"=uid"
#endif

#ifndef DEF_BASEDIR
#define DEF_BASEDIR	"/home"
#endif

#ifndef DEF_SKELDIR
#define DEF_SKELDIR	"/etc/skel"
#endif

#ifndef DEF_SHELL
#define DEF_SHELL	_PATH_KSHELL
#endif

#ifndef DEF_COMMENT
#define DEF_COMMENT	""
#endif

#ifndef DEF_LOWUID
#define DEF_LOWUID	1000
#endif

#ifndef DEF_HIGHUID
#define DEF_HIGHUID	60000
#endif

#ifndef DEF_INACTIVE
#define DEF_INACTIVE	0
#endif

#ifndef DEF_EXPIRE
#define DEF_EXPIRE	NULL
#endif

#ifndef DEF_CLASS
#define DEF_CLASS	""
#endif

#ifndef WAITSECS
#define WAITSECS	10
#endif

#ifndef NOBODY_UID
#define NOBODY_UID	32767
#endif

/* some useful constants */
enum {
	MaxShellNameLen = 256,
	MaxFileNameLen = PATH_MAX,
	MaxUserNameLen = _PW_NAME_LEN,
	MaxCommandLen = 2048,
	PasswordLength = _PASSWORD_LEN,
	LowGid = DEF_LOWUID,
	HighGid = DEF_HIGHUID
};

/* Full paths of programs used here */
#define CHMOD		"/bin/chmod"
#define CHOWN		"/sbin/chown"
#define MKDIR		"/bin/mkdir"
#define MV		"/bin/mv"
#define NOLOGIN		"/sbin/nologin"
#define CP		"/bin/cp"
#define RM		"/bin/rm"

#define UNSET_INACTIVE	"Null (unset)"
#define UNSET_EXPIRY	"Null (unset)"

static int adduser(char *, user_t *);
static int append_group(char *, int, const char **);
static int copydotfiles(char *, char *);
static int creategid(char *, gid_t, const char *);
static int getnextgid(uid_t *, uid_t, uid_t);
static int getnextuid(int, uid_t *, uid_t, uid_t);
static int is_local(char *, const char *);
static int modify_gid(char *, char *);
static int moduser(char *, char *, user_t *);
static int removehomedir(const char *, uid_t, const char *);
static int rm_user_from_groups(char *);
static int save_range(user_t *, char *);
static int scantime(time_t *, char *);
static int setdefaults(user_t *);
static int valid_class(char *);
static int valid_group(char *);
static int valid_login(char *);
static size_t expand_len(const char *, const char *);
static struct group *find_group_info(const char *);
static struct passwd *find_user_info(const char *);
static void checkeuid(void);
static void strsave(char **, const char *);
static void read_defaults(user_t *);

static int	verbose;

/* free *cpp, then store a copy of `s' in it */
static void
strsave(char **cpp, const char *s)
{
	free(*cpp);
	if ((*cpp = strdup(s)) == NULL)
		err(1, NULL);
}

/* run the given command with optional arguments as the specified uid */
static int
runas(const char *path, const char *const argv[], uid_t uid, int flags)
{
	int argc, status, ret = 0;
	char buf[MaxCommandLen];
	pid_t child;

	strlcpy(buf, path, sizeof(buf));
	for (argc = 1; argv[argc] != NULL; argc++) {
		strlcat(buf, " ", sizeof(buf));
		strlcat(buf, argv[argc], sizeof(buf));
	}
	if (verbose)
		printf("Command: %s\n", buf);

	child = fork();
	switch (child) {
	case -1:
		err(EXIT_FAILURE, "fork");
	case 0:
		if (flags & RUNAS_DUP_DEVNULL) {
			/* Redirect output to /dev/null if possible. */
			int dev_null = open(_PATH_DEVNULL, O_RDWR);
			if (dev_null != -1) {
				dup2(dev_null, STDOUT_FILENO);
				dup2(dev_null, STDERR_FILENO);
				if (dev_null > STDERR_FILENO)
					close(dev_null);
			} else {
				warn("%s", _PATH_DEVNULL);
			}
		}
		if (uid != -1) {
			if (setresuid(uid, uid, uid) == -1)
				warn("setresuid(%u, %u, %u)", uid, uid, uid);
		}
		execv(path, (char **)argv);
		warn("%s", buf);
		_exit(EXIT_FAILURE);
	default:
		while (waitpid(child, &status, 0) == -1) {
			if (errno != EINTR)
				err(EXIT_FAILURE, "waitpid");
		}
		if (WIFSIGNALED(status)) {
			ret = WTERMSIG(status);
			warnx("[Warning] `%s' killed by signal %d", buf, ret);
			ret |= 128;
		} else {
			if (!(flags & RUNAS_IGNORE_EXITVAL))
				ret = WEXITSTATUS(status);
			if (ret != 0) {
				warnx("[Warning] `%s' failed with status %d",
				    buf, ret);
			}
		}
		return ret;
	}
}

/* run the given command with optional arguments */
static int
run(const char *path, const char *const argv[])
{
	return runas(path, argv, -1, 0);
}

/* remove a users home directory, returning 1 for success (ie, no problems encountered) */
static int
removehomedir(const char *user, uid_t uid, const char *dir)
{
	const char *rm_argv[] = { "rm", "-rf", dir, NULL };
	struct stat st;

	/* userid not root? */
	if (uid == 0) {
		warnx("Not deleting home directory `%s'; userid is 0", dir);
		return 0;
	}

	/* directory exists (and is a directory!) */
	if (stat(dir, &st) == -1) {
		warnx("Home directory `%s' doesn't exist", dir);
		return 0;
	}
	if (!S_ISDIR(st.st_mode)) {
		warnx("Home directory `%s' is not a directory", dir);
		return 0;
	}

	/* userid matches directory owner? */
	if (st.st_uid != uid) {
		warnx("User `%s' doesn't own directory `%s', not removed",
		    user, dir);
		return 0;
	}

	/* run "rm -rf dir 2>&1/dev/null" as user, not root */
	(void) runas(RM, rm_argv, uid, RUNAS_DUP_DEVNULL|RUNAS_IGNORE_EXITVAL);
	if (rmdir(dir) == -1 && errno != ENOENT) {
		warnx("Unable to remove all files in `%s'", dir);
		return 0;
	}
	return 1;
}

/*
 * check that the effective uid is 0 - called from funcs which will
 * modify data and config files.
 */
static void
checkeuid(void)
{
	if (geteuid() != 0) {
		errx(EXIT_FAILURE, "Program must be run as root");
	}
}

/* copy any dot files into the user's home directory */
static int
copydotfiles(char *skeldir, char *dst)
{
	char		src[MaxFileNameLen];
	struct dirent	*dp;
	DIR		*dirp;
	int		len, n;

	if (*skeldir == '\0')
		return 0;
	if ((dirp = opendir(skeldir)) == NULL) {
		warn("can't open source . files dir `%s'", skeldir);
		return 0;
	}
	for (n = 0; (dp = readdir(dirp)) != NULL && n == 0 ; ) {
		if (strcmp(dp->d_name, ".") == 0 ||
		    strcmp(dp->d_name, "..") == 0) {
			continue;
		}
		n = 1;
	}
	(void) closedir(dirp);
	if (n == 0) {
		warnx("No \"dot\" initialisation files found");
	} else {
		len = snprintf(src, sizeof(src), "%s/.", skeldir);
		if (len < 0 || len >= sizeof(src)) {
			warnx("skeleton directory `%s' too long", skeldir);
			n = 0;
		} else {
			const char *cp_argv[] = { "cp", "-a", src, dst, NULL };
			if (verbose)
				cp_argv[1] = "-av";
			run(CP, cp_argv);
		}
	}
	return n;
}

/* returns 1 if the specified gid exists in the group file, else 0 */
static int
gid_exists(gid_t gid)
{
    return group_from_gid(gid, 1) != NULL;
}

/* return 1 if the specified group exists in the group file, else 0 */
static int
group_exists(const char *group)
{
    gid_t gid;

    return gid_from_group(group, &gid) != -1;
}

/* create a group entry with gid `gid' */
static int
creategid(char *group, gid_t gid, const char *name)
{
	struct stat	st;
	FILE		*from;
	FILE		*to;
	char		*buf;
	char		f[MaxFileNameLen];
	int		fd, ret;
	int		wroteit = 0;
	size_t		len;

	if (group_exists(group)) {
		warnx("group `%s' already exists", group);
		return 0;
	}
	if ((from = fopen(_PATH_GROUP, "r")) == NULL) {
		warn("can't create gid for `%s': can't open `%s'", group,
		    _PATH_GROUP);
		return 0;
	}
	if (flock(fileno(from), LOCK_EX | LOCK_NB) == -1) {
		warn("can't lock `%s'", _PATH_GROUP);
	}
	(void) fstat(fileno(from), &st);
	(void) snprintf(f, sizeof(f), "%s.XXXXXXXX", _PATH_GROUP);
	if ((fd = mkstemp(f)) == -1) {
		warn("can't create gid: mkstemp failed");
		fclose(from);
		return 0;
	}
	if ((to = fdopen(fd, "w")) == NULL) {
		warn("can't create gid: fdopen `%s' failed", f);
		fclose(from);
		close(fd);
		unlink(f);
		return 0;
	}
	while ((buf = fgetln(from, &len)) != NULL && len > 0) {
		ret = 0;
		if (buf[0] == '+' && wroteit == 0) {
			ret = fprintf(to, "%s:*:%u:%s\n", group, gid, name);
			wroteit = 1;
		}
		if (ret == -1 ||
		    fprintf(to, "%*.*s", (int)len, (int)len, buf) != len) {
			warn("can't create gid: short write to `%s'", f);
			fclose(from);
			fclose(to);
			unlink(f);
			return 0;
		}
	}
	ret = 0;
	if (wroteit == 0)
		ret = fprintf(to, "%s:*:%u:%s\n", group, gid, name);
	fclose(from);
	if (fclose(to) == EOF || ret == -1) {
		warn("can't create gid: short write to `%s'", f);
		unlink(f);
		return 0;
	}
	if (rename(f, _PATH_GROUP) == -1) {
		warn("can't create gid: can't rename `%s' to `%s'", f,
		    _PATH_GROUP);
		unlink(f);
		return 0;
	}
	(void) chmod(_PATH_GROUP, st.st_mode & 0777);
	syslog(LOG_INFO, "new group added: name=%s, gid=%u", group, gid);
	return 1;
}

/* modify the group entry with name `group' to be newent */
static int
modify_gid(char *group, char *newent)
{
	struct stat	st;
	FILE		*from;
	FILE		*to;
	char		buf[LINE_MAX];
	char		f[MaxFileNameLen];
	char		*colon;
	int		groupc;
	int		entc;
	int		fd;
	int		cc;

	if ((from = fopen(_PATH_GROUP, "r")) == NULL) {
		warn("can't modify gid for `%s': can't open `%s'", group,
		    _PATH_GROUP);
		return 0;
	}
	if (flock(fileno(from), LOCK_EX | LOCK_NB) == -1) {
		warn("can't lock `%s'", _PATH_GROUP);
	}
	(void) fstat(fileno(from), &st);
	(void) snprintf(f, sizeof(f), "%s.XXXXXXXX", _PATH_GROUP);
	if ((fd = mkstemp(f)) == -1) {
		warn("can't modify gid: mkstemp failed");
		fclose(from);
		return 0;
	}
	if ((to = fdopen(fd, "w")) == NULL) {
		warn("can't modify gid: fdopen `%s' failed", f);
		fclose(from);
		close(fd);
		unlink(f);
		return 0;
	}
	groupc = strlen(group);
	while (fgets(buf, sizeof(buf), from) != NULL) {
		cc = strlen(buf);
		if (cc > 0 && buf[cc - 1] != '\n' && !feof(from)) {
			while (fgetc(from) != '\n' && !feof(from))
				cc++;
			warnx("%s: line `%s' too long (%d bytes), skipping",
			    _PATH_GROUP, buf, cc);
			continue;
		}
		if ((colon = strchr(buf, ':')) == NULL) {
			/*
			 * The only valid entry with no column is the all-YP
			 * line.
			 */
			if (strcmp(buf, "+\n") != 0) {
				warnx("badly formed entry `%.*s'", cc - 1, buf);
				continue;
			}
		} else {
			entc = (int)(colon - buf);
			if (entc == groupc && strncmp(group, buf, entc) == 0) {
				if (newent == NULL) {
					continue;
				} else {
					cc = strlcpy(buf, newent, sizeof(buf));
					if (cc >= sizeof(buf)) {
						warnx("group `%s' entry too long",
						    newent);
						fclose(from);
						fclose(to);
						unlink(f);
						return (0);
					}
				}
			}
		}
		if (fwrite(buf, cc, 1, to) != 1) {
			warn("can't modify gid: short write to `%s'", f);
			fclose(from);
			fclose(to);
			unlink(f);
			return 0;
		}
	}
	fclose(from);
	if (fclose(to) == EOF) {
		warn("can't modify gid: short write to `%s'", f);
		unlink(f);
		return 0;
	}
	if (rename(f, _PATH_GROUP) == -1) {
		warn("can't modify gid: can't rename `%s' to `%s'", f, _PATH_GROUP);
		unlink(f);
		return 0;
	}
	(void) chmod(_PATH_GROUP, st.st_mode & 0777);
	if (newent == NULL) {
		syslog(LOG_INFO, "group deleted: name=%s", group);
	} else {
		syslog(LOG_INFO, "group information modified: name=%s", group);
	}
	return 1;
}

/* modify the group entries for all `groups', by adding `user' */
static int
append_group(char *user, int ngroups, const char **groups)
{
	struct group	*grp;
	struct passwd	*pwp;
	struct stat	st;
	FILE		*from;
	FILE		*to;
	char		buf[LINE_MAX];
	char		f[MaxFileNameLen];
	char		*colon;
	const char	*ugid = NULL;
	int		fd;
	int		cc;
	int		i;
	int		j;

	if ((pwp = getpwnam(user))) {
		if ((ugid = group_from_gid(pwp->pw_gid, 1)) == NULL) {
			warnx("can't get primary group for user `%s'", user);
			return 0;
		}
	}

	for (i = 0 ; i < ngroups ; i++) {
		if ((grp = getgrnam(groups[i])) == NULL) {
			warnx("can't append group `%s' for user `%s'",
			    groups[i], user);
		} else {
			for (j = 0 ; grp->gr_mem[j] ; j++) {
				if (strcmp(user, grp->gr_mem[j]) == 0) {
					/* already in it */
					groups[i] = "";
				}
			}
		}
	}
	if ((from = fopen(_PATH_GROUP, "r")) == NULL) {
		warn("can't append group for `%s': can't open `%s'", user,
		    _PATH_GROUP);
		return 0;
	}
	if (flock(fileno(from), LOCK_EX | LOCK_NB) == -1) {
		warn("can't lock `%s'", _PATH_GROUP);
	}
	(void) fstat(fileno(from), &st);
	(void) snprintf(f, sizeof(f), "%s.XXXXXXXX", _PATH_GROUP);
	if ((fd = mkstemp(f)) == -1) {
		warn("can't append group: mkstemp failed");
		fclose(from);
		return 0;
	}
	if ((to = fdopen(fd, "w")) == NULL) {
		warn("can't append group: fdopen `%s' failed", f);
		fclose(from);
		close(fd);
		unlink(f);
		return 0;
	}
	while (fgets(buf, sizeof(buf), from) != NULL) {
		cc = strlen(buf);
		if (cc > 0 && buf[cc - 1] != '\n' && !feof(from)) {
			while (fgetc(from) != '\n' && !feof(from))
				cc++;
			warnx("%s: line `%s' too long (%d bytes), skipping",
			    _PATH_GROUP, buf, cc);
			continue;
		}
		if ((colon = strchr(buf, ':')) == NULL) {
			warnx("badly formed entry `%s'", buf);
			continue;
		}
		for (i = 0 ; i < ngroups ; i++) {
			j = (int)(colon - buf);
			if (ugid) {
				if (strcmp(ugid, groups[i]) == 0) {
					/* user's primary group, no need to append */
					groups[i] = "";
				}
			}
			if (strncmp(groups[i], buf, j) == 0 &&
			    groups[i][j] == '\0') {
				while (isspace((unsigned char)buf[cc - 1]))
					cc--;
				buf[(j = cc)] = '\0';
				if (buf[strlen(buf) - 1] != ':')
					strlcat(buf, ",", sizeof(buf));
				cc = strlcat(buf, user, sizeof(buf)) + 1;
				if (cc >= sizeof(buf)) {
					warnx("Warning: group `%s' would "
					    "become too long, not modifying",
					    groups[i]);
					cc = j + 1;
				}
				buf[cc - 1] = '\n';
				buf[cc] = '\0';
			}
		}
		if (fwrite(buf, cc, 1, to) != 1) {
			warn("can't append group: short write to `%s'", f);
			fclose(from);
			fclose(to);
			unlink(f);
			return 0;
		}
	}
	fclose(from);
	if (fclose(to) == EOF) {
		warn("can't append group: short write to `%s'", f);
		unlink(f);
		return 0;
	}
	if (rename(f, _PATH_GROUP) == -1) {
		warn("can't append group: can't rename `%s' to `%s'", f, _PATH_GROUP);
		unlink(f);
		return 0;
	}
	(void) chmod(_PATH_GROUP, st.st_mode & 0777);
	return 1;
}

/* return 1 if `login' is a valid login name */
static int
valid_login(char *login_name)
{
	unsigned char	*cp;

	/* The first character cannot be a hyphen */
	if (*login_name == '-')
		return 0;

	for (cp = login_name ; *cp ; cp++) {
		/* We allow '$' as the last character for samba */
		if (!isalnum((unsigned char)*cp) && *cp != '.' &&
		    *cp != '_' && *cp != '-' &&
		    !(*cp == '$' && *(cp + 1) == '\0')) {
			return 0;
		}
	}
	if ((char *)cp - login_name > MaxUserNameLen)
		return 0;
	return 1;
}

/* return 1 if `group' is a valid group name */
static int
valid_group(char *group)
{
	unsigned char	*cp;

	for (cp = group ; *cp ; cp++) {
		if (!isalnum((unsigned char)*cp) && *cp != '.' &&
		    *cp != '_' && *cp != '-') {
			return 0;
		}
	}
	if ((char *)cp - group > MaxUserNameLen)
		return 0;
	return 1;
}

/* return 1 if `class' exists */
static int
valid_class(char *class)
{
	login_cap_t *lc;

	if ((lc = login_getclass(class)) != NULL)
		login_close(lc);
	return lc != NULL;
}

/* find the next gid in the range lo .. hi */
static int
getnextgid(uid_t *gidp, uid_t lo, uid_t hi)
{
	for (*gidp = lo ; *gidp < hi ; *gidp += 1) {
		if (!gid_exists((gid_t)*gidp)) {
			return 1;
		}
	}
	return 0;
}

/* save a range of uids */
static int
save_range(user_t *up, char *cp)
{
	uid_t	from;
	uid_t	to;
	int	i;

	if (up->u_rc == up->u_rsize) {
		up->u_rsize *= 2;
		if ((up->u_rv = reallocarray(up->u_rv, up->u_rsize,
		    sizeof(range_t))) == NULL) {
			warn(NULL);
			return 0;
		}
	}
	if (up->u_rv && sscanf(cp, "%u..%u", &from, &to) == 2) {
		for (i = up->u_defrc ; i < up->u_rc ; i++) {
			if (up->u_rv[i].r_from == from && up->u_rv[i].r_to == to) {
				break;
			}
		}
		if (i == up->u_rc) {
			up->u_rv[up->u_rc].r_from = from;
			up->u_rv[up->u_rc].r_to = to;
			up->u_rc += 1;
		}
	} else {
		warnx("Bad uid range `%s'", cp);
		return 0;
	}
	return 1;
}

/* set the defaults in the defaults file */
static int
setdefaults(user_t *up)
{
	char	template[MaxFileNameLen];
	FILE	*fp;
	int	ret;
	int	fd;
	int	i;

	(void) snprintf(template, sizeof(template), "%s.XXXXXXXX", CONFFILE);
	if ((fd = mkstemp(template)) == -1) {
		warnx("can't mkstemp `%s' for writing", CONFFILE);
		return 0;
	}
	if ((fp = fdopen(fd, "w")) == NULL) {
		warn("can't fdopen `%s' for writing", CONFFILE);
		return 0;
	}
	ret = 1;
	if (fprintf(fp, "group\t\t%s\n", up->u_primgrp) <= 0 ||
	    fprintf(fp, "base_dir\t%s\n", up->u_basedir) <= 0 ||
	    fprintf(fp, "skel_dir\t%s\n", up->u_skeldir) <= 0 ||
	    fprintf(fp, "shell\t\t%s\n", up->u_shell) <= 0 ||
	    fprintf(fp, "class\t\t%s\n", up->u_class) <= 0 ||
	    fprintf(fp, "inactive\t%s\n", (up->u_inactive == NULL) ? UNSET_INACTIVE : up->u_inactive) <= 0 ||
	    fprintf(fp, "expire\t\t%s\n", (up->u_expire == NULL) ? UNSET_EXPIRY : up->u_expire) <= 0 ||
	    fprintf(fp, "preserve\t%s\n", (up->u_preserve == 0) ? "false" : "true") <= 0) {
		warn("can't write to `%s'", CONFFILE);
		ret = 0;
	}
	for (i = (up->u_defrc != up->u_rc) ? up->u_defrc : 0 ; i < up->u_rc ; i++) {
		if (fprintf(fp, "range\t\t%u..%u\n", up->u_rv[i].r_from, up->u_rv[i].r_to) <= 0) {
			warn("can't write to `%s'", CONFFILE);
			ret = 0;
		}
	}
	if (fclose(fp) == EOF) {
		warn("can't write to `%s'", CONFFILE);
		ret = 0;
	}
	if (ret) {
		ret = ((rename(template, CONFFILE) == 0) && (chmod(CONFFILE, 0644) == 0));
	}
	return ret;
}

/* read the defaults file */
static void
read_defaults(user_t *up)
{
	struct stat	st;
	size_t		lineno;
	size_t		len;
	FILE		*fp;
	unsigned char	*cp;
	unsigned char	*s;

	strsave(&up->u_primgrp, DEF_GROUP);
	strsave(&up->u_basedir, DEF_BASEDIR);
	strsave(&up->u_skeldir, DEF_SKELDIR);
	strsave(&up->u_shell, DEF_SHELL);
	strsave(&up->u_comment, DEF_COMMENT);
	strsave(&up->u_class, DEF_CLASS);
	up->u_rsize = 16;
	up->u_defrc = 0;
	if ((up->u_rv = calloc(up->u_rsize, sizeof(range_t))) == NULL)
		err(1, NULL);
	up->u_inactive = DEF_INACTIVE;
	up->u_expire = DEF_EXPIRE;
	if ((fp = fopen(CONFFILE, "r")) == NULL) {
		if (stat(CONFFILE, &st) == -1 && !setdefaults(up)) {
			warn("can't create `%s' defaults file", CONFFILE);
		}
		fp = fopen(CONFFILE, "r");
	}
	if (fp != NULL) {
		while ((s = fparseln(fp, &len, &lineno, NULL, 0)) != NULL) {
			if (strncmp(s, "group", 5) == 0) {
				for (cp = s + 5 ; isspace((unsigned char)*cp); cp++) {
				}
				strsave(&up->u_primgrp, cp);
			} else if (strncmp(s, "base_dir", 8) == 0) {
				for (cp = s + 8 ; isspace((unsigned char)*cp); cp++) {
				}
				strsave(&up->u_basedir, cp);
			} else if (strncmp(s, "skel_dir", 8) == 0) {
				for (cp = s + 8 ; isspace((unsigned char)*cp); cp++) {
				}
				strsave(&up->u_skeldir, cp);
			} else if (strncmp(s, "shell", 5) == 0) {
				for (cp = s + 5 ; isspace((unsigned char)*cp); cp++) {
				}
				strsave(&up->u_shell, cp);
			} else if (strncmp(s, "password", 8) == 0) {
				for (cp = s + 8 ; isspace((unsigned char)*cp); cp++) {
				}
				strsave(&up->u_password, cp);
			} else if (strncmp(s, "class", 5) == 0) {
				for (cp = s + 5 ; isspace((unsigned char)*cp); cp++) {
				}
				strsave(&up->u_class, cp);
			} else if (strncmp(s, "inactive", 8) == 0) {
				for (cp = s + 8 ; isspace((unsigned char)*cp); cp++) {
				}
				if (strcmp(cp, UNSET_INACTIVE) == 0) {
					free(up->u_inactive);
					up->u_inactive = NULL;
				} else {
					strsave(&up->u_inactive, cp);
				}
			} else if (strncmp(s, "range", 5) == 0) {
				for (cp = s + 5 ; isspace((unsigned char)*cp); cp++) {
				}
				(void) save_range(up, cp);
			} else if (strncmp(s, "preserve", 8) == 0) {
				for (cp = s + 8 ; isspace((unsigned char)*cp); cp++) {
				}
				up->u_preserve = (strncmp(cp, "true", 4) == 0) ? 1 :
						  (strncmp(cp, "yes", 3) == 0) ? 1 :
						   strtonum(cp, INT_MIN, INT_MAX, NULL);
			} else if (strncmp(s, "expire", 6) == 0) {
				for (cp = s + 6 ; isspace((unsigned char)*cp); cp++) {
				}
				if (strcmp(cp, UNSET_EXPIRY) == 0) {
					free(up->u_expire);
					up->u_expire = NULL;
				} else {
					strsave(&up->u_expire, cp);
				}
			}
			free(s);
		}
		fclose(fp);
	}
	if (up->u_rc == 0) {
		up->u_rv[up->u_rc].r_from = DEF_LOWUID;
		up->u_rv[up->u_rc].r_to = DEF_HIGHUID;
		up->u_rc += 1;
	}
	up->u_defrc = up->u_rc;
}

/* return 1 if the specified uid exists in the passwd file, else 0 */
static int
uid_exists(uid_t uid)
{
    return user_from_uid(uid, 1) != NULL;
}

/* return 1 if the specified user exists in the passwd file, else 0 */
static int
user_exists(const char *user)
{
    uid_t uid;

    return uid_from_user(user, &uid) != -1;
}

/* return the next valid unused uid */
static int
getnextuid(int sync_uid_gid, uid_t *uid, uid_t low_uid, uid_t high_uid)
{
	for (*uid = low_uid ; *uid <= high_uid ; (*uid)++) {
		if (!uid_exists((uid_t)*uid) && *uid != NOBODY_UID) {
			if (sync_uid_gid) {
				if (!gid_exists((gid_t)*uid)) {
					return 1;
				}
			} else {
				return 1;
			}
		}
	}
	return 0;
}

/* look for a valid time, return 0 if it was specified but bad */
static int
scantime(time_t *tp, char *s)
{
	struct tm	tm;

	*tp = 0;
	if (s != NULL) {
		memset(&tm, 0, sizeof(tm));
		tm.tm_isdst = -1;
		if (strptime(s, "%c", &tm) != NULL) {
			*tp = mktime(&tm);
		} else if (strptime(s, "%B %d %Y", &tm) != NULL) {
			*tp = mktime(&tm);
		} else if (isdigit((unsigned char) s[0]) != 0) {
			*tp = (time_t)atoll(s);
		} else {
			return 0;
		}
	}
	return 1;
}

/* compute the extra length '&' expansion consumes */
static size_t
expand_len(const char *p, const char *username)
{
	size_t alen;
	size_t ulen;

	ulen = strlen(username);
	for (alen = 0; *p != '\0'; p++)
		if (*p == '&')
			alen += ulen - 1;
	return alen;
}

/* see if we can find out the user struct */
static struct passwd *
find_user_info(const char *name)
{
	struct passwd	*pwp;
	const char	*errstr;
	uid_t		uid;

	if ((pwp = getpwnam(name)) == NULL) {
		uid = strtonum(name, -1, UID_MAX, &errstr);
		if (errstr == NULL)
			pwp = getpwuid(uid);
	}
	return pwp;
}

/* see if we can find out the group struct */
static struct group *
find_group_info(const char *name)
{
	struct group	*grp;
	const char	*errstr;
	gid_t		gid;

	if ((grp = getgrnam(name)) == NULL) {
		gid = strtonum(name, -1, GID_MAX, &errstr);
		if (errstr == NULL)
			grp = getgrgid(gid);
	}
	return grp;
}

/* add a user */
static int
adduser(char *login_name, user_t *up)
{
	struct group	*grp;
	struct stat	st;
	time_t		expire;
	time_t		inactive;
	char		password[PasswordLength + 1];
	char		home[MaxFileNameLen];
	char		buf[LINE_MAX];
	int		sync_uid_gid;
	int		masterfd;
	int		ptmpfd;
	gid_t		gid;
	int		cc;
	int		i, yp = 0;
	FILE		*fp;

	if (!valid_login(login_name)) {
		errx(EXIT_FAILURE, "`%s' is not a valid login name", login_name);
	}
	if (!valid_class(up->u_class)) {
		errx(EXIT_FAILURE, "No such login class `%s'", up->u_class);
	}
	if ((masterfd = open(_PATH_MASTERPASSWD, O_RDONLY)) == -1) {
		err(EXIT_FAILURE, "can't open `%s'", _PATH_MASTERPASSWD);
	}
	if (flock(masterfd, LOCK_EX | LOCK_NB) == -1) {
		err(EXIT_FAILURE, "can't lock `%s'", _PATH_MASTERPASSWD);
	}
	pw_init();
	if ((ptmpfd = pw_lock(WAITSECS)) == -1) {
		int saved_errno = errno;
		close(masterfd);
		errc(EXIT_FAILURE, saved_errno, "can't obtain pw_lock");
	}
	if ((fp = fdopen(masterfd, "r")) == NULL) {
		int saved_errno = errno;
		close(masterfd);
		close(ptmpfd);
		pw_abort();
		errc(EXIT_FAILURE, saved_errno,
		    "can't fdopen `%s' for reading", _PATH_MASTERPASSWD);
	}
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		cc = strlen(buf);
		/*
		 * Stop copying the file at the yp entry; we want to
		 * put the new user before it, and preserve entries
		 * after the yp entry.
		 */
		if (cc > 1 && buf[0] == '+' && buf[1] == ':') {
			yp = 1;
			break;
		}
		if (write(ptmpfd, buf, (size_t)(cc)) != cc) {
			int saved_errno = errno;
			fclose(fp);
			close(ptmpfd);
			pw_abort();
			errc(EXIT_FAILURE, saved_errno,
			    "short write to /etc/ptmp (not %d chars)", cc);
		}
	}
	if (ferror(fp)) {
		int saved_errno = errno;
		fclose(fp);
		close(ptmpfd);
		pw_abort();
		errc(EXIT_FAILURE, saved_errno, "read error on %s",
		    _PATH_MASTERPASSWD);
	}
	/* if no uid was specified, get next one in [low_uid..high_uid] range */
	sync_uid_gid = (strcmp(up->u_primgrp, "=uid") == 0);
	if (up->u_uid == -1) {
		int got_id = 0;

		/*
		 * Look for a free UID in the command line ranges (if any).
		 * These start after the ranges specified in the config file.
		 */
		for (i = up->u_defrc; got_id == 0 && i < up->u_rc ; i++) {
			got_id = getnextuid(sync_uid_gid, &up->u_uid,
			    up->u_rv[i].r_from, up->u_rv[i].r_to);
		}
		/*
		 * If there were no free UIDs in the command line ranges,
		 * try the ranges from the config file (there will always
		 * be at least one default).
		 */
		if (got_id == 0) {
			for (i = 0; got_id == 0 && i < up->u_defrc; i++) {
				got_id = getnextuid(sync_uid_gid, &up->u_uid,
				    up->u_rv[i].r_from, up->u_rv[i].r_to);
			}
		}
		if (got_id == 0) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "can't get next uid for %u", up->u_uid);
		}
	}
	/* check uid isn't already allocated */
	if (!(up->u_flags & F_DUPUID) && uid_exists((uid_t)up->u_uid)) {
		close(ptmpfd);
		pw_abort();
		errx(EXIT_FAILURE, "uid %u is already in use", up->u_uid);
	}
	/* if -g=uid was specified, check gid is unused */
	if (sync_uid_gid) {
		if (gid_exists((gid_t)up->u_uid)) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "gid %u is already in use", up->u_uid);
		}
		gid = up->u_uid;
	} else {
		if ((grp = find_group_info(up->u_primgrp)) == NULL) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "group %s not found", up->u_primgrp);
		}
		gid = grp->gr_gid;
	}
	/* check name isn't already in use */
	if (!(up->u_flags & F_DUPUID) && user_exists(login_name)) {
		close(ptmpfd);
		pw_abort();
		errx(EXIT_FAILURE, "already a `%s' user", login_name);
	}
	if (up->u_flags & F_HOMEDIR) {
		if (strlcpy(home, up->u_home, sizeof(home)) >= sizeof(home)) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "home directory `%s' too long",
			    up->u_home);
		}
	} else {
		/* if home directory hasn't been given, make it up */
		if (snprintf(home, sizeof(home), "%s/%s", up->u_basedir,
		    login_name) >= sizeof(home)) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "home directory `%s/%s' too long",
			    up->u_basedir, login_name);
		}
	}
	if (!scantime(&inactive, up->u_inactive)) {
		warnx("Warning: inactive time `%s' invalid, password expiry off",
				up->u_inactive);
	}
	if (!scantime(&expire, up->u_expire)) {
		warnx("Warning: expire time `%s' invalid, account expiry off",
				up->u_expire);
	}
	if (lstat(home, &st) == -1 && !(up->u_flags & F_MKDIR) &&
	    strcmp(home, _PATH_NONEXISTENT) != 0) {
		warnx("Warning: home directory `%s' doesn't exist, and -m was"
		    " not specified", home);
	}
	(void) strlcpy(password, up->u_password ? up->u_password : "*",
	    sizeof(password));
	cc = snprintf(buf, sizeof(buf), "%s:%s:%u:%u:%s:%lld:%lld:%s:%s:%s\n",
	    login_name,
	    password,
	    up->u_uid,
	    gid,
	    up->u_class,
	    (long long) inactive,
	    (long long) expire,
	    up->u_comment,
	    home,
	    up->u_shell);
	if (cc >= sizeof(buf) || cc < 0 ||
	    cc + expand_len(up->u_comment, login_name) >= 1023) {
		close(ptmpfd);
		pw_abort();
		errx(EXIT_FAILURE, "can't add `%s', line too long", buf);
	}
	if (write(ptmpfd, buf, (size_t) cc) != cc) {
		int saved_errno = errno;
		close(ptmpfd);
		pw_abort();
		errc(EXIT_FAILURE, saved_errno, "can't add `%s'", buf);
	}
	if (yp) {
		/* put back the + line */
		cc = snprintf(buf, sizeof(buf), "+:*::::::::\n");
		if (cc < 0 || cc >= sizeof(buf)) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "can't add `%s', line too long", buf);
		}
		if (write(ptmpfd, buf, (size_t) cc) != cc) {
			int saved_errno = errno;
			close(ptmpfd);
			pw_abort();
			errc(EXIT_FAILURE, saved_errno, "can't add `%s'", buf);
		}
		/* copy the entries following it, if any */
		while (fgets(buf, sizeof(buf), fp) != NULL) {
			cc = strlen(buf);
			if (write(ptmpfd, buf, (size_t)(cc)) != cc) {
				int saved_errno = errno;
				fclose(fp);
				close(ptmpfd);
				pw_abort();
				errc(EXIT_FAILURE, saved_errno,
				    "short write to /etc/ptmp (not %d chars)",
				    cc);
			}
		}
		if (ferror(fp)) {
			int saved_errno = errno;
			fclose(fp);
			close(ptmpfd);
			pw_abort();
			errc(EXIT_FAILURE, saved_errno, "read error on %s",
			    _PATH_MASTERPASSWD);
		}
	}
	if (up->u_flags & F_MKDIR) {
		if (lstat(home, &st) == 0) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "home directory `%s' already exists",
			    home);
		} else {
			char idstr[64];
			const char *mkdir_argv[] =
			    { "mkdir", "-p", home, NULL };
			const char *chown_argv[] =
			    { "chown", "-RP", idstr, home, NULL };
			const char *chmod_argv[] =
			    { "chmod", "-R", "u+w", home, NULL };

			if (run(MKDIR, mkdir_argv) != 0) {
				int saved_errno = errno;
				close(ptmpfd);
				pw_abort();
				errc(EXIT_FAILURE, saved_errno,
				    "can't mkdir `%s'", home);
			}
			(void) copydotfiles(up->u_skeldir, home);
			(void) snprintf(idstr, sizeof(idstr), "%u:%u",
			    up->u_uid, gid);
			(void) run(CHOWN, chown_argv);
			(void) run(CHMOD, chmod_argv);
		}
	}
	if (strcmp(up->u_primgrp, "=uid") == 0 && !group_exists(login_name) &&
	    !creategid(login_name, gid, "")) {
		close(ptmpfd);
		pw_abort();
		errx(EXIT_FAILURE, "can't create gid %u for login name %s",
		    gid, login_name);
	}
	if (up->u_groupc > 0 && !append_group(login_name, up->u_groupc, up->u_groupv)) {
		close(ptmpfd);
		pw_abort();
		errx(EXIT_FAILURE, "can't append `%s' to new groups", login_name);
	}
	fclose(fp);
	close(ptmpfd);
	if (pw_mkdb(yp ? NULL : login_name, 0) == -1) {
		pw_abort();
		err(EXIT_FAILURE, "pw_mkdb failed");
	}
	syslog(LOG_INFO, "new user added: name=%s, uid=%u, gid=%u, home=%s, shell=%s",
		login_name, up->u_uid, gid, home, up->u_shell);
	return 1;
}

/* remove a user from the groups file */
static int
rm_user_from_groups(char *login_name)
{
	struct stat	st;
	size_t		login_len;
	FILE		*from;
	FILE		*to;
	char		buf[LINE_MAX];
	char		f[MaxFileNameLen];
	char		*cp, *ep;
	int		fd;
	int		cc;

	login_len = strlen(login_name);
	if ((from = fopen(_PATH_GROUP, "r")) == NULL) {
		warn("can't remove gid for `%s': can't open `%s'",
		    login_name, _PATH_GROUP);
		return 0;
	}
	if (flock(fileno(from), LOCK_EX | LOCK_NB) == -1) {
		warn("can't lock `%s'", _PATH_GROUP);
	}
	(void) fstat(fileno(from), &st);
	(void) snprintf(f, sizeof(f), "%s.XXXXXXXX", _PATH_GROUP);
	if ((fd = mkstemp(f)) == -1) {
		warn("can't remove gid for `%s': mkstemp failed", login_name);
		fclose(from);
		return 0;
	}
	if ((to = fdopen(fd, "w")) == NULL) {
		warn("can't remove gid for `%s': fdopen `%s' failed",
		    login_name, f);
		fclose(from);
		close(fd);
		unlink(f);
		return 0;
	}
	while (fgets(buf, sizeof(buf), from) != NULL) {
		cc = strlen(buf);
		if (cc > 0 && buf[cc - 1] != '\n' && !feof(from)) {
			while (fgetc(from) != '\n' && !feof(from))
				cc++;
			warnx("%s: line `%s' too long (%d bytes), skipping",
			    _PATH_GROUP, buf, cc);
			continue;
		}

		/* Break out the group list. */
		for (cp = buf, cc = 0; *cp != '\0' && cc < 3; cp++) {
			if (*cp == ':')
				cc++;
		}
		if (cc != 3) {
			buf[strcspn(buf, "\n")] = '\0';
			warnx("Malformed entry `%s'. Skipping", buf);
			continue;
		}
		while ((cp = strstr(cp, login_name)) != NULL) {
			if ((cp[-1] == ':' || cp[-1] == ',') &&
			    (cp[login_len] == ',' || cp[login_len] == '\n')) {
				ep = cp + login_len;
				if (cp[login_len] == ',')
					ep++;
				else if (cp[-1] == ',')
					cp--;
				memmove(cp, ep, strlen(ep) + 1);
			} else {
				if ((cp = strchr(cp, ',')) == NULL)
					break;
				cp++;
			}
		}
		if (fwrite(buf, strlen(buf), 1, to) != 1) {
			warn("can't remove gid for `%s': short write to `%s'",
			    login_name, f);
			fclose(from);
			fclose(to);
			unlink(f);
			return 0;
		}
	}
	(void) fchmod(fileno(to), st.st_mode & 0777);
	fclose(from);
	if (fclose(to) == EOF) {
		warn("can't remove gid for `%s': short write to `%s'",
		    login_name, f);
		unlink(f);
		return 0;
	}
	if (rename(f, _PATH_GROUP) == -1) {
		warn("can't remove gid for `%s': can't rename `%s' to `%s'",
		    login_name, f, _PATH_GROUP);
		unlink(f);
		return 0;
	}
	return 1;
}

/* check that the user or group is local, not from YP/NIS */
static int
is_local(char *name, const char *file)
{
	FILE		*fp;
	char		buf[LINE_MAX];
	size_t		len;
	int		ret;
	int		cc;

	if ((fp = fopen(file, "r")) == NULL) {
		err(EXIT_FAILURE, "can't open `%s'", file);
	}
	len = strlen(name);
	for (ret = 0 ; fgets(buf, sizeof(buf), fp) != NULL ; ) {
		cc = strlen(buf);
		if (cc > 0 && buf[cc - 1] != '\n' && !feof(fp)) {
			while (fgetc(fp) != '\n' && !feof(fp))
				cc++;
			warnx("%s: line `%s' too long (%d bytes), skipping",
			    file, buf, cc);
			continue;
		}
		if (strncmp(buf, name, len) == 0 && buf[len] == ':') {
			ret = 1;
			break;
		}
	}
	fclose(fp);
	return ret;
}

/* modify a user */
static int
moduser(char *login_name, char *newlogin, user_t *up)
{
	struct passwd	*pwp = NULL;
	struct group	*grp;
	const char	*homedir;
	char		buf[LINE_MAX];
	char		acctlock_str[] = "-";
	char		pwlock_str[] = "*";
	char		pw_len[PasswordLength + 1];
	char		shell_len[MaxShellNameLen];
	char		*shell_last_char;
	size_t		colonc, loginc;
	size_t		cc;
	size_t		shell_buf;
	FILE		*master;
	char		newdir[MaxFileNameLen];
	char		*colon;
	char		*pw_tmp = NULL;
	char		*shell_tmp = NULL;
	int		len;
	int		locked = 0;
	int		unlocked = 0;
	int		masterfd;
	int		ptmpfd;
	int		rval;
	int		i;

	if (!valid_login(newlogin)) {
		errx(EXIT_FAILURE, "`%s' is not a valid login name", login_name);
	}
	if ((pwp = getpwnam_shadow(login_name)) == NULL) {
		errx(EXIT_FAILURE, "No such user `%s'", login_name);
	}
	if (up != NULL) {
		if ((*pwp->pw_passwd != '\0') &&
		    (up->u_flags & F_PASSWORD) == 0) {
			up->u_flags |= F_PASSWORD;
			strsave(&up->u_password, pwp->pw_passwd);
			explicit_bzero(pwp->pw_passwd, strlen(pwp->pw_passwd));
		}
	}
	endpwent();

	if (pledge("stdio rpath wpath cpath fattr flock proc exec getpw id",
	    NULL) == -1)
		err(1, "pledge");

	if (!is_local(login_name, _PATH_MASTERPASSWD)) {
		errx(EXIT_FAILURE, "User `%s' must be a local user", login_name);
	}
	if (up != NULL) {
		if ((up->u_flags & (F_ACCTLOCK | F_ACCTUNLOCK)) && (pwp->pw_uid < 1000))
			errx(EXIT_FAILURE, "(un)locking is not supported for the `%s' account", pwp->pw_name);
	}
	/* keep dir name in case we need it for '-m' */
	homedir = pwp->pw_dir;

	/* get the last char of the shell in case we need it for '-U' or '-Z' */
	shell_last_char = pwp->pw_shell+strlen(pwp->pw_shell) - 1;

	if ((masterfd = open(_PATH_MASTERPASSWD, O_RDONLY)) == -1) {
		err(EXIT_FAILURE, "can't open `%s'", _PATH_MASTERPASSWD);
	}
	if (flock(masterfd, LOCK_EX | LOCK_NB) == -1) {
		err(EXIT_FAILURE, "can't lock `%s'", _PATH_MASTERPASSWD);
	}
	pw_init();
	if ((ptmpfd = pw_lock(WAITSECS)) == -1) {
		int saved_errno = errno;
		close(masterfd);
		errc(EXIT_FAILURE, saved_errno, "can't obtain pw_lock");
	}
	if ((master = fdopen(masterfd, "r")) == NULL) {
		int saved_errno = errno;
		close(masterfd);
		close(ptmpfd);
		pw_abort();
		errc(EXIT_FAILURE, saved_errno, "can't fdopen fd for %s",
		    _PATH_MASTERPASSWD);
	}
	if (up != NULL) {
		if (up->u_flags & F_USERNAME) {
			/* if changing name, check new name isn't already in use */
			if (strcmp(login_name, newlogin) != 0 &&
			    user_exists(newlogin)) {
				close(ptmpfd);
				pw_abort();
				errx(EXIT_FAILURE, "already a `%s' user", newlogin);
			}
			pwp->pw_name = newlogin;

			/*
			 * Provide a new directory name in case the
			 * home directory is to be moved.
			 */
			if (up->u_flags & F_MKDIR) {
				(void) snprintf(newdir, sizeof(newdir),
				    "%s/%s", up->u_basedir, newlogin);
				pwp->pw_dir = newdir;
			}
		}
		if (up->u_flags & F_PASSWORD) {
			if (up->u_password != NULL)
				pwp->pw_passwd = up->u_password;
		}
		if (up->u_flags & F_ACCTLOCK) {
			/* lock the account */
			if (*shell_last_char != *acctlock_str) {
				shell_tmp = malloc(strlen(pwp->pw_shell) + sizeof(acctlock_str));
				if (shell_tmp == NULL) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE, "account lock: cannot allocate memory");
				}
				strlcpy(shell_tmp, pwp->pw_shell, sizeof(shell_len));
				strlcat(shell_tmp, acctlock_str, sizeof(shell_len));
				pwp->pw_shell = shell_tmp;
			} else {
				locked++;
			}
			/* lock the password */
			if (strncmp(pwp->pw_passwd, pwlock_str, sizeof(pwlock_str)-1) != 0) {
				pw_tmp = malloc(strlen(pwp->pw_passwd) + sizeof(pwlock_str));
				if (pw_tmp == NULL) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE, "password lock: cannot allocate memory");
				}
				strlcpy(pw_tmp, pwlock_str, sizeof(pw_len));
				strlcat(pw_tmp, pwp->pw_passwd, sizeof(pw_len));
				pwp->pw_passwd = pw_tmp;
			} else {
				locked++;
			}

			if (locked > 1)
				warnx("account `%s' is already locked", pwp->pw_name);
		}
		if (up->u_flags & F_ACCTUNLOCK) {
			/* unlock the password */
			if (strcmp(pwp->pw_passwd, pwlock_str) != 0 &&
			    strcmp(pwp->pw_passwd, "*************") != 0) {
				if (strncmp(pwp->pw_passwd, pwlock_str, sizeof(pwlock_str)-1) == 0) {
					pwp->pw_passwd += sizeof(pwlock_str)-1;
				} else {
					unlocked++;
				}
			} else {
				warnx("account `%s' has no password: cannot fully unlock", pwp->pw_name);
			}
			/* unlock the account */
			if (*shell_last_char == *acctlock_str) {
				shell_buf = strlen(pwp->pw_shell) + 2 - sizeof(acctlock_str);
				shell_tmp = malloc(shell_buf);
				if (shell_tmp == NULL) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE, "unlock: cannot allocate memory");
				}
				strlcpy(shell_tmp, pwp->pw_shell, shell_buf);
				pwp->pw_shell = shell_tmp;
			} else {
				unlocked++;
			}

			if (unlocked > 1)
				warnx("account `%s' is not locked", pwp->pw_name);
		}
		if (up->u_flags & F_UID) {
			/* check uid isn't already allocated */
			if (!(up->u_flags & F_DUPUID) &&
			    uid_exists((uid_t)up->u_uid)) {
				close(ptmpfd);
				pw_abort();
				errx(EXIT_FAILURE, "uid %u is already in use", up->u_uid);
			}
			pwp->pw_uid = up->u_uid;
		}
		if (up->u_flags & F_GROUP) {
			/* if -g=uid was specified, check gid is unused */
			if (strcmp(up->u_primgrp, "=uid") == 0) {
				if (gid_exists((gid_t)pwp->pw_uid)) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE, "gid %u is already "
					    "in use", pwp->pw_uid);
				}
				pwp->pw_gid = pwp->pw_uid;
				if (!creategid(newlogin, pwp->pw_gid, "")) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE, "could not create "
					    "group %s with gid %u", newlogin,
					    pwp->pw_gid);
				}
			} else {
				if ((grp = find_group_info(up->u_primgrp)) == NULL) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE, "group %s not found",
					    up->u_primgrp);
				}
				pwp->pw_gid = grp->gr_gid;
			}
		}
		if (up->u_flags & F_INACTIVE) {
			if (!scantime(&pwp->pw_change, up->u_inactive)) {
				warnx("Warning: inactive time `%s' invalid, password expiry off",
					up->u_inactive);
			}
		}
		if (up->u_flags & F_EXPIRE) {
			if (!scantime(&pwp->pw_expire, up->u_expire)) {
				warnx("Warning: expire time `%s' invalid, account expiry off",
					up->u_expire);
			}
		}
		if (up->u_flags & F_COMMENT)
			pwp->pw_gecos = up->u_comment;
		if (up->u_flags & F_HOMEDIR)
			pwp->pw_dir = up->u_home;
		if (up->u_flags & F_SHELL)
			pwp->pw_shell = up->u_shell;
		if (up->u_flags & F_CLASS) {
			if (!valid_class(up->u_class)) {
				close(ptmpfd);
				pw_abort();
				errx(EXIT_FAILURE,
				    "No such login class `%s'", up->u_class);
			}
			pwp->pw_class = up->u_class;
		}
	}
	loginc = strlen(login_name);
	while (fgets(buf, sizeof(buf), master) != NULL) {
		if ((colon = strchr(buf, ':')) == NULL) {
			warnx("Malformed entry `%s'. Skipping", buf);
			continue;
		}
		colonc = (size_t)(colon - buf);
		if (strncmp(login_name, buf, loginc) == 0 && loginc == colonc) {
			if (up != NULL) {
				if ((len = snprintf(buf, sizeof(buf),
				    "%s:%s:%u:%u:%s:%lld:%lld:%s:%s:%s\n",
				    newlogin,
				    pwp->pw_passwd,
				    pwp->pw_uid,
				    pwp->pw_gid,
				    pwp->pw_class,
				    (long long)pwp->pw_change,
				    (long long)pwp->pw_expire,
				    pwp->pw_gecos,
				    pwp->pw_dir,
				    pwp->pw_shell)) >= sizeof(buf) || len < 0 ||
				    len + expand_len(pwp->pw_gecos, newlogin)
				    >= 1023) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE, "can't add `%s', "
					    "line too long (%zu bytes)", buf,
					    len + expand_len(pwp->pw_gecos,
					    newlogin));
				}
				if (write(ptmpfd, buf, len) != len) {
					int saved_errno = errno;
					close(ptmpfd);
					pw_abort();
					errc(EXIT_FAILURE, saved_errno,
					    "can't add `%s'", buf);
				}
			}
		} else {
			len = strlen(buf);
			if ((cc = write(ptmpfd, buf, len)) != len) {
				int saved_errno = errno;
				close(masterfd);
				close(ptmpfd);
				pw_abort();
				errc(EXIT_FAILURE, saved_errno,
				    "short write to /etc/ptmp (%lld not %lld chars)",
				    (long long)cc, (long long)len);
			}
		}
	}
	if (up != NULL) {
		const char *mv_argv[] = { "mv", homedir, pwp->pw_dir, NULL };
		if ((up->u_flags & F_MKDIR) &&
		    run(MV, mv_argv) != 0) {
			int saved_errno = errno;
			close(ptmpfd);
			pw_abort();
			errc(EXIT_FAILURE, saved_errno,
			    "can't move `%s' to `%s'", homedir, pwp->pw_dir);
		}
		if (up->u_flags & F_SETSECGROUP) {
			for (i = 0 ; i < up->u_groupc ; i++) {
				if (!group_exists(up->u_groupv[i])) {
					close(ptmpfd);
					pw_abort();
					errx(EXIT_FAILURE,
					    "aborting, group `%s' does not exist",
					    up->u_groupv[i]);
				}
			}
			if (!rm_user_from_groups(newlogin)) {
				close(ptmpfd);
				pw_abort();
				errx(EXIT_FAILURE,
				    "can't reset groups for `%s'", newlogin);
			}
		}
		if (up->u_groupc > 0) {
		    if (!append_group(newlogin, up->u_groupc, up->u_groupv)) {
			close(ptmpfd);
			pw_abort();
			errx(EXIT_FAILURE, "can't append `%s' to new groups",
			    newlogin);
		    }
		}
	}
	fclose(master);
	close(ptmpfd);
	if (up != NULL && strcmp(login_name, newlogin) == 0)
		rval = pw_mkdb(login_name, 0);
	else
		rval = pw_mkdb(NULL, 0);
	if (rval == -1) {
		pw_abort();
		err(EXIT_FAILURE, "pw_mkdb failed");
	}
	if (up == NULL) {
		syslog(LOG_INFO, "user removed: name=%s", login_name);
	} else if (strcmp(login_name, newlogin) == 0) {
		syslog(LOG_INFO, "user information modified: name=%s, uid=%u, gid=%u, home=%s, shell=%s",
			login_name, pwp->pw_uid, pwp->pw_gid, pwp->pw_dir, pwp->pw_shell);
	} else {
		syslog(LOG_INFO, "user information modified: name=%s, new name=%s, uid=%u, gid=%u, home=%s, shell=%s",
			login_name, newlogin, pwp->pw_uid, pwp->pw_gid, pwp->pw_dir, pwp->pw_shell);
	}
	free(pw_tmp);
	free(shell_tmp);
	return 1;
}

/* print out usage message, and then exit */
void
usermgmt_usage(const char *prog)
{
	if (strcmp(prog, "useradd") == 0) {
		fprintf(stderr, "usage: %s -D [-b base-directory] "
		    "[-e expiry-time] [-f inactive-time]\n"
		    "               [-g gid | name | =uid] [-k skel-directory] "
		    "[-L login-class]\n"
		    "               [-r low..high] [-s shell]\n", prog);
		fprintf(stderr, "       %s [-mov] [-b base-directory] "
		    "[-c comment] [-d home-directory]\n"
		    "               [-e expiry-time] [-f inactive-time]\n"
		    "               [-G secondary-group[,group,...]] "
		    "[-g gid | name | =uid]\n"
		    "               [-k skel-directory] [-L login-class] "
		    "[-p password] [-r low..high]\n"
		    "               [-s shell] [-u uid] user\n", prog);
	} else if (strcmp(prog, "usermod") == 0) {
		fprintf(stderr, "usage: %s [-moUvZ] "
		    "[-c comment] [-d home-directory] [-e expiry-time]\n"
		    "               [-f inactive-time] "
		    "[-G secondary-group[,group,...]]\n"
		    "               [-g gid | name | =uid] [-L login-class] "
		    "[-l new-login]\n"
		    "               [-p password] "
		    "[-S secondary-group[,group,...]]\n"
		    "               [-s shell] [-u uid] user\n",
		    prog);
	} else if (strcmp(prog, "userdel") == 0) {
		fprintf(stderr, "usage: %s -D [-p preserve-value]\n",
		    prog);
		fprintf(stderr, "       %s [-rv] [-p preserve-value] "
		    "user\n", prog);
	} else if (strcmp(prog, "userinfo") == 0) {
		fprintf(stderr, "usage: %s [-e] user\n", prog);
	} else if (strcmp(prog, "groupadd") == 0) {
		fprintf(stderr, "usage: %s [-ov] [-g gid] group\n",
		    prog);
	} else if (strcmp(prog, "groupdel") == 0) {
		fprintf(stderr, "usage: %s [-v] group\n", prog);
	} else if (strcmp(prog, "groupmod") == 0) {
		fprintf(stderr, "usage: %s [-ov] [-g gid] [-n newname] "
		    "group\n", prog);
	} else if (strcmp(prog, "user") == 0 || strcmp(prog, "group") == 0) {
		fprintf(stderr, "usage: %s [add | del | mod"
		" | info"
		"] ...\n",
		    prog);
	} else if (strcmp(prog, "groupinfo") == 0) {
		fprintf(stderr, "usage: %s [-e] group\n", prog);
	} else {
		fprintf(stderr, "This program must be called as {user,group}{add,del,mod,info},\n%s is not an understood name.\n", prog);
	}
	exit(EXIT_FAILURE);
}

int
useradd(int argc, char **argv)
{
	user_t	u;
	const char *errstr;
	int	defaultfield;
	int	bigD;
	int	c;
	int	i;

	memset(&u, 0, sizeof(u));
	read_defaults(&u);
	u.u_uid = -1;
	defaultfield = bigD = 0;
	while ((c = getopt(argc, argv, "DG:L:b:c:d:e:f:g:k:mop:r:s:u:v")) != -1) {
		switch(c) {
		case 'D':
			bigD = 1;
			break;
		case 'G':
			while ((u.u_groupv[u.u_groupc] = strsep(&optarg, ",")) != NULL &&
			    u.u_groupc < NGROUPS_MAX - 2) {
				if (u.u_groupv[u.u_groupc][0] != 0) {
					u.u_groupc++;
				}
			}
			if (optarg != NULL) {
				warnx("Truncated list of secondary groups to %d entries", NGROUPS_MAX - 2);
			}
			break;
		case 'b':
			defaultfield = 1;
			strsave(&u.u_basedir, optarg);
			break;
		case 'c':
			strsave(&u.u_comment, optarg);
			break;
		case 'd':
			strsave(&u.u_home, optarg);
			u.u_flags |= F_HOMEDIR;
			break;
		case 'e':
			defaultfield = 1;
			strsave(&u.u_expire, optarg);
			break;
		case 'f':
			defaultfield = 1;
			strsave(&u.u_inactive, optarg);
			break;
		case 'g':
			defaultfield = 1;
			strsave(&u.u_primgrp, optarg);
			break;
		case 'k':
			defaultfield = 1;
			strsave(&u.u_skeldir, optarg);
			break;
		case 'L':
			defaultfield = 1;
			strsave(&u.u_class, optarg);
			break;
		case 'm':
			u.u_flags |= F_MKDIR;
			break;
		case 'o':
			u.u_flags |= F_DUPUID;
			break;
		case 'p':
			strsave(&u.u_password, optarg);
			explicit_bzero(optarg, strlen(optarg));
			break;
		case 'r':
			defaultfield = 1;
			if (!save_range(&u, optarg))
				exit(EXIT_FAILURE);
			break;
		case 's':
			defaultfield = 1;
			strsave(&u.u_shell, optarg);
			break;
		case 'u':
			u.u_uid = strtonum(optarg, -1, UID_MAX, &errstr);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "When using [-u uid], the uid must be numeric");
			}
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("useradd");
		}
	}

	if (pledge("stdio rpath wpath cpath fattr flock proc exec getpw id",
	    NULL) == -1)
		err(1, "pledge");

	if (bigD) {
		if (defaultfield) {
			checkeuid();
			return setdefaults(&u) ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		printf("group\t\t%s\n", u.u_primgrp);
		printf("base_dir\t%s\n", u.u_basedir);
		printf("skel_dir\t%s\n", u.u_skeldir);
		printf("shell\t\t%s\n", u.u_shell);
		printf("class\t\t%s\n", u.u_class);
		printf("inactive\t%s\n", (u.u_inactive == NULL) ? UNSET_INACTIVE : u.u_inactive);
		printf("expire\t\t%s\n", (u.u_expire == NULL) ? UNSET_EXPIRY : u.u_expire);
		for (i = 0 ; i < u.u_rc ; i++) {
			printf("range\t\t%u..%u\n", u.u_rv[i].r_from, u.u_rv[i].r_to);
		}
		return EXIT_SUCCESS;
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("useradd");
	}
	checkeuid();
	openlog("useradd", LOG_PID, LOG_USER);
	return adduser(*argv, &u) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
usermod(int argc, char **argv)
{
	user_t	u;
	char	newuser[MaxUserNameLen + 1];
	int	c, have_new_user;
	const char *errstr;

	memset(&u, 0, sizeof(u));
	memset(newuser, 0, sizeof(newuser));
	read_defaults(&u);
	free(u.u_primgrp);
	u.u_primgrp = NULL;
	have_new_user = 0;
	while ((c = getopt(argc, argv, "G:L:S:UZc:d:e:f:g:l:mop:s:u:v")) != -1) {
		switch(c) {
		case 'G':
			while ((u.u_groupv[u.u_groupc] = strsep(&optarg, ",")) != NULL &&
			    u.u_groupc < NGROUPS_MAX - 2) {
				if (u.u_groupv[u.u_groupc][0] != 0) {
					u.u_groupc++;
				}
			}
			if (optarg != NULL) {
				warnx("Truncated list of secondary groups to %d entries", NGROUPS_MAX - 2);
			}
			u.u_flags |= F_SECGROUP;
			break;
		case 'S':
			while ((u.u_groupv[u.u_groupc] = strsep(&optarg, ",")) != NULL &&
			    u.u_groupc < NGROUPS_MAX - 2) {
				if (u.u_groupv[u.u_groupc][0] != 0) {
					u.u_groupc++;
				}
			}
			if (optarg != NULL) {
				warnx("Truncated list of secondary groups to %d entries", NGROUPS_MAX - 2);
			}
			u.u_flags |= F_SETSECGROUP;
			break;
		case 'U':
			u.u_flags |= F_ACCTUNLOCK;
			break;
		case 'Z':
			u.u_flags |= F_ACCTLOCK;
			break;
		case 'c':
			strsave(&u.u_comment, optarg);
			u.u_flags |= F_COMMENT;
			break;
		case 'd':
			strsave(&u.u_home, optarg);
			u.u_flags |= F_HOMEDIR;
			break;
		case 'e':
			strsave(&u.u_expire, optarg);
			u.u_flags |= F_EXPIRE;
			break;
		case 'f':
			strsave(&u.u_inactive, optarg);
			u.u_flags |= F_INACTIVE;
			break;
		case 'g':
			strsave(&u.u_primgrp, optarg);
			u.u_flags |= F_GROUP;
			break;
		case 'l':
			if (strlcpy(newuser, optarg, sizeof(newuser)) >=
			    sizeof(newuser))
				errx(EXIT_FAILURE, "username `%s' too long",
				    optarg);
			have_new_user = 1;
			u.u_flags |= F_USERNAME;
			break;
		case 'L':
			strsave(&u.u_class, optarg);
			u.u_flags |= F_CLASS;
			break;
		case 'm':
			u.u_flags |= F_MKDIR;
			break;
		case 'o':
			u.u_flags |= F_DUPUID;
			break;
		case 'p':
			strsave(&u.u_password, optarg);
			explicit_bzero(optarg, strlen(optarg));
			u.u_flags |= F_PASSWORD;
			break;
		case 's':
			strsave(&u.u_shell, optarg);
			u.u_flags |= F_SHELL;
			break;
		case 'u':
			u.u_uid = strtonum(optarg, -1, UID_MAX, &errstr);
			u.u_flags |= F_UID;
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "When using [-u uid], the uid must be numeric");
			}
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("usermod");
		}
	}

	if ((u.u_flags & F_MKDIR) && !(u.u_flags & F_HOMEDIR) &&
	    !(u.u_flags & F_USERNAME)) {
		warnx("option 'm' useless without 'd' or 'l' -- ignored");
		u.u_flags &= ~F_MKDIR;
	}
	if ((u.u_flags & F_SECGROUP) && (u.u_flags & F_SETSECGROUP))
		errx(EXIT_FAILURE, "options 'G' and 'S' are mutually exclusive");
	if ((u.u_flags & F_ACCTLOCK) && (u.u_flags & F_ACCTUNLOCK))
		errx(EXIT_FAILURE, "options 'U' and 'Z' are mutually exclusive");
	if ((u.u_flags & F_PASSWORD) && (u.u_flags & (F_ACCTLOCK | F_ACCTUNLOCK)))
		errx(EXIT_FAILURE, "options 'U' or 'Z' with 'p' are mutually exclusive");
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("usermod");
	}
	checkeuid();
	openlog("usermod", LOG_PID, LOG_USER);
	return moduser(*argv, (have_new_user) ? newuser : *argv, &u) ?
	    EXIT_SUCCESS : EXIT_FAILURE;
}

int
userdel(int argc, char **argv)
{
	struct passwd	*pwp;
	user_t		u;
	int		defaultfield;
	int		rmhome;
	int		bigD;
	int		c;

	memset(&u, 0, sizeof(u));
	read_defaults(&u);
	defaultfield = bigD = rmhome = 0;
	while ((c = getopt(argc, argv, "Dp:rv")) != -1) {
		switch(c) {
		case 'D':
			bigD = 1;
			break;
		case 'p':
			defaultfield = 1;
			u.u_preserve = (strcmp(optarg, "true") == 0) ? 1 :
					(strcmp(optarg, "yes") == 0) ? 1 :
					 strtonum(optarg, INT_MIN, INT_MAX, NULL);
			break;
		case 'r':
			rmhome = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("userdel");
		}
	}
	if (bigD) {
		if (defaultfield) {
			checkeuid();
			return setdefaults(&u) ? EXIT_SUCCESS : EXIT_FAILURE;
		}
		printf("preserve\t%s\n", (u.u_preserve) ? "true" : "false");
		return EXIT_SUCCESS;
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("userdel");
	}

	if (pledge("stdio rpath wpath cpath fattr flock proc exec getpw id",
	    NULL) == -1)
		err(1, "pledge");

	checkeuid();
	if ((pwp = getpwnam(*argv)) == NULL) {
		warnx("No such user `%s'", *argv);
		return EXIT_FAILURE;
	}
	if (rmhome)
		(void)removehomedir(pwp->pw_name, pwp->pw_uid, pwp->pw_dir);
	if (u.u_preserve) {
		u.u_flags |= F_SHELL;
		strsave(&u.u_shell, NOLOGIN);
		strsave(&u.u_password, "*");
		u.u_flags |= F_PASSWORD;
		openlog("userdel", LOG_PID, LOG_USER);
		return moduser(*argv, *argv, &u) ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (!rm_user_from_groups(*argv)) {
		return 0;
	}
	openlog("userdel", LOG_PID, LOG_USER);
	return moduser(*argv, *argv, NULL) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* add a group */
int
groupadd(int argc, char **argv)
{
	int	dupgid;
	int	gid;
	int	c;
	const char *errstr;

	gid = -1;
	dupgid = 0;
	while ((c = getopt(argc, argv, "g:ov")) != -1) {
		switch(c) {
		case 'g':
			gid = strtonum(optarg, -1, GID_MAX, &errstr);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "When using [-g gid], the gid must be numeric");
			}
			break;
		case 'o':
			dupgid = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("groupadd");
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("groupadd");
	}

	if (pledge("stdio rpath wpath cpath fattr flock getpw", NULL) == -1)
		err(1, "pledge");

	checkeuid();
	if (!valid_group(*argv)) {
		errx(EXIT_FAILURE, "invalid group name `%s'", *argv);
	}
	if (gid < 0 && !getnextgid(&gid, LowGid, HighGid)) {
		errx(EXIT_FAILURE, "can't add group: can't get next gid");
	}
	if (!dupgid && gid_exists((gid_t)gid)) {
		errx(EXIT_FAILURE, "can't add group: gid %d is a duplicate", gid);
	}
	openlog("groupadd", LOG_PID, LOG_USER);
	if (!creategid(*argv, gid, "")) {
		errx(EXIT_FAILURE, "can't add group: problems with %s file",
		    _PATH_GROUP);
	}
	return EXIT_SUCCESS;
}

/* remove a group */
int
groupdel(int argc, char **argv)
{
	int	c;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch(c) {
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("groupdel");
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("groupdel");
	}
	checkeuid();
	openlog("groupdel", LOG_PID, LOG_USER);
	if (!group_exists(*argv)) {
		warnx("No such group: `%s'", *argv);
		return EXIT_FAILURE;
	}

	if (pledge("stdio rpath wpath cpath fattr flock", NULL) == -1)
		err(1, "pledge");

	if (!modify_gid(*argv, NULL)) {
		err(EXIT_FAILURE, "can't change %s file", _PATH_GROUP);
	}
	return EXIT_SUCCESS;
}

/* modify a group */
int
groupmod(int argc, char **argv)
{
	struct group	*grp;
	const char	*errstr;
	char		buf[LINE_MAX];
	char		*newname;
	char		**cpp;
	int		dupgid;
	int		gid;
	int		cc;
	int		c;

	gid = -1;
	dupgid = 0;
	newname = NULL;
	while ((c = getopt(argc, argv, "g:n:ov")) != -1) {
		switch(c) {
		case 'g':
			gid = strtonum(optarg, -1, GID_MAX, &errstr);
			if (errstr != NULL) {
				errx(EXIT_FAILURE, "When using [-g gid], the gid must be numeric");
			}
			break;
		case 'o':
			dupgid = 1;
			break;
		case 'n':
			strsave(&newname, optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("groupmod");
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("groupmod");
	}
	checkeuid();
	if (gid < 0 && newname == NULL) {
		errx(EXIT_FAILURE, "Nothing to change");
	}
	if (dupgid && gid < 0) {
		errx(EXIT_FAILURE, "Duplicate which gid?");
	}
	if ((grp = getgrnam(*argv)) == NULL) {
		errx(EXIT_FAILURE, "can't find group `%s' to modify", *argv);
	}

	if (pledge("stdio rpath wpath cpath fattr flock", NULL) == -1)
		err(1, "pledge");

	if (!is_local(*argv, _PATH_GROUP)) {
		errx(EXIT_FAILURE, "Group `%s' must be a local group", *argv);
	}
	if (newname != NULL && !valid_group(newname)) {
		errx(EXIT_FAILURE, "invalid group name `%s'", newname);
	}
	if ((cc = snprintf(buf, sizeof(buf), "%s:%s:%u:",
	    (newname) ? newname : grp->gr_name, grp->gr_passwd,
	    (gid < 0) ? grp->gr_gid : gid)) >= sizeof(buf) || cc < 0)
		errx(EXIT_FAILURE, "group `%s' entry too long", grp->gr_name);

	for (cpp = grp->gr_mem ; *cpp ; cpp++) {
		cc = strlcat(buf, *cpp, sizeof(buf)) + 1;
		if (cc >= sizeof(buf))
			errx(EXIT_FAILURE, "group `%s' entry too long",
			    grp->gr_name);
		if (cpp[1] != NULL) {
			buf[cc - 1] = ',';
			buf[cc] = '\0';
		}
	}
	cc = strlcat(buf, "\n", sizeof(buf));
	if (cc >= sizeof(buf))
		errx(EXIT_FAILURE, "group `%s' entry too long", grp->gr_name);

	openlog("groupmod", LOG_PID, LOG_USER);
	if (!modify_gid(*argv, buf))
		err(EXIT_FAILURE, "can't change %s file", _PATH_GROUP);
	return EXIT_SUCCESS;
}

/* display user information */
int
userinfo(int argc, char **argv)
{
	struct passwd	*pwp;
	struct group	*grp;
	char		**cpp;
	int		exists;
	int		i;

	exists = 0;
	while ((i = getopt(argc, argv, "ev")) != -1) {
		switch(i) {
		case 'e':
			exists = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("userinfo");
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("userinfo");
	}

	if (pledge("stdio getpw", NULL) == -1)
		err(1, "pledge");

	pwp = find_user_info(*argv);
	if (exists) {
		exit((pwp) ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	if (pwp == NULL) {
		errx(EXIT_FAILURE, "can't find user `%s'", *argv);
	}
	printf("login\t%s\n", pwp->pw_name);
	printf("passwd\t%s\n", pwp->pw_passwd);
	printf("uid\t%u\n", pwp->pw_uid);
	if ((grp = getgrgid(pwp->pw_gid)) == NULL)
		printf("groups\t%u", pwp->pw_gid);
	else
		printf("groups\t%s", grp->gr_name);
	while ((grp = getgrent()) != NULL) {
		for (cpp = grp->gr_mem ; *cpp ; cpp++) {
			if (strcmp(*cpp, pwp->pw_name) == 0 &&
			    grp->gr_gid != pwp->pw_gid)
				printf(" %s", grp->gr_name);
		}
	}
	fputc('\n', stdout);
	printf("change\t%s", pwp->pw_change ? ctime(&pwp->pw_change) : "NEVER\n");
	printf("class\t%s\n", pwp->pw_class);
	printf("gecos\t%s\n", pwp->pw_gecos);
	printf("dir\t%s\n", pwp->pw_dir);
	printf("shell\t%s\n", pwp->pw_shell);
	printf("expire\t%s", pwp->pw_expire ? ctime(&pwp->pw_expire) : "NEVER\n");
	return EXIT_SUCCESS;
}

/* display user information */
int
groupinfo(int argc, char **argv)
{
	struct group	*grp;
	char		**cpp;
	int		exists;
	int		i;

	exists = 0;
	while ((i = getopt(argc, argv, "ev")) != -1) {
		switch(i) {
		case 'e':
			exists = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usermgmt_usage("groupinfo");
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1) {
		usermgmt_usage("groupinfo");
	}

	if (pledge("stdio getpw", NULL) == -1)
		err(1, "pledge");

	grp = find_group_info(*argv);
	if (exists) {
		exit((grp) ? EXIT_SUCCESS : EXIT_FAILURE);
	}
	if (grp == NULL) {
		errx(EXIT_FAILURE, "can't find group `%s'", *argv);
	}
	printf("name\t%s\n", grp->gr_name);
	printf("passwd\t%s\n", grp->gr_passwd);
	printf("gid\t%u\n", grp->gr_gid);
	printf("members\t");
	for (cpp = grp->gr_mem ; *cpp ; cpp++) {
		printf("%s ", *cpp);
	}
	fputc('\n', stdout);
	return EXIT_SUCCESS;
}
