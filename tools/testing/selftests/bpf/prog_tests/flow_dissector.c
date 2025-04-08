// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <test_progs.h>
#include <network_helpers.h>
#include <linux/if_tun.h>
#include <sys/uio.h>

#include "bpf_flow.skel.h"

#define TEST_NS	"flow_dissector_ns"
#define FLOW_CONTINUE_SADDR 0x7f00007f /* 127.0.0.127 */
#define TEST_NAME_MAX_LEN	64

#ifndef IP_MF
#define IP_MF 0x2000
#endif

struct ipv4_pkt {
	struct ethhdr eth;
	struct iphdr iph;
	struct tcphdr tcp;
} __packed;

struct ipip_pkt {
	struct ethhdr eth;
	struct iphdr iph;
	struct iphdr iph_inner;
	struct tcphdr tcp;
} __packed;

struct svlan_ipv4_pkt {
	struct ethhdr eth;
	__u16 vlan_tci;
	__u16 vlan_proto;
	struct iphdr iph;
	struct tcphdr tcp;
} __packed;

struct ipv6_pkt {
	struct ethhdr eth;
	struct ipv6hdr iph;
	struct tcphdr tcp;
} __packed;

struct ipv6_frag_pkt {
	struct ethhdr eth;
	struct ipv6hdr iph;
	struct frag_hdr {
		__u8 nexthdr;
		__u8 reserved;
		__be16 frag_off;
		__be32 identification;
	} ipf;
	struct tcphdr tcp;
} __packed;

struct dvlan_ipv6_pkt {
	struct ethhdr eth;
	__u16 vlan_tci;
	__u16 vlan_proto;
	__u16 vlan_tci2;
	__u16 vlan_proto2;
	struct ipv6hdr iph;
	struct tcphdr tcp;
} __packed;

struct gre_base_hdr {
	__be16 flags;
	__be16 protocol;
} gre_base_hdr;

struct gre_minimal_pkt {
	struct ethhdr eth;
	struct iphdr iph;
	struct gre_base_hdr gre_hdr;
	struct iphdr iph_inner;
	struct tcphdr tcp;
} __packed;

struct test {
	const char *name;
	union {
		struct ipv4_pkt ipv4;
		struct svlan_ipv4_pkt svlan_ipv4;
		struct ipip_pkt ipip;
		struct ipv6_pkt ipv6;
		struct ipv6_frag_pkt ipv6_frag;
		struct dvlan_ipv6_pkt dvlan_ipv6;
		struct gre_minimal_pkt gre_minimal;
	} pkt;
	struct bpf_flow_keys keys;
	__u32 flags;
	__u32 retval;
};

#define VLAN_HLEN	4

