/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1985, 1988, 1990, 1992, 1993, 1994
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

#if 0
#ifndef lint
static char copyright[] =
"@(#) Copyright (c) 1985, 1988, 1990, 1992, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)ftpd.c	8.4 (Berkeley) 4/16/94";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * FTP server.
 */
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define	FTP_NAMES
#include <arpa/ftp.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>
#include <opie.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <libutil.h>
#ifdef	LOGIN_CAP
#include <login_cap.h>
#endif

#ifdef USE_PAM
#include <security/pam_appl.h>
#endif

#include "blacklist_client.h"
#include "pathnames.h"
#include "extern.h"

#include <stdarg.h>

static char version[] = "Version 6.00LS";
#undef main

union sockunion ctrl_addr;
union sockunion data_source;
union sockunion data_dest;
union sockunion his_addr;
union sockunion pasv_addr;

int	daemon_mode;
int	data;
int	dataport;
int	hostinfo = 1;	/* print host-specific info in messages */
int	logged_in;
struct	passwd *pw;
char	*homedir;
int	ftpdebug;
int	timeout = 900;    /* timeout after 15 minutes of inactivity */
int	maxtimeout = 7200;/* don't allow idle time to be set beyond 2 hours */
int	logging;
int	restricted_data_ports = 1;
int	paranoid = 1;	  /* be extra careful about security */
int	anon_only = 0;    /* Only anonymous ftp allowed */
int	assumeutf8 = 0;   /* Assume that server file names are in UTF-8 */
int	guest;
int	dochroot;
char	*chrootdir;
int	dowtmp = 1;
int	stats;
int	statfd = -1;
int	type;
int	form;
int	stru;			/* avoid C keyword */
int	mode;
int	usedefault = 1;		/* for data transfers */
int	pdata = -1;		/* for passive mode */
int	readonly = 0;		/* Server is in readonly mode.	*/
int	noepsv = 0;		/* EPSV command is disabled.	*/
int	noretr = 0;		/* RETR command is disabled.	*/
int	noguestretr = 0;	/* RETR command is disabled for anon users. */
int	noguestmkd = 0;		/* MKD command is disabled for anon users. */
int	noguestmod = 1;		/* anon users may not modify existing files. */
int	use_blacklist = 0;

off_t	file_size;
off_t	byte_count;
#if !defined(CMASK) || CMASK == 0
#undef CMASK
#define CMASK 027
#endif
int	defumask = CMASK;		/* default umask value */
char	tmpline[7];
char	*hostname;
int	epsvall = 0;

#ifdef VIRTUAL_HOSTING
char	*ftpuser;

static struct ftphost {
	struct ftphost	*next;
	struct addrinfo *hostinfo;
	char		*hostname;
	char		*anonuser;
	char		*statfile;
	char		*welcome;
	char		*loginmsg;
} *thishost, *firsthost;

#endif
char	remotehost[NI_MAXHOST];
char	*ident = NULL;

static char	wtmpid[20];

#ifdef USE_PAM
static int	auth_pam(struct passwd**, const char*);
pam_handle_t	*pamh = NULL;
#endif

static struct opie	opiedata;
static char		opieprompt[OPIE_CHALLENGE_MAX+1];
static int		pwok;

char	*pid_file = NULL; /* means default location to pidfile(3) */

/*
 * Limit number of pathnames that glob can return.
 * A limit of 0 indicates the number of pathnames is unlimited.
 */
#define MAXGLOBARGS	16384
#

/*
 * Timeout intervals for retrying connections
 * to hosts that don't accept PORT cmds.  This
 * is a kludge, but given the problems with TCP...
 */
#define	SWAITMAX	90	/* wait at most 90 seconds */
#define	SWAITINT	5	/* interval between retries */

int	swaitmax = SWAITMAX;
int	swaitint = SWAITINT;

#ifdef SETPROCTITLE
#ifdef OLD_SETPROCTITLE
char	**Argv = NULL;		/* pointer to argument vector */
char	*LastArgv = NULL;	/* end of argv */
#endif /* OLD_SETPROCTITLE */
char	proctitle[LINE_MAX];	/* initial part of title */
#endif /* SETPROCTITLE */

#define LOGCMD(cmd, file)		logcmd((cmd), (file), NULL, -1)
#define LOGCMD2(cmd, file1, file2)	logcmd((cmd), (file1), (file2), -1)
#define LOGBYTES(cmd, file, cnt)	logcmd((cmd), (file), NULL, (cnt))

static	volatile sig_atomic_t recvurg;
static	int transflag;		/* NB: for debugging only */

#define STARTXFER	flagxfer(1)
#define ENDXFER		flagxfer(0)

#define START_UNSAFE	maskurg(1)
#define END_UNSAFE	maskurg(0)

/* It's OK to put an `else' clause after this macro. */
#define CHECKOOB(action)						\
	if (recvurg) {							\
		recvurg = 0;						\
		if (myoob()) {						\
			ENDXFER;					\
			action;						\
		}							\
	}

#ifdef VIRTUAL_HOSTING
static void	 inithosts(int);
static void	 selecthost(union sockunion *);
#endif
static void	 ack(char *);
static void	 sigurg(int);
static void	 maskurg(int);
static void	 flagxfer(int);
static int	 myoob(void);
static int	 checkuser(char *, char *, int, char **, int *);
static FILE	*dataconn(char *, off_t, char *);
static void	 dolog(struct sockaddr *);
static void	 end_login(void);
static FILE	*getdatasock(char *);
static int	 guniquefd(char *, char **);
static void	 lostconn(int);
static void	 sigquit(int);
static int	 receive_data(FILE *, FILE *);
static int	 send_data(FILE *, FILE *, size_t, off_t, int);
static struct passwd *
		 sgetpwnam(char *);
static char	*sgetsave(char *);
static void	 reapchild(int);
static void	 appendf(char **, char *, ...) __printflike(2, 3);
static void	 logcmd(char *, char *, char *, off_t);
static void      logxfer(char *, off_t, time_t);
static char	*doublequote(char *);
static int	*socksetup(int, char *, const char *);

int
main(int argc, char *argv[], char **envp)
{
	socklen_t addrlen;
	int ch, on = 1, tos, s = STDIN_FILENO;
	char *cp, line[LINE_MAX];
	FILE *fd;
	char	*bindname = NULL;
	const char *bindport = "ftp";
	int	family = AF_UNSPEC;
	struct sigaction sa;

	tzset();		/* in case no timezone database in ~ftp */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

#ifdef OLD_SETPROCTITLE
	/*
	 *  Save start and extent of argv for setproctitle.
	 */
	Argv = argv;
	while (*envp)
		envp++;
	LastArgv = envp[-1] + strlen(envp[-1]);
#endif /* OLD_SETPROCTITLE */

	/*
	 * Prevent diagnostic messages from appearing on stderr.
	 * We run as a daemon or from inetd; in both cases, there's
	 * more reason in logging to syslog.
	 */
	(void) freopen(_PATH_DEVNULL, "w", stderr);
	opterr = 0;

	/*
	 * LOG_NDELAY sets up the logging connection immediately,
	 * necessary for anonymous ftp's that chroot and can't do it later.
	 */
	openlog("ftpd", LOG_PID | LOG_NDELAY, LOG_FTP);

	while ((ch = getopt(argc, argv,
	                    "468a:ABdDEhlmMoOp:P:rRSt:T:u:UvW")) != -1) {
		switch (ch) {
		case '4':
			family = (family == AF_INET6) ? AF_UNSPEC : AF_INET;
			break;

		case '6':
			family = (family == AF_INET) ? AF_UNSPEC : AF_INET6;
			break;

		case '8':
			assumeutf8 = 1;
			break;

		case 'a':
			bindname = optarg;
			break;

		case 'A':
			anon_only = 1;
			break;

		case 'B':
#ifdef USE_BLACKLIST
			use_blacklist = 1;
#else
			syslog(LOG_WARNING, "not compiled with USE_BLACKLIST support");
#endif
			break;

		case 'd':
			ftpdebug++;
			break;

		case 'D':
			daemon_mode++;
			break;

		case 'E':
			noepsv = 1;
			break;

		case 'h':
			hostinfo = 0;
			break;

		case 'l':
			logging++;	/* > 1 == extra logging */
			break;

		case 'm':
			noguestmod = 0;
			break;

		case 'M':
			noguestmkd = 1;
			break;

		case 'o':
			noretr = 1;
			break;

		case 'O':
			noguestretr = 1;
			break;

		case 'p':
			pid_file = optarg;
			break;

		case 'P':
			bindport = optarg;
			break;

		case 'r':
			readonly = 1;
			break;

		case 'R':
			paranoid = 0;
			break;

		case 'S':
			stats++;
			break;

		case 't':
			timeout = atoi(optarg);
			if (maxtimeout < timeout)
				maxtimeout = timeout;
			break;

		case 'T':
			maxtimeout = atoi(optarg);
			if (timeout > maxtimeout)
				timeout = maxtimeout;
			break;

		case 'u':
		    {
			long val = 0;

			val = strtol(optarg, &optarg, 8);
			if (*optarg != '\0' || val < 0)
				syslog(LOG_WARNING, "bad value for -u");
			else
				defumask = val;
			break;
		    }
		case 'U':
			restricted_data_ports = 0;
			break;

		case 'v':
			ftpdebug++;
			break;

		case 'W':
			dowtmp = 0;
			break;

		default:
			syslog(LOG_WARNING, "unknown flag -%c ignored", optopt);
			break;
		}
	}

	/* handle filesize limit gracefully */
	sa.sa_handler = SIG_IGN;
	(void)sigaction(SIGXFSZ, &sa, NULL);

	if (daemon_mode) {
		int *ctl_sock, fd, maxfd = -1, nfds, i;
		fd_set defreadfds, readfds;
		pid_t pid;
		struct pidfh *pfh;

		if ((pfh = pidfile_open(pid_file, 0600, &pid)) == NULL) {
			if (errno == EEXIST) {
				syslog(LOG_ERR, "%s already running, pid %d",
				       getprogname(), (int)pid);
				exit(1);
			}
			syslog(LOG_WARNING, "pidfile_open: %m");
		}

		/*
		 * Detach from parent.
		 */
		if (daemon(1, 1) < 0) {
			syslog(LOG_ERR, "failed to become a daemon");
			exit(1);
		}

		if (pfh != NULL && pidfile_write(pfh) == -1)
			syslog(LOG_WARNING, "pidfile_write: %m");

		sa.sa_handler = reapchild;
		(void)sigaction(SIGCHLD, &sa, NULL);

#ifdef VIRTUAL_HOSTING
		inithosts(family);
#endif

		/*
		 * Open a socket, bind it to the FTP port, and start
		 * listening.
		 */
		ctl_sock = socksetup(family, bindname, bindport);
		if (ctl_sock == NULL)
			exit(1);

		FD_ZERO(&defreadfds);
		for (i = 1; i <= *ctl_sock; i++) {
			FD_SET(ctl_sock[i], &defreadfds);
			if (listen(ctl_sock[i], 32) < 0) {
				syslog(LOG_ERR, "control listen: %m");
				exit(1);
			}
			if (maxfd < ctl_sock[i])
				maxfd = ctl_sock[i];
		}

		/*
		 * Loop forever accepting connection requests and forking off
		 * children to handle them.
		 */
		while (1) {
			FD_COPY(&defreadfds, &readfds);
			nfds = select(maxfd + 1, &readfds, NULL, NULL, 0);
			if (nfds <= 0) {
				if (nfds < 0 && errno != EINTR)
					syslog(LOG_WARNING, "select: %m");
				continue;
			}

			pid = -1;
                        for (i = 1; i <= *ctl_sock; i++)
				if (FD_ISSET(ctl_sock[i], &readfds)) {
					addrlen = sizeof(his_addr);
					fd = accept(ctl_sock[i],
					    (struct sockaddr *)&his_addr,
					    &addrlen);
					if (fd == -1) {
						syslog(LOG_WARNING,
						       "accept: %m");
						continue;
					}
					switch (pid = fork()) {
					case 0:
						/* child */
						(void) dup2(fd, s);
						(void) dup2(fd, STDOUT_FILENO);
						(void) close(fd);
						for (i = 1; i <= *ctl_sock; i++)
							close(ctl_sock[i]);
						if (pfh != NULL)
							pidfile_close(pfh);
						goto gotchild;
					case -1:
						syslog(LOG_WARNING, "fork: %m");
						/* FALLTHROUGH */
					default:
						close(fd);
					}
				}
		}
	} else {
		addrlen = sizeof(his_addr);
		if (getpeername(s, (struct sockaddr *)&his_addr, &addrlen) < 0) {
			syslog(LOG_ERR, "getpeername (%s): %m",argv[0]);
			exit(1);
		}

#ifdef VIRTUAL_HOSTING
		if (his_addr.su_family == AF_INET6 &&
		    IN6_IS_ADDR_V4MAPPED(&his_addr.su_sin6.sin6_addr))
			family = AF_INET;
		else
			family = his_addr.su_family;
		inithosts(family);
#endif
	}

gotchild:
	sa.sa_handler = SIG_DFL;
	(void)sigaction(SIGCHLD, &sa, NULL);

	sa.sa_handler = sigurg;
	sa.sa_flags = 0;		/* don't restart syscalls for SIGURG */
	(void)sigaction(SIGURG, &sa, NULL);

	sigfillset(&sa.sa_mask);	/* block all signals in handler */
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = sigquit;
	(void)sigaction(SIGHUP, &sa, NULL);
	(void)sigaction(SIGINT, &sa, NULL);
	(void)sigaction(SIGQUIT, &sa, NULL);
	(void)sigaction(SIGTERM, &sa, NULL);

	sa.sa_handler = lostconn;
	(void)sigaction(SIGPIPE, &sa, NULL);

	addrlen = sizeof(ctrl_addr);
	if (getsockname(s, (struct sockaddr *)&ctrl_addr, &addrlen) < 0) {
		syslog(LOG_ERR, "getsockname (%s): %m",argv[0]);
		exit(1);
	}
	dataport = ntohs(ctrl_addr.su_port) - 1; /* as per RFC 959 */
#ifdef VIRTUAL_HOSTING
	/* select our identity from virtual host table */
	selecthost(&ctrl_addr);
#endif
#ifdef IP_TOS
	if (ctrl_addr.su_family == AF_INET)
      {
	tos = IPTOS_LOWDELAY;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0)
		syslog(LOG_WARNING, "control setsockopt (IP_TOS): %m");
      }
