/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Definitions for the SMC module (socket related)
 *
 *  Copyright IBM Corp. 2016
 *
 *  Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */
#ifndef __SMC_H
#define __SMC_H

#include <linux/socket.h>
#include <linux/types.h>
#include <linux/compiler.h> /* __aligned */
#include <net/genetlink.h>
#include <net/sock.h>

#include "smc_ib.h"

#define SMC_V1		1		/* SMC version V1 */
#define SMC_V2		2		/* SMC version V2 */

#define SMC_RELEASE_0 0
#define SMC_RELEASE_1 1
#define SMC_RELEASE	SMC_RELEASE_1 /* the latest release version */

#define SMCPROTO_SMC		0	/* SMC protocol, IPv4 */
#define SMCPROTO_SMC6		1	/* SMC protocol, IPv6 */

#define SMC_MAX_ISM_DEVS	8	/* max # of proposed non-native ISM
					 * devices
					 */
#define SMC_AUTOCORKING_DEFAULT_SIZE	0x10000	/* 64K by default */

extern struct proto smc_proto;
extern struct proto smc_proto6;

#ifdef ATOMIC64_INIT
#define KERNEL_HAS_ATOMIC64
#endif

enum smc_state {		/* possible states of an SMC socket */
	SMC_ACTIVE	= 1,
	SMC_INIT	= 2,
	SMC_CLOSED	= 7,
	SMC_LISTEN	= 10,
	/* normal close */
	SMC_PEERCLOSEWAIT1	= 20,
	SMC_PEERCLOSEWAIT2	= 21,
	SMC_APPFINCLOSEWAIT	= 24,
	SMC_APPCLOSEWAIT1	= 22,
	SMC_APPCLOSEWAIT2	= 23,
	SMC_PEERFINCLOSEWAIT	= 25,
	/* abnormal close */
	SMC_PEERABORTWAIT	= 26,
	SMC_PROCESSABORT	= 27,
};

struct smc_link_group;

struct smc_wr_rx_hdr {	/* common prefix part of LLC and CDC to demultiplex */
	union {
		u8 type;
#if defined(__BIG_ENDIAN_BITFIELD)
		struct {
			u8 llc_version:4,
			   llc_type:4;
		};
#elif defined(__LITTLE_ENDIAN_BITFIELD)
		struct {
			u8 llc_type:4,
			   llc_version:4;
		};
#endif
	};
} __aligned(1);

struct smc_cdc_conn_state_flags {
#if defined(__BIG_ENDIAN_BITFIELD)
	u8	peer_done_writing : 1;	/* Sending done indicator */
	u8	peer_conn_closed : 1;	/* Peer connection closed indicator */
	u8	peer_conn_abort : 1;	/* Abnormal close indicator */
	u8	reserved : 5;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8	reserved : 5;
	u8	peer_conn_abort : 1;
	u8	peer_conn_closed : 1;
	u8	peer_done_writing : 1;
#endif
};

struct smc_cdc_producer_flags {
#if defined(__BIG_ENDIAN_BITFIELD)
	u8	write_blocked : 1;	/* Writing Blocked, no rx buf space */
	u8	urg_data_pending : 1;	/* Urgent Data Pending */
	u8	urg_data_present : 1;	/* Urgent Data Present */
	u8	cons_curs_upd_req : 1;	/* cursor update requested */
	u8	failover_validation : 1;/* message replay due to failover */
	u8	reserved : 3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8	reserved : 3;
	u8	failover_validation : 1;
	u8	cons_curs_upd_req : 1;
	u8	urg_data_present : 1;
	u8	urg_data_pending : 1;
	u8	write_blocked : 1;
#endif
};

/* in host byte order */
union smc_host_cursor {	/* SMC cursor - an offset in an RMBE */
	struct {
		u16	reserved;
		u16	wrap;		/* window wrap sequence number */
		u32	count;		/* cursor (= offset) part */
	};
#ifdef KERNEL_HAS_ATOMIC64
	atomic64_t		acurs;	/* for atomic processing */
#else
	u64			acurs;	/* for atomic processing */
#endif
} __aligned(8);

/* in host byte order, except for flag bitfields in network byte order */
struct smc_host_cdc_msg {		/* Connection Data Control message */
	struct smc_wr_rx_hdr		common; /* .type = 0xFE */
	u8				len;	/* length = 44 */
	u16				seqno;	/* connection seq # */
	u32				token;	/* alert_token */
	union smc_host_cursor		prod;		/* producer cursor */
	union smc_host_cursor		cons;		/* consumer cursor,
							 * piggy backed "ack"
							 */
	struct smc_cdc_producer_flags	prod_flags;	/* conn. tx/rx status */
	struct smc_cdc_conn_state_flags	conn_state_flags; /* peer conn. status*/
	u8				reserved[18];
} __aligned(8);