struct test tests[] = {
	{
		.name = "ipv4",
		.pkt.ipv4 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_TCP,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.sport = 80,
			.dport = 8080,
		},
		.retval = BPF_OK,
	},
	{
		.name = "ipv6",
		.pkt.ipv6 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_TCP,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct ipv6hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
			.sport = 80,
			.dport = 8080,
		},
		.retval = BPF_OK,
	},
	{
		.name = "802.1q-ipv4",
		.pkt.svlan_ipv4 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_8021Q),
			.vlan_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_TCP,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN + VLAN_HLEN,
			.thoff = ETH_HLEN + VLAN_HLEN + sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.sport = 80,
			.dport = 8080,
		},
		.retval = BPF_OK,
	},
	{
		.name = "802.1ad-ipv6",
		.pkt.dvlan_ipv6 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_8021AD),
			.vlan_proto = __bpf_constant_htons(ETH_P_8021Q),
			.vlan_proto2 = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_TCP,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN + VLAN_HLEN * 2,
			.thoff = ETH_HLEN + VLAN_HLEN * 2 +
				sizeof(struct ipv6hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
			.sport = 80,
			.dport = 8080,
		},
		.retval = BPF_OK,
	},
	{
		.name = "ipv4-frag",
		.pkt.ipv4 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_TCP,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph.frag_off = __bpf_constant_htons(IP_MF),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.flags = BPF_FLOW_DISSECTOR_F_PARSE_1ST_FRAG,
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.is_frag = true,
			.is_first_frag = true,
			.sport = 80,
			.dport = 8080,
		},
		.flags = BPF_FLOW_DISSECTOR_F_PARSE_1ST_FRAG,
		.retval = BPF_OK,
	},
	{
		.name = "ipv4-no-frag",
		.pkt.ipv4 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_TCP,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph.frag_off = __bpf_constant_htons(IP_MF),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.is_frag = true,
			.is_first_frag = true,
		},
		.retval = BPF_OK,
	},
	{
		.name = "ipv6-frag",
		.pkt.ipv6_frag = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_FRAGMENT,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.ipf.nexthdr = IPPROTO_TCP,
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.flags = BPF_FLOW_DISSECTOR_F_PARSE_1ST_FRAG,
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct ipv6hdr) +
				sizeof(struct frag_hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
			.is_frag = true,
			.is_first_frag = true,
			.sport = 80,
			.dport = 8080,
		},
		.flags = BPF_FLOW_DISSECTOR_F_PARSE_1ST_FRAG,
		.retval = BPF_OK,
	},
	{
		.name = "ipv6-no-frag",
		.pkt.ipv6_frag = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_FRAGMENT,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.ipf.nexthdr = IPPROTO_TCP,
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct ipv6hdr) +
				sizeof(struct frag_hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
			.is_frag = true,
			.is_first_frag = true,
		},
		.retval = BPF_OK,
	},
	{
		.name = "ipv6-flow-label",
		.pkt.ipv6 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_TCP,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph.flow_lbl = { 0xb, 0xee, 0xef },
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct ipv6hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
			.sport = 80,
			.dport = 8080,
			.flow_label = __bpf_constant_htonl(0xbeeef),
		},
		.retval = BPF_OK,
	},
	{
		.name = "ipv6-no-flow-label",
		.pkt.ipv6 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_TCP,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph.flow_lbl = { 0xb, 0xee, 0xef },
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL,
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct ipv6hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
			.flow_label = __bpf_constant_htonl(0xbeeef),
		},
		.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL,
		.retval = BPF_OK,
	},
	{
		.name = "ipv6-empty-flow-label",
		.pkt.ipv6 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_TCP,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph.flow_lbl = { 0x00, 0x00, 0x00 },
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL,
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct ipv6hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
			.sport = 80,
			.dport = 8080,
		},
		.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL,
		.retval = BPF_OK,
	},
	{
		.name = "ipip-encap",
		.pkt.ipip = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_IPIP,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph_inner.ihl = 5,
			.iph_inner.protocol = IPPROTO_TCP,
			.iph_inner.tot_len =
				__bpf_constant_htons(MAGIC_BYTES -
				sizeof(struct iphdr)),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr) +
				sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.is_encap = true,
			.sport = 80,
			.dport = 8080,
		},
		.retval = BPF_OK,
	},
	{
		.name = "ipip-no-encap",
		.pkt.ipip = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_IPIP,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph_inner.ihl = 5,
			.iph_inner.protocol = IPPROTO_TCP,
			.iph_inner.tot_len =
				__bpf_constant_htons(MAGIC_BYTES -
				sizeof(struct iphdr)),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_ENCAP,
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_IPIP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.is_encap = true,
		},
		.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_ENCAP,
		.retval = BPF_OK,
	},
	{
		.name = "ipip-encap-dissector-continue",
		.pkt.ipip = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_IPIP,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph.saddr = __bpf_constant_htonl(FLOW_CONTINUE_SADDR),
			.iph_inner.ihl = 5,
			.iph_inner.protocol = IPPROTO_TCP,
			.iph_inner.tot_len =
				__bpf_constant_htons(MAGIC_BYTES -
				sizeof(struct iphdr)),
			.tcp.doff = 5,
			.tcp.source = 99,
			.tcp.dest = 9090,
		},
		.retval = BPF_FLOW_DISSECTOR_CONTINUE,
	},
	{
		.name = "ip-gre",
		.pkt.gre_minimal = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_GRE,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.gre_hdr = {
				.flags = 0,
				.protocol = __bpf_constant_htons(ETH_P_IP),
			},
			.iph_inner.ihl = 5,
			.iph_inner.protocol = IPPROTO_TCP,
			.iph_inner.tot_len =
				__bpf_constant_htons(MAGIC_BYTES -
				sizeof(struct iphdr)),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr) * 2 +
				 sizeof(struct gre_base_hdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.is_encap = true,
			.sport = 80,
			.dport = 8080,
		},
		.retval = BPF_OK,
	},
	{
		.name = "ip-gre-no-encap",
		.pkt.ipip = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
			.iph.ihl = 5,
			.iph.protocol = IPPROTO_GRE,
			.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
			.iph_inner.ihl = 5,
			.iph_inner.protocol = IPPROTO_TCP,
			.iph_inner.tot_len =
				__bpf_constant_htons(MAGIC_BYTES -
				sizeof(struct iphdr)),
			.tcp.doff = 5,
			.tcp.source = 80,
			.tcp.dest = 8080,
		},
		.keys = {
			.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_ENCAP,
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr)
				 + sizeof(struct gre_base_hdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_GRE,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
			.is_encap = true,
		},
		.flags = BPF_FLOW_DISSECTOR_F_STOP_AT_ENCAP,
		.retval = BPF_OK,
	},
};

