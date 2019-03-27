/*	$FreeBSD$	*/
/*	$KAME: rtadvd.c,v 1.82 2003/08/05 12:34:23 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * Copyright (C) 2011 Hiroki Sato <hrs@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>

#include <arpa/inet.h>

#include <netinet/in_var.h>
#include <netinet6/nd6.h>

#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <libutil.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include <poll.h>

#include "pathnames.h"
#include "rtadvd.h"
#include "if.h"
#include "rrenum.h"
#include "advcap.h"
#include "timer_subr.h"
#include "timer.h"
#include "config.h"
#include "control.h"
#include "control_server.h"

#define RTADV_TYPE2BITMASK(type) (0x1 << type)

struct msghdr rcvmhdr;
static char *rcvcmsgbuf;
static size_t rcvcmsgbuflen;
static char *sndcmsgbuf = NULL;
static size_t sndcmsgbuflen;
struct msghdr sndmhdr;
struct iovec rcviov[2];
struct iovec sndiov[2];
struct sockaddr_in6 rcvfrom;
static const char *pidfilename = _PATH_RTADVDPID;
const char *conffile = _PATH_RTADVDCONF;
static struct pidfh *pfh;
static int dflag, sflag;
static int wait_shutdown;

#define	PFD_RAWSOCK	0
#define	PFD_RTSOCK	1
#define	PFD_CSOCK	2
#define	PFD_MAX		3

struct railist_head_t railist =
    TAILQ_HEAD_INITIALIZER(railist);
struct ifilist_head_t ifilist =
    TAILQ_HEAD_INITIALIZER(ifilist);

struct nd_optlist {
	TAILQ_ENTRY(nd_optlist)	nol_next;
	struct nd_opt_hdr *nol_opt;
};
union nd_opt {
	struct nd_opt_hdr *opt_array[9];
	struct {
		struct nd_opt_hdr *zero;
		struct nd_opt_hdr *src_lladdr;
		struct nd_opt_hdr *tgt_lladdr;
		struct nd_opt_prefix_info *pi;
		struct nd_opt_rd_hdr *rh;
		struct nd_opt_mtu *mtu;
		TAILQ_HEAD(, nd_optlist) opt_list;
	} nd_opt_each;
};
#define opt_src_lladdr	nd_opt_each.src_lladdr
#define opt_tgt_lladdr	nd_opt_each.tgt_lladdr
#define opt_pi		nd_opt_each.pi
#define opt_rh		nd_opt_each.rh
#define opt_mtu		nd_opt_each.mtu
#define opt_list	nd_opt_each.opt_list

#define NDOPT_FLAG_SRCLINKADDR	(1 << 0)
#define NDOPT_FLAG_TGTLINKADDR	(1 << 1)
#define NDOPT_FLAG_PREFIXINFO	(1 << 2)
#define NDOPT_FLAG_RDHDR	(1 << 3)
#define NDOPT_FLAG_MTU		(1 << 4)
#define NDOPT_FLAG_RDNSS	(1 << 5)
#define NDOPT_FLAG_DNSSL	(1 << 6)

static uint32_t ndopt_flags[] = {
	[ND_OPT_SOURCE_LINKADDR]	= NDOPT_FLAG_SRCLINKADDR,
	[ND_OPT_TARGET_LINKADDR]	= NDOPT_FLAG_TGTLINKADDR,
	[ND_OPT_PREFIX_INFORMATION]	= NDOPT_FLAG_PREFIXINFO,
	[ND_OPT_REDIRECTED_HEADER]	= NDOPT_FLAG_RDHDR,
	[ND_OPT_MTU]			= NDOPT_FLAG_MTU,
	[ND_OPT_RDNSS]			= NDOPT_FLAG_RDNSS,
	[ND_OPT_DNSSL]			= NDOPT_FLAG_DNSSL,
};

static void	rtadvd_shutdown(void);
static void	sock_open(struct sockinfo *);
static void	rtsock_open(struct sockinfo *);
static void	rtadvd_input(struct sockinfo *);
static void	rs_input(int, struct nd_router_solicit *,
		    struct in6_pktinfo *, struct sockaddr_in6 *);
static void	ra_input(int, struct nd_router_advert *,
		    struct in6_pktinfo *, struct sockaddr_in6 *);
static int	prefix_check(struct nd_opt_prefix_info *, struct rainfo *,
		    struct sockaddr_in6 *);
static int	nd6_options(struct nd_opt_hdr *, int,
		    union nd_opt *, uint32_t);
static void	free_ndopts(union nd_opt *);
static void	rtmsg_input(struct sockinfo *);
static void	set_short_delay(struct ifinfo *);
static int	check_accept_rtadv(int);

static void
usage(void)
{

	fprintf(stderr, "usage: rtadvd [-dDfRs] "
	    "[-c configfile] [-C ctlsock] [-M ifname] [-p pidfile]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct pollfd set[PFD_MAX];
	struct timespec *timeout;
	int i, ch;
	int fflag = 0, logopt;
	int error;
	pid_t pid, otherpid;

	/* get command line options and arguments */
	while ((ch = getopt(argc, argv, "c:C:dDfhM:p:Rs")) != -1) {
		switch (ch) {
		case 'c':
			conffile = optarg;
			break;
		case 'C':
			ctrlsock.si_name = optarg;
			break;
		case 'd':
			dflag++;
			break;
		case 'D':
			dflag += 3;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'M':
			mcastif = optarg;
			break;
		case 'R':
			fprintf(stderr, "rtadvd: "
				"the -R option is currently ignored.\n");
			/* accept_rr = 1; */
			/* run anyway... */
			break;
		case 's':
			sflag = 1;
			break;
		case 'p':
			pidfilename = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	logopt = LOG_NDELAY | LOG_PID;
	if (fflag)
		logopt |= LOG_PERROR;
	openlog("rtadvd", logopt, LOG_DAEMON);

	/* set log level */
	if (dflag > 2)
		(void)setlogmask(LOG_UPTO(LOG_DEBUG));
	else if (dflag > 1)
		(void)setlogmask(LOG_UPTO(LOG_INFO));
	else if (dflag > 0)
		(void)setlogmask(LOG_UPTO(LOG_NOTICE));
	else
		(void)setlogmask(LOG_UPTO(LOG_ERR));

	/* timer initialization */
	rtadvd_timer_init();

	pfh = pidfile_open(pidfilename, 0600, &otherpid);
	if (pfh == NULL) {
		if (errno == EEXIST)
			errx(1, "%s already running, pid: %d",
			    getprogname(), otherpid);
		syslog(LOG_ERR,
		    "failed to open the pid file %s, run anyway.",
		    pidfilename);
	}
	if (!fflag)
		daemon(1, 0);

	sock_open(&sock);

	update_ifinfo(&ifilist, UPDATE_IFINFO_ALL);
	for (i = 0; i < argc; i++)
		update_persist_ifinfo(&ifilist, argv[i]);

	csock_open(&ctrlsock, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (ctrlsock.si_fd == -1) {
		syslog(LOG_ERR, "cannot open control socket: %s",
		    strerror(errno));
		exit(1);
	}

	/* record the current PID */
	pid = getpid();
	pidfile_write(pfh);

	set[PFD_RAWSOCK].fd = sock.si_fd;
	set[PFD_RAWSOCK].events = POLLIN;
	if (sflag == 0) {
		rtsock_open(&rtsock);
		set[PFD_RTSOCK].fd = rtsock.si_fd;
		set[PFD_RTSOCK].events = POLLIN;
	} else
		set[PFD_RTSOCK].fd = -1;
	set[PFD_CSOCK].fd = ctrlsock.si_fd;
	set[PFD_CSOCK].events = POLLIN;
	signal(SIGTERM, set_do_shutdown);
	signal(SIGINT, set_do_shutdown);
	signal(SIGHUP, set_do_reload);

	error = csock_listen(&ctrlsock);
	if (error) {
		syslog(LOG_ERR, "cannot listen control socket: %s",
		    strerror(errno));
		exit(1);
	}

	/* load configuration file */
	set_do_reload(0);

	while (1) {
		if (is_do_shutdown())
			rtadvd_shutdown();

		if (is_do_reload()) {
			loadconfig_ifname(reload_ifname());
			if (reload_ifname() == NULL)
				syslog(LOG_INFO,
				    "configuration file reloaded.");
			else
				syslog(LOG_INFO,
				    "configuration file for %s reloaded.",
				    reload_ifname());
			reset_do_reload();
		}

		/* timeout handler update for active interfaces */
		rtadvd_update_timeout_handler();

		/* timer expiration check and reset the timer */
		timeout = rtadvd_check_timer();

		if (timeout != NULL) {
			syslog(LOG_DEBUG,
			    "<%s> set timer to %ld:%ld. waiting for "
			    "inputs or timeout", __func__,
			    (long int)timeout->tv_sec,
			    (long int)timeout->tv_nsec / 1000);
		} else {
			syslog(LOG_DEBUG,
			    "<%s> there's no timer. waiting for inputs",
			    __func__);
		}
		if ((i = poll(set, sizeof(set)/sizeof(set[0]),
			    timeout ? (timeout->tv_sec * 1000 +
				timeout->tv_nsec / 1000 / 1000) : INFTIM)) < 0) {

			/* EINTR would occur if a signal was delivered */
			if (errno != EINTR)
				syslog(LOG_ERR, "poll() failed: %s",
				    strerror(errno));
			continue;
		}
		if (i == 0)	/* timeout */
			continue;
		if (rtsock.si_fd != -1 && set[PFD_RTSOCK].revents & POLLIN)
			rtmsg_input(&rtsock);

		if (set[PFD_RAWSOCK].revents & POLLIN)
			rtadvd_input(&sock);

		if (set[PFD_CSOCK].revents & POLLIN) {
			int fd;

			fd = csock_accept(&ctrlsock);
			if (fd == -1)
				syslog(LOG_ERR,
				    "cannot accept() control socket: %s",
				    strerror(errno));
			else {
				cm_handler_server(fd);
				close(fd);
			}
		}
	}
	exit(0);		/* NOTREACHED */
}

static void
rtadvd_shutdown(void)
{
	struct ifinfo *ifi;
	struct rainfo *rai;
	struct rdnss *rdn;
	struct dnssl *dns;

	if (wait_shutdown) {
		syslog(LOG_INFO,
		    "waiting expiration of the all RA timers.");

		TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
			/*
			 * Ignore !IFF_UP interfaces in waiting for shutdown.
			 */
			if (!(ifi->ifi_flags & IFF_UP) &&
			    ifi->ifi_ra_timer != NULL) {
				ifi->ifi_state = IFI_STATE_UNCONFIGURED;
				rtadvd_remove_timer(ifi->ifi_ra_timer);
				ifi->ifi_ra_timer = NULL;
				syslog(LOG_DEBUG, "<%s> %s(idx=%d) is down. "
				    "Timer removed and marked as UNCONFIGURED.",
				     __func__, ifi->ifi_ifname,
				    ifi->ifi_ifindex);
			}
		}
		TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
			if (ifi->ifi_ra_timer != NULL)
				break;
		}
		if (ifi == NULL) {
			syslog(LOG_NOTICE, "gracefully terminated.");
			exit(0);
		}

		sleep(1);
		return;
	}

	syslog(LOG_DEBUG, "<%s> cease to be an advertising router",
	    __func__);

	wait_shutdown = 1;

	TAILQ_FOREACH(rai, &railist, rai_next) {
		rai->rai_lifetime = 0;
		TAILQ_FOREACH(rdn, &rai->rai_rdnss, rd_next)
			rdn->rd_ltime = 0;
		TAILQ_FOREACH(dns, &rai->rai_dnssl, dn_next)
			dns->dn_ltime = 0;
	}
	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (!ifi->ifi_persist)
			continue;
		if (ifi->ifi_state == IFI_STATE_UNCONFIGURED)
			continue;
		if (ifi->ifi_ra_timer == NULL)
			continue;
		if (ifi->ifi_ra_lastsent.tv_sec == 0 &&
		    ifi->ifi_ra_lastsent.tv_nsec == 0 &&
		    ifi->ifi_ra_timer != NULL) {
			/*
			 * When RA configured but never sent,
			 * ignore the IF immediately.
			 */
			rtadvd_remove_timer(ifi->ifi_ra_timer);
			ifi->ifi_ra_timer = NULL;
			ifi->ifi_state = IFI_STATE_UNCONFIGURED;
			continue;
		}

		ifi->ifi_state = IFI_STATE_TRANSITIVE;

		/* Mark as the shut-down state. */
		ifi->ifi_rainfo_trans = ifi->ifi_rainfo;
		ifi->ifi_rainfo = NULL;

		ifi->ifi_burstcount = MAX_FINAL_RTR_ADVERTISEMENTS;
		ifi->ifi_burstinterval = MIN_DELAY_BETWEEN_RAS;

		ra_timer_update(ifi, &ifi->ifi_ra_timer->rat_tm);
		rtadvd_set_timer(&ifi->ifi_ra_timer->rat_tm,
		    ifi->ifi_ra_timer);
	}
	syslog(LOG_NOTICE, "final RA transmission started.");

	pidfile_remove(pfh);
	csock_close(&ctrlsock);
}

