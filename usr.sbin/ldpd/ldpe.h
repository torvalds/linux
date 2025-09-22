/*	$OpenBSD: ldpe.h,v 1.80 2024/11/21 13:27:13 claudio Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
 * Copyright (c) 2004, 2005, 2008 Esben Norby <norby@openbsd.org>
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

#ifndef _LDPE_H_
#define _LDPE_H_

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <net/pfkeyv2.h>

#include "ldpd.h"

#define min(x,y) ((x) <= (y) ? (x) : (y))
#define max(x,y) ((x) > (y) ? (x) : (y))

struct hello_source {
	enum hello_type		 type;
	struct in_addr		 lsr_id;
	struct {
		struct iface_af	*ia;
		union ldpd_addr	 src_addr;
	} link;
	struct tnbr		*target;
};

struct adj {
	LIST_ENTRY(adj)		 global_entry;
	LIST_ENTRY(adj)		 nbr_entry;
	LIST_ENTRY(adj)		 ia_entry;
	struct in_addr		 lsr_id;
	struct nbr		*nbr;
	int			 ds_tlv;
	struct hello_source	 source;
	struct event		 inactivity_timer;
	uint16_t		 holdtime;
	union ldpd_addr		 trans_addr;
};

struct buf_read {
	uint8_t			 buf[IBUF_READ_SIZE];
	size_t			 wpos;
};

struct tcp_conn {
	struct nbr		*nbr;
	int			 fd;
	struct buf_read		*rbuf;
	struct evbuf		 wbuf;
	struct event		 rev;
};

struct nbr {
	RB_ENTRY(nbr)		 id_tree, addr_tree, pid_tree;
	struct tcp_conn		*tcp;
	LIST_HEAD(, adj)	 adj_list;	/* adjacencies */
	struct event		 ev_connect;
	struct event		 keepalive_timer;
	struct event		 keepalive_timeout;
	struct event		 init_timeout;
	struct event		 initdelay_timer;

	struct mapping_head	 mapping_list;
	struct mapping_head	 withdraw_list;
	struct mapping_head	 request_list;
	struct mapping_head	 release_list;
	struct mapping_head	 abortreq_list;

	uint32_t		 peerid;	/* unique ID in DB */
	int			 af;
	int			 ds_tlv;
	int			 v4_enabled;	/* announce/process v4 msgs */
	int			 v6_enabled;	/* announce/process v6 msgs */
	struct in_addr		 id;		/* lsr id */
	union ldpd_addr		 laddr;		/* local address */
	union ldpd_addr		 raddr;		/* remote address */
	uint32_t		 raddr_scope;	/* remote address scope (v6) */
	time_t			 uptime;
	int			 fd;
	int			 state;
	uint32_t		 conf_seqnum;
	int			 idtimer_cnt;
	uint16_t		 keepalive;
	uint16_t		 max_pdu_len;

	uint32_t		 auth_spi_in;
	uint32_t		 auth_spi_out;
	int			 auth_established;

	int			 flags;
};
#define F_NBR_GTSM_NEGOTIATED	 0x01
#define F_NBR_CAP_DYNAMIC	 0x02
#define F_NBR_CAP_TWCARD	 0x04
#define F_NBR_CAP_UNOTIF	 0x08

RB_HEAD(nbr_id_head, nbr);
RB_PROTOTYPE(nbr_id_head, nbr, id_tree, nbr_id_compare)
RB_HEAD(nbr_addr_head, nbr);
RB_PROTOTYPE(nbr_addr_head, nbr, addr_tree, nbr_addr_compare)
RB_HEAD(nbr_pid_head, nbr);
RB_PROTOTYPE(nbr_pid_head, nbr, pid_tree, nbr_pid_compare)

struct pending_conn {
	TAILQ_ENTRY(pending_conn)	 entry;
	int				 fd;
	int				 af;
	union ldpd_addr			 addr;
	struct event			 ev_timeout;
};
#define PENDING_CONN_TIMEOUT	5

