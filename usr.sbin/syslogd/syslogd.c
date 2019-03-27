/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1988, 1993, 1994
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
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Prodrive Technologies, https://prodrive-technologies.com/
 * Author: Ed Schouten <ed@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1988, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)syslogd.c	8.3 (Berkeley) 4/4/94";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 *  syslogd -- log system messages
 *
 * This program implements a system log. It takes a series of lines.
 * Each line may have a priority, signified as "<n>" as
 * the first characters of the line.  If this is
 * not present, a default priority is used.
 *
 * To kill syslogd, send a signal 15 (terminate).  A signal 1 (hup) will
 * cause it to reread its configuration file.
 *
 * Defined Constants:
 *
 * MAXLINE -- the maximum line length that can be handled.
 * DEFUPRI -- the default priority for user messages
 * DEFSPRI -- the default priority for kernel messages
 *
 * Author: Eric Allman
 * extensive changes by Ralph Campbell
 * more extensive changes by Eric Allman (again)
 * Extension to log by program name as well as facility and priority
 *   by Peter da Silva.
 * -u and -v by Harlan Stenn.
 * Priority comparison code by Harlan Stenn.
 */

/* Maximum number of characters in time of last occurrence */
#define	MAXLINE		2048		/* maximum line length */
#define	MAXSVLINE	MAXLINE		/* maximum saved line length */
#define	DEFUPRI		(LOG_USER|LOG_NOTICE)
#define	DEFSPRI		(LOG_KERN|LOG_CRIT)
#define	TIMERINTVL	30		/* interval for checking flush, mark */
#define	TTYMSGTIME	1		/* timeout passed to ttymsg */
#define	RCVBUF_MINSIZE	(80 * 1024)	/* minimum size of dgram rcv buffer */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>

#if defined(INET) || defined(INET6)
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <libutil.h>
#include <limits.h>
#include <netdb.h>
#include <paths.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <utmpx.h>

#include "pathnames.h"
#include "ttymsg.h"

#define SYSLOG_NAMES
#include <sys/syslog.h>

static const char *ConfFile = _PATH_LOGCONF;
static const char *PidFile = _PATH_LOGPID;
static const char ctty[] = _PATH_CONSOLE;
static const char include_str[] = "include";
static const char include_ext[] = ".conf";

#define	dprintf		if (Debug) printf

#define	MAXUNAMES	20	/* maximum number of user names */

#define	sstosa(ss)	((struct sockaddr *)(ss))
#ifdef INET
#define	sstosin(ss)	((struct sockaddr_in *)(void *)(ss))
#define	satosin(sa)	((struct sockaddr_in *)(void *)(sa))
#endif
#ifdef INET6
#define	sstosin6(ss)	((struct sockaddr_in6 *)(void *)(ss))
#define	satosin6(sa)	((struct sockaddr_in6 *)(void *)(sa))
#define	s6_addr32	__u6_addr.__u6_addr32
#define	IN6_ARE_MASKED_ADDR_EQUAL(d, a, m)	(	\
	(((d)->s6_addr32[0] ^ (a)->s6_addr32[0]) & (m)->s6_addr32[0]) == 0 && \
	(((d)->s6_addr32[1] ^ (a)->s6_addr32[1]) & (m)->s6_addr32[1]) == 0 && \
	(((d)->s6_addr32[2] ^ (a)->s6_addr32[2]) & (m)->s6_addr32[2]) == 0 && \
	(((d)->s6_addr32[3] ^ (a)->s6_addr32[3]) & (m)->s6_addr32[3]) == 0 )
#endif
/*
 * List of peers and sockets for binding.
 */
struct peer {
	const char	*pe_name;
	const char	*pe_serv;
	mode_t		pe_mode;
	STAILQ_ENTRY(peer)	next;
};
static STAILQ_HEAD(, peer) pqueue = STAILQ_HEAD_INITIALIZER(pqueue);

struct socklist {
	struct addrinfo		sl_ai;
#define	sl_sa		sl_ai.ai_addr
#define	sl_salen	sl_ai.ai_addrlen
#define	sl_family	sl_ai.ai_family
	int			sl_socket;
	struct peer		*sl_peer;
	int			(*sl_recv)(struct socklist *);
	STAILQ_ENTRY(socklist)	next;
};
static STAILQ_HEAD(, socklist) shead = STAILQ_HEAD_INITIALIZER(shead);

/*
 * Flags to logmsg().
 */

#define	IGN_CONS	0x001	/* don't print on console */
#define	SYNC_FILE	0x002	/* do fsync on file after printing */
#define	MARK		0x008	/* this message is a mark */

/* Timestamps of log entries. */
struct logtime {
	struct tm	tm;
	suseconds_t	usec;
};

/* Traditional syslog timestamp format. */
#define	RFC3164_DATELEN	15
#define	RFC3164_DATEFMT	"%b %e %H:%M:%S"

/*
 * This structure represents the files that will have log
 * copies printed.
 * We require f_file to be valid if f_type is F_FILE, F_CONSOLE, F_TTY
 * or if f_type is F_PIPE and f_pid > 0.
 */

struct filed {
	STAILQ_ENTRY(filed)	next;	/* next in linked list */
	short	f_type;			/* entry type, see below */
	short	f_file;			/* file descriptor */
	time_t	f_time;			/* time this was last written */
	char	*f_host;		/* host from which to recd. */
	u_char	f_pmask[LOG_NFACILITIES+1];	/* priority mask */
	u_char	f_pcmp[LOG_NFACILITIES+1];	/* compare priority */
#define PRI_LT	0x1
#define PRI_EQ	0x2
#define PRI_GT	0x4
	char	*f_program;		/* program this applies to */
	union {
		char	f_uname[MAXUNAMES][MAXLOGNAME];
		struct {
			char	f_hname[MAXHOSTNAMELEN];
			struct addrinfo *f_addr;

		} f_forw;		/* forwarding address */
		char	f_fname[MAXPATHLEN];
		struct {
			char	f_pname[MAXPATHLEN];
			pid_t	f_pid;
		} f_pipe;
	} f_un;
#define	fu_uname	f_un.f_uname
#define	fu_forw_hname	f_un.f_forw.f_hname
#define	fu_forw_addr	f_un.f_forw.f_addr
#define	fu_fname	f_un.f_fname
#define	fu_pipe_pname	f_un.f_pipe.f_pname
#define	fu_pipe_pid	f_un.f_pipe.f_pid
	char	f_prevline[MAXSVLINE];		/* last message logged */
	struct logtime f_lasttime;		/* time of last occurrence */
	int	f_prevpri;			/* pri of f_prevline */
	size_t	f_prevlen;			/* length of f_prevline */
	int	f_prevcount;			/* repetition cnt of prevline */
	u_int	f_repeatcount;			/* number of "repeated" msgs */
	int	f_flags;			/* file-specific flags */
#define	FFLAG_SYNC 0x01
#define	FFLAG_NEEDSYNC	0x02
};

/*
 * Queue of about-to-be dead processes we should watch out for.
 */
struct deadq_entry {
	pid_t				dq_pid;
	int				dq_timeout;
	TAILQ_ENTRY(deadq_entry)	dq_entries;
};
static TAILQ_HEAD(, deadq_entry) deadq_head =
    TAILQ_HEAD_INITIALIZER(deadq_head);

/*
 * The timeout to apply to processes waiting on the dead queue.  Unit
 * of measure is `mark intervals', i.e. 20 minutes by default.
 * Processes on the dead queue will be terminated after that time.
 */

#define	 DQ_TIMO_INIT	2

/*
 * Struct to hold records of network addresses that are allowed to log
 * to us.
 */
struct allowedpeer {
	int isnumeric;
	u_short port;
	union {
		struct {
			struct sockaddr_storage addr;
			struct sockaddr_storage mask;
		} numeric;
		char *name;
	} u;
#define a_addr u.numeric.addr
#define a_mask u.numeric.mask
#define a_name u.name
	STAILQ_ENTRY(allowedpeer)	next;
};
static STAILQ_HEAD(, allowedpeer) aphead = STAILQ_HEAD_INITIALIZER(aphead);


/*
 * Intervals at which we flush out "message repeated" messages,
 * in seconds after previous message is logged.  After each flush,
 * we move to the next interval until we reach the largest.
 */
static int repeatinterval[] = { 30, 120, 600 };	/* # of secs before flush */
#define	MAXREPEAT	(nitems(repeatinterval) - 1)
#define	REPEATTIME(f)	((f)->f_time + repeatinterval[(f)->f_repeatcount])
#define	BACKOFF(f)	do {						\
				if (++(f)->f_repeatcount > MAXREPEAT)	\
					(f)->f_repeatcount = MAXREPEAT;	\
			} while (0)

/* values for f_type */
#define F_UNUSED	0		/* unused entry */
#define F_FILE		1		/* regular file */
#define F_TTY		2		/* terminal */
#define F_CONSOLE	3		/* console terminal */
#define F_FORW		4		/* remote machine */
#define F_USERS		5		/* list of users */
#define F_WALL		6		/* everyone logged on */
#define F_PIPE		7		/* pipe to program */

static const char *TypeNames[] = {
	"UNUSED",	"FILE",		"TTY",		"CONSOLE",
	"FORW",		"USERS",	"WALL",		"PIPE"
};

static STAILQ_HEAD(, filed) fhead =
    STAILQ_HEAD_INITIALIZER(fhead);	/* Log files that we write to */
static struct filed consfile;	/* Console */

static int	Debug;		/* debug flag */
static int	Foreground = 0;	/* Run in foreground, instead of daemonizing */
static int	resolve = 1;	/* resolve hostname */
static char	LocalHostName[MAXHOSTNAMELEN];	/* our hostname */
static const char *LocalDomain;	/* our local domain name */
static int	Initialized;	/* set when we have initialized ourselves */
static int	MarkInterval = 20 * 60;	/* interval between marks in seconds */
static int	MarkSeq;	/* mark sequence number */
static int	NoBind;		/* don't bind() as suggested by RFC 3164 */
static int	SecureMode;	/* when true, receive only unix domain socks */
#ifdef INET6
static int	family = PF_UNSPEC; /* protocol family (IPv4, IPv6 or both) */
#else
static int	family = PF_INET; /* protocol family (IPv4 only) */
#endif
static int	mask_C1 = 1;	/* mask characters from 0x80 - 0x9F */
static int	send_to_all;	/* send message to all IPv4/IPv6 addresses */
static int	use_bootfile;	/* log entire bootfile for every kern msg */
static int	no_compress;	/* don't compress messages (1=pipes, 2=all) */
static int	logflags = O_WRONLY|O_APPEND; /* flags used to open log files */

static char	bootfile[MAXLINE+1]; /* booted kernel file */

static int	RemoteAddDate;	/* Always set the date on remote messages */
static int	RemoteHostname;	/* Log remote hostname from the message */

static int	UniquePriority;	/* Only log specified priority? */
static int	LogFacPri;	/* Put facility and priority in log message: */
				/* 0=no, 1=numeric, 2=names */
static int	KeepKernFac;	/* Keep remotely logged kernel facility */
static int	needdofsync = 0; /* Are any file(s) waiting to be fsynced? */
static struct pidfh *pfh;
static int	sigpipe[2];	/* Pipe to catch a signal during select(). */
static bool	RFC3164OutputFormat = true; /* Use legacy format by default. */

static volatile sig_atomic_t MarkSet, WantDie, WantInitialize, WantReapchild;

struct iovlist;

static int	allowaddr(char *);
static int	addfile(struct filed *);
static int	addpeer(struct peer *);
static int	addsock(struct addrinfo *, struct socklist *);
static struct filed *cfline(const char *, const char *, const char *);
static const char *cvthname(struct sockaddr *);
static void	deadq_enter(pid_t, const char *);
static int	deadq_remove(struct deadq_entry *);
static int	deadq_removebypid(pid_t);
static int	decode(const char *, const CODE *);
static void	die(int) __dead2;
static void	dodie(int);
static void	dofsync(void);
static void	domark(int);
static void	fprintlog_first(struct filed *, const char *, const char *,
    const char *, const char *, const char *, const char *, int);
static void	fprintlog_write(struct filed *, struct iovlist *, int);
static void	fprintlog_successive(struct filed *, int);
static void	init(int);
static void	logerror(const char *);
static void	logmsg(int, const struct logtime *, const char *, const char *,
    const char *, const char *, const char *, const char *, int);
static void	log_deadchild(pid_t, int, const char *);
static void	markit(void);
static int	socksetup(struct peer *);
static int	socklist_recv_file(struct socklist *);
static int	socklist_recv_sock(struct socklist *);
static int	socklist_recv_signal(struct socklist *);
static void	sighandler(int);
static int	skip_message(const char *, const char *, int);
static void	parsemsg(const char *, char *);
static void	printsys(char *);
static int	p_open(const char *, pid_t *);
static void	reapchild(int);
static const char *ttymsg_check(struct iovec *, int, char *, int);
static void	usage(void);
static int	validate(struct sockaddr *, const char *);
static void	unmapped(struct sockaddr *);
static void	wallmsg(struct filed *, struct iovec *, const int iovlen);
static int	waitdaemon(int);
static void	timedout(int);
static void	increase_rcvbuf(int);