void serial_test_flow_dissector_namespace(void)
{
	struct bpf_flow *skel;
	struct nstoken *ns;
	int err, prog_fd;

	skel = bpf_flow__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open/load skeleton"))
		return;

	prog_fd = bpf_program__fd(skel->progs._dissect);
	if (!ASSERT_OK_FD(prog_fd, "get dissector fd"))
		goto out_destroy_skel;

	/* We must be able to attach a flow dissector to root namespace */
	err = bpf_prog_attach(prog_fd, 0, BPF_FLOW_DISSECTOR, 0);
	if (!ASSERT_OK(err, "attach on root namespace ok"))
		goto out_destroy_skel;

	err = make_netns(TEST_NS);
	if (!ASSERT_OK(err, "create non-root net namespace"))
		goto out_destroy_skel;

	/* We must not be able to additionally attach a flow dissector to a
	 * non-root net namespace
	 */
	ns = open_netns(TEST_NS);
	if (!ASSERT_OK_PTR(ns, "enter non-root net namespace"))
		goto out_clean_ns;
	err = bpf_prog_attach(prog_fd, 0, BPF_FLOW_DISSECTOR, 0);
	if (!ASSERT_ERR(err,
			"refuse new flow dissector in non-root net namespace"))
		bpf_prog_detach2(prog_fd, 0, BPF_FLOW_DISSECTOR);
	else
		ASSERT_EQ(errno, EEXIST,
			  "refused because of already attached prog");
	close_netns(ns);

	/* If no flow dissector is attached to the root namespace, we must
	 * be able to attach one to a non-root net namespace
	 */
	bpf_prog_detach2(prog_fd, 0, BPF_FLOW_DISSECTOR);
	ns = open_netns(TEST_NS);
	ASSERT_OK_PTR(ns, "enter non-root net namespace");
	err = bpf_prog_attach(prog_fd, 0, BPF_FLOW_DISSECTOR, 0);
	close_netns(ns);
	ASSERT_OK(err, "accept new flow dissector in non-root net namespace");

	/* If a flow dissector is attached to non-root net namespace, attaching
	 * a flow dissector to root namespace must fail
	 */
	err = bpf_prog_attach(prog_fd, 0, BPF_FLOW_DISSECTOR, 0);
	if (!ASSERT_ERR(err, "refuse new flow dissector on root namespace"))
		bpf_prog_detach2(prog_fd, 0, BPF_FLOW_DISSECTOR);
	else
		ASSERT_EQ(errno, EEXIST,
			  "refused because of already attached prog");

	ns = open_netns(TEST_NS);
	bpf_prog_detach2(prog_fd, 0, BPF_FLOW_DISSECTOR);
	close_netns(ns);
out_clean_ns:
	remove_netns(TEST_NS);
out_destroy_skel:
	bpf_flow__destroy(skel);
}

static int create_tap(const char *ifname)
{
	struct ifreq ifr = {
		.ifr_flags = IFF_TAP | IFF_NO_PI | IFF_NAPI | IFF_NAPI_FRAGS,
	};
	int fd, ret;

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0)
		return -1;

	ret = ioctl(fd, TUNSETIFF, &ifr);
	if (ret)
		return -1;

	return fd;
}

