/*	$OpenBSD: neighbor.c,v 1.11 2023/03/08 04:43:13 guenther Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#include <string.h>
#include <stdlib.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "rde.h"
#include "log.h"

static __inline int	 nbr_compare(struct nbr *, struct nbr *);
static __inline int	 nbr_pid_compare(struct nbr *, struct nbr *);
static void		 nbr_update_peerid(struct nbr *);
static void		 nbr_timeout(int, short, void *);
static void		 nbr_stop_timeout(struct nbr *);

RB_GENERATE(nbr_addr_head, nbr, addr_tree, nbr_compare)
RB_GENERATE(nbr_pid_head, nbr, pid_tree, nbr_pid_compare)

struct nbr_pid_head nbrs_by_pid = RB_INITIALIZER(&nbrs_by_pid);

static __inline int
nbr_compare(struct nbr *a, struct nbr *b)
{
	if (a->ei->iface->ifindex < b->ei->iface->ifindex)
		return (-1);
	if (a->ei->iface->ifindex > b->ei->iface->ifindex)
		return (1);

	return (eigrp_addrcmp(a->ei->eigrp->af, &a->addr, &b->addr));
}

static __inline int
nbr_pid_compare(struct nbr *a, struct nbr *b)
{
	return (a->peerid - b->peerid);
}

struct nbr *
nbr_new(struct eigrp_iface *ei, union eigrpd_addr *addr, uint16_t holdtime,
    int self)
{
	struct eigrp		*eigrp = ei->eigrp;
	struct nbr		*nbr;

	if (!self)
		log_debug("%s: interface %s addr %s as %u", __func__,
		    ei->iface->name, log_addr(eigrp->af, addr), eigrp->as);

	if ((nbr = calloc(1, sizeof(*nbr))) == NULL)
		fatal("nbr_new");

	nbr->ei = ei;
	TAILQ_INSERT_TAIL(&ei->nbr_list, nbr, entry);
	nbr->addr = *addr;
	nbr->peerid = 0;
	nbr->hello_holdtime = holdtime;
	nbr->flags = F_EIGRP_NBR_PENDING;
	if (self)
		nbr->flags |= F_EIGRP_NBR_SELF;
	TAILQ_INIT(&nbr->update_list);
	TAILQ_INIT(&nbr->query_list);
	TAILQ_INIT(&nbr->reply_list);
	TAILQ_INIT(&nbr->retrans_list);

	if (RB_INSERT(nbr_addr_head, &eigrp->nbrs, nbr) != NULL)
		fatalx("nbr_new: RB_INSERT(eigrp->nbrs) failed");

	/* timeout handling */
	if (!self) {
		evtimer_set(&nbr->ev_ack, rtp_ack_timer, nbr);
		evtimer_set(&nbr->ev_hello_timeout, nbr_timeout, nbr);
		nbr_start_timeout(nbr);
	}

	return (nbr);
}

void
nbr_init(struct nbr *nbr)
{
	struct timeval		 now;
	struct rde_nbr		 rnbr;

	nbr->flags &= ~F_EIGRP_NBR_PENDING;

	gettimeofday(&now, NULL);
	nbr->uptime = now.tv_sec;

	nbr_update_peerid(nbr);

	memset(&rnbr, 0, sizeof(rnbr));
	rnbr.addr = nbr->addr;
	rnbr.ifaceid = nbr->ei->ifaceid;
	if (nbr->flags & F_EIGRP_NBR_SELF)
		rnbr.flags = F_RDE_NBR_SELF|F_RDE_NBR_LOCAL;

	/* rde is not aware of pending nbrs */
	eigrpe_imsg_compose_rde(IMSG_NEIGHBOR_UP, nbr->peerid, 0, &rnbr,
	    sizeof(rnbr));
}

