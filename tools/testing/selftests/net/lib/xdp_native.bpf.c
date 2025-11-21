// SPDX-License-Identifier: GPL-2.0

#include <stddef.h>
#include <linux/bpf.h>
#include <linux/in.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

#define MAX_ADJST_OFFSET 256
#define MAX_PAYLOAD_LEN 5000
#define MAX_HDR_LEN 64

extern int bpf_xdp_pull_data(struct xdp_md *xdp, __u32 len) __ksym __weak;

enum {
	XDP_MODE = 0,
	XDP_PORT = 1,
	XDP_ADJST_OFFSET = 2,
	XDP_ADJST_TAG = 3,
} xdp_map_setup_keys;

enum {
	XDP_MODE_PASS = 0,
	XDP_MODE_DROP = 1,
	XDP_MODE_TX = 2,
	XDP_MODE_TAIL_ADJST = 3,
	XDP_MODE_HEAD_ADJST = 4,
} xdp_map_modes;

enum {
	STATS_RX = 0,
	STATS_PASS = 1,
	STATS_DROP = 2,
	STATS_TX = 3,
	STATS_ABORT = 4,
} xdp_stats;

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 5);
	__type(key, __u32);
	__type(value, __s32);
} map_xdp_setup SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 5);
	__type(key, __u32);
	__type(value, __u64);
} map_xdp_stats SEC(".maps");

static __u32 min(__u32 a, __u32 b)
{
	return a < b ? a : b;
}

static void record_stats(struct xdp_md *ctx, __u32 stat_type)
{
	__u64 *count;

	count = bpf_map_lookup_elem(&map_xdp_stats, &stat_type);

	if (count)
		__sync_fetch_and_add(count, 1);
}

static struct udphdr *filter_udphdr(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph = NULL;
	void *data, *data_end;
	struct ethhdr *eth;
	int err;

	err = bpf_xdp_pull_data(ctx, sizeof(*eth));
	if (err)
		return NULL;

	data_end = (void *)(long)ctx->data_end;
	data = eth = (void *)(long)ctx->data;

	if (data + sizeof(*eth) > data_end)
		return NULL;

	if (eth->h_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph;

		err = bpf_xdp_pull_data(ctx, sizeof(*eth) + sizeof(*iph) +
					     sizeof(*udph));
		if (err)
			return NULL;

		data_end = (void *)(long)ctx->data_end;
		data = (void *)(long)ctx->data;

		iph = data + sizeof(*eth);

		if (iph + 1 > (struct iphdr *)data_end ||
		    iph->protocol != IPPROTO_UDP)
			return NULL;

		udph = data + sizeof(*iph) + sizeof(*eth);
	} else if (eth->h_proto == bpf_htons(ETH_P_IPV6)) {
		struct ipv6hdr *ipv6h;

		err = bpf_xdp_pull_data(ctx, sizeof(*eth) + sizeof(*ipv6h) +
					     sizeof(*udph));
		if (err)
			return NULL;

		data_end = (void *)(long)ctx->data_end;
		data = (void *)(long)ctx->data;

		ipv6h = data + sizeof(*eth);

		if (ipv6h + 1 > (struct ipv6hdr *)data_end ||
		    ipv6h->nexthdr != IPPROTO_UDP)
			return NULL;

		udph = data + sizeof(*ipv6h) + sizeof(*eth);
	} else {
		return NULL;
	}

	if (udph + 1 > (struct udphdr *)data_end)
		return NULL;

	if (udph->dest != bpf_htons(port))
		return NULL;

	record_stats(ctx, STATS_RX);

	return udph;
}

static int xdp_mode_pass(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph = NULL;

	udph = filter_udphdr(ctx, port);
	if (!udph)
		return XDP_PASS;

	record_stats(ctx, STATS_PASS);

	return XDP_PASS;
}

static int xdp_mode_drop_handler(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph = NULL;

	udph = filter_udphdr(ctx, port);
	if (!udph)
		return XDP_PASS;

	record_stats(ctx, STATS_DROP);

	return XDP_DROP;
}