static int tx_tap(int fd, void *pkt, size_t len)
{
	struct iovec iov[] = {
		{
			.iov_len = len,
			.iov_base = pkt,
		},
	};
	return writev(fd, iov, ARRAY_SIZE(iov));
}

static int ifup(const char *ifname)
{
	struct ifreq ifr = {};
	int sk, ret;

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));

	sk = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk < 0)
		return -1;

	ret = ioctl(sk, SIOCGIFFLAGS, &ifr);
	if (ret) {
		close(sk);
		return -1;
	}

	ifr.ifr_flags |= IFF_UP;
	ret = ioctl(sk, SIOCSIFFLAGS, &ifr);
	if (ret) {
		close(sk);
		return -1;
	}

	close(sk);
	return 0;
}

static int init_prog_array(struct bpf_object *obj, struct bpf_map *prog_array)
{
	int i, err, map_fd, prog_fd;
	struct bpf_program *prog;
	char prog_name[32];

	map_fd = bpf_map__fd(prog_array);
	if (map_fd < 0)
		return -1;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "flow_dissector_%d", i);

		prog = bpf_object__find_program_by_name(obj, prog_name);
		if (!prog)
			return -1;

		prog_fd = bpf_program__fd(prog);
		if (prog_fd < 0)
			return -1;

		err = bpf_map_update_elem(map_fd, &i, &prog_fd, BPF_ANY);
		if (err)
			return -1;
	}
	return 0;
}