enum smc_urg_state {
	SMC_URG_VALID	= 1,			/* data present */
	SMC_URG_NOTYET	= 2,			/* data pending */
	SMC_URG_READ	= 3,			/* data was already read */
};

struct smc_mark_woken {
	bool woken;
	void *key;
	wait_queue_entry_t wait_entry;
};

struct smc_connection {
	struct rb_node		alert_node;
	struct smc_link_group	*lgr;		/* link group of connection */
	struct smc_link		*lnk;		/* assigned SMC-R link */
	u32			alert_token_local; /* unique conn. id */
	u8			peer_rmbe_idx;	/* from tcp handshake */
	int			peer_rmbe_size;	/* size of peer rx buffer */
	atomic_t		peer_rmbe_space;/* remaining free bytes in peer
						 * rmbe
						 */
	int			rtoken_idx;	/* idx to peer RMB rkey/addr */

	struct smc_buf_desc	*sndbuf_desc;	/* send buffer descriptor */
	struct smc_buf_desc	*rmb_desc;	/* RMBE descriptor */
	int                     rmbe_size_comp; /* compressed notation */
	int			rmbe_update_limit;
						/* lower limit for consumer
						 * cursor update
						 */

	struct smc_host_cdc_msg	local_tx_ctrl;	/* host byte order staging
						 * buffer for CDC msg send
						 * .prod cf. TCP snd_nxt
						 * .cons cf. TCP sends ack
						 */
	union smc_host_cursor	local_tx_ctrl_fin;
						/* prod crsr - confirmed by peer
						 */
	union smc_host_cursor	tx_curs_prep;	/* tx - prepared data
						 * snd_max..wmem_alloc
						 */
	union smc_host_cursor	tx_curs_sent;	/* tx - sent data
						 * snd_nxt ?
						 */
	union smc_host_cursor	tx_curs_fin;	/* tx - confirmed by peer
						 * snd-wnd-begin ?
						 */
	atomic_t		sndbuf_space;	/* remaining space in sndbuf */
	u16			tx_cdc_seq;	/* sequence # for CDC send */
	u16			tx_cdc_seq_fin;	/* sequence # - tx completed */
	spinlock_t		send_lock;	/* protect wr_sends */
	atomic_t		cdc_pend_tx_wr; /* number of pending tx CDC wqe
						 * - inc when post wqe,
						 * - dec on polled tx cqe
						 */
	wait_queue_head_t	cdc_pend_tx_wq; /* wakeup on no cdc_pend_tx_wr*/
	atomic_t		tx_pushing;     /* nr_threads trying tx push */
	struct delayed_work	tx_work;	/* retry of smc_cdc_msg_send */
	u32			tx_off;		/* base offset in peer rmb */

	struct smc_host_cdc_msg	local_rx_ctrl;	/* filled during event_handl.
						 * .prod cf. TCP rcv_nxt
						 * .cons cf. TCP snd_una
						 */
	union smc_host_cursor	rx_curs_confirmed; /* confirmed to peer
						    * source of snd_una ?
						    */
	union smc_host_cursor	urg_curs;	/* points at urgent byte */
	enum smc_urg_state	urg_state;
	bool			urg_tx_pend;	/* urgent data staged */
	bool			urg_rx_skip_pend;
						/* indicate urgent oob data
						 * read, but previous regular
						 * data still pending
						 */
	char			urg_rx_byte;	/* urgent byte */
	bool			tx_in_release_sock;
						/* flush pending tx data in
						 * sock release_cb()
						 */
	atomic_t		bytes_to_rcv;	/* arrived data,
						 * not yet received
						 */
	atomic_t		splice_pending;	/* number of spliced bytes
						 * pending processing
						 */
#ifndef KERNEL_HAS_ATOMIC64
	spinlock_t		acurs_lock;	/* protect cursors */
#endif
	struct work_struct	close_work;	/* peer sent some closing */
	struct work_struct	abort_work;	/* abort the connection */
	struct tasklet_struct	rx_tsklet;	/* Receiver tasklet for SMC-D */
	u8			rx_off;		/* receive offset:
						 * 0 for SMC-R, 32 for SMC-D
						 */
	u64			peer_token;	/* SMC-D token of peer */
	u8			killed : 1;	/* abnormal termination */
	u8			freed : 1;	/* normal termiation */
	u8			out_of_sync : 1; /* out of sync with peer */
};

