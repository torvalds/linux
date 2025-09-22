/*	$OpenBSD: frontend.c,v 1.74 2024/11/21 13:35:20 claudio Exp $	*/

/*
 * Copyright (c) 2017 Florian Obser <florian@openbsd.org>
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
#include "slaacd.h"
#include "frontend.h"
#include "control.h"

#define	ROUTE_SOCKET_BUF_SIZE	16384
#define	ALLROUTER		"ff02::2"

struct icmp6_ev {
	struct event		 ev;
	uint8_t			 answer[1500];
	struct msghdr		 rcvmhdr;
	struct iovec		 rcviov[1];
	struct sockaddr_in6	 from;
	int			 refcnt;
};

struct iface {
	LIST_ENTRY(iface)	 entries;
	struct icmp6_ev		*icmp6ev;
	struct ether_addr	 hw_address;
	uint32_t		 if_index;
	int			 rdomain;
	int			 send_solicitation;
	int			 ll_tentative;
};

__dead void	 frontend_shutdown(void);
void		 frontend_sig_handler(int, short, void *);
void		 update_iface(uint32_t, char*);
void		 frontend_startup(void);
void		 route_receive(int, short, void *);
void		 handle_route_message(struct rt_msghdr *, struct sockaddr **);
void		 get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
void		 icmp6_receive(int, short, void *);
int		 get_flags(char *);
int		 get_xflags(char *);
int		 get_ifrdomain(char *);
struct iface	*get_iface_by_id(uint32_t);
void		 remove_iface(uint32_t);
struct icmp6_ev	*get_icmp6ev_by_rdomain(int);
void		 unref_icmp6ev(struct iface *);
void		 set_icmp6sock(int, int);
void		 send_solicitation(uint32_t);
#ifndef	SMALL
const char	*flags_to_str(int);
#endif	/* SMALL */

LIST_HEAD(, iface)		 interfaces;
static struct imsgev		*iev_main;
static struct imsgev		*iev_engine;
struct event			 ev_route;
struct msghdr			 sndmhdr;
struct iovec			 sndiov[4];
struct nd_router_solicit	 rs;
struct nd_opt_hdr		 nd_opt_hdr;
struct ether_addr		 nd_opt_source_link_addr;
struct sockaddr_in6		 dst;
int				 ioctlsock;

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
	struct in6_pktinfo	*pi;
	struct cmsghdr		*cm;
	size_t			 sndcmsglen;
	int			 hoplimit = 255;
	uint8_t			*sndcmsgbuf;

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if ((pw = getpwnam(SLAACD_USER)) == NULL)
		fatal("getpwnam");

	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	if (unveil("/", "") == -1)
		fatal("unveil /");
	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

	setproctitle("%s", "frontend");
	log_procinit("frontend");

	if ((ioctlsock = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0)) == -1)
		fatal("socket");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio unix recvfd route", NULL) == -1)
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

	sndcmsglen = CMSG_SPACE(sizeof(struct in6_pktinfo)) +
	    CMSG_SPACE(sizeof(int));

	if ((sndcmsgbuf = malloc(sndcmsglen)) == NULL)
		fatal("malloc");

	rs.nd_rs_type = ND_ROUTER_SOLICIT;
	rs.nd_rs_code = 0;
	rs.nd_rs_cksum = 0;
	rs.nd_rs_reserved = 0;

	nd_opt_hdr.nd_opt_type = ND_OPT_SOURCE_LINKADDR;
	nd_opt_hdr.nd_opt_len = 1;

	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	if (inet_pton(AF_INET6, ALLROUTER, &dst.sin6_addr.s6_addr) != 1)
		fatal("inet_pton");

	sndmhdr.msg_namelen = sizeof(struct sockaddr_in6);
	sndmhdr.msg_iov = sndiov;
	sndmhdr.msg_iovlen = 3;
	sndmhdr.msg_control = (caddr_t)sndcmsgbuf;
	sndmhdr.msg_controllen = sndcmsglen;

	sndmhdr.msg_name = (caddr_t)&dst;
	sndmhdr.msg_iov[0].iov_base = (caddr_t)&rs;
	sndmhdr.msg_iov[0].iov_len = sizeof(rs);
	sndmhdr.msg_iov[1].iov_base = (caddr_t)&nd_opt_hdr;
	sndmhdr.msg_iov[1].iov_len = sizeof(nd_opt_hdr);
	sndmhdr.msg_iov[2].iov_base = (caddr_t)&nd_opt_source_link_addr;
	sndmhdr.msg_iov[2].iov_len = sizeof(nd_opt_source_link_addr);

	cm = CMSG_FIRSTHDR(&sndmhdr);

	cm->cmsg_level = IPPROTO_IPV6;
	cm->cmsg_type = IPV6_PKTINFO;
	cm->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	pi = (struct in6_pktinfo *)CMSG_DATA(cm);
	memset(&pi->ipi6_addr, 0, sizeof(pi->ipi6_addr));
	pi->ipi6_ifindex = 0;

	cm = CMSG_NXTHDR(&sndmhdr, cm);
	cm->cmsg_level = IPPROTO_IPV6;
	cm->cmsg_type = IPV6_HOPLIMIT;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &hoplimit, sizeof(int));

	LIST_INIT(&interfaces);

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

	free(iev_engine);
	free(iev_main);

	log_info("frontend exiting");
	exit(0);
}

