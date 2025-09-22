/*	$OpenBSD: ftpd.c,v 1.235 2025/05/08 15:22:49 deraadt Exp $	*/
/*	$NetBSD: ftpd.c,v 1.15 1995/06/03 22:46:47 mycroft Exp $	*/

/*
 * Copyright (C) 1997 and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
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

/*
 * FTP server.
 */
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define	FTP_NAMES
#include <arpa/ftp.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>

#include <bsd_auth.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <login_cap.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <vis.h>
#include <unistd.h>
#include <utmp.h>
#include <poll.h>

#include "pathnames.h"
#include "monitor.h"
#include "extern.h"

extern	off_t restart_point;
extern	char cbuf[];

union sockunion ctrl_addr;
union sockunion data_source;
union sockunion data_dest;
union sockunion his_addr;
union sockunion pasv_addr;

sigset_t allsigs;

int	daemon_mode = 0;
int	data;
int	logged_in;
struct	passwd *pw;
int	debug = 0;
int	timeout = 900;    /* timeout after 15 minutes of inactivity */
int	maxtimeout = 7200;/* don't allow idle time to be set beyond 2 hours */
int	logging;
int	anon_ok = 1;
int	anon_only = 0;
unsigned int minuid = 1000;
int	multihome = 0;
int	guest;
int	stats;
int	statfd = -1;
int	portcheck = 1;
int	dochroot;
int	type;
int	form;
int	stru;			/* avoid C keyword */
int	mode;
int	doutmp = 0;		/* update utmp file */
int	nowtmp = 0;		/* do not update wtmp file */
int	usedefault = 1;		/* for data transfers */
int	pdata = -1;		/* for passive mode */
int	family = AF_UNSPEC;
volatile sig_atomic_t transflag;
off_t	file_size;
off_t	byte_count;
mode_t	defumask = S_IWGRP|S_IWOTH;		/* default umask value */
int	umaskchange = 1;		/* allow user to change umask value. */
char	tmpline[7];
char	hostname[HOST_NAME_MAX+1];
char	remotehost[HOST_NAME_MAX+1];
char	dhostname[HOST_NAME_MAX+1];
char	*guestpw;
char	ttyline[20];
static struct utmp utmp;	/* for utmp */
static	login_cap_t *lc;
static	auth_session_t *as;
static	volatile sig_atomic_t recvurg;

int epsvall = 0;

/*
 * Timeout intervals for retrying connections
 * to hosts that don't accept PORT cmds.  This
 * is a kludge, but given the problems with TCP...
 */
#define	SWAITMAX	90	/* wait at most 90 seconds */
#define	SWAITINT	5	/* interval between retries */

int	swaitmax = SWAITMAX;
int	swaitint = SWAITINT;

char	proctitle[BUFSIZ];	/* initial part of title */

#define LOGCMD(cmd, file) \
	if (logging > 1) \
	    syslog(LOG_INFO,"%s %s%s", cmd, \
		*(file) == '/' ? "" : curdir(), file);
#define LOGCMD2(cmd, file1, file2) \
	 if (logging > 1) \
	    syslog(LOG_INFO,"%s %s%s %s%s", cmd, \
		*(file1) == '/' ? "" : curdir(), file1, \
		*(file2) == '/' ? "" : curdir(), file2);
#define LOGBYTES(cmd, file, cnt) \
	if (logging > 1) { \
		if ((cnt) == -1) \
		    syslog(LOG_INFO,"%s %s%s", cmd, \
			*(file) == '/' ? "" : curdir(), file); \
		else \
		    syslog(LOG_INFO, "%s %s%s = %lld bytes", \
			cmd, (*(file) == '/') ? "" : curdir(), file, \
			(long long)(cnt)); \
	}

static void	 ack(const char *);
static void	 sigurg(int);
static void	 myoob(void);
static int	 checkuser(char *, const char *);
static FILE	*dataconn(const char *, off_t, char *);
static void	 dolog(struct sockaddr *);
static char	*copy_dir(char *, struct passwd *);
static char	*curdir(void);
static void	 end_login(void);
static FILE	*getdatasock(char *);
static int	 guniquefd(const char *, char **);
static void	 lostconn(int);
static void	 sigquit(int);
static int	 receive_data(FILE *, FILE *);
static void	 replydirname(const char *, const char *);
static int	 send_data(FILE *, FILE *, off_t, off_t, int);
static struct passwd *
		 sgetpwnam(const char *, struct passwd *);
static void	 reapchild(int);
static void	 usage(void);

void	 logxfer(const char *, off_t, time_t);
void	 set_slave_signals(void);

static char *
curdir(void)
{
	static char path[PATH_MAX+1];	/* path + '/' */

	if (getcwd(path, sizeof(path)-1) == NULL)
		return ("");
	if (path[1] != '\0')		/* special case for root dir. */
		strlcat(path, "/", sizeof path);
	/* For guest account, skip / since it's chrooted */
	return (guest ? path+1 : path);
}

char *argstr = "AdDhnlm:MSt:T:u:PUvW46";

static void
usage(void)
{
	syslog(LOG_ERR,
	    "usage: ftpd [-46ADdlMnPSUW] [-m minuid] [-T maxtimeout] "
	    "[-t timeout] [-u mask]");
	exit(2);
}

