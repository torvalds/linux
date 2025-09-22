/*	$OpenBSD: kroute.c,v 1.20 2023/03/08 04:43:13 guenther Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eigrpd.h"
#include "log.h"

static struct {
	uint32_t		rtseq;
	pid_t			pid;
	int			fib_sync;
	int			fd;
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
	union eigrpd_addr		 prefix;
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
};
RB_HEAD(kif_tree, kif_node);
RB_PROTOTYPE(kif_tree, kif_node, entry, kif_compare)

static void		 kr_dispatch_msg(int, short, void *);
static void		 kr_redist_remove(struct kroute *);
static int		 kr_redist_eval(struct kroute *);
static void		 kr_redistribute(struct kroute_prefix *);
static __inline int	 kroute_compare(struct kroute_prefix *,
			    struct kroute_prefix *);
static struct kroute_prefix *kroute_find_prefix(int, union eigrpd_addr *,
			    uint8_t);
static struct kroute_priority *kroute_find_prio(struct kroute_prefix *,
			    uint8_t);
static struct kroute_node *kroute_find_gw(struct kroute_priority *,
			    union eigrpd_addr *);
static struct kroute_node *kroute_insert(struct kroute *);
static int		 kroute_remove(struct kroute *);
static void		 kroute_clear(void);
static __inline int	 kif_compare(struct kif_node *, struct kif_node *);
static struct kif_node	*kif_find(unsigned short);
static struct kif_node	*kif_insert(unsigned short);
static int		 kif_remove(struct kif_node *);
static struct kif	*kif_update(unsigned short, int, struct if_data *,
			    struct sockaddr_dl *);
static int		 kif_validate(unsigned short);
static void		 protect_lo(void);
static uint8_t		 prefixlen_classful(in_addr_t);
static void		 get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
static void		 if_change(unsigned short, int, struct if_data *,
			    struct sockaddr_dl *);
static void		 if_newaddr(unsigned short, struct sockaddr *,
			    struct sockaddr *, struct sockaddr *);
static void		 if_deladdr(unsigned short, struct sockaddr *,
			    struct sockaddr *, struct sockaddr *);
static void		 if_announce(void *);
static int		 send_rtmsg_v4(int, int, struct kroute *);
static int		 send_rtmsg_v6(int, int, struct kroute *);
static int		 send_rtmsg(int, int, struct kroute *);
static int		 fetchtable(void);
static int		 fetchifs(void);
static int		 dispatch_rtmsg(void);
static int		 rtmsg_process(char *, size_t);
static int		 rtmsg_process_route(struct rt_msghdr *,
			    struct sockaddr *[RTAX_MAX]);

RB_GENERATE(kroute_tree, kroute_prefix, entry, kroute_compare)
RB_GENERATE(kif_tree, kif_node, entry, kif_compare)

static struct kroute_tree	 krt = RB_INITIALIZER(&krt);
static struct kif_tree		 kit = RB_INITIALIZER(&kit);

int
kif_init(void)
{
	if (fetchifs() == -1)
		return (-1);

	return (0);
}

int
kr_init(int fs, unsigned int rdomain)
{
	int		opt = 0, rcvbuf, default_rcvbuf;
	socklen_t	optlen;

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

	protect_lo();

	event_set(&kr_state.ev, kr_state.fd, EV_READ | EV_PERSIST,
	    kr_dispatch_msg, NULL);
	event_add(&kr_state.ev, NULL);

	return (0);
}

void
kif_redistribute(void)
{
	struct kif_node		*kif;
	struct kif_addr		*ka;

	RB_FOREACH(kif, kif_tree, &kit) {
		main_imsg_compose_eigrpe(IMSG_IFINFO, 0, &kif->k,
		    sizeof(struct kif));
		TAILQ_FOREACH(ka, &kif->addrs, entry) {
			main_imsg_compose_eigrpe(IMSG_NEWADDR, 0, &ka->a,
			    sizeof(ka->a));
		}
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
		kn = kroute_insert(kr);
	else {
		kprio = kroute_find_prio(kp, kr->priority);
		if (kprio == NULL)
			kn = kroute_insert(kr);
		else {
			kn = kroute_find_gw(kprio, &kr->nexthop);
			if (kn == NULL)
				kn = kroute_insert(kr);
			else
				action = RTM_CHANGE;
		}
	}

	/* send update */
	if (send_rtmsg(kr_state.fd, action, kr) == -1)
		return (-1);

	kn->r.flags |= F_EIGRPD_INSERTED;

	return (0);
}

