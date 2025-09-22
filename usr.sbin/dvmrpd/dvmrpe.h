/*	$OpenBSD: dvmrpe.h,v 1.8 2023/06/26 10:08:56 claudio Exp $ */

/*
 * Copyright (c) 2004, 2005, 2006 Esben Norby <norby@openbsd.org>
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

#ifndef _DVMRPE_H_
#define _DVMRPE_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

/* neighbor events */
enum nbr_event {
	NBR_EVT_NOTHING,
	NBR_EVT_PROBE_RCVD,
	NBR_EVT_1_WAY_RCVD,
	NBR_EVT_2_WAY_RCVD,
	NBR_EVT_KILL_NBR,
	NBR_EVT_ITIMER,
	NBR_EVT_LL_DOWN
};

/* neighbor actions */
enum nbr_action {
	NBR_ACT_NOTHING,
	NBR_ACT_RST_ITIMER,
	NBR_ACT_STRT_ITIMER,
	NBR_ACT_RESET,
	NBR_ACT_DEL,
	NBR_ACT_CLR_LST
};

struct nbr {
	LIST_ENTRY(nbr)		 entry, hash;
	struct event		 inactivity_timer;

	struct rr_head		 rr_list;

	struct in_addr		 addr;
	struct in_addr		 id;

	struct iface		*iface;

	u_int32_t		 peerid;	/* unique ID in DB */
	u_int32_t		 gen_id;

	time_t			 uptime;

	int			 state;
	u_int8_t		 link_state;
	u_int8_t		 capabilities;
	u_int8_t		 compat;	/* mrouted compat */
};

struct route_report {
	struct in_addr		 net;
	struct in_addr		 mask;
	struct in_addr		 nexthop;
	struct in_addr		 adv_rtr;
	int			 refcount;
	u_short			 ifindex;
	u_int8_t		 metric;

};

struct rr_entry {
	TAILQ_ENTRY(rr_entry)	 entry;
	struct route_report	*re;
};

struct ctl_conn;

/* ask_nbrs2.c */
int	 send_ask_nbrs2(struct iface *, struct in_addr, void *, int);
void	 recv_ask_nbrs2(struct nbr *, char *, u_int16_t);

/* dvmrpe.c */
pid_t	 dvmrpe(struct dvmrpd_conf *, int[2], int[2], int[2]);
void	 dvmrpe_dispatch_main(int, short, void *);
void	 dvmrpe_dispatch_rde(int, short, void *);
int	 dvmrpe_imsg_compose_parent(int, pid_t, void *, u_int16_t);
int	 dvmrpe_imsg_compose_rde(int, u_int32_t, pid_t, void *, u_int16_t);

void	 dvmrpe_iface_ctl(struct ctl_conn *, unsigned int);
void	 dvmrpe_iface_igmp_ctl(struct ctl_conn *, unsigned int);
void	 dvmrpe_nbr_ctl(struct ctl_conn *);

/* graft.c */
int	 send_graft(struct iface *, struct in_addr, void *, int);
void	 recv_graft(struct nbr *, char *, u_int16_t);

/* graft_ack.c */
int	 send_graft_ack(struct iface *, struct in_addr, void *, int);
void	 recv_graft_ack(struct nbr *, char *, u_int16_t);

/* group.c */
struct ctl_group *group_to_ctl(struct group *);

/* igmp.c */
int	 group_fsm(struct group *, enum group_event);
int	 send_igmp_query(struct iface *, struct group *group);
void	 recv_igmp_query(struct iface *, struct in_addr, char *, u_int16_t);
void	 recv_igmp_report(struct iface *, struct in_addr, char *, u_int16_t,
	    u_int8_t);
void	 recv_igmp_leave(struct iface *, struct in_addr, char *, u_int16_t);

struct group	*group_list_add(struct iface *, u_int32_t);
void		 group_list_remove(struct iface *, struct group *);
struct group	*group_list_find(struct iface *, u_int32_t);
void		 group_list_clr(struct iface *);
int		 group_list_empty(struct iface *);
void		 group_list_dump(struct iface *, struct ctl_conn *);

const char	*group_event_name(int);
const char	*group_action_name(int);

/* interface.c */
int		 if_fsm(struct iface *, enum iface_event);
struct iface	*if_new(struct kif *);
int		 if_del(struct iface *);
int		 if_nbr_list_empty(struct iface *);
void		 if_init(struct dvmrpd_conf *, struct iface *);

const char	*if_event_name(int);
const char	*if_action_name(int);

int		 if_set_mcast_ttl(int, u_int8_t);
int		 if_set_tos(int, int);
int		 if_set_mcast_loop(int);
void		 if_set_recvbuf(int);

int		 if_join_group(struct iface *, struct in_addr *);
int		 if_leave_group(struct iface *, struct in_addr *);
int		 if_set_mcast(struct iface *);

struct ctl_iface *if_to_ctl(struct iface *);

/* nbrs2.c */
int	 send_nbrs2(struct iface *, struct in_addr, void *, int);
void	 recv_nbrs2(struct nbr *, char *, u_int16_t);

/* neighbor.c */
void		 nbr_init(u_int32_t);
struct nbr	*nbr_new(u_int32_t, struct iface *, int);
int		 nbr_del(struct nbr *);

struct nbr	*nbr_find_ip(struct iface *, u_int32_t);
struct nbr	*nbr_find_peerid(u_int32_t);

int		 nbr_fsm(struct nbr *, enum nbr_event);

int		 nbr_start_itimer(struct nbr *);
int		 nbr_stop_itimer(struct nbr *);
int		 nbr_reset_itimer(struct nbr *);

void		 nbr_itimer(int, short, void *);

int		 nbr_act_start(struct nbr *);
int		 nbr_act_reset_itimer(struct nbr *);
int		 nbr_act_start_itimer(struct nbr *);
int		 nbr_act_delete(struct nbr *);
int		 nbr_act_clear_lists(struct nbr *);

const char	*nbr_event_name(int);
const char	*nbr_action_name(int);

struct ctl_nbr	*nbr_to_ctl(struct nbr *);

/* packet.c */
int		 gen_dvmrp_hdr(struct ibuf *, struct iface *, u_int8_t);
int		 send_packet(struct iface *, struct ibuf *,
		     struct sockaddr_in *);
void		 recv_packet(int, short, void *);

/* probe.c */
int		 send_probe(struct iface *);
void		 recv_probe(struct iface *, struct in_addr, u_int32_t, u_int8_t,
		    char *, u_int16_t);

/* prune.c */
int		 send_prune(struct nbr *, struct prune *);
void		 recv_prune(struct nbr *, char *, u_int16_t);

/* report.c */
int		 send_report(struct iface *, struct in_addr, void *, int);
void		 recv_report(struct nbr *, char *, u_int16_t);

void		 report_timer(int, short, void *);
int		 start_report_timer(void);
int		 stop_report_timer(void);

void		 rr_list_add(struct rr_head *, struct route_report *);
void		 rr_list_clr(struct rr_head *);
void		 rr_list_send(struct rr_head *, struct iface *, struct nbr *);

#endif	/* _DVMRPE_H_ */
