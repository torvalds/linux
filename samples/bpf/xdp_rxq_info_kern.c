/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2017 Jesper Dangaard Brouer, Red Hat Inc.
 *
 *  Example howto extract XDP RX-queue info
 */
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

/* Config setup from with userspace
 *
 * User-side setup ifindex in config_map, to verify that
 * ctx->ingress_ifindex is correct (against configured ifindex)
 */
struct config {
	__u32 action;
	int ifindex;
};
struct bpf_map_def SEC("maps") config_map = {
	.type		= BPF_MAP_TYPE_ARRAY,
	.key_size	= sizeof(int),
	.value_size	= sizeof(struct config),
	.max_entries	= 1,
};

/* Common stats data record (shared with userspace) */
struct datarec {
	__u64 processed;
	__u64 issue;
};

struct bpf_map_def SEC("maps") stats_global_map = {
	.type		= BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size	= sizeof(u32),
	.value_size	= sizeof(struct datarec),
	.max_entries	= 1,
};

#define MAX_RXQs 64

/* Stats per rx_queue_index (per CPU) */
struct bpf_map_def SEC("maps") rx_queue_index_map = {
	.type		= BPF_MAP_TYPE_PERCPU_ARRAY,
	.key_size	= sizeof(u32),
	.value_size	= sizeof(struct datarec),
	.max_entries	= MAX_RXQs + 1,
};

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

	return config->action;
}

char _license[] SEC("license") = "GPL";
