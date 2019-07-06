// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <error.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#define CHECK_FLOW_KEYS(desc, got, expected)				\
	CHECK_ATTR(memcmp(&got, &expected, sizeof(got)) != 0,		\
	      desc,							\
	      "nhoff=%u/%u "						\
	      "thoff=%u/%u "						\
	      "addr_proto=0x%x/0x%x "					\
	      "is_frag=%u/%u "						\
	      "is_first_frag=%u/%u "					\
	      "is_encap=%u/%u "						\
	      "ip_proto=0x%x/0x%x "					\
	      "n_proto=0x%x/0x%x "					\
	      "sport=%u/%u "						\
	      "dport=%u/%u\n",						\
	      got.nhoff, expected.nhoff,				\
	      got.thoff, expected.thoff,				\
	      got.addr_proto, expected.addr_proto,			\
	      got.is_frag, expected.is_frag,				\
	      got.is_first_frag, expected.is_first_frag,		\
	      got.is_encap, expected.is_encap,				\
	      got.ip_proto, expected.ip_proto,				\
	      got.n_proto, expected.n_proto,				\
	      got.sport, expected.sport,				\
	      got.dport, expected.dport)

struct ipv4_pkt {
	struct ethhdr eth;
	struct iphdr iph;
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

struct dvlan_ipv6_pkt {
	struct ethhdr eth;
	__u16 vlan_tci;
	__u16 vlan_proto;
	__u16 vlan_tci2;
	__u16 vlan_proto2;
	struct ipv6hdr iph;
	struct tcphdr tcp;
} __packed;

struct test {
	const char *name;
	union {
		struct ipv4_pkt ipv4;
		struct svlan_ipv4_pkt svlan_ipv4;
		struct ipv6_pkt ipv6;
		struct dvlan_ipv6_pkt dvlan_ipv6;
	} pkt;
	struct bpf_flow_keys keys;
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
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
		},
	},
	{
		.name = "ipv6",
		.pkt.ipv6 = {
			.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
			.iph.nexthdr = IPPROTO_TCP,
			.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
			.tcp.doff = 5,
		},
		.keys = {
			.nhoff = ETH_HLEN,
			.thoff = ETH_HLEN + sizeof(struct ipv6hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
		},
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
		},
		.keys = {
			.nhoff = ETH_HLEN + VLAN_HLEN,
			.thoff = ETH_HLEN + VLAN_HLEN + sizeof(struct iphdr),
			.addr_proto = ETH_P_IP,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IP),
		},
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
		},
		.keys = {
			.nhoff = ETH_HLEN + VLAN_HLEN * 2,
			.thoff = ETH_HLEN + VLAN_HLEN * 2 +
				sizeof(struct ipv6hdr),
			.addr_proto = ETH_P_IPV6,
			.ip_proto = IPPROTO_TCP,
			.n_proto = __bpf_constant_htons(ETH_P_IPV6),
		},
	},
};

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

void test_flow_dissector(void)
{
	int i, err, prog_fd, keys_fd = -1, tap_fd;
	struct bpf_object *obj;
	__u32 duration = 0;

	err = bpf_flow_load(&obj, "./bpf_flow.o", "flow_dissector",
			    "jmp_table", "last_dissection", &prog_fd, &keys_fd);
	if (err) {
		error_cnt++;
		return;
	}

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		struct bpf_flow_keys flow_keys;
		struct bpf_prog_test_run_attr tattr = {
			.prog_fd = prog_fd,
			.data_in = &tests[i].pkt,
			.data_size_in = sizeof(tests[i].pkt),
			.data_out = &flow_keys,
		};

		err = bpf_prog_test_run_xattr(&tattr);
		CHECK_ATTR(tattr.data_size_out != sizeof(flow_keys) ||
			   err || tattr.retval != 1,
			   tests[i].name,
			   "err %d errno %d retval %d duration %d size %u/%lu\n",
			   err, errno, tattr.retval, tattr.duration,
			   tattr.data_size_out, sizeof(flow_keys));
		CHECK_FLOW_KEYS(tests[i].name, flow_keys, tests[i].keys);
	}

	/* Do the same tests but for skb-less flow dissector.
	 * We use a known path in the net/tun driver that calls
	 * eth_get_headlen and we manually export bpf_flow_keys
	 * via BPF map in this case.
	 */

	err = bpf_prog_attach(prog_fd, 0, BPF_FLOW_DISSECTOR, 0);
	CHECK(err, "bpf_prog_attach", "err %d errno %d\n", err, errno);

	tap_fd = create_tap("tap0");
	CHECK(tap_fd < 0, "create_tap", "tap_fd %d errno %d\n", tap_fd, errno);
	err = ifup("tap0");
	CHECK(err, "ifup", "err %d errno %d\n", err, errno);

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		struct bpf_flow_keys flow_keys = {};
		struct bpf_prog_test_run_attr tattr = {};
		__u32 key = 0;

		err = tx_tap(tap_fd, &tests[i].pkt, sizeof(tests[i].pkt));
		CHECK(err < 0, "tx_tap", "err %d errno %d\n", err, errno);

		err = bpf_map_lookup_elem(keys_fd, &key, &flow_keys);
		CHECK_ATTR(err, tests[i].name, "bpf_map_lookup_elem %d\n", err);

		CHECK_ATTR(err, tests[i].name, "skb-less err %d\n", err);
		CHECK_FLOW_KEYS(tests[i].name, flow_keys, tests[i].keys);
	}

	bpf_prog_detach(prog_fd, BPF_FLOW_DISSECTOR);
	bpf_object__close(obj);
}
