// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

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

struct meta {
	int ifindex;
	int pkt_len;
};

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__type(key, int);
	__type(value, int);
} perf_buf_map SEC(".maps");

__u64 test_result_fentry = 0;
SEC("fentry/FUNC")
int BPF_PROG(trace_on_entry, struct xdp_buff *xdp)
{
	struct meta meta;
	void *data_end = (void *)(long)xdp->data_end;
	void *data = (void *)(long)xdp->data;

	meta.ifindex = xdp->rxq->dev->ifindex;
	meta.pkt_len = bpf_xdp_get_buff_len((struct xdp_md *)xdp);
	bpf_xdp_output(xdp, &perf_buf_map,
		       ((__u64) meta.pkt_len << 32) |
		       BPF_F_CURRENT_CPU,
		       &meta, sizeof(meta));

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
