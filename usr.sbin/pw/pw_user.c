/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <sys/param.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <libutil.h>
#include <login_cap.h>
#include <paths.h>
#include <string.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#include "pw.h"
#include "bitmap.h"
#include "psdate.h"

#define LOGNAMESIZE (MAXLOGNAME-1)

static		char locked_str[] = "*LOCKED*";

static struct passwd fakeuser = {
	"nouser",
	"*",
	-1,
	-1,
	0,
	"",
	"User &",
	"/nonexistent",
	"/bin/sh",
	0,
	0
};

static int	 print_user(struct passwd *pwd, bool pretty, bool v7);
static uid_t	 pw_uidpolicy(struct userconf *cnf, intmax_t id);
static uid_t	 pw_gidpolicy(struct userconf *cnf, char *grname, char *nam,
    gid_t prefer, bool dryrun);
static char	*pw_homepolicy(struct userconf * cnf, char *homedir,
    const char *user);
static char	*pw_shellpolicy(struct userconf * cnf);
static char	*pw_password(struct userconf * cnf, char const * user,
    bool dryrun);
static char	*shell_path(char const * path, char *shells[], char *sh);
static void	rmat(uid_t uid);
static void	rmopie(char const * name);

static void
mkdir_home_parents(int dfd, const char *dir)
{
	struct stat st;
	char *dirs, *tmp;

	if (*dir != '/')
		errx(EX_DATAERR, "invalid base directory for home '%s'", dir);

	dir++;

	if (fstatat(dfd, dir, &st, 0) != -1) {
		if (S_ISDIR(st.st_mode))
			return;
		errx(EX_OSFILE, "root home `/%s' is not a directory", dir);
	}

	dirs = strdup(dir);
	if (dirs == NULL)
		errx(EX_UNAVAILABLE, "out of memory");

	tmp = strrchr(dirs, '/');
	if (tmp == NULL) {
		free(dirs);
		return;
	}
	tmp[0] = '\0';

	/*
	 * This is a kludge especially for Joerg :)
	 * If the home directory would be created in the root partition, then
	 * we really create it under /usr which is likely to have more space.
	 * But we create a symlink from cnf->home -> "/usr" -> cnf->home
	 */
	if (strchr(dirs, '/') == NULL) {
		asprintf(&tmp, "usr/%s", dirs);
		if (tmp == NULL)
			errx(EX_UNAVAILABLE, "out of memory");
		if (mkdirat(dfd, tmp, _DEF_DIRMODE) != -1 || errno == EEXIST) {
			fchownat(dfd, tmp, 0, 0, 0);
			symlinkat(tmp, dfd, dirs);
		}
		free(tmp);
	}
	tmp = dirs;
	if (fstatat(dfd, dirs, &st, 0) == -1) {
		while ((tmp = strchr(tmp + 1, '/')) != NULL) {
			*tmp = '\0';
			if (fstatat(dfd, dirs, &st, 0) == -1) {
				if (mkdirat(dfd, dirs, _DEF_DIRMODE) == -1)
					err(EX_OSFILE,  "'%s' (root home parent) is not a directory", dirs);
			}
			*tmp = '/';
		}
	}
	if (fstatat(dfd, dirs, &st, 0) == -1) {
		if (mkdirat(dfd, dirs, _DEF_DIRMODE) == -1)
			err(EX_OSFILE,  "'%s' (root home parent) is not a directory", dirs);
		fchownat(dfd, dirs, 0, 0, 0);
	}

	free(dirs);
}

static void
create_and_populate_homedir(struct userconf *cnf, struct passwd *pwd,
    const char *skeldir, mode_t homemode, bool update)
{
	int skelfd = -1;

	/* Create home parents directories */
	mkdir_home_parents(conf.rootfd, pwd->pw_dir);

	if (skeldir != NULL && *skeldir != '\0') {
		if (*skeldir == '/')
			skeldir++;
		skelfd = openat(conf.rootfd, skeldir, O_DIRECTORY|O_CLOEXEC);
	}

	copymkdir(conf.rootfd, pwd->pw_dir, skelfd, homemode, pwd->pw_uid,
	    pwd->pw_gid, 0);
	pw_log(cnf, update ? M_UPDATE : M_ADD, W_USER, "%s(%ju) home %s made",
	    pwd->pw_name, (uintmax_t)pwd->pw_uid, pwd->pw_dir);
}

static int
pw_set_passwd(struct passwd *pwd, int fd, bool precrypted, bool update)
{
	int		 b, istty;
	struct termios	 t, n;
	login_cap_t	*lc;
	char		line[_PASSWORD_LEN+1];
	char		*p;

	if (fd == '-') {
		if (!pwd->pw_passwd || *pwd->pw_passwd != '*') {
			pwd->pw_passwd = "*";	/* No access */
			return (1);
		}
		return (0);
	}

	if ((istty = isatty(fd))) {
		if (tcgetattr(fd, &t) == -1)
			istty = 0;
		else {
			n = t;
			n.c_lflag &= ~(ECHO);
			tcsetattr(fd, TCSANOW, &n);
			printf("%s%spassword for user %s:",
			    update ? "new " : "",
			    precrypted ? "encrypted " : "",
			    pwd->pw_name);
			fflush(stdout);
		}
	}
	b = read(fd, line, sizeof(line) - 1);
	if (istty) {	/* Restore state */
		tcsetattr(fd, TCSANOW, &t);
		fputc('\n', stdout);
		fflush(stdout);
	}

	if (b < 0)
		err(EX_IOERR, "-%c file descriptor",
		    precrypted ? 'H' : 'h');
	line[b] = '\0';
	if ((p = strpbrk(line, "\r\n")) != NULL)
		*p = '\0';
	if (!*line)
		errx(EX_DATAERR, "empty password read on file descriptor %d",
		    fd);
	if (precrypted) {
		if (strchr(line, ':') != NULL)
			errx(EX_DATAERR, "bad encrypted password");
		pwd->pw_passwd = strdup(line);
	} else {
		lc = login_getpwclass(pwd);
		if (lc == NULL ||
				login_setcryptfmt(lc, "sha512", NULL) == NULL)
			warn("setting crypt(3) format");
		login_close(lc);
		pwd->pw_passwd = pw_pwcrypt(line);
	}
	return (1);
}

static void
perform_chgpwent(const char *name, struct passwd *pwd, char *nispasswd)
{
	int rc;
	struct passwd *nispwd;

	/* duplicate for nis so that chgpwent is not modifying before NIS */
	if (nispasswd && *nispasswd == '/')
		nispwd = pw_dup(pwd);

	rc = chgpwent(name, pwd);
	if (rc == -1)
		errx(EX_IOERR, "user '%s' does not exist (NIS?)", pwd->pw_name);
	else if (rc != 0)
		err(EX_IOERR, "passwd file update");

	if (nispasswd && *nispasswd == '/') {
		rc = chgnispwent(nispasswd, name, nispwd);
		if (rc == -1)
			warn("User '%s' not found in NIS passwd", pwd->pw_name);
		else if (rc != 0)
			warn("NIS passwd update");
		/* NOTE: NIS-only update errors are not fatal */
	}
}

