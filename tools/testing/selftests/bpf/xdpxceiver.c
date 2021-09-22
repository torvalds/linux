// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 Intel Corporation. */

/*
 * Some functions in this program are taken from
 * Linux kernel samples/bpf/xdpsock* and modified
 * for use.
 *
 * See test_xsk.sh for detailed information on test topology
 * and prerequisite network setup.
 *
 * This test program contains two threads, each thread is single socket with
 * a unique UMEM. It validates in-order packet delivery and packet content
 * by sending packets to each other.
 *
 * Tests Information:
 * ------------------
 * These selftests test AF_XDP SKB and Native/DRV modes using veth
 * Virtual Ethernet interfaces.
 *
 * For each mode, the following tests are run:
 *    a. nopoll - soft-irq processing in run-to-completion mode
 *    b. poll - using poll() syscall
 *    c. Socket Teardown
 *       Create a Tx and a Rx socket, Tx from one socket, Rx on another. Destroy
 *       both sockets, then repeat multiple times. Only nopoll mode is used
 *    d. Bi-directional sockets
 *       Configure sockets as bi-directional tx/rx sockets, sets up fill and
 *       completion rings on each socket, tx/rx in both directions. Only nopoll
 *       mode is used
 *    e. Statistics
 *       Trigger some error conditions and ensure that the appropriate statistics
 *       are incremented. Within this test, the following statistics are tested:
 *       i.   rx dropped
 *            Increase the UMEM frame headroom to a value which results in
 *            insufficient space in the rx buffer for both the packet and the headroom.
 *       ii.  tx invalid
 *            Set the 'len' field of tx descriptors to an invalid value (umem frame
 *            size + 1).
 *       iii. rx ring full
 *            Reduce the size of the RX ring to a fraction of the fill ring size.
 *       iv.  fill queue empty
 *            Do not populate the fill queue and then try to receive pkts.
 *    f. bpf_link resource persistence
 *       Configure sockets at indexes 0 and 1, run a traffic on queue ids 0,
 *       then remove xsk sockets from queue 0 on both veth interfaces and
 *       finally run a traffic on queues ids 1
 *    g. unaligned mode
 *    h. tests for invalid and corner case Tx descriptors so that the correct ones
 *       are discarded and let through, respectively.
 *    i. 2K frame size tests
 *
 * Total tests: 12
 *
 * Flow:
 * -----
 * - Single process spawns two threads: Tx and Rx
 * - Each of these two threads attach to a veth interface within their assigned
 *   namespaces
 * - Each thread Creates one AF_XDP socket connected to a unique umem for each
 *   veth interface
 * - Tx thread Transmits 10k packets from veth<xxxx> to veth<yyyy>
 * - Rx thread verifies if all 10k packets were received and delivered in-order,
 *   and have the right content
 *
 * Enable/disable packet dump mode:
 * --------------------------
 * To enable L2 - L4 headers and payload dump of each packet on STDOUT, add
 * parameter -D to params array in test_xsk.sh, i.e. params=("-S" "-D")
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <asm/barrier.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <bpf/xsk.h>
#include "xdpxceiver.h"
#include "../kselftest.h"

static const char *MAC1 = "\x00\x0A\x56\x9E\xEE\x62";
static const char *MAC2 = "\x00\x0A\x56\x9E\xEE\x61";
static const char *IP1 = "192.168.100.162";
static const char *IP2 = "192.168.100.161";
static const u16 UDP_PORT1 = 2020;
static const u16 UDP_PORT2 = 2121;

static void __exit_with_error(int error, const char *file, const char *func, int line)
{
	ksft_test_result_fail("[%s:%s:%i]: ERROR: %d/\"%s\"\n", file, func, line, error,
			      strerror(error));
	ksft_exit_xfail();
}

#define exit_with_error(error) __exit_with_error(error, __FILE__, __func__, __LINE__)

#define mode_string(test) (test)->ifobj_tx->xdp_flags & XDP_FLAGS_SKB_MODE ? "SKB" : "DRV"

#define print_ksft_result(test)						\
	(ksft_test_result_pass("PASS: %s %s\n", mode_string(test), (test)->name))

static void memset32_htonl(void *dest, u32 val, u32 size)
{
	u32 *ptr = (u32 *)dest;
	int i;

	val = htonl(val);

	for (i = 0; i < (size & (~0x3)); i += 4)
		ptr[i >> 2] = val;
}

/*
 * Fold a partial checksum
 * This function code has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static __u16 csum_fold(__u32 csum)
{
	u32 sum = (__force u32)csum;

	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return (__force __u16)~sum;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static u32 from64to32(u64 x)
{
	/* add up 32-bit and 32-bit for 32+c bit */
	x = (x & 0xffffffff) + (x >> 32);
	/* add up carry.. */
	x = (x & 0xffffffff) + (x >> 32);
	return (u32)x;
}

/*
 * This function code has been taken from
 * Linux kernel lib/checksum.c
 */
static __u32 csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len, __u8 proto, __u32 sum)
{
	unsigned long long s = (__force u32)sum;

	s += (__force u32)saddr;
	s += (__force u32)daddr;
#ifdef __BIG_ENDIAN__
	s += proto + len;
#else
	s += (proto + len) << 8;
#endif
	return (__force __u32)from64to32(s);
}

/*
 * This function has been taken from
 * Linux kernel include/asm-generic/checksum.h
 */
static __u16 csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len, __u8 proto, __u32 sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

static u16 udp_csum(u32 saddr, u32 daddr, u32 len, u8 proto, u16 *udp_pkt)
{
	u32 csum = 0;
	u32 cnt = 0;

	/* udp hdr and data */
	for (; cnt < len; cnt += 2)
		csum += udp_pkt[cnt >> 1];

	return csum_tcpudp_magic(saddr, daddr, len, proto, csum);
}

static void gen_eth_hdr(struct ifobject *ifobject, struct ethhdr *eth_hdr)
{
	memcpy(eth_hdr->h_dest, ifobject->dst_mac, ETH_ALEN);
	memcpy(eth_hdr->h_source, ifobject->src_mac, ETH_ALEN);
	eth_hdr->h_proto = htons(ETH_P_IP);
}