void
nbr_del(struct nbr *nbr)
{
	struct eigrp		*eigrp = nbr->ei->eigrp;
	struct packet		*pkt;

	if (!(nbr->flags & F_EIGRP_NBR_SELF))
		log_debug("%s: addr %s", __func__,
		    log_addr(eigrp->af, &nbr->addr));

	eigrpe_imsg_compose_rde(IMSG_NEIGHBOR_DOWN, nbr->peerid, 0, NULL, 0);

	nbr_stop_timeout(nbr);

	/* clear retransmission list */
	while ((pkt = TAILQ_FIRST(&nbr->retrans_list)) != NULL)
		rtp_packet_del(pkt);

	if (nbr->peerid)
		RB_REMOVE(nbr_pid_head, &nbrs_by_pid, nbr);
	RB_REMOVE(nbr_addr_head, &eigrp->nbrs, nbr);
	TAILQ_REMOVE(&nbr->ei->nbr_list, nbr, entry);

	free(nbr);
}

static void
nbr_update_peerid(struct nbr *nbr)
{
	static uint32_t	 peercnt = NBR_CNTSTART;

	if (nbr->peerid)
		RB_REMOVE(nbr_pid_head, &nbrs_by_pid, nbr);

	/* get next unused peerid */
	while (nbr_find_peerid(++peercnt))
		;
	nbr->peerid = peercnt;

	if (RB_INSERT(nbr_pid_head, &nbrs_by_pid, nbr) != NULL)
		fatalx("nbr_new: RB_INSERT(nbrs_by_pid) failed");
}

struct nbr *
nbr_find(struct eigrp_iface *ei, union eigrpd_addr *addr)
{
	struct nbr		 n;
	struct eigrp_iface	 i;
	struct eigrp		 e;

	e.af = ei->eigrp->af;
	e.as = ei->eigrp->as;
	i.eigrp = &e;
	i.iface = ei->iface;
	n.ei = &i;
	n.addr = *addr;

	return (RB_FIND(nbr_addr_head, &ei->eigrp->nbrs, &n));
}

struct nbr *
nbr_find_peerid(uint32_t peerid)
{
	struct nbr	n;
	n.peerid = peerid;
	return (RB_FIND(nbr_pid_head, &nbrs_by_pid, &n));
}

struct ctl_nbr *
nbr_to_ctl(struct nbr *nbr)
{
	static struct ctl_nbr	 nctl;
	struct timeval		 now;

	nctl.af = nbr->ei->eigrp->af;
	nctl.as = nbr->ei->eigrp->as;
	memcpy(nctl.ifname, nbr->ei->iface->name, sizeof(nctl.ifname));
	nctl.addr = nbr->addr;
	nctl.hello_holdtime = nbr->hello_holdtime;
	gettimeofday(&now, NULL);
	nctl.uptime = now.tv_sec - nbr->uptime;

	return (&nctl);
}

void
nbr_clear_ctl(struct ctl_nbr *nctl)
{
	struct eigrp		*eigrp;
	struct nbr		*nbr, *safe;

	TAILQ_FOREACH(eigrp, &econf->instances, entry) {
		if (nctl->af && nctl->af != eigrp->af)
			continue;
		if (nctl->as && nctl->as != eigrp->as)
			continue;

		RB_FOREACH_SAFE(nbr, nbr_addr_head, &eigrp->nbrs, safe) {
			if (nbr->flags & (F_EIGRP_NBR_PENDING|F_EIGRP_NBR_SELF))
				continue;
			if (eigrp_addrisset(nctl->af, &nctl->addr) &&
			    eigrp_addrcmp(nctl->af, &nctl->addr, &nbr->addr))
				continue;

			log_debug("%s: neighbor %s manually cleared", __func__,
			    log_addr(nbr->ei->eigrp->af, &nbr->addr));
			send_peerterm(nbr);
			nbr_del(nbr);
		}
	}
}

/* timers */

static void
nbr_timeout(int fd, short event, void *arg)
{
	struct nbr	*nbr = arg;
	struct eigrp	*eigrp = nbr->ei->eigrp;

	log_debug("%s: neighbor %s", __func__, log_addr(eigrp->af, &nbr->addr));

	nbr_del(nbr);
}

void
nbr_start_timeout(struct nbr *nbr)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = nbr->hello_holdtime;

	if (evtimer_add(&nbr->ev_hello_timeout, &tv) == -1)
		fatal("nbr_start_timeout");
}

static void
nbr_stop_timeout(struct nbr *nbr)
{
	if (evtimer_pending(&nbr->ev_hello_timeout, NULL) &&
	    evtimer_del(&nbr->ev_hello_timeout) == -1)
		fatal("nbr_stop_timeout");
}
