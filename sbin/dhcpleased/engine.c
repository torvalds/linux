/*	$OpenBSD: engine.c,v 1.59 2025/09/18 11:37:01 florian Exp $	*/

/*
 * Copyright (c) 2017, 2021 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2004, 2005 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>
#include <sys/mbuf.h>

#include <net/if.h>
#include <net/route.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vis.h>

#include "checksum.h"
#include "log.h"
#include "dhcpleased.h"
#include "engine.h"

/*
 * RFC 2131 4.1 p23 has "SHOULD be 4 seconds", we are a bit more aggressive,
 * networks are faster these days.
 */
#define	START_EXP_BACKOFF	 1
#define	MAX_EXP_BACKOFF_SLOW	64 /* RFC 2131 4.1 p23 */
#define	MAX_EXP_BACKOFF_FAST	 2
#define	MINIMUM(a, b)		(((a) < (b)) ? (a) : (b))

enum if_state {
	IF_DOWN,
	IF_INIT,
	/* IF_SELECTING, */
	IF_REQUESTING,
	IF_BOUND,
	IF_RENEWING,
	IF_REBINDING,
	/* IF_INIT_REBOOT, */
	IF_REBOOTING,
	IF_IPV6_ONLY,
};

const char* if_state_name[] = {
	"Down",
	"Init",
	/* "Selecting", */
	"Requesting",
	"Bound",
	"Renewing",
	"Rebinding",
	/* "Init-Reboot", */
	"Rebooting",
	"IPv6 only",
};

struct dhcpleased_iface {
	LIST_ENTRY(dhcpleased_iface)	 entries;
	enum if_state			 state;
	struct event			 timer;
	struct timeval			 timo;
	uint32_t			 if_index;
	char				 if_name[IF_NAMESIZE];
	int				 rdomain;
	int				 running;
	struct ether_addr		 hw_address;
	int				 link_state;
	uint32_t			 cur_mtu;
	uint32_t			 xid;
	struct timespec			 request_time;
	struct in_addr			 server_identifier;
	struct in_addr			 dhcp_server; /* for unicast */
	struct in_addr			 requested_ip;
	struct in_addr			 mask;
	struct in_addr			 siaddr;
	char				 file[4 * DHCP_FILE_LEN + 1];
	char				 hostname[4 * 255 + 1];
	char				 domainname[4 * 255 + 1];
	struct dhcp_route		 prev_routes[MAX_DHCP_ROUTES];
	int				 prev_routes_len;
	struct dhcp_route		 routes[MAX_DHCP_ROUTES];
	int				 routes_len;
	struct in_addr			 nameservers[MAX_RDNS_COUNT];
	uint32_t			 lease_time;
	uint32_t			 renewal_time;
	uint32_t			 rebinding_time;
	uint32_t			 ipv6_only_time;
};

LIST_HEAD(, dhcpleased_iface) dhcpleased_interfaces;

__dead void		 engine_shutdown(void);
void			 engine_sig_handler(int sig, short, void *);
void			 engine_dispatch_frontend(int, short, void *);
void			 engine_dispatch_main(int, short, void *);
#ifndef	SMALL
void			 send_interface_info(struct dhcpleased_iface *, pid_t);
void			 engine_showinfo_ctl(pid_t, uint32_t);
#endif	/* SMALL */
void			 engine_update_iface(struct imsg_ifinfo *);
struct dhcpleased_iface	*get_dhcpleased_iface_by_id(uint32_t);
void			 remove_dhcpleased_iface(uint32_t);
void			 parse_dhcp(struct dhcpleased_iface *,
			     struct imsg_dhcp *);
void			 state_transition(struct dhcpleased_iface *, enum
			     if_state);
void			 iface_timeout(int, short, void *);
void			 request_dhcp_discover(struct dhcpleased_iface *);
void			 request_dhcp_request(struct dhcpleased_iface *);
void			 log_lease(struct dhcpleased_iface *, int);
void			 log_rdns(struct dhcpleased_iface *, int);
void			 send_configure_interface(struct dhcpleased_iface *);
void			 send_rdns_proposal(struct dhcpleased_iface *);
void			 send_deconfigure_interface(struct dhcpleased_iface *);
void			 send_rdns_withdraw(struct dhcpleased_iface *);
void			 send_routes_withdraw(struct dhcpleased_iface *);
void			 parse_lease(struct dhcpleased_iface *,
			     struct imsg_ifinfo *);
int			 engine_imsg_compose_main(int, pid_t, void *, uint16_t);
void			 log_dhcp_hdr(struct dhcp_hdr *);
const char		*dhcp_message_type2str(uint8_t);

#ifndef SMALL
struct dhcpleased_conf	*engine_conf;
#endif /* SMALL */

static struct imsgev	*iev_frontend;
static struct imsgev	*iev_main;
int64_t			 proposal_id;

void
engine_sig_handler(int sig, short event, void *arg)
{
	/*
	 * Normal signal handler rules don't apply because libevent
	 * decouples for us.
	 */

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		engine_shutdown();
	default:
		fatalx("unexpected signal");
	}
}

void
engine(int debug, int verbose)
{
	struct event		 ev_sigint, ev_sigterm;
	struct passwd		*pw;

#ifndef SMALL
	engine_conf = config_new_empty();
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

	setproctitle("%s", "engine");
	log_procinit("engine");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	event_init();

	/* Setup signal handler(s). */
	signal_set(&ev_sigint, SIGINT, engine_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, engine_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Setup pipe and event handler to the main process. */
	if ((iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);

	if (imsgbuf_init(&iev_main->ibuf, 3) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_main->ibuf);
	iev_main->handler = engine_dispatch_main;

	/* Setup event handlers. */
	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	LIST_INIT(&dhcpleased_interfaces);

	event_dispatch();

	engine_shutdown();
}

__dead void
engine_shutdown(void)
{
	/* Close pipes. */
	imsgbuf_clear(&iev_frontend->ibuf);
	close(iev_frontend->ibuf.fd);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	free(iev_frontend);
	free(iev_main);

	log_info("engine exiting");
	exit(0);
}

int
engine_imsg_compose_frontend(int type, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_frontend, type, 0, pid, -1,
	    data, datalen));
}

int
engine_imsg_compose_main(int type, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1,
	    data, datalen));
}

