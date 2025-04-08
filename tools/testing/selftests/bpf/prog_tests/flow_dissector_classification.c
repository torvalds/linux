// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <bpf/bpf.h>
#include <linux/bpf.h>
#include <bpf/libbpf.h>
#include <arpa/inet.h>
#include <asm/byteorder.h>
#include <netinet/udp.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "test_progs.h"
#include "network_helpers.h"
#include "bpf_util.h"
#include "bpf_flow.skel.h"

#define CFG_PORT_INNER 8000
#define CFG_PORT_GUE 6080
#define SUBTEST_NAME_MAX_LEN 32
#define TEST_NAME_MAX_LEN (32 + SUBTEST_NAME_MAX_LEN)
#define MAX_SOURCE_PORTS 3
#define TEST_PACKETS_COUNT 10
#define TEST_PACKET_LEN 100
#define TEST_PACKET_PATTERN 'a'
#define TEST_IPV4 "192.168.0.1/32"
#define TEST_IPV6 "100::a/128"
#define TEST_TUNNEL_REMOTE "127.0.0.2"
#define TEST_TUNNEL_LOCAL "127.0.0.1"

#define INIT_ADDR4(addr4, port)					\
	{							\
		.sin_family = AF_INET,				\
		.sin_port = __constant_htons(port),		\
		.sin_addr.s_addr = __constant_htonl(addr4),	\
	}

#define INIT_ADDR6(addr6, port)				\
	{						\
		.sin6_family = AF_INET6,		\
		.sin6_port = __constant_htons(port),	\
		.sin6_addr = addr6,			\
	}
#define TEST_IN4_SRC_ADDR_DEFAULT INIT_ADDR4(INADDR_LOOPBACK + 2, 0)
#define TEST_IN4_DST_ADDR_DEFAULT INIT_ADDR4(INADDR_LOOPBACK, CFG_PORT_INNER)
#define TEST_OUT4_SRC_ADDR_DEFAULT INIT_ADDR4(INADDR_LOOPBACK + 1, 0)
#define TEST_OUT4_DST_ADDR_DEFAULT INIT_ADDR4(INADDR_LOOPBACK, 0)

#define TEST_IN6_SRC_ADDR_DEFAULT INIT_ADDR6(IN6ADDR_LOOPBACK_INIT, 0)
#define TEST_IN6_DST_ADDR_DEFAULT \
	INIT_ADDR6(IN6ADDR_LOOPBACK_INIT, CFG_PORT_INNER)
#define TEST_OUT6_SRC_ADDR_DEFAULT INIT_ADDR6(IN6ADDR_LOOPBACK_INIT, 0)
#define TEST_OUT6_DST_ADDR_DEFAULT INIT_ADDR6(IN6ADDR_LOOPBACK_INIT, 0)

#define TEST_IN4_SRC_ADDR_DISSECT_CONTINUE INIT_ADDR4(INADDR_LOOPBACK + 126, 0)
#define TEST_IN4_SRC_ADDR_IPIP INIT_ADDR4((in_addr_t)0x01010101, 0)
#define TEST_IN4_DST_ADDR_IPIP INIT_ADDR4((in_addr_t)0xC0A80001, CFG_PORT_INNER)

struct grehdr {
	uint16_t unused;
	uint16_t protocol;
} __packed;

struct guehdr {
	union {
		struct {
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8 hlen : 5, control : 1, version : 2;
#elif defined(__BIG_ENDIAN_BITFIELD)
			__u8 version : 2, control : 1, hlen : 5;
#else
#error "Please fix <asm/byteorder.h>"
#endif
			__u8 proto_ctype;
			__be16 flags;
		};
		__be32 word;
	};
};

static char buf[ETH_DATA_LEN];