int
frontend_imsg_compose_main(int type, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data,
	    datalen));
}

int
frontend_imsg_compose_engine(int type, uint32_t peerid, pid_t pid,
    void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_engine, type, peerid, pid, -1,
	    data, datalen));
}

void
frontend_dispatch_main(int fd, short event, void *bula)
{
	struct imsg		 imsg;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	ssize_t			 n;
	uint32_t		 type;
	int			 shut = 0, icmp6sock, rdomain;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* Connection closed. */
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

		type = imsg_get_type(&imsg);

		switch (type) {
		case IMSG_SOCKET_IPC:
			/*
			 * Setup pipe and event handler to the engine
			 * process.
			 */
			if (iev_engine)
				fatalx("%s: received unexpected imsg fd "
				    "to frontend", __func__);

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
		case IMSG_ICMP6SOCK:
			if ((icmp6sock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "ICMPv6 fd but didn't receive any",
				    __func__);
			if (imsg_get_data(&imsg, &rdomain,
			    sizeof(rdomain)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			set_icmp6sock(icmp6sock, rdomain);
			break;
		case IMSG_ROUTESOCK:
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "routesocket fd but didn't receive any",
				    __func__);

			event_set(&ev_route, fd, EV_READ | EV_PERSIST,
			    route_receive, NULL);
			break;
		case IMSG_STARTUP:
			frontend_startup();
			break;
#ifndef	SMALL
		case IMSG_CONTROLFD:
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "control fd but didn't receive any",
				    __func__);

			/* Listen on control socket. */
			control_listen(fd);
			break;
		case IMSG_CTL_END:
			control_imsg_relay(&imsg);
			break;
#endif	/* SMALL */
		default:
			log_debug("%s: error handling imsg %d", __func__, type);
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
	ssize_t			 n;
	int			 shut = 0;
	uint32_t		 if_index, type;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* Connection closed. */
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

		type = imsg_get_type(&imsg);

		switch (type) {
#ifndef	SMALL
		case IMSG_CTL_END:
		case IMSG_CTL_SHOW_INTERFACE_INFO:
		case IMSG_CTL_SHOW_INTERFACE_INFO_RA:
		case IMSG_CTL_SHOW_INTERFACE_INFO_RA_PREFIX:
		case IMSG_CTL_SHOW_INTERFACE_INFO_RA_RDNS:
		case IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSALS:
		case IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSAL:
		case IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSALS:
		case IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSAL:
		case IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSALS:
		case IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSAL:
			control_imsg_relay(&imsg);
			break;
#endif	/* SMALL */
		case IMSG_CTL_SEND_SOLICITATION:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			send_solicitation(if_index);
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__, type);
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

int
get_flags(char *if_name)
{
	struct ifreq		 ifr;

	strlcpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));
	if (ioctl(ioctlsock, SIOCGIFFLAGS, (caddr_t)&ifr) == -1) {
		log_warn("SIOCGIFFLAGS");
		return -1;
	}
	return ifr.ifr_flags;
}

int
get_xflags(char *if_name)
{
	struct ifreq		 ifr;

	strlcpy(ifr.ifr_name, if_name, sizeof(ifr.ifr_name));
	if (ioctl(ioctlsock, SIOCGIFXFLAGS, (caddr_t)&ifr) == -1) {
		log_warn("SIOCGIFXFLAGS");
		return -1;
	}
	return ifr.ifr_flags;
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
update_iface(uint32_t if_index, char* if_name)
{
	struct iface		*iface;
	struct ifaddrs		*ifap, *ifa;
	struct imsg_ifinfo	 imsg_ifinfo;
	struct sockaddr_dl	*sdl;
	struct sockaddr_in6	*sin6;
	struct in6_ifreq	 ifr6;
	int			 flags, xflags, ifrdomain;

	if ((flags = get_flags(if_name)) == -1 || (xflags =
	    get_xflags(if_name)) == -1)
		return;

	if (!(xflags & (IFXF_AUTOCONF6 | IFXF_AUTOCONF6TEMP)))
		return;

	if ((ifrdomain = get_ifrdomain(if_name)) == -1)
		return;

	iface = get_iface_by_id(if_index);

	if (iface != NULL) {
		if (iface->rdomain != ifrdomain) {
			unref_icmp6ev(iface);
			iface->rdomain = ifrdomain;
			iface->icmp6ev = get_icmp6ev_by_rdomain(ifrdomain);
		}
	} else {
		if ((iface = calloc(1, sizeof(*iface))) == NULL)
			fatal("calloc");
		iface->if_index = if_index;
		iface->rdomain = ifrdomain;
		iface->icmp6ev = get_icmp6ev_by_rdomain(ifrdomain);
		iface->ll_tentative = 1;

		LIST_INSERT_HEAD(&interfaces, iface, entries);
	}

	memset(&imsg_ifinfo, 0, sizeof(imsg_ifinfo));

	imsg_ifinfo.if_index = if_index;
	imsg_ifinfo.rdomain = ifrdomain;
	imsg_ifinfo.running = (flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP |
	    IFF_RUNNING);
	imsg_ifinfo.autoconf = (xflags & IFXF_AUTOCONF6);
	imsg_ifinfo.temporary = (xflags & IFXF_AUTOCONF6TEMP);
	imsg_ifinfo.soii = !(xflags & IFXF_INET6_NOSOII);

	if (getifaddrs(&ifap) != 0)
		fatal("getifaddrs");

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(if_name, ifa->ifa_name) != 0)
			continue;
		if (ifa->ifa_addr == NULL)
			continue;

		switch(ifa->ifa_addr->sa_family) {
		case AF_LINK:
			imsg_ifinfo.link_state =
			    ((struct if_data *)ifa->ifa_data)->ifi_link_state;
			sdl = (struct sockaddr_dl *)ifa->ifa_addr;
			if (sdl->sdl_type != IFT_ETHER ||
			    sdl->sdl_alen != ETHER_ADDR_LEN)
				continue;
			memcpy(iface->hw_address.ether_addr_octet,
			    LLADDR(sdl), ETHER_ADDR_LEN);
			memcpy(imsg_ifinfo.hw_address.ether_addr_octet,
			    LLADDR(sdl), ETHER_ADDR_LEN);
		case AF_INET6:
			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
#ifdef __KAME__
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) &&
			    sin6->sin6_scope_id == 0) {
				sin6->sin6_scope_id = ntohs(*(u_int16_t *)
				    &sin6->sin6_addr.s6_addr[2]);
				sin6->sin6_addr.s6_addr[2] =
				    sin6->sin6_addr.s6_addr[3] = 0;
			}
#endif
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				memcpy(&imsg_ifinfo.ll_address, sin6,
				    sizeof(imsg_ifinfo.ll_address));

				if (!iface->ll_tentative)
					break;

				memset(&ifr6, 0, sizeof(ifr6));
				strlcpy(ifr6.ifr_name, if_name,
				    sizeof(ifr6.ifr_name));
				memcpy(&ifr6.ifr_addr, sin6,
				    sizeof(ifr6.ifr_addr));

				if (ioctl(ioctlsock, SIOCGIFAFLAG_IN6,
				    (caddr_t)&ifr6) == -1) {
					log_warn("SIOCGIFAFLAG_IN6");
					break;
				}

				if (!(ifr6.ifr_ifru.ifru_flags6 &
				    IN6_IFF_TENTATIVE)) {
					iface->ll_tentative = 0;
					if (iface->send_solicitation)
						send_solicitation(
						    iface->if_index);
				}
			}
			break;
		default:
			break;
		}
	}

	freeifaddrs(ifap);

	frontend_imsg_compose_main(IMSG_UPDATE_IF, 0, &imsg_ifinfo,
	    sizeof(imsg_ifinfo));
}

