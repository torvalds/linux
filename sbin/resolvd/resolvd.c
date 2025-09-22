/*	$OpenBSD: resolvd.c,v 1.32 2022/12/09 18:22:35 tb Exp $	*/
/*
 * Copyright (c) 2021 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2021 Theo de Raadt <deraadt@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/un.h>
#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/route.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <event.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	ROUTE_SOCKET_BUF_SIZE	16384
#define	ASR_MAXNS		10
#define	_PATH_LOCKFILE		"/dev/resolvd.lock"
#define	_PATH_UNWIND_SOCKET	"/dev/unwind.sock"
#define	_PATH_RESCONF		"/etc/resolv.conf"
#define	_PATH_RESCONF_NEW	"/etc/resolv.conf.new"

#ifndef nitems
#define	nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

__dead void	usage(void);

struct rdns_proposal {
	uint32_t	 if_index;
	int		 af;
	int		 prio;
	char		 ip[INET6_ADDRSTRLEN];
};

void		route_receive(int);
void		handle_route_message(struct rt_msghdr *, struct sockaddr **);
void		get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
void		solicit_dns_proposals(int);
void		regen_resolvconf(const char *reason);
int		cmp(const void *, const void *);
int		findslot(struct rdns_proposal *);
void		zeroslot(struct rdns_proposal *);

struct rdns_proposal	 learned[ASR_MAXNS];
int			 resolvfd = -1;
int			 newkevent = 1;

#ifndef SMALL
int			 open_unwind_ctl(void);
int			 check_unwind = 1, unwind_running = 0;

struct loggers {
	__dead void (*err)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	__dead void (*errx)(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
	void (*warn)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*warnx)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*info)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
	void (*debug)(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
};

void		warnx_verbose(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));

const struct loggers conslogger = {
	err,
	errx,
	warn,
	warnx,
	warnx_verbose, /* info */
	warnx_verbose /* debug */
};