int
main(int argc, char *argv[])
{
	socklen_t addrlen;
	int ch, on = 1, tos;
	char line[LINE_MAX];
	FILE *fp;
	struct hostent *hp;
	struct sigaction sa;
	int error = 0;
	const char *errstr;

	tzset();		/* in case no timezone database in ~ftp */
	sigfillset(&allsigs);	/* used to block signals while root */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	while ((ch = getopt(argc, argv, argstr)) != -1) {
		switch (ch) {
		case 'A':
			anon_only = 1;
			break;

		case 'd':
		case 'v':		/* deprecated */
			debug = 1;
			break;

		case 'D':
			daemon_mode = 1;
			break;

		case 'P':
			portcheck = 0;
			break;

		case 'h':		/* deprecated */
			break;

		case 'l':
			logging++;	/* > 1 == extra logging */
			break;

		case 'm':
			minuid = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr) {
				syslog(LOG_ERR,
				    "%s is a bad value for -m, aborting",
				    optarg);
				exit(2);
			}
			break;

		case 'M':
			multihome = 1;
			break;

		case 'n':
			anon_ok = 0;
			break;

		case 'S':
			stats = 1;
			break;

		case 't':
			timeout = strtonum(optarg, 0, INT_MAX, &errstr);
			if (errstr) {
				syslog(LOG_ERR,
				    "%s is a bad value for -t, aborting",
				    optarg);
				exit(2);
			}
			if (maxtimeout < timeout)
				maxtimeout = timeout;
			break;

		case 'T':
			maxtimeout = strtonum(optarg, 0, INT_MAX,
			    &errstr);
			if (errstr) {
				syslog(LOG_ERR,
				    "%s is a bad value for -T, aborting",
				    optarg);
				exit(2);
			}
			if (timeout > maxtimeout)
				timeout = maxtimeout;
			break;

		case 'u':
		    {
			long val = 0;
			char *p;
			umaskchange = 0;

			val = strtol(optarg, &p, 8);
			if (*optarg == '\0' || *p != '\0' || val < 0 ||
			    (val & ~ACCESSPERMS)) {
				syslog(LOG_ERR,
				    "%s is a bad value for -u, aborting",
				    optarg);
				exit(2);
			}
			defumask = val;
			break;
		    }

		case 'U':
			doutmp = 1;
			break;

		case 'W':
			nowtmp = 1;
			break;

		case '4':
			family = AF_INET;
			break;

		case '6':
			family = AF_INET6;
			break;

		default:
			usage();
			break;
		}
	}

	if (nowtmp && doutmp) {
		syslog(LOG_ERR, "options 'U' and 'W' are mutually exclusive");
		exit(1);
	}

	(void) freopen(_PATH_DEVNULL, "w", stderr);

	/*
	 * LOG_NDELAY sets up the logging connection immediately,
	 * necessary for anonymous ftp's that chroot and can't do it later.
	 */
	openlog("ftpd", LOG_PID | LOG_NDELAY, LOG_FTP);

	if (getpwnam(FTPD_PRIVSEP_USER) == NULL) {
		syslog(LOG_ERR, "privilege separation user %s not found",
		    FTPD_PRIVSEP_USER);
		exit(1);
	}

	if (daemon_mode) {
		int *fds, fd;
		struct pollfd *pfds;
		struct addrinfo hints, *res, *res0;
		nfds_t n, i;

		/*
		 * Detach from parent.
		 */
		if (daemon(1, 1) == -1) {
			syslog(LOG_ERR, "failed to become a daemon");
			exit(1);
		}
		sa.sa_handler = reapchild;
		(void) sigaction(SIGCHLD, &sa, NULL);

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = family;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;
		error = getaddrinfo(NULL, "ftp", &hints, &res0);
		if (error) {
			syslog(LOG_ERR, "%s", gai_strerror(error));
			exit(1);
		}

		n = 0;
		for (res = res0; res; res = res->ai_next)
			n++;

		fds = calloc(n, sizeof(int));
		pfds = calloc(n, sizeof(struct pollfd));
		if (!fds || !pfds) {
			syslog(LOG_ERR, "%s", strerror(errno));
			exit(1);
		}

		/*
		 * Open sockets, bind it to the FTP port, and start
		 * listening.
		 */
		n = 0;
		for (res = res0; res; res = res->ai_next) {
			fds[n] = socket(res->ai_family, res->ai_socktype,
			    res->ai_protocol);
			if (fds[n] == -1)
				continue;

			if (setsockopt(fds[n], SOL_SOCKET, SO_KEEPALIVE,
			    &on, sizeof(on)) == -1) {
				close(fds[n]);
				fds[n] = -1;
				continue;
			}

			if (setsockopt(fds[n], SOL_SOCKET, SO_REUSEADDR,
			    &on, sizeof(on)) == -1) {
				close(fds[n]);
				fds[n] = -1;
				continue;
			}

			if (bind(fds[n], res->ai_addr, res->ai_addrlen) == -1) {
				close(fds[n]);
				fds[n] = -1;
				continue;
			}
			if (listen(fds[n], 32) == -1) {
				close(fds[n]);
				fds[n] = -1;
				continue;
			}

			pfds[n].fd = fds[n];
			pfds[n].events = POLLIN;
			n++;
		}
		freeaddrinfo(res0);

		if (n == 0) {
			syslog(LOG_ERR, "could not open control socket");
			exit(1);
		}

		/*
		 * Loop forever accepting connection requests and forking off
		 * children to handle them.
		 */
		while (1) {
			if (poll(pfds, n, INFTIM) == -1) {
				if (errno == EINTR)
					continue;
				syslog(LOG_ERR, "poll: %m");
				exit(1);
			}
			for (i = 0; i < n; i++)
				if (pfds[i].revents & POLLIN) {
					addrlen = sizeof(his_addr);
					fd = accept(pfds[i].fd,
					    (struct sockaddr *)&his_addr,
					    &addrlen);
					if (fd != -1) {
						if (fork() == 0)
							goto child;
						close(fd);
					}
				}
		}

	child:
		/* child */
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		for (i = 0; i < n; i++)
			close(fds[i]);
	} else {
		addrlen = sizeof(his_addr);
		if (getpeername(0, (struct sockaddr *)&his_addr,
		    &addrlen) == -1) {
			/* syslog(LOG_ERR, "getpeername (%s): %m", argv[0]); */
			exit(1);
		}
	}

	/* set this here so klogin can use it... */
	(void)snprintf(ttyline, sizeof(ttyline), "ftp%ld", (long)getpid());

	set_slave_signals();

	addrlen = sizeof(ctrl_addr);
	if (getsockname(0, (struct sockaddr *)&ctrl_addr, &addrlen) == -1) {
		syslog(LOG_ERR, "getsockname: %m");
		exit(1);
	}
	if (his_addr.su_family == AF_INET6 &&
	    IN6_IS_ADDR_V4MAPPED(&his_addr.su_sin6.sin6_addr)) {
		syslog(LOG_WARNING,
		    "Connection from IPv4 mapped address is not supported.");
		reply(530, "System not available.");
		exit(1);
	}
	tos = IPTOS_LOWDELAY;
	switch (his_addr.su_family) {
	case AF_INET:
		if (setsockopt(0, IPPROTO_IP, IP_TOS, &tos,
		    sizeof(int)) == -1)
			syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
		break;
	case AF_INET6:
		if (setsockopt(0, IPPROTO_IPV6, IPV6_TCLASS, &tos,
		    sizeof(int)) == -1)
			syslog(LOG_WARNING, "setsockopt (IPV6_TCLASS): %m");
		break;
	}
	data_source.su_port = htons(ntohs(ctrl_addr.su_port) - 1);

	/* Try to handle urgent data inline */
	if (setsockopt(0, SOL_SOCKET, SO_OOBINLINE, &on, sizeof(on)) == -1)
		syslog(LOG_ERR, "setsockopt: %m");

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
	if ((fp = fopen(_PATH_NOLOGIN, "r")) != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL) {
			line[strcspn(line, "\n")] = '\0';
			lreply(530, "%s", line);
		}
		(void) fclose(fp);
		reply(530, "System not available.");
		exit(0);
	}
	if ((fp = fopen(_PATH_FTPWELCOME, "r")) != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL) {
			line[strcspn(line, "\n")] = '\0';
			lreply(220, "%s", line);
		}
		(void) fclose(fp);
		/* reply(220,) must follow */
	}
	(void) gethostname(hostname, sizeof(hostname));

	/* Make sure hostname is fully qualified. */
	hp = gethostbyname(hostname);
	if (hp != NULL)
		strlcpy(hostname, hp->h_name, sizeof(hostname));

	if (multihome) {
		error = getnameinfo((struct sockaddr *)&ctrl_addr,
		    ctrl_addr.su_len, dhostname, sizeof(dhostname), NULL, 0, 0);
	}

	if (error != 0)
		reply(220, "FTP server ready.");
	else
		reply(220, "%s FTP server ready.",
		    (multihome ? dhostname : hostname));

	monitor_init();

	for (;;)
		(void) yyparse();
	/* NOTREACHED */
}

/*
 * Signal handlers.
 */
static void
lostconn(int signo)
{
	struct syslog_data sdata = SYSLOG_DATA_INIT;

	sdata.log_fac = LOG_FTP;
	if (debug)
		syslog_r(LOG_DEBUG, &sdata, "lost connection");
	dologout(1);
}

static void
sigquit(int signo)
{
	struct syslog_data sdata = SYSLOG_DATA_INIT;

	sdata.log_fac = LOG_FTP;
	syslog_r(LOG_DEBUG, &sdata, "got signal %s", sys_signame[signo]);
	dologout(1);
}

/*
 * Save the result of a getpwnam.  Used for USER command, since
 * the data returned must not be clobbered by any other command
 * (e.g., globbing).
 */
static struct passwd *
sgetpwnam(const char *name, struct passwd *pw)
{
	static struct passwd *save;
	struct passwd *old;

	if (pw == NULL && (pw = getpwnam(name)) == NULL)
		return (NULL);
	old = save;
	save = pw_dup(pw);
	if (save == NULL) {
		perror_reply(421, "Local resource failure: malloc");
		dologout(1);
		/* NOTREACHED */
	}
	if (old) {
		explicit_bzero(old->pw_passwd, strlen(old->pw_passwd));
		free(old);
	}
	return (save);
}

static int login_attempts;	/* number of failed login attempts */
static int askpasswd;		/* had user command, ask for passwd */
static char curname[LOGIN_NAME_MAX];	/* current USER name */

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
	char *cp, *shell, *style, *host;
	char *class = NULL;

	if (logged_in) {
		kill_slave("user already logged in");
		end_login();
	}

	/* Close session from previous user if there was one. */
	if (as) {
		auth_close(as);
		as = NULL;
	}
	if (lc) {
		login_close(lc);
		lc = NULL;
	}

	if ((style = strchr(name, ':')) != NULL)
		*style++ = 0;

	guest = 0;
	askpasswd = 0;
	host = multihome ? dhostname : hostname;
	if (anon_ok &&
	    (strcmp(name, "ftp") == 0 || strcmp(name, "anonymous") == 0)) {
		if (checkuser(_PATH_FTPUSERS, "ftp") ||
		    checkuser(_PATH_FTPUSERS, "anonymous"))
			reply(530, "User %s access denied.", name);
		else if ((pw = sgetpwnam("ftp", NULL)) != NULL) {
			if ((lc = login_getclass(pw->pw_class)) == NULL ||
			    (as = auth_open()) == NULL ||
			    auth_setpwd(as, pw) != 0 ||
			    auth_setoption(as, "FTPD_HOST", host) < 0) {
				if (as) {
					auth_close(as);
					as = NULL;
				}
				if (lc) {
					login_close(lc);
					lc = NULL;
				}
				reply(421, "Local resource failure");
				return;
			}
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

	shell = _PATH_BSHELL;
	if ((pw = sgetpwnam(name, NULL))) {
		class = pw->pw_class;
		if (pw->pw_shell != NULL && *pw->pw_shell != '\0')
			shell = pw->pw_shell;
		while ((cp = getusershell()) != NULL)
			if (strcmp(cp, shell) == 0)
				break;
		shell = cp;
		endusershell();
	}

	/* Get login class; if invalid style treat like unknown user. */
	lc = login_getclass(class);
	if (lc && (style = login_getstyle(lc, style, "auth-ftp")) == NULL) {
		login_close(lc);
		lc = NULL;
		pw = NULL;
	}

	/* Do pre-authentication setup. */
	if (lc && ((as = auth_open()) == NULL ||
	    (pw != NULL && auth_setpwd(as, pw) != 0) ||
	    auth_setitem(as, AUTHV_STYLE, style) < 0 ||
	    auth_setitem(as, AUTHV_NAME, name) < 0 ||
	    auth_setitem(as, AUTHV_CLASS, class) < 0 ||
	    auth_setoption(as, "login", "yes") < 0 ||
	    auth_setoption(as, "notickets", "yes") < 0 ||
	    auth_setoption(as, "FTPD_HOST", host) < 0)) {
		if (as) {
			auth_close(as);
			as = NULL;
		}
		login_close(lc);
		lc = NULL;
		reply(421, "Local resource failure");
		return;
	}
	if (logging)
		strlcpy(curname, name, sizeof(curname));

	dochroot = (lc && login_getcapbool(lc, "ftp-chroot", 0)) ||
	    checkuser(_PATH_FTPCHROOT, name);
	if (anon_only && !dochroot) {
		reply(530, "User %s access denied.", name);
		return;
	}
	if (pw) {
		if (pw->pw_uid < minuid) {
			reply(530, "User %s access denied.", name);
			if (logging)
				syslog(LOG_NOTICE,
				    "FTP LOGIN REFUSED FROM %s, %s (UID))",
				    remotehost, name);
			return;
		}
		if ((!shell && !dochroot) || checkuser(_PATH_FTPUSERS, name)) {
			reply(530, "User %s access denied.", name);
			if (logging)
				syslog(LOG_NOTICE,
				    "FTP LOGIN REFUSED FROM %s, %s",
				    remotehost, name);
			pw = NULL;
			return;
		}
	}

	if (as != NULL && (cp = auth_challenge(as)) != NULL)
		reply(331, "%s", cp);
	else
		reply(331, "Password required for %s.", name);

	askpasswd = 1;
	/*
	 * Delay before reading passwd after first failed
	 * attempt to slow down passwd-guessing programs.
	 */
	if (login_attempts)
		sleep((unsigned) login_attempts);
}

/*
 * Check if a user is in the file "fname"
 */
static int
checkuser(char *fname, const char *name)
{
	FILE *fp;
	int found = 0;
	char *p, line[BUFSIZ];

	if ((fp = fopen(fname, "r")) != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL)
			if ((p = strchr(line, '\n')) != NULL) {
				*p = '\0';
				if (line[0] == '#')
					continue;
				if (strcmp(line, name) == 0) {
					found = 1;
					break;
				}
			}
		(void) fclose(fp);
	}
	return (found);
}

