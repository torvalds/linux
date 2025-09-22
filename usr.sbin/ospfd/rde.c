/*	$OpenBSD: rde.c,v 1.118 2024/11/21 13:38:14 claudio Exp $ */

/*
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
#include <sys/socket.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <event.h>

#include "ospf.h"
#include "ospfd.h"
#include "ospfe.h"
#include "log.h"
#include "rde.h"

void		 rde_sig_handler(int sig, short, void *);
__dead void	 rde_shutdown(void);
void		 rde_dispatch_imsg(int, short, void *);
void		 rde_dispatch_parent(int, short, void *);
void		 rde_dump_area(struct area *, int, pid_t);

void		 rde_send_summary(pid_t);
void		 rde_send_summary_area(struct area *, pid_t);
void		 rde_nbr_init(u_int32_t);
void		 rde_nbr_free(void);
struct rde_nbr	*rde_nbr_find(u_int32_t);
struct rde_nbr	*rde_nbr_new(u_int32_t, struct rde_nbr *);
void		 rde_nbr_del(struct rde_nbr *);

void		 rde_req_list_add(struct rde_nbr *, struct lsa_hdr *);
int		 rde_req_list_exists(struct rde_nbr *, struct lsa_hdr *);
void		 rde_req_list_del(struct rde_nbr *, struct lsa_hdr *);
void		 rde_req_list_free(struct rde_nbr *);

struct iface	*rde_asext_lookup(u_int32_t, int);
void		 rde_asext_get(struct kroute *);
void		 rde_asext_put(struct kroute *);
void		 rde_asext_free(void);
struct lsa	*orig_asext_lsa(struct kroute *, u_int32_t, u_int16_t);
struct lsa	*orig_sum_lsa(struct rt_node *, struct area *, u_int8_t, int);

struct ospfd_conf	*rdeconf = NULL, *nconf = NULL;
static struct imsgev	*iev_ospfe;
static struct imsgev	*iev_main;
struct rde_nbr		*nbrself;
struct lsa_tree		 asext_tree;

void
rde_sig_handler(int sig, short event, void *arg)
{
	/*
	 * signal handler rules don't apply, libevent decouples for us
	 */

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		rde_shutdown();
		/* NOTREACHED */
	default:
		fatalx("unexpected signal");
	}
}

