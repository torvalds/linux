// SPDX-License-Identifier: GPL-2.0
#include <stddef.h>
#include <string.h>
#include <netinet/in.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/tcp.h>
#include "bpf_helpers.h"
#include "bpf_endian.h"
#include "test_tcpnotify.h"

struct bpf_map_def SEC("maps") global_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(__u32),
	.value_size = sizeof(struct tcpnotify_globals),
	.max_entries = 4,
};

struct bpf_map_def SEC("maps") perf_event_map = {
	.type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(__u32),
	.max_entries = 2,
};

int _version SEC("version") = 1;

SEC("sockops")
int bpf_testcb(struct bpf_sock_ops *skops)
{
	int rv = -1;
	int op;

	op = (int) skops->op;

	if (bpf_ntohl(skops->remote_port) != TESTPORT) {
		skops->reply = -1;
		return 0;
	}

	switch (op) {
	case BPF_SOCK_OPS_TIMEOUT_INIT:
	case BPF_SOCK_OPS_RWND_INIT:
	case BPF_SOCK_OPS_NEEDS_ECN:
	case BPF_SOCK_OPS_BASE_RTT:
	case BPF_SOCK_OPS_RTO_CB:
		rv = 1;
		break;

	case BPF_SOCK_OPS_TCP_CONNECT_CB:
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		bpf_sock_ops_cb_flags_set(skops, (BPF_SOCK_OPS_RETRANS_CB_FLAG|
					  BPF_SOCK_OPS_RTO_CB_FLAG));
		rv = 1;
		break;
	case BPF_SOCK_OPS_RETRANS_CB: {
			__u32 key = 0;
			struct tcpnotify_globals g, *gp;
			struct tcp_notifier msg = {
				.type = 0xde,
				.subtype = 0xad,
				.source = 0xbe,
				.hash = 0xef,
			};

			rv = 1;

			/* Update results */
			gp = bpf_map_lookup_elem(&global_map, &key);
			if (!gp)
				break;
			g = *gp;
			g.total_retrans = skops->total_retrans;
			g.ncalls++;
			bpf_map_update_elem(&global_map, &key, &g,
					    BPF_ANY);
			bpf_perf_event_output(skops, &perf_event_map,
					      BPF_F_CURRENT_CPU,
					      &msg, sizeof(msg));
		}
		break;
	default:
		rv = -1;
	}
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
