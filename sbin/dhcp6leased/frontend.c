/*	$OpenBSD: frontend.c,v 1.22 2025/06/19 10:28:41 jmatthew Exp $	*/

/*
 * Copyright (c) 2017, 2021, 2024 Florian Obser <florian@openbsd.org>
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
#include <sys/utsname.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include <arpa/inet.h>

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
#include "dhcp6leased.h"
#include "frontend.h"
#include "control.h"

#define	ALL_DHCP_RELAY_AGENTS_AND_SERVERS	"ff02::1:2"
#define	ROUTE_SOCKET_BUF_SIZE			16384

struct iface {
	LIST_ENTRY(iface)	 entries;
	struct event		 udpev;
	struct imsg_ifinfo	 ifinfo;
	int			 send_solicit;
	int			 elapsed_time;
	uint8_t			 xid[XID_SIZE];
	int			 serverid_len;
	uint8_t			 serverid[SERVERID_SIZE];
	struct prefix		 pds[MAX_IA];
};

__dead void	 frontend_shutdown(void);
void		 frontend_sig_handler(int, short, void *);
void		 frontend_startup(void);
void		 update_iface(uint32_t);
void		 route_receive(int, short, void *);
void		 handle_route_message(struct rt_msghdr *, struct sockaddr **);
void		 get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
void		 udp_receive(int, short, void *);
int		 get_flags(char *);
struct iface	*get_iface_by_id(uint32_t);
struct iface	*get_iface_by_name(const char *);
void		 remove_iface(uint32_t);
void		 set_udpsock(int, uint32_t);
void		 iface_data_from_imsg(struct iface*, struct imsg_req_dhcp *);
ssize_t		 build_packet(uint8_t, struct iface *, char *);
void		 send_packet(uint8_t, struct iface *);
int		 iface_conf_cmp(struct iface_conf *, struct iface_conf *);

LIST_HEAD(, iface)		 interfaces;
struct dhcp6leased_conf		*frontend_conf;
static struct imsgev		*iev_main;
static struct imsgev		*iev_engine;
struct event			 ev_route;
struct sockaddr_in6		 dst;
int				 ioctlsock;

uint8_t				 dhcp_packet[1500];
static struct dhcp_duid		 duid;
char				*vendor_class_data;
int				 vendor_class_len;

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
	struct utsname		 utsname;

	frontend_conf = config_new_empty();

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if ((pw = getpwnam(DHCP6LEASED_USER)) == NULL)
		fatal("getpwnam");

	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	if (unveil("/", "") == -1)
		fatal("unveil /");
	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

	setproctitle("%s", "frontend");
	log_procinit("frontend");

	if ((ioctlsock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) == -1)
		fatal("socket");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio unix recvfd route", NULL) == -1)
		fatal("pledge");

	if (uname(&utsname) == -1)
		fatal("uname");
	vendor_class_len = asprintf(&vendor_class_data, "%s %s %s",
	    utsname.sysname, utsname.release, utsname.machine);
	if (vendor_class_len == -1)
		fatal("Cannot generate vendor-class-data");

	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	if (inet_pton(AF_INET6, ALL_DHCP_RELAY_AGENTS_AND_SERVERS,
	    &dst.sin6_addr.s6_addr) != 1)
		fatal("inet_pton");

	dst.sin6_port = ntohs(SERVER_PORT);

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

	config_clear(frontend_conf);

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
	static struct dhcp6leased_conf	*nconf;
	static struct iface_conf	*iface_conf;
	static struct iface_ia_conf	*iface_ia_conf;
	struct iface_pd_conf		*iface_pd_conf;
	struct imsg			 imsg;
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	ssize_t				 n;
	int				 shut = 0, udpsock, if_index;

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

		switch (imsg.hdr.type) {
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
		case IMSG_UDPSOCK:
			if ((udpsock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "udp fd but didn't receive any",
				    __func__);
			if (IMSG_DATA_SIZE(imsg) != sizeof(if_index))
				fatalx("%s: IMSG_UDPSOCK wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&if_index, imsg.data, sizeof(if_index));
			set_udpsock(udpsock, if_index);
			break;
		case IMSG_ROUTESOCK:
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "routesocket fd but didn't receive any",
				    __func__);
			event_set(&ev_route, fd, EV_READ | EV_PERSIST,
			    route_receive, NULL);
			break;
		case IMSG_UUID:
			if (IMSG_DATA_SIZE(imsg) != sizeof(duid.uuid))
				fatalx("%s: IMSG_UUID wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			duid.type = htons(DUID_UUID_TYPE);
			memcpy(duid.uuid, imsg.data, sizeof(duid.uuid));
			break;
		case IMSG_STARTUP:
			frontend_startup();
			break;
		case IMSG_RECONF_CONF:
			if (nconf != NULL)
				fatalx("%s: IMSG_RECONF_CONF already in "
				    "progress", __func__);
			if (IMSG_DATA_SIZE(imsg) !=
			    sizeof(struct dhcp6leased_conf))
				fatalx("%s: IMSG_RECONF_CONF wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			if ((nconf = malloc(sizeof(struct dhcp6leased_conf))) ==
			    NULL)
				fatal(NULL);
			memcpy(nconf, imsg.data,
			    sizeof(struct dhcp6leased_conf));
			SIMPLEQ_INIT(&nconf->iface_list);
			break;
		case IMSG_RECONF_IFACE:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    iface_conf))
				fatalx("%s: IMSG_RECONF_IFACE wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			if ((iface_conf = malloc(sizeof(struct iface_conf)))
			    == NULL)
				fatal(NULL);
			memcpy(iface_conf, imsg.data, sizeof(struct
			    iface_conf));
			if (iface_conf->name[sizeof(iface_conf->name) - 1]
			    != '\0')
				fatalx("%s: IMSG_RECONF_IFACE invalid name",
				    __func__);

			SIMPLEQ_INIT(&iface_conf->iface_ia_list);
			SIMPLEQ_INSERT_TAIL(&nconf->iface_list,
			    iface_conf, entry);
			iface_conf->ia_count = 0;
			break;
		case IMSG_RECONF_IFACE_IA:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    iface_ia_conf))
				fatalx("%s: IMSG_RECONF_IFACE_IA wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			if ((iface_ia_conf =
			    malloc(sizeof(struct iface_ia_conf))) == NULL)
				fatal(NULL);
			memcpy(iface_ia_conf, imsg.data, sizeof(struct
			    iface_ia_conf));
			SIMPLEQ_INIT(&iface_ia_conf->iface_pd_list);
			SIMPLEQ_INSERT_TAIL(&iface_conf->iface_ia_list,
			    iface_ia_conf, entry);
			iface_ia_conf->id = iface_conf->ia_count++;
			if (iface_conf->ia_count > MAX_IA)
				fatalx("Too many prefix delegation requests.");
			break;
		case IMSG_RECONF_IFACE_PD:
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct
			    iface_pd_conf))
				fatalx("%s: IMSG_RECONF_IFACE_PD wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			if ((iface_pd_conf =
			    malloc(sizeof(struct iface_pd_conf))) == NULL)
				fatal(NULL);
			memcpy(iface_pd_conf, imsg.data, sizeof(struct
			    iface_pd_conf));
			if (iface_pd_conf->name[sizeof(iface_pd_conf->name) - 1]
			    != '\0')
				fatalx("%s: IMSG_RECONF_IFACE_PD invalid name",
				    __func__);

			SIMPLEQ_INSERT_TAIL(&iface_ia_conf->iface_pd_list,
			    iface_pd_conf, entry);
			break;
		case IMSG_RECONF_IFACE_IA_END:
			iface_ia_conf = NULL;
			break;
		case IMSG_RECONF_IFACE_END:
			iface_conf = NULL;
			break;
		case IMSG_RECONF_END: {
			int	 i;
			int	*ifaces;
			char	 ifnamebuf[IF_NAMESIZE], *if_name;

			if (nconf == NULL)
				fatalx("%s: IMSG_RECONF_END without "
				    "IMSG_RECONF_CONF", __func__);

			ifaces = changed_ifaces(frontend_conf, nconf);
			merge_config(frontend_conf, nconf);
			nconf = NULL;
			for (i = 0; ifaces[i] != 0; i++) {
				if_index = ifaces[i];
				if_name = if_indextoname(if_index, ifnamebuf);
				log_debug("changed iface: %s[%d]", if_name !=
				    NULL ? if_name : "<unknown>", if_index);
				update_iface(if_index);
				frontend_imsg_compose_engine(
				    IMSG_REQUEST_REBOOT, 0, 0, &if_index,
				    sizeof(if_index));
			}
			free(ifaces);
			break;
		}
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
	struct iface		*iface;
	ssize_t			 n;
	int			 shut = 0;

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

		switch (imsg.hdr.type) {
		case IMSG_CTL_END:
		case IMSG_CTL_SHOW_INTERFACE_INFO:
			control_imsg_relay(&imsg);
			break;
		case IMSG_SEND_SOLICIT:
		case IMSG_SEND_REQUEST:
		case IMSG_SEND_RENEW:
		case IMSG_SEND_REBIND: {
			struct imsg_req_dhcp	 imsg_req_dhcp;
			if (IMSG_DATA_SIZE(imsg) != sizeof(imsg_req_dhcp))
				fatalx("%s: IMSG_SEND_DISCOVER wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_req_dhcp, imsg.data,
			    sizeof(imsg_req_dhcp));

			iface = get_iface_by_id(imsg_req_dhcp.if_index);

			if (iface == NULL)
				break;

			iface_data_from_imsg(iface, &imsg_req_dhcp);
			switch (imsg.hdr.type) {
			case IMSG_SEND_SOLICIT:
				send_packet(DHCPSOLICIT, iface);
				break;
			case IMSG_SEND_REQUEST:
				send_packet(DHCPREQUEST, iface);
				break;
			case IMSG_SEND_RENEW:
				send_packet(DHCPRENEW, iface);
				break;
			case IMSG_SEND_REBIND:
				send_packet(DHCPREBIND, iface);
				break;
			}
			break;
		}
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

void
update_iface(uint32_t if_index)
{
	struct ifaddrs		*ifap, *ifa;
	struct iface		*iface;
	struct imsg_ifinfo	 ifinfo;
	int			 flags;
	char			 ifnamebuf[IF_NAMESIZE], *if_name;

	if ((if_name = if_indextoname(if_index, ifnamebuf)) == NULL)
		return;

	if ((flags = get_flags(if_name)) == -1)
		return;

	if (find_iface_conf(&frontend_conf->iface_list, if_name) == NULL)
		return;

	if (getifaddrs(&ifap) != 0)
		fatal("getifaddrs");

	memset(&ifinfo, 0, sizeof(ifinfo));
	ifinfo.if_index = if_index;
	ifinfo.link_state = -1;
	ifinfo.running = (flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING);

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(if_name, ifa->ifa_name) != 0)
			continue;
		if (ifa->ifa_addr == NULL)
			continue;

		switch (ifa->ifa_addr->sa_family) {
		case AF_LINK: {
			struct if_data		*if_data;

			if_data = (struct if_data *)ifa->ifa_data;
			ifinfo.link_state = if_data->ifi_link_state;
			ifinfo.rdomain = if_data->ifi_rdomain;
			goto out;
		}
		default:
			break;
		}
	}
 out:
	freeifaddrs(ifap);
	iface = get_iface_by_id(if_index);
	if (iface == NULL) {
		if ((iface = calloc(1, sizeof(*iface))) == NULL)
			fatal("calloc");
		memcpy(&iface->ifinfo, &ifinfo, sizeof(iface->ifinfo));
		LIST_INSERT_HEAD(&interfaces, iface, entries);
		frontend_imsg_compose_main(IMSG_OPEN_UDPSOCK, 0,
		    &if_index, sizeof(if_index));
	} else
		/* XXX check rdomain changed ?*/
		memcpy(&iface->ifinfo, &ifinfo, sizeof(iface->ifinfo));

	frontend_imsg_compose_main(IMSG_UPDATE_IF, 0, &iface->ifinfo,
	    sizeof(iface->ifinfo));
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
	struct if_announcemsghdr	*ifan;
	uint32_t			 if_index;

	switch (rtm->rtm_type) {
	case RTM_IFINFO:
		if_index = ((struct if_msghdr *)rtm)->ifm_index;
		update_iface(if_index);
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
udp_receive(int fd, short events, void *arg)
{
	struct imsg_dhcp	 imsg_dhcp;
	struct iface		*iface;
	ssize_t			 len;

	iface = (struct iface *)arg;
	memset(&imsg_dhcp, 0, sizeof(imsg_dhcp));

	if ((len = read(fd, imsg_dhcp.packet, 1500)) == -1) {
		log_warn("%s: read", __func__);
		return;
	}

	if (len == 0)
		fatal("%s len == 0", __func__);

	imsg_dhcp.if_index = iface->ifinfo.if_index;
	imsg_dhcp.len = len;
	frontend_imsg_compose_engine(IMSG_DHCP, 0, 0, &imsg_dhcp,
	    sizeof(imsg_dhcp));
}

void
iface_data_from_imsg(struct iface* iface, struct imsg_req_dhcp *imsg)
{
	memcpy(iface->xid, imsg->xid, sizeof(iface->xid));
	iface->elapsed_time = imsg->elapsed_time;
	iface->serverid_len = imsg->serverid_len;
	memcpy(iface->serverid, imsg->serverid, SERVERID_SIZE);
	memcpy(iface->pds, imsg->pds, sizeof(iface->pds));
}

ssize_t
build_packet(uint8_t message_type, struct iface *iface, char *if_name)
{
	struct iface_conf		*iface_conf;
	struct iface_ia_conf		*ia_conf;
	struct dhcp_hdr			 hdr;
	struct dhcp_option_hdr		 opt_hdr;
	struct dhcp_iapd		 iapd;
	struct dhcp_iaprefix		 iaprefix;
	struct dhcp_vendor_class	 vendor_class;
	size_t				 i;
	ssize_t				 len;
	uint16_t			 request_option_code, elapsed_time;
	const uint16_t			 options[] = {DHO_SOL_MAX_RT,
					     DHO_INF_MAX_RT};
	uint8_t				*p;

	switch (message_type) {
	case DHCPSOLICIT:
	case DHCPREQUEST:
	case DHCPRENEW:
	case DHCPREBIND:
		break;
	default:
		fatalx("%s: %s not implemented", __func__,
		    dhcp_message_type2str(message_type));
	}

	iface_conf = find_iface_conf(&frontend_conf->iface_list, if_name);

	memset(dhcp_packet, 0, sizeof(dhcp_packet));

	p = dhcp_packet;
	hdr.msg_type = message_type;
	memcpy(hdr.xid, iface->xid, sizeof(hdr.xid));
	memcpy(p, &hdr, sizeof(struct dhcp_hdr));
	p += sizeof(struct dhcp_hdr);

	opt_hdr.code = htons(DHO_CLIENTID);
	opt_hdr.len = htons(sizeof(struct dhcp_duid));
	memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
	p += sizeof(struct dhcp_option_hdr);
	memcpy(p, &duid, sizeof(struct dhcp_duid));
	p += sizeof(struct dhcp_duid);

	switch (message_type) {
	case DHCPSOLICIT:
	case DHCPREBIND:
		break;
	case DHCPREQUEST:
	case DHCPRENEW:
		opt_hdr.code = htons(DHO_SERVERID);
		opt_hdr.len = htons(iface->serverid_len);
		memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
		p += sizeof(struct dhcp_option_hdr);
		memcpy(p, iface->serverid, iface->serverid_len);
		p += iface->serverid_len;
		break;
	default:
		fatalx("%s: %s not implemented", __func__,
		    dhcp_message_type2str(message_type));
	}
	SIMPLEQ_FOREACH(ia_conf, &iface_conf->iface_ia_list, entry) {
		struct prefix *pd;

		opt_hdr.code = htons(DHO_IA_PD);
		opt_hdr.len = htons(sizeof(struct dhcp_iapd) +
		    sizeof(struct dhcp_option_hdr) +
		    sizeof(struct dhcp_iaprefix));
		memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
		p += sizeof(struct dhcp_option_hdr);
		iapd.iaid = htonl(ia_conf->id);
		iapd.t1 = 0;
		iapd.t2 = 0;
		memcpy(p, &iapd, sizeof(struct dhcp_iapd));
		p += sizeof(struct dhcp_iapd);

		opt_hdr.code = htons(DHO_IA_PREFIX);
		opt_hdr.len = htons(sizeof(struct dhcp_iaprefix));
		memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
		p += sizeof(struct dhcp_option_hdr);

		memset(&iaprefix, 0, sizeof(struct dhcp_iaprefix));

		switch (message_type) {
		case DHCPSOLICIT:
			iaprefix.prefix_len = ia_conf->prefix_len;
			break;
		case DHCPREQUEST:
		case DHCPRENEW:
		case DHCPREBIND:
			pd = &iface->pds[ia_conf->id];
			if (pd->prefix_len > 0) {
				iaprefix.prefix_len = pd->prefix_len;
				memcpy(&iaprefix.prefix, &pd->prefix,
				    sizeof(struct in6_addr));
			} else
				iaprefix.prefix_len = ia_conf->prefix_len;
			break;
		default:
			fatalx("%s: %s not implemented", __func__,
			    dhcp_message_type2str(message_type));
		}
		memcpy(p, &iaprefix, sizeof(struct dhcp_iaprefix));
		p += sizeof(struct dhcp_iaprefix);
	}

	opt_hdr.code = htons(DHO_ORO);
	opt_hdr.len = htons(sizeof(request_option_code) * nitems(options));
	memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
	p += sizeof(struct dhcp_option_hdr);
	for (i = 0; i < nitems(options); i++) {
		request_option_code = htons(options[i]);
		memcpy(p, &request_option_code, sizeof(uint16_t));
		p += sizeof(uint16_t);
	}

	opt_hdr.code = htons(DHO_ELAPSED_TIME);
	opt_hdr.len = htons(2);
	memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
	p += sizeof(struct dhcp_option_hdr);
	elapsed_time = htons(iface->elapsed_time);
	memcpy(p, &elapsed_time, sizeof(uint16_t));
	p += sizeof(uint16_t);

	if (message_type == DHCPSOLICIT && frontend_conf->rapid_commit) {
		opt_hdr.code = htons(DHO_RAPID_COMMIT);
		opt_hdr.len = htons(0);
		memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
		p += sizeof(struct dhcp_option_hdr);
	}

	opt_hdr.code = htons(DHO_VENDOR_CLASS);
	opt_hdr.len = htons(sizeof(struct dhcp_vendor_class) +
	    vendor_class_len);
	memcpy(p, &opt_hdr, sizeof(struct dhcp_option_hdr));
	p += sizeof(struct dhcp_option_hdr);
	vendor_class.enterprise_number = htonl(OPENBSD_ENTERPRISENO);
	vendor_class.vendor_class_len = htons(vendor_class_len);
	memcpy(p, &vendor_class, sizeof(struct dhcp_vendor_class));
	p += sizeof(struct dhcp_vendor_class);
	/* Not a C-string, leave out \0 */
	memcpy(p, vendor_class_data, vendor_class_len);
	p += vendor_class_len;

	len = p - dhcp_packet;
	return (len);
}

void
send_packet(uint8_t message_type, struct iface *iface)
{
	ssize_t	 pkt_len;
	char	 ifnamebuf[IF_NAMESIZE], *if_name, *message_name;

	if (!event_initialized(&iface->udpev)) {
		iface->send_solicit = 1;
		return;
	}

	iface->send_solicit = 0;

	if ((if_name = if_indextoname(iface->ifinfo.if_index, ifnamebuf))
	    == NULL)
		return; /* iface went away, nothing to do */

	switch (message_type) {
	case DHCPSOLICIT:
		message_name = "Soliciting";
		break;
	case DHCPREQUEST:
		message_name = "Requesting";
		break;
	case DHCPRENEW:
		message_name = "Renewing";
		break;
	case DHCPREBIND:
		message_name = "Rebinding";
		break;
	default:
		message_name = NULL;
		break;
	}

	if (message_name)
		log_info("%s lease on %s", message_name, if_name);

	pkt_len = build_packet(message_type, iface, if_name);

	dst.sin6_scope_id = iface->ifinfo.if_index;

	if (sendto(EVENT_FD(&iface->udpev), dhcp_packet, pkt_len, 0,
	    (struct sockaddr *)&dst, sizeof(dst)) == -1)
		log_warn("sendto");
}

struct iface*
get_iface_by_id(uint32_t if_index)
{
	struct iface	*iface;

	LIST_FOREACH (iface, &interfaces, entries) {
		if (iface->ifinfo.if_index == if_index)
			return (iface);
	}

	return (NULL);
}

struct iface*
get_iface_by_name(const char *if_name)
{
	uint32_t ifidx = if_nametoindex(if_name);

	if (ifidx == 0)
		return (NULL);
	return get_iface_by_id(ifidx);
}

void
remove_iface(uint32_t if_index)
{
	struct iface	*iface;

	iface = get_iface_by_id(if_index);

	if (iface == NULL)
		return;

	LIST_REMOVE(iface, entries);
	if (event_initialized(&iface->udpev)) {
		event_del(&iface->udpev);
		close(EVENT_FD(&iface->udpev));
	}
	free(iface);
}

void
set_udpsock(int udpsock, uint32_t if_index)
{
	struct iface	*iface;

	iface = get_iface_by_id(if_index);

	if (iface == NULL) {
		/*
		 * The interface disappeared while we were waiting for the
		 * parent process to open the udp socket.
		 */
		close(udpsock);
	} else if (event_initialized(&iface->udpev)) {
		/*
		 * XXX
		 * The autoconf flag is flapping and we have multiple udp
		 * sockets in flight. We don't need this one because we already
		 * got one.
		 */
		close(udpsock);
	} else {
		event_set(&iface->udpev, udpsock, EV_READ |
		    EV_PERSIST, udp_receive, iface);
		event_add(&iface->udpev, NULL);
		if (iface->send_solicit)
			send_packet(DHCPSOLICIT, iface);
	}
}

struct iface_conf*
find_iface_conf(struct iface_conf_head *head, char *if_name)
{
	struct iface_conf	*iface_conf;

	if (if_name == NULL)
		return (NULL);

	SIMPLEQ_FOREACH(iface_conf, head, entry) {
		if (strcmp(iface_conf->name, if_name) == 0)
			return iface_conf;
	}
	return (NULL);
}

int*
changed_ifaces(struct dhcp6leased_conf *oconf, struct dhcp6leased_conf *nconf)
{
	struct iface_conf	*iface_conf, *oiface_conf;
	int			*ret, if_index, count = 0, i = 0;

	/*
	 * Worst case: All old interfaces replaced with new interfaces.
	 * This should still be a small number
	 */
	SIMPLEQ_FOREACH(iface_conf, &oconf->iface_list, entry)
	    count++;
	SIMPLEQ_FOREACH(iface_conf, &nconf->iface_list, entry)
	    count++;

	ret = calloc(count + 1, sizeof(int));

	SIMPLEQ_FOREACH(iface_conf, &nconf->iface_list, entry) {
		if ((if_index = if_nametoindex(iface_conf->name)) == 0)
			continue;
		oiface_conf = find_iface_conf(&oconf->iface_list,
		    iface_conf->name);
		if (oiface_conf == NULL) {
			/* new interface added to config */
			ret[i++] = if_index;
		} else if (iface_conf_cmp(iface_conf, oiface_conf) != 0) {
			/* interface conf changed */
			ret[i++] = if_index;
		}
	}
	SIMPLEQ_FOREACH(oiface_conf, &oconf->iface_list, entry) {
		if ((if_index = if_nametoindex(oiface_conf->name)) == 0)
			continue;
		if (find_iface_conf(&nconf->iface_list, oiface_conf->name) ==
		    NULL) {
			/* interface removed from config */
			ret[i++] = if_index;
		}
	}
	return ret;
}

int
iface_conf_cmp(struct iface_conf *a, struct iface_conf *b)
{
	return 0;
}
