/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 *
 *  Example howto extract XDP RX-queue info
 */
#include <uapi/linux/bpf.h>
#include <uapi/linux/if_ether.h>
#include <uapi/linux/in.h>
#include <bpf/bpf_helpers.h>

/* Config setup from with userspace
 *
 * User-side setup ifindex in config_map, to verify that
 * ctx->ingress_ifindex is correct (against configured ifindex)
 */
struct config {
	__u32 action;
	int ifindex;
	__u32 options;
};
enum cfg_options_flags {
	NO_TOUCH = 0x0U,
	READ_MEM = 0x1U,
	SWAP_MAC = 0x2U,
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct config);
	__uint(max_entries, 1);
} config_map SEC(".maps");

/* Common stats data record (shared with userspace) */
struct datarec {
	__u64 processed;
	__u64 issue;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, struct datarec);
	__uint(max_entries, 1);
} stats_global_map SEC(".maps");

#define MAX_RXQs 64

/* Stats per rx_queue_index (per CPU) */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, u32);
	__type(value, struct datarec);
	__uint(max_entries, MAX_RXQs + 1);
} rx_queue_index_map SEC(".maps");

static __always_inline
void swap_src_dst_mac(void *data)
{
	unsigned short *p = data;
	unsigned short dst[3];

	dst[0] = p[0];
	dst[1] = p[1];
	dst[2] = p[2];
	p[0] = p[3];
	p[1] = p[4];
	p[2] = p[5];
	p[3] = dst[0];
	p[4] = dst[1];
	p[5] = dst[2];
}

SEC("xdp_prog0")
int  xdp_prognum0(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data     = (void *)(long)ctx->data;
	struct datarec *rec, *rxq_rec;
	int ingress_ifindex;
	struct config *config;
	u32 key = 0;

	/* Global stats record */
	rec = bpf_map_lookup_elem(&stats_global_map, &key);
	if (!rec)
		return XDP_ABORTED;
	rec->processed++;

	/* Accessing ctx->ingress_ifindex, cause BPF to rewrite BPF
	 * instructions inside kernel to access xdp_rxq->dev->ifindex
	 */
	ingress_ifindex = ctx->ingress_ifindex;

	config = bpf_map_lookup_elem(&config_map, &key);
	if (!config)
		return XDP_ABORTED;

	/* Simple test: check ctx provided ifindex is as expected */
	if (ingress_ifindex != config->ifindex) {
		/* count this error case */
		rec->issue++;
		return XDP_ABORTED;
	}

	/* Update stats per rx_queue_index. Handle if rx_queue_index
	 * is larger than stats map can contain info for.
	 */
	key = ctx->rx_queue_index;
	if (key >= MAX_RXQs)
		key = MAX_RXQs;
	rxq_rec = bpf_map_lookup_elem(&rx_queue_index_map, &key);
	if (!rxq_rec)
		return XDP_ABORTED;
	rxq_rec->processed++;
	if (key == MAX_RXQs)
		rxq_rec->issue++;

	/* Default: Don't touch packet data, only count packets */
	if (unlikely(config->options & (READ_MEM|SWAP_MAC))) {
		struct ethhdr *eth = data;

		if (eth + 1 > data_end)
			return XDP_ABORTED;

		/* Avoid compiler removing this: Drop non 802.3 Ethertypes */
		if (ntohs(eth->h_proto) < ETH_P_802_3_MIN)
			return XDP_ABORTED;

		/* XDP_TX requires changing MAC-addrs, else HW may drop.
		 * Can also be enabled with --swapmac (for test purposes)
		 */
		if (unlikely(config->options & SWAP_MAC))
			swap_src_dst_mac(data);
	}

	return config->action;
}

char _license[] SEC("license") = "GPL";
