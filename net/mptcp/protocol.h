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
#include <net/genetlink.h>
#include <net/rstreason.h>

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

/* MPTCP socket atomic flags */
#define MPTCP_WORK_RTX		1
#define MPTCP_FALLBACK_DONE	2
#define MPTCP_WORK_CLOSE_SUBFLOW 3

/* MPTCP socket release cb flags */
#define MPTCP_PUSH_PENDING	1
#define MPTCP_CLEAN_UNA		2
#define MPTCP_ERROR_REPORT	3
#define MPTCP_RETRANSMIT	4
#define MPTCP_FLUSH_JOIN_LIST	5
#define MPTCP_SYNC_STATE	6
#define MPTCP_SYNC_SNDBUF	7

struct mptcp_skb_cb {
	u64 map_seq;
	u64 end_seq;
	u32 offset;
	u8  has_rxtstamp:1;
};

#define MPTCP_SKB_CB(__skb)	((struct mptcp_skb_cb *)&((__skb)->cb[0]))

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
	MPTCP_PM_SUBFLOW_ESTABLISHED,
	MPTCP_PM_ALREADY_ESTABLISHED,	/* persistent status, set after ESTABLISHED event */
	MPTCP_PM_MPC_ENDPOINT_ACCOUNTED /* persistent status, set after MPC local address is
					 * accounted int id_avail_bitmap
					 */
};

enum mptcp_pm_type {
	MPTCP_PM_TYPE_KERNEL = 0,
	MPTCP_PM_TYPE_USERSPACE,

	__MPTCP_PM_TYPE_NR,
	__MPTCP_PM_TYPE_MAX = __MPTCP_PM_TYPE_NR - 1,
};

/* Status bits below MPTCP_PM_ALREADY_ESTABLISHED need pm worker actions */
#define MPTCP_PM_WORK_MASK ((1 << MPTCP_PM_ALREADY_ESTABLISHED) - 1)

enum mptcp_addr_signal_status {
	MPTCP_ADD_ADDR_SIGNAL,
	MPTCP_ADD_ADDR_ECHO,
	MPTCP_RM_ADDR_SIGNAL,
};

/* max value of mptcp_addr_info.id */
#define MPTCP_PM_MAX_ADDR_ID		U8_MAX

struct mptcp_pm_data {
	struct mptcp_addr_info local;
	struct mptcp_addr_info remote;
	struct list_head anno_list;
	struct list_head userspace_pm_local_addr_list;

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
	u8		pm_type;
	u8		subflows;
	u8		status;
	DECLARE_BITMAP(id_avail_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
	struct mptcp_rm_list rm_list_tx;
	struct mptcp_rm_list rm_list_rx;
};

struct mptcp_pm_addr_entry {
	struct list_head	list;
	struct mptcp_addr_info	addr;
	u8			flags;
	int			ifindex;
	struct socket		*lsk;
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
	u64		local_key;		/* protected by the first subflow socket lock
						 * lockless access read
						 */
	u64		remote_key;		/* same as above */
	u64		write_seq;
	u64		bytes_sent;
	u64		snd_nxt;
	u64		bytes_received;
	u64		ack_seq;
	atomic64_t	rcv_wnd_sent;
	u64		rcv_data_fin_seq;
	u64		bytes_retrans;
	u64		bytes_consumed;
	int		rmem_fwd_alloc;
	int		snd_burst;
	int		old_wspace;
	u64		recovery_snd_nxt;	/* in recovery mode accept up to this seq;
						 * recovery related fields are under data_lock
						 * protection
						 */
	u64		bytes_acked;
	u64		snd_una;
	u64		wnd_end;
	u32		last_data_sent;
	u32		last_data_recv;
	u32		last_ack_recv;
	unsigned long	timer_ival;
	u32		token;
	int		rmem_released;
	unsigned long	flags;
	unsigned long	cb_flags;
	bool		recovery;		/* closing subflow write queue reinjected */
	bool		can_ack;
	bool		fully_established;
	bool		rcv_data_fin;
	bool		snd_data_fin_enable;
	bool		rcv_fastclose;
	bool		use_64bit_ack; /* Set when we received a 64-bit DSN */
	bool		csum_enabled;
	bool		allow_infinite_fallback;
	u8		pending_state; /* A subflow asked to set this sk_state,
					* protected by the msk data lock
					*/
	u8		mpc_endpoint_id;
	u8		recvmsg_inq:1,
			cork:1,
			nodelay:1,
			fastopening:1,
			in_accept_queue:1,
			free_first:1,
			rcvspace_init:1;
	u32		notsent_lowat;
	int		keepalive_cnt;
	int		keepalive_idle;
	int		keepalive_intvl;
	struct work_struct work;
	struct sk_buff  *ooo_last_skb;
	struct rb_root  out_of_order_queue;
	struct sk_buff_head receive_queue;
	struct list_head conn_list;
	struct list_head rtx_queue;
	struct mptcp_data_frag *first_pending;
	struct list_head join_list;
	struct sock	*first; /* The mptcp ops can safely dereference, using suitable
				 * ONCE annotation, the subflow outside the socket
				 * lock as such sock is freed after close().
				 */
	struct mptcp_pm_data	pm;
	struct mptcp_sched_ops	*sched;
	struct {
		u32	space;	/* bytes copied in last measurement window */
		u32	copied; /* bytes copied in this measurement window */
		u64	time;	/* start time of measurement window */
		u64	rtt_us; /* last maximum rtt of subflows */
	} rcvq_space;
	u8		scaling_ratio;