struct test_configuration {
	char name[SUBTEST_NAME_MAX_LEN];
	int (*test_setup)(void);
	void (*test_teardown)(void);
	int source_ports[MAX_SOURCE_PORTS];
	int cfg_l3_inner;
	struct sockaddr_in in_saddr4;
	struct sockaddr_in in_daddr4;
	struct sockaddr_in6 in_saddr6;
	struct sockaddr_in6 in_daddr6;
	int cfg_l3_outer;
	struct sockaddr_in out_saddr4;
	struct sockaddr_in out_daddr4;
	struct sockaddr_in6 out_saddr6;
	struct sockaddr_in6 out_daddr6;
	int cfg_encap_proto;
	uint8_t cfg_dsfield_inner;
	uint8_t cfg_dsfield_outer;
	int cfg_l3_extra;
	struct sockaddr_in extra_saddr4;
	struct sockaddr_in extra_daddr4;
	struct sockaddr_in6 extra_saddr6;
	struct sockaddr_in6 extra_daddr6;
};

static unsigned long util_gettime(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

static void build_ipv4_header(void *header, uint8_t proto, uint32_t src,
			      uint32_t dst, int payload_len, uint8_t tos)
{
	struct iphdr *iph = header;

	iph->ihl = 5;
	iph->version = 4;
	iph->tos = tos;
	iph->ttl = 8;
	iph->tot_len = htons(sizeof(*iph) + payload_len);
	iph->id = htons(1337);
	iph->protocol = proto;
	iph->saddr = src;
	iph->daddr = dst;
	iph->check = build_ip_csum((void *)iph);
}

static void ipv6_set_dsfield(struct ipv6hdr *ip6h, uint8_t dsfield)
{
	uint16_t val, *ptr = (uint16_t *)ip6h;

	val = ntohs(*ptr);
	val &= 0xF00F;
	val |= ((uint16_t)dsfield) << 4;
	*ptr = htons(val);
}

static void build_ipv6_header(void *header, uint8_t proto,
			      const struct sockaddr_in6 *src,
			      const struct sockaddr_in6 *dst, int payload_len,
			      uint8_t dsfield)
{
	struct ipv6hdr *ip6h = header;

	ip6h->version = 6;
	ip6h->payload_len = htons(payload_len);
	ip6h->nexthdr = proto;
	ip6h->hop_limit = 8;
	ipv6_set_dsfield(ip6h, dsfield);

	memcpy(&ip6h->saddr, &src->sin6_addr, sizeof(ip6h->saddr));
	memcpy(&ip6h->daddr, &dst->sin6_addr, sizeof(ip6h->daddr));
}

static void build_udp_header(void *header, int payload_len, uint16_t sport,
			     uint16_t dport, int family)
{
	struct udphdr *udph = header;
	int len = sizeof(*udph) + payload_len;

	udph->source = htons(sport);
	udph->dest = htons(dport);
	udph->len = htons(len);
	udph->check = 0;
	if (family == AF_INET)
		udph->check = build_udp_v4_csum(header - sizeof(struct iphdr),
						udph);
	else
		udph->check = build_udp_v6_csum(header - sizeof(struct ipv6hdr),
						udph);
}

static void build_gue_header(void *header, uint8_t proto)
{
	struct guehdr *gueh = header;

	gueh->proto_ctype = proto;
}

static void build_gre_header(void *header, uint16_t proto)
{
	struct grehdr *greh = header;

	greh->protocol = htons(proto);
}

static int l3_length(int family)
{
	if (family == AF_INET)
		return sizeof(struct iphdr);
	else
		return sizeof(struct ipv6hdr);
}

static int build_packet(const struct test_configuration *test, uint16_t sport)
{
	int ol3_len = 0, ol4_len = 0, il3_len = 0, il4_len = 0;
	int el3_len = 0, packet_len;

	memset(buf, 0, ETH_DATA_LEN);

	if (test->cfg_l3_extra)
		el3_len = l3_length(test->cfg_l3_extra);

	/* calculate header offsets */
	if (test->cfg_encap_proto) {
		ol3_len = l3_length(test->cfg_l3_outer);

		if (test->cfg_encap_proto == IPPROTO_GRE)
			ol4_len = sizeof(struct grehdr);
		else if (test->cfg_encap_proto == IPPROTO_UDP)
			ol4_len = sizeof(struct udphdr) + sizeof(struct guehdr);
	}

	il3_len = l3_length(test->cfg_l3_inner);
	il4_len = sizeof(struct udphdr);

	packet_len = el3_len + ol3_len + ol4_len + il3_len + il4_len +
		     TEST_PACKET_LEN;
	if (!ASSERT_LE(packet_len, sizeof(buf), "check packet size"))
		return -1;

	/*
	 * Fill packet from inside out, to calculate correct checksums.
	 * But create ip before udp headers, as udp uses ip for pseudo-sum.
	 */
	memset(buf + el3_len + ol3_len + ol4_len + il3_len + il4_len,
	       TEST_PACKET_PATTERN, TEST_PACKET_LEN);

	/* add zero byte for udp csum padding */
	buf[el3_len + ol3_len + ol4_len + il3_len + il4_len + TEST_PACKET_LEN] =
		0;

	switch (test->cfg_l3_inner) {
	case PF_INET:
		build_ipv4_header(buf + el3_len + ol3_len + ol4_len,
				  IPPROTO_UDP, test->in_saddr4.sin_addr.s_addr,
				  test->in_daddr4.sin_addr.s_addr,
				  il4_len + TEST_PACKET_LEN,
				  test->cfg_dsfield_inner);
		break;
	case PF_INET6:
		build_ipv6_header(buf + el3_len + ol3_len + ol4_len,
				  IPPROTO_UDP, &test->in_saddr6,
				  &test->in_daddr6, il4_len + TEST_PACKET_LEN,
				  test->cfg_dsfield_inner);
		break;
	}

	build_udp_header(buf + el3_len + ol3_len + ol4_len + il3_len,
			 TEST_PACKET_LEN, sport, CFG_PORT_INNER,
			 test->cfg_l3_inner);

	if (!test->cfg_encap_proto)
		return il3_len + il4_len + TEST_PACKET_LEN;

	switch (test->cfg_l3_outer) {
	case PF_INET:
		build_ipv4_header(buf + el3_len, test->cfg_encap_proto,
				  test->out_saddr4.sin_addr.s_addr,
				  test->out_daddr4.sin_addr.s_addr,
				  ol4_len + il3_len + il4_len + TEST_PACKET_LEN,
				  test->cfg_dsfield_outer);
		break;
	case PF_INET6:
		build_ipv6_header(buf + el3_len, test->cfg_encap_proto,
				  &test->out_saddr6, &test->out_daddr6,
				  ol4_len + il3_len + il4_len + TEST_PACKET_LEN,
				  test->cfg_dsfield_outer);
		break;
	}

	switch (test->cfg_encap_proto) {
	case IPPROTO_UDP:
		build_gue_header(buf + el3_len + ol3_len + ol4_len -
					 sizeof(struct guehdr),
				 test->cfg_l3_inner == PF_INET ? IPPROTO_IPIP :
								 IPPROTO_IPV6);
		build_udp_header(buf + el3_len + ol3_len,
				 sizeof(struct guehdr) + il3_len + il4_len +
					 TEST_PACKET_LEN,
				 sport, CFG_PORT_GUE, test->cfg_l3_outer);
		break;
	case IPPROTO_GRE:
		build_gre_header(buf + el3_len + ol3_len,
				 test->cfg_l3_inner == PF_INET ? ETH_P_IP :
								 ETH_P_IPV6);
		break;
	}

	switch (test->cfg_l3_extra) {
	case PF_INET:
		build_ipv4_header(buf,
				  test->cfg_l3_outer == PF_INET ? IPPROTO_IPIP :
								  IPPROTO_IPV6,
				  test->extra_saddr4.sin_addr.s_addr,
				  test->extra_daddr4.sin_addr.s_addr,
				  ol3_len + ol4_len + il3_len + il4_len +
					  TEST_PACKET_LEN,
				  0);
		break;
	case PF_INET6:
		build_ipv6_header(buf,
				  test->cfg_l3_outer == PF_INET ? IPPROTO_IPIP :
								  IPPROTO_IPV6,
				  &test->extra_saddr6, &test->extra_daddr6,
				  ol3_len + ol4_len + il3_len + il4_len +
					  TEST_PACKET_LEN,
				  0);
		break;
	}

	return el3_len + ol3_len + ol4_len + il3_len + il4_len +
	       TEST_PACKET_LEN;
}

/* sender transmits encapsulated over RAW or unencap'd over UDP */
static int setup_tx(const struct test_configuration *test)
{
	int family, fd, ret;

	if (test->cfg_l3_extra)
		family = test->cfg_l3_extra;
	else if (test->cfg_l3_outer)
		family = test->cfg_l3_outer;
	else
		family = test->cfg_l3_inner;

	fd = socket(family, SOCK_RAW, IPPROTO_RAW);
	if (!ASSERT_OK_FD(fd, "setup tx socket"))
		return fd;

	if (test->cfg_l3_extra) {
		if (test->cfg_l3_extra == PF_INET)
			ret = connect(fd, (void *)&test->extra_daddr4,
				      sizeof(test->extra_daddr4));
		else
			ret = connect(fd, (void *)&test->extra_daddr6,
				      sizeof(test->extra_daddr6));
		if (!ASSERT_OK(ret, "connect")) {
			close(fd);
			return ret;
		}
	} else if (test->cfg_l3_outer) {
		/* connect to destination if not encapsulated */
		if (test->cfg_l3_outer == PF_INET)
			ret = connect(fd, (void *)&test->out_daddr4,
				      sizeof(test->out_daddr4));
		else
			ret = connect(fd, (void *)&test->out_daddr6,
				      sizeof(test->out_daddr6));
		if (!ASSERT_OK(ret, "connect")) {
			close(fd);
			return ret;
		}
	} else {
		/* otherwise using loopback */
		if (test->cfg_l3_inner == PF_INET)
			ret = connect(fd, (void *)&test->in_daddr4,
				      sizeof(test->in_daddr4));
		else
			ret = connect(fd, (void *)&test->in_daddr6,
				      sizeof(test->in_daddr6));
		if (!ASSERT_OK(ret, "connect")) {
			close(fd);
			return ret;
		}
	}

	return fd;
}

/* receiver reads unencapsulated UDP */
static int setup_rx(const struct test_configuration *test)
{
	int fd, ret;

	fd = socket(test->cfg_l3_inner, SOCK_DGRAM, 0);
	if (!ASSERT_OK_FD(fd, "socket rx"))
		return fd;

	if (test->cfg_l3_inner == PF_INET)
		ret = bind(fd, (void *)&test->in_daddr4,
			   sizeof(test->in_daddr4));
	else
		ret = bind(fd, (void *)&test->in_daddr6,
			   sizeof(test->in_daddr6));
	if (!ASSERT_OK(ret, "bind rx")) {
		close(fd);
		return ret;
	}

	return fd;
}

static int do_tx(int fd, const char *pkt, int len)
{
	int ret;

	ret = write(fd, pkt, len);
	return ret != len;
}

static int do_poll(int fd, short events, int timeout)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = fd;
	pfd.events = events;

	ret = poll(&pfd, 1, timeout);
	return ret;
}