static void gen_ip_hdr(struct ifobject *ifobject, struct iphdr *ip_hdr)
{
	ip_hdr->version = IP_PKT_VER;
	ip_hdr->ihl = 0x5;
	ip_hdr->tos = IP_PKT_TOS;
	ip_hdr->tot_len = htons(IP_PKT_SIZE);
	ip_hdr->id = 0;
	ip_hdr->frag_off = 0;
	ip_hdr->ttl = IPDEFTTL;
	ip_hdr->protocol = IPPROTO_UDP;
	ip_hdr->saddr = ifobject->src_ip;
	ip_hdr->daddr = ifobject->dst_ip;
	ip_hdr->check = 0;
}

static void gen_udp_hdr(u32 payload, void *pkt, struct ifobject *ifobject,
			struct udphdr *udp_hdr)
{
	udp_hdr->source = htons(ifobject->src_port);
	udp_hdr->dest = htons(ifobject->dst_port);
	udp_hdr->len = htons(UDP_PKT_SIZE);
	memset32_htonl(pkt + PKT_HDR_SIZE, payload, UDP_PKT_DATA_SIZE);
}

static void gen_udp_csum(struct udphdr *udp_hdr, struct iphdr *ip_hdr)
{
	udp_hdr->check = 0;
	udp_hdr->check =
	    udp_csum(ip_hdr->saddr, ip_hdr->daddr, UDP_PKT_SIZE, IPPROTO_UDP, (u16 *)udp_hdr);
}

static int xsk_configure_umem(struct xsk_umem_info *umem, void *buffer, u64 size)
{
	struct xsk_umem_config cfg = {
		.fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.frame_size = umem->frame_size,
		.frame_headroom = umem->frame_headroom,
		.flags = XSK_UMEM__DEFAULT_FLAGS
	};
	int ret;

	if (umem->unaligned_mode)
		cfg.flags |= XDP_UMEM_UNALIGNED_CHUNK_FLAG;

	ret = xsk_umem__create(&umem->umem, buffer, size,
			       &umem->fq, &umem->cq, &cfg);
	if (ret)
		return ret;

	umem->buffer = buffer;
	return 0;
}

static int xsk_configure_socket(struct xsk_socket_info *xsk, struct xsk_umem_info *umem,
				struct ifobject *ifobject, u32 qid)
{
	struct xsk_socket_config cfg;
	struct xsk_ring_cons *rxr;
	struct xsk_ring_prod *txr;

	xsk->umem = umem;
	cfg.rx_size = xsk->rxqsize;
	cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	cfg.libbpf_flags = 0;
	cfg.xdp_flags = ifobject->xdp_flags;
	cfg.bind_flags = ifobject->bind_flags;

	txr = ifobject->tx_on ? &xsk->tx : NULL;
	rxr = ifobject->rx_on ? &xsk->rx : NULL;
	return xsk_socket__create(&xsk->xsk, ifobject->ifname, qid, umem->umem, rxr, txr, &cfg);
}

static struct option long_options[] = {
	{"interface", required_argument, 0, 'i'},
	{"queue", optional_argument, 0, 'q'},
	{"dump-pkts", optional_argument, 0, 'D'},
	{"verbose", no_argument, 0, 'v'},
	{0, 0, 0, 0}
};

static void usage(const char *prog)
{
	const char *str =
		"  Usage: %s [OPTIONS]\n"
		"  Options:\n"
		"  -i, --interface      Use interface\n"
		"  -q, --queue=n        Use queue n (default 0)\n"
		"  -D, --dump-pkts      Dump packets L2 - L5\n"
		"  -v, --verbose        Verbose output\n";

	ksft_print_msg(str, prog);
}

static int switch_namespace(const char *nsname)
{
	char fqns[26] = "/var/run/netns/";
	int nsfd;

	if (!nsname || strlen(nsname) == 0)
		return -1;

	strncat(fqns, nsname, sizeof(fqns) - strlen(fqns) - 1);
	nsfd = open(fqns, O_RDONLY);

	if (nsfd == -1)
		exit_with_error(errno);

	if (setns(nsfd, 0) == -1)
		exit_with_error(errno);

	print_verbose("NS switched: %s\n", nsname);

	return nsfd;
}

static bool validate_interface(struct ifobject *ifobj)
{
	if (!strcmp(ifobj->ifname, ""))
		return false;
	return true;
}

static void parse_command_line(struct ifobject *ifobj_tx, struct ifobject *ifobj_rx, int argc,
			       char **argv)
{
	struct ifobject *ifobj;
	u32 interface_nb = 0;
	int option_index, c;

	opterr = 0;

	for (;;) {
		char *sptr, *token;

		c = getopt_long(argc, argv, "i:Dv", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'i':
			if (interface_nb == 0)
				ifobj = ifobj_tx;
			else if (interface_nb == 1)
				ifobj = ifobj_rx;
			else
				break;

			sptr = strndupa(optarg, strlen(optarg));
			memcpy(ifobj->ifname, strsep(&sptr, ","), MAX_INTERFACE_NAME_CHARS);
			token = strsep(&sptr, ",");
			if (token)
				memcpy(ifobj->nsname, token, MAX_INTERFACES_NAMESPACE_CHARS);
			interface_nb++;
			break;
		case 'D':
			opt_pkt_dump = true;
			break;
		case 'v':
			opt_verbose = true;
			break;
		default:
			usage(basename(argv[0]));
			ksft_exit_xfail();
		}
	}
}

static void __test_spec_init(struct test_spec *test, struct ifobject *ifobj_tx,
			     struct ifobject *ifobj_rx)
{
	u32 i, j;

	for (i = 0; i < MAX_INTERFACES; i++) {
		struct ifobject *ifobj = i ? ifobj_rx : ifobj_tx;

		ifobj->umem = &ifobj->umem_arr[0];
		ifobj->xsk = &ifobj->xsk_arr[0];
		ifobj->use_poll = false;
		ifobj->pacing_on = true;
		ifobj->pkt_stream = test->pkt_stream_default;

		if (i == 0) {
			ifobj->rx_on = false;
			ifobj->tx_on = true;
		} else {
			ifobj->rx_on = true;
			ifobj->tx_on = false;
		}

		for (j = 0; j < MAX_SOCKETS; j++) {
			memset(&ifobj->umem_arr[j], 0, sizeof(ifobj->umem_arr[j]));
			memset(&ifobj->xsk_arr[j], 0, sizeof(ifobj->xsk_arr[j]));
			ifobj->umem_arr[j].num_frames = DEFAULT_UMEM_BUFFERS;
			ifobj->umem_arr[j].frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE;
			ifobj->xsk_arr[j].rxqsize = XSK_RING_CONS__DEFAULT_NUM_DESCS;
		}
	}

