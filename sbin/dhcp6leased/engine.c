/*	$OpenBSD: engine.c,v 1.34 2025/09/18 11:49:23 florian Exp $	*/

/*
 * Copyright (c) 2017, 2021, 2024 Florian Obser <florian@openbsd.org>
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
#include <netinet/ip.h>

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

#include "log.h"
#include "dhcp6leased.h"
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

enum reconfigure_action {
	CONFIGURE,
	DECONFIGURE,
};

const char* reconfigure_action_name[] = {
	"configure",
	"deconfigure",
};

struct dhcp6leased_iface {
	LIST_ENTRY(dhcp6leased_iface)	 entries;
	enum if_state			 state;
	struct event			 timer;
	struct timeval			 timo;
	uint32_t			 if_index;
	int				 rdomain;
	int				 running;
	int				 link_state;
	uint8_t				 xid[XID_SIZE];
	int				 serverid_len;
	uint8_t				 serverid[SERVERID_SIZE];
	struct prefix			 pds[MAX_IA];
	struct prefix			 new_pds[MAX_IA];
	struct timespec			 request_time;
	struct timespec			 elapsed_time_start;
	uint32_t			 lease_time;
	uint32_t			 t1;
	uint32_t			 t2;
};

LIST_HEAD(, dhcp6leased_iface) dhcp6leased_interfaces;

__dead void		 engine_shutdown(void);
void			 engine_sig_handler(int sig, short, void *);
void			 engine_dispatch_frontend(int, short, void *);
void			 engine_dispatch_main(int, short, void *);
void			 send_interface_info(struct dhcp6leased_iface *, pid_t);
void			 engine_showinfo_ctl(struct imsg *, uint32_t);
void			 engine_update_iface(struct imsg_ifinfo *);
struct dhcp6leased_iface	*get_dhcp6leased_iface_by_id(uint32_t);
void			 remove_dhcp6leased_iface(uint32_t);
void			 parse_dhcp(struct dhcp6leased_iface *,
			     struct imsg_dhcp *);
int			 parse_ia_pd_options(uint8_t *, size_t, struct prefix *);
void			 state_transition(struct dhcp6leased_iface *, enum
			     if_state);
void			 iface_timeout(int, short, void *);
void			 request_dhcp_discover(struct dhcp6leased_iface *);
void			 request_dhcp_request(struct dhcp6leased_iface *);
void			 configure_interfaces(struct dhcp6leased_iface *);
void			 deconfigure_interfaces(struct dhcp6leased_iface *);
void			 deprecate_interfaces(struct dhcp6leased_iface *);
int			 prefixcmp(struct prefix *, struct prefix *, int);
void			 send_reconfigure_interface(struct iface_pd_conf *,
			     struct prefix *, enum reconfigure_action);
void			 send_reconfigure_reject_route(
			     struct dhcp6leased_iface *, struct in6_addr *,
			     uint8_t, enum reconfigure_action);
int			 engine_imsg_compose_main(int, pid_t, void *, uint16_t);
const char		*dhcp_option_type2str(int);
const char		*dhcp_duid2str(int, uint8_t *);
const char		*dhcp_status2str(int);
void			 in6_prefixlen2mask(struct in6_addr *, int len);

struct dhcp6leased_conf	*engine_conf;

static struct imsgev	*iev_frontend;
static struct imsgev	*iev_main;
int64_t			 proposal_id;
static struct dhcp_duid	 duid;

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

	engine_conf = config_new_empty();

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

	LIST_INIT(&dhcp6leased_interfaces);

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
	struct dhcp6leased_iface		*iface;
	ssize_t				 n;
	int				 shut = 0;
	int				 verbose;
	uint32_t			 if_index;

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
		case IMSG_CTL_LOG_VERBOSE:
			if (IMSG_DATA_SIZE(imsg) != sizeof(verbose))
				fatalx("%s: IMSG_CTL_LOG_VERBOSE wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_setverbose(verbose);
			break;
		case IMSG_CTL_SHOW_INTERFACE_INFO:
			if (IMSG_DATA_SIZE(imsg) != sizeof(if_index))
				fatalx("%s: IMSG_CTL_SHOW_INTERFACE_INFO wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&if_index, imsg.data, sizeof(if_index));
			engine_showinfo_ctl(&imsg, if_index);
			break;
		case IMSG_REQUEST_REBOOT:
			if (IMSG_DATA_SIZE(imsg) != sizeof(if_index))
				fatalx("%s: IMSG_CTL_SEND_DISCOVER wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&if_index, imsg.data, sizeof(if_index));
			iface = get_dhcp6leased_iface_by_id(if_index);
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
					state_transition(iface, IF_REBOOTING);
					break;
				}
			}
			break;
		case IMSG_REMOVE_IF:
			if (IMSG_DATA_SIZE(imsg) != sizeof(if_index))
				fatalx("%s: IMSG_REMOVE_IF wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&if_index, imsg.data, sizeof(if_index));
			remove_dhcp6leased_iface(if_index);
			break;
		case IMSG_DHCP: {
			struct imsg_dhcp	imsg_dhcp;
			if (IMSG_DATA_SIZE(imsg) != sizeof(imsg_dhcp))
				fatalx("%s: IMSG_DHCP wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_dhcp, imsg.data, sizeof(imsg_dhcp));
			iface = get_dhcp6leased_iface_by_id(imsg_dhcp.if_index);
			if (iface != NULL)
				parse_dhcp(iface, &imsg_dhcp);
			break;
		}
		default:
			log_debug("%s: unexpected imsg %d", __func__,
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
engine_dispatch_main(int fd, short event, void *bula)
{
	static struct dhcp6leased_conf	*nconf;
	static struct iface_conf	*iface_conf;
	static struct iface_ia_conf	*iface_ia_conf;
	struct iface_pd_conf		*iface_pd_conf;
	struct imsg			 imsg;
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	struct imsg_ifinfo		 imsg_ifinfo;
	ssize_t				 n;
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

		switch (imsg.hdr.type) {
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
		case IMSG_UUID:
			if (IMSG_DATA_SIZE(imsg) != sizeof(duid.uuid))
				fatalx("%s: IMSG_UUID wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			duid.type = htons(DUID_UUID_TYPE);
			memcpy(duid.uuid, imsg.data, sizeof(duid.uuid));
			break;
		case IMSG_UPDATE_IF:
			if (IMSG_DATA_SIZE(imsg) != sizeof(imsg_ifinfo))
				fatalx("%s: IMSG_UPDATE_IF wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_ifinfo, imsg.data, sizeof(imsg_ifinfo));
			engine_update_iface(&imsg_ifinfo);
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
			struct dhcp6leased_iface	*iface;
			int			*ifaces;
			int			 i, if_index;
			char			*if_name;
			char			 ifnamebuf[IF_NAMESIZE];

			if (nconf == NULL)
				fatalx("%s: IMSG_RECONF_END without "
				    "IMSG_RECONF_CONF", __func__);
			ifaces = changed_ifaces(engine_conf, nconf);
			merge_config(engine_conf, nconf);
			nconf = NULL;
			for (i = 0; ifaces[i] != 0; i++) {
				if_index = ifaces[i];
				if_name = if_indextoname(if_index, ifnamebuf);
				iface = get_dhcp6leased_iface_by_id(if_index);
				if (if_name == NULL || iface == NULL)
					continue;
				iface_conf = find_iface_conf(
				    &engine_conf->iface_list, if_name);
				if (iface_conf == NULL)
					continue;
			}
			free(ifaces);
			break;
		}
		default:
			log_debug("%s: unexpected imsg %d", __func__,
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
send_interface_info(struct dhcp6leased_iface *iface, pid_t pid)
{
	struct ctl_engine_info	 cei;

	memset(&cei, 0, sizeof(cei));
	cei.if_index = iface->if_index;
	cei.running = iface->running;
	cei.link_state = iface->link_state;
	strlcpy(cei.state, if_state_name[iface->state], sizeof(cei.state));
	memcpy(&cei.request_time, &iface->request_time,
	    sizeof(cei.request_time));
	cei.lease_time = iface->lease_time;
	cei.t1 = iface->t1;
	cei.t2 = iface->t2;
	memcpy(&cei.pds, &iface->pds, sizeof(cei.pds));
	engine_imsg_compose_frontend(IMSG_CTL_SHOW_INTERFACE_INFO, pid, &cei,
	    sizeof(cei));
}

void
engine_showinfo_ctl(struct imsg *imsg, uint32_t if_index)
{
	struct dhcp6leased_iface			*iface;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_INTERFACE_INFO:
		if ((iface = get_dhcp6leased_iface_by_id(if_index)) != NULL)
			send_interface_info(iface, imsg->hdr.pid);
		else
			engine_imsg_compose_frontend(IMSG_CTL_END,
			    imsg->hdr.pid, NULL, 0);
		break;
	default:
		log_debug("%s: error handling imsg", __func__);
		break;
	}
}

void
engine_update_iface(struct imsg_ifinfo *imsg_ifinfo)
{
	struct dhcp6leased_iface	*iface;
	struct iface_conf		*iface_conf;
	int				 need_refresh = 0;
	char				 ifnamebuf[IF_NAMESIZE], *if_name;

	iface = get_dhcp6leased_iface_by_id(imsg_ifinfo->if_index);

	if (iface == NULL) {
		if ((iface = calloc(1, sizeof(*iface))) == NULL)
			fatal("calloc");
		iface->state = IF_DOWN;
		arc4random_buf(iface->xid, sizeof(iface->xid));
		iface->timo.tv_usec = arc4random_uniform(1000000);
		evtimer_set(&iface->timer, iface_timeout, iface);
		iface->if_index = imsg_ifinfo->if_index;
		iface->rdomain = imsg_ifinfo->rdomain;
		iface->running = imsg_ifinfo->running;
		iface->link_state = imsg_ifinfo->link_state;
		LIST_INSERT_HEAD(&dhcp6leased_interfaces, iface, entries);
		need_refresh = 1;
	} else {
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

	if ((if_name = if_indextoname(iface->if_index, ifnamebuf)) == NULL) {
		log_debug("%s: unknown interface %d", __func__,
		    iface->if_index);
		return;
	}

	if ((iface_conf = find_iface_conf(&engine_conf->iface_list, if_name))
	    == NULL) {
		log_debug("%s: no interface configuration for %d", __func__,
		    iface->if_index);
		return;
	}

	if (iface->running && LINK_STATE_IS_UP(iface->link_state)) {
		uint32_t	 i;
		int		 got_lease;

		if (iface->pds[0].prefix_len == 0)
			memcpy(iface->pds, imsg_ifinfo->pds,
			    sizeof(iface->pds));

		got_lease = 0;
		for (i = 0; i < iface_conf->ia_count; i++) {
			if (iface->pds[i].prefix_len > 0) {
				got_lease = 1;
				break;
			}
		}
		if (got_lease)
			state_transition(iface, IF_REBOOTING);
		else
			state_transition(iface, IF_INIT);
	} else
		state_transition(iface, IF_DOWN);
}
struct dhcp6leased_iface*
get_dhcp6leased_iface_by_id(uint32_t if_index)
{
	struct dhcp6leased_iface	*iface;
	LIST_FOREACH (iface, &dhcp6leased_interfaces, entries) {
		if (iface->if_index == if_index)
			return (iface);
	}

	return (NULL);
}

void
remove_dhcp6leased_iface(uint32_t if_index)
{
	struct dhcp6leased_iface	*iface;

	iface = get_dhcp6leased_iface_by_id(if_index);

	if (iface == NULL)
		return;

	deconfigure_interfaces(iface);
	LIST_REMOVE(iface, entries);
	evtimer_del(&iface->timer);
	free(iface);
}

void
parse_dhcp(struct dhcp6leased_iface *iface, struct imsg_dhcp *dhcp)
{
	struct iface_conf	*iface_conf;
	struct iface_ia_conf	*ia_conf;
	struct dhcp_hdr		 hdr;
	struct dhcp_option_hdr	 opt_hdr;
	struct dhcp_iapd	 iapd;
	size_t			 rem;
	uint32_t		 t1, t2, lease_time;
	int			 serverid_len, rapid_commit = 0;
	uint8_t			 serverid[SERVERID_SIZE];
	uint8_t			*p;
	char			 ifnamebuf[IF_NAMESIZE], *if_name;
	char			 ntopbuf[INET6_ADDRSTRLEN];

	if ((if_name = if_indextoname(iface->if_index, ifnamebuf)) == NULL) {
		log_debug("%s: unknown interface %d", __func__,
		    iface->if_index);
		goto out;
	}
	if ((iface_conf = find_iface_conf(&engine_conf->iface_list, if_name))
	    == NULL) {
		log_debug("%s: no interface configuration for %d", __func__,
		    iface->if_index);
		goto out;
	}

	log_debug("%s: %s ia_count: %d", __func__, if_name,
	    iface_conf->ia_count);

	serverid_len = t1 = t2 = lease_time = 0;
	memset(iface->new_pds, 0, sizeof(iface->new_pds));

	p = dhcp->packet;
	rem = dhcp->len;

	if (rem < sizeof(struct dhcp_hdr)) {
		log_warnx("%s: message too short", __func__);
		goto out;
	}
	memcpy(&hdr, p, sizeof(struct dhcp_hdr));
	p += sizeof(struct dhcp_hdr);
	rem -= sizeof(struct dhcp_hdr);

	if (log_getverbose() > 1)
		log_debug("%s: %s, xid: 0x%02x%02x%02x", __func__,
		    dhcp_message_type2str(hdr.msg_type), hdr.xid[0], hdr.xid[1],
		    hdr.xid[2]);

	while (rem >= sizeof(struct dhcp_option_hdr)) {
		memcpy(&opt_hdr, p, sizeof(struct dhcp_option_hdr));
		opt_hdr.code = ntohs(opt_hdr.code);
		opt_hdr.len = ntohs(opt_hdr.len);
		p += sizeof(struct dhcp_option_hdr);
		rem -= sizeof(struct dhcp_option_hdr);
		if (log_getverbose() > 1)
			log_debug("%s: %s, len: %u", __func__,
			    dhcp_option_type2str(opt_hdr.code), opt_hdr.len);

		if (rem < opt_hdr.len) {
			log_warnx("%s: malformed packet, ignoring", __func__);
			goto out;
		}

		switch (opt_hdr.code) {
		case DHO_CLIENTID:
			if (opt_hdr.len != sizeof(struct dhcp_duid) ||
			    memcmp(&duid, p, sizeof(struct dhcp_duid)) != 0) {
				log_debug("%s: message not for us", __func__);
				goto out;
			}
			break;
		case DHO_SERVERID:
			/*
			 * RFC 8415, 11.1:
			 * The length of the DUID (not including the type code)
			 * is at least 1 octet and at most 128 octets.
			 */
			if (opt_hdr.len < 2 + 1) {
				log_warnx("%s: SERVERID too short", __func__);
				goto out;
			}
			if (opt_hdr.len > SERVERID_SIZE) {
				log_warnx("%s: SERVERID too long", __func__);
				goto out;
			}
			log_debug("%s: SERVERID: %s", __func__,
			    dhcp_duid2str(opt_hdr.len, p));
			if (serverid_len != 0) {
				log_warnx("%s: duplicate SERVERID option",
				    __func__);
				goto out;
			}
			serverid_len = opt_hdr.len;
			memcpy(serverid, p, serverid_len);
			break;
		case DHO_IA_PD:
			if (opt_hdr.len < sizeof(struct dhcp_iapd)) {
				log_warnx("%s: IA_PD too short", __func__);
				goto out;
			}
			memcpy(&iapd, p, sizeof(struct dhcp_iapd));

			if (t1 == 0 || t1 > ntohl(iapd.t1))
				t1 = ntohl(iapd.t1);
			if (t2 == 0 || t2 > ntohl(iapd.t2))
				t2 = ntohl(iapd.t2);

			log_debug("%s: IA_PD, IAID: %08x, T1: %u, T2: %u",
			    __func__, ntohl(iapd.iaid), ntohl(iapd.t1),
			    ntohl(iapd.t2));
			if (ntohl(iapd.iaid) < iface_conf->ia_count) {
				int status_code;
				status_code = parse_ia_pd_options(p +
				    sizeof(struct dhcp_iapd), opt_hdr.len -
				    sizeof(struct dhcp_iapd),
				    &iface->new_pds[ntohl(iapd.iaid)]);

				if (status_code != DHCP_STATUS_SUCCESS &&
				    iface->state == IF_RENEWING) {
					state_transition(iface, IF_REBINDING);
					goto out;
				}
			}
			break;
		case DHO_RAPID_COMMIT:
			if (opt_hdr.len != 0) {
				log_warnx("%s: invalid rapid commit option",
				    __func__);
				goto out;
			}
			rapid_commit = 1;
			break;
		default:
			log_debug("unhandled option: %u", opt_hdr.code);
			break;
		}

		p += opt_hdr.len;
		rem -= opt_hdr.len;
	}

	/* check that we got all the information we need */
	if (serverid_len == 0) {
		log_warnx("%s: Did not receive server identifier", __func__);
		goto out;
	}


	SIMPLEQ_FOREACH(ia_conf, &iface_conf->iface_ia_list, entry) {
		struct prefix	*pd = &iface->new_pds[ia_conf->id];

		if (pd->prefix_len == 0) {
			log_warnx("%s: no IA for IAID %d found", __func__,
			    ia_conf->id);
			goto out;
		}
		if (pd->prefix_len > ia_conf->prefix_len) {
			log_warnx("%s: prefix for IAID %d too small: %d > %d",
			    __func__, ia_conf->id, pd->prefix_len,
			    ia_conf->prefix_len);
			goto out;
		}

		if (lease_time == 0 || lease_time > pd->vltime)
			lease_time = pd->vltime;

		log_debug("%s: pltime: %u, vltime: %u, prefix: %s/%u",
		    __func__, pd->pltime, pd->vltime, inet_ntop(AF_INET6,
		    &pd->prefix, ntopbuf, INET6_ADDRSTRLEN), pd->prefix_len);
	}

	switch (hdr.msg_type) {
	case DHCPSOLICIT:
	case DHCPREQUEST:
	case DHCPCONFIRM:
	case DHCPRENEW:
	case DHCPREBIND:
	case DHCPRELEASE:
	case DHCPDECLINE:
	case DHCPINFORMATIONREQUEST:
		log_warnx("%s: Ignoring client-only message (%s) from server",
		    __func__, dhcp_message_type2str(hdr.msg_type));
		goto out;
	case DHCPRELAYFORW:
	case DHCPRELAYREPL:
		log_warnx("%s: Ignoring relay-agent-only message (%s) from "
		    "server", __func__, dhcp_message_type2str(hdr.msg_type));
		goto out;
	case DHCPADVERTISE:
		if (iface->state != IF_INIT) {
			log_debug("%s: ignoring unexpected %s", __func__,
			    dhcp_message_type2str(hdr.msg_type));
			goto out;
		}
		iface->serverid_len = serverid_len;
		memcpy(iface->serverid, serverid, SERVERID_SIZE);
		memcpy(iface->pds, iface->new_pds, sizeof(iface->pds));
		state_transition(iface, IF_REQUESTING);
		break;
	case DHCPREPLY:
		switch (iface->state) {
		case IF_REQUESTING:
		case IF_RENEWING:
		case IF_REBINDING:
		case IF_REBOOTING:
			break;
		case IF_INIT:
			if (rapid_commit && engine_conf->rapid_commit)
				break;
			/* fall through */
		default:
			log_debug("%s: ignoring unexpected %s", __func__,
			    dhcp_message_type2str(hdr.msg_type));
			goto out;
		}
		iface->serverid_len = serverid_len;
		memcpy(iface->serverid, serverid, SERVERID_SIZE);

		if (t1 == 0)
			iface->t1 = lease_time / 2;
		else
			iface->t1 = t1;
		if (t2 == 0)
		    iface->t2 = lease_time - (lease_time / 8);
		else
			iface->t2 = t2;
		iface->lease_time = lease_time;
		clock_gettime(CLOCK_MONOTONIC, &iface->request_time);
		state_transition(iface, IF_BOUND);
		break;
	case DHCPRECONFIGURE:
		log_warnx("%s: Ignoring %s from server",
		    __func__, dhcp_message_type2str(hdr.msg_type));
		goto out;
	default:
		fatalx("%s: %s unhandled",
		    __func__, dhcp_message_type2str(hdr.msg_type));
		break;
	}
 out:
	return;
}

