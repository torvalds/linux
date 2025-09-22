/*	$OpenBSD: lde.c,v 1.84 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
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
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netmpls/mpls.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <limits.h>

#include "ldp.h"
#include "ldpd.h"
#include "ldpe.h"
#include "log.h"
#include "lde.h"

static void		 lde_sig_handler(int sig, short, void *);
static __dead void	 lde_shutdown(void);
static int		 lde_imsg_compose_parent(int, pid_t, void *, uint16_t);
static void		 lde_dispatch_imsg(int, short, void *);
static void		 lde_dispatch_parent(int, short, void *);
static __inline	int	 lde_nbr_compare(struct lde_nbr *,
			    struct lde_nbr *);
static struct lde_nbr	*lde_nbr_new(uint32_t, struct lde_nbr *);
static void		 lde_nbr_del(struct lde_nbr *);
static struct lde_nbr	*lde_nbr_find(uint32_t);
static void		 lde_nbr_clear(void);
static void		 lde_nbr_addr_update(struct lde_nbr *,
			    struct lde_addr *, int);
static void		 lde_map_free(void *);
static int		 lde_address_add(struct lde_nbr *, struct lde_addr *);
static int		 lde_address_del(struct lde_nbr *, struct lde_addr *);
static void		 lde_address_list_free(struct lde_nbr *);

RB_GENERATE(nbr_tree, lde_nbr, entry, lde_nbr_compare)

struct ldpd_conf	*ldeconf;
struct nbr_tree		 lde_nbrs = RB_INITIALIZER(&lde_nbrs);

static struct imsgev	*iev_ldpe;
static struct imsgev	*iev_main;

static void
lde_sig_handler(int sig, short event, void *arg)
{
	/*
	 * signal handler rules don't apply, libevent decouples for us
	 */

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		lde_shutdown();
		/* NOTREACHED */
	default:
		fatalx("unexpected signal");
	}
}

/* label decision engine */
void
lde(int debug, int verbose)
{
	struct event		 ev_sigint, ev_sigterm;
	struct timeval		 now;
	struct passwd		*pw;

	ldeconf = config_new_empty();

	log_init(debug);
	log_verbose(verbose);

	setproctitle("label decision engine");
	ldpd_process = PROC_LDE_ENGINE;
	log_procname = "lde";

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

	if (pledge("stdio recvfd", NULL) == -1)
		fatal("pledge");

	event_init();

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, lde_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, lde_sig_handler, NULL);
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
	iev_main->handler = lde_dispatch_parent;
	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	/* setup and start the LIB garbage collector */
	evtimer_set(&gc_timer, lde_gc_timer, NULL);
	lde_gc_start_timer();

	gettimeofday(&now, NULL);
	global.uptime = now.tv_sec;

	event_dispatch();

	lde_shutdown();
}

static __dead void
lde_shutdown(void)
{
	/* close pipes */
	imsgbuf_clear(&iev_ldpe->ibuf);
	close(iev_ldpe->ibuf.fd);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	lde_gc_stop_timer();
	lde_nbr_clear();
	fec_tree_clear();

	config_clear(ldeconf);

	free(iev_ldpe);
	free(iev_main);

	log_info("label decision engine exiting");
	exit(0);
}

/* imesg */
static int
lde_imsg_compose_parent(int type, pid_t pid, void *data, uint16_t datalen)
{
	return (imsg_compose_event(iev_main, type, 0, pid, -1, data, datalen));
}

int
lde_imsg_compose_ldpe(int type, uint32_t peerid, pid_t pid, void *data,
    uint16_t datalen)
{
	return (imsg_compose_event(iev_ldpe, type, peerid, pid,
	     -1, data, datalen));
}

