/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Paul Vixie          <paul@vix.com>          uunet!decwrl!vixie!paul
 * From Id: crontab.c,v 2.13 1994/01/17 03:20:37 vixie Exp
 */

#if !defined(lint) && !defined(LINT)
static const char rcsid[] =
  "$FreeBSD$";
#endif

/* crontab - install and manage per-user crontab files
 * vix 02may87 [RCS has the rest of the log]
 * vix 26jan87 [original]
 */

#define	MAIN_PROGRAM

#include <sys/param.h>
#include "cron.h"
#include <errno.h>
#include <fcntl.h>
#include <md5.h>
#include <paths.h>
#include <sys/file.h>
#include <sys/stat.h>
#ifdef USE_UTIMES
# include <sys/time.h>
#else
# include <time.h>
# include <utime.h>
#endif
#if defined(POSIX)
# include <locale.h>
#endif

#define MD5_SIZE 33
#define NHEADER_LINES 3


enum opt_t	{ opt_unknown, opt_list, opt_delete, opt_edit, opt_replace };

#if DEBUGGING
static char	*Options[] = { "???", "list", "delete", "edit", "replace" };
#endif


static	PID_T		Pid;
static	char		User[MAXLOGNAME], RealUser[MAXLOGNAME];
static	char		Filename[MAX_FNAME];
static	FILE		*NewCrontab;
static	int		CheckErrorCount;
static	enum opt_t	Option;
static	int		fflag;
static	struct passwd	*pw;
static	void		list_cmd(void),
			delete_cmd(void),
			edit_cmd(void),
			poke_daemon(void),
			check_error(char *),
			parse_args(int c, char *v[]);
static	int		replace_cmd(void);


static void
usage(char *msg)
{
	fprintf(stderr, "crontab: usage error: %s\n", msg);
	fprintf(stderr, "%s\n%s\n",
		"usage: crontab [-u user] file",
		"       crontab [-u user] { -l | -r [-f] | -e }");
	exit(ERROR_EXIT);
}


int
main(int argc, char *argv[])
{
	int	exitstatus;

	Pid = getpid();
	ProgramName = argv[0];

#if defined(POSIX)
	setlocale(LC_ALL, "");
#endif

#if defined(BSD)
	setlinebuf(stderr);
#endif
	parse_args(argc, argv);		/* sets many globals, opens a file */
	set_cron_uid();
	set_cron_cwd();
	if (!allowed(User)) {
		warnx("you (%s) are not allowed to use this program", User);
		log_it(RealUser, Pid, "AUTH", "crontab command not allowed");
		exit(ERROR_EXIT);
	}
	exitstatus = OK_EXIT;
	switch (Option) {
	case opt_list:		list_cmd();
				break;
	case opt_delete:	delete_cmd();
				break;
	case opt_edit:		edit_cmd();
				break;
	case opt_replace:	if (replace_cmd() < 0)
					exitstatus = ERROR_EXIT;
				break;
	case opt_unknown:
				break;
	}
	exit(exitstatus);
	/*NOTREACHED*/
}


