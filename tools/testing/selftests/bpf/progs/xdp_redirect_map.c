// SPDX-License-Identifier: GPL-2.0

#include <linux/if_ether.h>

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(max_entries, 8);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
} tx_port SEC(".maps");

SEC("xdp")
int xdp_redirect_map_0(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 0, 0);
}

SEC("xdp")
int xdp_redirect_map_1(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 1, 0);
}

SEC("xdp")
int xdp_redirect_map_2(struct xdp_md *xdp)
{
	return bpf_redirect_map(&tx_port, 2, 0);
}

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 3);
	__type(key, __u32);
	__type(value, __u64);
} rxcnt SEC(".maps");

static int xdp_count(struct xdp_md *xdp, __u32 key)
{
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;
	struct ethhdr *eth = data;
	__u64 *count;

	if (data + sizeof(*eth) > data_end)
		return XDP_DROP;

	if (bpf_htons(eth->h_proto) == ETH_P_IP) {
		/* We only count IPv4 packets */
		count = bpf_map_lookup_elem(&rxcnt, &key);
		if (count)
			*count += 1;
	}

	return XDP_PASS;
}

SEC("xdp")
int xdp_count_0(struct xdp_md *xdp)
{
	return xdp_count(xdp, 0);
}

SEC("xdp")
int xdp_count_1(struct xdp_md *xdp)
{
	return xdp_count(xdp, 1);
}

SEC("xdp")
int xdp_count_2(struct xdp_md *xdp)
{
	return xdp_count(xdp, 2);
}

char _license[] SEC("license") = "GPL";