	u32		subflow_id;
	u32		setsockopt_seq;
	char		ca_name[TCP_CA_NAME_MAX];
};

#define mptcp_data_lock(sk) spin_lock_bh(&(sk)->sk_lock.slock)
#define mptcp_data_unlock(sk) spin_unlock_bh(&(sk)->sk_lock.slock)

#define mptcp_for_each_subflow(__msk, __subflow)			\
	list_for_each_entry(__subflow, &((__msk)->conn_list), node)
#define mptcp_for_each_subflow_safe(__msk, __subflow, __tmp)			\
	list_for_each_entry_safe(__subflow, __tmp, &((__msk)->conn_list), node)

extern struct genl_family mptcp_genl_family;

static inline void msk_owned_by_me(const struct mptcp_sock *msk)
{
	sock_owned_by_me((const struct sock *)msk);
}

#ifdef CONFIG_DEBUG_NET
/* MPTCP-specific: we might (indirectly) call this helper with the wrong sk */
#undef tcp_sk
#define tcp_sk(ptr) ({								\
	typeof(ptr) _ptr = (ptr);						\
	WARN_ON(_ptr->sk_protocol != IPPROTO_TCP);				\
	container_of_const(_ptr, struct tcp_sock, inet_conn.icsk_inet.sk);	\
})
#define mptcp_sk(ptr) ({						\
	typeof(ptr) _ptr = (ptr);					\
	WARN_ON(_ptr->sk_protocol != IPPROTO_MPTCP);			\
	container_of_const(_ptr, struct mptcp_sock, sk.icsk_inet.sk);	\
})

#else /* !CONFIG_DEBUG_NET */
#define mptcp_sk(ptr) container_of_const(ptr, struct mptcp_sock, sk.icsk_inet.sk)
#endif

/* the msk socket don't use the backlog, also account for the bulk
 * free memory
 */
static inline int __mptcp_rmem(const struct sock *sk)
{
	return atomic_read(&sk->sk_rmem_alloc) - READ_ONCE(mptcp_sk(sk)->rmem_released);
}

static inline int mptcp_win_from_space(const struct sock *sk, int space)
{
	return __tcp_win_from_space(mptcp_sk(sk)->scaling_ratio, space);
}

static inline int __mptcp_space(const struct sock *sk)
{
	return mptcp_win_from_space(sk, READ_ONCE(sk->sk_rcvbuf) - __mptcp_rmem(sk));
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
	const struct mptcp_sock *msk = mptcp_sk(sk);

	if (!msk->first_pending)
		return NULL;

	if (WARN_ON_ONCE(list_empty(&msk->rtx_queue)))
		return NULL;

	return list_last_entry(&msk->rtx_queue, struct mptcp_data_frag, list);
}

