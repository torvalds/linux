/* SPDX-License-Identifier: GPL-2.0 */
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#ifndef __MPTCP_PROTOCOL_H
#define __MPTCP_PROTOCOL_H

#include <linux/random.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <uapi/linux/mptcp.h>

#define MPTCP_SUPPORTED_VERSION	1

/* MPTCP option bits */
#define OPTION_MPTCP_MPC_SYN	BIT(0)
#define OPTION_MPTCP_MPC_SYNACK	BIT(1)
#define OPTION_MPTCP_MPC_ACK	BIT(2)
#define OPTION_MPTCP_MPJ_SYN	BIT(3)
#define OPTION_MPTCP_MPJ_SYNACK	BIT(4)
#define OPTION_MPTCP_MPJ_ACK	BIT(5)
#define OPTION_MPTCP_ADD_ADDR	BIT(6)
#define OPTION_MPTCP_RM_ADDR	BIT(7)
#define OPTION_MPTCP_FASTCLOSE	BIT(8)
#define OPTION_MPTCP_PRIO	BIT(9)
#define OPTION_MPTCP_RST	BIT(10)
#define OPTION_MPTCP_DSS	BIT(11)
#define OPTION_MPTCP_FAIL	BIT(12)

#define OPTION_MPTCP_CSUMREQD	BIT(13)

#define OPTIONS_MPTCP_MPC	(OPTION_MPTCP_MPC_SYN | OPTION_MPTCP_MPC_SYNACK | \
				 OPTION_MPTCP_MPC_ACK)
#define OPTIONS_MPTCP_MPJ	(OPTION_MPTCP_MPJ_SYN | OPTION_MPTCP_MPJ_SYNACK | \
				 OPTION_MPTCP_MPJ_ACK)

/* MPTCP option subtypes */
#define MPTCPOPT_MP_CAPABLE	0
#define MPTCPOPT_MP_JOIN	1
#define MPTCPOPT_DSS		2
#define MPTCPOPT_ADD_ADDR	3
#define MPTCPOPT_RM_ADDR	4
#define MPTCPOPT_MP_PRIO	5
#define MPTCPOPT_MP_FAIL	6
#define MPTCPOPT_MP_FASTCLOSE	7
#define MPTCPOPT_RST		8

/* MPTCP suboption lengths */
#define TCPOLEN_MPTCP_MPC_SYN		4
#define TCPOLEN_MPTCP_MPC_SYNACK	12
#define TCPOLEN_MPTCP_MPC_ACK		20
#define TCPOLEN_MPTCP_MPC_ACK_DATA	22
#define TCPOLEN_MPTCP_MPJ_SYN		12
#define TCPOLEN_MPTCP_MPJ_SYNACK	16
#define TCPOLEN_MPTCP_MPJ_ACK		24
#define TCPOLEN_MPTCP_DSS_BASE		4
#define TCPOLEN_MPTCP_DSS_ACK32		4
#define TCPOLEN_MPTCP_DSS_ACK64		8
#define TCPOLEN_MPTCP_DSS_MAP32		10
#define TCPOLEN_MPTCP_DSS_MAP64		14
#define TCPOLEN_MPTCP_DSS_CHECKSUM	2
#define TCPOLEN_MPTCP_ADD_ADDR		16
#define TCPOLEN_MPTCP_ADD_ADDR_PORT	18
#define TCPOLEN_MPTCP_ADD_ADDR_BASE	8
#define TCPOLEN_MPTCP_ADD_ADDR_BASE_PORT	10
#define TCPOLEN_MPTCP_ADD_ADDR6		28
#define TCPOLEN_MPTCP_ADD_ADDR6_PORT	30
#define TCPOLEN_MPTCP_ADD_ADDR6_BASE	20
#define TCPOLEN_MPTCP_ADD_ADDR6_BASE_PORT	22
#define TCPOLEN_MPTCP_PORT_LEN		2
#define TCPOLEN_MPTCP_PORT_ALIGN	2
#define TCPOLEN_MPTCP_RM_ADDR_BASE	3
#define TCPOLEN_MPTCP_PRIO		3
#define TCPOLEN_MPTCP_PRIO_ALIGN	4
#define TCPOLEN_MPTCP_FASTCLOSE		12
#define TCPOLEN_MPTCP_RST		4
#define TCPOLEN_MPTCP_FAIL		12

#define TCPOLEN_MPTCP_MPC_ACK_DATA_CSUM	(TCPOLEN_MPTCP_DSS_CHECKSUM + TCPOLEN_MPTCP_MPC_ACK_DATA)

