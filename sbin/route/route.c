/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1989, 1991, 1993
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1983, 1989, 1991, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
#if 0
static char sccsid[] = "@(#)route.c	8.6 (Berkeley) 4/28/95";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>
#include <ifaddrs.h>

struct fibl {
	TAILQ_ENTRY(fibl)	fl_next;

	int	fl_num;
	int	fl_error;
	int	fl_errno;
};

static struct keytab {
	const char	*kt_cp;
	int	kt_i;
} const keywords[] = {
#include "keywords.h"
	{0, 0}
};

static struct sockaddr_storage so[RTAX_MAX];
static int	pid, rtm_addrs;
static int	s;
static int	nflag, af, qflag, tflag;
static int	verbose, aflen;
static int	locking, lockrest, debugonly;
static struct rt_metrics rt_metrics;
static u_long  rtm_inits;
static uid_t	uid;
static int	defaultfib;
static int	numfibs;
static char	domain[MAXHOSTNAMELEN + 1];
static bool	domain_initialized;
static int	rtm_seq;
static char	rt_line[NI_MAXHOST];
static char	net_line[MAXHOSTNAMELEN + 1];

static struct {
	struct	rt_msghdr m_rtm;
	char	m_space[512];
} m_rtmsg;

static TAILQ_HEAD(fibl_head_t, fibl) fibl_head;

static void	printb(int, const char *);
static void	flushroutes(int argc, char *argv[]);
static int	flushroutes_fib(int);
static int	getaddr(int, char *, struct hostent **, int);
static int	keyword(const char *);
#ifdef INET
static void	inet_makenetandmask(u_long, struct sockaddr_in *,
		    struct sockaddr_in *, u_long);
#endif
#ifdef INET6
static int	inet6_makenetandmask(struct sockaddr_in6 *, const char *);
#endif
static void	interfaces(void);
static void	monitor(int, char*[]);
static const char	*netname(struct sockaddr *);
static void	newroute(int, char **);
static int	newroute_fib(int, char *, int);
static void	pmsg_addrs(char *, int, size_t);
static void	pmsg_common(struct rt_msghdr *, size_t);
static int	prefixlen(const char *);
static void	print_getmsg(struct rt_msghdr *, int, int);
static void	print_rtmsg(struct rt_msghdr *, size_t);
static const char	*routename(struct sockaddr *);
static int	rtmsg(int, int, int);
static void	set_metric(char *, int);
static int	set_sofib(int);
static void	sockaddr(char *, struct sockaddr *, size_t);
static void	sodump(struct sockaddr *, const char *);
static int	fiboptlist_csv(const char *, struct fibl_head_t *);
static int	fiboptlist_range(const char *, struct fibl_head_t *);

static void usage(const char *) __dead2;

#define	READ_TIMEOUT	10
static volatile sig_atomic_t stop_read;

static void
stopit(int sig __unused)
{

	stop_read = 1;
}

static void
usage(const char *cp)
{
	if (cp != NULL)
		warnx("bad keyword: %s", cp);
	errx(EX_USAGE, "usage: route [-46dnqtv] command [[modifiers] args]");
	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	int ch;
	size_t len;

	if (argc < 2)
		usage(NULL);

	while ((ch = getopt(argc, argv, "46nqdtv")) != -1)
		switch(ch) {
		case '4':
#ifdef INET
			af = AF_INET;
			aflen = sizeof(struct sockaddr_in);
#else
			errx(1, "IPv4 support is not compiled in");
#endif
			break;
		case '6':
#ifdef INET6
			af = AF_INET6;
			aflen = sizeof(struct sockaddr_in6);
#else
			errx(1, "IPv6 support is not compiled in");
#endif
			break;
		case 'n':
			nflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'd':
			debugonly = 1;
			break;
		case '?':
		default:
			usage(NULL);
		}
	argc -= optind;
	argv += optind;

	pid = getpid();
	uid = geteuid();
	if (tflag)
		s = open(_PATH_DEVNULL, O_WRONLY, 0);
	else
		s = socket(PF_ROUTE, SOCK_RAW, 0);
	if (s < 0)
		err(EX_OSERR, "socket");

	len = sizeof(numfibs);
	if (sysctlbyname("net.fibs", (void *)&numfibs, &len, NULL, 0) == -1)
		numfibs = -1;

	len = sizeof(defaultfib);
	if (numfibs != -1 &&
	    sysctlbyname("net.my_fibnum", (void *)&defaultfib, &len, NULL,
		0) == -1)
		defaultfib = -1;

	if (*argv != NULL)
		switch (keyword(*argv)) {
		case K_GET:
		case K_SHOW:
			uid = 0;
			/* FALLTHROUGH */

		case K_CHANGE:
		case K_ADD:
		case K_DEL:
		case K_DELETE:
			newroute(argc, argv);
			/* NOTREACHED */

		case K_MONITOR:
			monitor(argc, argv);
			/* NOTREACHED */

		case K_FLUSH:
			flushroutes(argc, argv);
			exit(0);
			/* NOTREACHED */
		}
	usage(*argv);
	/* NOTREACHED */
}

static int
set_sofib(int fib)
{

	if (fib < 0)
		return (0);
	return (setsockopt(s, SOL_SOCKET, SO_SETFIB, (void *)&fib,
	    sizeof(fib)));
}

static int
fiboptlist_range(const char *arg, struct fibl_head_t *flh)
{
	struct fibl *fl;
	char *str0, *str, *token, *endptr;
	int fib[2], i, error;

	str0 = str = strdup(arg);
	error = 0;
	i = 0;
	while ((token = strsep(&str, "-")) != NULL) {
		switch (i) {
		case 0:
		case 1:
			errno = 0;
			fib[i] = strtol(token, &endptr, 0);
			if (errno == 0) {
				if (*endptr != '\0' ||
				    fib[i] < 0 ||
				    (numfibs != -1 && fib[i] > numfibs - 1))
					errno = EINVAL;
			}
			if (errno)
				error = 1;
			break;
		default:
			error = 1;
		}
		if (error)
			goto fiboptlist_range_ret;
		i++;
	}
	if (fib[0] >= fib[1]) {
		error = 1;
		goto fiboptlist_range_ret;
	}
	for (i = fib[0]; i <= fib[1]; i++) {
		fl = calloc(1, sizeof(*fl));
		if (fl == NULL) {
			error = 1;
			goto fiboptlist_range_ret;
		}
		fl->fl_num = i;
		TAILQ_INSERT_TAIL(flh, fl, fl_next);
	}
fiboptlist_range_ret:
	free(str0);
	return (error);
}

