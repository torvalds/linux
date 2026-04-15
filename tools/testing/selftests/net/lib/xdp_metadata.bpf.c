// SPDX-License-Identifier: GPL-2.0

#include <stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

enum {
	XDP_PORT = 1,
	XDP_PROTO = 4,
} xdp_map_setup_keys;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 5);
	__type(key, __u32);
	__type(value, __s32);
} map_xdp_setup SEC(".maps");

/* RSS hash results: key 0 = hash, key 1 = hash type,
 * key 2 = packet count, key 3 = error count.
 */
enum {
	RSS_KEY_HASH = 0,
	RSS_KEY_TYPE = 1,
	RSS_KEY_PKT_CNT = 2,
	RSS_KEY_ERR_CNT = 3,
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, __u32);
	__type(value, __u32);
	__uint(max_entries, 4);
} map_rss SEC(".maps");

/* Mirror of enum xdp_rss_hash_type from include/net/xdp.h.
 * Needed because the enum is not part of UAPI headers.
 */
enum xdp_rss_hash_type {
	XDP_RSS_L3_IPV4 = 1U << 0,
	XDP_RSS_L3_IPV6 = 1U << 1,
	XDP_RSS_L3_DYNHDR = 1U << 2,
	XDP_RSS_L4 = 1U << 3,
	XDP_RSS_L4_TCP = 1U << 4,
	XDP_RSS_L4_UDP = 1U << 5,
	XDP_RSS_L4_SCTP = 1U << 6,
	XDP_RSS_L4_IPSEC = 1U << 7,
	XDP_RSS_L4_ICMP = 1U << 8,
};

extern int bpf_xdp_metadata_rx_hash(const struct xdp_md *ctx, __u32 *hash,
				    enum xdp_rss_hash_type *rss_type) __ksym;

static __always_inline __u16 get_dest_port(void *l4, void *data_end,
					   __u8 protocol)
{
	if (protocol == IPPROTO_UDP) {
		struct udphdr *udp = l4;

		if ((void *)(udp + 1) > data_end)
			return 0;
		return udp->dest;
	} else if (protocol == IPPROTO_TCP) {
		struct tcphdr *tcp = l4;

		if ((void *)(tcp + 1) > data_end)
			return 0;
		return tcp->dest;
	}

	return 0;
}

SEC("xdp")
int xdp_rss_hash(struct xdp_md *ctx)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	enum xdp_rss_hash_type rss_type = 0;
	struct ethhdr *eth = data;
	__u8 l4_proto = 0;
	__u32 hash = 0;
	__u32 key, val;
	void *l4 = NULL;
	__u32 *cnt;
	int ret;

	if ((void *)(eth + 1) > data_end)
		return XDP_PASS;

	if (eth->h_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph = (void *)(eth + 1);

		if ((void *)(iph + 1) > data_end)
			return XDP_PASS;
		l4_proto = iph->protocol;
		l4 = (void *)(iph + 1);
	} else if (eth->h_proto == bpf_htons(ETH_P_IPV6)) {
		struct ipv6hdr *ip6h = (void *)(eth + 1);

		if ((void *)(ip6h + 1) > data_end)
			return XDP_PASS;
		l4_proto = ip6h->nexthdr;
		l4 = (void *)(ip6h + 1);
	}

	if (!l4)
		return XDP_PASS;

	/* Filter on the configured protocol (map_xdp_setup key XDP_PROTO).
	 * When set, only process packets matching the requested L4 protocol.
	 */
	key = XDP_PROTO;
	__s32 *proto_cfg = bpf_map_lookup_elem(&map_xdp_setup, &key);

	if (proto_cfg && *proto_cfg != 0 && l4_proto != (__u8)*proto_cfg)
		return XDP_PASS;

	/* Filter on the configured port (map_xdp_setup key XDP_PORT).
	 * Only applies to protocols with ports (UDP, TCP).
	 */
	key = XDP_PORT;
	__s32 *port_cfg = bpf_map_lookup_elem(&map_xdp_setup, &key);

	if (port_cfg && *port_cfg != 0) {
		__u16 dest = get_dest_port(l4, data_end, l4_proto);

		if (!dest || bpf_ntohs(dest) != (__u16)*port_cfg)
			return XDP_PASS;
	}

	ret = bpf_xdp_metadata_rx_hash(ctx, &hash, &rss_type);
	if (ret < 0) {
		key = RSS_KEY_ERR_CNT;
		cnt = bpf_map_lookup_elem(&map_rss, &key);
		if (cnt)
			__sync_fetch_and_add(cnt, 1);
		return XDP_PASS;
	}

	key = RSS_KEY_HASH;
	bpf_map_update_elem(&map_rss, &key, &hash, BPF_ANY);

	key = RSS_KEY_TYPE;
	val = (__u32)rss_type;
	bpf_map_update_elem(&map_rss, &key, &val, BPF_ANY);

	key = RSS_KEY_PKT_CNT;
	cnt = bpf_map_lookup_elem(&map_rss, &key);
	if (cnt)
		__sync_fetch_and_add(cnt, 1);

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