/* MPTCP MP_JOIN flags */
#define MPTCPOPT_BACKUP		BIT(0)
#define MPTCPOPT_HMAC_LEN	20
#define MPTCPOPT_THMAC_LEN	8

/* MPTCP MP_CAPABLE flags */
#define MPTCP_VERSION_MASK	(0x0F)
#define MPTCP_CAP_CHECKSUM_REQD	BIT(7)
#define MPTCP_CAP_EXTENSIBILITY	BIT(6)
#define MPTCP_CAP_DENY_JOIN_ID0	BIT(5)
#define MPTCP_CAP_HMAC_SHA256	BIT(0)
#define MPTCP_CAP_FLAG_MASK	(0x1F)

/* MPTCP DSS flags */
#define MPTCP_DSS_DATA_FIN	BIT(4)
#define MPTCP_DSS_DSN64		BIT(3)
#define MPTCP_DSS_HAS_MAP	BIT(2)
#define MPTCP_DSS_ACK64		BIT(1)
#define MPTCP_DSS_HAS_ACK	BIT(0)
#define MPTCP_DSS_FLAG_MASK	(0x1F)

/* MPTCP ADD_ADDR flags */
#define MPTCP_ADDR_ECHO		BIT(0)

/* MPTCP MP_PRIO flags */
#define MPTCP_PRIO_BKUP		BIT(0)

/* MPTCP TCPRST flags */
#define MPTCP_RST_TRANSIENT	BIT(0)

/* MPTCP socket flags */
#define MPTCP_DATA_READY	0
#define MPTCP_NOSPACE		1
#define MPTCP_WORK_RTX		2
#define MPTCP_WORK_EOF		3
#define MPTCP_FALLBACK_DONE	4
#define MPTCP_WORK_CLOSE_SUBFLOW 5
#define MPTCP_PUSH_PENDING	6
#define MPTCP_CLEAN_UNA		7
#define MPTCP_ERROR_REPORT	8
#define MPTCP_RETRANSMIT	9
#define MPTCP_WORK_SYNC_SETSOCKOPT 10
#define MPTCP_CONNECTED		11

static inline bool before64(__u64 seq1, __u64 seq2)
{
	return (__s64)(seq1 - seq2) < 0;
}

#define after64(seq2, seq1)	before64(seq1, seq2)

struct mptcp_options_received {
	u64	sndr_key;
	u64	rcvr_key;
	u64	data_ack;
	u64	data_seq;
	u32	subflow_seq;
	u16	data_len;
	__sum16	csum;
	u16	suboptions;
	u32	token;
	u32	nonce;
	u16	use_map:1,
		dsn64:1,
		data_fin:1,
		use_ack:1,
		ack64:1,
		mpc_map:1,
		reset_reason:4,
		reset_transient:1,
		echo:1,
		backup:1,
		deny_join_id0:1,
		__unused:2;
	u8	join_id;
	u64	thmac;
	u8	hmac[MPTCPOPT_HMAC_LEN];
	struct mptcp_addr_info addr;
	struct mptcp_rm_list rm_list;
	u64	ahmac;
	u64	fail_seq;
};

static inline __be32 mptcp_option(u8 subopt, u8 len, u8 nib, u8 field)
{
	return htonl((TCPOPT_MPTCP << 24) | (len << 16) | (subopt << 12) |
		     ((nib & 0xF) << 8) | field);
}

enum mptcp_pm_status {
	MPTCP_PM_ADD_ADDR_RECEIVED,
	MPTCP_PM_ADD_ADDR_SEND_ACK,
	MPTCP_PM_RM_ADDR_RECEIVED,
	MPTCP_PM_ESTABLISHED,
	MPTCP_PM_ALREADY_ESTABLISHED,	/* persistent status, set after ESTABLISHED event */
	MPTCP_PM_SUBFLOW_ESTABLISHED,
};

enum mptcp_addr_signal_status {
	MPTCP_ADD_ADDR_SIGNAL,
	MPTCP_ADD_ADDR_ECHO,
	MPTCP_RM_ADDR_SIGNAL,
};

struct mptcp_pm_data {
	struct mptcp_addr_info local;
	struct mptcp_addr_info remote;
	struct list_head anno_list;

	spinlock_t	lock;		/*protects the whole PM data */