#ifndef	SMALL
const char*
flags_to_str(int flags)
{
	static char	buf[sizeof(" anycast tentative duplicated detached "
			    "deprecated autoconf temporary")];

	buf[0] = '\0';
	if (flags & IN6_IFF_ANYCAST)
		strlcat(buf, " anycast", sizeof(buf));
	if (flags & IN6_IFF_TENTATIVE)
		strlcat(buf, " tentative", sizeof(buf));
	if (flags & IN6_IFF_DUPLICATED)
		strlcat(buf, " duplicated", sizeof(buf));
	if (flags & IN6_IFF_DETACHED)
		strlcat(buf, " detached", sizeof(buf));
	if (flags & IN6_IFF_DEPRECATED)
		strlcat(buf, " deprecated", sizeof(buf));
	if (flags & IN6_IFF_AUTOCONF)
		strlcat(buf, " autoconf", sizeof(buf));
	if (flags & IN6_IFF_TEMPORARY)
		strlcat(buf, " temporary", sizeof(buf));

	return (buf);
}
#endif	/* SMALL */

void
frontend_startup(void)
{
	struct if_nameindex	*ifnidxp, *ifnidx;

	if (!event_initialized(&ev_route))
		fatalx("%s: did not receive a route socket from the main "
		    "process", __func__);

	event_add(&ev_route, NULL);

	if ((ifnidxp = if_nameindex()) == NULL)
		fatalx("if_nameindex");

	for(ifnidx = ifnidxp; ifnidx->if_index !=0 && ifnidx->if_name != NULL;
	    ifnidx++)
		update_iface(ifnidx->if_index, ifnidx->if_name);

	if_freenameindex(ifnidxp);
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
	struct if_msghdr		*ifm;
	struct if_announcemsghdr	*ifan;
	struct imsg_del_addr		 del_addr;
	struct imsg_del_route		 del_route;
	struct imsg_dup_addr		 dup_addr;
	struct sockaddr_rtlabel		*rl;
	struct sockaddr_in6		*sin6;
	struct in6_ifreq		 ifr6;
	struct in6_addr			*in6;
	int				 xflags, if_index;
	char				 ifnamebuf[IFNAMSIZ];
	char				*if_name;

	switch (rtm->rtm_type) {
	case RTM_IFINFO:
		ifm = (struct if_msghdr *)rtm;
		if_index = ifm->ifm_index;
		if_name = if_indextoname(if_index, ifnamebuf);
		if (if_name == NULL) {
			log_debug("RTM_IFINFO: lost if %d", if_index);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0, 0,
			    &if_index, sizeof(if_index));
			remove_iface(if_index);
			break;
		}
		xflags = get_xflags(if_name);
		if (xflags == -1 || !(xflags & (IFXF_AUTOCONF6 |
		    IFXF_AUTOCONF6TEMP))) {
			log_debug("RTM_IFINFO: %s(%d) no(longer) autoconf6",
			    if_name, if_index);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0,
			    0, &if_index, sizeof(if_index));
			remove_iface(if_index);
		} else {
			update_iface(if_index, if_name);
		}
		break;
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *)rtm;
		if_index = ifan->ifan_index;
                if (ifan->ifan_what == IFAN_DEPARTURE) {
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0, 0,
			    &if_index, sizeof(if_index));
			remove_iface(if_index);
		}
		break;
	case RTM_NEWADDR:
		ifm = (struct if_msghdr *)rtm;
		if_index = ifm->ifm_index;
		if_name = if_indextoname(if_index, ifnamebuf);
		if (if_name == NULL) {
			log_debug("RTM_NEWADDR: lost if %d", if_index);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0, 0,
			    &if_index, sizeof(if_index));
			remove_iface(if_index);
			break;
		}

		log_debug("RTM_NEWADDR: %s[%u]", if_name, if_index);
		update_iface(if_index, if_name);
		break;
	case RTM_DELADDR:
		ifm = (struct if_msghdr *)rtm;
		if_index = ifm->ifm_index;
		if_name = if_indextoname(if_index, ifnamebuf);
		if (if_name == NULL) {
			log_debug("RTM_DELADDR: lost if %d", if_index);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0, 0,
			    &if_index, sizeof(if_index));
			remove_iface(if_index);
			break;
		}
		if (rtm->rtm_addrs & RTA_IFA && rti_info[RTAX_IFA]->sa_family
		    == AF_INET6) {
			del_addr.if_index = if_index;
			memcpy(&del_addr.addr, rti_info[RTAX_IFA], sizeof(
			    del_addr.addr));
			frontend_imsg_compose_engine(IMSG_DEL_ADDRESS,
				    0, 0, &del_addr, sizeof(del_addr));
			log_debug("RTM_DELADDR: %s[%u]", if_name, if_index);
		}
		break;
	case RTM_CHGADDRATTR:
		ifm = (struct if_msghdr *)rtm;
		if_index = ifm->ifm_index;
		if_name = if_indextoname(if_index, ifnamebuf);
		if (if_name == NULL) {
			log_debug("RTM_CHGADDRATTR: lost if %d", if_index);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0, 0,
			    &if_index, sizeof(if_index));
			remove_iface(if_index);
			break;
		}
		if (rtm->rtm_addrs & RTA_IFA && rti_info[RTAX_IFA]->sa_family
		    == AF_INET6) {
			sin6 = (struct sockaddr_in6 *) rti_info[RTAX_IFA];

			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				update_iface(if_index, if_name);
				break;
			}

			memset(&ifr6, 0, sizeof(ifr6));
			strlcpy(ifr6.ifr_name, if_name, sizeof(ifr6.ifr_name));
			memcpy(&ifr6.ifr_addr, sin6, sizeof(ifr6.ifr_addr));

			if (ioctl(ioctlsock, SIOCGIFAFLAG_IN6, (caddr_t)&ifr6)
			    == -1) {
				log_warn("SIOCGIFAFLAG_IN6");
				break;
			}

