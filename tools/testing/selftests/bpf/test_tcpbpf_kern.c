// SPDX-License-Identifier: GPL-2.0
#include <stddef.h>
#include <string.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"
#include "test_tcpbpf.h"

struct bpf_map_def SEC("maps") global_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(struct tcpbpf_globals),
	.max_entries = 2,
};

static inline void update_event_map(int event)
{
	__u32 key = 0;
	struct tcpbpf_globals g, *gp;

	gp = bpf_map_lookup_elem(&global_map, &key);
	if (gp == NULL) {
		struct tcpbpf_globals g = {0};

		g.event_map |= (1 << event);
		bpf_map_update_elem(&global_map, &key, &g,
			    BPF_ANY);
	} else {
		g = *gp;
		g.event_map |= (1 << event);
		bpf_map_update_elem(&global_map, &key, &g,
			    BPF_ANY);
	}
}

int _version SEC("version") = 1;

SEC("sockops")
int bpf_testcb(struct bpf_sock_ops *skops)
{
	int rv = -1;
	int bad_call_rv = 0;
	int good_call_rv = 0;
	int op;
	int v = 0;

	op = (int) skops->op;

	update_event_map(op);

	switch (op) {
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		/* Test failure to set largest cb flag (assumes not defined) */
		bad_call_rv = bpf_sock_ops_cb_flags_set(skops, 0x80);
		/* Set callback */
		good_call_rv = bpf_sock_ops_cb_flags_set(skops,
						 BPF_SOCK_OPS_STATE_CB_FLAG);
		/* Update results */
		{
			__u32 key = 0;
			struct tcpbpf_globals g, *gp;

			gp = bpf_map_lookup_elem(&global_map, &key);
			if (!gp)
				break;
			g = *gp;
			g.bad_cb_test_rv = bad_call_rv;
			g.good_cb_test_rv = good_call_rv;
			bpf_map_update_elem(&global_map, &key, &g,
					    BPF_ANY);
		}
		break;
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		skops->sk_txhash = 0x12345f;
		v = 0xff;
		rv = bpf_setsockopt(skops, SOL_IPV6, IPV6_TCLASS, &v,
				    sizeof(v));
		break;
	case BPF_SOCK_OPS_RTO_CB:
		break;
	case BPF_SOCK_OPS_RETRANS_CB:
		break;
	case BPF_SOCK_OPS_STATE_CB:
		if (skops->args[1] == BPF_TCP_CLOSE) {
			__u32 key = 0;
			struct tcpbpf_globals g, *gp;

			gp = bpf_map_lookup_elem(&global_map, &key);
			if (!gp)
				break;
			g = *gp;
			if (skops->args[0] == BPF_TCP_LISTEN) {
				g.num_listen++;
			} else {
				g.total_retrans = skops->total_retrans;
				g.data_segs_in = skops->data_segs_in;
				g.data_segs_out = skops->data_segs_out;
				g.bytes_received = skops->bytes_received;
				g.bytes_acked = skops->bytes_acked;
			}
			bpf_map_update_elem(&global_map, &key, &g,
					    BPF_ANY);
		}
		break;
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		bpf_sock_ops_cb_flags_set(skops, BPF_SOCK_OPS_STATE_CB_FLAG);
		break;
	default:
		rv = -1;
	}
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