#define	ALLSTRLEN	64
static int
fiboptlist_csv(const char *arg, struct fibl_head_t *flh)
{
	struct fibl *fl;
	char *str0, *str, *token, *endptr;
	int fib, error;

	str0 = str = NULL;
	if (strcmp("all", arg) == 0) {
		str = calloc(1, ALLSTRLEN);
		if (str == NULL) {
			error = 1;
			goto fiboptlist_csv_ret;
		}
		if (numfibs > 1)
			snprintf(str, ALLSTRLEN - 1, "%d-%d", 0, numfibs - 1);
		else
			snprintf(str, ALLSTRLEN - 1, "%d", 0);
	} else if (strcmp("default", arg) == 0) {
		str0 = str = calloc(1, ALLSTRLEN);
		if (str == NULL) {
			error = 1;
			goto fiboptlist_csv_ret;
		}
		snprintf(str, ALLSTRLEN - 1, "%d", defaultfib);
	} else
		str0 = str = strdup(arg);

	error = 0;
	while ((token = strsep(&str, ",")) != NULL) {
		if (*token != '-' && strchr(token, '-') != NULL) {
			error = fiboptlist_range(token, flh);
			if (error)
				goto fiboptlist_csv_ret;
		} else {
			errno = 0;
			fib = strtol(token, &endptr, 0);
			if (errno == 0) {
				if (*endptr != '\0' ||
				    fib < 0 ||
				    (numfibs != -1 && fib > numfibs - 1))
					errno = EINVAL;
			}
			if (errno) {
				error = 1;
				goto fiboptlist_csv_ret;
			}
			fl = calloc(1, sizeof(*fl));
			if (fl == NULL) {
				error = 1;
				goto fiboptlist_csv_ret;
			}
			fl->fl_num = fib;
			TAILQ_INSERT_TAIL(flh, fl, fl_next);
		}
	}
fiboptlist_csv_ret:
	if (str0 != NULL)
		free(str0);
	return (error);
}

/*
 * Purge all entries in the routing tables not
 * associated with network interfaces.
 */
static void
flushroutes(int argc, char *argv[])
{
	struct fibl *fl;
	int error;

	if (uid != 0 && !debugonly && !tflag)
		errx(EX_NOPERM, "must be root to alter routing table");
	shutdown(s, SHUT_RD); /* Don't want to read back our messages */

	TAILQ_INIT(&fibl_head);
	while (argc > 1) {
		argc--;
		argv++;
		if (**argv != '-')
			usage(*argv);
		switch (keyword(*argv + 1)) {
#ifdef INET
		case K_4:
		case K_INET:
			af = AF_INET;
			break;
#endif
#ifdef INET6
		case K_6:
		case K_INET6:
			af = AF_INET6;
			break;
#endif
		case K_LINK:
			af = AF_LINK;
			break;
		case K_FIB:
			if (!--argc)
				usage(*argv);
			error = fiboptlist_csv(*++argv, &fibl_head);
			if (error)
				errx(EX_USAGE, "invalid fib number: %s", *argv);
			break;
		default:
			usage(*argv);
		}
	}
	if (TAILQ_EMPTY(&fibl_head)) {
		error = fiboptlist_csv("default", &fibl_head);
		if (error)
			errx(EX_OSERR, "fiboptlist_csv failed.");
	}
	TAILQ_FOREACH(fl, &fibl_head, fl_next)
		flushroutes_fib(fl->fl_num);
}

static int
flushroutes_fib(int fib)
{
	struct rt_msghdr *rtm;
	size_t needed;
	char *buf, *next, *lim;
	int mib[7], rlen, seqno, count = 0;
	int error;

	error = set_sofib(fib);
	if (error) {
		warn("fib number %d is ignored", fib);
		return (error);
	}

retry:
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = AF_UNSPEC;
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;		/* no flags */
	mib[6] = fib;
	if (sysctl(mib, nitems(mib), NULL, &needed, NULL, 0) < 0)
		err(EX_OSERR, "route-sysctl-estimate");
	if ((buf = malloc(needed)) == NULL)
		errx(EX_OSERR, "malloc failed");
	if (sysctl(mib, nitems(mib), buf, &needed, NULL, 0) < 0) {
		if (errno == ENOMEM && count++ < 10) {
			warnx("Routing table grew, retrying");
			sleep(1);
			free(buf);
			goto retry;
		}
		err(EX_OSERR, "route-sysctl-get");
	}
	lim = buf + needed;
	if (verbose)
		(void)printf("Examining routing table from sysctl\n");
	seqno = 0;		/* ??? */
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)(void *)next;
		if (verbose)
			print_rtmsg(rtm, rtm->rtm_msglen);
		if ((rtm->rtm_flags & RTF_GATEWAY) == 0)
			continue;
		if (af != 0) {
			struct sockaddr *sa = (struct sockaddr *)(rtm + 1);

			if (sa->sa_family != af)
				continue;
		}
		if (debugonly)
			continue;
		rtm->rtm_type = RTM_DELETE;
		rtm->rtm_seq = seqno;
		rlen = write(s, next, rtm->rtm_msglen);
		if (rlen < 0 && errno == EPERM)
			err(1, "write to routing socket");
		if (rlen < (int)rtm->rtm_msglen) {
			warn("write to routing socket");
			(void)printf("got only %d for rlen\n", rlen);
			free(buf);
			goto retry;
			break;
		}
		seqno++;
		if (qflag)
			continue;
		if (verbose)
			print_rtmsg(rtm, rlen);
		else {
			struct sockaddr *sa = (struct sockaddr *)(rtm + 1);

			printf("%-20.20s ", rtm->rtm_flags & RTF_HOST ?
			    routename(sa) : netname(sa));
			sa = (struct sockaddr *)(SA_SIZE(sa) + (char *)sa);
			printf("%-20.20s ", routename(sa));
			if (fib >= 0)
				printf("-fib %-3d ", fib);
			printf("done\n");
		}
	}
	free(buf);
	return (error);
}

static const char *
routename(struct sockaddr *sa)
{
	struct sockaddr_dl *sdl;
	const char *cp;
	int n;

	if (!domain_initialized) {
		domain_initialized = true;
		if (gethostname(domain, MAXHOSTNAMELEN) == 0 &&
		    (cp = strchr(domain, '.'))) {
			domain[MAXHOSTNAMELEN] = '\0';
			(void)strcpy(domain, cp + 1);
		} else
			domain[0] = '\0';
	}

	/* If the address is zero-filled, use "default". */
	if (sa->sa_len == 0 && nflag == 0)
		return ("default");
#if defined(INET) || defined(INET6)
	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		/* If the address is zero-filled, use "default". */
		if (nflag == 0 &&
		    ((struct sockaddr_in *)(void *)sa)->sin_addr.s_addr ==
		    INADDR_ANY)
			return("default");
		break;
#endif
#ifdef INET6
	case AF_INET6:
		/* If the address is zero-filled, use "default". */
		if (nflag == 0 &&
		    IN6_IS_ADDR_UNSPECIFIED(&((struct sockaddr_in6 *)(void *)sa)->sin6_addr))
			return("default");
		break;
#endif
	}
