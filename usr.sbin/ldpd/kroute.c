/*	$OpenBSD: kroute.c,v 1.71 2023/03/08 04:43:13 guenther Exp $ */

/*
 * Copyright (c) 2015, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netmpls/mpls.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "ldpd.h"
#include "log.h"

struct {
	uint32_t		rtseq;
	pid_t			pid;
	int			fib_sync;
	int			fd;
	int			ioctl_fd;
	struct event		ev;
	unsigned int		rdomain;
} kr_state;

struct kroute_node {
	TAILQ_ENTRY(kroute_node)	 entry;
	struct kroute_priority		*kprio;		/* back pointer */
	struct kroute			 r;
};

struct kroute_priority {
	TAILQ_ENTRY(kroute_priority)	 entry;
	struct kroute_prefix		*kp;		/* back pointer */
	uint8_t				 priority;
	TAILQ_HEAD(, kroute_node)	 nexthops;
};

struct kroute_prefix {
	RB_ENTRY(kroute_prefix)		 entry;
	int				 af;
	union ldpd_addr			 prefix;
	uint8_t				 prefixlen;
	TAILQ_HEAD(plist, kroute_priority) priorities;
};
RB_HEAD(kroute_tree, kroute_prefix);
RB_PROTOTYPE(kroute_tree, kroute_prefix, entry, kroute_compare)

struct kif_addr {
	TAILQ_ENTRY(kif_addr)	 entry;
	struct kaddr		 a;
};

struct kif_node {
	RB_ENTRY(kif_node)	 entry;
	TAILQ_HEAD(, kif_addr)	 addrs;
	struct kif		 k;
	struct kpw		*kpw;
};
RB_HEAD(kif_tree, kif_node);
RB_PROTOTYPE(kif_tree, kif_node, entry, kif_compare)

static void		 kr_dispatch_msg(int, short, void *);
static void		 kr_redist_remove(struct kroute *);
static int		 kr_redist_eval(struct kroute *);
static void		 kr_redistribute(struct kroute_prefix *);
static __inline int	 kroute_compare(struct kroute_prefix *,
			    struct kroute_prefix *);
static struct kroute_prefix	*kroute_find_prefix(int, union ldpd_addr *,
			    uint8_t);
static struct kroute_priority	*kroute_find_prio(struct kroute_prefix *,
			    uint8_t);
static struct kroute_node	*kroute_find_gw(struct kroute_priority *,
				    union ldpd_addr *);
static int		 kroute_insert(struct kroute *);
static int		 kroute_uninstall(struct kroute_node *);
static int		 kroute_remove(struct kroute *);
static void		 kroute_clear(void);
static __inline int	 kif_compare(struct kif_node *, struct kif_node *);
static struct kif_node	*kif_find(unsigned short);
static struct kif_node	*kif_insert(unsigned short);
static int		 kif_remove(struct kif_node *);
static struct kif_node	*kif_update(unsigned short, int, struct if_data *,
			    struct sockaddr_dl *, int *);
static struct kroute_priority	*kroute_match(int, union ldpd_addr *);
static uint8_t		 prefixlen_classful(in_addr_t);
static void		 get_rtaddrs(int, struct sockaddr *,
			    struct sockaddr **);
static void		 if_change(unsigned short, int, struct if_data *,
		 	   struct sockaddr_dl *);
static void		 if_newaddr(unsigned short, struct sockaddr *,
			    struct sockaddr *, struct sockaddr *);
static void		 if_deladdr(unsigned short, struct sockaddr *,
			    struct sockaddr *, struct sockaddr *);
static void		 if_announce(void *);
static int		 send_rtmsg(int, int, struct kroute *, int);
static int		 send_rtmsg_v4(int fd, int, struct kroute *, int);
static int		 send_rtmsg_v6(int fd, int, struct kroute *, int);
static int		 fetchtable(void);
static int		 fetchifs(void);
static int		 dispatch_rtmsg(void);
static int		 rtmsg_process(char *, size_t);
static int		 rtmsg_process_route(struct rt_msghdr *,
			    struct sockaddr *[RTAX_MAX]);
static int		 kmpw_install(const char *, struct kpw *);
static int		 kmpw_uninstall(const char *);

RB_GENERATE(kroute_tree, kroute_prefix, entry, kroute_compare)
RB_GENERATE(kif_tree, kif_node, entry, kif_compare)

static struct kroute_tree	 krt = RB_INITIALIZER(&krt);
static struct kif_tree		 kit = RB_INITIALIZER(&kit);

