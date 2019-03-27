/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1983, 1991, 1993, 1994
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/filio.h>
#include <sys/ioccom.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/utsname.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <syslog.h>
#include <unistd.h>

#include "inetd.h"

static void	chargen_dg(int, struct servtab *);
static void	chargen_stream(int, struct servtab *);
static void	daytime_dg(int, struct servtab *);
static void	daytime_stream(int, struct servtab *);
static void	discard_dg(int, struct servtab *);
static void	discard_stream(int, struct servtab *);
static void	echo_dg(int, struct servtab *);
static void	echo_stream(int, struct servtab *);
static int	get_line(int, char *, int);
static void	iderror(int, int, int, const char *);
static void	ident_stream(int, struct servtab *);
static void	initring(void);
static uint32_t	machtime(void);
static void	machtime_dg(int, struct servtab *);
static void	machtime_stream(int, struct servtab *);

static char ring[128];
static char *endring;

struct biltin biltins[] = {
	/* Echo received data */
	{ "echo",	SOCK_STREAM,	1, -1,	echo_stream },
	{ "echo",	SOCK_DGRAM,	0, 1,	echo_dg },

	/* Internet /dev/null */
	{ "discard",	SOCK_STREAM,	1, -1,	discard_stream },
	{ "discard",	SOCK_DGRAM,	0, 1,	discard_dg },

	/* Return 32 bit time since 1900 */
	{ "time",	SOCK_STREAM,	0, -1,	machtime_stream },
	{ "time",	SOCK_DGRAM,	0, 1,	machtime_dg },

	/* Return human-readable time */
	{ "daytime",	SOCK_STREAM,	0, -1,	daytime_stream },
	{ "daytime",	SOCK_DGRAM,	0, 1,	daytime_dg },

	/* Familiar character generator */
	{ "chargen",	SOCK_STREAM,	1, -1,	chargen_stream },
	{ "chargen",	SOCK_DGRAM,	0, 1,	chargen_dg },

	{ "tcpmux",	SOCK_STREAM,	1, -1,	(bi_fn_t *)tcpmux },

	{ "auth",	SOCK_STREAM,	1, -1,	ident_stream },

	{ NULL,		0,		0, 0,	NULL }
};

/*
 * RFC864 Character Generator Protocol. Generates character data without
 * any regard for input.
 */

static void
initring(void)
{
	int i;

	endring = ring;

	for (i = 0; i <= 128; ++i)
		if (isprint(i))
			*endring++ = i;
}

/* Character generator
 * The RFC says that we should send back a random number of
 * characters chosen from the range 0 to 512. We send LINESIZ+2.
 */
/* ARGSUSED */
static void
chargen_dg(int s, struct servtab *sep)
{
	struct sockaddr_storage ss;
	static char *rs;
	int len;
	socklen_t size;
	char text[LINESIZ+2];

	if (endring == 0) {
		initring();
		rs = ring;
	}

	size = sizeof(ss);
	if (recvfrom(s, text, sizeof(text), 0,
		     (struct sockaddr *)&ss, &size) < 0)
		return;

	if (check_loop((struct sockaddr *)&ss, sep))
		return;

	if ((len = endring - rs) >= LINESIZ)
		memmove(text, rs, LINESIZ);
	else {
		memmove(text, rs, len);
		memmove(text + len, ring, LINESIZ - len);
	}
	if (++rs == endring)
		rs = ring;
	text[LINESIZ] = '\r';
	text[LINESIZ + 1] = '\n';
	(void) sendto(s, text, sizeof(text), 0, (struct sockaddr *)&ss, size);
}

/* Character generator */
/* ARGSUSED */
static void
chargen_stream(int s, struct servtab *sep)
{
	int len;
	char *rs, text[LINESIZ+2];

	inetd_setproctitle(sep->se_service, s);

	if (!endring)
		initring();

	text[LINESIZ] = '\r';
	text[LINESIZ + 1] = '\n';
	for (rs = ring;;) {
		if ((len = endring - rs) >= LINESIZ)
			memmove(text, rs, LINESIZ);
		else {
			memmove(text, rs, len);
			memmove(text + len, ring, LINESIZ - len);
		}
		if (++rs == endring)
			rs = ring;
		if (write(s, text, sizeof(text)) != sizeof(text))
			break;
	}
	exit(0);
}