static void
close_filed(struct filed *f)
{

	if (f == NULL || f->f_file == -1)
		return;

	switch (f->f_type) {
	case F_FORW:
		if (f->fu_forw_addr != NULL) {
			freeaddrinfo(f->fu_forw_addr);
			f->fu_forw_addr = NULL;
		}
		/* FALLTHROUGH */

	case F_FILE:
	case F_TTY:
	case F_CONSOLE:
		f->f_type = F_UNUSED;
		break;
	case F_PIPE:
		f->fu_pipe_pid = 0;
		break;
	}
	(void)close(f->f_file);
	f->f_file = -1;
}

static int
addfile(struct filed *f0)
{
	struct filed *f;

	f = calloc(1, sizeof(*f));
	if (f == NULL)
		err(1, "malloc failed");
	*f = *f0;
	STAILQ_INSERT_TAIL(&fhead, f, next);

	return (0);
}

static int
addpeer(struct peer *pe0)
{
	struct peer *pe;

	pe = calloc(1, sizeof(*pe));
	if (pe == NULL)
		err(1, "malloc failed");
	*pe = *pe0;
	STAILQ_INSERT_TAIL(&pqueue, pe, next);

	return (0);
}

static int
addsock(struct addrinfo *ai, struct socklist *sl0)
{
	struct socklist *sl;

	/* Copy *ai->ai_addr to the tail of struct socklist if any. */
	sl = calloc(1, sizeof(*sl) + ((ai != NULL) ? ai->ai_addrlen : 0));
	if (sl == NULL)
		err(1, "malloc failed");
	*sl = *sl0;
	if (ai != NULL) {
		memcpy(&sl->sl_ai, ai, sizeof(*ai));
		if (ai->ai_addrlen > 0) {
			memcpy((sl + 1), ai->ai_addr, ai->ai_addrlen);
			sl->sl_sa = (struct sockaddr *)(sl + 1);
		} else
			sl->sl_sa = NULL;
	}
	STAILQ_INSERT_TAIL(&shead, sl, next);

	return (0);
}

int
main(int argc, char *argv[])
{
	int ch, i, s, fdsrmax = 0, bflag = 0, pflag = 0, Sflag = 0;
	fd_set *fdsr = NULL;
	struct timeval tv, *tvp;
	struct peer *pe;
	struct socklist *sl;
	pid_t ppid = 1, spid;
	char *p;

	if (madvise(NULL, 0, MADV_PROTECT) != 0)
		dprintf("madvise() failed: %s\n", strerror(errno));

	while ((ch = getopt(argc, argv, "468Aa:b:cCdf:FHkl:m:nNoO:p:P:sS:Tuv"))
	    != -1)
		switch (ch) {
#ifdef INET
		case '4':
			family = PF_INET;
			break;
#endif
#ifdef INET6
		case '6':
			family = PF_INET6;
			break;
#endif
		case '8':
			mask_C1 = 0;
			break;
		case 'A':
			send_to_all++;
			break;
		case 'a':		/* allow specific network addresses only */
			if (allowaddr(optarg) == -1)
				usage();
			break;
		case 'b':
			bflag = 1;
			p = strchr(optarg, ']');
			if (p != NULL)
				p = strchr(p + 1, ':');
			else {
				p = strchr(optarg, ':');
				if (p != NULL && strchr(p + 1, ':') != NULL)
					p = NULL; /* backward compatibility */
			}
			if (p == NULL) {
				/* A hostname or filename only. */
				addpeer(&(struct peer){
					.pe_name = optarg,
					.pe_serv = "syslog"
				});
			} else {
				/* The case of "name:service". */
				*p++ = '\0';
				addpeer(&(struct peer){
					.pe_serv = p,
					.pe_name = (strlen(optarg) == 0) ?
					    NULL : optarg,
				});
			}
			break;
		case 'c':
			no_compress++;
			break;
		case 'C':
			logflags |= O_CREAT;
			break;
		case 'd':		/* debug */
			Debug++;
			break;
		case 'f':		/* configuration file */
			ConfFile = optarg;
			break;
		case 'F':		/* run in foreground instead of daemon */
			Foreground++;
			break;
		case 'H':
			RemoteHostname = 1;
			break;
		case 'k':		/* keep remote kern fac */
			KeepKernFac = 1;
			break;
		case 'l':
		case 'p':
		case 'S':
		    {
			long	perml;
			mode_t	mode;
			char	*name, *ep;

			if (ch == 'l')
				mode = DEFFILEMODE;
			else if (ch == 'p') {
				mode = DEFFILEMODE;
				pflag = 1;
			} else {
				mode = S_IRUSR | S_IWUSR;
				Sflag = 1;
			}
			if (optarg[0] == '/')
				name = optarg;
			else if ((name = strchr(optarg, ':')) != NULL) {
				*name++ = '\0';
				if (name[0] != '/')
					errx(1, "socket name must be absolute "
					    "path");
				if (isdigit(*optarg)) {
					perml = strtol(optarg, &ep, 8);
				    if (*ep || perml < 0 ||
					perml & ~(S_IRWXU|S_IRWXG|S_IRWXO))
					    errx(1, "invalid mode %s, exiting",
						optarg);
				    mode = (mode_t )perml;
				} else
					errx(1, "invalid mode %s, exiting",
					    optarg);
			} else
				errx(1, "invalid filename %s, exiting",
				    optarg);
			addpeer(&(struct peer){
				.pe_name = name,
				.pe_mode = mode
			});
			break;
		   }
		case 'm':		/* mark interval */
			MarkInterval = atoi(optarg) * 60;
			break;
		case 'N':
			NoBind = 1;
			SecureMode = 1;
			break;
		case 'n':
			resolve = 0;
			break;
		case 'O':
			if (strcmp(optarg, "bsd") == 0 ||
			    strcmp(optarg, "rfc3164") == 0)
				RFC3164OutputFormat = true;
			else if (strcmp(optarg, "syslog") == 0 ||
			    strcmp(optarg, "rfc5424") == 0)
				RFC3164OutputFormat = false;
			else
				usage();
			break;
		case 'o':
			use_bootfile = 1;
			break;
		case 'P':		/* path for alt. PID */
			PidFile = optarg;
			break;
		case 's':		/* no network mode */
			SecureMode++;
			break;
		case 'T':
			RemoteAddDate = 1;
			break;
		case 'u':		/* only log specified priority */
			UniquePriority++;
			break;
		case 'v':		/* log facility and priority */
		  	LogFacPri++;
			break;
		default:
			usage();
		}
	if ((argc -= optind) != 0)
		usage();

	/* Pipe to catch a signal during select(). */
	s = pipe2(sigpipe, O_CLOEXEC);
	if (s < 0) {
		err(1, "cannot open a pipe for signals");
	} else {
		addsock(NULL, &(struct socklist){
		    .sl_socket = sigpipe[0],
		    .sl_recv = socklist_recv_signal
		});
	}

	/* Listen by default: /dev/klog. */
	s = open(_PATH_KLOG, O_RDONLY | O_NONBLOCK | O_CLOEXEC, 0);
	if (s < 0) {
		dprintf("can't open %s (%d)\n", _PATH_KLOG, errno);
	} else {
		addsock(NULL, &(struct socklist){
			.sl_socket = s,
			.sl_recv = socklist_recv_file,
		});
	}
	/* Listen by default: *:514 if no -b flag. */
	if (bflag == 0)
		addpeer(&(struct peer){
			.pe_serv = "syslog"
		});
	/* Listen by default: /var/run/log if no -p flag. */
	if (pflag == 0)
		addpeer(&(struct peer){
			.pe_name = _PATH_LOG,
			.pe_mode = DEFFILEMODE,
		});
	/* Listen by default: /var/run/logpriv if no -S flag. */
	if (Sflag == 0)
		addpeer(&(struct peer){
			.pe_name = _PATH_LOG_PRIV,
			.pe_mode = S_IRUSR | S_IWUSR,
		});
	STAILQ_FOREACH(pe, &pqueue, next)
		socksetup(pe);

	pfh = pidfile_open(PidFile, 0600, &spid);
	if (pfh == NULL) {
		if (errno == EEXIST)
			errx(1, "syslogd already running, pid: %d", spid);
		warn("cannot open pid file");
	}

	if ((!Foreground) && (!Debug)) {
		ppid = waitdaemon(30);
		if (ppid < 0) {
			warn("could not become daemon");
			pidfile_remove(pfh);
			exit(1);
		}
	} else if (Debug)
		setlinebuf(stdout);

	consfile.f_type = F_CONSOLE;
	(void)strlcpy(consfile.fu_fname, ctty + sizeof _PATH_DEV - 1,
	    sizeof(consfile.fu_fname));
	(void)strlcpy(bootfile, getbootfile(), sizeof(bootfile));
	(void)signal(SIGTERM, dodie);
	(void)signal(SIGINT, Debug ? dodie : SIG_IGN);
	(void)signal(SIGQUIT, Debug ? dodie : SIG_IGN);
	(void)signal(SIGHUP, sighandler);
	(void)signal(SIGCHLD, sighandler);
	(void)signal(SIGALRM, domark);
	(void)signal(SIGPIPE, SIG_IGN);	/* We'll catch EPIPE instead. */
	(void)alarm(TIMERINTVL);

	/* tuck my process id away */
	pidfile_write(pfh);

	dprintf("off & running....\n");

	tvp = &tv;
	tv.tv_sec = tv.tv_usec = 0;

	STAILQ_FOREACH(sl, &shead, next) {
		if (sl->sl_socket > fdsrmax)
			fdsrmax = sl->sl_socket;
	}
	fdsr = (fd_set *)calloc(howmany(fdsrmax+1, NFDBITS),
	    sizeof(*fdsr));
	if (fdsr == NULL)
		errx(1, "calloc fd_set");

	for (;;) {
		if (Initialized == 0)
			init(0);
		else if (WantInitialize)
			init(WantInitialize);
		if (WantReapchild)
			reapchild(WantReapchild);
		if (MarkSet)
			markit();
		if (WantDie) {
			free(fdsr);
			die(WantDie);
		}

		bzero(fdsr, howmany(fdsrmax+1, NFDBITS) *
		    sizeof(*fdsr));

		STAILQ_FOREACH(sl, &shead, next) {
			if (sl->sl_socket != -1 && sl->sl_recv != NULL)
				FD_SET(sl->sl_socket, fdsr);
		}
		i = select(fdsrmax + 1, fdsr, NULL, NULL,
		    needdofsync ? &tv : tvp);
		switch (i) {
		case 0:
			dofsync();
			needdofsync = 0;
			if (tvp) {
				tvp = NULL;
				if (ppid != 1)
					kill(ppid, SIGALRM);
			}
			continue;
		case -1:
			if (errno != EINTR)
				logerror("select");
			continue;
		}
		STAILQ_FOREACH(sl, &shead, next) {
			if (FD_ISSET(sl->sl_socket, fdsr))
				(*sl->sl_recv)(sl);
		}
	}
	free(fdsr);
}

static int
socklist_recv_signal(struct socklist *sl __unused)
{
	ssize_t len;
	int i, nsig, signo;

	if (ioctl(sigpipe[0], FIONREAD, &i) != 0) {
		logerror("ioctl(FIONREAD)");
		err(1, "signal pipe read failed");
	}
	nsig = i / sizeof(signo);
	dprintf("# of received signals = %d\n", nsig);
	for (i = 0; i < nsig; i++) {
		len = read(sigpipe[0], &signo, sizeof(signo));
		if (len != sizeof(signo)) {
			logerror("signal pipe read failed");
			err(1, "signal pipe read failed");
		}
		dprintf("Received signal: %d from fd=%d\n", signo,
		    sigpipe[0]);
		switch (signo) {
		case SIGHUP:
			WantInitialize = 1;
			break;
		case SIGCHLD:
			WantReapchild = 1;
			break;
		}
	}
	return (0);
}

static int
socklist_recv_sock(struct socklist *sl)
{
	struct sockaddr_storage ss;
	struct sockaddr *sa = (struct sockaddr *)&ss;
	socklen_t sslen;
	const char *hname;
	char line[MAXLINE + 1];
	int len;

	sslen = sizeof(ss);
	len = recvfrom(sl->sl_socket, line, sizeof(line) - 1, 0, sa, &sslen);
	dprintf("received sa_len = %d\n", sslen);
	if (len == 0)
		return (-1);
	if (len < 0) {
		if (errno != EINTR)
			logerror("recvfrom");
		return (-1);
	}
	/* Received valid data. */
	line[len] = '\0';
	if (sl->sl_sa != NULL && sl->sl_family == AF_LOCAL)
		hname = LocalHostName;
	else {
		hname = cvthname(sa);
		unmapped(sa);
		if (validate(sa, hname) == 0) {
			dprintf("Message from %s was ignored.", hname);
			return (-1);
		}
	}
	parsemsg(hname, line);

	return (0);
}