	u8		addr_signal;
	bool		server_side;
	bool		work_pending;
	bool		accept_addr;
	bool		accept_subflow;
	bool		remote_deny_join_id0;
	u8		add_addr_signaled;
	u8		add_addr_accepted;
	u8		local_addr_used;
	u8		subflows;
	u8		status;
	struct mptcp_rm_list rm_list_tx;
	struct mptcp_rm_list rm_list_rx;
};

struct mptcp_data_frag {
	struct list_head list;
	u64 data_seq;
	u16 data_len;
	u16 offset;
	u16 overhead;
	u16 already_sent;
	struct page *page;
};

/* MPTCP connection sock */
struct mptcp_sock {
	/* inet_connection_sock must be the first member */
	struct inet_connection_sock sk;
	u64		local_key;
	u64		remote_key;
	u64		write_seq;
	u64		snd_nxt;
	u64		ack_seq;
	u64		rcv_wnd_sent;
	u64		rcv_data_fin_seq;
	int		rmem_fwd_alloc;
	struct sock	*last_snd;
	int		snd_burst;
	int		old_wspace;
	u64		recovery_snd_nxt;	/* in recovery mode accept up to this seq;
						 * recovery related fields are under data_lock
						 * protection
						 */
	u64		snd_una;
	u64		wnd_end;
	unsigned long	timer_ival;
	u32		token;
	int		rmem_released;
	unsigned long	flags;
	bool		recovery;		/* closing subflow write queue reinjected */
	bool		can_ack;
	bool		fully_established;
	bool		rcv_data_fin;
	bool		snd_data_fin_enable;
	bool		rcv_fastclose;
	bool		use_64bit_ack; /* Set when we received a 64-bit DSN */
	bool		csum_enabled;
	spinlock_t	join_list_lock;
	struct work_struct work;
	struct sk_buff  *ooo_last_skb;
	struct rb_root  out_of_order_queue;
	struct sk_buff_head receive_queue;
	struct list_head conn_list;
	struct list_head rtx_queue;
	struct mptcp_data_frag *first_pending;
	struct list_head join_list;
	struct socket	*subflow; /* outgoing connect/listener/!mp_capable */
	struct sock	*first;
	struct mptcp_pm_data	pm;
	struct {
		u32	space;	/* bytes copied in last measurement window */
		u32	copied; /* bytes copied in this measurement window */
		u64	time;	/* start time of measurement window */
		u64	rtt_us; /* last maximum rtt of subflows */
	} rcvq_space;

	u32 setsockopt_seq;
	char		ca_name[TCP_CA_NAME_MAX];
};

#define mptcp_data_lock(sk) spin_lock_bh(&(sk)->sk_lock.slock)
#define mptcp_data_unlock(sk) spin_unlock_bh(&(sk)->sk_lock.slock)

#define mptcp_for_each_subflow(__msk, __subflow)			\
	list_for_each_entry(__subflow, &((__msk)->conn_list), node)

static inline void msk_owned_by_me(const struct mptcp_sock *msk)
{
	sock_owned_by_me((const struct sock *)msk);
}

static inline struct mptcp_sock *mptcp_sk(const struct sock *sk)
{
	return (struct mptcp_sock *)sk;
}

/* the msk socket don't use the backlog, also account for the bulk
 * free memory
 */
static inline int __mptcp_rmem(const struct sock *sk)
{
	return atomic_read(&sk->sk_rmem_alloc) - READ_ONCE(mptcp_sk(sk)->rmem_released);
}

static inline int __mptcp_space(const struct sock *sk)
{
	return tcp_win_from_space(sk, READ_ONCE(sk->sk_rcvbuf) - __mptcp_rmem(sk));
}

static inline struct mptcp_data_frag *mptcp_send_head(const struct sock *sk)
{
	const struct mptcp_sock *msk = mptcp_sk(sk);

	return READ_ONCE(msk->first_pending);
}

static inline struct mptcp_data_frag *mptcp_send_next(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct mptcp_data_frag *cur;

	cur = msk->first_pending;
	return list_is_last(&cur->list, &msk->rtx_queue) ? NULL :
						     list_next_entry(cur, list);
}

static inline struct mptcp_data_frag *mptcp_pending_tail(const struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (!msk->first_pending)
		return NULL;

	if (WARN_ON_ONCE(list_empty(&msk->rtx_queue)))
		return NULL;

	return list_last_entry(&msk->rtx_queue, struct mptcp_data_frag, list);
}

static inline struct mptcp_data_frag *mptcp_rtx_head(const struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (msk->snd_una == READ_ONCE(msk->snd_nxt))
		return NULL;

	return list_first_entry_or_null(&msk->rtx_queue, struct mptcp_data_frag, list);
}