int
kif_init(void)
{
	if (fetchifs() == -1)
		return (-1);

	if ((kr_state.ioctl_fd = socket(AF_INET,
	    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) == -1) {
		log_warn("%s: ioctl socket", __func__);
		return (-1);
	}

	return (0);
}

int
kr_init(int fs, unsigned int rdomain)
{
	int		opt = 0, rcvbuf, default_rcvbuf;
	socklen_t	optlen;
	unsigned int	rtfilter;

	kr_state.fib_sync = fs;
	kr_state.rdomain = rdomain;

	if ((kr_state.fd = socket(AF_ROUTE,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) == -1) {
		log_warn("%s: socket", __func__);
		return (-1);
	}

	/* not interested in my own messages */
	if (setsockopt(kr_state.fd, SOL_SOCKET, SO_USELOOPBACK,
	    &opt, sizeof(opt)) == -1)
		log_warn("%s: setsockopt(SO_USELOOPBACK)", __func__);

	/* filter out unwanted messages */
	rtfilter = ROUTE_FILTER(RTM_ADD) | ROUTE_FILTER(RTM_GET) |
	    ROUTE_FILTER(RTM_CHANGE) | ROUTE_FILTER(RTM_DELETE) |
	    ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_NEWADDR) |
	    ROUTE_FILTER(RTM_DELADDR) | ROUTE_FILTER(RTM_IFANNOUNCE);

	if (setsockopt(kr_state.fd, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)
		log_warn("%s: setsockopt(ROUTE_MSGFILTER)", __func__);

	/* grow receive buffer, don't wanna miss messages */
	optlen = sizeof(default_rcvbuf);
	if (getsockopt(kr_state.fd, SOL_SOCKET, SO_RCVBUF,
	    &default_rcvbuf, &optlen) == -1)
		log_warn("%s: getsockopt SOL_SOCKET SO_RCVBUF", __func__);
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

	event_set(&kr_state.ev, kr_state.fd, EV_READ | EV_PERSIST,
	    kr_dispatch_msg, NULL);
	event_add(&kr_state.ev, NULL);

	return (0);
}

void
kif_redistribute(const char *ifname)
{
	struct kif_node		*kif;
	struct kif_addr		*ka;

	RB_FOREACH(kif, kif_tree, &kit) {
		if (kif->k.rdomain != kr_state.rdomain)
			continue;

		if (ifname && strcmp(kif->k.ifname, ifname) != 0)
			continue;

		TAILQ_FOREACH(ka, &kif->addrs, entry)
			main_imsg_compose_ldpe(IMSG_NEWADDR, 0, &ka->a,
			    sizeof(ka->a));
	}
}

int
kr_change(struct kroute *kr)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;
	int			 action = RTM_ADD;

	kp = kroute_find_prefix(kr->af, &kr->prefix, kr->prefixlen);
	if (kp == NULL)
		goto miss;

	kprio = kroute_find_prio(kp, kr->priority);
	if (kprio == NULL)
		goto miss;

	kn = kroute_find_gw(kprio, &kr->nexthop);
	if (kn == NULL)
		goto miss;

	if (kn->r.flags & F_LDPD_INSERTED)
		action = RTM_CHANGE;

	kn->r.local_label = kr->local_label;
	kn->r.remote_label = kr->remote_label;
	kn->r.flags = kn->r.flags | F_LDPD_INSERTED;

	/* send update */
	if (send_rtmsg(kr_state.fd, action, &kn->r, AF_MPLS) == -1)
		return (-1);

	if (ldp_addrisset(kn->r.af, &kn->r.nexthop) &&
	    kn->r.remote_label != NO_LABEL) {
		if (send_rtmsg(kr_state.fd, RTM_CHANGE, &kn->r, kn->r.af) == -1)
			return (-1);
	}

	return (0);

 miss:
	log_warnx("%s: lost FEC %s/%d nexthop %s", __func__,
	    log_addr(kr->af, &kr->prefix), kr->prefixlen,
	    log_addr(kr->af, &kr->nexthop));
	return (-1);
}

int
kr_delete(struct kroute *kr)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;
	int			 update = 0;

	kp = kroute_find_prefix(kr->af, &kr->prefix, kr->prefixlen);
	if (kp == NULL)
		return (0);
	kprio = kroute_find_prio(kp, kr->priority);
	if (kprio == NULL)
		return (0);
	kn = kroute_find_gw(kprio, &kr->nexthop);
	if (kn == NULL)
		return (0);

	if (!(kn->r.flags & F_LDPD_INSERTED))
		return (0);
	if (ldp_addrisset(kn->r.af, &kn->r.nexthop) &&
	    kn->r.remote_label != NO_LABEL)
		update = 1;

	/* kill MPLS LSP */
	if (send_rtmsg(kr_state.fd, RTM_DELETE, &kn->r, AF_MPLS) == -1)
		return (-1);

	kn->r.flags &= ~F_LDPD_INSERTED;
	kn->r.local_label = NO_LABEL;
	kn->r.remote_label = NO_LABEL;

	if (update &&
	    send_rtmsg(kr_state.fd, RTM_CHANGE, &kn->r, kn->r.af) == -1)
		return (-1);

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
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;
	struct kif_node		*kif;

	if (kr_state.fib_sync == 1)	/* already coupled */
		return;

	kr_state.fib_sync = 1;

	RB_FOREACH(kp, kroute_tree, &krt) {
		kprio = TAILQ_FIRST(&kp->priorities);
		if (kprio == NULL)
			continue;

		TAILQ_FOREACH(kn, &kprio->nexthops, entry) {
			if (!(kn->r.flags & F_LDPD_INSERTED))
				continue;

			send_rtmsg(kr_state.fd, RTM_ADD, &kn->r, AF_MPLS);

			if (ldp_addrisset(kn->r.af, &kn->r.nexthop) &&
			    kn->r.remote_label != NO_LABEL) {
				send_rtmsg(kr_state.fd, RTM_CHANGE,
				    &kn->r, kn->r.af);
			}
		}
	}

	RB_FOREACH(kif, kif_tree, &kit)
		if (kif->kpw)
			kmpw_install(kif->k.ifname, kif->kpw);

	log_info("kernel routing table coupled");
}

void
kr_fib_decouple(void)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;
	uint32_t		 rl;
	struct kif_node		*kif;

	if (kr_state.fib_sync == 0)	/* already decoupled */
		return;

	RB_FOREACH(kp, kroute_tree, &krt) {
		kprio = TAILQ_FIRST(&kp->priorities);
		if (kprio == NULL)
			continue;

		TAILQ_FOREACH(kn, &kprio->nexthops, entry) {
			if (!(kn->r.flags & F_LDPD_INSERTED))
				continue;

			send_rtmsg(kr_state.fd, RTM_DELETE,
			    &kn->r, AF_MPLS);

			if (ldp_addrisset(kn->r.af, &kn->r.nexthop) &&
			    kn->r.remote_label != NO_LABEL) {
				rl = kn->r.remote_label;
				kn->r.remote_label = NO_LABEL;
				send_rtmsg(kr_state.fd, RTM_CHANGE,
				    &kn->r, kn->r.af);
				kn->r.remote_label = rl;
			}
		}
	}

	RB_FOREACH(kif, kif_tree, &kit)
		if (kif->kpw)
			kmpw_uninstall(kif->k.ifname);

	kr_state.fib_sync = 0;
	log_info("kernel routing table decoupled");
}

void
kr_change_egress_label(int af, int was_implicit)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	RB_FOREACH(kp, kroute_tree, &krt) {
		if (kp->af != af)
			continue;

		TAILQ_FOREACH(kprio, &kp->priorities, entry) {
			TAILQ_FOREACH(kn, &kprio->nexthops, entry) {
				if (kn->r.local_label > MPLS_LABEL_RESERVED_MAX)
					continue;

				if (!was_implicit) {
					kn->r.local_label = MPLS_LABEL_IMPLNULL;
					continue;
				}

				switch (kn->r.af) {
				case AF_INET:
					kn->r.local_label = MPLS_LABEL_IPV4NULL;
					break;
				case AF_INET6:
					kn->r.local_label = MPLS_LABEL_IPV6NULL;
					break;
				default:
					break;
				}
			}
		}
	}
}

static void
kr_dispatch_msg(int fd, short event, void *bula)
{
	if (dispatch_rtmsg() == -1)
		event_loopexit(NULL);
}

void
kr_show_route(struct imsg *imsg)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;
	int			 flags;
	struct kroute		 kr;

	switch (imsg->hdr.type) {
	case IMSG_CTL_KROUTE:
		if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(flags)) {
			log_warnx("%s: wrong imsg len", __func__);
			return;
		}
		memcpy(&flags, imsg->data, sizeof(flags));

		RB_FOREACH(kp, kroute_tree, &krt)
			TAILQ_FOREACH(kprio, &kp->priorities, entry)
				TAILQ_FOREACH(kn, &kprio->nexthops, entry) {
					if (flags && !(kn->r.flags & flags))
						continue;

					main_imsg_compose_ldpe(IMSG_CTL_KROUTE,
					    imsg->hdr.pid, &kn->r,
					    sizeof(kn->r));
				}
		break;
	case IMSG_CTL_KROUTE_ADDR:
		if (imsg->hdr.len != IMSG_HEADER_SIZE + sizeof(kr)) {
			log_warnx("%s: wrong imsg len", __func__);
			return;
		}
		memcpy(&kr, imsg->data, sizeof(kr));

		kprio = kroute_match(kr.af, &kr.prefix);
		if (kprio == NULL)
			break;

		TAILQ_FOREACH(kn, &kprio->nexthops, entry)
			main_imsg_compose_ldpe(IMSG_CTL_KROUTE, imsg->hdr.pid,
			    &kn->r, sizeof(kn->r));
		break;
	default:
		log_debug("%s: error handling imsg", __func__);
		break;
	}
	main_imsg_compose_ldpe(IMSG_CTL_END, imsg->hdr.pid, NULL, 0);
}