int
parse_ia_pd_options(uint8_t *p, size_t len, struct prefix *prefix)
{
	struct dhcp_option_hdr	 opt_hdr;
	struct dhcp_iaprefix	 iaprefix;
	struct in6_addr		 mask;
	int			 i;
	uint16_t		 status_code = DHCP_STATUS_SUCCESS;
	char			 ntopbuf[INET6_ADDRSTRLEN], *visbuf;

	while (len >= sizeof(struct dhcp_option_hdr)) {
		memcpy(&opt_hdr, p, sizeof(struct dhcp_option_hdr));
		opt_hdr.code = ntohs(opt_hdr.code);
		opt_hdr.len = ntohs(opt_hdr.len);
		p += sizeof(struct dhcp_option_hdr);
		len -= sizeof(struct dhcp_option_hdr);
		if (log_getverbose() > 1)
			log_debug("%s: %s, len: %u", __func__,
			    dhcp_option_type2str(opt_hdr.code), opt_hdr.len);
		if (len < opt_hdr.len) {
			log_warnx("%s: malformed packet, ignoring", __func__);
			return DHCP_STATUS_UNSPECFAIL;
		}

		switch (opt_hdr.code) {
		case DHO_IA_PREFIX:
			if (len < sizeof(struct dhcp_iaprefix)) {
				log_warnx("%s: malformed packet, ignoring",
				    __func__);
				return DHCP_STATUS_UNSPECFAIL;
			}

			memcpy(&iaprefix, p, sizeof(struct dhcp_iaprefix));
			log_debug("%s: pltime: %u, vltime: %u, prefix: %s/%u",
			    __func__, ntohl(iaprefix.pltime),
			    ntohl(iaprefix.vltime), inet_ntop(AF_INET6,
			    &iaprefix.prefix, ntopbuf, INET6_ADDRSTRLEN),
			    iaprefix.prefix_len);

			if (ntohl(iaprefix.vltime) < ntohl(iaprefix.pltime)) {
				log_warnx("%s: vltime < pltime, ignoring IA_PD",
				    __func__);
				break;
			}

			if (ntohl(iaprefix.vltime) == 0) {
				log_debug("%s: vltime == 0, ignoring IA_PD",
				    __func__);
				break;
			}

			prefix->prefix = iaprefix.prefix;
			prefix->prefix_len = iaprefix.prefix_len;
			prefix->vltime = ntohl(iaprefix.vltime);
			prefix->pltime = ntohl(iaprefix.pltime);

			/* make sure prefix is masked correctly */
			memset(&mask, 0, sizeof(mask));
			in6_prefixlen2mask(&mask, prefix->prefix_len);
			for (i = 0; i < 16; i++)
				prefix->prefix.s6_addr[i] &= mask.s6_addr[i];

			break;
		case DHO_STATUS_CODE:
			/* XXX STATUS_CODE can also appear outside of options */
			if (len < 2) {
				log_warnx("%s: malformed packet, ignoring",
				    __func__);
				return DHCP_STATUS_UNSPECFAIL;
			}
			memcpy(&status_code, p, sizeof(uint16_t));
			status_code = ntohs(status_code);
			/* must be at least 4 * srclen + 1 long */
			visbuf = calloc(4, opt_hdr.len - 2 + 1);
			if (visbuf == NULL) {
				log_warn("%s", __func__);
				break;
			}
			strvisx(visbuf, p + 2, opt_hdr.len - 2, VIS_SAFE);
			log_debug("%s: %s - %s", __func__,
			    dhcp_status2str(status_code), visbuf);
			break;
		default:
			log_debug("unhandled option: %u", opt_hdr.code);
		}
		p += opt_hdr.len;
		len -= opt_hdr.len;
	}
	return status_code;
}