static inline struct mptcp_data_frag *mptcp_rtx_head(struct sock *sk)
{
	struct mptcp_sock *msk = mptcp_sk(sk);

	if (msk->snd_una == msk->snd_nxt)
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

struct mptcp_delegated_action {
	struct napi_struct napi;
	struct list_head head;
};

DECLARE_PER_CPU(struct mptcp_delegated_action, mptcp_delegated_actions);

#define MPTCP_DELEGATE_SCHEDULED	0
#define MPTCP_DELEGATE_SEND		1
#define MPTCP_DELEGATE_ACK		2
#define MPTCP_DELEGATE_SNDBUF		3

#define MPTCP_DELEGATE_ACTIONS_MASK	(~BIT(MPTCP_DELEGATE_SCHEDULED))
/* MPTCP subflow context */
struct mptcp_subflow_context {
	struct	list_head node;/* conn_list of subflows */

	struct_group(reset,

	unsigned long avg_pacing_rate; /* protected by msk socket lock */
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
		send_fastclose : 1,
		send_infinite_map : 1,
		remote_key_valid : 1,        /* received the peer key from */
		disposable : 1,	    /* ctx can be free at ulp release time */
		stale : 1,	    /* unable to snd/rcv data, do not use for xmit */
		valid_csum_seen : 1,        /* at least one csum validated */
		is_mptfo : 1,	    /* subflow is doing TFO */
		__unused : 10;
	bool	data_avail;
	bool	scheduled;
	u32	remote_nonce;
	u64	thmac;
	u32	local_nonce;
	u32	remote_token;
	union {
		u8	hmac[MPTCPOPT_HMAC_LEN]; /* MPJ subflow only */
		u64	iasn;	    /* initial ack sequence number, MPC subflows only */
	};
	s16	local_id;	    /* if negative not initialized yet */
	u8	remote_id;
	u8	reset_seen:1;
	u8	reset_transient:1;
	u8	reset_reason:4;
	u8	stale_count;

	u32	subflow_id;

	long	delegated_status;
	unsigned long	fail_tout;

	);

	struct	list_head delegated_node;   /* link into delegated_action, protected by local BH */

	u32	setsockopt_seq;
	u32	stale_rcv_tstamp;
	int     cached_sndbuf;	    /* sndbuf size when last synced with the msk sndbuf,
				     * protected by the msk socket lock
				     */

	struct	sock *tcp_sock;	    /* tcp sk backpointer */
	struct	sock *conn;	    /* parent mptcp_sock */
	const	struct inet_connection_sock_af_ops *icsk_af_ops;
	void	(*tcp_state_change)(struct sock *sk);
	void	(*tcp_error_report)(struct sock *sk);

	struct	rcu_head rcu;
};

static inline struct mptcp_subflow_context *
mptcp_subflow_ctx(const struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);

	/* Use RCU on icsk_ulp_data only for sock diag code */
	return (__force struct mptcp_subflow_context *)icsk->icsk_ulp_data;
}

static inline struct sock *
mptcp_subflow_tcp_sock(const struct mptcp_subflow_context *subflow)
{
	return subflow->tcp_sock;
}

static inline void
mptcp_subflow_ctx_reset(struct mptcp_subflow_context *subflow)
{
	memset(&subflow->reset, 0, sizeof(subflow->reset));
	subflow->request_mptcp = 1;
	WRITE_ONCE(subflow->local_id, -1);
}

/* Convert reset reasons in MPTCP to enum sk_rst_reason type */
static inline enum sk_rst_reason
sk_rst_convert_mptcp_reason(u32 reason)
{
	switch (reason) {
	case MPTCP_RST_EUNSPEC:
		return SK_RST_REASON_MPTCP_RST_EUNSPEC;
	case MPTCP_RST_EMPTCP:
		return SK_RST_REASON_MPTCP_RST_EMPTCP;
	case MPTCP_RST_ERESOURCE:
		return SK_RST_REASON_MPTCP_RST_ERESOURCE;
	case MPTCP_RST_EPROHIBIT:
		return SK_RST_REASON_MPTCP_RST_EPROHIBIT;
	case MPTCP_RST_EWQ2BIG:
		return SK_RST_REASON_MPTCP_RST_EWQ2BIG;
	case MPTCP_RST_EBADPERF:
		return SK_RST_REASON_MPTCP_RST_EBADPERF;
	case MPTCP_RST_EMIDDLEBOX:
		return SK_RST_REASON_MPTCP_RST_EMIDDLEBOX;
	default:
		/* It should not happen, or else errors may occur
		 * in MPTCP layer
		 */
		return SK_RST_REASON_ERROR;
	}
}