#endif
	/*
	 * Disable Nagle on the control channel so that we don't have to wait
	 * for peer's ACK before issuing our next reply.
	 */
	if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "control setsockopt (TCP_NODELAY): %m");

	data_source.su_port = htons(ntohs(ctrl_addr.su_port) - 1);

	(void)snprintf(wtmpid, sizeof(wtmpid), "%xftpd", getpid());

	/* Try to handle urgent data inline */
#ifdef SO_OOBINLINE
	if (setsockopt(s, SOL_SOCKET, SO_OOBINLINE, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "control setsockopt (SO_OOBINLINE): %m");
#endif

#ifdef	F_SETOWN
	if (fcntl(s, F_SETOWN, getpid()) == -1)
		syslog(LOG_ERR, "fcntl F_SETOWN: %m");
#endif
	dolog((struct sockaddr *)&his_addr);
	/*
	 * Set up default state
	 */
	data = -1;
	type = TYPE_A;
	form = FORM_N;
	stru = STRU_F;
	mode = MODE_S;
	tmpline[0] = '\0';

	/* If logins are disabled, print out the message. */
	if ((fd = fopen(_PATH_NOLOGIN,"r")) != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(530, "%s", line);
		}
		(void) fflush(stdout);
		(void) fclose(fd);
		reply(530, "System not available.");
		exit(0);
	}
#ifdef VIRTUAL_HOSTING
	fd = fopen(thishost->welcome, "r");
#else
	fd = fopen(_PATH_FTPWELCOME, "r");
#endif
	if (fd != NULL) {
		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(220, "%s", line);
		}
		(void) fflush(stdout);
		(void) fclose(fd);
		/* reply(220,) must follow */
	}
#ifndef VIRTUAL_HOSTING
	if ((hostname = malloc(MAXHOSTNAMELEN)) == NULL)
		fatalerror("Ran out of memory.");
	if (gethostname(hostname, MAXHOSTNAMELEN - 1) < 0)
		hostname[0] = '\0';
	hostname[MAXHOSTNAMELEN - 1] = '\0';
#endif
	if (hostinfo)
		reply(220, "%s FTP server (%s) ready.", hostname, version);
	else
		reply(220, "FTP server ready.");
	BLACKLIST_INIT();
	for (;;)
		(void) yyparse();
	/* NOTREACHED */
}

static void
lostconn(int signo)
{

	if (ftpdebug)
		syslog(LOG_DEBUG, "lost connection");
	dologout(1);
}

static void
sigquit(int signo)
{

	syslog(LOG_ERR, "got signal %d", signo);
	dologout(1);
}

#ifdef VIRTUAL_HOSTING
/*
 * read in virtual host tables (if they exist)
 */

static void
inithosts(int family)
{
	int insert;
	size_t len;
	FILE *fp;
	char *cp, *mp, *line;
	char *hostname;
	char *vhost, *anonuser, *statfile, *welcome, *loginmsg;
	struct ftphost *hrp, *lhrp;
	struct addrinfo hints, *res, *ai;

	/*
	 * Fill in the default host information
	 */
	if ((hostname = malloc(MAXHOSTNAMELEN)) == NULL)
		fatalerror("Ran out of memory.");
	if (gethostname(hostname, MAXHOSTNAMELEN - 1) < 0)
		hostname[0] = '\0';
	hostname[MAXHOSTNAMELEN - 1] = '\0';
	if ((hrp = malloc(sizeof(struct ftphost))) == NULL)
		fatalerror("Ran out of memory.");
	hrp->hostname = hostname;
	hrp->hostinfo = NULL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(hrp->hostname, NULL, &hints, &res) == 0)
		hrp->hostinfo = res;
	hrp->statfile = _PATH_FTPDSTATFILE;
	hrp->welcome  = _PATH_FTPWELCOME;
	hrp->loginmsg = _PATH_FTPLOGINMESG;
	hrp->anonuser = "ftp";
	hrp->next = NULL;
	thishost = firsthost = lhrp = hrp;
	if ((fp = fopen(_PATH_FTPHOSTS, "r")) != NULL) {
		int addrsize, gothost;
		void *addr;
		struct hostent *hp;

		while ((line = fgetln(fp, &len)) != NULL) {
			int	i, hp_error;

			/* skip comments */
			if (line[0] == '#')
				continue;
			if (line[len - 1] == '\n') {
				line[len - 1] = '\0';
				mp = NULL;
			} else {
				if ((mp = malloc(len + 1)) == NULL)
					fatalerror("Ran out of memory.");
				memcpy(mp, line, len);
				mp[len] = '\0';
				line = mp;
			}
			cp = strtok(line, " \t");
			/* skip empty lines */
			if (cp == NULL)
				goto nextline;
			vhost = cp;

			/* set defaults */
			anonuser = "ftp";
			statfile = _PATH_FTPDSTATFILE;
			welcome  = _PATH_FTPWELCOME;
			loginmsg = _PATH_FTPLOGINMESG;

			/*
			 * Preparse the line so we can use its info
			 * for all the addresses associated with
			 * the virtual host name.
			 * Field 0, the virtual host name, is special:
			 * it's already parsed off and will be strdup'ed
			 * later, after we know its canonical form.
			 */
			for (i = 1; i < 5 && (cp = strtok(NULL, " \t")); i++)
				if (*cp != '-' && (cp = strdup(cp)))
					switch (i) {
					case 1:	/* anon user permissions */
						anonuser = cp;
						break;
					case 2: /* statistics file */
						statfile = cp;
						break;
					case 3: /* welcome message */
						welcome  = cp;
						break;
					case 4: /* login message */
						loginmsg = cp;
						break;
					default: /* programming error */
						abort();
						/* NOTREACHED */
					}

			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = family;
			hints.ai_socktype = SOCK_STREAM;
			if (getaddrinfo(vhost, NULL, &hints, &res) != 0)
				goto nextline;
			for (ai = res; ai != NULL && ai->ai_addr != NULL;
			     ai = ai->ai_next) {

			gothost = 0;
			for (hrp = firsthost; hrp != NULL; hrp = hrp->next) {
				struct addrinfo *hi;

				for (hi = hrp->hostinfo; hi != NULL;
				     hi = hi->ai_next)
					if (hi->ai_addrlen == ai->ai_addrlen &&
					    memcmp(hi->ai_addr,
						   ai->ai_addr,
						   ai->ai_addr->sa_len) == 0) {
						gothost++;
						break;
					}
				if (gothost)
					break;
			}
			if (hrp == NULL) {
				if ((hrp = malloc(sizeof(struct ftphost))) == NULL)
					goto nextline;
				hrp->hostname = NULL;
				insert = 1;
			} else {
				if (hrp->hostinfo && hrp->hostinfo != res)
					freeaddrinfo(hrp->hostinfo);
				insert = 0; /* host already in the chain */
			}
			hrp->hostinfo = res;

			/*
			 * determine hostname to use.
			 * force defined name if there is a valid alias
			 * otherwise fallback to primary hostname
			 */
			/* XXX: getaddrinfo() can't do alias check */
			switch(hrp->hostinfo->ai_family) {
			case AF_INET:
				addr = &((struct sockaddr_in *)hrp->hostinfo->ai_addr)->sin_addr;
				addrsize = sizeof(struct in_addr);
				break;
			case AF_INET6:
				addr = &((struct sockaddr_in6 *)hrp->hostinfo->ai_addr)->sin6_addr;
				addrsize = sizeof(struct in6_addr);
				break;
			default:
				/* should not reach here */
				freeaddrinfo(hrp->hostinfo);
				if (insert)
					free(hrp); /*not in chain, can free*/
				else
					hrp->hostinfo = NULL; /*mark as blank*/
				goto nextline;
				/* NOTREACHED */
			}
			if ((hp = getipnodebyaddr(addr, addrsize,
						  hrp->hostinfo->ai_family,
						  &hp_error)) != NULL) {
				if (strcmp(vhost, hp->h_name) != 0) {
					if (hp->h_aliases == NULL)
						vhost = hp->h_name;
					else {
						i = 0;
						while (hp->h_aliases[i] &&
						       strcmp(vhost, hp->h_aliases[i]) != 0)
							++i;
						if (hp->h_aliases[i] == NULL)
							vhost = hp->h_name;
					}
				}
			}
			if (hrp->hostname &&
			    strcmp(hrp->hostname, vhost) != 0) {
				free(hrp->hostname);
				hrp->hostname = NULL;
			}
			if (hrp->hostname == NULL &&
			    (hrp->hostname = strdup(vhost)) == NULL) {
				freeaddrinfo(hrp->hostinfo);
				hrp->hostinfo = NULL; /* mark as blank */
				if (hp)
					freehostent(hp);
				goto nextline;
			}
			hrp->anonuser = anonuser;
			hrp->statfile = statfile;
			hrp->welcome  = welcome;
			hrp->loginmsg = loginmsg;
			if (insert) {
				hrp->next  = NULL;
				lhrp->next = hrp;
				lhrp = hrp;
			}
			if (hp)
				freehostent(hp);
		      }
nextline:
			if (mp)
				free(mp);
		}
		(void) fclose(fp);
	}
}

static void
selecthost(union sockunion *su)
{
	struct ftphost	*hrp;
	u_int16_t port;
#ifdef INET6
	struct in6_addr *mapped_in6 = NULL;
#endif
	struct addrinfo *hi;

#ifdef INET6
	/*
	 * XXX IPv4 mapped IPv6 addr consideraton,
	 * specified in rfc2373.
	 */
	if (su->su_family == AF_INET6 &&
	    IN6_IS_ADDR_V4MAPPED(&su->su_sin6.sin6_addr))
		mapped_in6 = &su->su_sin6.sin6_addr;
#endif

	hrp = thishost = firsthost;	/* default */
	port = su->su_port;
	su->su_port = 0;
	while (hrp != NULL) {
	    for (hi = hrp->hostinfo; hi != NULL; hi = hi->ai_next) {
		if (memcmp(su, hi->ai_addr, hi->ai_addrlen) == 0) {
			thishost = hrp;
			goto found;
		}
#ifdef INET6
		/* XXX IPv4 mapped IPv6 addr consideraton */
		if (hi->ai_addr->sa_family == AF_INET && mapped_in6 != NULL &&
		    (memcmp(&mapped_in6->s6_addr[12],
			    &((struct sockaddr_in *)hi->ai_addr)->sin_addr,
			    sizeof(struct in_addr)) == 0)) {
			thishost = hrp;
			goto found;
		}
#endif
	    }
	    hrp = hrp->next;
	}
found:
	su->su_port = port;
	/* setup static variables as appropriate */
	hostname = thishost->hostname;
	ftpuser = thishost->anonuser;
}
#endif