/* XXX check valid transitions */
void
state_transition(struct dhcp6leased_iface *iface, enum if_state new_state)
{
	enum if_state	 old_state = iface->state;
	char		 ifnamebuf[IF_NAMESIZE], *if_name;

	iface->state = new_state;

	switch (new_state) {
	case IF_DOWN:
		switch (old_state) {
		case IF_RENEWING:
		case IF_REBINDING:
		case IF_REBOOTING:
		case IF_BOUND:
			deprecate_interfaces(iface);
			break;
		default:
			break;
		}
		/*
		 * Nothing else to do until iface comes up.
		 * IP addresses will expire.
		 */
		iface->timo.tv_sec = -1;
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
			deconfigure_interfaces(iface);
			/* fall through */
		case IF_DOWN:
			iface->timo.tv_sec = START_EXP_BACKOFF;
			clock_gettime(CLOCK_MONOTONIC,
			    &iface->elapsed_time_start);
			break;
		case IF_BOUND:
			fatal("invalid transition Bound -> Init");
			break;
		}
		request_dhcp_discover(iface);
		break;
	case IF_REBOOTING:
		if (old_state == IF_REBOOTING)
			iface->timo.tv_sec *= 2;
		else {
			iface->timo.tv_sec = START_EXP_BACKOFF;
			arc4random_buf(iface->xid, sizeof(iface->xid));
		}
		request_dhcp_request(iface);
		break;
	case IF_REQUESTING:
		if (old_state == IF_REQUESTING)
			iface->timo.tv_sec *= 2;
		else {
			iface->timo.tv_sec = START_EXP_BACKOFF;
			clock_gettime(CLOCK_MONOTONIC,
			    &iface->elapsed_time_start);
		}
		request_dhcp_request(iface);
		break;
	case IF_BOUND:
		iface->timo.tv_sec = iface->t1;
		switch (old_state) {
		case IF_REQUESTING:
		case IF_RENEWING:
		case IF_REBINDING:
		case IF_REBOOTING:
			configure_interfaces(iface);
			break;
		case IF_INIT:
			if (engine_conf->rapid_commit)
				configure_interfaces(iface);
			else
				fatal("invalid transition Init -> Bound");
			break;
		default:
			break;
		}
		break;
	case IF_RENEWING:
		if (old_state == IF_BOUND) {
			iface->timo.tv_sec = (iface->t2 -
			    iface->t1) / 2; /* RFC 2131 4.4.5 */
			arc4random_buf(iface->xid, sizeof(iface->xid));
		} else
			iface->timo.tv_sec /= 2;

		if (iface->timo.tv_sec < 60)
			iface->timo.tv_sec = 60;
		request_dhcp_request(iface);
		break;
	case IF_REBINDING:
		if (old_state == IF_RENEWING) {
			iface->timo.tv_sec = (iface->lease_time -
			    iface->t2) / 2; /* RFC 2131 4.4.5 */
		} else
			iface->timo.tv_sec /= 2;
		request_dhcp_request(iface);
		break;
	}

	if_name = if_indextoname(iface->if_index, ifnamebuf);
	log_debug("%s[%s] %s -> %s, timo: %lld", __func__, if_name == NULL ?
	    "?" : if_name, if_state_name[old_state], if_state_name[new_state],
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
	struct dhcp6leased_iface	*iface = (struct dhcp6leased_iface *)arg;
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
		log_debug("%s: res.tv_sec: %lld, t2: %u", __func__,
		    res.tv_sec, iface->t2);
		if (res.tv_sec >= iface->t2)
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
	}
}