static int do_rx(int fd)
{
	char rbuf;
	int ret, num = 0;

	while (1) {
		ret = recv(fd, &rbuf, 1, MSG_DONTWAIT);
		if (ret == -1 && errno == EAGAIN)
			break;
		if (ret < 0)
			return -1;
		if (!ASSERT_EQ(rbuf, TEST_PACKET_PATTERN, "check pkt pattern"))
			return -1;
		num++;
	}

	return num;
}

static int run_test(const struct test_configuration *test,
		    int source_port_index)
{
	int fdt = -1, fdr = -1, len, tx = 0, rx = 0, err;
	unsigned long tstop, tcur;

	fdr = setup_rx(test);
	fdt = setup_tx(test);
	if (!ASSERT_OK_FD(fdr, "setup rx") || !ASSERT_OK_FD(fdt, "setup tx")) {
		err = -1;
		goto out_close_sockets;
	}

	len = build_packet(test,
			   (uint16_t)test->source_ports[source_port_index]);
	if (!ASSERT_GT(len, 0, "build test packet"))
		return -1;

	tcur = util_gettime();
	tstop = tcur;

	while (tx < TEST_PACKETS_COUNT) {
		if (!ASSERT_OK(do_tx(fdt, buf, len), "do_tx"))
			break;
		tx++;
		err = do_rx(fdr);
		if (!ASSERT_GE(err, 0, "do_rx"))
			break;
		rx += err;
	}

	/* read straggler packets, if any */
	if (rx < tx) {
		tstop = util_gettime() + 100;
		while (rx < tx) {
			tcur = util_gettime();
			if (tcur >= tstop)
				break;

			err = do_poll(fdr, POLLIN, tstop - tcur);
			if (err < 0)
				break;
			err = do_rx(fdr);
			if (err >= 0)
				rx += err;
		}
	}

out_close_sockets:
	close(fdt);
	close(fdr);
	return rx;
}