	test->ifobj_tx = ifobj_tx;
	test->ifobj_rx = ifobj_rx;
	test->current_step = 0;
	test->total_steps = 1;
	test->nb_sockets = 1;
}

static void test_spec_init(struct test_spec *test, struct ifobject *ifobj_tx,
			   struct ifobject *ifobj_rx, enum test_mode mode)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = test->pkt_stream_default;
	memset(test, 0, sizeof(*test));
	test->pkt_stream_default = pkt_stream;

	for (i = 0; i < MAX_INTERFACES; i++) {
		struct ifobject *ifobj = i ? ifobj_rx : ifobj_tx;

		ifobj->xdp_flags = XDP_FLAGS_UPDATE_IF_NOEXIST;
		if (mode == TEST_MODE_SKB)
			ifobj->xdp_flags |= XDP_FLAGS_SKB_MODE;
		else
			ifobj->xdp_flags |= XDP_FLAGS_DRV_MODE;

		ifobj->bind_flags = XDP_USE_NEED_WAKEUP | XDP_COPY;
	}

	__test_spec_init(test, ifobj_tx, ifobj_rx);
}

static void test_spec_reset(struct test_spec *test)
{
	__test_spec_init(test, test->ifobj_tx, test->ifobj_rx);
}

static void test_spec_set_name(struct test_spec *test, const char *name)
{
	strncpy(test->name, name, MAX_TEST_NAME_SIZE);
}

static void pkt_stream_reset(struct pkt_stream *pkt_stream)
{
	if (pkt_stream)
		pkt_stream->rx_pkt_nb = 0;
}

static struct pkt *pkt_stream_get_pkt(struct pkt_stream *pkt_stream, u32 pkt_nb)
{
	if (pkt_nb >= pkt_stream->nb_pkts)
		return NULL;

	return &pkt_stream->pkts[pkt_nb];
}

static struct pkt *pkt_stream_get_next_rx_pkt(struct pkt_stream *pkt_stream)
{
	while (pkt_stream->rx_pkt_nb < pkt_stream->nb_pkts) {
		if (pkt_stream->pkts[pkt_stream->rx_pkt_nb].valid)
			return &pkt_stream->pkts[pkt_stream->rx_pkt_nb++];
		pkt_stream->rx_pkt_nb++;
	}
	return NULL;
}

static void pkt_stream_delete(struct pkt_stream *pkt_stream)
{
	free(pkt_stream->pkts);
	free(pkt_stream);
}

static void pkt_stream_restore_default(struct test_spec *test)
{
	if (test->ifobj_tx->pkt_stream != test->pkt_stream_default) {
		pkt_stream_delete(test->ifobj_tx->pkt_stream);
		test->ifobj_tx->pkt_stream = test->pkt_stream_default;
	}
	test->ifobj_rx->pkt_stream = test->pkt_stream_default;
}

static struct pkt_stream *__pkt_stream_alloc(u32 nb_pkts)
{
	struct pkt_stream *pkt_stream;

	pkt_stream = calloc(1, sizeof(*pkt_stream));
	if (!pkt_stream)
		return NULL;

	pkt_stream->pkts = calloc(nb_pkts, sizeof(*pkt_stream->pkts));
	if (!pkt_stream->pkts) {
		free(pkt_stream);
		return NULL;
	}

	pkt_stream->nb_pkts = nb_pkts;
	return pkt_stream;
}

static struct pkt_stream *pkt_stream_generate(struct xsk_umem_info *umem, u32 nb_pkts, u32 pkt_len)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = __pkt_stream_alloc(nb_pkts);
	if (!pkt_stream)
		exit_with_error(ENOMEM);

	pkt_stream->nb_pkts = nb_pkts;
	for (i = 0; i < nb_pkts; i++) {
		pkt_stream->pkts[i].addr = (i % umem->num_frames) * umem->frame_size;
		pkt_stream->pkts[i].len = pkt_len;
		pkt_stream->pkts[i].payload = i;

		if (pkt_len > umem->frame_size)
			pkt_stream->pkts[i].valid = false;
		else
			pkt_stream->pkts[i].valid = true;
	}

	return pkt_stream;
}

static struct pkt_stream *pkt_stream_clone(struct xsk_umem_info *umem,
					   struct pkt_stream *pkt_stream)
{
	return pkt_stream_generate(umem, pkt_stream->nb_pkts, pkt_stream->pkts[0].len);
}

static void pkt_stream_replace(struct test_spec *test, u32 nb_pkts, u32 pkt_len)
{
	struct pkt_stream *pkt_stream;

	pkt_stream = pkt_stream_generate(test->ifobj_tx->umem, nb_pkts, pkt_len);
	test->ifobj_tx->pkt_stream = pkt_stream;
	test->ifobj_rx->pkt_stream = pkt_stream;
}

static void pkt_stream_replace_half(struct test_spec *test, u32 pkt_len, int offset)
{
	struct xsk_umem_info *umem = test->ifobj_tx->umem;
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = pkt_stream_clone(umem, test->pkt_stream_default);
	for (i = 1; i < test->pkt_stream_default->nb_pkts; i += 2) {
		pkt_stream->pkts[i].addr = (i % umem->num_frames) * umem->frame_size + offset;
		pkt_stream->pkts[i].len = pkt_len;
	}

	test->ifobj_tx->pkt_stream = pkt_stream;
	test->ifobj_rx->pkt_stream = pkt_stream;
}

static struct pkt *pkt_generate(struct ifobject *ifobject, u32 pkt_nb)
{
	struct pkt *pkt = pkt_stream_get_pkt(ifobject->pkt_stream, pkt_nb);
	struct udphdr *udp_hdr;
	struct ethhdr *eth_hdr;
	struct iphdr *ip_hdr;
	void *data;

	if (!pkt)
		return NULL;
	if (!pkt->valid || pkt->len < PKT_SIZE)
		return pkt;

	data = xsk_umem__get_data(ifobject->umem->buffer, pkt->addr);
	udp_hdr = (struct udphdr *)(data + sizeof(struct ethhdr) + sizeof(struct iphdr));
	ip_hdr = (struct iphdr *)(data + sizeof(struct ethhdr));
	eth_hdr = (struct ethhdr *)data;

	gen_udp_hdr(pkt_nb, data, ifobject, udp_hdr);
	gen_ip_hdr(ifobject, ip_hdr);
	gen_udp_csum(udp_hdr, ip_hdr);
	gen_eth_hdr(ifobject, eth_hdr);