static void
unmapped(struct sockaddr *sa)
{
#if defined(INET) && defined(INET6)
	struct sockaddr_in6 *sin6;
	struct sockaddr_in sin;

	if (sa == NULL ||
	    sa->sa_family != AF_INET6 ||
	    sa->sa_len != sizeof(*sin6))
		return;
	sin6 = satosin6(sa);
	if (!IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
		return;
	sin = (struct sockaddr_in){
		.sin_family = AF_INET,
		.sin_len = sizeof(sin),
		.sin_port = sin6->sin6_port
	};
	memcpy(&sin.sin_addr, &sin6->sin6_addr.s6_addr[12],
	    sizeof(sin.sin_addr));
	memcpy(sa, &sin, sizeof(sin));
#else
	if (sa == NULL)
		return;
#endif
}

static void
usage(void)
{

	fprintf(stderr,
		"usage: syslogd [-468ACcdFHknosTuv] [-a allowed_peer]\n"
		"               [-b bind_address] [-f config_file]\n"
		"               [-l [mode:]path] [-m mark_interval]\n"
		"               [-O format] [-P pid_file] [-p log_socket]\n"
		"               [-S logpriv_socket]\n");
	exit(1);
}

/*
 * Removes characters from log messages that are unsafe to display.
 * TODO: Permit UTF-8 strings that include a BOM per RFC 5424?
 */
static void
parsemsg_remove_unsafe_characters(const char *in, char *out, size_t outlen)
{
	char *q;
	int c;

	q = out;
	while ((c = (unsigned char)*in++) != '\0' && q < out + outlen - 4) {
		if (mask_C1 && (c & 0x80) && c < 0xA0) {
			c &= 0x7F;
			*q++ = 'M';
			*q++ = '-';
		}
		if (isascii(c) && iscntrl(c)) {
			if (c == '\n') {
				*q++ = ' ';
			} else if (c == '\t') {
				*q++ = '\t';
			} else {
				*q++ = '^';
				*q++ = c ^ 0100;
			}
		} else {
			*q++ = c;
		}
	}
	*q = '\0';
}

/*
 * Parses a syslog message according to RFC 5424, assuming that PRI and
 * VERSION (i.e., "<%d>1 ") have already been parsed by parsemsg(). The
 * parsed result is passed to logmsg().
 */
static void
parsemsg_rfc5424(const char *from, int pri, char *msg)
{
	const struct logtime *timestamp;
	struct logtime timestamp_remote;
	const char *omsg, *hostname, *app_name, *procid, *msgid,
	    *structured_data;
	char line[MAXLINE + 1];

#define	FAIL_IF(field, expr) do {					\
	if (expr) {							\
		dprintf("Failed to parse " field " from %s: %s\n",	\
		    from, omsg);					\
		return;							\
	}								\
} while (0)
#define	PARSE_CHAR(field, sep) do {					\
	FAIL_IF(field, *msg != sep);					\
	++msg;								\
} while (0)
#define	IF_NOT_NILVALUE(var)						\
	if (msg[0] == '-' && msg[1] == ' ') {				\
		msg += 2;						\
		var = NULL;						\
	} else if (msg[0] == '-' && msg[1] == '\0') {			\
		++msg;							\
		var = NULL;						\
	} else

	omsg = msg;
	IF_NOT_NILVALUE(timestamp) {
		/* Parse RFC 3339-like timestamp. */
#define	PARSE_NUMBER(dest, length, min, max) do {			\
	int i, v;							\
									\
	v = 0;								\
	for (i = 0; i < length; ++i) {					\
		FAIL_IF("TIMESTAMP", *msg < '0' || *msg > '9');		\
		v = v * 10 + *msg++ - '0';				\
	}								\
	FAIL_IF("TIMESTAMP", v < min || v > max);			\
	dest = v;							\
} while (0)
		/* Date and time. */
		memset(&timestamp_remote, 0, sizeof(timestamp_remote));
		PARSE_NUMBER(timestamp_remote.tm.tm_year, 4, 0, 9999);
		timestamp_remote.tm.tm_year -= 1900;
		PARSE_CHAR("TIMESTAMP", '-');
		PARSE_NUMBER(timestamp_remote.tm.tm_mon, 2, 1, 12);
		--timestamp_remote.tm.tm_mon;
		PARSE_CHAR("TIMESTAMP", '-');
		PARSE_NUMBER(timestamp_remote.tm.tm_mday, 2, 1, 31);
		PARSE_CHAR("TIMESTAMP", 'T');
		PARSE_NUMBER(timestamp_remote.tm.tm_hour, 2, 0, 23);
		PARSE_CHAR("TIMESTAMP", ':');
		PARSE_NUMBER(timestamp_remote.tm.tm_min, 2, 0, 59);
		PARSE_CHAR("TIMESTAMP", ':');
		PARSE_NUMBER(timestamp_remote.tm.tm_sec, 2, 0, 59);
		/* Perform normalization. */
		timegm(&timestamp_remote.tm);
		/* Optional: fractional seconds. */
		if (msg[0] == '.' && msg[1] >= '0' && msg[1] <= '9') {
			int i;

			++msg;
			for (i = 100000; i != 0; i /= 10) {
				if (*msg < '0' || *msg > '9')
					break;
				timestamp_remote.usec += (*msg++ - '0') * i;
			}
		}
		/* Timezone. */
		if (*msg == 'Z') {
			/* UTC. */
			++msg;
		} else {
			int sign, tz_hour, tz_min;

			/* Local time zone offset. */
			FAIL_IF("TIMESTAMP", *msg != '-' && *msg != '+');
			sign = *msg++ == '-' ? -1 : 1;
			PARSE_NUMBER(tz_hour, 2, 0, 23);
			PARSE_CHAR("TIMESTAMP", ':');
			PARSE_NUMBER(tz_min, 2, 0, 59);
			timestamp_remote.tm.tm_gmtoff =
			    sign * (tz_hour * 3600 + tz_min * 60);
		}
#undef PARSE_NUMBER
		PARSE_CHAR("TIMESTAMP", ' ');
		timestamp = RemoteAddDate ? NULL : &timestamp_remote;
	}

	/* String fields part of the HEADER. */
#define	PARSE_STRING(field, var)					\
	IF_NOT_NILVALUE(var) {						\
		var = msg;						\
		while (*msg >= '!' && *msg <= '~')			\
			++msg;						\
		FAIL_IF(field, var == msg);				\
		PARSE_CHAR(field, ' ');					\
		msg[-1] = '\0';						\
	}
	PARSE_STRING("HOSTNAME", hostname);
	if (hostname == NULL || !RemoteHostname)
		hostname = from;
	PARSE_STRING("APP-NAME", app_name);
	PARSE_STRING("PROCID", procid);
	PARSE_STRING("MSGID", msgid);
#undef PARSE_STRING

	/* Structured data. */
#define	PARSE_SD_NAME() do {						\
	const char *start;						\
									\
	start = msg;							\
	while (*msg >= '!' && *msg <= '~' && *msg != '=' &&		\
	    *msg != ']' && *msg != '"')					\
		++msg;							\
	FAIL_IF("STRUCTURED-NAME", start == msg);			\
} while (0)
	IF_NOT_NILVALUE(structured_data) {
		/* SD-ELEMENT. */
		while (*msg == '[') {
			++msg;
			/* SD-ID. */
			PARSE_SD_NAME();
			/* SD-PARAM. */
			while (*msg == ' ') {
				++msg;
				/* PARAM-NAME. */
				PARSE_SD_NAME();
				PARSE_CHAR("STRUCTURED-NAME", '=');
				PARSE_CHAR("STRUCTURED-NAME", '"');
				while (*msg != '"') {
					FAIL_IF("STRUCTURED-NAME",
					    *msg == '\0');
					if (*msg++ == '\\') {
						FAIL_IF("STRUCTURED-NAME",
						    *msg == '\0');
						++msg;
					}
				}
				++msg;
			}
			PARSE_CHAR("STRUCTURED-NAME", ']');
		}
		PARSE_CHAR("STRUCTURED-NAME", ' ');
		msg[-1] = '\0';
	}
#undef PARSE_SD_NAME

#undef FAIL_IF
#undef PARSE_CHAR
#undef IF_NOT_NILVALUE

	parsemsg_remove_unsafe_characters(msg, line, sizeof(line));
	logmsg(pri, timestamp, hostname, app_name, procid, msgid,
	    structured_data, line, 0);
}

/*
 * Trims the application name ("TAG" in RFC 3164 terminology) and
 * process ID from a message if present.
 */
static void
parsemsg_rfc3164_app_name_procid(char **msg, const char **app_name,
    const char **procid) {
	char *m, *app_name_begin, *procid_begin;
	size_t app_name_length, procid_length;

	m = *msg;

	/* Application name. */
	app_name_begin = m;
	app_name_length = strspn(m,
	    "abcdefghijklmnopqrstuvwxyz"
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	    "0123456789"
	    "_-/");
	if (app_name_length == 0)
		goto bad;
	m += app_name_length;

	/* Process identifier (optional). */
	if (*m == '[') {
		procid_begin = ++m;
		procid_length = strspn(m, "0123456789");
		if (procid_length == 0)
			goto bad;
		m += procid_length;
		if (*m++ != ']')
			goto bad;
	} else {
		procid_begin = NULL;
		procid_length = 0;
	}

	/* Separator. */
	if (m[0] != ':' || m[1] != ' ')
		goto bad;

	/* Split strings from input. */
	app_name_begin[app_name_length] = '\0';
	if (procid_begin != 0)
		procid_begin[procid_length] = '\0';

	*msg = m + 2;
	*app_name = app_name_begin;
	*procid = procid_begin;
	return;
bad:
	*app_name = NULL;
	*procid = NULL;
}

/*
 * Parses a syslog message according to RFC 3164, assuming that PRI
 * (i.e., "<%d>") has already been parsed by parsemsg(). The parsed
 * result is passed to logmsg().
 */
static void
parsemsg_rfc3164(const char *from, int pri, char *msg)
{
	struct tm tm_parsed;
	const struct logtime *timestamp;
	struct logtime timestamp_remote;
	const char *app_name, *procid;
	size_t i, msglen;
	char line[MAXLINE + 1];

	/*
	 * Parse the TIMESTAMP provided by the remote side. If none is
	 * found, assume this is not an RFC 3164 formatted message,
	 * only containing a TAG and a MSG.
	 */
	timestamp = NULL;
	if (strptime(msg, RFC3164_DATEFMT, &tm_parsed) ==
	    msg + RFC3164_DATELEN && msg[RFC3164_DATELEN] == ' ') {
		msg += RFC3164_DATELEN + 1;
		if (!RemoteAddDate) {
			struct tm tm_now;
			time_t t_now;
			int year;

			/*
			 * As the timestamp does not contain the year
			 * number, daylight saving time information, nor
			 * a time zone, attempt to infer it. Due to
			 * clock skews, the timestamp may even be part
			 * of the next year. Use the last year for which
			 * the timestamp is at most one week in the
			 * future.
			 *
			 * This loop can only run for at most three
			 * iterations before terminating.
			 */
			t_now = time(NULL);
			localtime_r(&t_now, &tm_now);
			for (year = tm_now.tm_year + 1;; --year) {
				assert(year >= tm_now.tm_year - 1);
				timestamp_remote.tm = tm_parsed;
				timestamp_remote.tm.tm_year = year;
				timestamp_remote.tm.tm_isdst = -1;
				timestamp_remote.usec = 0;
				if (mktime(&timestamp_remote.tm) <
				    t_now + 7 * 24 * 60 * 60)
					break;
			}
			timestamp = &timestamp_remote;
		}

		/*
		 * A single space character MUST also follow the HOSTNAME field.
		 */
		msglen = strlen(msg);
		for (i = 0; i < MIN(MAXHOSTNAMELEN, msglen); i++) {
			if (msg[i] == ' ') {
				if (RemoteHostname) {
					msg[i] = '\0';
					from = msg;
				}
				msg += i + 1;
				break;
			}
			/*
			 * Support non RFC compliant messages, without hostname.
			 */
			if (msg[i] == ':')
				break;
		}
		if (i == MIN(MAXHOSTNAMELEN, msglen)) {
			dprintf("Invalid HOSTNAME from %s: %s\n", from, msg);
			return;
		}
	}

	/* Remove the TAG, if present. */
	parsemsg_rfc3164_app_name_procid(&msg, &app_name, &procid);
	parsemsg_remove_unsafe_characters(msg, line, sizeof(line));
	logmsg(pri, timestamp, from, app_name, procid, NULL, NULL, line, 0);
}

/*
 * Takes a raw input line, extracts PRI and determines whether the
 * message is formatted according to RFC 3164 or RFC 5424. Continues
 * parsing of addition fields in the message according to those
 * standards and prints the message on the appropriate log files.
 */
