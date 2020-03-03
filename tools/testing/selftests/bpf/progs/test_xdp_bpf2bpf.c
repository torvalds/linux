// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

struct net_device {
	/* Structure does not need to contain all entries,
	 * as "preserve_access_index" will use BTF to fix this...
	 */
	int ifindex;
} __attribute__((preserve_access_index));

struct xdp_rxq_info {
	/* Structure does not need to contain all entries,
	 * as "preserve_access_index" will use BTF to fix this...
	 */
	struct net_device *dev;
	__u32 queue_index;
} __attribute__((preserve_access_index));

struct xdp_buff {
	void *data;
	void *data_end;
	void *data_meta;
	void *data_hard_start;
	unsigned long handle;
	struct xdp_rxq_info *rxq;
} __attribute__((preserve_access_index));

__u64 test_result_fentry = 0;
SEC("fentry/FUNC")
int BPF_PROG(trace_on_entry, struct xdp_buff *xdp)
{
	test_result_fentry = xdp->rxq->dev->ifindex;
	return 0;
}

__u64 test_result_fexit = 0;
SEC("fexit/FUNC")
int BPF_PROG(trace_on_exit, struct xdp_buff *xdp, int ret)
{
	test_result_fexit = ret;
	return 0;
}