/*
 * Terminate login as previous user, if any, resetting state;
 * used when USER command is given or login fails.
 */
static void
end_login(void)
{
	sigprocmask (SIG_BLOCK, &allsigs, NULL);
	if (logged_in) {
		if (!nowtmp)
			ftpdlogwtmp(ttyline, "", "");
		if (doutmp)
			ftpd_logout(utmp.ut_line);
	}
	reply(530, "Please reconnect to work as another user");
	_exit(0);
}

enum auth_ret
pass(char *passwd)
{
	int authok;
	unsigned int flags;
	FILE *fp;
	static char homedir[PATH_MAX];
	char *motd, *dir, rootdir[PATH_MAX];
	size_t sz_pw_dir;

	if (logged_in || askpasswd == 0) {
		reply(503, "Login with USER first.");
		return (AUTH_FAILED);
	}
	askpasswd = 0;
	if (!guest) {		/* "ftp" is only account allowed no password */
		authok = 0;
		if (pw == NULL || pw->pw_passwd[0] == '\0') {
			useconds_t us;

			/* Sleep between 1 and 3 seconds to emulate a crypt. */
			us = arc4random_uniform(3000000);
			usleep(us);
			if (as != NULL) {
				auth_close(as);
				as = NULL;
			}
		} else {
			authok = auth_userresponse(as, passwd, 0);
			as = NULL;
		}
		if (authok == 0) {
			reply(530, "Login incorrect.");
			if (logging)
				syslog(LOG_NOTICE,
				    "FTP LOGIN FAILED FROM %s, %s",
				    remotehost, curname);
			pw = NULL;
			if (login_attempts++ >= 5) {
				syslog(LOG_NOTICE,
				    "repeated login failures from %s",
				    remotehost);
				kill_slave("repeated login failures");
				_exit(0);
			}
			return (AUTH_FAILED);
		}
	} else if (lc != NULL) {
		/* Save anonymous' password. */
		free(guestpw);
		guestpw = strdup(passwd);
		if (guestpw == NULL) {
			kill_slave("out of mem");
			fatal("Out of memory.");
		}

		authok = auth_approval(as, lc, pw->pw_name, "ftp");
		auth_close(as);
		as = NULL;
		if (authok == 0) {
			syslog(LOG_INFO|LOG_AUTH,
			    "FTP LOGIN FAILED (HOST) as %s: approval failure.",
			    pw->pw_name);
			reply(530, "Approval failure.");
			kill_slave("approval failure");
			_exit(0);
		}
	} else {
		syslog(LOG_INFO|LOG_AUTH,
		    "FTP LOGIN CLASS %s MISSING for %s: approval failure.",
		    pw->pw_class, pw->pw_name);
		reply(530, "Permission denied.");
		kill_slave("permission denied");
		_exit(0);
	}

	if (monitor_post_auth() == 1) {
		/* Post-auth monitor process */
		logged_in = 1;
		return (AUTH_MONITOR);
	}

	login_attempts = 0;		/* this time successful */
	/* set umask via setusercontext() unless -u flag was given. */
	flags = LOGIN_SETGROUP|LOGIN_SETPRIORITY|LOGIN_SETRESOURCES;
	if (umaskchange)
		flags |= LOGIN_SETUMASK;
	else
		(void) umask(defumask);
	if (setusercontext(lc, pw, 0, flags) != 0) {
		perror_reply(421, "Local resource failure: setusercontext");
		syslog(LOG_NOTICE, "setusercontext: %m");
		dologout(1);
		/* NOTREACHED */
	}

	/* open wtmp before chroot */
	if (!nowtmp)
		ftpdlogwtmp(ttyline, pw->pw_name, remotehost);

	/* open utmp before chroot */
	if (doutmp) {
		memset(&utmp, 0, sizeof(utmp));
		(void)time(&utmp.ut_time);
		(void)strncpy(utmp.ut_name, pw->pw_name, sizeof(utmp.ut_name));
		(void)strncpy(utmp.ut_host, remotehost, sizeof(utmp.ut_host));
		(void)strncpy(utmp.ut_line, ttyline, sizeof(utmp.ut_line));
		ftpd_login(&utmp);
	}

	/* open stats file before chroot */
	if (guest && (stats == 1) && (statfd < 0))
		if ((statfd = open(_PATH_FTPDSTATFILE, O_WRONLY|O_APPEND)) == -1)
			stats = 0;

	logged_in = 1;

	if ((dir = login_getcapstr(lc, "ftp-dir", NULL, NULL))) {
		char *newdir;

		newdir = copy_dir(dir, pw);
		if (newdir == NULL) {
			perror_reply(421, "Local resource failure: malloc");
			dologout(1);
			/* NOTREACHED */
		}
		pw->pw_dir = newdir;
		pw = sgetpwnam(NULL, pw);
		free(dir);
		free(newdir);
	}

	/* make sure pw->pw_dir is big enough to hold "/" */
	sz_pw_dir = strlen(pw->pw_dir) + 1;
	if (sz_pw_dir < 2) {
		pw->pw_dir = "/";
		pw = sgetpwnam(NULL, pw);
		sz_pw_dir = 2;
	}

	if (guest || dochroot) {
		if (multihome && guest) {
			struct stat ts;

			/* Compute root directory. */
			snprintf(rootdir, sizeof(rootdir), "%s/%s",
			    pw->pw_dir, dhostname);
			if (stat(rootdir, &ts) == -1) {
				snprintf(rootdir, sizeof(rootdir), "%s/%s",
				    pw->pw_dir, hostname);
			}
		} else
			strlcpy(rootdir, pw->pw_dir, sizeof(rootdir));
	}
	if (guest) {
		/*
		 * We MUST do a chdir() after the chroot. Otherwise
		 * the old current directory will be accessible as "."
		 * outside the new root!
		 */
		if (chroot(rootdir) == -1 || chdir("/") == -1) {
			reply(550, "Can't set guest privileges.");
			goto bad;
		}
		strlcpy(pw->pw_dir, "/", sz_pw_dir);
		if (setenv("HOME", "/", 1) == -1) {
			reply(550, "Can't setup environment.");
			goto bad;
		}
	} else if (dochroot) {
		if (chroot(rootdir) == -1 || chdir("/") == -1) {
			reply(550, "Can't change root.");
			goto bad;
		}
		strlcpy(pw->pw_dir, "/", sz_pw_dir);
		if (setenv("HOME", "/", 1) == -1) {
			reply(550, "Can't setup environment.");
			goto bad;
		}
	} else if (chdir(pw->pw_dir) == -1) {
		if (chdir("/") == -1) {
			reply(530, "User %s: can't change directory to %s.",
			    pw->pw_name, pw->pw_dir);
			goto bad;
		} else
			lreply(230, "No directory! Logging in with home=/");
	}
	if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) == -1) {
		reply(550, "Can't set gid.");
		goto bad;
	}
	if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) == -1) {
		reply(550, "Can't set uid.");
		goto bad;
	}
	sigprocmask(SIG_UNBLOCK, &allsigs, NULL);

	/*
	 * Set home directory so that use of ~ (tilde) works correctly.
	 */
	if (getcwd(homedir, PATH_MAX) != NULL) {
		if (setenv("HOME", homedir, 1) == -1) {
			reply(550, "Can't setup environment.");
			goto bad;
		}
	}

	/*
	 * Display a login message, if it exists.
	 * N.B. reply(230,) must follow the message.
	 */
	motd = login_getcapstr(lc, "welcome", NULL, NULL);
	if ((fp = fopen(motd ? motd : _PATH_FTPLOGINMESG, "r")) != NULL) {
		char line[LINE_MAX];

		while (fgets(line, sizeof(line), fp) != NULL) {
			line[strcspn(line, "\n")] = '\0';
			lreply(230, "%s", line);
		}
		(void) fclose(fp);
	}
	free(motd);
	if (guest) {
		reply(230, "Guest login ok, access restrictions apply.");
		snprintf(proctitle, sizeof(proctitle),
		    "%s: anonymous/%.*s", remotehost,
		    (int)(sizeof(proctitle) - sizeof(remotehost) -
		    sizeof(": anonymous/")), passwd);
		setproctitle("%s", proctitle);
		if (logging)
			syslog(LOG_INFO, "ANONYMOUS FTP LOGIN FROM %s, %s",
			    remotehost, passwd);
	} else {
		reply(230, "User %s logged in.", pw->pw_name);
		snprintf(proctitle, sizeof(proctitle),
		    "%s: %s", remotehost, pw->pw_name);
		setproctitle("%s", proctitle);
		if (logging)
			syslog(LOG_INFO, "FTP LOGIN FROM %s as %s",
			    remotehost, pw->pw_name);
	}
	login_close(lc);
	lc = NULL;
	return (AUTH_SLAVE);