static void
parsemsg(const char *from, char *msg)
{
	char *q;
	long n;
	size_t i;
	int pri;

	/* Parse PRI. */
	if (msg[0] != '<' || !isdigit(msg[1])) {
		dprintf("Invalid PRI from %s\n", from);
		return;
	}
	for (i = 2; i <= 4; i++) {
		if (msg[i] == '>')
			break;
		if (!isdigit(msg[i])) {
			dprintf("Invalid PRI header from %s\n", from);
			return;
		}
	}
	if (msg[i] != '>') {
		dprintf("Invalid PRI header from %s\n", from);
		return;
	}
	errno = 0;
	n = strtol(msg + 1, &q, 10);
	if (errno != 0 || *q != msg[i] || n < 0 || n >= INT_MAX) {
		dprintf("Invalid PRI %ld from %s: %s\n",
		    n, from, strerror(errno));
		return;
	}
	pri = n;
	if (pri &~ (LOG_FACMASK|LOG_PRIMASK))
		pri = DEFUPRI;

	/*
	 * Don't allow users to log kernel messages.
	 * NOTE: since LOG_KERN == 0 this will also match
	 *       messages with no facility specified.
	 */
	if ((pri & LOG_FACMASK) == LOG_KERN && !KeepKernFac)
		pri = LOG_MAKEPRI(LOG_USER, LOG_PRI(pri));

	/* Parse VERSION. */
	msg += i + 1;
	if (msg[0] == '1' && msg[1] == ' ')
		parsemsg_rfc5424(from, pri, msg + 2);
	else
		parsemsg_rfc3164(from, pri, msg);
}

/*
 * Read /dev/klog while data are available, split into lines.
 */
static int
socklist_recv_file(struct socklist *sl)
{
	char *p, *q, line[MAXLINE + 1];
	int len, i;

	len = 0;
	for (;;) {
		i = read(sl->sl_socket, line + len, MAXLINE - 1 - len);
		if (i > 0) {
			line[i + len] = '\0';
		} else {
			if (i < 0 && errno != EINTR && errno != EAGAIN) {
				logerror("klog");
				close(sl->sl_socket);
				sl->sl_socket = -1;
			}
			break;
		}

		for (p = line; (q = strchr(p, '\n')) != NULL; p = q + 1) {
			*q = '\0';
			printsys(p);
		}
		len = strlen(p);
		if (len >= MAXLINE - 1) {
			printsys(p);
			len = 0;
		}
		if (len > 0)
			memmove(line, p, len + 1);
	}
	if (len > 0)
		printsys(line);

	return (len);
}

/*
 * Take a raw input line from /dev/klog, format similar to syslog().
 */
static void
printsys(char *msg)
{
	char *p, *q;
	long n;
	int flags, isprintf, pri;

	flags = SYNC_FILE;	/* fsync after write */
	p = msg;
	pri = DEFSPRI;
	isprintf = 1;
	if (*p == '<') {
		errno = 0;
		n = strtol(p + 1, &q, 10);
		if (*q == '>' && n >= 0 && n < INT_MAX && errno == 0) {
			p = q + 1;
			pri = n;
			isprintf = 0;
		}
	}
	/*
	 * Kernel printf's and LOG_CONSOLE messages have been displayed
	 * on the console already.
	 */
	if (isprintf || (pri & LOG_FACMASK) == LOG_CONSOLE)
		flags |= IGN_CONS;
	if (pri &~ (LOG_FACMASK|LOG_PRIMASK))
		pri = DEFSPRI;
	logmsg(pri, NULL, LocalHostName, "kernel", NULL, NULL, NULL, p, flags);
}

static time_t	now;

/*
 * Match a program or host name against a specification.
 * Return a non-0 value if the message must be ignored
 * based on the specification.
 */
static int
skip_message(const char *name, const char *spec, int checkcase)
{
	const char *s;
	char prev, next;
	int exclude = 0;
	/* Behaviour on explicit match */

	if (spec == NULL)
		return 0;
	switch (*spec) {
	case '-':
		exclude = 1;
		/*FALLTHROUGH*/
	case '+':
		spec++;
		break;
	default:
		break;
	}
	if (checkcase)
		s = strstr (spec, name);
	else
		s = strcasestr (spec, name);

	if (s != NULL) {
		prev = (s == spec ? ',' : *(s - 1));
		next = *(s + strlen (name));

		if (prev == ',' && (next == '\0' || next == ','))
			/* Explicit match: skip iff the spec is an
			   exclusive one. */
			return exclude;
	}

	/* No explicit match for this name: skip the message iff
	   the spec is an inclusive one. */
	return !exclude;
}

/*
 * Logs a message to the appropriate log files, users, etc. based on the
 * priority. Log messages are always formatted according to RFC 3164,
 * even if they were in RFC 5424 format originally, The MSGID and
 * STRUCTURED-DATA fields are thus discarded for the time being.
 */
static void
logmsg(int pri, const struct logtime *timestamp, const char *hostname,
    const char *app_name, const char *procid, const char *msgid,
    const char *structured_data, const char *msg, int flags)
{
	struct timeval tv;
	struct logtime timestamp_now;
	struct filed *f;
	size_t savedlen;
	int fac, prilev;
	char saved[MAXSVLINE];

	dprintf("logmsg: pri %o, flags %x, from %s, msg %s\n",
	    pri, flags, hostname, msg);

	(void)gettimeofday(&tv, NULL);
	now = tv.tv_sec;
	if (timestamp == NULL) {
		localtime_r(&now, &timestamp_now.tm);
		timestamp_now.usec = tv.tv_usec;
		timestamp = &timestamp_now;
	}

	/* extract facility and priority level */
	if (flags & MARK)
		fac = LOG_NFACILITIES;
	else
		fac = LOG_FAC(pri);

	/* Check maximum facility number. */
	if (fac > LOG_NFACILITIES)
		return;

	prilev = LOG_PRI(pri);

	/* log the message to the particular outputs */
	if (!Initialized) {
		f = &consfile;
		/*
		 * Open in non-blocking mode to avoid hangs during open
		 * and close(waiting for the port to drain).
		 */
		f->f_file = open(ctty, O_WRONLY | O_NONBLOCK, 0);

		if (f->f_file >= 0) {
			f->f_lasttime = *timestamp;
			fprintlog_first(f, hostname, app_name, procid, msgid,
			    structured_data, msg, flags);
			close(f->f_file);
			f->f_file = -1;
		}
		return;
	}

	/*
	 * Store all of the fields of the message, except the timestamp,
	 * in a single string. This string is used to detect duplicate
	 * messages.
	 */
	assert(hostname != NULL);
	assert(msg != NULL);
	savedlen = snprintf(saved, sizeof(saved),
	    "%d %s %s %s %s %s %s", pri, hostname,
	    app_name == NULL ? "-" : app_name, procid == NULL ? "-" : procid,
	    msgid == NULL ? "-" : msgid,
	    structured_data == NULL ? "-" : structured_data, msg);

	STAILQ_FOREACH(f, &fhead, next) {
		/* skip messages that are incorrect priority */
		if (!(((f->f_pcmp[fac] & PRI_EQ) && (f->f_pmask[fac] == prilev))
		     ||((f->f_pcmp[fac] & PRI_LT) && (f->f_pmask[fac] < prilev))
		     ||((f->f_pcmp[fac] & PRI_GT) && (f->f_pmask[fac] > prilev))
		     )
		    || f->f_pmask[fac] == INTERNAL_NOPRI)
			continue;

		/* skip messages with the incorrect hostname */
		if (skip_message(hostname, f->f_host, 0))
			continue;

		/* skip messages with the incorrect program name */
		if (skip_message(app_name == NULL ? "" : app_name,
		    f->f_program, 1))
			continue;

		/* skip message to console if it has already been printed */
		if (f->f_type == F_CONSOLE && (flags & IGN_CONS))
			continue;

		/* don't output marks to recently written files */
		if ((flags & MARK) && (now - f->f_time) < MarkInterval / 2)
			continue;

		/*
		 * suppress duplicate lines to this file
		 */
		if (no_compress - (f->f_type != F_PIPE) < 1 &&
		    (flags & MARK) == 0 && savedlen == f->f_prevlen &&
		    strcmp(saved, f->f_prevline) == 0) {
			f->f_lasttime = *timestamp;
			f->f_prevcount++;
			dprintf("msg repeated %d times, %ld sec of %d\n",
			    f->f_prevcount, (long)(now - f->f_time),
			    repeatinterval[f->f_repeatcount]);
			/*
			 * If domark would have logged this by now,
			 * flush it now (so we don't hold isolated messages),
			 * but back off so we'll flush less often
			 * in the future.
			 */
			if (now > REPEATTIME(f)) {
				fprintlog_successive(f, flags);
				BACKOFF(f);
			}
		} else {
			/* new line, save it */
			if (f->f_prevcount)
				fprintlog_successive(f, 0);
			f->f_repeatcount = 0;
			f->f_prevpri = pri;
			f->f_lasttime = *timestamp;
			static_assert(sizeof(f->f_prevline) == sizeof(saved),
			    "Space to store saved line incorrect");
			(void)strcpy(f->f_prevline, saved);
			f->f_prevlen = savedlen;
			fprintlog_first(f, hostname, app_name, procid, msgid,
			    structured_data, msg, flags);
		}
	}
}

static void
dofsync(void)
{
	struct filed *f;

	STAILQ_FOREACH(f, &fhead, next) {
		if ((f->f_type == F_FILE) &&
		    (f->f_flags & FFLAG_NEEDSYNC)) {
			f->f_flags &= ~FFLAG_NEEDSYNC;
			(void)fsync(f->f_file);
		}
	}
}

/*
 * List of iovecs to which entries can be appended.
 * Used for constructing the message to be logged.
 */
struct iovlist {
	struct iovec	iov[TTYMSG_IOV_MAX];
	size_t		iovcnt;
	size_t		totalsize;
};

static void
iovlist_init(struct iovlist *il)
{

	il->iovcnt = 0;
	il->totalsize = 0;
}

static void
iovlist_append(struct iovlist *il, const char *str)
{
	size_t size;

	/* Discard components if we've run out of iovecs. */
	if (il->iovcnt < nitems(il->iov)) {
		size = strlen(str);
		il->iov[il->iovcnt++] = (struct iovec){
			.iov_base	= __DECONST(char *, str),
			.iov_len	= size,
		};
		il->totalsize += size;
	}
}

#if defined(INET) || defined(INET6)
static void
iovlist_truncate(struct iovlist *il, size_t size)
{
	struct iovec *last;
	size_t diff;

	while (il->totalsize > size) {
		diff = il->totalsize - size;
		last = &il->iov[il->iovcnt - 1];
		if (diff >= last->iov_len) {
			/* Remove the last iovec entirely. */
			--il->iovcnt;
			il->totalsize -= last->iov_len;
		} else {
			/* Remove the last iovec partially. */
			last->iov_len -= diff;
			il->totalsize -= diff;
		}
	}
}
#endif

static void
fprintlog_write(struct filed *f, struct iovlist *il, int flags)
{
	struct msghdr msghdr;
	struct addrinfo *r;
	struct socklist *sl;
	const char *msgret;
	ssize_t lsent;

	switch (f->f_type) {
	case F_FORW:
		/* Truncate messages to RFC 5426 recommended size. */
		dprintf(" %s", f->fu_forw_hname);
		switch (f->fu_forw_addr->ai_family) {
#ifdef INET
		case AF_INET:
			dprintf(":%d\n",
			    ntohs(satosin(f->fu_forw_addr->ai_addr)->sin_port));
			iovlist_truncate(il, 480);
			break;
#endif
#ifdef INET6
		case AF_INET6:
			dprintf(":%d\n",
			    ntohs(satosin6(f->fu_forw_addr->ai_addr)->sin6_port));
			iovlist_truncate(il, 1180);
			break;
#endif
		default:
			dprintf("\n");
		}

		lsent = 0;
		for (r = f->fu_forw_addr; r; r = r->ai_next) {
			memset(&msghdr, 0, sizeof(msghdr));
			msghdr.msg_name = r->ai_addr;
			msghdr.msg_namelen = r->ai_addrlen;
			msghdr.msg_iov = il->iov;
			msghdr.msg_iovlen = il->iovcnt;
			STAILQ_FOREACH(sl, &shead, next) {
				if (sl->sl_socket < 0)
					continue;
				if (sl->sl_sa != NULL &&
				    (sl->sl_family == AF_LOCAL ||
				     sl->sl_family == AF_UNSPEC))
					continue;
				lsent = sendmsg(sl->sl_socket, &msghdr, 0);
				if (lsent == (ssize_t)il->totalsize)
					break;
			}
			if (lsent == (ssize_t)il->totalsize && !send_to_all)
				break;
		}
		dprintf("lsent/totalsize: %zd/%zu\n", lsent, il->totalsize);
		if (lsent != (ssize_t)il->totalsize) {
			int e = errno;
			logerror("sendto");
			errno = e;
			switch (errno) {
			case ENOBUFS:
			case ENETDOWN:
			case ENETUNREACH:
			case EHOSTUNREACH:
			case EHOSTDOWN:
			case EADDRNOTAVAIL:
				break;
			/* case EBADF: */
			/* case EACCES: */
			/* case ENOTSOCK: */
			/* case EFAULT: */
			/* case EMSGSIZE: */
			/* case EAGAIN: */
			/* case ENOBUFS: */
			/* case ECONNREFUSED: */
			default:
				dprintf("removing entry: errno=%d\n", e);
				f->f_type = F_UNUSED;
				break;
			}
		}
		break;

	case F_FILE:
		dprintf(" %s\n", f->fu_fname);
		iovlist_append(il, "\n");
		if (writev(f->f_file, il->iov, il->iovcnt) < 0) {
			/*
			 * If writev(2) fails for potentially transient errors
			 * like the filesystem being full, ignore it.
			 * Otherwise remove this logfile from the list.
			 */
			if (errno != ENOSPC) {
				int e = errno;
				close_filed(f);
				errno = e;
				logerror(f->fu_fname);
			}
		} else if ((flags & SYNC_FILE) && (f->f_flags & FFLAG_SYNC)) {
			f->f_flags |= FFLAG_NEEDSYNC;
			needdofsync = 1;
		}
		break;

	case F_PIPE:
		dprintf(" %s\n", f->fu_pipe_pname);
		iovlist_append(il, "\n");
		if (f->fu_pipe_pid == 0) {
			if ((f->f_file = p_open(f->fu_pipe_pname,
						&f->fu_pipe_pid)) < 0) {
				logerror(f->fu_pipe_pname);
				break;
			}
		}
		if (writev(f->f_file, il->iov, il->iovcnt) < 0) {
			int e = errno;

			deadq_enter(f->fu_pipe_pid, f->fu_pipe_pname);
			close_filed(f);
			errno = e;
			logerror(f->fu_pipe_pname);
		}
		break;

	case F_CONSOLE:
		if (flags & IGN_CONS) {
			dprintf(" (ignored)\n");
			break;
		}
		/* FALLTHROUGH */

	case F_TTY:
		dprintf(" %s%s\n", _PATH_DEV, f->fu_fname);
		iovlist_append(il, "\r\n");
		errno = 0;	/* ttymsg() only sometimes returns an errno */
		if ((msgret = ttymsg(il->iov, il->iovcnt, f->fu_fname, 10))) {
			f->f_type = F_UNUSED;
			logerror(msgret);
		}
		break;

	case F_USERS:
	case F_WALL:
		dprintf("\n");
		iovlist_append(il, "\r\n");
		wallmsg(f, il->iov, il->iovcnt);
		break;
	}
}