/*
 * The M_LOCK and M_UNLOCK functions simply add or remove
 * a "*LOCKED*" prefix from in front of the password to
 * prevent it decoding correctly, and therefore prevents
 * access. Of course, this only prevents access via
 * password authentication (not ssh, kerberos or any
 * other method that does not use the UNIX password) but
 * that is a known limitation.
 */
static int
pw_userlock(char *arg1, int mode)
{
	struct passwd *pwd = NULL;
	char *passtmp = NULL;
	char *name;
	bool locked = false;
	uid_t id = (uid_t)-1;

	if (geteuid() != 0)
		errx(EX_NOPERM, "you must be root");

	if (arg1 == NULL)
		errx(EX_DATAERR, "username or id required");

	name = arg1;
	if (arg1[strspn(name, "0123456789")] == '\0')
		id = pw_checkid(name, UID_MAX);

	pwd = GETPWNAM(pw_checkname(name, 0));
	if (pwd == NULL && id != (uid_t)-1) {
		pwd = GETPWUID(id);
		if (pwd != NULL)
			name = pwd->pw_name;
	}
	if (pwd == NULL) {
		if (id == (uid_t)-1)
			errx(EX_NOUSER, "no such name or uid `%ju'", (uintmax_t) id);
		errx(EX_NOUSER, "no such user `%s'", name);
	}

	if (name == NULL)
		name = pwd->pw_name;

	if (strncmp(pwd->pw_passwd, locked_str, sizeof(locked_str) -1) == 0)
		locked = true;
	if (mode == M_LOCK && locked)
		errx(EX_DATAERR, "user '%s' is already locked", pwd->pw_name);
	if (mode == M_UNLOCK && !locked)
		errx(EX_DATAERR, "user '%s' is not locked", pwd->pw_name);

	if (mode == M_LOCK) {
		asprintf(&passtmp, "%s%s", locked_str, pwd->pw_passwd);
		if (passtmp == NULL)	/* disaster */
			errx(EX_UNAVAILABLE, "out of memory");
		pwd->pw_passwd = passtmp;
	} else {
		pwd->pw_passwd += sizeof(locked_str)-1;
	}

	perform_chgpwent(name, pwd, NULL);
	free(passtmp);

	return (EXIT_SUCCESS);
}

static uid_t
pw_uidpolicy(struct userconf * cnf, intmax_t id)
{
	struct passwd  *pwd;
	struct bitmap   bm;
	uid_t           uid = (uid_t) - 1;

	/*
	 * Check the given uid, if any
	 */
	if (id >= 0) {
		uid = (uid_t) id;

		if ((pwd = GETPWUID(uid)) != NULL && conf.checkduplicate)
			errx(EX_DATAERR, "uid `%ju' has already been allocated",
			    (uintmax_t)pwd->pw_uid);
		return (uid);
	}
	/*
	 * We need to allocate the next available uid under one of
	 * two policies a) Grab the first unused uid b) Grab the
	 * highest possible unused uid
	 */
	if (cnf->min_uid >= cnf->max_uid) {	/* Sanity
						 * claus^H^H^H^Hheck */
		cnf->min_uid = 1000;
		cnf->max_uid = 32000;
	}
	bm = bm_alloc(cnf->max_uid - cnf->min_uid + 1);

	/*
	 * Now, let's fill the bitmap from the password file
	 */
	SETPWENT();
	while ((pwd = GETPWENT()) != NULL)
		if (pwd->pw_uid >= (uid_t) cnf->min_uid && pwd->pw_uid <= (uid_t) cnf->max_uid)
			bm_setbit(&bm, pwd->pw_uid - cnf->min_uid);
	ENDPWENT();

	/*
	 * Then apply the policy, with fallback to reuse if necessary
	 */
	if (cnf->reuse_uids || (uid = (uid_t) (bm_lastset(&bm) + cnf->min_uid + 1)) > cnf->max_uid)
		uid = (uid_t) (bm_firstunset(&bm) + cnf->min_uid);

	/*
	 * Another sanity check
	 */
	if (uid < cnf->min_uid || uid > cnf->max_uid)
		errx(EX_SOFTWARE, "unable to allocate a new uid - range fully used");
	bm_dealloc(&bm);
	return (uid);
}

static uid_t
pw_gidpolicy(struct userconf *cnf, char *grname, char *nam, gid_t prefer, bool dryrun)
{
	struct group   *grp;
	gid_t           gid = (uid_t) - 1;

	/*
	 * Check the given gid, if any
	 */
	SETGRENT();
	if (grname) {
		if ((grp = GETGRNAM(grname)) == NULL) {
			gid = pw_checkid(grname, GID_MAX);
			grp = GETGRGID(gid);
		}
		gid = grp->gr_gid;
	} else if ((grp = GETGRNAM(nam)) != NULL &&
	    (grp->gr_mem == NULL || grp->gr_mem[0] == NULL)) {
		gid = grp->gr_gid;  /* Already created? Use it anyway... */
	} else {
		intmax_t		grid = -1;

		/*
		 * We need to auto-create a group with the user's name. We
		 * can send all the appropriate output to our sister routine
		 * bit first see if we can create a group with gid==uid so we
		 * can keep the user and group ids in sync. We purposely do
		 * NOT check the gid range if we can force the sync. If the
		 * user's name dups an existing group, then the group add
		 * function will happily handle that case for us and exit.
		 */
		if (GETGRGID(prefer) == NULL)
			grid = prefer;
		if (dryrun) {
			gid = pw_groupnext(cnf, true);
		} else {
			if (grid == -1)
				grid =  pw_groupnext(cnf, true);
			groupadd(cnf, nam, grid, NULL, -1, false, false, false);
			if ((grp = GETGRNAM(nam)) != NULL)
				gid = grp->gr_gid;
		}
	}
	ENDGRENT();
	return (gid);
}

static char *
pw_homepolicy(struct userconf * cnf, char *homedir, const char *user)
{
	static char     home[128];

	if (homedir)
		return (homedir);

	if (cnf->home == NULL || *cnf->home == '\0')
		errx(EX_CONFIG, "no base home directory set");
	snprintf(home, sizeof(home), "%s/%s", cnf->home, user);

	return (home);
}

static char *
shell_path(char const * path, char *shells[], char *sh)
{
	if (sh != NULL && (*sh == '/' || *sh == '\0'))
		return sh;	/* specified full path or forced none */
	else {
		char           *p;
		char            paths[_UC_MAXLINE];

		/*
		 * We need to search paths
		 */
		strlcpy(paths, path, sizeof(paths));
		for (p = strtok(paths, ": \t\r\n"); p != NULL; p = strtok(NULL, ": \t\r\n")) {
			int             i;
			static char     shellpath[256];

			if (sh != NULL) {
				snprintf(shellpath, sizeof(shellpath), "%s/%s", p, sh);
				if (access(shellpath, X_OK) == 0)
					return shellpath;
			} else
				for (i = 0; i < _UC_MAXSHELLS && shells[i] != NULL; i++) {
					snprintf(shellpath, sizeof(shellpath), "%s/%s", p, shells[i]);
					if (access(shellpath, X_OK) == 0)
						return shellpath;
				}
		}
		if (sh == NULL)
			errx(EX_OSFILE, "can't find shell `%s' in shell paths", sh);
		errx(EX_CONFIG, "no default shell available or defined");
		return NULL;
	}
}