	return pkt;
}

static void pkt_stream_generate_custom(struct test_spec *test, struct pkt *pkts, u32 nb_pkts)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = __pkt_stream_alloc(nb_pkts);
	if (!pkt_stream)
		exit_with_error(ENOMEM);

	test->ifobj_tx->pkt_stream = pkt_stream;
	test->ifobj_rx->pkt_stream = pkt_stream;

	for (i = 0; i < nb_pkts; i++) {
		pkt_stream->pkts[i].addr = pkts[i].addr;
		pkt_stream->pkts[i].len = pkts[i].len;
		pkt_stream->pkts[i].payload = i;
		pkt_stream->pkts[i].valid = pkts[i].valid;
	}
}

static void pkt_dump(void *pkt, u32 len)
{
	char s[INET_ADDRSTRLEN];
	struct ethhdr *ethhdr;
	struct udphdr *udphdr;
	struct iphdr *iphdr;
	int payload, i;

	ethhdr = pkt;
	iphdr = pkt + sizeof(*ethhdr);
	udphdr = pkt + sizeof(*ethhdr) + sizeof(*iphdr);

	/*extract L2 frame */
	fprintf(stdout, "DEBUG>> L2: dst mac: ");
	for (i = 0; i < ETH_ALEN; i++)
		fprintf(stdout, "%02X", ethhdr->h_dest[i]);

	fprintf(stdout, "\nDEBUG>> L2: src mac: ");
	for (i = 0; i < ETH_ALEN; i++)
		fprintf(stdout, "%02X", ethhdr->h_source[i]);

	/*extract L3 frame */
	fprintf(stdout, "\nDEBUG>> L3: ip_hdr->ihl: %02X\n", iphdr->ihl);
	fprintf(stdout, "DEBUG>> L3: ip_hdr->saddr: %s\n",
		inet_ntop(AF_INET, &iphdr->saddr, s, sizeof(s)));
	fprintf(stdout, "DEBUG>> L3: ip_hdr->daddr: %s\n",
		inet_ntop(AF_INET, &iphdr->daddr, s, sizeof(s)));
	/*extract L4 frame */
	fprintf(stdout, "DEBUG>> L4: udp_hdr->src: %d\n", ntohs(udphdr->source));
	fprintf(stdout, "DEBUG>> L4: udp_hdr->dst: %d\n", ntohs(udphdr->dest));
	/*extract L5 frame */
	payload = *((uint32_t *)(pkt + PKT_HDR_SIZE));

	fprintf(stdout, "DEBUG>> L5: payload: %d\n", payload);
	fprintf(stdout, "---------------------------------------\n");
}

static bool is_offset_correct(struct xsk_umem_info *umem, struct pkt_stream *pkt_stream, u64 addr,
			      u64 pkt_stream_addr)
{
	u32 headroom = umem->unaligned_mode ? 0 : umem->frame_headroom;
	u32 offset = addr % umem->frame_size, expected_offset = 0;

	if (!pkt_stream->use_addr_for_fill)
		pkt_stream_addr = 0;

	expected_offset += (pkt_stream_addr + headroom + XDP_PACKET_HEADROOM) % umem->frame_size;

	if (offset == expected_offset)
		return true;

	ksft_test_result_fail("ERROR: [%s] expected [%u], got [%u]\n", __func__, expected_offset,
			      offset);
	return false;
}

static bool is_pkt_valid(struct pkt *pkt, void *buffer, u64 addr, u32 len)
{
	void *data = xsk_umem__get_data(buffer, addr);
	struct iphdr *iphdr = (struct iphdr *)(data + sizeof(struct ethhdr));

	if (!pkt) {
		ksft_test_result_fail("ERROR: [%s] too many packets received\n", __func__);
		return false;
	}

	if (len < PKT_SIZE) {
		/*Do not try to verify packets that are smaller than minimum size. */
		return true;
	}

	if (pkt->len != len) {
		ksft_test_result_fail
			("ERROR: [%s] expected length [%d], got length [%d]\n",
			 __func__, pkt->len, len);
		return false;
	}

	if (iphdr->version == IP_PKT_VER && iphdr->tos == IP_PKT_TOS) {
		u32 seqnum = ntohl(*((u32 *)(data + PKT_HDR_SIZE)));

		if (opt_pkt_dump)
			pkt_dump(data, PKT_SIZE);

		if (pkt->payload != seqnum) {
			ksft_test_result_fail
				("ERROR: [%s] expected seqnum [%d], got seqnum [%d]\n",
					__func__, pkt->payload, seqnum);
			return false;
		}
	} else {
		ksft_print_msg("Invalid frame received: ");
		ksft_print_msg("[IP_PKT_VER: %02X], [IP_PKT_TOS: %02X]\n", iphdr->version,
			       iphdr->tos);
		return false;
	}

	return true;
}

static void kick_tx(struct xsk_socket_info *xsk)
{
	int ret;

	ret = sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
	if (ret >= 0 || errno == ENOBUFS || errno == EAGAIN || errno == EBUSY || errno == ENETDOWN)
		return;
	exit_with_error(errno);
}

static void complete_pkts(struct xsk_socket_info *xsk, int batch_size)
{
	unsigned int rcvd;
	u32 idx;

	if (xsk_ring_prod__needs_wakeup(&xsk->tx))
		kick_tx(xsk);

	rcvd = xsk_ring_cons__peek(&xsk->umem->cq, batch_size, &idx);
	if (rcvd) {
		if (rcvd > xsk->outstanding_tx) {
			u64 addr = *xsk_ring_cons__comp_addr(&xsk->umem->cq, idx + rcvd - 1);

			ksft_test_result_fail("ERROR: [%s] Too many packets completed\n",
					      __func__);
			ksft_print_msg("Last completion address: %llx\n", addr);
			return;
		}

		xsk_ring_cons__release(&xsk->umem->cq, rcvd);
		xsk->outstanding_tx -= rcvd;
	}
}

static void receive_pkts(struct pkt_stream *pkt_stream, struct xsk_socket_info *xsk,
			 struct pollfd *fds)
{
	struct pkt *pkt = pkt_stream_get_next_rx_pkt(pkt_stream);
	struct xsk_umem_info *umem = xsk->umem;
	u32 idx_rx = 0, idx_fq = 0, rcvd, i;
	u32 total = 0;
	int ret;