static void
rtmsg_input(struct sockinfo *s)
{
	int n, type, ifindex = 0, plen;
	size_t len;
	char msg[2048], *next, *lim;
	char ifname[IFNAMSIZ];
	struct if_announcemsghdr *ifan;
	struct rt_msghdr *rtm;
	struct prefix *pfx;
	struct rainfo *rai;
	struct in6_addr *addr;
	struct ifinfo *ifi;
	char addrbuf[INET6_ADDRSTRLEN];
	int prefixchange = 0;

	if (s == NULL) {
		syslog(LOG_ERR, "<%s> internal error", __func__);
		exit(1);
	}
	n = read(s->si_fd, msg, sizeof(msg));
	rtm = (struct rt_msghdr *)msg;
	syslog(LOG_DEBUG, "<%s> received a routing message "
	    "(type = %d, len = %d)", __func__, rtm->rtm_type, n);

	if (n > rtm->rtm_msglen) {
		/*
		 * This usually won't happen for messages received on
		 * a routing socket.
		 */
		syslog(LOG_DEBUG,
		    "<%s> received data length is larger than "
		    "1st routing message len. multiple messages? "
		    "read %d bytes, but 1st msg len = %d",
		    __func__, n, rtm->rtm_msglen);
#if 0
		/* adjust length */
		n = rtm->rtm_msglen;
#endif
	}

	lim = msg + n;
	for (next = msg; next < lim; next += len) {
		int oldifflags;

		next = get_next_msg(next, lim, 0, &len,
		    RTADV_TYPE2BITMASK(RTM_ADD) |
		    RTADV_TYPE2BITMASK(RTM_DELETE) |
		    RTADV_TYPE2BITMASK(RTM_NEWADDR) |
		    RTADV_TYPE2BITMASK(RTM_DELADDR) |
		    RTADV_TYPE2BITMASK(RTM_IFINFO) |
		    RTADV_TYPE2BITMASK(RTM_IFANNOUNCE));
		if (len == 0)
			break;
		type = ((struct rt_msghdr *)next)->rtm_type;
		switch (type) {
		case RTM_ADD:
		case RTM_DELETE:
			ifindex = get_rtm_ifindex(next);
			break;
		case RTM_NEWADDR:
		case RTM_DELADDR:
			ifindex = (int)((struct ifa_msghdr *)next)->ifam_index;
			break;
		case RTM_IFINFO:
			ifindex = (int)((struct if_msghdr *)next)->ifm_index;
			break;
		case RTM_IFANNOUNCE:
			ifan = (struct if_announcemsghdr *)next;
			switch (ifan->ifan_what) {
			case IFAN_ARRIVAL:
			case IFAN_DEPARTURE:
				break;
			default:
				syslog(LOG_DEBUG,
				    "<%s:%d> unknown ifan msg (ifan_what=%d)",
				   __func__, __LINE__, ifan->ifan_what);
				continue;
			}

			syslog(LOG_DEBUG, "<%s>: if_announcemsg (idx=%d:%d)",
			       __func__, ifan->ifan_index, ifan->ifan_what);
			switch (ifan->ifan_what) {
			case IFAN_ARRIVAL:
				syslog(LOG_NOTICE,
				    "interface added (idx=%d)",
				    ifan->ifan_index);
				update_ifinfo(&ifilist, ifan->ifan_index);
				loadconfig_index(ifan->ifan_index);
				break;
			case IFAN_DEPARTURE:
				syslog(LOG_NOTICE,
				    "interface removed (idx=%d)",
				    ifan->ifan_index);
				rm_ifinfo_index(ifan->ifan_index);

				/* Clear ifi_ifindex */
				TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
					if (ifi->ifi_ifindex
					    == ifan->ifan_index) {
						ifi->ifi_ifindex = 0;
						break;
					}
				}
				update_ifinfo(&ifilist, ifan->ifan_index);
				break;
			}
			continue;
		default:
			/* should not reach here */
			syslog(LOG_DEBUG,
			       "<%s:%d> unknown rtmsg %d on %s",
			       __func__, __LINE__, type,
			       if_indextoname(ifindex, ifname));
			continue;
		}
		ifi = if_indextoifinfo(ifindex);
		if (ifi == NULL) {
			syslog(LOG_DEBUG,
			    "<%s> ifinfo not found for idx=%d.  Why?",
			    __func__, ifindex);
			continue;
		}
		rai = ifi->ifi_rainfo;
		if (rai == NULL) {
			syslog(LOG_DEBUG,
			    "<%s> route changed on "
			    "non advertising interface(%s)",
			    __func__, ifi->ifi_ifname);
			continue;
		}

		oldifflags = ifi->ifi_flags;
		/* init ifflags because it may have changed */
		update_ifinfo(&ifilist, ifindex);

		switch (type) {
		case RTM_ADD:
			if (sflag)
				break;	/* we aren't interested in prefixes  */

			addr = get_addr(msg);
			plen = get_prefixlen(msg);
			/* sanity check for plen */
			/* as RFC2373, prefixlen is at least 4 */
			if (plen < 4 || plen > 127) {
				syslog(LOG_INFO, "<%s> new interface route's"
				    "plen %d is invalid for a prefix",
				    __func__, plen);
				break;
			}
			pfx = find_prefix(rai, addr, plen);
			if (pfx) {
				if (pfx->pfx_timer) {
					/*
					 * If the prefix has been invalidated,
					 * make it available again.
					 */
					update_prefix(pfx);
					prefixchange = 1;
				} else
					syslog(LOG_DEBUG,
					    "<%s> new prefix(%s/%d) "
					    "added on %s, "
					    "but it was already in list",
					    __func__,
					    inet_ntop(AF_INET6, addr,
						(char *)addrbuf,
						sizeof(addrbuf)),
					    plen, ifi->ifi_ifname);
				break;
			}
			make_prefix(rai, ifindex, addr, plen);
			prefixchange = 1;
			break;
		case RTM_DELETE:
			if (sflag)
				break;

			addr = get_addr(msg);
			plen = get_prefixlen(msg);
			/* sanity check for plen */
			/* as RFC2373, prefixlen is at least 4 */
			if (plen < 4 || plen > 127) {
				syslog(LOG_INFO,
				    "<%s> deleted interface route's "
				    "plen %d is invalid for a prefix",
				    __func__, plen);
				break;
			}
			pfx = find_prefix(rai, addr, plen);
			if (pfx == NULL) {
				syslog(LOG_DEBUG,
				    "<%s> prefix(%s/%d) was deleted on %s, "
				    "but it was not in list",
				    __func__, inet_ntop(AF_INET6, addr,
					(char *)addrbuf, sizeof(addrbuf)),
					plen, ifi->ifi_ifname);
				break;
			}
			invalidate_prefix(pfx);
			prefixchange = 1;
			break;
		case RTM_NEWADDR:
		case RTM_DELADDR:
		case RTM_IFINFO:
			break;
		default:
			/* should not reach here */
			syslog(LOG_DEBUG,
			    "<%s:%d> unknown rtmsg %d on %s",
			    __func__, __LINE__, type,
			    if_indextoname(ifindex, ifname));
			return;
		}

		/* check if an interface flag is changed */
		if ((oldifflags & IFF_UP) && /* UP to DOWN */
		    !(ifi->ifi_flags & IFF_UP)) {
			syslog(LOG_NOTICE,
			    "<interface %s becomes down. stop timer.",
			    ifi->ifi_ifname);
			rtadvd_remove_timer(ifi->ifi_ra_timer);
			ifi->ifi_ra_timer = NULL;
		} else if (!(oldifflags & IFF_UP) && /* DOWN to UP */
		    (ifi->ifi_flags & IFF_UP)) {
			syslog(LOG_NOTICE,
			    "interface %s becomes up. restart timer.",
			    ifi->ifi_ifname);

			ifi->ifi_state = IFI_STATE_TRANSITIVE;
			ifi->ifi_burstcount =
			    MAX_INITIAL_RTR_ADVERTISEMENTS;
			ifi->ifi_burstinterval =
			    MAX_INITIAL_RTR_ADVERT_INTERVAL;

			ifi->ifi_ra_timer = rtadvd_add_timer(ra_timeout,
			    ra_timer_update, ifi, ifi);
			ra_timer_update(ifi, &ifi->ifi_ra_timer->rat_tm);
			rtadvd_set_timer(&ifi->ifi_ra_timer->rat_tm,
			    ifi->ifi_ra_timer);
		} else if (prefixchange &&
		    (ifi->ifi_flags & IFF_UP)) {
			/*
			 * An advertised prefix has been added or invalidated.
			 * Will notice the change in a short delay.
			 */
			set_short_delay(ifi);
		}
	}

	return;
}