static void swap_machdr(void *data)
{
	struct ethhdr *eth = data;
	__u8 tmp_mac[ETH_ALEN];

	__builtin_memcpy(tmp_mac, eth->h_source, ETH_ALEN);
	__builtin_memcpy(eth->h_source, eth->h_dest, ETH_ALEN);
	__builtin_memcpy(eth->h_dest, tmp_mac, ETH_ALEN);
}

static int xdp_mode_tx_handler(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph = NULL;
	void *data, *data_end;
	struct ethhdr *eth;
	int err;

	err = bpf_xdp_pull_data(ctx, sizeof(*eth));
	if (err)
		return XDP_PASS;

	data_end = (void *)(long)ctx->data_end;
	data = eth = (void *)(long)ctx->data;

	if (data + sizeof(*eth) > data_end)
		return XDP_PASS;

	if (eth->h_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph;
		__be32 tmp_ip;

		err = bpf_xdp_pull_data(ctx, sizeof(*eth) + sizeof(*iph) +
					     sizeof(*udph));
		if (err)
			return XDP_PASS;

		data_end = (void *)(long)ctx->data_end;
		data = (void *)(long)ctx->data;

		iph = data + sizeof(*eth);

		if (iph + 1 > (struct iphdr *)data_end ||
		    iph->protocol != IPPROTO_UDP)
			return XDP_PASS;

		udph = data + sizeof(*iph) + sizeof(*eth);

		if (udph + 1 > (struct udphdr *)data_end)
			return XDP_PASS;
		if (udph->dest != bpf_htons(port))
			return XDP_PASS;

		record_stats(ctx, STATS_RX);
		eth = data;
		swap_machdr((void *)eth);

		tmp_ip = iph->saddr;
		iph->saddr = iph->daddr;
		iph->daddr = tmp_ip;

		record_stats(ctx, STATS_TX);

		return XDP_TX;

	} else if (eth->h_proto == bpf_htons(ETH_P_IPV6)) {
		struct in6_addr tmp_ipv6;
		struct ipv6hdr *ipv6h;

		err = bpf_xdp_pull_data(ctx, sizeof(*eth) + sizeof(*ipv6h) +
					     sizeof(*udph));
		if (err)
			return XDP_PASS;

		data_end = (void *)(long)ctx->data_end;
		data = (void *)(long)ctx->data;

		ipv6h = data + sizeof(*eth);

		if (ipv6h + 1 > (struct ipv6hdr *)data_end ||
		    ipv6h->nexthdr != IPPROTO_UDP)
			return XDP_PASS;

		udph = data + sizeof(*ipv6h) + sizeof(*eth);

		if (udph + 1 > (struct udphdr *)data_end)
			return XDP_PASS;
		if (udph->dest != bpf_htons(port))
			return XDP_PASS;

		record_stats(ctx, STATS_RX);
		eth = data;
		swap_machdr((void *)eth);

		__builtin_memcpy(&tmp_ipv6, &ipv6h->saddr, sizeof(tmp_ipv6));
		__builtin_memcpy(&ipv6h->saddr, &ipv6h->daddr,
				 sizeof(tmp_ipv6));
		__builtin_memcpy(&ipv6h->daddr, &tmp_ipv6, sizeof(tmp_ipv6));

		record_stats(ctx, STATS_TX);

		return XDP_TX;
	}

	return XDP_PASS;
}

