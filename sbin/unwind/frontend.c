/*	$OpenBSD: frontend.c,v 1.92 2025/04/27 16:21:26 florian Exp $	*/

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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/tree.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <net/if.h>
#include <net/route.h>

#include <errno.h>
#include <event.h>
#include <ifaddrs.h>
#include <imsg.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "libunbound/config.h"
#include "libunbound/sldns/pkthdr.h"
#include "libunbound/sldns/sbuffer.h"
#include "libunbound/sldns/str2wire.h"
#include "libunbound/sldns/wire2str.h"
#include "libunbound/util/alloc.h"
#include "libunbound/util/net_help.h"
#include "libunbound/util/regional.h"
#include "libunbound/util/data/dname.h"
#include "libunbound/util/data/msgencode.h"
#include "libunbound/util/data/msgparse.h"
#include "libunbound/util/data/msgreply.h"

#include "log.h"
#include "unwind.h"
#include "frontend.h"
#include "control.h"
#include "dns64_synth.h"

#define	MINIMUM(a, b)		(((a) < (b)) ? (a) : (b))
#define	ROUTE_SOCKET_BUF_SIZE   16384

/*
 * size of a resource record with name a two octed pointer to qname
 * 2 octets pointer to qname
 * 2 octets TYPE
 * 2 octets CLASS
 * 4 octets TTL
 * 2 octets RDLENGTH
 */
#define COMPRESSED_RR_SIZE	12
#define MINIMIZE_ANSWER		1

#define FD_RESERVE		5
#define TCP_TIMEOUT		15
#define DEFAULT_TCP_SIZE	512

struct udp_ev {
	struct event		 ev;
	uint8_t			 query[65536];
	struct msghdr		 rcvmhdr;
	struct iovec		 rcviov[1];
	struct sockaddr_storage	 from;
} udp4ev, udp6ev;

struct tcp_accept_ev {
	struct event		 ev;
	struct event		 pause;
} tcp4ev, tcp6ev;

struct pending_query {
	TAILQ_ENTRY(pending_query)	 entry;
	struct sockaddr_storage		 from;
	struct sldns_buffer		*qbuf;
	struct sldns_buffer		*abuf;
	struct regional			*region;
	struct query_info		 qinfo;
	struct edns_data		 edns;
	struct event			 ev;		/* for tcp */
	struct event			 resp_ev;	/* for tcp */
	struct event			 tmo_ev;	/* for tcp */
	uint64_t			 imsg_id;
	uint16_t			 id;
	uint16_t			 flags;
	int				 fd;
	int				 tcp;
	int				 dns64_synthesize;
};

TAILQ_HEAD(, pending_query)	 pending_queries;

struct bl_node {
	RB_ENTRY(bl_node)	 entry;
	char			*domain;
	int			 len;
	int			 wildcard;
};

__dead void		 frontend_shutdown(void);
void			 frontend_sig_handler(int, short, void *);
void			 frontend_startup(void);
void			 udp_receive(int, short, void *);
void			 handle_query(struct pending_query *);
void			 free_pending_query(struct pending_query *);
void			 tcp_accept(int, short, void *);
int			 accept_reserve(int, struct sockaddr *, socklen_t *);
void			 accept_paused(int, short, void *);
void			 tcp_request(int, short, void *);
void			 tcp_response(int, short, void *);
void			 tcp_timeout(int, short, void *);
int			 check_query(sldns_buffer*);
void			 noerror_answer(struct pending_query *);
void			 synthesize_dns64_answer(struct pending_query *);
void			 resend_dns64_query(struct pending_query *);
void			 chaos_answer(struct pending_query *);
void			 error_answer(struct pending_query *, int rcode);
void			 send_answer(struct pending_query *);
void			 route_receive(int, short, void *);
void			 handle_route_message(struct rt_msghdr *,
			     struct sockaddr **);
void			 get_rtaddrs(int, struct sockaddr *,
			     struct sockaddr **);
struct pending_query	*find_pending_query(uint64_t);
void			 parse_trust_anchor(struct trust_anchor_head *, int);
void			 send_trust_anchors(struct trust_anchor_head *);
void			 write_trust_anchors(struct trust_anchor_head *, int);
void			 parse_blocklist(int);
int			 bl_cmp(struct bl_node *, struct bl_node *);
void			 free_bl(void);
int			 pending_query_cnt(void);
void			 check_available_af(void);
void			 reverse(char *, char *);

struct uw_conf		*frontend_conf;
static struct imsgev	*iev_main;
static struct imsgev	*iev_resolver;
struct event		 ev_route;
int			 udp4sock = -1, udp6sock = -1;
int			 tcp4sock = -1, tcp6sock = -1;
int			 ta_fd = -1;

static struct trust_anchor_head	 trust_anchors, new_trust_anchors;

RB_HEAD(bl_tree, bl_node)	 bl_head = RB_INITIALIZER(&bl_head);
RB_PROTOTYPE(bl_tree, bl_node, entry, bl_cmp)
RB_GENERATE(bl_tree, bl_node, entry, bl_cmp)

struct dns64_prefix	*dns64_prefixes;
int			 dns64_prefix_count;

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
	struct event	 ev_sigint, ev_sigterm;
	struct passwd	*pw;

	frontend_conf = config_new_empty();

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if ((pw = getpwnam(UNWIND_USER)) == NULL)
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

	if (pledge("stdio dns unix recvfd", NULL) == -1)
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
	if (iev_main != NULL)
		fatal("iev_main");
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

	udp4ev.rcviov[0].iov_base = (caddr_t)udp4ev.query;
	udp4ev.rcviov[0].iov_len = sizeof(udp4ev.query);
	udp4ev.rcvmhdr.msg_name = (caddr_t)&udp4ev.from;
	udp4ev.rcvmhdr.msg_namelen = sizeof(udp4ev.from);
	udp4ev.rcvmhdr.msg_iov = udp4ev.rcviov;
	udp4ev.rcvmhdr.msg_iovlen = 1;

	udp6ev.rcviov[0].iov_base = (caddr_t)udp6ev.query;
	udp6ev.rcviov[0].iov_len = sizeof(udp6ev.query);
	udp6ev.rcvmhdr.msg_name = (caddr_t)&udp6ev.from;
	udp6ev.rcvmhdr.msg_namelen = sizeof(udp6ev.from);
	udp6ev.rcvmhdr.msg_iov = udp6ev.rcviov;
	udp6ev.rcvmhdr.msg_iovlen = 1;

	TAILQ_INIT(&pending_queries);

	TAILQ_INIT(&trust_anchors);
	TAILQ_INIT(&new_trust_anchors);

	add_new_ta(&trust_anchors, KSK2017);
	add_new_ta(&trust_anchors, KSK2024);

	event_dispatch();

	frontend_shutdown();
}

__dead void
frontend_shutdown(void)
{
	/* Close pipes. */
	imsgbuf_write(&iev_resolver->ibuf);
	imsgbuf_clear(&iev_resolver->ibuf);
	close(iev_resolver->ibuf.fd);
	imsgbuf_write(&iev_main->ibuf);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	config_clear(frontend_conf);

	free(iev_resolver);
	free(iev_main);

	log_info("frontend exiting");
	exit(0);
}