static char *
pw_shellpolicy(struct userconf * cnf)
{

	return shell_path(cnf->shelldir, cnf->shells, cnf->shell_default);
}

#define	SALTSIZE	32

static char const chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ./";

char *
pw_pwcrypt(char *password)
{
	int             i;
	char            salt[SALTSIZE + 1];
	char		*cryptpw;
	static char     buf[256];
	size_t		pwlen;

	/*
	 * Calculate a salt value
	 */
	for (i = 0; i < SALTSIZE; i++)
		salt[i] = chars[arc4random_uniform(sizeof(chars) - 1)];
	salt[SALTSIZE] = '\0';

	cryptpw = crypt(password, salt);
	if (cryptpw == NULL)
		errx(EX_CONFIG, "crypt(3) failure");
	pwlen = strlcpy(buf, cryptpw, sizeof(buf));
	assert(pwlen < sizeof(buf));
	return (buf);
}

static char *
pw_password(struct userconf * cnf, char const * user, bool dryrun)
{
	int             i, l;
	char            pwbuf[32];

	switch (cnf->default_password) {
	case P_NONE:		/* No password at all! */
		return "";
	case P_RANDOM:			/* Random password */
		l = (arc4random() % 8 + 8);	/* 8 - 16 chars */
		for (i = 0; i < l; i++)
			pwbuf[i] = chars[arc4random_uniform(sizeof(chars)-1)];
		pwbuf[i] = '\0';

		/*
		 * We give this information back to the user
		 */
		if (conf.fd == -1 && !dryrun) {
			if (isatty(STDOUT_FILENO))
				printf("Password for '%s' is: ", user);
			printf("%s\n", pwbuf);
			fflush(stdout);
		}
		break;
	case P_YES:		/* user's name */
		strlcpy(pwbuf, user, sizeof(pwbuf));
		break;
	case P_NO:		/* No login - default */
				/* FALLTHROUGH */
	default:
		return "*";
	}
	return pw_pwcrypt(pwbuf);
}

static int
print_user(struct passwd * pwd, bool pretty, bool v7)
{
	int		j;
	char           *p;
	struct group   *grp = GETGRGID(pwd->pw_gid);
	char            uname[60] = "User &", office[60] = "[None]",
			wphone[60] = "[None]", hphone[60] = "[None]";
	char		acexpire[32] = "[None]", pwexpire[32] = "[None]";
	struct tm *    tptr;

	if (!pretty) {
		p = v7 ? pw_make_v7(pwd) : pw_make(pwd);
		printf("%s\n", p);
		free(p);
		return (EXIT_SUCCESS);
	}

	if ((p = strtok(pwd->pw_gecos, ",")) != NULL) {
		strlcpy(uname, p, sizeof(uname));
		if ((p = strtok(NULL, ",")) != NULL) {
			strlcpy(office, p, sizeof(office));
			if ((p = strtok(NULL, ",")) != NULL) {
				strlcpy(wphone, p, sizeof(wphone));
				if ((p = strtok(NULL, "")) != NULL) {
					strlcpy(hphone, p, sizeof(hphone));
				}
			}
		}
	}
	/*
	 * Handle '&' in gecos field
	 */
	if ((p = strchr(uname, '&')) != NULL) {
		int             l = strlen(pwd->pw_name);
		int             m = strlen(p);

		memmove(p + l, p + 1, m);
		memmove(p, pwd->pw_name, l);
		*p = (char) toupper((unsigned char)*p);
	}
	if (pwd->pw_expire > (time_t)0 && (tptr = localtime(&pwd->pw_expire)) != NULL)
		strftime(acexpire, sizeof acexpire, "%c", tptr);
		if (pwd->pw_change > (time_t)0 && (tptr = localtime(&pwd->pw_change)) != NULL)
		strftime(pwexpire, sizeof pwexpire, "%c", tptr);
	printf("Login Name: %-15s   #%-12ju Group: %-15s   #%ju\n"
	       " Full Name: %s\n"
	       "      Home: %-26.26s      Class: %s\n"
	       "     Shell: %-26.26s     Office: %s\n"
	       "Work Phone: %-26.26s Home Phone: %s\n"
	       "Acc Expire: %-26.26s Pwd Expire: %s\n",
	       pwd->pw_name, (uintmax_t)pwd->pw_uid,
	       grp ? grp->gr_name : "(invalid)", (uintmax_t)pwd->pw_gid,
	       uname, pwd->pw_dir, pwd->pw_class,
	       pwd->pw_shell, office, wphone, hphone,
	       acexpire, pwexpire);
        SETGRENT();
	j = 0;
	while ((grp=GETGRENT()) != NULL) {
		int     i = 0;
		if (grp->gr_mem != NULL) {
			while (grp->gr_mem[i] != NULL) {
				if (strcmp(grp->gr_mem[i], pwd->pw_name)==0) {
					printf(j++ == 0 ? "    Groups: %s" : ",%s", grp->gr_name);
					break;
				}
				++i;
			}
		}
	}
	ENDGRENT();
	printf("%s", j ? "\n" : "");
	return (EXIT_SUCCESS);
}

char *
pw_checkname(char *name, int gecos)
{
	char showch[8];
	const char *badchars, *ch, *showtype;
	int reject;

	ch = name;
	reject = 0;
	if (gecos) {
		/* See if the name is valid as a gecos (comment) field. */
		badchars = ":";
		showtype = "gecos field";
	} else {
		/* See if the name is valid as a userid or group. */
		badchars = " ,\t:+&#%$^()!@~*?<>=|\\/\"";
		showtype = "userid/group name";
		/* Userids and groups can not have a leading '-'. */
		if (*ch == '-')
			reject = 1;
	}
	if (!reject) {
		while (*ch) {
			if (strchr(badchars, *ch) != NULL ||
			    (!gecos && *ch < ' ') ||
			    *ch == 127) {
				reject = 1;
				break;
			}
			/* 8-bit characters are only allowed in GECOS fields */
			if (!gecos && (*ch & 0x80)) {
				reject = 1;
				break;
			}
			ch++;
		}
	}
	/*
	 * A `$' is allowed as the final character for userids and groups,
	 * mainly for the benefit of samba.
	 */
	if (reject && !gecos) {
		if (*ch == '$' && *(ch + 1) == '\0') {
			reject = 0;
			ch++;
		}
	}
	if (reject) {
		snprintf(showch, sizeof(showch), (*ch >= ' ' && *ch < 127)
		    ? "`%c'" : "0x%02x", *ch);
		errx(EX_DATAERR, "invalid character %s at position %td in %s",
		    showch, (ch - name), showtype);
	}
	if (!gecos && (ch - name) > LOGNAMESIZE)
		errx(EX_USAGE, "name too long `%s' (max is %d)", name,
		    LOGNAMESIZE);

	return (name);
}