static int attach_and_configure_program(struct bpf_flow *skel)
{
	struct bpf_map *prog_array = skel->maps.jmp_table;
	int main_prog_fd, sub_prog_fd, map_fd, i, err;
	struct bpf_program *prog;
	char prog_name[32];

	main_prog_fd = bpf_program__fd(skel->progs._dissect);
	if (main_prog_fd < 0)
		return main_prog_fd;

	err = bpf_prog_attach(main_prog_fd, 0, BPF_FLOW_DISSECTOR, 0);
	if (err)
		return err;

	map_fd = bpf_map__fd(prog_array);
	if (map_fd < 0)
		return map_fd;

	for (i = 0; i < bpf_map__max_entries(prog_array); i++) {
		snprintf(prog_name, sizeof(prog_name), "flow_dissector_%d", i);

		prog = bpf_object__find_program_by_name(skel->obj, prog_name);
		if (!prog)
			return -1;

		sub_prog_fd = bpf_program__fd(prog);
		if (sub_prog_fd < 0)
			return -1;

		err = bpf_map_update_elem(map_fd, &i, &sub_prog_fd, BPF_ANY);
		if (err)
			return -1;
	}

	return main_prog_fd;
}

static void detach_program(struct bpf_flow *skel, int prog_fd)
{
	bpf_prog_detach2(prog_fd, 0, BPF_FLOW_DISSECTOR);
}