static void
fprintlog_rfc5424(struct filed *f, const char *hostname, const char *app_name,
    const char *procid, const char *msgid, const char *structured_data,
    const char *msg, int flags)
{
	struct iovlist il;
	suseconds_t usec;
	int i;
	char timebuf[33], priority_number[5];

	iovlist_init(&il);
	if (f->f_type == F_WALL)
		iovlist_append(&il, "\r\n\aMessage from syslogd ...\r\n");
	iovlist_append(&il, "<");
	snprintf(priority_number, sizeof(priority_number), "%d", f->f_prevpri);
	iovlist_append(&il, priority_number);
	iovlist_append(&il, ">1 ");
	if (strftime(timebuf, sizeof(timebuf), "%FT%T.______%z",
	    &f->f_lasttime.tm) == sizeof(timebuf) - 2) {
		/* Add colon to the time zone offset, which %z doesn't do. */
		timebuf[32] = '\0';
		timebuf[31] = timebuf[30];
		timebuf[30] = timebuf[29];
		timebuf[29] = ':';

		/* Overwrite space for microseconds with actual value. */
		usec = f->f_lasttime.usec;
		for (i = 25; i >= 20; --i) {
			timebuf[i] = usec % 10 + '0';
			usec /= 10;
		}
		iovlist_append(&il, timebuf);
	} else
		iovlist_append(&il, "-");
	iovlist_append(&il, " ");
	iovlist_append(&il, hostname);
	iovlist_append(&il, " ");
	iovlist_append(&il, app_name == NULL ? "-" : app_name);
	iovlist_append(&il, " ");
	iovlist_append(&il, procid == NULL ? "-" : procid);
	iovlist_append(&il, " ");
	iovlist_append(&il, msgid == NULL ? "-" : msgid);
	iovlist_append(&il, " ");
	iovlist_append(&il, structured_data == NULL ? "-" : structured_data);
	iovlist_append(&il, " ");
	iovlist_append(&il, msg);

	fprintlog_write(f, &il, flags);
}

static void
fprintlog_rfc3164(struct filed *f, const char *hostname, const char *app_name,
    const char *procid, const char *msg, int flags)
{
	struct iovlist il;
	const CODE *c;
	int facility, priority;
	char timebuf[RFC3164_DATELEN + 1], facility_number[5],
	    priority_number[5];
	bool facility_found, priority_found;

	if (strftime(timebuf, sizeof(timebuf), RFC3164_DATEFMT,
	    &f->f_lasttime.tm) == 0)
		timebuf[0] = '\0';

	iovlist_init(&il);
	switch (f->f_type) {
	case F_FORW:
		/* Message forwarded over the network. */
		iovlist_append(&il, "<");
		snprintf(priority_number, sizeof(priority_number), "%d",
		    f->f_prevpri);
		iovlist_append(&il, priority_number);
		iovlist_append(&il, ">");
		iovlist_append(&il, timebuf);
		if (strcasecmp(hostname, LocalHostName) != 0) {
			iovlist_append(&il, " Forwarded from ");
			iovlist_append(&il, hostname);
			iovlist_append(&il, ":");
		}
		iovlist_append(&il, " ");
		break;

	case F_WALL:
		/* Message written to terminals. */
		iovlist_append(&il, "\r\n\aMessage from syslogd@");
		iovlist_append(&il, hostname);
		iovlist_append(&il, " at ");
		iovlist_append(&il, timebuf);
		iovlist_append(&il, " ...\r\n");
		break;

	default:
		/* Message written to files. */
		iovlist_append(&il, timebuf);
		iovlist_append(&il, " ");

		if (LogFacPri) {
			iovlist_append(&il, "<");

			facility = f->f_prevpri & LOG_FACMASK;
			facility_found = false;
			if (LogFacPri > 1) {
				for (c = facilitynames; c->c_name; c++) {
					if (c->c_val == facility) {
						iovlist_append(&il, c->c_name);
						facility_found = true;
						break;
					}
				}
			}
			if (!facility_found) {
				snprintf(facility_number,
				    sizeof(facility_number), "%d",
				    LOG_FAC(facility));
				iovlist_append(&il, facility_number);
			}

			iovlist_append(&il, ".");

			priority = LOG_PRI(f->f_prevpri);
			priority_found = false;
			if (LogFacPri > 1) {
				for (c = prioritynames; c->c_name; c++) {
					if (c->c_val == priority) {
						iovlist_append(&il, c->c_name);
						priority_found = true;
						break;
					}
				}
			}
			if (!priority_found) {
				snprintf(priority_number,
				    sizeof(priority_number), "%d", priority);
				iovlist_append(&il, priority_number);
			}

			iovlist_append(&il, "> ");
		}

		iovlist_append(&il, hostname);
		iovlist_append(&il, " ");
		break;
	}

	/* Message body with application name and process ID prefixed. */
	if (app_name != NULL) {
		iovlist_append(&il, app_name);
		if (procid != NULL) {
			iovlist_append(&il, "[");
			iovlist_append(&il, procid);
			iovlist_append(&il, "]");
		}
		iovlist_append(&il, ": ");
	}
	iovlist_append(&il, msg);

	fprintlog_write(f, &il, flags);
}

static void
fprintlog_first(struct filed *f, const char *hostname, const char *app_name,
    const char *procid, const char *msgid __unused,
    const char *structured_data __unused, const char *msg, int flags)
{

	dprintf("Logging to %s", TypeNames[f->f_type]);
	f->f_time = now;
	f->f_prevcount = 0;
	if (f->f_type == F_UNUSED) {
		dprintf("\n");
		return;
	}

	if (RFC3164OutputFormat)
		fprintlog_rfc3164(f, hostname, app_name, procid, msg, flags);
	else
		fprintlog_rfc5424(f, hostname, app_name, procid, msgid,
		    structured_data, msg, flags);
}

/*
 * Prints a message to a log file that the previously logged message was
 * received multiple times.
 */
static void
fprintlog_successive(struct filed *f, int flags)
{
	char msg[100];

	assert(f->f_prevcount > 0);
	snprintf(msg, sizeof(msg), "last message repeated %d times",
	    f->f_prevcount);
	fprintlog_first(f, LocalHostName, "syslogd", NULL, NULL, NULL, msg,
	    flags);
}

/*
 *  WALLMSG -- Write a message to the world at large
 *
 *	Write the specified message to either the entire
 *	world, or a list of approved users.
 */
static void
wallmsg(struct filed *f, struct iovec *iov, const int iovlen)
{
	static int reenter;			/* avoid calling ourselves */
	struct utmpx *ut;
	int i;
	const char *p;

	if (reenter++)
		return;
	setutxent();
	/* NOSTRICT */
	while ((ut = getutxent()) != NULL) {
		if (ut->ut_type != USER_PROCESS)
			continue;
		if (f->f_type == F_WALL) {
			if ((p = ttymsg(iov, iovlen, ut->ut_line,
			    TTYMSGTIME)) != NULL) {
				errno = 0;	/* already in msg */
				logerror(p);
			}
			continue;
		}
		/* should we send the message to this user? */
		for (i = 0; i < MAXUNAMES; i++) {
			if (!f->fu_uname[i][0])
				break;
			if (!strcmp(f->fu_uname[i], ut->ut_user)) {
				if ((p = ttymsg_check(iov, iovlen, ut->ut_line,
				    TTYMSGTIME)) != NULL) {
					errno = 0;	/* already in msg */
					logerror(p);
				}
				break;
			}
		}
	}
	endutxent();
	reenter = 0;
}

/*
 * Wrapper routine for ttymsg() that checks the terminal for messages enabled.
 */
static const char *
ttymsg_check(struct iovec *iov, int iovcnt, char *line, int tmout)
{
	static char device[1024];
	static char errbuf[1024];
	struct stat sb;

	(void) snprintf(device, sizeof(device), "%s%s", _PATH_DEV, line);

	if (stat(device, &sb) < 0) {
		(void) snprintf(errbuf, sizeof(errbuf),
		    "%s: %s", device, strerror(errno));
		return (errbuf);
	}
	if ((sb.st_mode & S_IWGRP) == 0)
		/* Messages disabled. */
		return (NULL);
	return ttymsg(iov, iovcnt, line, tmout);
}

static void
reapchild(int signo __unused)
{
	int status;
	pid_t pid;
	struct filed *f;

	while ((pid = wait3(&status, WNOHANG, (struct rusage *)NULL)) > 0) {
		/* First, look if it's a process from the dead queue. */
		if (deadq_removebypid(pid))
			continue;

		/* Now, look in list of active processes. */
		STAILQ_FOREACH(f, &fhead, next) {
			if (f->f_type == F_PIPE &&
			    f->fu_pipe_pid == pid) {
				close_filed(f);
				log_deadchild(pid, status, f->fu_pipe_pname);
				break;
			}
		}
	}
	WantReapchild = 0;
}

/*
 * Return a printable representation of a host address.
 */
static const char *
cvthname(struct sockaddr *f)
{
	int error, hl;
	static char hname[NI_MAXHOST], ip[NI_MAXHOST];

	dprintf("cvthname(%d) len = %d\n", f->sa_family, f->sa_len);
	error = getnameinfo(f, f->sa_len, ip, sizeof(ip), NULL, 0,
		    NI_NUMERICHOST);
	if (error) {
		dprintf("Malformed from address %s\n", gai_strerror(error));
		return ("???");
	}
	dprintf("cvthname(%s)\n", ip);

	if (!resolve)
		return (ip);

	error = getnameinfo(f, f->sa_len, hname, sizeof(hname),
		    NULL, 0, NI_NAMEREQD);
	if (error) {
		dprintf("Host name for your address (%s) unknown\n", ip);
		return (ip);
	}
	hl = strlen(hname);
	if (hl > 0 && hname[hl-1] == '.')
		hname[--hl] = '\0';
	trimdomain(hname, hl);
	return (hname);
}

static void
dodie(int signo)
{

	WantDie = signo;
}

static void
domark(int signo __unused)
{

	MarkSet = 1;
}

/*
 * Print syslogd errors some place.
 */
static void
logerror(const char *msg)
{
	char buf[512];
	static int recursed = 0;

	/* If there's an error while trying to log an error, give up. */
	if (recursed)
		return;
	recursed++;
	if (errno != 0) {
		(void)snprintf(buf, sizeof(buf), "%s: %s", msg,
		    strerror(errno));
		msg = buf;
	}
	errno = 0;
	dprintf("%s\n", buf);
	logmsg(LOG_SYSLOG|LOG_ERR, NULL, LocalHostName, "syslogd", NULL, NULL,
	    NULL, msg, 0);
	recursed--;
}

static void
die(int signo)
{
	struct filed *f;
	struct socklist *sl;
	char buf[100];

	STAILQ_FOREACH(f, &fhead, next) {
		/* flush any pending output */
		if (f->f_prevcount)
			fprintlog_successive(f, 0);
		if (f->f_type == F_PIPE && f->fu_pipe_pid > 0)
			close_filed(f);
	}
	if (signo) {
		dprintf("syslogd: exiting on signal %d\n", signo);
		(void)snprintf(buf, sizeof(buf), "exiting on signal %d", signo);
		errno = 0;
		logerror(buf);
	}
	STAILQ_FOREACH(sl, &shead, next) {
		if (sl->sl_sa != NULL && sl->sl_family == AF_LOCAL)
			unlink(sl->sl_peer->pe_name);
	}
	pidfile_remove(pfh);

	exit(1);
}