/* XXX can this be merged into dhcp_request()? */
void
request_dhcp_discover(struct dhcp6leased_iface *iface)
{
	struct imsg_req_dhcp	 imsg;
	struct timespec		 now, res;

	memset(&imsg, 0, sizeof(imsg));
	imsg.if_index = iface->if_index;
	memcpy(imsg.xid, iface->xid, sizeof(imsg.xid));
	clock_gettime(CLOCK_MONOTONIC, &now);
	timespecsub(&now, &iface->elapsed_time_start, &res);
	if (res.tv_sec * 100 > 0xffff)
		imsg.elapsed_time = 0xffff;
	else
		imsg.elapsed_time = res.tv_sec * 100;
	engine_imsg_compose_frontend(IMSG_SEND_SOLICIT, 0, &imsg, sizeof(imsg));
}

void
request_dhcp_request(struct dhcp6leased_iface *iface)
{
	struct imsg_req_dhcp	 imsg;
	struct timespec		 now, res;

	memset(&imsg, 0, sizeof(imsg));
	imsg.if_index = iface->if_index;
	memcpy(imsg.xid, iface->xid, sizeof(imsg.xid));

	clock_gettime(CLOCK_MONOTONIC, &now);
	timespecsub(&now, &iface->elapsed_time_start, &res);
	if (res.tv_sec * 100 > 0xffff)
		imsg.elapsed_time = 0xffff;
	else
		imsg.elapsed_time = res.tv_sec * 100;

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
	case IF_REQUESTING:
	case IF_RENEWING:
	case IF_REBINDING:
		imsg.serverid_len = iface->serverid_len;
		memcpy(imsg.serverid, iface->serverid, SERVERID_SIZE);
		memcpy(imsg.pds, iface->pds, sizeof(iface->pds));
		break;
	}
	switch (iface->state) {
	case IF_REQUESTING:
		engine_imsg_compose_frontend(IMSG_SEND_REQUEST, 0, &imsg,
		    sizeof(imsg));
		break;
	case IF_RENEWING:
		engine_imsg_compose_frontend(IMSG_SEND_RENEW, 0, &imsg,
		    sizeof(imsg));
		break;
	case IF_REBOOTING:
	case IF_REBINDING:
		engine_imsg_compose_frontend(IMSG_SEND_REBIND, 0, &imsg,
		    sizeof(imsg));
		break;
	default:
		fatalx("%s: wrong state", __func__);
	}
}