/*
 * Helper function for sgetpwnam().
 */
static char *
sgetsave(char *s)
{
	char *new = malloc(strlen(s) + 1);

	if (new == NULL) {
		reply(421, "Ran out of memory.");
		dologout(1);
		/* NOTREACHED */
	}
	(void) strcpy(new, s);
	return (new);
}

/*
 * Save the result of a getpwnam.  Used for USER command, since
 * the data returned must not be clobbered by any other command
 * (e.g., globbing).
 * NB: The data returned by sgetpwnam() will remain valid until
 * the next call to this function.  Its difference from getpwnam()
 * is that sgetpwnam() is known to be called from ftpd code only.
 */
static struct passwd *
sgetpwnam(char *name)
{
	static struct passwd save;
	struct passwd *p;

	if ((p = getpwnam(name)) == NULL)
		return (p);
	if (save.pw_name) {
		free(save.pw_name);
		free(save.pw_passwd);
		free(save.pw_class);
		free(save.pw_gecos);
		free(save.pw_dir);
		free(save.pw_shell);
	}
	save = *p;
	save.pw_name = sgetsave(p->pw_name);
	save.pw_passwd = sgetsave(p->pw_passwd);
	save.pw_class = sgetsave(p->pw_class);
	save.pw_gecos = sgetsave(p->pw_gecos);
	save.pw_dir = sgetsave(p->pw_dir);
	save.pw_shell = sgetsave(p->pw_shell);
	return (&save);
}

static int login_attempts;	/* number of failed login attempts */
static int askpasswd;		/* had user command, ask for passwd */
static char curname[MAXLOGNAME];	/* current USER name */

/*
 * USER command.
 * Sets global passwd pointer pw if named account exists and is acceptable;
 * sets askpasswd if a PASS command is expected.  If logged in previously,
 * need to reset state.  If name is "ftp" or "anonymous", the name is not in
 * _PATH_FTPUSERS, and ftp account exists, set guest and pw, then just return.
 * If account doesn't exist, ask for passwd anyway.  Otherwise, check user
 * requesting login privileges.  Disallow anyone who does not have a standard
 * shell as returned by getusershell().  Disallow anyone mentioned in the file
 * _PATH_FTPUSERS to allow people such as root and uucp to be avoided.
 */
void
user(char *name)
{
	int ecode;
	char *cp, *shell;

	if (logged_in) {
		if (guest) {
			reply(530, "Can't change user from guest login.");
			return;
		} else if (dochroot) {
			reply(530, "Can't change user from chroot user.");
			return;
		}
		end_login();
	}

	guest = 0;
#ifdef VIRTUAL_HOSTING
	pw = sgetpwnam(thishost->anonuser);
#else
	pw = sgetpwnam("ftp");
#endif
	if (strcmp(name, "ftp") == 0 || strcmp(name, "anonymous") == 0) {
		if (checkuser(_PATH_FTPUSERS, "ftp", 0, NULL, &ecode) ||
		    (ecode != 0 && ecode != ENOENT))
			reply(530, "User %s access denied.", name);
		else if (checkuser(_PATH_FTPUSERS, "anonymous", 0, NULL, &ecode) ||
		    (ecode != 0 && ecode != ENOENT))
			reply(530, "User %s access denied.", name);
		else if (pw != NULL) {
			guest = 1;
			askpasswd = 1;
			reply(331,
			"Guest login ok, send your email address as password.");
		} else
			reply(530, "User %s unknown.", name);
		if (!askpasswd && logging)
			syslog(LOG_NOTICE,
			    "ANONYMOUS FTP LOGIN REFUSED FROM %s", remotehost);
		return;
	}
	if (anon_only != 0) {
		reply(530, "Sorry, only anonymous ftp allowed.");
		return;
	}
		
	if ((pw = sgetpwnam(name))) {
		if ((shell = pw->pw_shell) == NULL || *shell == 0)
			shell = _PATH_BSHELL;
		setusershell();
		while ((cp = getusershell()) != NULL)
			if (strcmp(cp, shell) == 0)
				break;
		endusershell();

		if (cp == NULL || 
		    (checkuser(_PATH_FTPUSERS, name, 1, NULL, &ecode) ||
		    (ecode != 0 && ecode != ENOENT))) {
			reply(530, "User %s access denied.", name);
			if (logging)
				syslog(LOG_NOTICE,
				    "FTP LOGIN REFUSED FROM %s, %s",
				    remotehost, name);
			pw = NULL;
			return;
		}
	}
	if (logging)
		strlcpy(curname, name, sizeof(curname));

	pwok = 0;
#ifdef USE_PAM
	/* XXX Kluge! The conversation mechanism needs to be fixed. */
#endif
	if (opiechallenge(&opiedata, name, opieprompt) == 0) {
		pwok = (pw != NULL) &&
		       opieaccessfile(remotehost) &&
		       opiealways(pw->pw_dir);
		reply(331, "Response to %s %s for %s.",
		      opieprompt, pwok ? "requested" : "required", name);
	} else {
		pwok = 1;
		reply(331, "Password required for %s.", name);
	}
	askpasswd = 1;
	/*
	 * Delay before reading passwd after first failed
	 * attempt to slow down passwd-guessing programs.
	 */
	if (login_attempts)
		sleep(login_attempts);
}

/*
 * Check if a user is in the file "fname",
 * return a pointer to a malloc'd string with the rest
 * of the matching line in "residue" if not NULL.
 */
static int
checkuser(char *fname, char *name, int pwset, char **residue, int *ecode)
{
	FILE *fd;
	int found = 0;
	size_t len;
	char *line, *mp, *p;

	if (ecode != NULL)
		*ecode = 0;
	if ((fd = fopen(fname, "r")) != NULL) {
		while (!found && (line = fgetln(fd, &len)) != NULL) {
			/* skip comments */
			if (line[0] == '#')
				continue;
			if (line[len - 1] == '\n') {
				line[len - 1] = '\0';
				mp = NULL;
			} else {
				if ((mp = malloc(len + 1)) == NULL)
					fatalerror("Ran out of memory.");
				memcpy(mp, line, len);
				mp[len] = '\0';
				line = mp;
			}
			/* avoid possible leading and trailing whitespace */
			p = strtok(line, " \t");
			/* skip empty lines */
			if (p == NULL)
				goto nextline;
			/*
			 * if first chr is '@', check group membership
			 */
			if (p[0] == '@') {
				int i = 0;
				struct group *grp;

				if (p[1] == '\0') /* single @ matches anyone */
					found = 1;
				else {
					if ((grp = getgrnam(p+1)) == NULL)
						goto nextline;
					/*
					 * Check user's default group
					 */
					if (pwset && grp->gr_gid == pw->pw_gid)
						found = 1;
					/*
					 * Check supplementary groups
					 */
					while (!found && grp->gr_mem[i])
						found = strcmp(name,
							grp->gr_mem[i++])
							== 0;
				}
			}
			/*
			 * Otherwise, just check for username match
			 */
			else
				found = strcmp(p, name) == 0;
			/*
			 * Save the rest of line to "residue" if matched
			 */
			if (found && residue) {
				if ((p = strtok(NULL, "")) != NULL)
					p += strspn(p, " \t");
				if (p && *p) {
				 	if ((*residue = strdup(p)) == NULL)
						fatalerror("Ran out of memory.");
				} else
					*residue = NULL;
			}
nextline:
			if (mp)
				free(mp);
		}
		(void) fclose(fd);
	} else if (ecode != NULL)
		*ecode = errno;
	return (found);
}

/*
 * Terminate login as previous user, if any, resetting state;
 * used when USER command is given or login fails.
 */
static void
end_login(void)
{
#ifdef USE_PAM
	int e;
#endif

	(void) seteuid(0);
#ifdef	LOGIN_CAP
	setusercontext(NULL, getpwuid(0), 0, LOGIN_SETALL & ~(LOGIN_SETLOGIN |
		       LOGIN_SETUSER | LOGIN_SETGROUP | LOGIN_SETPATH |
		       LOGIN_SETENV));
#endif
	if (logged_in && dowtmp)
		ftpd_logwtmp(wtmpid, NULL, NULL);
	pw = NULL;
#ifdef USE_PAM
	if (pamh) {
		if ((e = pam_setcred(pamh, PAM_DELETE_CRED)) != PAM_SUCCESS)
			syslog(LOG_ERR, "pam_setcred: %s", pam_strerror(pamh, e));
		if ((e = pam_close_session(pamh,0)) != PAM_SUCCESS)
			syslog(LOG_ERR, "pam_close_session: %s", pam_strerror(pamh, e));
		if ((e = pam_end(pamh, e)) != PAM_SUCCESS)
			syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e));
		pamh = NULL;
	}
#endif
	logged_in = 0;
	guest = 0;
	dochroot = 0;
}

#ifdef USE_PAM

/*
 * the following code is stolen from imap-uw PAM authentication module and
 * login.c
 */
#define COPY_STRING(s) (s ? strdup(s) : NULL)

struct cred_t {
	const char *uname;		/* user name */
	const char *pass;		/* password */
};
typedef struct cred_t cred_t;

static int
auth_conv(int num_msg, const struct pam_message **msg,
	  struct pam_response **resp, void *appdata)
{
	int i;
	cred_t *cred = (cred_t *) appdata;
	struct pam_response *reply;

	reply = calloc(num_msg, sizeof *reply);
	if (reply == NULL)
		return PAM_BUF_ERR;

	for (i = 0; i < num_msg; i++) {
		switch (msg[i]->msg_style) {
		case PAM_PROMPT_ECHO_ON:	/* assume want user name */
			reply[i].resp_retcode = PAM_SUCCESS;
			reply[i].resp = COPY_STRING(cred->uname);
			/* PAM frees resp. */
			break;
		case PAM_PROMPT_ECHO_OFF:	/* assume want password */
			reply[i].resp_retcode = PAM_SUCCESS;
			reply[i].resp = COPY_STRING(cred->pass);
			/* PAM frees resp. */
			break;
		case PAM_TEXT_INFO:
		case PAM_ERROR_MSG:
			reply[i].resp_retcode = PAM_SUCCESS;
			reply[i].resp = NULL;
			break;
		default:			/* unknown message style */
			free(reply);
			return PAM_CONV_ERR;
		}
	}

	*resp = reply;
	return PAM_SUCCESS;
}

/*
 * Attempt to authenticate the user using PAM.  Returns 0 if the user is
 * authenticated, or 1 if not authenticated.  If some sort of PAM system
 * error occurs (e.g., the "/etc/pam.conf" file is missing) then this
 * function returns -1.  This can be used as an indication that we should
 * fall back to a different authentication mechanism.
 */
