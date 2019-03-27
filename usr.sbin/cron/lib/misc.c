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
 */

#if !defined(lint) && !defined(LINT)
static const char rcsid[] =
  "$FreeBSD$";
#endif

/* vix 26jan87 [RCS has the rest of the log]
 * vix 30dec86 [written]
 */


#include "cron.h"
#if SYS_TIME_H
# include <sys/time.h>
#else
# include <time.h>
#endif
#include <sys/file.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#if defined(SYSLOG)
# include <syslog.h>
#endif


#if defined(LOG_DAEMON) && !defined(LOG_CRON)
#define LOG_CRON LOG_DAEMON
#endif


static int		LogFD = ERR;


int
strcmp_until(left, right, until)
	char	*left;
	char	*right;
	int	until;
{
	register int	diff;

	while (*left && *left != until && *left == *right) {
		left++;
		right++;
	}

	if ((*left=='\0' || *left == until) &&
	    (*right=='\0' || *right == until)) {
		diff = 0;
	} else {
		diff = *left - *right;
	}

	return diff;
}


/* strdtb(s) - delete trailing blanks in string 's' and return new length
 */
int
strdtb(s)
	char	*s;
{
	char	*x = s;

	/* scan forward to the null
	 */
	while (*x)
		x++;

	/* scan backward to either the first character before the string,
	 * or the last non-blank in the string, whichever comes first.
	 */
	do	{x--;}
	while (x >= s && isspace(*x));

	/* one character beyond where we stopped above is where the null
	 * goes.
	 */
	*++x = '\0';

	/* the difference between the position of the null character and
	 * the position of the first character of the string is the length.
	 */
	return x - s;
}


int
set_debug_flags(flags)
	char	*flags;
{
	/* debug flags are of the form    flag[,flag ...]
	 *
	 * if an error occurs, print a message to stdout and return FALSE.
	 * otherwise return TRUE after setting ERROR_FLAGS.
	 */

#if !DEBUGGING

	printf("this program was compiled without debugging enabled\n");
	return FALSE;

#else /* DEBUGGING */

	char	*pc = flags;

	DebugFlags = 0;

	while (*pc) {
		char	**test;
		int	mask;

		/* try to find debug flag name in our list.
		 */
		for (	test = DebugFlagNames, mask = 1;
			*test && strcmp_until(*test, pc, ',');
			test++, mask <<= 1
		    )
			;

		if (!*test) {
			fprintf(stderr,
				"unrecognized debug flag <%s> <%s>\n",
				flags, pc);
			return FALSE;
		}

		DebugFlags |= mask;

		/* skip to the next flag
		 */
		while (*pc && *pc != ',')
			pc++;
		if (*pc == ',')
			pc++;
	}

	if (DebugFlags) {
		int	flag;

		fprintf(stderr, "debug flags enabled:");

		for (flag = 0;  DebugFlagNames[flag];  flag++)
			if (DebugFlags & (1 << flag))
				fprintf(stderr, " %s", DebugFlagNames[flag]);
		fprintf(stderr, "\n");
	}

	return TRUE;

#endif /* DEBUGGING */
}


void
set_cron_uid()
{
#if defined(BSD) || defined(POSIX)
	if (seteuid(ROOT_UID) < OK)
		err(ERROR_EXIT, "seteuid");
#else
	if (setuid(ROOT_UID) < OK)
		err(ERROR_EXIT, "setuid");
#endif
}


void
set_cron_cwd()
{
	struct stat	sb;

	/* first check for CRONDIR ("/var/cron" or some such)
	 */
	if (stat(CRONDIR, &sb) < OK && errno == ENOENT) {
		warn("%s", CRONDIR);
		if (OK == mkdir(CRONDIR, 0700)) {
			warnx("%s: created", CRONDIR);
			stat(CRONDIR, &sb);
		} else {
			err(ERROR_EXIT, "%s: mkdir", CRONDIR);
		}
	}
	if (!(sb.st_mode & S_IFDIR))
		err(ERROR_EXIT, "'%s' is not a directory, bailing out", CRONDIR);
	if (chdir(CRONDIR) < OK)
		err(ERROR_EXIT, "cannot chdir(%s), bailing out", CRONDIR);

	/* CRONDIR okay (now==CWD), now look at SPOOL_DIR ("tabs" or some such)
	 */
	if (stat(SPOOL_DIR, &sb) < OK && errno == ENOENT) {
		warn("%s", SPOOL_DIR);
		if (OK == mkdir(SPOOL_DIR, 0700)) {
			warnx("%s: created", SPOOL_DIR);
			stat(SPOOL_DIR, &sb);
		} else {
			err(ERROR_EXIT, "%s: mkdir", SPOOL_DIR);
		}
	}
	if (!(sb.st_mode & S_IFDIR))
		err(ERROR_EXIT, "'%s' is not a directory, bailing out", SPOOL_DIR);
}


