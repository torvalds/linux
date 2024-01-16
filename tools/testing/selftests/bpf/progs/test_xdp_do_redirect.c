// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

#define ETH_ALEN 6
#define HDR_SZ (sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct udphdr))
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

	if (*payload == 0) {
		*payload = 0x42;
		pkts_seen_zero++;
	}

	if (bpf_xdp_adjust_meta(xdp, 4))
		return XDP_ABORTED;

	if (retcode > XDP_PASS)
		retcode--;

	if (ret == XDP_REDIRECT)
		return bpf_redirect(ifindex_out, 0);

	return ret;
}

static bool check_pkt(void *data, void *data_end)
{
	struct ipv6hdr *iph = data + sizeof(struct ethhdr);
	__u8 *payload = data + HDR_SZ;

	if (payload + 1 > data_end)
		return false;

	if (iph->nexthdr != IPPROTO_UDP || *payload != 0x42)
		return false;

	/* reset the payload so the same packet doesn't get counted twice when
	 * it cycles back through the kernel path and out the dst veth
	 */
	*payload = 0;
	return true;
}

SEC("xdp")
int xdp_count_pkts(struct xdp_md *xdp)
{
	void *data = (void *)(long)xdp->data;
	void *data_end = (void *)(long)xdp->data_end;

	if (check_pkt(data, data_end))
		pkts_seen_xdp++;

	/* Return XDP_DROP to make sure the data page is recycled, like when it
	 * exits a physical NIC. Recycled pages will be counted in the
	 * pkts_seen_zero counter above.
	 */
	return XDP_DROP;
}

SEC("tc")
int tc_count_pkts(struct __sk_buff *skb)
{
	void *data = (void *)(long)skb->data;
	void *data_end = (void *)(long)skb->data_end;

	if (check_pkt(data, data_end))
		pkts_seen_tc++;

	return 0;
}

char _license[] SEC("license") = "GPL";
