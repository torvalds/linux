/*
 * Kernel Connection Multiplexor
 *
 * Copyright (c) 2016 Tom Herbert <tom@herbertland.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#ifndef __NET_KCM_H_
#define __NET_KCM_H_

#include <linux/skbuff.h>
#include <net/sock.h>
#include <uapi/linux/kcm.h>

extern unsigned int kcm_net_id;

#define KCM_STATS_ADD(stat, count) ((stat) += (count))
#define KCM_STATS_INCR(stat) ((stat)++)

struct kcm_psock_stats {
	unsigned long long rx_msgs;
	unsigned long long rx_bytes;
	unsigned long long tx_msgs;
	unsigned long long tx_bytes;
	unsigned int rx_aborts;
	unsigned int rx_mem_fail;
	unsigned int rx_need_more_hdr;
	unsigned int rx_msg_too_big;
	unsigned int rx_msg_timeouts;
	unsigned int rx_bad_hdr_len;
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

struct kcm_rx_msg {
	int full_len;
	int accum_len;
	int offset;
	int early_eaten;
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
	struct kcm_mux *mux;
	int index;

	u32 tx_stopped : 1;
	u32 rx_stopped : 1;
	u32 done : 1;
	u32 unattaching : 1;

	void (*save_state_change)(struct sock *sk);
	void (*save_data_ready)(struct sock *sk);
	void (*save_write_space)(struct sock *sk);

	struct list_head psock_list;

	struct kcm_psock_stats stats;

	/* Receive */
	struct sk_buff *rx_skb_head;
	struct sk_buff **rx_skb_nextp;
	struct sk_buff *ready_rx_msg;
	struct list_head psock_ready_list;
	struct work_struct rx_work;
	struct delayed_work rx_delayed_work;
	struct bpf_prog *bpf_prog;
	struct kcm_sock *rx_kcm;
	unsigned long long saved_rx_bytes;
	unsigned long long saved_rx_msgs;
	struct timer_list rx_msg_timer;
	unsigned int rx_need_bytes;

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
	SAVE_PSOCK_STATS(rx_msgs);
	SAVE_PSOCK_STATS(rx_bytes);
	SAVE_PSOCK_STATS(rx_aborts);
	SAVE_PSOCK_STATS(rx_mem_fail);
	SAVE_PSOCK_STATS(rx_need_more_hdr);
	SAVE_PSOCK_STATS(rx_msg_too_big);
	SAVE_PSOCK_STATS(rx_msg_timeouts);
	SAVE_PSOCK_STATS(rx_bad_hdr_len);
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