/*
 * RFC867 Daytime Protocol. Sends the current date and time as an ascii
 * character string without any regard for input.
 */

/* Return human-readable time of day */
/* ARGSUSED */
static void
daytime_dg(int s, struct servtab *sep)
{
	char buffer[256];
	time_t now;
	struct sockaddr_storage ss;
	socklen_t size;

	now = time((time_t *) 0);

	size = sizeof(ss);
	if (recvfrom(s, buffer, sizeof(buffer), 0,
		     (struct sockaddr *)&ss, &size) < 0)
		return;

	if (check_loop((struct sockaddr *)&ss, sep))
		return;

	(void) sprintf(buffer, "%.24s\r\n", ctime(&now));
	(void) sendto(s, buffer, strlen(buffer), 0,
		      (struct sockaddr *)&ss, size);
}

/* Return human-readable time of day */
/* ARGSUSED */
static void
daytime_stream(int s, struct servtab *sep __unused)
{
	char buffer[256];
	time_t now;

	now = time((time_t *) 0);

	(void) sprintf(buffer, "%.24s\r\n", ctime(&now));
	(void) send(s, buffer, strlen(buffer), MSG_EOF);
}

/*
 * RFC863 Discard Protocol. Any data received is thrown away and no response
 * is sent.
 */

/* Discard service -- ignore data */
/* ARGSUSED */
static void
discard_dg(int s, struct servtab *sep __unused)
{
	char buffer[BUFSIZE];

	(void) read(s, buffer, sizeof(buffer));
}

/* Discard service -- ignore data */
/* ARGSUSED */
static void
discard_stream(int s, struct servtab *sep)
{
	int ret;
	char buffer[BUFSIZE];

	inetd_setproctitle(sep->se_service, s);
	while (1) {
		while ((ret = read(s, buffer, sizeof(buffer))) > 0)
			;
		if (ret == 0 || errno != EINTR)
			break;
	}
	exit(0);
}

/*
 * RFC862 Echo Protocol. Any data received is sent back to the sender as
 * received.
 */

/* Echo service -- echo data back */
/* ARGSUSED */
static void
echo_dg(int s, struct servtab *sep)
{
	char buffer[65536]; /* Should be sizeof(max datagram). */
	int i;
	socklen_t size;
	struct sockaddr_storage ss;

	size = sizeof(ss);
	if ((i = recvfrom(s, buffer, sizeof(buffer), 0,
			  (struct sockaddr *)&ss, &size)) < 0)
		return;

	if (check_loop((struct sockaddr *)&ss, sep))
		return;

	(void) sendto(s, buffer, i, 0, (struct sockaddr *)&ss, size);
}

/* Echo service -- echo data back */
/* ARGSUSED */
static void
echo_stream(int s, struct servtab *sep)
{
	char buffer[BUFSIZE];
	int i;

	inetd_setproctitle(sep->se_service, s);
	while ((i = read(s, buffer, sizeof(buffer))) > 0 &&
	    write(s, buffer, i) > 0)
		;
	exit(0);
}

/*
 * RFC1413 Identification Protocol. Given a TCP port number pair, return a
 * character string which identifies the owner of that connection on the
 * server's system. Extended to allow for ~/.fakeid support and ~/.noident
 * support.
 */

/* RFC 1413 says the following are the only errors you can return. */
#define ID_INVALID	"INVALID-PORT"	/* Port number improperly specified. */
#define ID_NOUSER	"NO-USER"	/* Port not in use/not identifable. */
#define ID_HIDDEN	"HIDDEN-USER"	/* Hiden at user's request. */
#define ID_UNKNOWN	"UNKNOWN-ERROR"	/* Everything else. */

