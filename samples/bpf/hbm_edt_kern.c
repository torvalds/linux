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
 * a user program, such as hbm, can access.
 */

#include "hbm_kern.h"

SEC("cgroup_skb/egress")
int _hbm_out_cg(struct __sk_buff *skb)
{
	long long delta = 0, delta_send;
	unsigned long long curtime, sendtime;
	struct hbm_queue_stats *qsp = NULL;
	unsigned int queue_index = 0;
	bool congestion_flag = false;
	bool ecn_ce_flag = false;
	struct hbm_pkt_info pkti = {};
	struct hbm_vqueue *qdp;
	bool drop_flag = false;
	bool cwr_flag = false;
	int len = skb->len;
	int rv = ALLOW_PKT;

	qsp = bpf_map_lookup_elem(&queue_stats, &queue_index);

	// Check if we should ignore loopback traffic
	if (qsp != NULL && !qsp->loopback && (skb->ifindex == 1))
		return ALLOW_PKT;

	hbm_get_pkt_info(skb, &pkti);

	// We may want to account for the length of headers in len
	// calculation, like ETH header + overhead, specially if it
	// is a gso packet. But I am not doing it right now.

	qdp = bpf_get_local_storage(&queue_state, 0);
	if (!qdp)
		return ALLOW_PKT;
	if (qdp->lasttime == 0)
		hbm_init_edt_vqueue(qdp, 1024);

	curtime = bpf_ktime_get_ns();

	// Begin critical section
	bpf_spin_lock(&qdp->lock);
	delta = qdp->lasttime - curtime;
	// bound bursts to 100us
	if (delta < -BURST_SIZE_NS) {
		// negative delta is a credit that allows bursts
		qdp->lasttime = curtime - BURST_SIZE_NS;
		delta = -BURST_SIZE_NS;
	}
	sendtime = qdp->lasttime;
	delta_send = BYTES_TO_NS(len, qdp->rate);
	__sync_add_and_fetch(&(qdp->lasttime), delta_send);
	bpf_spin_unlock(&qdp->lock);
	// End critical section

	// Set EDT of packet
	skb->tstamp = sendtime;

	// Check if we should update rate
	if (qsp != NULL && (qsp->rate * 128) != qdp->rate)
		qdp->rate = qsp->rate * 128;

	// Set flags (drop, congestion, cwr)
	// last packet will be sent in the future, bound latency
	if (delta > DROP_THRESH_NS || (delta > LARGE_PKT_DROP_THRESH_NS &&
				       len > LARGE_PKT_THRESH)) {
		drop_flag = true;
		if (pkti.is_tcp && pkti.ecn == 0)
			cwr_flag = true;
	} else if (delta > MARK_THRESH_NS) {
		if (pkti.is_tcp)
			congestion_flag = true;
		else
			drop_flag = true;
	}

	if (congestion_flag) {
		if (bpf_skb_ecn_set_ce(skb)) {
			ecn_ce_flag = true;
		} else {
			if (pkti.is_tcp) {
				unsigned int rand = bpf_get_prandom_u32();

				if (delta >= MARK_THRESH_NS +
				    (rand % MARK_REGION_SIZE_NS)) {
					// Do congestion control
					cwr_flag = true;
				}
			} else if (len > LARGE_PKT_THRESH) {
				// Problem if too many small packets?
				drop_flag = true;
				congestion_flag = false;
			}
		}
	}

	if (pkti.is_tcp && drop_flag && pkti.packets_out <= 1) {
		drop_flag = false;
		cwr_flag = true;
		congestion_flag = false;
	}

	if (qsp != NULL && qsp->no_cn)
			cwr_flag = false;

	hbm_update_stats(qsp, len, curtime, congestion_flag, drop_flag,
			 cwr_flag, ecn_ce_flag, &pkti, (int) delta);

	if (drop_flag) {
		__sync_add_and_fetch(&(qdp->lasttime), -delta_send);
		rv = DROP_PKT;
	}

	if (cwr_flag)
		rv |= CWR;
	return rv;
}
char _license[] SEC("license") = "GPL";