static void
rmat(uid_t uid)
{
	DIR            *d = opendir("/var/at/jobs");

	if (d != NULL) {
		struct dirent  *e;

		while ((e = readdir(d)) != NULL) {
			struct stat     st;

			if (strncmp(e->d_name, ".lock", 5) != 0 &&
			    stat(e->d_name, &st) == 0 &&
			    !S_ISDIR(st.st_mode) &&
			    st.st_uid == uid) {
				char            tmp[MAXPATHLEN];

				snprintf(tmp, sizeof(tmp), "/usr/bin/atrm %s",
				    e->d_name);
				system(tmp);
			}
		}
		closedir(d);
	}
}

static void
rmopie(char const * name)
{
	char tmp[1014];
	FILE *fp;
	int fd;
	size_t len;
	off_t	atofs = 0;
	
	if ((fd = openat(conf.rootfd, "etc/opiekeys", O_RDWR)) == -1)
		return;

	fp = fdopen(fd, "r+");
	len = strlen(name);

	while (fgets(tmp, sizeof(tmp), fp) != NULL) {
		if (strncmp(name, tmp, len) == 0 && tmp[len]==' ') {
			/* Comment username out */
			if (fseek(fp, atofs, SEEK_SET) == 0)
				fwrite("#", 1, 1, fp);
			break;
		}
		atofs = ftell(fp);
	}
	/*
	 * If we got an error of any sort, don't update!
	 */
	fclose(fp);
}

int
pw_user_next(int argc, char **argv, char *name __unused)
{
	struct userconf *cnf = NULL;
	const char *cfg = NULL;
	int ch;
	bool quiet = false;
	uid_t next;

	while ((ch = getopt(argc, argv, "C:q")) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		}
	}

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);

	cnf = get_userconfig(cfg);

	next = pw_uidpolicy(cnf, -1);

	printf("%ju:", (uintmax_t)next);
	pw_groupnext(cnf, quiet);

	return (EXIT_SUCCESS);
}

int
pw_user_show(int argc, char **argv, char *arg1)
{
	struct passwd *pwd = NULL;
	char *name = NULL;
	intmax_t id = -1;
	int ch;
	bool all = false;
	bool pretty = false;
	bool force = false;
	bool v7 = false;
	bool quiet = false;

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, UID_MAX);
		else
			name = arg1;
	}

	while ((ch = getopt(argc, argv, "C:qn:u:FPa7")) != -1) {
		switch (ch) {
		case 'C':
			/* ignore compatibility */
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 'u':
			id = pw_checkid(optarg, UID_MAX);
			break;
		case 'F':
			force = true;
			break;
		case 'P':
			pretty = true;
			break;
		case 'a':
			all = true;
			break;
		case '7':
			v7 = true;
			break;
		}
	}

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);

	if (all) {
		SETPWENT();
		while ((pwd = GETPWENT()) != NULL)
			print_user(pwd, pretty, v7);
		ENDPWENT();
		return (EXIT_SUCCESS);
	}

	if (id < 0 && name == NULL)
		errx(EX_DATAERR, "username or id required");

	pwd = (name != NULL) ? GETPWNAM(pw_checkname(name, 0)) : GETPWUID(id);
	if (pwd == NULL) {
		if (force) {
			pwd = &fakeuser;
		} else {
			if (name == NULL)
				errx(EX_NOUSER, "no such uid `%ju'",
				    (uintmax_t) id);
			errx(EX_NOUSER, "no such user `%s'", name);
		}
	}

	return (print_user(pwd, pretty, v7));
}

int
pw_user_del(int argc, char **argv, char *arg1)
{
	struct userconf *cnf = NULL;
	struct passwd *pwd = NULL;
	struct group *gr, *grp;
	char *name = NULL;
	char grname[MAXLOGNAME];
	char *nispasswd = NULL;
	char file[MAXPATHLEN];
	char home[MAXPATHLEN];
	const char *cfg = NULL;
	struct stat st;
	intmax_t id = -1;
	int ch, rc;
	bool nis = false;
	bool deletehome = false;
	bool quiet = false;

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, UID_MAX);
		else
			name = arg1;
	}

	while ((ch = getopt(argc, argv, "C:qn:u:rYy:")) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 'u':
			id = pw_checkid(optarg, UID_MAX);
			break;
		case 'r':
			deletehome = true;
			break;
		case 'y':
			nispasswd = optarg;
			break;
		case 'Y':
			nis = true;
			break;
		}
	}

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);

	if (id < 0 && name == NULL)
		errx(EX_DATAERR, "username or id required");

	cnf = get_userconfig(cfg);

	if (nispasswd == NULL)
		nispasswd = cnf->nispasswd;

	pwd = (name != NULL) ? GETPWNAM(pw_checkname(name, 0)) : GETPWUID(id);
	if (pwd == NULL) {
		if (name == NULL)
			errx(EX_NOUSER, "no such uid `%ju'", (uintmax_t) id);
		errx(EX_NOUSER, "no such user `%s'", name);
	}

	if (PWF._altdir == PWF_REGULAR &&
	    ((pwd->pw_fields & _PWF_SOURCE) != _PWF_FILES)) {
		if ((pwd->pw_fields & _PWF_SOURCE) == _PWF_NIS) {
			if (!nis && nispasswd && *nispasswd != '/')
				errx(EX_NOUSER, "Cannot remove NIS user `%s'",
				    name);
		} else {
			errx(EX_NOUSER, "Cannot remove non local user `%s'",
			    name);
		}
	}

	id = pwd->pw_uid;
	if (name == NULL)
		name = pwd->pw_name;

	if (strcmp(pwd->pw_name, "root") == 0)
		errx(EX_DATAERR, "cannot remove user 'root'");

	/* Remove opie record from /etc/opiekeys */
	if (PWALTDIR() != PWF_ALT)
		rmopie(pwd->pw_name);

	if (!PWALTDIR()) {
		/* Remove crontabs */
		snprintf(file, sizeof(file), "/var/cron/tabs/%s", pwd->pw_name);
		if (access(file, F_OK) == 0) {
			snprintf(file, sizeof(file), "crontab -u %s -r",
			    pwd->pw_name);
			system(file);
		}
	}

	/*
	 * Save these for later, since contents of pwd may be
	 * invalidated by deletion
	 */
	snprintf(file, sizeof(file), "%s/%s", _PATH_MAILDIR, pwd->pw_name);
	strlcpy(home, pwd->pw_dir, sizeof(home));
	gr = GETGRGID(pwd->pw_gid);
	if (gr != NULL)
		strlcpy(grname, gr->gr_name, LOGNAMESIZE);
	else
		grname[0] = '\0';

	rc = delpwent(pwd);
	if (rc == -1)
		err(EX_IOERR, "user '%s' does not exist", pwd->pw_name);
	else if (rc != 0)
		err(EX_IOERR, "passwd update");

	if (nis && nispasswd && *nispasswd=='/') {
		rc = delnispwent(nispasswd, name);
		if (rc == -1)
			warnx("WARNING: user '%s' does not exist in NIS passwd",
			    pwd->pw_name);
		else if (rc != 0)
			warn("WARNING: NIS passwd update");
	}

	grp = GETGRNAM(name);
	if (grp != NULL &&
	    (grp->gr_mem == NULL || *grp->gr_mem == NULL) &&
	    strcmp(name, grname) == 0)
		delgrent(GETGRNAM(name));
	SETGRENT();
	while ((grp = GETGRENT()) != NULL) {
		int i, j;
		char group[MAXLOGNAME];
		if (grp->gr_mem == NULL)
			continue;

		for (i = 0; grp->gr_mem[i] != NULL; i++) {
			if (strcmp(grp->gr_mem[i], name) != 0)
				continue;

			for (j = i; grp->gr_mem[j] != NULL; j++)
				grp->gr_mem[j] = grp->gr_mem[j+1];
			strlcpy(group, grp->gr_name, MAXLOGNAME);
			chggrent(group, grp);
		}
	}
	ENDGRENT();

	pw_log(cnf, M_DELETE, W_USER, "%s(%ju) account removed", name,
	    (uintmax_t)id);

	/* Remove mail file */
	if (PWALTDIR() != PWF_ALT)
		unlinkat(conf.rootfd, file + 1, 0);

	/* Remove at jobs */
	if (!PWALTDIR() && getpwuid(id) == NULL)
		rmat(id);

	/* Remove home directory and contents */
	if (PWALTDIR() != PWF_ALT && deletehome && *home == '/' &&
	    GETPWUID(id) == NULL &&
	    fstatat(conf.rootfd, home + 1, &st, 0) != -1) {
		rm_r(conf.rootfd, home, id);
		pw_log(cnf, M_DELETE, W_USER, "%s(%ju) home '%s' %s"
		    "removed", name, (uintmax_t)id, home,
		     fstatat(conf.rootfd, home + 1, &st, 0) == -1 ? "" : "not "
		     "completely ");
	}

	return (EXIT_SUCCESS);
}