/* get_char(file) : like getc() but increment LineNumber on newlines
 */
int
get_char(file)
	FILE	*file;
{
	int	ch;

	ch = getc(file);
	if (ch == '\n')
		Set_LineNum(LineNumber + 1)
	return ch;
}


/* unget_char(ch, file) : like ungetc but do LineNumber processing
 */
void
unget_char(ch, file)
	int	ch;
	FILE	*file;
{
	ungetc(ch, file);
	if (ch == '\n')
		Set_LineNum(LineNumber - 1)
}


/* get_string(str, max, file, termstr) : like fgets() but
 *		(1) has terminator string which should include \n
 *		(2) will always leave room for the null
 *		(3) uses get_char() so LineNumber will be accurate
 *		(4) returns EOF or terminating character, whichever
 */
int
get_string(string, size, file, terms)
	char	*string;
	int	size;
	FILE	*file;
	char	*terms;
{
	int	ch;

	while (EOF != (ch = get_char(file)) && !strchr(terms, ch)) {
		if (size > 1) {
			*string++ = (char) ch;
			size--;
		}
	}

	if (size > 0)
		*string = '\0';

	return ch;
}


/* skip_comments(file) : read past comment (if any)
 */
void
skip_comments(file)
	FILE	*file;
{
	int	ch;

	while (EOF != (ch = get_char(file))) {
		/* ch is now the first character of a line.
		 */

		while (ch == ' ' || ch == '\t')
			ch = get_char(file);

		if (ch == EOF)
			break;

		/* ch is now the first non-blank character of a line.
		 */

		if (ch != '\n' && ch != '#')
			break;

		/* ch must be a newline or comment as first non-blank
		 * character on a line.
		 */

		while (ch != '\n' && ch != EOF)
			ch = get_char(file);

		/* ch is now the newline of a line which we're going to
		 * ignore.
		 */
	}
	if (ch != EOF)
		unget_char(ch, file);
}


/* int in_file(char *string, FILE *file)
 *	return TRUE if one of the lines in file matches string exactly,
 *	FALSE otherwise.
 */
static int
in_file(char *string, FILE *file)
{
	char line[MAX_TEMPSTR];

	rewind(file);
	while (fgets(line, MAX_TEMPSTR, file)) {
		if (line[0] != '\0')
			if (line[strlen(line)-1] == '\n')
				line[strlen(line)-1] = '\0';
		if (0 == strcmp(line, string))
			return TRUE;
	}
	return FALSE;
}


/* int allowed(char *username)
 *	returns TRUE if (ALLOW_FILE exists and user is listed)
 *	or (DENY_FILE exists and user is NOT listed)
 *	or (neither file exists but user=="root" so it's okay)
 */
int
allowed(username)
	char *username;
{
	FILE	*allow, *deny;
	int	isallowed;

	isallowed = FALSE;

	deny = NULL;
#if defined(ALLOW_FILE) && defined(DENY_FILE)
	if ((allow = fopen(ALLOW_FILE, "r")) == NULL && errno != ENOENT)
		goto out;
	if ((deny = fopen(DENY_FILE, "r")) == NULL && errno != ENOENT)
		goto out;
	Debug(DMISC, ("allow/deny enabled, %d/%d\n", !!allow, !!deny))
#else
	allow = NULL;
#endif

	if (allow)
		isallowed = in_file(username, allow);
	else if (deny)
		isallowed = !in_file(username, deny);
	else {
#if defined(ALLOW_ONLY_ROOT)
		isallowed = (strcmp(username, ROOT_USER) == 0);
#else
		isallowed = TRUE; 
#endif
	}
out:	if (allow)
		fclose(allow);
	if (deny)
		fclose(deny);
	return (isallowed);
}