struct csum_pseudo_header {
	__be64 data_seq;
	__be32 subflow_seq;
	__be16 data_len;
	__sum16 csum;
};

struct mptcp_subflow_request_sock {
	struct	tcp_request_sock sk;
	u16	mp_capable : 1,
		mp_join : 1,
		backup : 1,
		csum_reqd : 1,
		allow_join_id0 : 1;
	u8	local_id;
	u8	remote_id;
	u64	local_key;
	u64	idsn;
	u32	token;
	u32	ssn_offset;
	u64	thmac;
	u32	local_nonce;
	u32	remote_nonce;
	struct mptcp_sock	*msk;
	struct hlist_nulls_node token_node;
};

static inline struct mptcp_subflow_request_sock *
mptcp_subflow_rsk(const struct request_sock *rsk)
{
	return (struct mptcp_subflow_request_sock *)rsk;
}

enum mptcp_data_avail {
	MPTCP_SUBFLOW_NODATA,
	MPTCP_SUBFLOW_DATA_AVAIL,
};

struct mptcp_delegated_action {
	struct napi_struct napi;
	struct list_head head;
};

DECLARE_PER_CPU(struct mptcp_delegated_action, mptcp_delegated_actions);

#define MPTCP_DELEGATE_SEND		0

/* MPTCP subflow context */
struct mptcp_subflow_context {
	struct	list_head node;/* conn_list of subflows */
	u64	local_key;
	u64	remote_key;
	u64	idsn;
	u64	map_seq;
	u32	snd_isn;
	u32	token;
	u32	rel_write_seq;
	u32	map_subflow_seq;
	u32	ssn_offset;
	u32	map_data_len;
	__wsum	map_data_csum;
	u32	map_csum_len;
	u32	request_mptcp : 1,  /* send MP_CAPABLE */
		request_join : 1,   /* send MP_JOIN */
		request_bkup : 1,
		mp_capable : 1,	    /* remote is MPTCP capable */
		mp_join : 1,	    /* remote is JOINing */
		fully_established : 1,	    /* path validated */
		pm_notified : 1,    /* PM hook called for established status */
		conn_finished : 1,
		map_valid : 1,
		map_csum_reqd : 1,
		map_data_fin : 1,
		mpc_map : 1,
		backup : 1,
		send_mp_prio : 1,
		send_mp_fail : 1,
		rx_eof : 1,
		can_ack : 1,        /* only after processing the remote a key */
		disposable : 1,	    /* ctx can be free at ulp release time */
		stale : 1;	    /* unable to snd/rcv data, do not use for xmit */
	enum mptcp_data_avail data_avail;
	u32	remote_nonce;
	u64	thmac;
	u32	local_nonce;
	u32	remote_token;
	u8	hmac[MPTCPOPT_HMAC_LEN];
	u8	local_id;
	u8	remote_id;
	u8	reset_seen:1;
	u8	reset_transient:1;
	u8	reset_reason:4;
	u8	stale_count;

	long	delegated_status;
	struct	list_head delegated_node;   /* link into delegated_action, protected by local BH */

	u32	setsockopt_seq;
	u32	stale_rcv_tstamp;

	struct	sock *tcp_sock;	    /* tcp sk backpointer */
	struct	sock *conn;	    /* parent mptcp_sock */
	const	struct inet_connection_sock_af_ops *icsk_af_ops;
	void	(*tcp_data_ready)(struct sock *sk);
	void	(*tcp_state_change)(struct sock *sk);
	void	(*tcp_write_space)(struct sock *sk);
	void	(*tcp_error_report)(struct sock *sk);

	struct	rcu_head rcu;
};

static inline struct mptcp_subflow_context *
mptcp_subflow_ctx(const struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	/* Use RCU on icsk_ulp_data only for sock diag code */
	return (__force struct mptcp_subflow_context *)icsk->icsk_ulp_data;
}

static inline struct sock *
mptcp_subflow_tcp_sock(const struct mptcp_subflow_context *subflow)
{
	return subflow->tcp_sock;
}

static inline u64
mptcp_subflow_get_map_offset(const struct mptcp_subflow_context *subflow)
{
	return tcp_sk(mptcp_subflow_tcp_sock(subflow))->copied_seq -
		      subflow->ssn_offset -
		      subflow->map_subflow_seq;
}

