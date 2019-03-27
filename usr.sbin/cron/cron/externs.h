/*	$FreeBSD$	*/

/* Copyright 1993,1994 by Paul Vixie
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

#if defined(POSIX) || defined(ATT)
# include <stdlib.h>
# include <unistd.h>
# include <string.h>
# include <dirent.h>
# define DIR_T	struct dirent
# define WAIT_T	int
# define WAIT_IS_INT 1
extern char *tzname[2];
# define TZONE(tm) tzname[(tm).tm_isdst]
#endif

#if defined(UNIXPC)
# undef WAIT_T
# undef WAIT_IS_INT
# define WAIT_T	union wait
#endif

#if defined(POSIX)
# define SIG_T	sig_t
# define TIME_T	time_t
# define PID_T pid_t
#endif

#if defined(ATT)
# define SIG_T	void
# define TIME_T	long
# define PID_T int
#endif

#if !defined(POSIX) && !defined(ATT)
/* classic BSD */
extern	time_t		time();
extern	unsigned	sleep();
extern	struct tm	*localtime();
extern	struct passwd	*getpwnam();
extern	int		errno;
extern	void		perror(), exit(), free();
extern	char		*getenv(), *strcpy(), *strchr(), *strtok();
extern	void		*malloc(), *realloc();
# define SIG_T	void
# define TIME_T	long
# define PID_T int
# define WAIT_T	union wait
# define DIR_T	struct direct
# include <sys/dir.h>
# define TZONE(tm) (tm).tm_zone
#endif

/* getopt() isn't part of POSIX.  some systems define it in <stdlib.h> anyway.
 * of those that do, some complain that our definition is different and some
 * do not.  to add to the misery and confusion, some systems define getopt()
 * in ways that we cannot predict or comprehend, yet do not define the adjunct
 * external variables needed for the interface.
 */
#if (!defined(BSD) || (BSD < 198911)) && !defined(ATT) && !defined(UNICOS)
int	getopt(int, char * const *, const char *);
#endif

#if (!defined(BSD) || (BSD < 199103))
extern	char *optarg;
extern	int optind, opterr, optopt;
#endif

#if WAIT_IS_INT
# ifndef WEXITSTATUS
#  define WEXITSTATUS(x) (((x) >> 8) & 0xff)
# endif
# ifndef WTERMSIG
#  define WTERMSIG(x)	((x) & 0x7f)
# endif
# ifndef WCOREDUMP
#  define WCOREDUMP(x)	((x) & 0x80)
# endif
#else /*WAIT_IS_INT*/
# ifndef WEXITSTATUS
#  define WEXITSTATUS(x) ((x).w_retcode)
# endif
# ifndef WTERMSIG
#  define WTERMSIG(x)	((x).w_termsig)
# endif
# ifndef WCOREDUMP
#  define WCOREDUMP(x)	((x).w_coredump)
# endif
#endif /*WAIT_IS_INT*/

#ifndef WIFSIGNALED
#define WIFSIGNALED(x)	(WTERMSIG(x) != 0)
#endif
#ifndef WIFEXITED
#define WIFEXITED(x)	(WTERMSIG(x) == 0)
#endif

#ifdef NEED_STRCASECMP
extern	int		strcasecmp(char *, char *);
#endif

#ifdef NEED_STRDUP
extern	char		*strdup(char *);
#endif

#ifdef NEED_STRERROR
extern	char		*strerror(int);
#endif

#ifdef NEED_FLOCK
extern	int		flock(int, int);
# define LOCK_SH 1
# define LOCK_EX 2
# define LOCK_NB 4
# define LOCK_UN 8
#endif

#ifdef NEED_SETSID
extern	int		setsid(void);
#endif

#ifdef NEED_GETDTABLESIZE
extern	int		getdtablesize(void);
#endif

#ifdef NEED_SETENV
extern	int		setenv(char *, char *, int);
#endif

#ifdef NEED_VFORK
extern	PID_T		vfork(void);
#endif