static inline void
mptcp_send_active_reset_reason(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	enum sk_rst_reason reason;

	reason = sk_rst_convert_mptcp_reason(subflow->reset_reason);
	tcp_send_active_reset(sk, GFP_ATOMIC, reason);
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

void mptcp_subflow_process_delegated(struct sock *ssk, long actions);

static inline void mptcp_subflow_delegate(struct mptcp_subflow_context *subflow, int action)
{
	long old, set_bits = BIT(MPTCP_DELEGATE_SCHEDULED) | BIT(action);
	struct mptcp_delegated_action *delegated;
	bool schedule;

	/* the caller held the subflow bh socket lock */
	lockdep_assert_in_softirq();

	/* The implied barrier pairs with tcp_release_cb_override()
	 * mptcp_napi_poll(), and ensures the below list check sees list
	 * updates done prior to delegated status bits changes
	 */
	old = set_mask_bits(&subflow->delegated_status, 0, set_bits);
	if (!(old & BIT(MPTCP_DELEGATE_SCHEDULED))) {
		if (WARN_ON_ONCE(!list_empty(&subflow->delegated_node)))
			return;

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

int mptcp_is_enabled(const struct net *net);
unsigned int mptcp_get_add_addr_timeout(const struct net *net);
int mptcp_is_checksum_enabled(const struct net *net);
int mptcp_allow_join_id0(const struct net *net);
unsigned int mptcp_stale_loss_cnt(const struct net *net);
unsigned int mptcp_close_timeout(const struct sock *sk);
int mptcp_get_pm_type(const struct net *net);
const char *mptcp_get_scheduler(const struct net *net);
void mptcp_get_available_schedulers(char *buf, size_t maxlen);
void __mptcp_subflow_fully_established(struct mptcp_sock *msk,
				       struct mptcp_subflow_context *subflow,
				       const struct mptcp_options_received *mp_opt);
bool __mptcp_retransmit_pending_data(struct sock *sk);
void mptcp_check_and_set_pending(struct sock *sk);
void __mptcp_push_pending(struct sock *sk, unsigned int flags);
bool mptcp_subflow_data_available(struct sock *sk);
void __init mptcp_subflow_init(void);
void mptcp_subflow_shutdown(struct sock *sk, struct sock *ssk, int how);
void mptcp_close_ssk(struct sock *sk, struct sock *ssk,
		     struct mptcp_subflow_context *subflow);
void __mptcp_subflow_send_ack(struct sock *ssk);
void mptcp_subflow_reset(struct sock *ssk);
void mptcp_subflow_queue_clean(struct sock *sk, struct sock *ssk);
void mptcp_sock_graft(struct sock *sk, struct socket *parent);
struct sock *__mptcp_nmpc_sk(struct mptcp_sock *msk);
bool __mptcp_close(struct sock *sk, long timeout);
void mptcp_cancel_work(struct sock *sk);
void __mptcp_unaccepted_force_close(struct sock *sk);
void mptcp_set_owner_r(struct sk_buff *skb, struct sock *sk);
void mptcp_set_state(struct sock *sk, int state);

bool mptcp_addresses_equal(const struct mptcp_addr_info *a,
			   const struct mptcp_addr_info *b, bool use_port);
void mptcp_local_address(const struct sock_common *skc, struct mptcp_addr_info *addr);

/* called with sk socket lock held */
int __mptcp_subflow_connect(struct sock *sk, const struct mptcp_addr_info *loc,
			    const struct mptcp_addr_info *remote);
int mptcp_subflow_create_socket(struct sock *sk, unsigned short family,
				struct socket **new_sock);
void mptcp_info2sockaddr(const struct mptcp_addr_info *info,
			 struct sockaddr_storage *addr,
			 unsigned short family);
struct mptcp_sched_ops *mptcp_sched_find(const char *name);
int mptcp_register_scheduler(struct mptcp_sched_ops *sched);
void mptcp_unregister_scheduler(struct mptcp_sched_ops *sched);
void mptcp_sched_init(void);
int mptcp_init_sched(struct mptcp_sock *msk,
		     struct mptcp_sched_ops *sched);
void mptcp_release_sched(struct mptcp_sock *msk);
void mptcp_subflow_set_scheduled(struct mptcp_subflow_context *subflow,
				 bool scheduled);
struct sock *mptcp_subflow_get_send(struct mptcp_sock *msk);
struct sock *mptcp_subflow_get_retrans(struct mptcp_sock *msk);
int mptcp_sched_get_send(struct mptcp_sock *msk);
int mptcp_sched_get_retrans(struct mptcp_sock *msk);

static inline u64 mptcp_data_avail(const struct mptcp_sock *msk)
{
	return READ_ONCE(msk->bytes_received) - READ_ONCE(msk->bytes_consumed);
}

static inline bool mptcp_epollin_ready(const struct sock *sk)
{
	/* mptcp doesn't have to deal with small skbs in the receive queue,
	 * at it can always coalesce them
	 */
	return (mptcp_data_avail(mptcp_sk(sk)) >= sk->sk_rcvlowat) ||
	       (mem_cgroup_sockets_enabled && sk->sk_memcg &&
		mem_cgroup_under_socket_pressure(sk->sk_memcg)) ||
	       READ_ONCE(tcp_memory_pressure);
}

int mptcp_set_rcvlowat(struct sock *sk, int val);

static inline bool __tcp_can_send(const struct sock *ssk)
{
	/* only send if our side has not closed yet */
	return ((1 << inet_sk_state_load(ssk)) & (TCPF_ESTABLISHED | TCPF_CLOSE_WAIT));
}

static inline bool __mptcp_subflow_active(struct mptcp_subflow_context *subflow)
{
	/* can't send if JOIN hasn't completed yet (i.e. is usable for mptcp) */
	if (subflow->request_join && !subflow->fully_established)
		return false;

	return __tcp_can_send(mptcp_subflow_tcp_sock(subflow));
}

void mptcp_subflow_set_active(struct mptcp_subflow_context *subflow);

bool mptcp_subflow_active(struct mptcp_subflow_context *subflow);

void mptcp_subflow_drop_ctx(struct sock *ssk);

static inline void mptcp_subflow_tcp_fallback(struct sock *sk,
					      struct mptcp_subflow_context *ctx)
{
	sk->sk_data_ready = sock_def_readable;
	sk->sk_state_change = ctx->tcp_state_change;
	sk->sk_write_space = sk_stream_write_space;
	sk->sk_error_report = ctx->tcp_error_report;

	inet_csk(sk)->icsk_af_ops = ctx->icsk_af_ops;
}

void __init mptcp_proto_init(void);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
int __init mptcp_proto_v6_init(void);
#endif

struct sock *mptcp_sk_clone_init(const struct sock *sk,
				 const struct mptcp_options_received *mp_opt,
				 struct sock *ssk,
				 struct request_sock *req);
void mptcp_get_options(const struct sk_buff *skb,
		       struct mptcp_options_received *mp_opt);

void mptcp_finish_connect(struct sock *sk);
void __mptcp_sync_state(struct sock *sk, int state);
void mptcp_reset_tout_timer(struct mptcp_sock *msk, unsigned long fail_tout);

static inline void mptcp_stop_tout_timer(struct sock *sk)
{
	if (!inet_csk(sk)->icsk_mtup.probe_timestamp)
		return;

	sk_stop_timer(sk, &sk->sk_timer);
	inet_csk(sk)->icsk_mtup.probe_timestamp = 0;
}

static inline void mptcp_set_close_tout(struct sock *sk, unsigned long tout)
{
	/* avoid 0 timestamp, as that means no close timeout */
	inet_csk(sk)->icsk_mtup.probe_timestamp = tout ? : 1;
}

static inline void mptcp_start_tout_timer(struct sock *sk)
{
	mptcp_set_close_tout(sk, tcp_jiffies32);
	mptcp_reset_tout_timer(mptcp_sk(sk), 0);
}

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
bool mptcp_update_rcv_data_fin(struct mptcp_sock *msk, u64 data_fin_seq, bool use_64bit);
static inline bool mptcp_data_fin_enabled(const struct mptcp_sock *msk)
{
	return READ_ONCE(msk->snd_data_fin_enable) &&
	       READ_ONCE(msk->write_seq) == READ_ONCE(msk->snd_nxt);
}

static inline u32 mptcp_notsent_lowat(const struct sock *sk)
{
	struct net *net = sock_net(sk);
	u32 val;

	val = READ_ONCE(mptcp_sk(sk)->notsent_lowat);
	return val ?: READ_ONCE(net->ipv4.sysctl_tcp_notsent_lowat);
}

static inline bool mptcp_stream_memory_free(const struct sock *sk, int wake)
{
	const struct mptcp_sock *msk = mptcp_sk(sk);
	u32 notsent_bytes;

	notsent_bytes = READ_ONCE(msk->write_seq) - READ_ONCE(msk->snd_nxt);
	return (notsent_bytes << wake) < mptcp_notsent_lowat(sk);
}

static inline bool __mptcp_stream_is_writeable(const struct sock *sk, int wake)
{
	return mptcp_stream_memory_free(sk, wake) &&
	       __sk_stream_is_writeable(sk, wake);
}

static inline void mptcp_write_space(struct sock *sk)
{
	/* pairs with memory barrier in mptcp_poll */
	smp_mb();
	if (mptcp_stream_memory_free(sk, 1))
		sk_stream_write_space(sk);
}

static inline void __mptcp_sync_sndbuf(struct sock *sk)
{
	struct mptcp_subflow_context *subflow;
	int ssk_sndbuf, new_sndbuf;

	if (sk->sk_userlocks & SOCK_SNDBUF_LOCK)
		return;

	new_sndbuf = READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_wmem[0]);
	mptcp_for_each_subflow(mptcp_sk(sk), subflow) {
		ssk_sndbuf =  READ_ONCE(mptcp_subflow_tcp_sock(subflow)->sk_sndbuf);

		subflow->cached_sndbuf = ssk_sndbuf;
		new_sndbuf += ssk_sndbuf;
	}

	/* the msk max wmem limit is <nr_subflows> * tcp wmem[2] */
	WRITE_ONCE(sk->sk_sndbuf, new_sndbuf);
	mptcp_write_space(sk);
}

/* The called held both the msk socket and the subflow socket locks,
 * possibly under BH
 */
static inline void __mptcp_propagate_sndbuf(struct sock *sk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);

	if (READ_ONCE(ssk->sk_sndbuf) != subflow->cached_sndbuf)
		__mptcp_sync_sndbuf(sk);
}

/* the caller held only the subflow socket lock, either in process or
 * BH context. Additionally this can be called under the msk data lock,
 * so we can't acquire such lock here: let the delegate action acquires
 * the needed locks in suitable order.
 */
static inline void mptcp_propagate_sndbuf(struct sock *sk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);

	if (likely(READ_ONCE(ssk->sk_sndbuf) == subflow->cached_sndbuf))
		return;

	local_bh_disable();
	mptcp_subflow_delegate(subflow, MPTCP_DELEGATE_SNDBUF);
	local_bh_enable();
}