	while (pkt) {
		rcvd = xsk_ring_cons__peek(&xsk->rx, BATCH_SIZE, &idx_rx);
		if (!rcvd) {
			if (xsk_ring_prod__needs_wakeup(&umem->fq)) {
				ret = poll(fds, 1, POLL_TMOUT);
				if (ret < 0)
					exit_with_error(-ret);
			}
			continue;
		}

		ret = xsk_ring_prod__reserve(&umem->fq, rcvd, &idx_fq);
		while (ret != rcvd) {
			if (ret < 0)
				exit_with_error(-ret);
			if (xsk_ring_prod__needs_wakeup(&umem->fq)) {
				ret = poll(fds, 1, POLL_TMOUT);
				if (ret < 0)
					exit_with_error(-ret);
			}
			ret = xsk_ring_prod__reserve(&umem->fq, rcvd, &idx_fq);
		}

		for (i = 0; i < rcvd; i++) {
			const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++);
			u64 addr = desc->addr, orig;

			if (!pkt) {
				ksft_test_result_fail("ERROR: [%s] Received too many packets.\n",
						      __func__);
				ksft_print_msg("Last packet has addr: %llx len: %u\n",
					       addr, desc->len);
				return;
			}

			orig = xsk_umem__extract_addr(addr);
			addr = xsk_umem__add_offset_to_addr(addr);

			if (!is_pkt_valid(pkt, umem->buffer, addr, desc->len))
				return;
			if (!is_offset_correct(umem, pkt_stream, addr, pkt->addr))
				return;

			*xsk_ring_prod__fill_addr(&umem->fq, idx_fq++) = orig;
			pkt = pkt_stream_get_next_rx_pkt(pkt_stream);
		}

		xsk_ring_prod__submit(&umem->fq, rcvd);
		xsk_ring_cons__release(&xsk->rx, rcvd);

		pthread_mutex_lock(&pacing_mutex);
		pkts_in_flight -= rcvd;
		total += rcvd;
		if (pkts_in_flight < umem->num_frames)
			pthread_cond_signal(&pacing_cond);
		pthread_mutex_unlock(&pacing_mutex);
	}
}

static u32 __send_pkts(struct ifobject *ifobject, u32 pkt_nb)
{
	struct xsk_socket_info *xsk = ifobject->xsk;
	u32 i, idx, valid_pkts = 0;

	while (xsk_ring_prod__reserve(&xsk->tx, BATCH_SIZE, &idx) < BATCH_SIZE)
		complete_pkts(xsk, BATCH_SIZE);

	for (i = 0; i < BATCH_SIZE; i++) {
		struct xdp_desc *tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, idx + i);
		struct pkt *pkt = pkt_generate(ifobject, pkt_nb);

		if (!pkt)
			break;

		tx_desc->addr = pkt->addr;
		tx_desc->len = pkt->len;
		pkt_nb++;
		if (pkt->valid)
			valid_pkts++;
	}

	pthread_mutex_lock(&pacing_mutex);
	pkts_in_flight += valid_pkts;
	if (ifobject->pacing_on && pkts_in_flight >= ifobject->umem->num_frames - BATCH_SIZE) {
		kick_tx(xsk);
		pthread_cond_wait(&pacing_cond, &pacing_mutex);
	}
	pthread_mutex_unlock(&pacing_mutex);

	xsk_ring_prod__submit(&xsk->tx, i);
	xsk->outstanding_tx += valid_pkts;
	complete_pkts(xsk, i);

	usleep(10);
	return i;
}

static void wait_for_tx_completion(struct xsk_socket_info *xsk)
{
	while (xsk->outstanding_tx)
		complete_pkts(xsk, BATCH_SIZE);
}

static void send_pkts(struct ifobject *ifobject)
{
	struct pollfd fds = { };
	u32 pkt_cnt = 0;

	fds.fd = xsk_socket__fd(ifobject->xsk->xsk);
	fds.events = POLLOUT;

	while (pkt_cnt < ifobject->pkt_stream->nb_pkts) {
		if (ifobject->use_poll) {
			int ret;

			ret = poll(&fds, 1, POLL_TMOUT);
			if (ret <= 0)
				continue;

			if (!(fds.revents & POLLOUT))
				continue;
		}

		pkt_cnt += __send_pkts(ifobject, pkt_cnt);
	}

	wait_for_tx_completion(ifobject->xsk);
}

static bool rx_stats_are_valid(struct ifobject *ifobject)
{
	u32 xsk_stat = 0, expected_stat = ifobject->pkt_stream->nb_pkts;
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	int fd = xsk_socket__fd(xsk);
	struct xdp_statistics stats;
	socklen_t optlen;
	int err;

	optlen = sizeof(stats);
	err = getsockopt(fd, SOL_XDP, XDP_STATISTICS, &stats, &optlen);
	if (err) {
		ksft_test_result_fail("ERROR Rx: [%s] getsockopt(XDP_STATISTICS) error %u %s\n",
				      __func__, -err, strerror(-err));
		return true;
	}

	if (optlen == sizeof(struct xdp_statistics)) {
		switch (stat_test_type) {
		case STAT_TEST_RX_DROPPED:
			xsk_stat = stats.rx_dropped;
			break;
		case STAT_TEST_TX_INVALID:
			return true;
		case STAT_TEST_RX_FULL:
			xsk_stat = stats.rx_ring_full;
			expected_stat -= RX_FULL_RXQSIZE;
			break;
		case STAT_TEST_RX_FILL_EMPTY:
			xsk_stat = stats.rx_fill_ring_empty_descs;
			break;
		default:
			break;
		}

		if (xsk_stat == expected_stat)
			return true;
	}

	return false;
}

static void tx_stats_validate(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	int fd = xsk_socket__fd(xsk);
	struct xdp_statistics stats;
	socklen_t optlen;
	int err;

	optlen = sizeof(stats);
	err = getsockopt(fd, SOL_XDP, XDP_STATISTICS, &stats, &optlen);
	if (err) {
		ksft_test_result_fail("ERROR Tx: [%s] getsockopt(XDP_STATISTICS) error %u %s\n",
				      __func__, -err, strerror(-err));
		return;
	}

	if (stats.tx_invalid_descs == ifobject->pkt_stream->nb_pkts)
		return;

	ksft_test_result_fail("ERROR: [%s] tx_invalid_descs incorrect. Got [%u] expected [%u]\n",
			      __func__, stats.tx_invalid_descs, ifobject->pkt_stream->nb_pkts);
}