static inline u64
mptcp_subflow_get_mapped_dsn(const struct mptcp_subflow_context *subflow)
{
	return subflow->map_seq + mptcp_subflow_get_map_offset(subflow);
}

static inline void mptcp_add_pending_subflow(struct mptcp_sock *msk,
					     struct mptcp_subflow_context *subflow)
{
	sock_hold(mptcp_subflow_tcp_sock(subflow));
	spin_lock_bh(&msk->join_list_lock);
	list_add_tail(&subflow->node, &msk->join_list);
	spin_unlock_bh(&msk->join_list_lock);
}

void mptcp_subflow_process_delegated(struct sock *ssk);

static inline void mptcp_subflow_delegate(struct mptcp_subflow_context *subflow)
{
	struct mptcp_delegated_action *delegated;
	bool schedule;

	/* The implied barrier pairs with mptcp_subflow_delegated_done(), and
	 * ensures the below list check sees list updates done prior to status
	 * bit changes
	 */
	if (!test_and_set_bit(MPTCP_DELEGATE_SEND, &subflow->delegated_status)) {
		/* still on delegated list from previous scheduling */
		if (!list_empty(&subflow->delegated_node))
			return;

		/* the caller held the subflow bh socket lock */
		lockdep_assert_in_softirq();

		delegated = this_cpu_ptr(&mptcp_delegated_actions);
		schedule = list_empty(&delegated->head);
		list_add_tail(&subflow->delegated_node, &delegated->head);
		sock_hold(mptcp_subflow_tcp_sock(subflow));
		if (schedule)
			napi_schedule(&delegated->napi);
	}
}

static inline struct mptcp_subflow_context *
mptcp_subflow_delegated_next(struct mptcp_delegated_action *delegated)
{
	struct mptcp_subflow_context *ret;

	if (list_empty(&delegated->head))
		return NULL;

	ret = list_first_entry(&delegated->head, struct mptcp_subflow_context, delegated_node);
	list_del_init(&ret->delegated_node);
	return ret;
}

static inline bool mptcp_subflow_has_delegated_action(const struct mptcp_subflow_context *subflow)
{
	return test_bit(MPTCP_DELEGATE_SEND, &subflow->delegated_status);
}

static inline void mptcp_subflow_delegated_done(struct mptcp_subflow_context *subflow)
{
	/* pairs with mptcp_subflow_delegate, ensures delegate_node is updated before
	 * touching the status bit
	 */
	smp_wmb();
	clear_bit(MPTCP_DELEGATE_SEND, &subflow->delegated_status);
}

int mptcp_is_enabled(const struct net *net);
unsigned int mptcp_get_add_addr_timeout(const struct net *net);
int mptcp_is_checksum_enabled(const struct net *net);
int mptcp_allow_join_id0(const struct net *net);
unsigned int mptcp_stale_loss_cnt(const struct net *net);
void mptcp_subflow_fully_established(struct mptcp_subflow_context *subflow,
				     struct mptcp_options_received *mp_opt);
bool __mptcp_retransmit_pending_data(struct sock *sk);
void __mptcp_push_pending(struct sock *sk, unsigned int flags);
bool mptcp_subflow_data_available(struct sock *sk);
void __init mptcp_subflow_init(void);
void mptcp_subflow_shutdown(struct sock *sk, struct sock *ssk, int how);
void mptcp_close_ssk(struct sock *sk, struct sock *ssk,
		     struct mptcp_subflow_context *subflow);
void mptcp_subflow_send_ack(struct sock *ssk);
void mptcp_subflow_reset(struct sock *ssk);
void mptcp_sock_graft(struct sock *sk, struct socket *parent);
struct socket *__mptcp_nmpc_socket(const struct mptcp_sock *msk);

/* called with sk socket lock held */
int __mptcp_subflow_connect(struct sock *sk, const struct mptcp_addr_info *loc,
			    const struct mptcp_addr_info *remote);
int mptcp_subflow_create_socket(struct sock *sk, struct socket **new_sock);
void mptcp_info2sockaddr(const struct mptcp_addr_info *info,
			 struct sockaddr_storage *addr,
			 unsigned short family);

static inline bool __mptcp_subflow_active(struct mptcp_subflow_context *subflow)
{
	struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

	/* can't send if JOIN hasn't completed yet (i.e. is usable for mptcp) */
	if (subflow->request_join && !subflow->fully_established)
		return false;

	/* only send if our side has not closed yet */
	return ((1 << ssk->sk_state) & (TCPF_ESTABLISHED | TCPF_CLOSE_WAIT));
}