static int set_port_drop(int pf, bool multi_port)
{
	char dst_port[16];

	snprintf(dst_port, sizeof(dst_port), "%d", CFG_PORT_INNER);

	SYS(fail, "tc qdisc add dev lo ingress");
	SYS(fail_delete_qdisc, "tc filter add %s %s %s %s %s %s %s %s %s %s %s %s",
	    "dev lo",
	    "parent FFFF:",
	    "protocol", pf == PF_INET6 ? "ipv6" : "ip",
	    "pref 1337",
	    "flower",
	    "ip_proto udp",
	    "src_port", multi_port ? "8-10" : "9",
	    "dst_port", dst_port,
	    "action drop");
	return 0;

fail_delete_qdisc:
	SYS_NOFAIL("tc qdisc del dev lo ingress");
fail:
	return 1;
}

static void remove_filter(void)
{
	SYS_NOFAIL("tc filter del dev lo ingress");
	SYS_NOFAIL("tc qdisc del dev lo ingress");
}

static int ipv4_setup(void)
{
	return set_port_drop(PF_INET, false);
}

static int ipv6_setup(void)
{
	return set_port_drop(PF_INET6, false);
}

static int port_range_setup(void)
{
	return set_port_drop(PF_INET, true);
}

static int set_addresses(void)
{
	SYS(out, "ip -4 addr add  %s dev lo", TEST_IPV4);
	SYS(out_remove_ipv4, "ip -6 addr add %s dev lo", TEST_IPV6);
	return 0;
out_remove_ipv4:
	SYS_NOFAIL("ip -4 addr del %s dev lo", TEST_IPV4);
out:
	return -1;
}