bad:
	/* Forget all about it... */
	login_close(lc);
	lc = NULL;
	end_login();
	return (AUTH_FAILED);
}

void
retrieve(enum ret_cmd cmd, const char *name)
{
	FILE *fin, *dout;
	struct stat st;
	pid_t pid;
	time_t start;

	if (cmd == RET_FILE) {
		fin = fopen(name, "r");
		st.st_size = 0;
	} else {
		fin = ftpd_ls(name, &pid);
		st.st_size = -1;
		st.st_blksize = BUFSIZ;
	}
	if (fin == NULL) {
		if (errno != 0) {
			perror_reply(550, name);
			if (cmd == RET_FILE) {
				LOGCMD("get", name);
			}
		}
		return;
	}
	byte_count = -1;
	if (cmd == RET_FILE &&
	    (fstat(fileno(fin), &st) == -1 || !S_ISREG(st.st_mode))) {
		reply(550, "%s: not a plain file.", name);
		goto done;
	}
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c = getc(fin)) == EOF) {
					if (ferror(fin)) {
						perror_reply(550, name);
						goto done;
					} else
						break;
				}
				if (c == '\n')
					i++;
			}
		} else if (lseek(fileno(fin), restart_point, SEEK_SET) == -1) {
			perror_reply(550, name);
			goto done;
		}
	}
	dout = dataconn(name, st.st_size, "w");
	if (dout == NULL)
		goto done;
	time(&start);
	send_data(fin, dout, st.st_blksize, st.st_size,
	    (restart_point == 0 && cmd == RET_FILE && S_ISREG(st.st_mode)));
	if ((cmd == RET_FILE) && stats)
		logxfer(name, byte_count, start);
	(void) fclose(dout);
	data = -1;
done:
	if (pdata >= 0)
		(void) close(pdata);
	pdata = -1;
	if (cmd == RET_FILE) {
		LOGBYTES("get", name, byte_count);
		fclose(fin);
	} else {
		ftpd_pclose(fin, pid);
	}
}

void
store(const char *name, const char *mode, int unique)
{
	FILE *fout, *din;
	int (*closefunc)(FILE *);
	struct stat st;
	int fd;

	if (restart_point && *mode != 'a')
		mode = "r+";

	if (unique && stat(name, &st) == 0) {
		char *nam;

		fd = guniquefd(name, &nam);
		if (fd == -1) {
			LOGCMD(*mode == 'w' ? "put" : "append", name);
			return;
		}
		name = nam;
		fout = fdopen(fd, mode);
	} else
		fout = fopen(name, mode);

	closefunc = fclose;
	if (fout == NULL) {
		perror_reply(553, name);
		LOGCMD(*mode == 'w' ? "put" : "append", name);
		return;
	}
	byte_count = -1;
	if (restart_point) {
		if (type == TYPE_A) {
			off_t i, n;
			int c;

			n = restart_point;
			i = 0;
			while (i++ < n) {
				if ((c = getc(fout)) == EOF) {
					if (ferror(fout)) {
						perror_reply(550, name);
						goto done;
					} else
						break;
				}
				if (c == '\n')
					i++;
			}
			/*
			 * We must do this seek to "current" position
			 * because we are changing from reading to
			 * writing.
			 */
			if (fseek(fout, 0, SEEK_CUR) == -1) {
				perror_reply(550, name);
				goto done;
			}
		} else if (lseek(fileno(fout), restart_point, SEEK_SET) == -1) {
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
	LOGBYTES(*mode == 'w' ? "put" : "append", name, byte_count);
	(*closefunc)(fout);
}

static FILE *
getdatasock(char *mode)
{
	int opt, s, t, tries;

	if (data >= 0)
		return (fdopen(data, mode));
	sigprocmask (SIG_BLOCK, &allsigs, NULL);
	s = monitor_socket(ctrl_addr.su_family);
	if (s < 0)
		goto bad;
	opt = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
	    &opt, sizeof(opt)) == -1)
		goto bad;
	/* anchor socket to avoid multi-homing problems */
	data_source = ctrl_addr;
	data_source.su_port = htons(20); /* ftp-data port */
	for (tries = 1; ; tries++) {
		if (monitor_bind(s, (struct sockaddr *)&data_source,
		    data_source.su_len) >= 0)
			break;
		if (errno != EADDRINUSE || tries > 10)
			goto bad;
		sleep((unsigned int)tries);
	}
	sigprocmask (SIG_UNBLOCK, &allsigs, NULL);

	opt = IPTOS_THROUGHPUT;
	switch (ctrl_addr.su_family) {
	case AF_INET:
		if (setsockopt(s, IPPROTO_IP, IP_TOS, &opt,
		    sizeof(opt)) == -1)
			syslog(LOG_WARNING, "setsockopt (IP_TOS): %m");
		break;
	case AF_INET6:
		if (setsockopt(s, IPPROTO_IPV6, IPV6_TCLASS, &opt,
		    sizeof(opt)) == -1)
			syslog(LOG_WARNING, "setsockopt (IPV6_TCLASS): %m");
		break;
	}
	/*
	 * Turn off push flag to keep sender TCP from sending short packets
	 * at the boundaries of each write().  Should probably do a SO_SNDBUF
	 * to set the send buffer size as well, but that may not be desirable
	 * in heavy-load situations.
	 */
	opt = 1;
	if (setsockopt(s, IPPROTO_TCP, TCP_NOPUSH, &opt, sizeof(opt)) == -1)
		syslog(LOG_WARNING, "setsockopt (TCP_NOPUSH): %m");
	opt = 65536;
	if (setsockopt(s, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt)) == -1)
		syslog(LOG_WARNING, "setsockopt (SO_SNDBUF): %m");

	return (fdopen(s, mode));
bad:
	/* Return the real value of errno (close may change it) */
	t = errno;
	sigprocmask (SIG_UNBLOCK, &allsigs, NULL);
	if (s >= 0)
		(void) close(s);
	errno = t;
	return (NULL);
}

