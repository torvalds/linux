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

/* cron.h - header for vixie's cron
 *
 * $FreeBSD$
 *
 * vix 14nov88 [rest of log is in RCS]
 * vix 14jan87 [0 or 7 can be sunday; thanks, mwm@berkeley]
 * vix 30dec86 [written]
 */

/* reorder these #include's at your peril */

#include <sys/types.h>
#include <sys/param.h>
#include "compat.h"

#include <bitstring.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <sys/wait.h>

#include "pathnames.h"
#include "config.h"
#include "externs.h"

	/* these are really immutable, and are
	 *   defined for symbolic convenience only
	 * TRUE, FALSE, and ERR must be distinct
	 * ERR must be < OK.
	 */
#define TRUE		1
#define FALSE		0
	/* system calls return this on success */
#define OK		0
	/*   or this on error */
#define ERR		(-1)

	/* turn this on to get '-x' code */
#ifndef DEBUGGING
#define DEBUGGING	FALSE
#endif

#define READ_PIPE	0	/* which end of a pipe pair do you read? */
#define WRITE_PIPE	1	/*   or write to? */
#define STDIN		0	/* what is stdin's file descriptor? */
#define STDOUT		1	/*   stdout's? */
#define STDERR		2	/*   stderr's? */
#define ERROR_EXIT	1	/* exit() with this will scare the shell */
#define	OK_EXIT		0	/* exit() with this is considered 'normal' */
#define	MAX_FNAME	100	/* max length of internally generated fn */
#define	MAX_COMMAND	1000	/* max length of internally generated cmd */
#define	MAX_ENVSTR	1000	/* max length of envvar=value\0 strings */
#define	MAX_TEMPSTR	100	/* obvious */
#define	ROOT_UID	0	/* don't change this, it really must be root */
#define	ROOT_USER	"root"	/* ditto */
#define	SYS_NAME	"*system*" /* magic owner name for system crontab */

				/* NOTE: these correspond to DebugFlagNames,
				 *	defined below.
				 */
#define	DEXT		0x0001	/* extend flag for other debug masks */
#define	DSCH		0x0002	/* scheduling debug mask */
#define	DPROC		0x0004	/* process control debug mask */
#define	DPARS		0x0008	/* parsing debug mask */
#define	DLOAD		0x0010	/* database loading debug mask */
#define	DMISC		0x0020	/* misc debug mask */
#define	DTEST		0x0040	/* test mode: don't execute any commands */
#define	DBIT		0x0080	/* bit twiddling shown (long) */

#define	CRON_TAB(u)	"%s/%s", SPOOL_DIR, u
#define	REG		register
#define	PPC_NULL	((char **)NULL)

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#define	Skip_Blanks(c, f) \
			while (c == '\t' || c == ' ') \
				c = get_char(f);

#define	Skip_Nonblanks(c, f) \
			while (c!='\t' && c!=' ' && c!='\n' && c != EOF) \
				c = get_char(f);

#define	Skip_Line(c, f) \
			do {c = get_char(f);} while (c != '\n' && c != EOF);

#if DEBUGGING
# define Debug(mask, message) \
			if ( (DebugFlags & (mask) ) == (mask) ) \
				printf message;
#else /* !DEBUGGING */
# define Debug(mask, message) \
			;
#endif /* DEBUGGING */

#define	MkLower(ch)	(isupper(ch) ? tolower(ch) : ch)
#define	MkUpper(ch)	(islower(ch) ? toupper(ch) : ch)
#define	Set_LineNum(ln)	{Debug(DPARS|DEXT,("linenum=%d\n",ln)); \
			 LineNumber = ln; \
			}

#define	FIRST_SECOND	0
#define	LAST_SECOND	59
#define	SECOND_COUNT	(LAST_SECOND - FIRST_SECOND + 1)

#define	FIRST_MINUTE	0
#define	LAST_MINUTE	59
#define	MINUTE_COUNT	(LAST_MINUTE - FIRST_MINUTE + 1)

#define	FIRST_HOUR	0
#define	LAST_HOUR	23
#define	HOUR_COUNT	(LAST_HOUR - FIRST_HOUR + 1)

#define	FIRST_DOM	1
#define	LAST_DOM	31
#define	DOM_COUNT	(LAST_DOM - FIRST_DOM + 1)

#define	FIRST_MONTH	1
#define	LAST_MONTH	12
#define	MONTH_COUNT	(LAST_MONTH - FIRST_MONTH + 1)

/* note on DOW: 0 and 7 are both Sunday, for compatibility reasons. */
#define	FIRST_DOW	0
#define	LAST_DOW	7
#define	DOW_COUNT	(LAST_DOW - FIRST_DOW + 1)