void
kr_ifinfo(char *ifname, pid_t pid)
{
	struct kif_node	*kif;

	RB_FOREACH(kif, kif_tree, &kit)
		if (ifname == NULL || !strcmp(ifname, kif->k.ifname)) {
			main_imsg_compose_ldpe(IMSG_CTL_IFINFO,
			    pid, &kif->k, sizeof(kif->k));
		}

	main_imsg_compose_ldpe(IMSG_CTL_END, pid, NULL, 0);
}

static void
kr_redist_remove(struct kroute *kr)
{
	/* was the route redistributed? */
	if ((kr->flags & F_REDISTRIBUTED) == 0)
		return;

	/* remove redistributed flag */
	kr->flags &= ~F_REDISTRIBUTED;
	main_imsg_compose_lde(IMSG_NETWORK_DEL, 0, kr, sizeof(*kr));
}

static int
kr_redist_eval(struct kroute *kr)
{
	/* was the route redistributed? */
	if (kr->flags & F_REDISTRIBUTED)
		goto dont_redistribute;

	/* Dynamic routes are not redistributable. */
	if (kr->flags & F_DYNAMIC)
		goto dont_redistribute;

	/* filter-out non-redistributable addresses */
	if (bad_addr(kr->af, &kr->prefix) ||
	    (kr->af == AF_INET6 && IN6_IS_SCOPE_EMBED(&kr->prefix.v6)))
		goto dont_redistribute;

	/* do not redistribute the default route */
	if (kr->prefixlen == 0)
		goto dont_redistribute;

	/*
	 * Consider networks with nexthop loopback as not redistributable
	 * unless it is a reject or blackhole route.
	 */
	switch (kr->af) {
	case AF_INET:
		if (kr->nexthop.v4.s_addr == htonl(INADDR_LOOPBACK) &&
		    !(kr->flags & (F_BLACKHOLE|F_REJECT)))
			goto dont_redistribute;
		break;
	case AF_INET6:
		if (IN6_IS_ADDR_LOOPBACK(&kr->nexthop.v6) &&
		    !(kr->flags & (F_BLACKHOLE|F_REJECT)))
			goto dont_redistribute;
		break;
	default:
		log_debug("%s: unexpected address-family", __func__);
		break;
	}

	/* prefix should be redistributed */
	kr->flags |= F_REDISTRIBUTED;
	main_imsg_compose_lde(IMSG_NETWORK_ADD, 0, kr, sizeof(*kr));
	return (1);

 dont_redistribute:
	return (0);
}

static void
kr_redistribute(struct kroute_prefix *kp)
{
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	TAILQ_FOREACH_REVERSE(kprio, &kp->priorities, plist, entry) {
		if (kprio == TAILQ_FIRST(&kp->priorities)) {
			TAILQ_FOREACH(kn, &kprio->nexthops, entry)
				kr_redist_eval(&kn->r);
		} else {
			TAILQ_FOREACH(kn, &kprio->nexthops, entry)
				kr_redist_remove(&kn->r);
		}
	}
}

/* rb-tree compare */
static __inline int
kroute_compare(struct kroute_prefix *a, struct kroute_prefix *b)
{
	int		 addrcmp;

	if (a->af < b->af)
		return (-1);
	if (a->af > b->af)
		return (1);

	addrcmp = ldp_addrcmp(a->af, &a->prefix, &b->prefix);
	if (addrcmp != 0)
		return (addrcmp);

	if (a->prefixlen < b->prefixlen)
		return (-1);
	if (a->prefixlen > b->prefixlen)
		return (1);

	return (0);
}

/* tree management */
static struct kroute_prefix *
kroute_find_prefix(int af, union ldpd_addr *prefix, uint8_t prefixlen)
{
	struct kroute_prefix	 s;

	s.af = af;
	s.prefix = *prefix;
	s.prefixlen = prefixlen;

	return (RB_FIND(kroute_tree, &krt, &s));
}

static struct kroute_priority *
kroute_find_prio(struct kroute_prefix *kp, uint8_t prio)
{
	struct kroute_priority	*kprio;

	/* RTP_ANY here picks the lowest priority node */
	if (prio == RTP_ANY)
		return (TAILQ_FIRST(&kp->priorities));

	TAILQ_FOREACH(kprio, &kp->priorities, entry)
		if (kprio->priority == prio)
			return (kprio);

	return (NULL);
}

static struct kroute_node *
kroute_find_gw(struct kroute_priority *kprio, union ldpd_addr *nh)
{
	struct kroute_node	*kn;

	TAILQ_FOREACH(kn, &kprio->nexthops, entry)
		if (ldp_addrcmp(kprio->kp->af, &kn->r.nexthop, nh) == 0)
			return (kn);

	return (NULL);
}

static int
kroute_insert(struct kroute *kr)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio, *tmp;
	struct kroute_node	*kn;

	kp = kroute_find_prefix(kr->af, &kr->prefix, kr->prefixlen);
	if (kp == NULL) {
		kp = calloc(1, sizeof((*kp)));
		if (kp == NULL)
			fatal(__func__);
		kp->af = kr->af;
		kp->prefix = kr->prefix;
		kp->prefixlen = kr->prefixlen;
		TAILQ_INIT(&kp->priorities);
		RB_INSERT(kroute_tree, &krt, kp);
	}

	kprio = kroute_find_prio(kp, kr->priority);
	if (kprio == NULL) {
		kprio = calloc(1, sizeof(*kprio));
		if (kprio == NULL)
			fatal(__func__);
		kprio->kp = kp;
		kprio->priority = kr->priority;
		TAILQ_INIT(&kprio->nexthops);

		/* lower priorities first */
		TAILQ_FOREACH(tmp, &kp->priorities, entry)
			if (tmp->priority > kprio->priority)
				break;
		if (tmp)
			TAILQ_INSERT_BEFORE(tmp, kprio, entry);
		else
			TAILQ_INSERT_TAIL(&kp->priorities, kprio, entry);
	}

	kn = kroute_find_gw(kprio, &kr->nexthop);
	if (kn == NULL) {
		kn = calloc(1, sizeof(*kn));
		if (kn == NULL)
			fatal(__func__);
		kn->kprio = kprio;
		kn->r = *kr;
		TAILQ_INSERT_TAIL(&kprio->nexthops, kn, entry);
	}

	kr_redistribute(kp);
	return (0);
}