static void *update_pkt(struct xdp_md *ctx, __s16 offset, __u32 *udp_csum)
{
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	struct udphdr *udph = NULL;
	struct ethhdr *eth = data;
	__u32 len, len_new;

	if (data + sizeof(*eth) > data_end)
		return NULL;

	if (eth->h_proto == bpf_htons(ETH_P_IP)) {
		struct iphdr *iph = data + sizeof(*eth);
		__u16 total_len;

		if (iph + 1 > (struct iphdr *)data_end)
			return NULL;

		iph->tot_len = bpf_htons(bpf_ntohs(iph->tot_len) + offset);

		udph = (void *)eth + sizeof(*iph) + sizeof(*eth);
		if (!udph || udph + 1 > (struct udphdr *)data_end)
			return NULL;

		len_new = bpf_htons(bpf_ntohs(udph->len) + offset);
	} else if (eth->h_proto  == bpf_htons(ETH_P_IPV6)) {
		struct ipv6hdr *ipv6h = data + sizeof(*eth);
		__u16 payload_len;

		if (ipv6h + 1 > (struct ipv6hdr *)data_end)
			return NULL;

		udph = (void *)eth + sizeof(*ipv6h) + sizeof(*eth);
		if (!udph || udph + 1 > (struct udphdr *)data_end)
			return NULL;

		*udp_csum = ~((__u32)udph->check);

		len = ipv6h->payload_len;
		len_new = bpf_htons(bpf_ntohs(len) + offset);
		ipv6h->payload_len = len_new;

		*udp_csum = bpf_csum_diff(&len, sizeof(len), &len_new,
					  sizeof(len_new), *udp_csum);

		len = udph->len;
		len_new = bpf_htons(bpf_ntohs(udph->len) + offset);
		*udp_csum = bpf_csum_diff(&len, sizeof(len), &len_new,
					  sizeof(len_new), *udp_csum);
	} else {
		return NULL;
	}

	udph->len = len_new;

	return udph;
}

static __u16 csum_fold_helper(__u32 csum)
{
	return ~((csum & 0xffff) + (csum >> 16)) ? : 0xffff;
}

static int xdp_adjst_tail_shrnk_data(struct xdp_md *ctx, __u16 offset,
				     __u32 hdr_len)
{
	char tmp_buff[MAX_ADJST_OFFSET];
	__u32 buff_pos, udp_csum = 0;
	struct udphdr *udph = NULL;
	__u32 buff_len;

	udph = update_pkt(ctx, 0 - offset, &udp_csum);
	if (!udph)
		return -1;

	buff_len = bpf_xdp_get_buff_len(ctx);

	offset = (offset & 0x1ff) >= MAX_ADJST_OFFSET ? MAX_ADJST_OFFSET :
				     offset & 0xff;
	if (offset == 0)
		return -1;

	/* Make sure we have enough data to avoid eating the header */
	if (buff_len - offset < hdr_len)
		return -1;

	buff_pos = buff_len - offset;
	if (bpf_xdp_load_bytes(ctx, buff_pos, tmp_buff, offset) < 0)
		return -1;

	udp_csum = bpf_csum_diff((__be32 *)tmp_buff, offset, 0, 0, udp_csum);
	udph->check = (__u16)csum_fold_helper(udp_csum);

	if (bpf_xdp_adjust_tail(ctx, 0 - offset) < 0)
		return -1;

	return 0;
}

static int xdp_adjst_tail_grow_data(struct xdp_md *ctx, __u16 offset)
{
	char tmp_buff[MAX_ADJST_OFFSET];
	__u32 buff_pos, udp_csum = 0;
	__u32 buff_len, hdr_len, key;
	struct udphdr *udph;
	__s32 *val;
	__u8 tag;

	/* Proceed to update the packet headers before attempting to adjuste
	 * the tail. Once the tail is adjusted we lose access to the offset
	 * amount of data at the end of the packet which is crucial to update
	 * the checksum.
	 * Since any failure beyond this would abort the packet, we should
	 * not worry about passing a packet up the stack with wrong headers
	 */
	udph = update_pkt(ctx, offset, &udp_csum);
	if (!udph)
		return -1;

	key = XDP_ADJST_TAG;
	val = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!val)
		return -1;

	tag = (__u8)(*val);

	for (int i = 0; i < MAX_ADJST_OFFSET; i++)
		__builtin_memcpy(&tmp_buff[i], &tag, 1);

	offset = (offset & 0x1ff) >= MAX_ADJST_OFFSET ? MAX_ADJST_OFFSET :
				     offset & 0xff;
	if (offset == 0)
		return -1;

	udp_csum = bpf_csum_diff(0, 0, (__be32 *)tmp_buff, offset, udp_csum);
	udph->check = (__u16)csum_fold_helper(udp_csum);

	buff_len = bpf_xdp_get_buff_len(ctx);

	if (bpf_xdp_adjust_tail(ctx, offset) < 0) {
		bpf_printk("Failed to adjust tail\n");
		return -1;
	}

	if (bpf_xdp_store_bytes(ctx, buff_len, tmp_buff, offset) < 0)
		return -1;

	return 0;
}

