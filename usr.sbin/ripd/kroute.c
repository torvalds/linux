/*	$OpenBSD: kroute.c,v 1.37 2025/01/02 06:35:57 anton Exp $ */

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

#include "rip.h"
#include "ripd.h"
#include "log.h"

struct {
	u_int32_t		rtseq;
	pid_t			pid;
	int			fib_sync;
	u_int8_t		fib_prio;
	int			fd;
	struct event		ev;
	u_int			rdomain;
} kr_state;

struct kroute_node {
	RB_ENTRY(kroute_node)	 entry;
	struct kroute		 r;
};

struct kif_node {
	RB_ENTRY(kif_node)	 entry;
	struct kif		 k;
};

void	kr_redistribute(int, struct kroute *);
int	kroute_compare(struct kroute_node *, struct kroute_node *);
int	kif_compare(struct kif_node *, struct kif_node *);
int	kr_change_fib(struct kroute_node *, struct kroute *, int);

struct kroute_node	*kroute_find(in_addr_t, in_addr_t, u_int8_t);
int			 kroute_insert(struct kroute_node *);
int			 kroute_remove(struct kroute_node *);
void			 kroute_clear(void);

struct kif_node		*kif_find(int);
int			 kif_insert(struct kif_node *);
int			 kif_remove(struct kif_node *);
void			 kif_clear(void);
int			 kif_validate(int);

struct kroute_node	*kroute_match(in_addr_t);

int		protect_lo(void);
u_int8_t	prefixlen_classful(in_addr_t);
void		get_rtaddrs(int, struct sockaddr *, struct sockaddr **);
void		if_change(u_short, int, struct if_data *);
void		if_announce(void *);

int		send_rtmsg(int, int, struct kroute *);
int		dispatch_rtmsg(void);
int		fetchtable(void);
int		fetchifs(int);

RB_HEAD(kroute_tree, kroute_node)	krt;
RB_PROTOTYPE(kroute_tree, kroute_node, entry, kroute_compare)
RB_GENERATE(kroute_tree, kroute_node, entry, kroute_compare)

RB_HEAD(kif_tree, kif_node)		kit;
RB_PROTOTYPE(kif_tree, kif_node, entry, kif_compare)
RB_GENERATE(kif_tree, kif_node, entry, kif_compare)

int
kif_init(void)
{
	RB_INIT(&kit);

	if (fetchifs(0) == -1)
		return (-1);

	return (0);
}

