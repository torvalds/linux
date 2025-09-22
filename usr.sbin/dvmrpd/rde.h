/*	$OpenBSD: rde.h,v 1.17 2024/05/18 11:17:30 jsg Exp $ */

/*
 * Copyright (c) 2005, 2006 Esben Norby <norby@openbsd.org>
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

#ifndef _RDE_H_
#define _RDE_H_

#include <sys/types.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <sys/queue.h>
#include <event.h>
#include <limits.h>

struct adv_rtr {
	struct in_addr		 addr;
	u_int8_t		 metric;
};

struct rt_node {
	RB_ENTRY(rt_node)	 entry;
	struct event		 expiration_timer;
	struct event		 holddown_timer;

	struct adv_rtr		 adv_rtr[MAXVIFS];

	u_int16_t		 ds_cnt[MAXVIFS];
	u_int8_t		 ttls[MAXVIFS];	/* downstream vif(s) */

	LIST_HEAD(, ds_nbr)	 ds_list;

	struct in_addr		 prefix;
	struct in_addr		 nexthop;
	time_t			 uptime;

	u_short			 ifindex;	/* learned from this iface */

	u_int8_t		 cost;
	u_int8_t		 old_cost;	/* used when in hold-down */
	u_int8_t		 flags;
	u_int8_t		 prefixlen;
	u_int8_t		 invalid;
	u_int8_t		 connected;
};

struct prune_node {
	LIST_ENTRY(prune_node)	 entry;
	struct event		 lifetime_timer;

	struct mfc_node		*parent;	/* back ptr to mfc_node */

	struct in_addr		 nbr;
	unsigned int		 ifindex;
};

struct mfc_node {
	RB_ENTRY(mfc_node)	 entry;
	struct event		 expiration_timer;
	struct event		 prune_timer;

	LIST_HEAD(, prune_node)	 prune_list;

	struct in_addr		 origin;
	struct in_addr		 group;
	time_t			 uptime;
	u_short			 ifindex;		/* incoming vif */
	u_int8_t		 ttls[MAXVIFS];		/* outgoing vif(s) */
	u_int8_t		 prune_cnt[MAXVIFS];
};

/* downstream neighbor per source */
struct ds_nbr {
	LIST_ENTRY(ds_nbr)	 entry;
	struct in_addr		 addr;
};

/* rde.c */
pid_t	rde(struct dvmrpd_conf *, int [2], int [2], int [2]);
int	rde_imsg_compose_parent(int, pid_t, void *, u_int16_t);
int	rde_imsg_compose_dvmrpe(int, u_int32_t, pid_t, void *, u_int16_t);

void	rde_group_list_add(struct iface *, struct in_addr);
int	rde_group_list_find(struct iface *, struct in_addr);
void	rde_group_list_remove(struct iface *, struct in_addr);

/* rde_mfc.c */
void		 mfc_init(void);
int		 mfc_compare(struct mfc_node *, struct mfc_node *);
struct mfc_node *mfc_find(in_addr_t, in_addr_t);
int		 mfc_insert(struct mfc_node *);
int		 mfc_remove(struct mfc_node *);
void		 mfc_clear(void);
void		 mfc_dump(pid_t);
void		 mfc_update(struct mfc *);
void		 mfc_delete(struct mfc *);
struct rt_node	*mfc_find_origin(struct in_addr);
void		 mfc_update_source(struct rt_node *);
int		 mfc_check_members(struct rt_node *, struct iface *);
void		 mfc_recv_prune(struct prune *);

/* rde_srt.c */
void		 rt_init(void);
int		 rt_compare(struct rt_node *, struct rt_node *);
struct rt_node	*rt_find(in_addr_t, u_int8_t);
struct rt_node	*rr_new_rt(struct route_report *, u_int32_t, int);
int		 rt_insert(struct rt_node *);
void		 rt_update(struct rt_node *);
int		 rt_remove(struct rt_node *);
void		 rt_clear(void);
void		 rt_snap(u_int32_t);
void		 rt_dump(pid_t);
struct rt_node	*rt_match_origin(in_addr_t);

int		 srt_check_route(struct route_report *, int);

struct ds_nbr	*srt_find_ds(struct rt_node *, u_int32_t);
void		 srt_expire_nbr(struct in_addr, unsigned int);
void		 srt_check_downstream_ifaces(struct rt_node *, struct iface *);

#endif	/* _RDE_H_ */
