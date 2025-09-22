/*	$OpenBSD: crontab.c,v 1.96 2023/05/05 13:50:40 millert Exp $	*/

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
#include <sys/wait.h>

#include <bitstring.h>		/* for structs.h */
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "pathnames.h"
#include "macros.h"
#include "structs.h"
#include "funcs.h"
#include "globals.h"

#define NHEADER_LINES 3

enum opt_t	{ opt_unknown, opt_list, opt_delete, opt_edit, opt_replace };

static	gid_t		crontab_gid;
static	gid_t		user_gid;
static	char		User[MAX_UNAME], RealUser[MAX_UNAME];
static	char		Filename[PATH_MAX], TempFilename[PATH_MAX];
static	FILE		*NewCrontab;
static	int		CheckErrorCount;
static	enum opt_t	Option;
static	struct passwd	*pw;
int			editit(const char *);
static	void		list_cmd(void),
			delete_cmd(void),
			edit_cmd(void),
			check_error(const char *),
			parse_args(int c, char *v[]),
			copy_crontab(FILE *, FILE *),
			die(int);
static	int		replace_cmd(void);

static void
usage(const char *msg)
{
	if (msg != NULL)
		warnx("usage error: %s", msg);
	fprintf(stderr, "usage: %s [-u user] file\n", __progname);
	fprintf(stderr, "       %s [-e | -l | -r] [-u user]\n", __progname);

	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	int exitstatus;

	if (pledge("stdio rpath wpath cpath fattr getpw unix id proc exec",
	    NULL) == -1) {
		err(EXIT_FAILURE, "pledge");
	}

	user_gid = getgid();
	crontab_gid = getegid();

	openlog(__progname, LOG_PID, LOG_CRON);

	setvbuf(stderr, NULL, _IOLBF, 0);
	parse_args(argc, argv);		/* sets many globals, opens a file */
	if (!allowed(RealUser, _PATH_CRON_ALLOW, _PATH_CRON_DENY)) {
		fprintf(stderr, "You do not have permission to use crontab\n");
		fprintf(stderr, "See crontab(1) for more information\n");
		syslog(LOG_WARNING, "(%s) AUTH (crontab command not allowed)",
		    RealUser);
		exit(EXIT_FAILURE);
	}
	exitstatus = EXIT_SUCCESS;
	switch (Option) {
	case opt_list:
		list_cmd();
		break;
	case opt_delete:
		delete_cmd();
		break;
	case opt_edit:
		edit_cmd();
		break;
	case opt_replace:
		if (replace_cmd() < 0)
			exitstatus = EXIT_FAILURE;
		break;
	default:
		exitstatus = EXIT_FAILURE;
		break;
	}
	exit(exitstatus);
	/*NOTREACHED*/
}

static void
parse_args(int argc, char *argv[])
{
	int argch;

	if (!(pw = getpwuid(getuid())))
		errx(EXIT_FAILURE, "your UID isn't in the password database");
	if (strlen(pw->pw_name) >= sizeof User)
		errx(EXIT_FAILURE, "username too long");
	strlcpy(User, pw->pw_name, sizeof(User));
	strlcpy(RealUser, User, sizeof(RealUser));
	Filename[0] = '\0';
	Option = opt_unknown;
	while ((argch = getopt(argc, argv, "u:ler")) != -1) {
		switch (argch) {
		case 'u':
			if (getuid() != 0)
				errx(EXIT_FAILURE,
				    "only the super user may use -u");
			if (!(pw = getpwnam(optarg)))
				errx(EXIT_FAILURE, "unknown user %s", optarg);
			if (strlcpy(User, optarg, sizeof User) >= sizeof User)
				usage("username too long");
			break;
		case 'l':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_list;
			break;
		case 'r':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_delete;
			break;
		case 'e':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_edit;
			break;
		default:
			usage(NULL);
		}
	}

	endpwent();

	if (Option != opt_unknown) {
		if (argv[optind] != NULL)
			usage("no arguments permitted after this option");
	} else {
		if (argv[optind] != NULL) {
			Option = opt_replace;
			if (strlcpy(Filename, argv[optind], sizeof Filename)
			    >= sizeof Filename)
				usage("filename too long");
		} else
			usage("file name must be specified for replace");
	}

	if (Option == opt_replace) {
		/* XXX - no longer need to open the file early, move this. */
		if (!strcmp(Filename, "-"))
			NewCrontab = stdin;
		else {
			/* relinquish the setgid status of the binary during
			 * the open, lest nonroot users read files they should
			 * not be able to read.  we can't use access() here
			 * since there's a race condition.  thanks go out to
			 * Arnt Gulbrandsen <agulbra@pvv.unit.no> for spotting
			 * the race.
			 */

			if (setegid(user_gid) == -1)
				err(EXIT_FAILURE, "setegid(user_gid)");
			if (!(NewCrontab = fopen(Filename, "r")))
				err(EXIT_FAILURE, "%s", Filename);
			if (setegid(crontab_gid) == -1)
				err(EXIT_FAILURE, "setegid(crontab_gid)");
		}
	}
}

