/*	$OpenBSD: ldpe.c,v 1.88 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2008 Esben Norby <norby@openbsd.org>
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
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#include "ldpd.h"
#include "ldpe.h"
#include "lde.h"
#include "control.h"
#include "log.h"

static void	 ldpe_sig_handler(int, short, void *);
static __dead void ldpe_shutdown(void);
static void	 ldpe_dispatch_main(int, short, void *);
static void	 ldpe_dispatch_lde(int, short, void *);
static void	 ldpe_dispatch_pfkey(int, short, void *);
static void	 ldpe_setup_sockets(int, int, int, int);
static void	 ldpe_close_sockets(int);
static void	 ldpe_iface_af_ctl(struct ctl_conn *, int, unsigned int);

struct ldpd_conf	*leconf;
struct ldpd_sysdep	 sysdep;

static struct imsgev	*iev_main;
static struct imsgev	*iev_lde;
static struct event	 pfkey_ev;

static void
ldpe_sig_handler(int sig, short event, void *bula)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		ldpe_shutdown();
		/* NOTREACHED */
	default:
		fatalx("unexpected signal");
	}
}

/* label distribution protocol engine */
void
ldpe(int debug, int verbose, char *sockname)
{
	struct passwd		*pw;
	struct event		 ev_sigint, ev_sigterm;

	leconf = config_new_empty();

	log_init(debug);
	log_verbose(verbose);

	setproctitle("ldp engine");
	ldpd_process = PROC_LDP_ENGINE;
	log_procname = "ldpe";

	/* create ldpd control socket outside chroot */
	global.csock = sockname;
	if (control_init(global.csock) == -1)
		fatalx("control socket setup failed");

	LIST_INIT(&global.addr_list);
	LIST_INIT(&global.adj_list);
	TAILQ_INIT(&global.pending_conns);
	if (inet_pton(AF_INET, AllRouters_v4, &global.mcast_addr_v4) != 1)
		fatal("inet_pton");
	if (inet_pton(AF_INET6, AllRouters_v6, &global.mcast_addr_v6) != 1)
		fatal("inet_pton");
	global.pfkeysock = pfkey_init();

	if ((pw = getpwnam(LDPD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio inet mcast recvfd", NULL) == -1)
		fatal("pledge");

	event_init();
	accept_init();

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, ldpe_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, ldpe_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* setup pipe and event handler to the parent process */
	if ((iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_main->ibuf, 3) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_main->ibuf);
	iev_main->handler = ldpe_dispatch_main;
	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	if (sysdep.no_pfkey == 0) {
		event_set(&pfkey_ev, global.pfkeysock, EV_READ | EV_PERSIST,
		    ldpe_dispatch_pfkey, NULL);
		event_add(&pfkey_ev, NULL);
	}

	/* mark sockets as closed */
	global.ipv4.ldp_disc_socket = -1;
	global.ipv4.ldp_edisc_socket = -1;
	global.ipv4.ldp_session_socket = -1;
	global.ipv6.ldp_disc_socket = -1;
	global.ipv6.ldp_edisc_socket = -1;
	global.ipv6.ldp_session_socket = -1;

	/* listen on ldpd control socket */
	control_listen();

	event_dispatch();

	ldpe_shutdown();
}

static __dead void
ldpe_shutdown(void)
{
	struct if_addr		*if_addr;
	struct adj		*adj;

	/* close pipes */
	imsgbuf_write(&iev_lde->ibuf);
	imsgbuf_clear(&iev_lde->ibuf);
	close(iev_lde->ibuf.fd);
	imsgbuf_write(&iev_main->ibuf);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	control_cleanup();
	config_clear(leconf);

	if (sysdep.no_pfkey == 0) {
		event_del(&pfkey_ev);
		close(global.pfkeysock);
	}
	ldpe_close_sockets(AF_INET);
	ldpe_close_sockets(AF_INET6);

	/* remove addresses from global list */
	while ((if_addr = LIST_FIRST(&global.addr_list)) != NULL) {
		LIST_REMOVE(if_addr, entry);
		free(if_addr);
	}
	while ((adj = LIST_FIRST(&global.adj_list)) != NULL)
		adj_del(adj, S_SHUTDOWN);

	/* clean up */
	free(iev_lde);
	free(iev_main);

	log_info("ldp engine exiting");
	exit(0);
}

/* imesg */
int
ldpe_imsg_compose_parent(int type, pid_t pid, void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data, datalen));
}

int
ldpe_imsg_compose_lde(int type, uint32_t peerid, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_lde, type, peerid, pid, -1,
	    data, datalen));
}