void
log_it(char *username, int xpid, char *event, const char *detail)
{
#if defined(LOG_FILE) || DEBUGGING
	PID_T			pid = xpid;
#endif
#if defined(LOG_FILE)
	char			*msg;
	TIME_T			now = time((TIME_T) 0);
	register struct tm	*t = localtime(&now);
#endif /*LOG_FILE*/

#if defined(SYSLOG)
	static int		syslog_open = 0;
#endif

#if defined(LOG_FILE)
	/* we assume that MAX_TEMPSTR will hold the date, time, &punctuation.
	 */
	msg = malloc(strlen(username)
		     + strlen(event)
		     + strlen(detail)
		     + MAX_TEMPSTR);

	if (msg == NULL)
		warnx("failed to allocate memory for log message");
	else {
		if (LogFD < OK) {
			LogFD = open(LOG_FILE, O_WRONLY|O_APPEND|O_CREAT, 0600);
			if (LogFD < OK) {
				warn("can't open log file %s", LOG_FILE);
			} else {
				(void) fcntl(LogFD, F_SETFD, 1);
			}
		}

		/* we have to sprintf() it because fprintf() doesn't always
		 * write everything out in one chunk and this has to be
		 * atomically appended to the log file.
		 */
		sprintf(msg, "%s (%02d/%02d-%02d:%02d:%02d-%d) %s (%s)\n",
			username,
			t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min,
			t->tm_sec, pid, event, detail);

		/* we have to run strlen() because sprintf() returns (char*)
		 * on old BSD.
		 */
		if (LogFD < OK || write(LogFD, msg, strlen(msg)) < OK) {
			if (LogFD >= OK)
				warn("%s", LOG_FILE);
			warnx("can't write to log file");
			write(STDERR, msg, strlen(msg));
		}

		free(msg);
	}
#endif /*LOG_FILE*/

#if defined(SYSLOG)
	if (!syslog_open) {
		/* we don't use LOG_PID since the pid passed to us by
		 * our client may not be our own.  therefore we want to
		 * print the pid ourselves.
		 */
# ifdef LOG_DAEMON
		openlog(ProgramName, LOG_PID, LOG_CRON);
# else
		openlog(ProgramName, LOG_PID);
# endif
		syslog_open = TRUE;		/* assume openlog success */
	}

	syslog(LOG_INFO, "(%s) %s (%s)\n", username, event, detail);

#endif /*SYSLOG*/

#if DEBUGGING
	if (DebugFlags) {
		fprintf(stderr, "log_it: (%s %d) %s (%s)\n",
			username, pid, event, detail);
	}
#endif
}


void
log_close() {
	if (LogFD != ERR) {
		close(LogFD);
		LogFD = ERR;
	}
}


/* two warnings:
 *	(1) this routine is fairly slow
 *	(2) it returns a pointer to static storage
 */
char *
first_word(s, t)
	register char *s;	/* string we want the first word of */
	register char *t;	/* terminators, implicitly including \0 */
{
	static char retbuf[2][MAX_TEMPSTR + 1];	/* sure wish C had GC */
	static int retsel = 0;
	register char *rb, *rp;

	/* select a return buffer */
	retsel = 1-retsel;
	rb = &retbuf[retsel][0];
	rp = rb;

	/* skip any leading terminators */
	while (*s && (NULL != strchr(t, *s))) {
		s++;
	}

	/* copy until next terminator or full buffer */
	while (*s && (NULL == strchr(t, *s)) && (rp < &rb[MAX_TEMPSTR])) {
		*rp++ = *s++;
	}

	/* finish the return-string and return it */
	*rp = '\0';
	return rb;
}


/* warning:
 *	heavily ascii-dependent.
 */
static void
mkprint(register char *dst, register unsigned char *src, register int len)
{
	while (len-- > 0)
	{
		register unsigned char ch = *src++;

		if (ch < ' ') {			/* control character */
			*dst++ = '^';
			*dst++ = ch + '@';
		} else if (ch < 0177) {		/* printable */
			*dst++ = ch;
		} else if (ch == 0177) {	/* delete/rubout */
			*dst++ = '^';
			*dst++ = '?';
		} else {			/* parity character */
			sprintf(dst, "\\%03o", ch);
			dst += 4;
		}
	}
	*dst = '\0';
}


/* warning:
 *	returns a pointer to malloc'd storage, you must call free yourself.
 */
char *
mkprints(src, len)
	register unsigned char *src;
	register unsigned int len;
{
	register char *dst = malloc(len*4 + 1);

	if (dst != NULL)
		mkprint(dst, src, len);

	return dst;
}


#ifdef MAIL_DATE
/* Sat, 27 Feb 93 11:44:51 CST
 * 123456789012345678901234567
 */
char *
arpadate(clock)
	time_t *clock;
{
	time_t t = clock ?*clock :time(0L);
	struct tm *tm = localtime(&t);
	static char ret[32];	/* zone name might be >3 chars */

	if (tm->tm_year >= 100)
		tm->tm_year += 1900;

	(void) snprintf(ret, sizeof(ret), "%s, %2d %s %d %02d:%02d:%02d %s",
		       DowNames[tm->tm_wday],
		       tm->tm_mday,
		       MonthNames[tm->tm_mon],
		       tm->tm_year,
		       tm->tm_hour,
		       tm->tm_min,
		       tm->tm_sec,
		       TZONE(*tm));
	return ret;
}
#endif /*MAIL_DATE*/


#ifdef HAVE_SAVED_UIDS
static int save_euid;
int swap_uids() { save_euid = geteuid(); return seteuid(getuid()); }
int swap_uids_back() { return seteuid(save_euid); }
#else /*HAVE_SAVED_UIDS*/
int swap_uids() { return setreuid(geteuid(), getuid()); }
int swap_uids_back() { return swap_uids(); }
#endif /*HAVE_SAVED_UIDS*/