static void
parse_args(argc, argv)
	int	argc;
	char	*argv[];
{
	int		argch;
	char		resolved_path[PATH_MAX];

	if (!(pw = getpwuid(getuid())))
		errx(ERROR_EXIT, "your UID isn't in the passwd file, bailing out");
	bzero(pw->pw_passwd, strlen(pw->pw_passwd));
	(void) strncpy(User, pw->pw_name, (sizeof User)-1);
	User[(sizeof User)-1] = '\0';
	strcpy(RealUser, User);
	Filename[0] = '\0';
	Option = opt_unknown;
	while ((argch = getopt(argc, argv, "u:lerx:f")) != -1) {
		switch (argch) {
		case 'x':
			if (!set_debug_flags(optarg))
				usage("bad debug option");
			break;
		case 'u':
			if (getuid() != ROOT_UID)
				errx(ERROR_EXIT, "must be privileged to use -u");
			if (!(pw = getpwnam(optarg)))
				errx(ERROR_EXIT, "user `%s' unknown", optarg);
			bzero(pw->pw_passwd, strlen(pw->pw_passwd));
			(void) strncpy(User, pw->pw_name, (sizeof User)-1);
			User[(sizeof User)-1] = '\0';
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
		case 'f':
			fflag = 1;
			break;
		default:
			usage("unrecognized option");
		}
	}

	endpwent();

	if (Option != opt_unknown) {
		if (argv[optind] != NULL) {
			usage("no arguments permitted after this option");
		}
	} else {
		if (argv[optind] != NULL) {
			Option = opt_replace;
			(void) strncpy (Filename, argv[optind], (sizeof Filename)-1);
			Filename[(sizeof Filename)-1] = '\0';

		} else {
			usage("file name must be specified for replace");
		}
	}

	if (Option == opt_replace) {
		/* relinquish the setuid status of the binary during
		 * the open, lest nonroot users read files they should
		 * not be able to read.  we can't use access() here
		 * since there's a race condition.  thanks go out to
		 * Arnt Gulbrandsen <agulbra@pvv.unit.no> for spotting
		 * the race.
		 */

		if (swap_uids() < OK)
			err(ERROR_EXIT, "swapping uids");

		/* we have to open the file here because we're going to
		 * chdir(2) into /var/cron before we get around to
		 * reading the file.
		 */
		if (!strcmp(Filename, "-")) {
			NewCrontab = stdin;
		} else if (realpath(Filename, resolved_path) != NULL &&
		    !strcmp(resolved_path, SYSCRONTAB)) {
			err(ERROR_EXIT, SYSCRONTAB " must be edited manually");
		} else {
			if (!(NewCrontab = fopen(Filename, "r")))
				err(ERROR_EXIT, "%s", Filename);
		}
		if (swap_uids_back() < OK)
			err(ERROR_EXIT, "swapping uids back");
	}

	Debug(DMISC, ("user=%s, file=%s, option=%s\n",
		      User, Filename, Options[(int)Option]))
}

static void
copy_file(FILE *in, FILE *out) {
	int	x, ch;

	Set_LineNum(1)
	/* ignore the top few comments since we probably put them there.
	 */
	for (x = 0;  x < NHEADER_LINES;  x++) {
		ch = get_char(in);
		if (EOF == ch)
			break;
		if ('#' != ch) {
			putc(ch, out);
			break;
		}
		while (EOF != (ch = get_char(in)))
			if (ch == '\n')
				break;
		if (EOF == ch)
			break;
	}

	/* copy the rest of the crontab (if any) to the output file.
	 */
	if (EOF != ch)
		while (EOF != (ch = get_char(in)))
			putc(ch, out);
}

static void
list_cmd() {
	char	n[MAX_FNAME];
	FILE	*f;

	log_it(RealUser, Pid, "LIST", User);
	(void) snprintf(n, sizeof(n), CRON_TAB(User));
	if (!(f = fopen(n, "r"))) {
		if (errno == ENOENT)
			errx(ERROR_EXIT, "no crontab for %s", User);
		else
			err(ERROR_EXIT, "%s", n);
	}

	/* file is open. copy to stdout, close.
	 */
	copy_file(f, stdout);
	fclose(f);
}


static void
delete_cmd() {
	char	n[MAX_FNAME];
	int ch, first;

	if (!fflag && isatty(STDIN_FILENO)) {
		(void)fprintf(stderr, "remove crontab for %s? ", User);
		first = ch = getchar();
		while (ch != '\n' && ch != EOF)
			ch = getchar();
		if (first != 'y' && first != 'Y')
			return;
	}

	log_it(RealUser, Pid, "DELETE", User);
	(void) snprintf(n, sizeof(n), CRON_TAB(User));
	if (unlink(n)) {
		if (errno == ENOENT)
			errx(ERROR_EXIT, "no crontab for %s", User);
		else
			err(ERROR_EXIT, "%s", n);
	}
	poke_daemon();
}


static void
check_error(msg)
	char	*msg;
{
	CheckErrorCount++;
	fprintf(stderr, "\"%s\":%d: %s\n", Filename, LineNumber-1, msg);
}