#ifndef	SMALL
			log_debug("RTM_CHGADDRATTR: %s - %s",
			    sin6_to_str(sin6),
			    flags_to_str(ifr6.ifr_ifru.ifru_flags6));
#endif	/* SMALL */

			if (ifr6.ifr_ifru.ifru_flags6 & IN6_IFF_DUPLICATED) {
				dup_addr.if_index = if_index;
				dup_addr.addr = *sin6;
				frontend_imsg_compose_engine(IMSG_DUP_ADDRESS,
				    0, 0, &dup_addr, sizeof(dup_addr));
			}

		}
		break;
	case RTM_DELETE:
		ifm = (struct if_msghdr *)rtm;
		if ((rtm->rtm_addrs & (RTA_DST | RTA_GATEWAY | RTA_LABEL)) !=
		    (RTA_DST | RTA_GATEWAY | RTA_LABEL))
			break;
		if (rti_info[RTAX_DST]->sa_family != AF_INET6)
			break;
		if (!IN6_IS_ADDR_UNSPECIFIED(&((struct sockaddr_in6 *)
		    rti_info[RTAX_DST])->sin6_addr))
			break;
		if (rti_info[RTAX_GATEWAY]->sa_family != AF_INET6)
			break;
		if (rti_info[RTAX_LABEL]->sa_len !=
		    sizeof(struct sockaddr_rtlabel))
			break;

		rl = (struct sockaddr_rtlabel *)rti_info[RTAX_LABEL];
		if (strcmp(rl->sr_label, SLAACD_RTA_LABEL) != 0)
			break;
		if_index = ifm->ifm_index;
		if_name = if_indextoname(if_index, ifnamebuf);
		if (if_name == NULL) {
			log_debug("RTM_DELETE: lost if %d", if_index);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0, 0,
			    &if_index, sizeof(if_index));
			remove_iface(if_index);
			break;
		}

		del_route.if_index = if_index;
		memcpy(&del_route.gw, rti_info[RTAX_GATEWAY],
		    sizeof(del_route.gw));
		in6 = &del_route.gw.sin6_addr;