void
rtadvd_input(struct sockinfo *s)
{
	ssize_t i;
	int *hlimp = NULL;
#ifdef OLDRAWSOCKET
	struct ip6_hdr *ip;
#endif
	struct icmp6_hdr *icp;
	int ifindex = 0;
	struct cmsghdr *cm;
	struct in6_pktinfo *pi = NULL;
	char ntopbuf[INET6_ADDRSTRLEN], ifnamebuf[IFNAMSIZ];
	struct in6_addr dst = in6addr_any;
	struct ifinfo *ifi;

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (s == NULL) {
		syslog(LOG_ERR, "<%s> internal error", __func__);
		exit(1);
	}
	/*
	 * Get message. We reset msg_controllen since the field could
	 * be modified if we had received a message before setting
	 * receive options.
	 */
	rcvmhdr.msg_controllen = rcvcmsgbuflen;
	if ((i = recvmsg(s->si_fd, &rcvmhdr, 0)) < 0)
		return;

	/* extract optional information via Advanced API */
	for (cm = (struct cmsghdr *)CMSG_FIRSTHDR(&rcvmhdr);
	     cm;
	     cm = (struct cmsghdr *)CMSG_NXTHDR(&rcvmhdr, cm)) {
		if (cm->cmsg_level == IPPROTO_IPV6 &&
		    cm->cmsg_type == IPV6_PKTINFO &&
		    cm->cmsg_len == CMSG_LEN(sizeof(struct in6_pktinfo))) {
			pi = (struct in6_pktinfo *)(CMSG_DATA(cm));
			ifindex = pi->ipi6_ifindex;
			dst = pi->ipi6_addr;
		}
		if (cm->cmsg_level == IPPROTO_IPV6 &&
		    cm->cmsg_type == IPV6_HOPLIMIT &&
		    cm->cmsg_len == CMSG_LEN(sizeof(int)))
			hlimp = (int *)CMSG_DATA(cm);
	}
	if (ifindex == 0) {
		syslog(LOG_ERR, "failed to get receiving interface");
		return;
	}
	if (hlimp == NULL) {
		syslog(LOG_ERR, "failed to get receiving hop limit");
		return;
	}

	/*
	 * If we happen to receive data on an interface which is now gone
	 * or down, just discard the data.
	 */
	ifi = if_indextoifinfo(pi->ipi6_ifindex);
	if (ifi == NULL || !(ifi->ifi_flags & IFF_UP)) {
		syslog(LOG_INFO,
		    "<%s> received data on a disabled interface (%s)",
		    __func__,
		    (ifi == NULL) ? "[gone]" : ifi->ifi_ifname);
		return;
	}

#ifdef OLDRAWSOCKET
	if ((size_t)i < sizeof(struct ip6_hdr) + sizeof(struct icmp6_hdr)) {
		syslog(LOG_ERR,
		    "packet size(%d) is too short", i);
		return;
	}

	ip = (struct ip6_hdr *)rcvmhdr.msg_iov[0].iov_base;
	icp = (struct icmp6_hdr *)(ip + 1); /* XXX: ext. hdr? */
#else
	if ((size_t)i < sizeof(struct icmp6_hdr)) {
		syslog(LOG_ERR, "packet size(%zd) is too short", i);
		return;
	}

	icp = (struct icmp6_hdr *)rcvmhdr.msg_iov[0].iov_base;
#endif

	switch (icp->icmp6_type) {
	case ND_ROUTER_SOLICIT:
		/*
		 * Message verification - RFC 4861 6.1.1
		 * XXX: these checks must be done in the kernel as well,
		 *      but we can't completely rely on them.
		 */
		if (*hlimp != 255) {
			syslog(LOG_NOTICE,
			    "RS with invalid hop limit(%d) "
			    "received from %s on %s",
			    *hlimp,
			    inet_ntop(AF_INET6, &rcvfrom.sin6_addr, ntopbuf,
			    sizeof(ntopbuf)),
			    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
			return;
		}
		if (icp->icmp6_code) {
			syslog(LOG_NOTICE,
			    "RS with invalid ICMP6 code(%d) "
			    "received from %s on %s",
			    icp->icmp6_code,
			    inet_ntop(AF_INET6, &rcvfrom.sin6_addr, ntopbuf,
			    sizeof(ntopbuf)),
			    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
			return;
		}
		if ((size_t)i < sizeof(struct nd_router_solicit)) {
			syslog(LOG_NOTICE,
			    "RS from %s on %s does not have enough "
			    "length (len = %zd)",
			    inet_ntop(AF_INET6, &rcvfrom.sin6_addr, ntopbuf,
			    sizeof(ntopbuf)),
			    if_indextoname(pi->ipi6_ifindex, ifnamebuf), i);
			return;
		}
		rs_input(i, (struct nd_router_solicit *)icp, pi, &rcvfrom);
		break;
	case ND_ROUTER_ADVERT:
		/*
		 * Message verification - RFC 4861 6.1.2
		 * XXX: there's the same dilemma as above...
		 */
		if (!IN6_IS_ADDR_LINKLOCAL(&rcvfrom.sin6_addr)) {
			syslog(LOG_NOTICE,
			    "RA with non-linklocal source address "
			    "received from %s on %s",
			    inet_ntop(AF_INET6, &rcvfrom.sin6_addr,
			    ntopbuf, sizeof(ntopbuf)),
			    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
			return;
		}
		if (*hlimp != 255) {
			syslog(LOG_NOTICE,
			    "RA with invalid hop limit(%d) "
			    "received from %s on %s",
			    *hlimp,
			    inet_ntop(AF_INET6, &rcvfrom.sin6_addr, ntopbuf,
			    sizeof(ntopbuf)),
			    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
			return;
		}
		if (icp->icmp6_code) {
			syslog(LOG_NOTICE,
			    "RA with invalid ICMP6 code(%d) "
			    "received from %s on %s",
			    icp->icmp6_code,
			    inet_ntop(AF_INET6, &rcvfrom.sin6_addr, ntopbuf,
			    sizeof(ntopbuf)),
			    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
			return;
		}
		if ((size_t)i < sizeof(struct nd_router_advert)) {
			syslog(LOG_NOTICE,
			    "RA from %s on %s does not have enough "
			    "length (len = %zd)",
			    inet_ntop(AF_INET6, &rcvfrom.sin6_addr, ntopbuf,
			    sizeof(ntopbuf)),
			    if_indextoname(pi->ipi6_ifindex, ifnamebuf), i);
			return;
		}
		ra_input(i, (struct nd_router_advert *)icp, pi, &rcvfrom);
		break;
	case ICMP6_ROUTER_RENUMBERING:
		if (mcastif == NULL) {
			syslog(LOG_ERR, "received a router renumbering "
			    "message, but not allowed to be accepted");
			break;
		}
		rr_input(i, (struct icmp6_router_renum *)icp, pi, &rcvfrom,
		    &dst);
		break;
	default:
		/*
		 * Note that this case is POSSIBLE, especially just
		 * after invocation of the daemon. This is because we
		 * could receive message after opening the socket and
		 * before setting ICMP6 type filter(see sock_open()).
		 */
		syslog(LOG_ERR, "invalid icmp type(%d)", icp->icmp6_type);
		return;
	}

	return;
}

static void
rs_input(int len, struct nd_router_solicit *rs,
	 struct in6_pktinfo *pi, struct sockaddr_in6 *from)
{
	char ntopbuf[INET6_ADDRSTRLEN];
	char ifnamebuf[IFNAMSIZ];
	union nd_opt ndopts;
	struct rainfo *rai;
	struct ifinfo *ifi;
	struct soliciter *sol;

	syslog(LOG_DEBUG,
	    "<%s> RS received from %s on %s",
	    __func__,
	    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf, sizeof(ntopbuf)),
	    if_indextoname(pi->ipi6_ifindex, ifnamebuf));

	/* ND option check */
	memset(&ndopts, 0, sizeof(ndopts));
	TAILQ_INIT(&ndopts.opt_list);
	if (nd6_options((struct nd_opt_hdr *)(rs + 1),
			len - sizeof(struct nd_router_solicit),
			&ndopts, NDOPT_FLAG_SRCLINKADDR)) {
		syslog(LOG_INFO,
		    "<%s> ND option check failed for an RS from %s on %s",
		    __func__,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)),
		    if_indextoname(pi->ipi6_ifindex, ifnamebuf));
		return;
	}

	/*
	 * If the IP source address is the unspecified address, there
	 * must be no source link-layer address option in the message.
	 * (RFC 4861 6.1.1)
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&from->sin6_addr) &&
	    ndopts.opt_src_lladdr) {
		syslog(LOG_INFO,
		    "<%s> RS from unspecified src on %s has a link-layer"
		    " address option",
		    __func__, if_indextoname(pi->ipi6_ifindex, ifnamebuf));
		goto done;
	}

	ifi = if_indextoifinfo(pi->ipi6_ifindex);
	if (ifi == NULL) {
		syslog(LOG_INFO,
		    "<%s> if (idx=%d) not found.  Why?",
		    __func__, pi->ipi6_ifindex);
		goto done;
	}
	rai = ifi->ifi_rainfo;
	if (rai == NULL) {
		syslog(LOG_INFO,
		       "<%s> RS received on non advertising interface(%s)",
		       __func__,
		       if_indextoname(pi->ipi6_ifindex, ifnamebuf));
		goto done;
	}

	rai->rai_ifinfo->ifi_rsinput++;

	/*
	 * Decide whether to send RA according to the rate-limit
	 * consideration.
	 */

	/* record sockaddr waiting for RA, if possible */
	sol = (struct soliciter *)malloc(sizeof(*sol));
	if (sol) {
		sol->sol_addr = *from;
		/* XXX RFC 2553 need clarification on flowinfo */
		sol->sol_addr.sin6_flowinfo = 0;
		TAILQ_INSERT_TAIL(&rai->rai_soliciter, sol, sol_next);
	}

	/*
	 * If there is already a waiting RS packet, don't
	 * update the timer.
	 */
	if (ifi->ifi_rs_waitcount++)
		goto done;

	set_short_delay(ifi);

  done:
	free_ndopts(&ndopts);
	return;
}

static void
set_short_delay(struct ifinfo *ifi)
{
	long delay;	/* must not be greater than 1000000 */
	struct timespec interval, now, min_delay, tm_tmp, *rest;

	if (ifi->ifi_ra_timer == NULL)
		return;
	/*
	 * Compute a random delay. If the computed value
	 * corresponds to a time later than the time the next
	 * multicast RA is scheduled to be sent, ignore the random
	 * delay and send the advertisement at the
	 * already-scheduled time. RFC 4861 6.2.6
	 */
	delay = arc4random_uniform(MAX_RA_DELAY_TIME);
	interval.tv_sec = 0;
	interval.tv_nsec = delay * 1000;
	rest = rtadvd_timer_rest(ifi->ifi_ra_timer);
	if (TS_CMP(rest, &interval, <)) {
		syslog(LOG_DEBUG, "<%s> random delay is larger than "
		    "the rest of the current timer", __func__);
		interval = *rest;
	}

	/*
	 * If we sent a multicast Router Advertisement within
	 * the last MIN_DELAY_BETWEEN_RAS seconds, schedule
	 * the advertisement to be sent at a time corresponding to
	 * MIN_DELAY_BETWEEN_RAS plus the random value after the
	 * previous advertisement was sent.
	 */
	clock_gettime(CLOCK_MONOTONIC_FAST, &now);
	TS_SUB(&now, &ifi->ifi_ra_lastsent, &tm_tmp);
	min_delay.tv_sec = MIN_DELAY_BETWEEN_RAS;
	min_delay.tv_nsec = 0;
	if (TS_CMP(&tm_tmp, &min_delay, <)) {
		TS_SUB(&min_delay, &tm_tmp, &min_delay);
		TS_ADD(&min_delay, &interval, &interval);
	}
	rtadvd_set_timer(&interval, ifi->ifi_ra_timer);
}

static int
check_accept_rtadv(int idx)
{
	struct ifinfo *ifi;

	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (ifi->ifi_ifindex == idx)
			break;
	}
	if (ifi == NULL) {
		syslog(LOG_DEBUG,
		    "<%s> if (idx=%d) not found.  Why?",
		    __func__, idx);
		return (0);
	}
#if (__FreeBSD_version < 900000)
	/*
	 * RA_RECV: !ip6.forwarding && ip6.accept_rtadv
	 * RA_SEND: ip6.forwarding
	 */
	return ((getinet6sysctl(IPV6CTL_FORWARDING) == 0) &&
	    (getinet6sysctl(IPV6CTL_ACCEPT_RTADV) == 1));
#else
	/*
	 * RA_RECV: ND6_IFF_ACCEPT_RTADV
	 * RA_SEND: ip6.forwarding
	 */
	if (update_ifinfo_nd_flags(ifi) != 0) {
		syslog(LOG_ERR, "cannot get nd6 flags (idx=%d)", idx);
		return (0);
	}

	return (ifi->ifi_nd_flags & ND6_IFF_ACCEPT_RTADV);
#endif
}

static void
ra_input(int len, struct nd_router_advert *nra,
	 struct in6_pktinfo *pi, struct sockaddr_in6 *from)
{
	struct rainfo *rai;
	struct ifinfo *ifi;
	char ntopbuf[INET6_ADDRSTRLEN];
	char ifnamebuf[IFNAMSIZ];
	union nd_opt ndopts;
	const char *on_off[] = {"OFF", "ON"};
	uint32_t reachabletime, retranstimer, mtu;
	int inconsistent = 0;
	int error;

	syslog(LOG_DEBUG, "<%s> RA received from %s on %s", __func__,
	    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf, sizeof(ntopbuf)),
	    if_indextoname(pi->ipi6_ifindex, ifnamebuf));

	/* ND option check */
	memset(&ndopts, 0, sizeof(ndopts));
	TAILQ_INIT(&ndopts.opt_list);
	error = nd6_options((struct nd_opt_hdr *)(nra + 1),
	    len - sizeof(struct nd_router_advert), &ndopts,
	    NDOPT_FLAG_SRCLINKADDR | NDOPT_FLAG_PREFIXINFO | NDOPT_FLAG_MTU |
	    NDOPT_FLAG_RDNSS | NDOPT_FLAG_DNSSL);
	if (error) {
		syslog(LOG_INFO,
		    "<%s> ND option check failed for an RA from %s on %s",
		    __func__,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), if_indextoname(pi->ipi6_ifindex,
			ifnamebuf));
		return;
	}

	/*
	 * RA consistency check according to RFC 4861 6.2.7
	 */
	ifi = if_indextoifinfo(pi->ipi6_ifindex);
	if (ifi->ifi_rainfo == NULL) {
		syslog(LOG_INFO,
		    "<%s> received RA from %s on non-advertising"
		    " interface(%s)",
		    __func__,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), if_indextoname(pi->ipi6_ifindex,
			ifnamebuf));
		goto done;
	}
	rai = ifi->ifi_rainfo;
	ifi->ifi_rainput++;
	syslog(LOG_DEBUG, "<%s> ifi->ifi_rainput = %" PRIu64, __func__,
	    ifi->ifi_rainput);

	/* Cur Hop Limit value */
	if (nra->nd_ra_curhoplimit && rai->rai_hoplimit &&
	    nra->nd_ra_curhoplimit != rai->rai_hoplimit) {
		syslog(LOG_NOTICE,
		    "CurHopLimit inconsistent on %s:"
		    " %d from %s, %d from us",
		    ifi->ifi_ifname, nra->nd_ra_curhoplimit,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), rai->rai_hoplimit);
		inconsistent++;
	}
	/* M flag */
	if ((nra->nd_ra_flags_reserved & ND_RA_FLAG_MANAGED) !=
	    rai->rai_managedflg) {
		syslog(LOG_NOTICE,
		    "M flag inconsistent on %s:"
		    " %s from %s, %s from us",
		    ifi->ifi_ifname, on_off[!rai->rai_managedflg],
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), on_off[rai->rai_managedflg]);
		inconsistent++;
	}
	/* O flag */
	if ((nra->nd_ra_flags_reserved & ND_RA_FLAG_OTHER) !=
	    rai->rai_otherflg) {
		syslog(LOG_NOTICE,
		    "O flag inconsistent on %s:"
		    " %s from %s, %s from us",
		    ifi->ifi_ifname, on_off[!rai->rai_otherflg],
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), on_off[rai->rai_otherflg]);
		inconsistent++;
	}