static void
list_cmd(void)
{
	char n[PATH_MAX];
	FILE *f;

	syslog(LOG_INFO, "(%s) LIST (%s)", RealUser, User);
	if (snprintf(n, sizeof n, "%s/%s", _PATH_CRON_SPOOL, User) >= sizeof(n))
		errc(EXIT_FAILURE, ENAMETOOLONG, "%s/%s", _PATH_CRON_SPOOL, User);
	if (!(f = fopen(n, "r"))) {
		if (errno == ENOENT)
			warnx("no crontab for %s", User);
		else
			warn("%s", n);
		exit(EXIT_FAILURE);
	}

	/* file is open. copy to stdout, close.
	 */
	Set_LineNum(1)

	copy_crontab(f, stdout);
	fclose(f);
}

static void
delete_cmd(void)
{
	char n[PATH_MAX];

	syslog(LOG_INFO, "(%s) DELETE (%s)", RealUser, User);
	if (snprintf(n, sizeof n, "%s/%s", _PATH_CRON_SPOOL, User) >= sizeof(n))
		errc(EXIT_FAILURE, ENAMETOOLONG, "%s/%s", _PATH_CRON_SPOOL, User);
	if (unlink(n) != 0) {
		if (errno == ENOENT)
			warnx("no crontab for %s", User);
		else
			warn("%s", n);
		exit(EXIT_FAILURE);
	}
	poke_daemon(RELOAD_CRON);
}

static void
check_error(const char *msg)
{
	CheckErrorCount++;
	fprintf(stderr, "\"%s\":%d: %s\n", Filename, LineNumber-1, msg);
}

static void
edit_cmd(void)
{
	char n[PATH_MAX], q[MAX_TEMPSTR];
	FILE *f;
	int t;
	struct stat statbuf, xstatbuf;
	struct timespec ts[2];

	syslog(LOG_INFO, "(%s) BEGIN EDIT (%s)", RealUser, User);
	if (snprintf(n, sizeof n, "%s/%s", _PATH_CRON_SPOOL, User) >= sizeof(n))
		errc(EXIT_FAILURE, ENAMETOOLONG, "%s/%s", _PATH_CRON_SPOOL, User);
	if (!(f = fopen(n, "r"))) {
		if (errno != ENOENT)
			err(EXIT_FAILURE, "%s", n);
		warnx("creating new crontab for %s", User);
		if (!(f = fopen(_PATH_DEVNULL, "r")))
			err(EXIT_FAILURE, _PATH_DEVNULL);
	}

	if (fstat(fileno(f), &statbuf) == -1) {
		warn("fstat");
		goto fatal;
	}
	ts[0] = statbuf.st_atim;
	ts[1] = statbuf.st_mtim;

	/* Turn off signals. */
	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);

	if (snprintf(Filename, sizeof Filename, "%scrontab.XXXXXXXXXX",
	    _PATH_TMP) >= sizeof(Filename)) {
		warnc(ENAMETOOLONG, "%scrontab.XXXXXXXXXX", _PATH_TMP);
		goto fatal;
	}
	t = mkstemp(Filename);
	if (t == -1) {
		warn("%s", Filename);
		goto fatal;
	}
	if (!(NewCrontab = fdopen(t, "r+"))) {
		warn("fdopen");
		goto fatal;
	}

	Set_LineNum(1)

	copy_crontab(f, NewCrontab);
	fclose(f);
	if (fflush(NewCrontab) == EOF)
		err(EXIT_FAILURE, "%s", Filename);
	if (futimens(t, ts) == -1)
		warn("unable to set times on %s", Filename);
 again:
	rewind(NewCrontab);
	if (ferror(NewCrontab)) {
		warnx("error writing new crontab to %s", Filename);
 fatal:
		unlink(Filename);
		exit(EXIT_FAILURE);
	}

	/* we still have the file open.  editors will generally rewrite the
	 * original file rather than renaming/unlinking it and starting a
	 * new one; even backup files are supposed to be made by copying
	 * rather than by renaming.  if some editor does not support this,
	 * then don't use it.  the security problems are more severe if we
	 * close and reopen the file around the edit.
	 */
	if (editit(Filename) == -1) {
		warn("error starting editor");
		goto fatal;
	}

	if (fstat(t, &statbuf) == -1) {
		warn("fstat");
		goto fatal;
	}
	if (timespeccmp(&ts[1], &statbuf.st_mtim, ==)) {
		if (lstat(Filename, &xstatbuf) == 0 &&
		    statbuf.st_ino != xstatbuf.st_ino) {
			warnx("crontab temp file moved, editor "
			   "may create backup files improperly");
		}
		warnx("no changes made to crontab");
		goto remove;
	}
	warnx("installing new crontab");
	switch (replace_cmd()) {
	case 0:
		break;
	case -1:
		for (;;) {
			printf("Do you want to retry the same edit? ");
			fflush(stdout);
			q[0] = '\0';
			if (fgets(q, sizeof q, stdin) == NULL) {
				putchar('\n');
				goto abandon;
			}
			switch (q[0]) {
			case 'y':
			case 'Y':
				goto again;
			case 'n':
			case 'N':
				goto abandon;
			default:
				fprintf(stderr, "Enter Y or N\n");
			}
		}
		/*NOTREACHED*/
	case -2:
	abandon:
		warnx("edits left in %s", Filename);
		goto done;
	default:
		warnx("panic: bad switch() in replace_cmd()");
		goto fatal;
	}
 remove:
	unlink(Filename);
 done:
	syslog(LOG_INFO, "(%s) END EDIT (%s)", RealUser, User);
}