static int
configfiles(const struct dirent *dp)
{
	const char *p;
	size_t ext_len;

	if (dp->d_name[0] == '.')
		return (0);

	ext_len = sizeof(include_ext) -1;

	if (dp->d_namlen <= ext_len)
		return (0);

	p = &dp->d_name[dp->d_namlen - ext_len];
	if (strcmp(p, include_ext) != 0)
		return (0);

	return (1);
}

static void
readconfigfile(FILE *cf, int allow_includes)
{
	FILE *cf2;
	struct filed *f;
	struct dirent **ent;
	char cline[LINE_MAX];
	char host[MAXHOSTNAMELEN];
	char prog[LINE_MAX];
	char file[MAXPATHLEN];
	char *p, *tmp;
	int i, nents;
	size_t include_len;

	/*
	 *  Foreach line in the conf table, open that file.
	 */
	include_len = sizeof(include_str) -1;
	(void)strlcpy(host, "*", sizeof(host));
	(void)strlcpy(prog, "*", sizeof(prog));
	while (fgets(cline, sizeof(cline), cf) != NULL) {
		/*
		 * check for end-of-section, comments, strip off trailing
		 * spaces and newline character. #!prog is treated specially:
		 * following lines apply only to that program.
		 */
		for (p = cline; isspace(*p); ++p)
			continue;
		if (*p == 0)
			continue;
		if (allow_includes &&
		    strncmp(p, include_str, include_len) == 0 &&
		    isspace(p[include_len])) {
			p += include_len;
			while (isspace(*p))
				p++;
			tmp = p;
			while (*tmp != '\0' && !isspace(*tmp))
				tmp++;
			*tmp = '\0';
			dprintf("Trying to include files in '%s'\n", p);
			nents = scandir(p, &ent, configfiles, alphasort);
			if (nents == -1) {
				dprintf("Unable to open '%s': %s\n", p,
				    strerror(errno));
				continue;
			}
			for (i = 0; i < nents; i++) {
				if (snprintf(file, sizeof(file), "%s/%s", p,
				    ent[i]->d_name) >= (int)sizeof(file)) {
					dprintf("ignoring path too long: "
					    "'%s/%s'\n", p, ent[i]->d_name);
					free(ent[i]);
					continue;
				}
				free(ent[i]);
				cf2 = fopen(file, "r");
				if (cf2 == NULL)
					continue;
				dprintf("reading %s\n", file);
				readconfigfile(cf2, 0);
				fclose(cf2);
			}
			free(ent);
			continue;
		}
		if (*p == '#') {
			p++;
			if (*p != '!' && *p != '+' && *p != '-')
				continue;
		}
		if (*p == '+' || *p == '-') {
			host[0] = *p++;
			while (isspace(*p))
				p++;
			if ((!*p) || (*p == '*')) {
				(void)strlcpy(host, "*", sizeof(host));
				continue;
			}
			if (*p == '@')
				p = LocalHostName;
			for (i = 1; i < MAXHOSTNAMELEN - 1; i++) {
				if (!isalnum(*p) && *p != '.' && *p != '-'
				    && *p != ',' && *p != ':' && *p != '%')
					break;
				host[i] = *p++;
			}
			host[i] = '\0';
			continue;
		}
		if (*p == '!') {
			p++;
			while (isspace(*p)) p++;
			if ((!*p) || (*p == '*')) {
				(void)strlcpy(prog, "*", sizeof(prog));
				continue;
			}
			for (i = 0; i < LINE_MAX - 1; i++) {
				if (!isprint(p[i]) || isspace(p[i]))
					break;
				prog[i] = p[i];
			}
			prog[i] = 0;
			continue;
		}
		for (p = cline + 1; *p != '\0'; p++) {
			if (*p != '#')
				continue;
			if (*(p - 1) == '\\') {
				strcpy(p - 1, p);
				p--;
				continue;
			}
			*p = '\0';
			break;
		}
		for (i = strlen(cline) - 1; i >= 0 && isspace(cline[i]); i--)
			cline[i] = '\0';
		f = cfline(cline, prog, host);
		if (f != NULL)
			addfile(f);
		free(f);
	}
}

static void
sighandler(int signo)
{

	/* Send an wake-up signal to the select() loop. */
	write(sigpipe[1], &signo, sizeof(signo));
}

/*
 *  INIT -- Initialize syslogd from configuration table
 */
static void
init(int signo)
{
	int i;
	FILE *cf;
	struct filed *f;
	char *p;
	char oldLocalHostName[MAXHOSTNAMELEN];
	char hostMsg[2*MAXHOSTNAMELEN+40];
	char bootfileMsg[LINE_MAX];

	dprintf("init\n");
	WantInitialize = 0;

	/*
	 * Load hostname (may have changed).
	 */
	if (signo != 0)
		(void)strlcpy(oldLocalHostName, LocalHostName,
		    sizeof(oldLocalHostName));
	if (gethostname(LocalHostName, sizeof(LocalHostName)))
		err(EX_OSERR, "gethostname() failed");
	if ((p = strchr(LocalHostName, '.')) != NULL) {
		/* RFC 5424 prefers logging FQDNs. */
		if (RFC3164OutputFormat)
			*p = '\0';
		LocalDomain = p + 1;
	} else {
		LocalDomain = "";
	}

	/*
	 * Load / reload timezone data (in case it changed).
	 *
	 * Just calling tzset() again does not work, the timezone code
	 * caches the result.  However, by setting the TZ variable, one
	 * can defeat the caching and have the timezone code really
	 * reload the timezone data.  Respect any initial setting of
	 * TZ, in case the system is configured specially.
	 */
	dprintf("loading timezone data via tzset()\n");
	if (getenv("TZ")) {
		tzset();
	} else {
		setenv("TZ", ":/etc/localtime", 1);
		tzset();
		unsetenv("TZ");
	}

	/*
	 *  Close all open log files.
	 */
	Initialized = 0;
	STAILQ_FOREACH(f, &fhead, next) {
		/* flush any pending output */
		if (f->f_prevcount)
			fprintlog_successive(f, 0);

		switch (f->f_type) {
		case F_FILE:
		case F_FORW:
		case F_CONSOLE:
		case F_TTY:
			close_filed(f);
			break;
		case F_PIPE:
			deadq_enter(f->fu_pipe_pid, f->fu_pipe_pname);
			close_filed(f);
			break;
		}
	}
	while(!STAILQ_EMPTY(&fhead)) {
		f = STAILQ_FIRST(&fhead);
		STAILQ_REMOVE_HEAD(&fhead, next);
		free(f->f_program);
		free(f->f_host);
		free(f);
	}

	/* open the configuration file */
	if ((cf = fopen(ConfFile, "r")) == NULL) {
		dprintf("cannot open %s\n", ConfFile);
		f = cfline("*.ERR\t/dev/console", "*", "*");
		if (f != NULL)
			addfile(f);
		free(f);
		f = cfline("*.PANIC\t*", "*", "*");
		if (f != NULL)
			addfile(f);
		free(f);
		Initialized = 1;

		return;
	}

	readconfigfile(cf, 1);

	/* close the configuration file */
	(void)fclose(cf);

	Initialized = 1;

	if (Debug) {
		int port;
		STAILQ_FOREACH(f, &fhead, next) {
			for (i = 0; i <= LOG_NFACILITIES; i++)
				if (f->f_pmask[i] == INTERNAL_NOPRI)
					printf("X ");
				else
					printf("%d ", f->f_pmask[i]);
			printf("%s: ", TypeNames[f->f_type]);
			switch (f->f_type) {
			case F_FILE:
				printf("%s", f->fu_fname);
				break;

			case F_CONSOLE:
			case F_TTY:
				printf("%s%s", _PATH_DEV, f->fu_fname);
				break;

			case F_FORW:
				switch (f->fu_forw_addr->ai_family) {
#ifdef INET
				case AF_INET:
					port = ntohs(satosin(f->fu_forw_addr->ai_addr)->sin_port);
					break;
#endif
#ifdef INET6
				case AF_INET6:
					port = ntohs(satosin6(f->fu_forw_addr->ai_addr)->sin6_port);
					break;
#endif
				default:
					port = 0;
				}
				if (port != 514) {
					printf("%s:%d",
						f->fu_forw_hname, port);
				} else {
					printf("%s", f->fu_forw_hname);
				}
				break;

			case F_PIPE:
				printf("%s", f->fu_pipe_pname);
				break;

			case F_USERS:
				for (i = 0; i < MAXUNAMES && *f->fu_uname[i]; i++)
					printf("%s, ", f->fu_uname[i]);
				break;
			}
			if (f->f_program)
				printf(" (%s)", f->f_program);
			printf("\n");
		}
	}

	logmsg(LOG_SYSLOG | LOG_INFO, NULL, LocalHostName, "syslogd", NULL,
	    NULL, NULL, "restart", 0);
	dprintf("syslogd: restarted\n");
	/*
	 * Log a change in hostname, but only on a restart.
	 */
	if (signo != 0 && strcmp(oldLocalHostName, LocalHostName) != 0) {
		(void)snprintf(hostMsg, sizeof(hostMsg),
		    "hostname changed, \"%s\" to \"%s\"",
		    oldLocalHostName, LocalHostName);
		logmsg(LOG_SYSLOG | LOG_INFO, NULL, LocalHostName, "syslogd",
		    NULL, NULL, NULL, hostMsg, 0);
		dprintf("%s\n", hostMsg);
	}
	/*
	 * Log the kernel boot file if we aren't going to use it as
	 * the prefix, and if this is *not* a restart.
	 */
	if (signo == 0 && !use_bootfile) {
		(void)snprintf(bootfileMsg, sizeof(bootfileMsg),
		    "kernel boot file is %s", bootfile);
		logmsg(LOG_KERN | LOG_INFO, NULL, LocalHostName, "syslogd",
		    NULL, NULL, NULL, bootfileMsg, 0);
		dprintf("%s\n", bootfileMsg);
	}
}

/*
 * Crack a configuration file line
 */