static FILE *
dataconn(const char *name, off_t size, char *mode)
{
	char sizebuf[32];
	FILE *file = NULL;
	int retry = 0;
	in_port_t *p;
	u_char *fa, *ha;
	size_t alen;
	int error;

	file_size = size;
	byte_count = 0;
	if (size != -1) {
		(void) snprintf(sizebuf, sizeof(sizebuf), " (%lld bytes)",
		    (long long)size);
	} else
		sizebuf[0] = '\0';
	if (pdata >= 0) {
		union sockunion from;
		int s;
		socklen_t fromlen = sizeof(from);

		(void) alarm ((unsigned) timeout);
		s = accept(pdata, (struct sockaddr *)&from, &fromlen);
		(void) alarm (0);
		if (s == -1) {
			reply(425, "Can't open data connection.");
			(void) close(pdata);
			pdata = -1;
			return (NULL);
		}
		switch (from.su_family) {
		case AF_INET:
			p = (in_port_t *)&from.su_sin.sin_port;
			fa = (u_char *)&from.su_sin.sin_addr;
			ha = (u_char *)&his_addr.su_sin.sin_addr;
			alen = sizeof(struct in_addr);
			break;
		case AF_INET6:
			p = (in_port_t *)&from.su_sin6.sin6_port;
			fa = (u_char *)&from.su_sin6.sin6_addr;
			ha = (u_char *)&his_addr.su_sin6.sin6_addr;
			alen = sizeof(struct in6_addr);
			break;
		default:
			reply(425, "Can't build data connection: "
			    "unknown address family");
			(void) close(pdata);
			(void) close(s);
			pdata = -1;
			return (NULL);
		}
		if (from.su_family != his_addr.su_family ||
		    ntohs(*p) < IPPORT_RESERVED) {
			reply(425, "Can't build data connection: "
			    "address family or port error");
			(void) close(pdata);
			(void) close(s);
			pdata = -1;
			return (NULL);
		}
		if (portcheck && memcmp(fa, ha, alen) != 0) {
			reply(425, "Can't build data connection: "
			    "illegal port number");
			(void) close(pdata);
			(void) close(s);
			pdata = -1;
			return (NULL);
		}
		(void) close(pdata);
		pdata = s;
		reply(150, "Opening %s mode data connection for '%s'%s.",
		    type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
		return (fdopen(pdata, mode));
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
		if (file != NULL)
			(void) fclose(file);
		file = getdatasock(mode);
		if (file == NULL) {
			char hbuf[HOST_NAME_MAX+1], pbuf[10];

			error = getnameinfo((struct sockaddr *)&data_source,
			    data_source.su_len, hbuf, sizeof(hbuf), pbuf,
			    sizeof(pbuf), NI_NUMERICHOST | NI_NUMERICSERV);
			if (error != 0)
				reply(425, "Can't create data socket: %s.",
				    strerror(errno));
			else
				reply(425,
				    "Can't create data socket (%s,%s): %s.",
				    hbuf, pbuf, strerror(errno));
			return (NULL);
		}

		/*
		 * attempt to connect to reserved port on client machine;
		 * this looks like an attack
		 */
		switch (data_dest.su_family) {
		case AF_INET:
			p = (in_port_t *)&data_dest.su_sin.sin_port;
			fa = (u_char *)&data_dest.su_sin.sin_addr;
			ha = (u_char *)&his_addr.su_sin.sin_addr;
			alen = sizeof(struct in_addr);
			break;
		case AF_INET6:
			p = (in_port_t *)&data_dest.su_sin6.sin6_port;
			fa = (u_char *)&data_dest.su_sin6.sin6_addr;
			ha = (u_char *)&his_addr.su_sin6.sin6_addr;
			alen = sizeof(struct in6_addr);
			break;
		default:
			reply(425, "Can't build data connection: "
			    "unknown address family");
			(void) fclose(file);
			pdata = -1;
			return (NULL);
		}
		if (data_dest.su_family != his_addr.su_family ||
		    ntohs(*p) < IPPORT_RESERVED || ntohs(*p) == 2049) { /* XXX */
			reply(425, "Can't build data connection: "
			    "address family or port error");
			(void) fclose(file);
			return NULL;
		}
		if (portcheck && memcmp(fa, ha, alen) != 0) {
			reply(435, "Can't build data connection: "
			    "illegal port number");
			(void) fclose(file);
			return NULL;
		}

		if (connect(fileno(file), (struct sockaddr *)&data_dest,
		    data_dest.su_len) == 0) {
			reply(150, "Opening %s mode data connection for '%s'%s.",
			    type == TYPE_A ? "ASCII" : "BINARY", name, sizebuf);
			data = fileno(file);
			return (file);
		}
		if (errno != EADDRINUSE)
			break;
		sleep((unsigned) swaitint);
		retry += swaitint;
	} while (retry <= swaitmax);
	perror_reply(425, "Can't build data connection");
	(void) fclose(file);
	return (NULL);
}

/*
 * Transfer the contents of "instr" to "outstr" peer using the appropriate
 * encapsulation of the data subject to Mode, Structure, and Type.
 *
 * NB: Form isn't handled.
 */
static int
send_data(FILE *instr, FILE *outstr, off_t blksize, off_t filesize, int isreg)
{
	int c, cnt, filefd, netfd;
	char *buf, *bp;
	size_t len;

	transflag++;
	switch (type) {

	case TYPE_A:
		while ((c = getc(instr)) != EOF) {
			if (recvurg)
				goto got_oob;
			byte_count++;
			if (c == '\n') {
				if (ferror(outstr))
					goto data_err;
				(void) putc('\r', outstr);
			}
			(void) putc(c, outstr);
		}
		fflush(outstr);
		transflag = 0;
		if (ferror(instr))
			goto file_err;
		if (ferror(outstr))
			goto data_err;
		reply(226, "Transfer complete.");
		return(0);

	case TYPE_I:
	case TYPE_L:
		/*
		 * isreg is only set if we are not doing restart and we
		 * are sending a regular file
		 */
		netfd = fileno(outstr);
		filefd = fileno(instr);

		if (isreg && filesize < 16 * 1024 * 1024) {
			size_t fsize = (size_t)filesize;

			if (fsize == 0) {
				transflag = 0;
				reply(226, "Transfer complete.");
				return(0);
			}

			buf = mmap(0, fsize, PROT_READ, MAP_SHARED, filefd, 0);
			if (buf == MAP_FAILED) {
				syslog(LOG_WARNING, "mmap(%llu): %m",
				    (unsigned long long)fsize);
				goto oldway;
			}
			bp = buf;
			len = fsize;
			do {
				cnt = write(netfd, bp, len);
				if (recvurg) {
					munmap(buf, fsize);
					goto got_oob;
				}
				len -= cnt;
				bp += cnt;
				if (cnt > 0)
					byte_count += cnt;
			} while(cnt > 0 && len > 0);

			transflag = 0;
			munmap(buf, fsize);
			if (cnt < 0)
				goto data_err;
			reply(226, "Transfer complete.");
			return(0);
		}

oldway:
		if ((buf = malloc((size_t)blksize)) == NULL) {
			transflag = 0;
			perror_reply(451, "Local resource failure: malloc");
			return(-1);
		}

		while ((cnt = read(filefd, buf, (size_t)blksize)) > 0 &&
		    write(netfd, buf, cnt) == cnt)
			byte_count += cnt;
		transflag = 0;
		(void)free(buf);
		if (cnt != 0) {
			if (cnt == -1)
				goto file_err;
			goto data_err;
		}
		reply(226, "Transfer complete.");
		return(0);
	default:
		transflag = 0;
		reply(550, "Unimplemented TYPE %d in send_data", type);
		return(-1);
	}

data_err:
	transflag = 0;
	reply(426, "Data connection");
	return(-1);

file_err:
	transflag = 0;
	reply(551, "Error on input file");
	return(-1);

got_oob:
	myoob();
	recvurg = 0;
	transflag = 0;
	return(-1);
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
	int c;
	int cnt;
	char buf[BUFSIZ];
	struct sigaction sa, sa_saved;
	volatile int bare_lfs = 0;

	transflag++;
	switch (type) {

	case TYPE_I:
	case TYPE_L:
		memset(&sa, 0, sizeof(sa));
		sigfillset(&sa.sa_mask);
		sa.sa_flags = SA_RESTART;
		sa.sa_handler = lostconn;
		(void) sigaction(SIGALRM, &sa, &sa_saved);
		do {
			(void) alarm ((unsigned) timeout);
			cnt = read(fileno(instr), buf, sizeof(buf));
			(void) alarm (0);
			if (recvurg)
				goto got_oob;

			if (cnt > 0) {
				if (write(fileno(outstr), buf, cnt) != cnt)
					goto file_err;
				byte_count += cnt;
			}
		} while (cnt > 0);
		(void) sigaction(SIGALRM, &sa_saved, NULL);
		if (cnt == -1)
			goto data_err;
		transflag = 0;
		return (0);

	case TYPE_E:
		reply(553, "TYPE E not implemented.");
		transflag = 0;
		return (-1);

	case TYPE_A:
		while ((c = getc(instr)) != EOF) {
			if (recvurg)
				goto got_oob;
			byte_count++;
			if (c == '\n')
				bare_lfs++;
			while (c == '\r') {
				if (ferror(outstr))
					goto data_err;
				if ((c = getc(instr)) != '\n') {
					(void) putc ('\r', outstr);
					if (c == '\0' || c == EOF)
						goto contin2;
				}
			}
			(void) putc(c, outstr);
	contin2:	;
		}
		fflush(outstr);
		if (ferror(instr))
			goto data_err;
		if (ferror(outstr))
			goto file_err;
		transflag = 0;
		if (bare_lfs) {
			lreply(226,
			    "WARNING! %d bare linefeeds received in ASCII mode",
			    bare_lfs);
			printf("   File may not have transferred correctly.\r\n");
		}
		return (0);
	default:
		reply(550, "Unimplemented TYPE %d in receive_data", type);
		transflag = 0;
		return (-1);
	}

data_err:
	transflag = 0;
	reply(426, "Data Connection");
	return (-1);

file_err:
	transflag = 0;
	reply(452, "Error writing file");
	return (-1);

got_oob:
	myoob();
	recvurg = 0;
	transflag = 0;
	return (-1);
}

void
statfilecmd(const char *filename)
{
	FILE *fin;
	int c;
	int atstart;
	pid_t pid;
	fin = ftpd_ls(filename, &pid);
	if (fin == NULL) {
		reply(451, "Local resource failure");
		return;
	}
	lreply(211, "status of %s:", filename);
	atstart = 1;
	while ((c = getc(fin)) != EOF) {
		if (c == '\n') {
			if (ferror(stdout)){
				perror_reply(421, "control connection");
				(void) ftpd_pclose(fin, pid);
				dologout(1);
				/* NOTREACHED */
			}
			if (ferror(fin)) {
				perror_reply(551, filename);
				(void) ftpd_pclose(fin, pid);
				return;
			}
			(void) putc('\r', stdout);
		}
		if (atstart && isdigit(c))
			(void) putc(' ', stdout);
		(void) putc(c, stdout);
		atstart = (c == '\n');
	}
	(void) ftpd_pclose(fin, pid);
	reply(211, "End of Status");
}

void
statcmd(void)
{
	union sockunion *su;
	u_char *a, *p;
	char hbuf[HOST_NAME_MAX+1];
	int ispassive;
	int error;

	lreply(211, "%s FTP server status:", hostname);
	error = getnameinfo((struct sockaddr *)&his_addr, his_addr.su_len,
	    hbuf, sizeof(hbuf), NULL, 0, NI_NUMERICHOST);
	printf("     Connected to %s", remotehost);
	if (error == 0 && strcmp(remotehost, hbuf) != 0)
		printf(" (%s)", hbuf);
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
		printf(" 8");
	printf("; STRUcture: %s; transfer MODE: %s\r\n",
	    strunames[stru], modenames[mode]);
	ispassive = 0;
	if (data != -1)
		printf("     Data connection open\r\n");
	else if (pdata != -1) {
		printf("     in Passive mode\r\n");
		su = (union sockunion *)&pasv_addr;
		ispassive++;
		goto printaddr;
	} else if (usedefault == 0) {
		size_t alen, i;
		int af;

		su = (union sockunion *)&data_dest;
printaddr:
		/* PASV/PORT */
		if (su->su_family == AF_INET) {
			if (ispassive)
				printf("211- PASV ");
			else
				printf("211- PORT ");
			a = (u_char *)&su->su_sin.sin_addr;
			p = (u_char *)&su->su_sin.sin_port;
			printf("(%u,%u,%u,%u,%u,%u)\r\n",
			    a[0], a[1], a[2], a[3],
			    p[0], p[1]);
		}

		/* LPSV/LPRT */
		alen = 0;
		switch (su->su_family) {
		case AF_INET:
			a = (u_char *)&su->su_sin.sin_addr;
			p = (u_char *)&su->su_sin.sin_port;
			alen = sizeof(su->su_sin.sin_addr);
			af = 4;
			break;
		case AF_INET6:
			a = (u_char *)&su->su_sin6.sin6_addr;
			p = (u_char *)&su->su_sin6.sin6_port;
			alen = sizeof(su->su_sin6.sin6_addr);
			af = 6;
			break;
		default:
			af = 0;
			break;
		}
		if (af) {
			if (ispassive)
				printf("211- LPSV ");
			else
				printf("211- LPRT ");
			printf("(%u,%llu", af, (unsigned long long)alen);
			for (i = 0; i < alen; i++)
				printf(",%u", a[i]);
			printf(",%u,%u,%u)\r\n", 2, p[0], p[1]);
		}

		/* EPRT/EPSV */
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
			char pbuf[10];
			union sockunion tmp = *su;

			if (tmp.su_family == AF_INET6)
				tmp.su_sin6.sin6_scope_id = 0;
			if (getnameinfo((struct sockaddr *)&tmp, tmp.su_len,
			    hbuf, sizeof(hbuf), pbuf, sizeof(pbuf),
			    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
				if (ispassive)
					printf("211- EPSV ");
				else
					printf("211- EPRT ");
				printf("(|%u|%s|%s|)\r\n",
				    af, hbuf, pbuf);
			}
		}
	} else
		printf("     No data connection\r\n");
	reply(211, "End of status");
}

void
fatal(const char *s)
{

	reply(451, "Error in server: %s", s);
	reply(221, "Closing connection due to server error.");
	dologout(0);
	/* NOTREACHED */
}

void
reply(int n, const char *fmt, ...)
{
	char *buf, *p, *next;
	int rval;
	va_list ap;

	va_start(ap, fmt);
	rval = vasprintf(&buf, fmt, ap);
	va_end(ap);
	if (rval == -1 || buf == NULL) {
		printf("421 Local resource failure: malloc\r\n");
		fflush(stdout);
		dologout(1);
	}
	next = buf;
	while ((p = strsep(&next, "\n\r"))) {
		printf("%d%s %s\r\n", n, (next != NULL) ? "-" : "", p);
		if (debug)
			syslog(LOG_DEBUG, "<--- %d%s %s", n,
			    (next != NULL) ? "-" : "", p);
	}
	(void)fflush(stdout);
	free(buf);
}


void
reply_r(int n, const char *fmt, ...)
{
	char *p, *next;
	char msg[BUFSIZ];
	char buf[BUFSIZ];
	va_list ap;
	struct syslog_data sdata = SYSLOG_DATA_INIT;

	sdata.log_fac = LOG_FTP;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	next = msg;

	while ((p = strsep(&next, "\n\r"))) {
		snprintf(buf, sizeof(buf), "%d%s %s\r\n", n,
		    (next != NULL) ? "-" : "", p);
		write(STDOUT_FILENO, buf, strlen(buf));
		if (debug) {
			buf[strlen(buf) - 2] = '\0';
			syslog_r(LOG_DEBUG, &sdata, "<--- %s", buf);
		}
	}
}

void
lreply(int n, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)printf("%d- ", n);
	(void)vprintf(fmt, ap);
	va_end(ap);
	(void)printf("\r\n");
	(void)fflush(stdout);
	if (debug) {
		va_start(ap, fmt);
		syslog(LOG_DEBUG, "<--- %d- ", n);
		vsyslog(LOG_DEBUG, fmt, ap);
		va_end(ap);
	}
}

