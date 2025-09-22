/*	$OpenBSD: frontend.c,v 1.57 2025/09/15 09:01:56 florian Exp $	*/

/*
 * Copyright (c) 2018 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

/*
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
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <arpa/inet.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet6/nd6.h>
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>

#include <ctype.h>
#include <errno.h>
#include <event.h>
#include <ifaddrs.h>
#include <imsg.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "rad.h"
#include "frontend.h"
#include "control.h"

#define	RA_MAX_SIZE		1500
#define	ROUTE_SOCKET_BUF_SIZE	16384

struct icmp6_ev {
	struct event		 ev;
	uint8_t			 answer[1500];
	struct msghdr		 rcvmhdr;
	struct iovec		 rcviov[1];
	struct sockaddr_in6	 from;
	int			 refcnt;
};

struct ra_iface {
	TAILQ_ENTRY(ra_iface)		 entry;
	struct icmp6_ev			*icmp6ev;
	struct ra_prefix_conf_head	 prefixes;
	struct ether_addr		 hw_addr;
	char				 name[IF_NAMESIZE];
	char				 conf_name[IF_NAMESIZE];
	uint32_t			 if_index;
	int				 rdomain;
	int				 removed;
	int				 link_state;
	int				 prefix_count;
	int				 has_autoconf_prefix;
	size_t				 datalen;
	uint8_t				 data[RA_MAX_SIZE];
};

#define ND_OPT_PREF64	38
struct nd_opt_pref64 {
	u_int8_t	nd_opt_pref64_type;
	u_int8_t	nd_opt_pref64_len;
	u_int16_t	nd_opt_pref64_sltime_plc;
	u_int8_t	nd_opt_pref64[12];
};

struct nd_opt_source_link_addr {
	u_int8_t		nd_opt_source_link_addr_type;
	u_int8_t		nd_opt_source_link_addr_len;
	struct ether_addr	nd_opt_source_link_addr_hw_addr;
};

TAILQ_HEAD(, ra_iface)	ra_interfaces;

__dead void		 frontend_shutdown(void);
void			 frontend_sig_handler(int, short, void *);
void			 frontend_startup(void);
void			 icmp6_receive(int, short, void *);
void			 join_all_routers_mcast_group(struct ra_iface *);
void			 leave_all_routers_mcast_group(struct ra_iface *);
int			 get_ifrdomain(char *);
void			 merge_ra_interface(char *, char *, struct ifaddrs *);
void			 merge_ra_interfaces(void);
struct ra_iface		*find_ra_iface_by_id(uint32_t);
struct ra_iface		*find_ra_iface_by_name(char *);
struct ra_iface_conf	*find_ra_iface_conf(struct ra_iface_conf_head *,
			    char *);
struct ra_prefix_conf	*find_ra_prefix_conf(struct ra_prefix_conf_head*,
			    struct in6_addr *, int);
struct icmp6_ev		*get_icmp6ev_by_rdomain(int);
void			 unref_icmp6ev(struct ra_iface *);
void			 set_icmp6sock(int, int);
void			 add_new_prefix_to_ra_iface(struct ra_iface *r,
			    struct in6_addr *, int, struct ra_prefix_conf *,
			    int, uint32_t, uint32_t);
void			 free_ra_iface(struct ra_iface *);
int			 in6_mask2prefixlen(struct in6_addr *);
void			 get_interface_prefixes(struct ra_iface *,
			     struct ra_prefix_conf *, struct ifaddrs *);
uint32_t		 calc_autoconf_ltime(time_t, uint32_t, uint32_t);
int			 build_packet(struct ra_iface *);
void			 build_leaving_packet(struct ra_iface *);
void			 ra_output(struct ra_iface *, struct sockaddr_in6 *);
void			 get_rtaddrs(int, struct sockaddr *,
			     struct sockaddr **);
void			 route_receive(int, short, void *);
void			 handle_route_message(struct rt_msghdr *,
			     struct sockaddr **);

struct rad_conf	*frontend_conf;
static struct imsgev	*iev_main;
static struct imsgev	*iev_engine;
struct event		 ev_route;
int			 ioctlsock = -1, routesock = -1;
struct ipv6_mreq	 all_routers;
struct msghdr		 sndmhdr;
struct iovec		 sndiov[2];

void
frontend_sig_handler(int sig, short event, void *bula)
{
	/*
	 * Normal signal handler rules don't apply because libevent
	 * decouples for us.
	 */

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		frontend_shutdown();
	default:
		fatalx("unexpected signal");
	}
}

void
frontend(int debug, int verbose)
{
	struct event		 ev_sigint, ev_sigterm;
	struct passwd		*pw;
	size_t			 sndcmsgbuflen;
	uint8_t			*sndcmsgbuf = NULL;

	frontend_conf = config_new_empty();

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if ((pw = getpwnam(RAD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("%s", "frontend");
	log_procinit("frontend");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	/* XXX pass in from main */
	if ((ioctlsock = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0)) == -1)
		fatal("socket");

	if (pledge("stdio inet unix recvfd route mcast", NULL) == -1)
		fatal("pledge");

	event_init();

	/* Setup signal handler. */
	signal_set(&ev_sigint, SIGINT, frontend_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, frontend_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Setup pipe and event handler to the parent process. */
	if ((iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_main->ibuf, 3) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_main->ibuf);
	iev_main->handler = frontend_dispatch_main;
	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	if (inet_pton(AF_INET6, "ff02::2",
	    &all_routers.ipv6mr_multiaddr.s6_addr) == -1)
		fatal("inet_pton");

	sndcmsgbuflen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
	    CMSG_SPACE(sizeof(int));
	if ((sndcmsgbuf = malloc(sndcmsgbuflen)) == NULL)
		fatal("%s", __func__);

	sndmhdr.msg_namelen = sizeof(struct sockaddr_in6);
	sndmhdr.msg_iov = sndiov;
	sndmhdr.msg_iovlen = 1;
	sndmhdr.msg_control = sndcmsgbuf;
	sndmhdr.msg_controllen = sndcmsgbuflen;

	TAILQ_INIT(&ra_interfaces);

	event_dispatch();

	frontend_shutdown();
}

__dead void
frontend_shutdown(void)
{
	/* Close pipes. */
	imsgbuf_write(&iev_engine->ibuf);
	imsgbuf_clear(&iev_engine->ibuf);
	close(iev_engine->ibuf.fd);
	imsgbuf_write(&iev_main->ibuf);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	config_clear(frontend_conf);

	free(iev_engine);
	free(iev_main);

	log_info("frontend exiting");
	exit(0);
}

int
frontend_imsg_compose_main(int type, pid_t pid, void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data,
	    datalen));
}

int
frontend_imsg_compose_engine(int type, pid_t pid, void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_engine, type, 0, pid, -1, data,
	    datalen));
}