int
kr_delete(struct kroute *kr)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	kp = kroute_find_prefix(kr->af, &kr->prefix, kr->prefixlen);
	if (kp == NULL)
		return (0);
	kprio = kroute_find_prio(kp, kr->priority);
	if (kprio == NULL)
		return (0);
	kn = kroute_find_gw(kprio, &kr->nexthop);
	if (kn == NULL)
		return (0);

	if (!(kn->r.flags & F_EIGRPD_INSERTED))
		return (0);

	if (send_rtmsg(kr_state.fd, RTM_DELETE, &kn->r) == -1)
		return (-1);

	if (kroute_remove(kr) == -1)
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

	if (kr_state.fib_sync == 1)	/* already coupled */
		return;

	kr_state.fib_sync = 1;

	RB_FOREACH(kp, kroute_tree, &krt)
		TAILQ_FOREACH(kprio, &kp->priorities, entry)
			TAILQ_FOREACH(kn, &kprio->nexthops, entry) {
				if (!(kn->r.flags & F_EIGRPD_INSERTED))
					continue;
				send_rtmsg(kr_state.fd, RTM_ADD, &kn->r);
			}

	log_info("kernel routing table coupled");
}

void
kr_fib_decouple(void)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	if (kr_state.fib_sync == 0)	/* already decoupled */
		return;

	RB_FOREACH(kp, kroute_tree, &krt)
		TAILQ_FOREACH(kprio, &kp->priorities, entry)
			TAILQ_FOREACH(kn, &kprio->nexthops, entry) {
				if (!(kn->r.flags & F_EIGRPD_INSERTED))
					continue;

				send_rtmsg(kr_state.fd, RTM_DELETE, &kn->r);
			}

	kr_state.fib_sync = 0;

	log_info("kernel routing table decoupled");
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
	struct kroute		 kr;
	int			 flags;

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

				kr = kn->r;
				if (kr.priority ==
				    eigrpd_conf->fib_priority_external)
					kr.flags |= F_CTL_EXTERNAL;
				main_imsg_compose_eigrpe(IMSG_CTL_KROUTE,
				    imsg->hdr.pid, &kr, sizeof(kr));
			}

	main_imsg_compose_eigrpe(IMSG_CTL_END, imsg->hdr.pid, NULL, 0);
}

void
kr_ifinfo(char *ifname, pid_t pid)
{
	struct kif_node	*kif;

	RB_FOREACH(kif, kif_tree, &kit)
		if (ifname == NULL || !strcmp(ifname, kif->k.ifname)) {
			main_imsg_compose_eigrpe(IMSG_CTL_IFINFO,
			    pid, &kif->k, sizeof(kif->k));
		}

	main_imsg_compose_eigrpe(IMSG_CTL_END, pid, NULL, 0);
}

static void
kr_redist_remove(struct kroute *kr)
{
	/* was the route redistributed? */
	if (!(kr->flags & F_REDISTRIBUTED))
		return;

	/* remove redistributed flag */
	kr->flags &= ~F_REDISTRIBUTED;
	main_imsg_compose_rde(IMSG_NETWORK_DEL, 0, kr, sizeof(*kr));
}

static int
kr_redist_eval(struct kroute *kr)
{
	/* Only non-eigrpd routes are considered for redistribution. */
	if (!(kr->flags & F_KERNEL))
		goto dont_redistribute;

	/* Dynamic routes are not redistributable. */
	if (kr->flags & F_DYNAMIC)
		goto dont_redistribute;

	/* filter-out non-redistributable addresses */
	if (bad_addr(kr->af, &kr->prefix) ||
	    (kr->af == AF_INET6 && IN6_IS_SCOPE_EMBED(&kr->prefix.v6)))
		goto dont_redistribute;

	/* interface is not up and running so don't announce */
	if (kr->flags & F_DOWN)
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
	main_imsg_compose_rde(IMSG_NETWORK_ADD, 0, kr, sizeof(*kr));
	return (1);

dont_redistribute:
	kr_redist_remove(kr);
	return (0);
}

static void
kr_redistribute(struct kroute_prefix *kp)
{
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;

	/* only the highest prio route can be redistributed */
	TAILQ_FOREACH_REVERSE(kprio, &kp->priorities, plist, entry) {
		if (kprio == TAILQ_FIRST(&kp->priorities)) {
			TAILQ_FOREACH(kn, &kprio->nexthops, entry)
				/* pick just one entry in case of multipath */
				if (kr_redist_eval(&kn->r))
					break;
		} else {
			TAILQ_FOREACH(kn, &kprio->nexthops, entry)
				kr_redist_remove(&kn->r);
		}
	}
}