int
pw_user_lock(int argc, char **argv, char *arg1)
{
	int ch;

	while ((ch = getopt(argc, argv, "Cq")) != -1) {
		switch (ch) {
		case 'C':
		case 'q':
			/* compatibility */
			break;
		}
	}

	return (pw_userlock(arg1, M_LOCK));
}

int
pw_user_unlock(int argc, char **argv, char *arg1)
{
	int ch;

	while ((ch = getopt(argc, argv, "Cq")) != -1) {
		switch (ch) {
		case 'C':
		case 'q':
			/* compatibility */
			break;
		}
	}

	return (pw_userlock(arg1, M_UNLOCK));
}

static struct group *
group_from_name_or_id(char *name)
{
	const char *errstr = NULL;
	struct group *grp;
	uintmax_t id;

	if ((grp = GETGRNAM(name)) == NULL) {
		id = strtounum(name, 0, GID_MAX, &errstr);
		if (errstr)
			errx(EX_NOUSER, "group `%s' does not exist", name);
		grp = GETGRGID(id);
		if (grp == NULL)
			errx(EX_NOUSER, "group `%s' does not exist", name);
	}

	return (grp);
}

static void
split_groups(StringList **groups, char *groupsstr)
{
	struct group *grp;
	char *p;
	char tok[] = ", \t";

	if (*groups == NULL)
		*groups = sl_init();
	for (p = strtok(groupsstr, tok); p != NULL; p = strtok(NULL, tok)) {
		grp = group_from_name_or_id(p);
		sl_add(*groups, newstr(grp->gr_name));
	}
}

static void
validate_grname(struct userconf *cnf, char *group)
{
	struct group *grp;

	if (group == NULL || *group == '\0') {
		cnf->default_group = "";
		return;
	}
	grp = group_from_name_or_id(group);
	cnf->default_group = newstr(grp->gr_name);
}

static mode_t
validate_mode(char *mode)
{
	mode_t m;
	void *set;

	if ((set = setmode(mode)) == NULL)
		errx(EX_DATAERR, "invalid directory creation mode '%s'", mode);

	m = getmode(set, _DEF_DIRMODE);
	free(set);
	return (m);
}

static long
validate_expire(char *str, int opt)
{
	if (!numerics(str))
		errx(EX_DATAERR, "-%c argument must be numeric "
		     "when setting defaults: %s", (char)opt, str);
	return strtol(str, NULL, 0);
}

static void
mix_config(struct userconf *cmdcnf, struct userconf *cfg)
{

	if (cmdcnf->default_password < 0)
		cmdcnf->default_password = cfg->default_password;
	if (cmdcnf->reuse_uids == 0)
		cmdcnf->reuse_uids = cfg->reuse_uids;
	if (cmdcnf->reuse_gids == 0)
		cmdcnf->reuse_gids = cfg->reuse_gids;
	if (cmdcnf->nispasswd == NULL)
		cmdcnf->nispasswd = cfg->nispasswd;
	if (cmdcnf->dotdir == NULL)
		cmdcnf->dotdir = cfg->dotdir;
	if (cmdcnf->newmail == NULL)
		cmdcnf->newmail = cfg->newmail;
	if (cmdcnf->logfile == NULL)
		cmdcnf->logfile = cfg->logfile;
	if (cmdcnf->home == NULL)
		cmdcnf->home = cfg->home;
	if (cmdcnf->homemode == 0)
		cmdcnf->homemode = cfg->homemode;
	if (cmdcnf->shelldir == NULL)
		cmdcnf->shelldir = cfg->shelldir;
	if (cmdcnf->shells == NULL)
		cmdcnf->shells = cfg->shells;
	if (cmdcnf->shell_default == NULL)
		cmdcnf->shell_default = cfg->shell_default;
	if (cmdcnf->default_group == NULL)
		cmdcnf->default_group = cfg->default_group;
	if (cmdcnf->groups == NULL)
		cmdcnf->groups = cfg->groups;
	if (cmdcnf->default_class == NULL)
		cmdcnf->default_class = cfg->default_class;
	if (cmdcnf->min_uid == 0)
		cmdcnf->min_uid = cfg->min_uid;
	if (cmdcnf->max_uid == 0)
		cmdcnf->max_uid = cfg->max_uid;
	if (cmdcnf->min_gid == 0)
		cmdcnf->min_gid = cfg->min_gid;
	if (cmdcnf->max_gid == 0)
		cmdcnf->max_gid = cfg->max_gid;
	if (cmdcnf->expire_days < 0)
		cmdcnf->expire_days = cfg->expire_days;
	if (cmdcnf->password_days < 0)
		cmdcnf->password_days = cfg->password_days;
}