int
frontend_imsg_compose_main(int type, pid_t pid, void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data, datalen));
}

int
frontend_imsg_compose_resolver(int type, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_resolver, type, 0, pid, -1, data,
	    datalen));
}

void
frontend_dispatch_main(int fd, short event, void *bula)
{
	static struct uw_conf	*nconf;
	struct imsg		 imsg;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	int			 n, shut = 0;

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
		case IMSG_SOCKET_IPC_RESOLVER:
			/*
			 * Setup pipe and event handler to the resolver
			 * process.
			 */
			if (iev_resolver) {
				fatalx("%s: received unexpected imsg fd "
				    "to frontend", __func__);
				break;
			}
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				fatalx("%s: expected to receive imsg fd to "
				   "frontend but didn't receive any",
				   __func__);
				break;
			}

			if (iev_resolver != NULL)
				fatal("iev_resolver");
			iev_resolver = malloc(sizeof(struct imsgev));
			if (iev_resolver == NULL)
				fatal(NULL);

			if (imsgbuf_init(&iev_resolver->ibuf, fd) == -1)
				fatal(NULL);
			iev_resolver->handler = frontend_dispatch_resolver;
			iev_resolver->events = EV_READ;

			event_set(&iev_resolver->ev, iev_resolver->ibuf.fd,
			    iev_resolver->events, iev_resolver->handler,
			    iev_resolver);
			event_add(&iev_resolver->ev, NULL);
			break;
		case IMSG_RECONF_CONF:
		case IMSG_RECONF_BLOCKLIST_FILE:
		case IMSG_RECONF_FORWARDER:
		case IMSG_RECONF_DOT_FORWARDER:
		case IMSG_RECONF_FORCE:
			imsg_receive_config(&imsg, &nconf);
			break;
		case IMSG_RECONF_END:
			if (nconf == NULL)
				fatalx("%s: IMSG_RECONF_END without "
				    "IMSG_RECONF_CONF", __func__);
			merge_config(frontend_conf, nconf);
			if (frontend_conf->blocklist_file == NULL)
				free_bl();
			nconf = NULL;
			break;
		case IMSG_UDP6SOCK:
			if (udp6sock != -1)
				fatalx("%s: received unexpected udp6sock",
				    __func__);
			if ((udp6sock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "UDP6 fd but didn't receive any", __func__);
			event_set(&udp6ev.ev, udp6sock, EV_READ | EV_PERSIST,
			    udp_receive, &udp6ev);
			event_add(&udp6ev.ev, NULL);
			break;
		case IMSG_UDP4SOCK:
			if (udp4sock != -1)
				fatalx("%s: received unexpected udp4sock",
				    __func__);
			if ((udp4sock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "UDP4 fd but didn't receive any", __func__);
			event_set(&udp4ev.ev, udp4sock, EV_READ | EV_PERSIST,
			    udp_receive, &udp4ev);
			event_add(&udp4ev.ev, NULL);
			break;
		case IMSG_TCP4SOCK:
			if (tcp4sock != -1)
				fatalx("%s: received unexpected tcp4sock",
				    __func__);
			if ((tcp4sock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "TCP4 fd but didn't receive any", __func__);
			event_set(&tcp4ev.ev, tcp4sock, EV_READ | EV_PERSIST,
			    tcp_accept, &tcp4ev);
			event_add(&tcp4ev.ev, NULL);
			evtimer_set(&tcp4ev.pause, accept_paused, &tcp4ev);
			break;
		case IMSG_TCP6SOCK:
			if (tcp6sock != -1)
				fatalx("%s: received unexpected tcp6sock",
				    __func__);
			if ((tcp6sock = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "TCP6 fd but didn't receive any", __func__);
			event_set(&tcp6ev.ev, tcp6sock, EV_READ | EV_PERSIST,
			    tcp_accept, &tcp6ev);
			event_add(&tcp6ev.ev, NULL);
			evtimer_set(&tcp6ev.pause, accept_paused, &tcp6ev);
			break;
		case IMSG_ROUTESOCK: {
			static int	 routesock = -1;

			if (routesock != -1)
				fatalx("%s: received unexpected routesock",
				    __func__);
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg "
				    "routesocket fd but didn't receive any",
				    __func__);
			routesock = fd;
			event_set(&ev_route, fd, EV_READ | EV_PERSIST,
			    route_receive, NULL);
			break;
		}
		case IMSG_STARTUP:
			frontend_startup();
			break;
		case IMSG_CONTROLFD:
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg control "
				    "fd but didn't receive any", __func__);
			/* Listen on control socket. */
			control_listen(fd);
			break;
		case IMSG_TAFD:
			if ((ta_fd = imsg_get_fd(&imsg)) != -1)
				parse_trust_anchor(&trust_anchors, ta_fd);
			if (!TAILQ_EMPTY(&trust_anchors))
				send_trust_anchors(&trust_anchors);
			break;
		case IMSG_BLFD:
			if ((fd = imsg_get_fd(&imsg)) == -1)
				fatalx("%s: expected to receive imsg block "
				   "list fd but didn't receive any", __func__);
			parse_blocklist(fd);
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
frontend_dispatch_resolver(int fd, short event, void *bula)
{
	static struct dns64_prefix	*new_dns64_prefixes = NULL;
	static int			 new_dns64_prefix_count = 0;
	static int			 new_dns64_prefix_pos = 0;
	struct pending_query		*pq;
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf = &iev->ibuf;
	struct imsg			 imsg;
	int				 n, shut = 0, chg;

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
		case IMSG_ANSWER: {
			struct answer_header	*answer_header;
			int			 data_len;
			uint8_t			*data;

			if (IMSG_DATA_SIZE(imsg) < sizeof(*answer_header))
				fatalx("%s: IMSG_ANSWER wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			answer_header = (struct answer_header *)imsg.data;
			data = (uint8_t *)imsg.data + sizeof(*answer_header);
			if (answer_header->answer_len > UINT16_MAX)
				fatalx("%s: IMSG_ANSWER answer too big: %d",
				    __func__, answer_header->answer_len);
			data_len = IMSG_DATA_SIZE(imsg) -
			    sizeof(*answer_header);

			if ((pq = find_pending_query(answer_header->id)) ==
			    NULL) {
				log_warnx("%s: cannot find pending query %llu",
				    __func__, answer_header->id);
				break;
			}

			if (answer_header->srvfail) {
				error_answer(pq, LDNS_RCODE_SERVFAIL);
				send_answer(pq);
				break;
			}

			if (answer_header->bogus && !(pq->flags & BIT_CD)) {
				error_answer(pq, LDNS_RCODE_SERVFAIL);
				send_answer(pq);
				break;
			}

			if (sldns_buffer_position(pq->abuf) == 0 &&
			    !sldns_buffer_set_capacity(pq->abuf,
			    answer_header->answer_len)) {
				error_answer(pq, LDNS_RCODE_SERVFAIL);
				send_answer(pq);
				break;
			}

			if (sldns_buffer_position(pq->abuf) + data_len >
			    sldns_buffer_capacity(pq->abuf))
				fatalx("%s: IMSG_ANSWER answer too big: %d",
				    __func__, data_len);
			sldns_buffer_write(pq->abuf, data, data_len);

			if (sldns_buffer_position(pq->abuf) ==
			    sldns_buffer_capacity(pq->abuf)) {
				sldns_buffer_flip(pq->abuf);
				if (pq->dns64_synthesize) {
					synthesize_dns64_answer(pq);
					send_answer(pq);
				} else {
					noerror_answer(pq);
					if (pq->dns64_synthesize)
						/* we did not find a answer */
						resend_dns64_query(pq);
					else
						send_answer(pq);
				}
			}
			break;
		}
		case IMSG_CTL_RESOLVER_INFO:
		case IMSG_CTL_AUTOCONF_RESOLVER_INFO:
		case IMSG_CTL_MEM_INFO:
		case IMSG_CTL_END:
			control_imsg_relay(&imsg);
			break;
		case IMSG_NEW_TA:
			/* make sure this is a string */
			((char *)imsg.data)[IMSG_DATA_SIZE(imsg) - 1] = '\0';
			add_new_ta(&new_trust_anchors, imsg.data);
			break;
		case IMSG_NEW_TAS_ABORT:
			free_tas(&new_trust_anchors);
			break;
		case IMSG_NEW_TAS_DONE:
			chg = merge_tas(&new_trust_anchors, &trust_anchors);
			if (chg)
				send_trust_anchors(&trust_anchors);

			/*
			 * always write trust anchors, the modify date on
			 * the file is an indication when we made progress
			 */
			if (ta_fd != -1)
				write_trust_anchors(&trust_anchors, ta_fd);
			break;
		case IMSG_NEW_DNS64_PREFIXES_START:
			if (IMSG_DATA_SIZE(imsg) !=
			    sizeof(new_dns64_prefix_count))
				fatalx("%s: IMSG_NEW_DNS64_PREFIXES_START "
				    "wrong length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&new_dns64_prefix_count, imsg.data,
			    sizeof(new_dns64_prefix_count));
			free(new_dns64_prefixes);
			new_dns64_prefixes = NULL;
			if (new_dns64_prefix_count > 0)
				new_dns64_prefixes =
				    calloc(new_dns64_prefix_count,
				    sizeof(struct dns64_prefix));
			new_dns64_prefix_pos = 0;
			break;
		case IMSG_NEW_DNS64_PREFIX: {
			if (IMSG_DATA_SIZE(imsg) != sizeof(struct dns64_prefix))
				fatalx("%s: IMSG_NEW_DNS64_PREFIX wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			if (new_dns64_prefixes == NULL)
				break;
			if (new_dns64_prefix_pos >= new_dns64_prefix_count)
				fatalx("%s: IMSG_NEW_DNS64_PREFIX: too many "
				    "prefixes", __func__);
			memcpy(&new_dns64_prefixes[new_dns64_prefix_pos++],
			    imsg.data, sizeof(struct dns64_prefix));
			break;
		}
		case IMSG_NEW_DNS64_PREFIXES_DONE:
			free(dns64_prefixes);
			dns64_prefixes = new_dns64_prefixes;
			dns64_prefix_count = new_dns64_prefix_count;
			new_dns64_prefixes = NULL;
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

	frontend_imsg_compose_main(IMSG_STARTUP_DONE, 0, NULL, 0);
	check_available_af();
}

void
free_pending_query(struct pending_query *pq)
{
	if (!pq)
		return;

	TAILQ_REMOVE(&pending_queries, pq, entry);
	regional_destroy(pq->region);
	sldns_buffer_free(pq->qbuf);
	sldns_buffer_free(pq->abuf);
	if (pq->tcp) {
		if (event_initialized(&pq->ev))
			event_del(&pq->ev);
		if (event_initialized(&pq->resp_ev))
			event_del(&pq->resp_ev);
		if (event_initialized(&pq->tmo_ev))
			event_del(&pq->tmo_ev);
		if (pq->fd != -1)
			close(pq->fd);
	}
	free(pq);
}

void
udp_receive(int fd, short events, void *arg)
{
	struct udp_ev		*udpev = (struct udp_ev *)arg;
	struct pending_query	*pq = NULL;
	ssize_t			 len;

	if ((len = recvmsg(fd, &udpev->rcvmhdr, 0)) == -1) {
		log_warn("recvmsg");
		return;
	}

	if ((pq = calloc(1, sizeof(*pq))) == NULL) {
		log_warn(NULL);
		return;
	}

	do {
		arc4random_buf(&pq->imsg_id, sizeof(pq->imsg_id));
	} while(find_pending_query(pq->imsg_id) != NULL);

	TAILQ_INSERT_TAIL(&pending_queries, pq, entry);

	pq->from = udpev->from;
	pq->fd = fd;
	pq->qbuf = sldns_buffer_new(len);
	pq->abuf = sldns_buffer_new(len); /* make sure we can send errors */
	pq->region = regional_create();

	if (!pq->qbuf || !pq->abuf || !pq->region) {
		log_warnx("out of memory");
		free_pending_query(pq);
		return;
	}

	sldns_buffer_write(pq->qbuf, udpev->query, len);
	sldns_buffer_flip(pq->qbuf);
	handle_query(pq);
}

void
handle_query(struct pending_query *pq)
{
	struct query_imsg	 query_imsg;
	struct bl_node		 find;
	int			 rcode, matched;
	char			*str;
	char			 dname[LDNS_MAX_DOMAINLEN + 1];
	char			 qclass_buf[16];
	char			 qtype_buf[16];

	memset(&query_imsg, 0, sizeof (query_imsg));

	if (log_getverbose() & OPT_VERBOSE2 && (str =
	    sldns_wire2str_pkt(sldns_buffer_begin(pq->qbuf),
	    sldns_buffer_limit(pq->qbuf))) != NULL) {
		log_debug("from: %s\n%s", ip_port((struct sockaddr *)
		    &pq->from), str);
		free(str);
	}

	if (sldns_buffer_remaining(pq->qbuf) < LDNS_HEADER_SIZE) {
		log_warnx("bad query: too short, dropped");
		goto drop;
	}

	pq->id = sldns_buffer_read_u16_at(pq->qbuf, 0);
	pq->flags = sldns_buffer_read_u16_at(pq->qbuf, 2);

	if (!query_info_parse(&pq->qinfo, pq->qbuf)) {
		log_warnx("query_info_parse failed");
		goto drop;
	}

	rcode = check_query(pq->qbuf);
	switch (rcode) {
	case LDNS_RCODE_NOERROR:
		break;
	case -1:
		goto drop;
	default:
		error_answer(pq, rcode);
		goto send_answer;
	}

	rcode = parse_edns_from_query_pkt(pq->qbuf, &pq->edns, NULL, NULL,
	    NULL, 0, pq->region, NULL);
	if (rcode != LDNS_RCODE_NOERROR) {
		error_answer(pq, rcode);
		goto send_answer;
	}

	if (!dname_valid(pq->qinfo.qname, pq->qinfo.qname_len)) {
		error_answer(pq, LDNS_RCODE_FORMERR);
		goto send_answer;
	}
	dname_str(pq->qinfo.qname, dname);

	sldns_wire2str_class_buf(pq->qinfo.qclass, qclass_buf,
	    sizeof(qclass_buf));
	sldns_wire2str_type_buf(pq->qinfo.qtype, qtype_buf, sizeof(qtype_buf));
	log_debug("%s: %s %s %s ?", ip_port((struct sockaddr *)&pq->from),
	    dname, qclass_buf, qtype_buf);

	if (!RB_EMPTY(&bl_head)) {
		find.len = strlen(dname);
		find.wildcard = 0;
		reverse(dname, dname + find.len);
		find.domain = dname;
		matched = (RB_FIND(bl_tree, &bl_head, &find) != NULL);
		reverse(dname, dname + find.len);
		if (matched) {
			if (frontend_conf->blocklist_log)
				log_info("blocking %s", dname);
			error_answer(pq, LDNS_RCODE_REFUSED);
			goto send_answer;
		}
	}

	if (pq->qinfo.qtype == LDNS_RR_TYPE_AXFR || pq->qinfo.qtype ==
	    LDNS_RR_TYPE_IXFR) {
		error_answer(pq, LDNS_RCODE_REFUSED);
		goto send_answer;
	}

	if(pq->qinfo.qtype == LDNS_RR_TYPE_OPT ||
	    pq->qinfo.qtype == LDNS_RR_TYPE_TSIG ||
	    pq->qinfo.qtype == LDNS_RR_TYPE_TKEY ||
	    pq->qinfo.qtype == LDNS_RR_TYPE_MAILA ||
	    pq->qinfo.qtype == LDNS_RR_TYPE_MAILB ||
	    (pq->qinfo.qtype >= 128 && pq->qinfo.qtype <= 248)) {
		error_answer(pq, LDNS_RCODE_FORMERR);
		goto send_answer;
	}

	if (pq->qinfo.qclass == LDNS_RR_CLASS_CH) {
		if (strcasecmp(dname, "version.server.") == 0 ||
		    strcasecmp(dname, "version.bind.") == 0) {
			chaos_answer(pq);
		} else
			error_answer(pq, LDNS_RCODE_REFUSED);
		goto send_answer;
	}

	if (strlcpy(query_imsg.qname, dname, sizeof(query_imsg.qname)) >=
	    sizeof(query_imsg.qname)) {
		log_warnx("qname too long");
		error_answer(pq, LDNS_RCODE_FORMERR);
		goto send_answer;
	}
	query_imsg.id = pq->imsg_id;
	query_imsg.t = pq->qinfo.qtype;
	query_imsg.c = pq->qinfo.qclass;

	if (frontend_imsg_compose_resolver(IMSG_QUERY, 0, &query_imsg,
	    sizeof(query_imsg)) == -1) {
		error_answer(pq, LDNS_RCODE_SERVFAIL);
		goto send_answer;
	}
	return;

 send_answer:
	send_answer(pq);
	return;

 drop:
	free_pending_query(pq);
}

void
noerror_answer(struct pending_query *pq)
{
	struct query_info		 skip, qinfo;
	struct reply_info		*rinfo = NULL;
	struct alloc_cache		 alloc;
	struct edns_data		 edns;
	struct ub_packed_rrset_key	*an_rrset = NULL;
	struct packed_rrset_data	*an_rrset_data = NULL;

	alloc_init(&alloc, NULL, 0);
	memset(&qinfo, 0, sizeof(qinfo));
	/* read past query section, no memory is allocated */
	if (!query_info_parse(&skip, pq->abuf))
		goto srvfail;

	if (reply_info_parse(pq->abuf, &alloc, &qinfo, &rinfo, pq->region,
	    &edns) != 0)
		goto srvfail;

	if ((an_rrset = reply_find_answer_rrset(&qinfo, rinfo)) != NULL)
		an_rrset_data = (struct packed_rrset_data*)an_rrset->entry.data;

	/* reply_info_parse() allocates memory */
	query_info_clear(&qinfo);

	/* XXX check that there a no AAAA records in answer section? */
	if ((an_rrset_data == NULL || an_rrset_data->count == 0) &&
	    !pq->dns64_synthesize && pq->qinfo.qtype == LDNS_RR_TYPE_AAAA &&
	    pq->qinfo.qclass == LDNS_RR_CLASS_IN && dns64_prefix_count > 0) {
		pq->dns64_synthesize = 1;
		return;
	}

	sldns_buffer_clear(pq->abuf);
	if (reply_info_encode(&pq->qinfo, rinfo, htons(pq->id), rinfo->flags,
	    pq->abuf, 0, pq->region, pq->tcp ? UINT16_MAX : pq->edns.udp_size,
	    pq->edns.bits & EDNS_DO, MINIMIZE_ANSWER) == 0)
		goto srvfail;

	reply_info_parsedelete(rinfo, &alloc);
	alloc_clear(&alloc);
	return;

 srvfail:
	reply_info_parsedelete(rinfo, &alloc);
	alloc_clear(&alloc);
	error_answer(pq, LDNS_RCODE_SERVFAIL);
}

void
synthesize_dns64_answer(struct pending_query *pq)
{
	struct query_info		 skip, qinfo;
	struct reply_info		*rinfo = NULL, *synth_rinfo = NULL;
	struct alloc_cache		 alloc;
	struct edns_data		 edns;
	size_t				 i;

	pq->dns64_synthesize = 0;

	alloc_init(&alloc, NULL, 0);
	memset(&qinfo, 0, sizeof(qinfo));
	/* read past query section, no memory is allocated */
	if (!query_info_parse(&skip, pq->abuf))
		goto srvfail;

	if (reply_info_parse(pq->abuf, &alloc, &qinfo, &rinfo, pq->region,
	    &edns) != 0)
		goto srvfail;

	/* reply_info_parse() allocates memory */
	query_info_clear(&qinfo);

	synth_rinfo = construct_reply_info_base(pq->region, rinfo->flags,
	    rinfo->qdcount, rinfo->ttl, rinfo->prefetch_ttl,
	    rinfo->serve_expired_ttl, rinfo->serve_expired_norec_ttl,
	    rinfo->an_numrrsets, rinfo->ns_numrrsets, rinfo->ar_numrrsets,
	    rinfo->rrset_count, rinfo->security, rinfo->reason_bogus);

	if (!synth_rinfo)
		goto srvfail;

	if(!reply_info_alloc_rrset_keys(synth_rinfo, NULL, pq->region))
		goto srvfail;

	for (i = 0; i < synth_rinfo->rrset_count; i++) {
		struct ub_packed_rrset_key	*src_rrset_key, *dst_rrset_key;
		struct packed_rrset_data	*src_rrset_data;
		struct packed_rrset_data	*dst_rrset_data;

		src_rrset_key = rinfo->rrsets[i];
		src_rrset_data =
		    (struct packed_rrset_data *)src_rrset_key->entry.data;
		dst_rrset_key = synth_rinfo->rrsets[i];

		dst_rrset_key->id = src_rrset_key->id;
		dst_rrset_key->rk = src_rrset_key->rk;

		if (i < rinfo->an_numrrsets && src_rrset_key->rk.type ==
		    htons(LDNS_RR_TYPE_A)) {
			dns64_synth_aaaa_data(src_rrset_key, src_rrset_data,
			    dst_rrset_key, &dst_rrset_data, pq->region);
			if (dst_rrset_data == NULL)
				goto srvfail;
		} else {
			dst_rrset_key->entry.hash = src_rrset_key->entry.hash;
			dst_rrset_key->rk.dname = regional_alloc_init(
			    pq->region, src_rrset_key->rk.dname,
			    src_rrset_key->rk.dname_len);
			if (dst_rrset_key->rk.dname == NULL)
				goto srvfail;

			dst_rrset_data = regional_alloc_init(pq->region,
			    src_rrset_data,
			    packed_rrset_sizeof(src_rrset_data));
			if (dst_rrset_data == NULL)
				goto srvfail;
		}

		packed_rrset_ptr_fixup(dst_rrset_data);
		dst_rrset_key->entry.data = dst_rrset_data;
	}

	if (!sldns_buffer_set_capacity(pq->abuf, pq->tcp ? UINT16_MAX :
	    pq->edns.udp_size))
		goto srvfail;

	sldns_buffer_clear(pq->abuf);

	if (reply_info_encode(&pq->qinfo, synth_rinfo, htons(pq->id),
	    synth_rinfo->flags, pq->abuf, 0, pq->region,
	    pq->tcp ? UINT16_MAX : pq->edns.udp_size,
	    pq->edns.bits & EDNS_DO, MINIMIZE_ANSWER) == 0)
		goto srvfail;

	reply_info_parsedelete(rinfo, &alloc);
	alloc_clear(&alloc);
	return;

 srvfail:
	reply_info_parsedelete(rinfo, &alloc);
	alloc_clear(&alloc);
	error_answer(pq, LDNS_RCODE_SERVFAIL);
}

void
resend_dns64_query(struct pending_query *opq)
{
	struct pending_query	*pq;
	struct query_imsg	 query_imsg;
	int			 rcode;
	char			 dname[LDNS_MAX_DOMAINLEN + 1];

	if ((pq = calloc(1, sizeof(*pq))) == NULL) {
		log_warn(NULL);
		return;
	}

	do {
		arc4random_buf(&pq->imsg_id, sizeof(pq->imsg_id));
	} while(find_pending_query(pq->imsg_id) != NULL);

	TAILQ_INSERT_TAIL(&pending_queries, pq, entry);

	pq->from = opq->from;
	pq->fd = opq->fd;
	opq->fd = -1;
	pq->tcp = opq->tcp;
	pq->qbuf = sldns_buffer_new(sldns_buffer_capacity(opq->qbuf));
	pq->abuf = sldns_buffer_new(sldns_buffer_capacity(opq->abuf));
	pq->region = regional_create();

	if (!pq->qbuf || !pq->abuf || !pq->region) {
		log_warnx("out of memory");
		free_pending_query(pq);
		free_pending_query(opq);
		return;
	}

	sldns_buffer_rewind(opq->qbuf);
	sldns_buffer_write(pq->qbuf, sldns_buffer_current(opq->qbuf),
	    sldns_buffer_remaining(opq->qbuf));
	sldns_buffer_flip(pq->qbuf);

	if (pq->tcp) {
		struct timeval	 timeout = {TCP_TIMEOUT, 0};

		event_set(&pq->ev, pq->fd, EV_READ | EV_PERSIST, tcp_request,
		    pq);
		event_set(&pq->resp_ev, pq->fd, EV_WRITE | EV_PERSIST,
		    tcp_response, pq);
		evtimer_set(&pq->tmo_ev, tcp_timeout, pq);
		evtimer_add(&pq->tmo_ev, &timeout);
	}

	if (sldns_buffer_remaining(pq->qbuf) < LDNS_HEADER_SIZE) {
		log_warnx("bad query: too short, dropped");
		goto drop;
	}

	pq->id = sldns_buffer_read_u16_at(pq->qbuf, 0);
	pq->flags = sldns_buffer_read_u16_at(pq->qbuf, 2);

	if (!query_info_parse(&pq->qinfo, pq->qbuf)) {
		log_warnx("query_info_parse failed");
		goto drop;
	}

	rcode = parse_edns_from_query_pkt(pq->qbuf, &pq->edns, NULL, NULL,
	    NULL, 0, pq->region, NULL);
	if (rcode != LDNS_RCODE_NOERROR) {
		error_answer(pq, rcode);
		goto send_answer;
	}

	dname_str(pq->qinfo.qname, dname);
	strlcpy(query_imsg.qname, dname, sizeof(query_imsg.qname));
	query_imsg.id = pq->imsg_id;
	query_imsg.t = LDNS_RR_TYPE_A;
	query_imsg.c = pq->qinfo.qclass;

	pq->dns64_synthesize = 1;

	if (frontend_imsg_compose_resolver(IMSG_QUERY, 0, &query_imsg,
	    sizeof(query_imsg)) == -1) {
		error_answer(pq, LDNS_RCODE_SERVFAIL);
		goto send_answer;
	}

	free_pending_query(opq);
	return;

 send_answer:
	free_pending_query(opq);
	send_answer(pq);
	return;

 drop:
	free_pending_query(opq);
	free_pending_query(pq);
}

void
chaos_answer(struct pending_query *pq)
{
	size_t		 len;
	const char	*name = "unwind";

	len = strlen(name);
	if (!sldns_buffer_set_capacity(pq->abuf,
	    sldns_buffer_capacity(pq->qbuf) + COMPRESSED_RR_SIZE + 1 + len)) {
		error_answer(pq, LDNS_RCODE_SERVFAIL);
		return;
	}

	sldns_buffer_copy(pq->abuf, pq->qbuf);

	sldns_buffer_clear(pq->abuf);

	sldns_buffer_skip(pq->abuf, sizeof(uint16_t));	/* skip id */
	sldns_buffer_write_u16(pq->abuf, 0);		/* clear flags */
	LDNS_QR_SET(sldns_buffer_begin(pq->abuf));
	LDNS_RA_SET(sldns_buffer_begin(pq->abuf));
	if (LDNS_RD_WIRE(sldns_buffer_begin(pq->qbuf)))
		LDNS_RD_SET(sldns_buffer_begin(pq->abuf));
	if (LDNS_CD_WIRE(sldns_buffer_begin(pq->qbuf)))
		LDNS_CD_SET(sldns_buffer_begin(pq->abuf));
	LDNS_RCODE_SET(sldns_buffer_begin(pq->abuf), LDNS_RCODE_NOERROR);
	sldns_buffer_write_u16(pq->abuf, 1);		/* qdcount */
	sldns_buffer_write_u16(pq->abuf, 1);		/* ancount */
	sldns_buffer_write_u16(pq->abuf, 0);		/* nscount */
	sldns_buffer_write_u16(pq->abuf, 0);		/* arcount */
	(void)query_dname_len(pq->abuf);		/* skip qname */
	sldns_buffer_skip(pq->abuf, sizeof(uint16_t));	/* skip qtype */
	sldns_buffer_skip(pq->abuf, sizeof(uint16_t));	/* skip qclass */

	sldns_buffer_write_u16(pq->abuf, 0xc00c);	/* ptr to query */
	sldns_buffer_write_u16(pq->abuf, LDNS_RR_TYPE_TXT);
	sldns_buffer_write_u16(pq->abuf, LDNS_RR_CLASS_CH);
	sldns_buffer_write_u32(pq->abuf, 0);		/* TTL */
	sldns_buffer_write_u16(pq->abuf, 1 + len);	/* RDLENGTH */
	sldns_buffer_write_u8(pq->abuf, len);		/* length octed */
	sldns_buffer_write(pq->abuf, name, len);
	sldns_buffer_flip(pq->abuf);
}

void
error_answer(struct pending_query *pq, int rcode)
{
	sldns_buffer_clear(pq->abuf);
	error_encode(pq->abuf, rcode, &pq->qinfo, htons(pq->id), pq->flags,
	    pq->edns.edns_present ? &pq->edns : NULL);
}

int
check_query(sldns_buffer* pkt)
{
	if(sldns_buffer_limit(pkt) < LDNS_HEADER_SIZE) {
		log_warnx("bad query: too short, dropped");
		return -1;
	}
	if(LDNS_QR_WIRE(sldns_buffer_begin(pkt))) {
		log_warnx("bad query: QR set, dropped");
		return -1;
	}
	if(LDNS_TC_WIRE(sldns_buffer_begin(pkt))) {
		LDNS_TC_CLR(sldns_buffer_begin(pkt));
		log_warnx("bad query: TC set");
		return (LDNS_RCODE_FORMERR);
	}
	if(!(LDNS_RD_WIRE(sldns_buffer_begin(pkt)))) {
		log_warnx("bad query: RD not set");
		return (LDNS_RCODE_REFUSED);
	}
	if(LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_QUERY) {
		log_warnx("bad query: unknown opcode %d",
		    LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)));
		return (LDNS_RCODE_NOTIMPL);
	}

	if (LDNS_QDCOUNT(sldns_buffer_begin(pkt)) != 1 &&
	    LDNS_ANCOUNT(sldns_buffer_begin(pkt))!= 0 &&
	    LDNS_NSCOUNT(sldns_buffer_begin(pkt))!= 0 &&
	    LDNS_ARCOUNT(sldns_buffer_begin(pkt)) > 1) {
		log_warnx("bad query: qdcount: %d, ancount: %d "
		    "nscount: %d, arcount: %d",
		    LDNS_QDCOUNT(sldns_buffer_begin(pkt)),
		    LDNS_ANCOUNT(sldns_buffer_begin(pkt)),
		    LDNS_NSCOUNT(sldns_buffer_begin(pkt)),
		    LDNS_ARCOUNT(sldns_buffer_begin(pkt)));
		return (LDNS_RCODE_FORMERR);
	}
	return (LDNS_RCODE_NOERROR);
}

void
send_answer(struct pending_query *pq)
{
	char	*str;

	if (log_getverbose() & OPT_VERBOSE2 && (str =
	    sldns_wire2str_pkt(sldns_buffer_begin(pq->abuf),
	    sldns_buffer_limit(pq->abuf))) != NULL) {
		log_debug("from: %s\n%s", ip_port((struct sockaddr *)
		    &pq->from), str);
		free(str);
	}

	if (!pq->tcp) {
		if(sendto(pq->fd, sldns_buffer_current(pq->abuf),
		    sldns_buffer_remaining(pq->abuf), 0,
		    (struct sockaddr *)&pq->from, pq->from.ss_len) == -1)
			log_warn("sendto");
		free_pending_query(pq);
	} else {
		struct sldns_buffer	*tmp;

		tmp = sldns_buffer_new(sldns_buffer_limit(pq->abuf) + 2);

		if (!tmp) {
			free_pending_query(pq);
			return;
		}

		sldns_buffer_write_u16(tmp, sldns_buffer_limit(pq->abuf));
		sldns_buffer_write(tmp, sldns_buffer_current(pq->abuf),
		    sldns_buffer_remaining(pq->abuf));
		sldns_buffer_flip(tmp);
		sldns_buffer_free(pq->abuf);
		pq->abuf = tmp;
		event_add(&pq->resp_ev, NULL);
	}
}

char*
ip_port(struct sockaddr *sa)
{
	static char	 hbuf[NI_MAXHOST], buf[NI_MAXHOST];

	if (getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf), NULL, 0,
	    NI_NUMERICHOST) != 0) {
		snprintf(buf, sizeof(buf), "%s", "(unknown)");
		return buf;
	}

	if (sa->sa_family == AF_INET6)
		snprintf(buf, sizeof(buf), "[%s]:%d", hbuf, ntohs(
		    ((struct sockaddr_in6 *)sa)->sin6_port));
	if (sa->sa_family == AF_INET)
		snprintf(buf, sizeof(buf), "[%s]:%d", hbuf, ntohs(
		    ((struct sockaddr_in *)sa)->sin_port));

	return buf;
}