#ifdef DRAFT_IETF_6MAN_IPV6ONLY_FLAG
	/* S "IPv6-Only" (Six, Silence-IPv4) flag */
	if ((nra->nd_ra_flags_reserved & ND_RA_FLAG_IPV6_ONLY) !=
	    rai->rai_ipv6onlyflg) {
		syslog(LOG_NOTICE,
		    "S flag inconsistent on %s:"
		    " %s from %s, %s from us",
		    ifi->ifi_ifname, on_off[!rai->rai_ipv6onlyflg],
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), on_off[rai->rai_ipv6onlyflg]);
		inconsistent++;
	}
#endif
	/* Reachable Time */
	reachabletime = ntohl(nra->nd_ra_reachable);
	if (reachabletime && rai->rai_reachabletime &&
	    reachabletime != rai->rai_reachabletime) {
		syslog(LOG_NOTICE,
		    "ReachableTime inconsistent on %s:"
		    " %d from %s, %d from us",
		    ifi->ifi_ifname, reachabletime,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), rai->rai_reachabletime);
		inconsistent++;
	}
	/* Retrans Timer */
	retranstimer = ntohl(nra->nd_ra_retransmit);
	if (retranstimer && rai->rai_retranstimer &&
	    retranstimer != rai->rai_retranstimer) {
		syslog(LOG_NOTICE,
		    "RetranceTimer inconsistent on %s:"
		    " %d from %s, %d from us",
		    ifi->ifi_ifname, retranstimer,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), rai->rai_retranstimer);
		inconsistent++;
	}
	/* Values in the MTU options */
	if (ndopts.opt_mtu) {
		mtu = ntohl(ndopts.opt_mtu->nd_opt_mtu_mtu);
		if (mtu && rai->rai_linkmtu && mtu != rai->rai_linkmtu) {
			syslog(LOG_NOTICE,
			    "MTU option value inconsistent on %s:"
			    " %d from %s, %d from us",
			    ifi->ifi_ifname, mtu,
			    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
				sizeof(ntopbuf)), rai->rai_linkmtu);
			inconsistent++;
		}
	}
	/* Preferred and Valid Lifetimes for prefixes */
	{
		struct nd_optlist *nol;

		if (ndopts.opt_pi)
			if (prefix_check(ndopts.opt_pi, rai, from))
				inconsistent++;

		TAILQ_FOREACH(nol, &ndopts.opt_list, nol_next)
			if (prefix_check((struct nd_opt_prefix_info *)nol->nol_opt,
				rai, from))
				inconsistent++;
	}

	if (inconsistent)
		ifi->ifi_rainconsistent++;

  done:
	free_ndopts(&ndopts);
	return;
}