void mptcp_destroy_common(struct mptcp_sock *msk, unsigned int flags);

#define MPTCP_TOKEN_MAX_RETRIES	4

void __init mptcp_token_init(void);
static inline void mptcp_token_init_request(struct request_sock *req)
{
	mptcp_subflow_rsk(req)->token_node.pprev = NULL;
}

int mptcp_token_new_request(struct request_sock *req);
void mptcp_token_destroy_request(struct request_sock *req);
int mptcp_token_new_connect(struct sock *ssk);
void mptcp_token_accept(struct mptcp_subflow_request_sock *r,
			struct mptcp_sock *msk);
bool mptcp_token_exists(u32 token);
struct mptcp_sock *mptcp_token_get_sock(struct net *net, u32 token);
struct mptcp_sock *mptcp_token_iter_next(const struct net *net, long *s_slot,
					 long *s_num);
void mptcp_token_destroy(struct mptcp_sock *msk);

void mptcp_crypto_key_sha(u64 key, u32 *token, u64 *idsn);

void mptcp_crypto_hmac_sha(u64 key1, u64 key2, u8 *msg, int len, void *hmac);
__sum16 __mptcp_make_csum(u64 data_seq, u32 subflow_seq, u16 data_len, __wsum sum);

void __init mptcp_pm_init(void);
void mptcp_pm_data_init(struct mptcp_sock *msk);
void mptcp_pm_data_reset(struct mptcp_sock *msk);
int mptcp_pm_parse_addr(struct nlattr *attr, struct genl_info *info,
			struct mptcp_addr_info *addr);