#ifdef __KAME__
		/* XXX from route(8) p_sockaddr() */
		if ((IN6_IS_ADDR_LINKLOCAL(in6) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(in6) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(in6)) &&
		    del_route.gw.sin6_scope_id == 0) {
			del_route.gw.sin6_scope_id =
			    (u_int32_t)ntohs(*(u_short *) &in6->s6_addr[2]);
			*(u_short *)&in6->s6_addr[2] = 0;
		}
#endif
		frontend_imsg_compose_engine(IMSG_DEL_ROUTE,
		    0, 0, &del_route, sizeof(del_route));
		log_debug("RTM_DELETE: %s[%u]", if_name,
		    ifm->ifm_index);

		break;
#ifndef	SMALL
	case RTM_PROPOSAL:
		if (rtm->rtm_priority == RTP_PROPOSAL_SOLICIT) {
			log_debug("RTP_PROPOSAL_SOLICIT");
			frontend_imsg_compose_engine(IMSG_REPROPOSE_RDNS,
			    0, 0, NULL, 0);
		}
		break;
#endif	/* SMALL */
	default:
		log_debug("unexpected RTM: %d", rtm->rtm_type);
		break;
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
icmp6_receive(int fd, short events, void *arg)
{
	struct imsg_ra		 ra;
	struct icmp6_hdr	*icmp6_hdr;
	struct icmp6_ev		*icmp6ev;
	struct in6_pktinfo	*pi = NULL;
	struct cmsghdr		*cm;
	ssize_t			 len;
	int			 if_index = 0, *hlimp = NULL;
	char			 ntopbuf[INET6_ADDRSTRLEN];
#ifndef SMALL
	char			 ifnamebuf[IFNAMSIZ];
#endif	/* SMALL */

	icmp6ev = arg;
	if ((len = recvmsg(fd, &icmp6ev->rcvmhdr, 0)) == -1) {
		log_warn("recvmsg");
		return;
	}

	if ((size_t)len < sizeof(struct icmp6_hdr))
		return;

	icmp6_hdr = (struct icmp6_hdr *)icmp6ev->answer;
	if (icmp6_hdr->icmp6_type != ND_ROUTER_ADVERT)
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
		log_warnx("invalid RA with hop limit of %d from %s on %s",
		    *hlimp, inet_ntop(AF_INET6, &icmp6ev->from.sin6_addr,
		    ntopbuf, INET6_ADDRSTRLEN), if_indextoname(if_index,
		    ifnamebuf));
		return;
	}

	if ((size_t)len > sizeof(ra.packet)) {
		log_warnx("invalid RA with size %ld from %s on %s",
		    len, inet_ntop(AF_INET6, &icmp6ev->from.sin6_addr,
		    ntopbuf, INET6_ADDRSTRLEN), if_indextoname(if_index,
		    ifnamebuf));
		return;
	}
	ra.if_index = if_index;
	memcpy(&ra.from,  &icmp6ev->from, sizeof(ra.from));
	ra.len = len;
	memcpy(ra.packet, icmp6ev->answer, len);

	frontend_imsg_compose_engine(IMSG_RA, 0, 0, &ra, sizeof(ra));
}

