#ifndef _RDS_TCP_H
#define _RDS_TCP_H

#define RDS_TCP_PORT	16385

struct rds_tcp_incoming {
	struct rds_incoming	ti_inc;
	struct sk_buff_head	ti_skb_list;
};

struct rds_tcp_connection {

	struct list_head	t_tcp_node;
	struct rds_connection   *conn;
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
int __init rds_tcp_init(void);
void rds_tcp_exit(void);
void rds_tcp_tune(struct socket *sock);
void rds_tcp_nonagle(struct socket *sock);
void rds_tcp_set_callbacks(struct socket *sock, struct rds_connection *conn);
void rds_tcp_restore_callbacks(struct socket *sock,
			       struct rds_tcp_connection *tc);
u32 rds_tcp_snd_nxt(struct rds_tcp_connection *tc);
u32 rds_tcp_snd_una(struct rds_tcp_connection *tc);
u64 rds_tcp_map_seq(struct rds_tcp_connection *tc, u32 seq);
extern struct rds_transport rds_tcp_transport;

/* tcp_connect.c */
int rds_tcp_conn_connect(struct rds_connection *conn);
void rds_tcp_conn_shutdown(struct rds_connection *conn);
void rds_tcp_state_change(struct sock *sk);

/* tcp_listen.c */
int __init rds_tcp_listen_init(void);
void rds_tcp_listen_stop(void);
void rds_tcp_listen_data_ready(struct sock *sk, int bytes);

/* tcp_recv.c */
int __init rds_tcp_recv_init(void);
void rds_tcp_recv_exit(void);
void rds_tcp_data_ready(struct sock *sk, int bytes);
int rds_tcp_recv(struct rds_connection *conn);
void rds_tcp_inc_purge(struct rds_incoming *inc);
void rds_tcp_inc_free(struct rds_incoming *inc);
int rds_tcp_inc_copy_to_user(struct rds_incoming *inc, struct iovec *iov,
			     size_t size);

/* tcp_send.c */
void rds_tcp_xmit_prepare(struct rds_connection *conn);
void rds_tcp_xmit_complete(struct rds_connection *conn);
int rds_tcp_xmit(struct rds_connection *conn, struct rds_message *rm,
	         unsigned int hdr_off, unsigned int sg, unsigned int off);
void rds_tcp_write_space(struct sock *sk);
int rds_tcp_xmit_cong_map(struct rds_connection *conn,
			  struct rds_cong_map *map, unsigned long offset);

/* tcp_stats.c */
DECLARE_PER_CPU(struct rds_tcp_statistics, rds_tcp_stats);
#define rds_tcp_stats_inc(member) rds_stats_inc_which(rds_tcp_stats, member)
unsigned int rds_tcp_stats_info_copy(struct rds_info_iterator *iter,
				     unsigned int avail);

#endif