static void
lde_dispatch_imsg(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct lde_nbr		*ln;
	struct map		 map;
	struct lde_addr		 lde_addr;
	struct notify_msg	 nm;
	ssize_t			 n;
	int			 shut = 0, verbose;

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
			fatal("lde_dispatch_imsg: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_LABEL_MAPPING_FULL:
			ln = lde_nbr_find(imsg.hdr.peerid);
			if (ln == NULL) {
				log_debug("%s: cannot find lde neighbor",
				    __func__);
				break;
			}

			fec_snap(ln);
			break;
		case IMSG_LABEL_MAPPING:
		case IMSG_LABEL_REQUEST:
		case IMSG_LABEL_RELEASE:
		case IMSG_LABEL_WITHDRAW:
		case IMSG_LABEL_ABORT:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(map))
				fatalx("lde_dispatch_imsg: wrong imsg len");
			memcpy(&map, imsg.data, sizeof(map));

			ln = lde_nbr_find(imsg.hdr.peerid);
			if (ln == NULL) {
				log_debug("%s: cannot find lde neighbor",
				    __func__);
				break;
			}

			switch (imsg.hdr.type) {
			case IMSG_LABEL_MAPPING:
				lde_check_mapping(&map, ln);
				break;
			case IMSG_LABEL_REQUEST:
				lde_check_request(&map, ln);
				break;
			case IMSG_LABEL_RELEASE:
				lde_check_release(&map, ln);
				break;
			case IMSG_LABEL_WITHDRAW:
				lde_check_withdraw(&map, ln);
				break;
			case IMSG_LABEL_ABORT:
				/* not necessary */
				break;
			}
			break;
		case IMSG_ADDRESS_ADD:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(lde_addr))
				fatalx("lde_dispatch_imsg: wrong imsg len");
			memcpy(&lde_addr, imsg.data, sizeof(lde_addr));

			ln = lde_nbr_find(imsg.hdr.peerid);
			if (ln == NULL) {
				log_debug("%s: cannot find lde neighbor",
				    __func__);
				break;
			}
			if (lde_address_add(ln, &lde_addr) < 0) {
				log_debug("%s: cannot add address %s, it "
				    "already exists", __func__,
				    log_addr(lde_addr.af, &lde_addr.addr));
			}
			break;
		case IMSG_ADDRESS_DEL:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(lde_addr))
				fatalx("lde_dispatch_imsg: wrong imsg len");
			memcpy(&lde_addr, imsg.data, sizeof(lde_addr));

			ln = lde_nbr_find(imsg.hdr.peerid);
			if (ln == NULL) {
				log_debug("%s: cannot find lde neighbor",
				    __func__);
				break;
			}
			if (lde_address_del(ln, &lde_addr) < 0) {
				log_debug("%s: cannot delete address %s, it "
				    "does not exist", __func__,
				    log_addr(lde_addr.af, &lde_addr.addr));
			}
			break;
		case IMSG_NOTIFICATION:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(nm))
				fatalx("lde_dispatch_imsg: wrong imsg len");
			memcpy(&nm, imsg.data, sizeof(nm));

			ln = lde_nbr_find(imsg.hdr.peerid);
			if (ln == NULL) {
				log_debug("%s: cannot find lde neighbor",
				    __func__);
				break;
			}

			switch (nm.status_code) {
			case S_PW_STATUS:
				l2vpn_recv_pw_status(ln, &nm);
				break;
			case S_ENDOFLIB:
				/*
				 * Do nothing for now. Should be useful in
				 * the future when we implement LDP-IGP
				 * Synchronization (RFC 5443) and Graceful
				 * Restart (RFC 3478).
				 */
			default:
				break;
			}
			break;
		case IMSG_NEIGHBOR_UP:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct lde_nbr))
				fatalx("lde_dispatch_imsg: wrong imsg len");

			if (lde_nbr_find(imsg.hdr.peerid))
				fatalx("lde_dispatch_imsg: "
				    "neighbor already exists");
			lde_nbr_new(imsg.hdr.peerid, imsg.data);
			break;
		case IMSG_NEIGHBOR_DOWN:
			lde_nbr_del(lde_nbr_find(imsg.hdr.peerid));
			break;
		case IMSG_CTL_SHOW_LIB:
			rt_dump(imsg.hdr.pid);

			lde_imsg_compose_ldpe(IMSG_CTL_END, 0,
			    imsg.hdr.pid, NULL, 0);
			break;
		case IMSG_CTL_SHOW_L2VPN_PW:
			l2vpn_pw_ctl(imsg.hdr.pid);

			lde_imsg_compose_ldpe(IMSG_CTL_END, 0,
			    imsg.hdr.pid, NULL, 0);
			break;
		case IMSG_CTL_SHOW_L2VPN_BINDING:
			l2vpn_binding_ctl(imsg.hdr.pid);

			lde_imsg_compose_ldpe(IMSG_CTL_END, 0,
			    imsg.hdr.pid, NULL, 0);
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by ldpe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_verbose(verbose);
			break;
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
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

