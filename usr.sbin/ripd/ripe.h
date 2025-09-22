/*	$OpenBSD: ripe.h,v 1.14 2021/01/19 10:02:22 claudio Exp $ */

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

#ifndef _RIPE_H_
#define _RIPE_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

/* neighbor events */
enum nbr_event {
	NBR_EVT_RESPONSE_RCVD,
	NBR_EVT_REQUEST_RCVD,
	NBR_EVT_RESPONSE_SENT,
	NBR_EVT_TIMEOUT,
	NBR_EVT_KILL_NBR,
	NBR_EVT_NOTHING
};

/* neighbor actions */
enum nbr_action {
	NBR_ACT_STRT_TIMER,
	NBR_ACT_RST_TIMER,
	NBR_ACT_DEL,
	NBR_ACT_NOTHING
};

struct nbr_failed {
	struct event		 timeout_timer;
	LIST_ENTRY(nbr_failed)	 entry;
	struct in_addr		 addr;
	u_int32_t		 auth_seq_num;
};

struct nbr {
	LIST_ENTRY(nbr)		 entry, hash;
	struct event		 timeout_timer;
	struct in_addr		 addr;
	struct in_addr		 id;

	struct packet_head	 rq_list;
	struct packet_head	 rp_list;

	struct iface		*iface;

	u_int32_t		 peerid;	/* unique ID in DB */
	u_int32_t		 auth_seq_num;
	u_int16_t		 port;
	time_t			 uptime;
	int			 state;
	int			 flags;
};

struct ctl_conn;

/* packet.c */
int	 send_packet(struct iface *, void *, size_t, struct sockaddr_in *);
void	 recv_packet(int, short, void *);
int	 gen_rip_hdr(struct ibuf *, u_int8_t);

/* interface.c */
void			 if_init(struct ripd_conf *, struct iface *);
int			 if_fsm(struct iface *, enum iface_event);
int			 if_set_mcast(struct iface *);
int			 if_set_mcast_ttl(int, u_int8_t);
int			 if_set_mcast_loop(int);
int			 if_set_opt(int);
int			 if_set_tos(int, int);
void			 if_set_recvbuf(int);
struct iface		*if_new(struct kif *);
void			 if_del(struct iface *);
const char		*if_event_name(int);
const char		*if_action_name(int);
int			 if_join_group(struct iface *, struct in_addr *);
int			 if_leave_group(struct iface *, struct in_addr *);
struct ctl_iface	*if_to_ctl(struct iface *);

/* message.c */
void	 recv_request(struct iface *, struct nbr *, u_int8_t *, u_int16_t);
void	 recv_response(struct iface *, struct nbr *, u_int8_t *, u_int16_t);
void	 add_entry(struct packet_head *, struct rip_route *);
void	 clear_list(struct packet_head *);
int	 send_triggered_update(struct iface *, struct rip_route *);
int	 send_request(struct packet_head *, struct iface *, struct nbr *);
int	 send_response(struct packet_head *, struct iface *, struct nbr *);
int	 start_report_timer(void);
void	 report_timer(int, short, void *);

/* ripe.c */
pid_t	 ripe(struct ripd_conf *, int [2], int [2], int [2]);
int	 ripe_imsg_compose_parent(int, pid_t, void *, u_int16_t);
int	 ripe_imsg_compose_rde(int, u_int32_t, pid_t, void *,
	    u_int16_t);
void	 ripe_dispatch_main(int, short, void *);
void	 ripe_dispatch_rde(int, short, void *);
void	 ripe_iface_ctl(struct ctl_conn *, unsigned int);
void	 ripe_nbr_ctl(struct ctl_conn *);
void	 ripe_demote_iface(struct iface *, int);

/* auth.c */
int	 auth_validate(u_int8_t **, u_int16_t *, struct iface *, struct nbr *,
	    struct nbr_failed *, u_int32_t *);
int	 auth_gen(struct ibuf *, struct iface *);
int	 auth_add_trailer(struct ibuf *, struct iface *);
int	 md_list_add(struct auth_md_head *, u_int8_t, char *);
void	 md_list_copy(struct auth_md_head *, struct auth_md_head *);
void	 md_list_clr(struct auth_md_head *);

/* neighbor.c */
void		 nbr_init(u_int32_t);
struct nbr	*nbr_new(u_int32_t, struct iface *);
void		 nbr_del(struct nbr *);

struct nbr		*nbr_find_ip(struct iface *, u_int32_t);
struct nbr		*nbr_find_peerid(u_int32_t);
struct nbr_failed	*nbr_failed_find(struct iface *, u_int32_t);
void			 nbr_failed_delete(struct nbr_failed *);

int		 nbr_fsm(struct nbr *, enum nbr_event);
void		 nbr_timeout_timer(int, short, void *);
void		 nbr_act_del(struct nbr *);

const char	*nbr_event_name(int);
const char	*nbr_action_name(int);

struct ctl_nbr	*nbr_to_ctl(struct nbr *);

#endif /* _RIPE_H_ */