static void thread_common_ops(struct test_spec *test, struct ifobject *ifobject)
{
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
	u32 i;

	ifobject->ns_fd = switch_namespace(ifobject->nsname);

	if (ifobject->umem->unaligned_mode)
		mmap_flags |= MAP_HUGETLB;

	for (i = 0; i < test->nb_sockets; i++) {
		u64 umem_sz = ifobject->umem->num_frames * ifobject->umem->frame_size;
		u32 ctr = 0;
		void *bufs;
		int ret;

		bufs = mmap(NULL, umem_sz, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
		if (bufs == MAP_FAILED)
			exit_with_error(errno);

		ret = xsk_configure_umem(&ifobject->umem_arr[i], bufs, umem_sz);
		if (ret)
			exit_with_error(-ret);

		while (ctr++ < SOCK_RECONF_CTR) {
			ret = xsk_configure_socket(&ifobject->xsk_arr[i], &ifobject->umem_arr[i],
						   ifobject, i);
			if (!ret)
				break;

			/* Retry if it fails as xsk_socket__create() is asynchronous */
			if (ctr >= SOCK_RECONF_CTR)
				exit_with_error(-ret);
			usleep(USLEEP_MAX);
		}
	}

	ifobject->umem = &ifobject->umem_arr[0];
	ifobject->xsk = &ifobject->xsk_arr[0];
}

static void testapp_cleanup_xsk_res(struct ifobject *ifobj)
{
	print_verbose("Destroying socket\n");
	xsk_socket__delete(ifobj->xsk->xsk);
	munmap(ifobj->umem->buffer, ifobj->umem->num_frames * ifobj->umem->frame_size);
	xsk_umem__delete(ifobj->umem->umem);
}

static void *worker_testapp_validate_tx(void *arg)
{
	struct test_spec *test = (struct test_spec *)arg;
	struct ifobject *ifobject = test->ifobj_tx;

	if (test->current_step == 1)
		thread_common_ops(test, ifobject);

	print_verbose("Sending %d packets on interface %s\n", ifobject->pkt_stream->nb_pkts,
		      ifobject->ifname);
	send_pkts(ifobject);

	if (stat_test_type == STAT_TEST_TX_INVALID)
		tx_stats_validate(ifobject);

	if (test->total_steps == test->current_step)
		testapp_cleanup_xsk_res(ifobject);
	pthread_exit(NULL);
}

static void xsk_populate_fill_ring(struct xsk_umem_info *umem, struct pkt_stream *pkt_stream)
{
	u32 idx = 0, i, buffers_to_fill;
	int ret;

	if (umem->num_frames < XSK_RING_PROD__DEFAULT_NUM_DESCS)
		buffers_to_fill = umem->num_frames;
	else
		buffers_to_fill = XSK_RING_PROD__DEFAULT_NUM_DESCS;

	ret = xsk_ring_prod__reserve(&umem->fq, buffers_to_fill, &idx);
	if (ret != buffers_to_fill)
		exit_with_error(ENOSPC);
	for (i = 0; i < buffers_to_fill; i++) {
		u64 addr;

		if (pkt_stream->use_addr_for_fill) {
			struct pkt *pkt = pkt_stream_get_pkt(pkt_stream, i);

			if (!pkt)
				break;
			addr = pkt->addr;
		} else {
			addr = i * umem->frame_size;
		}

		*xsk_ring_prod__fill_addr(&umem->fq, idx++) = addr;
	}
	xsk_ring_prod__submit(&umem->fq, buffers_to_fill);
}

static void *worker_testapp_validate_rx(void *arg)
{
	struct test_spec *test = (struct test_spec *)arg;
	struct ifobject *ifobject = test->ifobj_rx;
	struct pollfd fds = { };

	if (test->current_step == 1)
		thread_common_ops(test, ifobject);

	xsk_populate_fill_ring(ifobject->umem, ifobject->pkt_stream);

	fds.fd = xsk_socket__fd(ifobject->xsk->xsk);
	fds.events = POLLIN;

	pthread_barrier_wait(&barr);

	if (test_type == TEST_TYPE_STATS)
		while (!rx_stats_are_valid(ifobject))
			continue;
	else
		receive_pkts(ifobject->pkt_stream, ifobject->xsk, &fds);

	if (test->total_steps == test->current_step)
		testapp_cleanup_xsk_res(ifobject);
	pthread_exit(NULL);
}

static void testapp_validate_traffic(struct test_spec *test)
{
	struct ifobject *ifobj_tx = test->ifobj_tx;
	struct ifobject *ifobj_rx = test->ifobj_rx;
	pthread_t t0, t1;

	if (pthread_barrier_init(&barr, NULL, 2))
		exit_with_error(errno);

	test->current_step++;
	pkt_stream_reset(ifobj_rx->pkt_stream);
	pkts_in_flight = 0;

	/*Spawn RX thread */
	pthread_create(&t0, NULL, ifobj_rx->func_ptr, test);

	pthread_barrier_wait(&barr);
	if (pthread_barrier_destroy(&barr))
		exit_with_error(errno);

	/*Spawn TX thread */
	pthread_create(&t1, NULL, ifobj_tx->func_ptr, test);

	pthread_join(t1, NULL);
	pthread_join(t0, NULL);
}

static void testapp_teardown(struct test_spec *test)
{
	int i;

	test_spec_set_name(test, "TEARDOWN");
	for (i = 0; i < MAX_TEARDOWN_ITER; i++) {
		testapp_validate_traffic(test);
		test_spec_reset(test);
	}
}

static void swap_directions(struct ifobject **ifobj1, struct ifobject **ifobj2)
{
	thread_func_t tmp_func_ptr = (*ifobj1)->func_ptr;
	struct ifobject *tmp_ifobj = (*ifobj1);

	(*ifobj1)->func_ptr = (*ifobj2)->func_ptr;
	(*ifobj2)->func_ptr = tmp_func_ptr;

	*ifobj1 = *ifobj2;
	*ifobj2 = tmp_ifobj;
}

static void testapp_bidi(struct test_spec *test)
{
	test_spec_set_name(test, "BIDIRECTIONAL");
	test->ifobj_tx->rx_on = true;
	test->ifobj_rx->tx_on = true;
	test->total_steps = 2;
	testapp_validate_traffic(test);

	print_verbose("Switching Tx/Rx vectors\n");
	swap_directions(&test->ifobj_rx, &test->ifobj_tx);
	testapp_validate_traffic(test);

	swap_directions(&test->ifobj_rx, &test->ifobj_tx);
}

static void swap_xsk_resources(struct ifobject *ifobj_tx, struct ifobject *ifobj_rx)
{
	xsk_socket__delete(ifobj_tx->xsk->xsk);
	xsk_umem__delete(ifobj_tx->umem->umem);
	xsk_socket__delete(ifobj_rx->xsk->xsk);
	xsk_umem__delete(ifobj_rx->umem->umem);
	ifobj_tx->umem = &ifobj_tx->umem_arr[1];
	ifobj_tx->xsk = &ifobj_tx->xsk_arr[1];
	ifobj_rx->umem = &ifobj_rx->umem_arr[1];
	ifobj_rx->xsk = &ifobj_rx->xsk_arr[1];
}

static void testapp_bpf_res(struct test_spec *test)
{
	test_spec_set_name(test, "BPF_RES");
	test->total_steps = 2;
	test->nb_sockets = 2;
	testapp_validate_traffic(test);

	swap_xsk_resources(test->ifobj_tx, test->ifobj_rx);
	testapp_validate_traffic(test);
}

static void testapp_headroom(struct test_spec *test)
{
	test_spec_set_name(test, "UMEM_HEADROOM");
	test->ifobj_rx->umem->frame_headroom = UMEM_HEADROOM_TEST_SIZE;
	testapp_validate_traffic(test);
}

static void testapp_stats(struct test_spec *test)
{
	int i;

	for (i = 0; i < STAT_TEST_TYPE_MAX; i++) {
		test_spec_reset(test);
		stat_test_type = i;
		/* No or few packets will be received so cannot pace packets */
		test->ifobj_tx->pacing_on = false;

		switch (stat_test_type) {
		case STAT_TEST_RX_DROPPED:
			test_spec_set_name(test, "STAT_RX_DROPPED");
			test->ifobj_rx->umem->frame_headroom = test->ifobj_rx->umem->frame_size -
				XDP_PACKET_HEADROOM - 1;
			testapp_validate_traffic(test);
			break;
		case STAT_TEST_RX_FULL:
			test_spec_set_name(test, "STAT_RX_FULL");
			test->ifobj_rx->xsk->rxqsize = RX_FULL_RXQSIZE;
			testapp_validate_traffic(test);
			break;
		case STAT_TEST_TX_INVALID:
			test_spec_set_name(test, "STAT_TX_INVALID");
			pkt_stream_replace(test, DEFAULT_PKT_CNT, XSK_UMEM__INVALID_FRAME_SIZE);
			testapp_validate_traffic(test);

			pkt_stream_restore_default(test);
			break;
		case STAT_TEST_RX_FILL_EMPTY:
			test_spec_set_name(test, "STAT_RX_FILL_EMPTY");
			test->ifobj_rx->pkt_stream = pkt_stream_generate(test->ifobj_rx->umem, 0,
									 MIN_PKT_SIZE);
			if (!test->ifobj_rx->pkt_stream)
				exit_with_error(ENOMEM);
			test->ifobj_rx->pkt_stream->use_addr_for_fill = true;
			testapp_validate_traffic(test);

			pkt_stream_restore_default(test);
			break;
		default:
			break;
		}
	}

	/* To only see the whole stat set being completed unless an individual test fails. */
	test_spec_set_name(test, "STATS");
}

/* Simple test */
static bool hugepages_present(struct ifobject *ifobject)
{
	const size_t mmap_sz = 2 * ifobject->umem->num_frames * ifobject->umem->frame_size;
	void *bufs;

	bufs = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_HUGETLB, -1, 0);
	if (bufs == MAP_FAILED)
		return false;

	munmap(bufs, mmap_sz);
	return true;
}

