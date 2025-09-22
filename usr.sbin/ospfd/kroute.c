/*	$OpenBSD: kroute.c,v 1.118 2025/01/01 13:44:52 anton Exp $ */

/*
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
#include <sys/sysctl.h>
#include <sys/tree.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "ospfd.h"
#include "log.h"

struct {
	u_int32_t		rtseq;
	pid_t			pid;
	int			fib_sync;
	int			fib_serial;
	u_int8_t		fib_prio;
	int			fd;
	struct event		ev;
	struct event		reload;
	u_int			rdomain;
#define KR_RELOAD_IDLE	0
#define KR_RELOAD_FETCH	1
#define KR_RELOAD_HOLD	2
	int			reload_state;
} kr_state;

struct kroute_node {
	RB_ENTRY(kroute_node)	 entry;
	struct kroute_node	*next;
	struct kroute		 r;
	int			 serial;
};

struct kif_node {
	RB_ENTRY(kif_node)	 entry;
	TAILQ_HEAD(, kif_addr)	 addrs;
	struct kif		 k;
};

void	kr_redist_remove(struct kroute_node *, struct kroute_node *);
int	kr_redist_eval(struct kroute *, struct kroute *);
void	kr_redistribute(struct kroute_node *);
int	kroute_compare(struct kroute_node *, struct kroute_node *);
int	kif_compare(struct kif_node *, struct kif_node *);
int	kr_change_fib(struct kroute_node *, struct kroute *, int, int);
int	kr_delete_fib(struct kroute_node *);

struct kroute_node	*kroute_find(in_addr_t, u_int8_t, u_int8_t);
struct kroute_node	*kroute_matchgw(struct kroute_node *, struct in_addr);
int			 kroute_insert(struct kroute_node *);
int			 kroute_remove(struct kroute_node *);
void			 kroute_clear(void);

struct kif_node		*kif_find(u_short);
struct kif_node		*kif_insert(u_short);
int			 kif_remove(struct kif_node *);
struct kif		*kif_update(u_short, int, struct if_data *,
			    struct sockaddr_dl *);
int			 kif_validate(u_short);

struct kroute_node	*kroute_match(in_addr_t);

int		protect_lo(void);
u_int8_t	prefixlen_classful(in_addr_t);
void		get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
void		if_change(u_short, int, struct if_data *, struct sockaddr_dl *);
void		if_newaddr(u_short, struct sockaddr_in *, struct sockaddr_in *,
		    struct sockaddr_in *);
void		if_deladdr(u_short, struct sockaddr_in *, struct sockaddr_in *,
		    struct sockaddr_in *);
void		if_announce(void *);

int		send_rtmsg(int, int, struct kroute *);
int		dispatch_rtmsg(void);
int		fetchtable(void);
int		fetchifs(u_short);
int		rtmsg_process(char *, size_t);
void		kr_fib_reload_timer(int, short, void *);
void		kr_fib_reload_arm_timer(int);

RB_HEAD(kroute_tree, kroute_node)	krt = RB_INITIALIZER(&krt);
RB_PROTOTYPE(kroute_tree, kroute_node, entry, kroute_compare)
RB_GENERATE(kroute_tree, kroute_node, entry, kroute_compare)

RB_HEAD(kif_tree, kif_node)		kit = RB_INITIALIZER(&kit);
RB_PROTOTYPE(kif_tree, kif_node, entry, kif_compare)
RB_GENERATE(kif_tree, kif_node, entry, kif_compare)

int
kif_init(void)
{
	if (fetchifs(0) == -1)
		return (-1);

	return (0);
}

int
kr_init(int fs, u_int rdomain, int redis_label_or_prefix, u_int8_t fib_prio)
{
	int		opt = 0, rcvbuf, default_rcvbuf;
	socklen_t	optlen;
	int		filter_prio = fib_prio;
	int		filter_flags = RTF_LLINFO | RTF_BROADCAST;

	kr_state.fib_sync = fs;
	kr_state.rdomain = rdomain;
	kr_state.fib_prio = fib_prio;

	if ((kr_state.fd = socket(AF_ROUTE,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, AF_INET)) == -1) {
		log_warn("kr_init: socket");
		return (-1);
	}

	/* not interested in my own messages */
	if (setsockopt(kr_state.fd, SOL_SOCKET, SO_USELOOPBACK,
	    &opt, sizeof(opt)) == -1)
		log_warn("kr_init: setsockopt");	/* not fatal */

	if (redis_label_or_prefix) {
		filter_prio = 0;
		log_info("%s: priority filter disabled", __func__);
	} else
		log_debug("%s: priority filter enabled", __func__);

	if (setsockopt(kr_state.fd, AF_ROUTE, ROUTE_PRIOFILTER, &filter_prio,
	    sizeof(filter_prio)) == -1) {
		log_warn("%s: setsockopt AF_ROUTE ROUTE_PRIOFILTER", __func__);
		/* not fatal */
	}
	if (setsockopt(kr_state.fd, AF_ROUTE, ROUTE_FLAGFILTER, &filter_flags,
	    sizeof(filter_flags)) == -1) {
		log_warn("%s: setsockopt AF_ROUTE ROUTE_FLAGFILTER", __func__);
		/* not fatal */
	}

	/* grow receive buffer, don't wanna miss messages */
	optlen = sizeof(default_rcvbuf);
	if (getsockopt(kr_state.fd, SOL_SOCKET, SO_RCVBUF,
	    &default_rcvbuf, &optlen) == -1)
		log_warn("kr_init getsockopt SOL_SOCKET SO_RCVBUF");
	else
		for (rcvbuf = MAX_RTSOCK_BUF;
		    rcvbuf > default_rcvbuf &&
		    setsockopt(kr_state.fd, SOL_SOCKET, SO_RCVBUF,
		    &rcvbuf, sizeof(rcvbuf)) == -1 && errno == ENOBUFS;
		    rcvbuf /= 2)
			;	/* nothing */

	kr_state.pid = getpid();
	kr_state.rtseq = 1;

	if (fetchtable() == -1)
		return (-1);

	if (protect_lo() == -1)
		return (-1);

	event_set(&kr_state.ev, kr_state.fd, EV_READ | EV_PERSIST,
	    kr_dispatch_msg, NULL);
	event_add(&kr_state.ev, NULL);

	kr_state.reload_state = KR_RELOAD_IDLE;
	evtimer_set(&kr_state.reload, kr_fib_reload_timer, NULL);

	return (0);
}

