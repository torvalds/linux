// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

#define ETH_ALEN 6
#define HDR_SZ (sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct udphdr))

/**
 * enum frame_mark - magics to distinguish page/packet paths
 * @MARK_XMIT: page was recycled due to the frame being "xmitted" by the NIC.
 * @MARK_IN: frame is being processed by the input XDP prog.
 * @MARK_SKB: frame did hit the TC ingress hook as an skb.
 */
enum frame_mark {
	MARK_XMIT	= 0U,
	MARK_IN		= 0x42,
	MARK_SKB	= 0x45,
};

const volatile int ifindex_out;
const volatile int ifindex_in;
const volatile __u8 expect_dst[ETH_ALEN];
volatile int pkts_seen_xdp = 0;
volatile int pkts_seen_zero = 0;
volatile int pkts_seen_tc = 0;
volatile int retcode = XDP_REDIRECT;

SEC("xdp")
int xdp_redirect(struct xdp_md *xdp)
{
	__u32 *metadata = (void *)(long)xdp->data_meta;
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;

	__u8 *payload = data + HDR_SZ;
	int ret = retcode;

	if (payload + 1 > data_end)
		return XDP_ABORTED;

	if (xdp->ingress_ifindex != ifindex_in)
		return XDP_ABORTED;

	if (metadata + 1 > data)
		return XDP_ABORTED;

	if (*metadata != 0x42)
		return XDP_ABORTED;

	if (*payload == MARK_XMIT)
		pkts_seen_zero++;

	*payload = MARK_IN;

	if (bpf_xdp_adjust_meta(xdp, sizeof(__u64)))
		return XDP_ABORTED;

	if (retcode > XDP_PASS)
		retcode--;

	if (ret == XDP_REDIRECT)
		return bpf_redirect(ifindex_out, 0);

	return ret;
}

static bool check_pkt(void *data, void *data_end, const __u32 mark)
{
	struct ipv6hdr *iph = data + sizeof(struct ethhdr);
	__u8 *payload = data + HDR_SZ;

	if (payload + 1 > data_end)
		return false;

	if (iph->nexthdr != IPPROTO_UDP || *payload != MARK_IN)
		return false;

	/* reset the payload so the same packet doesn't get counted twice when
	 * it cycles back through the kernel path and out the dst veth
	 */
	*payload = mark;
	return true;
}

SEC("xdp")
int xdp_count_pkts(struct xdp_md *xdp)
{
	void *data = (void *)(long)xdp->data;
	void *data_end = (void *)(long)xdp->data_end;

	if (check_pkt(data, data_end, MARK_XMIT))
		pkts_seen_xdp++;

	/* Return %XDP_DROP to recycle the data page with %MARK_XMIT, like
	 * it exited a physical NIC. Those pages will be counted in the
	 * pkts_seen_zero counter above.
	 */
	return XDP_DROP;
}

SEC("tc")
int tc_count_pkts(struct __sk_buff *skb)
{
	void *data = (void *)(long)skb->data;
	void *data_end = (void *)(long)skb->data_end;

	if (check_pkt(data, data_end, MARK_SKB))
		pkts_seen_tc++;

	/* Will be either recycled or freed, %MARK_SKB makes sure it won't
	 * hit any of the counters above.
	 */
	return 0;
}

char _license[] SEC("license") = "GPL";