static bool testapp_unaligned(struct test_spec *test)
{
	if (!hugepages_present(test->ifobj_tx)) {
		ksft_test_result_skip("No 2M huge pages present.\n");
		return false;
	}

	test_spec_set_name(test, "UNALIGNED_MODE");
	test->ifobj_tx->umem->unaligned_mode = true;
	test->ifobj_rx->umem->unaligned_mode = true;
	/* Let half of the packets straddle a buffer boundrary */
	pkt_stream_replace_half(test, PKT_SIZE, -PKT_SIZE / 2);
	test->ifobj_rx->pkt_stream->use_addr_for_fill = true;
	testapp_validate_traffic(test);

	pkt_stream_restore_default(test);
	return true;
}

static void testapp_single_pkt(struct test_spec *test)
{
	struct pkt pkts[] = {{0x1000, PKT_SIZE, 0, true}};

	pkt_stream_generate_custom(test, pkts, ARRAY_SIZE(pkts));
	testapp_validate_traffic(test);
	pkt_stream_restore_default(test);
}

static void testapp_invalid_desc(struct test_spec *test)
{
	struct pkt pkts[] = {
		/* Zero packet length at address zero allowed */
		{0, 0, 0, true},
		/* Zero packet length allowed */
		{0x1000, 0, 0, true},
		/* Straddling the start of umem */
		{-2, PKT_SIZE, 0, false},
		/* Packet too large */
		{0x2000, XSK_UMEM__INVALID_FRAME_SIZE, 0, false},
		/* After umem ends */
		{UMEM_SIZE, PKT_SIZE, 0, false},
		/* Straddle the end of umem */
		{UMEM_SIZE - PKT_SIZE / 2, PKT_SIZE, 0, false},
		/* Straddle a page boundrary */
		{0x3000 - PKT_SIZE / 2, PKT_SIZE, 0, false},
		/* Straddle a 2K boundrary */
		{0x3800 - PKT_SIZE / 2, PKT_SIZE, 0, true},
		/* Valid packet for synch so that something is received */
		{0x4000, PKT_SIZE, 0, true}};

	if (test->ifobj_tx->umem->unaligned_mode) {
		/* Crossing a page boundrary allowed */
		pkts[6].valid = true;
	}
	if (test->ifobj_tx->umem->frame_size == XSK_UMEM__DEFAULT_FRAME_SIZE / 2) {
		/* Crossing a 2K frame size boundrary not allowed */
		pkts[7].valid = false;
	}

	pkt_stream_generate_custom(test, pkts, ARRAY_SIZE(pkts));
	testapp_validate_traffic(test);
	pkt_stream_restore_default(test);
}