static int
kroute_uninstall(struct kroute_node *kn)
{
	/* kill MPLS LSP if one was installed */
	if (kn->r.flags & F_LDPD_INSERTED)
		if (send_rtmsg(kr_state.fd, RTM_DELETE, &kn->r, AF_MPLS) == -1)
			return (-1);

	return (0);
}

static int
kroute_remove(struct kroute *kr)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	kp = kroute_find_prefix(kr->af, &kr->prefix, kr->prefixlen);
	if (kp == NULL)
		goto notfound;
	kprio = kroute_find_prio(kp, kr->priority);
	if (kprio == NULL)
		goto notfound;
	kn = kroute_find_gw(kprio, &kr->nexthop);
	if (kn == NULL)
		goto notfound;

	kr_redist_remove(&kn->r);
	kroute_uninstall(kn);

	TAILQ_REMOVE(&kprio->nexthops, kn, entry);
	free(kn);

	if (TAILQ_EMPTY(&kprio->nexthops)) {
		TAILQ_REMOVE(&kp->priorities, kprio, entry);
		free(kprio);
	}

	if (TAILQ_EMPTY(&kp->priorities)) {
		if (RB_REMOVE(kroute_tree, &krt, kp) == NULL) {
			log_warnx("%s failed for %s/%u", __func__,
			    log_addr(kr->af, &kr->prefix), kp->prefixlen);
			return (-1);
		}
		free(kp);
	} else
		kr_redistribute(kp);

	return (0);

 notfound:
	log_warnx("%s failed to find %s/%u", __func__,
	    log_addr(kr->af, &kr->prefix), kr->prefixlen);
	return (-1);
}

static void
kroute_clear(void)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	while ((kp = RB_MIN(kroute_tree, &krt)) != NULL) {
		while ((kprio = TAILQ_FIRST(&kp->priorities)) != NULL) {
			while ((kn = TAILQ_FIRST(&kprio->nexthops)) != NULL) {
				kr_redist_remove(&kn->r);
				kroute_uninstall(kn);
				TAILQ_REMOVE(&kprio->nexthops, kn, entry);
				free(kn);
			}
			TAILQ_REMOVE(&kp->priorities, kprio, entry);
			free(kprio);
		}
		RB_REMOVE(kroute_tree, &krt, kp);
		free(kp);
	}
}

static __inline int
kif_compare(struct kif_node *a, struct kif_node *b)
{
	return (b->k.ifindex - a->k.ifindex);
}

/* tree management */
static struct kif_node *
kif_find(unsigned short ifindex)
{
	struct kif_node	s;

	memset(&s, 0, sizeof(s));
	s.k.ifindex = ifindex;

	return (RB_FIND(kif_tree, &kit, &s));
}

struct kif *
kif_findname(char *ifname)
{
	struct kif_node	*kif;

	RB_FOREACH(kif, kif_tree, &kit)
		if (!strcmp(ifname, kif->k.ifname))
			return (&kif->k);

	return (NULL);
}

static struct kif_node *
kif_insert(unsigned short ifindex)
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