void
send_solicitation(uint32_t if_index)
{
	struct in6_pktinfo	*pi;
	struct cmsghdr		*cm;
	struct iface		*iface;

	log_debug("%s(%u)", __func__, if_index);

	if ((iface = get_iface_by_id(if_index)) == NULL)
		return;

	if (!event_initialized(&iface->icmp6ev->ev)) {
		iface->send_solicitation = 1;
		return;
	} else if (iface->ll_tentative) {
		iface->send_solicitation = 1;
		return;
	}

	iface->send_solicitation = 0;

	dst.sin6_scope_id = if_index;

	cm = CMSG_FIRSTHDR(&sndmhdr);
	pi = (struct in6_pktinfo *)CMSG_DATA(cm);
	pi->ipi6_ifindex = if_index;

	memcpy(&nd_opt_source_link_addr, &iface->hw_address,
	    sizeof(nd_opt_source_link_addr));

	if (sendmsg(EVENT_FD(&iface->icmp6ev->ev), &sndmhdr, 0) != sizeof(rs) +
	    sizeof(nd_opt_hdr) + sizeof(nd_opt_source_link_addr))
		log_warn("sendmsg");
}

struct iface*
get_iface_by_id(uint32_t if_index)
{
	struct iface	*iface;

	LIST_FOREACH (iface, &interfaces, entries) {
		if (iface->if_index == if_index)
			return (iface);
	}

	return (NULL);
}