static void run_tests_skb_less(int tap_fd, struct bpf_map *keys,
			       char *test_suffix)
{
	char test_name[TEST_NAME_MAX_LEN];
	int i, err, keys_fd;

	keys_fd = bpf_map__fd(keys);
	if (!ASSERT_OK_FD(keys_fd, "bpf_map__fd"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		/* Keep in sync with 'flags' from eth_get_headlen. */
		__u32 eth_get_headlen_flags =
			BPF_FLOW_DISSECTOR_F_PARSE_1ST_FRAG;
		struct bpf_flow_keys flow_keys = {};
		__u32 key = (__u32)(tests[i].keys.sport) << 16 |
			    tests[i].keys.dport;
		snprintf(test_name, TEST_NAME_MAX_LEN, "%s-%s", tests[i].name,
			 test_suffix);
		if (!test__start_subtest(test_name))
			continue;

		/* For skb-less case we can't pass input flags; run
		 * only the tests that have a matching set of flags.
		 */

		if (tests[i].flags != eth_get_headlen_flags)
			continue;

		err = tx_tap(tap_fd, &tests[i].pkt, sizeof(tests[i].pkt));
		if (!ASSERT_EQ(err, sizeof(tests[i].pkt), "tx_tap"))
			continue;

		/* check the stored flow_keys only if BPF_OK expected */
		if (tests[i].retval != BPF_OK)
			continue;

		err = bpf_map_lookup_elem(keys_fd, &key, &flow_keys);
		if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
			continue;

		ASSERT_MEMEQ(&flow_keys, &tests[i].keys,
			     sizeof(struct bpf_flow_keys),
			     "returned flow keys");

		err = bpf_map_delete_elem(keys_fd, &key);
		ASSERT_OK(err, "bpf_map_delete_elem");
	}
}

void test_flow_dissector_skb_less_direct_attach(void)
{
	int err, prog_fd, tap_fd;
	struct bpf_flow *skel;
	struct netns_obj *ns;

	ns = netns_new("flow_dissector_skb_less_indirect_attach_ns", true);
	if (!ASSERT_OK_PTR(ns, "create and open netns"))
		return;

	skel = bpf_flow__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open/load skeleton"))
		goto out_clean_ns;

	err = init_prog_array(skel->obj, skel->maps.jmp_table);
	if (!ASSERT_OK(err, "init_prog_array"))
		goto out_destroy_skel;

	prog_fd = bpf_program__fd(skel->progs._dissect);
	if (!ASSERT_OK_FD(prog_fd, "bpf_program__fd"))
		goto out_destroy_skel;

	err = bpf_prog_attach(prog_fd, 0, BPF_FLOW_DISSECTOR, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach"))
		goto out_destroy_skel;

	tap_fd = create_tap("tap0");
	if (!ASSERT_OK_FD(tap_fd, "create_tap"))
		goto out_destroy_skel;
	err = ifup("tap0");
	if (!ASSERT_OK(err, "ifup"))
		goto out_close_tap;

	run_tests_skb_less(tap_fd, skel->maps.last_dissection,
			   "non-skb-direct-attach");

	err = bpf_prog_detach2(prog_fd, 0, BPF_FLOW_DISSECTOR);
	ASSERT_OK(err, "bpf_prog_detach2");

out_close_tap:
	close(tap_fd);
out_destroy_skel:
	bpf_flow__destroy(skel);
out_clean_ns:
	netns_free(ns);
}

void test_flow_dissector_skb_less_indirect_attach(void)
{
	int err, net_fd, tap_fd;
	struct bpf_flow *skel;
	struct bpf_link *link;
	struct netns_obj *ns;

	ns = netns_new("flow_dissector_skb_less_indirect_attach_ns", true);
	if (!ASSERT_OK_PTR(ns, "create and open netns"))
		return;

	skel = bpf_flow__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open/load skeleton"))
		goto out_clean_ns;

	net_fd = open("/proc/self/ns/net", O_RDONLY);
	if (!ASSERT_OK_FD(net_fd, "open(/proc/self/ns/net"))
		goto out_destroy_skel;

	err = init_prog_array(skel->obj, skel->maps.jmp_table);
	if (!ASSERT_OK(err, "init_prog_array"))
		goto out_destroy_skel;

	tap_fd = create_tap("tap0");
	if (!ASSERT_OK_FD(tap_fd, "create_tap"))
		goto out_close_ns;
	err = ifup("tap0");
	if (!ASSERT_OK(err, "ifup"))
		goto out_close_tap;

	link = bpf_program__attach_netns(skel->progs._dissect, net_fd);
	if (!ASSERT_OK_PTR(link, "attach_netns"))
		goto out_close_tap;

	run_tests_skb_less(tap_fd, skel->maps.last_dissection,
			   "non-skb-indirect-attach");

	err = bpf_link__destroy(link);
	ASSERT_OK(err, "bpf_link__destroy");

out_close_tap:
	close(tap_fd);
out_close_ns:
	close(net_fd);
out_destroy_skel:
	bpf_flow__destroy(skel);
out_clean_ns:
	netns_free(ns);
}

void test_flow_dissector_skb(void)
{
	char test_name[TEST_NAME_MAX_LEN];
	struct bpf_flow *skel;
	int i, err, prog_fd;

	skel = bpf_flow__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open/load skeleton"))
		return;

	err = init_prog_array(skel->obj, skel->maps.jmp_table);
	if (!ASSERT_OK(err, "init_prog_array"))
		goto out_destroy_skel;

	prog_fd = bpf_program__fd(skel->progs._dissect);
	if (!ASSERT_OK_FD(prog_fd, "bpf_program__fd"))
		goto out_destroy_skel;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		struct bpf_flow_keys flow_keys;
		LIBBPF_OPTS(bpf_test_run_opts, topts,
			.data_in = &tests[i].pkt,
			.data_size_in = sizeof(tests[i].pkt),
			.data_out = &flow_keys,
		);
		static struct bpf_flow_keys ctx = {};

		snprintf(test_name, TEST_NAME_MAX_LEN, "%s-skb", tests[i].name);
		if (!test__start_subtest(test_name))
			continue;

		if (tests[i].flags) {
			topts.ctx_in = &ctx;
			topts.ctx_size_in = sizeof(ctx);
			ctx.flags = tests[i].flags;
		}

		err = bpf_prog_test_run_opts(prog_fd, &topts);
		ASSERT_OK(err, "test_run");
		ASSERT_EQ(topts.retval, tests[i].retval, "test_run retval");

		/* check the resulting flow_keys only if BPF_OK returned */
		if (topts.retval != BPF_OK)
			continue;
		ASSERT_EQ(topts.data_size_out, sizeof(flow_keys),
			  "test_run data_size_out");
		ASSERT_MEMEQ(&flow_keys, &tests[i].keys,
			     sizeof(struct bpf_flow_keys),
			     "returned flow keys");
	}

out_destroy_skel:
	bpf_flow__destroy(skel);
}

