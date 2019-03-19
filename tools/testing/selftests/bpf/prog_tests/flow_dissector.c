// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

#define CHECK_FLOW_KEYS(desc, got, expected)				\
	CHECK(memcmp(&got, &expected, sizeof(got)) != 0,		\
	      desc,							\
	      "nhoff=%u/%u "						\
	      "thoff=%u/%u "						\
	      "addr_proto=0x%x/0x%x "					\
	      "is_frag=%u/%u "						\
	      "is_first_frag=%u/%u "					\
	      "is_encap=%u/%u "						\
	      "n_proto=0x%x/0x%x "					\
	      "sport=%u/%u "						\
	      "dport=%u/%u\n",						\
	      got.nhoff, expected.nhoff,				\
	      got.thoff, expected.thoff,				\
	      got.addr_proto, expected.addr_proto,			\
	      got.is_frag, expected.is_frag,				\
	      got.is_first_frag, expected.is_first_frag,		\
	      got.is_encap, expected.is_encap,				\
	      got.n_proto, expected.n_proto,				\
	      got.sport, expected.sport,				\
	      got.dport, expected.dport)

static struct bpf_flow_keys pkt_v4_flow_keys = {
	.nhoff = 0,
	.thoff = sizeof(struct iphdr),
	.addr_proto = ETH_P_IP,
	.ip_proto = IPPROTO_TCP,
	.n_proto = __bpf_constant_htons(ETH_P_IP),
};

static struct bpf_flow_keys pkt_v6_flow_keys = {
	.nhoff = 0,
	.thoff = sizeof(struct ipv6hdr),
	.addr_proto = ETH_P_IPV6,
	.ip_proto = IPPROTO_TCP,
	.n_proto = __bpf_constant_htons(ETH_P_IPV6),
};

void test_flow_dissector(void)
{
	struct bpf_flow_keys flow_keys;
	struct bpf_object *obj;
	__u32 duration, retval;
	int err, prog_fd;
	__u32 size;

	err = bpf_flow_load(&obj, "./bpf_flow.o", "flow_dissector",
			    "jmp_table", &prog_fd);
	if (err) {
		error_cnt++;
		return;
	}

	err = bpf_prog_test_run(prog_fd, 10, &pkt_v4, sizeof(pkt_v4),
				&flow_keys, &size, &retval, &duration);
	CHECK(size != sizeof(flow_keys) || err || retval != 1, "ipv4",
	      "err %d errno %d retval %d duration %d size %u/%lu\n",
	      err, errno, retval, duration, size, sizeof(flow_keys));
	CHECK_FLOW_KEYS("ipv4_flow_keys", flow_keys, pkt_v4_flow_keys);

	err = bpf_prog_test_run(prog_fd, 10, &pkt_v6, sizeof(pkt_v6),
				&flow_keys, &size, &retval, &duration);
	CHECK(size != sizeof(flow_keys) || err || retval != 1, "ipv6",
	      "err %d errno %d retval %d duration %d size %u/%lu\n",
	      err, errno, retval, duration, size, sizeof(flow_keys));
	CHECK_FLOW_KEYS("ipv6_flow_keys", flow_keys, pkt_v6_flow_keys);

	bpf_object__close(obj);
}