/* Create a temporary file in the spool dir owned by "pw". */
static FILE *
spool_mkstemp(char *template)
{
	uid_t euid = geteuid();
	int fd = -1;
	FILE *fp;

	if (euid != pw->pw_uid) {
		if (seteuid(pw->pw_uid) == -1) {
			warn("unable to change uid to %u", pw->pw_uid);
			goto bad;
		}
	}
	fd = mkstemp(template);
	if (euid != pw->pw_uid) {
		if (seteuid(euid) == -1) {
			warn("unable to change uid to %u", euid);
			goto bad;
		}
	}
	if (fd == -1 || !(fp = fdopen(fd, "w+"))) {
		warn("%s", template);
		goto bad;
	}
	return (fp);

bad:
	if (fd != -1) {
		close(fd);
		unlink(template);
	}
	return (NULL);
}

/* returns	0	on success
 *		-1	on syntax error
 *		-2	on install error
 */
static int
replace_cmd(void)
{
	char n[PATH_MAX], envstr[MAX_ENVSTR];
	FILE *tmp;
	int ch, eof;
	int error = 0;
	entry *e;
	time_t now = time(NULL);
	char **envp = env_init();

	if (envp == NULL) {
		warn(NULL);		/* ENOMEM */
		return (-2);
	}
	if (snprintf(TempFilename, sizeof TempFilename, "%s/tmp.XXXXXXXXX",
	    _PATH_CRON_SPOOL) >= sizeof(TempFilename)) {
		TempFilename[0] = '\0';
		warnc(ENAMETOOLONG, "%s/tmp.XXXXXXXXX", _PATH_CRON_SPOOL);
		return (-2);
	}
	tmp = spool_mkstemp(TempFilename);
	if (tmp == NULL) {
		TempFilename[0] = '\0';
		return (-2);
	}

	(void) signal(SIGHUP, die);
	(void) signal(SIGINT, die);
	(void) signal(SIGQUIT, die);

	/* write a signature at the top of the file.
	 *
	 * VERY IMPORTANT: make sure NHEADER_LINES agrees with this code.
	 */
	fprintf(tmp, "# DO NOT EDIT THIS FILE - edit the master and reinstall.\n");
	fprintf(tmp, "# (%s installed on %-24.24s)\n", Filename, ctime(&now));
	fprintf(tmp, "# (Cron version %s)\n", CRON_VERSION);

	/* copy the crontab to the tmp
	 */
	rewind(NewCrontab);
	Set_LineNum(1)
	while (EOF != (ch = get_char(NewCrontab)))
		putc(ch, tmp);
	ftruncate(fileno(tmp), ftello(tmp));	/* XXX redundant with "w+"? */
	fflush(tmp);  rewind(tmp);

	if (ferror(tmp)) {
		warnx("error while writing new crontab to %s", TempFilename);
		fclose(tmp);
		error = -2;
		goto done;
	}

	/* check the syntax of the file being installed.
	 */

	/* BUG: was reporting errors after the EOF if there were any errors
	 * in the file proper -- kludged it by stopping after first error.
	 *		vix 31mar87
	 */
	Set_LineNum(1 - NHEADER_LINES)
	CheckErrorCount = 0;  eof = FALSE;
	while (!CheckErrorCount && !eof) {
		switch (load_env(envstr, tmp)) {
		case -1:
			/* check for data before the EOF */
			if (envstr[0] != '\0') {
				Set_LineNum(LineNumber + 1);
				check_error("premature EOF");
			}
			eof = TRUE;
			break;
		case FALSE:
			e = load_entry(tmp, check_error, pw, envp);
			if (e)
				free_entry(e);
			break;
		case TRUE:
			break;
		}
	}

	if (CheckErrorCount != 0) {
		warnx("errors in crontab file, unable to install");
		fclose(tmp);
		error = -1;
		goto done;
	}

	if (fclose(tmp) == EOF) {
		warn("fclose");
		error = -2;
		goto done;
	}

	if (snprintf(n, sizeof n, "%s/%s", _PATH_CRON_SPOOL, User) >= sizeof(n)) {
		warnc(ENAMETOOLONG, "%s/%s", _PATH_CRON_SPOOL, User);
		error = -2;
		goto done;
	}
	if (rename(TempFilename, n)) {
		warn("unable to rename %s to %s", TempFilename, n);
		error = -2;
		goto done;
	}
	TempFilename[0] = '\0';
	syslog(LOG_INFO, "(%s) REPLACE (%s)", RealUser, User);

	poke_daemon(RELOAD_CRON);

done:
	(void) signal(SIGHUP, SIG_DFL);
	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);
	if (TempFilename[0]) {
		(void) unlink(TempFilename);
		TempFilename[0] = '\0';
	}
	return (error);
}