static void
ack(const char *s)
{

	reply(250, "%s command successful.", s);
}

void
nack(const char *s)
{

	reply(502, "%s command not implemented.", s);
}

void
yyerror(char *s)
{
	cbuf[strcspn(cbuf, "\n")] = '\0';
	reply(500, "'%s': command not understood.", cbuf);
}

void
delete(const char *name)
{
	struct stat st;

	LOGCMD("delete", name);
	if (stat(name, &st) == -1) {
		perror_reply(550, name);
		return;
	}
	if ((st.st_mode&S_IFMT) == S_IFDIR) {
		if (rmdir(name) == -1) {
			perror_reply(550, name);
			return;
		}
		goto done;
	}
	if (unlink(name) == -1) {
		perror_reply(550, name);
		return;
	}
done:
	ack("DELE");
}

void
cwd(char *path)
{
	FILE *message;

	if (chdir(path) == -1)
		perror_reply(550, path);
	else {
		if ((message = fopen(_PATH_CWDMESG, "r")) != NULL) {
			char line[LINE_MAX];

			while (fgets(line, sizeof(line), message) != NULL) {
				line[strcspn(line, "\n")] = '\0';
				lreply(250, "%s", line);
			}
			(void) fclose(message);
		}
		ack("CWD");
	}
}

void
replydirname(const char *name, const char *message)
{
	char *p, *ep;
	char npath[PATH_MAX * 2];

	p = npath;
	ep = &npath[sizeof(npath) - 1];
	while (*name) {
		if (*name == '"') {
			if (ep - p < 2)
				break;
			*p++ = *name++;
			*p++ = '"';
		} else {
			if (ep - p < 1)
				break;
			*p++ = *name++;
		}
	}
	*p = '\0';
	reply(257, "\"%s\" %s", npath, message);
}

void
makedir(const char *name)
{

	LOGCMD("mkdir", name);
	if (mkdir(name, 0777) == -1)
		perror_reply(550, name);
	else
		replydirname(name, "directory created.");
}

void
removedir(const char *name)
{

	LOGCMD("rmdir", name);
	if (rmdir(name) == -1)
		perror_reply(550, name);
	else
		ack("RMD");
}

void
pwd(void)
{
	char path[PATH_MAX];

	if (getcwd(path, sizeof(path)) == NULL)
		perror_reply(550, "Can't get current directory");
	else
		replydirname(path, "is current directory.");
}

char *
renamefrom(char *name)
{
	struct stat st;

	if (stat(name, &st) == -1) {
		perror_reply(550, name);
		return (NULL);
	}
	reply(350, "File exists, ready for destination name");
	return (name);
}

void
renamecmd(const char *from, const char *to)
{

	LOGCMD2("rename", from, to);
	if (rename(from, to) == -1)
		perror_reply(550, "rename");
	else
		ack("RNTO");
}

static void
dolog(struct sockaddr *sa)
{
	char hbuf[sizeof(remotehost)];

	if (getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf), NULL, 0, 0) == 0)
		(void) strlcpy(remotehost, hbuf, sizeof(remotehost));
	else
		(void) strlcpy(remotehost, "unknown", sizeof(remotehost));

	snprintf(proctitle, sizeof(proctitle), "%s: connected", remotehost);
	setproctitle("%s", proctitle);

	if (logging) {
		int error;
		error = getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf),
		    NULL, 0, NI_NUMERICHOST);
		syslog(LOG_INFO, "connection from %s [%s]", remotehost,
		    error ? gai_strerror(error) : hbuf);
	}
}

/*
 * Record logout in wtmp file and exit with supplied status.
 * NOTE: because this is called from signal handlers it cannot
 *       use stdio (or call other functions that use stdio).
 */
