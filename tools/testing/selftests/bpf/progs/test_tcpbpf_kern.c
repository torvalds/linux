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
#include "test_tcpbpf.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 4);
	__type(key, __u32);
	__type(value, struct tcpbpf_globals);
} global_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, int);
} sockopt_results SEC(".maps");

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
	char header[sizeof(struct ipv6hdr) + sizeof(struct tcphdr)];
	struct tcphdr *thdr;
	int good_call_rv = 0;
	int bad_call_rv = 0;
	int save_syn = 1;
	int rv = -1;
	int v = 0;
	int op;

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
		if (skops->family == AF_INET6) {
			v = bpf_getsockopt(skops, IPPROTO_TCP, TCP_SAVED_SYN,
					   header, (sizeof(struct ipv6hdr) +
						    sizeof(struct tcphdr)));
			if (!v) {
				int offset = sizeof(struct ipv6hdr);

				thdr = (struct tcphdr *)(header + offset);
				v = thdr->syn;
				__u32 key = 1;

				bpf_map_update_elem(&sockopt_results, &key, &v,
						    BPF_ANY);
			}
		}
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
			g.num_close_events++;
			bpf_map_update_elem(&global_map, &key, &g,
					    BPF_ANY);
		}
		break;
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		bpf_sock_ops_cb_flags_set(skops, BPF_SOCK_OPS_STATE_CB_FLAG);
		v = bpf_setsockopt(skops, IPPROTO_TCP, TCP_SAVE_SYN,
				   &save_syn, sizeof(save_syn));
		/* Update global map w/ result of setsock opt */
		__u32 key = 0;

		bpf_map_update_elem(&sockopt_results, &key, &v, BPF_ANY);
		break;
	default:
		rv = -1;
	}
	skops->reply = rv;
	return 1;
}
char _license[] SEC("license") = "GPL";