int
kr_change_fib(struct kroute_node *kr, struct kroute *kroute, int krcount,
    int action)
{
	int			 i;
	struct kroute_node	*kn, *nkn;

	if (action == RTM_ADD) {
		/*
		 * First remove all stale multipath routes.
		 * This step must be skipped when the action is RTM_CHANGE
		 * because it is already a single path route that will be
		 * changed.
		 */
		for (kn = kr; kn != NULL; kn = nkn) {
			for (i = 0; i < krcount; i++) {
				if (kn->r.nexthop.s_addr ==
				    kroute[i].nexthop.s_addr)
					break;
			}
			nkn = kn->next;
			if (i == krcount) {
				/* stale route */
				if (kr_delete_fib(kn) == -1)
					log_warnx("kr_delete_fib failed");
				/*
				 * if head element was removed we need to adjust
				 * the head
				 */
				if (kr == kn)
					kr = nkn;
			}
		}
	}

	/*
	 * now add or change the route
	 */
	for (i = 0; i < krcount; i++) {
		/* nexthop within 127/8 -> ignore silently */
		if ((kroute[i].nexthop.s_addr & htonl(IN_CLASSA_NET)) ==
		    htonl(INADDR_LOOPBACK & IN_CLASSA_NET))
			continue;

		if (action == RTM_ADD && kr) {
			for (kn = kr; kn != NULL; kn = kn->next) {
				if (kn->r.nexthop.s_addr ==
				    kroute[i].nexthop.s_addr)
					break;
			}

			if (kn != NULL)
				/* nexthop already present, skip it */
				continue;
		} else
			/* modify first entry */
			kn = kr;

		/* send update */
		if (send_rtmsg(kr_state.fd, action, &kroute[i]) == -1)
			return (-1);

		/* create new entry unless we are changing the first entry */
		if (action == RTM_ADD)
			if ((kn = calloc(1, sizeof(*kn))) == NULL)
				fatal(NULL);

		kn->r.prefix.s_addr = kroute[i].prefix.s_addr;
		kn->r.prefixlen = kroute[i].prefixlen;
		kn->r.nexthop.s_addr = kroute[i].nexthop.s_addr;
		kn->r.flags = kroute[i].flags | F_OSPFD_INSERTED;
		kn->r.priority = kr_state.fib_prio;
		kn->r.ext_tag = kroute[i].ext_tag;
		rtlabel_unref(kn->r.rtlabel);	/* for RTM_CHANGE */
		kn->r.rtlabel = kroute[i].rtlabel;

		if (action == RTM_ADD)
			if (kroute_insert(kn) == -1) {
				log_debug("kr_update_fib: cannot insert %s",
				    inet_ntoa(kn->r.nexthop));
				free(kn);
			}
		action = RTM_ADD;
	}
	return  (0);
}

int
kr_change(struct kroute *kroute, int krcount)
{
	struct kroute_node	*kr;
	int			 action = RTM_ADD;

	kroute->rtlabel = rtlabel_tag2id(kroute->ext_tag);

	kr = kroute_find(kroute->prefix.s_addr, kroute->prefixlen,
	    kr_state.fib_prio);
	if (kr != NULL && kr->next == NULL && krcount == 1)
		/* single path OSPF route */
		action = RTM_CHANGE;

	return (kr_change_fib(kr, kroute, krcount, action));
}

int
kr_delete_fib(struct kroute_node *kr)
{
	if (kr->r.priority != kr_state.fib_prio)
		log_warn("kr_delete_fib: %s/%d has wrong priority %d",
		    inet_ntoa(kr->r.prefix), kr->r.prefixlen, kr->r.priority);

	if (send_rtmsg(kr_state.fd, RTM_DELETE, &kr->r) == -1)
		return (-1);

	if (kroute_remove(kr) == -1)
		return (-1);

	return (0);
}

int
kr_delete(struct kroute *kroute)
{
	struct kroute_node	*kr, *nkr;

	if ((kr = kroute_find(kroute->prefix.s_addr, kroute->prefixlen,
	    kr_state.fib_prio)) == NULL)
		return (0);

	while (kr != NULL) {
		nkr = kr->next;
		if (kr_delete_fib(kr) == -1)
			return (-1);
		kr = nkr;
	}
	return (0);
}

void
kr_shutdown(void)
{
	kr_fib_decouple();
	kroute_clear();
	kif_clear();
}

void
kr_fib_couple(void)
{
	struct kroute_node	*kr;
	struct kroute_node	*kn;

	if (kr_state.fib_sync == 1)	/* already coupled */
		return;

	kr_state.fib_sync = 1;

	RB_FOREACH(kr, kroute_tree, &krt)
		if (kr->r.priority == kr_state.fib_prio)
			for (kn = kr; kn != NULL; kn = kn->next)
				send_rtmsg(kr_state.fd, RTM_ADD, &kn->r);

	log_info("kernel routing table coupled");
}

void
kr_fib_decouple(void)
{
	struct kroute_node	*kr;
	struct kroute_node	*kn;

	if (kr_state.fib_sync == 0)	/* already decoupled */
		return;

	RB_FOREACH(kr, kroute_tree, &krt)
		if (kr->r.priority == kr_state.fib_prio)
			for (kn = kr; kn != NULL; kn = kn->next)
				send_rtmsg(kr_state.fd, RTM_DELETE, &kn->r);

	kr_state.fib_sync = 0;

	log_info("kernel routing table decoupled");
}

void
kr_fib_reload_timer(int fd, short event, void *bula)
{
	if (kr_state.reload_state == KR_RELOAD_FETCH) {
		kr_fib_reload();
		kr_state.reload_state = KR_RELOAD_HOLD;
		kr_fib_reload_arm_timer(KR_RELOAD_HOLD_TIMER);
	} else {
		kr_state.reload_state = KR_RELOAD_IDLE;
	}
}

void
kr_fib_reload_arm_timer(int delay)
{
	struct timeval		 tv;

	timerclear(&tv);
	tv.tv_sec = delay / 1000;
	tv.tv_usec = (delay % 1000) * 1000;

	if (evtimer_add(&kr_state.reload, &tv) == -1)
		fatal("add_reload_timer");
}

