/*	$KAME: rtsold.c,v 1.67 2003/05/17 18:16:15 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/capsicum.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <netinet/in_var.h>
#include <arpa/inet.h>

#include <netinet6/nd6.h>

#include <capsicum_helpers.h>
#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <libcasper.h>
#include <casper/cap_syslog.h>
#include <libutil.h>

#include "rtsold.h"

#define RTSOL_DUMPFILE	"/var/run/rtsold.dump"

struct timespec tm_max;
static int log_upto = 999;
static int fflag = 0;

int Fflag = 0;	/* force setting sysctl parameters */
int aflag = 0;
int dflag = 0;
int uflag = 0;

const char *otherconf_script;
const char *resolvconf_script = "/sbin/resolvconf";

cap_channel_t *capllflags, *capscript, *capsendmsg, *capsyslog;

/* protocol constants */
#define MAX_RTR_SOLICITATION_DELAY	1 /* second */
#define RTR_SOLICITATION_INTERVAL	4 /* seconds */
#define MAX_RTR_SOLICITATIONS		3 /* times */

/*
 * implementation dependent constants in seconds
 * XXX: should be configurable
 */
#define PROBE_INTERVAL 60

/* static variables and functions */
static int mobile_node = 0;

static sig_atomic_t do_dump, do_exit;
static struct pidfh *pfh;

static char **autoifprobe(void);
static int ifconfig(char *ifname);
static int init_capabilities(void);
static int make_packet(struct ifinfo *);
static struct timespec *rtsol_check_timer(void);

static void set_dumpfile(int);
static void set_exit(int);
static void usage(const char *progname);

