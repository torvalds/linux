/*	$OpenBSD: rde_mfc.c,v 1.11 2024/05/18 11:17:30 jsg Exp $ */

/*
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2006 Esben Norby <norby@openbsd.org>
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
#include <sys/tree.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "igmp.h"
#include "dvmrp.h"
#include "dvmrpd.h"
#include "log.h"
#include "dvmrpe.h"
#include "rde.h"

/* multicast forwarding cache */

void			 mfc_send_prune(struct rt_node *, struct mfc_node *);
void			 mfc_add_prune(struct mfc_node *, struct prune *);
struct prune_node	*mfc_find_prune(struct mfc_node *, struct prune *);
void			 mfc_delete_prune(struct mfc_node *,
			    struct prune_node *);

int	prune_compare(struct mfc_node *, struct rt_node *, int);
void	prune_expire_timer(int, short, void *);
int	mfc_reset_prune_expire_timer(struct prune_node *);


void	 mfc_expire_timer(int, short, void *);
int	 mfc_start_expire_timer(struct mfc_node *);
int	 mfc_reset_expire_timer(struct mfc_node *);
void	 mfc_prune_timer(int, short, void *);
int	 mfc_start_prune_timer(struct mfc_node *);
int	 mfc_reset_prune_timer(struct mfc_node *);

int	 mfc_compare(struct mfc_node *, struct mfc_node *);

RB_HEAD(mfc_tree, mfc_node)	 mfc;
RB_PROTOTYPE(mfc_tree, mfc_node, entry, mfc_compare)
RB_GENERATE(mfc_tree, mfc_node, entry, mfc_compare)

extern struct dvmrpd_conf	*rdeconf;

/* timers */
void
mfc_expire_timer(int fd, short event, void *arg)
{
	struct mfc_node	*mn = arg;
	struct mfc	 nmfc;

	log_debug("mfc_expire_timer: group %s", inet_ntoa(mn->group));

	/* remove route entry */
	nmfc.origin = mn->origin;
	nmfc.group = mn->group;
	rde_imsg_compose_parent(IMSG_MFC_DEL, 0, &nmfc, sizeof(nmfc));

	event_del(&mn->expiration_timer);
	mfc_remove(mn);
}

int
mfc_reset_expire_timer(struct mfc_node *mn)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = ROUTE_EXPIRATION_TIME;
	return (evtimer_add(&mn->expiration_timer, &tv));
}

int
mfc_start_expire_timer(struct mfc_node *mn)
{
	struct timeval	tv;

	log_debug("mfc_start_expire_timer: group %s", inet_ntoa(mn->group));

	timerclear(&tv);
	tv.tv_sec = ROUTE_EXPIRATION_TIME;
	return (evtimer_add(&mn->expiration_timer, &tv));
}

void
mfc_prune_timer(int fd, short event, void *arg)
{
	struct mfc_node	*mn = arg;

	log_debug("mfc_prune_timer: group %s", inet_ntoa(mn->group));

	event_del(&mn->prune_timer);
}

int
mfc_start_prune_timer(struct mfc_node *mn)
{
	struct timeval	tv;

	log_debug("mfc_start_prune_timer: group %s", inet_ntoa(mn->group));

	timerclear(&tv);
	tv.tv_sec = MAX_PRUNE_LIFETIME;
	return (evtimer_add(&mn->prune_timer, &tv));
}

int
mfc_reset_prune_timer(struct mfc_node *mn)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = MAX_PRUNE_LIFETIME;
	return (evtimer_add(&mn->prune_timer, &tv));
}

/* route table */
void
mfc_init(void)
{
	RB_INIT(&mfc);
}

int
mfc_compare(struct mfc_node *a, struct mfc_node *b)
{
	if (ntohl(a->origin.s_addr) < ntohl(b->origin.s_addr))
		return (-1);
	if (ntohl(a->origin.s_addr) > ntohl(b->origin.s_addr))
		return (1);
	if (ntohl(a->group.s_addr) < ntohl(b->group.s_addr))
		return (-1);
	if (ntohl(a->group.s_addr) > ntohl(b->group.s_addr))
		return (1);
	return (0);
}

struct mfc_node *
mfc_find(in_addr_t origin, in_addr_t group)
{
	struct mfc_node	s;

	s.origin.s_addr = origin;
	s.group.s_addr = group;

	return (RB_FIND(mfc_tree, &mfc, &s));
}

int
mfc_insert(struct mfc_node *m)
{
	if (RB_INSERT(mfc_tree, &mfc, m) != NULL) {
		log_warnx("mfc_insert failed for group %s",
		    inet_ntoa(m->group));
		free(m);
		return (-1);
	}

	return (0);
}

int
mfc_remove(struct mfc_node *m)
{
	if (RB_REMOVE(mfc_tree, &mfc, m) == NULL) {
		log_warnx("mfc_remove failed for group %s",
		    inet_ntoa(m->group));
		return (-1);
	}

	free(m);
	return (0);
}