int mptcp_pm_parse_entry(struct nlattr *attr, struct genl_info *info,
			 bool require_family,
			 struct mptcp_pm_addr_entry *entry);
bool mptcp_pm_addr_families_match(const struct sock *sk,
				  const struct mptcp_addr_info *loc,
				  const struct mptcp_addr_info *rem);
void mptcp_pm_subflow_chk_stale(const struct mptcp_sock *msk, struct sock *ssk);
void mptcp_pm_nl_subflow_chk_stale(const struct mptcp_sock *msk, struct sock *ssk);
void mptcp_pm_new_connection(struct mptcp_sock *msk, const struct sock *ssk, int server_side);
void mptcp_pm_fully_established(struct mptcp_sock *msk, const struct sock *ssk);
bool mptcp_pm_allow_new_subflow(struct mptcp_sock *msk);
void mptcp_pm_connection_closed(struct mptcp_sock *msk);
void mptcp_pm_subflow_established(struct mptcp_sock *msk);
bool mptcp_pm_nl_check_work_pending(struct mptcp_sock *msk);
void mptcp_pm_subflow_check_next(struct mptcp_sock *msk,
				 const struct mptcp_subflow_context *subflow);
void mptcp_pm_add_addr_received(const struct sock *ssk,
				const struct mptcp_addr_info *addr);
void mptcp_pm_add_addr_echoed(struct mptcp_sock *msk,
			      const struct mptcp_addr_info *addr);