void
remove_iface(uint32_t if_index)
{
	struct iface	*iface;

	iface = get_iface_by_id(if_index);

	if (iface == NULL)
		return;

	LIST_REMOVE(iface, entries);

	unref_icmp6ev(iface);
	free(iface);
}

struct icmp6_ev*
get_icmp6ev_by_rdomain(int rdomain)
{
	struct iface	*iface;
	struct icmp6_ev	*icmp6ev = NULL;

	LIST_FOREACH (iface, &interfaces, entries) {
		if (iface->rdomain == rdomain) {
			icmp6ev = iface->icmp6ev;
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
unref_icmp6ev(struct iface *iface)
{
	struct icmp6_ev *icmp6ev = iface->icmp6ev;

	iface->icmp6ev = NULL;

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
	struct iface	*iface;

	LIST_FOREACH (iface, &interfaces, entries) {
		if (!event_initialized(&iface->icmp6ev->ev) && iface->rdomain
		    == rdomain) {
			event_set(&iface->icmp6ev->ev, icmp6sock, EV_READ |
			    EV_PERSIST, icmp6_receive, iface->icmp6ev);
			event_add(&iface->icmp6ev->ev, NULL);
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

	LIST_FOREACH (iface, &interfaces, entries) {
		if (event_initialized(&iface->icmp6ev->ev) &&
		    iface->send_solicitation)
			send_solicitation(iface->if_index);
	}
}
