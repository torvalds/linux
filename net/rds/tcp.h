/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RDS_TCP_H
#define _RDS_TCP_H

#define RDS_TCP_PORT	16385

struct rds_tcp_incoming {
	struct rds_incoming	ti_inc;
	struct sk_buff_head	ti_skb_list;
};

struct rds_tcp_connection {

	struct list_head	t_tcp_node;
	bool			t_tcp_node_detached;
	struct rds_conn_path	*t_cpath;
	/* t_conn_path_lock synchronizes the connection establishment between
	 * rds_tcp_accept_one and rds_tcp_conn_path_connect
	 */
	struct mutex		t_conn_path_lock;
	struct socket		*t_sock;
	void			*t_orig_write_space;
	void			*t_orig_data_ready;
	void			*t_orig_state_change;

	struct rds_tcp_incoming	*t_tinc;
	size_t			t_tinc_hdr_rem;
	size_t			t_tinc_data_rem;

	/* XXX error report? */
	struct work_struct	t_conn_w;
	struct work_struct	t_send_w;
	struct work_struct	t_down_w;
	struct work_struct	t_recv_w;

	/* for info exporting only */
	struct list_head	t_list_item;
	u32			t_last_sent_nxt;
	u32			t_last_expected_una;
	u32			t_last_seen_una;
};

struct rds_tcp_statistics {
	uint64_t	s_tcp_data_ready_calls;
	uint64_t	s_tcp_write_space_calls;
	uint64_t	s_tcp_sndbuf_full;
	uint64_t	s_tcp_connect_raced;
	uint64_t	s_tcp_listen_closed_stale;
};

/* tcp.c */
void rds_tcp_tune(struct socket *sock);
void rds_tcp_set_callbacks(struct socket *sock, struct rds_conn_path *cp);
void rds_tcp_reset_callbacks(struct socket *sock, struct rds_conn_path *cp);
void rds_tcp_restore_callbacks(struct socket *sock,
			       struct rds_tcp_connection *tc);
u32 rds_tcp_write_seq(struct rds_tcp_connection *tc);
u32 rds_tcp_snd_una(struct rds_tcp_connection *tc);
u64 rds_tcp_map_seq(struct rds_tcp_connection *tc, u32 seq);
extern struct rds_transport rds_tcp_transport;
void rds_tcp_accept_work(struct sock *sk);
int rds_tcp_laddr_check(struct net *net, const struct in6_addr *addr,
			__u32 scope_id);
/* tcp_connect.c */
int rds_tcp_conn_path_connect(struct rds_conn_path *cp);
void rds_tcp_conn_path_shutdown(struct rds_conn_path *conn);
void rds_tcp_state_change(struct sock *sk);

/* tcp_listen.c */
struct socket *rds_tcp_listen_init(struct net *net, bool isv6);
void rds_tcp_listen_stop(struct socket *sock, struct work_struct *acceptor);
void rds_tcp_listen_data_ready(struct sock *sk);
int rds_tcp_accept_one(struct socket *sock);
void rds_tcp_keepalive(struct socket *sock);
void *rds_tcp_listen_sock_def_readable(struct net *net);

/* tcp_recv.c */
int rds_tcp_recv_init(void);
void rds_tcp_recv_exit(void);
void rds_tcp_data_ready(struct sock *sk);
int rds_tcp_recv_path(struct rds_conn_path *cp);
void rds_tcp_inc_free(struct rds_incoming *inc);
int rds_tcp_inc_copy_to_user(struct rds_incoming *inc, struct iov_iter *to);

/* tcp_send.c */
void rds_tcp_xmit_path_prepare(struct rds_conn_path *cp);
void rds_tcp_xmit_path_complete(struct rds_conn_path *cp);
int rds_tcp_xmit(struct rds_connection *conn, struct rds_message *rm,
		 unsigned int hdr_off, unsigned int sg, unsigned int off);
void rds_tcp_write_space(struct sock *sk);

/* tcp_stats.c */
DECLARE_PER_CPU(struct rds_tcp_statistics, rds_tcp_stats);
#define rds_tcp_stats_inc(member) rds_stats_inc_which(rds_tcp_stats, member)
unsigned int rds_tcp_stats_info_copy(struct rds_info_iterator *iter,
				     unsigned int avail);

#endif
