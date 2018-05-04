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
#include <net/sock.h>

#include "smc_ib.h"

#define SMCPROTO_SMC		0	/* SMC protocol, IPv4 */
#define SMCPROTO_SMC6		1	/* SMC protocol, IPv6 */

#define SMC_MAX_PORTS		2	/* Max # of ports */

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
	u8			type;
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

struct smc_connection {
	struct rb_node		alert_node;
	struct smc_link_group	*lgr;		/* link group of connection */
	u32			alert_token_local; /* unique conn. id */
	u8			peer_conn_idx;	/* from tcp handshake */
	int			peer_rmbe_size;	/* size of peer rx buffer */
	atomic_t		peer_rmbe_space;/* remaining free bytes in peer
						 * rmbe
						 */
	int			rtoken_idx;	/* idx to peer RMB rkey/addr */

	struct smc_buf_desc	*sndbuf_desc;	/* send buffer descriptor */
	int			sndbuf_size;	/* sndbuf size <== sock wmem */
	struct smc_buf_desc	*rmb_desc;	/* RMBE descriptor */
	int			rmbe_size;	/* RMBE size <== sock rmem */
	int			rmbe_size_short;/* compressed notation */
	int			rmbe_update_limit;
						/* lower limit for consumer
						 * cursor update
						 */

	struct smc_host_cdc_msg	local_tx_ctrl;	/* host byte order staging
						 * buffer for CDC msg send
						 * .prod cf. TCP snd_nxt
						 * .cons cf. TCP sends ack
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
	spinlock_t		send_lock;	/* protect wr_sends */
	struct delayed_work	tx_work;	/* retry of smc_cdc_msg_send */

	struct smc_host_cdc_msg	local_rx_ctrl;	/* filled during event_handl.
						 * .prod cf. TCP rcv_nxt
						 * .cons cf. TCP snd_una
						 */
	union smc_host_cursor	rx_curs_confirmed; /* confirmed to peer
						    * source of snd_una ?
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
};

struct smc_sock {				/* smc sock container */
	struct sock		sk;
	struct socket		*clcsock;	/* internal tcp socket */
	struct smc_connection	conn;		/* smc connection */
	struct smc_sock		*listen_smc;	/* listen parent */
	struct work_struct	tcp_listen_work;/* handle tcp socket accepts */
	struct work_struct	smc_listen_work;/* prepare new accept socket */
	struct list_head	accept_q;	/* sockets to be accepted */
	spinlock_t		accept_q_lock;	/* protects accept_q */
	bool			use_fallback;	/* fallback to tcp */
	int			sockopt_defer_accept;
						/* sockopt TCP_DEFER_ACCEPT
						 * value
						 */
	u8			wait_close_tx_prepared : 1;
						/* shutdown wr or close
						 * started, waiting for unsent
						 * data to be sent
						 */
};

static inline struct smc_sock *smc_sk(const struct sock *sk)
{
	return (struct smc_sock *)sk;
}

#define SMC_SYSTEMID_LEN		8

extern u8	local_systemid[SMC_SYSTEMID_LEN]; /* unique system identifier */

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

#define SMC_BUF_MIN_SIZE 16384		/* minimum size of an RMB */

#define SMC_RMBE_SIZES	16	/* number of distinct sizes for an RMBE */
/* theoretically, the RFC states that largest size would be 512K,
 * i.e. compressed 5 and thus 6 sizes (0..5), despite
 * struct smc_clc_msg_accept_confirm.rmbe_size being a 4 bit value (0..15)
 */

/* convert the RMB size into the compressed notation - minimum 16K.
 * In contrast to plain ilog2, this rounds towards the next power of 2,
 * so the socket application gets at least its desired sndbuf / rcvbuf size.
 */
static inline u8 smc_compress_bufsize(int size)
{
	u8 compressed;

	if (size <= SMC_BUF_MIN_SIZE)
		return 0;

	size = (size - 1) >> 14;
	compressed = ilog2(size) + 1;
	if (compressed >= SMC_RMBE_SIZES)
		compressed = SMC_RMBE_SIZES - 1;
	return compressed;
}

/* convert the RMB size from compressed notation into integer */
static inline int smc_uncompress_bufsize(u8 compressed)
{
	u32 size;

	size = 0x00000001 << (((int)compressed) + 14);
	return (int)size;
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

struct smc_clc_msg_local;

void smc_conn_free(struct smc_connection *conn);
int smc_conn_create(struct smc_sock *smc,
		    struct smc_ib_device *smcibdev, u8 ibport,
		    struct smc_clc_msg_local *lcl, int srv_first_contact);
struct sock *smc_accept_dequeue(struct sock *parent, struct socket *new_sock);
void smc_close_non_accepted(struct sock *sk);

#endif	/* __SMC_H */