/* Generic ident_stream error-sending func */
/* ARGSUSED */
static void
iderror(int lport, int fport, int s, const char *er)
{
	char *p;

	asprintf(&p, "%d , %d : ERROR : %s\r\n", lport, fport, er);
	if (p == NULL) {
		syslog(LOG_ERR, "asprintf: %m");
		exit(EX_OSERR);
	}
	send(s, p, strlen(p), MSG_EOF);
	free(p);

	exit(0);
}

/* Ident service (AKA "auth") */
/* ARGSUSED */
static void
ident_stream(int s, struct servtab *sep)
{
	struct utsname un;
	struct stat sb;
	struct sockaddr_in sin4[2];
#ifdef INET6
	struct sockaddr_in6 sin6[2];
#endif
	struct sockaddr_storage ss[2];
	struct xucred uc;
	struct timeval tv = {
		10,
		0
	}, to;
	struct passwd *pw = NULL;
	fd_set fdset;
	char buf[BUFSIZE], *p, **av, *osname = NULL, e;
	char idbuf[MAXLOGNAME] = ""; /* Big enough to hold uid in decimal. */
	socklen_t socklen;
	ssize_t ssize;
	size_t size, bufsiz;
	int c, fflag = 0, nflag = 0, rflag = 0, argc = 0;
	int gflag = 0, iflag = 0, Fflag = 0, getcredfail = 0, onreadlen;
	u_short lport, fport;

	inetd_setproctitle(sep->se_service, s);
	/*
	 * Reset getopt() since we are a fork() but not an exec() from
	 * a parent which used getopt() already.
	 */
	optind = 1;
	optreset = 1;
	/*
	 * Take the internal argument vector and count it out to make an
	 * argument count for getopt. This can be used for any internal
	 * service to read arguments and use getopt() easily.
	 */
	for (av = sep->se_argv; *av; av++)
		argc++;
	if (argc) {
		int sec, usec;
		size_t i;
		u_int32_t rnd32;

		while ((c = getopt(argc, sep->se_argv, "d:fFgino:rt:")) != -1)
			switch (c) {
			case 'd':
				if (!gflag)
					strlcpy(idbuf, optarg, sizeof(idbuf));
				break;
			case 'f':
				fflag = 1;
				break;
			case 'F':
				fflag = 1;
				Fflag=1;
				break;
			case 'g':
				gflag = 1;
				rnd32 = 0;	/* Shush, compiler. */
				/*
				 * The number of bits in "rnd32" divided
				 * by the number of bits needed per iteration
				 * gives a more optimal way to reload the
				 * random number only when necessary.
				 *
				 * 32 bits from arc4random corresponds to
				 * about 6 base-36 digits, so we reseed evey 6.
				 */
				for (i = 0; i < sizeof(idbuf) - 1; i++) {
					static const char *const base36 =
					    "0123456789"
					    "abcdefghijklmnopqrstuvwxyz";
					if (i % 6 == 0)
						rnd32 = arc4random();
					idbuf[i] = base36[rnd32 % 36];
					rnd32 /= 36;
				}
				idbuf[i] = '\0';
				break;
			case 'i':
				iflag = 1;
				break;
			case 'n':
				nflag = 1;
				break;
			case 'o':
				osname = optarg;
				break;
			case 'r':
				rflag = 1;
				break;
			case 't':
				switch (sscanf(optarg, "%d.%d", &sec, &usec)) {
				case 2:
					tv.tv_usec = usec;
					/* FALLTHROUGH */
				case 1:
					tv.tv_sec = sec;
					break;
				default:
					if (debug)
						warnx("bad -t argument");
					break;
				}
				break;
			default:
				break;
			}
	}
	if (osname == NULL) {
		if (uname(&un) == -1)
			iderror(0, 0, s, ID_UNKNOWN);
		osname = un.sysname;
	}

	/*
	 * We're going to prepare for and execute reception of a
	 * packet of data from the user. The data is in the format
	 * "local_port , foreign_port\r\n" (with local being the
	 * server's port and foreign being the client's.)
	 */
	gettimeofday(&to, NULL);
	to.tv_sec += tv.tv_sec;
	to.tv_usec += tv.tv_usec;
	if (to.tv_usec >= 1000000) {
		to.tv_usec -= 1000000;
		to.tv_sec++;
	}

	size = 0;
	bufsiz = sizeof(buf) - 1;
	FD_ZERO(&fdset);
 	while (bufsiz > 0) {
		gettimeofday(&tv, NULL);
		tv.tv_sec = to.tv_sec - tv.tv_sec;
		tv.tv_usec = to.tv_usec - tv.tv_usec;
		if (tv.tv_usec < 0) {
			tv.tv_usec += 1000000;
			tv.tv_sec--;
		}
		if (tv.tv_sec < 0)
			break;
		FD_SET(s, &fdset);
		if (select(s + 1, &fdset, NULL, NULL, &tv) == -1)
			iderror(0, 0, s, ID_UNKNOWN);
		if (ioctl(s, FIONREAD, &onreadlen) == -1)
			iderror(0, 0, s, ID_UNKNOWN);
		if ((size_t)onreadlen > bufsiz)
			onreadlen = bufsiz;
		ssize = read(s, &buf[size], (size_t)onreadlen);
		if (ssize == -1)
			iderror(0, 0, s, ID_UNKNOWN);
		else if (ssize == 0)
			break;
		bufsiz -= ssize;
		size += ssize;
		if (memchr(&buf[size - ssize], '\n', ssize) != NULL)
			break;
 	}
	buf[size] = '\0';
	/* Read two characters, and check for a delimiting character */
	if (sscanf(buf, "%hu , %hu%c", &lport, &fport, &e) != 3 || isdigit(e))
		iderror(0, 0, s, ID_INVALID);

	/* Send garbage? */
	if (gflag)
		goto printit;

	/*
	 * If not "real" (-r), send a HIDDEN-USER error for everything.
	 * If -d is used to set a fallback username, this is used to
	 * override it, and the fallback is returned instead.
	 */
	if (!rflag) {
		if (*idbuf == '\0')
			iderror(lport, fport, s, ID_HIDDEN);
		goto printit;
	}

	/*
	 * We take the input and construct an array of two sockaddr_ins
	 * which contain the local address information and foreign
	 * address information, respectively, used to look up the
	 * credentials for the socket (which are returned by the
	 * sysctl "net.inet.tcp.getcred" when we call it.)
	 */
	socklen = sizeof(ss[0]);
	if (getsockname(s, (struct sockaddr *)&ss[0], &socklen) == -1)
		iderror(lport, fport, s, ID_UNKNOWN);
	socklen = sizeof(ss[1]);
	if (getpeername(s, (struct sockaddr *)&ss[1], &socklen) == -1)
		iderror(lport, fport, s, ID_UNKNOWN);
	if (ss[0].ss_family != ss[1].ss_family)
		iderror(lport, fport, s, ID_UNKNOWN);
	size = sizeof(uc);
	switch (ss[0].ss_family) {
	case AF_INET:
		sin4[0] = *(struct sockaddr_in *)&ss[0];
		sin4[0].sin_port = htons(lport);
		sin4[1] = *(struct sockaddr_in *)&ss[1];
		sin4[1].sin_port = htons(fport);
		if (sysctlbyname("net.inet.tcp.getcred", &uc, &size, sin4,
				 sizeof(sin4)) == -1)
			getcredfail = errno;
		break;
#ifdef INET6
	case AF_INET6:
		sin6[0] = *(struct sockaddr_in6 *)&ss[0];
		sin6[0].sin6_port = htons(lport);
		sin6[1] = *(struct sockaddr_in6 *)&ss[1];
		sin6[1].sin6_port = htons(fport);
		if (sysctlbyname("net.inet6.tcp6.getcred", &uc, &size, sin6,
				 sizeof(sin6)) == -1)
			getcredfail = errno;
		break;
#endif
	default: /* should not reach here */
		getcredfail = EAFNOSUPPORT;
		break;
	}
	if (getcredfail != 0 || uc.cr_version != XUCRED_VERSION) {
		if (*idbuf == '\0')
			iderror(lport, fport, s,
			    getcredfail == ENOENT ? ID_NOUSER : ID_UNKNOWN);
		goto printit;
	}

	/* Look up the pw to get the username and home directory*/
	errno = 0;
	pw = getpwuid(uc.cr_uid);
	if (pw == NULL)
		iderror(lport, fport, s, errno == 0 ? ID_NOUSER : ID_UNKNOWN);

	if (iflag)
		snprintf(idbuf, sizeof(idbuf), "%u", (unsigned)pw->pw_uid);
	else
		strlcpy(idbuf, pw->pw_name, sizeof(idbuf));

	/*
	 * If enabled, we check for a file named ".noident" in the user's
	 * home directory. If found, we return HIDDEN-USER.
	 */
	if (nflag) {
		if (asprintf(&p, "%s/.noident", pw->pw_dir) == -1)
			iderror(lport, fport, s, ID_UNKNOWN);
		if (lstat(p, &sb) == 0) {
			free(p);
			iderror(lport, fport, s, ID_HIDDEN);
		}
		free(p);
	}

	/*
	 * Here, if enabled, we read a user's ".fakeid" file in their
	 * home directory. It consists of a line containing the name
	 * they want.
	 */
	if (fflag) {
		int fakeid_fd;

		/*
		 * Here we set ourself to effectively be the user, so we don't
		 * open any files we have no permission to open, especially
		 * symbolic links to sensitive root-owned files or devices.
		 */
		if (initgroups(pw->pw_name, pw->pw_gid) == -1)
			iderror(lport, fport, s, ID_UNKNOWN);
		if (seteuid(pw->pw_uid) == -1)
			iderror(lport, fport, s, ID_UNKNOWN);
		/*
		 * We can't stat() here since that would be a race
		 * condition.
		 * Therefore, we open the file we have permissions to open
		 * and if it's not a regular file, we close it and end up
		 * returning the user's real username.
		 */
		if (asprintf(&p, "%s/.fakeid", pw->pw_dir) == -1)
			iderror(lport, fport, s, ID_UNKNOWN);
		fakeid_fd = open(p, O_RDONLY | O_NONBLOCK);
		free(p);
		if (fakeid_fd == -1 || fstat(fakeid_fd, &sb) == -1 ||
		    !S_ISREG(sb.st_mode))
			goto fakeid_fail;

		if ((ssize = read(fakeid_fd, buf, sizeof(buf) - 1)) < 0)
			goto fakeid_fail;
		buf[ssize] = '\0';

		/*
		 * Usually, the file will have the desired identity
		 * in the form "identity\n". Allow for leading white
		 * space and trailing white space/end of line.
		 */
		p = buf;
		p += strspn(p, " \t");
		p[strcspn(p, " \t\r\n")] = '\0';
		if (strlen(p) > MAXLOGNAME - 1) /* Too long (including nul)? */
			p[MAXLOGNAME - 1] = '\0';

		/*
		 * If the name is a zero-length string or matches it
		 * the id or name of another user (unless permitted by -F)
		 * then it is invalid.
		 */
		if (*p == '\0')
			goto fakeid_fail;
		if (!Fflag) {
			if (iflag) {
				if (p[strspn(p, "0123456789")] == '\0' &&
				    getpwuid(atoi(p)) != NULL)
					goto fakeid_fail;
			} else {
				if (getpwnam(p) != NULL)
					goto fakeid_fail;
			}
		}

		strlcpy(idbuf, p, sizeof(idbuf));

fakeid_fail:
		if (fakeid_fd != -1)
			close(fakeid_fd);
	}

printit:
	/* Finally, we make and send the reply. */
	if (asprintf(&p, "%d , %d : USERID : %s : %s\r\n", lport, fport, osname,
	    idbuf) == -1) {
		syslog(LOG_ERR, "asprintf: %m");
		exit(EX_OSERR);
	}
	send(s, p, strlen(p), MSG_EOF);
	free(p);
	
	exit(0);
}