__dead void	syslog_err(int, const char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
__dead void	syslog_errx(int, const char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
void		syslog_warn(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_warnx(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_info(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_debug(const char *, ...)
		    __attribute__((__format__ (printf, 1, 2)));
void		syslog_vstrerror(int, int, const char *, va_list)
		    __attribute__((__format__ (printf, 3, 0)));

int verbose = 0;

const struct loggers syslogger = {
	syslog_err,
	syslog_errx,
	syslog_warn,
	syslog_warnx,
	syslog_info,
	syslog_debug
};

const struct loggers *logger = &conslogger;

#define lerr(_e, _f...) logger->err((_e), _f)
#define lerrx(_e, _f...) logger->errx((_e), _f)
#define lwarn(_f...) logger->warn(_f)
#define lwarnx(_f...) logger->warnx(_f)
#define linfo(_f...) logger->info(_f)
#define ldebug(_f...) logger->debug(_f)
#else
#define lerr(x...) do {} while(0)
#define lerrx(x...) do {} while(0)
#define lwarn(x...) do {} while(0)
#define lwarnx(x...) do {} while(0)
#define linfo(x...) do {} while(0)
#define ldebug(x...) do {} while(0)
#endif /* SMALL */

enum {
	KQ_ROUTE,
	KQ_RESOLVE_CONF,
#ifndef SMALL
	KQ_UNWIND,
#endif
	KQ_TOTAL
};

int
main(int argc, char *argv[])
{
	struct timespec		 one = {1, 0};
	int			 kq, ch, debug = 0, routesock;
	int			 rtfilter, nready, lockfd;
	struct kevent		 kev[KQ_TOTAL];
#ifndef SMALL
	int			 unwindsock = -1;
#endif

	while ((ch = getopt(argc, argv, "dv")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'v':
#ifndef SMALL
			verbose++;
#endif
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	/* Check for root privileges. */
	if (geteuid())
		errx(1, "need root privileges");

	lockfd = open(_PATH_LOCKFILE, O_CREAT|O_RDWR|O_EXLOCK|O_NONBLOCK, 0600);
	if (lockfd == -1) {
		if (errno == EAGAIN)
			errx(1, "already running");
		err(1, "%s", _PATH_LOCKFILE);
	}

	if (!debug)
		daemon(0, 0);

#ifndef SMALL
	if (!debug) {
		openlog("resolvd", LOG_PID|LOG_NDELAY, LOG_DAEMON);
		logger = &syslogger;
	}
#endif

	signal(SIGHUP, SIG_IGN);

	if ((routesock = socket(AF_ROUTE, SOCK_RAW, 0)) == -1)
		lerr(1, "route socket");

	rtfilter = ROUTE_FILTER(RTM_PROPOSAL) | ROUTE_FILTER(RTM_IFANNOUNCE);
	if (setsockopt(routesock, AF_ROUTE, ROUTE_MSGFILTER, &rtfilter,
	    sizeof(rtfilter)) == -1)
		lerr(1, "setsockopt(ROUTE_MSGFILTER)");

	solicit_dns_proposals(routesock);

	if (unveil(_PATH_RESCONF, "rwc") == -1)
		lerr(1, "unveil " _PATH_RESCONF);
	if (unveil(_PATH_RESCONF_NEW, "rwc") == -1)
		lerr(1, "unveil " _PATH_RESCONF_NEW);
#ifndef SMALL
	if (unveil(_PATH_UNWIND_SOCKET, "w") == -1)
		lerr(1, "unveil " _PATH_UNWIND_SOCKET);
#endif

	if (pledge("stdio unix rpath wpath cpath", NULL) == -1)
		lerr(1, "pledge");

	if ((kq = kqueue()) == -1)
		lerr(1, "kqueue");

	for(;;) {
		int	i;

#ifndef SMALL
		if (!unwind_running && check_unwind) {
			check_unwind = 0;
			unwindsock = open_unwind_ctl();
			unwind_running = unwindsock != -1;
			if (unwind_running)
				regen_resolvconf("new unwind");
		}
#endif

		if (newkevent) {
			int kevi = 0;

			if (routesock != -1)
				EV_SET(&kev[kevi++], routesock, EVFILT_READ,
				    EV_ADD, 0, 0,
				    (void *)KQ_ROUTE);
			if (resolvfd != -1)
				EV_SET(&kev[kevi++], resolvfd, EVFILT_VNODE,
				    EV_ADD | EV_CLEAR,
				    NOTE_DELETE | NOTE_RENAME | NOTE_TRUNCATE | NOTE_WRITE, 0,
				    (void *)KQ_RESOLVE_CONF);

#ifndef SMALL
			if (unwind_running) {
				EV_SET(&kev[kevi++], unwindsock, EVFILT_READ,
				    EV_ADD, 0, 0,
				    (void *)KQ_UNWIND);
			}
#endif /* SMALL */

			if (kevent(kq, kev, kevi, NULL, 0, NULL) == -1)
				lerr(1, "kevent");
			newkevent = 0;
		}

		nready = kevent(kq, NULL, 0, kev, KQ_TOTAL, NULL);
		if (nready == -1) {
			if (errno == EINTR)
				continue;
			lerr(1, "kevent");
		}

		if (nready == 0)
			continue;

		for (i = 0; i < nready; i++) {
			unsigned short fflags = kev[i].fflags;

			switch ((int)(long)kev[i].udata) {
			case KQ_ROUTE:
				route_receive(routesock);
				break;

			case KQ_RESOLVE_CONF:
				if (fflags & (NOTE_DELETE | NOTE_RENAME)) {
					close(resolvfd);
					resolvfd = -1;
					regen_resolvconf("file delete/rename");
				}
				if (fflags & (NOTE_TRUNCATE | NOTE_WRITE)) {
					/* some editors truncate and write */
					if (fflags & NOTE_TRUNCATE)
						nanosleep(&one, NULL);
					regen_resolvconf("file trunc/write");
				}
				break;

#ifndef SMALL
			case KQ_UNWIND: {
				uint8_t buf[1024];
				ssize_t	n;

				n = read(unwindsock, buf, sizeof(buf));
				if (n == -1) {
					if (errno == EAGAIN || errno == EINTR)
						continue;
				}
				if (n == 0 || n == -1) {
					if (n == -1)
						check_unwind = 1;
					newkevent = 1;
					close(unwindsock);
					unwindsock = -1;
					unwind_running = 0;
					regen_resolvconf("unwind closed");
				} else
					lwarnx("read %ld from unwind ctl", n);
				break;
			}
#endif

			default:
				lwarnx("unknown kqueue event on %lu",
				    kev[i].ident);
			}
		}
	}
	return 0;
}

__dead void
usage(void)
{
	fprintf(stderr, "usage: resolvd [-dv]\n");
	exit(1);
}

void
route_receive(int fd)
{
	uint8_t			 rsock_buf[ROUTE_SOCKET_BUF_SIZE];
	struct sockaddr		*sa, *rti_info[RTAX_MAX];
	struct rt_msghdr	*rtm;
	ssize_t			 n;

	rtm = (struct rt_msghdr *) rsock_buf;
	if ((n = read(fd, rsock_buf, sizeof(rsock_buf))) == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		lwarn("%s: read error", __func__);
		return;
	}

	if (n == 0)
		lerr(1, "routing socket closed");

	if (n < (ssize_t)sizeof(rtm->rtm_msglen) || n < rtm->rtm_msglen) {
		lwarnx("partial rtm of %zd in buffer", n);
		return;
	}

	if (rtm->rtm_version != RTM_VERSION)
		return;

	if (rtm->rtm_pid == getpid())
		return;

	sa = (struct sockaddr *)(rsock_buf + rtm->rtm_hdrlen);
	get_rtaddrs(rtm->rtm_addrs, sa, rti_info);
	handle_route_message(rtm, rti_info);
}

void
zeroslot(struct rdns_proposal *tab)
{
	tab->prio = 0;
	tab->af = 0;
	tab->if_index = 0;
	tab->ip[0] = '\0';
}

int
findslot(struct rdns_proposal *tab)
{
	int i;

	for (i = 0; i < ASR_MAXNS; i++)
		if (tab[i].prio == 0)
			return i;

	/* New proposals might be important, so replace the last slot */
	i = ASR_MAXNS - 1;
	zeroslot(&tab[i]);
	return i;
}

void
handle_route_message(struct rt_msghdr *rtm, struct sockaddr **rti_info)
{
	struct rdns_proposal		 learning[nitems(learned)];
	struct sockaddr_rtdns		*rtdns;
	struct if_announcemsghdr	*ifan;
	size_t				 addrsz;
	int				 rdns_count, af, i;
	char				*src;

	memcpy(learning, learned, sizeof learned);

	switch (rtm->rtm_type) {
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *)rtm;
		if (ifan->ifan_what == IFAN_ARRIVAL)
			return;
		/* Delete proposals learned from departing interfaces */
		for (i = 0; i < ASR_MAXNS; i++)
			if (learning[i].if_index == ifan->ifan_index)
				zeroslot(&learning[i]);
		break;
	case RTM_PROPOSAL:
		if (rtm->rtm_priority == RTP_PROPOSAL_SOLICIT) {
#ifndef SMALL
			check_unwind = 1;
#endif /* SMALL */
			return;
		}

		if (!(rtm->rtm_addrs & RTA_DNS))
			return;

		rtdns = (struct sockaddr_rtdns*)rti_info[RTAX_DNS];
		src = rtdns->sr_dns;
		af = rtdns->sr_family;

		switch (af) {
		case AF_INET:
			addrsz = sizeof(struct in_addr);
			break;
		case AF_INET6:
			addrsz = sizeof(struct in6_addr);
			break;
		default:
			lwarnx("ignoring invalid RTM_PROPOSAL");
			return;
		}

		if ((rtdns->sr_len - 2) % addrsz != 0) {
			lwarnx("ignoring invalid RTM_PROPOSAL");
			return;
		}
		rdns_count = (rtdns->sr_len -
		    offsetof(struct sockaddr_rtdns, sr_dns)) / addrsz;

		/* New proposal from interface means previous proposals expire */
		for (i = 0; i < ASR_MAXNS; i++)
			if (learning[i].af == af &&
			    learning[i].if_index == rtm->rtm_index)
				zeroslot(&learning[i]);

		/* Add the new proposals */
		for (i = 0; i < rdns_count; i++) {
			struct sockaddr_storage ss;
			struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;
			int new, err;

			memset(&ss, 0, sizeof(ss));
			ss.ss_family = af;
			new = findslot(learning);
			switch (af) {
			case AF_INET:
				memcpy(&sin->sin_addr, src, addrsz);
				ss.ss_len = sizeof(*sin);
				break;
			case AF_INET6:
				memcpy(&sin6->sin6_addr, src, addrsz);
				if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
					sin6->sin6_scope_id = rtm->rtm_index;
				ss.ss_len = sizeof(*sin6);
				break;
			}
			src += addrsz;

			if ((err = getnameinfo((struct sockaddr *)&ss, ss.ss_len,
			    learning[new].ip, sizeof(learning[new].ip),
			    NULL, 0, NI_NUMERICHOST)) == 0) {
				learning[new].prio = rtm->rtm_priority;
				learning[new].if_index = rtm->rtm_index;
				learning[new].af = af;
			} else
				lwarnx("getnameinfo: %s", gai_strerror(err));
		}
		break;
	default:
		return;
	}

	/* Sort proposals, based upon priority */
	if (mergesort(learning, ASR_MAXNS, sizeof(learning[0]), cmp) == -1) {
		lwarn("mergesort");
		return;
	}

	/* Eliminate duplicate IPs per interface */
	for (i = 0; i < ASR_MAXNS - 1; i++) {
		int j;

		if (learning[i].prio == 0)
			continue;

		for (j = i + 1; j < ASR_MAXNS; j++) {
			if (learning[i].if_index == learning[j].if_index &&
			    strcmp(learning[i].ip, learning[j].ip) == 0) {
				zeroslot(&learning[j]);
			}
		}
	}

	/* If proposal result is different, rebuild the file */
	if (memcmp(learned, learning, sizeof(learned)) != 0) {
		memcpy(learned, learning, sizeof(learned));
		regen_resolvconf("route proposals");
	}
}

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

void
get_rtaddrs(int addrs, struct sockaddr *sa, struct sockaddr **rti_info)
{
	int	i;

	for (i = 0; i < RTAX_MAX; i++) {
		if (addrs & (1 << i)) {
			rti_info[i] = sa;
			sa = (struct sockaddr *)((char *)(sa) +
			    ROUNDUP(sa->sa_len));
		} else
			rti_info[i] = NULL;
	}
}

void
solicit_dns_proposals(int routesock)
{
	struct rt_msghdr	 rtm;
	struct iovec		 iov[1];
	int			 iovcnt = 0;

	memset(&rtm, 0, sizeof(rtm));

	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_type = RTM_PROPOSAL;
	rtm.rtm_msglen = sizeof(rtm);
	rtm.rtm_tableid = 0;
	rtm.rtm_index = 0;
	rtm.rtm_seq = arc4random();
	rtm.rtm_priority = RTP_PROPOSAL_SOLICIT;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	if (writev(routesock, iov, iovcnt) == -1)
		lwarn("failed to send solicitation");
}

void
regen_resolvconf(const char *why)
{
	struct iovec	 iov[UIO_MAXIOV];
	int		 i, fd, len, iovcnt = 0;

	linfo("rebuilding: %s", why);

	if ((fd = open(_PATH_RESCONF_NEW, O_CREAT|O_TRUNC|O_RDWR, 0644)) == -1) {
		lwarn(_PATH_RESCONF_NEW);
		return;
	}

	memset(iov, 0, sizeof(iov));

#ifndef SMALL
	if (unwind_running) {
		len = asprintf((char **)&iov[iovcnt].iov_base,
		    "nameserver 127.0.0.1 # resolvd: unwind\n");
		if (len < 0) {
			lwarn("asprintf");
			goto err;
		}
		iov[iovcnt++].iov_len = len;
	}

#endif /* SMALL */
	for (i = 0; i < ASR_MAXNS; i++) {
		if (learned[i].prio != 0) {
			char ifnambuf[IF_NAMESIZE], *ifnam;

			ifnam = if_indextoname(learned[i].if_index,
			    ifnambuf);
			len = asprintf((char **)&iov[iovcnt].iov_base,
			    "%snameserver %s # resolvd: %s\n",
#ifndef SMALL
			    unwind_running ? "#" : "",
#else
			    "",
#endif
			    learned[i].ip,
			    ifnam ? ifnam : "");
			if (len < 0) {
				lwarn("asprintf");
				goto err;
			}
			iov[iovcnt++].iov_len = len;
		}
	}

	/* Replay user-managed lines from old resolv.conf file */
	if (resolvfd == -1)
		resolvfd = open(_PATH_RESCONF, O_RDWR);
	if (resolvfd != -1) {
		char *line = NULL;
		size_t linesize = 0;
		ssize_t linelen;
		FILE *fp;
		int fd2;

		if ((fd2 = dup(resolvfd)) == -1)
			goto err;
		lseek(fd2, 0, SEEK_SET);
		fp = fdopen(fd2, "r");
		if (fp == NULL) {
			close(fd2);
			goto err;
		}
		while ((linelen = getline(&line, &linesize, fp)) != -1) {
			char *end = strchr(line, '\n');
			if (end)
				*end = '\0';
			if (strstr(line, "# resolvd: "))
				continue;
			len = asprintf((char **)&iov[iovcnt].iov_base, "%s\n",
			    line);
			if (len < 0) {
				lwarn("asprintf");
				free(line);
				fclose(fp);
				goto err;
			}
			iov[iovcnt++].iov_len = len;
			if (iovcnt >= UIO_MAXIOV) {
				lwarnx("too many user-managed lines");
				free(line);
				fclose(fp);
				goto err;
			}
		}
		free(line);
		fclose(fp);
	}

	if (iovcnt > 0) {
		if (writev(fd, iov, iovcnt) == -1) {
			lwarn("writev");
			goto err;
		}
	}

	if (fsync(fd) == -1) {
		lwarn("fsync");
		goto err;
	}
	if (rename(_PATH_RESCONF_NEW, _PATH_RESCONF) == -1)
		goto err;

	if (resolvfd == -1) {
		close(fd);
		resolvfd = open(_PATH_RESCONF, O_RDWR);
	} else {
		dup2(fd, resolvfd);
		close(fd);
	}

	newkevent = 1;
	goto out;

 err:
	if (fd != -1)
		close(fd);
	unlink(_PATH_RESCONF_NEW);
 out:
	for (i = 0; i < iovcnt; i++)
		free(iov[i].iov_base);

}

int
cmp(const void *a, const void *b)
{
	const struct rdns_proposal	*rpa = a, *rpb = b;

	return (rpa->prio < rpb->prio) ? -1 : (rpa->prio > rpb->prio);
}

#ifndef SMALL
int
open_unwind_ctl(void)
{
	static struct sockaddr_un	 sun;
	int				 s;

	if (sun.sun_family == 0) {
		sun.sun_family = AF_UNIX;
		strlcpy(sun.sun_path, _PATH_UNWIND_SOCKET, sizeof(sun.sun_path));
	}

	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) != -1) {
		if (connect(s, (struct sockaddr *)&sun, sizeof(sun)) == -1) {
			close(s);
			s = -1;
		}
	}
	newkevent = 1;
	return s;
}

void
syslog_vstrerror(int e, int priority, const char *fmt, va_list ap)
{
	char *s;

	if (vasprintf(&s, fmt, ap) == -1) {
		syslog(LOG_EMERG, "unable to alloc in syslog_vstrerror");
		exit(1);
	}
	syslog(priority, "%s: %s", s, strerror(e));
	free(s);
}

__dead void
syslog_err(int ecode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	syslog_vstrerror(errno, LOG_CRIT, fmt, ap);
	va_end(ap);
	exit(ecode);
}

__dead void
syslog_errx(int ecode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_CRIT, fmt, ap);
	va_end(ap);
	exit(ecode);
}

void
syslog_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	syslog_vstrerror(errno, LOG_ERR, fmt, ap);
	va_end(ap);
}

void
syslog_warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
}

void
syslog_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_INFO, fmt, ap);
	va_end(ap);
}

void
syslog_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_DEBUG, fmt, ap);
	va_end(ap);
}

void
warnx_verbose(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (verbose)
		vwarnx(fmt, ap);
	va_end(ap);
}

#endif /* SMALL */