/* route decision engine */
pid_t
rde(struct ospfd_conf *xconf, int pipe_parent2rde[2], int pipe_ospfe2rde[2],
    int pipe_parent2ospfe[2])
{
	struct event		 ev_sigint, ev_sigterm;
	struct timeval		 now;
	struct area		*area;
	struct iface		*iface;
	struct passwd		*pw;
	pid_t			 pid;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
		/* NOTREACHED */
	case 0:
		break;
	default:
		return (pid);
	}

	/* cleanup a bit */
	kif_clear();

	rdeconf = xconf;

	if ((pw = getpwnam(OSPFD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("route decision engine");
	/*
	 * XXX needed with fork+exec
	 * log_init(debug, LOG_DAEMON);
	 * log_setverbose(verbose);
	 */

	ospfd_process = PROC_RDE_ENGINE;
	log_procinit(log_procnames[ospfd_process]);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio", NULL) == -1)
		fatal("pledge");

	event_init();
	rde_nbr_init(NBR_HASHSIZE);
	lsa_init(&asext_tree);

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, rde_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, rde_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* setup pipes */
	close(pipe_ospfe2rde[0]);
	close(pipe_parent2rde[0]);
	close(pipe_parent2ospfe[0]);
	close(pipe_parent2ospfe[1]);

	if ((iev_ospfe = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_ospfe->ibuf, pipe_ospfe2rde[1]) == -1)
		fatal(NULL);
	iev_ospfe->handler = rde_dispatch_imsg;
	if (imsgbuf_init(&iev_main->ibuf, pipe_parent2rde[1]) == -1)
		fatal(NULL);
	iev_main->handler = rde_dispatch_parent;

	/* setup event handler */
	iev_ospfe->events = EV_READ;
	event_set(&iev_ospfe->ev, iev_ospfe->ibuf.fd, iev_ospfe->events,
	    iev_ospfe->handler, iev_ospfe);
	event_add(&iev_ospfe->ev, NULL);

	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);

	evtimer_set(&rdeconf->ev, spf_timer, rdeconf);
	cand_list_init();
	rt_init();

	/* remove unneeded stuff from config */
	LIST_FOREACH(area, &rdeconf->area_list, entry)
		LIST_FOREACH(iface, &area->iface_list, entry)
			md_list_clr(&iface->auth_md_list);

	conf_clear_redist_list(&rdeconf->redist_list);

	gettimeofday(&now, NULL);
	rdeconf->uptime = now.tv_sec;

	event_dispatch();

	rde_shutdown();
	/* NOTREACHED */

	return (0);
}

__dead void
rde_shutdown(void)
{
	struct area	*a;
	struct vertex	*v, *nv;

	/* close pipes */
	imsgbuf_clear(&iev_ospfe->ibuf);
	close(iev_ospfe->ibuf.fd);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	stop_spf_timer(rdeconf);
	cand_list_clr();
	rt_clear();

	while ((a = LIST_FIRST(&rdeconf->area_list)) != NULL) {
		LIST_REMOVE(a, entry);
		area_del(a);
	}
	for (v = RB_MIN(lsa_tree, &asext_tree); v != NULL; v = nv) {
		nv = RB_NEXT(lsa_tree, &asext_tree, v);
		vertex_free(v);
	}
	rde_asext_free();
	rde_nbr_free();

	free(iev_ospfe);
	free(iev_main);
	free(rdeconf);

	log_info("route decision engine exiting");
	_exit(0);
}

int
rde_imsg_compose_ospfe(int type, u_int32_t peerid, pid_t pid, void *data,
    u_int16_t datalen)
{
	return (imsg_compose_event(iev_ospfe, type, peerid, pid, -1,
	    data, datalen));
}

void
rde_dispatch_imsg(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf;
	struct imsg		 imsg;
	struct in_addr		 aid;
	struct ls_req_hdr	 req_hdr;
	struct lsa_hdr		 lsa_hdr, *db_hdr;
	struct rde_nbr		 rn, *nbr;
	struct timespec		 tp;
	struct lsa		*lsa;
	struct area		*area;
	struct in_addr		 addr;
	struct vertex		*v;
	char			*buf;
	ssize_t			 n;
	time_t			 now;
	int			 r, state, self, error, shut = 0, verbose;
	u_int16_t		 l;

	ibuf = &iev->ibuf;

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

	clock_gettime(CLOCK_MONOTONIC, &tp);
	now = tp.tv_sec;

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("rde_dispatch_imsg: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_NEIGHBOR_UP:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(rn))
				fatalx("invalid size of OE request");
			memcpy(&rn, imsg.data, sizeof(rn));

			if (rde_nbr_new(imsg.hdr.peerid, &rn) == NULL)
				fatalx("rde_dispatch_imsg: "
				    "neighbor already exists");
			break;
		case IMSG_NEIGHBOR_DOWN:
			rde_nbr_del(rde_nbr_find(imsg.hdr.peerid));
			break;
		case IMSG_NEIGHBOR_ADDR:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(addr))
				fatalx("invalid size of OE request");
			memcpy(&addr, imsg.data, sizeof(addr));

			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			nbr->addr.s_addr = addr.s_addr;
			break;
		case IMSG_NEIGHBOR_CHANGE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(state))
				fatalx("invalid size of OE request");
			memcpy(&state, imsg.data, sizeof(state));

			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			nbr->state = state;
			if (nbr->state & NBR_STA_FULL)
				rde_req_list_free(nbr);
			break;
		case IMSG_NEIGHBOR_CAPA:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(u_int8_t))
				fatalx("invalid size of OE request");
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;
			nbr->capa_options = *(u_int8_t *)imsg.data;
			break;
		case IMSG_AREA_CHANGE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(state))
				fatalx("invalid size of OE request");

			LIST_FOREACH(area, &rdeconf->area_list, entry) {
				if (area->id.s_addr == imsg.hdr.peerid)
					break;
			}
			if (area == NULL)
				break;
			memcpy(&state, imsg.data, sizeof(state));
			area->active = state;
			break;
		case IMSG_DB_SNAPSHOT:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			lsa_snap(nbr);

			imsg_compose_event(iev_ospfe, IMSG_DB_END, imsg.hdr.peerid,
			    0, -1, NULL, 0);
			break;
		case IMSG_DD:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			buf = imsg.data;
			error = 0;
			for (l = imsg.hdr.len - IMSG_HEADER_SIZE;
			    l >= sizeof(lsa_hdr); l -= sizeof(lsa_hdr)) {
				memcpy(&lsa_hdr, buf, sizeof(lsa_hdr));
				buf += sizeof(lsa_hdr);

				if (lsa_hdr.type == LSA_TYPE_EXTERNAL &&
				    nbr->area->stub) {
					error = 1;
					break;
				}
				v = lsa_find(nbr->iface, lsa_hdr.type,
				    lsa_hdr.ls_id, lsa_hdr.adv_rtr);
				if (v == NULL)
					db_hdr = NULL;
				else
					db_hdr = &v->lsa->hdr;

				if (lsa_newer(&lsa_hdr, db_hdr) > 0) {
					/*
					 * only request LSAs that are
					 * newer or missing
					 */
					rde_req_list_add(nbr, &lsa_hdr);
					imsg_compose_event(iev_ospfe, IMSG_DD,
					    imsg.hdr.peerid, 0, -1, &lsa_hdr,
					    sizeof(lsa_hdr));
				}
			}
			if (l != 0 && !error)
				log_warnx("rde_dispatch_imsg: peerid %u, "
				    "trailing garbage in Database Description "
				    "packet", imsg.hdr.peerid);

			if (!error)
				imsg_compose_event(iev_ospfe, IMSG_DD_END,
				    imsg.hdr.peerid, 0, -1, NULL, 0);
			else
				imsg_compose_event(iev_ospfe, IMSG_DD_BADLSA,
				    imsg.hdr.peerid, 0, -1, NULL, 0);
			break;
		case IMSG_LS_REQ:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			buf = imsg.data;
			for (l = imsg.hdr.len - IMSG_HEADER_SIZE;
			    l >= sizeof(req_hdr); l -= sizeof(req_hdr)) {
				memcpy(&req_hdr, buf, sizeof(req_hdr));
				buf += sizeof(req_hdr);

				if ((v = lsa_find(nbr->iface,
				    ntohl(req_hdr.type), req_hdr.ls_id,
				    req_hdr.adv_rtr)) == NULL) {
					log_debug("rde_dispatch_imsg: "
					    "requested LSA not found");
					imsg_compose_event(iev_ospfe,
					    IMSG_LS_BADREQ, imsg.hdr.peerid,
					    0, -1, NULL, 0);
					continue;
				}
				imsg_compose_event(iev_ospfe, IMSG_LS_UPD,
				    imsg.hdr.peerid, 0, -1, v->lsa,
				    ntohs(v->lsa->hdr.len));
			}
			if (l != 0)
				log_warnx("rde_dispatch_imsg: peerid %u, "
				    "trailing garbage in LS Request "
				    "packet", imsg.hdr.peerid);
			break;
		case IMSG_LS_UPD:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			lsa = malloc(imsg.hdr.len - IMSG_HEADER_SIZE);
			if (lsa == NULL)
				fatal(NULL);
			memcpy(lsa, imsg.data, imsg.hdr.len - IMSG_HEADER_SIZE);

			if (!lsa_check(nbr, lsa,
			    imsg.hdr.len - IMSG_HEADER_SIZE)) {
				free(lsa);
				break;
			}

			v = lsa_find(nbr->iface, lsa->hdr.type, lsa->hdr.ls_id,
			    lsa->hdr.adv_rtr);
			if (v == NULL)
				db_hdr = NULL;
			else
				db_hdr = &v->lsa->hdr;

			if (nbr->self) {
				lsa_merge(nbr, lsa, v);
				/* lsa_merge frees the right lsa */
				break;
			}

			r = lsa_newer(&lsa->hdr, db_hdr);
			if (r > 0) {
				/* new LSA newer than DB */
				if (v && v->flooded &&
				    v->changed + MIN_LS_ARRIVAL >= now) {
					free(lsa);
					break;
				}

				rde_req_list_del(nbr, &lsa->hdr);

				if (!(self = lsa_self(nbr, lsa, v)))
					if (lsa_add(nbr, lsa))
						/* delayed lsa */
						break;

				/* flood and perhaps ack LSA */
				imsg_compose_event(iev_ospfe, IMSG_LS_FLOOD,
				    imsg.hdr.peerid, 0, -1, lsa,
				    ntohs(lsa->hdr.len));

				/* reflood self originated LSA */
				if (self && v)
					imsg_compose_event(iev_ospfe,
					    IMSG_LS_FLOOD, v->peerid, 0, -1,
					    v->lsa, ntohs(v->lsa->hdr.len));
				/* new LSA was not added so free it */
				if (self)
					free(lsa);
			} else if (r < 0) {
				/*
				 * point 6 of "The Flooding Procedure"
				 * We are violating the RFC here because
				 * it does not make sense to reset a session
				 * because an equal LSA is already in the table.
				 * Only if the LSA sent is older than the one
				 * in the table we should reset the session.
				 */
				if (rde_req_list_exists(nbr, &lsa->hdr)) {
					imsg_compose_event(iev_ospfe,
					    IMSG_LS_BADREQ, imsg.hdr.peerid,
					    0, -1, NULL, 0);
					free(lsa);
					break;
				}

				/* lsa no longer needed */
				free(lsa);

				/* new LSA older than DB */
				if (ntohl(db_hdr->seq_num) == MAX_SEQ_NUM &&
				    ntohs(db_hdr->age) == MAX_AGE)
					/* seq-num wrap */
					break;

				if (v->changed + MIN_LS_ARRIVAL >= now)
					break;

				/* directly send current LSA, no ack */
				imsg_compose_event(iev_ospfe, IMSG_LS_UPD,
				    imsg.hdr.peerid, 0, -1, v->lsa,
				    ntohs(v->lsa->hdr.len));
			} else {
				/* LSA equal send direct ack */
				imsg_compose_event(iev_ospfe, IMSG_LS_ACK,
				    imsg.hdr.peerid, 0, -1, &lsa->hdr,
				    sizeof(lsa->hdr));
				free(lsa);
			}
			break;
		case IMSG_LS_MAXAGE:
			nbr = rde_nbr_find(imsg.hdr.peerid);
			if (nbr == NULL)
				break;

			if (imsg.hdr.len != IMSG_HEADER_SIZE +
			    sizeof(struct lsa_hdr))
				fatalx("invalid size of OE request");
			memcpy(&lsa_hdr, imsg.data, sizeof(lsa_hdr));

			if (rde_nbr_loading(nbr->area))
				break;

			v = lsa_find(nbr->iface, lsa_hdr.type, lsa_hdr.ls_id,
			    lsa_hdr.adv_rtr);
			if (v == NULL)
				db_hdr = NULL;
			else
				db_hdr = &v->lsa->hdr;

			/*
			 * only delete LSA if the one in the db is not newer
			 */
			if (lsa_newer(db_hdr, &lsa_hdr) <= 0)
				lsa_del(nbr, &lsa_hdr);
			break;
		case IMSG_CTL_SHOW_DATABASE:
		case IMSG_CTL_SHOW_DB_EXT:
		case IMSG_CTL_SHOW_DB_NET:
		case IMSG_CTL_SHOW_DB_RTR:
		case IMSG_CTL_SHOW_DB_SELF:
		case IMSG_CTL_SHOW_DB_SUM:
		case IMSG_CTL_SHOW_DB_ASBR:
		case IMSG_CTL_SHOW_DB_OPAQ:
			if (imsg.hdr.len != IMSG_HEADER_SIZE &&
			    imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(aid)) {
				log_warnx("rde_dispatch_imsg: wrong imsg len");
				break;
			}
			if (imsg.hdr.len == IMSG_HEADER_SIZE) {
				LIST_FOREACH(area, &rdeconf->area_list, entry) {
					rde_dump_area(area, imsg.hdr.type,
					    imsg.hdr.pid);
				}
				lsa_dump(&asext_tree, imsg.hdr.type,
				    imsg.hdr.pid);
			} else {
				memcpy(&aid, imsg.data, sizeof(aid));
				if ((area = area_find(rdeconf, aid)) != NULL) {
					rde_dump_area(area, imsg.hdr.type,
					    imsg.hdr.pid);
					if (!area->stub)
						lsa_dump(&asext_tree,
						    imsg.hdr.type,
						    imsg.hdr.pid);
				}
			}
			imsg_compose_event(iev_ospfe, IMSG_CTL_END, 0,
			    imsg.hdr.pid, -1, NULL, 0);
			break;
		case IMSG_CTL_SHOW_RIB:
			LIST_FOREACH(area, &rdeconf->area_list, entry) {
				imsg_compose_event(iev_ospfe, IMSG_CTL_AREA,
				    0, imsg.hdr.pid, -1, area, sizeof(*area));

				rt_dump(area->id, imsg.hdr.pid, RIB_RTR);
				rt_dump(area->id, imsg.hdr.pid, RIB_NET);
			}
			aid.s_addr = 0;
			rt_dump(aid, imsg.hdr.pid, RIB_EXT);

			imsg_compose_event(iev_ospfe, IMSG_CTL_END, 0,
			    imsg.hdr.pid, -1, NULL, 0);
			break;
		case IMSG_CTL_SHOW_SUM:
			rde_send_summary(imsg.hdr.pid);
			LIST_FOREACH(area, &rdeconf->area_list, entry)
				rde_send_summary_area(area, imsg.hdr.pid);
			imsg_compose_event(iev_ospfe, IMSG_CTL_END, 0,
			    imsg.hdr.pid, -1, NULL, 0);
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by ospfe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_setverbose(verbose);
			break;
		default:
			log_debug("rde_dispatch_imsg: unexpected imsg %d",
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

void
rde_dispatch_parent(int fd, short event, void *bula)
{
	static struct area	*narea;
	struct iface		*niface;
	struct imsg		 imsg;
	struct kroute		 rr;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf;
	struct redistribute	*nred;
	ssize_t			 n;
	int			 shut = 0;

	ibuf = &iev->ibuf;

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
			fatal("rde_dispatch_parent: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_NETWORK_ADD:
			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(rr)) {
				log_warnx("rde_dispatch_parent: "
				    "wrong imsg len");
				break;
			}
			memcpy(&rr, imsg.data, sizeof(rr));
			rde_asext_get(&rr);
			break;
		case IMSG_NETWORK_DEL:
			if (imsg.hdr.len != IMSG_HEADER_SIZE + sizeof(rr)) {
				log_warnx("rde_dispatch_parent: "
				    "wrong imsg len");
				break;
			}
			memcpy(&rr, imsg.data, sizeof(rr));
			rde_asext_put(&rr);
			break;
		case IMSG_RECONF_CONF:
			if ((nconf = malloc(sizeof(struct ospfd_conf))) ==
			    NULL)
				fatal(NULL);
			memcpy(nconf, imsg.data, sizeof(struct ospfd_conf));

			LIST_INIT(&nconf->area_list);
			LIST_INIT(&nconf->cand_list);
			break;
		case IMSG_RECONF_AREA:
			if ((narea = area_new()) == NULL)
				fatal(NULL);
			memcpy(narea, imsg.data, sizeof(struct area));

			LIST_INIT(&narea->iface_list);
			LIST_INIT(&narea->nbr_list);
			RB_INIT(&narea->lsa_tree);
			SIMPLEQ_INIT(&narea->redist_list);

			LIST_INSERT_HEAD(&nconf->area_list, narea, entry);
			break;
		case IMSG_RECONF_REDIST:
			if ((nred= malloc(sizeof(struct redistribute))) == NULL)
				fatal(NULL);
			memcpy(nred, imsg.data, sizeof(struct redistribute));

			SIMPLEQ_INSERT_TAIL(&narea->redist_list, nred, entry);
			break;
		case IMSG_RECONF_IFACE:
			if ((niface = malloc(sizeof(struct iface))) == NULL)
				fatal(NULL);
			memcpy(niface, imsg.data, sizeof(struct iface));

			LIST_INIT(&niface->nbr_list);
			TAILQ_INIT(&niface->ls_ack_list);
			TAILQ_INIT(&niface->auth_md_list);
			RB_INIT(&niface->lsa_tree);

			niface->area = narea;
			LIST_INSERT_HEAD(&narea->iface_list, niface, entry);

			break;
		case IMSG_RECONF_END:
			merge_config(rdeconf, nconf);
			nconf = NULL;
			break;
		default:
			log_debug("rde_dispatch_parent: unexpected imsg %d",
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

void
rde_dump_area(struct area *area, int imsg_type, pid_t pid)
{
	struct iface	*iface;

	/* dump header */
	imsg_compose_event(iev_ospfe, IMSG_CTL_AREA, 0, pid, -1,
	    area, sizeof(*area));

	/* dump link local lsa */
	LIST_FOREACH(iface, &area->iface_list, entry) {
		imsg_compose_event(iev_ospfe, IMSG_CTL_IFACE,
		    0, pid, -1, iface, sizeof(*iface));
		lsa_dump(&iface->lsa_tree, imsg_type, pid);
	}

	/* dump area lsa */
	lsa_dump(&area->lsa_tree, imsg_type, pid);
}

u_int32_t
rde_router_id(void)
{
	return (rdeconf->rtr_id.s_addr);
}

struct area *
rde_backbone_area(void)
{
	struct in_addr	id;

	id.s_addr = INADDR_ANY;

	return (area_find(rdeconf, id));
}

void
rde_send_change_kroute(struct rt_node *r)
{
	int			 krcount = 0;
	struct kroute		 kr;
	struct rt_nexthop	*rn;
	struct ibuf		*wbuf;

	if ((wbuf = imsg_create(&iev_main->ibuf, IMSG_KROUTE_CHANGE, 0, 0,
	    sizeof(kr))) == NULL) {
		return;
	}

	TAILQ_FOREACH(rn, &r->nexthop, entry) {
		if (rn->invalid)
			continue;
		if (rn->connected)
			/* skip self-originated routes */
			continue;
		krcount++;

		bzero(&kr, sizeof(kr));
		kr.prefix.s_addr = r->prefix.s_addr;
		kr.nexthop.s_addr = rn->nexthop.s_addr;
		kr.prefixlen = r->prefixlen;
		kr.ext_tag = r->ext_tag;
		imsg_add(wbuf, &kr, sizeof(kr));
	}
	if (krcount == 0) {
		/* no valid nexthop or self originated, so remove */
		ibuf_free(wbuf);
		rde_send_delete_kroute(r);
		return;
	}
	imsg_close(&iev_main->ibuf, wbuf);
	imsg_event_add(iev_main);
}

void
rde_send_delete_kroute(struct rt_node *r)
{
	struct kroute	 kr;

	bzero(&kr, sizeof(kr));
	kr.prefix.s_addr = r->prefix.s_addr;
	kr.prefixlen = r->prefixlen;

	imsg_compose_event(iev_main, IMSG_KROUTE_DELETE, 0, 0, -1,
	    &kr, sizeof(kr));
}

void
rde_send_summary(pid_t pid)
{
	static struct ctl_sum	 sumctl;
	struct timeval		 now;
	struct area		*area;
	struct vertex		*v;

	bzero(&sumctl, sizeof(struct ctl_sum));

	sumctl.rtr_id.s_addr = rde_router_id();
	sumctl.spf_delay = rdeconf->spf_delay;
	sumctl.spf_hold_time = rdeconf->spf_hold_time;

	LIST_FOREACH(area, &rdeconf->area_list, entry)
		sumctl.num_area++;

	RB_FOREACH(v, lsa_tree, &asext_tree) {
		sumctl.num_ext_lsa++;
		sumctl.ext_lsa_cksum += ntohs(v->lsa->hdr.ls_chksum);
	}

	gettimeofday(&now, NULL);
	if (rdeconf->uptime < now.tv_sec)
		sumctl.uptime = now.tv_sec - rdeconf->uptime;
	else
		sumctl.uptime = 0;

	sumctl.rfc1583compat = rdeconf->rfc1583compat;

	rde_imsg_compose_ospfe(IMSG_CTL_SHOW_SUM, 0, pid, &sumctl,
	    sizeof(sumctl));
}

void
rde_send_summary_area(struct area *area, pid_t pid)
{
	static struct ctl_sum_area	 sumareactl;
	struct iface			*iface;
	struct rde_nbr			*nbr;
	struct lsa_tree			*tree = &area->lsa_tree;
	struct vertex			*v;

	bzero(&sumareactl, sizeof(struct ctl_sum_area));

	sumareactl.area.s_addr = area->id.s_addr;
	sumareactl.num_spf_calc = area->num_spf_calc;

	LIST_FOREACH(iface, &area->iface_list, entry)
		sumareactl.num_iface++;

	LIST_FOREACH(nbr, &area->nbr_list, entry)
		if (nbr->state == NBR_STA_FULL && !nbr->self)
			sumareactl.num_adj_nbr++;

	RB_FOREACH(v, lsa_tree, tree) {
		sumareactl.num_lsa++;
		sumareactl.lsa_cksum += ntohs(v->lsa->hdr.ls_chksum);
	}

	rde_imsg_compose_ospfe(IMSG_CTL_SHOW_SUM_AREA, 0, pid, &sumareactl,
	    sizeof(sumareactl));
}

LIST_HEAD(rde_nbr_head, rde_nbr);

struct nbr_table {
	struct rde_nbr_head	*hashtbl;
	u_int32_t		 hashmask;
} rdenbrtable;

#define RDE_NBR_HASH(x)		\
	&rdenbrtable.hashtbl[(x) & rdenbrtable.hashmask]

void
rde_nbr_init(u_int32_t hashsize)
{
	struct rde_nbr_head	*head;
	u_int32_t		 hs, i;

	for (hs = 1; hs < hashsize; hs <<= 1)
		;
	rdenbrtable.hashtbl = calloc(hs, sizeof(struct rde_nbr_head));
	if (rdenbrtable.hashtbl == NULL)
		fatal("rde_nbr_init");

	for (i = 0; i < hs; i++)
		LIST_INIT(&rdenbrtable.hashtbl[i]);

	rdenbrtable.hashmask = hs - 1;

	if ((nbrself = calloc(1, sizeof(*nbrself))) == NULL)
		fatal("rde_nbr_init");

	nbrself->id.s_addr = rde_router_id();
	nbrself->peerid = NBR_IDSELF;
	nbrself->state = NBR_STA_DOWN;
	nbrself->self = 1;
	head = RDE_NBR_HASH(NBR_IDSELF);
	LIST_INSERT_HEAD(head, nbrself, hash);
}

void
rde_nbr_free(void)
{
	free(nbrself);
	free(rdenbrtable.hashtbl);
}

struct rde_nbr *
rde_nbr_find(u_int32_t peerid)
{
	struct rde_nbr_head	*head;
	struct rde_nbr		*nbr;

	head = RDE_NBR_HASH(peerid);

	LIST_FOREACH(nbr, head, hash) {
		if (nbr->peerid == peerid)
			return (nbr);
	}

	return (NULL);
}

struct rde_nbr *
rde_nbr_new(u_int32_t peerid, struct rde_nbr *new)
{
	struct rde_nbr_head	*head;
	struct rde_nbr		*nbr;
	struct area		*area;
	struct iface		*iface;

	if (rde_nbr_find(peerid))
		return (NULL);
	if ((area = area_find(rdeconf, new->area_id)) == NULL)
		fatalx("rde_nbr_new: unknown area");

	LIST_FOREACH(iface, &area->iface_list, entry) {
		if (iface->ifindex == new->ifindex)
			break;
	}
	if (iface == NULL)
		fatalx("rde_nbr_new: unknown interface");

	if ((nbr = calloc(1, sizeof(*nbr))) == NULL)
		fatal("rde_nbr_new");

	memcpy(nbr, new, sizeof(*nbr));
	nbr->peerid = peerid;
	nbr->area = area;
	nbr->iface = iface;

	TAILQ_INIT(&nbr->req_list);

	head = RDE_NBR_HASH(peerid);
	LIST_INSERT_HEAD(head, nbr, hash);
	LIST_INSERT_HEAD(&area->nbr_list, nbr, entry);

	return (nbr);
}

void
rde_nbr_iface_del(struct iface *iface)
{
	struct rde_nbr_head	*head;
	struct rde_nbr		*nbr, *xnbr;
	u_int32_t		 i;

	for (i = 0; i <= rdenbrtable.hashmask; i++) {
		head = &rdenbrtable.hashtbl[i];
		LIST_FOREACH_SAFE(nbr, head, hash, xnbr) {
			if (nbr->iface == iface)
				rde_nbr_del(nbr);
		}
	}
}

void
rde_nbr_del(struct rde_nbr *nbr)
{
	if (nbr == NULL)
		return;

	rde_req_list_free(nbr);

	LIST_REMOVE(nbr, entry);
	LIST_REMOVE(nbr, hash);

	free(nbr);
}

int
rde_nbr_loading(struct area *area)
{
	struct rde_nbr		*nbr;
	int			 checkall = 0;

	if (area == NULL) {
		area = LIST_FIRST(&rdeconf->area_list);
		checkall = 1;
	}

	while (area != NULL) {
		LIST_FOREACH(nbr, &area->nbr_list, entry) {
			if (nbr->self)
				continue;
			if (nbr->state & NBR_STA_XCHNG ||
			    nbr->state & NBR_STA_LOAD)
				return (1);
		}
		if (!checkall)
			break;
		area = LIST_NEXT(area, entry);
	}

	return (0);
}

struct rde_nbr *
rde_nbr_self(struct area *area)
{
	struct rde_nbr		*nbr;

	LIST_FOREACH(nbr, &area->nbr_list, entry)
		if (nbr->self)
			return (nbr);

	/* this may not happen */
	fatalx("rde_nbr_self: area without self");
	return (NULL);
}

/*
 * LSA req list
 */
void
rde_req_list_add(struct rde_nbr *nbr, struct lsa_hdr *lsa)
{
	struct rde_req_entry	*le;

	if ((le = calloc(1, sizeof(*le))) == NULL)
		fatal("rde_req_list_add");

	TAILQ_INSERT_TAIL(&nbr->req_list, le, entry);
	le->type = lsa->type;
	le->ls_id = lsa->ls_id;
	le->adv_rtr = lsa->adv_rtr;
}

int
rde_req_list_exists(struct rde_nbr *nbr, struct lsa_hdr *lsa_hdr)
{
	struct rde_req_entry	*le;

	TAILQ_FOREACH(le, &nbr->req_list, entry) {
		if ((lsa_hdr->type == le->type) &&
		    (lsa_hdr->ls_id == le->ls_id) &&
		    (lsa_hdr->adv_rtr == le->adv_rtr))
			return (1);
	}
	return (0);
}

void
rde_req_list_del(struct rde_nbr *nbr, struct lsa_hdr *lsa_hdr)
{
	struct rde_req_entry	*le;

	TAILQ_FOREACH(le, &nbr->req_list, entry) {
		if ((lsa_hdr->type == le->type) &&
		    (lsa_hdr->ls_id == le->ls_id) &&
		    (lsa_hdr->adv_rtr == le->adv_rtr)) {
			TAILQ_REMOVE(&nbr->req_list, le, entry);
			free(le);
			return;
		}
	}
}

void
rde_req_list_free(struct rde_nbr *nbr)
{
	struct rde_req_entry	*le;

	while ((le = TAILQ_FIRST(&nbr->req_list)) != NULL) {
		TAILQ_REMOVE(&nbr->req_list, le, entry);
		free(le);
	}
}

/*
 * as-external LSA handling
 */
struct asext_node {
	RB_ENTRY(asext_node)    entry;
	struct kroute		r;
	u_int32_t		ls_id;
};

static __inline int	asext_compare(struct asext_node *, struct asext_node *);
struct asext_node	*asext_find(u_int32_t, u_int8_t);

RB_HEAD(asext_tree, asext_node)		ast;
RB_PROTOTYPE(asext_tree, asext_node, entry, asext_compare)
RB_GENERATE(asext_tree, asext_node, entry, asext_compare)

static __inline int
asext_compare(struct asext_node *a, struct asext_node *b)
{
	if (ntohl(a->r.prefix.s_addr) < ntohl(b->r.prefix.s_addr))
		return (-1);
	if (ntohl(a->r.prefix.s_addr) > ntohl(b->r.prefix.s_addr))
		return (1);
	if (a->r.prefixlen < b->r.prefixlen)
		return (-1);
	if (a->r.prefixlen > b->r.prefixlen)
		return (1);
	return (0);
}

struct asext_node *
asext_find(u_int32_t addr, u_int8_t prefixlen)
{
	struct asext_node	a;

	a.r.prefix.s_addr = addr;
	a.r.prefixlen = prefixlen;

	return (RB_FIND(asext_tree, &ast, &a));
}

struct iface *
rde_asext_lookup(u_int32_t prefix, int plen)
{
	struct area	*area;
	struct iface	*iface;

	LIST_FOREACH(area, &rdeconf->area_list, entry) {
		LIST_FOREACH(iface, &area->iface_list, entry) {
			if ((iface->addr.s_addr & iface->mask.s_addr) ==
			    (prefix & iface->mask.s_addr) && (plen == -1 ||
			    iface->mask.s_addr == prefixlen2mask(plen)))
				return (iface);
		}
	}
	return (NULL);
}

void
rde_asext_get(struct kroute *kr)
{
	struct asext_node	*an, *oan;
	struct vertex		*v;
	struct lsa		*lsa;
	u_int32_t		 mask;

	if (rde_asext_lookup(kr->prefix.s_addr, kr->prefixlen)) {
		/* already announced as (stub) net LSA */
		log_debug("rde_asext_get: %s/%d is net LSA",
		    inet_ntoa(kr->prefix), kr->prefixlen);
		return;
	}

	an = asext_find(kr->prefix.s_addr, kr->prefixlen);
	if (an == NULL) {
		if ((an = calloc(1, sizeof(*an))) == NULL)
			fatal("rde_asext_get");
		bcopy(kr, &an->r, sizeof(*kr));
		an->ls_id = kr->prefix.s_addr;
		RB_INSERT(asext_tree, &ast, an);
	} else {
		/* the bcopy does not change the lookup key so it is save */
		bcopy(kr, &an->r, sizeof(*kr));
	}

	/*
	 * ls_id must be unique, for overlapping routes this may
	 * not be true. In this case a unique ls_id needs to be found.
	 * The algorithm will change the ls_id of the less specific
	 * route. E.g. in the case of 10.0.0.0/16 and 10.0.0.0/24
	 * 10.0.0.0/24 will get the 10.0.0.0 ls_id and 10.0.0.0/16
	 * will change the ls_id to 10.0.255.255 and see if that is unique.
	 */
	oan = an;
	mask = prefixlen2mask(oan->r.prefixlen);
	v = lsa_find(NULL, LSA_TYPE_EXTERNAL, oan->ls_id,
	    rdeconf->rtr_id.s_addr);
	while (v && v->lsa->data.asext.mask != mask) {
		/* conflict needs to be resolved. change less specific lsa */
		if (ntohl(v->lsa->data.asext.mask) < ntohl(mask)) {
			/* lsa to insert is more specific, fix other lsa */
			mask = v->lsa->data.asext.mask;
			oan = asext_find(v->lsa->hdr.ls_id & mask,
			   mask2prefixlen(mask));
			if (oan == NULL)
				fatalx("as-ext LSA DB corrupted");
		}
		/* oan is less specific and needs new ls_id */
		if (oan->ls_id == oan->r.prefix.s_addr)
			oan->ls_id |= ~mask;
		else {
			u_int32_t	tmp = ntohl(oan->ls_id);
			oan->ls_id = htonl(tmp - 1);
			if (oan->ls_id == oan->r.prefix.s_addr) {
				log_warnx("prefix %s/%d can not be "
				    "redistributed, no unique ls_id found.",
				    inet_ntoa(kr->prefix), kr->prefixlen);
				RB_REMOVE(asext_tree, &ast, an);
				free(an);
				return;
			}
		}
		mask = prefixlen2mask(oan->r.prefixlen);
		v = lsa_find(NULL, LSA_TYPE_EXTERNAL, oan->ls_id,
		    rdeconf->rtr_id.s_addr);
	}

	v = lsa_find(NULL, LSA_TYPE_EXTERNAL, an->ls_id,
	    rdeconf->rtr_id.s_addr);
	lsa = orig_asext_lsa(kr, an->ls_id, DEFAULT_AGE);
	lsa_merge(nbrself, lsa, v);

	if (oan != an) {
		v = lsa_find(NULL, LSA_TYPE_EXTERNAL, oan->ls_id,
		    rdeconf->rtr_id.s_addr);
		lsa = orig_asext_lsa(&oan->r, oan->ls_id, DEFAULT_AGE);
		lsa_merge(nbrself, lsa, v);
	}
}

void
rde_asext_put(struct kroute *kr)
{
	struct asext_node	*an;
	struct vertex		*v;
	struct lsa		*lsa;

	/*
	 * just try to remove the LSA. If the prefix is announced as
	 * stub net LSA asext_find() will fail and nothing will happen.
	 */
	an = asext_find(kr->prefix.s_addr, kr->prefixlen);
	if (an == NULL) {
		log_debug("rde_asext_put: NO SUCH LSA %s/%d",
		    inet_ntoa(kr->prefix), kr->prefixlen);
		return;
	}

	/* inherit metric and ext_tag from the current LSA,
	 * some routers don't like to get withdraws that are
	 * different from what they have in their table.
	 */
	v = lsa_find(NULL, LSA_TYPE_EXTERNAL, an->ls_id,
	    rdeconf->rtr_id.s_addr);
	if (v != NULL) {
		kr->metric = ntohl(v->lsa->data.asext.metric);
		kr->ext_tag = ntohl(v->lsa->data.asext.ext_tag);
	}

	/* remove by reflooding with MAX_AGE */
	lsa = orig_asext_lsa(kr, an->ls_id, MAX_AGE);
	lsa_merge(nbrself, lsa, v);

	RB_REMOVE(asext_tree, &ast, an);
	free(an);
}

void
rde_asext_free(void)
{
	struct asext_node	*an, *nan;

	for (an = RB_MIN(asext_tree, &ast); an != NULL; an = nan) {
		nan = RB_NEXT(asext_tree, &ast, an);
		RB_REMOVE(asext_tree, &ast, an);
		free(an);
	}
}

struct lsa *
orig_asext_lsa(struct kroute *kr, u_int32_t ls_id, u_int16_t age)
{
	struct lsa	*lsa;
	struct iface	*iface;
	u_int16_t	 len;

	len = sizeof(struct lsa_hdr) + sizeof(struct lsa_asext);
	if ((lsa = calloc(1, len)) == NULL)
		fatal("orig_asext_lsa");

	log_debug("orig_asext_lsa: %s/%d age %d",
	    inet_ntoa(kr->prefix), kr->prefixlen, age);

	/* LSA header */
	lsa->hdr.age = htons(age);
	lsa->hdr.opts = area_ospf_options(NULL);
	lsa->hdr.type = LSA_TYPE_EXTERNAL;
	lsa->hdr.adv_rtr = rdeconf->rtr_id.s_addr;
	/* update of seqnum is done by lsa_merge */
	lsa->hdr.seq_num = htonl(INIT_SEQ_NUM);
	lsa->hdr.len = htons(len);

	/* prefix and mask */
	lsa->hdr.ls_id = ls_id;
	lsa->data.asext.mask = prefixlen2mask(kr->prefixlen);

	/*
	 * nexthop -- on connected routes we are the nexthop,
	 * in other cases we may announce the true nexthop if the
	 * nexthop is reachable via an OSPF enabled interface but only
	 * broadcast & NBMA interfaces are considered in that case.
	 * It does not make sense to announce the nexthop of a point-to-point
	 * link since the traffic has to go through this box anyway.
	 * Some implementations actually check that there are multiple
	 * neighbors on the particular segment, we skip that check.
	 */
	iface = rde_asext_lookup(kr->nexthop.s_addr, -1);
	if (kr->flags & F_CONNECTED)
		lsa->data.asext.fw_addr = 0;
	else if (iface && (iface->type == IF_TYPE_BROADCAST ||
	    iface->type == IF_TYPE_NBMA))
		lsa->data.asext.fw_addr = kr->nexthop.s_addr;
	else
		lsa->data.asext.fw_addr = 0;

	lsa->data.asext.metric = htonl(kr->metric);
	lsa->data.asext.ext_tag = htonl(kr->ext_tag);

	lsa->hdr.ls_chksum = 0;
	lsa->hdr.ls_chksum = htons(iso_cksum(lsa, len, LS_CKSUM_OFFSET));

	return (lsa);
}

/*
 * summary LSA stuff
 */
void
rde_summary_update(struct rt_node *rte, struct area *area)
{
	struct rt_nexthop	*rn;
	struct rt_node		*nr;
	struct vertex		*v = NULL;
	struct lsa		*lsa;
	u_int8_t		 type = 0;

	/* first check if we actually need to announce this route */
	if (!(rte->d_type == DT_NET || rte->flags & OSPF_RTR_E))
		return;
	/* route is invalid, lsa_remove_invalid_sums() will do the cleanup */
	if (rte->cost >= LS_INFINITY)
		return;
	/* never create summaries for as-ext LSA */
	if (rte->p_type == PT_TYPE1_EXT || rte->p_type == PT_TYPE2_EXT)
		return;
	/* no need for summary LSA in the originating area */
	if (rte->area.s_addr == area->id.s_addr)
		return;
	/* no need to originate inter-area routes to the backbone */
	if (rte->p_type == PT_INTER_AREA && area->id.s_addr == INADDR_ANY)
		return;
	/* nexthop check, nexthop part of area -> no summary */
	TAILQ_FOREACH(rn, &rte->nexthop, entry) {
		if (rn->invalid)
			continue;
		nr = rt_lookup(DT_NET, rn->nexthop.s_addr);
		if (nr && nr->area.s_addr == area->id.s_addr)
			continue;
		break;
	}
	if (rn == NULL)
		/* all nexthops belong to this area or are invalid */
		return;

	/* TODO AS border router specific checks */
	/* TODO inter-area network route stuff */
	/* TODO intra-area stuff -- condense LSA ??? */

	if (rte->d_type == DT_NET) {
		type = LSA_TYPE_SUM_NETWORK;
	} else if (rte->d_type == DT_RTR) {
		if (area->stub)
			/* do not redistribute type 4 LSA into stub areas */
			return;
		type = LSA_TYPE_SUM_ROUTER;
	} else
		fatalx("rde_summary_update: unknown route type");

	/* update lsa but only if it was changed */
	v = lsa_find_area(area, type, rte->prefix.s_addr, rde_router_id());
	lsa = orig_sum_lsa(rte, area, type, rte->invalid);
	lsa_merge(rde_nbr_self(area), lsa, v);

	if (v == NULL)
		v = lsa_find_area(area, type, rte->prefix.s_addr,
		    rde_router_id());

	/* suppressed/deleted routes are not found in the second lsa_find */
	if (v)
		v->cost = rte->cost;
}

struct lsa *
orig_sum_lsa(struct rt_node *rte, struct area *area, u_int8_t type, int invalid)
{
	struct lsa	*lsa;
	u_int16_t	 len;

	len = sizeof(struct lsa_hdr) + sizeof(struct lsa_sum);
	if ((lsa = calloc(1, len)) == NULL)
		fatal("orig_sum_lsa");

	/* LSA header */
	lsa->hdr.age = htons(invalid ? MAX_AGE : DEFAULT_AGE);
	lsa->hdr.opts = area_ospf_options(area);
	lsa->hdr.type = type;
	lsa->hdr.adv_rtr = rdeconf->rtr_id.s_addr;
	lsa->hdr.seq_num = htonl(INIT_SEQ_NUM);
	lsa->hdr.len = htons(len);

	/* prefix and mask */
	/*
	 * TODO ls_id must be unique, for overlapping routes this may
	 * not be true. In this case a hack needs to be done to
	 * make the ls_id unique.
	 */
	lsa->hdr.ls_id = rte->prefix.s_addr;
	if (type == LSA_TYPE_SUM_NETWORK)
		lsa->data.sum.mask = prefixlen2mask(rte->prefixlen);
	else
		lsa->data.sum.mask = 0;	/* must be zero per RFC */

	lsa->data.sum.metric = htonl(rte->cost & LSA_METRIC_MASK);

	lsa->hdr.ls_chksum = 0;
	lsa->hdr.ls_chksum =
	    htons(iso_cksum(lsa, len, LS_CKSUM_OFFSET));

	return (lsa);
}