struct pending_query*
find_pending_query(uint64_t id)
{
	struct pending_query	*pq;

	TAILQ_FOREACH(pq, &pending_queries, entry)
		if (pq->imsg_id == id)
			return pq;
	return NULL;
}

void
route_receive(int fd, short events, void *arg)
{
	static uint8_t		*buf;

	struct rt_msghdr	*rtm;
	struct sockaddr		*sa, *rti_info[RTAX_MAX];
	ssize_t			 n;

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
handle_route_message(struct rt_msghdr *rtm, struct sockaddr **rti_info)
{
	struct imsg_rdns_proposal	 rdns_proposal;
	struct sockaddr_rtdns		*rtdns;
	struct if_announcemsghdr	*ifan;

	switch (rtm->rtm_type) {
	case RTM_IFANNOUNCE:
		ifan = (struct if_announcemsghdr *)rtm;
		if (ifan->ifan_what == IFAN_ARRIVAL)
			break;
		rdns_proposal.if_index = ifan->ifan_index;
		rdns_proposal.src = 0;
		rdns_proposal.rtdns.sr_family = AF_INET;
		rdns_proposal.rtdns.sr_len = offsetof(struct sockaddr_rtdns,
		    sr_dns);
		frontend_imsg_compose_resolver(IMSG_REPLACE_DNS, 0,
		    &rdns_proposal, sizeof(rdns_proposal));
		break;
	case RTM_IFINFO:
		frontend_imsg_compose_resolver(IMSG_NETWORK_CHANGED, 0, NULL,
		    0);
		break;
	case RTM_PROPOSAL:
		if (!(rtm->rtm_addrs & RTA_DNS))
			break;

		rtdns = (struct sockaddr_rtdns*)rti_info[RTAX_DNS];
		rdns_proposal.if_index = rtm->rtm_index;
		rdns_proposal.src = rtm->rtm_priority;
		memcpy(&rdns_proposal.rtdns, rtdns, sizeof(rdns_proposal.rtdns));
		frontend_imsg_compose_resolver(IMSG_REPLACE_DNS, 0,
		    &rdns_proposal, sizeof(rdns_proposal));
		break;
	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_DESYNC:
		check_available_af();
		break;
	default:
		break;
	}
}

void
add_new_ta(struct trust_anchor_head *tah, char *val)
{
	struct trust_anchor	*ta, *i;
	int			 cmp;

	if ((ta = malloc(sizeof(*ta))) == NULL)
		fatal("%s", __func__);
	if ((ta->ta = strdup(val)) == NULL)
		fatal("%s", __func__);

	/* keep the list sorted to prevent churn if the order changes in DNS */
	TAILQ_FOREACH(i, tah, entry) {
		cmp = strcmp(i->ta, ta->ta);
		if ( cmp == 0) {
			/* duplicate */
			free(ta->ta);
			free(ta);
			return;
		} else if (cmp > 0) {
			TAILQ_INSERT_BEFORE(i, ta, entry);
			return;
		}
	}
	TAILQ_INSERT_TAIL(tah, ta, entry);
}

void
free_tas(struct trust_anchor_head *tah)
{
	struct trust_anchor	*ta;

	while ((ta = TAILQ_FIRST(tah))) {
		TAILQ_REMOVE(tah, ta, entry);
		free(ta->ta);
		free(ta);
	}
}

int
merge_tas(struct trust_anchor_head *newh, struct trust_anchor_head *oldh)
{
	struct trust_anchor	*i, *j;
	int			 chg = 0;

	j = TAILQ_FIRST(oldh);

	TAILQ_FOREACH(i, newh, entry) {
		if (j == NULL || strcmp(i->ta, j->ta) != 0) {
			chg = 1;
			break;
		}
		j = TAILQ_NEXT(j, entry);
	}
	if (j != NULL)
		chg = 1;

	if (chg) {
		free_tas(oldh);
		TAILQ_CONCAT(oldh, newh, entry);
	} else {
		free_tas(newh);
	}
	return (chg);
}

void
parse_trust_anchor(struct trust_anchor_head *tah, int fd)
{
	size_t	 len, dname_len;
	ssize_t	 n, sz;
	uint8_t	 rr[LDNS_RR_BUF_SIZE];
	char	*str, *p, buf[512], *line;

	sz = 0;
	str = NULL;

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		p = recallocarray(str, sz, sz + n, 1);
		if (p == NULL) {
			log_warn("%s", __func__);
			goto out;
		}
		str = p;
		memcpy(str + sz, buf, n);
		sz += n;
	}

	if (n == -1) {
		log_warn("%s", __func__);
		goto out;
	}

	/* make it a string */
	p = recallocarray(str, sz, sz + 1, 1);
	if (p == NULL) {
		log_warn("%s", __func__);
		goto out;
	}
	str = p;
	sz++;

	len = sizeof(rr);

	while ((line = strsep(&p, "\n")) != NULL) {
		if (sldns_str2wire_rr_buf(line, rr, &len, &dname_len,
		    ROOT_DNSKEY_TTL, NULL, 0, NULL, 0) != 0)
			continue;
		if (sldns_wirerr_get_type(rr, len, dname_len) ==
		    LDNS_RR_TYPE_DNSKEY)
			add_new_ta(tah, line);
	}

out:
	free(str);
	return;
}