static uint32_t
udiff(uint32_t u, uint32_t v)
{
	return (u >= v ? u - v : v - u);
}

/* return a non-zero value if the received prefix is inconsistent with ours */
static int
prefix_check(struct nd_opt_prefix_info *pinfo,
	struct rainfo *rai, struct sockaddr_in6 *from)
{
	struct ifinfo *ifi;
	uint32_t preferred_time, valid_time;
	struct prefix *pfx;
	int inconsistent = 0;
	char ntopbuf[INET6_ADDRSTRLEN];
	char prefixbuf[INET6_ADDRSTRLEN];
	struct timespec now;

#if 0				/* impossible */
	if (pinfo->nd_opt_pi_type != ND_OPT_PREFIX_INFORMATION)
		return (0);
#endif
	ifi = rai->rai_ifinfo;
	/*
	 * log if the adveritsed prefix has link-local scope(sanity check?)
	 */
	if (IN6_IS_ADDR_LINKLOCAL(&pinfo->nd_opt_pi_prefix))
		syslog(LOG_INFO,
		    "<%s> link-local prefix %s/%d is advertised "
		    "from %s on %s",
		    __func__,
		    inet_ntop(AF_INET6, &pinfo->nd_opt_pi_prefix, prefixbuf,
			sizeof(prefixbuf)),
		    pinfo->nd_opt_pi_prefix_len,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), ifi->ifi_ifname);

	if ((pfx = find_prefix(rai, &pinfo->nd_opt_pi_prefix,
		pinfo->nd_opt_pi_prefix_len)) == NULL) {
		syslog(LOG_INFO,
		    "<%s> prefix %s/%d from %s on %s is not in our list",
		    __func__,
		    inet_ntop(AF_INET6, &pinfo->nd_opt_pi_prefix, prefixbuf,
			sizeof(prefixbuf)),
		    pinfo->nd_opt_pi_prefix_len,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), ifi->ifi_ifname);
		return (0);
	}

	preferred_time = ntohl(pinfo->nd_opt_pi_preferred_time);
	if (pfx->pfx_pltimeexpire) {
		/*
		 * The lifetime is decremented in real time, so we should
		 * compare the expiration time.
		 * (RFC 2461 Section 6.2.7.)
		 * XXX: can we really expect that all routers on the link
		 * have synchronized clocks?
		 */
		clock_gettime(CLOCK_MONOTONIC_FAST, &now);
		preferred_time += now.tv_sec;

		if (!pfx->pfx_timer && rai->rai_clockskew &&
		    udiff(preferred_time, pfx->pfx_pltimeexpire) > rai->rai_clockskew) {
			syslog(LOG_INFO,
			    "<%s> preferred lifetime for %s/%d"
			    " (decr. in real time) inconsistent on %s:"
			    " %" PRIu32 " from %s, %" PRIu32 " from us",
			    __func__,
			    inet_ntop(AF_INET6, &pinfo->nd_opt_pi_prefix, prefixbuf,
				sizeof(prefixbuf)),
			    pinfo->nd_opt_pi_prefix_len,
			    ifi->ifi_ifname, preferred_time,
			    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
				sizeof(ntopbuf)), pfx->pfx_pltimeexpire);
			inconsistent++;
		}
	} else if (!pfx->pfx_timer && preferred_time != pfx->pfx_preflifetime)
		syslog(LOG_INFO,
		    "<%s> preferred lifetime for %s/%d"
		    " inconsistent on %s:"
		    " %d from %s, %d from us",
		    __func__,
		    inet_ntop(AF_INET6, &pinfo->nd_opt_pi_prefix, prefixbuf,
			sizeof(prefixbuf)),
		    pinfo->nd_opt_pi_prefix_len,
		    ifi->ifi_ifname, preferred_time,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), pfx->pfx_preflifetime);

	valid_time = ntohl(pinfo->nd_opt_pi_valid_time);
	if (pfx->pfx_vltimeexpire) {
		clock_gettime(CLOCK_MONOTONIC_FAST, &now);
		valid_time += now.tv_sec;

		if (!pfx->pfx_timer && rai->rai_clockskew &&
		    udiff(valid_time, pfx->pfx_vltimeexpire) > rai->rai_clockskew) {
			syslog(LOG_INFO,
			    "<%s> valid lifetime for %s/%d"
			    " (decr. in real time) inconsistent on %s:"
			    " %d from %s, %" PRIu32 " from us",
			    __func__,
			    inet_ntop(AF_INET6, &pinfo->nd_opt_pi_prefix, prefixbuf,
				sizeof(prefixbuf)),
			    pinfo->nd_opt_pi_prefix_len,
			    ifi->ifi_ifname, preferred_time,
			    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
				sizeof(ntopbuf)), pfx->pfx_vltimeexpire);
			inconsistent++;
		}
	} else if (!pfx->pfx_timer && valid_time != pfx->pfx_validlifetime) {
		syslog(LOG_INFO,
		    "<%s> valid lifetime for %s/%d"
		    " inconsistent on %s:"
		    " %d from %s, %d from us",
		    __func__,
		    inet_ntop(AF_INET6, &pinfo->nd_opt_pi_prefix, prefixbuf,
			sizeof(prefixbuf)),
		    pinfo->nd_opt_pi_prefix_len,
		    ifi->ifi_ifname, valid_time,
		    inet_ntop(AF_INET6, &from->sin6_addr, ntopbuf,
			sizeof(ntopbuf)), pfx->pfx_validlifetime);
		inconsistent++;
	}

	return (inconsistent);
}