#endif

	switch (sa->sa_family) {
#if defined(INET) || defined(INET6)
#ifdef INET
	case AF_INET:
#endif
#ifdef INET6
	case AF_INET6:
#endif
	{
		struct sockaddr_storage ss;
		int error;
		char *p;

		memset(&ss, 0, sizeof(ss));
		if (sa->sa_len == 0)
			ss.ss_family = sa->sa_family;
		else
			memcpy(&ss, sa, sa->sa_len);
		/* Expand sa->sa_len because it could be shortened. */
		if (sa->sa_family == AF_INET)
			ss.ss_len = sizeof(struct sockaddr_in);
		else if (sa->sa_family == AF_INET6)
			ss.ss_len = sizeof(struct sockaddr_in6);
		error = getnameinfo((struct sockaddr *)&ss, ss.ss_len,
		    rt_line, sizeof(rt_line), NULL, 0,
		    (nflag == 0) ? 0 : NI_NUMERICHOST);
		if (error) {
			warnx("getnameinfo(): %s", gai_strerror(error));
			strncpy(rt_line, "invalid", sizeof(rt_line));
		}

		/* Remove the domain part if any. */
		p = strchr(rt_line, '.');
		if (p != NULL && strcmp(p + 1, domain) == 0)
			*p = '\0';

		return (rt_line);
		break;
	}
#endif
	case AF_LINK:
		sdl = (struct sockaddr_dl *)(void *)sa;

		if (sdl->sdl_nlen == 0 &&
		    sdl->sdl_alen == 0 &&
		    sdl->sdl_slen == 0) {
			n = snprintf(rt_line, sizeof(rt_line), "link#%d",
			    sdl->sdl_index);
			if (n > (int)sizeof(rt_line))
			    rt_line[0] = '\0';
			return (rt_line);
		} else
			return (link_ntoa(sdl));
		break;

	default:
	    {
		u_short *sp = (u_short *)(void *)sa;
		u_short *splim = sp + ((sa->sa_len + 1) >> 1);
		char *cps = rt_line + sprintf(rt_line, "(%d)", sa->sa_family);
		char *cpe = rt_line + sizeof(rt_line);

		while (++sp < splim && cps < cpe) /* start with sa->sa_data */
			if ((n = snprintf(cps, cpe - cps, " %x", *sp)) > 0)
				cps += n;
			else
				*cps = '\0';
		break;
	    }
	}
	return (rt_line);
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be that of a net, not a host.
 */
static const char *
netname(struct sockaddr *sa)
{
	struct sockaddr_dl *sdl;
	int n;
#ifdef INET
	struct netent *np = NULL;
	const char *cp = NULL;
	u_long i;
#endif

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
	{
		struct in_addr in;

		in = ((struct sockaddr_in *)(void *)sa)->sin_addr;
		i = in.s_addr = ntohl(in.s_addr);
		if (in.s_addr == 0)
			cp = "default";
		else if (!nflag) {
			np = getnetbyaddr(i, AF_INET);
			if (np != NULL)
				cp = np->n_name;
		}
#define C(x)	(unsigned)((x) & 0xff)
		if (cp != NULL)
			strncpy(net_line, cp, sizeof(net_line));
		else if ((in.s_addr & 0xffffff) == 0)
			(void)sprintf(net_line, "%u", C(in.s_addr >> 24));
		else if ((in.s_addr & 0xffff) == 0)
			(void)sprintf(net_line, "%u.%u", C(in.s_addr >> 24),
			    C(in.s_addr >> 16));
		else if ((in.s_addr & 0xff) == 0)
			(void)sprintf(net_line, "%u.%u.%u", C(in.s_addr >> 24),
			    C(in.s_addr >> 16), C(in.s_addr >> 8));
		else
			(void)sprintf(net_line, "%u.%u.%u.%u", C(in.s_addr >> 24),
			    C(in.s_addr >> 16), C(in.s_addr >> 8),
			    C(in.s_addr));
#undef C
		break;
	}
#endif
#ifdef INET6
	case AF_INET6:
	{
		struct sockaddr_in6 sin6;
		int niflags = 0;

		memset(&sin6, 0, sizeof(sin6));
		memcpy(&sin6, sa, sa->sa_len);
		sin6.sin6_len = sizeof(sin6);
		sin6.sin6_family = AF_INET6;
		if (nflag)
			niflags |= NI_NUMERICHOST;
		if (getnameinfo((struct sockaddr *)&sin6, sin6.sin6_len,
		    net_line, sizeof(net_line), NULL, 0, niflags) != 0)
			strncpy(net_line, "invalid", sizeof(net_line));

		return(net_line);
	}
#endif
	case AF_LINK:
		sdl = (struct sockaddr_dl *)(void *)sa;

		if (sdl->sdl_nlen == 0 &&
		    sdl->sdl_alen == 0 &&
		    sdl->sdl_slen == 0) {
			n = snprintf(net_line, sizeof(net_line), "link#%d",
			    sdl->sdl_index);
			if (n > (int)sizeof(net_line))
			    net_line[0] = '\0';
			return (net_line);
		} else
			return (link_ntoa(sdl));
		break;

	default:
	    {
		u_short *sp = (u_short *)(void *)sa->sa_data;
		u_short *splim = sp + ((sa->sa_len + 1)>>1);
		char *cps = net_line + sprintf(net_line, "af %d:", sa->sa_family);
		char *cpe = net_line + sizeof(net_line);

		while (sp < splim && cps < cpe)
			if ((n = snprintf(cps, cpe - cps, " %x", *sp++)) > 0)
				cps += n;
			else
				*cps = '\0';
		break;
	    }
	}
	return (net_line);
}

static void
set_metric(char *value, int key)
{
	int flag = 0;
	char *endptr;
	u_long noval, *valp = &noval;

	switch (key) {
#define caseof(x, y, z)	case x: valp = &rt_metrics.z; flag = y; break
	caseof(K_MTU, RTV_MTU, rmx_mtu);
	caseof(K_HOPCOUNT, RTV_HOPCOUNT, rmx_hopcount);
	caseof(K_EXPIRE, RTV_EXPIRE, rmx_expire);
	caseof(K_RECVPIPE, RTV_RPIPE, rmx_recvpipe);
	caseof(K_SENDPIPE, RTV_SPIPE, rmx_sendpipe);
	caseof(K_SSTHRESH, RTV_SSTHRESH, rmx_ssthresh);
	caseof(K_RTT, RTV_RTT, rmx_rtt);
	caseof(K_RTTVAR, RTV_RTTVAR, rmx_rttvar);
	caseof(K_WEIGHT, RTV_WEIGHT, rmx_weight);
	}
	rtm_inits |= flag;
	if (lockrest || locking)
		rt_metrics.rmx_locks |= flag;
	if (locking)
		locking = 0;
	errno = 0;
	*valp = strtol(value, &endptr, 0);
	if (errno == 0 && *endptr != '\0')
		errno = EINVAL;
	if (errno)
		err(EX_USAGE, "%s", value);
	if (flag & RTV_EXPIRE && (value[0] == '+' || value[0] == '-')) {
		struct timespec ts;

		clock_gettime(CLOCK_REALTIME_FAST, &ts);
		*valp += ts.tv_sec;
	}
}

#define	F_ISHOST	0x01
#define	F_FORCENET	0x02
#define	F_FORCEHOST	0x04
#define	F_PROXY		0x08
#define	F_INTERFACE	0x10