void
send_trust_anchors(struct trust_anchor_head *tah)
{
	struct trust_anchor	*ta;

	TAILQ_FOREACH(ta, tah, entry)
		frontend_imsg_compose_resolver(IMSG_NEW_TA, 0, ta->ta,
		    strlen(ta->ta) + 1);
	frontend_imsg_compose_resolver(IMSG_NEW_TAS_DONE, 0, NULL, 0);
}

void
write_trust_anchors(struct trust_anchor_head *tah, int fd)
{
	struct trust_anchor	*ta;
	size_t			 len = 0;
	ssize_t			 n;
	char			*str;

	if (lseek(fd, 0, SEEK_SET) == -1) {
		log_warn("%s", __func__);
		goto out;
	}

	TAILQ_FOREACH(ta, tah, entry) {
		if ((n = asprintf(&str, "%s\n", ta->ta)) == -1) {
			log_warn("%s", __func__);
			len = 0;
			goto out;
		}
		len += n;
		if (write(fd, str, n) != n) {
			log_warn("%s", __func__);
			free(str);
			len = 0;
			goto out;
		}
		free(str);
	}
out:
	ftruncate(fd, len);
	fsync(fd);
}

void
parse_blocklist(int fd)
{
	FILE		 *f;
	struct bl_node	*bl_node;
	char		 *line = NULL;
	size_t		  linesize = 0;
	ssize_t		  linelen;

	if((f = fdopen(fd, "r")) == NULL) {
		log_warn("cannot read block list");
		close(fd);
		return;
	}

	free_bl();

	while ((linelen = getline(&line, &linesize, f)) != -1) {
		if (line[linelen - 1] == '\n') {
			if (linelen >= 2 && line[linelen - 2] != '.')
				line[linelen - 1] = '.';
			else
				line[linelen-- - 1] = '\0';
		}

		if (line[0] == '#')
		    continue;

		bl_node = malloc(sizeof *bl_node);
		if (bl_node == NULL)
			fatal("%s: malloc", __func__);
		if ((bl_node->domain = strdup(line)) == NULL)
			fatal("%s: strdup", __func__);
		reverse(bl_node->domain, bl_node->domain + linelen);
		bl_node->len = linelen;
		bl_node->wildcard = line[0] == '.';
		if (RB_INSERT(bl_tree, &bl_head, bl_node) != NULL) {
			log_warnx("duplicate blocked domain \"%s\"", line);
			free(bl_node->domain);
			free(bl_node);
		}
	}
	free(line);
	if (ferror(f))
		log_warn("getline");
	fclose(f);
}

