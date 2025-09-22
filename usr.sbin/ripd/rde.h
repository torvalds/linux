/*	$OpenBSD: rde.h,v 1.4 2007/10/24 20:38:03 claudio Exp $ */

/*
 * Copyright (c) 2006 Michele Marchetto <mydecay@openbeer.it>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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

struct rt_node {
	RB_ENTRY(rt_node)	 entry;
	struct event		 timeout_timer;
	struct event		 garbage_timer;
	struct in_addr		 prefix;
	struct in_addr		 netmask;
	struct in_addr		 nexthop;
	u_short			 ifindex;
	u_int16_t		 flags;
	u_int8_t		 metric;
};

/* rde.c */
pid_t		 rde(struct ripd_conf *, int [2], int [2], int [2]);
void		 rde_send_change_kroute(struct rt_node *);
void		 rde_send_delete_kroute(struct rt_node *);
int		 rde_imsg_compose_ripe(int, u_int32_t, pid_t, void *,
		     u_int16_t);

/* rde_rib.c */
void		 rt_init(void);
int		 rt_compare(struct rt_node *, struct rt_node *);
struct rt_node	*rt_find(in_addr_t, in_addr_t);
struct rt_node	*rt_new_kr(struct kroute *);
struct rt_node	*rt_new_rr(struct rip_route *, u_int8_t);
int		 rt_insert(struct rt_node *);
int		 rt_remove(struct rt_node *);
void		 rt_complete(struct rip_route *);
void		 rt_snap(u_int32_t);
void		 rt_clear(void);
void		 route_reset_timers(struct rt_node *);
int		 route_start_timeout(struct rt_node *);
void		 route_start_garbage(struct rt_node *);
void		 rt_dump(pid_t);

#endif /* _RDE_H_ */