void mptcp_pm_add_addr_send_ack(struct mptcp_sock *msk);
void mptcp_pm_nl_addr_send_ack(struct mptcp_sock *msk);
void mptcp_pm_rm_addr_received(struct mptcp_sock *msk,
			       const struct mptcp_rm_list *rm_list);
void mptcp_pm_mp_prio_received(struct sock *sk, u8 bkup);
void mptcp_pm_mp_fail_received(struct sock *sk, u64 fail_seq);
int mptcp_pm_nl_mp_prio_send_ack(struct mptcp_sock *msk,
				 struct mptcp_addr_info *addr,
				 struct mptcp_addr_info *rem,
				 u8 bkup);
bool mptcp_pm_alloc_anno_list(struct mptcp_sock *msk,
			      const struct mptcp_addr_info *addr);
void mptcp_pm_free_anno_list(struct mptcp_sock *msk);
bool mptcp_pm_sport_in_anno_list(struct mptcp_sock *msk, const struct sock *sk);
struct mptcp_pm_add_entry *
mptcp_pm_del_add_timer(struct mptcp_sock *msk,
		       const struct mptcp_addr_info *addr, bool check_id);
struct mptcp_pm_add_entry *
mptcp_lookup_anno_list_by_saddr(const struct mptcp_sock *msk,
				const struct mptcp_addr_info *addr);
int mptcp_pm_get_flags_and_ifindex_by_id(struct mptcp_sock *msk,
					 unsigned int id,
					 u8 *flags, int *ifindex);
int mptcp_pm_nl_get_flags_and_ifindex_by_id(struct mptcp_sock *msk, unsigned int id,
					    u8 *flags, int *ifindex);
int mptcp_userspace_pm_get_flags_and_ifindex_by_id(struct mptcp_sock *msk,
						   unsigned int id,
						   u8 *flags, int *ifindex);