struct prefix *
find_prefix(struct rainfo *rai, struct in6_addr *prefix, int plen)
{
	struct prefix *pfx;
	int bytelen, bitlen;
	char bitmask;

	TAILQ_FOREACH(pfx, &rai->rai_prefix, pfx_next) {
		if (plen != pfx->pfx_prefixlen)
			continue;

		bytelen = plen / 8;
		bitlen = plen % 8;
		bitmask = 0xff << (8 - bitlen);

		if (memcmp((void *)prefix, (void *)&pfx->pfx_prefix, bytelen))
			continue;

		if (bitlen == 0 ||
		    ((prefix->s6_addr[bytelen] & bitmask) ==
		     (pfx->pfx_prefix.s6_addr[bytelen] & bitmask))) {
			return (pfx);
		}
	}

	return (NULL);
}

/* check if p0/plen0 matches p1/plen1; return 1 if matches, otherwise 0. */
int
prefix_match(struct in6_addr *p0, int plen0,
	struct in6_addr *p1, int plen1)
{
	int bytelen, bitlen;
	char bitmask;

	if (plen0 < plen1)
		return (0);

	bytelen = plen1 / 8;
	bitlen = plen1 % 8;
	bitmask = 0xff << (8 - bitlen);

	if (memcmp((void *)p0, (void *)p1, bytelen))
		return (0);

	if (bitlen == 0 ||
	    ((p0->s6_addr[bytelen] & bitmask) ==
	     (p1->s6_addr[bytelen] & bitmask))) {
		return (1);
	}

	return (0);
}

static int
nd6_options(struct nd_opt_hdr *hdr, int limit,
	union nd_opt *ndopts, uint32_t optflags)
{
	int optlen = 0;