static int
auth_pam(struct passwd **ppw, const char *pass)
{
	const char *tmpl_user;
	const void *item;
	int rval;
	int e;
	cred_t auth_cred = { (*ppw)->pw_name, pass };
	struct pam_conv conv = { &auth_conv, &auth_cred };

	e = pam_start("ftpd", (*ppw)->pw_name, &conv, &pamh);
	if (e != PAM_SUCCESS) {
		/*
		 * In OpenPAM, it's OK to pass NULL to pam_strerror()
		 * if context creation has failed in the first place.
		 */
		syslog(LOG_ERR, "pam_start: %s", pam_strerror(NULL, e));
		return -1;
	}

	e = pam_set_item(pamh, PAM_RHOST, remotehost);
	if (e != PAM_SUCCESS) {
		syslog(LOG_ERR, "pam_set_item(PAM_RHOST): %s",
			pam_strerror(pamh, e));
		if ((e = pam_end(pamh, e)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e));
		}
		pamh = NULL;
		return -1;
	}

	e = pam_authenticate(pamh, 0);
	switch (e) {
	case PAM_SUCCESS:
		/*
		 * With PAM we support the concept of a "template"
		 * user.  The user enters a login name which is
		 * authenticated by PAM, usually via a remote service
		 * such as RADIUS or TACACS+.  If authentication
		 * succeeds, a different but related "template" name
		 * is used for setting the credentials, shell, and
		 * home directory.  The name the user enters need only
		 * exist on the remote authentication server, but the
		 * template name must be present in the local password
		 * database.
		 *
		 * This is supported by two various mechanisms in the
		 * individual modules.  However, from the application's
		 * point of view, the template user is always passed
		 * back as a changed value of the PAM_USER item.
		 */
		if ((e = pam_get_item(pamh, PAM_USER, &item)) ==
		    PAM_SUCCESS) {
			tmpl_user = (const char *) item;
			if (strcmp((*ppw)->pw_name, tmpl_user) != 0)
				*ppw = getpwnam(tmpl_user);
		} else
			syslog(LOG_ERR, "Couldn't get PAM_USER: %s",
			    pam_strerror(pamh, e));
		rval = 0;
		break;

	case PAM_AUTH_ERR:
	case PAM_USER_UNKNOWN:
	case PAM_MAXTRIES:
		rval = 1;
		break;

	default:
		syslog(LOG_ERR, "pam_authenticate: %s", pam_strerror(pamh, e));
		rval = -1;
		break;
	}

	if (rval == 0) {
		e = pam_acct_mgmt(pamh, 0);
		if (e != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_acct_mgmt: %s",
						pam_strerror(pamh, e));
			rval = 1;
		}
	}

	if (rval != 0) {
		if ((e = pam_end(pamh, e)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e));
		}
		pamh = NULL;
	}
	return rval;
}

#endif /* USE_PAM */

void
pass(char *passwd)
{
	int rval, ecode;
	FILE *fd;
#ifdef	LOGIN_CAP
	login_cap_t *lc = NULL;
#endif
#ifdef USE_PAM
	int e;
#endif
	char *residue = NULL;
	char *xpasswd;

	if (logged_in || askpasswd == 0) {
		reply(503, "Login with USER first.");
		return;
	}
	askpasswd = 0;
	if (!guest) {		/* "ftp" is only account allowed no password */
		if (pw == NULL) {
			rval = 1;	/* failure below */
			goto skip;
		}
#ifdef USE_PAM
		rval = auth_pam(&pw, passwd);
		if (rval >= 0) {
			opieunlock();
			goto skip;
		}
#endif
		if (opieverify(&opiedata, passwd) == 0)
			xpasswd = pw->pw_passwd;
		else if (pwok) {
			xpasswd = crypt(passwd, pw->pw_passwd);
			if (passwd[0] == '\0' && pw->pw_passwd[0] != '\0')
				xpasswd = ":";
		} else {
			rval = 1;
			goto skip;
		}
		rval = strcmp(pw->pw_passwd, xpasswd);
		if (pw->pw_expire && time(NULL) >= pw->pw_expire)
			rval = 1;	/* failure */
skip:
		/*
		 * If rval == 1, the user failed the authentication check
		 * above.  If rval == 0, either PAM or local authentication
		 * succeeded.
		 */
		if (rval) {
			reply(530, "Login incorrect.");
			BLACKLIST_NOTIFY(BLACKLIST_AUTH_FAIL, STDIN_FILENO, "Login incorrect");
			if (logging) {
				syslog(LOG_NOTICE,
				    "FTP LOGIN FAILED FROM %s",
				    remotehost);
				syslog(LOG_AUTHPRIV | LOG_NOTICE,
				    "FTP LOGIN FAILED FROM %s, %s",
				    remotehost, curname);
			}
			pw = NULL;
			if (login_attempts++ >= 5) {
				syslog(LOG_NOTICE,
				    "repeated login failures from %s",
				    remotehost);
				exit(0);
			}
			return;
		} else {
			BLACKLIST_NOTIFY(BLACKLIST_AUTH_OK, STDIN_FILENO, "Login successful");
		}
	}
	login_attempts = 0;		/* this time successful */
	if (setegid(pw->pw_gid) < 0) {
		reply(550, "Can't set gid.");
		return;
	}
	/* May be overridden by login.conf */
	(void) umask(defumask);
#ifdef	LOGIN_CAP
	if ((lc = login_getpwclass(pw)) != NULL) {
		char	remote_ip[NI_MAXHOST];

		if (getnameinfo((struct sockaddr *)&his_addr, his_addr.su_len,
			remote_ip, sizeof(remote_ip) - 1, NULL, 0,
			NI_NUMERICHOST))
				*remote_ip = 0;
		remote_ip[sizeof(remote_ip) - 1] = 0;
		if (!auth_hostok(lc, remotehost, remote_ip)) {
			syslog(LOG_INFO|LOG_AUTH,
			    "FTP LOGIN FAILED (HOST) as %s: permission denied.",
			    pw->pw_name);
			reply(530, "Permission denied.");
			pw = NULL;
			return;
		}
		if (!auth_timeok(lc, time(NULL))) {
			reply(530, "Login not available right now.");
			pw = NULL;
			return;
		}
	}
	setusercontext(lc, pw, 0, LOGIN_SETALL &
		       ~(LOGIN_SETRESOURCES | LOGIN_SETUSER | LOGIN_SETPATH | LOGIN_SETENV));
#else
	setlogin(pw->pw_name);
	(void) initgroups(pw->pw_name, pw->pw_gid);
#endif

#ifdef USE_PAM
	if (pamh) {
		if ((e = pam_open_session(pamh, 0)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_open_session: %s", pam_strerror(pamh, e));
		} else if ((e = pam_setcred(pamh, PAM_ESTABLISH_CRED)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_setcred: %s", pam_strerror(pamh, e));
		}
	}
#endif

	dochroot =
		checkuser(_PATH_FTPCHROOT, pw->pw_name, 1, &residue, &ecode)
#ifdef	LOGIN_CAP	/* Allow login.conf configuration as well */
		|| login_getcapbool(lc, "ftp-chroot", 0)
#endif
	;
	/*
	 * It is possible that checkuser() failed to open the chroot file.
	 * If this is the case, report that logins are un-available, since we
	 * have no way of checking whether or not the user should be chrooted.
	 * We ignore ENOENT since it is not required that this file be present.
	 */
	if (ecode != 0 && ecode != ENOENT) {
		reply(530, "Login not available right now.");
		return;
	}
	chrootdir = NULL;

	/* Disable wtmp logging when chrooting. */
	if (dochroot || guest)
		dowtmp = 0;
	if (dowtmp)
		ftpd_logwtmp(wtmpid, pw->pw_name,
		    (struct sockaddr *)&his_addr);
	logged_in = 1;

#ifdef	LOGIN_CAP
	setusercontext(lc, pw, 0, LOGIN_SETRESOURCES);
#endif

	if (guest && stats && statfd < 0)
#ifdef VIRTUAL_HOSTING
		statfd = open(thishost->statfile, O_WRONLY|O_APPEND);
#else
		statfd = open(_PATH_FTPDSTATFILE, O_WRONLY|O_APPEND);
#endif
		if (statfd < 0)
			stats = 0;

	/*
	 * For a chrooted local user,
	 * a) see whether ftpchroot(5) specifies a chroot directory,
	 * b) extract the directory pathname from the line,
	 * c) expand it to the absolute pathname if necessary.
	 */
	if (dochroot && residue &&
	    (chrootdir = strtok(residue, " \t")) != NULL) {
		if (chrootdir[0] != '/')
			asprintf(&chrootdir, "%s/%s", pw->pw_dir, chrootdir);
		else
			chrootdir = strdup(chrootdir); /* make it permanent */
		if (chrootdir == NULL)
			fatalerror("Ran out of memory.");
	}
	if (guest || dochroot) {
		/*
		 * If no chroot directory set yet, use the login directory.
		 * Copy it so it can be modified while pw->pw_dir stays intact.
		 */
		if (chrootdir == NULL &&
		    (chrootdir = strdup(pw->pw_dir)) == NULL)
			fatalerror("Ran out of memory.");
		/*
		 * Check for the "/chroot/./home" syntax,
		 * separate the chroot and home directory pathnames.
		 */
		if ((homedir = strstr(chrootdir, "/./")) != NULL) {
			*(homedir++) = '\0';	/* wipe '/' */
			homedir++;		/* skip '.' */
		} else {
			/*
			 * We MUST do a chdir() after the chroot. Otherwise
			 * the old current directory will be accessible as "."
			 * outside the new root!
			 */
			homedir = "/";
		}
		/*
		 * Finally, do chroot()
		 */
		if (chroot(chrootdir) < 0) {
			reply(550, "Can't change root.");
			goto bad;
		}
		__FreeBSD_libc_enter_restricted_mode();
	} else	/* real user w/o chroot */
		homedir = pw->pw_dir;
	/*
	 * Set euid *before* doing chdir() so
	 * a) the user won't be carried to a directory that he couldn't reach
	 *    on his own due to no permission to upper path components,
	 * b) NFS mounted homedirs w/restrictive permissions will be accessible
	 *    (uid 0 has no root power over NFS if not mapped explicitly.)
	 */
	if (seteuid(pw->pw_uid) < 0) {
		reply(550, "Can't set uid.");
		goto bad;
	}
	if (chdir(homedir) < 0) {
		if (guest || dochroot) {
			reply(550, "Can't change to base directory.");
			goto bad;
		} else {
			if (chdir("/") < 0) {
				reply(550, "Root is inaccessible.");
				goto bad;
			}
			lreply(230, "No directory! Logging in with home=/.");
		}
	}

	/*
	 * Display a login message, if it exists.
	 * N.B. reply(230,) must follow the message.
	 */
#ifdef VIRTUAL_HOSTING
	fd = fopen(thishost->loginmsg, "r");
#else
	fd = fopen(_PATH_FTPLOGINMESG, "r");
#endif
	if (fd != NULL) {
		char *cp, line[LINE_MAX];

		while (fgets(line, sizeof(line), fd) != NULL) {
			if ((cp = strchr(line, '\n')) != NULL)
				*cp = '\0';
			lreply(230, "%s", line);
		}
		(void) fflush(stdout);
		(void) fclose(fd);
	}
	if (guest) {
		if (ident != NULL)
			free(ident);
		ident = strdup(passwd);
		if (ident == NULL)
			fatalerror("Ran out of memory.");

		reply(230, "Guest login ok, access restrictions apply.");
#ifdef SETPROCTITLE
#ifdef VIRTUAL_HOSTING
		if (thishost != firsthost)
			snprintf(proctitle, sizeof(proctitle),
				 "%s: anonymous(%s)/%s", remotehost, hostname,
				 passwd);
		else
#endif
			snprintf(proctitle, sizeof(proctitle),
				 "%s: anonymous/%s", remotehost, passwd);
		setproctitle("%s", proctitle);
#endif /* SETPROCTITLE */
		if (logging)
			syslog(LOG_INFO, "ANONYMOUS FTP LOGIN FROM %s, %s",
			    remotehost, passwd);
	} else {
		if (dochroot)
			reply(230, "User %s logged in, "
				   "access restrictions apply.", pw->pw_name);
		else
			reply(230, "User %s logged in.", pw->pw_name);

#ifdef SETPROCTITLE
		snprintf(proctitle, sizeof(proctitle),
			 "%s: user/%s", remotehost, pw->pw_name);
		setproctitle("%s", proctitle);
#endif /* SETPROCTITLE */
		if (logging)
			syslog(LOG_INFO, "FTP LOGIN FROM %s as %s",
			    remotehost, pw->pw_name);
	}
	if (logging && (guest || dochroot))
		syslog(LOG_INFO, "session root changed to %s", chrootdir);
#ifdef	LOGIN_CAP
	login_close(lc);
#endif
	if (residue)
		free(residue);
	return;
bad:
	/* Forget all about it... */
#ifdef	LOGIN_CAP
	login_close(lc);
#endif
	if (residue)
		free(residue);
	end_login();
}

void
retrieve(char *cmd, char *name)
{
	FILE *fin, *dout;
	struct stat st;
	int (*closefunc)(FILE *);
	time_t start;
	char line[BUFSIZ];

	if (cmd == 0) {
		fin = fopen(name, "r"), closefunc = fclose;
		st.st_size = 0;
	} else {
		(void) snprintf(line, sizeof(line), cmd, name);
		name = line;
		fin = ftpd_popen(line, "r"), closefunc = ftpd_pclose;
		st.st_size = -1;
		st.st_blksize = BUFSIZ;
	}
	if (fin == NULL) {
		if (errno != 0) {
			perror_reply(550, name);
			if (cmd == 0) {
				LOGCMD("get", name);
			}
		}
		return;
	}
	byte_count = -1;
	if (cmd == 0) {
		if (fstat(fileno(fin), &st) < 0) {
			perror_reply(550, name);
			goto done;
		}
		if (!S_ISREG(st.st_mode)) {
			/*
			 * Never sending a raw directory is a workaround
			 * for buggy clients that will attempt to RETR
			 * a directory before listing it, e.g., Mozilla.
			 * Preventing a guest from getting irregular files
			 * is a simple security measure.
			 */
			if (S_ISDIR(st.st_mode) || guest) {
				reply(550, "%s: not a plain file.", name);
				goto done;
			}
			st.st_size = -1;
			/* st.st_blksize is set for all descriptor types */
		}
	}
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fin)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
		} else if (lseek(fileno(fin), restart_point, L_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	dout = dataconn(name, st.st_size, "w");
	if (dout == NULL)
		goto done;
	time(&start);
	send_data(fin, dout, st.st_blksize, st.st_size,
		  restart_point == 0 && cmd == 0 && S_ISREG(st.st_mode));
	if (cmd == 0 && guest && stats && byte_count > 0)
		logxfer(name, byte_count, start);
	(void) fclose(dout);
	data = -1;
	pdata = -1;
done:
	if (cmd == 0)
		LOGBYTES("get", name, byte_count);
	(*closefunc)(fin);
}

void
store(char *name, char *mode, int unique)
{
	int fd;
	FILE *fout, *din;
	int (*closefunc)(FILE *);

	if (*mode == 'a') {		/* APPE */
		if (unique) {
			/* Programming error */
			syslog(LOG_ERR, "Internal: unique flag to APPE");
			unique = 0;
		}
		if (guest && noguestmod) {
			reply(550, "Appending to existing file denied.");
			goto err;
		}
		restart_point = 0;	/* not affected by preceding REST */
	}
	if (unique)			/* STOU overrides REST */
		restart_point = 0;
	if (guest && noguestmod) {
		if (restart_point) {	/* guest STOR w/REST */
			reply(550, "Modifying existing file denied.");
			goto err;
		} else			/* treat guest STOR as STOU */
			unique = 1;
	}

	if (restart_point)
		mode = "r+";	/* so ASCII manual seek can work */
	if (unique) {
		if ((fd = guniquefd(name, &name)) < 0)
			goto err;
		fout = fdopen(fd, mode);
	} else
		fout = fopen(name, mode);
	closefunc = fclose;
	if (fout == NULL) {
		perror_reply(553, name);
		goto err;
	}
	byte_count = -1;
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c=getc(fout)) == EOF) {
					perror_reply(550, name);
					goto done;
				}
				if (c == '\n')
					i++;
			}
			/*
			 * We must do this seek to "current" position
			 * because we are changing from reading to
			 * writing.
			 */
			if (fseeko(fout, 0, SEEK_CUR) < 0) {
				perror_reply(550, name);
				goto done;
			}
		} else if (lseek(fileno(fout), restart_point, L_SET) < 0) {
			perror_reply(550, name);
			goto done;
		}
	}
	din = dataconn(name, -1, "r");
	if (din == NULL)
		goto done;
	if (receive_data(din, fout) == 0) {
		if (unique)
			reply(226, "Transfer complete (unique file name:%s).",
			    name);
		else
			reply(226, "Transfer complete.");
	}
	(void) fclose(din);
	data = -1;
	pdata = -1;