struct smc_sock {				/* smc sock container */
	struct sock		sk;
	struct socket		*clcsock;	/* internal tcp socket */
	void			(*clcsk_state_change)(struct sock *sk);
						/* original stat_change fct. */
	void			(*clcsk_data_ready)(struct sock *sk);
						/* original data_ready fct. */
	void			(*clcsk_write_space)(struct sock *sk);
						/* original write_space fct. */
	void			(*clcsk_error_report)(struct sock *sk);
						/* original error_report fct. */
	struct smc_connection	conn;		/* smc connection */
	struct smc_sock		*listen_smc;	/* listen parent */
	struct work_struct	connect_work;	/* handle non-blocking connect*/
	struct work_struct	tcp_listen_work;/* handle tcp socket accepts */
	struct work_struct	smc_listen_work;/* prepare new accept socket */
	struct list_head	accept_q;	/* sockets to be accepted */
	spinlock_t		accept_q_lock;	/* protects accept_q */
	bool			limit_smc_hs;	/* put constraint on handshake */
	bool			use_fallback;	/* fallback to tcp */
	int			fallback_rsn;	/* reason for fallback */
	u32			peer_diagnosis; /* decline reason from peer */
	atomic_t                queued_smc_hs;  /* queued smc handshakes */
	struct inet_connection_sock_af_ops		af_ops;
	const struct inet_connection_sock_af_ops	*ori_af_ops;
						/* original af ops */
	int			sockopt_defer_accept;
						/* sockopt TCP_DEFER_ACCEPT
						 * value
						 */
	u8			wait_close_tx_prepared : 1;
						/* shutdown wr or close
						 * started, waiting for unsent
						 * data to be sent
						 */
	u8			connect_nonblock : 1;
						/* non-blocking connect in
						 * flight
						 */
	struct mutex            clcsock_release_lock;
						/* protects clcsock of a listen
						 * socket
						 * */
};

#define smc_sk(ptr) container_of_const(ptr, struct smc_sock, sk)

static inline void smc_init_saved_callbacks(struct smc_sock *smc)
{
	smc->clcsk_state_change	= NULL;
	smc->clcsk_data_ready	= NULL;
	smc->clcsk_write_space	= NULL;
	smc->clcsk_error_report	= NULL;
}

static inline struct smc_sock *smc_clcsock_user_data(const struct sock *clcsk)
{
	return (struct smc_sock *)
	       ((uintptr_t)clcsk->sk_user_data & ~SK_USER_DATA_NOCOPY);
}

/* save target_cb in saved_cb, and replace target_cb with new_cb */
static inline void smc_clcsock_replace_cb(void (**target_cb)(struct sock *),
					  void (*new_cb)(struct sock *),
					  void (**saved_cb)(struct sock *))
{
	/* only save once */
	if (!*saved_cb)
		*saved_cb = *target_cb;
	*target_cb = new_cb;
}

/* restore target_cb to saved_cb, and reset saved_cb to NULL */
static inline void smc_clcsock_restore_cb(void (**target_cb)(struct sock *),
					  void (**saved_cb)(struct sock *))
{
	if (!*saved_cb)
		return;
	*target_cb = *saved_cb;
	*saved_cb = NULL;
}

extern struct workqueue_struct	*smc_hs_wq;	/* wq for handshake work */
extern struct workqueue_struct	*smc_close_wq;	/* wq for close work */

#define SMC_SYSTEMID_LEN		8

extern u8	local_systemid[SMC_SYSTEMID_LEN]; /* unique system identifier */

#define ntohll(x) be64_to_cpu(x)
#define htonll(x) cpu_to_be64(x)

/* convert an u32 value into network byte order, store it into a 3 byte field */
static inline void hton24(u8 *net, u32 host)
{
	__be32 t;

	t = cpu_to_be32(host);
	memcpy(net, ((u8 *)&t) + 1, 3);
}

/* convert a received 3 byte field into host byte order*/
static inline u32 ntoh24(u8 *net)
{
	__be32 t = 0;

	memcpy(((u8 *)&t) + 1, net, 3);
	return be32_to_cpu(t);
}

#ifdef CONFIG_XFRM
static inline bool using_ipsec(struct smc_sock *smc)
{
	return (smc->clcsock->sk->sk_policy[0] ||
		smc->clcsock->sk->sk_policy[1]) ? true : false;
}
#else
static inline bool using_ipsec(struct smc_sock *smc)
{
	return false;
}
#endif

struct smc_gidlist;

struct sock *smc_accept_dequeue(struct sock *parent, struct socket *new_sock);
void smc_close_non_accepted(struct sock *sk);
void smc_fill_gid_list(struct smc_link_group *lgr,
		       struct smc_gidlist *gidlist,
		       struct smc_ib_device *known_dev, u8 *known_gid);

/* smc handshake limitation interface for netlink  */
int smc_nl_dump_hs_limitation(struct sk_buff *skb, struct netlink_callback *cb);
int smc_nl_enable_hs_limitation(struct sk_buff *skb, struct genl_info *info);
int smc_nl_disable_hs_limitation(struct sk_buff *skb, struct genl_info *info);

static inline void smc_sock_set_flag(struct sock *sk, enum sock_flags flag)
{
	set_bit(flag, &sk->sk_flags);
}

#endif	/* __SMC_H */
