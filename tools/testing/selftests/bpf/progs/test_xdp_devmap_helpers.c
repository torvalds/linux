// SPDX-License-Identifier: GPL-2.0
/* fails to load without expected_attach_type = BPF_XDP_DEVMAP
 * because of access to egress_ifindex
 */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("xdp_dm_log")
int xdpdm_devlog(struct xdp_md *ctx)
{
	char fmt[] = "devmap redirect: dev %u -> dev %u len %u\n";
	void *data_end = (void *)(long)ctx->data_end;
	void *data = (void *)(long)ctx->data;
	unsigned int len = data_end - data;

	bpf_trace_printk(fmt, sizeof(fmt),
			 ctx->ingress_ifindex, ctx->egress_ifindex, len);

	return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
