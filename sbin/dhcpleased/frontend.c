/*	$OpenBSD: frontend.c,v 1.46 2025/09/18 11:37:01 florian Exp $	*/

/*
 * Copyright (c) 2017, 2021 Florian Obser <florian@openbsd.org>
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

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

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

#include "bpf.h"
#include "log.h"
#include "dhcpleased.h"
#include "frontend.h"
#include "control.h"
#include "checksum.h"

#define	ROUTE_SOCKET_BUF_SIZE	16384
#define	BOOTP_MIN_LEN		300	/* fixed bootp packet adds up to 300 */

struct bpf_ev {
	struct event		 ev;
	uint8_t			 buf[BPFLEN];
};

struct iface {
	LIST_ENTRY(iface)	 entries;
	struct bpf_ev		 bpfev;
	struct imsg_ifinfo	 ifinfo;
	int			 send_discover;
	uint32_t		 xid;
	struct in_addr		 ciaddr;
	struct in_addr		 requested_ip;
	struct in_addr		 server_identifier;
	struct in_addr		 dhcp_server;
	int			 udpsock;
};

__dead void	 frontend_shutdown(void);
void		 frontend_sig_handler(int, short, void *);
void		 update_iface(struct if_msghdr *, struct sockaddr_dl *);
void		 frontend_startup(void);
void		 init_ifaces(void);
void		 route_receive(int, short, void *);
void		 handle_route_message(struct rt_msghdr *, struct sockaddr **);
void		 get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
void		 bpf_receive(int, short, void *);
int		 get_flags(char *);
int		 get_xflags(char *);
struct iface	*get_iface_by_id(uint32_t);
void		 remove_iface(uint32_t);
void		 set_bpfsock(int, uint32_t);
void		 iface_data_from_imsg(struct iface*, struct imsg_req_dhcp *);
ssize_t		 build_packet(uint8_t, char *, uint32_t, struct ether_addr *,
		     struct in_addr *, struct in_addr *, struct in_addr *);
void		 send_packet(uint8_t, struct iface *);
void		 bpf_send_packet(struct iface *, uint8_t *, ssize_t);
int		 udp_send_packet(struct iface *, uint8_t *, ssize_t);
#ifndef SMALL
int		 iface_conf_cmp(struct iface_conf *, struct iface_conf *);
#endif /* SMALL */

LIST_HEAD(, iface)		 interfaces;
struct dhcpleased_conf		*frontend_conf;
static struct imsgev		*iev_main;
static struct imsgev		*iev_engine;
struct event			 ev_route;
int				 ioctlsock;

uint8_t				 dhcp_packet[1500];

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

#ifndef SMALL
	frontend_conf = config_new_empty();
#endif /* SMALL */

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if ((pw = getpwnam(DHCPLEASED_USER)) == NULL)
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

#ifndef SMALL
	config_clear(frontend_conf);
