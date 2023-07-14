// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int _xdp_adjust_tail_grow(struct xdp_md *xdp)
{
	int data_len = bpf_xdp_get_buff_len(xdp);
	int offset = 0;
	/* SKB_DATA_ALIGN(sizeof(struct skb_shared_info)) */
#if defined(__TARGET_ARCH_s390)
	int tailroom = 512;
#else
	int tailroom = 320;
#endif

	/* Data length determine test case */

	if (data_len == 54) { /* sizeof(pkt_v4) */
		offset = 4096; /* test too large offset */
	} else if (data_len == 74) { /* sizeof(pkt_v6) */
		offset = 40;
	} else if (data_len == 64) {
		offset = 128;
	} else if (data_len == 128) {
		/* Max tail grow 3520 */
		offset = 4096 - 256 - tailroom - data_len;
	} else if (data_len == 9000) {
		offset = 10;
	} else if (data_len == 9001) {
		offset = 4096;
	} else {
		return XDP_ABORTED; /* No matching test */
	}

	if (bpf_xdp_adjust_tail(xdp, offset))
		return XDP_DROP;
	return XDP_TX;
}

char _license[] SEC("license") = "GPL";