static void
newroute(int argc, char **argv)
{
	struct sigaction sa;
	struct hostent *hp;
	struct fibl *fl;
	char *cmd;
	const char *dest, *gateway, *errmsg;
	int key, error, flags, nrflags, fibnum;

	if (uid != 0 && !debugonly && !tflag)
		errx(EX_NOPERM, "must be root to alter routing table");
	dest = NULL;
	gateway = NULL;
	flags = RTF_STATIC;
	nrflags = 0;
	hp = NULL;
	TAILQ_INIT(&fibl_head);

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = stopit;
	if (sigaction(SIGALRM, &sa, 0) == -1)
		warn("sigaction SIGALRM");

	cmd = argv[0];
	if (*cmd != 'g' && *cmd != 's')
		shutdown(s, SHUT_RD); /* Don't want to read back our messages */
	while (--argc > 0) {
		if (**(++argv)== '-') {
			switch (key = keyword(1 + *argv)) {
			case K_LINK:
				af = AF_LINK;
				aflen = sizeof(struct sockaddr_dl);
				break;
#ifdef INET
			case K_4:
			case K_INET:
				af = AF_INET;
				aflen = sizeof(struct sockaddr_in);
				break;
#endif
#ifdef INET6
			case K_6:
			case K_INET6:
				af = AF_INET6;
				aflen = sizeof(struct sockaddr_in6);
				break;
#endif
			case K_SA:
				af = PF_ROUTE;
				aflen = sizeof(struct sockaddr_storage);
				break;
			case K_IFACE:
			case K_INTERFACE:
				nrflags |= F_INTERFACE;
				break;
			case K_NOSTATIC:
				flags &= ~RTF_STATIC;
				break;
			case K_LOCK:
				locking = 1;
				break;
			case K_LOCKREST:
				lockrest = 1;
				break;
			case K_HOST:
				nrflags |= F_FORCEHOST;
				break;
			case K_REJECT:
				flags |= RTF_REJECT;
				break;
			case K_BLACKHOLE:
				flags |= RTF_BLACKHOLE;
				break;
			case K_PROTO1:
				flags |= RTF_PROTO1;
				break;
			case K_PROTO2:
				flags |= RTF_PROTO2;
				break;
			case K_PROXY:
				nrflags |= F_PROXY;
				break;
			case K_XRESOLVE:
				flags |= RTF_XRESOLVE;
				break;
			case K_STATIC:
				flags |= RTF_STATIC;
				break;
			case K_STICKY:
				flags |= RTF_STICKY;
				break;
			case K_NOSTICK:
				flags &= ~RTF_STICKY;
				break;
			case K_FIB:
				if (!--argc)
					usage(NULL);
				error = fiboptlist_csv(*++argv, &fibl_head);
				if (error)
					errx(EX_USAGE,
					    "invalid fib number: %s", *argv);
				break;
			case K_IFA:
				if (!--argc)
					usage(NULL);
				getaddr(RTAX_IFA, *++argv, 0, nrflags);
				break;
			case K_IFP:
				if (!--argc)
					usage(NULL);
				getaddr(RTAX_IFP, *++argv, 0, nrflags);
				break;
			case K_GENMASK:
				if (!--argc)
					usage(NULL);
				getaddr(RTAX_GENMASK, *++argv, 0, nrflags);
				break;
			case K_GATEWAY:
				if (!--argc)
					usage(NULL);
				getaddr(RTAX_GATEWAY, *++argv, 0, nrflags);
				gateway = *argv;
				break;
			case K_DST:
				if (!--argc)
					usage(NULL);
				if (getaddr(RTAX_DST, *++argv, &hp, nrflags))
					nrflags |= F_ISHOST;
				dest = *argv;
				break;
			case K_NETMASK:
				if (!--argc)
					usage(NULL);
				getaddr(RTAX_NETMASK, *++argv, 0, nrflags);
				/* FALLTHROUGH */
			case K_NET:
				nrflags |= F_FORCENET;
				break;
			case K_PREFIXLEN:
				if (!--argc)
					usage(NULL);
				if (prefixlen(*++argv) == -1) {
					nrflags &= ~F_FORCENET;
					nrflags |= F_ISHOST;
				} else {
					nrflags |= F_FORCENET;
					nrflags &= ~F_ISHOST;
				}
				break;
			case K_MTU:
			case K_HOPCOUNT:
			case K_EXPIRE:
			case K_RECVPIPE:
			case K_SENDPIPE:
			case K_SSTHRESH:
			case K_RTT:
			case K_RTTVAR:
			case K_WEIGHT:
				if (!--argc)
					usage(NULL);
				set_metric(*++argv, key);
				break;
			default:
				usage(1+*argv);
			}
		} else {
			if ((rtm_addrs & RTA_DST) == 0) {
				dest = *argv;
				if (getaddr(RTAX_DST, *argv, &hp, nrflags))
					nrflags |= F_ISHOST;
			} else if ((rtm_addrs & RTA_GATEWAY) == 0) {
				gateway = *argv;
				getaddr(RTAX_GATEWAY, *argv, &hp, nrflags);
			} else {
				getaddr(RTAX_NETMASK, *argv, 0, nrflags);
				nrflags |= F_FORCENET;
			}
		}
	}

	/* Do some sanity checks on resulting request */
	if (so[RTAX_DST].ss_len == 0) {
		warnx("destination parameter required");
		usage(NULL);
	}

	if (so[RTAX_NETMASK].ss_len != 0 &&
	    so[RTAX_DST].ss_family != so[RTAX_NETMASK].ss_family) {
		warnx("destination and netmask family need to be the same");
		usage(NULL);
	}

	if (nrflags & F_FORCEHOST) {
		nrflags |= F_ISHOST;
#ifdef INET6
		if (af == AF_INET6) {
			rtm_addrs &= ~RTA_NETMASK;
			memset(&so[RTAX_NETMASK], 0, sizeof(so[RTAX_NETMASK]));
		}
#endif
	}
	if (nrflags & F_FORCENET)
		nrflags &= ~F_ISHOST;
	flags |= RTF_UP;
	if (nrflags & F_ISHOST)
		flags |= RTF_HOST;
	if ((nrflags & F_INTERFACE) == 0)
		flags |= RTF_GATEWAY;
	if (nrflags & F_PROXY)
		flags |= RTF_ANNOUNCE;
	if (dest == NULL)
		dest = "";
	if (gateway == NULL)
		gateway = "";

	if (TAILQ_EMPTY(&fibl_head)) {
		error = fiboptlist_csv("default", &fibl_head);
		if (error)
			errx(EX_OSERR, "fiboptlist_csv failed.");
	}
	error = 0;
	TAILQ_FOREACH(fl, &fibl_head, fl_next) {
		fl->fl_error = newroute_fib(fl->fl_num, cmd, flags);
		if (fl->fl_error)
			fl->fl_errno = errno;
		error += fl->fl_error;
	}
	if (*cmd == 'g' || *cmd == 's')
		exit(error);

	error = 0;
	if (!qflag) {
		fibnum = 0;
		TAILQ_FOREACH(fl, &fibl_head, fl_next) {
			if (fl->fl_error == 0)
				fibnum++;
		}
		if (fibnum > 0) {
			int firstfib = 1;

			printf("%s %s %s", cmd,
			    (nrflags & F_ISHOST) ? "host" : "net", dest);
			if (*gateway)
				printf(": gateway %s", gateway);

			if (numfibs > 1) {
				TAILQ_FOREACH(fl, &fibl_head, fl_next) {
					if (fl->fl_error == 0
					    && fl->fl_num >= 0) {
						if (firstfib) {
							printf(" fib ");
							firstfib = 0;
						}
						printf("%d", fl->fl_num);
						if (fibnum-- > 1)
							printf(",");
					}
				}
			}
			printf("\n");
		}
	}

	fibnum = 0;
	TAILQ_FOREACH(fl, &fibl_head, fl_next) {
		if (fl->fl_error != 0) {
			error = 1;
			if (!qflag) {
				printf("%s %s %s", cmd, (nrflags & F_ISHOST)
				    ? "host" : "net", dest);
				if (*gateway)
					printf(": gateway %s", gateway);

				if (fl->fl_num >= 0)
					printf(" fib %d", fl->fl_num);

				switch (fl->fl_errno) {
				case ESRCH:
					errmsg = "not in table";
					break;
				case EBUSY:
					errmsg = "entry in use";
					break;
				case ENOBUFS:
					errmsg = "not enough memory";
					break;
				case EADDRINUSE:
					/*
					 * handle recursion avoidance
					 * in rt_setgate()
					 */
					errmsg = "gateway uses the same route";
					break;
				case EEXIST:
					errmsg = "route already in table";
					break;
				default:
					errmsg = strerror(fl->fl_errno);
					break;
				}
				printf(": %s\n", errmsg);
			}
		}
	}
	exit(error);
}