#ifdef LOGIN_CAP
/* see init.c */
#define RESOURCE_RC "daemon"
#endif

			/* each user's crontab will be held as a list of
			 * the following structure.
			 *
			 * These are the cron commands.
			 */

typedef	struct _entry {
	struct _entry	*next;
	uid_t		uid;
	gid_t		gid;
#ifdef LOGIN_CAP
	char            *class;
#endif
	char		**envp;
	char		*cmd;
	union {
		struct {
			bitstr_t	bit_decl(second, SECOND_COUNT);
			bitstr_t	bit_decl(minute, MINUTE_COUNT);
			bitstr_t	bit_decl(hour,   HOUR_COUNT);
			bitstr_t	bit_decl(dom,    DOM_COUNT);
			bitstr_t	bit_decl(month,  MONTH_COUNT);
			bitstr_t	bit_decl(dow,    DOW_COUNT);
		};
		struct {
			time_t	lastexit;
			time_t	interval;
			pid_t	child;
		};
	};
	int		flags;
#define	DOM_STAR	0x01
#define	DOW_STAR	0x02
#define	WHEN_REBOOT	0x04
#define	RUN_AT		0x08
#define	NOT_UNTIL	0x10
#define	SEC_RES		0x20
#define	INTERVAL	0x40
	time_t	lastrun;
} entry;

			/* the crontab database will be a list of the
			 * following structure, one element per user
			 * plus one for the system.
			 *
			 * These are the crontabs.
			 */

typedef	struct _user {
	struct _user	*next, *prev;	/* links */
	char		*name;
	time_t		mtime;		/* last modtime of crontab */
	entry		*crontab;	/* this person's crontab */
} user;

typedef	struct _cron_db {
	user		*head, *tail;	/* links */
	time_t		mtime;		/* last modtime on spooldir */
} cron_db;


void		set_cron_uid(void),
		set_cron_cwd(void),
		load_database(cron_db *),
		open_logfile(void),
		sigpipe_func(void),
		job_add(entry *, user *),
		do_command(entry *, user *),
		link_user(cron_db *, user *),
		unlink_user(cron_db *, user *),
		free_user(user *),
		env_free(char **),
		unget_char(int, FILE *),
		free_entry(entry *),
		skip_comments(FILE *),
		log_it(char *, int, char *, const char *),
		log_close(void);

int		job_runqueue(void),
		set_debug_flags(char *),
		get_char(FILE *),
		get_string(char *, int, FILE *, char *),
		swap_uids(void),
		swap_uids_back(void),
		load_env(char *, FILE *),
		cron_pclose(FILE *),
		strcmp_until(char *, char *, int),
		allowed(char *),
		strdtb(char *);

char		*env_get(char *, char **),
		*arpadate(time_t *),
		*mkprints(unsigned char *, unsigned int),
		*first_word(char *, char *),
		**env_init(void),
		**env_copy(char **),
		**env_set(char **, char *);

user		*load_user(int, struct passwd *, char *),
		*find_user(cron_db *, char *);

entry		*load_entry(FILE *, void (*)(char *),
				 struct passwd *, char **);

FILE		*cron_popen(char *, char *, entry *);


				/* in the C tradition, we only create
				 * variables for the main program, just
				 * extern them elsewhere.
				 */

#ifdef MAIN_PROGRAM
# if !defined(LINT) && !defined(lint)
char	*copyright[] = {
		"@(#) Copyright 1988,1989,1990,1993,1994 by Paul Vixie",
		"@(#) All rights reserved"
	};
# endif

char	*MonthNames[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
		NULL
	};

char	*DowNames[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun",
		NULL
	};

char	*ProgramName,
	*defmailto;
int	LineNumber;
unsigned Jitter,
	RootJitter;
time_t	TargetTime;

# if DEBUGGING
int	DebugFlags;
char	*DebugFlagNames[] = {	/* sync with #defines */
		"ext", "sch", "proc", "pars", "load", "misc", "test", "bit",
		NULL		/* NULL must be last element */
	};
# endif /* DEBUGGING */
#else /*MAIN_PROGRAM*/
extern	char	*copyright[],
		*MonthNames[],
		*DowNames[],
		*ProgramName,
		*defmailto;
extern	int	LineNumber;
extern unsigned	Jitter,
		RootJitter;
extern	time_t	TargetTime;
extern struct pidfh *pfh;
# if DEBUGGING
extern	int	DebugFlags;
extern	char	*DebugFlagNames[];
# endif /* DEBUGGING */
#endif /*MAIN_PROGRAM*/