/*
 * RFC738/868 Time Server.
 * Return a machine readable date and time, in the form of the
 * number of seconds since midnight, Jan 1, 1900.  Since gettimeofday
 * returns the number of seconds since midnight, Jan 1, 1970,
 * we must add 2208988800 seconds to this figure to make up for
 * some seventy years Bell Labs was asleep.
 */

static uint32_t
machtime(void)
{

#define	OFFSET ((uint32_t)25567 * 24*60*60)
	return (htonl((uint32_t)(time(NULL) + OFFSET)));
#undef OFFSET
}

/* ARGSUSED */
static void
machtime_dg(int s, struct servtab *sep)
{
	uint32_t result;
	struct sockaddr_storage ss;
	socklen_t size;

	size = sizeof(ss);
	if (recvfrom(s, (char *)&result, sizeof(result), 0,
		     (struct sockaddr *)&ss, &size) < 0)
		return;

	if (check_loop((struct sockaddr *)&ss, sep))
		return;

	result = machtime();
	(void) sendto(s, (char *) &result, sizeof(result), 0,
		      (struct sockaddr *)&ss, size);
}

/* ARGSUSED */
static void
machtime_stream(int s, struct servtab *sep __unused)
{
	uint32_t result;

	result = machtime();
	(void) send(s, (char *) &result, sizeof(result), MSG_EOF);
}