struct mapping_entry {
	TAILQ_ENTRY(mapping_entry)	entry;
	struct map			map;
};

struct ctl_conn;

struct ldpd_sysdep {
	uint8_t		no_pfkey;
	uint8_t		no_md5sig;
};

extern struct ldpd_conf		*leconf;
extern struct ldpd_sysdep	 sysdep;
extern struct nbr_id_head	 nbrs_by_id;
extern struct nbr_addr_head	 nbrs_by_addr;
extern struct nbr_pid_head	 nbrs_by_pid;

/* accept.c */
void	accept_init(void);
int	accept_add(int, void (*)(int, short, void *), void *);
void	accept_del(int);
void	accept_pause(void);
void	accept_unpause(void);

/* hello.c */
int	 send_hello(enum hello_type, struct iface_af *, struct tnbr *);
void	 recv_hello(struct in_addr, struct ldp_msg *, int, union ldpd_addr *,
	    struct iface *, int, char *, uint16_t);

/* init.c */
void	 send_init(struct nbr *);
int	 recv_init(struct nbr *, char *, uint16_t);
void	 send_capability(struct nbr *, uint16_t, int);
int	 recv_capability(struct nbr *, char *, uint16_t);

/* keepalive.c */
void	 send_keepalive(struct nbr *);
int	 recv_keepalive(struct nbr *, char *, uint16_t);

/* notification.c */
void	 send_notification_full(struct tcp_conn *, struct notify_msg *);
void	 send_notification(struct tcp_conn *, uint32_t, uint32_t, uint16_t);
void	 send_notification_rtlvs(struct nbr *, uint32_t, uint32_t, uint16_t,
	    uint16_t, uint16_t, char *);
int	 recv_notification(struct nbr *, char *, uint16_t);
int	 gen_status_tlv(struct ibuf *, uint32_t, uint32_t, uint16_t);

/* address.c */
void	 send_address_single(struct nbr *, struct if_addr *, int);
void	 send_address_all(struct nbr *, int);
void	 send_mac_withdrawal(struct nbr *, struct map *, uint8_t *);
int	 recv_address(struct nbr *, char *, uint16_t);

/* labelmapping.c */
#define PREFIX_SIZE(x)	(((x) + 7) / 8)
void	 send_labelmessage(struct nbr *, uint16_t, struct mapping_head *);
int	 recv_labelmessage(struct nbr *, char *, uint16_t, uint16_t);
int	 gen_pw_status_tlv(struct ibuf *, uint32_t);
uint16_t len_fec_tlv(struct map *);
int	 gen_fec_tlv(struct ibuf *, struct map *);
int	 tlv_decode_fec_elm(struct nbr *, struct ldp_msg *, char *,
	    uint16_t, struct map *);

/* ldpe.c */
void		 ldpe(int, int, char *);
int		 ldpe_imsg_compose_parent(int, pid_t, void *,
		    uint16_t);
int		 ldpe_imsg_compose_lde(int, uint32_t, pid_t, void *,
		    uint16_t);
void		 ldpe_reset_nbrs(int);
void		 ldpe_reset_ds_nbrs(void);
void		 ldpe_remove_dynamic_tnbrs(int);
void		 ldpe_stop_init_backoff(int);
void		 ldpe_iface_ctl(struct ctl_conn *, unsigned int);
void		 ldpe_adj_ctl(struct ctl_conn *);
void		 ldpe_nbr_ctl(struct ctl_conn *);
void		 mapping_list_add(struct mapping_head *, struct map *);
void		 mapping_list_clr(struct mapping_head *);

/* interface.c */
struct iface	*if_new(struct kif *);
void		 if_exit(struct iface *);
struct iface	*if_lookup(struct ldpd_conf *, unsigned short);
struct iface_af *iface_af_get(struct iface *, int);
void		 if_addr_add(struct kaddr *);
void		 if_addr_del(struct kaddr *);
void		 if_update(struct iface *, int);
void		 if_update_all(int);
struct ctl_iface *if_to_ctl(struct iface_af *);
in_addr_t	 if_get_ipv4_addr(struct iface *);