	for (; limit > 0; limit -= optlen) {
		if ((size_t)limit < sizeof(struct nd_opt_hdr)) {
			syslog(LOG_INFO, "<%s> short option header", __func__);
			goto bad;
		}

		hdr = (struct nd_opt_hdr *)((caddr_t)hdr + optlen);
		if (hdr->nd_opt_len == 0) {
			syslog(LOG_INFO,
			    "<%s> bad ND option length(0) (type = %d)",
			    __func__, hdr->nd_opt_type);
			goto bad;
		}
		optlen = hdr->nd_opt_len << 3;
		if (optlen > limit) {
			syslog(LOG_INFO, "<%s> short option", __func__);
			goto bad;
		}

		if (hdr->nd_opt_type > ND_OPT_MTU &&
		    hdr->nd_opt_type != ND_OPT_RDNSS &&
		    hdr->nd_opt_type != ND_OPT_DNSSL) {
			syslog(LOG_INFO, "<%s> unknown ND option(type %d)",
			    __func__, hdr->nd_opt_type);
			continue;
		}

		if ((ndopt_flags[hdr->nd_opt_type] & optflags) == 0) {
			syslog(LOG_INFO, "<%s> unexpected ND option(type %d)",
			    __func__, hdr->nd_opt_type);
			continue;
		}

		/*
		 * Option length check.  Do it here for all fixed-length
		 * options.
		 */
		switch (hdr->nd_opt_type) {
		case ND_OPT_MTU:
			if (optlen == sizeof(struct nd_opt_mtu))
				break;
			goto skip;
		case ND_OPT_RDNSS:
			if (optlen >= 24 &&
			    (optlen - sizeof(struct nd_opt_rdnss)) % 16 == 0)
				break;
			goto skip;
		case ND_OPT_DNSSL:
			if (optlen >= 16 &&
			    (optlen - sizeof(struct nd_opt_dnssl)) % 8 == 0)
				break;
			goto skip;
		case ND_OPT_PREFIX_INFORMATION:
			if (optlen == sizeof(struct nd_opt_prefix_info))
				break;
skip:
			syslog(LOG_INFO, "<%s> invalid option length",
			    __func__);
			continue;
		}

		switch (hdr->nd_opt_type) {
		case ND_OPT_TARGET_LINKADDR:
		case ND_OPT_REDIRECTED_HEADER:
		case ND_OPT_RDNSS:
		case ND_OPT_DNSSL:
			break;	/* we don't care about these options */
		case ND_OPT_SOURCE_LINKADDR:
		case ND_OPT_MTU:
			if (ndopts->opt_array[hdr->nd_opt_type]) {
				syslog(LOG_INFO,
				    "<%s> duplicated ND option (type = %d)",
				    __func__, hdr->nd_opt_type);
			}
			ndopts->opt_array[hdr->nd_opt_type] = hdr;
			break;
		case ND_OPT_PREFIX_INFORMATION:
		{
			struct nd_optlist *nol;

			if (ndopts->opt_pi == 0) {
				ndopts->opt_pi =
				    (struct nd_opt_prefix_info *)hdr;
				continue;
			}
			nol = malloc(sizeof(*nol));
			if (nol == NULL) {
				syslog(LOG_ERR, "<%s> can't allocate memory",
				    __func__);
				goto bad;
			}
			nol->nol_opt = hdr;
			TAILQ_INSERT_TAIL(&(ndopts->opt_list), nol, nol_next);

			break;
		}
		default:	/* impossible */
			break;
		}
	}

	return (0);

  bad:
	free_ndopts(ndopts);

	return (-1);
}

static void
free_ndopts(union nd_opt *ndopts)
{
	struct nd_optlist *nol;

	while ((nol = TAILQ_FIRST(&ndopts->opt_list)) != NULL) {
		TAILQ_REMOVE(&ndopts->opt_list, nol, nol_next);
		free(nol);
	}
}

void
sock_open(struct sockinfo *s)
{
	struct icmp6_filter filt;
	int on;
	/* XXX: should be max MTU attached to the node */
	static char answer[1500];

	syslog(LOG_DEBUG, "<%s> enter", __func__);

	if (s == NULL) {
		syslog(LOG_ERR, "<%s> internal error", __func__);
		exit(1);
	}
	rcvcmsgbuflen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
	    CMSG_SPACE(sizeof(int));
	rcvcmsgbuf = (char *)malloc(rcvcmsgbuflen);
	if (rcvcmsgbuf == NULL) {
		syslog(LOG_ERR, "<%s> not enough core", __func__);
		exit(1);
	}

	sndcmsgbuflen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
	    CMSG_SPACE(sizeof(int));
	sndcmsgbuf = (char *)malloc(sndcmsgbuflen);
	if (sndcmsgbuf == NULL) {
		syslog(LOG_ERR, "<%s> not enough core", __func__);
		exit(1);
	}

	if ((s->si_fd = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6)) < 0) {
		syslog(LOG_ERR, "<%s> socket: %s", __func__, strerror(errno));
		exit(1);
	}
	/* specify to tell receiving interface */
	on = 1;
	if (setsockopt(s->si_fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on,
	    sizeof(on)) < 0) {
		syslog(LOG_ERR, "<%s> IPV6_RECVPKTINFO: %s", __func__,
		    strerror(errno));
		exit(1);
	}
	on = 1;
	/* specify to tell value of hoplimit field of received IP6 hdr */
	if (setsockopt(s->si_fd, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &on,
		sizeof(on)) < 0) {
		syslog(LOG_ERR, "<%s> IPV6_RECVHOPLIMIT: %s", __func__,
		    strerror(errno));
		exit(1);
	}
	ICMP6_FILTER_SETBLOCKALL(&filt);
	ICMP6_FILTER_SETPASS(ND_ROUTER_SOLICIT, &filt);
	ICMP6_FILTER_SETPASS(ND_ROUTER_ADVERT, &filt);
	if (mcastif != NULL)
		ICMP6_FILTER_SETPASS(ICMP6_ROUTER_RENUMBERING, &filt);

	if (setsockopt(s->si_fd, IPPROTO_ICMPV6, ICMP6_FILTER, &filt,
	    sizeof(filt)) < 0) {
		syslog(LOG_ERR, "<%s> IICMP6_FILTER: %s",
		    __func__, strerror(errno));
		exit(1);
	}

	/* initialize msghdr for receiving packets */
	rcviov[0].iov_base = (caddr_t)answer;
	rcviov[0].iov_len = sizeof(answer);
	rcvmhdr.msg_name = (caddr_t)&rcvfrom;
	rcvmhdr.msg_namelen = sizeof(rcvfrom);
	rcvmhdr.msg_iov = rcviov;
	rcvmhdr.msg_iovlen = 1;
	rcvmhdr.msg_control = (caddr_t) rcvcmsgbuf;
	rcvmhdr.msg_controllen = rcvcmsgbuflen;

	/* initialize msghdr for sending packets */
	sndmhdr.msg_namelen = sizeof(struct sockaddr_in6);
	sndmhdr.msg_iov = sndiov;
	sndmhdr.msg_iovlen = 1;
	sndmhdr.msg_control = (caddr_t)sndcmsgbuf;
	sndmhdr.msg_controllen = sndcmsgbuflen;

	return;
}

/* open a routing socket to watch the routing table */
static void
rtsock_open(struct sockinfo *s)
{
	if (s == NULL) {
		syslog(LOG_ERR, "<%s> internal error", __func__);
		exit(1);
	}
	if ((s->si_fd = socket(PF_ROUTE, SOCK_RAW, 0)) < 0) {
		syslog(LOG_ERR,
		    "<%s> socket: %s", __func__, strerror(errno));
		exit(1);
	}
}

struct ifinfo *
if_indextoifinfo(int idx)
{
	struct ifinfo *ifi;
	char *name, name0[IFNAMSIZ];

	/* Check if the interface has a valid name or not. */
	if (if_indextoname(idx, name0) == NULL)
		return (NULL);

	TAILQ_FOREACH(ifi, &ifilist, ifi_next) {
		if (ifi->ifi_ifindex == idx)
			return (ifi);
	}

	if (ifi != NULL)
		syslog(LOG_DEBUG, "<%s> ifi found (idx=%d)",
		    __func__, idx);
	else
		syslog(LOG_DEBUG, "<%s> ifi not found (idx=%d)",
		    __func__, idx);

	return (NULL);		/* search failed */
}