int
bl_cmp(struct bl_node *e1, struct bl_node *e2) {
	if (e1->wildcard == e2->wildcard)
		return (strcasecmp(e1->domain, e2->domain));
	else if (e1->wildcard)
		return (strncasecmp(e1->domain, e2->domain, e1->len));
	else /* e2->wildcard */
		return (strncasecmp(e1->domain, e2->domain, e2->len));
}

void
free_bl(void)
{
	struct bl_node	*n, *nxt;

	RB_FOREACH_SAFE(n, bl_tree, &bl_head, nxt) {
		RB_REMOVE(bl_tree, &bl_head, n);
		free(n->domain);
		free(n);
	}
}

int
pending_query_cnt(void)
{
	struct pending_query	*e;
	int			 cnt = 0;

	TAILQ_FOREACH(e, &pending_queries, entry)
		cnt++;
	return cnt;
}

void
accept_paused(int fd, short events, void *arg)
{
	struct tcp_accept_ev	*tcpev = arg;
	event_add(&tcpev->ev, NULL);
}

int
accept_reserve(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	if (getdtablecount() + FD_RESERVE >= getdtablesize()) {
		log_debug("%s: inflight fds exceeded", __func__);
		errno = EMFILE;
		return -1;
	}
	return accept4(sockfd, addr, addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
}

void
tcp_accept(int fd, short events, void *arg)
{
	static struct timeval	 timeout = {TCP_TIMEOUT, 0};
	static struct timeval	 backoff = {1, 0};
	struct pending_query	*pq;
	struct tcp_accept_ev	*tcpev;
	struct sockaddr_storage	 ss;
	socklen_t		 len;
	int			 s;

	tcpev = arg;
	len = sizeof(ss);

	if ((s = accept_reserve(fd, (struct sockaddr *)&ss, &len)) == -1) {
		switch (errno) {
		case EINTR:
		case EWOULDBLOCK:
		case ECONNABORTED:
			return;
		case EMFILE:
		case ENFILE:
			event_del(&tcpev->ev);
			evtimer_add(&tcpev->pause, &backoff);
			return;
		default:
			fatal("accept");
		}
	}

	if ((pq = calloc(1, sizeof(*pq))) == NULL) {
		log_warn(NULL);
		close(s);
		return;
	}

	do {
		arc4random_buf(&pq->imsg_id, sizeof(pq->imsg_id));
	} while(find_pending_query(pq->imsg_id) != NULL);

	TAILQ_INSERT_TAIL(&pending_queries, pq, entry);

	pq->from = ss;
	pq->fd = s;
	pq->tcp = 1;
	pq->qbuf = sldns_buffer_new(DEFAULT_TCP_SIZE);
	pq->region = regional_create();

	if (!pq->qbuf || !pq->region) {
		free_pending_query(pq);
		return;
	}

	event_set(&pq->ev, s, EV_READ | EV_PERSIST, tcp_request, pq);
	event_add(&pq->ev, NULL);
	event_set(&pq->resp_ev, s, EV_WRITE | EV_PERSIST, tcp_response, pq);

	evtimer_set(&pq->tmo_ev, tcp_timeout, pq);
	evtimer_add(&pq->tmo_ev, &timeout);
}

void
tcp_request(int fd, short events, void *arg)
{
	struct pending_query	*pq;
	ssize_t			 n;

	pq = arg;

	n = read(fd, sldns_buffer_current(pq->qbuf),
	    sldns_buffer_remaining(pq->qbuf));

	switch (n) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			return;
		default:
			goto fail;
		}
		break;
	case 0:
		log_debug("closed connection");
		goto fail;
	default:
		break;
	}

	sldns_buffer_skip(pq->qbuf, n);

	if (sldns_buffer_position(pq->qbuf) >= 2 && !pq->abuf) {
		struct sldns_buffer	*tmp;
		size_t			 rem;
		uint16_t		 len;

		sldns_buffer_flip(pq->qbuf);
		len = sldns_buffer_read_u16(pq->qbuf);
		tmp = sldns_buffer_new(len);
		if (tmp == NULL)
			goto fail;
		pq->abuf = sldns_buffer_new(len);
		if (pq->abuf == NULL) {
			sldns_buffer_free(tmp);
			goto fail;
		}

		rem = sldns_buffer_remaining(pq->qbuf);
		sldns_buffer_write(tmp, sldns_buffer_current(pq->qbuf),
		    MINIMUM(len, rem));
		sldns_buffer_free(pq->qbuf);
		pq->qbuf = tmp;
	}
	if (sldns_buffer_remaining(pq->qbuf) == 0) {
		sldns_buffer_flip(pq->qbuf);
		shutdown(fd, SHUT_RD);
		event_del(&pq->ev);
		handle_query(pq);
	}
	return;