static int
newroute_fib(int fib, char *cmd, int flags)
{
	int error;

	error = set_sofib(fib);
	if (error) {
		warn("fib number %d is ignored", fib);
		return (error);
	}

	error = rtmsg(*cmd, flags, fib);
	return (error);
}

#ifdef INET
static void
inet_makenetandmask(u_long net, struct sockaddr_in *sin,
    struct sockaddr_in *sin_mask, u_long bits)
{
	u_long mask = 0;

	rtm_addrs |= RTA_NETMASK;

	/*
	 * MSB of net should be meaningful. 0/0 is exception.
	 */
	if (net > 0)
		while ((net & 0xff000000) == 0)
			net <<= 8;

	/*
	 * If no /xx was specified we must calculate the
	 * CIDR address.
	 */
	if ((bits == 0) && (net != 0)) {
		u_long i, j;

		for(i = 0, j = 0xff; i < 4; i++)  {
			if (net & j) {
				break;
			}
			j <<= 8;
		}
		/* i holds the first non zero bit */
		bits = 32 - (i*8);
	}
	if (bits != 0)
		mask = 0xffffffff << (32 - bits);

	sin->sin_addr.s_addr = htonl(net);
	sin_mask->sin_addr.s_addr = htonl(mask);
	sin_mask->sin_len = sizeof(struct sockaddr_in);
	sin_mask->sin_family = AF_INET;
}
#endif

#ifdef INET6
/*
 * XXX the function may need more improvement...
 */
static int
inet6_makenetandmask(struct sockaddr_in6 *sin6, const char *plen)
{

	if (plen == NULL) {
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr) &&
		    sin6->sin6_scope_id == 0)
			plen = "0";
	}

	if (plen == NULL || strcmp(plen, "128") == 0)
		return (1);
	rtm_addrs |= RTA_NETMASK;
	prefixlen(plen);
	return (0);
}
#endif

/*
 * Interpret an argument as a network address of some kind,
 * returning 1 if a host address, 0 if a network address.
 */
static int
getaddr(int idx, char *str, struct hostent **hpp, int nrflags)
{
	struct sockaddr *sa;
#if defined(INET)
	struct sockaddr_in *sin;
	struct hostent *hp;
	struct netent *np;
	u_long val;
	char *q;
#elif defined(INET6)
	char *q;
#endif

	if (idx < 0 || idx >= RTAX_MAX)
		usage("internal error");
	if (af == 0) {
#if defined(INET)
		af = AF_INET;
		aflen = sizeof(struct sockaddr_in);
#elif defined(INET6)
		af = AF_INET6;
		aflen = sizeof(struct sockaddr_in6);
#else
		af = AF_LINK;
		aflen = sizeof(struct sockaddr_dl);
#endif
	}
#ifndef INET
	hpp = NULL;
#endif
	rtm_addrs |= (1 << idx);
	sa = (struct sockaddr *)&so[idx];
	sa->sa_family = af;
	sa->sa_len = aflen;

	switch (idx) {
	case RTAX_GATEWAY:
		if (nrflags & F_INTERFACE) {
			struct ifaddrs *ifap, *ifa;
			struct sockaddr_dl *sdl0 = (struct sockaddr_dl *)(void *)sa;
			struct sockaddr_dl *sdl = NULL;

			if (getifaddrs(&ifap))
				err(EX_OSERR, "getifaddrs");

			for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
				if (ifa->ifa_addr->sa_family != AF_LINK)
					continue;

				if (strcmp(str, ifa->ifa_name) != 0)
					continue;

				sdl = (struct sockaddr_dl *)(void *)ifa->ifa_addr;
			}
			/* If we found it, then use it */
			if (sdl != NULL) {
				/*
				 * Note that we need to copy before calling
				 * freeifaddrs().
				 */
				memcpy(sdl0, sdl, sdl->sdl_len);
			}
			freeifaddrs(ifap);
			if (sdl != NULL)
				return(1);
			else
				errx(EX_DATAERR,
				    "interface '%s' does not exist", str);
		}
		break;
	case RTAX_IFP:
		sa->sa_family = AF_LINK;
		break;
	}
	if (strcmp(str, "default") == 0) {
		/*
		 * Default is net 0.0.0.0/0
		 */
		switch (idx) {
		case RTAX_DST:
			nrflags |= F_FORCENET;
			getaddr(RTAX_NETMASK, str, 0, nrflags);
			break;
		}
		return (0);
	}
	switch (sa->sa_family) {
#ifdef INET6
	case AF_INET6:
	{
		struct addrinfo hints, *res;
		int ecode;

		q = NULL;
		if (idx == RTAX_DST && (q = strchr(str, '/')) != NULL)
			*q = '\0';
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = sa->sa_family;
		hints.ai_socktype = SOCK_DGRAM;
		ecode = getaddrinfo(str, NULL, &hints, &res);
		if (ecode != 0 || res->ai_family != AF_INET6 ||
		    res->ai_addrlen != sizeof(struct sockaddr_in6))
			errx(EX_OSERR, "%s: %s", str, gai_strerror(ecode));
		memcpy(sa, res->ai_addr, res->ai_addrlen);
		freeaddrinfo(res);
		if (q != NULL)
			*q++ = '/';
		if (idx == RTAX_DST)
			return (inet6_makenetandmask((struct sockaddr_in6 *)(void *)sa, q));
		return (0);
	}
#endif /* INET6 */
	case AF_LINK:
		link_addr(str, (struct sockaddr_dl *)(void *)sa);
		return (1);

	case PF_ROUTE:
		sockaddr(str, sa, sizeof(struct sockaddr_storage));
		return (1);
#ifdef INET
	case AF_INET:
#endif
	default:
		break;
	}