void mptcp_subflow_set_active(struct mptcp_subflow_context *subflow);

bool mptcp_subflow_active(struct mptcp_subflow_context *subflow);

static inline void mptcp_subflow_tcp_fallback(struct sock *sk,
					      struct mptcp_subflow_context *ctx)
{
	sk->sk_data_ready = ctx->tcp_data_ready;
	sk->sk_state_change = ctx->tcp_state_change;
	sk->sk_write_space = ctx->tcp_write_space;
	sk->sk_error_report = ctx->tcp_error_report;

	inet_csk(sk)->icsk_af_ops = ctx->icsk_af_ops;
}

static inline bool mptcp_has_another_subflow(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk), *tmp;
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);

	mptcp_for_each_subflow(msk, tmp) {
		if (tmp != subflow)
			return true;
	}

	return false;
}

void __init mptcp_proto_init(void);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
int __init mptcp_proto_v6_init(void);
#endif

struct sock *mptcp_sk_clone(const struct sock *sk,
			    const struct mptcp_options_received *mp_opt,
			    struct request_sock *req);
void mptcp_get_options(const struct sock *sk,
		       const struct sk_buff *skb,
		       struct mptcp_options_received *mp_opt);

void mptcp_finish_connect(struct sock *sk);
void __mptcp_set_connected(struct sock *sk);
static inline bool mptcp_is_fully_established(struct sock *sk)
{
	return inet_sk_state_load(sk) == TCP_ESTABLISHED &&
	       READ_ONCE(mptcp_sk(sk)->fully_established);
}
void mptcp_rcv_space_init(struct mptcp_sock *msk, const struct sock *ssk);
void mptcp_data_ready(struct sock *sk, struct sock *ssk);
bool mptcp_finish_join(struct sock *sk);
bool mptcp_schedule_work(struct sock *sk);
int mptcp_setsockopt(struct sock *sk, int level, int optname,
		     sockptr_t optval, unsigned int optlen);
int mptcp_getsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int __user *option);

u64 __mptcp_expand_seq(u64 old_seq, u64 cur_seq);
static inline u64 mptcp_expand_seq(u64 old_seq, u64 cur_seq, bool use_64bit)
{
	if (use_64bit)
		return cur_seq;

	return __mptcp_expand_seq(old_seq, cur_seq);
}
void __mptcp_check_push(struct sock *sk, struct sock *ssk);
void __mptcp_data_acked(struct sock *sk);
void __mptcp_error_report(struct sock *sk);
void mptcp_subflow_eof(struct sock *sk);
bool mptcp_update_rcv_data_fin(struct mptcp_sock *msk, u64 data_fin_seq, bool use_64bit);
void __mptcp_flush_join_list(struct mptcp_sock *msk);
static inline bool mptcp_data_fin_enabled(const struct mptcp_sock *msk)
{
	return READ_ONCE(msk->snd_data_fin_enable) &&
	       READ_ONCE(msk->write_seq) == READ_ONCE(msk->snd_nxt);
}

static inline bool mptcp_propagate_sndbuf(struct sock *sk, struct sock *ssk)
{
	if ((sk->sk_userlocks & SOCK_SNDBUF_LOCK) || ssk->sk_sndbuf <= READ_ONCE(sk->sk_sndbuf))
		return false;

	WRITE_ONCE(sk->sk_sndbuf, ssk->sk_sndbuf);
	return true;
}

static inline void mptcp_write_space(struct sock *sk)
{
	if (sk_stream_is_writeable(sk)) {
		/* pairs with memory barrier in mptcp_poll */
		smp_mb();
		if (test_and_clear_bit(MPTCP_NOSPACE, &mptcp_sk(sk)->flags))
			sk_stream_write_space(sk);
	}
}

void mptcp_destroy_common(struct mptcp_sock *msk);

#define MPTCP_TOKEN_MAX_RETRIES	4

void __init mptcp_token_init(void);
static inline void mptcp_token_init_request(struct request_sock *req)
{
	mptcp_subflow_rsk(req)->token_node.pprev = NULL;
}

int mptcp_token_new_request(struct request_sock *req);
void mptcp_token_destroy_request(struct request_sock *req);
int mptcp_token_new_connect(struct sock *sk);
void mptcp_token_accept(struct mptcp_subflow_request_sock *r,
			struct mptcp_sock *msk);