static void
edit_cmd() {
	char		n[MAX_FNAME], q[MAX_TEMPSTR], *editor;
	FILE		*f;
	int		t;
	struct stat	statbuf, fsbuf;
	WAIT_T		waiter;
	PID_T		pid, xpid;
	mode_t		um;
	int		syntax_error = 0;
	char		orig_md5[MD5_SIZE];
	char		new_md5[MD5_SIZE];

	log_it(RealUser, Pid, "BEGIN EDIT", User);
	(void) snprintf(n, sizeof(n), CRON_TAB(User));
	if (!(f = fopen(n, "r"))) {
		if (errno != ENOENT)
			err(ERROR_EXIT, "%s", n);
		warnx("no crontab for %s - using an empty one", User);
		if (!(f = fopen(_PATH_DEVNULL, "r")))
			err(ERROR_EXIT, _PATH_DEVNULL);
	}

	um = umask(077);
	(void) snprintf(Filename, sizeof(Filename), "/tmp/crontab.XXXXXXXXXX");
	if ((t = mkstemp(Filename)) == -1) {
		warn("%s", Filename);
		(void) umask(um);
		goto fatal;
	}
	(void) umask(um);
#ifdef HAS_FCHOWN
	if (fchown(t, getuid(), getgid()) < 0) {
#else
	if (chown(Filename, getuid(), getgid()) < 0) {
#endif
		warn("fchown");
		goto fatal;
	}
	if (!(NewCrontab = fdopen(t, "r+"))) {
		warn("fdopen");
		goto fatal;
	}

	copy_file(f, NewCrontab);
	fclose(f);
	if (fflush(NewCrontab))
		err(ERROR_EXIT, "%s", Filename);
	if (fstat(t, &fsbuf) < 0) {
		warn("unable to fstat temp file");
		goto fatal;
	}
 again:
	if (swap_uids() < OK)
		err(ERROR_EXIT, "swapping uids");
	if (stat(Filename, &statbuf) < 0) {
		warn("stat");
 fatal:		unlink(Filename);
		exit(ERROR_EXIT);
	}
	if (swap_uids_back() < OK)
		err(ERROR_EXIT, "swapping uids back");
	if (statbuf.st_dev != fsbuf.st_dev || statbuf.st_ino != fsbuf.st_ino)
		errx(ERROR_EXIT, "temp file must be edited in place");
	if (MD5File(Filename, orig_md5) == NULL) {
		warn("MD5");
		goto fatal;
	}

	if ((!(editor = getenv("VISUAL")))
	 && (!(editor = getenv("EDITOR")))
	    ) {
		editor = EDITOR;
	}

	/* we still have the file open.  editors will generally rewrite the
	 * original file rather than renaming/unlinking it and starting a
	 * new one; even backup files are supposed to be made by copying
	 * rather than by renaming.  if some editor does not support this,
	 * then don't use it.  the security problems are more severe if we
	 * close and reopen the file around the edit.
	 */

	switch (pid = fork()) {
	case -1:
		warn("fork");
		goto fatal;
	case 0:
		/* child */
		if (setuid(getuid()) < 0)
			err(ERROR_EXIT, "setuid(getuid())");
		if (chdir("/tmp") < 0)
			err(ERROR_EXIT, "chdir(/tmp)");
		if (strlen(editor) + strlen(Filename) + 2 >= MAX_TEMPSTR)
			errx(ERROR_EXIT, "editor or filename too long");
		execlp(editor, editor, Filename, (char *)NULL);
		err(ERROR_EXIT, "%s", editor);
		/*NOTREACHED*/
	default:
		/* parent */
		break;
	}

	/* parent */
	{
	void (*sig[3])(int signal);
	sig[0] = signal(SIGHUP, SIG_IGN);
	sig[1] = signal(SIGINT, SIG_IGN);
	sig[2] = signal(SIGTERM, SIG_IGN);
	xpid = wait(&waiter);
	signal(SIGHUP, sig[0]);
	signal(SIGINT, sig[1]);
	signal(SIGTERM, sig[2]);
	}
	if (xpid != pid) {
		warnx("wrong PID (%d != %d) from \"%s\"", xpid, pid, editor);
		goto fatal;
	}
	if (WIFEXITED(waiter) && WEXITSTATUS(waiter)) {
		warnx("\"%s\" exited with status %d", editor, WEXITSTATUS(waiter));
		goto fatal;
	}
	if (WIFSIGNALED(waiter)) {
		warnx("\"%s\" killed; signal %d (%score dumped)",
			editor, WTERMSIG(waiter), WCOREDUMP(waiter) ?"" :"no ");
		goto fatal;
	}
	if (swap_uids() < OK)
		err(ERROR_EXIT, "swapping uids");
	if (stat(Filename, &statbuf) < 0) {
		warn("stat");
		goto fatal;
	}
	if (statbuf.st_dev != fsbuf.st_dev || statbuf.st_ino != fsbuf.st_ino)
		errx(ERROR_EXIT, "temp file must be edited in place");
	if (MD5File(Filename, new_md5) == NULL) {
		warn("MD5");
		goto fatal;
	}
	if (swap_uids_back() < OK)
		err(ERROR_EXIT, "swapping uids back");
	if (strcmp(orig_md5, new_md5) == 0 && !syntax_error) {
		warnx("no changes made to crontab");
		goto remove;
	}
	warnx("installing new crontab");
	switch (replace_cmd()) {
	case 0:			/* Success */
		break;
	case -1:		/* Syntax error */
		for (;;) {
			printf("Do you want to retry the same edit? ");
			fflush(stdout);
			q[0] = '\0';
			(void) fgets(q, sizeof q, stdin);
			switch (islower(q[0]) ? q[0] : tolower(q[0])) {
			case 'y':
				syntax_error = 1;
				goto again;
			case 'n':
				goto abandon;
			default:
				fprintf(stderr, "Enter Y or N\n");
			}
		}
		/*NOTREACHED*/
	case -2:		/* Install error */
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
	log_it(RealUser, Pid, "END EDIT", User);
}


/* returns	0	on success
 *		-1	on syntax error
 *		-2	on install error
 */
static int
replace_cmd() {
	char	n[MAX_FNAME], envstr[MAX_ENVSTR], tn[MAX_FNAME];
	FILE	*tmp;
	int	ch, eof;
	entry	*e;
	time_t	now = time(NULL);
	char	**envp = env_init();

	if (envp == NULL) {
		warnx("cannot allocate memory");
		return (-2);
	}

	(void) snprintf(n, sizeof(n), "tmp.%d", Pid);
	(void) snprintf(tn, sizeof(tn), CRON_TAB(n));

	if (!(tmp = fopen(tn, "w+"))) {
		warn("%s", tn);
		return (-2);
	}

	/* write a signature at the top of the file.
	 *
	 * VERY IMPORTANT: make sure NHEADER_LINES agrees with this code.
	 */
	fprintf(tmp, "# DO NOT EDIT THIS FILE - edit the master and reinstall.\n");
	fprintf(tmp, "# (%s installed on %-24.24s)\n", Filename, ctime(&now));
	fprintf(tmp, "# (Cron version -- %s)\n", rcsid);

	/* copy the crontab to the tmp
	 */
	rewind(NewCrontab);
	Set_LineNum(1)
	while (EOF != (ch = get_char(NewCrontab)))
		putc(ch, tmp);
	ftruncate(fileno(tmp), ftello(tmp));
	fflush(tmp);  rewind(tmp);

	if (ferror(tmp)) {
		warnx("error while writing new crontab to %s", tn);
		fclose(tmp);  unlink(tn);
		return (-2);
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
		case ERR:
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
		warnx("errors in crontab file, can't install");
		fclose(tmp);  unlink(tn);
		return (-1);
	}

#ifdef HAS_FCHOWN
	if (fchown(fileno(tmp), ROOT_UID, -1) < OK)
#else
	if (chown(tn, ROOT_UID, -1) < OK)
#endif
	{
		warn("chown");
		fclose(tmp);  unlink(tn);
		return (-2);
	}

#ifdef HAS_FCHMOD
	if (fchmod(fileno(tmp), 0600) < OK)
#else
	if (chmod(tn, 0600) < OK)
#endif
	{
		warn("chown");
		fclose(tmp);  unlink(tn);
		return (-2);
	}

	if (fclose(tmp) == EOF) {
		warn("fclose");
		unlink(tn);
		return (-2);
	}

	(void) snprintf(n, sizeof(n), CRON_TAB(User));
	if (rename(tn, n)) {
		warn("error renaming %s to %s", tn, n);
		unlink(tn);
		return (-2);
	}

	log_it(RealUser, Pid, "REPLACE", User);

	/*
	 * Creating the 'tn' temp file has already updated the
	 * modification time of the spool directory.  Sleep for a
	 * second to ensure that poke_daemon() sets a later
	 * modification time.  Otherwise, this can race with the cron
	 * daemon scanning for updated crontabs.
	 */
	sleep(1);

	poke_daemon();

	return (0);
}


static void
poke_daemon() {
#ifdef USE_UTIMES
	struct timeval tvs[2];

	(void)gettimeofday(&tvs[0], NULL);
	tvs[1] = tvs[0];
	if (utimes(SPOOL_DIR, tvs) < OK) {
		warn("can't update mtime on spooldir %s", SPOOL_DIR);
		return;
	}
#else
	if (utime(SPOOL_DIR, NULL) < OK) {
		warn("can't update mtime on spooldir %s", SPOOL_DIR);
		return;
	}
#endif /*USE_UTIMES*/
}
