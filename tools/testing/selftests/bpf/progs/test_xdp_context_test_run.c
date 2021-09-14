// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp")
int xdp_context(struct xdp_md *xdp)
{
	void *data = (void *)(long)xdp->data;
	__u32 *metadata = (void *)(long)xdp->data_meta;
	__u32 ret;

	if (metadata + 1 > data)
		return XDP_ABORTED;
	ret = *metadata;
	if (bpf_xdp_adjust_meta(xdp, 4))
		return XDP_ABORTED;
	return ret;
}

char _license[] SEC("license") = "GPL";
