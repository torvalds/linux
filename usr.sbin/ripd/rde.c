/*	$OpenBSD: rde.c,v 1.31 2024/11/21 13:38:15 claudio Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
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

#include "ripd.h"
#include "rip.h"
#include "ripe.h"
#include "log.h"
#include "rde.h"

#define	MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

struct ripd_conf	*rdeconf = NULL;
static struct imsgev	*iev_ripe;
static struct imsgev	*iev_main;

void	rde_sig_handler(int, short, void *);
__dead void rde_shutdown(void);
void	rde_dispatch_imsg(int, short, void *);
void	rde_dispatch_parent(int, short, void *);
int	rde_imsg_compose_ripe(int, u_int32_t, pid_t, void *, u_int16_t);
int	rde_check_route(struct rip_route *);
void	triggered_update(struct rt_node *);

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
rde(struct ripd_conf *xconf, int pipe_parent2rde[2], int pipe_ripe2rde[2],
    int pipe_parent2ripe[2])
{
	struct event		 ev_sigint, ev_sigterm;
	struct passwd		*pw;
	struct redistribute	*r;
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

	rdeconf = xconf;

	if ((pw = getpwnam(RIPD_USER)) == NULL)
		fatal("getpwnam");

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");

	setproctitle("route decision engine");
	log_procname = "rde";

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	if (pledge("stdio", NULL) == -1)
		fatal("pledge");

	event_init();

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, rde_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, rde_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* setup pipes */
	close(pipe_ripe2rde[0]);
	close(pipe_parent2rde[0]);
	close(pipe_parent2ripe[0]);
	close(pipe_parent2ripe[1]);

	if ((iev_ripe = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_main = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_ripe->ibuf, pipe_ripe2rde[1]) == -1)
		fatal(NULL);
	iev_ripe->handler =  rde_dispatch_imsg;
	if (imsgbuf_init(&iev_main->ibuf, pipe_parent2rde[1]) == -1)
		fatal(NULL);
	iev_main->handler = rde_dispatch_parent;

	/* setup event handler */
	iev_ripe->events = EV_READ;
	event_set(&iev_ripe->ev, iev_ripe->ibuf.fd, iev_ripe->events,
	    iev_ripe->handler, iev_ripe);
	event_add(&iev_ripe->ev, NULL);

	iev_main->events = EV_READ;
	event_set(&iev_main->ev, iev_main->ibuf.fd, iev_main->events,
	    iev_main->handler, iev_main);
	event_add(&iev_main->ev, NULL);
	rt_init();

	/* remove unneeded config stuff */
	while ((r = SIMPLEQ_FIRST(&rdeconf->redist_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&rdeconf->redist_list, entry);
		free(r);
	}

	event_dispatch();

	rde_shutdown();
	/* NOTREACHED */

	return (0);
}

__dead void
rde_shutdown(void)
{
	/* close pipes */
	imsgbuf_clear(&iev_ripe->ibuf);
	close(iev_ripe->ibuf.fd);
	imsgbuf_clear(&iev_main->ibuf);
	close(iev_main->ibuf.fd);

	rt_clear();
	free(iev_ripe);
	free(iev_main);
	free(rdeconf);

	log_info("route decision engine exiting");
	_exit(0);
}

int
rde_imsg_compose_ripe(int type, u_int32_t peerid, pid_t pid, void *data,
    u_int16_t datalen)
{
	return (imsg_compose_event(iev_ripe, type, peerid, pid, -1,
		    data, datalen));
}

void
rde_dispatch_imsg(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct rip_route	 rr;
	struct imsg		 imsg;
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
			fatal("rde_dispatch_imsg: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_ROUTE_FEED:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(rr))
				fatalx("invalid size of RDE request");

			memcpy(&rr, imsg.data, sizeof(rr));

			if (rde_check_route(&rr) == -1)
				log_debug("rde_dispatch_imsg: "
				    "packet malformed\n");
			break;
		case IMSG_FULL_REQUEST:
			bzero(&rr, sizeof(rr));
			/*
			 * AFI == 0 && metric == INFINITY request the
			 * whole routing table
			 */
			rr.metric = INFINITY;
			rde_imsg_compose_ripe(IMSG_REQUEST_ADD, 0,
			    0, &rr, sizeof(rr));
			rde_imsg_compose_ripe(IMSG_SEND_REQUEST, 0,
			    0, NULL, 0);
			break;
		case IMSG_FULL_RESPONSE:
			rt_snap(imsg.hdr.peerid);
			rde_imsg_compose_ripe(IMSG_SEND_RESPONSE,
			    imsg.hdr.peerid, 0, NULL, 0);
			break;
		case IMSG_ROUTE_REQUEST:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(rr))
				fatalx("invalid size of RDE request");

			memcpy(&rr, imsg.data, sizeof(rr));

			rt_complete(&rr);
			rde_imsg_compose_ripe(IMSG_RESPONSE_ADD,
			    imsg.hdr.peerid, 0, &rr, sizeof(rr));

			break;
		case IMSG_ROUTE_REQUEST_END:
			rde_imsg_compose_ripe(IMSG_SEND_RESPONSE,
			    imsg.hdr.peerid, 0, NULL, 0);
			break;
		case IMSG_CTL_SHOW_RIB:
			rt_dump(imsg.hdr.pid);

			imsg_compose_event(iev_ripe, IMSG_CTL_END, 0,
			    imsg.hdr.pid, -1, NULL, 0);

			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by ripe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_verbose(verbose);
			break;
		default:
			log_debug("rde_dispatch_msg: unexpected imsg %d",
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
	struct imsg		 imsg;
	struct rt_node		*rt;
	struct kroute		 kr;
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	ssize_t			 n;
	int			 shut = 0;

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
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(kr)) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}

			memcpy(&kr, imsg.data, sizeof(kr));

			rt = rt_new_kr(&kr);
			rt_insert(rt);
			break;
		case IMSG_NETWORK_DEL:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(kr)) {
				log_warnx("rde_dispatch: wrong imsg len");
				break;
			}
			memcpy(&kr, imsg.data, sizeof(kr));

			if ((rt = rt_find(kr.prefix.s_addr,
			    kr.netmask.s_addr)) != NULL)
				rt_remove(rt);
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
rde_send_change_kroute(struct rt_node *r)
{
	struct kroute	 kr;

	bzero(&kr, sizeof(kr));
	kr.prefix.s_addr = r->prefix.s_addr;
	kr.nexthop.s_addr = r->nexthop.s_addr;
	kr.netmask.s_addr = r->netmask.s_addr;
	kr.metric = r->metric;
	kr.flags = r->flags;
	kr.ifindex = r->ifindex;

	imsg_compose_event(iev_main, IMSG_KROUTE_CHANGE, 0, 0, -1,
	    &kr, sizeof(kr));
}