static struct filed *
cfline(const char *line, const char *prog, const char *host)
{
	struct filed *f;
	struct addrinfo hints, *res;
	int error, i, pri, syncfile;
	const char *p, *q;
	char *bp;
	char buf[MAXLINE], ebuf[100];

	dprintf("cfline(\"%s\", f, \"%s\", \"%s\")\n", line, prog, host);

	f = calloc(1, sizeof(*f));
	if (f == NULL) {
		logerror("malloc");
		exit(1);
	}
	errno = 0;	/* keep strerror() stuff out of logerror messages */

	for (i = 0; i <= LOG_NFACILITIES; i++)
		f->f_pmask[i] = INTERNAL_NOPRI;

	/* save hostname if any */
	if (host && *host == '*')
		host = NULL;
	if (host) {
		int hl;

		f->f_host = strdup(host);
		if (f->f_host == NULL) {
			logerror("strdup");
			exit(1);
		}
		hl = strlen(f->f_host);
		if (hl > 0 && f->f_host[hl-1] == '.')
			f->f_host[--hl] = '\0';
		trimdomain(f->f_host, hl);
	}

	/* save program name if any */
	if (prog && *prog == '*')
		prog = NULL;
	if (prog) {
		f->f_program = strdup(prog);
		if (f->f_program == NULL) {
			logerror("strdup");
			exit(1);
		}
	}

	/* scan through the list of selectors */
	for (p = line; *p && *p != '\t' && *p != ' ';) {
		int pri_done;
		int pri_cmp;
		int pri_invert;

		/* find the end of this facility name list */
		for (q = p; *q && *q != '\t' && *q != ' ' && *q++ != '.'; )
			continue;

		/* get the priority comparison */
		pri_cmp = 0;
		pri_done = 0;
		pri_invert = 0;
		if (*q == '!') {
			pri_invert = 1;
			q++;
		}
		while (!pri_done) {
			switch (*q) {
			case '<':
				pri_cmp |= PRI_LT;
				q++;
				break;
			case '=':
				pri_cmp |= PRI_EQ;
				q++;
				break;
			case '>':
				pri_cmp |= PRI_GT;
				q++;
				break;
			default:
				pri_done++;
				break;
			}
		}

		/* collect priority name */
		for (bp = buf; *q && !strchr("\t,; ", *q); )
			*bp++ = *q++;
		*bp = '\0';

		/* skip cruft */
		while (strchr(",;", *q))
			q++;

		/* decode priority name */
		if (*buf == '*') {
			pri = LOG_PRIMASK;
			pri_cmp = PRI_LT | PRI_EQ | PRI_GT;
		} else {
			/* Ignore trailing spaces. */
			for (i = strlen(buf) - 1; i >= 0 && buf[i] == ' '; i--)
				buf[i] = '\0';

			pri = decode(buf, prioritynames);
			if (pri < 0) {
				errno = 0;
				(void)snprintf(ebuf, sizeof ebuf,
				    "unknown priority name \"%s\"", buf);
				logerror(ebuf);
				free(f);
				return (NULL);
			}
		}
		if (!pri_cmp)
			pri_cmp = (UniquePriority)
				  ? (PRI_EQ)
				  : (PRI_EQ | PRI_GT)
				  ;
		if (pri_invert)
			pri_cmp ^= PRI_LT | PRI_EQ | PRI_GT;

		/* scan facilities */
		while (*p && !strchr("\t.; ", *p)) {
			for (bp = buf; *p && !strchr("\t,;. ", *p); )
				*bp++ = *p++;
			*bp = '\0';

			if (*buf == '*') {
				for (i = 0; i < LOG_NFACILITIES; i++) {
					f->f_pmask[i] = pri;
					f->f_pcmp[i] = pri_cmp;
				}
			} else {
				i = decode(buf, facilitynames);
				if (i < 0) {
					errno = 0;
					(void)snprintf(ebuf, sizeof ebuf,
					    "unknown facility name \"%s\"",
					    buf);
					logerror(ebuf);
					free(f);
					return (NULL);
				}
				f->f_pmask[i >> 3] = pri;
				f->f_pcmp[i >> 3] = pri_cmp;
			}
			while (*p == ',' || *p == ' ')
				p++;
		}

		p = q;
	}

	/* skip to action part */
	while (*p == '\t' || *p == ' ')
		p++;

	if (*p == '-') {
		syncfile = 0;
		p++;
	} else
		syncfile = 1;

	switch (*p) {
	case '@':
		{
			char *tp;
			char endkey = ':';
			/*
			 * scan forward to see if there is a port defined.
			 * so we can't use strlcpy..
			 */
			i = sizeof(f->fu_forw_hname);
			tp = f->fu_forw_hname;
			p++;

			/*
			 * an ipv6 address should start with a '[' in that case
			 * we should scan for a ']'
			 */
			if (*p == '[') {
				p++;
				endkey = ']';
			}
			while (*p && (*p != endkey) && (i-- > 0)) {
				*tp++ = *p++;
			}
			if (endkey == ']' && *p == endkey)
				p++;
			*tp = '\0';
		}
		/* See if we copied a domain and have a port */
		if (*p == ':')
			p++;
		else
			p = NULL;

		hints = (struct addrinfo){
			.ai_family = family,
			.ai_socktype = SOCK_DGRAM
		};
		error = getaddrinfo(f->fu_forw_hname,
				p ? p : "syslog", &hints, &res);
		if (error) {
			logerror(gai_strerror(error));
			break;
		}
		f->fu_forw_addr = res;
		f->f_type = F_FORW;
		break;

	case '/':
		if ((f->f_file = open(p, logflags, 0600)) < 0) {
			f->f_type = F_UNUSED;
			logerror(p);
			break;
		}
		if (syncfile)
			f->f_flags |= FFLAG_SYNC;
		if (isatty(f->f_file)) {
			if (strcmp(p, ctty) == 0)
				f->f_type = F_CONSOLE;
			else
				f->f_type = F_TTY;
			(void)strlcpy(f->fu_fname, p + sizeof(_PATH_DEV) - 1,
			    sizeof(f->fu_fname));
		} else {
			(void)strlcpy(f->fu_fname, p, sizeof(f->fu_fname));
			f->f_type = F_FILE;
		}
		break;

	case '|':
		f->fu_pipe_pid = 0;
		(void)strlcpy(f->fu_pipe_pname, p + 1,
		    sizeof(f->fu_pipe_pname));
		f->f_type = F_PIPE;
		break;

	case '*':
		f->f_type = F_WALL;
		break;

	default:
		for (i = 0; i < MAXUNAMES && *p; i++) {
			for (q = p; *q && *q != ','; )
				q++;
			(void)strncpy(f->fu_uname[i], p, MAXLOGNAME - 1);
			if ((q - p) >= MAXLOGNAME)
				f->fu_uname[i][MAXLOGNAME - 1] = '\0';
			else
				f->fu_uname[i][q - p] = '\0';
			while (*q == ',' || *q == ' ')
				q++;
			p = q;
		}
		f->f_type = F_USERS;
		break;
	}
	return (f);
}


/*
 *  Decode a symbolic name to a numeric value
 */
static int
decode(const char *name, const CODE *codetab)
{
	const CODE *c;
	char *p, buf[40];

	if (isdigit(*name))
		return (atoi(name));

	for (p = buf; *name && p < &buf[sizeof(buf) - 1]; p++, name++) {
		if (isupper(*name))
			*p = tolower(*name);
		else
			*p = *name;
	}
	*p = '\0';
	for (c = codetab; c->c_name; c++)
		if (!strcmp(buf, c->c_name))
			return (c->c_val);

	return (-1);
}

static void
markit(void)
{
	struct filed *f;
	struct deadq_entry *dq, *dq0;

	now = time((time_t *)NULL);
	MarkSeq += TIMERINTVL;
	if (MarkSeq >= MarkInterval) {
		logmsg(LOG_INFO, NULL, LocalHostName, NULL, NULL, NULL, NULL,
		    "-- MARK --", MARK);
		MarkSeq = 0;
	}

	STAILQ_FOREACH(f, &fhead, next) {
		if (f->f_prevcount && now >= REPEATTIME(f)) {
			dprintf("flush %s: repeated %d times, %d sec.\n",
			    TypeNames[f->f_type], f->f_prevcount,
			    repeatinterval[f->f_repeatcount]);
			fprintlog_successive(f, 0);
			BACKOFF(f);
		}
	}

	/* Walk the dead queue, and see if we should signal somebody. */
	TAILQ_FOREACH_SAFE(dq, &deadq_head, dq_entries, dq0) {
		switch (dq->dq_timeout) {
		case 0:
			/* Already signalled once, try harder now. */
			if (kill(dq->dq_pid, SIGKILL) != 0)
				(void)deadq_remove(dq);
			break;

		case 1:
			/*
			 * Timed out on dead queue, send terminate
			 * signal.  Note that we leave the removal
			 * from the dead queue to reapchild(), which
			 * will also log the event (unless the process
			 * didn't even really exist, in case we simply
			 * drop it from the dead queue).
			 */
			if (kill(dq->dq_pid, SIGTERM) != 0)
				(void)deadq_remove(dq);
			else
				dq->dq_timeout--;
			break;
		default:
			dq->dq_timeout--;
		}
	}
	MarkSet = 0;
	(void)alarm(TIMERINTVL);
}

/*
 * fork off and become a daemon, but wait for the child to come online
 * before returning to the parent, or we get disk thrashing at boot etc.
 * Set a timer so we don't hang forever if it wedges.
 */
