/*	$OpenBSD: eigrpe.h,v 1.18 2021/01/19 10:37:25 claudio Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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

#ifndef _EIGRPE_H_
#define _EIGRPE_H_

#include <sys/queue.h>
#include <sys/tree.h>

#include <event.h>

struct pbuf {
	struct ibuf		*buf;
	int			 refcnt;
};

struct packet {
	TAILQ_ENTRY(packet)	 entry;
	struct nbr		*nbr;
	uint32_t		 seq_num;
	struct pbuf		*pbuf;
	struct event		 ev_timeout;
	int			 attempts;
};

struct nbr {
	RB_ENTRY(nbr)		 addr_tree, pid_tree;
	TAILQ_ENTRY(nbr)	 entry;
	struct event		 ev_ack;
	struct event		 ev_hello_timeout;
	struct eigrp_iface	*ei;
	union eigrpd_addr	 addr;
	uint32_t		 peerid;
	time_t			 uptime;
	uint16_t		 hello_holdtime;
	uint8_t			 flags;
#define F_EIGRP_NBR_SELF		0x01
#define F_EIGRP_NBR_PENDING		0x02
#define F_EIGRP_NBR_CR_MODE		0x04

	struct rinfo_head	 update_list;	/* unicast updates */
	struct rinfo_head	 query_list;	/* unicast queries  */
	struct rinfo_head	 reply_list;	/* unicast replies */

	/* RTP */
	uint32_t		 recv_seq;
	uint32_t		 next_mcast_seq;
	TAILQ_HEAD(, packet)	 retrans_list;
};
RB_PROTOTYPE(nbr_addr_head, nbr, addr_tree, nbr_compare)
RB_PROTOTYPE(nbr_pid_head, nbr, pid_tree, nbr_pid_compare)

#define PREFIX_SIZE4(x)	(((x - 1) / 8) + 1)
#define PREFIX_SIZE6(x)	((x == 128) ? 16 : ((x / 8) + 1))

extern struct eigrpd_conf        *econf;

struct ctl_conn;

/* eigrpe.c */
void		 eigrpe(int, int, char *);
int		 eigrpe_imsg_compose_parent(int, pid_t, void *, uint16_t);
int		 eigrpe_imsg_compose_rde(int, uint32_t, pid_t, void *,
		    uint16_t);
void		 eigrpe_instance_init(struct eigrp *);
void		 eigrpe_instance_del(struct eigrp *);
void		 message_add(struct rinfo_head *, struct rinfo *);
void		 message_list_clr(struct rinfo_head *);
void		 seq_addr_list_clr(struct seq_addr_head *);
void		 eigrpe_orig_local_route(struct eigrp_iface *,
		    struct if_addr *, int);
void		 eigrpe_iface_ctl(struct ctl_conn *, unsigned int);
void		 eigrpe_nbr_ctl(struct ctl_conn *);
void		 eigrpe_stats_ctl(struct ctl_conn *);

/* interface.c */
struct iface		*if_lookup(struct eigrpd_conf *, unsigned int);
void			 if_addr_new(struct iface *, struct kaddr *);
void			 if_addr_del(struct iface *, struct kaddr *);
in_addr_t		 if_primary_addr(struct iface *);
uint8_t			 if_primary_addr_prefixlen(struct iface *);
void			 if_update(struct iface *, int);
struct eigrp_iface	*eigrp_if_new(struct eigrpd_conf *, struct eigrp *,
			    struct kif *);
void			 eigrp_if_del(struct eigrp_iface *);
struct eigrp_iface	*eigrp_if_lookup(struct iface *, int, uint16_t);
struct eigrp_iface	*eigrp_if_lookup_id(uint32_t);
struct ctl_iface	*if_to_ctl(struct eigrp_iface *);
void			 if_set_sockbuf(int);
int			 if_set_ipv4_mcast_ttl(int, uint8_t);
int			 if_set_ipv4_mcast(struct iface *);
int			 if_set_ipv4_mcast_loop(int);
int			 if_set_ipv4_recvif(int, int);
int			 if_set_ipv4_hdrincl(int);
int			 if_set_ipv6_mcast(struct iface *);
int			 if_set_ipv6_mcast_loop(int);
int			 if_set_ipv6_pktinfo(int, int);
int			 if_set_ipv6_dscp(int, int);

/* neighbor.c */
struct nbr	*nbr_new(struct eigrp_iface *, union eigrpd_addr *,
		    uint16_t, int);
void		 nbr_init(struct nbr *);
void		 nbr_del(struct nbr *);
struct nbr	*nbr_find(struct eigrp_iface *, union eigrpd_addr *);
struct nbr	*nbr_find_peerid(uint32_t);
struct ctl_nbr	*nbr_to_ctl(struct nbr *);
void		 nbr_clear_ctl(struct ctl_nbr *);
void		 nbr_start_timeout(struct nbr *);

/* rtp.c */
void		 rtp_packet_del(struct packet *);
void		 rtp_process_ack(struct nbr *, uint32_t);
void		 rtp_send_ucast(struct nbr *, struct ibuf *);
void		 rtp_send(struct eigrp_iface *, struct nbr *, struct ibuf *);
void		 rtp_send_ack(struct nbr *);
void		 rtp_ack_timer(int, short, void *);
void		 rtp_ack_start_timer(struct nbr *);
void		 rtp_ack_stop_timer(struct nbr *);

/* packet.c */
int	 gen_eigrp_hdr(struct ibuf *, uint16_t, uint8_t, uint32_t,
	    uint16_t);
int	 send_packet(struct eigrp_iface *, struct nbr *, uint32_t,
	    struct ibuf *);
void	 recv_packet(int, short, void *);

/* tlv.c */
int			 gen_parameter_tlv(struct ibuf *, struct eigrp_iface *,
			    int);
int			 gen_sequence_tlv(struct ibuf *,
			    struct seq_addr_head *);
int			 gen_sw_version_tlv(struct ibuf *);
int			 gen_mcast_seq_tlv(struct ibuf *, uint32_t);
uint16_t		 len_route_tlv(struct rinfo *);
int			 gen_route_tlv(struct ibuf *, struct rinfo *);
struct tlv_parameter	*tlv_decode_parameter(struct tlv *, char *);
int			 tlv_decode_seq(int, struct tlv *, char *,
			    struct seq_addr_head *);
struct tlv_sw_version	*tlv_decode_sw_version(struct tlv *, char *);
struct tlv_mcast_seq	*tlv_decode_mcast_seq(struct tlv *, char *);
int			 tlv_decode_route(int, struct tlv *, char *,
			    struct rinfo *);
void			 metric_encode_mtu(uint8_t *, int);
int			 metric_decode_mtu(uint8_t *);

/* hello.c */
void	 send_hello(struct eigrp_iface *, struct seq_addr_head *, uint32_t);
void	 send_peerterm(struct nbr *);
void	 recv_hello(struct eigrp_iface *, union eigrpd_addr *, struct nbr *,
	    struct tlv_parameter *);

/* update.c */
void	 send_update(struct eigrp_iface *, struct nbr *, uint32_t,
	    struct rinfo_head *);
void	 recv_update(struct nbr *, struct rinfo_head *, uint32_t);

/* query.c */
void	 send_query(struct eigrp_iface *, struct nbr *, struct rinfo_head *,
	    int);
void	 recv_query(struct nbr *, struct rinfo_head *, int);

/* reply.c */
void	 send_reply(struct nbr *, struct rinfo_head *, int);
void	 recv_reply(struct nbr *, struct rinfo_head *, int);

#endif	/* _EIGRPE_H_ */