int
kr_init(int fs, u_int rdomain, u_int8_t fib_prio)
{
	int		opt = 0, rcvbuf, default_rcvbuf;
	socklen_t	optlen;

	if ((kr_state.fd = socket(AF_ROUTE,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) == -1) {
		log_warn("kr_init: socket");
		return (-1);
	}

	/* not interested in my own messages */
	if (setsockopt(kr_state.fd, SOL_SOCKET, SO_USELOOPBACK,
	    &opt, sizeof(opt)) == -1)
		log_warn("kr_init: setsockopt");	/* not fatal */

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
	kr_state.fib_prio = fib_prio;

	RB_INIT(&krt);

	if (fetchtable() == -1)
		return (-1);

	if (protect_lo() == -1)
		return (-1);

	kr_state.fib_sync = fs; /* now set correct sync mode */
	kr_state.rdomain = rdomain;

	event_set(&kr_state.ev, kr_state.fd, EV_READ | EV_PERSIST,
	    kr_dispatch_msg, NULL);
	event_add(&kr_state.ev, NULL);

	return (0);
}

int
kr_change_fib(struct kroute_node *kr, struct kroute *kroute, int action)
{
	/* nexthop within 127/8 -> ignore silently */
	if ((kroute->nexthop.s_addr & htonl(IN_CLASSA_NET)) ==
	    htonl(INADDR_LOOPBACK & IN_CLASSA_NET))
		return (0);

	if (send_rtmsg(kr_state.fd, action, kroute) == -1)
		return (-1);

	if (action == RTM_ADD) {
		if ((kr = calloc(1, sizeof(struct kroute_node))) == NULL)
			fatal("kr_change_fib");

		kr->r.prefix.s_addr = kroute->prefix.s_addr;
		kr->r.netmask.s_addr = kroute->netmask.s_addr;
		kr->r.nexthop.s_addr = kroute->nexthop.s_addr;
		kr->r.flags = kroute->flags |= F_RIPD_INSERTED;
		kr->r.priority = kr_state.fib_prio;

		if (kroute_insert(kr) == -1) {
			log_debug("kr_update_fib: cannot insert %s",
			    inet_ntoa(kroute->nexthop));
		}
	} else
		kr->r.nexthop.s_addr = kroute->nexthop.s_addr;

	return (0);
}

int
kr_change(struct kroute *kroute)
{
	struct kroute_node	*kr;
	int			 action = RTM_ADD;

	kr = kroute_find(kroute->prefix.s_addr, kroute->netmask.s_addr,
	    kr_state.fib_prio);
	if (kr != NULL)
		action = RTM_CHANGE;

	return (kr_change_fib(kr, kroute, action));
}

int
kr_delete(struct kroute *kroute)
{
	struct kroute_node	*kr;

	kr = kroute_find(kroute->prefix.s_addr, kroute->netmask.s_addr,
	    kr_state.fib_prio);
	if (kr == NULL)
		return (0);

	if (kr->r.priority != kr_state.fib_prio)
		log_warn("kr_delete_fib: %s/%d has wrong priority %d",
		    inet_ntoa(kr->r.prefix), mask2prefixlen(kr->r.netmask.s_addr),
		    kr->r.priority);

	if (send_rtmsg(kr_state.fd, RTM_DELETE, kroute) == -1)
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
	struct kroute_node	*kr;

	if (kr_state.fib_sync == 1)	/* already coupled */
		return;

	kr_state.fib_sync = 1;

	RB_FOREACH(kr, kroute_tree, &krt)
		if (kr->r.priority == kr_state.fib_prio)
			send_rtmsg(kr_state.fd, RTM_ADD, &kr->r);

	log_info("kernel routing table coupled");
}

void
kr_fib_decouple(void)
{
	struct kroute_node	*kr;

	if (kr_state.fib_sync == 0)	/* already decoupled */
		return;

	RB_FOREACH(kr, kroute_tree, &krt)
		if (kr->r.priority == kr_state.fib_prio)
			send_rtmsg(kr_state.fd, RTM_DELETE, &kr->r);

	kr_state.fib_sync = 0;

	log_info("kernel routing table decoupled");
}

void
kr_dispatch_msg(int fd, short event, void *bula)
{
	dispatch_rtmsg();
}

void
kr_show_route(struct imsg *imsg)
{
	struct kroute_node	*kr;
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
				main_imsg_compose_ripe(IMSG_CTL_KROUTE,
				    imsg->hdr.pid, &kr->r, sizeof(kr->r));
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
			main_imsg_compose_ripe(IMSG_CTL_KROUTE, imsg->hdr.pid,
			    &kr->r, sizeof(kr->r));
		break;
	default:
		log_debug("kr_show_route: error handling imsg");
		break;
	}

	main_imsg_compose_ripe(IMSG_CTL_END, imsg->hdr.pid, NULL, 0);
}

void
kr_ifinfo(char *ifname, pid_t pid)
{
	struct kif_node	*kif;

	RB_FOREACH(kif, kif_tree, &kit)
		if (ifname == NULL || !strcmp(ifname, kif->k.ifname)) {
			main_imsg_compose_ripe(IMSG_CTL_IFINFO,
			    pid, &kif->k, sizeof(kif->k));
		}

	main_imsg_compose_ripe(IMSG_CTL_END, pid, NULL, 0);
}