void
kr_fib_reload(void)
{
	struct kroute_node	*krn, *kr, *kn;

	log_info("reloading interface list and routing table");

	kr_state.fib_serial++;

	if (fetchifs(0) == -1 || fetchtable() == -1)
		return;

	for (kr = RB_MIN(kroute_tree, &krt); kr != NULL; kr = krn) {
		krn = RB_NEXT(kroute_tree, &krt, kr);

		do {
			kn = kr->next;

			if (kr->serial != kr_state.fib_serial) {
				if (kr->r.priority == kr_state.fib_prio) {
					kr->serial = kr_state.fib_serial;
					if (send_rtmsg(kr_state.fd,
					    RTM_ADD, &kr->r) != 0)
						break;
				} else
					kroute_remove(kr);
			}

		} while ((kr = kn) != NULL);
	}
}

void
kr_fib_update_prio(u_int8_t fib_prio)
{
	struct kroute_node      *kr;

	RB_FOREACH(kr, kroute_tree, &krt)
		if ((kr->r.flags & F_OSPFD_INSERTED))
			kr->r.priority = fib_prio;

	log_info("fib priority changed from %hhu to %hhu",
	    kr_state.fib_prio, fib_prio);

	kr_state.fib_prio = fib_prio;
 }

void
kr_dispatch_msg(int fd, short event, void *bula)
{
	/* XXX this is stupid */
	if (dispatch_rtmsg() == -1)
		event_loopexit(NULL);
}

void
kr_show_route(struct imsg *imsg)
{
	struct kroute_node	*kr;
	struct kroute_node	*kn;
	int			 flags;
	struct in_addr		 addr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_KROUTE:
		if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(flags)) {
			log_warnx("kr_show_route: wrong imsg len");
			return;
		}
		memcpy(&flags, imsg->data, sizeof(flags));
		RB_FOREACH(kr, kroute_tree, &krt)
			if (!flags || kr->r.flags & flags) {
				kn = kr;
				do {
					main_imsg_compose_ospfe(IMSG_CTL_KROUTE,
					    imsg->hdr.pid,
					    &kn->r, sizeof(kn->r));
				} while ((kn = kn->next) != NULL);
			}
		break;
	case IMSG_CTL_KROUTE_ADDR:
		if (imsg->hdr.len != IMSG_HEADER_SIZE +
		    sizeof(struct in_addr)) {
			log_warnx("kr_show_route: wrong imsg len");
			return;
		}
		memcpy(&addr, imsg->data, sizeof(addr));
		kr = NULL;
		kr = kroute_match(addr.s_addr);
		if (kr != NULL)
			main_imsg_compose_ospfe(IMSG_CTL_KROUTE, imsg->hdr.pid,
			    &kr->r, sizeof(kr->r));
		break;
	default:
		log_debug("kr_show_route: error handling imsg");
		break;
	}

	main_imsg_compose_ospfe(IMSG_CTL_END, imsg->hdr.pid, NULL, 0);
}

void
kr_ifinfo(char *ifname, pid_t pid)
{
	struct kif_node	*kif;

	RB_FOREACH(kif, kif_tree, &kit)
		if (ifname == NULL || !strcmp(ifname, kif->k.ifname)) {
			main_imsg_compose_ospfe(IMSG_CTL_IFINFO,
			    pid, &kif->k, sizeof(kif->k));
		}

	main_imsg_compose_ospfe(IMSG_CTL_END, pid, NULL, 0);
}

void
kr_redist_remove(struct kroute_node *kh, struct kroute_node *kn)
{
	struct kroute	*kr;

	/* was the route redistributed? */
	if ((kn->r.flags & F_REDISTRIBUTED) == 0)
		return;

	/* remove redistributed flag */
	kn->r.flags &= ~F_REDISTRIBUTED;
	kr = &kn->r;

	/* probably inform the RDE (check if no other path is redistributed) */
	for (kn = kh; kn; kn = kn->next)
		if (kn->r.flags & F_REDISTRIBUTED)
			break;

	if (kn == NULL)
		main_imsg_compose_rde(IMSG_NETWORK_DEL, 0, kr,
		    sizeof(struct kroute));
}