/*
 * Execute an editor on the specified pathname, which is interpreted
 * from the shell.  This means flags may be included.
 *
 * Returns -1 on error, or the exit value on success.
 */
int
editit(const char *pathname)
{
	char *argp[] = {"sh", "-c", NULL, NULL}, *ed, *p;
	sig_t sighup, sigint, sigquit, sigchld;
	pid_t pid;
	int saved_errno, st, ret = -1;

	ed = getenv("VISUAL");
	if (ed == NULL || ed[0] == '\0')
		ed = getenv("EDITOR");
	if (ed == NULL || ed[0] == '\0')
		ed = _PATH_VI;
	if (asprintf(&p, "%s %s", ed, pathname) == -1)
		return (-1);
	argp[2] = p;

	sighup = signal(SIGHUP, SIG_IGN);
	sigint = signal(SIGINT, SIG_IGN);
	sigquit = signal(SIGQUIT, SIG_IGN);
	sigchld = signal(SIGCHLD, SIG_DFL);
	if ((pid = fork()) == -1)
		goto fail;
	if (pid == 0) {
		/* Drop setgid and exec the command. */
		if (setgid(user_gid) == -1) {
			warn("unable to set gid to %u", user_gid);
		} else {
			execv(_PATH_BSHELL, argp);
			warn("unable to execute %s", _PATH_BSHELL);
		}
		_exit(127);
	}
	while (waitpid(pid, &st, 0) == -1)
		if (errno != EINTR)
			goto fail;
	if (!WIFEXITED(st))
		errno = EINTR;
	else
		ret = WEXITSTATUS(st);

 fail:
	saved_errno = errno;
	(void)signal(SIGHUP, sighup);
	(void)signal(SIGINT, sigint);
	(void)signal(SIGQUIT, sigquit);
	(void)signal(SIGCHLD, sigchld);
	free(p);
	errno = saved_errno;
	return (ret);
}

static void
die(int x)
{
	if (TempFilename[0])
		(void) unlink(TempFilename);
	_exit(EXIT_FAILURE);
}

static void
copy_crontab(FILE *f, FILE *out)
{
	int ch, x;

	/* ignore the top few comments since we probably put them there.
	 */
	x = 0;
	while (EOF != (ch = get_char(f))) {
		if ('#' != ch) {
			putc(ch, out);
			break;
		}
		while (EOF != (ch = get_char(f)))
			if (ch == '\n')
				break;
		if (++x >= NHEADER_LINES)
			break;
	}

	/* copy out the rest of the crontab (if any)
	 */
	if (EOF != ch)
		while (EOF != (ch = get_char(f)))
			putc(ch, out);
}