#ifdef INET
	sin = (struct sockaddr_in *)(void *)sa;
	if (hpp == NULL)
		hpp = &hp;
	*hpp = NULL;

	q = strchr(str,'/');
	if (q != NULL && idx == RTAX_DST) {
		*q = '\0';
		if ((val = inet_network(str)) != INADDR_NONE) {
			inet_makenetandmask(val, sin,
			    (struct sockaddr_in *)&so[RTAX_NETMASK],
			    strtoul(q+1, 0, 0));
			return (0);
		}
		*q = '/';
	}
	if ((idx != RTAX_DST || (nrflags & F_FORCENET) == 0) &&
	    inet_aton(str, &sin->sin_addr)) {
		val = sin->sin_addr.s_addr;
		if (idx != RTAX_DST || nrflags & F_FORCEHOST ||
		    inet_lnaof(sin->sin_addr) != INADDR_ANY)
			return (1);
		else {
			val = ntohl(val);
			goto netdone;
		}
	}
	if (idx == RTAX_DST && (nrflags & F_FORCEHOST) == 0 &&
	    ((val = inet_network(str)) != INADDR_NONE ||
	    ((np = getnetbyname(str)) != NULL && (val = np->n_net) != 0))) {
netdone:
		inet_makenetandmask(val, sin,
		    (struct sockaddr_in *)&so[RTAX_NETMASK], 0);
		return (0);
	}
	hp = gethostbyname(str);
	if (hp != NULL) {
		*hpp = hp;
		sin->sin_family = hp->h_addrtype;
		memmove((char *)&sin->sin_addr, hp->h_addr,
		    MIN((size_t)hp->h_length, sizeof(sin->sin_addr)));
		return (1);
	}
#endif
	errx(EX_NOHOST, "bad address: %s", str);
}

static int
prefixlen(const char *str)
{
	int len = atoi(str), q, r;
	int max;
	char *p;

	rtm_addrs |= RTA_NETMASK;
	switch (af) {
#ifdef INET6
	case AF_INET6:
	{
		struct sockaddr_in6 *sin6 =
		    (struct sockaddr_in6 *)&so[RTAX_NETMASK];

		max = 128;
		p = (char *)&sin6->sin6_addr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(*sin6);
		break;
	}
#endif
#ifdef INET
	case AF_INET:
	{
		struct sockaddr_in *sin =
		    (struct sockaddr_in *)&so[RTAX_NETMASK];

		max = 32;
		p = (char *)&sin->sin_addr;
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(*sin);
		break;
	}
#endif
	default:
		errx(EX_OSERR, "prefixlen not supported in this af");
	}

	if (len < 0 || max < len)
		errx(EX_USAGE, "%s: invalid prefixlen", str);

	q = len >> 3;
	r = len & 7;
	memset((void *)p, 0, max / 8);
	if (q > 0)
		memset((void *)p, 0xff, q);
	if (r > 0)
		*((u_char *)p + q) = (0xff00 >> r) & 0xff;
	if (len == max)
		return (-1);
	else
		return (len);
}

static void
interfaces(void)
{
	size_t needed;
	int mib[6];
	char *buf, *lim, *next, count = 0;
	struct rt_msghdr *rtm;

retry2:
	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;		/* protocol */
	mib[3] = AF_UNSPEC;
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;		/* no flags */
	if (sysctl(mib, nitems(mib), NULL, &needed, NULL, 0) < 0)
		err(EX_OSERR, "route-sysctl-estimate");
	if ((buf = malloc(needed)) == NULL)
		errx(EX_OSERR, "malloc failed");
	if (sysctl(mib, nitems(mib), buf, &needed, NULL, 0) < 0) {
		if (errno == ENOMEM && count++ < 10) {
			warnx("Routing table grew, retrying");
			sleep(1);
			free(buf);
			goto retry2;
		}
		err(EX_OSERR, "actual retrieval of interface table");
	}
	lim = buf + needed;
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)(void *)next;
		print_rtmsg(rtm, rtm->rtm_msglen);
	}
	free(buf);
}

static void
monitor(int argc, char *argv[])
{
	int n, fib, error;
	char msg[2048], *endptr;

	fib = defaultfib;
	while (argc > 1) {
		argc--;
		argv++;
		if (**argv != '-')
			usage(*argv);
		switch (keyword(*argv + 1)) {
		case K_FIB:
			if (!--argc)
				usage(*argv);
			errno = 0;
			fib = strtol(*++argv, &endptr, 0);
			if (errno == 0) {
				if (*endptr != '\0' ||
				    fib < 0 ||
				    (numfibs != -1 && fib > numfibs - 1))
					errno = EINVAL;
			}
			if (errno)
				errx(EX_USAGE, "invalid fib number: %s", *argv);
			break;
		default:
			usage(*argv);
		}
	}
	error = set_sofib(fib);
	if (error)
		errx(EX_USAGE, "invalid fib number: %d", fib);

	verbose = 1;
	if (debugonly) {
		interfaces();
		exit(0);
	}
	for (;;) {
		time_t now;
		n = read(s, msg, 2048);
		now = time(NULL);
		(void)printf("\ngot message of size %d on %s", n, ctime(&now));
		print_rtmsg((struct rt_msghdr *)(void *)msg, n);
	}
}