done:
	LOGBYTES(*mode == 'a' ? "append" : "put", name, byte_count);
	(*closefunc)(fout);
	return;
err:
	LOGCMD(*mode == 'a' ? "append" : "put" , name);
	return;
}

static FILE *
getdatasock(char *mode)
{
	int on = 1, s, t, tries;

	if (data >= 0)
		return (fdopen(data, mode));

	s = socket(data_dest.su_family, SOCK_STREAM, 0);
	if (s < 0)
		goto bad;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "data setsockopt (SO_REUSEADDR): %m");
	/* anchor socket to avoid multi-homing problems */
	data_source = ctrl_addr;
	data_source.su_port = htons(dataport);
	(void) seteuid(0);
	for (tries = 1; ; tries++) {
		/*
		 * We should loop here since it's possible that
		 * another ftpd instance has passed this point and is
		 * trying to open a data connection in active mode now.
		 * Until the other connection is opened, we'll be getting
		 * EADDRINUSE because no SOCK_STREAM sockets in the system
		 * can share both local and remote addresses, localIP:20
		 * and *:* in this case.
		 */
		if (bind(s, (struct sockaddr *)&data_source,
		    data_source.su_len) >= 0)
			break;
		if (errno != EADDRINUSE || tries > 10)
			goto bad;
		sleep(tries);
	}
	(void) seteuid(pw->pw_uid);
#ifdef IP_TOS
	if (data_source.su_family == AF_INET)
      {
	on = IPTOS_THROUGHPUT;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, &on, sizeof(int)) < 0)
		syslog(LOG_WARNING, "data setsockopt (IP_TOS): %m");
      }
#endif
#ifdef TCP_NOPUSH
	/*
	 * Turn off push flag to keep sender TCP from sending short packets
	 * at the boundaries of each write().
	 */
	on = 1;
	if (setsockopt(s, IPPROTO_TCP, TCP_NOPUSH, &on, sizeof on) < 0)
		syslog(LOG_WARNING, "data setsockopt (TCP_NOPUSH): %m");
#endif
	return (fdopen(s, mode));
bad:
	/* Return the real value of errno (close may change it) */
	t = errno;
	(void) seteuid(pw->pw_uid);
	(void) close(s);
	errno = t;
	return (NULL);
}