static void
lde_dispatch_parent(int fd, short event, void *bula)
{
	static struct ldpd_conf	*nconf;
	struct iface		*niface;
	struct tnbr		*ntnbr;
	struct nbr_params	*nnbrp;
	static struct l2vpn	*nl2vpn;
	struct l2vpn_if		*nlif;
	struct l2vpn_pw		*npw;
	struct imsg		 imsg;
	struct kroute		 kr;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	ssize_t			 n;
	int			 shut = 0;
	struct fec		 fec;

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
			fatal("lde_dispatch_parent: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_NETWORK_ADD:
		case IMSG_NETWORK_DEL:
			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(kr)) {
				log_warnx("%s: wrong imsg len", __func__);
				break;
			}
			memcpy(&kr, imsg.data, sizeof(kr));

			switch (kr.af) {
			case AF_INET:
				fec.type = FEC_TYPE_IPV4;
				fec.u.ipv4.prefix = kr.prefix.v4;
				fec.u.ipv4.prefixlen = kr.prefixlen;
				break;
			case AF_INET6:
				fec.type = FEC_TYPE_IPV6;
				fec.u.ipv6.prefix = kr.prefix.v6;
				fec.u.ipv6.prefixlen = kr.prefixlen;
				break;
			default:
				fatalx("lde_dispatch_parent: unknown af");
			}

			switch (imsg.hdr.type) {
			case IMSG_NETWORK_ADD:
				lde_kernel_insert(&fec, kr.af, &kr.nexthop,
				    kr.priority, kr.flags & F_CONNECTED, NULL);
				break;
			case IMSG_NETWORK_DEL:
				lde_kernel_remove(&fec, kr.af, &kr.nexthop,
				    kr.priority);
				break;
			}
			break;
		case IMSG_SOCKET_IPC:
			if (iev_ldpe) {
				log_warnx("%s: received unexpected imsg fd "
				    "to ldpe", __func__);
				break;
			}
			if ((fd = imsg_get_fd(&imsg)) == -1) {
				log_warnx("%s: expected to receive imsg fd to "
				    "ldpe but didn't receive any", __func__);
				break;
			}

			if ((iev_ldpe = malloc(sizeof(struct imsgev))) == NULL)
				fatal(NULL);
			if (imsgbuf_init(&iev_ldpe->ibuf, fd) == -1)
				fatal(NULL);
			iev_ldpe->handler = lde_dispatch_imsg;
			iev_ldpe->events = EV_READ;
			event_set(&iev_ldpe->ev, iev_ldpe->ibuf.fd,
			    iev_ldpe->events, iev_ldpe->handler, iev_ldpe);
			event_add(&iev_ldpe->ev, NULL);
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
			merge_config(ldeconf, nconf);
			nconf = NULL;
			break;
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
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

uint32_t
lde_assign_label(void)
{
	static uint32_t label = MPLS_LABEL_RESERVED_MAX;

	/* XXX some checks needed */
	label++;
	return (label);
}

void
lde_send_change_klabel(struct fec_node *fn, struct fec_nh *fnh)
{
	struct kroute	kr;
	struct kpw	kpw;
	struct l2vpn_pw	*pw;

	switch (fn->fec.type) {
	case FEC_TYPE_IPV4:
		memset(&kr, 0, sizeof(kr));
		kr.af = AF_INET;
		kr.prefix.v4 = fn->fec.u.ipv4.prefix;
		kr.prefixlen = fn->fec.u.ipv4.prefixlen;
		kr.nexthop.v4 = fnh->nexthop.v4;
		kr.local_label = fn->local_label;
		kr.remote_label = fnh->remote_label;
		kr.priority = fnh->priority;

		lde_imsg_compose_parent(IMSG_KLABEL_CHANGE, 0, &kr,
		    sizeof(kr));

		if (fn->fec.u.ipv4.prefixlen == 32)
			l2vpn_sync_pws(AF_INET, (union ldpd_addr *)
			    &fn->fec.u.ipv4.prefix);
		break;
	case FEC_TYPE_IPV6:
		memset(&kr, 0, sizeof(kr));
		kr.af = AF_INET6;
		kr.prefix.v6 = fn->fec.u.ipv6.prefix;
		kr.prefixlen = fn->fec.u.ipv6.prefixlen;
		kr.nexthop.v6 = fnh->nexthop.v6;
		kr.local_label = fn->local_label;
		kr.remote_label = fnh->remote_label;
		kr.priority = fnh->priority;

		lde_imsg_compose_parent(IMSG_KLABEL_CHANGE, 0, &kr,
		    sizeof(kr));

		if (fn->fec.u.ipv6.prefixlen == 128)
			l2vpn_sync_pws(AF_INET6, (union ldpd_addr *)
			    &fn->fec.u.ipv6.prefix);
		break;
	case FEC_TYPE_PWID:
		if (fn->local_label == NO_LABEL ||
		    fnh->remote_label == NO_LABEL)
			return;

		pw = (struct l2vpn_pw *) fn->data;
		pw->flags |= F_PW_STATUS_UP;

		memset(&kpw, 0, sizeof(kpw));
		kpw.ifindex = pw->ifindex;
		kpw.pw_type = fn->fec.u.pwid.type;
		kpw.af = pw->af;
		kpw.nexthop = pw->addr;
		kpw.local_label = fn->local_label;
		kpw.remote_label = fnh->remote_label;
		kpw.flags = pw->flags;

		lde_imsg_compose_parent(IMSG_KPWLABEL_CHANGE, 0, &kpw,
		    sizeof(kpw));
		break;
	}
}

void
lde_send_delete_klabel(struct fec_node *fn, struct fec_nh *fnh)
{
	struct kroute	 kr;
	struct kpw	 kpw;
	struct l2vpn_pw	*pw;

	switch (fn->fec.type) {
	case FEC_TYPE_IPV4:
		memset(&kr, 0, sizeof(kr));
		kr.af = AF_INET;
		kr.prefix.v4 = fn->fec.u.ipv4.prefix;
		kr.prefixlen = fn->fec.u.ipv4.prefixlen;
		kr.nexthop.v4 = fnh->nexthop.v4;
		kr.local_label = fn->local_label;
		kr.remote_label = fnh->remote_label;
		kr.priority = fnh->priority;

		lde_imsg_compose_parent(IMSG_KLABEL_DELETE, 0, &kr,
		    sizeof(kr));

		if (fn->fec.u.ipv4.prefixlen == 32)
			l2vpn_sync_pws(AF_INET, (union ldpd_addr *)
			    &fn->fec.u.ipv4.prefix);
		break;
	case FEC_TYPE_IPV6:
		memset(&kr, 0, sizeof(kr));
		kr.af = AF_INET6;
		kr.prefix.v6 = fn->fec.u.ipv6.prefix;
		kr.prefixlen = fn->fec.u.ipv6.prefixlen;
		kr.nexthop.v6 = fnh->nexthop.v6;
		kr.local_label = fn->local_label;
		kr.remote_label = fnh->remote_label;
		kr.priority = fnh->priority;

		lde_imsg_compose_parent(IMSG_KLABEL_DELETE, 0, &kr,
		    sizeof(kr));

		if (fn->fec.u.ipv6.prefixlen == 128)
			l2vpn_sync_pws(AF_INET6, (union ldpd_addr *)
			    &fn->fec.u.ipv6.prefix);
		break;
	case FEC_TYPE_PWID:
		pw = (struct l2vpn_pw *) fn->data;
		if (!(pw->flags & F_PW_STATUS_UP))
			return;
		pw->flags &= ~F_PW_STATUS_UP;

		memset(&kpw, 0, sizeof(kpw));
		kpw.ifindex = pw->ifindex;
		kpw.pw_type = fn->fec.u.pwid.type;
		kpw.af = pw->af;
		kpw.nexthop = pw->addr;
		kpw.local_label = fn->local_label;
		kpw.remote_label = fnh->remote_label;
		kpw.flags = pw->flags;

		lde_imsg_compose_parent(IMSG_KPWLABEL_DELETE, 0, &kpw,
		    sizeof(kpw));
		break;
	}
}

void
lde_fec2map(struct fec *fec, struct map *map)
{
	memset(map, 0, sizeof(*map));

	switch (fec->type) {
	case FEC_TYPE_IPV4:
		map->type = MAP_TYPE_PREFIX;
		map->fec.prefix.af = AF_INET;
		map->fec.prefix.prefix.v4 = fec->u.ipv4.prefix;
		map->fec.prefix.prefixlen = fec->u.ipv4.prefixlen;
		break;
	case FEC_TYPE_IPV6:
		map->type = MAP_TYPE_PREFIX;
		map->fec.prefix.af = AF_INET6;
		map->fec.prefix.prefix.v6 = fec->u.ipv6.prefix;
		map->fec.prefix.prefixlen = fec->u.ipv6.prefixlen;
		break;
	case FEC_TYPE_PWID:
		map->type = MAP_TYPE_PWID;
		map->fec.pwid.type = fec->u.pwid.type;
		map->fec.pwid.group_id = 0;
		map->flags |= F_MAP_PW_ID;
		map->fec.pwid.pwid = fec->u.pwid.pwid;
		break;
	}
}

void
lde_map2fec(struct map *map, struct in_addr lsr_id, struct fec *fec)
{
	memset(fec, 0, sizeof(*fec));

	switch (map->type) {
	case MAP_TYPE_PREFIX:
		switch (map->fec.prefix.af) {
		case AF_INET:
			fec->type = FEC_TYPE_IPV4;
			fec->u.ipv4.prefix = map->fec.prefix.prefix.v4;
			fec->u.ipv4.prefixlen = map->fec.prefix.prefixlen;
			break;
		case AF_INET6:
			fec->type = FEC_TYPE_IPV6;
			fec->u.ipv6.prefix = map->fec.prefix.prefix.v6;
			fec->u.ipv6.prefixlen = map->fec.prefix.prefixlen;
			break;
		default:
			fatalx("lde_map2fec: unknown af");
			break;
		}
		break;
	case MAP_TYPE_PWID:
		fec->type = FEC_TYPE_PWID;
		fec->u.pwid.type = map->fec.pwid.type;
		fec->u.pwid.pwid = map->fec.pwid.pwid;
		fec->u.pwid.lsr_id = lsr_id;
		break;
	}
}

void
lde_send_labelmapping(struct lde_nbr *ln, struct fec_node *fn, int single)
{
	struct lde_req	*lre;
	struct lde_map	*me;
	struct map	 map;
	struct l2vpn_pw	*pw;

	/*
	 * This function skips SL.1 - 3 and SL.9 - 14 because the label
	 * allocation is done way earlier (because of the merging nature of
	 * ldpd).
	 */

	lde_fec2map(&fn->fec, &map);
	switch (fn->fec.type) {
	case FEC_TYPE_IPV4:
		if (!ln->v4_enabled)
			return;
		break;
	case FEC_TYPE_IPV6:
		if (!ln->v6_enabled)
			return;
		break;
	case FEC_TYPE_PWID:
		pw = (struct l2vpn_pw *) fn->data;
		if (pw == NULL || pw->lsr_id.s_addr != ln->id.s_addr)
			/* not the remote end of the pseudowire */
			return;

		map.flags |= F_MAP_PW_IFMTU;
		map.fec.pwid.ifmtu = pw->l2vpn->mtu;
		if (pw->flags & F_PW_CWORD)
			map.flags |= F_MAP_PW_CWORD;
		if (pw->flags & F_PW_STATUSTLV) {
			map.flags |= F_MAP_PW_STATUS;
			/* VPLS are always up */
			map.pw_status = PW_FORWARDING;
		}
		break;
	}
	map.label = fn->local_label;

	/* SL.6: is there a pending request for this mapping? */
	lre = (struct lde_req *)fec_find(&ln->recv_req, &fn->fec);
	if (lre) {
		/* set label request msg id in the mapping response. */
		map.requestid = lre->msg_id;
		map.flags = F_MAP_REQ_ID;

		/* SL.7: delete record of pending request */
		lde_req_del(ln, lre, 0);
	}

	/* SL.4: send label mapping */
	lde_imsg_compose_ldpe(IMSG_MAPPING_ADD, ln->peerid, 0,
	    &map, sizeof(map));
	if (single)
		lde_imsg_compose_ldpe(IMSG_MAPPING_ADD_END, ln->peerid, 0,
		    NULL, 0);

	/* SL.5: record sent label mapping */
	me = (struct lde_map *)fec_find(&ln->sent_map, &fn->fec);
	if (me == NULL)
		me = lde_map_add(ln, fn, 1);
	me->map = map;
}

void
lde_send_labelwithdraw(struct lde_nbr *ln, struct fec_node *fn,
    struct map *wcard, struct status_tlv *st)
{
	struct lde_wdraw	*lw;
	struct map		 map;
	struct fec		*f;
	struct l2vpn_pw		*pw;

	if (fn) {
		lde_fec2map(&fn->fec, &map);
		switch (fn->fec.type) {
		case FEC_TYPE_IPV4:
			if (!ln->v4_enabled)
				return;
			break;
		case FEC_TYPE_IPV6:
			if (!ln->v6_enabled)
				return;
			break;
		case FEC_TYPE_PWID:
			pw = (struct l2vpn_pw *) fn->data;
			if (pw == NULL || pw->lsr_id.s_addr != ln->id.s_addr)
				/* not the remote end of the pseudowire */
				return;

			if (pw->flags & F_PW_CWORD)
				map.flags |= F_MAP_PW_CWORD;
			break;
		}
		map.label = fn->local_label;
	} else
		memcpy(&map, wcard, sizeof(map));

	if (st) {
		map.st.status_code = st->status_code;
		map.st.msg_id = st->msg_id;
		map.st.msg_type = st->msg_type;
		map.flags |= F_MAP_STATUS;
	}

	/* SWd.1: send label withdraw. */
	lde_imsg_compose_ldpe(IMSG_WITHDRAW_ADD, ln->peerid, 0,
 	    &map, sizeof(map));
	lde_imsg_compose_ldpe(IMSG_WITHDRAW_ADD_END, ln->peerid, 0, NULL, 0);

	/* SWd.2: record label withdraw. */
	if (fn) {
		lw = (struct lde_wdraw *)fec_find(&ln->sent_wdraw, &fn->fec);
		if (lw == NULL)
			lw = lde_wdraw_add(ln, fn);
		lw->label = map.label;
	} else {
		struct lde_map *me;

		RB_FOREACH(f, fec_tree, &ft) {
			fn = (struct fec_node *)f;
			me = (struct lde_map *)fec_find(&ln->sent_map, &fn->fec);
			if (lde_wildcard_apply(wcard, &fn->fec, me) == 0)
				continue;

			lw = (struct lde_wdraw *)fec_find(&ln->sent_wdraw,
			    &fn->fec);
			if (lw == NULL)
				lw = lde_wdraw_add(ln, fn);
			lw->label = map.label;
		}
	}
}

void
lde_send_labelwithdraw_wcard(struct lde_nbr *ln, uint32_t label)
{
	struct map	 wcard;

	memset(&wcard, 0, sizeof(wcard));
	wcard.type = MAP_TYPE_WILDCARD;
	wcard.label = label;
	lde_send_labelwithdraw(ln, NULL, &wcard, NULL);
}

void
lde_send_labelwithdraw_twcard_prefix(struct lde_nbr *ln, uint16_t af,
    uint32_t label)
{
	struct map	 wcard;

	memset(&wcard, 0, sizeof(wcard));
	wcard.type = MAP_TYPE_TYPED_WCARD;
	wcard.fec.twcard.type = MAP_TYPE_PREFIX;
	wcard.fec.twcard.u.prefix_af = af;
	wcard.label = label;
	lde_send_labelwithdraw(ln, NULL, &wcard, NULL);
}

void
lde_send_labelwithdraw_twcard_pwid(struct lde_nbr *ln, uint16_t pw_type,
    uint32_t label)
{
	struct map	 wcard;

	memset(&wcard, 0, sizeof(wcard));
	wcard.type = MAP_TYPE_TYPED_WCARD;
	wcard.fec.twcard.type = MAP_TYPE_PWID;
	wcard.fec.twcard.u.pw_type = pw_type;
	wcard.label = label;
	lde_send_labelwithdraw(ln, NULL, &wcard, NULL);
}

void
lde_send_labelwithdraw_pwid_wcard(struct lde_nbr *ln, uint16_t pw_type,
    uint32_t group_id)
{
	struct map	 wcard;

	memset(&wcard, 0, sizeof(wcard));
	wcard.type = MAP_TYPE_PWID;
	wcard.fec.pwid.type = pw_type;
	wcard.fec.pwid.group_id = group_id;
	/* we can not append a Label TLV when using PWid group wildcards. */
	wcard.label = NO_LABEL;
	lde_send_labelwithdraw(ln, NULL, &wcard, NULL);
}

void
lde_send_labelrelease(struct lde_nbr *ln, struct fec_node *fn,
    struct map *wcard, uint32_t label)
{
	struct map		 map;
	struct l2vpn_pw		*pw;

	if (fn) {
		lde_fec2map(&fn->fec, &map);
		switch (fn->fec.type) {
		case FEC_TYPE_IPV4:
			if (!ln->v4_enabled)
				return;
			break;
		case FEC_TYPE_IPV6:
			if (!ln->v6_enabled)
				return;
			break;
		case FEC_TYPE_PWID:
			pw = (struct l2vpn_pw *) fn->data;
			if (pw == NULL || pw->lsr_id.s_addr != ln->id.s_addr)
				/* not the remote end of the pseudowire */
				return;

			if (pw->flags & F_PW_CWORD)
				map.flags |= F_MAP_PW_CWORD;
			break;
		}
	} else
		memcpy(&map, wcard, sizeof(map));
	map.label = label;

	lde_imsg_compose_ldpe(IMSG_RELEASE_ADD, ln->peerid, 0,
	    &map, sizeof(map));
	lde_imsg_compose_ldpe(IMSG_RELEASE_ADD_END, ln->peerid, 0, NULL, 0);
}

void
lde_send_notification(struct lde_nbr *ln, uint32_t status_code, uint32_t msg_id,
    uint16_t msg_type)
{
	struct notify_msg nm;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = status_code;
	/* 'msg_id' and 'msg_type' should be in network byte order */
	nm.msg_id = msg_id;
	nm.msg_type = msg_type;

	lde_imsg_compose_ldpe(IMSG_NOTIFICATION_SEND, ln->peerid, 0,
	    &nm, sizeof(nm));
}

void
lde_send_notification_eol_prefix(struct lde_nbr *ln, int af)
{
	struct notify_msg nm;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = S_ENDOFLIB;
	nm.fec.type = MAP_TYPE_TYPED_WCARD;
	nm.fec.fec.twcard.type = MAP_TYPE_PREFIX;
	nm.fec.fec.twcard.u.prefix_af = af;
	nm.flags |= F_NOTIF_FEC;

	lde_imsg_compose_ldpe(IMSG_NOTIFICATION_SEND, ln->peerid, 0,
	    &nm, sizeof(nm));
}

void
lde_send_notification_eol_pwid(struct lde_nbr *ln, uint16_t pw_type)
{
	struct notify_msg nm;

	memset(&nm, 0, sizeof(nm));
	nm.status_code = S_ENDOFLIB;
	nm.fec.type = MAP_TYPE_TYPED_WCARD;
	nm.fec.fec.twcard.type = MAP_TYPE_PWID;
	nm.fec.fec.twcard.u.pw_type = pw_type;
	nm.flags |= F_NOTIF_FEC;

	lde_imsg_compose_ldpe(IMSG_NOTIFICATION_SEND, ln->peerid, 0,
	    &nm, sizeof(nm));
}

static __inline int
lde_nbr_compare(struct lde_nbr *a, struct lde_nbr *b)
{
	return (a->peerid - b->peerid);
}

static struct lde_nbr *
lde_nbr_new(uint32_t peerid, struct lde_nbr *new)
{
	struct lde_nbr	*ln;

	if ((ln = calloc(1, sizeof(*ln))) == NULL)
		fatal(__func__);

	ln->id = new->id;
	ln->v4_enabled = new->v4_enabled;
	ln->v6_enabled = new->v6_enabled;
	ln->flags = new->flags;
	ln->peerid = peerid;
	fec_init(&ln->recv_map);
	fec_init(&ln->sent_map);
	fec_init(&ln->recv_req);
	fec_init(&ln->sent_req);
	fec_init(&ln->sent_wdraw);

	TAILQ_INIT(&ln->addr_list);

	if (RB_INSERT(nbr_tree, &lde_nbrs, ln) != NULL)
		fatalx("lde_nbr_new: RB_INSERT failed");

	return (ln);
}

static void
lde_nbr_del(struct lde_nbr *ln)
{
	struct fec		*f;
	struct fec_node		*fn;
	struct fec_nh		*fnh;
	struct l2vpn_pw		*pw;

	if (ln == NULL)
		return;

	/* uninstall received mappings */
	RB_FOREACH(f, fec_tree, &ft) {
		fn = (struct fec_node *)f;

		LIST_FOREACH(fnh, &fn->nexthops, entry) {
			switch (f->type) {
			case FEC_TYPE_IPV4:
			case FEC_TYPE_IPV6:
				if (!lde_address_find(ln, fnh->af,
				    &fnh->nexthop))
					continue;
				break;
			case FEC_TYPE_PWID:
				if (f->u.pwid.lsr_id.s_addr != ln->id.s_addr)
					continue;
				pw = (struct l2vpn_pw *) fn->data;
				if (pw)
					l2vpn_pw_reset(pw);
				break;
			default:
				break;
			}

			lde_send_delete_klabel(fn, fnh);
			fnh->remote_label = NO_LABEL;
		}
	}

	lde_address_list_free(ln);

	fec_clear(&ln->recv_map, lde_map_free);
	fec_clear(&ln->sent_map, lde_map_free);
	fec_clear(&ln->recv_req, free);
	fec_clear(&ln->sent_req, free);
	fec_clear(&ln->sent_wdraw, free);

	RB_REMOVE(nbr_tree, &lde_nbrs, ln);

	free(ln);
}

static struct lde_nbr *
lde_nbr_find(uint32_t peerid)
{
	struct lde_nbr		 ln;

	ln.peerid = peerid;

	return (RB_FIND(nbr_tree, &lde_nbrs, &ln));
}

struct lde_nbr *
lde_nbr_find_by_lsrid(struct in_addr addr)
{
	struct lde_nbr		*ln;

	RB_FOREACH(ln, nbr_tree, &lde_nbrs)
		if (ln->id.s_addr == addr.s_addr)
			return (ln);

	return (NULL);
}

struct lde_nbr *
lde_nbr_find_by_addr(int af, union ldpd_addr *addr)
{
	struct lde_nbr		*ln;

	RB_FOREACH(ln, nbr_tree, &lde_nbrs)
		if (lde_address_find(ln, af, addr) != NULL)
			return (ln);

	return (NULL);
}

static void
lde_nbr_clear(void)
{
	struct lde_nbr	*ln;

	while ((ln = RB_ROOT(&lde_nbrs)) != NULL)
		lde_nbr_del(ln);
}

static void
lde_nbr_addr_update(struct lde_nbr *ln, struct lde_addr *lde_addr, int removed)
{
	struct fec		*fec;
	struct fec_node		*fn;
	struct fec_nh		*fnh;
	struct lde_map		*me;

	RB_FOREACH(fec, fec_tree, &ln->recv_map) {
		fn = (struct fec_node *)fec_find(&ft, fec);
		switch (fec->type) {
		case FEC_TYPE_IPV4:
			if (lde_addr->af != AF_INET)
				continue;
			break;
		case FEC_TYPE_IPV6:
			if (lde_addr->af != AF_INET6)
				continue;
			break;
		default:
			continue;
		}

		LIST_FOREACH(fnh, &fn->nexthops, entry) {
			if (ldp_addrcmp(fnh->af, &fnh->nexthop,
			    &lde_addr->addr))
				continue;

			if (removed) {
				lde_send_delete_klabel(fn, fnh);
				fnh->remote_label = NO_LABEL;
			} else {
				me = (struct lde_map *)fec;
				fnh->remote_label = me->map.label;
				lde_send_change_klabel(fn, fnh);
			}
			break;
		}
	}
}

struct lde_map *
lde_map_add(struct lde_nbr *ln, struct fec_node *fn, int sent)
{
	struct lde_map  *me;

	me = calloc(1, sizeof(*me));
	if (me == NULL)
		fatal(__func__);

	me->fec = fn->fec;
	me->nexthop = ln;

	if (sent) {
		LIST_INSERT_HEAD(&fn->upstream, me, entry);
		if (fec_insert(&ln->sent_map, &me->fec))
			log_warnx("failed to add %s to sent map",
			    log_fec(&me->fec));
			/* XXX on failure more cleanup is needed */
	} else {
		LIST_INSERT_HEAD(&fn->downstream, me, entry);
		if (fec_insert(&ln->recv_map, &me->fec))
			log_warnx("failed to add %s to recv map",
			    log_fec(&me->fec));
	}

	return (me);
}

void
lde_map_del(struct lde_nbr *ln, struct lde_map *me, int sent)
{
	if (sent)
		fec_remove(&ln->sent_map, &me->fec);
	else
		fec_remove(&ln->recv_map, &me->fec);

	lde_map_free(me);
}

static void
lde_map_free(void *ptr)
{
	struct lde_map	*map = ptr;

	LIST_REMOVE(map, entry);
	free(map);
}

struct lde_req *
lde_req_add(struct lde_nbr *ln, struct fec *fec, int sent)
{
	struct fec_tree	*t;
	struct lde_req	*lre;

	t = sent ? &ln->sent_req : &ln->recv_req;

	lre = calloc(1, sizeof(*lre));
	if (lre != NULL) {
		lre->fec = *fec;

		if (fec_insert(t, &lre->fec)) {
			log_warnx("failed to add %s to %s req",
			    log_fec(&lre->fec), sent ? "sent" : "recv");
			free(lre);
			return (NULL);
		}
	}

	return (lre);
}

void
lde_req_del(struct lde_nbr *ln, struct lde_req *lre, int sent)
{
	if (sent)
		fec_remove(&ln->sent_req, &lre->fec);
	else
		fec_remove(&ln->recv_req, &lre->fec);

	free(lre);
}

struct lde_wdraw *
lde_wdraw_add(struct lde_nbr *ln, struct fec_node *fn)
{
	struct lde_wdraw  *lw;

	lw = calloc(1, sizeof(*lw));
	if (lw == NULL)
		fatal(__func__);

	lw->fec = fn->fec;

	if (fec_insert(&ln->sent_wdraw, &lw->fec))
		log_warnx("failed to add %s to sent wdraw",
		    log_fec(&lw->fec));

	return (lw);
}

void
lde_wdraw_del(struct lde_nbr *ln, struct lde_wdraw *lw)
{
	fec_remove(&ln->sent_wdraw, &lw->fec);
	free(lw);
}

void
lde_change_egress_label(int af, int was_implicit)
{
	struct lde_nbr	*ln;
	struct fec	*f;
	struct fec_node	*fn;

	RB_FOREACH(ln, nbr_tree, &lde_nbrs) {
		/* explicit withdraw */
		if (was_implicit)
			lde_send_labelwithdraw_wcard(ln, MPLS_LABEL_IMPLNULL);
		else {
			if (ln->v4_enabled)
				lde_send_labelwithdraw_wcard(ln,
				    MPLS_LABEL_IPV4NULL);
			if (ln->v6_enabled)
				lde_send_labelwithdraw_wcard(ln,
				    MPLS_LABEL_IPV6NULL);
		}

		/* advertise new label of connected prefixes */
		RB_FOREACH(f, fec_tree, &ft) {
			fn = (struct fec_node *)f;
			if (fn->local_label > MPLS_LABEL_RESERVED_MAX)
				continue;

			switch (af) {
			case AF_INET:
				if (fn->fec.type != FEC_TYPE_IPV4)
					continue;
				break;
			case AF_INET6:
				if (fn->fec.type != FEC_TYPE_IPV6)
					continue;
				break;
			default:
				fatalx("lde_change_egress_label: unknown af");
			}

			fn->local_label = egress_label(fn->fec.type);
			lde_send_labelmapping(ln, fn, 0);
		}

		lde_imsg_compose_ldpe(IMSG_MAPPING_ADD_END, ln->peerid, 0,
		    NULL, 0);
	}
}

static int
lde_address_add(struct lde_nbr *ln, struct lde_addr *lde_addr)
{
	struct lde_addr		*new;

	if (lde_address_find(ln, lde_addr->af, &lde_addr->addr) != NULL)
		return (-1);

	if ((new = calloc(1, sizeof(*new))) == NULL)
		fatal(__func__);

	new->af = lde_addr->af;
	new->addr = lde_addr->addr;
	TAILQ_INSERT_TAIL(&ln->addr_list, new, entry);

	/* reevaluate the previously received mappings from this neighbor */
	lde_nbr_addr_update(ln, lde_addr, 0);

	return (0);
}

static int
lde_address_del(struct lde_nbr *ln, struct lde_addr *lde_addr)
{
	lde_addr = lde_address_find(ln, lde_addr->af, &lde_addr->addr);
	if (lde_addr == NULL)
		return (-1);

	/* reevaluate the previously received mappings from this neighbor */
	lde_nbr_addr_update(ln, lde_addr, 1);

	TAILQ_REMOVE(&ln->addr_list, lde_addr, entry);
	free(lde_addr);

	return (0);
}

struct lde_addr *
lde_address_find(struct lde_nbr *ln, int af, union ldpd_addr *addr)
{
	struct lde_addr		*lde_addr;

	TAILQ_FOREACH(lde_addr, &ln->addr_list, entry)
		if (lde_addr->af == af &&
		    ldp_addrcmp(af, &lde_addr->addr, addr) == 0)
			return (lde_addr);

	return (NULL);
}

static void
lde_address_list_free(struct lde_nbr *ln)
{
	struct lde_addr		*lde_addr;

	while ((lde_addr = TAILQ_FIRST(&ln->addr_list)) != NULL) {
		TAILQ_REMOVE(&ln->addr_list, lde_addr, entry);
		free(lde_addr);
	}
}