void
configure_interfaces(struct dhcp6leased_iface *iface)
{
	struct iface_conf	*iface_conf;
	struct iface_ia_conf	*ia_conf;
	struct iface_pd_conf	*pd_conf;
	struct imsg_lease_info	 imsg_lease_info;
	uint32_t	 	 i;
	char		 	 ntopbuf[INET6_ADDRSTRLEN];
	char			 ifnamebuf[IF_NAMESIZE], *if_name;

	if ((if_name = if_indextoname(iface->if_index, ifnamebuf)) == NULL) {
		log_debug("%s: unknown interface %d", __func__,
		    iface->if_index);
		return;
	}
	if ((iface_conf = find_iface_conf(&engine_conf->iface_list, if_name))
	    == NULL) {
		log_debug("%s: no interface configuration for %d", __func__,
		    iface->if_index);
		return;
	}

	for (i = 0; i < iface_conf->ia_count; i++) {
		struct prefix	*pd = &iface->new_pds[i];

		log_info("prefix delegation #%d %s/%d received on %s from "
		    "server %s", i, inet_ntop(AF_INET6, &pd->prefix, ntopbuf,
		    INET6_ADDRSTRLEN), pd->prefix_len, if_name,
		    dhcp_duid2str(iface->serverid_len, iface->serverid));

		send_reconfigure_reject_route(iface, &pd->prefix,
		    pd->prefix_len, CONFIGURE);
	}

	SIMPLEQ_FOREACH(ia_conf, &iface_conf->iface_ia_list, entry) {
		struct prefix	*pd = &iface->new_pds[ia_conf->id];

		SIMPLEQ_FOREACH(pd_conf, &ia_conf->iface_pd_list, entry) {
			send_reconfigure_interface(pd_conf, pd, CONFIGURE);
		}
	}

	if (prefixcmp(iface->pds, iface->new_pds, iface_conf->ia_count) != 0) {
		log_info("Prefix delegations on %s from server %s changed",
		    if_name, dhcp_duid2str(iface->serverid_len,
		    iface->serverid));
		for (i = 0; i < iface_conf->ia_count; i++) {
			log_debug("%s: iface->pds [%d]: %s/%d", __func__, i,
			    inet_ntop(AF_INET6, &iface->pds[i].prefix, ntopbuf,
			    INET6_ADDRSTRLEN), iface->pds[i].prefix_len);
			log_debug("%s:        pds [%d]: %s/%d", __func__, i,
			    inet_ntop(AF_INET6, &iface->new_pds[i].prefix,
			    ntopbuf, INET6_ADDRSTRLEN),
			    iface->new_pds[i].prefix_len);
		}
		deconfigure_interfaces(iface);
	}

	memcpy(iface->pds, iface->new_pds, sizeof(iface->pds));
	memset(iface->new_pds, 0, sizeof(iface->new_pds));

	memset(&imsg_lease_info, 0, sizeof(imsg_lease_info));
	imsg_lease_info.if_index = iface->if_index;
	memcpy(imsg_lease_info.pds, iface->pds, sizeof(iface->pds));
	engine_imsg_compose_main(IMSG_WRITE_LEASE, 0, &imsg_lease_info,
	    sizeof(imsg_lease_info));
}