int mptcp_pm_set_flags(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_set_flags(struct sk_buff *skb, struct genl_info *info);
int mptcp_userspace_pm_set_flags(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_announce_addr(struct mptcp_sock *msk,
			   const struct mptcp_addr_info *addr,
			   bool echo);
int mptcp_pm_remove_addr(struct mptcp_sock *msk, const struct mptcp_rm_list *rm_list);
int mptcp_pm_remove_subflow(struct mptcp_sock *msk, const struct mptcp_rm_list *rm_list);
void mptcp_pm_remove_addrs(struct mptcp_sock *msk, struct list_head *rm_list);

void mptcp_free_local_addr_list(struct mptcp_sock *msk);

void mptcp_event(enum mptcp_event_type type, const struct mptcp_sock *msk,
		 const struct sock *ssk, gfp_t gfp);
void mptcp_event_addr_announced(const struct sock *ssk, const struct mptcp_addr_info *info);
void mptcp_event_addr_removed(const struct mptcp_sock *msk, u8 id);
void mptcp_event_pm_listener(const struct sock *ssk,
			     enum mptcp_event_type event);
bool mptcp_userspace_pm_active(const struct mptcp_sock *msk);

void __mptcp_fastopen_gen_msk_ackseq(struct mptcp_sock *msk, struct mptcp_subflow_context *subflow,
				     const struct mptcp_options_received *mp_opt);
void mptcp_fastopen_subflow_synack_set_params(struct mptcp_subflow_context *subflow,
					      struct request_sock *req);
int mptcp_nl_fill_addr(struct sk_buff *skb,
		       struct mptcp_pm_addr_entry *entry);

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

static inline bool mptcp_pm_is_userspace(const struct mptcp_sock *msk)
{
	return READ_ONCE(msk->pm.pm_type) == MPTCP_PM_TYPE_USERSPACE;
}

static inline bool mptcp_pm_is_kernel(const struct mptcp_sock *msk)
{
	return READ_ONCE(msk->pm.pm_type) == MPTCP_PM_TYPE_KERNEL;
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

bool mptcp_pm_add_addr_signal(struct mptcp_sock *msk, const struct sk_buff *skb,
			      unsigned int opt_size, unsigned int remaining,
			      struct mptcp_addr_info *addr, bool *echo,
			      bool *drop_other_suboptions);
bool mptcp_pm_rm_addr_signal(struct mptcp_sock *msk, unsigned int remaining,
			     struct mptcp_rm_list *rm_list);
int mptcp_pm_get_local_id(struct mptcp_sock *msk, struct sock_common *skc);
int mptcp_pm_nl_get_local_id(struct mptcp_sock *msk, struct mptcp_addr_info *skc);
int mptcp_userspace_pm_get_local_id(struct mptcp_sock *msk, struct mptcp_addr_info *skc);
int mptcp_pm_dump_addr(struct sk_buff *msg, struct netlink_callback *cb);
int mptcp_pm_nl_dump_addr(struct sk_buff *msg,
			  struct netlink_callback *cb);
int mptcp_userspace_pm_dump_addr(struct sk_buff *msg,
				 struct netlink_callback *cb);
int mptcp_pm_get_addr(struct sk_buff *skb, struct genl_info *info);
int mptcp_pm_nl_get_addr(struct sk_buff *skb, struct genl_info *info);
int mptcp_userspace_pm_get_addr(struct sk_buff *skb,
				struct genl_info *info);

static inline u8 subflow_get_local_id(const struct mptcp_subflow_context *subflow)
{
	int local_id = READ_ONCE(subflow->local_id);

	if (local_id < 0)
		return 0;
	return local_id;
}

void __init mptcp_pm_nl_init(void);
void mptcp_pm_nl_work(struct mptcp_sock *msk);
void mptcp_pm_nl_rm_subflow_received(struct mptcp_sock *msk,
				     const struct mptcp_rm_list *rm_list);
unsigned int mptcp_pm_get_add_addr_signal_max(const struct mptcp_sock *msk);
unsigned int mptcp_pm_get_add_addr_accept_max(const struct mptcp_sock *msk);
unsigned int mptcp_pm_get_subflows_max(const struct mptcp_sock *msk);
unsigned int mptcp_pm_get_local_addr_max(const struct mptcp_sock *msk);

/* called under PM lock */
static inline void __mptcp_pm_close_subflow(struct mptcp_sock *msk)
{
	if (--msk->pm.subflows < mptcp_pm_get_subflows_max(msk))
		WRITE_ONCE(msk->pm.accept_subflow, true);
}

static inline void mptcp_pm_close_subflow(struct mptcp_sock *msk)
{
	spin_lock_bh(&msk->pm.lock);
	__mptcp_pm_close_subflow(msk);
	spin_unlock_bh(&msk->pm.lock);
}

void mptcp_sockopt_sync(struct mptcp_sock *msk, struct sock *ssk);
void mptcp_sockopt_sync_locked(struct mptcp_sock *msk, struct sock *ssk);

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
	if (__mptcp_check_fallback(msk)) {
		pr_debug("TCP fallback already done (msk=%p)", msk);
		return;
	}
	set_bit(MPTCP_FALLBACK_DONE, &msk->flags);
}

static inline bool __mptcp_has_initial_subflow(const struct mptcp_sock *msk)
{
	struct sock *ssk = READ_ONCE(msk->first);

	return ssk && ((1 << inet_sk_state_load(ssk)) &
		       (TCPF_ESTABLISHED | TCPF_SYN_SENT |
			TCPF_SYN_RECV | TCPF_LISTEN));
}

static inline void mptcp_do_fallback(struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = subflow->conn;
	struct mptcp_sock *msk;

	msk = mptcp_sk(sk);
	__mptcp_do_fallback(msk);
	if (READ_ONCE(msk->snd_data_fin_enable) && !(ssk->sk_shutdown & SEND_SHUTDOWN)) {
		gfp_t saved_allocation = ssk->sk_allocation;

		/* we are in a atomic (BH) scope, override ssk default for data
		 * fin allocation
		 */
		ssk->sk_allocation = GFP_ATOMIC;
		ssk->sk_shutdown |= SEND_SHUTDOWN;
		tcp_shutdown(ssk, SEND_SHUTDOWN);
		ssk->sk_allocation = saved_allocation;
	}
}

#define pr_fallback(a) pr_debug("%s:fallback to TCP (msk=%p)", __func__, a)

static inline bool mptcp_check_infinite_map(struct sk_buff *skb)
{
	struct mptcp_ext *mpext;

	mpext = skb ? mptcp_get_ext(skb) : NULL;
	if (mpext && mpext->infinite_map)
		return true;

	return false;
}

static inline bool is_active_ssk(struct mptcp_subflow_context *subflow)
{
	return (subflow->request_mptcp || subflow->request_join);
}

static inline bool subflow_simultaneous_connect(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	return (1 << sk->sk_state) &
	       (TCPF_ESTABLISHED | TCPF_FIN_WAIT1 | TCPF_FIN_WAIT2 | TCPF_CLOSING) &&
	       is_active_ssk(subflow) &&
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