int
pw_user_add(int argc, char **argv, char *arg1)
{
	struct userconf *cnf, *cmdcnf;
	struct passwd *pwd;
	struct group *grp;
	struct stat st;
	char args[] = "C:qn:u:c:d:e:p:g:G:mM:k:s:oL:i:w:h:H:Db:NPy:Y";
	char line[_PASSWORD_LEN+1], path[MAXPATHLEN];
	char *gecos, *homedir, *skel, *walk, *userid, *groupid, *grname;
	char *default_passwd, *name, *p;
	const char *cfg = NULL;
	login_cap_t *lc;
	FILE *pfp, *fp;
	intmax_t id = -1;
	time_t now;
	int rc, ch, fd = -1;
	size_t i;
	bool dryrun, nis, pretty, quiet, createhome, precrypted, genconf;

	dryrun = nis = pretty = quiet = createhome = precrypted = false;
	genconf = false;
	gecos = homedir = skel = userid = groupid = default_passwd = NULL;
	grname = name = NULL;

	if ((cmdcnf = calloc(1, sizeof(struct userconf))) == NULL)
		err(EXIT_FAILURE, "calloc()");

	cmdcnf->default_password = cmdcnf->expire_days = cmdcnf->password_days = -1; 
	now = time(NULL);

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, UID_MAX);
		else
			name = pw_checkname(arg1, 0);
	}

	while ((ch = getopt(argc, argv, args)) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = pw_checkname(optarg, 0);
			break;
		case 'u':
			userid = optarg;
			break;
		case 'c':
			gecos = pw_checkname(optarg, 1);
			break;
		case 'd':
			homedir = optarg;
			break;
		case 'e':
			if (genconf)
			    cmdcnf->expire_days = validate_expire(optarg, ch);
			else
			    cmdcnf->expire_days = parse_date(now, optarg);
			break;
		case 'p':
			if (genconf)
			    cmdcnf->password_days = validate_expire(optarg, ch);
			else
			    cmdcnf->password_days = parse_date(now, optarg);
			break;
		case 'g':
			validate_grname(cmdcnf, optarg);
			grname = optarg;
			break;
		case 'G':
			split_groups(&cmdcnf->groups, optarg);
			break;
		case 'm':
			createhome = true;
			break;
		case 'M':
			cmdcnf->homemode = validate_mode(optarg);
			break;
		case 'k':
			walk = skel = optarg;
			if (*walk == '/')
				walk++;
			if (fstatat(conf.rootfd, walk, &st, 0) == -1)
				errx(EX_OSFILE, "skeleton `%s' does not "
				    "exists", skel);
			if (!S_ISDIR(st.st_mode))
				errx(EX_OSFILE, "skeleton `%s' is not a "
				    "directory", skel);
			cmdcnf->dotdir = skel;
			break;
		case 's':
			cmdcnf->shell_default = optarg;
			break;
		case 'o':
			conf.checkduplicate = false;
			break;
		case 'L':
			cmdcnf->default_class = pw_checkname(optarg, 0);
			break;
		case 'i':
			groupid = optarg;
			break;
		case 'w':
			default_passwd = optarg;
			break;
		case 'H':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			precrypted = true;
			if (fd == '-')
				errx(EX_USAGE, "-H expects a file descriptor");
			break;
		case 'h':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			break;
		case 'D':
			genconf = true;
			break;
		case 'b':
			cmdcnf->home = optarg;
			break;
		case 'N':
			dryrun = true;
			break;
		case 'P':
			pretty = true;
			break;
		case 'y':
			cmdcnf->nispasswd = optarg;
			break;
		case 'Y':
			nis = true;
			break;
		}
	}

	if (geteuid() != 0 && ! dryrun)
		errx(EX_NOPERM, "you must be root");

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);

	cnf = get_userconfig(cfg);

	mix_config(cmdcnf, cnf);
	if (default_passwd)
		cmdcnf->default_password = passwd_val(default_passwd,
		    cnf->default_password);
	if (genconf) {
		if (name != NULL)
			errx(EX_DATAERR, "can't combine `-D' with `-n name'");
		if (userid != NULL) {
			if ((p = strtok(userid, ", \t")) != NULL)
				cmdcnf->min_uid = pw_checkid(p, UID_MAX);
			if (cmdcnf->min_uid == 0)
				cmdcnf->min_uid = 1000;
			if ((p = strtok(NULL, " ,\t")) != NULL)
				cmdcnf->max_uid = pw_checkid(p, UID_MAX);
			if (cmdcnf->max_uid == 0)
				cmdcnf->max_uid = 32000;
		}
		if (groupid != NULL) {
			if ((p = strtok(groupid, ", \t")) != NULL)
				cmdcnf->min_gid = pw_checkid(p, GID_MAX);
			if (cmdcnf->min_gid == 0)
				cmdcnf->min_gid = 1000;
			if ((p = strtok(NULL, " ,\t")) != NULL)
				cmdcnf->max_gid = pw_checkid(p, GID_MAX);
			if (cmdcnf->max_gid == 0)
				cmdcnf->max_gid = 32000;
		}
		if (write_userconfig(cmdcnf, cfg))
			return (EXIT_SUCCESS);
		err(EX_IOERR, "config update");
	}

	if (userid)
		id = pw_checkid(userid, UID_MAX);
	if (id < 0 && name == NULL)
		errx(EX_DATAERR, "user name or id required");

	if (name == NULL)
		errx(EX_DATAERR, "login name required");

	if (GETPWNAM(name) != NULL)
		errx(EX_DATAERR, "login name `%s' already exists", name);

	if (!grname)
		grname = cmdcnf->default_group;

	pwd = &fakeuser;
	pwd->pw_name = name;
	pwd->pw_class = cmdcnf->default_class ? cmdcnf->default_class : "";
	pwd->pw_uid = pw_uidpolicy(cmdcnf, id);
	pwd->pw_gid = pw_gidpolicy(cnf, grname, pwd->pw_name,
	    (gid_t) pwd->pw_uid, dryrun);

	/* cmdcnf->password_days and cmdcnf->expire_days hold unixtime here */
	if (cmdcnf->password_days > 0)
		pwd->pw_change = cmdcnf->password_days;
	if (cmdcnf->expire_days > 0)
		pwd->pw_expire = cmdcnf->expire_days;

	pwd->pw_dir = pw_homepolicy(cmdcnf, homedir, pwd->pw_name);
	pwd->pw_shell = pw_shellpolicy(cmdcnf);
	lc = login_getpwclass(pwd);
	if (lc == NULL || login_setcryptfmt(lc, "sha512", NULL) == NULL)
		warn("setting crypt(3) format");
	login_close(lc);
	pwd->pw_passwd = pw_password(cmdcnf, pwd->pw_name, dryrun);
	if (pwd->pw_uid == 0 && strcmp(pwd->pw_name, "root") != 0)
		warnx("WARNING: new account `%s' has a uid of 0 "
		    "(superuser access!)", pwd->pw_name);
	if (gecos)
		pwd->pw_gecos = gecos;

	if (fd != -1)
		pw_set_passwd(pwd, fd, precrypted, false);

	if (dryrun)
		return (print_user(pwd, pretty, false));

	if ((rc = addpwent(pwd)) != 0) {
		if (rc == -1)
			errx(EX_IOERR, "user '%s' already exists",
			    pwd->pw_name);
		else if (rc != 0)
			err(EX_IOERR, "passwd file update");
	}
	if (nis && cmdcnf->nispasswd && *cmdcnf->nispasswd == '/') {
		printf("%s\n", cmdcnf->nispasswd);
		rc = addnispwent(cmdcnf->nispasswd, pwd);
		if (rc == -1)
			warnx("User '%s' already exists in NIS passwd",
			    pwd->pw_name);
		else if (rc != 0)
			warn("NIS passwd update");
		/* NOTE: we treat NIS-only update errors as non-fatal */
	}

	if (cmdcnf->groups != NULL) {
		for (i = 0; i < cmdcnf->groups->sl_cur; i++) {
			grp = GETGRNAM(cmdcnf->groups->sl_str[i]);
			grp = gr_add(grp, pwd->pw_name);
			/*
			 * grp can only be NULL in 2 cases:
			 * - the new member is already a member
			 * - a problem with memory occurs
			 * in both cases we want to skip now.
			 */
			if (grp == NULL)
				continue;
			chggrent(grp->gr_name, grp);
			free(grp);
		}
	}

	pwd = GETPWNAM(name);
	if (pwd == NULL)
		errx(EX_NOUSER, "user '%s' disappeared during update", name);

	grp = GETGRGID(pwd->pw_gid);
	pw_log(cnf, M_ADD, W_USER, "%s(%ju):%s(%ju):%s:%s:%s",
	       pwd->pw_name, (uintmax_t)pwd->pw_uid,
	    grp ? grp->gr_name : "unknown",
	       (uintmax_t)(grp ? grp->gr_gid : (uid_t)-1),
	       pwd->pw_gecos, pwd->pw_dir, pwd->pw_shell);

	/*
	 * let's touch and chown the user's mail file. This is not
	 * strictly necessary under BSD with a 0755 maildir but it also
	 * doesn't hurt anything to create the empty mailfile
	 */
	if (PWALTDIR() != PWF_ALT) {
		snprintf(path, sizeof(path), "%s/%s", _PATH_MAILDIR,
		    pwd->pw_name);
		/* Preserve contents & mtime */
		close(openat(conf.rootfd, path +1, O_RDWR | O_CREAT, 0600));
		fchownat(conf.rootfd, path + 1, pwd->pw_uid, pwd->pw_gid,
		    AT_SYMLINK_NOFOLLOW);
	}

	/*
	 * Let's create and populate the user's home directory. Note
	 * that this also `works' for editing users if -m is used, but
	 * existing files will *not* be overwritten.
	 */
	if (PWALTDIR() != PWF_ALT && createhome && pwd->pw_dir &&
	    *pwd->pw_dir == '/' && pwd->pw_dir[1])
		create_and_populate_homedir(cmdcnf, pwd, cmdcnf->dotdir,
		    cmdcnf->homemode, false);

	if (!PWALTDIR() && cmdcnf->newmail && *cmdcnf->newmail &&
	    (fp = fopen(cnf->newmail, "r")) != NULL) {
		if ((pfp = popen(_PATH_SENDMAIL " -t", "w")) == NULL)
			warn("sendmail");
		else {
			fprintf(pfp, "From: root\n" "To: %s\n"
			    "Subject: Welcome!\n\n", pwd->pw_name);
			while (fgets(line, sizeof(line), fp) != NULL) {
				/* Do substitutions? */
				fputs(line, pfp);
			}
			pclose(pfp);
			pw_log(cnf, M_ADD, W_USER, "%s(%ju) new user mail sent",
			    pwd->pw_name, (uintmax_t)pwd->pw_uid);
		}
		fclose(fp);
	}

	if (nis && nis_update() == 0)
		pw_log(cnf, M_ADD, W_USER, "NIS maps updated");

	return (EXIT_SUCCESS);
}