static void unset_addresses(void)
{
	SYS_NOFAIL("ip -4 addr del %s dev lo", TEST_IPV4);
	SYS_NOFAIL("ip -6 addr del %s dev lo", TEST_IPV6);
}

static int ipip_setup(void)
{
	if (!ASSERT_OK(set_addresses(), "configure addresses"))
		return -1;
	if (!ASSERT_OK(set_port_drop(PF_INET, false), "set filter"))
		goto out_unset_addresses;
	SYS(out_remove_filter,
	    "ip link add ipip_test type ipip remote %s local %s dev lo",
	    TEST_TUNNEL_REMOTE, TEST_TUNNEL_LOCAL);
	SYS(out_clean_netif, "ip link set ipip_test up");
	return 0;

out_clean_netif:
	SYS_NOFAIL("ip link del ipip_test");
out_remove_filter:
	remove_filter();
out_unset_addresses:
	unset_addresses();
	return -1;
}

static void ipip_shutdown(void)
{
	SYS_NOFAIL("ip link del ipip_test");
	remove_filter();
	unset_addresses();
}

static int gre_setup(void)
{
	if (!ASSERT_OK(set_addresses(), "configure addresses"))
		return -1;
	if (!ASSERT_OK(set_port_drop(PF_INET, false), "set filter"))
		goto out_unset_addresses;
	SYS(out_remove_filter,
	    "ip link add gre_test type gre remote %s local %s dev lo",
	    TEST_TUNNEL_REMOTE, TEST_TUNNEL_LOCAL);
	SYS(out_clean_netif, "ip link set gre_test up");
	return 0;

out_clean_netif:
	SYS_NOFAIL("ip link del ipip_test");
out_remove_filter:
	remove_filter();
out_unset_addresses:
	unset_addresses();
	return -1;
}

static void gre_shutdown(void)
{
	SYS_NOFAIL("ip link del gre_test");
	remove_filter();
	unset_addresses();
}

static const struct test_configuration tests_input[] = {
	{
		.name = "ipv4",
		.test_setup = ipv4_setup,
		.test_teardown = remove_filter,
		.source_ports = { 8, 9, 10 },
		.cfg_l3_inner = PF_INET,
		.in_saddr4 = TEST_IN4_SRC_ADDR_DEFAULT,
		.in_daddr4 = TEST_IN4_DST_ADDR_DEFAULT
	},
	{
		.name = "ipv4_continue_dissect",
		.test_setup = ipv4_setup,
		.test_teardown = remove_filter,
		.source_ports = { 8, 9, 10 },
		.cfg_l3_inner = PF_INET,
		.in_saddr4 = TEST_IN4_SRC_ADDR_DISSECT_CONTINUE,
		.in_daddr4 = TEST_IN4_DST_ADDR_DEFAULT },
	{
		.name = "ipip",
		.test_setup = ipip_setup,
		.test_teardown = ipip_shutdown,
		.source_ports = { 8, 9, 10 },
		.cfg_l3_inner = PF_INET,
		.in_saddr4 = TEST_IN4_SRC_ADDR_IPIP,
		.in_daddr4 = TEST_IN4_DST_ADDR_IPIP,
		.out_saddr4 = TEST_OUT4_SRC_ADDR_DEFAULT,
		.out_daddr4 = TEST_OUT4_DST_ADDR_DEFAULT,
		.cfg_l3_outer = PF_INET,
		.cfg_encap_proto = IPPROTO_IPIP,

	},
	{
		.name = "gre",
		.test_setup = gre_setup,
		.test_teardown = gre_shutdown,
		.source_ports = { 8, 9, 10 },
		.cfg_l3_inner = PF_INET,
		.in_saddr4 = TEST_IN4_SRC_ADDR_IPIP,
		.in_daddr4 = TEST_IN4_DST_ADDR_IPIP,
		.out_saddr4 = TEST_OUT4_SRC_ADDR_DEFAULT,
		.out_daddr4 = TEST_OUT4_DST_ADDR_DEFAULT,
		.cfg_l3_outer = PF_INET,
		.cfg_encap_proto = IPPROTO_GRE,
	},
	{
		.name = "port_range",
		.test_setup = port_range_setup,
		.test_teardown = remove_filter,
		.source_ports = { 7, 9, 11 },
		.cfg_l3_inner = PF_INET,
		.in_saddr4 = TEST_IN4_SRC_ADDR_DEFAULT,
		.in_daddr4 = TEST_IN4_DST_ADDR_DEFAULT },
	{
		.name = "ipv6",
		.test_setup = ipv6_setup,
		.test_teardown = remove_filter,
		.source_ports = { 8, 9, 10 },
		.cfg_l3_inner = PF_INET6,
		.in_saddr6 = TEST_IN6_SRC_ADDR_DEFAULT,
		.in_daddr6 = TEST_IN6_DST_ADDR_DEFAULT
	},
};