void
ra_output(struct ifinfo *ifi)
{
	int i;
	struct cmsghdr *cm;
	struct in6_pktinfo *pi;
	struct soliciter *sol;
	struct rainfo *rai;

	switch (ifi->ifi_state) {
	case IFI_STATE_CONFIGURED:
		rai = ifi->ifi_rainfo;
		break;
	case IFI_STATE_TRANSITIVE:
		rai = ifi->ifi_rainfo_trans;
		break;
	case IFI_STATE_UNCONFIGURED:
		syslog(LOG_DEBUG, "<%s> %s is unconfigured.  "
		    "Skip sending RAs.",
		    __func__, ifi->ifi_ifname);
		return;
	default:
		rai = NULL;
	}
	if (rai == NULL) {
		syslog(LOG_DEBUG, "<%s> rainfo is NULL on %s."
		    "Skip sending RAs.",
		    __func__, ifi->ifi_ifname);
		return;
	}
	if (!(ifi->ifi_flags & IFF_UP)) {
		syslog(LOG_DEBUG, "<%s> %s is not up.  "
		    "Skip sending RAs.",
		    __func__, ifi->ifi_ifname);
		return;
	}
	/*
	 * Check lifetime, ACCEPT_RTADV flag, and ip6.forwarding.
	 *
	 * (lifetime == 0) = output
	 * (lifetime != 0 && (check_accept_rtadv()) = no output
	 *
	 * Basically, hosts MUST NOT send Router Advertisement
	 * messages at any time (RFC 4861, Section 6.2.3). However, it
	 * would sometimes be useful to allow hosts to advertise some
	 * parameters such as prefix information and link MTU. Thus,
	 * we allow hosts to invoke rtadvd only when router lifetime
	 * (on every advertising interface) is explicitly set
	 * zero. (see also the above section)
	 */
	syslog(LOG_DEBUG,
	    "<%s> check lifetime=%d, ACCEPT_RTADV=%d, ip6.forwarding=%d "
	    "on %s", __func__,
	    rai->rai_lifetime,
	    check_accept_rtadv(ifi->ifi_ifindex),
	    getinet6sysctl(IPV6CTL_FORWARDING),
	    ifi->ifi_ifname);

	if (rai->rai_lifetime != 0) {
		if (getinet6sysctl(IPV6CTL_FORWARDING) == 0) {
			syslog(LOG_ERR,
			    "non-zero lifetime RA "
			    "but net.inet6.ip6.forwarding=0.  "
			    "Ignored.");
			return;
		}
		if (check_accept_rtadv(ifi->ifi_ifindex)) {
			syslog(LOG_ERR,
			    "non-zero lifetime RA "
			    "on RA receiving interface %s."
			    "  Ignored.", ifi->ifi_ifname);
			return;
		}
	}

	make_packet(rai);	/* XXX: inefficient */

	sndmhdr.msg_name = (caddr_t)&sin6_linklocal_allnodes;
	sndmhdr.msg_iov[0].iov_base = (caddr_t)rai->rai_ra_data;
	sndmhdr.msg_iov[0].iov_len = rai->rai_ra_datalen;

	cm = CMSG_FIRSTHDR(&sndmhdr);
	/* specify the outgoing interface */
	cm->cmsg_level = IPPROTO_IPV6;
	cm->cmsg_type = IPV6_PKTINFO;
	cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	pi = (struct in6_pktinfo *)CMSG_DATA(cm);
	memset(&pi->ipi6_addr, 0, sizeof(pi->ipi6_addr));	/*XXX*/
	pi->ipi6_ifindex = ifi->ifi_ifindex;

	/* specify the hop limit of the packet */
	{
		int hoplimit = 255;

		cm = CMSG_NXTHDR(&sndmhdr, cm);
		cm->cmsg_level = IPPROTO_IPV6;
		cm->cmsg_type = IPV6_HOPLIMIT;
		cm->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cm), &hoplimit, sizeof(int));
	}

	syslog(LOG_DEBUG,
	    "<%s> send RA on %s, # of RS waitings = %d",
	    __func__, ifi->ifi_ifname, ifi->ifi_rs_waitcount);

	i = sendmsg(sock.si_fd, &sndmhdr, 0);

	if (i < 0 || (size_t)i != rai->rai_ra_datalen)  {
		if (i < 0) {
			syslog(LOG_ERR, "<%s> sendmsg on %s: %s",
			    __func__, ifi->ifi_ifname,
			    strerror(errno));
		}
	}

	/*
	 * unicast advertisements
	 * XXX commented out.  reason: though spec does not forbit it, unicast
	 * advert does not really help
	 */
	while ((sol = TAILQ_FIRST(&rai->rai_soliciter)) != NULL) {
		TAILQ_REMOVE(&rai->rai_soliciter, sol, sol_next);
		free(sol);
	}

	/* update timestamp */
	clock_gettime(CLOCK_MONOTONIC_FAST, &ifi->ifi_ra_lastsent);

	/* update counter */
	ifi->ifi_rs_waitcount = 0;
	ifi->ifi_raoutput++;

	switch (ifi->ifi_state) {
	case IFI_STATE_CONFIGURED:
		if (ifi->ifi_burstcount > 0)
			ifi->ifi_burstcount--;
		break;
	case IFI_STATE_TRANSITIVE:
		ifi->ifi_burstcount--;
		if (ifi->ifi_burstcount == 0) {
			if (ifi->ifi_rainfo == ifi->ifi_rainfo_trans) {
				/* Initial burst finished. */
				if (ifi->ifi_rainfo_trans != NULL)
					ifi->ifi_rainfo_trans = NULL;
			}

			/* Remove burst RA information */
			if (ifi->ifi_rainfo_trans != NULL) {
				rm_rainfo(ifi->ifi_rainfo_trans);
				ifi->ifi_rainfo_trans = NULL;
			}

			if (ifi->ifi_rainfo != NULL) {
				/*
				 * TRANSITIVE -> CONFIGURED
				 *
				 * After initial burst or transition from
				 * one configuration to another,
				 * ifi_rainfo always points to the next RA
				 * information.
				 */
				ifi->ifi_state = IFI_STATE_CONFIGURED;
				syslog(LOG_DEBUG,
				    "<%s> ifname=%s marked as "
				    "CONFIGURED.", __func__,
				    ifi->ifi_ifname);
			} else {
				/*
				 * TRANSITIVE -> UNCONFIGURED
				 *
				 * If ifi_rainfo points to NULL, this
				 * interface is shutting down.
				 *
				 */
				int error;

				ifi->ifi_state = IFI_STATE_UNCONFIGURED;
				syslog(LOG_DEBUG,
				    "<%s> ifname=%s marked as "
				    "UNCONFIGURED.", __func__,
				    ifi->ifi_ifname);
				error = sock_mc_leave(&sock,
				    ifi->ifi_ifindex);
				if (error)
					exit(1);
			}
		}
		break;
	}
}

/* process RA timer */
struct rtadvd_timer *
ra_timeout(void *arg)
{
	struct ifinfo *ifi;

	ifi = (struct ifinfo *)arg;
	syslog(LOG_DEBUG, "<%s> RA timer on %s is expired",
	    __func__, ifi->ifi_ifname);

	ra_output(ifi);

	return (ifi->ifi_ra_timer);
}

/* update RA timer */
void
ra_timer_update(void *arg, struct timespec *tm)
{
	uint16_t interval;
	struct rainfo *rai;
	struct ifinfo *ifi;

	ifi = (struct ifinfo *)arg;
	rai = ifi->ifi_rainfo;
	interval = 0;

	switch (ifi->ifi_state) {
	case IFI_STATE_UNCONFIGURED:
		return;
		break;
	case IFI_STATE_CONFIGURED:
		/*
		 * Whenever a multicast advertisement is sent from
		 * an interface, the timer is reset to a
		 * uniformly-distributed random value between the
		 * interface's configured MinRtrAdvInterval and
		 * MaxRtrAdvInterval (RFC4861 6.2.4).
		 */
		interval = rai->rai_mininterval;
		interval += arc4random_uniform(rai->rai_maxinterval -
		    rai->rai_mininterval);
		break;
	case IFI_STATE_TRANSITIVE:
		/*
		 * For the first few advertisements (up to
		 * MAX_INITIAL_RTR_ADVERTISEMENTS), if the randomly chosen
		 * interval is greater than
		 * MAX_INITIAL_RTR_ADVERT_INTERVAL, the timer SHOULD be
		 * set to MAX_INITIAL_RTR_ADVERT_INTERVAL instead.  (RFC
		 * 4861 6.2.4)
		 *
		 * In such cases, the router SHOULD transmit one or more
		 * (but not more than MAX_FINAL_RTR_ADVERTISEMENTS) final
		 * multicast Router Advertisements on the interface with a
		 * Router Lifetime field of zero.  (RFC 4861 6.2.5)
		 */
		interval = ifi->ifi_burstinterval;
		break;
	}

	tm->tv_sec = interval;
	tm->tv_nsec = 0;

	syslog(LOG_DEBUG,
	    "<%s> RA timer on %s is set to %ld:%ld",
	    __func__, ifi->ifi_ifname,
	    (long int)tm->tv_sec, (long int)tm->tv_nsec / 1000);

	return;
}
