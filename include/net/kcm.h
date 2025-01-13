/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel Connection Multiplexor
 *
 * Copyright (c) 2016 Tom Herbert <tom@herbertland.com>
 */

#ifndef __NET_KCM_H_
#define __NET_KCM_H_

#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/strparser.h>
#include <uapi/linux/kcm.h>

extern unsigned int kcm_net_id;

#define KCM_STATS_ADD(stat, count) ((stat) += (count))
#define KCM_STATS_INCR(stat) ((stat)++)

struct kcm_psock_stats {
	unsigned long long tx_msgs;
	unsigned long long tx_bytes;
	unsigned long long reserved;
	unsigned long long unreserved;
	unsigned int tx_aborts;
};

struct kcm_mux_stats {
	unsigned long long rx_msgs;
	unsigned long long rx_bytes;
	unsigned long long tx_msgs;
	unsigned long long tx_bytes;
	unsigned int rx_ready_drops;
	unsigned int tx_retries;
	unsigned int psock_attach;
	unsigned int psock_unattach_rsvd;
	unsigned int psock_unattach;
};

struct kcm_stats {
	unsigned long long rx_msgs;
	unsigned long long rx_bytes;
	unsigned long long tx_msgs;
	unsigned long long tx_bytes;
};

struct kcm_tx_msg {
	unsigned int sent;
	unsigned int fragidx;
	unsigned int frag_offset;
	unsigned int msg_flags;
	struct sk_buff *frag_skb;
	struct sk_buff *last_skb;
};

/* Socket structure for KCM client sockets */
struct kcm_sock {
	struct sock sk;
	struct kcm_mux *mux;
	struct list_head kcm_sock_list;
	int index;
	u32 done : 1;
	struct work_struct done_work;

	struct kcm_stats stats;

	/* Transmit */
	struct kcm_psock *tx_psock;
	struct work_struct tx_work;
	struct list_head wait_psock_list;
	struct sk_buff *seq_skb;
	struct mutex tx_mutex;
	u32 tx_stopped : 1;

	/* Don't use bit fields here, these are set under different locks */
	bool tx_wait;
	bool tx_wait_more;

	/* Receive */
	struct kcm_psock *rx_psock;
	struct list_head wait_rx_list; /* KCMs waiting for receiving */
	bool rx_wait;
	u32 rx_disabled : 1;
};

struct bpf_prog;

/* Structure for an attached lower socket */
struct kcm_psock {
	struct sock *sk;
	struct strparser strp;
	struct kcm_mux *mux;
	int index;

	u32 tx_stopped : 1;
	u32 done : 1;
	u32 unattaching : 1;

	void (*save_state_change)(struct sock *sk);
	void (*save_data_ready)(struct sock *sk);
	void (*save_write_space)(struct sock *sk);

	struct list_head psock_list;

	struct kcm_psock_stats stats;

	/* Receive */
	struct list_head psock_ready_list;
	struct bpf_prog *bpf_prog;
	struct kcm_sock *rx_kcm;
	unsigned long long saved_rx_bytes;
	unsigned long long saved_rx_msgs;
	struct sk_buff *ready_rx_msg;

	/* Transmit */
	struct kcm_sock *tx_kcm;
	struct list_head psock_avail_list;
	unsigned long long saved_tx_bytes;
	unsigned long long saved_tx_msgs;
};

/* Per net MUX list */
struct kcm_net {
	struct mutex mutex;
	struct kcm_psock_stats aggregate_psock_stats;
	struct kcm_mux_stats aggregate_mux_stats;
	struct strp_aggr_stats aggregate_strp_stats;
	struct list_head mux_list;
	int count;
};

/* Structure for a MUX */
struct kcm_mux {
	struct list_head kcm_mux_list;
	struct rcu_head rcu;
	struct kcm_net *knet;

	struct list_head kcm_socks;	/* All KCM sockets on MUX */
	int kcm_socks_cnt;		/* Total KCM socket count for MUX */
	struct list_head psocks;	/* List of all psocks on MUX */
	int psocks_cnt;		/* Total attached sockets */

	struct kcm_mux_stats stats;
	struct kcm_psock_stats aggregate_psock_stats;
	struct strp_aggr_stats aggregate_strp_stats;

	/* Receive */
	spinlock_t rx_lock ____cacheline_aligned_in_smp;
	struct list_head kcm_rx_waiters; /* KCMs waiting for receiving */
	struct list_head psocks_ready;	/* List of psocks with a msg ready */
	struct sk_buff_head rx_hold_queue;

	/* Transmit */
	spinlock_t  lock ____cacheline_aligned_in_smp;	/* TX and mux locking */
	struct list_head psocks_avail;	/* List of available psocks */
	struct list_head kcm_tx_waiters; /* KCMs waiting for a TX psock */
};

#ifdef CONFIG_PROC_FS
int kcm_proc_init(void);
void kcm_proc_exit(void);
#else
static inline int kcm_proc_init(void) { return 0; }
static inline void kcm_proc_exit(void) { }
#endif

static inline void aggregate_psock_stats(struct kcm_psock_stats *stats,
					 struct kcm_psock_stats *agg_stats)
{
	/* Save psock statistics in the mux when psock is being unattached. */

#define SAVE_PSOCK_STATS(_stat) (agg_stats->_stat += stats->_stat)
	SAVE_PSOCK_STATS(tx_msgs);
	SAVE_PSOCK_STATS(tx_bytes);
	SAVE_PSOCK_STATS(reserved);
	SAVE_PSOCK_STATS(unreserved);
	SAVE_PSOCK_STATS(tx_aborts);
#undef SAVE_PSOCK_STATS
}

static inline void aggregate_mux_stats(struct kcm_mux_stats *stats,
				       struct kcm_mux_stats *agg_stats)
{
	/* Save psock statistics in the mux when psock is being unattached. */

#define SAVE_MUX_STATS(_stat) (agg_stats->_stat += stats->_stat)
	SAVE_MUX_STATS(rx_msgs);
	SAVE_MUX_STATS(rx_bytes);
	SAVE_MUX_STATS(tx_msgs);
	SAVE_MUX_STATS(tx_bytes);
	SAVE_MUX_STATS(rx_ready_drops);
	SAVE_MUX_STATS(psock_attach);
	SAVE_MUX_STATS(psock_unattach_rsvd);
	SAVE_MUX_STATS(psock_unattach);
#undef SAVE_MUX_STATS
}

#endif /* __NET_KCM_H_ */