void
dologout(int status)
{

	transflag = 0;

	if (logged_in) {
		sigprocmask(SIG_BLOCK, &allsigs, NULL);
		if (!nowtmp)
			ftpdlogwtmp(ttyline, "", "");
		if (doutmp)
			ftpd_logout(utmp.ut_line);
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
myoob(void)
{
	char *cp;
	int ret;

	/* only process if transfer occurring */
	if (!transflag)
		return;
	cp = tmpline;
	ret = get_line(cp, sizeof(tmpline)-1);
	if (ret == -1) {
		reply(221, "You could at least say goodbye.");
		dologout(0);
	} else if (ret == -2) {
		/* Ignore truncated command */
		return;
	}
	upper(cp);
	if (strcmp(cp, "ABOR\r\n") == 0) {
		tmpline[0] = '\0';
		reply(426, "Transfer aborted. Data connection closed.");
		reply(226, "Abort successful");
	}
	if (strcmp(cp, "STAT\r\n") == 0) {
		tmpline[0] = '\0';
		if (file_size != -1)
			reply(213, "Status: %lld of %lld bytes transferred",
			    (long long)byte_count, (long long)file_size);
		else
			reply(213, "Status: %lld bytes transferred",
			    (long long)byte_count);
	}
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
	int on = 1;
	u_char *p, *a;

	if (pw == NULL) {
		reply(530, "Please login with USER and PASS");
		return;
	}
	if (pdata >= 0)
		close(pdata);
	/*
	 * XXX
	 * At this point, it would be nice to have an algorithm that
	 * inserted a growing delay in an attack scenario.  Such a thing
	 * would look like continual passive sockets being opened, but
	 * nothing serious being done with them.  They're not used to
	 * move data; the entire attempt is just to use tcp FIN_WAIT
	 * resources.
	 */
	pdata = socket(AF_INET, SOCK_STREAM, 0);
	if (pdata == -1) {
		perror_reply(425, "Can't open passive connection");
		return;
	}

	if (setsockopt(pdata, SOL_SOCKET, SO_KEEPALIVE,
	    &on, sizeof(on)) == -1)
		goto pasv_error;

	on = IP_PORTRANGE_HIGH;
	if (setsockopt(pdata, IPPROTO_IP, IP_PORTRANGE,
	    &on, sizeof(on)) == -1)
		goto pasv_error;

	pasv_addr = ctrl_addr;
	pasv_addr.su_sin.sin_port = 0;
	if (bind(pdata, (struct sockaddr *)&pasv_addr,
	    pasv_addr.su_len) == -1)
		goto pasv_error;

	len = sizeof(pasv_addr);
	if (getsockname(pdata, (struct sockaddr *)&pasv_addr, &len) == -1)
		goto pasv_error;
	if (listen(pdata, 1) == -1)
		goto pasv_error;
	a = (u_char *)&pasv_addr.su_sin.sin_addr;
	p = (u_char *)&pasv_addr.su_sin.sin_port;

	reply(227, "Entering Passive Mode (%u,%u,%u,%u,%u,%u)", a[0],
	    a[1], a[2], a[3], p[0], p[1]);
	return;

pasv_error:
	perror_reply(425, "Can't open passive connection");
	(void) close(pdata);
	pdata = -1;
	return;
}

int
epsvproto2af(int proto)
{

	switch (proto) {
	case 1:	return AF_INET;
	case 2:	return AF_INET6;
	default: return -1;
	}
}

int
af2epsvproto(int af)
{

	switch (af) {
	case AF_INET:	return 1;
	case AF_INET6:	return 2;
	default:	return -1;
	}
}

/*
 * 228 Entering Long Passive Mode (af, hal, h1, h2, h3,..., pal, p1, p2...)
 * 229 Entering Extended Passive Mode (|||port|)
 */
void
long_passive(const char *cmd, int pf)
{
	socklen_t len;
	int on = 1;
	u_char *p, *a;

	if (!logged_in) {
		syslog(LOG_NOTICE, "long passive but not logged in");
		reply(503, "Login with USER first.");
		return;
	}

	if (pf != PF_UNSPEC && ctrl_addr.su_family != pf) {
		/*
		 * XXX
		 * only EPRT/EPSV ready clients will understand this
		 */
		if (strcmp(cmd, "EPSV") != 0)
			reply(501, "Network protocol mismatch"); /*XXX*/
		else
			epsv_protounsupp("Network protocol mismatch");

		return;
	}

	if (pdata >= 0)
		close(pdata);
	/*
	 * XXX
	 * At this point, it would be nice to have an algorithm that
	 * inserted a growing delay in an attack scenario.  Such a thing
	 * would look like continual passive sockets being opened, but
	 * nothing serious being done with them.  They not used to move
	 * data; the entire attempt is just to use tcp FIN_WAIT
	 * resources.
	 */
	pdata = socket(ctrl_addr.su_family, SOCK_STREAM, 0);
	if (pdata == -1) {
		perror_reply(425, "Can't open passive connection");
		return;
	}

	if (setsockopt(pdata, SOL_SOCKET, SO_KEEPALIVE,
	    &on, sizeof(on)) == -1)
		goto pasv_error;

	switch (ctrl_addr.su_family) {
	case AF_INET:
		on = IP_PORTRANGE_HIGH;
		if (setsockopt(pdata, IPPROTO_IP, IP_PORTRANGE,
		    &on, sizeof(on)) == -1)
			goto pasv_error;
		break;
	case AF_INET6:
		on = IPV6_PORTRANGE_HIGH;
		if (setsockopt(pdata, IPPROTO_IPV6, IPV6_PORTRANGE,
		    &on, sizeof(on)) == -1)
			goto pasv_error;
		break;
	}

	pasv_addr = ctrl_addr;
	pasv_addr.su_port = 0;
	if (bind(pdata, (struct sockaddr *)&pasv_addr, pasv_addr.su_len) == -1)
		goto pasv_error;
	len = pasv_addr.su_len;
	if (getsockname(pdata, (struct sockaddr *)&pasv_addr, &len) == -1)
		goto pasv_error;
	if (listen(pdata, 1) == -1)
		goto pasv_error;
	p = (u_char *)&pasv_addr.su_port;

	if (strcmp(cmd, "LPSV") == 0) {
		switch (pasv_addr.su_family) {
		case AF_INET:
			a = (u_char *)&pasv_addr.su_sin.sin_addr;
			reply(228,
			    "Entering Long Passive Mode (%u,%u,%u,%u,%u,%u,%u,%u,%u)",
			    4, 4, a[0], a[1], a[2], a[3], 2, p[0], p[1]);
			return;
		case AF_INET6:
			a = (u_char *)&pasv_addr.su_sin6.sin6_addr;
			reply(228,
			    "Entering Long Passive Mode (%u,%u,%u,%u,%u,%u,"
			    "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u)",
				6, 16, a[0], a[1], a[2], a[3], a[4],
				a[5], a[6], a[7], a[8], a[9], a[10],
				a[11], a[12], a[13], a[14], a[15],
				2, p[0], p[1]);
			return;
		}
	} else if (strcmp(cmd, "EPSV") == 0) {
		switch (pasv_addr.su_family) {
		case AF_INET:
		case AF_INET6:
			reply(229, "Entering Extended Passive Mode (|||%u|)",
			    ntohs(pasv_addr.su_port));
			return;
		}
	} else {
		/* more proper error code? */
	}

  pasv_error:
	perror_reply(425, "Can't open passive connection");
	(void) close(pdata);
	pdata = -1;
	return;
}

/*
 * EPRT |proto|addr|port|
 */
int
extended_port(const char *arg)
{
	char *tmp = NULL;
	char *result[3];
	char *p, *q;
	char delim;
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	int i;
	unsigned long proto;

	if (epsvall) {
		reply(501, "EPRT disallowed after EPSV ALL");
		return -1;
	}

	usedefault = 0;
	if (pdata >= 0) {
		(void) close(pdata);
		pdata = -1;
	}

	tmp = strdup(arg);
	if (!tmp) {
		fatal("not enough core.");
		/*NOTREACHED*/
	}
	p = tmp;
	delim = p[0];
	p++;
	memset(result, 0, sizeof(result));
	for (i = 0; i < 3; i++) {
		q = strchr(p, delim);
		if (!q || *q != delim)
			goto parsefail;
		*q++ = '\0';
		result[i] = p;
		p = q;
	}

	/* some more sanity check */
	p = NULL;
	(void)strtoul(result[2], &p, 10);
	if (!*result[2] || *p)
		goto protounsupp;
	p = NULL;
	proto = strtoul(result[0], &p, 10);
	if (!*result[0] || *p)
		goto protounsupp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = epsvproto2af((int)proto);
	if (hints.ai_family < 0)
		goto protounsupp;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST;	/*no DNS*/
	if (getaddrinfo(result[1], result[2], &hints, &res))
		goto parsefail;
	if (res->ai_next)
		goto parsefail;
	if (sizeof(data_dest) < res->ai_addrlen)
		goto parsefail;
	memcpy(&data_dest, res->ai_addr, res->ai_addrlen);
	if (his_addr.su_family == AF_INET6 &&
	    data_dest.su_family == AF_INET6) {
		/* XXX more sanity checks! */
		data_dest.su_sin6.sin6_scope_id =
		    his_addr.su_sin6.sin6_scope_id;
	}
	if (pdata >= 0) {
		(void) close(pdata);
		pdata = -1;
	}
	reply(200, "EPRT command successful.");

	free(tmp);
	if (res)
		freeaddrinfo(res);
	return 0;

parsefail:
	reply(500, "Invalid argument, rejected.");
	usedefault = 1;
	free(tmp);
	if (res)
		freeaddrinfo(res);
	return -1;

protounsupp:
	epsv_protounsupp("Protocol not supported");
	usedefault = 1;
	free(tmp);
	if (res)
		freeaddrinfo(res);
	return -1;
}

/*
 * 522 Protocol not supported (proto,...)
 * as we assume address family for control and data connections are the same,
 * we do not return the list of address families we support - instead, we
 * return the address family of the control connection.
 */
void
epsv_protounsupp(const char *message)
{
	int proto;

	proto = af2epsvproto(ctrl_addr.su_family);
	if (proto < 0)
		reply(501, "%s", message);	/*XXX*/
	else
		reply(522, "%s, use (%d)", message, proto);
}

/*
 * Generate unique name for file with basename "local".
 * The file named "local" is already known to exist.
 * Generates failure reply on error.
 */
static int
guniquefd(const char *local, char **nam)
{
	static char new[PATH_MAX];
	struct stat st;
	size_t len;
	int count, fd;
	char *cp;

	cp = strrchr(local, '/');
	if (cp)
		*cp = '\0';
	if (stat(cp ? local : ".", &st) == -1) {
		perror_reply(553, cp ? local : ".");
		return (-1);
	}
	if (cp)
		*cp = '/';
	len = strlcpy(new, local, sizeof(new));
	if (len+2+1 >= sizeof(new)-1)
		return (-1);
	cp = new + len;
	*cp++ = '.';
	for (count = 1; count < 100; count++) {
		(void)snprintf(cp, sizeof(new) - (cp - new), "%d", count);
		fd = open(new, O_RDWR|O_CREAT|O_EXCL, 0666);
		if (fd == -1)
			continue;
		if (nam)
			*nam = new;
		return (fd);
	}
	reply(452, "Unique file name cannot be created.");
	return (-1);
}

/*
 * Format and send reply containing system error number.
 */
void
perror_reply(int code, const char *string)
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
	char **dirlist;
	char *dirname;
	int simple = 0;
	volatile int freeglob = 0;
	glob_t gl;
	size_t prefixlen;

	if (strpbrk(whichf, "~{[*?") != NULL) {
		memset(&gl, 0, sizeof(gl));
		freeglob = 1;
		if (glob(whichf,
		    GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE|GLOB_LIMIT,
		    0, &gl)) {
			reply(550, "not found");
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
		if (stat(dirname, &st) == -1) {
			/*
			 * If user typed "ls -l", etc, and the client
			 * used NLST, do what the user meant.
			 */
			if (dirname[0] == '-' && *dirlist == NULL &&
			    transflag == 0) {
				retrieve(RET_FILE, dirname);
				goto out;
			}
			perror_reply(550, whichf);
			if (dout != NULL) {
				(void) fclose(dout);
				transflag = 0;
				data = -1;
				pdata = -1;
			}
			goto out;
		}

		if (S_ISREG(st.st_mode)) {
			if (dout == NULL) {
				dout = dataconn("file list", -1, "w");
				if (dout == NULL)
					goto out;
				transflag++;
			}
			fprintf(dout, "%s%s\n", dirname,
				type == TYPE_A ? "\r" : "");
			byte_count += strlen(dirname) + 1;
			continue;
		} else if (!S_ISDIR(st.st_mode))
			continue;

		if ((dirp = opendir(dirname)) == NULL)
			continue;

		if (dirname[0] == '.' && dirname[1] == '\0')
			prefixlen = 0;
		else
			prefixlen = strlen(dirname) + 1;
		while ((dir = readdir(dirp)) != NULL) {
			if (recvurg) {
				myoob();
				recvurg = 0;
				transflag = 0;
				goto out;
			}

			if (dir->d_name[0] == '.' && dir->d_namlen == 1)
				continue;
			if (dir->d_name[0] == '.' && dir->d_name[1] == '.' &&
			    dir->d_namlen == 2)
				continue;

			/*
			 * We have to do a stat to insure it's
			 * not a directory or special file.
			 */
			if (simple ||
			    (fstatat(dirfd(dirp), dir->d_name, &st, 0) == 0 &&
			    S_ISREG(st.st_mode))) {
				if (dout == NULL) {
					dout = dataconn("file list", -1, "w");
					if (dout == NULL)
						goto out;
					transflag++;
				}

				if (prefixlen) {
					fprintf(dout, "%s/", dirname);
					byte_count += prefixlen;
				}
				fprintf(dout, "%s%s\n", dir->d_name,
				    type == TYPE_A ? "\r" : "");
				byte_count += dir->d_namlen + 1;
			}
		}
		(void) closedir(dirp);
	}

	if (dout == NULL)
		reply(550, "No files found.");
	else if (ferror(dout) != 0)
		perror_reply(550, "Data connection");
	else
		reply(226, "Transfer complete.");

	transflag = 0;
	if (dout != NULL)
		(void) fclose(dout);
	else {
		if (pdata >= 0)
			close(pdata);
	}
	data = -1;
	pdata = -1;
out:
	if (freeglob) {
		freeglob = 0;
		globfree(&gl);
	}
}

static void
reapchild(int signo)
{
	int save_errno = errno;
	int rval;

	do {
		rval = waitpid(-1, NULL, WNOHANG);
	} while (rval > 0 || (rval == -1 && errno == EINTR));
	errno = save_errno;
}

void
logxfer(const char *name, off_t size, time_t start)
{
	char buf[400 + (HOST_NAME_MAX+1)*4 + PATH_MAX*4];
	char dir[PATH_MAX], path[PATH_MAX], rpath[PATH_MAX];
	char vremotehost[(HOST_NAME_MAX+1)*4], vpath[PATH_MAX*4];
	char *vpw;
	time_t now;
	int len;

	if ((statfd >= 0) && (getcwd(dir, sizeof(dir)) != NULL)) {
		char *cnow;

		time(&now);
		cnow = ctime(&now);

		vpw = malloc(strlen(guest ? guestpw : pw->pw_name) * 4 + 1);
		if (vpw == NULL)
			return;

		snprintf(path, sizeof(path), "%s/%s", dir, name);
		if (realpath(path, rpath) == NULL)
			strlcpy(rpath, path, sizeof(rpath));
		strvis(vpath, rpath, VIS_SAFE|VIS_NOSLASH);

		strvis(vremotehost, remotehost, VIS_SAFE|VIS_NOSLASH);
		strvis(vpw, guest? guestpw : pw->pw_name, VIS_SAFE|VIS_NOSLASH);

		len = snprintf(buf, sizeof(buf),
		    "%.24s %lld %s %lld %s %c %s %c %c %s ftp %d %s %s\n",
		    cnow ? cnow : "?",
		    (long long)(now - start + (now == start)),
		    vremotehost, (long long)size, vpath,
		    ((type == TYPE_A) ? 'a' : 'b'), "*" /* none yet */,
		    'o', ((guest) ? 'a' : 'r'),
		    vpw, 0 /* none yet */,
		    ((guest) ? "*" : pw->pw_name), dhostname);
		free(vpw);

		if (len < 0 || len >= sizeof(buf)) {
			if ((len = strlen(buf)) == 0)
				return;		/* should not happen */
			buf[len - 1] = '\n';
		}
		write(statfd, buf, len);
	}
}

void
set_slave_signals(void)
{
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sa.sa_handler = SIG_DFL;
	(void) sigaction(SIGCHLD, &sa, NULL);

	sa.sa_handler = sigurg;
	sa.sa_flags = 0;		/* don't restart syscalls for SIGURG */
	(void) sigaction(SIGURG, &sa, NULL);

	sigfillset(&sa.sa_mask);	/* block all signals in handler */
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = sigquit;
	(void) sigaction(SIGHUP, &sa, NULL);
	(void) sigaction(SIGINT, &sa, NULL);
	(void) sigaction(SIGQUIT, &sa, NULL);
	(void) sigaction(SIGTERM, &sa, NULL);

	sa.sa_handler = lostconn;
	(void) sigaction(SIGPIPE, &sa, NULL);

	sa.sa_handler = toolong;
	(void) sigaction(SIGALRM, &sa, NULL);

	if (fcntl(fileno(stdin), F_SETOWN, getpid()) == -1)
		syslog(LOG_ERR, "fcntl F_SETOWN: %m");
}

/*
 * Allocate space and return a copy of the specified dir.
 * If 'dir' begins with a tilde (~), expand it.
 */
char *
copy_dir(char *dir, struct passwd *pw)
{
	char *cp;
	char *newdir;
	char *user = NULL;

	/* Nothing to expand */
	if (dir[0] != '~')
		return (strdup(dir));

	/* "dir" is of form ~user/some/dir, lookup user. */
	if (dir[1] != '/' && dir[1] != '\0') {
		if ((cp = strchr(dir + 1, '/')) == NULL)
			cp = dir + strlen(dir);
		if ((user = malloc((size_t)(cp - dir))) == NULL)
			return (NULL);
		strlcpy(user, dir + 1, (size_t)(cp - dir));

		/* Only do lookup if it is a different user. */
		if (strcmp(user, pw->pw_name) != 0) {
			if ((pw = getpwnam(user)) == NULL) {
				/* No such user, interpret literally */
				free(user);
				return(strdup(dir));
			}
		}
		free(user);
	}

	/*
	 * If there is no directory separator (/) then it is just pw_dir.
	 * Otherwise, replace ~foo with pw_dir.
	 */
	if ((cp = strchr(dir + 1, '/')) == NULL) {
		newdir = strdup(pw->pw_dir);
	} else {
		if (asprintf(&newdir, "%s%s", pw->pw_dir, cp) == -1)
			return (NULL);
	}

	return(newdir);
}