int
pw_user_mod(int argc, char **argv, char *arg1)
{
	struct userconf *cnf;
	struct passwd *pwd;
	struct group *grp;
	StringList *groups = NULL;
	char args[] = "C:qn:u:c:d:e:p:g:G:mM:l:k:s:w:L:h:H:NPYy:";
	const char *cfg = NULL;
	char *gecos, *homedir, *grname, *name, *newname, *walk, *skel, *shell;
	char *passwd, *class, *nispasswd;
	login_cap_t *lc;
	struct stat st;
	intmax_t id = -1;
	int ch, fd = -1;
	size_t i, j;
	bool quiet, createhome, pretty, dryrun, nis, edited;
	bool precrypted;
	mode_t homemode = 0;
	time_t expire_time, password_time, now;

	expire_time = password_time = -1;
	gecos = homedir = grname = name = newname = skel = shell =NULL;
	passwd = NULL;
	class = nispasswd = NULL;
	quiet = createhome = pretty = dryrun = nis = precrypted = false;
	edited = false;
	now = time(NULL);

	if (arg1 != NULL) {
		if (arg1[strspn(arg1, "0123456789")] == '\0')
			id = pw_checkid(arg1, UID_MAX);
		else
			name = arg1;
	}

	while ((ch = getopt(argc, argv, args)) != -1) {
		switch (ch) {
		case 'C':
			cfg = optarg;
			break;
		case 'q':
			quiet = true;
			break;
		case 'n':
			name = optarg;
			break;
		case 'u':
			id = pw_checkid(optarg, UID_MAX);
			break;
		case 'c':
			gecos = pw_checkname(optarg, 1);
			break;
		case 'd':
			homedir = optarg;
			break;
		case 'e':
			expire_time = parse_date(now, optarg);
			break;
		case 'p':
			password_time = parse_date(now, optarg);
			break;
		case 'g':
			group_from_name_or_id(optarg);
			grname = optarg;
			break;
		case 'G':
			split_groups(&groups, optarg);
			break;
		case 'm':
			createhome = true;
			break;
		case 'M':
			homemode = validate_mode(optarg);
			break;
		case 'l':
			newname = optarg;
			break;
		case 'k':
			walk = skel = optarg;
			if (*walk == '/')
				walk++;
			if (fstatat(conf.rootfd, walk, &st, 0) == -1)
				errx(EX_OSFILE, "skeleton `%s' does not "
				    "exists", skel);
			if (!S_ISDIR(st.st_mode))
				errx(EX_OSFILE, "skeleton `%s' is not a "
				    "directory", skel);
			break;
		case 's':
			shell = optarg;
			break;
		case 'w':
			passwd = optarg;
			break;
		case 'L':
			class = pw_checkname(optarg, 0);
			break;
		case 'H':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			precrypted = true;
			if (fd == '-')
				errx(EX_USAGE, "-H expects a file descriptor");
			break;
		case 'h':
			if (fd != -1)
				errx(EX_USAGE, "'-h' and '-H' are mutually "
				    "exclusive options");
			fd = pw_checkfd(optarg);
			break;
		case 'N':
			dryrun = true;
			break;
		case 'P':
			pretty = true;
			break;
		case 'y':
			nispasswd = optarg;
			break;
		case 'Y':
			nis = true;
			break;
		}
	}

	if (geteuid() != 0 && ! dryrun)
		errx(EX_NOPERM, "you must be root");

	if (quiet)
		freopen(_PATH_DEVNULL, "w", stderr);

	cnf = get_userconfig(cfg);

	if (id < 0 && name == NULL)
		errx(EX_DATAERR, "username or id required");

	pwd = (name != NULL) ? GETPWNAM(pw_checkname(name, 0)) : GETPWUID(id);
	if (pwd == NULL) {
		if (name == NULL)
			errx(EX_NOUSER, "no such uid `%ju'",
			    (uintmax_t) id);
		errx(EX_NOUSER, "no such user `%s'", name);
	}

	if (name == NULL)
		name = pwd->pw_name;

	if (nis && nispasswd == NULL)
		nispasswd = cnf->nispasswd;

	if (PWF._altdir == PWF_REGULAR &&
	    ((pwd->pw_fields & _PWF_SOURCE) != _PWF_FILES)) {
		if ((pwd->pw_fields & _PWF_SOURCE) == _PWF_NIS) {
			if (!nis && nispasswd && *nispasswd != '/')
				errx(EX_NOUSER, "Cannot modify NIS user `%s'",
				    name);
		} else {
			errx(EX_NOUSER, "Cannot modify non local user `%s'",
			    name);
		}
	}

	if (newname) {
		if (strcmp(pwd->pw_name, "root") == 0)
			errx(EX_DATAERR, "can't rename `root' account");
		if (strcmp(pwd->pw_name, newname) != 0) {
			pwd->pw_name = pw_checkname(newname, 0);
			edited = true;
		}
	}

	if (id >= 0 && pwd->pw_uid != id) {
		pwd->pw_uid = id;
		edited = true;
		if (pwd->pw_uid != 0 && strcmp(pwd->pw_name, "root") == 0)
			errx(EX_DATAERR, "can't change uid of `root' account");
		if (pwd->pw_uid == 0 && strcmp(pwd->pw_name, "root") != 0)
			warnx("WARNING: account `%s' will have a uid of 0 "
			    "(superuser access!)", pwd->pw_name);
	}

	if (grname && pwd->pw_uid != 0) {
		grp = GETGRNAM(grname);
		if (grp == NULL)
			grp = GETGRGID(pw_checkid(grname, GID_MAX));
		if (grp->gr_gid != pwd->pw_gid) {
			pwd->pw_gid = grp->gr_gid;
			edited = true;
		}
	}


	if (password_time >= 0 && pwd->pw_change != password_time) {
		pwd->pw_change = password_time;
		edited = true;
	}

	if (expire_time >= 0 && pwd->pw_expire != expire_time) {
		pwd->pw_expire = expire_time;
		edited = true;
	}

	if (shell) {
		shell = shell_path(cnf->shelldir, cnf->shells, shell);
		if (shell == NULL)
			shell = "";
		if (strcmp(shell, pwd->pw_shell) != 0) {
			pwd->pw_shell = shell;
			edited = true;
		}
	}

	if (class && strcmp(pwd->pw_class, class) != 0) {
		pwd->pw_class = class;
		edited = true;
	}

	if (homedir && strcmp(pwd->pw_dir, homedir) != 0) {
		pwd->pw_dir = homedir;
		edited = true;
		if (fstatat(conf.rootfd, pwd->pw_dir, &st, 0) == -1) {
			if (!createhome)
				warnx("WARNING: home `%s' does not exist",
				    pwd->pw_dir);
		} else if (!S_ISDIR(st.st_mode)) {
			warnx("WARNING: home `%s' is not a directory",
			    pwd->pw_dir);
		}
	}

	if (passwd && conf.fd == -1) {
		lc = login_getpwclass(pwd);
		if (lc == NULL || login_setcryptfmt(lc, "sha512", NULL) == NULL)
			warn("setting crypt(3) format");
		login_close(lc);
		cnf->default_password = passwd_val(passwd,
		    cnf->default_password);
		pwd->pw_passwd = pw_password(cnf, pwd->pw_name, dryrun);
		edited = true;
	}

	if (gecos && strcmp(pwd->pw_gecos, gecos) != 0) {
		pwd->pw_gecos = gecos;
		edited = true;
	}

	if (fd != -1)
		edited = pw_set_passwd(pwd, fd, precrypted, true);

	if (dryrun)
		return (print_user(pwd, pretty, false));

	if (edited) /* Only updated this if required */
		perform_chgpwent(name, pwd, nis ? nispasswd : NULL);
	/* Now perform the needed changes concern groups */
	if (groups != NULL) {
		/* Delete User from groups using old name */
		SETGRENT();
		while ((grp = GETGRENT()) != NULL) {
			if (grp->gr_mem == NULL)
				continue;
			for (i = 0; grp->gr_mem[i] != NULL; i++) {
				if (strcmp(grp->gr_mem[i] , name) != 0)
					continue;
				for (j = i; grp->gr_mem[j] != NULL ; j++)
					grp->gr_mem[j] = grp->gr_mem[j+1];
				chggrent(grp->gr_name, grp);
				break;
			}
		}
		ENDGRENT();
		/* Add the user to the needed groups */
		for (i = 0; i < groups->sl_cur; i++) {
			grp = GETGRNAM(groups->sl_str[i]);
			grp = gr_add(grp, pwd->pw_name);
			if (grp == NULL)
				continue;
			chggrent(grp->gr_name, grp);
			free(grp);
		}
	}
	/* In case of rename we need to walk over the different groups */
	if (newname) {
		SETGRENT();
		while ((grp = GETGRENT()) != NULL) {
			if (grp->gr_mem == NULL)
				continue;
			for (i = 0; grp->gr_mem[i] != NULL; i++) {
				if (strcmp(grp->gr_mem[i], name) != 0)
					continue;
				grp->gr_mem[i] = newname;
				chggrent(grp->gr_name, grp);
				break;
			}
		}
	}

	/* go get a current version of pwd */
	if (newname)
		name = newname;
	pwd = GETPWNAM(name);
	if (pwd == NULL)
		errx(EX_NOUSER, "user '%s' disappeared during update", name);
	grp = GETGRGID(pwd->pw_gid);
	pw_log(cnf, M_UPDATE, W_USER, "%s(%ju):%s(%ju):%s:%s:%s",
	    pwd->pw_name, (uintmax_t)pwd->pw_uid,
	    grp ? grp->gr_name : "unknown",
	    (uintmax_t)(grp ? grp->gr_gid : (uid_t)-1),
	    pwd->pw_gecos, pwd->pw_dir, pwd->pw_shell);

	/*
	 * Let's create and populate the user's home directory. Note
	 * that this also `works' for editing users if -m is used, but
	 * existing files will *not* be overwritten.
	 */
	if (PWALTDIR() != PWF_ALT && createhome && pwd->pw_dir &&
	    *pwd->pw_dir == '/' && pwd->pw_dir[1]) {
		if (!skel)
			skel = cnf->dotdir;
		if (homemode == 0)
			homemode = cnf->homemode;
		create_and_populate_homedir(cnf, pwd, skel, homemode, true);
	}

	if (nis && nis_update() == 0)
		pw_log(cnf, M_UPDATE, W_USER, "NIS maps updated");

	return (EXIT_SUCCESS);
}