struct test_ctx {
	struct bpf_flow *skel;
	struct netns_obj *ns;
	int prog_fd;
};

static int test_global_init(struct test_ctx *ctx)
{
	int err;

	ctx->skel = bpf_flow__open_and_load();
	if (!ASSERT_OK_PTR(ctx->skel, "open and load flow_dissector"))
		return -1;

	ctx->ns = netns_new("flow_dissector_classification", true);
	if (!ASSERT_OK_PTR(ctx->ns, "switch ns"))
		goto out_destroy_skel;

	err = write_sysctl("/proc/sys/net/ipv4/conf/default/rp_filter", "0");
	err |= write_sysctl("/proc/sys/net/ipv4/conf/all/rp_filter", "0");
	err |= write_sysctl("/proc/sys/net/ipv4/conf/lo/rp_filter", "0");
	if (!ASSERT_OK(err, "configure net tunables"))
		goto out_clean_ns;

	ctx->prog_fd = attach_and_configure_program(ctx->skel);
	if (!ASSERT_OK_FD(ctx->prog_fd, "attach and configure program"))
		goto out_clean_ns;
	return 0;
out_clean_ns:
	netns_free(ctx->ns);
out_destroy_skel:
	bpf_flow__destroy(ctx->skel);
	return -1;
}

static void test_global_shutdown(struct test_ctx *ctx)
{
	detach_program(ctx->skel, ctx->prog_fd);
	netns_free(ctx->ns);
	bpf_flow__destroy(ctx->skel);
}

void test_flow_dissector_classification(void)
{
	struct test_ctx ctx;
	const struct test_configuration *test;
	int i;

	if (test_global_init(&ctx))
		return;

	for (i = 0; i < ARRAY_SIZE(tests_input); i++) {
		if (!test__start_subtest(tests_input[i].name))
			continue;
		test = &tests_input[i];
		/* All tests are expected to have one rx-ok port first,
		 * then a non-working rx port, and finally a rx-ok port
		 */
		if (test->test_setup &&
		    !ASSERT_OK(test->test_setup(), "init filter"))
			continue;

		ASSERT_EQ(run_test(test, 0), TEST_PACKETS_COUNT,
			  "test first port");
		ASSERT_EQ(run_test(test, 1), 0, "test second port");
		ASSERT_EQ(run_test(test, 2), TEST_PACKETS_COUNT,
			  "test third port");
		if (test->test_teardown)
			test->test_teardown();
	}
	test_global_shutdown(&ctx);
}