/*
 * RFC1078 TCP Port Service Multiplexer (TCPMUX). Service connections to
 * services based on the service name sent.
 *
 *  Based on TCPMUX.C by Mark K. Lottor November 1988
 *  sri-nic::ps:<mkl>tcpmux.c
 */

#define MAX_SERV_LEN	(256+2)		/* 2 bytes for \r\n */
#define strwrite(fd, buf)	(void) write(fd, buf, sizeof(buf)-1)

static int		/* # of characters up to \r,\n or \0 */
get_line(int fd, char *buf, int len)
{
	int count = 0, n;
	struct sigaction sa;

	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_DFL;
	sigaction(SIGALRM, &sa, (struct sigaction *)0);
	do {
		alarm(10);
		n = read(fd, buf, len-count);
		alarm(0);
		if (n == 0)
			return (count);
		if (n < 0)
			return (-1);
		while (--n >= 0) {
			if (*buf == '\r' || *buf == '\n' || *buf == '\0')
				return (count);
			count++;
			buf++;
		}
	} while (count < len);
	return (count);
}

struct servtab *
tcpmux(int s)
{
	struct servtab *sep;
	char service[MAX_SERV_LEN+1];
	int len;

	/* Get requested service name */
	if ((len = get_line(s, service, MAX_SERV_LEN)) < 0) {
		strwrite(s, "-Error reading service name\r\n");
		return (NULL);
	}
	service[len] = '\0';

	if (debug)
		warnx("tcpmux: someone wants %s", service);

	/*
	 * Help is a required command, and lists available services,
	 * one per line.
	 */
	if (!strcasecmp(service, "help")) {
		for (sep = servtab; sep; sep = sep->se_next) {
			if (!ISMUX(sep))
				continue;
			(void)write(s,sep->se_service,strlen(sep->se_service));
			strwrite(s, "\r\n");
		}
		return (NULL);
	}

	/* Try matching a service in inetd.conf with the request */
	for (sep = servtab; sep; sep = sep->se_next) {
		if (!ISMUX(sep))
			continue;
		if (!strcasecmp(service, sep->se_service)) {
			if (ISMUXPLUS(sep)) {
				strwrite(s, "+Go\r\n");
			}
			return (sep);
		}
	}
	strwrite(s, "-Service not available\r\n");
	return (NULL);
}