void
deconfigure_interfaces(struct dhcp6leased_iface *iface)
{
	struct iface_conf	*iface_conf;
	struct iface_ia_conf	*ia_conf;
	struct iface_pd_conf	*pd_conf;
	uint32_t	 	 i;
	char		 	 ntopbuf[INET6_ADDRSTRLEN];
	char			 ifnamebuf[IF_NAMESIZE], *if_name;


	if ((if_name = if_indextoname(iface->if_index, ifnamebuf)) == NULL) {
		log_debug("%s: unknown interface %d", __func__,
		    iface->if_index);
		return;
	}
	if ((iface_conf = find_iface_conf(&engine_conf->iface_list, if_name))
	    == NULL) {
		log_debug("%s: no interface configuration for %d", __func__,
		    iface->if_index);
		return;
	}

	for (i = 0; i < iface_conf->ia_count; i++) {
		struct prefix *pd = &iface->pds[i];

		log_info("Prefix delegation #%d %s/%d expired on %s from "
		    "server %s", i, inet_ntop(AF_INET6, &pd->prefix, ntopbuf,
		    INET6_ADDRSTRLEN), pd->prefix_len, if_name,
		    dhcp_duid2str(iface->serverid_len, iface->serverid));
		send_reconfigure_reject_route(iface, &pd->prefix,
		    pd->prefix_len, DECONFIGURE);
	}

	SIMPLEQ_FOREACH(ia_conf, &iface_conf->iface_ia_list, entry) {
		struct prefix	*pd = &iface->pds[ia_conf->id];

		SIMPLEQ_FOREACH(pd_conf, &ia_conf->iface_pd_list, entry) {
			send_reconfigure_interface(pd_conf, pd, DECONFIGURE);
		}
	}
	memset(iface->pds, 0, sizeof(iface->pds));
}