static int
rtmsg(int cmd, int flags, int fib)
{
	int rlen;
	char *cp = m_rtmsg.m_space;
	int l;

#define NEXTADDR(w, u)							\
	if (rtm_addrs & (w)) {						\
		l = SA_SIZE(&(u));					\
		memmove(cp, (char *)&(u), l);				\
		cp += l;						\
		if (verbose)						\
			sodump((struct sockaddr *)&(u), #w);		\
	}

	errno = 0;
	memset(&m_rtmsg, 0, sizeof(m_rtmsg));
	if (cmd == 'a')
		cmd = RTM_ADD;
	else if (cmd == 'c')
		cmd = RTM_CHANGE;
	else if (cmd == 'g' || cmd == 's') {
		cmd = RTM_GET;
		if (so[RTAX_IFP].ss_family == 0) {
			so[RTAX_IFP].ss_family = AF_LINK;
			so[RTAX_IFP].ss_len = sizeof(struct sockaddr_dl);
			rtm_addrs |= RTA_IFP;
		}
	} else {
		cmd = RTM_DELETE;
		flags |= RTF_PINNED;
	}
#define rtm m_rtmsg.m_rtm
	rtm.rtm_type = cmd;
	rtm.rtm_flags = flags;
	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_seq = ++rtm_seq;
	rtm.rtm_addrs = rtm_addrs;
	rtm.rtm_rmx = rt_metrics;
	rtm.rtm_inits = rtm_inits;

	NEXTADDR(RTA_DST, so[RTAX_DST]);
	NEXTADDR(RTA_GATEWAY, so[RTAX_GATEWAY]);
	NEXTADDR(RTA_NETMASK, so[RTAX_NETMASK]);
	NEXTADDR(RTA_GENMASK, so[RTAX_GENMASK]);
	NEXTADDR(RTA_IFP, so[RTAX_IFP]);
	NEXTADDR(RTA_IFA, so[RTAX_IFA]);
	rtm.rtm_msglen = l = cp - (char *)&m_rtmsg;
	if (verbose)
		print_rtmsg(&rtm, l);
	if (debugonly)
		return (0);
	if ((rlen = write(s, (char *)&m_rtmsg, l)) < 0) {
		switch (errno) {
		case EPERM:
			err(1, "writing to routing socket");
			break;
		case ESRCH:
			warnx("route has not been found");
			break;
		case EEXIST:
			/* Handled by newroute() */
			break;
		default:
			warn("writing to routing socket");
		}
		return (-1);
	}
	if (cmd == RTM_GET) {
		stop_read = 0;
		alarm(READ_TIMEOUT);
		do {
			l = read(s, (char *)&m_rtmsg, sizeof(m_rtmsg));
		} while (l > 0 && stop_read == 0 &&
		    (rtm.rtm_type != RTM_GET || rtm.rtm_seq != rtm_seq ||
			rtm.rtm_pid != pid));
		if (stop_read != 0) {
			warnx("read from routing socket timed out");
			return (-1);
		} else
			alarm(0);
		if (l < 0)
			warn("read from routing socket");
		else
			print_getmsg(&rtm, l, fib);
	}
#undef rtm
	return (0);
}

static const char *const msgtypes[] = {
	"",
	"RTM_ADD: Add Route",
	"RTM_DELETE: Delete Route",
	"RTM_CHANGE: Change Metrics or flags",
	"RTM_GET: Report Metrics",
	"RTM_LOSING: Kernel Suspects Partitioning",
	"RTM_REDIRECT: Told to use different route",
	"RTM_MISS: Lookup failed on this address",
	"RTM_LOCK: fix specified metrics",
	"RTM_OLDADD: caused by SIOCADDRT",
	"RTM_OLDDEL: caused by SIOCDELRT",
	"RTM_RESOLVE: Route created by cloning",
	"RTM_NEWADDR: address being added to iface",
	"RTM_DELADDR: address being removed from iface",
	"RTM_IFINFO: iface status change",
	"RTM_NEWMADDR: new multicast group membership on iface",
	"RTM_DELMADDR: multicast group membership removed from iface",
	"RTM_IFANNOUNCE: interface arrival/departure",
	"RTM_IEEE80211: IEEE 802.11 wireless event",
};

static const char metricnames[] =
    "\011weight\010rttvar\7rtt\6ssthresh\5sendpipe\4recvpipe\3expire"
    "\1mtu";
static const char routeflags[] =
    "\1UP\2GATEWAY\3HOST\4REJECT\5DYNAMIC\6MODIFIED\7DONE"
    "\012XRESOLVE\013LLINFO\014STATIC\015BLACKHOLE"
    "\017PROTO2\020PROTO1\021PRCLONING\022WASCLONED\023PROTO3"
    "\024FIXEDMTU\025PINNED\026LOCAL\027BROADCAST\030MULTICAST\035STICKY";
static const char ifnetflags[] =
    "\1UP\2BROADCAST\3DEBUG\4LOOPBACK\5PTP\6b6\7RUNNING\010NOARP"
    "\011PPROMISC\012ALLMULTI\013OACTIVE\014SIMPLEX\015LINK0\016LINK1"
    "\017LINK2\020MULTICAST";
static const char addrnames[] =
    "\1DST\2GATEWAY\3NETMASK\4GENMASK\5IFP\6IFA\7AUTHOR\010BRD";

static const char errfmt[] =
    "\n%s: truncated route message, only %zu bytes left\n";

static void
print_rtmsg(struct rt_msghdr *rtm, size_t msglen)
{
	struct if_msghdr *ifm;
	struct ifa_msghdr *ifam;
#ifdef RTM_NEWMADDR
	struct ifma_msghdr *ifmam;
#endif
	struct if_announcemsghdr *ifan;
	const char *state;

	if (verbose == 0)
		return;
	if (rtm->rtm_version != RTM_VERSION) {
		(void)printf("routing message version %d not understood\n",
		    rtm->rtm_version);
		return;
	}
	if (rtm->rtm_type < nitems(msgtypes))
		(void)printf("%s: ", msgtypes[rtm->rtm_type]);
	else
		(void)printf("unknown type %d: ", rtm->rtm_type);
	(void)printf("len %d, ", rtm->rtm_msglen);

#define	REQUIRE(x)	do {		\
	if (msglen < sizeof(x))		\
		goto badlen;		\
	else				\
		msglen -= sizeof(x);	\
	} while (0)

	switch (rtm->rtm_type) {
	case RTM_IFINFO:
		REQUIRE(struct if_msghdr);
		ifm = (struct if_msghdr *)rtm;
		(void)printf("if# %d, ", ifm->ifm_index);
		switch (ifm->ifm_data.ifi_link_state) {
		case LINK_STATE_DOWN:
			state = "down";
			break;
		case LINK_STATE_UP:
			state = "up";
			break;
		default:
			state = "unknown";
			break;
		}
		(void)printf("link: %s, flags:", state);
		printb(ifm->ifm_flags, ifnetflags);
		pmsg_addrs((char *)(ifm + 1), ifm->ifm_addrs, msglen);
		break;
	case RTM_NEWADDR:
	case RTM_DELADDR:
		REQUIRE(struct ifa_msghdr);
		ifam = (struct ifa_msghdr *)rtm;
		(void)printf("metric %d, flags:", ifam->ifam_metric);
		printb(ifam->ifam_flags, routeflags);
		pmsg_addrs((char *)(ifam + 1), ifam->ifam_addrs, msglen);
		break;
#ifdef RTM_NEWMADDR
	case RTM_NEWMADDR:
	case RTM_DELMADDR:
		REQUIRE(struct ifma_msghdr);
		ifmam = (struct ifma_msghdr *)rtm;
		pmsg_addrs((char *)(ifmam + 1), ifmam->ifmam_addrs, msglen);
		break;
#endif
	case RTM_IFANNOUNCE:
		REQUIRE(struct if_announcemsghdr);
		ifan = (struct if_announcemsghdr *)rtm;
		(void)printf("if# %d, what: ", ifan->ifan_index);
		switch (ifan->ifan_what) {
		case IFAN_ARRIVAL:
			(void)printf("arrival");
			break;
		case IFAN_DEPARTURE:
			printf("departure");
			break;
		default:
			printf("#%d", ifan->ifan_what);
			break;
		}
		printf("\n");
		fflush(stdout);
		break;

	default:
		if (rtm->rtm_type <= RTM_RESOLVE) {
			printf("pid: %ld, seq %d, errno %d, flags:",
			    (long)rtm->rtm_pid, rtm->rtm_seq, rtm->rtm_errno);
			printb(rtm->rtm_flags, routeflags);
			pmsg_common(rtm, msglen);
		} else
			printf("type: %u, len: %zu\n", rtm->rtm_type, msglen);
	}

	return;

badlen:
	(void)printf(errfmt, __func__, msglen);
#undef	REQUIRE
}