int
main(int argc, char **argv)
{
	struct kevent events[2];
	FILE *dumpfp;
	struct ifinfo *ifi;
	struct timespec *timeout;
	const char *opts, *pidfilepath, *progname;
	int ch, error, kq, once, rcvsock, rtsock;

	progname = basename(argv[0]);
	if (strcmp(progname, "rtsold") == 0) {
		opts = "adDfFm1O:p:R:u";
		once = 0;
		pidfilepath = NULL;
	} else {
		opts = "adDFO:R:u";
		fflag = 1;
		once = 1;
	}

	while ((ch = getopt(argc, argv, opts)) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'd':
			dflag += 1;
			break;
		case 'D':
			dflag += 2;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'F':
			Fflag = 1;
			break;
		case 'm':
			mobile_node = 1;
			break;
		case '1':
			once = 1;
			break;
		case 'O':
			otherconf_script = optarg;
			break;
		case 'p':
			pidfilepath = optarg;
			break;
		case 'R':
			resolvconf_script = optarg;
			break;
		case 'u':
			uflag = 1;
			break;
		default:
			usage(progname);
		}
	}
	argc -= optind;
	argv += optind;

	if ((!aflag && argc == 0) || (aflag && argc != 0))
		usage(progname);

	/* Generate maximum time in timespec. */
	tm_max.tv_sec = (-1) & ~((time_t)1 << ((sizeof(tm_max.tv_sec) * 8) - 1));
	tm_max.tv_nsec = (-1) & ~((long)1 << ((sizeof(tm_max.tv_nsec) * 8) - 1));

	/* set log level */
	if (dflag > 1)
		log_upto = LOG_DEBUG;
	else if (dflag > 0)
		log_upto = LOG_INFO;
	else
		log_upto = LOG_NOTICE;

	if (otherconf_script != NULL && *otherconf_script != '/')
		errx(1, "configuration script (%s) must be an absolute path",
		    otherconf_script);
	if (*resolvconf_script != '/')
		errx(1, "configuration script (%s) must be an absolute path",
		    resolvconf_script);

	if (!fflag) {
		pfh = pidfile_open(pidfilepath, 0644, NULL);
		if (pfh == NULL)
			errx(1, "failed to open pidfile: %s", strerror(errno));
		if (daemon(0, 0) != 0)
			errx(1, "failed to daemonize");
	}

	if ((error = init_capabilities()) != 0)
		err(1, "failed to initialize capabilities");

	if (!fflag) {
		cap_openlog(capsyslog, progname, LOG_NDELAY | LOG_PID,
		    LOG_DAEMON);
		if (log_upto >= 0)
			(void)cap_setlogmask(capsyslog, LOG_UPTO(log_upto));
		(void)signal(SIGTERM, set_exit);
		(void)signal(SIGINT, set_exit);
		(void)signal(SIGUSR1, set_dumpfile);
		dumpfp = rtsold_init_dumpfile(RTSOL_DUMPFILE);
	} else
		dumpfp = NULL;

	kq = kqueue();
	if (kq < 0) {
		warnmsg(LOG_ERR, __func__, "failed to create a kqueue: %s",
		    strerror(errno));
		exit(1);
	}

	/* Open global sockets and register for read events. */
	if ((rtsock = rtsock_open()) < 0) {
		warnmsg(LOG_ERR, __func__, "failed to open routing socket");
		exit(1);
	}
	if ((rcvsock = recvsockopen()) < 0) {
		warnmsg(LOG_ERR, __func__, "failed to open receive socket");
		exit(1);
	}
	EV_SET(&events[0], rtsock, EVFILT_READ, EV_ADD, 0, 0, NULL);
	EV_SET(&events[1], rcvsock, EVFILT_READ, EV_ADD, 0, 0, NULL);
	if (kevent(kq, events, 2, NULL, 0, NULL) < 0) {
		warnmsg(LOG_ERR, __func__, "kevent(): %s", strerror(errno));
		exit(1);
	}

	/* Probe network interfaces and set up tracking info. */
	if (ifinit() != 0) {
		warnmsg(LOG_ERR, __func__, "failed to initialize interfaces");
		exit(1);
	}
	if (aflag)
		argv = autoifprobe();
	while (argv && *argv) {
		if (ifconfig(*argv)) {
			warnmsg(LOG_ERR, __func__,
			    "failed to initialize %s", *argv);
			exit(1);
		}
		argv++;
	}

	/* Write to our pidfile. */
	if (pfh != NULL && pidfile_write(pfh) != 0) {
		warnmsg(LOG_ERR, __func__,
		    "failed to open pidfile: %s", strerror(errno));
		exit(1);
	}

	/* Enter capability mode. */
	caph_cache_catpages();
	if (caph_enter_casper() != 0) {
		warnmsg(LOG_ERR, __func__, "caph_enter(): %s", strerror(errno));
		exit(1);
	}

	for (;;) {
		if (do_exit) {
			/* Handle SIGTERM, SIGINT. */
			if (pfh != NULL)
				pidfile_remove(pfh);
			break;
		}
		if (do_dump) {
			/* Handle SIGUSR1. */
			do_dump = 0;
			if (dumpfp != NULL)
				rtsold_dump(dumpfp);
		}

		timeout = rtsol_check_timer();

		if (once) {
			/* if we have no timeout, we are done (or failed) */
			if (timeout == NULL)
				break;

			/* if all interfaces have got RA packet, we are done */
			TAILQ_FOREACH(ifi, &ifinfo_head, ifi_next) {
				if (ifi->state != IFS_DOWN && ifi->racnt == 0)
					break;
			}
			if (ifi == NULL)
				break;
		}

		error = kevent(kq, NULL, 0, &events[0], 1, timeout);
		if (error < 1) {
			if (error < 0 && errno != EINTR)
				warnmsg(LOG_ERR, __func__, "kevent(): %s",
				    strerror(errno));
			continue;
		}

		if (events[0].ident == (uintptr_t)rtsock)
			rtsock_input(rtsock);
		else
			rtsol_input(rcvsock);
	}

	return (0);
}

static int
init_capabilities(void)
{
#ifdef WITH_CASPER
	const char *const scripts[2] = { resolvconf_script, otherconf_script };
	cap_channel_t *capcasper;
	nvlist_t *limits;

	capcasper = cap_init();
	if (capcasper == NULL)
		return (-1);

	capllflags = cap_service_open(capcasper, "rtsold.llflags");
	if (capllflags == NULL)
		return (-1);

	capscript = cap_service_open(capcasper, "rtsold.script");
	if (capscript == NULL)
		return (-1);
	limits = nvlist_create(0);
	nvlist_add_string_array(limits, "scripts", scripts,
	    otherconf_script != NULL ? 2 : 1);
	if (cap_limit_set(capscript, limits) != 0)
		return (-1);

	capsendmsg = cap_service_open(capcasper, "rtsold.sendmsg");
	if (capsendmsg == NULL)
		return (-1);

	if (!fflag) {
		capsyslog = cap_service_open(capcasper, "system.syslog");
		if (capsyslog == NULL)
			return (-1);
	}

	cap_close(capcasper);
#endif /* WITH_CASPER */
	return (0);
}