void
engine_dispatch_frontend(int fd, short event, void *bula)
{
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	struct imsg			 imsg;
	struct dhcpleased_iface		*iface;
	ssize_t				 n;
	int				 shut = 0;
#ifndef	SMALL
	int				 verbose;
#endif	/* SMALL */
	uint32_t			 if_index, type;

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
		case IMSG_CTL_LOG_VERBOSE:
			if (imsg_get_data(&imsg, &verbose,
			    sizeof(verbose)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			log_setverbose(verbose);
			break;
		case IMSG_CTL_SHOW_INTERFACE_INFO:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			engine_showinfo_ctl(imsg_get_pid(&imsg), if_index);
			break;
		case IMSG_REQUEST_REBOOT:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_dhcpleased_iface_by_id(if_index);
			if (iface != NULL) {
				switch (iface->state) {
				case IF_DOWN:
					break;
				case IF_INIT:
				case IF_REQUESTING:
					state_transition(iface, iface->state);
					break;
				case IF_RENEWING:
				case IF_REBINDING:
				case IF_REBOOTING:
				case IF_BOUND:
				case IF_IPV6_ONLY:
					state_transition(iface, IF_REBOOTING);
					break;
				}
			}
			break;
#endif	/* SMALL */
		case IMSG_REMOVE_IF:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			remove_dhcpleased_iface(if_index);
			break;
		case IMSG_DHCP: {
			struct imsg_dhcp	imsg_dhcp;

			if (imsg_get_data(&imsg, &imsg_dhcp,
			    sizeof(imsg_dhcp)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			iface = get_dhcpleased_iface_by_id(imsg_dhcp.if_index);
			if (iface != NULL)
				parse_dhcp(iface, &imsg_dhcp);
			break;
		}
		case IMSG_REPROPOSE_RDNS:
			LIST_FOREACH (iface, &dhcpleased_interfaces, entries)
				send_rdns_proposal(iface);
			break;
		default:
			log_debug("%s: unexpected imsg %d", __func__, type);
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
engine_dispatch_main(int fd, short event, void *bula)
{
#ifndef SMALL
	static struct dhcpleased_conf	*nconf;
	static struct iface_conf	*iface_conf;
#endif /* SMALL */
	struct imsg			 imsg;
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	struct imsg_ifinfo		 imsg_ifinfo;
	ssize_t				 n;
	uint32_t			 type;
	int				 shut = 0;

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
			 * Setup pipe and event handler to the frontend
			 * process.
			 */
			if (iev_frontend)
				fatalx("%s: received unexpected imsg fd "
				    "to engine", __func__);

			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg fd to "
				   "engine but didn't receive any", __func__);

			iev_frontend = malloc(sizeof(struct imsgev));
			if (iev_frontend == NULL)
				fatal(NULL);

			if (imsgbuf_init(&iev_frontend->ibuf, fd) == -1)
				fatal(NULL);
			iev_frontend->handler = engine_dispatch_frontend;
			iev_frontend->events = EV_READ;

			event_set(&iev_frontend->ev, iev_frontend->ibuf.fd,
			iev_frontend->events, iev_frontend->handler,
			    iev_frontend);
			event_add(&iev_frontend->ev, NULL);

			if (pledge("stdio", NULL) == -1)
				fatal("pledge");

			break;
		case IMSG_UPDATE_IF:
			if (imsg_get_data(&imsg, &imsg_ifinfo,
			    sizeof(imsg_ifinfo)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_ifinfo.lease[LEASE_SIZE - 1] != '\0')
				fatalx("Invalid lease");

			engine_update_iface(&imsg_ifinfo);
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
			struct dhcpleased_iface	*iface;
			int			*ifaces;
			int			 i, if_index;

			if (nconf == NULL)
				fatalx("%s: %s without IMSG_RECONF_CONF",
				    __func__, i2s(type));

			ifaces = changed_ifaces(engine_conf, nconf);
			merge_config(engine_conf, nconf);
			nconf = NULL;
			for (i = 0; ifaces[i] != 0; i++) {
				if_index = ifaces[i];
				iface = get_dhcpleased_iface_by_id(if_index);
				if (iface == NULL)
					continue;
				iface_conf = find_iface_conf(
				    &engine_conf->iface_list, iface->if_name);
				if (iface_conf == NULL)
					continue;
				if (iface_conf->ignore & IGN_DNS)
					send_rdns_withdraw(iface);
				if (iface_conf->ignore & IGN_ROUTES)
					send_routes_withdraw(iface);
			}
			free(ifaces);
			break;
		}
#endif /* SMALL */
		default:
			log_debug("%s: unexpected imsg %d", __func__, type);
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

#ifndef	SMALL
void
send_interface_info(struct dhcpleased_iface *iface, pid_t pid)
{
	struct ctl_engine_info	 cei;

	memset(&cei, 0, sizeof(cei));
	cei.if_index = iface->if_index;
	cei.running = iface->running;
	cei.link_state = iface->link_state;
	strlcpy(cei.state, if_state_name[iface->state], sizeof(cei.state));
	memcpy(&cei.request_time, &iface->request_time,
	    sizeof(cei.request_time));
	cei.server_identifier = iface->server_identifier;
	cei.dhcp_server = iface->dhcp_server;
	cei.requested_ip = iface->requested_ip;
	cei.mask = iface->mask;
	cei.routes_len = iface->routes_len;
	memcpy(cei.routes, iface->routes, sizeof(cei.routes));
	memcpy(cei.nameservers, iface->nameservers, sizeof(cei.nameservers));
	cei.lease_time = iface->lease_time;
	cei.renewal_time = iface->renewal_time;
	cei.rebinding_time = iface->rebinding_time;
	engine_imsg_compose_frontend(IMSG_CTL_SHOW_INTERFACE_INFO, pid, &cei,
	    sizeof(cei));
}

void
engine_showinfo_ctl(pid_t pid, uint32_t if_index)
{
	struct dhcpleased_iface			*iface;

	if ((iface = get_dhcpleased_iface_by_id(if_index)) != NULL)
		send_interface_info(iface, pid);
	else
		engine_imsg_compose_frontend(IMSG_CTL_END, pid, NULL, 0);
}
#endif	/* SMALL */

void
engine_update_iface(struct imsg_ifinfo *imsg_ifinfo)
{
	struct dhcpleased_iface	*iface;
	int			 need_refresh = 0;

	iface = get_dhcpleased_iface_by_id(imsg_ifinfo->if_index);

	if (iface == NULL) {
		if ((iface = calloc(1, sizeof(*iface))) == NULL)
			fatal("calloc");
		iface->state = IF_DOWN;
		iface->xid = arc4random();
		iface->timo.tv_usec = arc4random_uniform(1000000);
		evtimer_set(&iface->timer, iface_timeout, iface);
		iface->if_index = imsg_ifinfo->if_index;
		iface->rdomain = imsg_ifinfo->rdomain;
		iface->running = imsg_ifinfo->running;
		iface->link_state = imsg_ifinfo->link_state;
		iface->requested_ip.s_addr = INADDR_ANY;
		memcpy(iface->if_name, imsg_ifinfo->if_name,
		    sizeof(iface->if_name));
		iface->if_name[sizeof(iface->if_name) - 1] = '\0';
		memcpy(&iface->hw_address, &imsg_ifinfo->hw_address,
		    sizeof(struct ether_addr));
		LIST_INSERT_HEAD(&dhcpleased_interfaces, iface, entries);
		need_refresh = 1;
	} else {
		if (memcmp(&iface->hw_address, &imsg_ifinfo->hw_address,
		    sizeof(struct ether_addr)) != 0) {
			memcpy(&iface->hw_address, &imsg_ifinfo->hw_address,
			    sizeof(struct ether_addr));
			need_refresh = 1;
		}
		if (imsg_ifinfo->rdomain != iface->rdomain) {
			iface->rdomain = imsg_ifinfo->rdomain;
			need_refresh = 1;
		}
		if (imsg_ifinfo->running != iface->running) {
			iface->running = imsg_ifinfo->running;
			need_refresh = 1;
		}

		if (imsg_ifinfo->link_state != iface->link_state) {
			iface->link_state = imsg_ifinfo->link_state;
			need_refresh = 1;
		}
	}

	if (!need_refresh)
		return;

	if (iface->running && LINK_STATE_IS_UP(iface->link_state)) {
		if (iface->requested_ip.s_addr == INADDR_ANY)
			parse_lease(iface, imsg_ifinfo);

		if (iface->requested_ip.s_addr == INADDR_ANY)
			state_transition(iface, IF_INIT);
		else
			state_transition(iface, IF_REBOOTING);
	} else
		state_transition(iface, IF_DOWN);
}
struct dhcpleased_iface*
get_dhcpleased_iface_by_id(uint32_t if_index)
{
	struct dhcpleased_iface	*iface;
	LIST_FOREACH (iface, &dhcpleased_interfaces, entries) {
		if (iface->if_index == if_index)
			return (iface);
	}

	return (NULL);
}

void
remove_dhcpleased_iface(uint32_t if_index)
{
	struct dhcpleased_iface	*iface;

	iface = get_dhcpleased_iface_by_id(if_index);

	if (iface == NULL)
		return;

	send_rdns_withdraw(iface);
	send_deconfigure_interface(iface);
	LIST_REMOVE(iface, entries);
	evtimer_del(&iface->timer);
	free(iface);
}

void
parse_dhcp(struct dhcpleased_iface *iface, struct imsg_dhcp *dhcp)
{
	static uint8_t		 cookie[] = DHCP_COOKIE;
	static struct ether_addr bcast_mac;
#ifndef SMALL
	struct iface_conf	*iface_conf;
#endif /* SMALL */
	struct ether_header	*eh;
	struct ether_addr	 ether_src, ether_dst;
	struct ip		*ip;
	struct udphdr		*udp;
	struct dhcp_hdr		*dhcp_hdr;
	struct in_addr		 server_identifier, subnet_mask;
	struct in_addr		 nameservers[MAX_RDNS_COUNT];
	struct dhcp_route	 routes[MAX_DHCP_ROUTES];
	size_t			 rem, i;
	uint32_t		 lease_time = 0, renewal_time = 0;
	uint32_t		 rebinding_time = 0;
	uint32_t		 ipv6_only_time = 0;
	uint8_t			*p, dho = DHO_PAD, dho_len, slen;
	uint8_t			 dhcp_message_type = 0;
	int			 routes_len = 0, routers = 0, csr = 0;
	char			 from[sizeof("xx:xx:xx:xx:xx:xx")];
	char			 to[sizeof("xx:xx:xx:xx:xx:xx")];
	char			 hbuf_src[INET_ADDRSTRLEN];
	char			 hbuf_dst[INET_ADDRSTRLEN];
	char			 hbuf[INET_ADDRSTRLEN];
	char			 domainname[4 * 255 + 1];
	char			 hostname[4 * 255 + 1];

	if (bcast_mac.ether_addr_octet[0] == 0)
		memset(bcast_mac.ether_addr_octet, 0xff, ETHER_ADDR_LEN);

#ifndef SMALL
	iface_conf = find_iface_conf(&engine_conf->iface_list, iface->if_name);
#endif /* SMALL*/

	memset(hbuf_src, 0, sizeof(hbuf_src));
	memset(hbuf_dst, 0, sizeof(hbuf_dst));

	p = dhcp->packet;
	rem = dhcp->len;

	if (rem < sizeof(*eh)) {
		log_warnx("%s: message too short", __func__);
		return;
	}
	eh = (struct ether_header *)p;
	memcpy(ether_src.ether_addr_octet, eh->ether_shost,
	    sizeof(ether_src.ether_addr_octet));
	strlcpy(from, ether_ntoa(&ether_src), sizeof(from));
	memcpy(ether_dst.ether_addr_octet, eh->ether_dhost,
	    sizeof(ether_dst.ether_addr_octet));
	strlcpy(to, ether_ntoa(&ether_dst), sizeof(to));
	p += sizeof(*eh);
	rem -= sizeof(*eh);

	if (memcmp(&ether_dst, &iface->hw_address, sizeof(ether_dst)) != 0 &&
	    memcmp(&ether_dst, &bcast_mac, sizeof(ether_dst)) != 0)
		return ; /* silently ignore packet not for us */

	if (rem < sizeof(*ip))
		goto too_short;

	if (log_getverbose() > 1)
		log_debug("%s, from: %s, to: %s", __func__, from, to);

	ip = (struct ip *)p;

	if (rem < (size_t)ip->ip_hl << 2)
		goto too_short;

	if ((dhcp->csumflags & M_IPV4_CSUM_IN_OK) == 0 &&
	    wrapsum(checksum((uint8_t *)ip, ip->ip_hl << 2, 0)) != 0) {
		log_warnx("%s: bad IP checksum", __func__);
		return;
	}
	if (rem < ntohs(ip->ip_len))
		goto too_short;

	p += ip->ip_hl << 2;
	rem -= ip->ip_hl << 2;

	if (inet_ntop(AF_INET, &ip->ip_src, hbuf_src, sizeof(hbuf_src)) == NULL)
		hbuf_src[0] = '\0';
	if (inet_ntop(AF_INET, &ip->ip_dst, hbuf_dst, sizeof(hbuf_dst)) == NULL)
		hbuf_dst[0] = '\0';

#ifndef SMALL
	if (iface_conf != NULL) {
		for (i = 0; (int)i < iface_conf->ignore_servers_len; i++) {
			if (iface_conf->ignore_servers[i].s_addr ==
			    ip->ip_src.s_addr) {
				log_debug("ignoring server %s", hbuf_src);
				return;
			}
		}
	}
#endif /* SMALL */

	if (rem < sizeof(*udp))
		goto too_short;

	udp = (struct udphdr *)p;
	if (rem < ntohs(udp->uh_ulen))
		goto too_short;

	if (rem > ntohs(udp->uh_ulen)) {
		if (log_getverbose() > 1) {
			log_debug("%s: accepting packet with %lu bytes of data"
			    " after udp payload", __func__, rem -
			    ntohs(udp->uh_ulen));
		}
		rem = ntohs(udp->uh_ulen);
	}

	p += sizeof(*udp);
	rem -= sizeof(*udp);

	if ((dhcp->csumflags & M_UDP_CSUM_IN_OK) == 0 &&
	    udp->uh_sum != 0) {
		udp->uh_sum = wrapsum(checksum((uint8_t *)udp, sizeof(*udp),
		    checksum(p, rem,
		    checksum((uint8_t *)&ip->ip_src, 2 * sizeof(ip->ip_src),
		    IPPROTO_UDP + ntohs(udp->uh_ulen)))));

		if (udp->uh_sum != 0) {
			log_warnx("%s: bad UDP checksum", __func__);
			return;
		}
	}

	if (log_getverbose() > 1) {
		log_debug("%s: %s:%d -> %s:%d", __func__, hbuf_src,
		    ntohs(udp->uh_sport), hbuf_dst, ntohs(udp->uh_dport));
	}

	if (rem < sizeof(*dhcp_hdr))
		goto too_short;

	dhcp_hdr = (struct dhcp_hdr *)p;
	p += sizeof(*dhcp_hdr);
	rem -= sizeof(*dhcp_hdr);

	dhcp_hdr->sname[DHCP_SNAME_LEN -1 ] = '\0'; /* ensure it's a string */
	dhcp_hdr->file[DHCP_FILE_LEN -1 ] = '\0'; /* ensure it's a string */

	if (log_getverbose() > 1)
		log_dhcp_hdr(dhcp_hdr);

	if (dhcp_hdr->op != DHCP_BOOTREPLY) {
		log_warnx("%s: ignoring non-reply packet", __func__);
		return;
	}

	if (ntohl(dhcp_hdr->xid) != iface->xid)
		return; /* silently ignore wrong xid */

	if (rem < sizeof(cookie))
		goto too_short;

	if (memcmp(p, cookie, sizeof(cookie)) != 0) {
		log_warnx("%s: no dhcp cookie in packet from %s", __func__,
		    from);
		return;
	}
	p += sizeof(cookie);
	rem -= sizeof(cookie);

	memset(&server_identifier, 0, sizeof(server_identifier));
	memset(&subnet_mask, 0, sizeof(subnet_mask));
	memset(&routes, 0, sizeof(routes));
	memset(&nameservers, 0, sizeof(nameservers));
	memset(hostname, 0, sizeof(hostname));
	memset(domainname, 0, sizeof(domainname));

	while (rem > 0 && dho != DHO_END) {
		dho = *p;
		p += 1;
		rem -= 1;
		/* only DHO_END and DHO_PAD are 1 byte long without length */
		if (dho == DHO_PAD || dho == DHO_END)
			dho_len = 0;
		else {
			if (rem == 0)
				goto too_short; /* missing option length */
			dho_len = *p;
			p += 1;
			rem -= 1;
			if (rem < dho_len)
				goto too_short;
		}

		switch (dho) {
		case DHO_PAD:
			if (log_getverbose() > 1)
				log_debug("DHO_PAD");
			break;
		case DHO_END:
			if (log_getverbose() > 1)
				log_debug("DHO_END");
			break;
		case DHO_DHCP_MESSAGE_TYPE:
			if (dho_len != 1)
				goto wrong_length;
			dhcp_message_type = *p;
			if (log_getverbose() > 1) {
				log_debug("DHO_DHCP_MESSAGE_TYPE: %s",
				    dhcp_message_type2str(dhcp_message_type));
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_DHCP_SERVER_IDENTIFIER:
			if (dho_len != sizeof(server_identifier))
				goto wrong_length;
			memcpy(&server_identifier, p,
			    sizeof(server_identifier));
			if (log_getverbose() > 1) {
				log_debug("DHO_DHCP_SERVER_IDENTIFIER: %s",
				    inet_ntop(AF_INET, &server_identifier,
				    hbuf, sizeof(hbuf)));
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_DHCP_LEASE_TIME:
			if (dho_len != sizeof(lease_time))
				goto wrong_length;
			memcpy(&lease_time, p, sizeof(lease_time));
			lease_time = ntohl(lease_time);
			if (log_getverbose() > 1) {
				log_debug("DHO_DHCP_LEASE_TIME %us",
				    lease_time);
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_SUBNET_MASK:
			if (dho_len != sizeof(subnet_mask))
				goto wrong_length;
			memcpy(&subnet_mask, p, sizeof(subnet_mask));
			if (log_getverbose() > 1) {
				log_debug("DHO_SUBNET_MASK: %s",
				    inet_ntop(AF_INET, &subnet_mask, hbuf,
				    sizeof(hbuf)));
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_ROUTERS:
			if (dho_len < sizeof(routes[routes_len].gw))
				goto wrong_length;
			if (dho_len % sizeof(routes[routes_len].gw) != 0)
				goto wrong_length;

			/*
			 * Ignore routers option if classless static routes
			 * are present (RFC3442).
			 */
			if (!csr) {
				routers = 1;
				while (routes_len < MAX_DHCP_ROUTES &&
				    dho_len > 0) {
					memcpy(&routes[routes_len].gw, p,
					    sizeof(routes[routes_len].gw));
					if (log_getverbose() > 1) {
						log_debug("DHO_ROUTER: %s",
						    inet_ntop(AF_INET,
						    &routes[routes_len].gw,
						    hbuf, sizeof(hbuf)));
					}
					p += sizeof(routes[routes_len].gw);
					rem -= sizeof(routes[routes_len].gw);
					dho_len -=
					    sizeof(routes[routes_len].gw);
					routes_len++;
				}
			}
			if (dho_len != 0) {
				/* ignore > MAX_DHCP_ROUTES routes */
				p += dho_len;
				rem -= dho_len;
			}
			break;
		case DHO_DOMAIN_NAME_SERVERS:
			if (dho_len < sizeof(nameservers[0]))
				goto wrong_length;
			if (dho_len % sizeof(nameservers[0]) != 0)
				goto wrong_length;
			/* we limit ourself to 8 nameservers for proposals */
			memcpy(&nameservers, p, MINIMUM(sizeof(nameservers),
			    dho_len));
			if (log_getverbose() > 1) {
				for (i = 0; i < MINIMUM(sizeof(nameservers),
				    dho_len / sizeof(nameservers[0])); i++) {
					log_debug("DHO_DOMAIN_NAME_SERVERS: %s "
					    "(%lu/%lu)", inet_ntop(AF_INET,
					    &nameservers[i], hbuf,
					    sizeof(hbuf)), i + 1,
					    dho_len / sizeof(nameservers[0]));
				}
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_HOST_NAME:
			if (dho_len < 1) {
				/*
				 * Protocol violation: minimum length is 1;
				 * pretend the option is not there
				 */
				break;
			}
			/* MUST delete trailing NUL, per RFC 2132 */
			slen = dho_len;
			while (slen > 0 && p[slen - 1] == '\0')
				slen--;
			/* slen might be 0 here, pretend option is not there. */
			strvisx(hostname, p, slen, VIS_SAFE);
			if (log_getverbose() > 1)
				log_debug("DHO_HOST_NAME: %s", hostname);
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_DOMAIN_NAME:
			if (dho_len < 1) {
				/*
				 * Protocol violation: minimum length is 1;
				 * pretend the option is not there
				 */
				break;
			}
			/* MUST delete trailing NUL, per RFC 2132 */
			slen = dho_len;
			while (slen > 0 && p[slen - 1] == '\0')
				slen--;
			/* slen might be 0 here, pretend option is not there. */
			strvisx(domainname, p, slen, VIS_SAFE);
			if (log_getverbose() > 1)
				log_debug("DHO_DOMAIN_NAME: %s", domainname);
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_DHCP_RENEWAL_TIME:
			if (dho_len != sizeof(renewal_time))
				goto wrong_length;
			memcpy(&renewal_time, p, sizeof(renewal_time));
			renewal_time = ntohl(renewal_time);
			if (log_getverbose() > 1) {
				log_debug("DHO_DHCP_RENEWAL_TIME %us",
				    renewal_time);
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_DHCP_REBINDING_TIME:
			if (dho_len != sizeof(rebinding_time))
				goto wrong_length;
			memcpy(&rebinding_time, p, sizeof(rebinding_time));
			rebinding_time = ntohl(rebinding_time);
			if (log_getverbose() > 1) {
				log_debug("DHO_DHCP_REBINDING_TIME %us",
				    rebinding_time);
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_DHCP_CLIENT_IDENTIFIER:
			/*
			 * The server is supposed to echo this back to us
			 * (RFC6841), but of course they don't.
			 */
#ifndef SMALL
			if (iface_conf != NULL && iface_conf->c_id_len > 0) {
				if (dho_len != iface_conf->c_id[1]) {
					log_warnx("wrong "
					    "DHO_DHCP_CLIENT_IDENTIFIER");
				} else if (memcmp(p, &iface_conf->c_id[2],
				    dho_len) != 0) {
					log_warnx("wrong "
					    "DHO_DHCP_CLIENT_IDENTIFIER");
				}
			} else
#endif /* SMALL */
			{
				if (dho_len != 1 + sizeof(iface->hw_address))
					goto wrong_length;
				if (*p != HTYPE_ETHER) {
					log_warnx("DHO_DHCP_CLIENT_IDENTIFIER: "
					    "wrong type");
				}
				if (memcmp(p + 1, &iface->hw_address,
				    sizeof(iface->hw_address)) != 0) {
					log_warnx("wrong "
					    "DHO_DHCP_CLIENT_IDENTIFIER");
				}
			}
			p += dho_len;
			rem -= dho_len;
			break;
		case DHO_CLASSLESS_STATIC_ROUTES: {
			int	prefixlen, compressed_prefixlen;

			csr = 1;
			if (routers) {
				/*
				 * Ignore routers option if classless static
				 * routes are present (RFC3442).
				 */
				routers = 0;
				routes_len = 0;
			}
			while (routes_len < MAX_DHCP_ROUTES && dho_len > 0) {
				prefixlen = *p;
				p += 1;
				rem -= 1;
				dho_len -= 1;

				if (prefixlen < 0 || prefixlen > 32) {
					log_warnx("%s: invalid prefixlen: %d",
					    __func__, prefixlen);
					return;
				}

				if (prefixlen > 0)
					routes[routes_len].mask.s_addr =
					    htonl(0xffffffff << (32 -
						prefixlen));
				else
					routes[routes_len].mask.s_addr =
					    INADDR_ANY;

				compressed_prefixlen = (prefixlen + 7) / 8;
				if (dho_len < compressed_prefixlen)
					goto wrong_length;

				memcpy(&routes[routes_len].dst, p,
				    compressed_prefixlen);
				p += compressed_prefixlen;
				rem -= compressed_prefixlen;
				dho_len -= compressed_prefixlen;

				if (dho_len < sizeof(routes[routes_len].gw))
					goto wrong_length;

				memcpy(&routes[routes_len].gw, p,
				    sizeof(routes[routes_len].gw));
				p += sizeof(routes[routes_len].gw);
				rem -= sizeof(routes[routes_len].gw);
				dho_len -= sizeof(routes[routes_len].gw);

				routes_len++;
			}

			if (dho_len != 0) {
				/* ignore > MAX_DHCP_ROUTES routes */
				p += dho_len;
				rem -= dho_len;
			}
			break;
		}
		case DHO_IPV6_ONLY_PREFERRED:
			if (dho_len != sizeof(ipv6_only_time))
				goto wrong_length;
			memcpy(&ipv6_only_time, p, sizeof(ipv6_only_time));
			ipv6_only_time = ntohl(ipv6_only_time);
			if (log_getverbose() > 1) {
				log_debug("DHO_IPV6_ONLY_PREFERRED %us",
				    ipv6_only_time);
			}
			p += dho_len;
			rem -= dho_len;
			break;
		default:
			if (log_getverbose() > 1)
				log_debug("DHO_%u, len: %u", dho, dho_len);
			p += dho_len;
			rem -= dho_len;
		}

	}
	while (rem != 0) {
		if (*p != DHO_PAD)
			break;
		p++;
		rem--;
	}
	if (rem != 0)
		log_debug("%s: %lu bytes garbage data from %s", __func__, rem,
		    from);

	log_debug("%s on %s from %s/%s to %s/%s",
	    dhcp_message_type2str(dhcp_message_type), iface->if_name, from,
	    hbuf_src, to, hbuf_dst);

	switch (dhcp_message_type) {
	case DHCPOFFER:
		if (iface->state != IF_INIT) {
			log_debug("ignoring unexpected DHCPOFFER");
			return;
		}
		if (server_identifier.s_addr == INADDR_ANY &&
		    dhcp_hdr->yiaddr.s_addr == INADDR_ANY) {
			log_warnx("%s: did not receive server identifier or "
			    "offered IP address", __func__);
			return;
		}
#ifndef SMALL
		if (iface_conf != NULL && iface_conf->prefer_ipv6 &&
		    ipv6_only_time > 0) {
			iface->ipv6_only_time = ipv6_only_time;
			state_transition(iface, IF_IPV6_ONLY);
			break;
		}
#endif
		iface->server_identifier = server_identifier;
		iface->dhcp_server = server_identifier;
		iface->requested_ip = dhcp_hdr->yiaddr;
		state_transition(iface, IF_REQUESTING);
		break;
	case DHCPACK:
		switch (iface->state) {
		case IF_REQUESTING:
		case IF_RENEWING:
		case IF_REBINDING:
		case IF_REBOOTING:
			break;
		default:
			log_debug("ignoring unexpected DHCPACK");
			return;
		}
		if (server_identifier.s_addr == INADDR_ANY &&
		    dhcp_hdr->yiaddr.s_addr == INADDR_ANY) {
			log_warnx("%s: did not receive server identifier or "
			    "offered IP address", __func__);
			return;
		}
		if (lease_time == 0) {
			log_warnx("%s no lease time from %s", __func__, from);
			return;
		}
		if (subnet_mask.s_addr == INADDR_ANY) {
			log_warnx("%s: no subnetmask received from %s",
			    __func__, from);
			return;
		}

		/* Defaults if we didn't receive renewal or rebinding time. */
		if (renewal_time == 0)
			renewal_time = lease_time / 2;
		if (rebinding_time == 0)
			rebinding_time = lease_time - (lease_time / 8);

		/* RFC 2131 4.4.5 */
		/* Ignore invalid T1/T2 options */
		if (renewal_time >= rebinding_time) {
			log_warnx("%s: renewal_time(%u) >= rebinding_time(%u) "
			    "from %s: using defaults",
			    __func__, renewal_time, rebinding_time, from);
			renewal_time = rebinding_time = 0;
		} else if (rebinding_time >= lease_time) {
			log_warnx("%s: rebinding_time(%u) >= lease_time(%u) "
			    "from %s: using defaults",
			    __func__, rebinding_time, lease_time, from);
			renewal_time = rebinding_time = 0;
		}

		/* Defaults if we received wrong renewal or rebinding time. */
		if (renewal_time == 0)
			renewal_time = lease_time / 2;
		if (rebinding_time == 0)
			rebinding_time = lease_time - (lease_time / 8);

		clock_gettime(CLOCK_MONOTONIC, &iface->request_time);
		iface->server_identifier = server_identifier;
		iface->dhcp_server = server_identifier;
		iface->requested_ip = dhcp_hdr->yiaddr;
		iface->mask = subnet_mask;
#ifndef SMALL
		if (iface_conf != NULL && iface_conf->ignore & IGN_ROUTES) {
			iface->routes_len = 0;
			memset(iface->routes, 0, sizeof(iface->routes));
		} else
#endif /* SMALL */
		{
			iface->prev_routes_len = iface->routes_len;
			memcpy(iface->prev_routes, iface->routes,
			    sizeof(iface->prev_routes));
			iface->routes_len = routes_len;
			memcpy(iface->routes, routes, sizeof(iface->routes));
		}
		iface->lease_time = lease_time;
		iface->renewal_time = renewal_time;
		iface->rebinding_time = rebinding_time;

#ifndef SMALL
		if (iface_conf != NULL && iface_conf->ignore & IGN_DNS) {
			memset(iface->nameservers, 0,
			    sizeof(iface->nameservers));
		} else
#endif /* SMALL */
		{
			memcpy(iface->nameservers, nameservers,
			    sizeof(iface->nameservers));
		}

		iface->siaddr = dhcp_hdr->siaddr;

		/* we made sure this is a string futher up */
		strnvis(iface->file, dhcp_hdr->file, sizeof(iface->file),
		    VIS_SAFE);

		strlcpy(iface->domainname, domainname,
		    sizeof(iface->domainname));
		strlcpy(iface->hostname, hostname, sizeof(iface->hostname));
#ifndef SMALL
		if (iface_conf != NULL && iface_conf->prefer_ipv6 &&
		    ipv6_only_time > 0) {
			iface->ipv6_only_time = ipv6_only_time;
			state_transition(iface, IF_IPV6_ONLY);
			break;
		}
#endif
		state_transition(iface, IF_BOUND);
		break;
	case DHCPNAK:
		switch (iface->state) {
		case IF_REQUESTING:
		case IF_RENEWING:
		case IF_REBINDING:
		case IF_REBOOTING:
			break;
		default:
			log_debug("ignoring unexpected DHCPNAK");
			return;
		}

		state_transition(iface, IF_INIT);
		break;
	default:
		log_warnx("%s: unimplemented message type %d", __func__,
		    dhcp_message_type);
		break;
	}
	return;
 too_short:
	log_warnx("%s: message from %s too short", __func__, from);
	return;
 wrong_length:
	log_warnx("%s: received option %d with wrong length: %d", __func__,
	    dho, dho_len);
	return;
}

/* XXX check valid transitions */
void
state_transition(struct dhcpleased_iface *iface, enum if_state new_state)
{
	enum if_state	 old_state = iface->state;
	struct timespec	 now, res;

	iface->state = new_state;

	switch (new_state) {
	case IF_DOWN:
		if (iface->requested_ip.s_addr == INADDR_ANY) {
			/* nothing to do until iface comes up */
			iface->timo.tv_sec = -1;
			break;
		}
		if (old_state == IF_DOWN) {
			/* nameservers already withdrawn when if went down */
			send_deconfigure_interface(iface);
			/* nothing more to do until iface comes back */
			iface->timo.tv_sec = -1;
		} else {
			send_rdns_withdraw(iface);
			clock_gettime(CLOCK_MONOTONIC, &now);
			timespecsub(&now, &iface->request_time, &res);
			iface->timo.tv_sec = iface->lease_time - res.tv_sec;
			if (iface->timo.tv_sec < 0)
				iface->timo.tv_sec = 0; /* deconfigure now */
		}
		break;
	case IF_INIT:
		switch (old_state) {
		case IF_INIT:
			if (iface->timo.tv_sec < MAX_EXP_BACKOFF_SLOW)
				iface->timo.tv_sec *= 2;
			break;
		case IF_REQUESTING:
		case IF_RENEWING:
		case IF_REBINDING:
		case IF_REBOOTING:
			/* lease expired, got DHCPNAK or timeout: delete IP */
			send_rdns_withdraw(iface);
			send_deconfigure_interface(iface);
			/* fall through */
		case IF_DOWN:
		case IF_IPV6_ONLY:
			iface->timo.tv_sec = START_EXP_BACKOFF;
			iface->xid = arc4random();
			break;
		case IF_BOUND:
			fatalx("invalid transition Bound -> Init");
			break;
		}
		request_dhcp_discover(iface);
		break;
	case IF_REBOOTING:
		if (old_state == IF_REBOOTING)
			iface->timo.tv_sec *= 2;
		else {
			iface->timo.tv_sec = START_EXP_BACKOFF;
			iface->xid = arc4random();
		}
		request_dhcp_request(iface);
		break;
	case IF_REQUESTING:
		if (old_state == IF_REQUESTING)
			iface->timo.tv_sec *= 2;
		else
			iface->timo.tv_sec = START_EXP_BACKOFF;
		request_dhcp_request(iface);
		break;
	case IF_BOUND:
		iface->timo.tv_sec = iface->renewal_time;
		if (old_state == IF_REQUESTING || old_state == IF_REBOOTING) {
			send_configure_interface(iface);
			send_rdns_proposal(iface);
		}
		break;
	case IF_RENEWING:
		if (old_state == IF_BOUND) {
			iface->timo.tv_sec = (iface->rebinding_time -
			    iface->renewal_time) / 2; /* RFC 2131 4.4.5 */
			iface->xid = arc4random();
		} else
			iface->timo.tv_sec /= 2;

		if (iface->timo.tv_sec < 60)
			iface->timo.tv_sec = 60;
		request_dhcp_request(iface);
		break;
	case IF_REBINDING:
		if (old_state == IF_RENEWING) {
			iface->timo.tv_sec = (iface->lease_time -
			    iface->rebinding_time) / 2; /* RFC 2131 4.4.5 */
		} else
			iface->timo.tv_sec /= 2;
		request_dhcp_request(iface);
		break;
	case IF_IPV6_ONLY:
		switch (old_state) {
		case IF_REQUESTING:
		case IF_RENEWING:
		case IF_REBINDING:
		case IF_REBOOTING:
			/* going IPv6 only: delete legacy IP */
			send_rdns_withdraw(iface);
			send_deconfigure_interface(iface);
			/* fall through */
		case IF_INIT:
		case IF_DOWN:
		case IF_IPV6_ONLY:
			iface->timo.tv_sec = iface->ipv6_only_time;
			break;
		case IF_BOUND:
			fatalx("invalid transition Bound -> IPv6 only");
			break;
		}
	}

	log_debug("%s[%s] %s -> %s, timo: %lld", __func__, iface->if_name,
	    if_state_name[old_state], if_state_name[new_state],
	    iface->timo.tv_sec);

	if (iface->timo.tv_sec == -1) {
		if (evtimer_pending(&iface->timer, NULL))
			evtimer_del(&iface->timer);
	} else
		evtimer_add(&iface->timer, &iface->timo);
}

void
iface_timeout(int fd, short events, void *arg)
{
	struct dhcpleased_iface	*iface = (struct dhcpleased_iface *)arg;
	struct timespec		 now, res;

	log_debug("%s[%d]: %s", __func__, iface->if_index,
	    if_state_name[iface->state]);

	switch (iface->state) {
	case IF_DOWN:
		state_transition(iface, IF_DOWN);
		break;
	case IF_INIT:
		state_transition(iface, IF_INIT);
		break;
	case IF_REBOOTING:
		if (iface->timo.tv_sec >= MAX_EXP_BACKOFF_FAST)
			state_transition(iface, IF_INIT);
		else
			state_transition(iface, IF_REBOOTING);
		break;
	case IF_REQUESTING:
		if (iface->timo.tv_sec >= MAX_EXP_BACKOFF_SLOW)
			state_transition(iface, IF_INIT);
		else
			state_transition(iface, IF_REQUESTING);
		break;
	case IF_BOUND:
		state_transition(iface, IF_RENEWING);
		break;
	case IF_RENEWING:
		clock_gettime(CLOCK_MONOTONIC, &now);
		timespecsub(&now, &iface->request_time, &res);
		log_debug("%s: res.tv_sec: %lld, rebinding_time: %u", __func__,
		    res.tv_sec, iface->rebinding_time);
		if (res.tv_sec >= iface->rebinding_time)
			state_transition(iface, IF_REBINDING);
		else
			state_transition(iface, IF_RENEWING);
		break;
	case IF_REBINDING:
		clock_gettime(CLOCK_MONOTONIC, &now);
		timespecsub(&now, &iface->request_time, &res);
		log_debug("%s: res.tv_sec: %lld, lease_time: %u", __func__,
		    res.tv_sec, iface->lease_time);
		if (res.tv_sec > iface->lease_time)
			state_transition(iface, IF_INIT);
		else
			state_transition(iface, IF_REBINDING);
		break;
	case IF_IPV6_ONLY:
		state_transition(iface, IF_REQUESTING);
		break;
	}
}

void
request_dhcp_discover(struct dhcpleased_iface *iface)
{
	struct imsg_req_dhcp	 imsg;

	memset(&imsg, 0, sizeof(imsg));

	imsg.if_index = iface->if_index;
	imsg.xid = iface->xid;

	/*
	 * similar to RFC 2131 4.3.6, Table 4 for DHCPDISCOVER
	 * ------------------------------
	 * |              | INIT         |
	 * ------------------------------
	 * |broad/unicast | broadcast    |
	 * |server-ip     | MUST NOT     |
	 * |requested-ip  | MAY          |
	 * |ciaddr        | zero         |
	 * ------------------------------
	 *
	 * Leaving everything at 0 from the memset results in this table with
	 * requested-ip not set.
	*/

	engine_imsg_compose_frontend(IMSG_SEND_DISCOVER, 0, &imsg, sizeof(imsg));
}

void
request_dhcp_request(struct dhcpleased_iface *iface)
{
	struct imsg_req_dhcp	 imsg;

	imsg.if_index = iface->if_index;
	imsg.xid = iface->xid;

	/*
	 * RFC 2131 4.3.6, Table 4
	 * ---------------------------------------------------------------------
	 * |              |REBOOTING    |REQUESTING   |RENEWING     |REBINDING |
	 * ---------------------------------------------------------------------
	 * |broad/unicast |broadcast    |broadcast    |unicast      |broadcast |
	 * |server-ip     |MUST NOT     |MUST         |MUST NOT     |MUST NOT  |
	 * |requested-ip  |MUST         |MUST         |MUST NOT     |MUST NOT  |
	 * |ciaddr        |zero         |zero         |IP address   |IP address|
	 * ---------------------------------------------------------------------
	*/
	switch (iface->state) {
	case IF_DOWN:
		fatalx("invalid state IF_DOWN in %s", __func__);
		break;
	case IF_INIT:
		fatalx("invalid state IF_INIT in %s", __func__);
		break;
	case IF_BOUND:
		fatalx("invalid state IF_BOUND in %s", __func__);
		break;
	case IF_REBOOTING:
		imsg.dhcp_server.s_addr = INADDR_ANY;		/* broadcast */
		imsg.server_identifier.s_addr = INADDR_ANY;	/* MUST NOT */
		imsg.requested_ip = iface->requested_ip;	/* MUST */
		imsg.ciaddr.s_addr = INADDR_ANY;		/* zero */
		break;
	case IF_REQUESTING:
		imsg.dhcp_server.s_addr = INADDR_ANY;		/* broadcast */
		imsg.server_identifier =
		    iface->server_identifier;			/* MUST */
		imsg.requested_ip = iface->requested_ip;	/* MUST */
		imsg.ciaddr.s_addr = INADDR_ANY;		/* zero */
		break;
	case IF_RENEWING:
		imsg.dhcp_server = iface->dhcp_server;		/* unicast */
		imsg.server_identifier.s_addr = INADDR_ANY;	/* MUST NOT */
		imsg.requested_ip.s_addr = INADDR_ANY;		/* MUST NOT */
		imsg.ciaddr = iface->requested_ip;		/* IP address */
		break;
	case IF_REBINDING:
		imsg.dhcp_server.s_addr = INADDR_ANY;		/* broadcast */
		imsg.server_identifier.s_addr = INADDR_ANY;	/* MUST NOT */
		imsg.requested_ip.s_addr = INADDR_ANY;		/* MUST NOT */
		imsg.ciaddr = iface->requested_ip;		/* IP address */
		break;
	case IF_IPV6_ONLY:
		fatalx("invalid state IF_IPV6_ONLY in %s", __func__);
		break;
	}

	engine_imsg_compose_frontend(IMSG_SEND_REQUEST, 0, &imsg, sizeof(imsg));
}

void
log_lease(struct dhcpleased_iface *iface, int deconfigure)
{
	char	 hbuf_lease[INET_ADDRSTRLEN], hbuf_server[INET_ADDRSTRLEN];

	inet_ntop(AF_INET, &iface->requested_ip, hbuf_lease,
	    sizeof(hbuf_lease));
	inet_ntop(AF_INET, &iface->server_identifier, hbuf_server,
	    sizeof(hbuf_server));


	if (deconfigure)
		log_info("deleting %s from %s (lease from %s)", hbuf_lease,
		    iface->if_name, hbuf_server);
	else
		log_info("adding %s to %s (lease from %s)", hbuf_lease,
		    iface->if_name, hbuf_server);
}

void
send_configure_interface(struct dhcpleased_iface *iface)
{
	struct imsg_configure_interface	 imsg;
	int				 i, j, found;

	log_lease(iface, 0);

	memset(&imsg, 0, sizeof(imsg));
	imsg.if_index = iface->if_index;
	imsg.rdomain = iface->rdomain;
	imsg.addr = iface->requested_ip;
	imsg.mask = iface->mask;
	imsg.siaddr = iface->siaddr;
	strlcpy(imsg.file, iface->file, sizeof(imsg.file));
	strlcpy(imsg.domainname, iface->domainname, sizeof(imsg.domainname));
	strlcpy(imsg.hostname, iface->hostname, sizeof(imsg.hostname));
	for (i = 0; i < iface->prev_routes_len; i++) {
		found = 0;
		for (j = 0; j < iface->routes_len; j++) {
			if (memcmp(&iface->prev_routes[i], &iface->routes[j],
			    sizeof(struct dhcp_route)) == 0) {
				found = 1;
				break;
			}
		}
		if (!found)
			imsg.routes[imsg.routes_len++] = iface->prev_routes[i];
	}
	if (imsg.routes_len > 0)
		engine_imsg_compose_main(IMSG_WITHDRAW_ROUTES, 0, &imsg,
		    sizeof(imsg));
	imsg.routes_len = iface->routes_len;
	memcpy(imsg.routes, iface->routes, sizeof(imsg.routes));
	engine_imsg_compose_main(IMSG_CONFIGURE_INTERFACE, 0, &imsg,
	    sizeof(imsg));
}

void
send_deconfigure_interface(struct dhcpleased_iface *iface)
{
	struct imsg_configure_interface	 imsg;

	if (iface->requested_ip.s_addr == INADDR_ANY)
		return;

	log_lease(iface, 1);

	memset(&imsg, 0, sizeof(imsg));
	imsg.if_index = iface->if_index;
	imsg.rdomain = iface->rdomain;
	imsg.addr = iface->requested_ip;
	imsg.mask = iface->mask;
	imsg.siaddr = iface->siaddr;
	strlcpy(imsg.file, iface->file, sizeof(imsg.file));
	strlcpy(imsg.domainname, iface->domainname, sizeof(imsg.domainname));
	strlcpy(imsg.hostname, iface->hostname, sizeof(imsg.hostname));
	imsg.routes_len = iface->routes_len;
	memcpy(imsg.routes, iface->routes, sizeof(imsg.routes));
	engine_imsg_compose_main(IMSG_DECONFIGURE_INTERFACE, 0, &imsg,
	    sizeof(imsg));

	iface->server_identifier.s_addr = INADDR_ANY;
	iface->dhcp_server.s_addr = INADDR_ANY;
	iface->requested_ip.s_addr = INADDR_ANY;
	iface->mask.s_addr = INADDR_ANY;
	iface->routes_len = 0;
	memset(iface->routes, 0, sizeof(iface->routes));
}

void
send_routes_withdraw(struct dhcpleased_iface *iface)
{
	struct imsg_configure_interface	 imsg;

	if (iface->requested_ip.s_addr == INADDR_ANY || iface->routes_len == 0)
		return;

	imsg.if_index = iface->if_index;
	imsg.rdomain = iface->rdomain;
	imsg.addr = iface->requested_ip;
	imsg.mask = iface->mask;
	imsg.siaddr = iface->siaddr;
	strlcpy(imsg.file, iface->file, sizeof(imsg.file));
	strlcpy(imsg.domainname, iface->domainname, sizeof(imsg.domainname));
	strlcpy(imsg.hostname, iface->hostname, sizeof(imsg.hostname));
	imsg.routes_len = iface->routes_len;
	memcpy(imsg.routes, iface->routes, sizeof(imsg.routes));
	engine_imsg_compose_main(IMSG_WITHDRAW_ROUTES, 0, &imsg,
	    sizeof(imsg));
}

void
log_rdns(struct dhcpleased_iface *iface, int withdraw)
{
	int	 i;
	char	 hbuf_rdns[INET_ADDRSTRLEN], hbuf_server[INET_ADDRSTRLEN];
	char	*rdns_buf = NULL, *tmp_buf;

	inet_ntop(AF_INET, &iface->server_identifier, hbuf_server,
	    sizeof(hbuf_server));

	for (i = 0; i < MAX_RDNS_COUNT && iface->nameservers[i].s_addr !=
		 INADDR_ANY; i++) {
		inet_ntop(AF_INET, &iface->nameservers[i], hbuf_rdns,
		    sizeof(hbuf_rdns));
		tmp_buf = rdns_buf;
		if (asprintf(&rdns_buf, "%s %s", tmp_buf ? tmp_buf : "",
		    hbuf_rdns) < 0) {
			rdns_buf = NULL;
			break;
		}
		free(tmp_buf);
	}

	if (rdns_buf != NULL) {
		if (withdraw) {
			log_info("deleting nameservers%s (lease from %s on %s)",
			    rdns_buf, hbuf_server, iface->if_name);
		} else {
			log_info("adding nameservers%s (lease from %s on %s)",
			    rdns_buf, hbuf_server, iface->if_name);
		}
		free(rdns_buf);
	}
}

void
send_rdns_proposal(struct dhcpleased_iface *iface)
{
	struct imsg_propose_rdns	 imsg;

	log_rdns(iface, 0);

	memset(&imsg, 0, sizeof(imsg));

	imsg.if_index = iface->if_index;
	imsg.rdomain = iface->rdomain;
	for (imsg.rdns_count = 0; imsg.rdns_count < MAX_RDNS_COUNT &&
		 iface->nameservers[imsg.rdns_count].s_addr != INADDR_ANY;
	    imsg.rdns_count++)
		;
	memcpy(imsg.rdns, iface->nameservers, sizeof(imsg.rdns));
	engine_imsg_compose_main(IMSG_PROPOSE_RDNS, 0, &imsg, sizeof(imsg));
}

void
send_rdns_withdraw(struct dhcpleased_iface *iface)
{
	struct imsg_propose_rdns	 imsg;

	log_rdns(iface, 1);

	memset(&imsg, 0, sizeof(imsg));

	imsg.if_index = iface->if_index;
	imsg.rdomain = iface->rdomain;
	engine_imsg_compose_main(IMSG_WITHDRAW_RDNS, 0, &imsg, sizeof(imsg));
	memset(iface->nameservers, 0, sizeof(iface->nameservers));
}

void
parse_lease(struct dhcpleased_iface *iface, struct imsg_ifinfo *imsg_ifinfo)
{
	char	*p, *p1;

	iface->requested_ip.s_addr = INADDR_ANY;

	if ((p = strstr(imsg_ifinfo->lease, LEASE_IP_PREFIX)) == NULL)
		return;

	p += sizeof(LEASE_IP_PREFIX) - 1;
	if ((p1 = strchr(p, '\n')) == NULL)
		return;
	*p1 = '\0';

	if (inet_pton(AF_INET, p, &iface->requested_ip) != 1)
		iface->requested_ip.s_addr = INADDR_ANY;
}

void
log_dhcp_hdr(struct dhcp_hdr *dhcp_hdr)
{
#ifndef	SMALL
	char	 hbuf[INET_ADDRSTRLEN];

	log_debug("dhcp_hdr op: %s (%d)", dhcp_hdr->op == DHCP_BOOTREQUEST ?
	    "Boot Request" : dhcp_hdr->op == DHCP_BOOTREPLY ? "Boot Reply" :
	    "Unknown", dhcp_hdr->op);
	log_debug("dhcp_hdr htype: %s (%d)", dhcp_hdr->htype == 1 ? "Ethernet":
	    "Unknown", dhcp_hdr->htype);
	log_debug("dhcp_hdr hlen: %d", dhcp_hdr->hlen);
	log_debug("dhcp_hdr hops: %d", dhcp_hdr->hops);
	log_debug("dhcp_hdr xid: 0x%x", ntohl(dhcp_hdr->xid));
	log_debug("dhcp_hdr secs: %u", dhcp_hdr->secs);
	log_debug("dhcp_hdr flags: 0x%x", dhcp_hdr->flags);
	log_debug("dhcp_hdr ciaddr: %s", inet_ntop(AF_INET, &dhcp_hdr->ciaddr,
	    hbuf, sizeof(hbuf)));
	log_debug("dhcp_hdr yiaddr: %s", inet_ntop(AF_INET, &dhcp_hdr->yiaddr,
	    hbuf, sizeof(hbuf)));
	log_debug("dhcp_hdr siaddr: %s", inet_ntop(AF_INET, &dhcp_hdr->siaddr,
	    hbuf, sizeof(hbuf)));
	log_debug("dhcp_hdr giaddr: %s", inet_ntop(AF_INET, &dhcp_hdr->giaddr,
	    hbuf, sizeof(hbuf)));
	log_debug("dhcp_hdr chaddr: %02x:%02x:%02x:%02x:%02x:%02x "
	    "(%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x)",
	    dhcp_hdr->chaddr[0], dhcp_hdr->chaddr[1], dhcp_hdr->chaddr[2],
	    dhcp_hdr->chaddr[3], dhcp_hdr->chaddr[4], dhcp_hdr->chaddr[5],
	    dhcp_hdr->chaddr[6], dhcp_hdr->chaddr[7], dhcp_hdr->chaddr[8],
	    dhcp_hdr->chaddr[9], dhcp_hdr->chaddr[10], dhcp_hdr->chaddr[11],
	    dhcp_hdr->chaddr[12], dhcp_hdr->chaddr[13], dhcp_hdr->chaddr[14],
	    dhcp_hdr->chaddr[15]);
	/* ignore sname and file, if we ever print it use strvis(3) */
#endif
}

const char *
dhcp_message_type2str(uint8_t dhcp_message_type)
{
	switch (dhcp_message_type) {
	case DHCPDISCOVER:
		return "DHCPDISCOVER";
	case DHCPOFFER:
		return "DHCPOFFER";
	case DHCPREQUEST:
		return "DHCPREQUEST";
	case DHCPDECLINE:
		return "DHCPDECLINE";
	case DHCPACK:
		return "DHCPACK";
	case DHCPNAK:
		return "DHCPNAK";
	case DHCPRELEASE:
		return "DHCPRELEASE";
	case DHCPINFORM:
		return "DHCPINFORM";
	default:
		return "Unknown";
	}
}