void
deprecate_interfaces(struct dhcp6leased_iface *iface)
{
	struct iface_conf	*iface_conf;
	struct iface_ia_conf	*ia_conf;
	struct iface_pd_conf	*pd_conf;
	struct timespec		 now, diff;
	uint32_t	 	 i;
	char		 	 ntopbuf[INET6_ADDRSTRLEN];
	char			 ifnamebuf[IF_NAMESIZE], *if_name;


	if ((if_name = if_indextoname(iface->if_index, ifnamebuf)) == NULL) {
		log_debug("%s: unknown interface %d", __func__,
		    iface->if_index);
		return;
	}
	if ((iface_conf = find_iface_conf(&engine_conf->iface_list, if_name))
	    == NULL) {
		log_debug("%s: no interface configuration for %d", __func__,
		    iface->if_index);
		return;
	}

	for (i = 0; i < iface_conf->ia_count; i++) {
		struct prefix *pd = &iface->pds[i];

		log_info("%s went down, deprecating prefix delegation #%d %s/%d"
		    " from server %s", if_name, i, inet_ntop(AF_INET6,
		    &pd->prefix, ntopbuf, INET6_ADDRSTRLEN), pd->prefix_len,
		    dhcp_duid2str(iface->serverid_len, iface->serverid));
	}

	clock_gettime(CLOCK_MONOTONIC, &now);
	timespecsub(&now, &iface->request_time, &diff);

	SIMPLEQ_FOREACH(ia_conf, &iface_conf->iface_ia_list, entry) {
		struct prefix	*pd = &iface->pds[ia_conf->id];

		if (pd->vltime > diff.tv_sec)
			pd->vltime -= diff.tv_sec;
		else
			pd->vltime = 0;

		pd->pltime = 0;

		SIMPLEQ_FOREACH(pd_conf, &ia_conf->iface_pd_list, entry) {
			send_reconfigure_interface(pd_conf, pd, CONFIGURE);
		}
	}
}

int
prefixcmp(struct prefix *a, struct prefix *b, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (a[i].prefix_len != b[i].prefix_len)
			return 1;
		if (memcmp(&a[i].prefix, &b[i].prefix,
		    sizeof(struct in6_addr)) != 0)
			return 1;
	}
	return 0;
}

void
send_reconfigure_interface(struct iface_pd_conf *pd_conf, struct prefix *pd,
    enum reconfigure_action action)
{
	struct imsg_configure_address	 address;
	uint32_t			 if_index;
	int				 i;
	char				 ntopbuf[INET6_ADDRSTRLEN];

	if (pd->prefix_len == 0)
		return;

	if (strcmp(pd_conf->name, "reserve") == 0)
		return;

	if ((if_index = if_nametoindex(pd_conf->name)) == 0)
		return;

	memset(&address, 0, sizeof(address));

	address.if_index = if_index;
	address.addr.sin6_family = AF_INET6;
	address.addr.sin6_len = sizeof(address.addr);
	address.addr.sin6_addr = pd->prefix;

	for (i = 0; i < 16; i++)
		address.addr.sin6_addr.s6_addr[i] |=
		    pd_conf->prefix_mask.s6_addr[i];

	/* XXX make this configurable & use SOII */
	address.addr.sin6_addr.s6_addr[15] |= 1;

	in6_prefixlen2mask(&address.mask, pd_conf->prefix_len);

	log_debug("%s: %s %s: %s/%d", __func__, pd_conf->name,
	    reconfigure_action_name[action], inet_ntop(AF_INET6,
	    &address.addr.sin6_addr, ntopbuf, INET6_ADDRSTRLEN),
	    pd_conf->prefix_len);