#endif /* SMALL */

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
	static struct dhcpleased_conf	*nconf;
	static struct iface_conf	*iface_conf;
	struct imsg			 imsg;
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	struct iface			*iface;
	ssize_t				 n;
	uint32_t			 type;
	int				 shut = 0, bpfsock, if_index, udpsock;

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
		case IMSG_BPFSOCK:
			if ((bpfsock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "bpf fd but didn't receive any",
				    __func__);
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			set_bpfsock(bpfsock, if_index);
			break;
		case IMSG_UDPSOCK:
			if ((udpsock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "udpsocket fd but didn't receive any",
				    __func__);
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			if ((iface = get_iface_by_id(if_index)) == NULL) {
				close(udpsock);
				break;
			}
			if (iface->udpsock != -1)
				fatalx("%s: received unexpected udpsocket",
				    __func__);
			iface->udpsock = udpsock;
			break;
		case IMSG_CLOSE_UDPSOCK:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			if ((iface = get_iface_by_id(if_index)) != NULL &&
			    iface->udpsock != -1) {
				close(iface->udpsock);
				iface->udpsock = -1;
			}
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
#ifndef SMALL
		case IMSG_RECONF_CONF:
			if (nconf != NULL)
				fatalx("%s: IMSG_RECONF_CONF already in "
				    "progress", __func__);
			if ((nconf = malloc(sizeof(struct dhcpleased_conf))) ==
			    NULL)
				fatal(NULL);
			SIMPLEQ_INIT(&nconf->iface_list);
			break;
		case IMSG_RECONF_IFACE:
			if ((iface_conf = malloc(sizeof(struct iface_conf)))
			    == NULL)
				fatal(NULL);

			if (imsg_get_data(&imsg, iface_conf,
			    sizeof(struct iface_conf)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface_conf->vc_id = NULL;
			iface_conf->vc_id_len = 0;
			iface_conf->c_id = NULL;
			iface_conf->c_id_len = 0;
			iface_conf->h_name = NULL;
			SIMPLEQ_INSERT_TAIL(&nconf->iface_list,
			    iface_conf, entry);
			break;
		case IMSG_RECONF_VC_ID:
			if (iface_conf == NULL)
				fatalx("%s: %s without IMSG_RECONF_IFACE",
				    __func__, i2s(type));
			if (iface_conf->vc_id != NULL)
				fatalx("%s: multiple %s for the same interface",
				    __func__, i2s(type));
			if ((iface_conf->vc_id_len = imsg_get_len(&imsg))
			    > 255 + 2 || iface_conf->vc_id_len == 0)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if ((iface_conf->vc_id = malloc(iface_conf->vc_id_len))
			    == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, iface_conf->vc_id,
			    iface_conf->vc_id_len) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			break;
		case IMSG_RECONF_C_ID:
			if (iface_conf == NULL)
				fatalx("%s: %s without IMSG_RECONF_IFACE",
				    __func__, i2s(type));
			if (iface_conf->c_id != NULL)
				fatalx("%s: multiple %s for the same interface",
				    __func__, i2s(type));
			if ((iface_conf->c_id_len = imsg_get_len(&imsg))
			    > 255 + 2 || iface_conf->c_id_len == 0)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if ((iface_conf->c_id = malloc(iface_conf->c_id_len))
			    == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, iface_conf->c_id,
			    iface_conf->c_id_len) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			break;
		case IMSG_RECONF_H_NAME: {
			size_t	len;

			if (iface_conf == NULL)
				fatalx("%s: %s without IMSG_RECONF_IFACE",
				    __func__, i2s(type));
			if (iface_conf->h_name != NULL)
				fatalx("%s: multiple %s for the same interface",
				    __func__, i2s(type));
			if ((len = imsg_get_len(&imsg)) > 256 || len == 0)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if ((iface_conf->h_name = malloc(len)) == NULL)
				fatal(NULL);
			if (imsg_get_data(&imsg, iface_conf->h_name, len) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (iface_conf->h_name[len - 1] != '\0')
				fatalx("Invalid hostname");
			break;
		}
		case IMSG_RECONF_END: {
			int	 i;
			int	*ifaces;

			if (nconf == NULL)
				fatalx("%s: %s without IMSG_RECONF_CONF",
				    __func__, i2s(type));

			ifaces = changed_ifaces(frontend_conf, nconf);
			merge_config(frontend_conf, nconf);
			nconf = NULL;
			for (i = 0; ifaces[i] != 0; i++) {
				if_index = ifaces[i];
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
	struct iface		*iface;
	ssize_t			 n;
	uint32_t		 type;
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

		type = imsg_get_type(&imsg);

		switch (type) {
#ifndef	SMALL
		case IMSG_CTL_END:
		case IMSG_CTL_SHOW_INTERFACE_INFO:
			control_imsg_relay(&imsg);
			break;
#endif	/* SMALL */
		case IMSG_SEND_DISCOVER: {
			struct imsg_req_dhcp	 imsg_req_dhcp;

			if (imsg_get_data(&imsg, &imsg_req_dhcp,
			    sizeof(imsg_req_dhcp)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_iface_by_id(imsg_req_dhcp.if_index);

			if (iface == NULL)
				break;

			iface_data_from_imsg(iface, &imsg_req_dhcp);
			send_packet(DHCPDISCOVER, iface);
			break;
		}
		case IMSG_SEND_REQUEST: {
			struct imsg_req_dhcp	 imsg_req_dhcp;

			if (imsg_get_data(&imsg, &imsg_req_dhcp,
			    sizeof(imsg_req_dhcp)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_iface_by_id(imsg_req_dhcp.if_index);

			if (iface == NULL)
				break;

			iface_data_from_imsg(iface, &imsg_req_dhcp);
			send_packet(DHCPREQUEST, iface);
			break;
		}
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

void
update_iface(struct if_msghdr *ifm, struct sockaddr_dl *sdl)
{
	struct iface		*iface;
	struct imsg_ifinfo	 ifinfo;
	uint32_t		 if_index;
	int			 flags, xflags;

	if_index = ifm->ifm_index;

	flags = ifm->ifm_flags;
	xflags = ifm->ifm_xflags;

	iface = get_iface_by_id(if_index);

	if (!(xflags & IFXF_AUTOCONF4)) {
		if (iface != NULL) {
			log_info("Removed autoconf flag from %s",
			    iface->ifinfo.if_name);
			frontend_imsg_compose_engine(IMSG_REMOVE_IF, 0, 0,
			    &if_index, sizeof(if_index));
			remove_iface(if_index);
		}
		return;
	}

	memset(&ifinfo, 0, sizeof(ifinfo));
	ifinfo.if_index = if_index;
	if (if_indextoname(if_index, ifinfo.if_name) == NULL)
		return; /* interface disappeared */

	ifinfo.link_state = ifm->ifm_data.ifi_link_state;
	ifinfo.rdomain = ifm->ifm_tableid;
	ifinfo.running = (flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING);

	if (sdl != NULL && (sdl->sdl_type == IFT_ETHER ||
	    sdl->sdl_type == IFT_CARP) && sdl->sdl_alen == ETHER_ADDR_LEN)
		memcpy(ifinfo.hw_address.ether_addr_octet, LLADDR(sdl),
		    ETHER_ADDR_LEN);
	else if (iface == NULL) {
		log_warnx("Could not find AF_LINK address for %s.",
		    ifinfo.if_name);
		return;
	}

	if (iface == NULL) {
		if ((iface = calloc(1, sizeof(*iface))) == NULL)
			fatal("calloc");
		iface->udpsock = -1;
		LIST_INSERT_HEAD(&interfaces, iface, entries);
		frontend_imsg_compose_main(IMSG_OPEN_BPFSOCK, 0,
		    &if_index, sizeof(if_index));
	} else {
		if (iface->ifinfo.rdomain != ifinfo.rdomain &&
		    iface->udpsock != -1) {
			close(iface->udpsock);
			iface->udpsock = -1;
		}
	}

	if (memcmp(&iface->ifinfo, &ifinfo, sizeof(iface->ifinfo)) != 0) {
		memcpy(&iface->ifinfo, &ifinfo, sizeof(iface->ifinfo));
		frontend_imsg_compose_main(IMSG_UPDATE_IF, 0, &iface->ifinfo,
		    sizeof(iface->ifinfo));
	}
}

void
frontend_startup(void)
{
	if (!event_initialized(&ev_route))
		fatalx("%s: did not receive a route socket from the main "
		    "process", __func__);

	init_ifaces();
	if (pledge("stdio unix recvfd", NULL) == -1)
		fatal("pledge");
	event_add(&ev_route, NULL);
}

void
init_ifaces(void)
{
	struct iface		*iface;
	struct imsg_ifinfo	 ifinfo;
	struct if_nameindex	*ifnidxp, *ifnidx;
	struct ifaddrs		*ifap, *ifa;
	uint32_t		 if_index;
	int			 flags, xflags;
	char			*if_name;

	if ((ifnidxp = if_nameindex()) == NULL)
		fatalx("if_nameindex");

	if (getifaddrs(&ifap) != 0)
		fatal("getifaddrs");

	for (ifnidx = ifnidxp; ifnidx->if_index != 0 && ifnidx->if_name != NULL;
	    ifnidx++) {
		if_index = ifnidx->if_index;
		if_name = ifnidx->if_name;
		if ((flags = get_flags(if_name)) == -1)
			continue;
		if ((xflags = get_xflags(if_name)) == -1)
			continue;
		if (!(xflags & IFXF_AUTOCONF4))
			continue;

		memset(&ifinfo, 0, sizeof(ifinfo));
		ifinfo.if_index = if_index;
		memcpy(&ifinfo.if_name, if_name, sizeof(ifinfo.if_name));
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
				struct sockaddr_dl	*sdl;

				sdl = (struct sockaddr_dl *)ifa->ifa_addr;
				if ((sdl->sdl_type != IFT_ETHER &&
				    sdl->sdl_type != IFT_CARP) ||
				    sdl->sdl_alen != ETHER_ADDR_LEN)
					continue;
				memcpy(ifinfo.hw_address.ether_addr_octet,
				    LLADDR(sdl), ETHER_ADDR_LEN);

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
		if (ifinfo.link_state == -1)
			/* no AF_LINK found */
			continue;

		if ((iface = calloc(1, sizeof(*iface))) == NULL)
			fatal("calloc");
		iface->udpsock = -1;
		memcpy(&iface->ifinfo, &ifinfo, sizeof(iface->ifinfo));
		LIST_INSERT_HEAD(&interfaces, iface, entries);
		frontend_imsg_compose_main(IMSG_OPEN_BPFSOCK, 0,
		    &if_index, sizeof(if_index));
		frontend_imsg_compose_main(IMSG_UPDATE_IF, 0, &iface->ifinfo,
		    sizeof(iface->ifinfo));
	}

	freeifaddrs(ifap);
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
	struct sockaddr_dl		*sdl = NULL;
	struct if_announcemsghdr	*ifan;
	uint32_t			 if_index;

	switch (rtm->rtm_type) {
	case RTM_IFINFO:
		if (rtm->rtm_addrs & RTA_IFP && rti_info[RTAX_IFP]->sa_family
		    == AF_LINK)
			sdl = (struct sockaddr_dl *)rti_info[RTAX_IFP];
		update_iface((struct if_msghdr *)rtm, sdl);
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
	case RTM_PROPOSAL:
		if (rtm->rtm_priority == RTP_PROPOSAL_SOLICIT) {
			log_debug("RTP_PROPOSAL_SOLICIT");
			frontend_imsg_compose_engine(IMSG_REPROPOSE_RDNS,
			    0, 0, NULL, 0);
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
bpf_receive(int fd, short events, void *arg)
{
	struct bpf_hdr		*hdr;
	struct imsg_dhcp	 imsg_dhcp;
	struct iface		*iface;
	ssize_t			 len, rem;
	uint8_t			*p;

	iface = (struct iface *)arg;

	if ((len = read(fd, iface->bpfev.buf, BPFLEN)) == -1) {
		log_warn("%s: read", __func__);
		return;
	}

	if (len == 0)
		fatal("%s len == 0", __func__);

	memset(&imsg_dhcp, 0, sizeof(imsg_dhcp));
	imsg_dhcp.if_index = iface->ifinfo.if_index;

	rem = len;
	p = iface->bpfev.buf;

	while (rem > 0) {
		if ((size_t)rem < sizeof(*hdr)) {
			log_warnx("packet too short");
			return;
		}
		hdr = (struct bpf_hdr *)p;
		if (hdr->bh_caplen != hdr->bh_datalen) {
			log_warnx("skipping truncated packet");
			goto cont;
		}
		if (rem < hdr->bh_hdrlen + hdr->bh_caplen)
			/* we are done */
			break;
		if (hdr->bh_caplen > sizeof(imsg_dhcp.packet)) {
			log_warn("packet too big");
			goto cont;
		}
		memcpy(&imsg_dhcp.packet, p + hdr->bh_hdrlen, hdr->bh_caplen);
		imsg_dhcp.len = hdr->bh_caplen;
		imsg_dhcp.csumflags = hdr->bh_csumflags;
		frontend_imsg_compose_engine(IMSG_DHCP, 0, 0, &imsg_dhcp,
		    sizeof(imsg_dhcp));
 cont:
		p += BPF_WORDALIGN(hdr->bh_hdrlen + hdr->bh_caplen);
		rem -= BPF_WORDALIGN(hdr->bh_hdrlen + hdr->bh_caplen);

	}
}

void
iface_data_from_imsg(struct iface* iface, struct imsg_req_dhcp *imsg)
{
	iface->xid = imsg->xid;
	iface->ciaddr = imsg->ciaddr;
	iface->requested_ip = imsg->requested_ip;
	iface->server_identifier = imsg->server_identifier;
	iface->dhcp_server = imsg->dhcp_server;
}

ssize_t
build_packet(uint8_t message_type, char *if_name, uint32_t xid,
    struct ether_addr *hw_address, struct in_addr *ciaddr, struct in_addr
    *requested_ip, struct in_addr *server_identifier)
{
	static uint8_t	 dhcp_cookie[] = DHCP_COOKIE;
	static uint8_t	 dhcp_message_type[] = {DHO_DHCP_MESSAGE_TYPE, 1,
		DHCPDISCOVER};
	static uint8_t	 dhcp_hostname[255 + 2] = {DHO_HOST_NAME, 0 /*, ... */};
	static uint8_t	 dhcp_client_id[] = {DHO_DHCP_CLIENT_IDENTIFIER, 7,
		HTYPE_ETHER, 0, 0, 0, 0, 0, 0};
	static uint8_t	 dhcp_req_list[] = {DHO_DHCP_PARAMETER_REQUEST_LIST,
		8, DHO_SUBNET_MASK, DHO_ROUTERS, DHO_DOMAIN_NAME_SERVERS,
		DHO_HOST_NAME, DHO_DOMAIN_NAME, DHO_BROADCAST_ADDRESS,
		DHO_DOMAIN_SEARCH, DHO_CLASSLESS_STATIC_ROUTES};
	static uint8_t	 dhcp_req_list_v6[] = {DHO_DHCP_PARAMETER_REQUEST_LIST,
		9, DHO_SUBNET_MASK, DHO_ROUTERS, DHO_DOMAIN_NAME_SERVERS,
		DHO_HOST_NAME, DHO_DOMAIN_NAME, DHO_BROADCAST_ADDRESS,
		DHO_DOMAIN_SEARCH, DHO_CLASSLESS_STATIC_ROUTES,
		DHO_IPV6_ONLY_PREFERRED};
	static uint8_t	 dhcp_requested_address[] = {DHO_DHCP_REQUESTED_ADDRESS,
		4, 0, 0, 0, 0};
	static uint8_t	 dhcp_server_identifier[] = {DHO_DHCP_SERVER_IDENTIFIER,
		4, 0, 0, 0, 0};
#ifndef SMALL
	struct iface_conf	*iface_conf;
#endif /* SMALL */
	struct dhcp_hdr		*hdr;
	ssize_t			 len;
	uint8_t			*p;
	char			*c;

#ifndef SMALL
	iface_conf = find_iface_conf(&frontend_conf->iface_list, if_name);
#endif /* SMALL */

	memset(dhcp_packet, 0, sizeof(dhcp_packet));
	dhcp_message_type[2] = message_type;
	p = dhcp_packet;
	hdr = (struct dhcp_hdr *)p;
	hdr->op = DHCP_BOOTREQUEST;
	hdr->htype = HTYPE_ETHER;
	hdr->hlen = 6;
	hdr->hops = 0;
	hdr->xid = htonl(xid);
	hdr->secs = 0;
	hdr->ciaddr = *ciaddr;
	memcpy(hdr->chaddr, hw_address, sizeof(*hw_address));
	p += sizeof(struct dhcp_hdr);
	memcpy(p, dhcp_cookie, sizeof(dhcp_cookie));
	p += sizeof(dhcp_cookie);
	memcpy(p, dhcp_message_type, sizeof(dhcp_message_type));
	p += sizeof(dhcp_message_type);

#ifndef SMALL
	if (iface_conf != NULL && iface_conf->h_name != NULL) {
		if (iface_conf->h_name[0] != '\0') {
			dhcp_hostname[1] = strlen(iface_conf->h_name);
			memcpy(dhcp_hostname + 2, iface_conf->h_name,
			    strlen(iface_conf->h_name));
			memcpy(p, dhcp_hostname, dhcp_hostname[1] + 2);
			p += dhcp_hostname[1] + 2;
		}
	} else
#endif /* SMALL */
	{
		if (gethostname(dhcp_hostname + 2,
		    sizeof(dhcp_hostname) - 2) == 0 &&
		    dhcp_hostname[2] != '\0') {
			if ((c = strchr(dhcp_hostname + 2, '.')) != NULL)
				*c = '\0';
			dhcp_hostname[1] = strlen(dhcp_hostname + 2);
			memcpy(p, dhcp_hostname, dhcp_hostname[1] + 2);
			p += dhcp_hostname[1] + 2;
		}
	}

#ifndef SMALL
	if (iface_conf != NULL) {
		if (iface_conf->c_id_len > 0) {
			/* XXX check space */
			memcpy(p, iface_conf->c_id, iface_conf->c_id_len);
			p += iface_conf->c_id_len;
		} else {
			memcpy(dhcp_client_id + 3, hw_address, sizeof(*hw_address));
			memcpy(p, dhcp_client_id, sizeof(dhcp_client_id));
			p += sizeof(dhcp_client_id);
		}
		if (iface_conf->vc_id_len > 0) {
			/* XXX check space */
			memcpy(p, iface_conf->vc_id, iface_conf->vc_id_len);
			p += iface_conf->vc_id_len;
		}
		if (iface_conf->prefer_ipv6) {
			memcpy(p, dhcp_req_list_v6, sizeof(dhcp_req_list_v6));
			p += sizeof(dhcp_req_list_v6);

		} else {
			memcpy(p, dhcp_req_list, sizeof(dhcp_req_list));
			p += sizeof(dhcp_req_list);
		}
	} else
#endif /* SMALL */
	{
		memcpy(dhcp_client_id + 3, hw_address, sizeof(*hw_address));
		memcpy(p, dhcp_client_id, sizeof(dhcp_client_id));
		p += sizeof(dhcp_client_id);
		memcpy(p, dhcp_req_list, sizeof(dhcp_req_list));
		p += sizeof(dhcp_req_list);
	}

	if (requested_ip->s_addr != INADDR_ANY) {
		memcpy(dhcp_requested_address + 2, requested_ip,
		    sizeof(*requested_ip));
		memcpy(p, dhcp_requested_address,
		    sizeof(dhcp_requested_address));
		p += sizeof(dhcp_requested_address);
	}

	if (server_identifier->s_addr != INADDR_ANY) {
		memcpy(dhcp_server_identifier + 2, server_identifier,
		    sizeof(*server_identifier));
		memcpy(p, dhcp_server_identifier,
		    sizeof(dhcp_server_identifier));
		p += sizeof(dhcp_server_identifier);
	}

	*p = DHO_END;
	p += 1;

	len = p - dhcp_packet;

	/* dhcp_packet is initialized with DHO_PADs */
	if (len < BOOTP_MIN_LEN)
		len = BOOTP_MIN_LEN;

	return (len);
}

void
send_packet(uint8_t message_type, struct iface *iface)
{
	ssize_t			 pkt_len;

	if (!event_initialized(&iface->bpfev.ev)) {
		iface->send_discover = 1;
		return;
	}

	iface->send_discover = 0;

	log_debug("%s on %s", message_type == DHCPDISCOVER ? "DHCPDISCOVER" :
	    "DHCPREQUEST", iface->ifinfo.if_name);

	pkt_len = build_packet(message_type, iface->ifinfo.if_name, iface->xid,
	    &iface->ifinfo.hw_address, &iface->ciaddr, &iface->requested_ip,
	    &iface->server_identifier);
	if (iface->dhcp_server.s_addr != INADDR_ANY) {
		if (udp_send_packet(iface, dhcp_packet, pkt_len) == -1)
			bpf_send_packet(iface, dhcp_packet, pkt_len);
	} else
		bpf_send_packet(iface, dhcp_packet, pkt_len);
}

int
udp_send_packet(struct iface *iface, uint8_t *packet, ssize_t len)
{
	struct sockaddr_in	to;

	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_len = sizeof(to);
	to.sin_addr = iface->dhcp_server;
	to.sin_port = ntohs(SERVER_PORT);

	if (sendto(iface->udpsock, packet, len, 0, (struct sockaddr *)&to,
	    sizeof(to)) == -1) {
		log_warn("sendto");
		return -1;
	}
	return 0;
}
void
bpf_send_packet(struct iface *iface, uint8_t *packet, ssize_t len)
{
	struct iovec		 iov[4];
	struct ether_header	 eh;
	struct ip		 ip;
	struct udphdr		 udp;
	ssize_t			 total, result;
	int			 iovcnt = 0, i;

	memset(eh.ether_dhost, 0xff, sizeof(eh.ether_dhost));
	memcpy(eh.ether_shost, &iface->ifinfo.hw_address,
	    sizeof(eh.ether_dhost));
	eh.ether_type = htons(ETHERTYPE_IP);
	iov[0].iov_base = &eh;
	iov[0].iov_len = sizeof(eh);
	iovcnt++;

	ip.ip_v = 4;
	ip.ip_hl = 5;
	ip.ip_tos = IPTOS_LOWDELAY;
	ip.ip_len = htons(sizeof(ip) + sizeof(udp) + len);
	ip.ip_id = 0;
	ip.ip_off = 0;
	ip.ip_ttl = 128;
	ip.ip_p = IPPROTO_UDP;
	ip.ip_sum = 0;
	ip.ip_src.s_addr = INADDR_ANY;
	ip.ip_dst.s_addr = INADDR_BROADCAST;
	ip.ip_sum = wrapsum(checksum((unsigned char *)&ip, sizeof(ip), 0));
	iov[iovcnt].iov_base = &ip;
	iov[iovcnt].iov_len = sizeof(ip);
	iovcnt++;

	udp.uh_sport = htons(CLIENT_PORT);
	udp.uh_dport = htons(SERVER_PORT);
	udp.uh_ulen = htons(sizeof(udp) + len);
	udp.uh_sum = 0;
	udp.uh_sum = wrapsum(checksum((unsigned char *)&udp, sizeof(udp),
	    checksum((unsigned char *)packet, len,
	    checksum((unsigned char *)&ip.ip_src,
	    2 * sizeof(ip.ip_src),
	    IPPROTO_UDP + (uint32_t)ntohs(udp.uh_ulen)))));
	iov[iovcnt].iov_base = &udp;
	iov[iovcnt].iov_len = sizeof(udp);
	iovcnt++;

	iov[iovcnt].iov_base = packet;
	iov[iovcnt].iov_len = len;
	iovcnt++;

	total = 0;
	for (i = 0; i < iovcnt; i++)
		total += iov[i].iov_len;

	result = writev(EVENT_FD(&iface->bpfev.ev), iov, iovcnt);
	if (result == -1)
		log_warn("%s: writev", __func__);
	else if (result < total) {
		log_warnx("%s, writev: %zd of %zd bytes", __func__, result,
		    total);
	}
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

void
remove_iface(uint32_t if_index)
{
	struct iface	*iface;

	iface = get_iface_by_id(if_index);

	if (iface == NULL)
		return;

	LIST_REMOVE(iface, entries);
	if (event_initialized(&iface->bpfev.ev)) {
		event_del(&iface->bpfev.ev);
		close(EVENT_FD(&iface->bpfev.ev));
	}
	if (iface->udpsock != -1)
		close(iface->udpsock);
	free(iface);
}

void
set_bpfsock(int bpfsock, uint32_t if_index)
{
	struct iface	*iface;

	iface = get_iface_by_id(if_index);

	if (iface == NULL) {
		/*
		 * The interface disappeared while we were waiting for the
		 * parent process to open the bpf socket.
		 */
		close(bpfsock);
	} else if (event_initialized(&iface->bpfev.ev)) {
		/*
		 * The autoconf flag is flapping and we have multiple bpf sockets in
		 * flight. We don't need this one because we already got one.
		 */
		close(bpfsock);
	} else {
		event_set(&iface->bpfev.ev, bpfsock, EV_READ |
		    EV_PERSIST, bpf_receive, iface);
		event_add(&iface->bpfev.ev, NULL);
		if (iface->send_discover)
			send_packet(DHCPDISCOVER, iface);
	}
}

#ifndef SMALL
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
changed_ifaces(struct dhcpleased_conf *oconf, struct dhcpleased_conf *nconf)
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
	if (a->vc_id_len != b->vc_id_len)
		return 1;
	if (memcmp(a->vc_id, b->vc_id, a->vc_id_len) != 0)
		return 1;
	if (a->c_id_len != b->c_id_len)
		return 1;
	if (memcmp(a->c_id, b->c_id, a->c_id_len) != 0)
		return 1;
	if (a->h_name == NULL ||  b->h_name == NULL)
		return 1;
	if (strcmp(a->h_name, b->h_name) != 0)
		return 1;
	if (a->ignore != b->ignore)
		return 1;
	if (a->ignore_servers_len != b->ignore_servers_len)
		return 1;
	if (memcmp(a->ignore_servers, b->ignore_servers,
	    a->ignore_servers_len * sizeof (struct in_addr)) != 0)
		return 1;
	return 0;
}
#endif /* SMALL */