static void
print_getmsg(struct rt_msghdr *rtm, int msglen, int fib)
{
	struct sockaddr *sp[RTAX_MAX];
	struct timespec ts;
	char *cp;
	int i;

	memset(sp, 0, sizeof(sp));
	(void)printf("   route to: %s\n",
	    routename((struct sockaddr *)&so[RTAX_DST]));
	if (rtm->rtm_version != RTM_VERSION) {
		warnx("routing message version %d not understood",
		     rtm->rtm_version);
		return;
	}
	if (rtm->rtm_msglen > msglen) {
		warnx("message length mismatch, in packet %d, returned %d",
		      rtm->rtm_msglen, msglen);
		return;
	}
	if (rtm->rtm_errno)  {
		errno = rtm->rtm_errno;
		warn("message indicates error %d", errno);
		return;
	}
	cp = ((char *)(rtm + 1));
	for (i = 0; i < RTAX_MAX; i++)
		if (rtm->rtm_addrs & (1 << i)) {
			sp[i] = (struct sockaddr *)cp;
			cp += SA_SIZE((struct sockaddr *)cp);
		}
	if ((rtm->rtm_addrs & RTA_IFP) &&
	    (sp[RTAX_IFP]->sa_family != AF_LINK ||
	     ((struct sockaddr_dl *)(void *)sp[RTAX_IFP])->sdl_nlen == 0))
			sp[RTAX_IFP] = NULL;
	if (sp[RTAX_DST])
		(void)printf("destination: %s\n", routename(sp[RTAX_DST]));
	if (sp[RTAX_NETMASK])
		(void)printf("       mask: %s\n", routename(sp[RTAX_NETMASK]));
	if (sp[RTAX_GATEWAY] && (rtm->rtm_flags & RTF_GATEWAY))
		(void)printf("    gateway: %s\n", routename(sp[RTAX_GATEWAY]));
	if (fib >= 0)
		(void)printf("        fib: %u\n", (unsigned int)fib);
	if (sp[RTAX_IFP])
		(void)printf("  interface: %.*s\n",
		    ((struct sockaddr_dl *)(void *)sp[RTAX_IFP])->sdl_nlen,
		    ((struct sockaddr_dl *)(void *)sp[RTAX_IFP])->sdl_data);
	(void)printf("      flags: ");
	printb(rtm->rtm_flags, routeflags);

#define lock(f)	((rtm->rtm_rmx.rmx_locks & __CONCAT(RTV_,f)) ? 'L' : ' ')
#define msec(u)	(((u) + 500) / 1000)		/* usec to msec */
	printf("\n%9s %9s %9s %9s %9s %10s %9s\n", "recvpipe",
	    "sendpipe", "ssthresh", "rtt,msec", "mtu   ", "weight", "expire");
	printf("%8lu%c ", rtm->rtm_rmx.rmx_recvpipe, lock(RPIPE));
	printf("%8lu%c ", rtm->rtm_rmx.rmx_sendpipe, lock(SPIPE));
	printf("%8lu%c ", rtm->rtm_rmx.rmx_ssthresh, lock(SSTHRESH));
	printf("%8lu%c ", msec(rtm->rtm_rmx.rmx_rtt), lock(RTT));
	printf("%8lu%c ", rtm->rtm_rmx.rmx_mtu, lock(MTU));
	printf("%8lu%c ", rtm->rtm_rmx.rmx_weight, lock(WEIGHT));
	if (rtm->rtm_rmx.rmx_expire > 0)
		clock_gettime(CLOCK_REALTIME_FAST, &ts);
	else
		ts.tv_sec = 0;
	printf("%8ld%c\n", (long)(rtm->rtm_rmx.rmx_expire - ts.tv_sec),
	    lock(EXPIRE));
#undef lock
#undef msec
#define	RTA_IGN	(RTA_DST|RTA_GATEWAY|RTA_NETMASK|RTA_IFP|RTA_IFA|RTA_BRD)
	if (verbose)
		pmsg_common(rtm, msglen);
	else if (rtm->rtm_addrs &~ RTA_IGN) {
		(void)printf("sockaddrs: ");
		printb(rtm->rtm_addrs, addrnames);
		putchar('\n');
	}
#undef	RTA_IGN
}

static void
pmsg_common(struct rt_msghdr *rtm, size_t msglen)
{

	(void)printf("\nlocks: ");
	printb(rtm->rtm_rmx.rmx_locks, metricnames);
	(void)printf(" inits: ");
	printb(rtm->rtm_inits, metricnames);
	if (msglen > sizeof(struct rt_msghdr))
		pmsg_addrs(((char *)(rtm + 1)), rtm->rtm_addrs,
		    msglen - sizeof(struct rt_msghdr));
	else
		(void)fflush(stdout);
}

static void
pmsg_addrs(char *cp, int addrs, size_t len)
{
	struct sockaddr *sa;
	int i;

	if (addrs == 0) {
		(void)putchar('\n');
		return;
	}
	(void)printf("\nsockaddrs: ");
	printb(addrs, addrnames);
	putchar('\n');
	for (i = 0; i < RTAX_MAX; i++)
		if (addrs & (1 << i)) {
			sa = (struct sockaddr *)cp;
			if (len == 0 || len < SA_SIZE(sa)) {
				(void)printf(errfmt, __func__, len);
				break;
			}
			(void)printf(" %s", routename(sa));
			len -= SA_SIZE(sa);
			cp += SA_SIZE(sa);
		}
	(void)putchar('\n');
	(void)fflush(stdout);
}

static void
printb(int b, const char *str)
{
	int i;
	int gotsome = 0;

	if (b == 0)
		return;
	while ((i = *str++) != 0) {
		if (b & (1 << (i-1))) {
			if (gotsome == 0)
				i = '<';
			else
				i = ',';
			putchar(i);
			gotsome = 1;
			for (; (i = *str) > 32; str++)
				putchar(i);
		} else
			while (*str > 32)
				str++;
	}
	if (gotsome)
		putchar('>');
}

int
keyword(const char *cp)
{
	const struct keytab *kt = keywords;

	while (kt->kt_cp != NULL && strcmp(kt->kt_cp, cp) != 0)
		kt++;
	return (kt->kt_i);
}

static void
sodump(struct sockaddr *sa, const char *which)
{
#ifdef INET6
	char nbuf[INET6_ADDRSTRLEN];
#endif

	switch (sa->sa_family) {
	case AF_LINK:
		(void)printf("%s: link %s; ", which,
		    link_ntoa((struct sockaddr_dl *)(void *)sa));
		break;
#ifdef INET
	case AF_INET:
		(void)printf("%s: inet %s; ", which,
		    inet_ntoa(((struct sockaddr_in *)(void *)sa)->sin_addr));
		break;
#endif
#ifdef INET6
	case AF_INET6:
		(void)printf("%s: inet6 %s; ", which, inet_ntop(sa->sa_family,
		    &((struct sockaddr_in6 *)(void *)sa)->sin6_addr, nbuf,
		    sizeof(nbuf)));
		break;
#endif
	}
	(void)fflush(stdout);
}

/* States*/
#define VIRGIN	0
#define GOTONE	1
#define GOTTWO	2
/* Inputs */
#define	DIGIT	(4*0)
#define	END	(4*1)
#define DELIM	(4*2)

static void
sockaddr(char *addr, struct sockaddr *sa, size_t size)
{
	char *cp = (char *)sa;
	char *cplim = cp + size;
	int byte = 0, state = VIRGIN, new = 0 /* foil gcc */;

	memset(cp, 0, size);
	cp++;
	do {
		if ((*addr >= '0') && (*addr <= '9')) {
			new = *addr - '0';
		} else if ((*addr >= 'a') && (*addr <= 'f')) {
			new = *addr - 'a' + 10;
		} else if ((*addr >= 'A') && (*addr <= 'F')) {
			new = *addr - 'A' + 10;
		} else if (*addr == '\0')
			state |= END;
		else
			state |= DELIM;
		addr++;
		switch (state /* | INPUT */) {
		case GOTTWO | DIGIT:
			*cp++ = byte; /*FALLTHROUGH*/
		case VIRGIN | DIGIT:
			state = GOTONE; byte = new; continue;
		case GOTONE | DIGIT:
			state = GOTTWO; byte = new + (byte << 4); continue;
		default: /* | DELIM */
			state = VIRGIN; *cp++ = byte; byte = 0; continue;
		case GOTONE | END:
		case GOTTWO | END:
			*cp++ = byte; /* FALLTHROUGH */
		case VIRGIN | END:
			break;
		}
		break;
	} while (cp < cplim);
	sa->sa_len = cp - (char *)sa;
}