void
mfc_clear(void)
{
	struct mfc_node	*m;

	while ((m = RB_MIN(mfc_tree, &mfc)) != NULL)
		mfc_remove(m);
}

void
mfc_dump(pid_t pid)
{
	static struct ctl_mfc	 mfcctl;
	struct timespec		 now;
	struct timeval		 tv, now2, res;
	struct mfc_node		*mn;
	int			 i;

	clock_gettime(CLOCK_MONOTONIC, &now);

	RB_FOREACH(mn, mfc_tree, &mfc) {
		mfcctl.origin.s_addr = mn->origin.s_addr;
		mfcctl.group.s_addr = mn->group.s_addr;
		mfcctl.uptime = now.tv_sec - mn->uptime;
		mfcctl.ifindex = mn->ifindex;

		for (i = 0; i < MAXVIFS; i ++) {
			mfcctl.ttls[i] = mn->ttls[i];
		}

		gettimeofday(&now2, NULL);
		if (evtimer_pending(&mn->expiration_timer, &tv)) {
			timersub(&tv, &now2, &res);
			mfcctl.expire = res.tv_sec;
		} else
			mfcctl.expire = -1;

		rde_imsg_compose_dvmrpe(IMSG_CTL_SHOW_MFC, 0, pid, &mfcctl,
		    sizeof(mfcctl));
	}
}

struct rt_node *
mfc_find_origin(struct in_addr group)
{
	struct mfc_node	*mn;

	RB_FOREACH(mn, mfc_tree, &mfc)
		if (group.s_addr == mn->group.s_addr)
			return (rt_match_origin(mn->origin.s_addr));

	return (NULL);
}

void
mfc_send_prune(struct rt_node *rn, struct mfc_node *mn)
{
	struct prune	p;

	memset(&p, 0, sizeof(p));

	p.origin.s_addr = (mn->origin.s_addr &
	    htonl(prefixlen2mask(rn->prefixlen)));
	p.netmask.s_addr = htonl(prefixlen2mask(rn->prefixlen));
	p.group.s_addr = mn->group.s_addr;
	p.nexthop.s_addr = rn->nexthop.s_addr;
	p.ifindex = mn->ifindex;

	rde_imsg_compose_dvmrpe(IMSG_SEND_PRUNE, 0, 0, &p, sizeof(p));

	mfc_start_prune_timer(mn);
}

void
mfc_update_source(struct rt_node *rn)
{
	struct mfc_node		*mn;
	struct mfc		 m;
	int			 i;
	u_int8_t		 found;

	RB_FOREACH(mn, mfc_tree, &mfc) {
		if (rn->prefix.s_addr == (mn->origin.s_addr &
		    htonl(prefixlen2mask(rn->prefixlen)))) {
			mn->ifindex = rn->ifindex;

			found = 0;

			for (i = 0; i < MAXVIFS; i++) {
				mn->ttls[i] = rn->ttls[i];
				if (mn->ttls[i] != 0)
					found = 1;
			}

			m.origin.s_addr = mn->origin.s_addr;
			m.group.s_addr = mn->group.s_addr;
			m.ifindex = mn->ifindex;

			for (i = 0; i < MAXVIFS; i++)
				m.ttls[i] = mn->ttls[i];

			rde_imsg_compose_parent(IMSG_MFC_ADD, 0, &m, sizeof(m));

			mfc_reset_expire_timer(mn);

			if (!found && !rn->connected)
				mfc_send_prune(rn, mn);
		}
	}
}

void
mfc_update(struct mfc *nmfc)
{
	struct mfc_node		*mn;
	struct rt_node		*rn;
	struct timespec		 now;
	int			 i;
	u_int8_t		 found = 0;

	clock_gettime(CLOCK_MONOTONIC, &now);

	if ((mn = mfc_find(nmfc->origin.s_addr, nmfc->group.s_addr)) == NULL) {
		if ((mn = calloc(1, sizeof(struct mfc_node))) == NULL)
			fatalx("mfc_update");

		mn->origin.s_addr = nmfc->origin.s_addr;
		mn->group.s_addr = nmfc->group.s_addr;
		mn->ifindex = nmfc->ifindex;
		mn->uptime = now.tv_sec;
		for (i = 0; i < MAXVIFS; i++) {
			mn->ttls[i] = nmfc->ttls[i];
			if (mn->ttls[i] != 0)
				found = 1;
		}

		if (mfc_insert(mn) == 0) {
			rde_imsg_compose_parent(IMSG_MFC_ADD, 0, nmfc,
			    sizeof(*nmfc));
		}

		evtimer_set(&mn->expiration_timer, mfc_expire_timer, mn);
		evtimer_set(&mn->prune_timer, mfc_expire_timer, mn);
		mfc_start_expire_timer(mn);

		if (!found) {
			/* We removed all downstream interfaces,
			   start the pruning process */
			rn = rt_match_origin(mn->origin.s_addr);
			if (rn == NULL) {
				fatal("mfc_update: cannot find information "
				    " about source");
			}

			mfc_send_prune(rn, mn);
		}
	}
}