static int xdp_adjst_tail(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph = NULL;
	__s32 *adjust_offset, *val;
	__u32 key, hdr_len;
	void *offset_ptr;
	__u8 tag;
	int ret;

	udph = filter_udphdr(ctx, port);
	if (!udph)
		return XDP_PASS;

	hdr_len = (void *)udph - (void *)(long)ctx->data +
		  sizeof(struct udphdr);
	key = XDP_ADJST_OFFSET;
	adjust_offset = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!adjust_offset)
		return XDP_PASS;

	if (*adjust_offset < 0)
		ret = xdp_adjst_tail_shrnk_data(ctx,
						(__u16)(0 - *adjust_offset),
						hdr_len);
	else
		ret = xdp_adjst_tail_grow_data(ctx, (__u16)(*adjust_offset));
	if (ret)
		goto abort_pkt;

	record_stats(ctx, STATS_PASS);
	return XDP_PASS;

abort_pkt:
	record_stats(ctx, STATS_ABORT);
	return XDP_ABORTED;
}

static int xdp_adjst_head_shrnk_data(struct xdp_md *ctx, __u64 hdr_len,
				     __u32 offset)
{
	char tmp_buff[MAX_ADJST_OFFSET];
	struct udphdr *udph;
	void *offset_ptr;
	__u32 udp_csum = 0;

	/* Update the length information in the IP and UDP headers before
	 * adjusting the headroom. This simplifies accessing the relevant
	 * fields in the IP and UDP headers for fragmented packets. Any
	 * failure beyond this point will result in the packet being aborted,
	 * so we don't need to worry about incorrect length information for
	 * passed packets.
	 */
	udph = update_pkt(ctx, (__s16)(0 - offset), &udp_csum);
	if (!udph)
		return -1;

	offset = (offset & 0x1ff) >= MAX_ADJST_OFFSET ? MAX_ADJST_OFFSET :
				     offset & 0xff;
	if (offset == 0)
		return -1;

	if (bpf_xdp_load_bytes(ctx, hdr_len, tmp_buff, offset) < 0)
		return -1;

	udp_csum = bpf_csum_diff((__be32 *)tmp_buff, offset, 0, 0, udp_csum);

	udph->check = (__u16)csum_fold_helper(udp_csum);

	if (bpf_xdp_load_bytes(ctx, 0, tmp_buff, MAX_ADJST_OFFSET) < 0)
		return -1;

	if (bpf_xdp_adjust_head(ctx, offset) < 0)
		return -1;

	if (offset > MAX_ADJST_OFFSET)
		return -1;

	if (hdr_len > MAX_ADJST_OFFSET || hdr_len == 0)
		return -1;

	/* Added here to handle clang complain about negative value */
	hdr_len = hdr_len & 0xff;

	if (hdr_len == 0)
		return -1;

	if (bpf_xdp_store_bytes(ctx, 0, tmp_buff, hdr_len) < 0)
		return -1;

	return 0;
}

