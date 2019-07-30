// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * Sample Host Bandwidth Manager (HBM) BPF program.
 *
 * A cgroup skb BPF egress program to limit cgroup output bandwidth.
 * It uses a modified virtual token bucket queue to limit average
 * egress bandwidth. The implementation uses credits instead of tokens.
 * Negative credits imply that queueing would have happened (this is
 * a virtual queue, so no queueing is done by it. However, queueing may
 * occur at the actual qdisc (which is not used for rate limiting).
 *
 * This implementation uses 3 thresholds, one to start marking packets and
 * the other two to drop packets:
 *                                  CREDIT
 *        - <--------------------------|------------------------> +
 *              |    |          |      0
 *              |  Large pkt    |
 *              |  drop thresh  |
 *   Small pkt drop             Mark threshold
 *       thresh
 *
 * The effect of marking depends on the type of packet:
 * a) If the packet is ECN enabled and it is a TCP packet, then the packet
 *    is ECN marked.
 * b) If the packet is a TCP packet, then we probabilistically call tcp_cwr
 *    to reduce the congestion window. The current implementation uses a linear
 *    distribution (0% probability at marking threshold, 100% probability
 *    at drop threshold).
 * c) If the packet is not a TCP packet, then it is dropped.
 *
 * If the credit is below the drop threshold, the packet is dropped. If it
 * is a TCP packet, then it also calls tcp_cwr since packets dropped by
 * by a cgroup skb BPF program do not automatically trigger a call to
 * tcp_cwr in the current kernel code.
 *
 * This BPF program actually uses 2 drop thresholds, one threshold
 * for larger packets (>= 120 bytes) and another for smaller packets. This
 * protects smaller packets such as SYNs, ACKs, etc.
 *
 * The default bandwidth limit is set at 1Gbps but this can be changed by
 * a user program through a shared BPF map. In addition, by default this BPF
 * program does not limit connections using loopback. This behavior can be
 * overwritten by the user program. There is also an option to calculate
 * some statistics, such as percent of packets marked or dropped, which
 * the user program can access.
 *
 * A latter patch provides such a program (hbm.c)
 */

#include "hbm_kern.h"

SEC("cgroup_skb/egress")
int _hbm_out_cg(struct __sk_buff *skb)
{
	struct hbm_pkt_info pkti;
	int len = skb->len;
	unsigned int queue_index = 0;
	unsigned long long curtime;
	int credit;
	signed long long delta = 0, zero = 0;
	int max_credit = MAX_CREDIT;
	bool congestion_flag = false;
	bool drop_flag = false;
	bool cwr_flag = false;
	struct hbm_vqueue *qdp;
	struct hbm_queue_stats *qsp = NULL;
	int rv = ALLOW_PKT;

	qsp = bpf_map_lookup_elem(&queue_stats, &queue_index);
	if (qsp != NULL && !qsp->loopback && (skb->ifindex == 1))
		return ALLOW_PKT;

	hbm_get_pkt_info(skb, &pkti);

	// We may want to account for the length of headers in len
	// calculation, like ETH header + overhead, specially if it
	// is a gso packet. But I am not doing it right now.

	qdp = bpf_get_local_storage(&queue_state, 0);
	if (!qdp)
		return ALLOW_PKT;
	else if (qdp->lasttime == 0)
		hbm_init_vqueue(qdp, 1024);

	curtime = bpf_ktime_get_ns();

	// Begin critical section
	bpf_spin_lock(&qdp->lock);
	credit = qdp->credit;
	delta = curtime - qdp->lasttime;
	/* delta < 0 implies that another process with a curtime greater
	 * than ours beat us to the critical section and already added
	 * the new credit, so we should not add it ourselves
	 */
	if (delta > 0) {
		qdp->lasttime = curtime;
		credit += CREDIT_PER_NS(delta, qdp->rate);
		if (credit > MAX_CREDIT)
			credit = MAX_CREDIT;
	}
	credit -= len;
	qdp->credit = credit;
	bpf_spin_unlock(&qdp->lock);
	// End critical section

	// Check if we should update rate
	if (qsp != NULL && (qsp->rate * 128) != qdp->rate) {
		qdp->rate = qsp->rate * 128;
		bpf_printk("Updating rate: %d (1sec:%llu bits)\n",
			   (int)qdp->rate,
			   CREDIT_PER_NS(1000000000, qdp->rate) * 8);
	}

	// Set flags (drop, congestion, cwr)
	// Dropping => we are congested, so ignore congestion flag
	if (credit < -DROP_THRESH ||
	    (len > LARGE_PKT_THRESH &&
	     credit < -LARGE_PKT_DROP_THRESH)) {
		// Very congested, set drop flag
		drop_flag = true;
	} else if (credit < 0) {
		// Congested, set congestion flag
		if (pkti.ecn) {
			if (credit < -MARK_THRESH)
				congestion_flag = true;
			else
				congestion_flag = false;
		} else {
			congestion_flag = true;
		}
	}

	if (congestion_flag) {
		if (!bpf_skb_ecn_set_ce(skb)) {
			if (len > LARGE_PKT_THRESH) {
				// Problem if too many small packets?
				drop_flag = true;
			}
		}
	}

	if (drop_flag)
		rv = DROP_PKT;

	hbm_update_stats(qsp, len, curtime, congestion_flag, drop_flag);

	if (rv == DROP_PKT)
		__sync_add_and_fetch(&(qdp->credit), len);

	return rv;
}
char _license[] SEC("license") = "GPL";
