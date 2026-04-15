/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NET_RPS_H
#define _NET_RPS_H

#include <linux/types.h>
#include <linux/static_key.h>
#include <net/sock.h>
#include <net/hotdata.h>

#ifdef CONFIG_RPS
#include <net/rps-types.h>

extern struct static_key_false rps_needed;
extern struct static_key_false rfs_needed;

/*
 * This structure holds an RPS map which can be of variable length.  The
 * map is an array of CPUs.
 */
struct rps_map {
	unsigned int	len;
	struct rcu_head	rcu;
	u16		cpus[];
};
#define RPS_MAP_SIZE(_num) (sizeof(struct rps_map) + ((_num) * sizeof(u16)))

/*
 * The rps_dev_flow structure contains the mapping of a flow to a CPU, the
 * tail pointer for that CPU's input queue at the time of last enqueue, a
 * hardware filter index, and the hash of the flow if aRFS is enabled.
 */
struct rps_dev_flow {
	u16		cpu;
	u16		filter;
	unsigned int	last_qtail;
#ifdef CONFIG_RFS_ACCEL
	u32		hash;
#endif
};
#define RPS_NO_FILTER 0xffff

/*
 * The rps_sock_flow_table contains mappings of flows to the last CPU
 * on which they were processed by the application (set in recvmsg).
 * Each entry is a 32bit value. Upper part is the high-order bits
 * of flow hash, lower part is CPU number.
 * rps_cpu_mask is used to partition the space, depending on number of
 * possible CPUs : rps_cpu_mask = roundup_pow_of_two(nr_cpu_ids) - 1
 * For example, if 64 CPUs are possible, rps_cpu_mask = 0x3f,
 * meaning we use 32-6=26 bits for the hash.
 */
struct rps_sock_flow_table {
	u32	ent;
};

#define RPS_NO_CPU 0xffff

static inline void rps_record_sock_flow(rps_tag_ptr tag_ptr, u32 hash)
{
	unsigned int index = hash & rps_tag_to_mask(tag_ptr);
	u32 val = hash & ~net_hotdata.rps_cpu_mask;
	struct rps_sock_flow_table *table;

	/* We only give a hint, preemption can change CPU under us */
	val |= raw_smp_processor_id();

	table = rps_tag_to_table(tag_ptr);
	/* The following WRITE_ONCE() is paired with the READ_ONCE()
	 * here, and another one in get_rps_cpu().
	 */
	if (READ_ONCE(table[index].ent) != val)
		WRITE_ONCE(table[index].ent, val);
}

static inline void _sock_rps_record_flow_hash(__u32 hash)
{
	rps_tag_ptr tag_ptr;

	if (!hash)
		return;
	rcu_read_lock();
	tag_ptr = READ_ONCE(net_hotdata.rps_sock_flow_table);
	if (tag_ptr)
		rps_record_sock_flow(tag_ptr, hash);
	rcu_read_unlock();
}

static inline void _sock_rps_record_flow(const struct sock *sk)
{
	/* Reading sk->sk_rxhash might incur an expensive cache line
	 * miss.
	 *
	 * TCP_ESTABLISHED does cover almost all states where RFS
	 * might be useful, and is cheaper [1] than testing :
	 *	IPv4: inet_sk(sk)->inet_daddr
	 *	IPv6: ipv6_addr_any(&sk->sk_v6_daddr)
	 * OR	an additional socket flag
	 * [1] : sk_state and sk_prot are in the same cache line.
	 */
	if (sk->sk_state == TCP_ESTABLISHED) {
		/* This READ_ONCE() is paired with the WRITE_ONCE()
		 * from sock_rps_save_rxhash() and sock_rps_reset_rxhash().
		 */
		_sock_rps_record_flow_hash(READ_ONCE(sk->sk_rxhash));
	}
}

static inline void _sock_rps_delete_flow(const struct sock *sk)
{
	struct rps_sock_flow_table *table;
	rps_tag_ptr tag_ptr;
	u32 hash, index;

	hash = READ_ONCE(sk->sk_rxhash);
	if (!hash)
		return;

	rcu_read_lock();
	tag_ptr = READ_ONCE(net_hotdata.rps_sock_flow_table);
	if (tag_ptr) {
		index = hash & rps_tag_to_mask(tag_ptr);
		table = rps_tag_to_table(tag_ptr);
		if (READ_ONCE(table[index].ent) != RPS_NO_CPU)
			WRITE_ONCE(table[index].ent, RPS_NO_CPU);
	}
	rcu_read_unlock();
}
#endif /* CONFIG_RPS */

static inline bool rfs_is_needed(void)
{
#ifdef CONFIG_RPS
	return static_branch_unlikely(&rfs_needed);
#else
	return false;
#endif
}

static inline void sock_rps_record_flow_hash(__u32 hash)
{
#ifdef CONFIG_RPS
	if (!rfs_is_needed())
		return;

	_sock_rps_record_flow_hash(hash);
#endif
}

static inline void sock_rps_record_flow(const struct sock *sk)
{
#ifdef CONFIG_RPS
	if (!rfs_is_needed())
		return;

	_sock_rps_record_flow(sk);
#endif
}

static inline void sock_rps_delete_flow(const struct sock *sk)
{
#ifdef CONFIG_RPS
	if (!rfs_is_needed())
		return;

	_sock_rps_delete_flow(sk);
#endif
}

static inline u32 rps_input_queue_tail_incr(struct softnet_data *sd)
{
#ifdef CONFIG_RPS
	return ++sd->input_queue_tail;
#else
	return 0;
#endif
}

static inline void rps_input_queue_tail_save(u32 *dest, u32 tail)
{
#ifdef CONFIG_RPS
	WRITE_ONCE(*dest, tail);
#endif
}

static inline void rps_input_queue_head_add(struct softnet_data *sd, int val)
{
#ifdef CONFIG_RPS
	WRITE_ONCE(sd->input_queue_head, sd->input_queue_head + val);
#endif
}

static inline void rps_input_queue_head_incr(struct softnet_data *sd)
{
	rps_input_queue_head_add(sd, 1);
}

#endif /* _NET_RPS_H */