static int
kif_remove(struct kif_node *kif)
{
	struct kif_addr	*ka;

	if (RB_REMOVE(kif_tree, &kit, kif) == NULL) {
		log_warnx("RB_REMOVE(kif_tree, &kit, kif)");
		return (-1);
	}

	while ((ka = TAILQ_FIRST(&kif->addrs)) != NULL) {
		main_imsg_compose_ldpe(IMSG_DELADDR, 0, &ka->a, sizeof(ka->a));
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

static struct kif_node *
kif_update(unsigned short ifindex, int flags, struct if_data *ifd,
    struct sockaddr_dl *sdl, int *link_old)
{
	struct kif_node		*kif;

	if ((kif = kif_find(ifindex)) == NULL) {
		if ((kif = kif_insert(ifindex)) == NULL)
			return (NULL);
	} else
		*link_old = (kif->k.flags & IFF_UP) &&
		    LINK_STATE_IS_UP(kif->k.link_state);

	kif->k.flags = flags;
	kif->k.link_state = ifd->ifi_link_state;
	if (sdl)
		memcpy(kif->k.mac, LLADDR(sdl), sizeof(kif->k.mac));
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

	return (kif);
}

static struct kroute_priority *
kroute_match(int af, union ldpd_addr *key)
{
	int			 i, maxprefixlen;
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	union ldpd_addr		 addr;

	switch (af) {
	case AF_INET:
		maxprefixlen = 32;
		break;
	case AF_INET6:
		maxprefixlen = 128;
		break;
	default:
		log_warnx("%s: unknown af", __func__);
		return (NULL);
	}

	for (i = maxprefixlen; i >= 0; i--) {
		ldp_applymask(af, &addr, key, i);

		kp = kroute_find_prefix(af, &addr, i);
		if (kp == NULL)
			continue;

		kprio = kroute_find_prio(kp, RTP_ANY);
		if (kprio != NULL)
			return (kprio);
	}

	return (NULL);
}

/* misc */
static uint8_t
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

#define ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

static void
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

static void
if_change(unsigned short ifindex, int flags, struct if_data *ifd,
    struct sockaddr_dl *sdl)
{
	struct kif_node		*kif;
	struct kif_addr		*ka;
	int			 link_old = 0, link_new;

	kif = kif_update(ifindex, flags, ifd, sdl, &link_old);
	if (!kif) {
		log_warn("%s: kif_update(%u)", __func__, ifindex);
		return;
	}
	link_new = (kif->k.flags & IFF_UP) &&
	    LINK_STATE_IS_UP(kif->k.link_state);

	if (link_new == link_old)
		return;

	main_imsg_compose_ldpe(IMSG_IFSTATUS, 0, &kif->k, sizeof(struct kif));
	if (link_new) {
		TAILQ_FOREACH(ka, &kif->addrs, entry)
			main_imsg_compose_ldpe(IMSG_NEWADDR, 0, &ka->a,
			    sizeof(ka->a));
	} else {
		TAILQ_FOREACH(ka, &kif->addrs, entry)
			main_imsg_compose_ldpe(IMSG_DELADDR, 0, &ka->a,
			    sizeof(ka->a));
	}
}

static void
if_newaddr(unsigned short ifindex, struct sockaddr *ifa, struct sockaddr *mask,
    struct sockaddr *brd)
{
	struct kif_node		*kif;
	struct sockaddr_in	*ifa4, *mask4, *brd4;
	struct sockaddr_in6	*ifa6, *mask6, *brd6;
	struct kif_addr		*ka;

	if (ifa == NULL)
		return;
	if ((kif = kif_find(ifindex)) == NULL) {
		log_warnx("%s: corresponding if %d not found", __func__,
		    ifindex);
		return;
	}

	switch (ifa->sa_family) {
	case AF_INET:
		ifa4 = (struct sockaddr_in *) ifa;
		mask4 = (struct sockaddr_in *) mask;
		brd4 = (struct sockaddr_in *) brd;

		/* filter out unwanted addresses */
		if (bad_addr_v4(ifa4->sin_addr))
			return;

		if ((ka = calloc(1, sizeof(struct kif_addr))) == NULL)
			fatal("if_newaddr");
		ka->a.addr.v4 = ifa4->sin_addr;
		if (mask4)
			ka->a.prefixlen =
			    mask2prefixlen(mask4->sin_addr.s_addr);
		if (brd4)
			ka->a.dstbrd.v4 = brd4->sin_addr;
		break;
	case AF_INET6:
		ifa6 = (struct sockaddr_in6 *) ifa;
		mask6 = (struct sockaddr_in6 *) mask;
		brd6 = (struct sockaddr_in6 *) brd;

		/* We only care about link-local and global-scope. */
		if (bad_addr_v6(&ifa6->sin6_addr))
			return;

		clearscope(&ifa6->sin6_addr);

		if ((ka = calloc(1, sizeof(struct kif_addr))) == NULL)
			fatal("if_newaddr");
		ka->a.addr.v6 = ifa6->sin6_addr;
		if (mask6)
			ka->a.prefixlen = mask2prefixlen6(mask6);
		if (brd6)
			ka->a.dstbrd.v6 = brd6->sin6_addr;
		break;
	default:
		return;
	}

	ka->a.ifindex = ifindex;
	ka->a.af = ifa->sa_family;
	TAILQ_INSERT_TAIL(&kif->addrs, ka, entry);

	/* notify ldpe about new address */
	main_imsg_compose_ldpe(IMSG_NEWADDR, 0, &ka->a, sizeof(ka->a));
}

static void
if_deladdr(unsigned short ifindex, struct sockaddr *ifa, struct sockaddr *mask,
    struct sockaddr *brd)
{
	struct kif_node		*kif;
	struct sockaddr_in	*ifa4, *mask4, *brd4;
	struct sockaddr_in6	*ifa6, *mask6, *brd6;
	struct kaddr		 k;
	struct kif_addr		*ka, *nka;

	if (ifa == NULL)
		return;
	if ((kif = kif_find(ifindex)) == NULL) {
		log_warnx("%s: corresponding if %d not found", __func__,
		    ifindex);
		return;
	}

	memset(&k, 0, sizeof(k));
	k.af = ifa->sa_family;
	switch (ifa->sa_family) {
	case AF_INET:
		ifa4 = (struct sockaddr_in *) ifa;
		mask4 = (struct sockaddr_in *) mask;
		brd4 = (struct sockaddr_in *) brd;

		/* filter out unwanted addresses */
		if (bad_addr_v4(ifa4->sin_addr))
			return;

		k.addr.v4 = ifa4->sin_addr;
		if (mask4)
			k.prefixlen = mask2prefixlen(mask4->sin_addr.s_addr);
		if (brd4)
			k.dstbrd.v4 = brd4->sin_addr;
		break;
	case AF_INET6:
		ifa6 = (struct sockaddr_in6 *) ifa;
		mask6 = (struct sockaddr_in6 *) mask;
		brd6 = (struct sockaddr_in6 *) brd;

		/* We only care about link-local and global-scope. */
		if (bad_addr_v6(&ifa6->sin6_addr))
			return;

		clearscope(&ifa6->sin6_addr);

		k.addr.v6 = ifa6->sin6_addr;
		if (mask6)
			k.prefixlen = mask2prefixlen6(mask6);
		if (brd6)
			k.dstbrd.v6 = brd6->sin6_addr;
		break;
	default:
		return;
	}

	for (ka = TAILQ_FIRST(&kif->addrs); ka != NULL; ka = nka) {
		nka = TAILQ_NEXT(ka, entry);

		if (ka->a.af != k.af ||
		    ka->a.prefixlen != k.prefixlen ||
		    ldp_addrcmp(ka->a.af, &ka->a.addr, &k.addr))
			continue;

		/* notify ldpe about removed address */
		main_imsg_compose_ldpe(IMSG_DELADDR, 0, &ka->a, sizeof(ka->a));
		TAILQ_REMOVE(&kif->addrs, ka, entry);
		free(ka);
		return;
	}
}

static void
if_announce(void *msg)
{
	struct if_announcemsghdr	*ifan;
	struct kif_node			*kif;

	ifan = msg;

	switch (ifan->ifan_what) {
	case IFAN_ARRIVAL:
		kif = kif_insert(ifan->ifan_index);
		if (kif)
			strlcpy(kif->k.ifname, ifan->ifan_name,
			    sizeof(kif->k.ifname));
		break;
	case IFAN_DEPARTURE:
		kif = kif_find(ifan->ifan_index);
		if (kif)
			kif_remove(kif);
		break;
	}
}

/* rtsock */
static int
send_rtmsg(int fd, int action, struct kroute *kr, int family)
{
	switch (kr->af) {
	case AF_INET:
		return (send_rtmsg_v4(fd, action, kr, family));
	case AF_INET6:
		return (send_rtmsg_v6(fd, action, kr, family));
	default:
		fatalx("send_rtmsg: unknown af");
	}
}

static int
send_rtmsg_v4(int fd, int action, struct kroute *kr, int family)
{
	struct iovec		iov[5];
	struct rt_msghdr	hdr;
	struct sockaddr_mpls	label_in, label_out;
	struct sockaddr_in	dst, mask, nexthop;
	int			iovcnt = 0;

	if (kr_state.fib_sync == 0)
		return (0);

	/*
	 * Reserved labels (implicit and explicit NULL) should not be added
	 * to the FIB.
	 */
	if (family == AF_MPLS && kr->local_label < MPLS_LABEL_RESERVED_MAX)
		return (0);

	/* initialize header */
	memset(&hdr, 0, sizeof(hdr));
	hdr.rtm_version = RTM_VERSION;

	hdr.rtm_type = action;
	hdr.rtm_flags = RTF_UP;
	hdr.rtm_fmask = RTF_MPLS;
	hdr.rtm_seq = kr_state.rtseq++;	/* overflow doesn't matter */
	hdr.rtm_msglen = sizeof(hdr);
	hdr.rtm_hdrlen = sizeof(struct rt_msghdr);
	hdr.rtm_priority = kr->priority;
	hdr.rtm_tableid = kr_state.rdomain;	/* rtableid */
	/* adjust iovec */
	iov[iovcnt].iov_base = &hdr;
	iov[iovcnt++].iov_len = sizeof(hdr);

	if (family == AF_MPLS) {
		memset(&label_in, 0, sizeof(label_in));
		label_in.smpls_len = sizeof(label_in);
		label_in.smpls_family = AF_MPLS;
		label_in.smpls_label =
		    htonl(kr->local_label << MPLS_LABEL_OFFSET);
		/* adjust header */
		hdr.rtm_flags |= RTF_MPLS | RTF_MPATH;
		hdr.rtm_addrs |= RTA_DST;
		hdr.rtm_msglen += sizeof(label_in);
		/* adjust iovec */
		iov[iovcnt].iov_base = &label_in;
		iov[iovcnt++].iov_len = sizeof(label_in);
	} else {
		memset(&dst, 0, sizeof(dst));
		dst.sin_len = sizeof(dst);
		dst.sin_family = AF_INET;
		dst.sin_addr = kr->prefix.v4;
		/* adjust header */
		hdr.rtm_addrs |= RTA_DST;
		hdr.rtm_msglen += sizeof(dst);
		/* adjust iovec */
		iov[iovcnt].iov_base = &dst;
		iov[iovcnt++].iov_len = sizeof(dst);
	}

	memset(&nexthop, 0, sizeof(nexthop));
	nexthop.sin_len = sizeof(nexthop);
	nexthop.sin_family = AF_INET;
	nexthop.sin_addr = kr->nexthop.v4;
	/* adjust header */
	hdr.rtm_flags |= RTF_GATEWAY;
	hdr.rtm_addrs |= RTA_GATEWAY;
	hdr.rtm_msglen += sizeof(nexthop);
	/* adjust iovec */
	iov[iovcnt].iov_base = &nexthop;
	iov[iovcnt++].iov_len = sizeof(nexthop);

	if (family == AF_INET) {
		memset(&mask, 0, sizeof(mask));
		mask.sin_len = sizeof(mask);
		mask.sin_family = AF_INET;
		mask.sin_addr.s_addr = prefixlen2mask(kr->prefixlen);
		/* adjust header */
		hdr.rtm_addrs |= RTA_NETMASK;
		hdr.rtm_msglen += sizeof(mask);
		/* adjust iovec */
		iov[iovcnt].iov_base = &mask;
		iov[iovcnt++].iov_len = sizeof(mask);
	}

	/* If action is RTM_DELETE we have to get rid of MPLS infos */
	if (kr->remote_label != NO_LABEL && action != RTM_DELETE) {
		memset(&label_out, 0, sizeof(label_out));
		label_out.smpls_len = sizeof(label_out);
		label_out.smpls_family = AF_MPLS;
		label_out.smpls_label =
		    htonl(kr->remote_label << MPLS_LABEL_OFFSET);
		/* adjust header */
		hdr.rtm_addrs |= RTA_SRC;
		hdr.rtm_flags |= RTF_MPLS;
		hdr.rtm_msglen += sizeof(label_out);
		/* adjust iovec */
		iov[iovcnt].iov_base = &label_out;
		iov[iovcnt++].iov_len = sizeof(label_out);

		if (kr->remote_label == MPLS_LABEL_IMPLNULL) {
			if (family == AF_MPLS)
				hdr.rtm_mpls = MPLS_OP_POP;
			else
				return (0);
		} else {
			if (family == AF_MPLS)
				hdr.rtm_mpls = MPLS_OP_SWAP;
			else
				hdr.rtm_mpls = MPLS_OP_PUSH;
		}
	}

 retry:
	if (writev(fd, iov, iovcnt) == -1) {
		if (errno == ESRCH) {
			if (hdr.rtm_type == RTM_CHANGE && family == AF_MPLS) {
				hdr.rtm_type = RTM_ADD;
				goto retry;
			} else if (hdr.rtm_type == RTM_DELETE) {
				log_info("route %s/%u vanished before delete",
				    inet_ntoa(kr->prefix.v4), kr->prefixlen);
				return (-1);
			}
		}
		log_warn("%s action %u, af %s, prefix %s/%u", __func__,
		    hdr.rtm_type, af_name(family), inet_ntoa(kr->prefix.v4),
		    kr->prefixlen);
		return (-1);
	}

	return (0);
}

static int
send_rtmsg_v6(int fd, int action, struct kroute *kr, int family)
{
	struct iovec		iov[5];
	struct rt_msghdr	hdr;
	struct sockaddr_mpls	label_in, label_out;
	struct sockaddr_in6	dst, mask, nexthop;
	int			iovcnt = 0;

	if (kr_state.fib_sync == 0)
		return (0);

	/*
	 * Reserved labels (implicit and explicit NULL) should not be added
	 * to the FIB.
	 */
	if (family == AF_MPLS && kr->local_label < MPLS_LABEL_RESERVED_MAX)
		return (0);

	/* initialize header */
	memset(&hdr, 0, sizeof(hdr));
	hdr.rtm_version = RTM_VERSION;

	hdr.rtm_type = action;
	hdr.rtm_flags = RTF_UP;
	hdr.rtm_fmask = RTF_MPLS;
	hdr.rtm_seq = kr_state.rtseq++;	/* overflow doesn't matter */
	hdr.rtm_msglen = sizeof(hdr);
	hdr.rtm_hdrlen = sizeof(struct rt_msghdr);
	hdr.rtm_priority = kr->priority;
	hdr.rtm_tableid = kr_state.rdomain;	/* rtableid */
	/* adjust iovec */
	iov[iovcnt].iov_base = &hdr;
	iov[iovcnt++].iov_len = sizeof(hdr);

	if (family == AF_MPLS) {
		memset(&label_in, 0, sizeof(label_in));
		label_in.smpls_len = sizeof(label_in);
		label_in.smpls_family = AF_MPLS;
		label_in.smpls_label =
		    htonl(kr->local_label << MPLS_LABEL_OFFSET);
		/* adjust header */
		hdr.rtm_flags |= RTF_MPLS | RTF_MPATH;
		hdr.rtm_addrs |= RTA_DST;
		hdr.rtm_msglen += sizeof(label_in);
		/* adjust iovec */
		iov[iovcnt].iov_base = &label_in;
		iov[iovcnt++].iov_len = sizeof(label_in);
	} else {
		memset(&dst, 0, sizeof(dst));
		dst.sin6_len = sizeof(dst);
		dst.sin6_family = AF_INET6;
		dst.sin6_addr = kr->prefix.v6;
		/* adjust header */
		hdr.rtm_addrs |= RTA_DST;
		hdr.rtm_msglen += ROUNDUP(sizeof(dst));
		/* adjust iovec */
		iov[iovcnt].iov_base = &dst;
		iov[iovcnt++].iov_len = ROUNDUP(sizeof(dst));
	}

	memset(&nexthop, 0, sizeof(nexthop));
	nexthop.sin6_len = sizeof(nexthop);
	nexthop.sin6_family = AF_INET6;
	nexthop.sin6_addr = kr->nexthop.v6;
	nexthop.sin6_scope_id = kr->ifindex;
	/*
	 * XXX we should set the sin6_scope_id but the kernel
	 * XXX does not expect it that way. It must be fiddled
	 * XXX into the sin6_addr. Welcome to the typical
	 * XXX IPv6 insanity and all without wine bottles.
	 */
	embedscope(&nexthop);

	/* adjust header */
	hdr.rtm_flags |= RTF_GATEWAY;
	hdr.rtm_addrs |= RTA_GATEWAY;
	hdr.rtm_msglen += ROUNDUP(sizeof(nexthop));
	/* adjust iovec */
	iov[iovcnt].iov_base = &nexthop;
	iov[iovcnt++].iov_len = ROUNDUP(sizeof(nexthop));

	if (family == AF_INET6) {
		memset(&mask, 0, sizeof(mask));
		mask.sin6_len = sizeof(mask);
		mask.sin6_family = AF_INET6;
		mask.sin6_addr = *prefixlen2mask6(kr->prefixlen);
		/* adjust header */
		if (kr->prefixlen == 128)
			hdr.rtm_flags |= RTF_HOST;
		hdr.rtm_addrs |= RTA_NETMASK;
		hdr.rtm_msglen += ROUNDUP(sizeof(mask));
		/* adjust iovec */
		iov[iovcnt].iov_base = &mask;
		iov[iovcnt++].iov_len = ROUNDUP(sizeof(mask));
	}

	/* If action is RTM_DELETE we have to get rid of MPLS infos */
	if (kr->remote_label != NO_LABEL && action != RTM_DELETE) {
		memset(&label_out, 0, sizeof(label_out));
		label_out.smpls_len = sizeof(label_out);
		label_out.smpls_family = AF_MPLS;
		label_out.smpls_label =
		    htonl(kr->remote_label << MPLS_LABEL_OFFSET);
		/* adjust header */
		hdr.rtm_addrs |= RTA_SRC;
		hdr.rtm_flags |= RTF_MPLS;
		hdr.rtm_msglen += sizeof(label_out);
		/* adjust iovec */
		iov[iovcnt].iov_base = &label_out;
		iov[iovcnt++].iov_len = sizeof(label_out);

		if (kr->remote_label == MPLS_LABEL_IMPLNULL) {
			if (family == AF_MPLS)
				hdr.rtm_mpls = MPLS_OP_POP;
			else
				return (0);
		} else {
			if (family == AF_MPLS)
				hdr.rtm_mpls = MPLS_OP_SWAP;
			else
				hdr.rtm_mpls = MPLS_OP_PUSH;
		}
	}

 retry:
	if (writev(fd, iov, iovcnt) == -1) {
		if (errno == ESRCH) {
			if (hdr.rtm_type == RTM_CHANGE && family == AF_MPLS) {
				hdr.rtm_type = RTM_ADD;
				goto retry;
			} else if (hdr.rtm_type == RTM_DELETE) {
				log_info("route %s/%u vanished before delete",
				    log_addr(kr->af, &kr->prefix),
				    kr->prefixlen);
				return (-1);
			}
		}
		log_warn("%s action %u, af %s, prefix %s/%u", __func__,
		    hdr.rtm_type, af_name(family), log_addr(kr->af,
		    &kr->prefix), kr->prefixlen);
		return (-1);
	}
	return (0);
}

static int
fetchtable(void)
{
	size_t			 len;
	int			 mib[7];
	char			*buf;
	int			 rv;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;
	mib[6] = kr_state.rdomain;	/* rtableid */

	if (sysctl(mib, 7, NULL, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		return (-1);
	}
	if ((buf = malloc(len)) == NULL) {
		log_warn(__func__);
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

static int
fetchifs(void)
{
	size_t			 len;
	int			 mib[6];
	char			*buf;
	int			 rv;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;	/* wildcard */
	mib[4] = NET_RT_IFLIST;
	mib[5] = 0;

	if (sysctl(mib, 6, NULL, &len, NULL, 0) == -1) {
		log_warn("sysctl");
		return (-1);
	}
	if ((buf = malloc(len)) == NULL) {
		log_warn(__func__);
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

static int
dispatch_rtmsg(void)
{
	char			 buf[RT_BUF_SIZE];
	ssize_t			 n;

	if ((n = read(kr_state.fd, &buf, sizeof(buf))) == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return (0);
		log_warn("%s: read error", __func__);
		return (-1);
	}

	if (n == 0) {
		log_warnx("routing socket closed");
		return (-1);
	}

	return (rtmsg_process(buf, n));
}

static int
rtmsg_process(char *buf, size_t len)
{
	struct rt_msghdr	*rtm;
	struct if_msghdr	 ifm;
	struct ifa_msghdr	*ifam;
	struct sockaddr		*sa, *rti_info[RTAX_MAX];
	size_t			 offset;
	char			*next;

	for (offset = 0; offset < len; offset += rtm->rtm_msglen) {
		next = buf + offset;
		rtm = (struct rt_msghdr *)next;
		if (len < offset + sizeof(unsigned short) ||
		    len < offset + rtm->rtm_msglen)
			fatalx("rtmsg_process: partial rtm in buffer");
		if (rtm->rtm_version != RTM_VERSION)
			continue;

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

			/* Skip ARP/ND cache and broadcast routes. */
			if (rtm->rtm_flags & (RTF_LLINFO|RTF_BROADCAST))
				continue;

			/* LDP should follow the IGP and ignore BGP routes */
			if (rtm->rtm_priority == RTP_BGP)
				continue;

			if (rtmsg_process_route(rtm, rti_info) == -1)
				return (-1);
		}

		switch (rtm->rtm_type) {
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
			    (struct sockaddr *)rti_info[RTAX_IFA],
			    (struct sockaddr *)rti_info[RTAX_NETMASK],
			    (struct sockaddr *)rti_info[RTAX_BRD]);
			break;
		case RTM_DELADDR:
			ifam = (struct ifa_msghdr *)rtm;
			if ((ifam->ifam_addrs & (RTA_NETMASK | RTA_IFA |
			    RTA_BRD)) == 0)
				break;

			if_deladdr(ifam->ifam_index,
			    (struct sockaddr *)rti_info[RTAX_IFA],
			    (struct sockaddr *)rti_info[RTAX_NETMASK],
			    (struct sockaddr *)rti_info[RTAX_BRD]);
			break;
		case RTM_IFANNOUNCE:
			if_announce(next);
			break;
		default:
			/* ignore for now */
			break;
		}
	}

	return (offset);
}

static int
rtmsg_process_route(struct rt_msghdr *rtm, struct sockaddr *rti_info[RTAX_MAX])
{
	struct sockaddr		*sa;
	struct sockaddr_in	*sa_in;
	struct sockaddr_in6	*sa_in6;
	struct kroute		 kr;
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	if ((sa = rti_info[RTAX_DST]) == NULL)
		return (-1);

	memset(&kr, 0, sizeof(kr));
	kr.af = sa->sa_family;
	switch (kr.af) {
	case AF_INET:
		kr.prefix.v4 = ((struct sockaddr_in *)sa)->sin_addr;
		sa_in = (struct sockaddr_in *) rti_info[RTAX_NETMASK];
		if (sa_in != NULL && sa_in->sin_len != 0)
			kr.prefixlen = mask2prefixlen(sa_in->sin_addr.s_addr);
		else if (rtm->rtm_flags & RTF_HOST)
			kr.prefixlen = 32;
		else if (kr.prefix.v4.s_addr == INADDR_ANY)
			kr.prefixlen = 0;
		else
			kr.prefixlen = prefixlen_classful(kr.prefix.v4.s_addr);
		break;
	case AF_INET6:
		kr.prefix.v6 = ((struct sockaddr_in6 *)sa)->sin6_addr;
		sa_in6 = (struct sockaddr_in6 *)rti_info[RTAX_NETMASK];
		if (sa_in6 != NULL && sa_in6->sin6_len != 0)
			kr.prefixlen = mask2prefixlen6(sa_in6);
		else if (rtm->rtm_flags & RTF_HOST)
			kr.prefixlen = 128;
		else if (IN6_IS_ADDR_UNSPECIFIED(&kr.prefix.v6))
			kr.prefixlen = 0;
		else
			fatalx("in6 net addr without netmask");
		break;
	default:
		return (0);
	}
	kr.ifindex = rtm->rtm_index;
	if ((sa = rti_info[RTAX_GATEWAY]) != NULL) {
		switch (sa->sa_family) {
		case AF_INET:
			kr.nexthop.v4 = ((struct sockaddr_in *)sa)->sin_addr;
			break;
		case AF_INET6:
			sa_in6 = (struct sockaddr_in6 *)sa;
			recoverscope(sa_in6);
			kr.nexthop.v6 = sa_in6->sin6_addr;
			if (sa_in6->sin6_scope_id)
				kr.ifindex = sa_in6->sin6_scope_id;
			break;
		case AF_LINK:
			kr.flags |= F_CONNECTED;
			break;
		}
	}

	if (rtm->rtm_flags & RTF_STATIC)
		kr.flags |= F_STATIC;
	if (rtm->rtm_flags & RTF_BLACKHOLE)
		kr.flags |= F_BLACKHOLE;
	if (rtm->rtm_flags & RTF_REJECT)
		kr.flags |= F_REJECT;
	if (rtm->rtm_flags & RTF_DYNAMIC)
		kr.flags |= F_DYNAMIC;
	/* routes attached to connected or loopback interfaces */
	if (rtm->rtm_flags & RTF_CONNECTED ||
	    ldp_addrcmp(kr.af, &kr.prefix, &kr.nexthop) == 0)
		kr.flags |= F_CONNECTED;
	kr.priority = rtm->rtm_priority;

	if (rtm->rtm_type == RTM_CHANGE) {
		/*
		 * The kernel doesn't allow RTM_CHANGE for multipath routes.
		 * If we got this message we know that the route has only one
		 * nexthop and we should remove it before installing the same
		 * route with the new nexthop.
		 */
		kp = kroute_find_prefix(kr.af, &kr.prefix, kr.prefixlen);
		if (kp) {
			kprio = kroute_find_prio(kp, kr.priority);
			if (kprio) {
				kn = TAILQ_FIRST(&kprio->nexthops);
				if (kn)
					kroute_remove(&kn->r);
			}
		}
	}

	kn = NULL;
	kp = kroute_find_prefix(kr.af, &kr.prefix, kr.prefixlen);
	if (kp) {
		kprio = kroute_find_prio(kp, kr.priority);
		if (kprio)
			kn = kroute_find_gw(kprio, &kr.nexthop);
	}

	if (rtm->rtm_type == RTM_DELETE) {
		if (kn == NULL)
			return (0);
		return (kroute_remove(&kr));
	}

	if (!ldp_addrisset(kr.af, &kr.nexthop) && !(kr.flags & F_CONNECTED)) {
		log_warnx("%s: no nexthop for %s/%u", __func__,
		    log_addr(kr.af, &kr.prefix), kr.prefixlen);
		return (-1);
	}

	if (kn != NULL) {
		/* update route */
		kn->r = kr;
		kr_redistribute(kp);
	} else {
		kr.local_label = NO_LABEL;
		kr.remote_label = NO_LABEL;
		kroute_insert(&kr);
	}

	return (0);
}

int
kmpw_set(struct kpw *kpw)
{
	struct kif_node		*kif;

	kif = kif_find(kpw->ifindex);
	if (kif == NULL) {
		log_warnx("%s: failed to find mpw by index (%u)", __func__,
		    kpw->ifindex);
		return (-1);
	}

	if (kif->kpw == NULL)
		kif->kpw = malloc(sizeof(*kif->kpw));
	*kif->kpw = *kpw;

	return (kmpw_install(kif->k.ifname, kpw));
}

int
kmpw_unset(struct kpw *kpw)
{
	struct kif_node		*kif;

	kif = kif_find(kpw->ifindex);
	if (kif == NULL) {
		log_warnx("%s: failed to find mpw by index (%u)", __func__,
		    kpw->ifindex);
		return (-1);
	}

	if (kif->kpw == NULL) {
		log_warnx("%s: %s is not set", __func__, kif->k.ifname);
		return (-1);
	}

	free(kif->kpw);
	kif->kpw = NULL;
	return (kmpw_uninstall(kif->k.ifname));
}

static int
kmpw_install(const char *ifname, struct kpw *kpw)
{
	struct ifreq		 ifr;
	struct ifmpwreq		 imr;

	memset(&imr, 0, sizeof(imr));
	switch (kpw->pw_type) {
	case PW_TYPE_ETHERNET:
		imr.imr_type = IMR_TYPE_ETHERNET;
		break;
	case PW_TYPE_ETHERNET_TAGGED:
		imr.imr_type = IMR_TYPE_ETHERNET_TAGGED;
		break;
	default:
		log_warnx("%s: unhandled pseudowire type (%#X)", __func__,
		    kpw->pw_type);
		return (-1);
	}

	if (kpw->flags & F_PW_CWORD)
		imr.imr_flags |= IMR_FLAG_CONTROLWORD;

	memcpy(&imr.imr_nexthop, addr2sa(kpw->af, &kpw->nexthop, 0),
	    sizeof(imr.imr_nexthop));

	imr.imr_lshim.shim_label = kpw->local_label;
	imr.imr_rshim.shim_label = kpw->remote_label;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t) &imr;
	if (ioctl(kr_state.ioctl_fd, SIOCSETMPWCFG, &ifr) == -1) {
		log_warn("ioctl SIOCSETMPWCFG");
		return (-1);
	}

	return (0);
}

static int
kmpw_uninstall(const char *ifname)
{
	struct ifreq		 ifr;
	struct ifmpwreq		 imr;

	memset(&ifr, 0, sizeof(ifr));
	memset(&imr, 0, sizeof(imr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	ifr.ifr_data = (caddr_t) &imr;
	if (ioctl(kr_state.ioctl_fd, SIOCSETMPWCFG, &ifr) == -1) {
		log_warn("ioctl SIOCSETMPWCFG");
		return (-1);
	}

	return (0);
}

int
kmpw_find(const char *ifname)
{
	struct ifreq		 ifr;

	memset(&ifr, 0, sizeof(ifr));
	if (strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name)) >=
	    sizeof(ifr.ifr_name)) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	if (ioctl(kr_state.ioctl_fd, SIOCGPWE3, &ifr) == -1)
		return (-1);

	if (ifr.ifr_pwe3 != IF_PWE3_ETHERNET) {
		errno = EPFNOSUPPORT;
 		return (-1);
 	}

	return (0);
}