static FILE *
dataconn(char *name, off_t size, char *mode)
{
	char sizebuf[32];
	FILE *file;
	int retry = 0, tos, conerrno;

	file_size = size;
	byte_count = 0;
	if (size != -1)
		(void) snprintf(sizebuf, sizeof(sizebuf),
				" (%jd bytes)", (intmax_t)size);
	else
		*sizebuf = '\0';
	if (pdata >= 0) {
		union sockunion from;
		socklen_t fromlen = ctrl_addr.su_len;
		int flags, s;
		struct timeval timeout;
		fd_set set;

		FD_ZERO(&set);
		FD_SET(pdata, &set);

		timeout.tv_usec = 0;
		timeout.tv_sec = 120;

		/*
		 * Granted a socket is in the blocking I/O mode,
		 * accept() will block after a successful select()
		 * if the selected connection dies in between.
		 * Therefore set the non-blocking I/O flag here.
		 */
		if ((flags = fcntl(pdata, F_GETFL, 0)) == -1 ||
		    fcntl(pdata, F_SETFL, flags | O_NONBLOCK) == -1)
			goto pdata_err;
		if (select(pdata+1, &set, NULL, NULL, &timeout) <= 0 ||
		    (s = accept(pdata, (struct sockaddr *) &from, &fromlen)) < 0)
			goto pdata_err;
		(void) close(pdata);
		pdata = s;
		/*
		 * Unset the inherited non-blocking I/O flag
		 * on the child socket so stdio can work on it.
		 */
		if ((flags = fcntl(pdata, F_GETFL, 0)) == -1 ||
		    fcntl(pdata, F_SETFL, flags & ~O_NONBLOCK) == -1)
			goto pdata_err;
#ifdef IP_TOS
		if (from.su_family == AF_INET)
	      {
		tos = IPTOS_THROUGHPUT;
		if (setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(int)) < 0)
			syslog(LOG_WARNING, "pdata setsockopt (IP_TOS): %m");
	      }
#endif
		reply(150, "Opening %s mode data connection for '%s'%s.",
		     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
		return (fdopen(pdata, mode));
pdata_err:
		reply(425, "Can't open data connection.");
		(void) close(pdata);
		pdata = -1;
		return (NULL);
	}
	if (data >= 0) {
		reply(125, "Using existing data connection for '%s'%s.",
		    name, sizebuf);
		usedefault = 1;
		return (fdopen(data, mode));
	}
	if (usedefault)
		data_dest = his_addr;
	usedefault = 1;
	do {
		file = getdatasock(mode);
		if (file == NULL) {
			char hostbuf[NI_MAXHOST], portbuf[NI_MAXSERV];

			if (getnameinfo((struct sockaddr *)&data_source,
				data_source.su_len,
				hostbuf, sizeof(hostbuf) - 1,
				portbuf, sizeof(portbuf) - 1,
				NI_NUMERICHOST|NI_NUMERICSERV))
					*hostbuf = *portbuf = 0;
			hostbuf[sizeof(hostbuf) - 1] = 0;
			portbuf[sizeof(portbuf) - 1] = 0;
			reply(425, "Can't create data socket (%s,%s): %s.",
				hostbuf, portbuf, strerror(errno));
			return (NULL);
		}
		data = fileno(file);
		conerrno = 0;
		if (connect(data, (struct sockaddr *)&data_dest,
		    data_dest.su_len) == 0)
			break;
		conerrno = errno;
		(void) fclose(file);
		data = -1;
		if (conerrno == EADDRINUSE) {
			sleep(swaitint);
			retry += swaitint;
		} else {
			break;
		}
	} while (retry <= swaitmax);
	if (conerrno != 0) {
		reply(425, "Can't build data connection: %s.",
			   strerror(conerrno));
		return (NULL);
	}
	reply(150, "Opening %s mode data connection for '%s'%s.",
	     type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
	return (file);
}

/*
 * A helper macro to avoid code duplication
 * in send_data() and receive_data().
 *
 * XXX We have to block SIGURG during putc() because BSD stdio
 * is unable to restart interrupted write operations and hence
 * the entire buffer contents will be lost as soon as a write()
 * call indicates EINTR to stdio.
 */
#define FTPD_PUTC(ch, file, label)					\
	do {								\
		int ret;						\
									\
		do {							\
			START_UNSAFE;					\
			ret = putc((ch), (file));			\
			END_UNSAFE;					\
			CHECKOOB(return (-1))				\
			else if (ferror(file))				\
				goto label;				\
			clearerr(file);					\
		} while (ret == EOF);					\
	} while (0)

/*
 * Transfer the contents of "instr" to "outstr" peer using the appropriate
 * encapsulation of the data subject to Mode, Structure, and Type.
 *
 * NB: Form isn't handled.
 */
static int
send_data(FILE *instr, FILE *outstr, size_t blksize, off_t filesize, int isreg)
{
	int c, cp, filefd, netfd;
	char *buf;

	STARTXFER;

	switch (type) {

	case TYPE_A:
		cp = EOF;
		for (;;) {
			c = getc(instr);
			CHECKOOB(return (-1))
			else if (c == EOF && ferror(instr))
				goto file_err;
			if (c == EOF) {
				if (ferror(instr)) {	/* resume after OOB */
					clearerr(instr);
					continue;
				}
				if (feof(instr))	/* EOF */
					break;
				syslog(LOG_ERR, "Internal: impossible condition"
						" on file after getc()");
				goto file_err;
			}
			if (c == '\n' && cp != '\r') {
				FTPD_PUTC('\r', outstr, data_err);
				byte_count++;
			}
			FTPD_PUTC(c, outstr, data_err);
			byte_count++;
			cp = c;
		}
#ifdef notyet	/* BSD stdio isn't ready for that */
		while (fflush(outstr) == EOF) {
			CHECKOOB(return (-1))
			else
				goto data_err;
			clearerr(outstr);
		}
		ENDXFER;
#else
		ENDXFER;
		if (fflush(outstr) == EOF)
			goto data_err;
#endif
		reply(226, "Transfer complete.");
		return (0);

	case TYPE_I:
	case TYPE_L:
		/*
		 * isreg is only set if we are not doing restart and we
		 * are sending a regular file
		 */
		netfd = fileno(outstr);
		filefd = fileno(instr);

		if (isreg) {
			char *msg = "Transfer complete.";
			off_t cnt, offset;
			int err;

			cnt = offset = 0;

			while (filesize > 0) {
				err = sendfile(filefd, netfd, offset, 0,
					       NULL, &cnt, 0);
				/*
				 * Calculate byte_count before OOB processing.
				 * It can be used in myoob() later.
				 */
				byte_count += cnt;
				offset += cnt;
				filesize -= cnt;
				CHECKOOB(return (-1))
				else if (err == -1) {
					if (errno != EINTR &&
					    cnt == 0 && offset == 0)
						goto oldway;
					goto data_err;
				}
				if (err == -1)	/* resume after OOB */
					continue;
				/*
				 * We hit the EOF prematurely.
				 * Perhaps the file was externally truncated.
				 */
				if (cnt == 0) {
					msg = "Transfer finished due to "
					      "premature end of file.";
					break;
				}
			}
			ENDXFER;
			reply(226, "%s", msg);
			return (0);
		}

oldway:
		if ((buf = malloc(blksize)) == NULL) {
			ENDXFER;
			reply(451, "Ran out of memory.");
			return (-1);
		}

		for (;;) {
			int cnt, len;
			char *bp;

			cnt = read(filefd, buf, blksize);
			CHECKOOB(free(buf); return (-1))
			else if (cnt < 0) {
				free(buf);
				goto file_err;
			}
			if (cnt < 0)	/* resume after OOB */
				continue;
			if (cnt == 0)	/* EOF */
				break;
			for (len = cnt, bp = buf; len > 0;) {
				cnt = write(netfd, bp, len);
				CHECKOOB(free(buf); return (-1))
				else if (cnt < 0) {
					free(buf);
					goto data_err;
				}
				if (cnt <= 0)
					continue;
				len -= cnt;
				bp += cnt;
				byte_count += cnt;
			}
		}
		ENDXFER;
		free(buf);
		reply(226, "Transfer complete.");
		return (0);
	default:
		ENDXFER;
		reply(550, "Unimplemented TYPE %d in send_data.", type);
		return (-1);
	}

data_err:
	ENDXFER;
	perror_reply(426, "Data connection");
	return (-1);

file_err:
	ENDXFER;
	perror_reply(551, "Error on input file");
	return (-1);
}

/*
 * Transfer data from peer to "outstr" using the appropriate encapulation of
 * the data subject to Mode, Structure, and Type.
 *
 * N.B.: Form isn't handled.
 */
static int
receive_data(FILE *instr, FILE *outstr)
{
	int c, cp;
	int bare_lfs = 0;

	STARTXFER;

	switch (type) {

	case TYPE_I:
	case TYPE_L:
		for (;;) {
			int cnt, len;
			char *bp;
			char buf[BUFSIZ];

			cnt = read(fileno(instr), buf, sizeof(buf));
			CHECKOOB(return (-1))
			else if (cnt < 0)
				goto data_err;
			if (cnt < 0)	/* resume after OOB */
				continue;
			if (cnt == 0)	/* EOF */
				break;
			for (len = cnt, bp = buf; len > 0;) {
				cnt = write(fileno(outstr), bp, len);
				CHECKOOB(return (-1))
				else if (cnt < 0)
					goto file_err;
				if (cnt <= 0)
					continue;
				len -= cnt;
				bp += cnt;
				byte_count += cnt;
			}
		}
		ENDXFER;
		return (0);

	case TYPE_E:
		ENDXFER;
		reply(553, "TYPE E not implemented.");
		return (-1);

	case TYPE_A:
		cp = EOF;
		for (;;) {
			c = getc(instr);
			CHECKOOB(return (-1))
			else if (c == EOF && ferror(instr))
				goto data_err;
			if (c == EOF && ferror(instr)) { /* resume after OOB */
				clearerr(instr);
				continue;
			}

			if (cp == '\r') {
				if (c != '\n')
					FTPD_PUTC('\r', outstr, file_err);
			} else
				if (c == '\n')
					bare_lfs++;
			if (c == '\r') {
				byte_count++;
				cp = c;
				continue;
			}

			/* Check for EOF here in order not to lose last \r. */
			if (c == EOF) {
				if (feof(instr))	/* EOF */
					break;
				syslog(LOG_ERR, "Internal: impossible condition"
						" on data stream after getc()");
				goto data_err;
			}

			byte_count++;
			FTPD_PUTC(c, outstr, file_err);
			cp = c;
		}
#ifdef notyet	/* BSD stdio isn't ready for that */
		while (fflush(outstr) == EOF) {
			CHECKOOB(return (-1))
			else
				goto file_err;
			clearerr(outstr);
		}
		ENDXFER;
#else
		ENDXFER;
		if (fflush(outstr) == EOF)
			goto file_err;
#endif
		if (bare_lfs) {
			lreply(226,
		"WARNING! %d bare linefeeds received in ASCII mode.",
			    bare_lfs);
		(void)printf("   File may not have transferred correctly.\r\n");
		}
		return (0);
	default:
		ENDXFER;
		reply(550, "Unimplemented TYPE %d in receive_data.", type);
		return (-1);
	}

data_err:
	ENDXFER;
	perror_reply(426, "Data connection");
	return (-1);

file_err:
	ENDXFER;
	perror_reply(452, "Error writing to file");
	return (-1);
}

void
statfilecmd(char *filename)
{
	FILE *fin;
	int atstart;
	int c, code;
	char line[LINE_MAX];
	struct stat st;

	code = lstat(filename, &st) == 0 && S_ISDIR(st.st_mode) ? 212 : 213;
	(void)snprintf(line, sizeof(line), _PATH_LS " -lgA %s", filename);
	fin = ftpd_popen(line, "r");
	if (fin == NULL) {
		perror_reply(551, filename);
		return;
	}
	lreply(code, "Status of %s:", filename);
	atstart = 1;
	while ((c = getc(fin)) != EOF) {
		if (c == '\n') {
			if (ferror(stdout)){
				perror_reply(421, "Control connection");
				(void) ftpd_pclose(fin);
				dologout(1);
				/* NOTREACHED */
			}
			if (ferror(fin)) {
				perror_reply(551, filename);
				(void) ftpd_pclose(fin);
				return;
			}
			(void) putc('\r', stdout);
		}
		/*
		 * RFC 959 says neutral text should be prepended before
		 * a leading 3-digit number followed by whitespace, but
		 * many ftp clients can be confused by any leading digits,
		 * as a matter of fact.
		 */
		if (atstart && isdigit(c))
			(void) putc(' ', stdout);
		(void) putc(c, stdout);
		atstart = (c == '\n');
	}
	(void) ftpd_pclose(fin);
	reply(code, "End of status.");
}

void
statcmd(void)
{
	union sockunion *su;
	u_char *a, *p;
	char hname[NI_MAXHOST];
	int ispassive;

	if (hostinfo) {
		lreply(211, "%s FTP server status:", hostname);
		printf("     %s\r\n", version);
	} else
		lreply(211, "FTP server status:");
	printf("     Connected to %s", remotehost);
	if (!getnameinfo((struct sockaddr *)&his_addr, his_addr.su_len,
			 hname, sizeof(hname) - 1, NULL, 0, NI_NUMERICHOST)) {
		hname[sizeof(hname) - 1] = 0;
		if (strcmp(hname, remotehost) != 0)
			printf(" (%s)", hname);
	}
	printf("\r\n");
	if (logged_in) {
		if (guest)
			printf("     Logged in anonymously\r\n");
		else
			printf("     Logged in as %s\r\n", pw->pw_name);
	} else if (askpasswd)
		printf("     Waiting for password\r\n");
	else
		printf("     Waiting for user name\r\n");
	printf("     TYPE: %s", typenames[type]);
	if (type == TYPE_A || type == TYPE_E)
		printf(", FORM: %s", formnames[form]);
	if (type == TYPE_L)
#if CHAR_BIT == 8
		printf(" %d", CHAR_BIT);
#else
		printf(" %d", bytesize);	/* need definition! */
#endif
	printf("; STRUcture: %s; transfer MODE: %s\r\n",
	    strunames[stru], modenames[mode]);
	if (data != -1)
		printf("     Data connection open\r\n");
	else if (pdata != -1) {
		ispassive = 1;
		su = &pasv_addr;
		goto printaddr;
	} else if (usedefault == 0) {
		ispassive = 0;
		su = &data_dest;
printaddr:
#define UC(b) (((int) b) & 0xff)
		if (epsvall) {
			printf("     EPSV only mode (EPSV ALL)\r\n");
			goto epsvonly;
		}

		/* PORT/PASV */
		if (su->su_family == AF_INET) {
			a = (u_char *) &su->su_sin.sin_addr;
			p = (u_char *) &su->su_sin.sin_port;
			printf("     %s (%d,%d,%d,%d,%d,%d)\r\n",
				ispassive ? "PASV" : "PORT",
				UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
				UC(p[0]), UC(p[1]));
		}

		/* LPRT/LPSV */
	    {
		int alen, af, i;

		switch (su->su_family) {
		case AF_INET:
			a = (u_char *) &su->su_sin.sin_addr;
			p = (u_char *) &su->su_sin.sin_port;
			alen = sizeof(su->su_sin.sin_addr);
			af = 4;
			break;
		case AF_INET6:
			a = (u_char *) &su->su_sin6.sin6_addr;
			p = (u_char *) &su->su_sin6.sin6_port;
			alen = sizeof(su->su_sin6.sin6_addr);
			af = 6;
			break;
		default:
			af = 0;
			break;
		}
		if (af) {
			printf("     %s (%d,%d,", ispassive ? "LPSV" : "LPRT",
				af, alen);
			for (i = 0; i < alen; i++)
				printf("%d,", UC(a[i]));
			printf("%d,%d,%d)\r\n", 2, UC(p[0]), UC(p[1]));
		}
	    }

epsvonly:;
		/* EPRT/EPSV */
	    {
		int af;

		switch (su->su_family) {
		case AF_INET:
			af = 1;
			break;
		case AF_INET6:
			af = 2;
			break;
		default:
			af = 0;
			break;
		}
		if (af) {
			union sockunion tmp;

			tmp = *su;
			if (tmp.su_family == AF_INET6)
				tmp.su_sin6.sin6_scope_id = 0;
			if (!getnameinfo((struct sockaddr *)&tmp, tmp.su_len,
					hname, sizeof(hname) - 1, NULL, 0,
					NI_NUMERICHOST)) {
				hname[sizeof(hname) - 1] = 0;
				printf("     %s |%d|%s|%d|\r\n",
					ispassive ? "EPSV" : "EPRT",
					af, hname, htons(tmp.su_port));
			}
		}
	    }
#undef UC
	} else
		printf("     No data connection\r\n");
	reply(211, "End of status.");
}

void
fatalerror(char *s)
{

	reply(451, "Error in server: %s", s);
	reply(221, "Closing connection due to server error.");
	dologout(0);
	/* NOTREACHED */
}

void
reply(int n, const char *fmt, ...)
{
	va_list ap;

	(void)printf("%d ", n);
	va_start(ap, fmt);
	(void)vprintf(fmt, ap);
	va_end(ap);
	(void)printf("\r\n");
	(void)fflush(stdout);
	if (ftpdebug) {
		syslog(LOG_DEBUG, "<--- %d ", n);
		va_start(ap, fmt);
		vsyslog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
}

void
lreply(int n, const char *fmt, ...)
{
	va_list ap;

	(void)printf("%d- ", n);
	va_start(ap, fmt);
	(void)vprintf(fmt, ap);
	va_end(ap);
	(void)printf("\r\n");
	(void)fflush(stdout);
	if (ftpdebug) {
		syslog(LOG_DEBUG, "<--- %d- ", n);
		va_start(ap, fmt);
		vsyslog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
}

static void
ack(char *s)
{

	reply(250, "%s command successful.", s);
}

void
nack(char *s)
{

	reply(502, "%s command not implemented.", s);
}

/* ARGSUSED */
void
yyerror(char *s)
{
	char *cp;

	if ((cp = strchr(cbuf,'\n')))
		*cp = '\0';
	reply(500, "%s: command not understood.", cbuf);
}

void
delete(char *name)
{
	struct stat st;

	LOGCMD("delete", name);
	if (lstat(name, &st) < 0) {
		perror_reply(550, name);
		return;
	}
	if (S_ISDIR(st.st_mode)) {
		if (rmdir(name) < 0) {
			perror_reply(550, name);
			return;
		}
		goto done;
	}
	if (guest && noguestmod) {
		reply(550, "Operation not permitted.");
		return;
	}
	if (unlink(name) < 0) {
		perror_reply(550, name);
		return;
	}
done:
	ack("DELE");
}

void
cwd(char *path)
{

	if (chdir(path) < 0)
		perror_reply(550, path);
	else
		ack("CWD");
}

void
makedir(char *name)
{
	char *s;

	LOGCMD("mkdir", name);
	if (guest && noguestmkd)
		reply(550, "Operation not permitted.");
	else if (mkdir(name, 0777) < 0)
		perror_reply(550, name);
	else {
		if ((s = doublequote(name)) == NULL)
			fatalerror("Ran out of memory.");
		reply(257, "\"%s\" directory created.", s);
		free(s);
	}
}

void
removedir(char *name)
{

	LOGCMD("rmdir", name);
	if (rmdir(name) < 0)
		perror_reply(550, name);
	else
		ack("RMD");
}

void
pwd(void)
{
	char *s, path[MAXPATHLEN + 1];

	if (getcwd(path, sizeof(path)) == NULL)
		perror_reply(550, "Get current directory");
	else {
		if ((s = doublequote(path)) == NULL)
			fatalerror("Ran out of memory.");
		reply(257, "\"%s\" is current directory.", s);
		free(s);
	}
}

char *
renamefrom(char *name)
{
	struct stat st;

	if (guest && noguestmod) {
		reply(550, "Operation not permitted.");
		return (NULL);
	}
	if (lstat(name, &st) < 0) {
		perror_reply(550, name);
		return (NULL);
	}
	reply(350, "File exists, ready for destination name.");
	return (name);
}

void
renamecmd(char *from, char *to)
{
	struct stat st;

	LOGCMD2("rename", from, to);

	if (guest && (stat(to, &st) == 0)) {
		reply(550, "%s: permission denied.", to);
		return;
	}

	if (rename(from, to) < 0)
		perror_reply(550, "rename");
	else
		ack("RNTO");
}

static void
dolog(struct sockaddr *who)
{
	char who_name[NI_MAXHOST];

	realhostname_sa(remotehost, sizeof(remotehost) - 1, who, who->sa_len);
	remotehost[sizeof(remotehost) - 1] = 0;
	if (getnameinfo(who, who->sa_len,
		who_name, sizeof(who_name) - 1, NULL, 0, NI_NUMERICHOST))
			*who_name = 0;
	who_name[sizeof(who_name) - 1] = 0;

#ifdef SETPROCTITLE
#ifdef VIRTUAL_HOSTING
	if (thishost != firsthost)
		snprintf(proctitle, sizeof(proctitle), "%s: connected (to %s)",
			 remotehost, hostname);
	else
#endif
		snprintf(proctitle, sizeof(proctitle), "%s: connected",
			 remotehost);
	setproctitle("%s", proctitle);
#endif /* SETPROCTITLE */

	if (logging) {
#ifdef VIRTUAL_HOSTING
		if (thishost != firsthost)
			syslog(LOG_INFO, "connection from %s (%s) to %s",
			       remotehost, who_name, hostname);
		else
#endif
			syslog(LOG_INFO, "connection from %s (%s)",
			       remotehost, who_name);
	}
}

/*
 * Record logout in wtmp file
 * and exit with supplied status.
 */
void
dologout(int status)
{

	if (logged_in && dowtmp) {
		(void) seteuid(0);
#ifdef		LOGIN_CAP
 	        setusercontext(NULL, getpwuid(0), 0, LOGIN_SETALL & ~(LOGIN_SETLOGIN |
		       LOGIN_SETUSER | LOGIN_SETGROUP | LOGIN_SETPATH |
		       LOGIN_SETENV));
#endif
		ftpd_logwtmp(wtmpid, NULL, NULL);
	}
	/* beware of flushing buffers after a SIGPIPE */
	_exit(status);
}

static void
sigurg(int signo)
{

	recvurg = 1;
}

static void
maskurg(int flag)
{
	int oerrno;
	sigset_t sset;

	if (!transflag) {
		syslog(LOG_ERR, "Internal: maskurg() while no transfer");
		return;
	}
	oerrno = errno;
	sigemptyset(&sset);
	sigaddset(&sset, SIGURG);
	sigprocmask(flag ? SIG_BLOCK : SIG_UNBLOCK, &sset, NULL);
	errno = oerrno;
}

static void
flagxfer(int flag)
{

	if (flag) {
		if (transflag)
			syslog(LOG_ERR, "Internal: flagxfer(1): "
					"transfer already under way");
		transflag = 1;
		maskurg(0);
		recvurg = 0;
	} else {
		if (!transflag)
			syslog(LOG_ERR, "Internal: flagxfer(0): "
					"no active transfer");
		maskurg(1);
		transflag = 0;
	}
}

/*
 * Returns 0 if OK to resume or -1 if abort requested.
 */
static int
myoob(void)
{
	char *cp;
	int ret;

	if (!transflag) {
		syslog(LOG_ERR, "Internal: myoob() while no transfer");
		return (0);
	}
	cp = tmpline;
	ret = get_line(cp, 7, stdin);
	if (ret == -1) {
		reply(221, "You could at least say goodbye.");
		dologout(0);
	} else if (ret == -2) {
		/* Ignore truncated command. */
		return (0);
	}
	upper(cp);
	if (strcmp(cp, "ABOR\r\n") == 0) {
		tmpline[0] = '\0';
		reply(426, "Transfer aborted. Data connection closed.");
		reply(226, "Abort successful.");
		return (-1);
	}
	if (strcmp(cp, "STAT\r\n") == 0) {
		tmpline[0] = '\0';
		if (file_size != -1)
			reply(213, "Status: %jd of %jd bytes transferred.",
				   (intmax_t)byte_count, (intmax_t)file_size);
		else
			reply(213, "Status: %jd bytes transferred.",
				   (intmax_t)byte_count);
	}
	return (0);
}

/*
 * Note: a response of 425 is not mentioned as a possible response to
 *	the PASV command in RFC959. However, it has been blessed as
 *	a legitimate response by Jon Postel in a telephone conversation
 *	with Rick Adams on 25 Jan 89.
 */
void
passive(void)
{
	socklen_t len;
	int on;
	char *p, *a;

	if (pdata >= 0)		/* close old port if one set */
		close(pdata);

	pdata = socket(ctrl_addr.su_family, SOCK_STREAM, 0);
	if (pdata < 0) {
		perror_reply(425, "Can't open passive connection");
		return;
	}
	on = 1;
	if (setsockopt(pdata, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "pdata setsockopt (SO_REUSEADDR): %m");

	(void) seteuid(0);

#ifdef IP_PORTRANGE
	if (ctrl_addr.su_family == AF_INET) {
	    on = restricted_data_ports ? IP_PORTRANGE_HIGH
				       : IP_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IP, IP_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif
#ifdef IPV6_PORTRANGE
	if (ctrl_addr.su_family == AF_INET6) {
	    on = restricted_data_ports ? IPV6_PORTRANGE_HIGH
				       : IPV6_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IPV6, IPV6_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif

	pasv_addr = ctrl_addr;
	pasv_addr.su_port = 0;
	if (bind(pdata, (struct sockaddr *)&pasv_addr, pasv_addr.su_len) < 0)
		goto pasv_error;

	(void) seteuid(pw->pw_uid);

	len = sizeof(pasv_addr);
	if (getsockname(pdata, (struct sockaddr *) &pasv_addr, &len) < 0)
		goto pasv_error;
	if (listen(pdata, 1) < 0)
		goto pasv_error;
	if (pasv_addr.su_family == AF_INET)
		a = (char *) &pasv_addr.su_sin.sin_addr;
	else if (pasv_addr.su_family == AF_INET6 &&
		 IN6_IS_ADDR_V4MAPPED(&pasv_addr.su_sin6.sin6_addr))
		a = (char *) &pasv_addr.su_sin6.sin6_addr.s6_addr[12];
	else
		goto pasv_error;
		
	p = (char *) &pasv_addr.su_port;

#define UC(b) (((int) b) & 0xff)

	reply(227, "Entering Passive Mode (%d,%d,%d,%d,%d,%d)", UC(a[0]),
		UC(a[1]), UC(a[2]), UC(a[3]), UC(p[0]), UC(p[1]));
	return;

pasv_error:
	(void) seteuid(pw->pw_uid);
	(void) close(pdata);
	pdata = -1;
	perror_reply(425, "Can't open passive connection");
	return;
}

/*
 * Long Passive defined in RFC 1639.
 *     228 Entering Long Passive Mode
 *         (af, hal, h1, h2, h3,..., pal, p1, p2...)
 */

void
long_passive(char *cmd, int pf)
{
	socklen_t len;
	int on;
	char *p, *a;

	if (pdata >= 0)		/* close old port if one set */
		close(pdata);

	if (pf != PF_UNSPEC) {
		if (ctrl_addr.su_family != pf) {
			switch (ctrl_addr.su_family) {
			case AF_INET:
				pf = 1;
				break;
			case AF_INET6:
				pf = 2;
				break;
			default:
				pf = 0;
				break;
			}
			/*
			 * XXX
			 * only EPRT/EPSV ready clients will understand this
			 */
			if (strcmp(cmd, "EPSV") == 0 && pf) {
				reply(522, "Network protocol mismatch, "
					"use (%d)", pf);
			} else
				reply(501, "Network protocol mismatch."); /*XXX*/

			return;
		}
	}
		
	pdata = socket(ctrl_addr.su_family, SOCK_STREAM, 0);
	if (pdata < 0) {
		perror_reply(425, "Can't open passive connection");
		return;
	}
	on = 1;
	if (setsockopt(pdata, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		syslog(LOG_WARNING, "pdata setsockopt (SO_REUSEADDR): %m");

	(void) seteuid(0);

	pasv_addr = ctrl_addr;
	pasv_addr.su_port = 0;
	len = pasv_addr.su_len;

#ifdef IP_PORTRANGE
	if (ctrl_addr.su_family == AF_INET) {
	    on = restricted_data_ports ? IP_PORTRANGE_HIGH
				       : IP_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IP, IP_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif
#ifdef IPV6_PORTRANGE
	if (ctrl_addr.su_family == AF_INET6) {
	    on = restricted_data_ports ? IPV6_PORTRANGE_HIGH
				       : IPV6_PORTRANGE_DEFAULT;

	    if (setsockopt(pdata, IPPROTO_IPV6, IPV6_PORTRANGE,
			    &on, sizeof(on)) < 0)
		    goto pasv_error;
	}
#endif

	if (bind(pdata, (struct sockaddr *)&pasv_addr, len) < 0)
		goto pasv_error;

	(void) seteuid(pw->pw_uid);

	if (getsockname(pdata, (struct sockaddr *) &pasv_addr, &len) < 0)
		goto pasv_error;
	if (listen(pdata, 1) < 0)
		goto pasv_error;

#define UC(b) (((int) b) & 0xff)

	if (strcmp(cmd, "LPSV") == 0) {
		p = (char *)&pasv_addr.su_port;
		switch (pasv_addr.su_family) {
		case AF_INET:
			a = (char *) &pasv_addr.su_sin.sin_addr;
		v4_reply:
			reply(228,
"Entering Long Passive Mode (%d,%d,%d,%d,%d,%d,%d,%d,%d)",
			      4, 4, UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
			      2, UC(p[0]), UC(p[1]));
			return;
		case AF_INET6:
			if (IN6_IS_ADDR_V4MAPPED(&pasv_addr.su_sin6.sin6_addr)) {
				a = (char *) &pasv_addr.su_sin6.sin6_addr.s6_addr[12];
				goto v4_reply;
			}
			a = (char *) &pasv_addr.su_sin6.sin6_addr;
			reply(228,
"Entering Long Passive Mode "
"(%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)",
			      6, 16, UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
			      UC(a[4]), UC(a[5]), UC(a[6]), UC(a[7]),
			      UC(a[8]), UC(a[9]), UC(a[10]), UC(a[11]),
			      UC(a[12]), UC(a[13]), UC(a[14]), UC(a[15]),
			      2, UC(p[0]), UC(p[1]));
			return;
		}
	} else if (strcmp(cmd, "EPSV") == 0) {
		switch (pasv_addr.su_family) {
		case AF_INET:
		case AF_INET6:
			reply(229, "Entering Extended Passive Mode (|||%d|)",
				ntohs(pasv_addr.su_port));
			return;
		}
	} else {
		/* more proper error code? */
	}

pasv_error:
	(void) seteuid(pw->pw_uid);
	(void) close(pdata);
	pdata = -1;
	perror_reply(425, "Can't open passive connection");
	return;
}

/*
 * Generate unique name for file with basename "local"
 * and open the file in order to avoid possible races.
 * Try "local" first, then "local.1", "local.2" etc, up to "local.99".
 * Return descriptor to the file, set "name" to its name.
 *
 * Generates failure reply on error.
 */
static int
guniquefd(char *local, char **name)
{
	static char new[MAXPATHLEN];
	struct stat st;
	char *cp;
	int count;
	int fd;

	cp = strrchr(local, '/');
	if (cp)
		*cp = '\0';
	if (stat(cp ? local : ".", &st) < 0) {
		perror_reply(553, cp ? local : ".");
		return (-1);
	}
	if (cp) {
		/*
		 * Let not overwrite dirname with counter suffix.
		 * -4 is for /nn\0
		 * In this extreme case dot won't be put in front of suffix.
		 */
		if (strlen(local) > sizeof(new) - 4) {
			reply(553, "Pathname too long.");
			return (-1);
		}
		*cp = '/';
	}
	/* -4 is for the .nn<null> we put on the end below */
	(void) snprintf(new, sizeof(new) - 4, "%s", local);
	cp = new + strlen(new);
	/* 
	 * Don't generate dotfile unless requested explicitly.
	 * This covers the case when basename gets truncated off
	 * by buffer size.
	 */
	if (cp > new && cp[-1] != '/')
		*cp++ = '.';
	for (count = 0; count < 100; count++) {
		/* At count 0 try unmodified name */
		if (count)
			(void)sprintf(cp, "%d", count);
		if ((fd = open(count ? new : local,
		    O_RDWR | O_CREAT | O_EXCL, 0666)) >= 0) {
			*name = count ? new : local;
			return (fd);
		}
		if (errno != EEXIST) {
			perror_reply(553, count ? new : local);
			return (-1);
		}
	}
	reply(452, "Unique file name cannot be created.");
	return (-1);
}

/*
 * Format and send reply containing system error number.
 */
void
perror_reply(int code, char *string)
{

	reply(code, "%s: %s.", string, strerror(errno));
}

static char *onefile[] = {
	"",
	0
};

void
send_file_list(char *whichf)
{
	struct stat st;
	DIR *dirp = NULL;
	struct dirent *dir;
	FILE *dout = NULL;
	char **dirlist, *dirname;
	int simple = 0;
	int freeglob = 0;
	glob_t gl;

	if (strpbrk(whichf, "~{[*?") != NULL) {
		int flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_TILDE;

		memset(&gl, 0, sizeof(gl));
		gl.gl_matchc = MAXGLOBARGS;
		flags |= GLOB_LIMIT;
		freeglob = 1;
		if (glob(whichf, flags, 0, &gl)) {
			reply(550, "No matching files found.");
			goto out;
		} else if (gl.gl_pathc == 0) {
			errno = ENOENT;
			perror_reply(550, whichf);
			goto out;
		}
		dirlist = gl.gl_pathv;
	} else {
		onefile[0] = whichf;
		dirlist = onefile;
		simple = 1;
	}

	while ((dirname = *dirlist++)) {
		if (stat(dirname, &st) < 0) {
			/*
			 * If user typed "ls -l", etc, and the client
			 * used NLST, do what the user meant.
			 */
			if (dirname[0] == '-' && *dirlist == NULL &&
			    dout == NULL)
				retrieve(_PATH_LS " %s", dirname);
			else
				perror_reply(550, whichf);
			goto out;
		}

		if (S_ISREG(st.st_mode)) {
			if (dout == NULL) {
				dout = dataconn("file list", -1, "w");
				if (dout == NULL)
					goto out;
				STARTXFER;
			}
			START_UNSAFE;
			fprintf(dout, "%s%s\n", dirname,
				type == TYPE_A ? "\r" : "");
			END_UNSAFE;
			if (ferror(dout))
				goto data_err;
			byte_count += strlen(dirname) +
				      (type == TYPE_A ? 2 : 1);
			CHECKOOB(goto abrt);
			continue;
		} else if (!S_ISDIR(st.st_mode))
			continue;

		if ((dirp = opendir(dirname)) == NULL)
			continue;

		while ((dir = readdir(dirp)) != NULL) {
			char nbuf[MAXPATHLEN];

			CHECKOOB(goto abrt);

			if (dir->d_name[0] == '.' && dir->d_namlen == 1)
				continue;
			if (dir->d_name[0] == '.' && dir->d_name[1] == '.' &&
			    dir->d_namlen == 2)
				continue;

			snprintf(nbuf, sizeof(nbuf),
				"%s/%s", dirname, dir->d_name);

			/*
			 * We have to do a stat to insure it's
			 * not a directory or special file.
			 */
			if (simple || (stat(nbuf, &st) == 0 &&
			    S_ISREG(st.st_mode))) {
				if (dout == NULL) {
					dout = dataconn("file list", -1, "w");
					if (dout == NULL)
						goto out;
					STARTXFER;
				}
				START_UNSAFE;
				if (nbuf[0] == '.' && nbuf[1] == '/')
					fprintf(dout, "%s%s\n", &nbuf[2],
						type == TYPE_A ? "\r" : "");
				else
					fprintf(dout, "%s%s\n", nbuf,
						type == TYPE_A ? "\r" : "");
				END_UNSAFE;
				if (ferror(dout))
					goto data_err;
				byte_count += strlen(nbuf) +
					      (type == TYPE_A ? 2 : 1);
				CHECKOOB(goto abrt);
			}
		}
		(void) closedir(dirp);
		dirp = NULL;
	}

	if (dout == NULL)
		reply(550, "No files found.");
	else if (ferror(dout))
data_err:	perror_reply(550, "Data connection");
	else
		reply(226, "Transfer complete.");
out:
	if (dout) {
		ENDXFER;
abrt:
		(void) fclose(dout);
		data = -1;
		pdata = -1;
	}
	if (dirp)
		(void) closedir(dirp);
	if (freeglob) {
		freeglob = 0;
		globfree(&gl);
	}
}

void
reapchild(int signo)
{
	while (waitpid(-1, NULL, WNOHANG) > 0);
}

#ifdef OLD_SETPROCTITLE
/*
 * Clobber argv so ps will show what we're doing.  (Stolen from sendmail.)
 * Warning, since this is usually started from inetd.conf, it often doesn't
 * have much of an environment or arglist to overwrite.
 */
void
setproctitle(const char *fmt, ...)
{
	int i;
	va_list ap;
	char *p, *bp, ch;
	char buf[LINE_MAX];

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);

	/* make ps print our process name */
	p = Argv[0];
	*p++ = '-';

	i = strlen(buf);
	if (i > LastArgv - p - 2) {
		i = LastArgv - p - 2;
		buf[i] = '\0';
	}
	bp = buf;
	while (ch = *bp++)
		if (ch != '\n' && ch != '\r')
			*p++ = ch;
	while (p < LastArgv)
		*p++ = ' ';
}
#endif /* OLD_SETPROCTITLE */

static void
appendf(char **strp, char *fmt, ...)
{
	va_list ap;
	char *ostr, *p;

	va_start(ap, fmt);
	vasprintf(&p, fmt, ap);
	va_end(ap);
	if (p == NULL)
		fatalerror("Ran out of memory.");
	if (*strp == NULL)
		*strp = p;
	else {
		ostr = *strp;
		asprintf(strp, "%s%s", ostr, p);
		if (*strp == NULL)
			fatalerror("Ran out of memory.");
		free(ostr);
	}
}

static void
logcmd(char *cmd, char *file1, char *file2, off_t cnt)
{
	char *msg = NULL;
	char wd[MAXPATHLEN + 1];

	if (logging <= 1)
		return;

	if (getcwd(wd, sizeof(wd) - 1) == NULL)
		strcpy(wd, strerror(errno));

	appendf(&msg, "%s", cmd);
	if (file1)
		appendf(&msg, " %s", file1);
	if (file2)
		appendf(&msg, " %s", file2);
	if (cnt >= 0)
		appendf(&msg, " = %jd bytes", (intmax_t)cnt);
	appendf(&msg, " (wd: %s", wd);
	if (guest || dochroot)
		appendf(&msg, "; chrooted");
	appendf(&msg, ")");
	syslog(LOG_INFO, "%s", msg);
	free(msg);
}

static void
logxfer(char *name, off_t size, time_t start)
{
	char buf[MAXPATHLEN + 1024];
	char path[MAXPATHLEN + 1];
	time_t now;

	if (statfd >= 0) {
		time(&now);
		if (realpath(name, path) == NULL) {
			syslog(LOG_NOTICE, "realpath failed on %s: %m", path);
			return;
		}
		snprintf(buf, sizeof(buf), "%.20s!%s!%s!%s!%jd!%ld\n",
			ctime(&now)+4, ident, remotehost,
			path, (intmax_t)size,
			(long)(now - start + (now == start)));
		write(statfd, buf, strlen(buf));
	}
}

static char *
doublequote(char *s)
{
	int n;
	char *p, *s2;

	for (p = s, n = 0; *p; p++)
		if (*p == '"')
			n++;

	if ((s2 = malloc(p - s + n + 1)) == NULL)
		return (NULL);

	for (p = s2; *s; s++, p++) {
		if ((*p = *s) == '"')
			*(++p) = '"';
	}
	*p = '\0';

	return (s2);
}

/* setup server socket for specified address family */
/* if af is PF_UNSPEC more than one socket may be returned */
/* the returned list is dynamically allocated, so caller needs to free it */
static int *
socksetup(int af, char *bindname, const char *bindport)
{
	struct addrinfo hints, *res, *r;
	int error, maxs, *s, *socks;
	const int on = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = af;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(bindname, bindport, &hints, &res);
	if (error) {
		syslog(LOG_ERR, "%s", gai_strerror(error));
		if (error == EAI_SYSTEM)
			syslog(LOG_ERR, "%s", strerror(errno));
		return NULL;
	}

	/* Count max number of sockets we may open */
	for (maxs = 0, r = res; r; r = r->ai_next, maxs++)
		;
	socks = malloc((maxs + 1) * sizeof(int));
	if (!socks) {
		freeaddrinfo(res);
		syslog(LOG_ERR, "couldn't allocate memory for sockets");
		return NULL;
	}

	*socks = 0;   /* num of sockets counter at start of array */
	s = socks + 1;
	for (r = res; r; r = r->ai_next) {
		*s = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (*s < 0) {
			syslog(LOG_DEBUG, "control socket: %m");
			continue;
		}
		if (setsockopt(*s, SOL_SOCKET, SO_REUSEADDR,
		    &on, sizeof(on)) < 0)
			syslog(LOG_WARNING,
			    "control setsockopt (SO_REUSEADDR): %m");
		if (r->ai_family == AF_INET6) {
			if (setsockopt(*s, IPPROTO_IPV6, IPV6_V6ONLY,
			    &on, sizeof(on)) < 0)
				syslog(LOG_WARNING,
				    "control setsockopt (IPV6_V6ONLY): %m");
		}
		if (bind(*s, r->ai_addr, r->ai_addrlen) < 0) {
			syslog(LOG_DEBUG, "control bind: %m");
			close(*s);
			continue;
		}
		(*socks)++;
		s++;
	}

	if (res)
		freeaddrinfo(res);

	if (*socks == 0) {
		syslog(LOG_ERR, "control socket: Couldn't bind to any socket");
		free(socks);
		return NULL;
	}
	return(socks);
}