void
mfc_delete(struct mfc *nmfc)
{
	struct mfc_node	*mn;

	if ((mn = mfc_find(nmfc->origin.s_addr, nmfc->group.s_addr)) == NULL)
		return;

	/* XXX decide if it should really be removed */
	mfc_remove(mn);

	/* XXX notify parent */
}

int
mfc_check_members(struct rt_node *rn, struct iface *iface)
{
	struct mfc_node		*mn;

	RB_FOREACH(mn, mfc_tree, &mfc) {
		if (mn->origin.s_addr == rn->prefix.s_addr) {
			if (rde_group_list_find(iface, mn->group) != 0)
				return (1);
		}
	}

	return (0);
}

void
mfc_recv_prune(struct prune *p)
{
	struct rt_node		*rn;
	struct mfc_node		*mn;
	struct prune_node	*pn;
	struct iface		*iface;
	struct mfc		 m;

	iface = if_find_index(p->ifindex);
	if (iface == NULL) {
		log_debug("mfc_recv_prune: unknown interface");
		return;
	}

	rn = rt_match_origin(p->origin.s_addr);
	if (rn == NULL) {
		log_debug("mfc_recv_prune: no information for %s\n",
		    inet_ntoa(p->origin));
		return;
	}

	if (srt_find_ds(rn, p->nexthop.s_addr) == NULL) {
		log_debug("mfc_recv_prune: prune received from a "
		    "non downstream neighbor\n");
		return;
	}

	mn = mfc_find(p->origin.s_addr, p->group.s_addr);
	if (mn) {
		log_debug("mfc_recv_prune: no information for %s\n",
		    inet_ntoa(p->origin));
		return;
	}

	pn = mfc_find_prune(mn, p);
	if (pn == NULL) {
		mfc_add_prune(mn, p);
		if (prune_compare(mn, rn, p->ifindex) &&
		    !rde_group_list_find(iface, p->group)) {
			mn->ttls[p->ifindex] = 0;

			m.ifindex = p->ifindex;
			m.origin.s_addr = p->origin.s_addr;
			m.group.s_addr = p->group.s_addr;
			mfc_update(&m);
		}
	} else
		mfc_reset_prune_expire_timer(pn);
}

void
mfc_add_prune(struct mfc_node *mn, struct prune *p)
{
	struct prune_node	*pn;
	struct timeval		 tv;

	pn = calloc(1, sizeof(struct prune));
	if (pn == NULL)
		fatal("prune_add");

	timerclear(&tv);
	tv.tv_sec = MAX_PRUNE_LIFETIME;

	pn->nbr.s_addr = p->nexthop.s_addr;
	pn->ifindex = p->ifindex;
	pn->parent = mn;

	evtimer_set(&pn->lifetime_timer, prune_expire_timer, pn);
	evtimer_add(&pn->lifetime_timer, &tv);

	LIST_INSERT_HEAD(&mn->prune_list, pn, entry);

	mn->prune_cnt[p->ifindex]++;
}

struct prune_node *
mfc_find_prune(struct mfc_node *mn, struct prune *p)
{
	struct prune_node	*pn;

	LIST_FOREACH(pn, &mn->prune_list, entry) {
		if (p->nexthop.s_addr == pn->nbr.s_addr)
			return (pn);
	}

	return (NULL);
}

void
mfc_delete_prune(struct mfc_node *mn, struct prune_node *pn)
{
	unsigned int	ifindex = pn->ifindex;

	if (evtimer_pending(&pn->lifetime_timer, NULL))
		if (evtimer_del(&pn->lifetime_timer) == -1)
			fatal("mfc_delete_prune");

	LIST_REMOVE(pn, entry);
	free(pn);
	mn->prune_cnt[ifindex]--;
}

int
prune_compare(struct mfc_node *mn, struct rt_node *rn, int ifindex)
{
	if (mn->prune_cnt[ifindex] == rn->ds_cnt[ifindex])
		return (1);

	return (0);
}

int
mfc_reset_prune_expire_timer(struct prune_node *pn)
{
	struct timeval	tv;

	timerclear(&tv);
	tv.tv_sec = MAX_PRUNE_LIFETIME;

	return (evtimer_add(&pn->lifetime_timer, &tv));
}

void
prune_expire_timer(int fd, short event, void *arg)
{
	struct prune_node	*pn = arg;

	LIST_REMOVE(pn, entry);

	pn->parent->prune_cnt[pn->ifindex]--;
	pn->parent->ttls[pn->ifindex] = 1;

	free(pn);
}