bool mptcp_token_exists(u32 token);
struct mptcp_sock *mptcp_token_get_sock(struct net *net, u32 token);
struct mptcp_sock *mptcp_token_iter_next(const struct net *net, long *s_slot,
					 long *s_num);
void mptcp_token_destroy(struct mptcp_sock *msk);

void mptcp_crypto_key_sha(u64 key, u32 *token, u64 *idsn);

void mptcp_crypto_hmac_sha(u64 key1, u64 key2, u8 *msg, int len, void *hmac);

void __init mptcp_pm_init(void);
void mptcp_pm_data_init(struct mptcp_sock *msk);
void mptcp_pm_subflow_chk_stale(const struct mptcp_sock *msk, struct sock *ssk);
void mptcp_pm_nl_subflow_chk_stale(const struct mptcp_sock *msk, struct sock *ssk);
void mptcp_pm_new_connection(struct mptcp_sock *msk, const struct sock *ssk, int server_side);
void mptcp_pm_fully_established(struct mptcp_sock *msk, const struct sock *ssk, gfp_t gfp);
bool mptcp_pm_allow_new_subflow(struct mptcp_sock *msk);
void mptcp_pm_connection_closed(struct mptcp_sock *msk);
void mptcp_pm_subflow_established(struct mptcp_sock *msk);
void mptcp_pm_subflow_closed(struct mptcp_sock *msk, u8 id);
void mptcp_pm_add_addr_received(struct mptcp_sock *msk,
				const struct mptcp_addr_info *addr);
void mptcp_pm_add_addr_echoed(struct mptcp_sock *msk,
			      struct mptcp_addr_info *addr);
void mptcp_pm_add_addr_send_ack(struct mptcp_sock *msk);
void mptcp_pm_nl_addr_send_ack(struct mptcp_sock *msk);
void mptcp_pm_rm_addr_received(struct mptcp_sock *msk,
			       const struct mptcp_rm_list *rm_list);
void mptcp_pm_mp_prio_received(struct sock *sk, u8 bkup);
void mptcp_pm_mp_fail_received(struct sock *sk, u64 fail_seq);
void mptcp_pm_free_anno_list(struct mptcp_sock *msk);
bool mptcp_pm_sport_in_anno_list(struct mptcp_sock *msk, const struct sock *sk);
struct mptcp_pm_add_entry *
mptcp_pm_del_add_timer(struct mptcp_sock *msk,
		       struct mptcp_addr_info *addr, bool check_id);
struct mptcp_pm_add_entry *
mptcp_lookup_anno_list_by_saddr(struct mptcp_sock *msk,
				struct mptcp_addr_info *addr);
int mptcp_pm_get_flags_and_ifindex_by_id(struct net *net, unsigned int id,
					 u8 *flags, int *ifindex);

int mptcp_pm_announce_addr(struct mptcp_sock *msk,
			   const struct mptcp_addr_info *addr,
			   bool echo);
int mptcp_pm_remove_addr(struct mptcp_sock *msk, const struct mptcp_rm_list *rm_list);
int mptcp_pm_remove_subflow(struct mptcp_sock *msk, const struct mptcp_rm_list *rm_list);

void mptcp_event(enum mptcp_event_type type, const struct mptcp_sock *msk,
		 const struct sock *ssk, gfp_t gfp);
void mptcp_event_addr_announced(const struct mptcp_sock *msk, const struct mptcp_addr_info *info);
void mptcp_event_addr_removed(const struct mptcp_sock *msk, u8 id);

static inline bool mptcp_pm_should_add_signal(struct mptcp_sock *msk)
{
	return READ_ONCE(msk->pm.addr_signal) &
		(BIT(MPTCP_ADD_ADDR_SIGNAL) | BIT(MPTCP_ADD_ADDR_ECHO));
}

static inline bool mptcp_pm_should_add_signal_addr(struct mptcp_sock *msk)
{
	return READ_ONCE(msk->pm.addr_signal) & BIT(MPTCP_ADD_ADDR_SIGNAL);
}

static inline bool mptcp_pm_should_add_signal_echo(struct mptcp_sock *msk)
{
	return READ_ONCE(msk->pm.addr_signal) & BIT(MPTCP_ADD_ADDR_ECHO);
}

static inline bool mptcp_pm_should_rm_signal(struct mptcp_sock *msk)
{
	return READ_ONCE(msk->pm.addr_signal) & BIT(MPTCP_RM_ADDR_SIGNAL);
}