	address.vltime = pd->vltime;
	address.pltime = pd->pltime;

	if (action == CONFIGURE)
		engine_imsg_compose_main(IMSG_CONFIGURE_ADDRESS, 0, &address,
		    sizeof(address));
	else
		engine_imsg_compose_main(IMSG_DECONFIGURE_ADDRESS, 0, &address,
		    sizeof(address));
}

void
send_reconfigure_reject_route(struct dhcp6leased_iface *iface,
    struct in6_addr *prefix, uint8_t prefix_len, enum reconfigure_action action)
{
	struct imsg_configure_reject_route	 imsg;

	memset(&imsg, 0, sizeof(imsg));

	imsg.if_index = iface->if_index;
	imsg.rdomain = iface->rdomain;
	memcpy(&imsg.prefix, prefix, sizeof(imsg.prefix));
	in6_prefixlen2mask(&imsg.mask, prefix_len);

	if (action == CONFIGURE)
		engine_imsg_compose_main(IMSG_CONFIGURE_REJECT_ROUTE, 0, &imsg,
		    sizeof(imsg));
	else
		engine_imsg_compose_main(IMSG_DECONFIGURE_REJECT_ROUTE, 0,
		    &imsg, sizeof(imsg));
}

const char *
dhcp_message_type2str(int type)
{
	static char buf[sizeof("Unknown [255]")];

	switch (type) {
	case DHCPSOLICIT:
		return "DHCPSOLICIT";
	case DHCPADVERTISE:
		return "DHCPADVERTISE";
	case DHCPREQUEST:
		return "DHCPREQUEST";
	case DHCPCONFIRM:
		return "DHCPCONFIRM";
	case DHCPRENEW:
		return "DHCPRENEW";
	case DHCPREBIND:
		return "DHCPREBIND";
	case DHCPREPLY:
		return "DHCPREPLY";
	case DHCPRELEASE:
		return "DHCPRELEASE";
	case DHCPDECLINE:
		return "DHCPDECLINE";
	case DHCPRECONFIGURE:
		return "DHCPRECONFIGURE";
	case DHCPINFORMATIONREQUEST:
		return "DHCPINFORMATIONREQUEST";
	case DHCPRELAYFORW:
		return "DHCPRELAYFORW";
	case DHCPRELAYREPL:
		return "DHCPRELAYREPL";
	default:
		snprintf(buf, sizeof(buf), "Unknown [%u]", type & 0xff);
		return buf;
	}
}

const char *
dhcp_option_type2str(int code)
{
	static char buf[sizeof("Unknown [65535]")];
	switch (code) {
	case DHO_CLIENTID:
		return "DHO_CLIENTID";
	case DHO_SERVERID:
		return "DHO_SERVERID";
	case DHO_ORO:
		return "DHO_ORO";
	case DHO_ELAPSED_TIME:
		return "DHO_ELAPSED_TIME";
	case DHO_STATUS_CODE:
		return "DHO_STATUS_CODE";
	case DHO_RAPID_COMMIT:
		return "DHO_RAPID_COMMIT";
	case DHO_VENDOR_CLASS:
		return "DHO_VENDOR_CLASS";
	case DHO_IA_PD:
		return "DHO_IA_PD";
	case DHO_IA_PREFIX:
		return "DHO_IA_PREFIX";
	case DHO_SOL_MAX_RT:
		return "DHO_SOL_MAX_RT";
	case DHO_INF_MAX_RT:
		return "DHO_INF_MAX_RT";
	default:
		snprintf(buf, sizeof(buf), "Unknown [%u]", code &0xffff);
		return buf;
	}
}

const char*
dhcp_duid2str(int len, uint8_t *p)
{
	static char	 buf[2 * 130];
	int		 i, rem;
	char		*pbuf;

	if (len > 130)
		return "invalid";

	pbuf = buf;
	rem = sizeof(buf);
	for (i = 0; i < len && rem > 0; i++, pbuf += 2, rem -=2)
		snprintf(pbuf, rem, "%02x", p[i]);

	return buf;
}

const char*
dhcp_status2str(int status)
{
	static char buf[sizeof("Unknown [255]")];

	switch (status) {
	case DHCP_STATUS_SUCCESS:
		return "Success";
	case DHCP_STATUS_UNSPECFAIL:
		return "UnspecFail";
	case DHCP_STATUS_NOADDRSAVAIL:
		return "NoAddrsAvail";
	case DHCP_STATUS_NOBINDING:
		return "NoBinding";
	case DHCP_STATUS_NOTONLINK:
		return "NotOnLink";
	case DHCP_STATUS_USEMULTICAST:
		return "UseMulticast";
	case DHCP_STATUS_NOPREFIXAVAIL:
		return "NoPrefixAvail";
	default:
		snprintf(buf, sizeof(buf), "Unknown [%u]", status & 0xff);
		return buf;
    }
}

/* from sys/netinet6/in6.c */
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
void
in6_prefixlen2mask(struct in6_addr *maskp, int len)
{
	u_char maskarray[8] = {0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff};
	int bytelen, bitlen, i;

	if (0 > len || len > 128)
		fatalx("%s: invalid prefix length(%d)\n", __func__, len);

	bzero(maskp, sizeof(*maskp));
	bytelen = len / 8;
	bitlen = len % 8;
	for (i = 0; i < bytelen; i++)
		maskp->s6_addr[i] = 0xff;
	/* len == 128 is ok because bitlen == 0 then */
	if (bitlen)
		maskp->s6_addr[bytelen] = maskarray[bitlen - 1];
}