fail:
	free_pending_query(pq);
}

void
tcp_response(int fd, short events, void *arg)
{
	struct pending_query	*pq;
	ssize_t			 n;

	pq = arg;

	n = write(fd, sldns_buffer_current(pq->abuf),
	    sldns_buffer_remaining(pq->abuf));

	if (n == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		free_pending_query(pq);
		return;
	}
	sldns_buffer_skip(pq->abuf, n);
	if (sldns_buffer_remaining(pq->abuf) == 0)
		free_pending_query(pq);
}

void
tcp_timeout(int fd, short events, void *arg)
{
	free_pending_query(arg);
}

void
check_available_af(void)
{
	static int		 available_af = HAVE_IPV4 | HAVE_IPV6;
	static int		 rtable = -1;
	struct ifaddrs		*ifap, *ifa;
	struct if_data		*ifa_data;
	struct sockaddr_in	*sin4;
	struct sockaddr_in6	*sin6;
	int			 new_available_af = 0, ifa_rtable = -1;

	if (rtable == -1)
		rtable = getrtable();

	if (getifaddrs(&ifap) != 0) {
		log_warn("getifaddrs");
		return;
	}

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;
		switch(ifa->ifa_addr->sa_family) {
		case AF_LINK:
			/* AF_LINK comes before inet / inet6 on an interface */
			ifa_data = (struct if_data *)ifa->ifa_data;
			ifa_rtable = ifa_data->ifi_rdomain;
			break;
		case AF_INET:
			if (ifa_rtable != rtable)
				continue;

			sin4 = (struct sockaddr_in *)ifa->ifa_addr;
			if ((ntohl(sin4->sin_addr.s_addr) >> 24) ==
			    IN_LOOPBACKNET)
				continue;
			new_available_af |= HAVE_IPV4;
			break;
		case AF_INET6:
			if (ifa_rtable != rtable)
				continue;

			sin6 = (struct sockaddr_in6 *)ifa->ifa_addr;
			if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr) ||
			    IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) ||
			    IN6_IS_ADDR_MC_LINKLOCAL(&sin6->sin6_addr) ||
			    IN6_IS_ADDR_MC_INTFACELOCAL(&sin6->sin6_addr))
				continue;
			new_available_af |= HAVE_IPV6;
			break;
		default:
			break;
		}
		if (new_available_af == (HAVE_IPV4 | HAVE_IPV6))
			break;
	}
	freeifaddrs(ifap);
	if (new_available_af != available_af) {
		available_af = new_available_af;
		frontend_imsg_compose_resolver(IMSG_CHANGE_AFS, 0,
		    &available_af, sizeof(available_af));
	}
}

void
reverse(char *begin, char *end)
{
	char	t;

	while (begin < --end) {
		t = *begin;
		*begin = *end;
		*end = t;
		++begin;
	}
}