static inline unsigned int mptcp_add_addr_len(int family, bool echo, bool port)
{
	u8 len = TCPOLEN_MPTCP_ADD_ADDR_BASE;

	if (family == AF_INET6)
		len = TCPOLEN_MPTCP_ADD_ADDR6_BASE;
	if (!echo)
		len += MPTCPOPT_THMAC_LEN;
	/* account for 2 trailing 'nop' options */
	if (port)
		len += TCPOLEN_MPTCP_PORT_LEN + TCPOLEN_MPTCP_PORT_ALIGN;

	return len;
}

static inline int mptcp_rm_addr_len(const struct mptcp_rm_list *rm_list)
{
	if (rm_list->nr == 0 || rm_list->nr > MPTCP_RM_IDS_MAX)
		return -EINVAL;

	return TCPOLEN_MPTCP_RM_ADDR_BASE + roundup(rm_list->nr - 1, 4) + 1;
}

bool mptcp_pm_add_addr_signal(struct mptcp_sock *msk, struct sk_buff *skb,
			      unsigned int opt_size, unsigned int remaining,
			      struct mptcp_addr_info *addr, bool *echo,
			      bool *port, bool *drop_other_suboptions);
bool mptcp_pm_rm_addr_signal(struct mptcp_sock *msk, unsigned int remaining,
			     struct mptcp_rm_list *rm_list);
int mptcp_pm_get_local_id(struct mptcp_sock *msk, struct sock_common *skc);

void __init mptcp_pm_nl_init(void);
void mptcp_pm_nl_data_init(struct mptcp_sock *msk);
void mptcp_pm_nl_work(struct mptcp_sock *msk);
void mptcp_pm_nl_rm_subflow_received(struct mptcp_sock *msk,
				     const struct mptcp_rm_list *rm_list);
int mptcp_pm_nl_get_local_id(struct mptcp_sock *msk, struct sock_common *skc);
unsigned int mptcp_pm_get_add_addr_signal_max(struct mptcp_sock *msk);
unsigned int mptcp_pm_get_add_addr_accept_max(struct mptcp_sock *msk);
unsigned int mptcp_pm_get_subflows_max(struct mptcp_sock *msk);
unsigned int mptcp_pm_get_local_addr_max(struct mptcp_sock *msk);

void mptcp_sockopt_sync(struct mptcp_sock *msk, struct sock *ssk);
void mptcp_sockopt_sync_all(struct mptcp_sock *msk);

static inline struct mptcp_ext *mptcp_get_ext(const struct sk_buff *skb)
{
	return (struct mptcp_ext *)skb_ext_find(skb, SKB_EXT_MPTCP);
}

void mptcp_diag_subflow_init(struct tcp_ulp_ops *ops);

static inline bool __mptcp_check_fallback(const struct mptcp_sock *msk)
{
	return test_bit(MPTCP_FALLBACK_DONE, &msk->flags);
}

static inline bool mptcp_check_fallback(const struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);

	return __mptcp_check_fallback(msk);
}

static inline void __mptcp_do_fallback(struct mptcp_sock *msk)
{
	if (test_bit(MPTCP_FALLBACK_DONE, &msk->flags)) {
		pr_debug("TCP fallback already done (msk=%p)", msk);
		return;
	}
	set_bit(MPTCP_FALLBACK_DONE, &msk->flags);
}

static inline void mptcp_do_fallback(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);

	__mptcp_do_fallback(msk);
}

#define pr_fallback(a) pr_debug("%s:fallback to TCP (msk=%p)", __func__, a)

static inline bool subflow_simultaneous_connect(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct sock *parent = subflow->conn;

	return sk->sk_state == TCP_ESTABLISHED &&
	       !mptcp_sk(parent)->pm.server_side &&
	       !subflow->conn_finished;
}

#ifdef CONFIG_SYN_COOKIES
void subflow_init_req_cookie_join_save(const struct mptcp_subflow_request_sock *subflow_req,
				       struct sk_buff *skb);
bool mptcp_token_join_cookie_init_state(struct mptcp_subflow_request_sock *subflow_req,
					struct sk_buff *skb);
void __init mptcp_join_cookie_init(void);
#else
static inline void
subflow_init_req_cookie_join_save(const struct mptcp_subflow_request_sock *subflow_req,
				  struct sk_buff *skb) {}
static inline bool
mptcp_token_join_cookie_init_state(struct mptcp_subflow_request_sock *subflow_req,
				   struct sk_buff *skb)
{
	return false;
}

static inline void mptcp_join_cookie_init(void) {}
#endif

#endif /* __MPTCP_PROTOCOL_H */