static __inline int
kroute_compare(struct kroute_prefix *a, struct kroute_prefix *b)
{
	int		 addrcmp;

	if (a->af < b->af)
		return (-1);
	if (a->af > b->af)
		return (1);

	addrcmp = eigrp_addrcmp(a->af, &a->prefix, &b->prefix);
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
kroute_find_prefix(int af, union eigrpd_addr *prefix, uint8_t prefixlen)
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
kroute_find_gw(struct kroute_priority *kprio, union eigrpd_addr *nh)
{
	struct kroute_node	*kn;

	TAILQ_FOREACH(kn, &kprio->nexthops, entry)
		if (eigrp_addrcmp(kprio->kp->af, &kn->r.nexthop, nh) == 0)
			return (kn);

	return (NULL);
}

static struct kroute_node *
kroute_insert(struct kroute *kr)
{
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio, *tmp;
	struct kroute_node	*kn;

	kp = kroute_find_prefix(kr->af, &kr->prefix, kr->prefixlen);
	if (kp == NULL) {
		kp = calloc(1, sizeof((*kp)));
		if (kp == NULL)
			fatal("kroute_insert");
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
			fatal("kroute_insert");
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
			fatal("kroute_insert");
		kn->kprio = kprio;
		kn->r = *kr;
		TAILQ_INSERT_TAIL(&kprio->nexthops, kn, entry);
	}

	if (!(kr->flags & F_KERNEL)) {
		/* don't validate or redistribute eigrp route */
		kr->flags &= ~F_DOWN;
		return (kn);
	}

	if (kif_validate(kr->ifindex))
		kr->flags &= ~F_DOWN;
	else
		kr->flags |= F_DOWN;

	kr_redistribute(kp);
	return (kn);
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
		log_warnx("%s failed for interface %s", __func__, kif->k.ifname);
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

static struct kif *
kif_update(unsigned short ifindex, int flags, struct if_data *ifd,
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

static int
kif_validate(unsigned short ifindex)
{
	struct kif_node		*kif;

	if ((kif = kif_find(ifindex)) == NULL)
		return (0);

	return (kif->k.nh_reachable);
}

/* misc */
static void
protect_lo(void)
{
	struct kroute	 kr4, kr6;

	/* special protection for 127/8 */
	memset(&kr4, 0, sizeof(kr4));
	kr4.af = AF_INET;
	kr4.prefix.v4.s_addr = htonl(INADDR_LOOPBACK & IN_CLASSA_NET);
	kr4.prefixlen = 8;
	kr4.flags = F_KERNEL|F_CONNECTED;
	kroute_insert(&kr4);

	/* special protection for ::1 */
	memset(&kr6, 0, sizeof(kr6));
	kr6.af = AF_INET6;
	kr6.prefix.v6 = in6addr_loopback;
	kr6.prefixlen = 128;
	kr6.flags = F_KERNEL|F_CONNECTED;
	kroute_insert(&kr6);
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
	struct kroute_prefix	*kp;
	struct kroute_priority	*kprio;
	struct kroute_node	*kn;
	struct kif		*kif;
	uint8_t			 reachable;

	if ((kif = kif_update(ifindex, flags, ifd, sdl)) == NULL) {
		log_warn("%s: kif_update(%u)", __func__, ifindex);
		return;
	}

	reachable = (kif->flags & IFF_UP) &&
	    LINK_STATE_IS_UP(kif->link_state);

	if (reachable == kif->nh_reachable)
		return;		/* nothing changed wrt nexthop validity */

	kif->nh_reachable = reachable;

	/* notify eigrpe about link state */
	main_imsg_compose_eigrpe(IMSG_IFINFO, 0, kif, sizeof(struct kif));

	/* notify rde about link going down */
	if (!kif->nh_reachable)
		main_imsg_compose_rde(IMSG_IFDOWN, 0, kif, sizeof(struct kif));

	/* update redistribute list */
	RB_FOREACH(kp, kroute_tree, &krt) {
		TAILQ_FOREACH(kprio, &kp->priorities, entry) {
			TAILQ_FOREACH(kn, &kprio->nexthops, entry) {
				if (kn->r.ifindex != ifindex)
					continue;

				if (reachable)
					kn->r.flags &= ~F_DOWN;
				else
					kn->r.flags |= F_DOWN;
			}
		}
		kr_redistribute(kp);
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

	/* notify eigrpe about new address */
	main_imsg_compose_eigrpe(IMSG_NEWADDR, 0, &ka->a, sizeof(ka->a));
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
		    eigrp_addrcmp(ka->a.af, &ka->a.addr, &k.addr) ||
		    eigrp_addrcmp(ka->a.af, &ka->a.dstbrd, &k.dstbrd))
			continue;

		/* notify eigrpe about removed address */
		main_imsg_compose_eigrpe(IMSG_DELADDR, 0, &ka->a,
		    sizeof(ka->a));
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
send_rtmsg_v4(int fd, int action, struct kroute *kr)
{
	struct iovec		iov[5];
	struct rt_msghdr	hdr;
	struct sockaddr_in	prefix;
	struct sockaddr_in	nexthop;
	struct sockaddr_in	mask;
	int			iovcnt = 0;

	if (kr_state.fib_sync == 0)
		return (0);

	/* initialize header */
	memset(&hdr, 0, sizeof(hdr));
	hdr.rtm_version = RTM_VERSION;
	hdr.rtm_type = action;
	hdr.rtm_priority = kr->priority;
	hdr.rtm_tableid = kr_state.rdomain;	/* rtableid */
	if (action == RTM_CHANGE)
		hdr.rtm_fmask = RTF_REJECT|RTF_BLACKHOLE;
	else
		hdr.rtm_flags = RTF_MPATH;
	if (kr->flags & F_BLACKHOLE)
		hdr.rtm_flags |= RTF_BLACKHOLE;
	hdr.rtm_seq = kr_state.rtseq++;	/* overflow doesn't matter */
	hdr.rtm_msglen = sizeof(hdr);
	/* adjust iovec */
	iov[iovcnt].iov_base = &hdr;
	iov[iovcnt++].iov_len = sizeof(hdr);

	memset(&prefix, 0, sizeof(prefix));
	prefix.sin_len = sizeof(prefix);
	prefix.sin_family = AF_INET;
	prefix.sin_addr = kr->prefix.v4;
	/* adjust header */
	hdr.rtm_addrs |= RTA_DST;
	hdr.rtm_msglen += sizeof(prefix);
	/* adjust iovec */
	iov[iovcnt].iov_base = &prefix;
	iov[iovcnt++].iov_len = sizeof(prefix);

	if (kr->nexthop.v4.s_addr != 0) {
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
	}

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

retry:
	if (writev(fd, iov, iovcnt) == -1) {
		if (errno == ESRCH) {
			if (hdr.rtm_type == RTM_CHANGE) {
				hdr.rtm_type = RTM_ADD;
				goto retry;
			} else if (hdr.rtm_type == RTM_DELETE) {
				log_info("route %s/%u vanished before delete",
				    inet_ntoa(kr->prefix.v4),
				    kr->prefixlen);
				return (0);
			}
		}
		log_warn("%s: action %u, prefix %s/%u", __func__, hdr.rtm_type,
		    inet_ntoa(kr->prefix.v4), kr->prefixlen);
		return (0);
	}

	return (0);
}

static int
send_rtmsg_v6(int fd, int action, struct kroute *kr)
{
	struct iovec		iov[5];
	struct rt_msghdr	hdr;
	struct pad {
		struct sockaddr_in6	addr;
		char			pad[sizeof(long)]; /* thank you IPv6 */
	} prefix, nexthop, mask;
	int			iovcnt = 0;

	if (kr_state.fib_sync == 0)
		return (0);

	/* initialize header */
	memset(&hdr, 0, sizeof(hdr));
	hdr.rtm_version = RTM_VERSION;
	hdr.rtm_type = action;
	hdr.rtm_priority = kr->priority;
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

	memset(&prefix, 0, sizeof(prefix));
	prefix.addr.sin6_len = sizeof(struct sockaddr_in6);
	prefix.addr.sin6_family = AF_INET6;
	prefix.addr.sin6_addr = kr->prefix.v6;
	/* adjust header */
	hdr.rtm_addrs |= RTA_DST;
	hdr.rtm_msglen += ROUNDUP(sizeof(struct sockaddr_in6));
	/* adjust iovec */
	iov[iovcnt].iov_base = &prefix;
	iov[iovcnt++].iov_len = ROUNDUP(sizeof(struct sockaddr_in6));

	if (!IN6_IS_ADDR_UNSPECIFIED(&kr->nexthop.v6)) {
		memset(&nexthop, 0, sizeof(nexthop));
		nexthop.addr.sin6_len = sizeof(struct sockaddr_in6);
		nexthop.addr.sin6_family = AF_INET6;
		nexthop.addr.sin6_addr = kr->nexthop.v6;
		nexthop.addr.sin6_scope_id = kr->ifindex;
		embedscope(&nexthop.addr);

		/* adjust header */
		hdr.rtm_flags |= RTF_GATEWAY;
		hdr.rtm_addrs |= RTA_GATEWAY;
		hdr.rtm_msglen += ROUNDUP(sizeof(struct sockaddr_in6));
		/* adjust iovec */
		iov[iovcnt].iov_base = &nexthop;
		iov[iovcnt++].iov_len = ROUNDUP(sizeof(struct sockaddr_in6));
	}

	memset(&mask, 0, sizeof(mask));
	mask.addr.sin6_len = sizeof(struct sockaddr_in6);
	mask.addr.sin6_family = AF_INET6;
	mask.addr.sin6_addr = *prefixlen2mask6(kr->prefixlen);
	/* adjust header */
	if (kr->prefixlen == 128)
		hdr.rtm_flags |= RTF_HOST;
	hdr.rtm_addrs |= RTA_NETMASK;
	hdr.rtm_msglen += ROUNDUP(sizeof(struct sockaddr_in6));
	/* adjust iovec */
	iov[iovcnt].iov_base = &mask;
	iov[iovcnt++].iov_len = ROUNDUP(sizeof(struct sockaddr_in6));

retry:
	if (writev(fd, iov, iovcnt) == -1) {
		if (errno == ESRCH) {
			if (hdr.rtm_type == RTM_CHANGE) {
				hdr.rtm_type = RTM_ADD;
				goto retry;
			} else if (hdr.rtm_type == RTM_DELETE) {
				log_info("route %s/%u vanished before delete",
				    log_in6addr(&kr->prefix.v6),
				    kr->prefixlen);
				return (0);
			}
		}
		log_warn("%s: action %u, prefix %s/%u", __func__, hdr.rtm_type,
		    log_in6addr(&kr->prefix.v6), kr->prefixlen);
		return (0);
	}

	return (0);
}

static int
send_rtmsg(int fd, int action, struct kroute *kr)
{
	switch (kr->af) {
	case AF_INET:
		return (send_rtmsg_v4(fd, action, kr));
	case AF_INET6:
		return (send_rtmsg_v6(fd, action, kr));
	default:
		break;
	}

	return (-1);
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
		log_warn("%s", __func__);
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
		log_warn("%s", __func__);
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
	kr.flags |= F_KERNEL;
	if (rtm->rtm_flags & RTF_STATIC)
		kr.flags |= F_STATIC;
	if (rtm->rtm_flags & RTF_BLACKHOLE)
		kr.flags |= F_BLACKHOLE;
	if (rtm->rtm_flags & RTF_REJECT)
		kr.flags |= F_REJECT;
	if (rtm->rtm_flags & RTF_DYNAMIC)
		kr.flags |= F_DYNAMIC;
	if (rtm->rtm_flags & RTF_CONNECTED)
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
		if (kn == NULL || !(kn->r.flags & F_KERNEL))
			return (0);
		return (kroute_remove(&kr));
	}

	if (!eigrp_addrisset(kr.af, &kr.nexthop) && !(kr.flags & F_CONNECTED)) {
		log_warnx("%s: no nexthop for %s/%u", __func__,
		    log_addr(kr.af, &kr.prefix), kr.prefixlen);
		return (-1);
	}

	if (kn != NULL) {
		/* update route */
		kn->r = kr;

		if (kif_validate(kn->r.ifindex))
			kn->r.flags &= ~F_DOWN;
		else
			kn->r.flags |= F_DOWN;

		kr_redistribute(kp);
	} else {
		if ((rtm->rtm_type == RTM_ADD || rtm->rtm_type == RTM_GET) &&
		    (kr.priority == eigrpd_conf->fib_priority_internal ||
		    kr.priority == eigrpd_conf->fib_priority_external ||
		    kr.priority == eigrpd_conf->fib_priority_summary)) {
			log_warnx("alien EIGRP route %s/%d", log_addr(kr.af,
			    &kr.prefix), kr.prefixlen);
			return (send_rtmsg(kr_state.fd, RTM_DELETE, &kr));
		}

		kroute_insert(&kr);
	}

	return (0);
}