void
rde_send_delete_kroute(struct rt_node *r)
{
	struct kroute	 kr;

	bzero(&kr, sizeof(kr));
	kr.prefix.s_addr = r->prefix.s_addr;
	kr.nexthop.s_addr = r->nexthop.s_addr;
	kr.netmask.s_addr = r->netmask.s_addr;
	kr.metric = r->metric;
	kr.flags = r->flags;
	kr.ifindex = r->ifindex;

	imsg_compose_event(iev_main, IMSG_KROUTE_DELETE, 0, 0, -1,
	    &kr, sizeof(kr));
}

int
rde_check_route(struct rip_route *e)
{
	struct timeval	 tv, now;
	struct rt_node	*rn;
	struct iface	*iface;
	u_int8_t	 metric;

	if ((e->nexthop.s_addr & htonl(IN_CLASSA_NET)) ==
	    htonl(INADDR_LOOPBACK & IN_CLASSA_NET) ||
	    e->nexthop.s_addr == INADDR_ANY)
		return (-1);

	if ((iface = if_find_index(e->ifindex)) == NULL)
		return (-1);

	metric = MINIMUM(INFINITY, e->metric + iface->cost);

	if ((rn = rt_find(e->address.s_addr, e->mask.s_addr)) == NULL) {
		if (metric >= INFINITY)
			return (0);
		rn = rt_new_rr(e, metric);
		rt_insert(rn);
		rde_send_change_kroute(rn);
		route_start_timeout(rn);
		triggered_update(rn);
	} else {
		/*
		 * XXX don't we have to track all incoming routes?
		 * what happens if the kernel route is removed later.
		 */
		if (rn->flags & F_KERNEL)
			return (0);

		if (metric < rn->metric) {
			rn->metric = metric;
			rn->nexthop.s_addr = e->nexthop.s_addr;
			rn->ifindex = e->ifindex;
			rde_send_change_kroute(rn);
			triggered_update(rn);
		} else if (e->nexthop.s_addr == rn->nexthop.s_addr &&
		    metric > rn->metric) {
				rn->metric = metric;
				rde_send_change_kroute(rn);
				triggered_update(rn);
				if (rn->metric == INFINITY)
					route_start_garbage(rn);
		} else if (e->nexthop.s_addr != rn->nexthop.s_addr &&
		    metric == rn->metric) {
			/* If the new metric is the same as the old one,
			 * examine the timeout for the existing route.  If it
			 * is at least halfway to the expiration point, switch
			 * to the new route.
			 */
			timerclear(&tv);
			gettimeofday(&now, NULL);
			evtimer_pending(&rn->timeout_timer, &tv);
			if (tv.tv_sec - now.tv_sec < ROUTE_TIMEOUT / 2) {
				rn->nexthop.s_addr = e->nexthop.s_addr;
				rn->ifindex = e->ifindex;
				rde_send_change_kroute(rn);
			}
		}

		if (e->nexthop.s_addr == rn->nexthop.s_addr &&
		    rn->metric < INFINITY)
			route_reset_timers(rn);
	}

	return (0);
}

void
triggered_update(struct rt_node *rn)
{
	struct rip_route	 rr;

	rr.address.s_addr = rn->prefix.s_addr;
	rr.mask.s_addr = rn->netmask.s_addr;
	rr.nexthop.s_addr = rn->nexthop.s_addr;
	rr.metric = rn->metric;
	rr.ifindex = rn->ifindex;

	rde_imsg_compose_ripe(IMSG_SEND_TRIGGERED_UPDATE, 0, 0, &rr,
	    sizeof(struct rip_route));
}