static int xdp_adjst_head_grow_data(struct xdp_md *ctx, __u64 hdr_len,
				    __u32 offset)
{
	char hdr_buff[MAX_HDR_LEN];
	char data_buff[MAX_ADJST_OFFSET];
	void *offset_ptr;
	__s32 *val;
	__u32 key;
	__u8 tag;
	__u32 udp_csum = 0;
	struct udphdr *udph;

	udph = update_pkt(ctx, (__s16)(offset), &udp_csum);
	if (!udph)
		return -1;

	key = XDP_ADJST_TAG;
	val = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!val)
		return -1;

	tag = (__u8)(*val);
	for (int i = 0; i < MAX_ADJST_OFFSET; i++)
		__builtin_memcpy(&data_buff[i], &tag, 1);

	offset = (offset & 0x1ff) >= MAX_ADJST_OFFSET ? MAX_ADJST_OFFSET :
				     offset & 0xff;
	if (offset == 0)
		return -1;

	udp_csum = bpf_csum_diff(0, 0, (__be32 *)data_buff, offset, udp_csum);
	udph->check = (__u16)csum_fold_helper(udp_csum);

	if (hdr_len > MAX_ADJST_OFFSET || hdr_len == 0)
		return -1;

	/* Added here to handle clang complain about negative value */
	hdr_len = hdr_len & 0xff;

	if (hdr_len == 0)
		return -1;

	if (bpf_xdp_load_bytes(ctx, 0, hdr_buff, hdr_len) < 0)
		return -1;

	if (offset > MAX_ADJST_OFFSET)
		return -1;

	if (bpf_xdp_adjust_head(ctx, 0 - offset) < 0)
		return -1;

	if (bpf_xdp_store_bytes(ctx, 0, hdr_buff, hdr_len) < 0)
		return -1;

	if (bpf_xdp_store_bytes(ctx, hdr_len, data_buff, offset) < 0)
		return -1;

	return 0;
}

static int xdp_head_adjst(struct xdp_md *ctx, __u16 port)
{
	struct udphdr *udph_ptr = NULL;
	__u32 key, size, hdr_len;
	__s32 *val;
	int res;

	/* Filter packets based on UDP port */
	udph_ptr = filter_udphdr(ctx, port);
	if (!udph_ptr)
		return XDP_PASS;

	hdr_len = (void *)udph_ptr - (void *)(long)ctx->data +
		  sizeof(struct udphdr);

	key = XDP_ADJST_OFFSET;
	val = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!val)
		return XDP_PASS;

	switch (*val) {
	case -16:
	case 16:
		size = 16;
		break;
	case -32:
	case 32:
		size = 32;
		break;
	case -64:
	case 64:
		size = 64;
		break;
	case -128:
	case 128:
		size = 128;
		break;
	case -256:
	case 256:
		size = 256;
		break;
	default:
		bpf_printk("Invalid adjustment offset: %d\n", *val);
		goto abort;
	}

	if (*val < 0)
		res = xdp_adjst_head_grow_data(ctx, hdr_len, size);
	else
		res = xdp_adjst_head_shrnk_data(ctx, hdr_len, size);

	if (res)
		goto abort;

	record_stats(ctx, STATS_PASS);
	return XDP_PASS;

abort:
	record_stats(ctx, STATS_ABORT);
	return XDP_ABORTED;
}

static int xdp_prog_common(struct xdp_md *ctx)
{
	__u32 key, *port;
	__s32 *mode;

	key = XDP_MODE;
	mode = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!mode)
		return XDP_PASS;

	key = XDP_PORT;
	port = bpf_map_lookup_elem(&map_xdp_setup, &key);
	if (!port)
		return XDP_PASS;

	switch (*mode) {
	case XDP_MODE_PASS:
		return xdp_mode_pass(ctx, (__u16)(*port));
	case XDP_MODE_DROP:
		return xdp_mode_drop_handler(ctx, (__u16)(*port));
	case XDP_MODE_TX:
		return xdp_mode_tx_handler(ctx, (__u16)(*port));
	case XDP_MODE_TAIL_ADJST:
		return xdp_adjst_tail(ctx, (__u16)(*port));
	case XDP_MODE_HEAD_ADJST:
		return xdp_head_adjst(ctx, (__u16)(*port));
	}

	/* Default action is to simple pass */
	return XDP_PASS;
}

SEC("xdp")
int xdp_prog(struct xdp_md *ctx)
{
	return xdp_prog_common(ctx);
}

SEC("xdp.frags")
int xdp_prog_frags(struct xdp_md *ctx)
{
	return xdp_prog_common(ctx);
}

char _license[] SEC("license") = "GPL";