static int
waitdaemon(int maxwait)
{
	int fd;
	int status;
	pid_t pid, childpid;

	switch (childpid = fork()) {
	case -1:
		return (-1);
	case 0:
		break;
	default:
		signal(SIGALRM, timedout);
		alarm(maxwait);
		while ((pid = wait3(&status, 0, NULL)) != -1) {
			if (WIFEXITED(status))
				errx(1, "child pid %d exited with return code %d",
					pid, WEXITSTATUS(status));
			if (WIFSIGNALED(status))
				errx(1, "child pid %d exited on signal %d%s",
					pid, WTERMSIG(status),
					WCOREDUMP(status) ? " (core dumped)" :
					"");
			if (pid == childpid)	/* it's gone... */
				break;
		}
		exit(0);
	}

	if (setsid() == -1)
		return (-1);

	(void)chdir("/");
	if ((fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			(void)close(fd);
	}
	return (getppid());
}

/*
 * We get a SIGALRM from the child when it's running and finished doing it's
 * fsync()'s or O_SYNC writes for all the boot messages.
 *
 * We also get a signal from the kernel if the timer expires, so check to
 * see what happened.
 */
static void
timedout(int sig __unused)
{
	int left;
	left = alarm(0);
	signal(SIGALRM, SIG_DFL);
	if (left == 0)
		errx(1, "timed out waiting for child");
	else
		_exit(0);
}

/*
 * Add `s' to the list of allowable peer addresses to accept messages
 * from.
 *
 * `s' is a string in the form:
 *
 *    [*]domainname[:{servicename|portnumber|*}]
 *
 * or
 *
 *    netaddr/maskbits[:{servicename|portnumber|*}]
 *
 * Returns -1 on error, 0 if the argument was valid.
 */
static int
#if defined(INET) || defined(INET6)
allowaddr(char *s)
#else
allowaddr(char *s __unused)
#endif
{
#if defined(INET) || defined(INET6)
	char *cp1, *cp2;
	struct allowedpeer *ap;
	struct servent *se;
	int masklen = -1;
	struct addrinfo hints, *res = NULL;
#ifdef INET
	in_addr_t *addrp, *maskp;
#endif
#ifdef INET6
	uint32_t *addr6p, *mask6p;
#endif
	char ip[NI_MAXHOST];

	ap = calloc(1, sizeof(*ap));
	if (ap == NULL)
		err(1, "malloc failed");

#ifdef INET6
	if (*s != '[' || (cp1 = strchr(s + 1, ']')) == NULL)
#endif
		cp1 = s;
	if ((cp1 = strrchr(cp1, ':'))) {
		/* service/port provided */
		*cp1++ = '\0';
		if (strlen(cp1) == 1 && *cp1 == '*')
			/* any port allowed */
			ap->port = 0;
		else if ((se = getservbyname(cp1, "udp"))) {
			ap->port = ntohs(se->s_port);
		} else {
			ap->port = strtol(cp1, &cp2, 0);
			/* port not numeric */
			if (*cp2 != '\0')
				goto err;
		}
	} else {
		if ((se = getservbyname("syslog", "udp")))
			ap->port = ntohs(se->s_port);
		else
			/* sanity, should not happen */
			ap->port = 514;
	}

	if ((cp1 = strchr(s, '/')) != NULL &&
	    strspn(cp1 + 1, "0123456789") == strlen(cp1 + 1)) {
		*cp1 = '\0';
		if ((masklen = atoi(cp1 + 1)) < 0)
			goto err;
	}
#ifdef INET6
	if (*s == '[') {
		cp2 = s + strlen(s) - 1;
		if (*cp2 == ']') {
			++s;
			*cp2 = '\0';
		} else {
			cp2 = NULL;
		}
	} else {
		cp2 = NULL;
	}
#endif
	hints = (struct addrinfo){
		.ai_family = PF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_flags = AI_PASSIVE | AI_NUMERICHOST
	};
	if (getaddrinfo(s, NULL, &hints, &res) == 0) {
		ap->isnumeric = 1;
		memcpy(&ap->a_addr, res->ai_addr, res->ai_addrlen);
		ap->a_mask = (struct sockaddr_storage){
			.ss_family = res->ai_family,
			.ss_len = res->ai_addrlen
		};
		switch (res->ai_family) {
#ifdef INET
		case AF_INET:
			maskp = &sstosin(&ap->a_mask)->sin_addr.s_addr;
			addrp = &sstosin(&ap->a_addr)->sin_addr.s_addr;
			if (masklen < 0) {
				/* use default netmask */
				if (IN_CLASSA(ntohl(*addrp)))
					*maskp = htonl(IN_CLASSA_NET);
				else if (IN_CLASSB(ntohl(*addrp)))
					*maskp = htonl(IN_CLASSB_NET);
				else
					*maskp = htonl(IN_CLASSC_NET);
			} else if (masklen == 0) {
				*maskp = 0;
			} else if (masklen <= 32) {
				/* convert masklen to netmask */
				*maskp = htonl(~((1 << (32 - masklen)) - 1));
			} else {
				goto err;
			}
			/* Lose any host bits in the network number. */
			*addrp &= *maskp;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (masklen > 128)
				goto err;

			if (masklen < 0)
				masklen = 128;
			mask6p = (uint32_t *)&sstosin6(&ap->a_mask)->sin6_addr.s6_addr32[0];
			addr6p = (uint32_t *)&sstosin6(&ap->a_addr)->sin6_addr.s6_addr32[0];
			/* convert masklen to netmask */
			while (masklen > 0) {
				if (masklen < 32) {
					*mask6p =
					    htonl(~(0xffffffff >> masklen));
					*addr6p &= *mask6p;
					break;
				} else {
					*mask6p++ = 0xffffffff;
					addr6p++;
					masklen -= 32;
				}
			}
			break;
#endif
		default:
			goto err;
		}
		freeaddrinfo(res);
	} else {
		/* arg `s' is domain name */
		ap->isnumeric = 0;
		ap->a_name = s;
		if (cp1)
			*cp1 = '/';
#ifdef INET6
		if (cp2) {
			*cp2 = ']';
			--s;
		}
#endif
	}
	STAILQ_INSERT_TAIL(&aphead, ap, next);

	if (Debug) {
		printf("allowaddr: rule ");
		if (ap->isnumeric) {
			printf("numeric, ");
			getnameinfo(sstosa(&ap->a_addr),
				    (sstosa(&ap->a_addr))->sa_len,
				    ip, sizeof ip, NULL, 0, NI_NUMERICHOST);
			printf("addr = %s, ", ip);
			getnameinfo(sstosa(&ap->a_mask),
				    (sstosa(&ap->a_mask))->sa_len,
				    ip, sizeof ip, NULL, 0, NI_NUMERICHOST);
			printf("mask = %s; ", ip);
		} else {
			printf("domainname = %s; ", ap->a_name);
		}
		printf("port = %d\n", ap->port);
	}

	return (0);
err:
	if (res != NULL)
		freeaddrinfo(res);
	free(ap);
#endif
	return (-1);
}

/*
 * Validate that the remote peer has permission to log to us.
 */
static int
validate(struct sockaddr *sa, const char *hname)
{
	int i;
	char name[NI_MAXHOST], ip[NI_MAXHOST], port[NI_MAXSERV];
	struct allowedpeer *ap;
#ifdef INET
	struct sockaddr_in *sin4, *a4p = NULL, *m4p = NULL;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6, *a6p = NULL, *m6p = NULL;
#endif
	struct addrinfo hints, *res;
	u_short sport;
	int num = 0;

	STAILQ_FOREACH(ap, &aphead, next) {
		num++;
	}
	dprintf("# of validation rule: %d\n", num);
	if (num == 0)
		/* traditional behaviour, allow everything */
		return (1);

	(void)strlcpy(name, hname, sizeof(name));
	hints = (struct addrinfo){
		.ai_family = PF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_flags = AI_PASSIVE | AI_NUMERICHOST
	};
	if (getaddrinfo(name, NULL, &hints, &res) == 0)
		freeaddrinfo(res);
	else if (strchr(name, '.') == NULL) {
		strlcat(name, ".", sizeof name);
		strlcat(name, LocalDomain, sizeof name);
	}
	if (getnameinfo(sa, sa->sa_len, ip, sizeof(ip), port, sizeof(port),
			NI_NUMERICHOST | NI_NUMERICSERV) != 0)
		return (0);	/* for safety, should not occur */
	dprintf("validate: dgram from IP %s, port %s, name %s;\n",
		ip, port, name);
	sport = atoi(port);

	/* now, walk down the list */
	i = 0;
	STAILQ_FOREACH(ap, &aphead, next) {
		i++;
		if (ap->port != 0 && ap->port != sport) {
			dprintf("rejected in rule %d due to port mismatch.\n",
			    i);
			continue;
		}

		if (ap->isnumeric) {
			if (ap->a_addr.ss_family != sa->sa_family) {
				dprintf("rejected in rule %d due to address family mismatch.\n", i);
				continue;
			}
#ifdef INET
			else if (ap->a_addr.ss_family == AF_INET) {
				sin4 = satosin(sa);
				a4p = satosin(&ap->a_addr);
				m4p = satosin(&ap->a_mask);
				if ((sin4->sin_addr.s_addr & m4p->sin_addr.s_addr)
				    != a4p->sin_addr.s_addr) {
					dprintf("rejected in rule %d due to IP mismatch.\n", i);
					continue;
				}
			}
#endif
#ifdef INET6
			else if (ap->a_addr.ss_family == AF_INET6) {
				sin6 = satosin6(sa);
				a6p = satosin6(&ap->a_addr);
				m6p = satosin6(&ap->a_mask);
				if (a6p->sin6_scope_id != 0 &&
				    sin6->sin6_scope_id != a6p->sin6_scope_id) {
					dprintf("rejected in rule %d due to scope mismatch.\n", i);
					continue;
				}
				if (!IN6_ARE_MASKED_ADDR_EQUAL(&sin6->sin6_addr,
				    &a6p->sin6_addr, &m6p->sin6_addr)) {
					dprintf("rejected in rule %d due to IP mismatch.\n", i);
					continue;
				}
			}
#endif
			else
				continue;
		} else {
			if (fnmatch(ap->a_name, name, FNM_NOESCAPE) ==
			    FNM_NOMATCH) {
				dprintf("rejected in rule %d due to name "
				    "mismatch.\n", i);
				continue;
			}
		}
		dprintf("accepted in rule %d.\n", i);
		return (1);	/* hooray! */
	}
	return (0);
}

/*
 * Fairly similar to popen(3), but returns an open descriptor, as
 * opposed to a FILE *.
 */
static int
p_open(const char *prog, pid_t *rpid)
{
	int pfd[2], nulldesc;
	pid_t pid;
	char *argv[4]; /* sh -c cmd NULL */
	char errmsg[200];

	if (pipe(pfd) == -1)
		return (-1);
	if ((nulldesc = open(_PATH_DEVNULL, O_RDWR)) == -1)
		/* we are royally screwed anyway */
		return (-1);

	switch ((pid = fork())) {
	case -1:
		close(nulldesc);
		return (-1);

	case 0:
		(void)setsid();	/* Avoid catching SIGHUPs. */
		argv[0] = strdup("sh");
		argv[1] = strdup("-c");
		argv[2] = strdup(prog);
		argv[3] = NULL;
		if (argv[0] == NULL || argv[1] == NULL || argv[2] == NULL) {
			logerror("strdup");
			exit(1);
		}

		alarm(0);

		/* Restore signals marked as SIG_IGN. */
		(void)signal(SIGINT, SIG_DFL);
		(void)signal(SIGQUIT, SIG_DFL);
		(void)signal(SIGPIPE, SIG_DFL);

		dup2(pfd[0], STDIN_FILENO);
		dup2(nulldesc, STDOUT_FILENO);
		dup2(nulldesc, STDERR_FILENO);
		closefrom(STDERR_FILENO + 1);

		(void)execvp(_PATH_BSHELL, argv);
		_exit(255);
	}
	close(nulldesc);
	close(pfd[0]);
	/*
	 * Avoid blocking on a hung pipe.  With O_NONBLOCK, we are
	 * supposed to get an EWOULDBLOCK on writev(2), which is
	 * caught by the logic above anyway, which will in turn close
	 * the pipe, and fork a new logging subprocess if necessary.
	 * The stale subprocess will be killed some time later unless
	 * it terminated itself due to closing its input pipe (so we
	 * get rid of really dead puppies).
	 */
	if (fcntl(pfd[1], F_SETFL, O_NONBLOCK) == -1) {
		/* This is bad. */
		(void)snprintf(errmsg, sizeof errmsg,
			       "Warning: cannot change pipe to PID %d to "
			       "non-blocking behaviour.",
			       (int)pid);
		logerror(errmsg);
	}
	*rpid = pid;
	return (pfd[1]);
}

static void
deadq_enter(pid_t pid, const char *name)
{
	struct deadq_entry *dq;
	int status;

	if (pid == 0)
		return;
	/*
	 * Be paranoid, if we can't signal the process, don't enter it
	 * into the dead queue (perhaps it's already dead).  If possible,
	 * we try to fetch and log the child's status.
	 */
	if (kill(pid, 0) != 0) {
		if (waitpid(pid, &status, WNOHANG) > 0)
			log_deadchild(pid, status, name);
		return;
	}

	dq = malloc(sizeof(*dq));
	if (dq == NULL) {
		logerror("malloc");
		exit(1);
	}
	*dq = (struct deadq_entry){
		.dq_pid = pid,
		.dq_timeout = DQ_TIMO_INIT
	};
	TAILQ_INSERT_TAIL(&deadq_head, dq, dq_entries);
}

static int
deadq_remove(struct deadq_entry *dq)
{
	if (dq != NULL) {
		TAILQ_REMOVE(&deadq_head, dq, dq_entries);
		free(dq);
		return (1);
	}

	return (0);
}

static int
deadq_removebypid(pid_t pid)
{
	struct deadq_entry *dq;

	TAILQ_FOREACH(dq, &deadq_head, dq_entries) {
		if (dq->dq_pid == pid)
			break;
	}
	return (deadq_remove(dq));
}

static void
log_deadchild(pid_t pid, int status, const char *name)
{
	int code;
	char buf[256];
	const char *reason;

	errno = 0; /* Keep strerror() stuff out of logerror messages. */
	if (WIFSIGNALED(status)) {
		reason = "due to signal";
		code = WTERMSIG(status);
	} else {
		reason = "with status";
		code = WEXITSTATUS(status);
		if (code == 0)
			return;
	}
	(void)snprintf(buf, sizeof buf,
		       "Logging subprocess %d (%s) exited %s %d.",
		       pid, name, reason, code);
	logerror(buf);
}

static int
socksetup(struct peer *pe)
{
	struct addrinfo hints, *res, *res0;
	int error;
	char *cp;
	int (*sl_recv)(struct socklist *);
	/*
	 * We have to handle this case for backwards compatibility:
	 * If there are two (or more) colons but no '[' and ']',
	 * assume this is an inet6 address without a service.
	 */
	if (pe->pe_name != NULL) {
#ifdef INET6
		if (pe->pe_name[0] == '[' &&
		    (cp = strchr(pe->pe_name + 1, ']')) != NULL) {
			pe->pe_name = &pe->pe_name[1];
			*cp = '\0';
			if (cp[1] == ':' && cp[2] != '\0')
				pe->pe_serv = cp + 2;
		} else {
#endif
			cp = strchr(pe->pe_name, ':');
			if (cp != NULL && strchr(cp + 1, ':') == NULL) {
				*cp = '\0';
				if (cp[1] != '\0')
					pe->pe_serv = cp + 1;
				if (cp == pe->pe_name)
					pe->pe_name = NULL;
			}
#ifdef INET6
		}
#endif
	}
	hints = (struct addrinfo){
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_DGRAM,
		.ai_flags = AI_PASSIVE
	};
	if (pe->pe_name != NULL)
		dprintf("Trying peer: %s\n", pe->pe_name);
	if (pe->pe_serv == NULL)
		pe->pe_serv = "syslog";
	error = getaddrinfo(pe->pe_name, pe->pe_serv, &hints, &res0);
	if (error) {
		char *msgbuf;

		asprintf(&msgbuf, "getaddrinfo failed for %s%s: %s",
		    pe->pe_name == NULL ? "" : pe->pe_name, pe->pe_serv,
		    gai_strerror(error));
		errno = 0;
		if (msgbuf == NULL)
			logerror(gai_strerror(error));
		else
			logerror(msgbuf);
		free(msgbuf);
		die(0);
	}
	for (res = res0; res != NULL; res = res->ai_next) {
		int s;

		if (res->ai_family != AF_LOCAL &&
		    SecureMode > 1) {
			/* Only AF_LOCAL in secure mode. */
			continue;
		}
		if (family != AF_UNSPEC &&
		    res->ai_family != AF_LOCAL && res->ai_family != family)
			continue;

		s = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (s < 0) {
			logerror("socket");
			error++;
			continue;
		}
#ifdef INET6
		if (res->ai_family == AF_INET6) {
			if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
			       &(int){1}, sizeof(int)) < 0) {
				logerror("setsockopt(IPV6_V6ONLY)");
				close(s);
				error++;
				continue;
			}
		}
#endif
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		    &(int){1}, sizeof(int)) < 0) {
			logerror("setsockopt(SO_REUSEADDR)");
			close(s);
			error++;
			continue;
		}

		/*
		 * Bind INET and UNIX-domain sockets.
		 *
		 * A UNIX-domain socket is always bound to a pathname
		 * regardless of -N flag.
		 *
		 * For INET sockets, RFC 3164 recommends that client
		 * side message should come from the privileged syslogd port.
		 *
		 * If the system administrator chooses not to obey
		 * this, we can skip the bind() step so that the
		 * system will choose a port for us.
		 */
		if (res->ai_family == AF_LOCAL)
			unlink(pe->pe_name);
		if (res->ai_family == AF_LOCAL ||
		    NoBind == 0 || pe->pe_name != NULL) {
			if (bind(s, res->ai_addr, res->ai_addrlen) < 0) {
				logerror("bind");
				close(s);
				error++;
				continue;
			}
			if (res->ai_family == AF_LOCAL ||
			    SecureMode == 0)
				increase_rcvbuf(s);
		}
		if (res->ai_family == AF_LOCAL &&
		    chmod(pe->pe_name, pe->pe_mode) < 0) {
			dprintf("chmod %s: %s\n", pe->pe_name,
			    strerror(errno));
			close(s);
			error++;
			continue;
		}
		dprintf("new socket fd is %d\n", s);
		if (res->ai_socktype != SOCK_DGRAM) {
			listen(s, 5);
		}
		sl_recv = socklist_recv_sock;
#if defined(INET) || defined(INET6)
		if (SecureMode && (res->ai_family == AF_INET ||
		    res->ai_family == AF_INET6)) {
			dprintf("shutdown\n");
			/* Forbid communication in secure mode. */
			if (shutdown(s, SHUT_RD) < 0 &&
			    errno != ENOTCONN) {
				logerror("shutdown");
				if (!Debug)
					die(0);
			}
			sl_recv = NULL;
		} else
#endif
			dprintf("listening on socket\n");
		dprintf("sending on socket\n");
		addsock(res, &(struct socklist){
			.sl_socket = s,
			.sl_peer = pe,
			.sl_recv = sl_recv
		});
	}
	freeaddrinfo(res0);

	return(error);
}

static void
increase_rcvbuf(int fd)
{
	socklen_t len;

	if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &len,
	    &(socklen_t){sizeof(len)}) == 0) {
		if (len < RCVBUF_MINSIZE) {
			len = RCVBUF_MINSIZE;
			setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &len, sizeof(len));
		}
	}
}