void
frontend_dispatch_main(int fd, short event, void *bula)
{
	static struct rad_conf		*nconf;
	static struct ra_iface_conf	*ra_iface_conf;
	static struct ra_options_conf	*ra_options;
	struct imsg			 imsg;
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	struct ra_prefix_conf		*ra_prefix_conf;
	struct ra_rdnss_conf		*ra_rdnss_conf;
	struct ra_dnssl_conf		*ra_dnssl_conf;
	struct ra_pref64_conf		*pref64;
	int				 n, shut = 0, icmp6sock, rdomain;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* connection closed */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get error", __func__);
		if (n == 0)	/* No more messages. */
			break;

		switch (imsg.hdr.type) {
		case IMSG_SOCKET_IPC:
			/*
			 * Setup pipe and event handler to the engine
			 * process.
			 */
			if (iev_engine)
				fatalx("%s: received unexpected imsg fd to "
				    "frontend", __func__);
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg fd to "
				   "frontend but didn't receive any",
				   __func__);

			iev_engine = malloc(sizeof(struct imsgev));
			if (iev_engine == NULL)
				fatal(NULL);

			if (imsgbuf_init(&iev_engine->ibuf, fd) == -1)
				fatal(NULL);
			iev_engine->handler = frontend_dispatch_engine;
			iev_engine->events = EV_READ;

			event_set(&iev_engine->ev, iev_engine->ibuf.fd,
			iev_engine->events, iev_engine->handler, iev_engine);
			event_add(&iev_engine->ev, NULL);
			break;
		case IMSG_RECONF_CONF:
			if (nconf != NULL)
				fatalx("%s: IMSG_RECONF_CONF already in "
				    "progress", __func__);
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct rad_conf))
				fatalx("%s: IMSG_RECONF_CONF wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			if ((nconf = malloc(sizeof(struct rad_conf))) ==
			    NULL)
				fatal(NULL);
			memcpy(nconf, imsg.data, sizeof(struct rad_conf));
			SIMPLEQ_INIT(&nconf->ra_iface_list);
			SIMPLEQ_INIT(&nconf->ra_options.ra_rdnss_list);
			SIMPLEQ_INIT(&nconf->ra_options.ra_dnssl_list);
			SIMPLEQ_INIT(&nconf->ra_options.ra_pref64_list);
			ra_options = &nconf->ra_options;
			break;
		case IMSG_RECONF_RA_IFACE:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    ra_iface_conf))
				fatalx("%s: IMSG_RECONF_RA_IFACE wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			if ((ra_iface_conf = malloc(sizeof(struct
			    ra_iface_conf))) == NULL)
				fatal(NULL);
			memcpy(ra_iface_conf, imsg.data, sizeof(struct
			    ra_iface_conf));
			if (ra_iface_conf->name[IF_NAMESIZE - 1] != '\0')
				fatalx("%s: IMSG_RECONF_RA_IFACE invalid name",
				    __func__);

			ra_iface_conf->autoprefix = NULL;
			SIMPLEQ_INIT(&ra_iface_conf->ra_prefix_list);
			SIMPLEQ_INIT(&ra_iface_conf->ra_options.ra_rdnss_list);
			SIMPLEQ_INIT(&ra_iface_conf->ra_options.ra_dnssl_list);
			SIMPLEQ_INIT(&ra_iface_conf->ra_options.ra_pref64_list);
			SIMPLEQ_INSERT_TAIL(&nconf->ra_iface_list,
			    ra_iface_conf, entry);
			ra_options = &ra_iface_conf->ra_options;
			break;
		case IMSG_RECONF_RA_AUTOPREFIX:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    ra_prefix_conf))
				fatalx("%s: IMSG_RECONF_RA_AUTOPREFIX wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			if ((ra_iface_conf->autoprefix = malloc(sizeof(struct
			    ra_prefix_conf))) == NULL)
				fatal(NULL);
			memcpy(ra_iface_conf->autoprefix, imsg.data,
			    sizeof(struct ra_prefix_conf));
			break;
		case IMSG_RECONF_RA_PREFIX:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    ra_prefix_conf))
				fatalx("%s: IMSG_RECONF_RA_PREFIX wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			if ((ra_prefix_conf = malloc(sizeof(struct
			    ra_prefix_conf))) == NULL)
				fatal(NULL);
			memcpy(ra_prefix_conf, imsg.data,
			    sizeof(struct ra_prefix_conf));
			SIMPLEQ_INSERT_TAIL(&ra_iface_conf->ra_prefix_list,
			    ra_prefix_conf, entry);
			break;
		case IMSG_RECONF_RA_RDNSS:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    ra_rdnss_conf))
				fatalx("%s: IMSG_RECONF_RA_RDNSS wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			if ((ra_rdnss_conf = malloc(sizeof(struct
			    ra_rdnss_conf))) == NULL)
				fatal(NULL);
			memcpy(ra_rdnss_conf, imsg.data, sizeof(struct
			    ra_rdnss_conf));
			SIMPLEQ_INSERT_TAIL(&ra_options->ra_rdnss_list,
			    ra_rdnss_conf, entry);
			break;
		case IMSG_RECONF_RA_DNSSL:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    ra_dnssl_conf))
				fatalx("%s: IMSG_RECONF_RA_DNSSL wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			if ((ra_dnssl_conf = malloc(sizeof(struct
			    ra_dnssl_conf))) == NULL)
				fatal(NULL);
			memcpy(ra_dnssl_conf, imsg.data, sizeof(struct
			    ra_dnssl_conf));
			if (ra_dnssl_conf->search[MAX_SEARCH - 1] != '\0')
				fatalx("%s: IMSG_RECONF_RA_DNSSL invalid "
				    "search list", __func__);

			SIMPLEQ_INSERT_TAIL(&ra_options->ra_dnssl_list,
			    ra_dnssl_conf, entry);
			break;
		case IMSG_RECONF_RA_PREF64:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    ra_pref64_conf))
				fatalx("%s: IMSG_RECONF_RA_PREF64 wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			if ((pref64 = malloc(sizeof(struct ra_pref64_conf))) ==
			    NULL)
				fatal(NULL);
			memcpy(pref64, imsg.data, sizeof(struct ra_pref64_conf));
			SIMPLEQ_INSERT_TAIL(&ra_options->ra_pref64_list, pref64,
			    entry);
			break;
		case IMSG_RECONF_END:
			if (nconf == NULL)
				fatalx("%s: IMSG_RECONF_END without "
				    "IMSG_RECONF_CONF", __func__);
			merge_config(frontend_conf, nconf);
			merge_ra_interfaces();
			nconf = NULL;
			break;
		case IMSG_ICMP6SOCK:
			if ((icmp6sock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "ICMPv6 fd but didn't receive any",
				    __func__);
			if (IMSG_DATA_SIZE(imsg) != sizeof(rdomain))
				fatalx("%s: IMSG_ICMP6SOCK wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&rdomain, imsg.data, sizeof(rdomain));
			set_icmp6sock(icmp6sock, rdomain);
			break;
		case IMSG_ROUTESOCK:
			if (routesock != -1)
				fatalx("%s: received unexpected routesock fd",
				    __func__);
			if ((routesock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "routesocket fd but didn't receive any",
				    __func__);
			event_set(&ev_route, routesock, EV_READ | EV_PERSIST,
			    route_receive, NULL);
			break;
		case IMSG_STARTUP:
			frontend_startup();
			break;
		case IMSG_CONTROLFD:
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "control fd but didn't receive any",
				    __func__);
			/* Listen on control socket. */
			control_listen(fd);
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__,
			    imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* This pipe is dead. Remove its event handler. */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

void
frontend_dispatch_engine(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct imsg_send_ra	 send_ra;
	struct ra_iface		*ra_iface;
	uint32_t		 if_index;
	int			 n, shut = 0;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* connection closed */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("%s: imsg_get error", __func__);
		if (n == 0)	/* No more messages. */
			break;

		switch (imsg.hdr.type) {
		case IMSG_SEND_RA:
			if (IMSG_DATA_SIZE(imsg) != sizeof(send_ra))
				fatalx("%s: IMSG_SEND_RA wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&send_ra, imsg.data, sizeof(send_ra));
			ra_iface = find_ra_iface_by_id(send_ra.if_index);
			if (ra_iface)
				ra_output(ra_iface, &send_ra.to);
			break;
		case IMSG_REMOVE_IF:
			if (IMSG_DATA_SIZE(imsg) != sizeof(if_index))
				fatalx("%s: IMSG_REMOVE_IF wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&if_index, imsg.data, sizeof(if_index));
			ra_iface = find_ra_iface_by_id(if_index);
			if (ra_iface) {
				TAILQ_REMOVE(&ra_interfaces, ra_iface, entry);
				free_ra_iface(ra_iface);
			}
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__,
			    imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* This pipe is dead. Remove its event handler. */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

void
frontend_startup(void)
{
	if (!event_initialized(&ev_route))
		fatalx("%s: did not receive a route socket from the main "
		    "process", __func__);

	event_add(&ev_route, NULL);
}


void
icmp6_receive(int fd, short events, void *arg)
{
	struct icmp6_ev		*icmp6ev;
	struct icmp6_hdr	*icmp6_hdr;
	struct imsg_ra_rs	 ra_rs;
	struct in6_pktinfo	*pi = NULL;
	struct cmsghdr		*cm;
	ssize_t			 len;
	int			 if_index = 0, *hlimp = NULL;
	char			 ntopbuf[INET6_ADDRSTRLEN], ifnamebuf[IFNAMSIZ];

	icmp6ev = arg;
	if ((len = recvmsg(fd, &icmp6ev->rcvmhdr, 0)) == -1) {
		log_warn("recvmsg");
		return;
	}

	if ((size_t)len < sizeof(struct icmp6_hdr))
		return;

	icmp6_hdr = (struct icmp6_hdr *)icmp6ev->answer;
	if (icmp6_hdr->icmp6_type != ND_ROUTER_ADVERT &&
	    icmp6_hdr->icmp6_type != ND_ROUTER_SOLICIT)
		return;

	/* extract optional information via Advanced API */
	for (cm = (struct cmsghdr *)CMSG_FIRSTHDR(&icmp6ev->rcvmhdr); cm;
	    cm = (struct cmsghdr *)CMSG_NXTHDR(&icmp6ev->rcvmhdr, cm)) {
		if (cm->cmsg_level == IPPROTO_IPV6 &&
		    cm->cmsg_type == IPV6_PKTINFO &&
		    cm->cmsg_len == CMSG_LEN(sizeof(struct in6_pktinfo))) {
			pi = (struct in6_pktinfo *)(CMSG_DATA(cm));
			if_index = pi->ipi6_ifindex;
		}
		if (cm->cmsg_level == IPPROTO_IPV6 &&
		    cm->cmsg_type == IPV6_HOPLIMIT &&
		    cm->cmsg_len == CMSG_LEN(sizeof(int)))
			hlimp = (int *)CMSG_DATA(cm);
	}

	if (if_index == 0) {
		log_warnx("failed to get receiving interface");
		return;
	}

	if (hlimp == NULL) {
		log_warnx("failed to get receiving hop limit");
		return;
	}

	if (*hlimp != 255) {
		log_warnx("invalid RA or RS with hop limit of %d from %s on %s",
		    *hlimp, inet_ntop(AF_INET6, &icmp6ev->from.sin6_addr,
		    ntopbuf, INET6_ADDRSTRLEN), if_indextoname(if_index,
		    ifnamebuf));
		return;
	}

	log_debug("RA or RS with hop limit of %d from %s on %s",
	    *hlimp, inet_ntop(AF_INET6, &icmp6ev->from.sin6_addr,
	    ntopbuf, INET6_ADDRSTRLEN), if_indextoname(if_index,
	    ifnamebuf));

	if ((size_t)len > sizeof(ra_rs.packet)) {
		log_warnx("invalid RA or RS with size %ld from %s on %s",
		    len, inet_ntop(AF_INET6, &icmp6ev->from.sin6_addr,
		    ntopbuf, INET6_ADDRSTRLEN), if_indextoname(if_index,
		    ifnamebuf));
		return;
	}

	ra_rs.if_index = if_index;
	memcpy(&ra_rs.from,  &icmp6ev->from, sizeof(ra_rs.from));
	ra_rs.len = len;
	memcpy(ra_rs.packet, icmp6ev->answer, len);

	frontend_imsg_compose_engine(IMSG_RA_RS, 0, &ra_rs, sizeof(ra_rs));
}

void
join_all_routers_mcast_group(struct ra_iface *ra_iface)
{
	if (!event_initialized(&ra_iface->icmp6ev->ev))
		return;
	log_debug("joining multicast group on %s", ra_iface->name);
	all_routers.ipv6mr_interface = ra_iface->if_index;
	if (setsockopt(EVENT_FD(&ra_iface->icmp6ev->ev), IPPROTO_IPV6,
	    IPV6_JOIN_GROUP, &all_routers, sizeof(all_routers)) == -1)
		fatal("IPV6_JOIN_GROUP(%s)", ra_iface->name);
}

void
leave_all_routers_mcast_group(struct ra_iface *ra_iface)
{
	if (!event_initialized(&ra_iface->icmp6ev->ev))
		return;
	log_debug("leaving multicast group on %s", ra_iface->name);
	all_routers.ipv6mr_interface = ra_iface->if_index;
	setsockopt(EVENT_FD(&ra_iface->icmp6ev->ev), IPPROTO_IPV6,
	    IPV6_LEAVE_GROUP, &all_routers, sizeof(all_routers));
}

struct ra_iface*
find_ra_iface_by_id(uint32_t if_index)
{
	struct ra_iface	*ra_iface;

	TAILQ_FOREACH(ra_iface, &ra_interfaces, entry) {
		if (ra_iface->if_index == if_index)
			return ra_iface;
	}
	return (NULL);
}

struct ra_iface*
find_ra_iface_by_name(char *if_name)
{
	struct ra_iface	*ra_iface;

	TAILQ_FOREACH(ra_iface, &ra_interfaces, entry) {
		if (strcmp(ra_iface->name, if_name) == 0)
			return ra_iface;
	}
	return (NULL);
}

struct ra_iface_conf*
find_ra_iface_conf(struct ra_iface_conf_head *head, char *if_name)
{
	struct ra_iface_conf	*ra_iface_conf;

	SIMPLEQ_FOREACH(ra_iface_conf, head, entry) {
		if (strcmp(ra_iface_conf->name, if_name) == 0)
			return ra_iface_conf;
	}
	return (NULL);
}

int
get_ifrdomain(char *if_name)
{
	struct ifreq		 ifr;

	strlcpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));
	if (ioctl(ioctlsock, SIOCGIFRDOMAIN, (caddr_t)&ifr) == -1) {
		log_warn("SIOCGIFRDOMAIN");
		return -1;
	}
	return ifr.ifr_rdomainid;
}

void
merge_ra_interface(char *if_name, char *conf_name, struct ifaddrs *ifap)
{
	struct ra_iface		*ra_iface;
	struct ifaddrs		*ifa;
	struct sockaddr_in6	*sin6;
	struct in6_ifreq	 ifr6;
	struct sockaddr_dl	*sdl;
	struct ether_addr	 hw_addr;
	uint32_t		 if_index;
	int			 link_state = LINK_STATE_UNKNOWN;
	int			 has_linklocal = 0, ifrdomain;
	int			 has_hw_addr = 0;

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		if (ifa->ifa_addr->sa_family != AF_LINK &&
		    ifa->ifa_addr->sa_family != AF_INET6)
			continue;
		if (strcmp(if_name, ifa->ifa_name) != 0)
			continue;

		if (ifa->ifa_addr->sa_family == AF_LINK) {
			link_state =
			    ((struct if_data*)ifa->ifa_data)->ifi_link_state;
			sdl = (struct sockaddr_dl *)ifa->ifa_addr;
			if (sdl != NULL && (sdl->sdl_type == IFT_ETHER ||
			    sdl->sdl_type == IFT_CARP) && sdl->sdl_alen ==
			    ETHER_ADDR_LEN) {
				has_hw_addr = 1;
				memcpy(&hw_addr, LLADDR(sdl), ETHER_ADDR_LEN);
			}
		} else if (ifa->ifa_addr->sa_family == AF_INET6) {
			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;

			if (!IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
				continue;

			memset(&ifr6, 0, sizeof(ifr6));
			strlcpy(ifr6.ifr_name, if_name, sizeof(ifr6.ifr_name));
			memcpy(&ifr6.ifr_addr, sin6, sizeof(ifr6.ifr_addr));
			if (ioctl(ioctlsock, SIOCGIFAFLAG_IN6,
			    (caddr_t)&ifr6) == -1) {
				log_warn("SIOCGIFAFLAG_IN6");
				continue;
			}

			if (ifr6.ifr_ifru.ifru_flags6 & (IN6_IFF_TENTATIVE |
			    IN6_IFF_DUPLICATED))
				continue;
			has_linklocal = 1;
		}
	}

	ifrdomain = get_ifrdomain(if_name);

	if ((ra_iface = find_ra_iface_by_name(if_name)) != NULL) {
		ra_iface->link_state = link_state;
		if (!LINK_STATE_IS_UP(link_state)) {
			log_debug("%s down, removing", if_name);
			ra_iface->removed = 1;
		} else if (!has_linklocal) {
			log_debug("%s has no IPv6 link-local address, "
			    "removing", if_name);
			ra_iface->removed = 1;
		} else if (ifrdomain == -1) {
			log_debug("can't get rdomain for %s, removing", if_name);
			ra_iface->removed = 1;
		} else if (!has_hw_addr) {
			log_debug("%s has no mac address, removing", if_name);
			ra_iface->removed = 1;
		} else if (ra_iface->rdomain != ifrdomain) {
			leave_all_routers_mcast_group(ra_iface);
			unref_icmp6ev(ra_iface);
			ra_iface->rdomain = ifrdomain;
			ra_iface->icmp6ev = get_icmp6ev_by_rdomain(ifrdomain);
			join_all_routers_mcast_group(ra_iface);
			ra_iface->removed = 0;
		} else {
			log_debug("keeping interface %s", if_name);
			ra_iface->removed = 0;
		}
		memcpy(&ra_iface->hw_addr, &hw_addr, sizeof(hw_addr));
		return;
	}

	if (!LINK_STATE_IS_UP(link_state)) {
		log_debug("%s down, ignoring", if_name);
		return;
	}

	if (!has_linklocal) {
		log_debug("%s has no IPv6 link-local address, ignoring",
		    if_name);
		return;
	}

	if (ifrdomain == -1) {
		log_debug("can't get rdomain for %s, ignoring", if_name);
		return;
	}

	if (!has_hw_addr) {
		log_debug("%s has no mac address, ignoring", if_name);
		return;
	}

	log_debug("new interface %s", if_name);
	if ((if_index = if_nametoindex(if_name)) == 0)
		return;

	log_debug("adding interface %s", if_name);
	if ((ra_iface = calloc(1, sizeof(*ra_iface))) == NULL)
		fatal("%s", __func__);

	strlcpy(ra_iface->name, if_name, sizeof(ra_iface->name));
	strlcpy(ra_iface->conf_name, conf_name,
	    sizeof(ra_iface->conf_name));

	ra_iface->if_index = if_index;
	ra_iface->rdomain = ifrdomain;
	memcpy(&ra_iface->hw_addr, &hw_addr, sizeof(hw_addr));
	SIMPLEQ_INIT(&ra_iface->prefixes);

	ra_iface->icmp6ev = get_icmp6ev_by_rdomain(ifrdomain);
	join_all_routers_mcast_group(ra_iface);
	TAILQ_INSERT_TAIL(&ra_interfaces, ra_iface, entry);
}

void
merge_ra_interfaces(void)
{
	struct ra_iface_conf	*ra_iface_conf;
	struct ra_prefix_conf	*ra_prefix_conf;
	struct ra_iface		*ra_iface;
	struct ifgroupreq	 ifgr;
	struct ifg_req		*ifg;
	struct ifaddrs		*ifap;
	char			*conf_name;
	unsigned int		 len;

	if (getifaddrs(&ifap) != 0) {
		log_warn("getifaddrs");
		return;
	}

	TAILQ_FOREACH(ra_iface, &ra_interfaces, entry)
		ra_iface->removed = 1;

	SIMPLEQ_FOREACH(ra_iface_conf, &frontend_conf->ra_iface_list, entry) {
		conf_name = ra_iface_conf->name;

		/* check if network interface or group */
		if (isdigit((unsigned char)conf_name[strlen(conf_name) - 1])) {
			merge_ra_interface(conf_name, conf_name, ifap);
		} else {
			log_debug("interface group %s", conf_name);

			memset(&ifgr, 0, sizeof(ifgr));
			strlcpy(ifgr.ifgr_name, conf_name,
			    sizeof(ifgr.ifgr_name));
			if (ioctl(ioctlsock, SIOCGIFGMEMB,
			    (caddr_t)&ifgr) == -1)
				continue;

			len = ifgr.ifgr_len;
			if ((ifgr.ifgr_groups = calloc(1, len)) == NULL)
				fatal("%s: calloc", __func__);
			if (ioctl(ioctlsock, SIOCGIFGMEMB,
			    (caddr_t)&ifgr) == -1) {
				log_debug("group %s without members",
				    conf_name);
				free(ifgr.ifgr_groups);
				continue;
			}

			for (ifg = ifgr.ifgr_groups;
			    (ifg != NULL) && (len >= sizeof(struct ifg_req));
			    ifg++) {
				len -= sizeof(struct ifg_req);
				merge_ra_interface(ifg->ifgrq_member,
				    conf_name, ifap);
			}
			free(ifgr.ifgr_groups);
		}
	}

	TAILQ_FOREACH(ra_iface, &ra_interfaces, entry) {
		while ((ra_prefix_conf = SIMPLEQ_FIRST(&ra_iface->prefixes))
		    != NULL) {
			SIMPLEQ_REMOVE_HEAD(&ra_iface->prefixes,
			    entry);
			free(ra_prefix_conf);
		}
		ra_iface->prefix_count = 0;

		if (ra_iface->removed) {
			log_debug("iface removed: %s", ra_iface->name);
			build_leaving_packet(ra_iface);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0,
			    &ra_iface->if_index, sizeof(ra_iface->if_index));
			continue;
		}

		ra_iface_conf = find_ra_iface_conf(
		    &frontend_conf->ra_iface_list, ra_iface->conf_name);

		log_debug("add static prefixes for %s", ra_iface->name);

		SIMPLEQ_FOREACH(ra_prefix_conf, &ra_iface_conf->ra_prefix_list,
		    entry) {
			add_new_prefix_to_ra_iface(ra_iface,
			    &ra_prefix_conf->prefix,
			    ra_prefix_conf->prefixlen, ra_prefix_conf, 0,
			    ND6_INFINITE_LIFETIME, ND6_INFINITE_LIFETIME);
		}

		if (ra_iface_conf->autoprefix)
			get_interface_prefixes(ra_iface,
			    ra_iface_conf->autoprefix, ifap);

		if (build_packet(ra_iface)) {
			/* packet changed; send new advertisements */
			if (event_initialized(&ra_iface->icmp6ev->ev))
				frontend_imsg_compose_engine(IMSG_UPDATE_IF, 0,
				    &ra_iface->if_index,
				    sizeof(ra_iface->if_index));
		}
	}
	freeifaddrs(ifap);
}

void
free_ra_iface(struct ra_iface *ra_iface)
{
	struct ra_prefix_conf	*prefix;

	leave_all_routers_mcast_group(ra_iface);

	while ((prefix = SIMPLEQ_FIRST(&ra_iface->prefixes)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&ra_iface->prefixes, entry);
		free(prefix);
	}

	unref_icmp6ev(ra_iface);
	free(ra_iface);
}

/* from kame via ifconfig, where it's called prefix() */
int
in6_mask2prefixlen(struct in6_addr *in6)
{
	u_char *nam = (u_char *)in6;
	int byte, bit, plen = 0, size = sizeof(struct in6_addr);

	for (byte = 0; byte < size; byte++, plen += 8)
		if (nam[byte] != 0xff)
			break;
	if (byte == size)
		return (plen);
	for (bit = 7; bit != 0; bit--, plen++)
		if (!(nam[byte] & (1 << bit)))
			break;
	for (; bit != 0; bit--)
		if (nam[byte] & (1 << bit))
			return (0);
	byte++;
	for (; byte < size; byte++)
		if (nam[byte])
			return (0);
	return (plen);
}

void
get_interface_prefixes(struct ra_iface *ra_iface, struct ra_prefix_conf
    *autoprefix_conf, struct ifaddrs *ifap)
{
	struct in6_ifreq	 ifr6;
	struct ifaddrs		*ifa;
	struct sockaddr_in6	*sin6;
	uint32_t		 if_vltime, if_pltime;
	int			 prefixlen;

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		if (strcmp(ra_iface->name, ifa->ifa_name) != 0)
			continue;

		sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;

		if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
			continue;

		memset(&ifr6, 0, sizeof(ifr6));
		strlcpy(ifr6.ifr_name, ra_iface->name, sizeof(ifr6.ifr_name));
		memcpy(&ifr6.ifr_addr, sin6, sizeof(ifr6.ifr_addr));

		if_vltime = ND6_INFINITE_LIFETIME;
		if_pltime = ND6_INFINITE_LIFETIME;

		if (ioctl(ioctlsock, SIOCGIFALIFETIME_IN6,
		    (caddr_t)&ifr6) != -1) {
			struct in6_addrlifetime	*lifetime;

			lifetime = &ifr6.ifr_ifru.ifru_lifetime;
			if (lifetime->ia6t_preferred)
				if_pltime = lifetime->ia6t_preferred;
			if (lifetime->ia6t_expire)
				if_vltime = lifetime->ia6t_expire;
		}

		memset(&ifr6, 0, sizeof(ifr6));
		strlcpy(ifr6.ifr_name, ra_iface->name, sizeof(ifr6.ifr_name));
		memcpy(&ifr6.ifr_addr, sin6, sizeof(ifr6.ifr_addr));

		if (ioctl(ioctlsock, SIOCGIFNETMASK_IN6, (caddr_t)&ifr6) == -1)
			continue; /* addr got deleted while we were looking */

		prefixlen = in6_mask2prefixlen(&((struct sockaddr_in6 *)
		    &ifr6.ifr_addr)->sin6_addr);

		if (prefixlen == 128)
			continue;

		mask_prefix(&sin6->sin6_addr, prefixlen);

		add_new_prefix_to_ra_iface(ra_iface, &sin6->sin6_addr,
		    prefixlen, autoprefix_conf, 1, if_vltime, if_pltime);
	}
}

struct ra_prefix_conf*
find_ra_prefix_conf(struct ra_prefix_conf_head* head, struct in6_addr *prefix,
    int prefixlen)
{
	struct ra_prefix_conf	*ra_prefix_conf;

	SIMPLEQ_FOREACH(ra_prefix_conf, head, entry) {
		if (ra_prefix_conf->prefixlen == prefixlen &&
		    memcmp(&ra_prefix_conf->prefix, prefix, sizeof(*prefix)) ==
		    0)
			return (ra_prefix_conf);
	}
	return (NULL);
}

void
add_new_prefix_to_ra_iface(struct ra_iface *ra_iface, struct in6_addr *addr,
    int prefixlen, struct ra_prefix_conf *ra_prefix_conf, int autoconf,
    uint32_t if_vltime, uint32_t if_pltime)
{
	struct ra_prefix_conf	*new_ra_prefix_conf;

	if ((new_ra_prefix_conf = find_ra_prefix_conf(&ra_iface->prefixes, addr,
	    prefixlen)) != NULL) {
		int changed = new_ra_prefix_conf->autoconf != autoconf;

		new_ra_prefix_conf->autoconf = autoconf;
		new_ra_prefix_conf->if_vltime = if_vltime;
		new_ra_prefix_conf->if_pltime = if_pltime;

		if (changed) {
			struct ra_prefix_conf *pc;

			ra_iface->has_autoconf_prefix = 0;
			SIMPLEQ_FOREACH(pc, &ra_iface->prefixes, entry) {
				if (pc->autoconf) {
					ra_iface->has_autoconf_prefix = 1;
					break;
				}
			}
		}
		return;
	}

	log_debug("adding %s/%d prefix", in6_to_str(addr), prefixlen);

	if ((new_ra_prefix_conf = calloc(1, sizeof(*ra_prefix_conf))) == NULL)
		fatal("%s", __func__);
	new_ra_prefix_conf->prefix = *addr;
	new_ra_prefix_conf->prefixlen = prefixlen;
	new_ra_prefix_conf->vltime = ra_prefix_conf->vltime;
	new_ra_prefix_conf->pltime = ra_prefix_conf->pltime;
	new_ra_prefix_conf->autoconf = autoconf;
	if (autoconf)
		ra_iface->has_autoconf_prefix = 1;
	new_ra_prefix_conf->if_vltime = if_vltime;
	new_ra_prefix_conf->if_pltime = if_pltime;
	new_ra_prefix_conf->aflag = ra_prefix_conf->aflag;
	new_ra_prefix_conf->lflag = ra_prefix_conf->lflag;
	SIMPLEQ_INSERT_TAIL(&ra_iface->prefixes, new_ra_prefix_conf, entry);
	ra_iface->prefix_count++;
}

uint32_t
calc_autoconf_ltime(time_t t, uint32_t conf_ltime, uint32_t if_ltime)
{
	uint32_t ltime;

	if (if_ltime == ND6_INFINITE_LIFETIME)
		return conf_ltime;

	if (if_ltime > t)
		ltime = if_ltime - t;
	else
		ltime = 0;

	if (ltime < conf_ltime)
		return ltime;
	else
		return conf_ltime;
}

int
build_packet(struct ra_iface *ra_iface)
{
	struct nd_router_advert		*ra;
	struct nd_opt_source_link_addr	*ndopt_source_link_addr;
	struct nd_opt_mtu		*ndopt_mtu;
	struct nd_opt_prefix_info	*ndopt_pi;
	struct ra_iface_conf		*ra_iface_conf;
	struct ra_options_conf		*ra_options_conf;
	struct ra_prefix_conf		*ra_prefix_conf;
	struct nd_opt_rdnss		*ndopt_rdnss;
	struct nd_opt_dnssl		*ndopt_dnssl;
	struct nd_opt_pref64		*ndopt_pref64;
	struct ra_rdnss_conf		*ra_rdnss;
	struct ra_dnssl_conf		*ra_dnssl;
	struct ra_pref64_conf		*pref64;
	size_t				 len, label_len;
	time_t				 t;
	uint32_t			 vltime, pltime;
	uint8_t				*p, buf[RA_MAX_SIZE];
	char				*label_start, *label_end;

	ra_iface_conf = find_ra_iface_conf(&frontend_conf->ra_iface_list,
	    ra_iface->conf_name);
	ra_options_conf = &ra_iface_conf->ra_options;
	t = time(NULL);
	len = sizeof(*ra);
	if (ra_iface_conf->ra_options.source_link_addr)
		len += sizeof(*ndopt_source_link_addr);
	if (ra_options_conf->mtu > 0)
		len += sizeof(*ndopt_mtu);
	len += sizeof(*ndopt_pi) * ra_iface->prefix_count;
	if (ra_iface_conf->ra_options.rdnss_count > 0)
		len += sizeof(*ndopt_rdnss) +
		    ra_iface_conf->ra_options.rdnss_count *
		    sizeof(struct in6_addr);

	if (ra_iface_conf->ra_options.dnssl_len > 0)
		/* round up to 8 byte boundary */
		len += sizeof(*ndopt_dnssl) +
		    ((ra_iface_conf->ra_options.dnssl_len + 7) & ~7);

	SIMPLEQ_FOREACH(pref64, &ra_iface_conf->ra_options.ra_pref64_list,
	    entry)
		len += sizeof(struct nd_opt_pref64);

	if (len > sizeof(ra_iface->data))
		fatalx("%s: packet too big", __func__); /* XXX send multiple */

	p = buf;

	ra = (struct nd_router_advert *)p;

	memset(ra, 0, sizeof(*ra));

	ra->nd_ra_type = ND_ROUTER_ADVERT;
	ra->nd_ra_curhoplimit = ra_options_conf->cur_hl;
	if (ra_options_conf->m_flag)
		ra->nd_ra_flags_reserved |= ND_RA_FLAG_MANAGED;
	if (ra_options_conf->o_flag)
		ra->nd_ra_flags_reserved |= ND_RA_FLAG_OTHER;
	if (ra_iface->removed)
		/* tell clients that we are no longer a default router */
		ra->nd_ra_router_lifetime = 0;
	else if (ra_options_conf->dfr) {
		ra->nd_ra_router_lifetime =
		    htons(ra_options_conf->router_lifetime);
		/*
		 * RFC 4191
		 * If the Router Lifetime is zero, the preference value MUST be
		 * set to (00) by the sender and MUST be ignored by the
		 * receiver.
		 */
		if (ra_options_conf->router_lifetime > 0)
			ra->nd_ra_flags_reserved |= ra_options_conf->rtpref;
	}
	ra->nd_ra_reachable = htonl(ra_options_conf->reachable_time);
	ra->nd_ra_retransmit = htonl(ra_options_conf->retrans_timer);
	p += sizeof(*ra);

	if (ra_iface_conf->ra_options.source_link_addr) {
		ndopt_source_link_addr = (struct nd_opt_source_link_addr *)p;
		ndopt_source_link_addr->nd_opt_source_link_addr_type =
		    ND_OPT_SOURCE_LINKADDR;
		ndopt_source_link_addr->nd_opt_source_link_addr_len = 1;
		memcpy(&ndopt_source_link_addr->nd_opt_source_link_addr_hw_addr,
		    &ra_iface->hw_addr, ETHER_ADDR_LEN);
		p += sizeof(*ndopt_source_link_addr);
	}

	if (ra_options_conf->mtu > 0) {
		ndopt_mtu = (struct nd_opt_mtu *)p;
		ndopt_mtu->nd_opt_mtu_type = ND_OPT_MTU;
		ndopt_mtu->nd_opt_mtu_len = 1;
		ndopt_mtu->nd_opt_mtu_reserved = 0;
		ndopt_mtu->nd_opt_mtu_mtu = htonl(ra_options_conf->mtu);
		p += sizeof(*ndopt_mtu);
	}

	SIMPLEQ_FOREACH(ra_prefix_conf, &ra_iface->prefixes, entry) {
		ndopt_pi = (struct nd_opt_prefix_info *)p;
		memset(ndopt_pi, 0, sizeof(*ndopt_pi));
		ndopt_pi->nd_opt_pi_type = ND_OPT_PREFIX_INFORMATION;
		ndopt_pi->nd_opt_pi_len = 4;
		ndopt_pi->nd_opt_pi_prefix_len = ra_prefix_conf->prefixlen;
		if (ra_prefix_conf->lflag)
			ndopt_pi->nd_opt_pi_flags_reserved |=
			    ND_OPT_PI_FLAG_ONLINK;
		if (ra_prefix_conf->aflag)
			ndopt_pi->nd_opt_pi_flags_reserved |=
			    ND_OPT_PI_FLAG_AUTO;

		if (ra_prefix_conf->autoconf) {
			pltime = calc_autoconf_ltime(t,
			    ra_prefix_conf->pltime, ra_prefix_conf->if_pltime);
			vltime = calc_autoconf_ltime(t,
			    ra_prefix_conf->vltime, ra_prefix_conf->if_vltime);
		} else {
			pltime = ra_prefix_conf->pltime;
			vltime = ra_prefix_conf->vltime;
		}

		ndopt_pi->nd_opt_pi_valid_time = htonl(vltime);
		ndopt_pi->nd_opt_pi_preferred_time = htonl(pltime);
		ndopt_pi->nd_opt_pi_prefix = ra_prefix_conf->prefix;

		p += sizeof(*ndopt_pi);
	}

	if (ra_iface_conf->ra_options.rdnss_count > 0) {
		ndopt_rdnss = (struct nd_opt_rdnss *)p;
		ndopt_rdnss->nd_opt_rdnss_type = ND_OPT_RDNSS;
		ndopt_rdnss->nd_opt_rdnss_len = 1 +
		    ra_iface_conf->ra_options.rdnss_count * 2;
		ndopt_rdnss->nd_opt_rdnss_reserved = 0;
		ndopt_rdnss->nd_opt_rdnss_lifetime =
		    htonl(ra_iface_conf->ra_options.rdns_lifetime);
		p += sizeof(struct nd_opt_rdnss);
		SIMPLEQ_FOREACH(ra_rdnss,
		    &ra_iface_conf->ra_options.ra_rdnss_list, entry) {
			memcpy(p, &ra_rdnss->rdnss, sizeof(ra_rdnss->rdnss));
			p += sizeof(ra_rdnss->rdnss);
		}
	}

	if (ra_iface_conf->ra_options.dnssl_len > 0) {
		ndopt_dnssl = (struct nd_opt_dnssl *)p;
		ndopt_dnssl->nd_opt_dnssl_type = ND_OPT_DNSSL;
		/* round up to 8 byte boundary */
		ndopt_dnssl->nd_opt_dnssl_len = 1 +
		    ((ra_iface_conf->ra_options.dnssl_len + 7) & ~7) / 8;
		ndopt_dnssl->nd_opt_dnssl_reserved = 0;
		ndopt_dnssl->nd_opt_dnssl_lifetime =
		    htonl(ra_iface_conf->ra_options.rdns_lifetime);
		p += sizeof(struct nd_opt_dnssl);

		SIMPLEQ_FOREACH(ra_dnssl,
		    &ra_iface_conf->ra_options.ra_dnssl_list, entry) {
			label_start = ra_dnssl->search;
			while ((label_end = strchr(label_start, '.')) != NULL) {
				label_len = label_end - label_start;
				*p++ = label_len;
				memcpy(p, label_start, label_len);
				p += label_len;
				label_start = label_end + 1;
			}
			*p++ = '\0'; /* last dot */
		}
		/* zero pad */
		while (((uintptr_t)p) % 8 != 0)
			*p++ = '\0';
	}

	SIMPLEQ_FOREACH(pref64, &ra_iface_conf->ra_options.ra_pref64_list,
	    entry) {
		uint16_t	sltime_plc;

		/* scaled lifetime in units of 8 seconds */
		sltime_plc = pref64->ltime / 8;
		sltime_plc = sltime_plc << 3;
		/* encode prefix length in lower 3 bits */
		switch (pref64->prefixlen) {
		case 96:
			sltime_plc |= 0;
			break;
		case 64:
			sltime_plc |= 1;
			break;
		case 56:
			sltime_plc |= 2;
			break;
		case 48:
			sltime_plc |= 3;
			break;
		case 40:
			sltime_plc |= 4;
			break;
		case 32:
			sltime_plc |= 5;
			break;
		default:
			fatalx("%s: invalid pref64 length: %d", __func__,
			    pref64->prefixlen);
		}
		ndopt_pref64 = (struct nd_opt_pref64 *)p;
		ndopt_pref64->nd_opt_pref64_type = ND_OPT_PREF64;
		ndopt_pref64->nd_opt_pref64_len = 2;
		ndopt_pref64->nd_opt_pref64_sltime_plc = htons(sltime_plc);
		memcpy(ndopt_pref64->nd_opt_pref64, &pref64->prefix,
		    sizeof(ndopt_pref64->nd_opt_pref64));
		p += sizeof(struct nd_opt_pref64);
	}

	if (len != ra_iface->datalen || memcmp(buf, ra_iface->data, len)
	    != 0) {
		memcpy(ra_iface->data, buf, len);
		ra_iface->datalen = len;
		return 1;
	}
	return 0;
}

void
build_leaving_packet(struct ra_iface *ra_iface)
{
	struct nd_router_advert		 ra;

	memset(&ra, 0, sizeof(ra));

	ra.nd_ra_type = ND_ROUTER_ADVERT;

	memcpy(ra_iface->data, &ra, sizeof(ra));
	ra_iface->datalen = sizeof(ra);
}

void
ra_output(struct ra_iface *ra_iface, struct sockaddr_in6 *to)
{

	struct cmsghdr		*cm;
	struct in6_pktinfo	*pi;
	ssize_t			 len;
	int			 hoplimit = 255;

	if (!LINK_STATE_IS_UP(ra_iface->link_state))
		return;

	if (ra_iface->has_autoconf_prefix)
		/* update vltime & pltime */
		build_packet(ra_iface);

	sndmhdr.msg_name = to;
	sndmhdr.msg_iov[0].iov_base = ra_iface->data;
	sndmhdr.msg_iov[0].iov_len = ra_iface->datalen;

	cm = CMSG_FIRSTHDR(&sndmhdr);
	/* specify the outgoing interface */
	cm->cmsg_level = IPPROTO_IPV6;
	cm->cmsg_type = IPV6_PKTINFO;
	cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	pi = (struct in6_pktinfo *)CMSG_DATA(cm);
	memset(&pi->ipi6_addr, 0, sizeof(pi->ipi6_addr));
	pi->ipi6_ifindex = ra_iface->if_index;

	/* specify the hop limit of the packet */
	cm = CMSG_NXTHDR(&sndmhdr, cm);
	cm->cmsg_level = IPPROTO_IPV6;
	cm->cmsg_type = IPV6_HOPLIMIT;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &hoplimit, sizeof(int));

	log_debug("send RA on %s", ra_iface->name);

	len = sendmsg(EVENT_FD(&ra_iface->icmp6ev->ev), &sndmhdr, 0);
	if (len == -1)
		log_warn("sendmsg on %s", ra_iface->name);

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
route_receive(int fd, short events, void *arg)
{
	static uint8_t			 *buf;

	struct rt_msghdr		*rtm;
	struct sockaddr			*sa, *rti_info[RTAX_MAX];
	ssize_t				 n;

	if (buf == NULL) {
		buf = malloc(ROUTE_SOCKET_BUF_SIZE);
		if (buf == NULL)
			fatal("malloc");
	}
	rtm = (struct rt_msghdr *)buf;
	if ((n = read(fd, buf, ROUTE_SOCKET_BUF_SIZE)) == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		log_warn("dispatch_rtmsg: read error");
		return;
	}

	if (n == 0)
		fatal("routing socket closed");

	if (n < (ssize_t)sizeof(rtm->rtm_msglen) || n < rtm->rtm_msglen) {
		log_warnx("partial rtm of %zd in buffer", n);
		return;
	}

	if (rtm->rtm_version != RTM_VERSION)
		return;

	sa = (struct sockaddr *)(buf + rtm->rtm_hdrlen);
	get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

	handle_route_message(rtm, rti_info);
}

void
handle_route_message(struct rt_msghdr *rtm, struct sockaddr **rti_info)
{
	switch (rtm->rtm_type) {
	case RTM_IFINFO:
	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_CHGADDRATTR:
		/*
		 * do the same thing as after a config reload when interfaces
		 * change or IPv6 addresses show up / disappear
		 */
		merge_ra_interfaces();
		break;
	default:
		log_debug("unexpected RTM: %d", rtm->rtm_type);
		break;
	}
}

struct icmp6_ev*
get_icmp6ev_by_rdomain(int rdomain)
{
	struct ra_iface	*ra_iface;
	struct icmp6_ev	*icmp6ev = NULL;

	TAILQ_FOREACH (ra_iface, &ra_interfaces, entry) {
		if (ra_iface->rdomain == rdomain) {
			icmp6ev = ra_iface->icmp6ev;
			break;
		}
	}

	if (icmp6ev == NULL) {
		if ((icmp6ev = calloc(1, sizeof(*icmp6ev))) == NULL)
			fatal("calloc");

		icmp6ev->rcviov[0].iov_base = (caddr_t)icmp6ev->answer;
		icmp6ev->rcviov[0].iov_len = sizeof(icmp6ev->answer);
		icmp6ev->rcvmhdr.msg_name = (caddr_t)&icmp6ev->from;
		icmp6ev->rcvmhdr.msg_namelen = sizeof(icmp6ev->from);
		icmp6ev->rcvmhdr.msg_iov = icmp6ev->rcviov;
		icmp6ev->rcvmhdr.msg_iovlen = 1;
		icmp6ev->rcvmhdr.msg_controllen =
		    CMSG_SPACE(sizeof(struct in6_pktinfo)) +
		    CMSG_SPACE(sizeof(int));
		if ((icmp6ev->rcvmhdr.msg_control = malloc(icmp6ev->
		    rcvmhdr.msg_controllen)) == NULL)
			fatal("malloc");
		frontend_imsg_compose_main(IMSG_OPEN_ICMP6SOCK, 0,
		    &rdomain, sizeof(rdomain));
	}

	icmp6ev->refcnt++;
	return (icmp6ev);
}

void
unref_icmp6ev(struct ra_iface *ra_iface)
{
	struct icmp6_ev *icmp6ev = ra_iface->icmp6ev;

	ra_iface->icmp6ev = NULL;

	if (icmp6ev != NULL) {
		icmp6ev->refcnt--;
		if (icmp6ev->refcnt == 0) {
			event_del(&icmp6ev->ev);
			close(EVENT_FD(&icmp6ev->ev));
			free(icmp6ev);
		}
	}
}

void
set_icmp6sock(int icmp6sock, int rdomain)
{
	struct ra_iface	*ra_iface;

	TAILQ_FOREACH (ra_iface, &ra_interfaces, entry) {
		if (!event_initialized(&ra_iface->icmp6ev->ev) &&
		    ra_iface->rdomain == rdomain) {
			event_set(&ra_iface->icmp6ev->ev, icmp6sock, EV_READ |
			    EV_PERSIST, icmp6_receive, ra_iface->icmp6ev);
			event_add(&ra_iface->icmp6ev->ev, NULL);
			icmp6sock = -1;
			break;
		}
	}

	if (icmp6sock != -1) {
		/*
		 * The interface disappeared or changed rdomain while we were
		 * waiting for the parent process to open the raw socket.
		 */
		close(icmp6sock);
		return;
	}

	TAILQ_FOREACH (ra_iface, &ra_interfaces, entry) {
		if (ra_iface->rdomain == rdomain) {
			join_all_routers_mcast_group(ra_iface);
			frontend_imsg_compose_engine(IMSG_UPDATE_IF, 0,
			    &ra_iface->if_index, sizeof(ra_iface->if_index));
		}
	}
}