/* adjacency.c */
struct adj	*adj_new(struct in_addr, struct hello_source *,
		    union ldpd_addr *);
void		 adj_del(struct adj *, uint32_t);
struct adj	*adj_find(struct hello_source *);
int		 adj_get_af(struct adj *adj);
void		 adj_start_itimer(struct adj *);
void		 adj_stop_itimer(struct adj *);
struct tnbr	*tnbr_new(struct ldpd_conf *, int, union ldpd_addr *);
struct tnbr	*tnbr_find(struct ldpd_conf *, int, union ldpd_addr *);
struct tnbr	*tnbr_check(struct tnbr *);
void		 tnbr_update(struct tnbr *);
void		 tnbr_update_all(int);
struct ctl_adj	*adj_to_ctl(struct adj *);

/* neighbor.c */
int			 nbr_fsm(struct nbr *, enum nbr_event);
struct nbr		*nbr_new(struct in_addr, int, int, union ldpd_addr *,
			    uint32_t);
void			 nbr_del(struct nbr *);
struct nbr		*nbr_find_ldpid(uint32_t);
struct nbr		*nbr_find_addr(int, union ldpd_addr *);
struct nbr		*nbr_find_peerid(uint32_t);
int			 nbr_adj_count(struct nbr *, int);
int			 nbr_session_active_role(struct nbr *);
void			 nbr_stop_ktimer(struct nbr *);
void			 nbr_stop_ktimeout(struct nbr *);
void			 nbr_stop_itimeout(struct nbr *);
void			 nbr_start_idtimer(struct nbr *);
void			 nbr_stop_idtimer(struct nbr *);
int			 nbr_pending_idtimer(struct nbr *);
int			 nbr_pending_connect(struct nbr *);
int			 nbr_establish_connection(struct nbr *);
int			 nbr_gtsm_enabled(struct nbr *, struct nbr_params *);
int			 nbr_gtsm_setup(int, int, struct nbr_params *);
int			 nbr_gtsm_check(int, struct nbr *, struct nbr_params *);
struct nbr_params	*nbr_params_new(struct in_addr);
struct nbr_params	*nbr_params_find(struct ldpd_conf *, struct in_addr);
uint16_t		 nbr_get_keepalive(int, struct in_addr);
struct ctl_nbr		*nbr_to_ctl(struct nbr *);
void			 nbr_clear_ctl(struct ctl_nbr *);

/* packet.c */
int			 gen_ldp_hdr(struct ibuf *, uint16_t);
int			 gen_msg_hdr(struct ibuf *, uint16_t, uint16_t);
int			 send_packet(int, int, union ldpd_addr *,
			    struct iface_af *, void *, size_t);
void			 disc_recv_packet(int, short, void *);
void			 session_accept(int, short, void *);
void			 session_accept_nbr(struct nbr *, int);
void			 session_shutdown(struct nbr *, uint32_t, uint32_t,
			    uint32_t);
void			 session_close(struct nbr *);
struct tcp_conn		*tcp_new(int, struct nbr *);
void			 pending_conn_del(struct pending_conn *);
struct pending_conn	*pending_conn_find(int, union ldpd_addr *);

/* pfkey.c */
int	pfkey_read(int, struct sadb_msg *);
int	pfkey_establish(struct ldpd_conf *, struct nbr *);
int	pfkey_remove(struct nbr *);
int	pfkey_init(void);

/* l2vpn.c */
void	ldpe_l2vpn_init(struct l2vpn *);
void	ldpe_l2vpn_exit(struct l2vpn *);
void	ldpe_l2vpn_pw_init(struct l2vpn_pw *);
void	ldpe_l2vpn_pw_exit(struct l2vpn_pw *);

#endif	/* _LDPE_H_ */