static void init_iface(struct ifobject *ifobj, const char *dst_mac, const char *src_mac,
		       const char *dst_ip, const char *src_ip, const u16 dst_port,
		       const u16 src_port, thread_func_t func_ptr)
{
	struct in_addr ip;

	memcpy(ifobj->dst_mac, dst_mac, ETH_ALEN);
	memcpy(ifobj->src_mac, src_mac, ETH_ALEN);

	inet_aton(dst_ip, &ip);
	ifobj->dst_ip = ip.s_addr;

	inet_aton(src_ip, &ip);
	ifobj->src_ip = ip.s_addr;

	ifobj->dst_port = dst_port;
	ifobj->src_port = src_port;

	ifobj->func_ptr = func_ptr;
}

static void run_pkt_test(struct test_spec *test, enum test_mode mode, enum test_type type)
{
	test_type = type;

	/* reset defaults after potential previous test */
	stat_test_type = -1;

	switch (test_type) {
	case TEST_TYPE_STATS:
		testapp_stats(test);
		break;
	case TEST_TYPE_TEARDOWN:
		testapp_teardown(test);
		break;
	case TEST_TYPE_BIDI:
		testapp_bidi(test);
		break;
	case TEST_TYPE_BPF_RES:
		testapp_bpf_res(test);
		break;
	case TEST_TYPE_RUN_TO_COMPLETION:
		test_spec_set_name(test, "RUN_TO_COMPLETION");
		testapp_validate_traffic(test);
		break;
	case TEST_TYPE_RUN_TO_COMPLETION_SINGLE_PKT:
		test_spec_set_name(test, "RUN_TO_COMPLETION_SINGLE_PKT");
		testapp_single_pkt(test);
		break;
	case TEST_TYPE_RUN_TO_COMPLETION_2K_FRAME:
		test_spec_set_name(test, "RUN_TO_COMPLETION_2K_FRAME_SIZE");
		test->ifobj_tx->umem->frame_size = 2048;
		test->ifobj_rx->umem->frame_size = 2048;
		pkt_stream_replace(test, DEFAULT_PKT_CNT, MIN_PKT_SIZE);
		testapp_validate_traffic(test);

		pkt_stream_restore_default(test);
		break;
	case TEST_TYPE_POLL:
		test->ifobj_tx->use_poll = true;
		test->ifobj_rx->use_poll = true;
		test_spec_set_name(test, "POLL");
		testapp_validate_traffic(test);
		break;
	case TEST_TYPE_ALIGNED_INV_DESC:
		test_spec_set_name(test, "ALIGNED_INV_DESC");
		testapp_invalid_desc(test);
		break;
	case TEST_TYPE_ALIGNED_INV_DESC_2K_FRAME:
		test_spec_set_name(test, "ALIGNED_INV_DESC_2K_FRAME_SIZE");
		test->ifobj_tx->umem->frame_size = 2048;
		test->ifobj_rx->umem->frame_size = 2048;
		testapp_invalid_desc(test);
		break;
	case TEST_TYPE_UNALIGNED_INV_DESC:
		test_spec_set_name(test, "UNALIGNED_INV_DESC");
		test->ifobj_tx->umem->unaligned_mode = true;
		test->ifobj_rx->umem->unaligned_mode = true;
		testapp_invalid_desc(test);
		break;
	case TEST_TYPE_UNALIGNED:
		if (!testapp_unaligned(test))
			return;
		break;
	case TEST_TYPE_HEADROOM:
		testapp_headroom(test);
		break;
	default:
		break;
	}

	print_ksft_result(test);
}

static struct ifobject *ifobject_create(void)
{
	struct ifobject *ifobj;

	ifobj = calloc(1, sizeof(struct ifobject));
	if (!ifobj)
		return NULL;

	ifobj->xsk_arr = calloc(MAX_SOCKETS, sizeof(*ifobj->xsk_arr));
	if (!ifobj->xsk_arr)
		goto out_xsk_arr;

	ifobj->umem_arr = calloc(MAX_SOCKETS, sizeof(*ifobj->umem_arr));
	if (!ifobj->umem_arr)
		goto out_umem_arr;

	return ifobj;

out_umem_arr:
	free(ifobj->xsk_arr);
out_xsk_arr:
	free(ifobj);
	return NULL;
}

static void ifobject_delete(struct ifobject *ifobj)
{
	free(ifobj->umem_arr);
	free(ifobj->xsk_arr);
	free(ifobj);
}

int main(int argc, char **argv)
{
	struct rlimit _rlim = { RLIM_INFINITY, RLIM_INFINITY };
	struct pkt_stream *pkt_stream_default;
	struct ifobject *ifobj_tx, *ifobj_rx;
	struct test_spec test;
	u32 i, j;

	if (setrlimit(RLIMIT_MEMLOCK, &_rlim))
		exit_with_error(errno);

	ifobj_tx = ifobject_create();
	if (!ifobj_tx)
		exit_with_error(ENOMEM);
	ifobj_rx = ifobject_create();
	if (!ifobj_rx)
		exit_with_error(ENOMEM);

	setlocale(LC_ALL, "");

	parse_command_line(ifobj_tx, ifobj_rx, argc, argv);

	if (!validate_interface(ifobj_tx) || !validate_interface(ifobj_rx)) {
		usage(basename(argv[0]));
		ksft_exit_xfail();
	}

	init_iface(ifobj_tx, MAC1, MAC2, IP1, IP2, UDP_PORT1, UDP_PORT2,
		   worker_testapp_validate_tx);
	init_iface(ifobj_rx, MAC2, MAC1, IP2, IP1, UDP_PORT2, UDP_PORT1,
		   worker_testapp_validate_rx);

	test_spec_init(&test, ifobj_tx, ifobj_rx, 0);
	pkt_stream_default = pkt_stream_generate(ifobj_tx->umem, DEFAULT_PKT_CNT, PKT_SIZE);
	if (!pkt_stream_default)
		exit_with_error(ENOMEM);
	test.pkt_stream_default = pkt_stream_default;

	ksft_set_plan(TEST_MODE_MAX * TEST_TYPE_MAX);

	for (i = 0; i < TEST_MODE_MAX; i++)
		for (j = 0; j < TEST_TYPE_MAX; j++) {
			test_spec_init(&test, ifobj_tx, ifobj_rx, i);
			run_pkt_test(&test, i, j);
			usleep(USLEEP_MAX);
		}

	pkt_stream_delete(pkt_stream_default);
	ifobject_delete(ifobj_tx);
	ifobject_delete(ifobj_rx);

	ksft_exit_pass();
	return 0;
}