int
kr_redist_eval(struct kroute *kr, struct kroute *new_kr)
{
	u_int32_t	 a, metric = 0;

	/* Only non-ospfd routes are considered for redistribution. */
	if (!(kr->flags & F_KERNEL))
		goto dont_redistribute;

	/* Dynamic routes are not redistributable. */
	if (kr->flags & F_DYNAMIC)
		goto dont_redistribute;

	/* interface is not up and running so don't announce */
	if (kr->flags & F_DOWN)
		goto dont_redistribute;

	/*
	 * We consider the loopback net and multicast addresses
	 * as not redistributable.
	 */
	a = ntohl(kr->prefix.s_addr);
	if (IN_MULTICAST(a) || (a >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET)
		goto dont_redistribute;
	/*
	 * Consider networks with nexthop loopback as not redistributable
	 * unless it is a reject or blackhole route.
	 */
	if (kr->nexthop.s_addr == htonl(INADDR_LOOPBACK) &&
	    !(kr->flags & (F_BLACKHOLE|F_REJECT)))
		goto dont_redistribute;

	/* Should we redistribute this route? */
	if (!ospf_redistribute(kr, &metric))
		goto dont_redistribute;

	/* prefix should be redistributed */
	kr->flags |= F_REDISTRIBUTED;
	/*
	 * only one of all multipath routes can be redistributed so
	 * redistribute the best one.
	 */
	if (new_kr->metric > metric) {
		*new_kr = *kr;
		new_kr->metric = metric;
	}

	return (1);

dont_redistribute:
	/* was the route redistributed? */
	if ((kr->flags & F_REDISTRIBUTED) == 0)
		return (0);

	kr->flags &= ~F_REDISTRIBUTED;
	return (1);
}

void
kr_redistribute(struct kroute_node *kh)
{
	struct kroute_node	*kn;
	struct kroute		 kr;
	int			 redistribute = 0;

	/* only the highest prio route can be redistributed */
	if (kroute_find(kh->r.prefix.s_addr, kh->r.prefixlen, RTP_ANY) != kh)
		return;

	bzero(&kr, sizeof(kr));
	kr.metric = UINT_MAX;
	for (kn = kh; kn; kn = kn->next)
		if (kr_redist_eval(&kn->r, &kr))
			redistribute = 1;

	if (!redistribute)
		return;

	if (kr.flags & F_REDISTRIBUTED) {
		main_imsg_compose_rde(IMSG_NETWORK_ADD, 0, &kr,
		    sizeof(struct kroute));
	} else {
		kr = kh->r;
		main_imsg_compose_rde(IMSG_NETWORK_DEL, 0, &kr,
		    sizeof(struct kroute));
	}
}

void
kr_reload(int redis_label_or_prefix)
{
	struct kroute_node	*kr, *kn;
	u_int32_t		 dummy;
	int			 r;
	int			 filter_prio = kr_state.fib_prio;

	/* update the priority filter */
	if (redis_label_or_prefix) {
		filter_prio = 0;
		log_info("%s: priority filter disabled", __func__);
	} else
		log_debug("%s: priority filter enabled", __func__);

	if (setsockopt(kr_state.fd, AF_ROUTE, ROUTE_PRIOFILTER, &filter_prio,
	    sizeof(filter_prio)) == -1) {
		log_warn("%s: setsockopt AF_ROUTE ROUTE_PRIOFILTER", __func__);
		/* not fatal */
	}

	/* update redistribute lists */
	RB_FOREACH(kr, kroute_tree, &krt) {
		for (kn = kr; kn; kn = kn->next) {
			r = ospf_redistribute(&kn->r, &dummy);
			/*
			 * if it is redistributed, redistribute again metric
			 * may have changed.
			 */
			if ((kn->r.flags & F_REDISTRIBUTED && !r) || r)
				break;
		}
		if (kn) {
			/*
			 * kr_redistribute copes with removes and RDE with
			 * duplicates
			 */
			kr_redistribute(kr);
		}
	}
}

/* rb-tree compare */
int
kroute_compare(struct kroute_node *a, struct kroute_node *b)
{
	if (ntohl(a->r.prefix.s_addr) < ntohl(b->r.prefix.s_addr))
		return (-1);
	if (ntohl(a->r.prefix.s_addr) > ntohl(b->r.prefix.s_addr))
		return (1);
	if (a->r.prefixlen < b->r.prefixlen)
		return (-1);
	if (a->r.prefixlen > b->r.prefixlen)
		return (1);

	/* if the priority is RTP_ANY finish on the first address hit */
	if (a->r.priority == RTP_ANY || b->r.priority == RTP_ANY)
		return (0);
	if (a->r.priority < b->r.priority)
		return (-1);
	if (a->r.priority > b->r.priority)
		return (1);
	return (0);
}

int
kif_compare(struct kif_node *a, struct kif_node *b)
{
	return (b->k.ifindex - a->k.ifindex);
}

/* tree management */
struct kroute_node *
kroute_find(in_addr_t prefix, u_int8_t prefixlen, u_int8_t prio)
{
	struct kroute_node	s;
	struct kroute_node	*kn, *tmp;

	s.r.prefix.s_addr = prefix;
	s.r.prefixlen = prefixlen;
	s.r.priority = prio;

	kn = RB_FIND(kroute_tree, &krt, &s);
	if (kn && prio == RTP_ANY) {
		tmp = RB_PREV(kroute_tree, &krt, kn);
		while (tmp) {
			if (kroute_compare(&s, tmp) == 0)
				kn = tmp;
			else
				break;
			tmp = RB_PREV(kroute_tree, &krt, kn);
		}
	}
	return (kn);
}

struct kroute_node *
kroute_matchgw(struct kroute_node *kr, struct in_addr nh)
{
	in_addr_t	nexthop;

	nexthop = nh.s_addr;

	while (kr) {
		if (kr->r.nexthop.s_addr == nexthop)
			return (kr);
		kr = kr->next;
	}

	return (NULL);
}

int
kroute_insert(struct kroute_node *kr)
{
	struct kroute_node	*krm, *krh;

	kr->serial = kr_state.fib_serial;

	if ((krh = RB_INSERT(kroute_tree, &krt, kr)) != NULL) {
		/*
		 * Multipath route, add at end of list.
		 */
		krm = krh;
		while (krm->next != NULL)
			krm = krm->next;
		krm->next = kr;
		kr->next = NULL; /* to be sure */
	} else
		krh = kr;

	if (!(kr->r.flags & F_KERNEL)) {
		/* don't validate or redistribute ospf route */
		kr->r.flags &= ~F_DOWN;
		return (0);
	}

	if (kif_validate(kr->r.ifindex))
		kr->r.flags &= ~F_DOWN;
	else
		kr->r.flags |= F_DOWN;

	kr_redistribute(krh);
	return (0);
}

int
kroute_remove(struct kroute_node *kr)
{
	struct kroute_node	*krm;

	if ((krm = RB_FIND(kroute_tree, &krt, kr)) == NULL) {
		log_warnx("kroute_remove failed to find %s/%u",
		    inet_ntoa(kr->r.prefix), kr->r.prefixlen);
		return (-1);
	}

	if (krm == kr) {
		/* head element */
		if (RB_REMOVE(kroute_tree, &krt, kr) == NULL) {
			log_warnx("kroute_remove failed for %s/%u",
			    inet_ntoa(kr->r.prefix), kr->r.prefixlen);
			return (-1);
		}
		if (kr->next != NULL) {
			if (RB_INSERT(kroute_tree, &krt, kr->next) != NULL) {
				log_warnx("kroute_remove failed to add %s/%u",
				    inet_ntoa(kr->r.prefix), kr->r.prefixlen);
				return (-1);
			}
		}
	} else {
		/* somewhere in the list */
		while (krm->next != kr && krm->next != NULL)
			krm = krm->next;
		if (krm->next == NULL) {
			log_warnx("kroute_remove multipath list corrupted "
			    "for %s/%u", inet_ntoa(kr->r.prefix),
			    kr->r.prefixlen);
			return (-1);
		}
		krm->next = kr->next;
	}

	kr_redist_remove(krm, kr);
	rtlabel_unref(kr->r.rtlabel);

	free(kr);
	return (0);
}

void
kroute_clear(void)
{
	struct kroute_node	*kr;

	while ((kr = RB_MIN(kroute_tree, &krt)) != NULL)
		kroute_remove(kr);
}

struct kif_node *
kif_find(u_short ifindex)
{
	struct kif_node	s;

	bzero(&s, sizeof(s));
	s.k.ifindex = ifindex;

	return (RB_FIND(kif_tree, &kit, &s));
}

struct kif *
kif_findname(char *ifname, struct in_addr addr, struct kif_addr **kap)
{
	struct kif_node	*kif;
	struct kif_addr	*ka;

	RB_FOREACH(kif, kif_tree, &kit)
		if (!strcmp(ifname, kif->k.ifname)) {
			ka = TAILQ_FIRST(&kif->addrs);
			if (addr.s_addr != 0) {
				TAILQ_FOREACH(ka, &kif->addrs, entry) {
					if (addr.s_addr == ka->addr.s_addr)
						break;
				}
			}
			if (kap != NULL)
				*kap = ka;
			return (&kif->k);
		}

	return (NULL);
}

struct kif_node *
kif_insert(u_short ifindex)
{
	struct kif_node	*kif;

	if ((kif = calloc(1, sizeof(struct kif_node))) == NULL)
		return (NULL);

	kif->k.ifindex = ifindex;
	TAILQ_INIT(&kif->addrs);

	if (RB_INSERT(kif_tree, &kit, kif) != NULL)
		fatalx("kif_insert: RB_INSERT");

	return (kif);
}

int
kif_remove(struct kif_node *kif)
{
	struct kif_addr	*ka;

	if (RB_REMOVE(kif_tree, &kit, kif) == NULL) {
		log_warnx("RB_REMOVE(kif_tree, &kit, kif)");
		return (-1);
	}

	while ((ka = TAILQ_FIRST(&kif->addrs)) != NULL) {
		TAILQ_REMOVE(&kif->addrs, ka, entry);
		free(ka);
	}
	free(kif);
	return (0);
}

void
kif_clear(void)
{
	struct kif_node	*kif;

	while ((kif = RB_MIN(kif_tree, &kit)) != NULL)
		kif_remove(kif);
}

struct kif *
kif_update(u_short ifindex, int flags, struct if_data *ifd,
    struct sockaddr_dl *sdl)
{
	struct kif_node		*kif;

	if ((kif = kif_find(ifindex)) == NULL) {
		if ((kif = kif_insert(ifindex)) == NULL)
			return (NULL);
		kif->k.nh_reachable = (flags & IFF_UP) &&
		    LINK_STATE_IS_UP(ifd->ifi_link_state);
	}

	kif->k.flags = flags;
	kif->k.link_state = ifd->ifi_link_state;
	kif->k.if_type = ifd->ifi_type;
	kif->k.baudrate = ifd->ifi_baudrate;
	kif->k.mtu = ifd->ifi_mtu;
	kif->k.rdomain = ifd->ifi_rdomain;

	if (sdl && sdl->sdl_family == AF_LINK) {
		if (sdl->sdl_nlen >= sizeof(kif->k.ifname))
			memcpy(kif->k.ifname, sdl->sdl_data,
			    sizeof(kif->k.ifname) - 1);
		else if (sdl->sdl_nlen > 0)
			memcpy(kif->k.ifname, sdl->sdl_data,
			    sdl->sdl_nlen);
		/* string already terminated via calloc() */
	}

	return (&kif->k);
}

int
kif_validate(u_short ifindex)
{
	struct kif_node		*kif;

	if ((kif = kif_find(ifindex)) == NULL) {
		log_warnx("interface with index %u not found", ifindex);
		return (1);
	}

	return (kif->k.nh_reachable);
}

struct kroute_node *
kroute_match(in_addr_t key)
{
	int			 i;
	struct kroute_node	*kr;

	/* we will never match the default route */
	for (i = 32; i > 0; i--)
		if ((kr = kroute_find(key & prefixlen2mask(i), i,
		    RTP_ANY)) != NULL)
			return (kr);

	/* if we don't have a match yet, try to find a default route */
	if ((kr = kroute_find(0, 0, RTP_ANY)) != NULL)
			return (kr);

	return (NULL);
}

/* misc */
int
protect_lo(void)
{
	struct kroute_node	*kr;

	/* special protection for 127/8 */
	if ((kr = calloc(1, sizeof(struct kroute_node))) == NULL) {
		log_warn("protect_lo");
		return (-1);
	}
	kr->r.prefix.s_addr = htonl(INADDR_LOOPBACK & IN_CLASSA_NET);
	kr->r.prefixlen = 8;
	kr->r.flags = F_KERNEL|F_CONNECTED;

	if (RB_INSERT(kroute_tree, &krt, kr) != NULL)
		free(kr);	/* kernel route already there, no problem */

	return (0);
}

u_int8_t
prefixlen_classful(in_addr_t ina)
{
	/* it hurt to write this. */

	if (ina >= 0xf0000000U)		/* class E */
		return (32);
	else if (ina >= 0xe0000000U)	/* class D */
		return (4);
	else if (ina >= 0xc0000000U)	/* class C */
		return (24);
	else if (ina >= 0x80000000U)	/* class B */
		return (16);
	else				/* class A */
		return (8);
}

u_int8_t
mask2prefixlen(in_addr_t ina)
{
	if (ina == 0)
		return (0);
	else
		return (33 - ffs(ntohl(ina)));
}

in_addr_t
prefixlen2mask(u_int8_t prefixlen)
{
	if (prefixlen == 0)
		return (0);

	return (htonl(0xffffffff << (32 - prefixlen)));
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
if_change(u_short ifindex, int flags, struct if_data *ifd,
    struct sockaddr_dl *sdl)
{
	struct kroute_node	*kr, *tkr;
	struct kif		*kif;
	u_int8_t		 reachable;

	if ((kif = kif_update(ifindex, flags, ifd, sdl)) == NULL) {
		log_warn("if_change:  kif_update(%u)", ifindex);
		return;
	}

	/* notify ospfe about interface link state */
	main_imsg_compose_ospfe(IMSG_IFINFO, 0, kif, sizeof(struct kif));

	reachable = (kif->flags & IFF_UP) &&
	    LINK_STATE_IS_UP(kif->link_state);

	if (reachable == kif->nh_reachable)
		return;		/* nothing changed wrt nexthop validity */

	kif->nh_reachable = reachable;

	/* update redistribute list */
	RB_FOREACH(kr, kroute_tree, &krt) {
		for (tkr = kr; tkr != NULL; tkr = tkr->next) {
			if (tkr->r.ifindex == ifindex) {
				if (reachable)
					tkr->r.flags &= ~F_DOWN;
				else
					tkr->r.flags |= F_DOWN;

			}
		}
		kr_redistribute(kr);
	}
}

void
if_newaddr(u_short ifindex, struct sockaddr_in *ifa, struct sockaddr_in *mask,
    struct sockaddr_in *brd)
{
	struct kif_node 	*kif;
	struct kif_addr 	*ka;
	struct ifaddrchange	 ifn;

	if (ifa == NULL || ifa->sin_family != AF_INET)
		return;
	if ((kif = kif_find(ifindex)) == NULL) {
		log_warnx("if_newaddr: corresponding if %d not found", ifindex);
		return;
	}
	if ((ka = calloc(1, sizeof(struct kif_addr))) == NULL)
		fatal("if_newaddr");
	ka->addr = ifa->sin_addr;
	if (mask)
		ka->mask = mask->sin_addr;
	else
		ka->mask.s_addr = INADDR_NONE;
	if (brd)
		ka->dstbrd = brd->sin_addr;
	else
		ka->dstbrd.s_addr = INADDR_NONE;

	TAILQ_INSERT_TAIL(&kif->addrs, ka, entry);

	ifn.addr = ka->addr;
	ifn.mask = ka->mask;
	ifn.dst = ka->dstbrd;
	ifn.ifindex = ifindex;
	main_imsg_compose_ospfe(IMSG_IFADDRADD, 0, &ifn, sizeof(ifn));
}

void
if_deladdr(u_short ifindex, struct sockaddr_in *ifa, struct sockaddr_in *mask,
    struct sockaddr_in *brd)
{
	struct kif_node 	*kif;
	struct kif_addr		*ka, *nka;
	struct ifaddrchange	 ifc;

	if (ifa == NULL || ifa->sin_family != AF_INET)
		return;
	if ((kif = kif_find(ifindex)) == NULL) {
		log_warnx("if_deladdr: corresponding if %d not found", ifindex);
		return;
	}

	for (ka = TAILQ_FIRST(&kif->addrs); ka != NULL; ka = nka) {
		nka = TAILQ_NEXT(ka, entry);

		if (ka->addr.s_addr == ifa->sin_addr.s_addr) {
			TAILQ_REMOVE(&kif->addrs, ka, entry);
			ifc.addr = ifa->sin_addr;
			ifc.ifindex = ifindex;
			main_imsg_compose_ospfe(IMSG_IFADDRDEL, 0, &ifc,
			    sizeof(ifc));
			free(ka);
			return;
		}
	}
}

void
if_announce(void *msg)
{
	struct if_announcemsghdr	*ifan;
	struct kif_node			*kif;

	ifan = msg;

	switch (ifan->ifan_what) {
	case IFAN_ARRIVAL:
		kif = kif_insert(ifan->ifan_index);
		strlcpy(kif->k.ifname, ifan->ifan_name, sizeof(kif->k.ifname));
		break;
	case IFAN_DEPARTURE:
		kif = kif_find(ifan->ifan_index);
		if (kif != NULL)
			kif_remove(kif);
		break;
	}
}

/* rtsock */
int
send_rtmsg(int fd, int action, struct kroute *kroute)
{
	struct iovec		iov[5];
	struct rt_msghdr	hdr;
	struct sockaddr_in	prefix;
	struct sockaddr_in	nexthop;
	struct sockaddr_in	mask;
	struct sockaddr_rtlabel	sa_rl;
	int			iovcnt = 0;
	const char		*label;

	if (kr_state.fib_sync == 0)
		return (0);

	/* initialize header */
	bzero(&hdr, sizeof(hdr));
	hdr.rtm_version = RTM_VERSION;
	hdr.rtm_type = action;
	hdr.rtm_priority = kr_state.fib_prio;
	hdr.rtm_tableid = kr_state.rdomain;	/* rtableid */
	if (action == RTM_CHANGE)
		hdr.rtm_fmask = RTF_REJECT|RTF_BLACKHOLE;
	else
		hdr.rtm_flags = RTF_MPATH;
	hdr.rtm_seq = kr_state.rtseq++;	/* overflow doesn't matter */
	hdr.rtm_msglen = sizeof(hdr);
	/* adjust iovec */
	iov[iovcnt].iov_base = &hdr;
	iov[iovcnt++].iov_len = sizeof(hdr);

	bzero(&prefix, sizeof(prefix));
	prefix.sin_len = sizeof(prefix);
	prefix.sin_family = AF_INET;
	prefix.sin_addr.s_addr = kroute->prefix.s_addr;
	/* adjust header */
	hdr.rtm_addrs |= RTA_DST;
	hdr.rtm_msglen += sizeof(prefix);
	/* adjust iovec */
	iov[iovcnt].iov_base = &prefix;
	iov[iovcnt++].iov_len = sizeof(prefix);

	if (kroute->nexthop.s_addr != 0) {
		bzero(&nexthop, sizeof(nexthop));
		nexthop.sin_len = sizeof(nexthop);
		nexthop.sin_family = AF_INET;
		nexthop.sin_addr.s_addr = kroute->nexthop.s_addr;
		/* adjust header */
		hdr.rtm_flags |= RTF_GATEWAY;
		hdr.rtm_addrs |= RTA_GATEWAY;
		hdr.rtm_msglen += sizeof(nexthop);
		/* adjust iovec */
		iov[iovcnt].iov_base = &nexthop;
		iov[iovcnt++].iov_len = sizeof(nexthop);
	}

	bzero(&mask, sizeof(mask));
	mask.sin_len = sizeof(mask);
	mask.sin_family = AF_INET;
	mask.sin_addr.s_addr = prefixlen2mask(kroute->prefixlen);
	/* adjust header */
	hdr.rtm_addrs |= RTA_NETMASK;
	hdr.rtm_msglen += sizeof(mask);
	/* adjust iovec */
	iov[iovcnt].iov_base = &mask;
	iov[iovcnt++].iov_len = sizeof(mask);

	if (kroute->rtlabel != 0) {
		sa_rl.sr_len = sizeof(sa_rl);
		sa_rl.sr_family = AF_UNSPEC;
		label = rtlabel_id2name(kroute->rtlabel);
		if (strlcpy(sa_rl.sr_label, label,
		    sizeof(sa_rl.sr_label)) >= sizeof(sa_rl.sr_label)) {
			log_warnx("send_rtmsg: invalid rtlabel");
			return (-1);
		}
		/* adjust header */
		hdr.rtm_addrs |= RTA_LABEL;
		hdr.rtm_msglen += sizeof(sa_rl);
		/* adjust iovec */
		iov[iovcnt].iov_base = &sa_rl;
		iov[iovcnt++].iov_len = sizeof(sa_rl);
	}

retry:
	if (writev(fd, iov, iovcnt) == -1) {
		if (errno == ESRCH) {
			if (hdr.rtm_type == RTM_CHANGE) {
				hdr.rtm_type = RTM_ADD;
				goto retry;
			} else if (hdr.rtm_type == RTM_DELETE) {
				log_info("route %s/%u vanished before delete",
				    inet_ntoa(kroute->prefix),
				    kroute->prefixlen);
				return (0);
			}
		}
		log_warn("send_rtmsg: action %u, prefix %s/%u", hdr.rtm_type,
		    inet_ntoa(kroute->prefix), kroute->prefixlen);
		return (0);
	}

	return (0);
}

int
fetchtable(void)
{
	size_t			 len;
	int			 mib[7];
	char			*buf;
	int			 rv;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;
	mib[6] = kr_state.rdomain;	/* rtableid */

	if (sysctl(mib, 7, NULL, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		return (-1);
	}
	if ((buf = malloc(len)) == NULL) {
		log_warn("fetchtable");
		return (-1);
	}
	if (sysctl(mib, 7, buf, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		free(buf);
		return (-1);
	}

	rv = rtmsg_process(buf, len);
	free(buf);

	return (rv);
}

int
fetchifs(u_short ifindex)
{
	size_t			 len;
	int			 mib[6];
	char			*buf;
	int			 rv;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_IFLIST;
	mib[5] = ifindex;

	if (sysctl(mib, 6, NULL, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		return (-1);
	}
	if ((buf = malloc(len)) == NULL) {
		log_warn("fetchif");
		return (-1);
	}
	if (sysctl(mib, 6, buf, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		free(buf);
		return (-1);
	}

	rv = rtmsg_process(buf, len);
	free(buf);

	return (rv);
}

int
dispatch_rtmsg(void)
{
	char			 buf[RT_BUF_SIZE];
	ssize_t			 n;

	if ((n = read(kr_state.fd, &buf, sizeof(buf))) == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return (0);
		log_warn("dispatch_rtmsg: read error");
		return (-1);
	}

	if (n == 0) {
		log_warnx("routing socket closed");
		return (-1);
	}

	return (rtmsg_process(buf, n));
}

int
rtmsg_process(char *buf, size_t len)
{
	struct rt_msghdr	*rtm;
	struct if_msghdr	 ifm;
	struct ifa_msghdr	*ifam;
	struct sockaddr		*sa, *rti_info[RTAX_MAX];
	struct sockaddr_in	*sa_in;
	struct sockaddr_rtlabel	*label;
	struct kroute_node	*kr, *okr;
	struct in_addr		 prefix, nexthop;
	u_int8_t		 prefixlen, prio;
	int			 flags, mpath;
	u_short			 ifindex = 0;
	int			 rv, delay;

	size_t			 offset;
	char			*next;

	for (offset = 0; offset < len; offset += rtm->rtm_msglen) {
		next = buf + offset;
		rtm = (struct rt_msghdr *)next;
		if (len < offset + sizeof(u_short) ||
		    len < offset + rtm->rtm_msglen)
			fatalx("%s: partial rtm in buffer", __func__);
		if (rtm->rtm_version != RTM_VERSION)
			continue;

		prefix.s_addr = 0;
		prefixlen = 0;
		nexthop.s_addr = 0;
		mpath = 0;
		prio = 0;
		flags = F_KERNEL;

		sa = (struct sockaddr *)(next + rtm->rtm_hdrlen);
		get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

		switch (rtm->rtm_type) {
		case RTM_ADD:
		case RTM_GET:
		case RTM_CHANGE:
		case RTM_DELETE:
			if (rtm->rtm_errno)		/* failed attempts... */
				continue;

			if (rtm->rtm_tableid != kr_state.rdomain)
				continue;

			if (rtm->rtm_type == RTM_GET &&
			    rtm->rtm_pid != kr_state.pid)
				continue;

			if ((sa = rti_info[RTAX_DST]) == NULL)
				continue;

			/* Skip ARP/ND cache and broadcast routes. */
			if (rtm->rtm_flags & (RTF_LLINFO|RTF_BROADCAST))
				continue;

			if (rtm->rtm_flags & RTF_MPATH)
				mpath = 1;
			prio = rtm->rtm_priority;
			flags = (prio == kr_state.fib_prio) ?
			    F_OSPFD_INSERTED : F_KERNEL;

			switch (sa->sa_family) {
			case AF_INET:
				prefix.s_addr =
				    ((struct sockaddr_in *)sa)->sin_addr.s_addr;
				sa_in = (struct sockaddr_in *)
				    rti_info[RTAX_NETMASK];
				if (sa_in != NULL) {
					if (sa_in->sin_len != 0)
						prefixlen = mask2prefixlen(
						    sa_in->sin_addr.s_addr);
				} else if (rtm->rtm_flags & RTF_HOST)
					prefixlen = 32;
				else
					prefixlen =
					    prefixlen_classful(prefix.s_addr);
				if (rtm->rtm_flags & RTF_STATIC)
					flags |= F_STATIC;
				if (rtm->rtm_flags & RTF_BLACKHOLE)
					flags |= F_BLACKHOLE;
				if (rtm->rtm_flags & RTF_REJECT)
					flags |= F_REJECT;
				if (rtm->rtm_flags & RTF_DYNAMIC)
					flags |= F_DYNAMIC;
				break;
			default:
				continue;
			}

			ifindex = rtm->rtm_index;
			if ((sa = rti_info[RTAX_GATEWAY]) != NULL) {
				switch (sa->sa_family) {
				case AF_INET:
					if (rtm->rtm_flags & RTF_CONNECTED)
						flags |= F_CONNECTED;

					nexthop.s_addr = ((struct
					    sockaddr_in *)sa)->sin_addr.s_addr;
					break;
				case AF_LINK:
					/*
					 * Traditional BSD connected routes have
					 * a gateway of type AF_LINK.
					 */
					flags |= F_CONNECTED;
					break;
				}
			}
		}

		switch (rtm->rtm_type) {
		case RTM_ADD:
		case RTM_GET:
		case RTM_CHANGE:
			if (nexthop.s_addr == 0 && !(flags & F_CONNECTED)) {
				log_warnx("no nexthop for %s/%u",
				    inet_ntoa(prefix), prefixlen);
				continue;
			}

			if ((okr = kroute_find(prefix.s_addr, prefixlen, prio))
			    != NULL) {
				/* get the correct route */
				kr = okr;
				if ((mpath || prio == kr_state.fib_prio) &&
				    (kr = kroute_matchgw(okr, nexthop)) ==
				    NULL) {
					log_warnx("%s: mpath route not found",
					    __func__);
					/* add routes we missed out earlier */
					goto add;
				}

				if (kr->r.flags & F_REDISTRIBUTED)
					flags |= F_REDISTRIBUTED;
				kr->r.nexthop.s_addr = nexthop.s_addr;
				kr->r.flags = flags;
				kr->r.ifindex = ifindex;

				rtlabel_unref(kr->r.rtlabel);
				kr->r.rtlabel = 0;
				kr->r.ext_tag = 0;
				if ((label = (struct sockaddr_rtlabel *)
				    rti_info[RTAX_LABEL]) != NULL) {
					kr->r.rtlabel =
					    rtlabel_name2id(label->sr_label);
					kr->r.ext_tag =
					    rtlabel_id2tag(kr->r.rtlabel);
				}

				if (kif_validate(kr->r.ifindex))
					kr->r.flags &= ~F_DOWN;
				else
					kr->r.flags |= F_DOWN;

				/* just readd, the RDE will care */
				kr->serial = kr_state.fib_serial;
				kr_redistribute(okr);
			} else {
add:
				if ((kr = calloc(1,
				    sizeof(struct kroute_node))) == NULL) {
					log_warn("%s: calloc", __func__);
					return (-1);
				}

				kr->r.prefix.s_addr = prefix.s_addr;
				kr->r.prefixlen = prefixlen;
				kr->r.nexthop.s_addr = nexthop.s_addr;
				kr->r.flags = flags;
				kr->r.ifindex = ifindex;
				kr->r.priority = prio;

				if (rtm->rtm_priority == kr_state.fib_prio) {
					log_warnx("alien OSPF route %s/%d",
					    inet_ntoa(prefix), prefixlen);
					rv = send_rtmsg(kr_state.fd,
					    RTM_DELETE, &kr->r);
					free(kr);
					if (rv == -1)
						return (-1);
				} else {
					if ((label = (struct sockaddr_rtlabel *)
					    rti_info[RTAX_LABEL]) != NULL) {
						kr->r.rtlabel =
						    rtlabel_name2id(
						    label->sr_label);
						kr->r.ext_tag =
						    rtlabel_id2tag(
						    kr->r.rtlabel);
					}

					kroute_insert(kr);
				}
			}
			break;
		case RTM_DELETE:
			if ((kr = kroute_find(prefix.s_addr, prefixlen, prio))
			    == NULL)
				continue;
			if (!(kr->r.flags & F_KERNEL))
				continue;
			/* get the correct route */
			okr = kr;
			if (mpath &&
			    (kr = kroute_matchgw(kr, nexthop)) == NULL) {
				log_warnx("%s: mpath route not found",
				    __func__);
				return (-1);
			}
			if (kroute_remove(kr) == -1)
				return (-1);
			break;
		case RTM_IFINFO:
			memcpy(&ifm, next, sizeof(ifm));
			if_change(ifm.ifm_index, ifm.ifm_flags, &ifm.ifm_data,
			    (struct sockaddr_dl *)rti_info[RTAX_IFP]);
			break;
		case RTM_NEWADDR:
			ifam = (struct ifa_msghdr *)rtm;
			if ((ifam->ifam_addrs & (RTA_NETMASK | RTA_IFA |
			    RTA_BRD)) == 0)
				break;

			if_newaddr(ifam->ifam_index,
			    (struct sockaddr_in *)rti_info[RTAX_IFA],
			    (struct sockaddr_in *)rti_info[RTAX_NETMASK],
			    (struct sockaddr_in *)rti_info[RTAX_BRD]);
			break;
		case RTM_DELADDR:
			ifam = (struct ifa_msghdr *)rtm;
			if ((ifam->ifam_addrs & (RTA_NETMASK | RTA_IFA |
			    RTA_BRD)) == 0)
				break;

			if_deladdr(ifam->ifam_index,
			    (struct sockaddr_in *)rti_info[RTAX_IFA],
			    (struct sockaddr_in *)rti_info[RTAX_NETMASK],
			    (struct sockaddr_in *)rti_info[RTAX_BRD]);
			break;
		case RTM_IFANNOUNCE:
			if_announce(next);
			break;
		case RTM_DESYNC:
			/*
			 * We lost some routing packets. Schedule a reload
			 * of the kernel route/interface information.
			 */
			if (kr_state.reload_state == KR_RELOAD_IDLE) {
				delay = KR_RELOAD_TIMER;
				log_info("desync; scheduling fib reload");
			} else {
				delay = KR_RELOAD_HOLD_TIMER;
				log_debug("desync during KR_RELOAD_%s",
				    kr_state.reload_state ==
				    KR_RELOAD_FETCH ? "FETCH" : "HOLD");
			}
			kr_state.reload_state = KR_RELOAD_FETCH;
			kr_fib_reload_arm_timer(delay);
			break;
		default:
			/* ignore for now */
			break;
		}
	}

	return (offset);
}