static void
ldpe_dispatch_main(int fd, short event, void *bula)
{
	static struct ldpd_conf	*nconf;
	struct iface		*niface;
	struct tnbr		*ntnbr;
	struct nbr_params	*nnbrp;
	static struct l2vpn	*l2vpn, *nl2vpn;
	struct l2vpn_if		*lif = NULL, *nlif;
	struct l2vpn_pw		*npw;
	struct imsg		 imsg;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct iface		*iface = NULL;
	struct kif		*kif;
	int			 af;
	enum socket_type	*socket_type;
	static int		 disc_socket = -1;
	static int		 edisc_socket = -1;
	static int		 session_socket = -1;
	struct nbr		*nbr;
	int			 n, shut = 0;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* connection closed */
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
			fatal("ldpe_dispatch_main: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_IFSTATUS:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kif))
				fatalx("IFSTATUS imsg with wrong len");
			kif = imsg.data;

			iface = if_lookup(leconf, kif->ifindex);
			if (iface) {
				iface->flags = kif->flags;
				iface->linkstate = kif->link_state;
				if_update(iface, AF_UNSPEC);
				break;
			}

			LIST_FOREACH(l2vpn, &leconf->l2vpn_list, entry) {
				lif = l2vpn_if_find(l2vpn, kif->ifindex);
				if (lif) {
					lif->flags = kif->flags;
					lif->linkstate = kif->link_state;
					memcpy(lif->mac, kif->mac,
					    sizeof(lif->mac));
					l2vpn_if_update(lif);
					break;
				}
			}
			break;
		case IMSG_NEWADDR:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kaddr))
				fatalx("NEWADDR imsg with wrong len");

			if_addr_add(imsg.data);
			break;
		case IMSG_DELADDR:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct kaddr))
				fatalx("DELADDR imsg with wrong len");

			if_addr_del(imsg.data);
			break;
		case IMSG_SOCKET_IPC:
			if (iev_lde) {
				log_warnx("%s: received unexpected imsg fd "
				    "to lde", __func__);
				break;
			}
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("%s: expected to receive imsg fd to "
				    "lde but didn't receive any", __func__);
				break;
			}

			if ((iev_lde = malloc(sizeof(struct imsgev))) == NULL)
				fatal(NULL);
			if (imsgbuf_init(&iev_lde->ibuf, fd) == -1)
				fatal(NULL);
			iev_lde->handler = ldpe_dispatch_lde;
			iev_lde->events = EV_READ;
			event_set(&iev_lde->ev, iev_lde->ibuf.fd,
			    iev_lde->events, iev_lde->handler, iev_lde);
			event_add(&iev_lde->ev, NULL);
			break;
		case IMSG_CLOSE_SOCKETS:
			af = imsg.hdr.peerid;

			RB_FOREACH(nbr, nbr_id_head, &nbrs_by_id) {
				if (nbr->af != af)
					continue;
				session_shutdown(nbr, S_SHUTDOWN, 0, 0);
				pfkey_remove(nbr);
			}
			ldpe_close_sockets(af);
			if_update_all(af);
			tnbr_update_all(af);

			disc_socket = -1;
			edisc_socket = -1;
			session_socket = -1;
			if ((ldp_af_conf_get(leconf, af))->flags &
			    F_LDPD_AF_ENABLED)
				ldpe_imsg_compose_parent(IMSG_REQUEST_SOCKETS,
				    af, NULL, 0);
			break;
		case IMSG_SOCKET_NET:
			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(enum socket_type))
				fatalx("SOCKET_NET imsg with wrong len");
			socket_type = imsg.data;

			switch (*socket_type) {
			case LDP_SOCKET_DISC:
				disc_socket = imsg_get_fd(&imsg);
				break;
			case LDP_SOCKET_EDISC:
				edisc_socket = imsg_get_fd(&imsg);
				break;
			case LDP_SOCKET_SESSION:
				session_socket = imsg_get_fd(&imsg);
				break;
			}
			break;
		case IMSG_SETUP_SOCKETS:
			af = imsg.hdr.peerid;
			if (disc_socket == -1 || edisc_socket == -1 ||
			    session_socket == -1) {
				if (disc_socket != -1)
					close(disc_socket);
				if (edisc_socket != -1)
					close(edisc_socket);
				if (session_socket != -1)
					close(session_socket);
				break;
			}

			ldpe_setup_sockets(af, disc_socket, edisc_socket,
			    session_socket);
			if_update_all(af);
			tnbr_update_all(af);
			RB_FOREACH(nbr, nbr_id_head, &nbrs_by_id) {
				if (nbr->af != af)
					continue;
				nbr->laddr = (ldp_af_conf_get(leconf,
				    af))->trans_addr;
				if (pfkey_establish(nconf, nbr) == -1)
					fatalx("pfkey setup failed");
				if (nbr_session_active_role(nbr))
					nbr_establish_connection(nbr);
			}
			break;
		case IMSG_RECONF_CONF:
			if ((nconf = malloc(sizeof(struct ldpd_conf))) ==
			    NULL)
				fatal(NULL);
			memcpy(nconf, imsg.data, sizeof(struct ldpd_conf));

			LIST_INIT(&nconf->iface_list);
			LIST_INIT(&nconf->tnbr_list);
			LIST_INIT(&nconf->nbrp_list);
			LIST_INIT(&nconf->l2vpn_list);
			LIST_INIT(&nconf->auth_list);
			break;
		case IMSG_RECONF_IFACE:
			if ((niface = malloc(sizeof(struct iface))) == NULL)
				fatal(NULL);
			memcpy(niface, imsg.data, sizeof(struct iface));

			LIST_INIT(&niface->addr_list);
			LIST_INIT(&niface->ipv4.adj_list);
			LIST_INIT(&niface->ipv6.adj_list);
			niface->ipv4.iface = niface;
			niface->ipv6.iface = niface;

			LIST_INSERT_HEAD(&nconf->iface_list, niface, entry);
			break;
		case IMSG_RECONF_TNBR:
			if ((ntnbr = malloc(sizeof(struct tnbr))) == NULL)
				fatal(NULL);
			memcpy(ntnbr, imsg.data, sizeof(struct tnbr));

			LIST_INSERT_HEAD(&nconf->tnbr_list, ntnbr, entry);
			break;
		case IMSG_RECONF_NBRP:
			if ((nnbrp = malloc(sizeof(struct nbr_params))) == NULL)
				fatal(NULL);
			memcpy(nnbrp, imsg.data, sizeof(struct nbr_params));

			LIST_INSERT_HEAD(&nconf->nbrp_list, nnbrp, entry);
			break;
		case IMSG_RECONF_L2VPN:
			if ((nl2vpn = malloc(sizeof(struct l2vpn))) == NULL)
				fatal(NULL);
			memcpy(nl2vpn, imsg.data, sizeof(struct l2vpn));

			LIST_INIT(&nl2vpn->if_list);
			LIST_INIT(&nl2vpn->pw_list);

			LIST_INSERT_HEAD(&nconf->l2vpn_list, nl2vpn, entry);
			break;
		case IMSG_RECONF_L2VPN_IF:
			if ((nlif = malloc(sizeof(struct l2vpn_if))) == NULL)
				fatal(NULL);
			memcpy(nlif, imsg.data, sizeof(struct l2vpn_if));

			nlif->l2vpn = nl2vpn;
			LIST_INSERT_HEAD(&nl2vpn->if_list, nlif, entry);
			break;
		case IMSG_RECONF_L2VPN_PW:
			if ((npw = malloc(sizeof(struct l2vpn_pw))) == NULL)
				fatal(NULL);
			memcpy(npw, imsg.data, sizeof(struct l2vpn_pw));

			npw->l2vpn = nl2vpn;
			LIST_INSERT_HEAD(&nl2vpn->pw_list, npw, entry);
			break;
		case IMSG_RECONF_CONF_AUTH: {
			struct ldp_auth *auth;

			auth = malloc(sizeof(*auth));
			if (auth == NULL)
				fatal(NULL);

			memcpy(auth, imsg.data, sizeof(*auth));

			LIST_INSERT_HEAD(&nconf->auth_list, auth, entry);
			break;
		}

		case IMSG_RECONF_END:
			merge_config(leconf, nconf);
			nconf = NULL;
			global.conf_seqnum++;
			break;
		case IMSG_CTL_KROUTE:
		case IMSG_CTL_KROUTE_ADDR:
		case IMSG_CTL_IFINFO:
		case IMSG_CTL_END:
			control_imsg_relay(&imsg);
			break;
		default:
			log_debug("ldpe_dispatch_main: error handling imsg %d",
			    imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

static void
ldpe_dispatch_lde(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct map		 map;
	struct notify_msg	 nm;
	int			 n, shut = 0;
	struct nbr		*nbr = NULL;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* connection closed */
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
			fatal("ldpe_dispatch_lde: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_MAPPING_ADD:
		case IMSG_RELEASE_ADD:
		case IMSG_REQUEST_ADD:
		case IMSG_WITHDRAW_ADD:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(map))
				fatalx("invalid size of map request");
			memcpy(&map, imsg.data, sizeof(map));

			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("ldpe_dispatch_lde: cannot find "
				    "neighbor");
				break;
			}
			if (nbr->state != NBR_STA_OPER)
				break;

			switch (imsg.hdr.type) {
			case IMSG_MAPPING_ADD:
				mapping_list_add(&nbr->mapping_list, &map);
				break;
			case IMSG_RELEASE_ADD:
				mapping_list_add(&nbr->release_list, &map);
				break;
			case IMSG_REQUEST_ADD:
				mapping_list_add(&nbr->request_list, &map);
				break;
			case IMSG_WITHDRAW_ADD:
				mapping_list_add(&nbr->withdraw_list, &map);
				break;
			}
			break;
		case IMSG_MAPPING_ADD_END:
		case IMSG_RELEASE_ADD_END:
		case IMSG_REQUEST_ADD_END:
		case IMSG_WITHDRAW_ADD_END:
			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("ldpe_dispatch_lde: cannot find "
				    "neighbor");
				break;
			}
			if (nbr->state != NBR_STA_OPER)
				break;

			switch (imsg.hdr.type) {
			case IMSG_MAPPING_ADD_END:
				send_labelmessage(nbr, MSG_TYPE_LABELMAPPING,
				    &nbr->mapping_list);
				break;
			case IMSG_RELEASE_ADD_END:
				send_labelmessage(nbr, MSG_TYPE_LABELRELEASE,
				    &nbr->release_list);
				break;
			case IMSG_REQUEST_ADD_END:
				send_labelmessage(nbr, MSG_TYPE_LABELREQUEST,
				    &nbr->request_list);
				break;
			case IMSG_WITHDRAW_ADD_END:
				send_labelmessage(nbr, MSG_TYPE_LABELWITHDRAW,
				    &nbr->withdraw_list);
				break;
			}
			break;
		case IMSG_NOTIFICATION_SEND:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(nm))
				fatalx("invalid size of OE request");
			memcpy(&nm, imsg.data, sizeof(nm));

			nbr = nbr_find_peerid(imsg.hdr.peerid);
			if (nbr == NULL) {
				log_debug("ldpe_dispatch_lde: cannot find "
				    "neighbor");
				break;
			}
			if (nbr->state != NBR_STA_OPER)
				break;

			send_notification_full(nbr->tcp, &nm);
			break;
		case IMSG_CTL_END:
		case IMSG_CTL_SHOW_LIB:
		case IMSG_CTL_SHOW_L2VPN_PW:
		case IMSG_CTL_SHOW_L2VPN_BINDING:
			control_imsg_relay(&imsg);
			break;
		default:
			log_debug("ldpe_dispatch_lde: error handling imsg %d",
			    imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

static void
ldpe_dispatch_pfkey(int fd, short event, void *bula)
{
	if (event & EV_READ) {
		if (pfkey_read(fd, NULL) == -1) {
			fatal("pfkey_read failed, exiting...");
		}
	}
}

static void
ldpe_setup_sockets(int af, int disc_socket, int edisc_socket,
    int session_socket)
{
	struct ldpd_af_global	*af_global;

	af_global = ldp_af_global_get(&global, af);

	/* discovery socket */
	af_global->ldp_disc_socket = disc_socket;
	event_set(&af_global->disc_ev, af_global->ldp_disc_socket,
	    EV_READ|EV_PERSIST, disc_recv_packet, NULL);
	event_add(&af_global->disc_ev, NULL);

	/* extended discovery socket */
	af_global->ldp_edisc_socket = edisc_socket;
	event_set(&af_global->edisc_ev, af_global->ldp_edisc_socket,
	    EV_READ|EV_PERSIST, disc_recv_packet, NULL);
	event_add(&af_global->edisc_ev, NULL);

	/* session socket */
	af_global->ldp_session_socket = session_socket;
	accept_add(af_global->ldp_session_socket, session_accept, NULL);
}

static void
ldpe_close_sockets(int af)
{
	struct ldpd_af_global	*af_global;

	af_global = ldp_af_global_get(&global, af);

	/* discovery socket */
	if (event_initialized(&af_global->disc_ev))
		event_del(&af_global->disc_ev);
	if (af_global->ldp_disc_socket != -1) {
		close(af_global->ldp_disc_socket);
		af_global->ldp_disc_socket = -1;
	}

	/* extended discovery socket */
	if (event_initialized(&af_global->edisc_ev))
		event_del(&af_global->edisc_ev);
	if (af_global->ldp_edisc_socket != -1) {
		close(af_global->ldp_edisc_socket);
		af_global->ldp_edisc_socket = -1;
	}

	/* session socket */
	if (af_global->ldp_session_socket != -1) {
		accept_del(af_global->ldp_session_socket);
		close(af_global->ldp_session_socket);
		af_global->ldp_session_socket = -1;
	}
}

void
ldpe_reset_nbrs(int af)
{
	struct nbr		*nbr;

	RB_FOREACH(nbr, nbr_id_head, &nbrs_by_id) {
		if (nbr->af == af)
			session_shutdown(nbr, S_SHUTDOWN, 0, 0);
	}
}

void
ldpe_reset_ds_nbrs(void)
{
	struct nbr		*nbr;

	RB_FOREACH(nbr, nbr_id_head, &nbrs_by_id) {
		if (nbr->ds_tlv)
			session_shutdown(nbr, S_SHUTDOWN, 0, 0);
	}
}

void
ldpe_remove_dynamic_tnbrs(int af)
{
	struct tnbr		*tnbr, *safe;

	LIST_FOREACH_SAFE(tnbr, &leconf->tnbr_list, entry, safe) {
		if (tnbr->af != af)
			continue;

		tnbr->flags &= ~F_TNBR_DYNAMIC;
		tnbr_check(tnbr);
	}
}

void
ldpe_stop_init_backoff(int af)
{
	struct nbr		*nbr;

	RB_FOREACH(nbr, nbr_id_head, &nbrs_by_id) {
		if (nbr->af == af && nbr_pending_idtimer(nbr)) {
			nbr_stop_idtimer(nbr);
			nbr_establish_connection(nbr);
		}
	}
}

static void
ldpe_iface_af_ctl(struct ctl_conn *c, int af, unsigned int idx)
{
	struct iface		*iface;
	struct iface_af		*ia;
	struct ctl_iface	*ictl;

	LIST_FOREACH(iface, &leconf->iface_list, entry) {
		if (idx == 0 || idx == iface->ifindex) {
			ia = iface_af_get(iface, af);
			if (!ia->enabled)
				continue;

			ictl = if_to_ctl(ia);
			imsg_compose_event(&c->iev, IMSG_CTL_SHOW_INTERFACE,
			    0, 0, -1, ictl, sizeof(struct ctl_iface));
		}
	}
}

void
ldpe_iface_ctl(struct ctl_conn *c, unsigned int idx)
{
	ldpe_iface_af_ctl(c, AF_INET, idx);
	ldpe_iface_af_ctl(c, AF_INET6, idx);
}

void
ldpe_adj_ctl(struct ctl_conn *c)
{
	struct nbr	*nbr;
	struct adj	*adj;
	struct ctl_adj	*actl;

	RB_FOREACH(nbr, nbr_addr_head, &nbrs_by_addr) {
		LIST_FOREACH(adj, &nbr->adj_list, nbr_entry) {
			actl = adj_to_ctl(adj);
			imsg_compose_event(&c->iev, IMSG_CTL_SHOW_DISCOVERY,
			    0, 0, -1, actl, sizeof(struct ctl_adj));
		}
	}
	/* show adjacencies not associated with any neighbor */
	LIST_FOREACH(adj, &global.adj_list, global_entry) {
		if (adj->nbr != NULL)
			continue;

		actl = adj_to_ctl(adj);
		imsg_compose_event(&c->iev, IMSG_CTL_SHOW_DISCOVERY, 0, 0,
		    -1, actl, sizeof(struct ctl_adj));
	}

	imsg_compose_event(&c->iev, IMSG_CTL_END, 0, 0, -1, NULL, 0);
}

void
ldpe_nbr_ctl(struct ctl_conn *c)
{
	struct nbr	*nbr;
	struct ctl_nbr	*nctl;

	RB_FOREACH(nbr, nbr_addr_head, &nbrs_by_addr) {
		nctl = nbr_to_ctl(nbr);
		imsg_compose_event(&c->iev, IMSG_CTL_SHOW_NBR, 0, 0, -1, nctl,
		    sizeof(struct ctl_nbr));
	}
	imsg_compose_event(&c->iev, IMSG_CTL_END, 0, 0, -1, NULL, 0);
}

void
mapping_list_add(struct mapping_head *mh, struct map *map)
{
	struct mapping_entry	*me;

	me = calloc(1, sizeof(*me));
	if (me == NULL)
		fatal(__func__);
	me->map = *map;

	TAILQ_INSERT_TAIL(mh, me, entry);
}

void
mapping_list_clr(struct mapping_head *mh)
{
	struct mapping_entry	*me;

	while ((me = TAILQ_FIRST(mh)) != NULL) {
		TAILQ_REMOVE(mh, me, entry);
		free(me);
	}
}