static int
ifconfig(char *ifname)
{
	struct ifinfo *ifi;
	struct sockaddr_dl *sdl;
	int flags;

	ifi = NULL;
	if ((sdl = if_nametosdl(ifname)) == NULL) {
		warnmsg(LOG_ERR, __func__,
		    "failed to get link layer information for %s", ifname);
		goto bad;
	}
	if (find_ifinfo(sdl->sdl_index)) {
		warnmsg(LOG_ERR, __func__,
		    "interface %s was already configured", ifname);
		goto bad;
	}

	if (Fflag) {
		struct in6_ndireq nd;
		int s;

		if ((s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
			warnmsg(LOG_ERR, __func__, "socket() failed.");
			goto bad;
		}
		memset(&nd, 0, sizeof(nd));
		strlcpy(nd.ifname, ifname, sizeof(nd.ifname));
		if (ioctl(s, SIOCGIFINFO_IN6, (caddr_t)&nd) < 0) {
			warnmsg(LOG_ERR, __func__,
			    "cannot get accept_rtadv flag");
			(void)close(s);
			goto bad;
		}
		nd.ndi.flags |= ND6_IFF_ACCEPT_RTADV;
		if (ioctl(s, SIOCSIFINFO_IN6, (caddr_t)&nd) < 0) {
			warnmsg(LOG_ERR, __func__,
			    "cannot set accept_rtadv flag");
			(void)close(s);
			goto bad;
		}
		(void)close(s);
	}

	if ((ifi = malloc(sizeof(*ifi))) == NULL) {
		warnmsg(LOG_ERR, __func__, "memory allocation failed");
		goto bad;
	}
	memset(ifi, 0, sizeof(*ifi));
	ifi->sdl = sdl;
	ifi->ifi_rdnss = IFI_DNSOPT_STATE_NOINFO;
	ifi->ifi_dnssl = IFI_DNSOPT_STATE_NOINFO;
	TAILQ_INIT(&ifi->ifi_rainfo);
	strlcpy(ifi->ifname, ifname, sizeof(ifi->ifname));

	/* construct a router solicitation message */
	if (make_packet(ifi))
		goto bad;

	/* set link ID of this interface. */
#ifdef HAVE_SCOPELIB
	if (inet_zoneid(AF_INET6, 2, ifname, &ifi->linkid))
		goto bad;
#else
	/* XXX: assume interface IDs as link IDs */
	ifi->linkid = ifi->sdl->sdl_index;
#endif

	/*
	 * check if the interface is available.
	 * also check if SIOCGIFMEDIA ioctl is OK on the interface.
	 */
	ifi->mediareqok = 1;
	ifi->active = interface_status(ifi);
	if (!ifi->mediareqok) {
		/*
		 * probe routers periodically even if the link status
		 * does not change.
		 */
		ifi->probeinterval = PROBE_INTERVAL;
	}

	/* activate interface: interface_up returns 0 on success */
	flags = interface_up(ifi->ifname);
	if (flags == 0)
		ifi->state = IFS_DELAY;
	else if (flags == IFS_TENTATIVE)
		ifi->state = IFS_TENTATIVE;
	else
		ifi->state = IFS_DOWN;

	rtsol_timer_update(ifi);

	TAILQ_INSERT_TAIL(&ifinfo_head, ifi, ifi_next);
	return (0);

bad:
	free(sdl);
	free(ifi);
	return (-1);
}

struct rainfo *
find_rainfo(struct ifinfo *ifi, struct sockaddr_in6 *sin6)
{
	struct rainfo *rai;

	TAILQ_FOREACH(rai, &ifi->ifi_rainfo, rai_next)
		if (memcmp(&rai->rai_saddr.sin6_addr, &sin6->sin6_addr,
		    sizeof(rai->rai_saddr.sin6_addr)) == 0)
			return (rai);

	return (NULL);
}

struct ifinfo *
find_ifinfo(int ifindex)
{
	struct ifinfo *ifi;

	TAILQ_FOREACH(ifi, &ifinfo_head, ifi_next) {
		if (ifi->sdl->sdl_index == ifindex)
			return (ifi);
	}
	return (NULL);
}

static int
make_packet(struct ifinfo *ifi)
{
	size_t packlen = sizeof(struct nd_router_solicit), lladdroptlen = 0;
	struct nd_router_solicit *rs;
	char *buf;

	if ((lladdroptlen = lladdropt_length(ifi->sdl)) == 0) {
		warnmsg(LOG_INFO, __func__,
		    "link-layer address option has null length"
		    " on %s. Treat as not included.", ifi->ifname);
	}
	packlen += lladdroptlen;
	ifi->rs_datalen = packlen;

	/* allocate buffer */
	if ((buf = malloc(packlen)) == NULL) {
		warnmsg(LOG_ERR, __func__,
		    "memory allocation failed for %s", ifi->ifname);
		return (-1);
	}
	ifi->rs_data = buf;

	/* fill in the message */
	rs = (struct nd_router_solicit *)buf;
	rs->nd_rs_type = ND_ROUTER_SOLICIT;
	rs->nd_rs_code = 0;
	rs->nd_rs_cksum = 0;
	rs->nd_rs_reserved = 0;
	buf += sizeof(*rs);

	/* fill in source link-layer address option */
	if (lladdroptlen)
		lladdropt_fill(ifi->sdl, (struct nd_opt_hdr *)buf);

	return (0);
}

static struct timespec *
rtsol_check_timer(void)
{
	static struct timespec returnval;
	struct timespec now, rtsol_timer;
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct ra_opt *rao, *raotmp;
	int error, flags;

	clock_gettime(CLOCK_MONOTONIC_FAST, &now);

	rtsol_timer = tm_max;

	TAILQ_FOREACH(ifi, &ifinfo_head, ifi_next) {
		if (TS_CMP(&ifi->expire, &now, <=)) {
			warnmsg(LOG_DEBUG, __func__, "timer expiration on %s, "
			    "state = %d", ifi->ifname, ifi->state);

			while((rai = TAILQ_FIRST(&ifi->ifi_rainfo)) != NULL) {
				/* Remove all RA options. */
				TAILQ_REMOVE(&ifi->ifi_rainfo, rai, rai_next);
				while ((rao = TAILQ_FIRST(&rai->rai_ra_opt)) !=
				    NULL) {
					TAILQ_REMOVE(&rai->rai_ra_opt, rao,
					    rao_next);
					if (rao->rao_msg != NULL)
						free(rao->rao_msg);
					free(rao);
				}
				free(rai);
			}
			switch (ifi->state) {
			case IFS_DOWN:
			case IFS_TENTATIVE:
				/* interface_up returns 0 on success */
				flags = interface_up(ifi->ifname);
				if (flags == 0)
					ifi->state = IFS_DELAY;
				else if (flags == IFS_TENTATIVE)
					ifi->state = IFS_TENTATIVE;
				else
					ifi->state = IFS_DOWN;
				break;
			case IFS_IDLE:
			{
				int oldstatus = ifi->active;
				int probe = 0;

				ifi->active = interface_status(ifi);

				if (oldstatus != ifi->active) {
					warnmsg(LOG_DEBUG, __func__,
					    "%s status is changed"
					    " from %d to %d",
					    ifi->ifname,
					    oldstatus, ifi->active);
					probe = 1;
					ifi->state = IFS_DELAY;
				} else if (ifi->probeinterval &&
				    (ifi->probetimer -=
				    ifi->timer.tv_sec) <= 0) {
					/* probe timer expired */
					ifi->probetimer =
					    ifi->probeinterval;
					probe = 1;
					ifi->state = IFS_PROBE;
				}

				/*
				 * If we need a probe, clear the previous
				 * status wrt the "other" configuration.
				 */
				if (probe)
					ifi->otherconfig = 0;
				if (probe && mobile_node) {
					error = cap_probe_defrouters(capsendmsg,
					    ifi);
					if (error != 0)
						warnmsg(LOG_DEBUG, __func__,
					    "failed to probe routers: %d",
						    error);
				}
				break;
			}
			case IFS_DELAY:
				ifi->state = IFS_PROBE;
				(void)cap_rssend(capsendmsg, ifi);
				break;
			case IFS_PROBE:
				if (ifi->probes < MAX_RTR_SOLICITATIONS)
					(void)cap_rssend(capsendmsg, ifi);
				else {
					warnmsg(LOG_INFO, __func__,
					    "No answer after sending %d RSs",
					    ifi->probes);
					ifi->probes = 0;
					ifi->state = IFS_IDLE;
				}
				break;
			}
			rtsol_timer_update(ifi);
		} else {
			/* Expiration check for RA options. */
			int expire = 0;

			TAILQ_FOREACH(rai, &ifi->ifi_rainfo, rai_next) {
				TAILQ_FOREACH_SAFE(rao, &rai->rai_ra_opt,
				    rao_next, raotmp) {
					warnmsg(LOG_DEBUG, __func__,
					    "RA expiration timer: "
					    "type=%d, msg=%s, expire=%s",
					    rao->rao_type, (char *)rao->rao_msg,
						sec2str(&rao->rao_expire));
					if (TS_CMP(&now, &rao->rao_expire,
					    >=)) {
						warnmsg(LOG_DEBUG, __func__,
						    "RA expiration timer: "
						    "expired.");
						TAILQ_REMOVE(&rai->rai_ra_opt,
						    rao, rao_next);
						if (rao->rao_msg != NULL)
							free(rao->rao_msg);
						free(rao);
						expire = 1;
					}
				}
			}
			if (expire)
				ra_opt_handler(ifi);
		}
		if (TS_CMP(&ifi->expire, &rtsol_timer, <))
			rtsol_timer = ifi->expire;
	}

	if (TS_CMP(&rtsol_timer, &tm_max, ==)) {
		warnmsg(LOG_DEBUG, __func__, "there is no timer");
		return (NULL);
	} else if (TS_CMP(&rtsol_timer, &now, <))
		/* this may occur when the interval is too small */
		returnval.tv_sec = returnval.tv_nsec = 0;
	else
		TS_SUB(&rtsol_timer, &now, &returnval);

	now.tv_sec += returnval.tv_sec;
	now.tv_nsec += returnval.tv_nsec;
	warnmsg(LOG_DEBUG, __func__, "New timer is %s",
	    sec2str(&now));

	return (&returnval);
}

void
rtsol_timer_update(struct ifinfo *ifi)
{
#define MILLION 1000000
#define DADRETRY 10		/* XXX: adhoc */
	long interval;
	struct timespec now;

	bzero(&ifi->timer, sizeof(ifi->timer));

	switch (ifi->state) {
	case IFS_DOWN:
	case IFS_TENTATIVE:
		if (++ifi->dadcount > DADRETRY) {
			ifi->dadcount = 0;
			ifi->timer.tv_sec = PROBE_INTERVAL;
		} else
			ifi->timer.tv_sec = 1;
		break;
	case IFS_IDLE:
		if (mobile_node)
			/* XXX should be configurable */
			ifi->timer.tv_sec = 3;
		else
			ifi->timer = tm_max;	/* stop timer(valid?) */
		break;
	case IFS_DELAY:
		interval = arc4random_uniform(MAX_RTR_SOLICITATION_DELAY * MILLION);
		ifi->timer.tv_sec = interval / MILLION;
		ifi->timer.tv_nsec = (interval % MILLION) * 1000;
		break;
	case IFS_PROBE:
		if (ifi->probes < MAX_RTR_SOLICITATIONS)
			ifi->timer.tv_sec = RTR_SOLICITATION_INTERVAL;
		else
			/*
			 * After sending MAX_RTR_SOLICITATIONS solicitations,
			 * we're just waiting for possible replies; there
			 * will be no more solicitation.  Thus, we change
			 * the timer value to MAX_RTR_SOLICITATION_DELAY based
			 * on RFC 2461, Section 6.3.7.
			 */
			ifi->timer.tv_sec = MAX_RTR_SOLICITATION_DELAY;
		break;
	default:
		warnmsg(LOG_ERR, __func__,
		    "illegal interface state(%d) on %s",
		    ifi->state, ifi->ifname);
		return;
	}

	/* reset the timer */
	if (TS_CMP(&ifi->timer, &tm_max, ==)) {
		ifi->expire = tm_max;
		warnmsg(LOG_DEBUG, __func__,
		    "stop timer for %s", ifi->ifname);
	} else {
		clock_gettime(CLOCK_MONOTONIC_FAST, &now);
		TS_ADD(&now, &ifi->timer, &ifi->expire);

		now.tv_sec += ifi->timer.tv_sec;
		now.tv_nsec += ifi->timer.tv_nsec;
		warnmsg(LOG_DEBUG, __func__, "set timer for %s to %s",
		    ifi->ifname, sec2str(&now));
	}

#undef MILLION
}

static void
set_dumpfile(int sig __unused)
{

	do_dump = 1;
}

static void
set_exit(int sig __unused)
{

	do_exit = 1;
}

static void
usage(const char *progname)
{

	if (strcmp(progname, "rtsold") == 0) {
		fprintf(stderr, "usage: rtsold [-dDfFm1] [-O script-name] "
		    "[-p pidfile] [-R script-name] interface ...\n");
		fprintf(stderr, "usage: rtsold [-dDfFm1] [-O script-name] "
		    "[-p pidfile] [-R script-name] -a\n");
	} else {
		fprintf(stderr, "usage: rtsol [-dDF] [-O script-name] "
		    "[-p pidfile] [-R script-name] interface ...\n");
		fprintf(stderr, "usage: rtsol [-dDF] [-O script-name] "
		    "[-p pidfile] [-R script-name] -a\n");
	}
	exit(1);
}

void
warnmsg(int priority, const char *func, const char *msg, ...)
{
	va_list ap;
	char buf[BUFSIZ];

	va_start(ap, msg);
	if (fflag) {
		if (priority <= log_upto)
			vwarnx(msg, ap);
	} else {
		snprintf(buf, sizeof(buf), "<%s> %s", func, msg);
		msg = buf;
		cap_vsyslog(capsyslog, priority, msg, ap);
	}
	va_end(ap);
}

/*
 * return a list of interfaces which is suitable to sending an RS.
 */
static char **
autoifprobe(void)
{
	static char **argv = NULL;
	static int n = 0;
	char **a;
	int s = 0, i, found;
	struct ifaddrs *ifap, *ifa;
	struct in6_ndireq nd;

	/* initialize */
	while (n--)
		free(argv[n]);
	if (argv) {
		free(argv);
		argv = NULL;
	}
	n = 0;

	if (getifaddrs(&ifap) != 0)
		return (NULL);

	if (!Fflag && (s = socket(AF_INET6, SOCK_DGRAM, 0)) < 0) {
		warnmsg(LOG_ERR, __func__, "socket");
		exit(1);
	}

	/* find an ethernet */
	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if ((ifa->ifa_flags & IFF_UP) == 0)
			continue;
		if ((ifa->ifa_flags & IFF_POINTOPOINT) != 0)
			continue;
		if ((ifa->ifa_flags & IFF_LOOPBACK) != 0)
			continue;
		if ((ifa->ifa_flags & IFF_MULTICAST) == 0)
			continue;

		if (ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		found = 0;
		for (i = 0; i < n; i++) {
			if (strcmp(argv[i], ifa->ifa_name) == 0) {
				found++;
				break;
			}
		}
		if (found)
			continue;

		/*
		 * Skip the interfaces which IPv6 and/or accepting RA
		 * is disabled.
		 */
		if (!Fflag) {
			memset(&nd, 0, sizeof(nd));
			strlcpy(nd.ifname, ifa->ifa_name, sizeof(nd.ifname));
			if (ioctl(s, SIOCGIFINFO_IN6, (caddr_t)&nd) < 0) {
				warnmsg(LOG_ERR, __func__,
					"ioctl(SIOCGIFINFO_IN6)");
				exit(1);
			}
			if ((nd.ndi.flags & ND6_IFF_IFDISABLED))
				continue;
			if (!(nd.ndi.flags & ND6_IFF_ACCEPT_RTADV))
				continue;
		}

		/* if we find multiple candidates, just warn. */
		if (n != 0 && dflag > 1)
			warnmsg(LOG_WARNING, __func__,
				"multiple interfaces found");

		a = realloc(argv, (n + 1) * sizeof(char *));
		if (a == NULL) {
			warnmsg(LOG_ERR, __func__, "realloc");
			exit(1);
		}
		argv = a;
		argv[n] = strdup(ifa->ifa_name);
		if (!argv[n]) {
			warnmsg(LOG_ERR, __func__, "malloc");
			exit(1);
		}
		n++;
	}

	if (n) {
		a = realloc(argv, (n + 1) * sizeof(char *));
		if (a == NULL) {
			warnmsg(LOG_ERR, __func__, "realloc");
			exit(1);
		}
		argv = a;
		argv[n] = NULL;

		if (dflag > 0) {
			for (i = 0; i < n; i++)
				warnmsg(LOG_WARNING, __func__, "probing %s",
					argv[i]);
		}
	}
	if (!Fflag)
		close(s);
	freeifaddrs(ifap);
	return (argv);
}