void
kr_redistribute(int type, struct kroute *kr)
{
	u_int32_t	a;


	if (type == IMSG_NETWORK_DEL) {
dont_redistribute:
		/* was the route redistributed? */
		if (kr->flags & F_REDISTRIBUTED) {
			/* remove redistributed flag */
			kr->flags &= ~F_REDISTRIBUTED;
			main_imsg_compose_rde(type, 0, kr,
			    sizeof(struct kroute));
		}
		return;
	}

	/* interface is not up and running so don't announce */
	if (kr->flags & F_DOWN)
		return;

	/*
	 * We consider the loopback net and multicast addresses
	 * as not redistributable.
	 */
	a = ntohl(kr->prefix.s_addr);
	if (IN_MULTICAST(a) || (a >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET)
		return;
	/*
	 * Consider networks with nexthop loopback as not redistributable
	 * unless it is a reject or blackhole route.
	 */
	if (kr->nexthop.s_addr == htonl(INADDR_LOOPBACK) &&
	    !(kr->flags & (F_BLACKHOLE|F_REJECT)))
		return;

	/* Should we redistribute this route? */
	if (!rip_redistribute(kr))
		goto dont_redistribute;

	/* Does not matter if we resend the kr, the RDE will cope. */
	kr->flags |= F_REDISTRIBUTED;
	main_imsg_compose_rde(type, 0, kr, sizeof(struct kroute));
}

/* rb-tree compare */
int
kroute_compare(struct kroute_node *a, struct kroute_node *b)
{
	if (ntohl(a->r.prefix.s_addr) < ntohl(b->r.prefix.s_addr))
		return (-1);
	if (ntohl(a->r.prefix.s_addr) > ntohl(b->r.prefix.s_addr))
		return (1);
	if (ntohl(a->r.netmask.s_addr) < ntohl(b->r.netmask.s_addr))
		return (-1);
	if (ntohl(a->r.netmask.s_addr) > ntohl(b->r.netmask.s_addr))
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
kroute_find(in_addr_t prefix, in_addr_t netmask, u_int8_t prio)
{
	struct kroute_node	s, *kn, *tmp;

	s.r.prefix.s_addr = prefix;
	s.r.netmask.s_addr = netmask;
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

int
kroute_insert(struct kroute_node *kr)
{
	if (RB_INSERT(kroute_tree, &krt, kr) != NULL) {
		log_warnx("kroute_insert failed for %s/%u",
		    inet_ntoa(kr->r.prefix),
		    mask2prefixlen(kr->r.netmask.s_addr));
		free(kr);
		return (-1);
	}

	if (!(kr->r.flags & F_KERNEL)) {
		/* don't validate or redistribute rip route */
		kr->r.flags &= ~F_DOWN;
		return (0);
	}

	if (kif_validate(kr->r.ifindex))
		kr->r.flags &= ~F_DOWN;
	else
		kr->r.flags |= F_DOWN;

	kr_redistribute(IMSG_NETWORK_ADD, &kr->r);

	return (0);
}

int
kroute_remove(struct kroute_node *kr)
{
	if (RB_REMOVE(kroute_tree, &krt, kr) == NULL) {
		log_warnx("kroute_remove failed for %s/%u",
		    inet_ntoa(kr->r.prefix),
		    mask2prefixlen(kr->r.netmask.s_addr));
		return (-1);
	}

	kr_redistribute(IMSG_NETWORK_DEL, &kr->r);
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
kif_find(int ifindex)
{
	struct kif_node	s;

	bzero(&s, sizeof(s));
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

int
kif_insert(struct kif_node *kif)
{
	if (RB_INSERT(kif_tree, &kit, kif) != NULL) {
		log_warnx("RB_INSERT(kif_tree, &kit, kif)");
		free(kif);
		return (-1);
	}

	return (0);
}

int
kif_remove(struct kif_node *kif)
{
	if (RB_REMOVE(kif_tree, &kit, kif) == NULL) {
		log_warnx("RB_REMOVE(kif_tree, &kit, kif)");
		return (-1);
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

int
kif_validate(int ifindex)
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
	u_int8_t		 i;
	struct kroute_node	*kr;

	/* we will never match the default route */
	for (i = 32; i > 0; i--)
		if ((kr = kroute_find(key & prefixlen2mask(i),
		    prefixlen2mask(i), RTP_ANY)) != NULL)
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
	kr->r.prefix.s_addr = htonl(INADDR_LOOPBACK);
	kr->r.netmask.s_addr = htonl(IN_CLASSA_NET);
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
if_change(u_short ifindex, int flags, struct if_data *ifd)
{
	struct kif_node		*kif;
	struct kroute_node	*kr;
	int			 type;
	u_int8_t		 reachable;

	if ((kif = kif_find(ifindex)) == NULL) {
		log_warnx("interface with index %u not found", ifindex);
		return;
	}

	kif->k.flags = flags;
	kif->k.link_state = ifd->ifi_link_state;
	kif->k.if_type = ifd->ifi_type;
	kif->k.baudrate = ifd->ifi_baudrate;

	if ((reachable = (flags & IFF_UP) &&
	    LINK_STATE_IS_UP(ifd->ifi_link_state)) == kif->k.nh_reachable)
		return;		/* nothing changed wrt nexthop validity */

	kif->k.nh_reachable = reachable;
	type = reachable ? IMSG_NETWORK_ADD : IMSG_NETWORK_DEL;

	/* notify ripe about interface link state */
	main_imsg_compose_ripe(IMSG_IFINFO, 0, &kif->k, sizeof(kif->k));

	/* update redistribute list */
	RB_FOREACH(kr, kroute_tree, &krt)
		if (kr->r.ifindex == ifindex) {
			if (reachable)
				kr->r.flags &= ~F_DOWN;
			else
				kr->r.flags |= F_DOWN;

			kr_redistribute(type, &kr->r);
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
		if ((kif = calloc(1, sizeof(struct kif_node))) == NULL) {
			log_warn("if_announce");
			return;
		}

		kif->k.ifindex = ifan->ifan_index;
		strlcpy(kif->k.ifname, ifan->ifan_name, sizeof(kif->k.ifname));
		kif_insert(kif);
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
	struct iovec		iov[4];
	struct rt_msghdr	hdr;
	struct sockaddr_in	prefix;
	struct sockaddr_in	nexthop;
	struct sockaddr_in	mask;
	int			iovcnt = 0;

	if (kr_state.fib_sync == 0)
		return (0);

	/* initialize header */
	bzero(&hdr, sizeof(hdr));
	hdr.rtm_version = RTM_VERSION;
	hdr.rtm_type = action;
	hdr.rtm_priority = kr_state.fib_prio;
	hdr.rtm_tableid = kr_state.rdomain;
	if (action == RTM_CHANGE)
		hdr.rtm_fmask = RTF_REJECT|RTF_BLACKHOLE;
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
	mask.sin_addr.s_addr = kroute->netmask.s_addr;
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
				    inet_ntoa(kroute->prefix),
				    mask2prefixlen(kroute->netmask.s_addr));
				return (0);
			}
		}
		log_warn("send_rtmsg: action %u, prefix %s/%u",
		    hdr.rtm_type, inet_ntoa(kroute->prefix),
		    mask2prefixlen(kroute->netmask.s_addr));
		return (0);
	}

	return (0);
}

int
fetchtable(void)
{
	size_t			 len;
	int			 mib[7];
	char			*buf, *next, *lim;
	struct rt_msghdr	*rtm;
	struct sockaddr		*sa, *rti_info[RTAX_MAX];
	struct sockaddr_in	*sa_in;
	struct sockaddr_rtlabel	*label;
	struct kroute_node	*kr;
	struct iface		*iface = NULL;

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

	lim = buf + len;
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		if (rtm->rtm_version != RTM_VERSION)
			continue;
		sa = (struct sockaddr *)(next + rtm->rtm_hdrlen);
		get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

		if ((sa = rti_info[RTAX_DST]) == NULL)
			continue;

		/* Skip ARP/ND cache and broadcast routes. */
		if (rtm->rtm_flags & (RTF_LLINFO|RTF_BROADCAST))
			continue;

#ifdef RTF_MPATH
		if (rtm->rtm_flags & RTF_MPATH)		/* multipath */
			continue;
#endif

		if ((kr = calloc(1, sizeof(struct kroute_node))) == NULL) {
			log_warn("fetchtable");
			free(buf);
			return (-1);
		}

		kr->r.flags = F_KERNEL;
		kr->r.priority = rtm->rtm_priority;

		switch (sa->sa_family) {
		case AF_INET:
			kr->r.prefix.s_addr =
			    ((struct sockaddr_in *)sa)->sin_addr.s_addr;
			sa_in = (struct sockaddr_in *)rti_info[RTAX_NETMASK];
			if (rtm->rtm_flags & RTF_STATIC)
				kr->r.flags |= F_STATIC;
			if (rtm->rtm_flags & RTF_BLACKHOLE)
				kr->r.flags |= F_BLACKHOLE;
			if (rtm->rtm_flags & RTF_REJECT)
				kr->r.flags |= F_REJECT;
			if (rtm->rtm_flags & RTF_DYNAMIC)
				kr->r.flags |= F_DYNAMIC;
			if (sa_in != NULL) {
				if (sa_in->sin_len == 0)
					break;
				kr->r.netmask.s_addr =
				    sa_in->sin_addr.s_addr;
			} else if (rtm->rtm_flags & RTF_HOST)
				kr->r.netmask.s_addr = prefixlen2mask(32);
			else
				kr->r.netmask.s_addr =
				    prefixlen2mask(prefixlen_classful
					(kr->r.prefix.s_addr));
			break;
		default:
			free(kr);
			continue;
		}

		kr->r.ifindex = rtm->rtm_index;

		iface = if_find_index(rtm->rtm_index);
		if (iface != NULL)
			kr->r.metric = iface->cost;
		else
			kr->r.metric = DEFAULT_COST;

		if ((sa = rti_info[RTAX_GATEWAY]) != NULL)
			switch (sa->sa_family) {
			case AF_INET:
				if (rtm->rtm_flags & RTF_CONNECTED) {
					kr->r.flags |= F_CONNECTED;
					break;
				}

				kr->r.nexthop.s_addr =
				    ((struct sockaddr_in *)sa)->sin_addr.s_addr;
				break;
			case AF_LINK:
				/*
				 * Traditional BSD connected routes have
				 * a gateway of type AF_LINK.
				 */
				kr->r.flags |= F_CONNECTED;
				break;
			}

		if (rtm->rtm_priority == kr_state.fib_prio) {
			send_rtmsg(kr_state.fd, RTM_DELETE, &kr->r);
			free(kr);
		} else {
			if ((label = (struct sockaddr_rtlabel *)
			    rti_info[RTAX_LABEL]) != NULL)
				kr->r.rtlabel =
				    rtlabel_name2id(label->sr_label);
			kroute_insert(kr);
		}

	}
	free(buf);
	return (0);
}

int
fetchifs(int ifindex)
{
	size_t			 len;
	int			 mib[6];
	char			*buf, *next, *lim;
	struct if_msghdr	 ifm;
	struct kif_node		*kif;
	struct sockaddr		*sa, *rti_info[RTAX_MAX];
	struct sockaddr_dl	*sdl;

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

	lim = buf + len;
	for (next = buf; next < lim; next += ifm.ifm_msglen) {
		memcpy(&ifm, next, sizeof(ifm));
		if (ifm.ifm_version != RTM_VERSION)
			continue;
		if (ifm.ifm_type != RTM_IFINFO)
			continue;

		sa = (struct sockaddr *)(next + sizeof(ifm));
		get_rtaddrs(ifm.ifm_addrs, sa, rti_info);

		if ((kif = calloc(1, sizeof(struct kif_node))) == NULL) {
			log_warn("fetchifs");
			free(buf);
			return (-1);
		}

		kif->k.ifindex = ifm.ifm_index;
		kif->k.flags = ifm.ifm_flags;
		kif->k.link_state = ifm.ifm_data.ifi_link_state;
		kif->k.if_type = ifm.ifm_data.ifi_type;
		kif->k.baudrate = ifm.ifm_data.ifi_baudrate;
		kif->k.mtu = ifm.ifm_data.ifi_mtu;
		kif->k.nh_reachable = (kif->k.flags & IFF_UP) &&
		    LINK_STATE_IS_UP(ifm.ifm_data.ifi_link_state);
		if ((sa = rti_info[RTAX_IFP]) != NULL)
			if (sa->sa_family == AF_LINK) {
				sdl = (struct sockaddr_dl *)sa;
				if (sdl->sdl_nlen >= sizeof(kif->k.ifname))
					memcpy(kif->k.ifname, sdl->sdl_data,
					    sizeof(kif->k.ifname) - 1);
				else if (sdl->sdl_nlen > 0)
					memcpy(kif->k.ifname, sdl->sdl_data,
					    sdl->sdl_nlen);
				/* string already terminated via calloc() */
			}

		kif_insert(kif);
	}
	free(buf);
	return (0);
}

int
dispatch_rtmsg(void)
{
	char			 buf[RT_BUF_SIZE];
	ssize_t			 n;
	char			*next, *lim;
	struct rt_msghdr	*rtm;
	struct if_msghdr	 ifm;
	struct sockaddr		*sa, *rti_info[RTAX_MAX];
	struct sockaddr_in	*sa_in;
	struct sockaddr_rtlabel	*label;
	struct kroute_node	*kr;
	struct in_addr		 prefix, nexthop, netmask;
	struct iface		*iface = NULL;
	int			 flags;
	u_short			 ifindex = 0;
	u_int8_t		 metric, prio;

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

	lim = buf + n;
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		if (lim < next + sizeof(u_short) ||
		    lim < next + rtm->rtm_msglen)
			fatalx("dispatch_rtmsg: partial rtm in buffer");
		if (rtm->rtm_version != RTM_VERSION)
			continue;

		prefix.s_addr = 0;
		netmask.s_addr = 0;
		flags = F_KERNEL;
		nexthop.s_addr = 0;
		prio = 0;

		if (rtm->rtm_type == RTM_ADD || rtm->rtm_type == RTM_CHANGE ||
		    rtm->rtm_type == RTM_DELETE) {
			sa = (struct sockaddr *)(next + rtm->rtm_hdrlen);
			get_rtaddrs(rtm->rtm_addrs, sa, rti_info);

			if (rtm->rtm_tableid != kr_state.rdomain)
				continue;

			if (rtm->rtm_pid == kr_state.pid)	/* cause by us */
				continue;

			if (rtm->rtm_errno)			/* failed attempts... */
				continue;

			/* Skip ARP/ND cache and broadcast routes. */
			if (rtm->rtm_flags & (RTF_LLINFO|RTF_BROADCAST))
				continue;

			prio = rtm->rtm_priority;

			switch (sa->sa_family) {
			case AF_INET:
				prefix.s_addr =
				    ((struct sockaddr_in *)sa)->sin_addr.s_addr;
				sa_in = (struct sockaddr_in *)
				    rti_info[RTAX_NETMASK];
				if (sa_in != NULL) {
					if (sa_in->sin_len != 0)
						netmask.s_addr =
						    sa_in->sin_addr.s_addr;
				} else if (rtm->rtm_flags & RTF_HOST)
					netmask.s_addr = prefixlen2mask(32);
				else
					netmask.s_addr =
					    prefixlen2mask(prefixlen_classful(
						prefix.s_addr));
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
					nexthop.s_addr = ((struct
					    sockaddr_in *)sa)->sin_addr.s_addr;
					break;
				case AF_LINK:
					flags |= F_CONNECTED;
					break;
				}
			}
		}

		switch (rtm->rtm_type) {
		case RTM_ADD:
		case RTM_CHANGE:
			if (nexthop.s_addr == 0 && !(flags & F_CONNECTED)) {
				log_warnx("dispatch_rtmsg no nexthop for %s/%u",
				    inet_ntoa(prefix),
				    mask2prefixlen(netmask.s_addr));
				continue;
			}

			if ((kr = kroute_find(prefix.s_addr, netmask.s_addr,
			    prio)) != NULL) {
				if (kr->r.flags & F_REDISTRIBUTED)
					flags |= F_REDISTRIBUTED;
				kr->r.nexthop.s_addr = nexthop.s_addr;
				kr->r.flags = flags;
				kr->r.ifindex = ifindex;
				kr->r.priority = prio;

				rtlabel_unref(kr->r.rtlabel);
				kr->r.rtlabel = 0;
				if ((label = (struct sockaddr_rtlabel *)
				    rti_info[RTAX_LABEL]) != NULL)
					kr->r.rtlabel =
					    rtlabel_name2id(label->sr_label);

				if (kif_validate(kr->r.ifindex))
					kr->r.flags &= ~F_DOWN;
				else
					kr->r.flags |= F_DOWN;

				/* just readd, the RDE will care */
				kr_redistribute(IMSG_NETWORK_ADD, &kr->r);
			} else {
				if ((kr = calloc(1,
				    sizeof(struct kroute_node))) == NULL) {
					log_warn("dispatch_rtmsg");
					return (-1);
				}

				iface = if_find_index(rtm->rtm_index);
				if (iface != NULL)
					metric = iface->cost;
				else
					metric = DEFAULT_COST;

				kr->r.prefix.s_addr = prefix.s_addr;
				kr->r.netmask.s_addr = netmask.s_addr;
				kr->r.nexthop.s_addr = nexthop.s_addr;
				kr->r.metric = metric;
				kr->r.flags = flags;
				kr->r.ifindex = ifindex;

				if ((label = (struct sockaddr_rtlabel *)
				    rti_info[RTAX_LABEL]) != NULL)
					kr->r.rtlabel =
					    rtlabel_name2id(label->sr_label);

				kroute_insert(kr);
			}
			break;
		case RTM_DELETE:
			if ((kr = kroute_find(prefix.s_addr, netmask.s_addr,
			    prio)) == NULL)
				continue;
			if (!(kr->r.flags & F_KERNEL))
				continue;
			if (kroute_remove(kr) == -1)
				return (-1);
			break;
		case RTM_IFINFO:
			memcpy(&ifm, next, sizeof(ifm));
			if_change(ifm.ifm_index, ifm.ifm_flags,
			    &ifm.ifm_data);
			break;
		case RTM_IFANNOUNCE:
			if_announce(next);
			break;
		default:
			/* ignore for now */
			break;
		}
	}
	return (0);
}
