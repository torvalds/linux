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
 *    j. If multi-buffer is supported, send 9k packets divided into 3 frames
 *    k. If multi-buffer and huge pages are supported, send 9k packets in a single frame
 *       using unaligned mode
 *    l. If multi-buffer is supported, try various nasty combinations of descriptors to
 *       check if they pass the validation or not
 *
 * Flow:
 * -----
 * - Single process spawns two threads: Tx and Rx
 * - Each of these two threads attach to a veth interface
 * - Each thread creates one AF_XDP socket connected to a unique umem for each
 *   veth interface
 * - Tx thread Transmits a number of packets from veth<xxxx> to veth<yyyy>
 * - Rx thread verifies if all packets were received and delivered in-order,
 *   and have the right content
 *
 * Enable/disable packet dump mode:
 * --------------------------
 * To enable L2 - L4 headers and payload dump of each packet on STDOUT, add
 * parameter -D to params array in test_xsk.sh, i.e. params=("-S" "-D")
 */

#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/mman.h>
#include <linux/netdev.h>
#include <linux/bitmap.h>
#include <linux/ethtool.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "xsk_xdp_progs.skel.h"
#include "xsk.h"
#include "xskxceiver.h"
#include <bpf/bpf.h>
#include <linux/filter.h>
#include "../kselftest.h"
#include "xsk_xdp_common.h"

#include <network_helpers.h>

#define MAX_TX_BUDGET_DEFAULT 32

static bool opt_verbose;
static bool opt_print_tests;
static enum test_mode opt_mode = TEST_MODE_ALL;
static u32 opt_run_test = RUN_ALL_TESTS;

void test__fail(void) { /* for network_helpers.c */ }

static void __exit_with_error(int error, const char *file, const char *func, int line)
{
	ksft_test_result_fail("[%s:%s:%i]: ERROR: %d/\"%s\"\n", file, func, line, error,
			      strerror(error));
	ksft_exit_xfail();
}

#define exit_with_error(error) __exit_with_error(error, __FILE__, __func__, __LINE__)
#define busy_poll_string(test) (test)->ifobj_tx->busy_poll ? "BUSY-POLL " : ""
static char *mode_string(struct test_spec *test)
{
	switch (test->mode) {
	case TEST_MODE_SKB:
		return "SKB";
	case TEST_MODE_DRV:
		return "DRV";
	case TEST_MODE_ZC:
		return "ZC";
	default:
		return "BOGUS";
	}
}

static void report_failure(struct test_spec *test)
{
	if (test->fail)
		return;

	ksft_test_result_fail("FAIL: %s %s%s\n", mode_string(test), busy_poll_string(test),
			      test->name);
	test->fail = true;
}

/* The payload is a word consisting of a packet sequence number in the upper
 * 16-bits and a intra packet data sequence number in the lower 16 bits. So the 3rd packet's
 * 5th word of data will contain the number (2<<16) | 4 as they are numbered from 0.
 */
static void write_payload(void *dest, u32 pkt_nb, u32 start, u32 size)
{
	u32 *ptr = (u32 *)dest, i;

	start /= sizeof(*ptr);
	size /= sizeof(*ptr);
	for (i = 0; i < size; i++)
		ptr[i] = htonl(pkt_nb << 16 | (i + start));
}

static void gen_eth_hdr(struct xsk_socket_info *xsk, struct ethhdr *eth_hdr)
{
	memcpy(eth_hdr->h_dest, xsk->dst_mac, ETH_ALEN);
	memcpy(eth_hdr->h_source, xsk->src_mac, ETH_ALEN);
	eth_hdr->h_proto = htons(ETH_P_LOOPBACK);
}

static bool is_umem_valid(struct ifobject *ifobj)
{
	return !!ifobj->umem->umem;
}

static u32 mode_to_xdp_flags(enum test_mode mode)
{
	return (mode == TEST_MODE_SKB) ? XDP_FLAGS_SKB_MODE : XDP_FLAGS_DRV_MODE;
}

static u64 umem_size(struct xsk_umem_info *umem)
{
	return umem->num_frames * umem->frame_size;
}

static int xsk_configure_umem(struct ifobject *ifobj, struct xsk_umem_info *umem, void *buffer,
			      u64 size)
{
	struct xsk_umem_config cfg = {
		.fill_size = XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.comp_size = XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.frame_size = umem->frame_size,
		.frame_headroom = umem->frame_headroom,
		.flags = XSK_UMEM__DEFAULT_FLAGS
	};
	int ret;

	if (umem->fill_size)
		cfg.fill_size = umem->fill_size;

	if (umem->comp_size)
		cfg.comp_size = umem->comp_size;

	if (umem->unaligned_mode)
		cfg.flags |= XDP_UMEM_UNALIGNED_CHUNK_FLAG;

	ret = xsk_umem__create(&umem->umem, buffer, size,
			       &umem->fq, &umem->cq, &cfg);
	if (ret)
		return ret;

	umem->buffer = buffer;
	if (ifobj->shared_umem && ifobj->rx_on) {
		umem->base_addr = umem_size(umem);
		umem->next_buffer = umem_size(umem);
	}

	return 0;
}

static u64 umem_alloc_buffer(struct xsk_umem_info *umem)
{
	u64 addr;

	addr = umem->next_buffer;
	umem->next_buffer += umem->frame_size;
	if (umem->next_buffer >= umem->base_addr + umem_size(umem))
		umem->next_buffer = umem->base_addr;

	return addr;
}

static void umem_reset_alloc(struct xsk_umem_info *umem)
{
	umem->next_buffer = 0;
}

static void enable_busy_poll(struct xsk_socket_info *xsk)
{
	int sock_opt;

	sock_opt = 1;
	if (setsockopt(xsk_socket__fd(xsk->xsk), SOL_SOCKET, SO_PREFER_BUSY_POLL,
		       (void *)&sock_opt, sizeof(sock_opt)) < 0)
		exit_with_error(errno);

	sock_opt = 20;
	if (setsockopt(xsk_socket__fd(xsk->xsk), SOL_SOCKET, SO_BUSY_POLL,
		       (void *)&sock_opt, sizeof(sock_opt)) < 0)
		exit_with_error(errno);

	sock_opt = xsk->batch_size;
	if (setsockopt(xsk_socket__fd(xsk->xsk), SOL_SOCKET, SO_BUSY_POLL_BUDGET,
		       (void *)&sock_opt, sizeof(sock_opt)) < 0)
		exit_with_error(errno);
}

static int __xsk_configure_socket(struct xsk_socket_info *xsk, struct xsk_umem_info *umem,
				  struct ifobject *ifobject, bool shared)
{
	struct xsk_socket_config cfg = {};
	struct xsk_ring_cons *rxr;
	struct xsk_ring_prod *txr;

	xsk->umem = umem;
	cfg.rx_size = xsk->rxqsize;
	cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	cfg.bind_flags = ifobject->bind_flags;
	if (shared)
		cfg.bind_flags |= XDP_SHARED_UMEM;
	if (ifobject->mtu > MAX_ETH_PKT_SIZE)
		cfg.bind_flags |= XDP_USE_SG;
	if (umem->comp_size)
		cfg.tx_size = umem->comp_size;
	if (umem->fill_size)
		cfg.rx_size = umem->fill_size;

	txr = ifobject->tx_on ? &xsk->tx : NULL;
	rxr = ifobject->rx_on ? &xsk->rx : NULL;
	return xsk_socket__create(&xsk->xsk, ifobject->ifindex, 0, umem->umem, rxr, txr, &cfg);
}

static bool ifobj_zc_avail(struct ifobject *ifobject)
{
	size_t umem_sz = DEFAULT_UMEM_BUFFERS * XSK_UMEM__DEFAULT_FRAME_SIZE;
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
	struct xsk_socket_info *xsk;
	struct xsk_umem_info *umem;
	bool zc_avail = false;
	void *bufs;
	int ret;

	bufs = mmap(NULL, umem_sz, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
	if (bufs == MAP_FAILED)
		exit_with_error(errno);

	umem = calloc(1, sizeof(struct xsk_umem_info));
	if (!umem) {
		munmap(bufs, umem_sz);
		exit_with_error(ENOMEM);
	}
	umem->frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE;
	ret = xsk_configure_umem(ifobject, umem, bufs, umem_sz);
	if (ret)
		exit_with_error(-ret);

	xsk = calloc(1, sizeof(struct xsk_socket_info));
	if (!xsk)
		goto out;
	ifobject->bind_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY;
	ifobject->rx_on = true;
	xsk->rxqsize = XSK_RING_CONS__DEFAULT_NUM_DESCS;
	ret = __xsk_configure_socket(xsk, umem, ifobject, false);
	if (!ret)
		zc_avail = true;

	xsk_socket__delete(xsk->xsk);
	free(xsk);
out:
	munmap(umem->buffer, umem_sz);
	xsk_umem__delete(umem->umem);
	free(umem);
	return zc_avail;
}

#define MAX_SKB_FRAGS_PATH "/proc/sys/net/core/max_skb_frags"
static unsigned int get_max_skb_frags(void)
{
	unsigned int max_skb_frags = 0;
	FILE *file;

	file = fopen(MAX_SKB_FRAGS_PATH, "r");
	if (!file) {
		ksft_print_msg("Error opening %s\n", MAX_SKB_FRAGS_PATH);
		return 0;
	}

	if (fscanf(file, "%u", &max_skb_frags) != 1)
		ksft_print_msg("Error reading %s\n", MAX_SKB_FRAGS_PATH);

	fclose(file);
	return max_skb_frags;
}

static struct option long_options[] = {
	{"interface", required_argument, 0, 'i'},
	{"busy-poll", no_argument, 0, 'b'},
	{"verbose", no_argument, 0, 'v'},
	{"mode", required_argument, 0, 'm'},
	{"list", no_argument, 0, 'l'},
	{"test", required_argument, 0, 't'},
	{"help", no_argument, 0, 'h'},
	{0, 0, 0, 0}
};

static void print_usage(char **argv)
{
	const char *str =
		"  Usage: xskxceiver [OPTIONS]\n"
		"  Options:\n"
		"  -i, --interface      Use interface\n"
		"  -v, --verbose        Verbose output\n"
		"  -b, --busy-poll      Enable busy poll\n"
		"  -m, --mode           Run only mode skb, drv, or zc\n"
		"  -l, --list           List all available tests\n"
		"  -t, --test           Run a specific test. Enter number from -l option.\n"
		"  -h, --help           Display this help and exit\n";

	ksft_print_msg(str, basename(argv[0]));
	ksft_exit_xfail();
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
		c = getopt_long(argc, argv, "i:vbm:lt:", long_options, &option_index);
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

			memcpy(ifobj->ifname, optarg,
			       min_t(size_t, MAX_INTERFACE_NAME_CHARS, strlen(optarg)));

			ifobj->ifindex = if_nametoindex(ifobj->ifname);
			if (!ifobj->ifindex)
				exit_with_error(errno);

			interface_nb++;
			break;
		case 'v':
			opt_verbose = true;
			break;
		case 'b':
			ifobj_tx->busy_poll = true;
			ifobj_rx->busy_poll = true;
			break;
		case 'm':
			if (!strncmp("skb", optarg, strlen(optarg)))
				opt_mode = TEST_MODE_SKB;
			else if (!strncmp("drv", optarg, strlen(optarg)))
				opt_mode = TEST_MODE_DRV;
			else if (!strncmp("zc", optarg, strlen(optarg)))
				opt_mode = TEST_MODE_ZC;
			else
				print_usage(argv);
			break;
		case 'l':
			opt_print_tests = true;
			break;
		case 't':
			errno = 0;
			opt_run_test = strtol(optarg, NULL, 0);
			if (errno)
				print_usage(argv);
			break;
		case 'h':
		default:
			print_usage(argv);
		}
	}
}

static int set_ring_size(struct ifobject *ifobj)
{
	int ret;
	u32 ctr = 0;

	while (ctr++ < SOCK_RECONF_CTR) {
		ret = set_hw_ring_size(ifobj->ifname, &ifobj->ring);
		if (!ret)
			break;

		/* Retry if it fails */
		if (ctr >= SOCK_RECONF_CTR || errno != EBUSY)
			return -errno;

		usleep(USLEEP_MAX);
	}

	return ret;
}

static int hw_ring_size_reset(struct ifobject *ifobj)
{
	ifobj->ring.tx_pending = ifobj->set_ring.default_tx;
	ifobj->ring.rx_pending = ifobj->set_ring.default_rx;
	return set_ring_size(ifobj);
}

static void __test_spec_init(struct test_spec *test, struct ifobject *ifobj_tx,
			     struct ifobject *ifobj_rx)
{
	u32 i, j;

	for (i = 0; i < MAX_INTERFACES; i++) {
		struct ifobject *ifobj = i ? ifobj_rx : ifobj_tx;

		ifobj->xsk = &ifobj->xsk_arr[0];
		ifobj->use_poll = false;
		ifobj->use_fill_ring = true;
		ifobj->release_rx = true;
		ifobj->validation_func = NULL;
		ifobj->use_metadata = false;

		if (i == 0) {
			ifobj->rx_on = false;
			ifobj->tx_on = true;
		} else {
			ifobj->rx_on = true;
			ifobj->tx_on = false;
		}

		memset(ifobj->umem, 0, sizeof(*ifobj->umem));
		ifobj->umem->num_frames = DEFAULT_UMEM_BUFFERS;
		ifobj->umem->frame_size = XSK_UMEM__DEFAULT_FRAME_SIZE;

		for (j = 0; j < MAX_SOCKETS; j++) {
			memset(&ifobj->xsk_arr[j], 0, sizeof(ifobj->xsk_arr[j]));
			ifobj->xsk_arr[j].rxqsize = XSK_RING_CONS__DEFAULT_NUM_DESCS;
			ifobj->xsk_arr[j].batch_size = DEFAULT_BATCH_SIZE;
			if (i == 0)
				ifobj->xsk_arr[j].pkt_stream = test->tx_pkt_stream_default;
			else
				ifobj->xsk_arr[j].pkt_stream = test->rx_pkt_stream_default;

			memcpy(ifobj->xsk_arr[j].src_mac, g_mac, ETH_ALEN);
			memcpy(ifobj->xsk_arr[j].dst_mac, g_mac, ETH_ALEN);
			ifobj->xsk_arr[j].src_mac[5] += ((j * 2) + 0);
			ifobj->xsk_arr[j].dst_mac[5] += ((j * 2) + 1);
		}
	}

	if (ifobj_tx->hw_ring_size_supp)
		hw_ring_size_reset(ifobj_tx);

	test->ifobj_tx = ifobj_tx;
	test->ifobj_rx = ifobj_rx;
	test->current_step = 0;
	test->total_steps = 1;
	test->nb_sockets = 1;
	test->fail = false;
	test->set_ring = false;
	test->adjust_tail = false;
	test->adjust_tail_support = false;
	test->mtu = MAX_ETH_PKT_SIZE;
	test->xdp_prog_rx = ifobj_rx->xdp_progs->progs.xsk_def_prog;
	test->xskmap_rx = ifobj_rx->xdp_progs->maps.xsk;
	test->xdp_prog_tx = ifobj_tx->xdp_progs->progs.xsk_def_prog;
	test->xskmap_tx = ifobj_tx->xdp_progs->maps.xsk;
}

static void test_spec_init(struct test_spec *test, struct ifobject *ifobj_tx,
			   struct ifobject *ifobj_rx, enum test_mode mode,
			   const struct test_spec *test_to_run)
{
	struct pkt_stream *tx_pkt_stream;
	struct pkt_stream *rx_pkt_stream;
	u32 i;

	tx_pkt_stream = test->tx_pkt_stream_default;
	rx_pkt_stream = test->rx_pkt_stream_default;
	memset(test, 0, sizeof(*test));
	test->tx_pkt_stream_default = tx_pkt_stream;
	test->rx_pkt_stream_default = rx_pkt_stream;

	for (i = 0; i < MAX_INTERFACES; i++) {
		struct ifobject *ifobj = i ? ifobj_rx : ifobj_tx;

		ifobj->bind_flags = XDP_USE_NEED_WAKEUP;
		if (mode == TEST_MODE_ZC)
			ifobj->bind_flags |= XDP_ZEROCOPY;
		else
			ifobj->bind_flags |= XDP_COPY;
	}

	strncpy(test->name, test_to_run->name, MAX_TEST_NAME_SIZE);
	test->test_func = test_to_run->test_func;
	test->mode = mode;
	__test_spec_init(test, ifobj_tx, ifobj_rx);
}

static void test_spec_reset(struct test_spec *test)
{
	__test_spec_init(test, test->ifobj_tx, test->ifobj_rx);
}

static void test_spec_set_xdp_prog(struct test_spec *test, struct bpf_program *xdp_prog_rx,
				   struct bpf_program *xdp_prog_tx, struct bpf_map *xskmap_rx,
				   struct bpf_map *xskmap_tx)
{
	test->xdp_prog_rx = xdp_prog_rx;
	test->xdp_prog_tx = xdp_prog_tx;
	test->xskmap_rx = xskmap_rx;
	test->xskmap_tx = xskmap_tx;
}

static int test_spec_set_mtu(struct test_spec *test, int mtu)
{
	int err;

	if (test->ifobj_rx->mtu != mtu) {
		err = xsk_set_mtu(test->ifobj_rx->ifindex, mtu);
		if (err)
			return err;
		test->ifobj_rx->mtu = mtu;
	}
	if (test->ifobj_tx->mtu != mtu) {
		err = xsk_set_mtu(test->ifobj_tx->ifindex, mtu);
		if (err)
			return err;
		test->ifobj_tx->mtu = mtu;
	}

	return 0;
}

static void pkt_stream_reset(struct pkt_stream *pkt_stream)
{
	if (pkt_stream) {
		pkt_stream->current_pkt_nb = 0;
		pkt_stream->nb_rx_pkts = 0;
	}
}

static struct pkt *pkt_stream_get_next_tx_pkt(struct pkt_stream *pkt_stream)
{
	if (pkt_stream->current_pkt_nb >= pkt_stream->nb_pkts)
		return NULL;

	return &pkt_stream->pkts[pkt_stream->current_pkt_nb++];
}

static struct pkt *pkt_stream_get_next_rx_pkt(struct pkt_stream *pkt_stream, u32 *pkts_sent)
{
	while (pkt_stream->current_pkt_nb < pkt_stream->nb_pkts) {
		(*pkts_sent)++;
		if (pkt_stream->pkts[pkt_stream->current_pkt_nb].valid)
			return &pkt_stream->pkts[pkt_stream->current_pkt_nb++];
		pkt_stream->current_pkt_nb++;
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
	struct pkt_stream *tx_pkt_stream = test->ifobj_tx->xsk->pkt_stream;
	struct pkt_stream *rx_pkt_stream = test->ifobj_rx->xsk->pkt_stream;

	if (tx_pkt_stream != test->tx_pkt_stream_default) {
		pkt_stream_delete(test->ifobj_tx->xsk->pkt_stream);
		test->ifobj_tx->xsk->pkt_stream = test->tx_pkt_stream_default;
	}

	if (rx_pkt_stream != test->rx_pkt_stream_default) {
		pkt_stream_delete(test->ifobj_rx->xsk->pkt_stream);
		test->ifobj_rx->xsk->pkt_stream = test->rx_pkt_stream_default;
	}
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

static bool pkt_continues(u32 options)
{
	return options & XDP_PKT_CONTD;
}

static u32 ceil_u32(u32 a, u32 b)
{
	return (a + b - 1) / b;
}

static u32 pkt_nb_frags(u32 frame_size, struct pkt_stream *pkt_stream, struct pkt *pkt)
{
	u32 nb_frags = 1, next_frag;

	if (!pkt)
		return 1;

	if (!pkt_stream->verbatim) {
		if (!pkt->valid || !pkt->len)
			return 1;
		return ceil_u32(pkt->len, frame_size);
	}

	/* Search for the end of the packet in verbatim mode */
	if (!pkt_continues(pkt->options))
		return nb_frags;

	next_frag = pkt_stream->current_pkt_nb;
	pkt++;
	while (next_frag++ < pkt_stream->nb_pkts) {
		nb_frags++;
		if (!pkt_continues(pkt->options) || !pkt->valid)
			break;
		pkt++;
	}
	return nb_frags;
}

static bool set_pkt_valid(int offset, u32 len)
{
	return len <= MAX_ETH_JUMBO_SIZE;
}

static void pkt_set(struct pkt_stream *pkt_stream, struct pkt *pkt, int offset, u32 len)
{
	pkt->offset = offset;
	pkt->len = len;
	pkt->valid = set_pkt_valid(offset, len);
}

static void pkt_stream_pkt_set(struct pkt_stream *pkt_stream, struct pkt *pkt, int offset, u32 len)
{
	bool prev_pkt_valid = pkt->valid;

	pkt_set(pkt_stream, pkt, offset, len);
	pkt_stream->nb_valid_entries += pkt->valid - prev_pkt_valid;
}

static u32 pkt_get_buffer_len(struct xsk_umem_info *umem, u32 len)
{
	return ceil_u32(len, umem->frame_size) * umem->frame_size;
}

static struct pkt_stream *__pkt_stream_generate(u32 nb_pkts, u32 pkt_len, u32 nb_start, u32 nb_off)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = __pkt_stream_alloc(nb_pkts);
	if (!pkt_stream)
		exit_with_error(ENOMEM);

	pkt_stream->nb_pkts = nb_pkts;
	pkt_stream->max_pkt_len = pkt_len;
	for (i = 0; i < nb_pkts; i++) {
		struct pkt *pkt = &pkt_stream->pkts[i];

		pkt_stream_pkt_set(pkt_stream, pkt, 0, pkt_len);
		pkt->pkt_nb = nb_start + i * nb_off;
	}

	return pkt_stream;
}

static struct pkt_stream *pkt_stream_generate(u32 nb_pkts, u32 pkt_len)
{
	return __pkt_stream_generate(nb_pkts, pkt_len, 0, 1);
}

static struct pkt_stream *pkt_stream_clone(struct pkt_stream *pkt_stream)
{
	return pkt_stream_generate(pkt_stream->nb_pkts, pkt_stream->pkts[0].len);
}

static void pkt_stream_replace_ifobject(struct ifobject *ifobj, u32 nb_pkts, u32 pkt_len)
{
	ifobj->xsk->pkt_stream = pkt_stream_generate(nb_pkts, pkt_len);
}

static void pkt_stream_replace(struct test_spec *test, u32 nb_pkts, u32 pkt_len)
{
	pkt_stream_replace_ifobject(test->ifobj_tx, nb_pkts, pkt_len);
	pkt_stream_replace_ifobject(test->ifobj_rx, nb_pkts, pkt_len);
}

static void __pkt_stream_replace_half(struct ifobject *ifobj, u32 pkt_len,
				      int offset)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	pkt_stream = pkt_stream_clone(ifobj->xsk->pkt_stream);
	for (i = 1; i < ifobj->xsk->pkt_stream->nb_pkts; i += 2)
		pkt_stream_pkt_set(pkt_stream, &pkt_stream->pkts[i], offset, pkt_len);

	ifobj->xsk->pkt_stream = pkt_stream;
}

static void pkt_stream_replace_half(struct test_spec *test, u32 pkt_len, int offset)
{
	__pkt_stream_replace_half(test->ifobj_tx, pkt_len, offset);
	__pkt_stream_replace_half(test->ifobj_rx, pkt_len, offset);
}

static void pkt_stream_receive_half(struct test_spec *test)
{
	struct pkt_stream *pkt_stream = test->ifobj_tx->xsk->pkt_stream;
	u32 i;

	test->ifobj_rx->xsk->pkt_stream = pkt_stream_generate(pkt_stream->nb_pkts,
							      pkt_stream->pkts[0].len);
	pkt_stream = test->ifobj_rx->xsk->pkt_stream;
	for (i = 1; i < pkt_stream->nb_pkts; i += 2)
		pkt_stream->pkts[i].valid = false;

	pkt_stream->nb_valid_entries /= 2;
}

static void pkt_stream_even_odd_sequence(struct test_spec *test)
{
	struct pkt_stream *pkt_stream;
	u32 i;

	for (i = 0; i < test->nb_sockets; i++) {
		pkt_stream = test->ifobj_tx->xsk_arr[i].pkt_stream;
		pkt_stream = __pkt_stream_generate(pkt_stream->nb_pkts / 2,
						   pkt_stream->pkts[0].len, i, 2);
		test->ifobj_tx->xsk_arr[i].pkt_stream = pkt_stream;

		pkt_stream = test->ifobj_rx->xsk_arr[i].pkt_stream;
		pkt_stream = __pkt_stream_generate(pkt_stream->nb_pkts / 2,
						   pkt_stream->pkts[0].len, i, 2);
		test->ifobj_rx->xsk_arr[i].pkt_stream = pkt_stream;
	}
}

static u64 pkt_get_addr(struct pkt *pkt, struct xsk_umem_info *umem)
{
	if (!pkt->valid)
		return pkt->offset;
	return pkt->offset + umem_alloc_buffer(umem);
}

static void pkt_stream_cancel(struct pkt_stream *pkt_stream)
{
	pkt_stream->current_pkt_nb--;
}

static void pkt_generate(struct xsk_socket_info *xsk, struct xsk_umem_info *umem, u64 addr, u32 len,
			 u32 pkt_nb, u32 bytes_written)
{
	void *data = xsk_umem__get_data(umem->buffer, addr);

	if (len < MIN_PKT_SIZE)
		return;

	if (!bytes_written) {
		gen_eth_hdr(xsk, data);

		len -= PKT_HDR_SIZE;
		data += PKT_HDR_SIZE;
	} else {
		bytes_written -= PKT_HDR_SIZE;
	}

	write_payload(data, pkt_nb, bytes_written, len);
}

static struct pkt_stream *__pkt_stream_generate_custom(struct ifobject *ifobj, struct pkt *frames,
						       u32 nb_frames, bool verbatim)
{
	u32 i, len = 0, pkt_nb = 0, payload = 0;
	struct pkt_stream *pkt_stream;

	pkt_stream = __pkt_stream_alloc(nb_frames);
	if (!pkt_stream)
		exit_with_error(ENOMEM);

	for (i = 0; i < nb_frames; i++) {
		struct pkt *pkt = &pkt_stream->pkts[pkt_nb];
		struct pkt *frame = &frames[i];

		pkt->offset = frame->offset;
		if (verbatim) {
			*pkt = *frame;
			pkt->pkt_nb = payload;
			if (!frame->valid || !pkt_continues(frame->options))
				payload++;
		} else {
			if (frame->valid)
				len += frame->len;
			if (frame->valid && pkt_continues(frame->options))
				continue;

			pkt->pkt_nb = pkt_nb;
			pkt->len = len;
			pkt->valid = frame->valid;
			pkt->options = 0;

			len = 0;
		}

		print_verbose("offset: %d len: %u valid: %u options: %u pkt_nb: %u\n",
			      pkt->offset, pkt->len, pkt->valid, pkt->options, pkt->pkt_nb);

		if (pkt->valid && pkt->len > pkt_stream->max_pkt_len)
			pkt_stream->max_pkt_len = pkt->len;

		if (pkt->valid)
			pkt_stream->nb_valid_entries++;

		pkt_nb++;
	}

	pkt_stream->nb_pkts = pkt_nb;
	pkt_stream->verbatim = verbatim;
	return pkt_stream;
}

static void pkt_stream_generate_custom(struct test_spec *test, struct pkt *pkts, u32 nb_pkts)
{
	struct pkt_stream *pkt_stream;

	pkt_stream = __pkt_stream_generate_custom(test->ifobj_tx, pkts, nb_pkts, true);
	test->ifobj_tx->xsk->pkt_stream = pkt_stream;

	pkt_stream = __pkt_stream_generate_custom(test->ifobj_rx, pkts, nb_pkts, false);
	test->ifobj_rx->xsk->pkt_stream = pkt_stream;
}

static void pkt_print_data(u32 *data, u32 cnt)
{
	u32 i;

	for (i = 0; i < cnt; i++) {
		u32 seqnum, pkt_nb;

		seqnum = ntohl(*data) & 0xffff;
		pkt_nb = ntohl(*data) >> 16;
		ksft_print_msg("%u:%u ", pkt_nb, seqnum);
		data++;
	}
}

static void pkt_dump(void *pkt, u32 len, bool eth_header)
{
	struct ethhdr *ethhdr = pkt;
	u32 i, *data;

	if (eth_header) {
		/*extract L2 frame */
		ksft_print_msg("DEBUG>> L2: dst mac: ");
		for (i = 0; i < ETH_ALEN; i++)
			ksft_print_msg("%02X", ethhdr->h_dest[i]);

		ksft_print_msg("\nDEBUG>> L2: src mac: ");
		for (i = 0; i < ETH_ALEN; i++)
			ksft_print_msg("%02X", ethhdr->h_source[i]);

		data = pkt + PKT_HDR_SIZE;
	} else {
		data = pkt;
	}

	/*extract L5 frame */
	ksft_print_msg("\nDEBUG>> L5: seqnum: ");
	pkt_print_data(data, PKT_DUMP_NB_TO_PRINT);
	ksft_print_msg("....");
	if (len > PKT_DUMP_NB_TO_PRINT * sizeof(u32)) {
		ksft_print_msg("\n.... ");
		pkt_print_data(data + len / sizeof(u32) - PKT_DUMP_NB_TO_PRINT,
			       PKT_DUMP_NB_TO_PRINT);
	}
	ksft_print_msg("\n---------------------------------------\n");
}

static bool is_offset_correct(struct xsk_umem_info *umem, struct pkt *pkt, u64 addr)
{
	u32 headroom = umem->unaligned_mode ? 0 : umem->frame_headroom;
	u32 offset = addr % umem->frame_size, expected_offset;
	int pkt_offset = pkt->valid ? pkt->offset : 0;

	if (!umem->unaligned_mode)
		pkt_offset = 0;

	expected_offset = (pkt_offset + headroom + XDP_PACKET_HEADROOM) % umem->frame_size;

	if (offset == expected_offset)
		return true;

	ksft_print_msg("[%s] expected [%u], got [%u]\n", __func__, expected_offset, offset);
	return false;
}

static bool is_metadata_correct(struct pkt *pkt, void *buffer, u64 addr)
{
	void *data = xsk_umem__get_data(buffer, addr);
	struct xdp_info *meta = data - sizeof(struct xdp_info);

	if (meta->count != pkt->pkt_nb) {
		ksft_print_msg("[%s] expected meta_count [%d], got meta_count [%llu]\n",
			       __func__, pkt->pkt_nb,
			       (unsigned long long)meta->count);
		return false;
	}

	return true;
}

static bool is_adjust_tail_supported(struct xsk_xdp_progs *skel_rx)
{
	struct bpf_map *data_map;
	int adjust_value = 0;
	int key = 0;
	int ret;

	data_map = bpf_object__find_map_by_name(skel_rx->obj, "xsk_xdp_.bss");
	if (!data_map || !bpf_map__is_internal(data_map)) {
		ksft_print_msg("Error: could not find bss section of XDP program\n");
		exit_with_error(errno);
	}

	ret = bpf_map_lookup_elem(bpf_map__fd(data_map), &key, &adjust_value);
	if (ret) {
		ksft_print_msg("Error: bpf_map_lookup_elem failed with error %d\n", ret);
		exit_with_error(errno);
	}

	/* Set the 'adjust_value' variable to -EOPNOTSUPP in the XDP program if the adjust_tail
	 * helper is not supported. Skip the adjust_tail test case in this scenario.
	 */
	return adjust_value != -EOPNOTSUPP;
}

static bool is_frag_valid(struct xsk_umem_info *umem, u64 addr, u32 len, u32 expected_pkt_nb,
			  u32 bytes_processed)
{
	u32 seqnum, pkt_nb, *pkt_data, words_to_end, expected_seqnum;
	void *data = xsk_umem__get_data(umem->buffer, addr);

	addr -= umem->base_addr;

	if (addr >= umem->num_frames * umem->frame_size ||
	    addr + len > umem->num_frames * umem->frame_size) {
		ksft_print_msg("Frag invalid addr: %llx len: %u\n",
			       (unsigned long long)addr, len);
		return false;
	}
	if (!umem->unaligned_mode && addr % umem->frame_size + len > umem->frame_size) {
		ksft_print_msg("Frag crosses frame boundary addr: %llx len: %u\n",
			       (unsigned long long)addr, len);
		return false;
	}

	pkt_data = data;
	if (!bytes_processed) {
		pkt_data += PKT_HDR_SIZE / sizeof(*pkt_data);
		len -= PKT_HDR_SIZE;
	} else {
		bytes_processed -= PKT_HDR_SIZE;
	}

	expected_seqnum = bytes_processed / sizeof(*pkt_data);
	seqnum = ntohl(*pkt_data) & 0xffff;
	pkt_nb = ntohl(*pkt_data) >> 16;

	if (expected_pkt_nb != pkt_nb) {
		ksft_print_msg("[%s] expected pkt_nb [%u], got pkt_nb [%u]\n",
			       __func__, expected_pkt_nb, pkt_nb);
		goto error;
	}
	if (expected_seqnum != seqnum) {
		ksft_print_msg("[%s] expected seqnum at start [%u], got seqnum [%u]\n",
			       __func__, expected_seqnum, seqnum);
		goto error;
	}

	words_to_end = len / sizeof(*pkt_data) - 1;
	pkt_data += words_to_end;
	seqnum = ntohl(*pkt_data) & 0xffff;
	expected_seqnum += words_to_end;
	if (expected_seqnum != seqnum) {
		ksft_print_msg("[%s] expected seqnum at end [%u], got seqnum [%u]\n",
			       __func__, expected_seqnum, seqnum);
		goto error;
	}

	return true;

error:
	pkt_dump(data, len, !bytes_processed);
	return false;
}

static bool is_pkt_valid(struct pkt *pkt, void *buffer, u64 addr, u32 len)
{
	if (pkt->len != len) {
		ksft_print_msg("[%s] expected packet length [%d], got length [%d]\n",
			       __func__, pkt->len, len);
		pkt_dump(xsk_umem__get_data(buffer, addr), len, true);
		return false;
	}

	return true;
}

static u32 load_value(u32 *counter)
{
	return __atomic_load_n(counter, __ATOMIC_ACQUIRE);
}

static bool kick_tx_with_check(struct xsk_socket_info *xsk, int *ret)
{
	u32 max_budget = MAX_TX_BUDGET_DEFAULT;
	u32 cons, ready_to_send;
	int delta;

	cons = load_value(xsk->tx.consumer);
	ready_to_send = load_value(xsk->tx.producer) - cons;
	*ret = sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

	delta = load_value(xsk->tx.consumer) - cons;
	/* By default, xsk should consume exact @max_budget descs at one
	 * send in this case where hitting the max budget limit in while
	 * loop is triggered in __xsk_generic_xmit(). Please make sure that
	 * the number of descs to be sent is larger than @max_budget, or
	 * else the tx.consumer will be updated in xskq_cons_peek_desc()
	 * in time which hides the issue we try to verify.
	 */
	if (ready_to_send > max_budget && delta != max_budget)
		return false;

	return true;
}

static int kick_tx(struct xsk_socket_info *xsk)
{
	int ret;

	if (xsk->check_consumer) {
		if (!kick_tx_with_check(xsk, &ret))
			return TEST_FAILURE;
	} else {
		ret = sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
	}
	if (ret >= 0)
		return TEST_PASS;
	if (errno == ENOBUFS || errno == EAGAIN || errno == EBUSY || errno == ENETDOWN) {
		usleep(100);
		return TEST_PASS;
	}
	return TEST_FAILURE;
}

static int kick_rx(struct xsk_socket_info *xsk)
{
	int ret;

	ret = recvfrom(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, NULL);
	if (ret < 0)
		return TEST_FAILURE;

	return TEST_PASS;
}

static int complete_pkts(struct xsk_socket_info *xsk, int batch_size)
{
	unsigned int rcvd;
	u32 idx;
	int ret;

	if (xsk_ring_prod__needs_wakeup(&xsk->tx)) {
		ret = kick_tx(xsk);
		if (ret)
			return TEST_FAILURE;
	}

	rcvd = xsk_ring_cons__peek(&xsk->umem->cq, batch_size, &idx);
	if (rcvd) {
		if (rcvd > xsk->outstanding_tx) {
			u64 addr = *xsk_ring_cons__comp_addr(&xsk->umem->cq, idx + rcvd - 1);

			ksft_print_msg("[%s] Too many packets completed\n", __func__);
			ksft_print_msg("Last completion address: %llx\n",
				       (unsigned long long)addr);
			return TEST_FAILURE;
		}

		xsk_ring_cons__release(&xsk->umem->cq, rcvd);
		xsk->outstanding_tx -= rcvd;
	}

	return TEST_PASS;
}

static int __receive_pkts(struct test_spec *test, struct xsk_socket_info *xsk)
{
	u32 frags_processed = 0, nb_frags = 0, pkt_len = 0;
	u32 idx_rx = 0, idx_fq = 0, rcvd, pkts_sent = 0;
	struct pkt_stream *pkt_stream = xsk->pkt_stream;
	struct ifobject *ifobj = test->ifobj_rx;
	struct xsk_umem_info *umem = xsk->umem;
	struct pollfd fds = { };
	struct pkt *pkt;
	u64 first_addr = 0;
	int ret;

	fds.fd = xsk_socket__fd(xsk->xsk);
	fds.events = POLLIN;

	ret = kick_rx(xsk);
	if (ret)
		return TEST_FAILURE;

	if (ifobj->use_poll) {
		ret = poll(&fds, 1, POLL_TMOUT);
		if (ret < 0)
			return TEST_FAILURE;

		if (!ret) {
			if (!is_umem_valid(test->ifobj_tx))
				return TEST_PASS;

			ksft_print_msg("ERROR: [%s] Poll timed out\n", __func__);
			return TEST_CONTINUE;
		}

		if (!(fds.revents & POLLIN))
			return TEST_CONTINUE;
	}

	rcvd = xsk_ring_cons__peek(&xsk->rx, xsk->batch_size, &idx_rx);
	if (!rcvd)
		return TEST_CONTINUE;

	if (ifobj->use_fill_ring) {
		ret = xsk_ring_prod__reserve(&umem->fq, rcvd, &idx_fq);
		while (ret != rcvd) {
			if (xsk_ring_prod__needs_wakeup(&umem->fq)) {
				ret = poll(&fds, 1, POLL_TMOUT);
				if (ret < 0)
					return TEST_FAILURE;
			}
			ret = xsk_ring_prod__reserve(&umem->fq, rcvd, &idx_fq);
		}
	}

	while (frags_processed < rcvd) {
		const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++);
		u64 addr = desc->addr, orig;

		orig = xsk_umem__extract_addr(addr);
		addr = xsk_umem__add_offset_to_addr(addr);

		if (!nb_frags) {
			pkt = pkt_stream_get_next_rx_pkt(pkt_stream, &pkts_sent);
			if (!pkt) {
				ksft_print_msg("[%s] received too many packets addr: %lx len %u\n",
					       __func__, addr, desc->len);
				return TEST_FAILURE;
			}
		}

		print_verbose("Rx: addr: %lx len: %u options: %u pkt_nb: %u valid: %u\n",
			      addr, desc->len, desc->options, pkt->pkt_nb, pkt->valid);

		if (!is_frag_valid(umem, addr, desc->len, pkt->pkt_nb, pkt_len) ||
		    !is_offset_correct(umem, pkt, addr) || (ifobj->use_metadata &&
		    !is_metadata_correct(pkt, umem->buffer, addr)))
			return TEST_FAILURE;

		if (!nb_frags++)
			first_addr = addr;
		frags_processed++;
		pkt_len += desc->len;
		if (ifobj->use_fill_ring)
			*xsk_ring_prod__fill_addr(&umem->fq, idx_fq++) = orig;

		if (pkt_continues(desc->options))
			continue;

		/* The complete packet has been received */
		if (!is_pkt_valid(pkt, umem->buffer, first_addr, pkt_len) ||
		    !is_offset_correct(umem, pkt, addr))
			return TEST_FAILURE;

		pkt_stream->nb_rx_pkts++;
		nb_frags = 0;
		pkt_len = 0;
	}

	if (nb_frags) {
		/* In the middle of a packet. Start over from beginning of packet. */
		idx_rx -= nb_frags;
		xsk_ring_cons__cancel(&xsk->rx, nb_frags);
		if (ifobj->use_fill_ring) {
			idx_fq -= nb_frags;
			xsk_ring_prod__cancel(&umem->fq, nb_frags);
		}
		frags_processed -= nb_frags;
	}

	if (ifobj->use_fill_ring)
		xsk_ring_prod__submit(&umem->fq, frags_processed);
	if (ifobj->release_rx)
		xsk_ring_cons__release(&xsk->rx, frags_processed);

	pthread_mutex_lock(&pacing_mutex);
	pkts_in_flight -= pkts_sent;
	pthread_mutex_unlock(&pacing_mutex);
	pkts_sent = 0;

return TEST_CONTINUE;
}

bool all_packets_received(struct test_spec *test, struct xsk_socket_info *xsk, u32 sock_num,
			  unsigned long *bitmap)
{
	struct pkt_stream *pkt_stream = xsk->pkt_stream;

	if (!pkt_stream) {
		__set_bit(sock_num, bitmap);
		return false;
	}

	if (pkt_stream->nb_rx_pkts == pkt_stream->nb_valid_entries) {
		__set_bit(sock_num, bitmap);
		if (bitmap_full(bitmap, test->nb_sockets))
			return true;
	}

	return false;
}

static int receive_pkts(struct test_spec *test)
{
	struct timeval tv_end, tv_now, tv_timeout = {THREAD_TMOUT, 0};
	DECLARE_BITMAP(bitmap, test->nb_sockets);
	struct xsk_socket_info *xsk;
	u32 sock_num = 0;
	int res, ret;

	ret = gettimeofday(&tv_now, NULL);
	if (ret)
		exit_with_error(errno);

	timeradd(&tv_now, &tv_timeout, &tv_end);

	while (1) {
		xsk = &test->ifobj_rx->xsk_arr[sock_num];

		if ((all_packets_received(test, xsk, sock_num, bitmap)))
			break;

		res = __receive_pkts(test, xsk);
		if (!(res == TEST_PASS || res == TEST_CONTINUE))
			return res;

		ret = gettimeofday(&tv_now, NULL);
		if (ret)
			exit_with_error(errno);

		if (timercmp(&tv_now, &tv_end, >)) {
			ksft_print_msg("ERROR: [%s] Receive loop timed out\n", __func__);
			return TEST_FAILURE;
		}
		sock_num = (sock_num + 1) % test->nb_sockets;
	}

	return TEST_PASS;
}

static int __send_pkts(struct ifobject *ifobject, struct xsk_socket_info *xsk, bool timeout)
{
	u32 i, idx = 0, valid_pkts = 0, valid_frags = 0, buffer_len;
	struct pkt_stream *pkt_stream = xsk->pkt_stream;
	struct xsk_umem_info *umem = ifobject->umem;
	bool use_poll = ifobject->use_poll;
	struct pollfd fds = { };
	int ret;

	buffer_len = pkt_get_buffer_len(umem, pkt_stream->max_pkt_len);
	/* pkts_in_flight might be negative if many invalid packets are sent */
	if (pkts_in_flight >= (int)((umem_size(umem) - xsk->batch_size * buffer_len) /
	    buffer_len)) {
		ret = kick_tx(xsk);
		if (ret)
			return TEST_FAILURE;
		return TEST_CONTINUE;
	}

	fds.fd = xsk_socket__fd(xsk->xsk);
	fds.events = POLLOUT;

	while (xsk_ring_prod__reserve(&xsk->tx, xsk->batch_size, &idx) < xsk->batch_size) {
		if (use_poll) {
			ret = poll(&fds, 1, POLL_TMOUT);
			if (timeout) {
				if (ret < 0) {
					ksft_print_msg("ERROR: [%s] Poll error %d\n",
						       __func__, errno);
					return TEST_FAILURE;
				}
				if (ret == 0)
					return TEST_PASS;
				break;
			}
			if (ret <= 0) {
				ksft_print_msg("ERROR: [%s] Poll error %d\n",
					       __func__, errno);
				return TEST_FAILURE;
			}
		}

		complete_pkts(xsk, xsk->batch_size);
	}

	for (i = 0; i < xsk->batch_size; i++) {
		struct pkt *pkt = pkt_stream_get_next_tx_pkt(pkt_stream);
		u32 nb_frags_left, nb_frags, bytes_written = 0;

		if (!pkt)
			break;

		nb_frags = pkt_nb_frags(umem->frame_size, pkt_stream, pkt);
		if (nb_frags > xsk->batch_size - i) {
			pkt_stream_cancel(pkt_stream);
			xsk_ring_prod__cancel(&xsk->tx, xsk->batch_size - i);
			break;
		}
		nb_frags_left = nb_frags;

		while (nb_frags_left--) {
			struct xdp_desc *tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, idx + i);

			tx_desc->addr = pkt_get_addr(pkt, ifobject->umem);
			if (pkt_stream->verbatim) {
				tx_desc->len = pkt->len;
				tx_desc->options = pkt->options;
			} else if (nb_frags_left) {
				tx_desc->len = umem->frame_size;
				tx_desc->options = XDP_PKT_CONTD;
			} else {
				tx_desc->len = pkt->len - bytes_written;
				tx_desc->options = 0;
			}
			if (pkt->valid)
				pkt_generate(xsk, umem, tx_desc->addr, tx_desc->len, pkt->pkt_nb,
					     bytes_written);
			bytes_written += tx_desc->len;

			print_verbose("Tx addr: %llx len: %u options: %u pkt_nb: %u\n",
				      tx_desc->addr, tx_desc->len, tx_desc->options, pkt->pkt_nb);

			if (nb_frags_left) {
				i++;
				if (pkt_stream->verbatim)
					pkt = pkt_stream_get_next_tx_pkt(pkt_stream);
			}
		}

		if (pkt && pkt->valid) {
			valid_pkts++;
			valid_frags += nb_frags;
		}
	}

	pthread_mutex_lock(&pacing_mutex);
	pkts_in_flight += valid_pkts;
	pthread_mutex_unlock(&pacing_mutex);

	xsk_ring_prod__submit(&xsk->tx, i);
	xsk->outstanding_tx += valid_frags;

	if (use_poll) {
		ret = poll(&fds, 1, POLL_TMOUT);
		if (ret <= 0) {
			if (ret == 0 && timeout)
				return TEST_PASS;

			ksft_print_msg("ERROR: [%s] Poll error %d\n", __func__, ret);
			return TEST_FAILURE;
		}
	}

	if (!timeout) {
		if (complete_pkts(xsk, i))
			return TEST_FAILURE;

		usleep(10);
		return TEST_PASS;
	}

	return TEST_CONTINUE;
}

static int wait_for_tx_completion(struct xsk_socket_info *xsk)
{
	struct timeval tv_end, tv_now, tv_timeout = {THREAD_TMOUT, 0};
	int ret;

	ret = gettimeofday(&tv_now, NULL);
	if (ret)
		exit_with_error(errno);
	timeradd(&tv_now, &tv_timeout, &tv_end);

	while (xsk->outstanding_tx) {
		ret = gettimeofday(&tv_now, NULL);
		if (ret)
			exit_with_error(errno);
		if (timercmp(&tv_now, &tv_end, >)) {
			ksft_print_msg("ERROR: [%s] Transmission loop timed out\n", __func__);
			return TEST_FAILURE;
		}

		complete_pkts(xsk, xsk->batch_size);
	}

	return TEST_PASS;
}

bool all_packets_sent(struct test_spec *test, unsigned long *bitmap)
{
	return bitmap_full(bitmap, test->nb_sockets);
}

static int send_pkts(struct test_spec *test, struct ifobject *ifobject)
{
	bool timeout = !is_umem_valid(test->ifobj_rx);
	DECLARE_BITMAP(bitmap, test->nb_sockets);
	u32 i, ret;

	while (!(all_packets_sent(test, bitmap))) {
		for (i = 0; i < test->nb_sockets; i++) {
			struct pkt_stream *pkt_stream;

			pkt_stream = ifobject->xsk_arr[i].pkt_stream;
			if (!pkt_stream || pkt_stream->current_pkt_nb >= pkt_stream->nb_pkts) {
				__set_bit(i, bitmap);
				continue;
			}
			ret = __send_pkts(ifobject, &ifobject->xsk_arr[i], timeout);
			if (ret == TEST_CONTINUE && !test->fail)
				continue;

			if ((ret || test->fail) && !timeout)
				return TEST_FAILURE;

			if (ret == TEST_PASS && timeout)
				return ret;

			ret = wait_for_tx_completion(&ifobject->xsk_arr[i]);
			if (ret)
				return TEST_FAILURE;
		}
	}

	return TEST_PASS;
}

static int get_xsk_stats(struct xsk_socket *xsk, struct xdp_statistics *stats)
{
	int fd = xsk_socket__fd(xsk), err;
	socklen_t optlen, expected_len;

	optlen = sizeof(*stats);
	err = getsockopt(fd, SOL_XDP, XDP_STATISTICS, stats, &optlen);
	if (err) {
		ksft_print_msg("[%s] getsockopt(XDP_STATISTICS) error %u %s\n",
			       __func__, -err, strerror(-err));
		return TEST_FAILURE;
	}

	expected_len = sizeof(struct xdp_statistics);
	if (optlen != expected_len) {
		ksft_print_msg("[%s] getsockopt optlen error. Expected: %u got: %u\n",
			       __func__, expected_len, optlen);
		return TEST_FAILURE;
	}

	return TEST_PASS;
}

static int validate_rx_dropped(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	struct xdp_statistics stats;
	int err;

	err = kick_rx(ifobject->xsk);
	if (err)
		return TEST_FAILURE;

	err = get_xsk_stats(xsk, &stats);
	if (err)
		return TEST_FAILURE;

	/* The receiver calls getsockopt after receiving the last (valid)
	 * packet which is not the final packet sent in this test (valid and
	 * invalid packets are sent in alternating fashion with the final
	 * packet being invalid). Since the last packet may or may not have
	 * been dropped already, both outcomes must be allowed.
	 */
	if (stats.rx_dropped == ifobject->xsk->pkt_stream->nb_pkts / 2 ||
	    stats.rx_dropped == ifobject->xsk->pkt_stream->nb_pkts / 2 - 1)
		return TEST_PASS;

	return TEST_FAILURE;
}

static int validate_rx_full(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	struct xdp_statistics stats;
	int err;

	usleep(1000);
	err = kick_rx(ifobject->xsk);
	if (err)
		return TEST_FAILURE;

	err = get_xsk_stats(xsk, &stats);
	if (err)
		return TEST_FAILURE;

	if (stats.rx_ring_full)
		return TEST_PASS;

	return TEST_FAILURE;
}

static int validate_fill_empty(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	struct xdp_statistics stats;
	int err;

	usleep(1000);
	err = kick_rx(ifobject->xsk);
	if (err)
		return TEST_FAILURE;

	err = get_xsk_stats(xsk, &stats);
	if (err)
		return TEST_FAILURE;

	if (stats.rx_fill_ring_empty_descs)
		return TEST_PASS;

	return TEST_FAILURE;
}

static int validate_tx_invalid_descs(struct ifobject *ifobject)
{
	struct xsk_socket *xsk = ifobject->xsk->xsk;
	int fd = xsk_socket__fd(xsk);
	struct xdp_statistics stats;
	socklen_t optlen;
	int err;

	optlen = sizeof(stats);
	err = getsockopt(fd, SOL_XDP, XDP_STATISTICS, &stats, &optlen);
	if (err) {
		ksft_print_msg("[%s] getsockopt(XDP_STATISTICS) error %u %s\n",
			       __func__, -err, strerror(-err));
		return TEST_FAILURE;
	}

	if (stats.tx_invalid_descs != ifobject->xsk->pkt_stream->nb_pkts / 2) {
		ksft_print_msg("[%s] tx_invalid_descs incorrect. Got [%llu] expected [%u]\n",
			       __func__,
			       (unsigned long long)stats.tx_invalid_descs,
			       ifobject->xsk->pkt_stream->nb_pkts);
		return TEST_FAILURE;
	}

	return TEST_PASS;
}

static void xsk_configure_socket(struct test_spec *test, struct ifobject *ifobject,
				 struct xsk_umem_info *umem, bool tx)
{
	int i, ret;

	for (i = 0; i < test->nb_sockets; i++) {
		bool shared = (ifobject->shared_umem && tx) ? true : !!i;
		u32 ctr = 0;

		while (ctr++ < SOCK_RECONF_CTR) {
			ret = __xsk_configure_socket(&ifobject->xsk_arr[i], umem,
						     ifobject, shared);
			if (!ret)
				break;

			/* Retry if it fails as xsk_socket__create() is asynchronous */
			if (ctr >= SOCK_RECONF_CTR)
				exit_with_error(-ret);
			usleep(USLEEP_MAX);
		}
		if (ifobject->busy_poll)
			enable_busy_poll(&ifobject->xsk_arr[i]);
	}
}

static void thread_common_ops_tx(struct test_spec *test, struct ifobject *ifobject)
{
	xsk_configure_socket(test, ifobject, test->ifobj_rx->umem, true);
	ifobject->xsk = &ifobject->xsk_arr[0];
	ifobject->xskmap = test->ifobj_rx->xskmap;
	memcpy(ifobject->umem, test->ifobj_rx->umem, sizeof(struct xsk_umem_info));
	ifobject->umem->base_addr = 0;
}

static void xsk_populate_fill_ring(struct xsk_umem_info *umem, struct pkt_stream *pkt_stream,
				   bool fill_up)
{
	u32 rx_frame_size = umem->frame_size - XDP_PACKET_HEADROOM;
	u32 idx = 0, filled = 0, buffers_to_fill, nb_pkts;
	int ret;

	if (umem->num_frames < XSK_RING_PROD__DEFAULT_NUM_DESCS)
		buffers_to_fill = umem->num_frames;
	else
		buffers_to_fill = umem->fill_size;

	ret = xsk_ring_prod__reserve(&umem->fq, buffers_to_fill, &idx);
	if (ret != buffers_to_fill)
		exit_with_error(ENOSPC);

	while (filled < buffers_to_fill) {
		struct pkt *pkt = pkt_stream_get_next_rx_pkt(pkt_stream, &nb_pkts);
		u64 addr;
		u32 i;

		for (i = 0; i < pkt_nb_frags(rx_frame_size, pkt_stream, pkt); i++) {
			if (!pkt) {
				if (!fill_up)
					break;
				addr = filled * umem->frame_size + umem->base_addr;
			} else if (pkt->offset >= 0) {
				addr = pkt->offset % umem->frame_size + umem_alloc_buffer(umem);
			} else {
				addr = pkt->offset + umem_alloc_buffer(umem);
			}

			*xsk_ring_prod__fill_addr(&umem->fq, idx++) = addr;
			if (++filled >= buffers_to_fill)
				break;
		}
	}
	xsk_ring_prod__submit(&umem->fq, filled);
	xsk_ring_prod__cancel(&umem->fq, buffers_to_fill - filled);

	pkt_stream_reset(pkt_stream);
	umem_reset_alloc(umem);
}

static void thread_common_ops(struct test_spec *test, struct ifobject *ifobject)
{
	u64 umem_sz = ifobject->umem->num_frames * ifobject->umem->frame_size;
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
	LIBBPF_OPTS(bpf_xdp_query_opts, opts);
	void *bufs;
	int ret;
	u32 i;

	if (ifobject->umem->unaligned_mode)
		mmap_flags |= MAP_HUGETLB | MAP_HUGE_2MB;

	if (ifobject->shared_umem)
		umem_sz *= 2;

	bufs = mmap(NULL, umem_sz, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
	if (bufs == MAP_FAILED)
		exit_with_error(errno);

	ret = xsk_configure_umem(ifobject, ifobject->umem, bufs, umem_sz);
	if (ret)
		exit_with_error(-ret);

	xsk_configure_socket(test, ifobject, ifobject->umem, false);

	ifobject->xsk = &ifobject->xsk_arr[0];

	if (!ifobject->rx_on)
		return;

	xsk_populate_fill_ring(ifobject->umem, ifobject->xsk->pkt_stream, ifobject->use_fill_ring);

	for (i = 0; i < test->nb_sockets; i++) {
		ifobject->xsk = &ifobject->xsk_arr[i];
		ret = xsk_update_xskmap(ifobject->xskmap, ifobject->xsk->xsk, i);
		if (ret)
			exit_with_error(errno);
	}
}

static void *worker_testapp_validate_tx(void *arg)
{
	struct test_spec *test = (struct test_spec *)arg;
	struct ifobject *ifobject = test->ifobj_tx;
	int err;

	if (test->current_step == 1) {
		if (!ifobject->shared_umem)
			thread_common_ops(test, ifobject);
		else
			thread_common_ops_tx(test, ifobject);
	}

	err = send_pkts(test, ifobject);

	if (!err && ifobject->validation_func)
		err = ifobject->validation_func(ifobject);
	if (err)
		report_failure(test);

	pthread_exit(NULL);
}

static void *worker_testapp_validate_rx(void *arg)
{
	struct test_spec *test = (struct test_spec *)arg;
	struct ifobject *ifobject = test->ifobj_rx;
	int err;

	if (test->current_step == 1) {
		thread_common_ops(test, ifobject);
	} else {
		xsk_clear_xskmap(ifobject->xskmap);
		err = xsk_update_xskmap(ifobject->xskmap, ifobject->xsk->xsk, 0);
		if (err) {
			ksft_print_msg("Error: Failed to update xskmap, error %s\n",
				       strerror(-err));
			exit_with_error(-err);
		}
	}

	pthread_barrier_wait(&barr);

	err = receive_pkts(test);

	if (!err && ifobject->validation_func)
		err = ifobject->validation_func(ifobject);

	if (err) {
		if (test->adjust_tail && !is_adjust_tail_supported(ifobject->xdp_progs))
			test->adjust_tail_support = false;
		else
			report_failure(test);
	}

	pthread_exit(NULL);
}

static u64 ceil_u64(u64 a, u64 b)
{
	return (a + b - 1) / b;
}

static void testapp_clean_xsk_umem(struct ifobject *ifobj)
{
	u64 umem_sz = ifobj->umem->num_frames * ifobj->umem->frame_size;

	if (ifobj->shared_umem)
		umem_sz *= 2;

	umem_sz = ceil_u64(umem_sz, HUGEPAGE_SIZE) * HUGEPAGE_SIZE;
	xsk_umem__delete(ifobj->umem->umem);
	munmap(ifobj->umem->buffer, umem_sz);
}

static void handler(int signum)
{
	pthread_exit(NULL);
}

static bool xdp_prog_changed_rx(struct test_spec *test)
{
	struct ifobject *ifobj = test->ifobj_rx;

	return ifobj->xdp_prog != test->xdp_prog_rx || ifobj->mode != test->mode;
}

static bool xdp_prog_changed_tx(struct test_spec *test)
{
	struct ifobject *ifobj = test->ifobj_tx;

	return ifobj->xdp_prog != test->xdp_prog_tx || ifobj->mode != test->mode;
}

static void xsk_reattach_xdp(struct ifobject *ifobj, struct bpf_program *xdp_prog,
			     struct bpf_map *xskmap, enum test_mode mode)
{
	int err;

	xsk_detach_xdp_program(ifobj->ifindex, mode_to_xdp_flags(ifobj->mode));
	err = xsk_attach_xdp_program(xdp_prog, ifobj->ifindex, mode_to_xdp_flags(mode));
	if (err) {
		ksft_print_msg("Error attaching XDP program\n");
		exit_with_error(-err);
	}

	if (ifobj->mode != mode && (mode == TEST_MODE_DRV || mode == TEST_MODE_ZC))
		if (!xsk_is_in_mode(ifobj->ifindex, XDP_FLAGS_DRV_MODE)) {
			ksft_print_msg("ERROR: XDP prog not in DRV mode\n");
			exit_with_error(EINVAL);
		}

	ifobj->xdp_prog = xdp_prog;
	ifobj->xskmap = xskmap;
	ifobj->mode = mode;
}

static void xsk_attach_xdp_progs(struct test_spec *test, struct ifobject *ifobj_rx,
				 struct ifobject *ifobj_tx)
{
	if (xdp_prog_changed_rx(test))
		xsk_reattach_xdp(ifobj_rx, test->xdp_prog_rx, test->xskmap_rx, test->mode);

	if (!ifobj_tx || ifobj_tx->shared_umem)
		return;

	if (xdp_prog_changed_tx(test))
		xsk_reattach_xdp(ifobj_tx, test->xdp_prog_tx, test->xskmap_tx, test->mode);
}

static int __testapp_validate_traffic(struct test_spec *test, struct ifobject *ifobj1,
				      struct ifobject *ifobj2)
{
	pthread_t t0, t1;
	int err;

	if (test->mtu > MAX_ETH_PKT_SIZE) {
		if (test->mode == TEST_MODE_ZC && (!ifobj1->multi_buff_zc_supp ||
						   (ifobj2 && !ifobj2->multi_buff_zc_supp))) {
			ksft_test_result_skip("Multi buffer for zero-copy not supported.\n");
			return TEST_SKIP;
		}
		if (test->mode != TEST_MODE_ZC && (!ifobj1->multi_buff_supp ||
						   (ifobj2 && !ifobj2->multi_buff_supp))) {
			ksft_test_result_skip("Multi buffer not supported.\n");
			return TEST_SKIP;
		}
	}
	err = test_spec_set_mtu(test, test->mtu);
	if (err) {
		ksft_print_msg("Error, could not set mtu.\n");
		exit_with_error(err);
	}

	if (ifobj2) {
		if (pthread_barrier_init(&barr, NULL, 2))
			exit_with_error(errno);
		pkt_stream_reset(ifobj2->xsk->pkt_stream);
	}

	test->current_step++;
	pkt_stream_reset(ifobj1->xsk->pkt_stream);
	pkts_in_flight = 0;

	signal(SIGUSR1, handler);
	/*Spawn RX thread */
	pthread_create(&t0, NULL, ifobj1->func_ptr, test);

	if (ifobj2) {
		pthread_barrier_wait(&barr);
		if (pthread_barrier_destroy(&barr))
			exit_with_error(errno);

		/*Spawn TX thread */
		pthread_create(&t1, NULL, ifobj2->func_ptr, test);

		pthread_join(t1, NULL);
	}

	if (!ifobj2)
		pthread_kill(t0, SIGUSR1);
	else
		pthread_join(t0, NULL);

	if (test->total_steps == test->current_step || test->fail) {
		u32 i;

		if (ifobj2)
			for (i = 0; i < test->nb_sockets; i++)
				xsk_socket__delete(ifobj2->xsk_arr[i].xsk);

		for (i = 0; i < test->nb_sockets; i++)
			xsk_socket__delete(ifobj1->xsk_arr[i].xsk);

		testapp_clean_xsk_umem(ifobj1);
		if (ifobj2 && !ifobj2->shared_umem)
			testapp_clean_xsk_umem(ifobj2);
	}

	return !!test->fail;
}

static int testapp_validate_traffic(struct test_spec *test)
{
	struct ifobject *ifobj_rx = test->ifobj_rx;
	struct ifobject *ifobj_tx = test->ifobj_tx;

	if ((ifobj_rx->umem->unaligned_mode && !ifobj_rx->unaligned_supp) ||
	    (ifobj_tx->umem->unaligned_mode && !ifobj_tx->unaligned_supp)) {
		ksft_test_result_skip("No huge pages present.\n");
		return TEST_SKIP;
	}

	if (test->set_ring) {
		if (ifobj_tx->hw_ring_size_supp) {
			if (set_ring_size(ifobj_tx)) {
				ksft_test_result_skip("Failed to change HW ring size.\n");
				return TEST_FAILURE;
			}
		} else {
			ksft_test_result_skip("Changing HW ring size not supported.\n");
			return TEST_SKIP;
		}
	}

	xsk_attach_xdp_progs(test, ifobj_rx, ifobj_tx);
	return __testapp_validate_traffic(test, ifobj_rx, ifobj_tx);
}

static int testapp_validate_traffic_single_thread(struct test_spec *test, struct ifobject *ifobj)
{
	return __testapp_validate_traffic(test, ifobj, NULL);
}

static int testapp_teardown(struct test_spec *test)
{
	int i;

	for (i = 0; i < MAX_TEARDOWN_ITER; i++) {
		if (testapp_validate_traffic(test))
			return TEST_FAILURE;
		test_spec_reset(test);
	}

	return TEST_PASS;
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

static int testapp_bidirectional(struct test_spec *test)
{
	int res;

	test->ifobj_tx->rx_on = true;
	test->ifobj_rx->tx_on = true;
	test->total_steps = 2;
	if (testapp_validate_traffic(test))
		return TEST_FAILURE;

	print_verbose("Switching Tx/Rx direction\n");
	swap_directions(&test->ifobj_rx, &test->ifobj_tx);
	res = __testapp_validate_traffic(test, test->ifobj_rx, test->ifobj_tx);

	swap_directions(&test->ifobj_rx, &test->ifobj_tx);
	return res;
}

static int swap_xsk_resources(struct test_spec *test)
{
	int ret;

	test->ifobj_tx->xsk_arr[0].pkt_stream = NULL;
	test->ifobj_rx->xsk_arr[0].pkt_stream = NULL;
	test->ifobj_tx->xsk_arr[1].pkt_stream = test->tx_pkt_stream_default;
	test->ifobj_rx->xsk_arr[1].pkt_stream = test->rx_pkt_stream_default;
	test->ifobj_tx->xsk = &test->ifobj_tx->xsk_arr[1];
	test->ifobj_rx->xsk = &test->ifobj_rx->xsk_arr[1];

	ret = xsk_update_xskmap(test->ifobj_rx->xskmap, test->ifobj_rx->xsk->xsk, 0);
	if (ret)
		return TEST_FAILURE;

	return TEST_PASS;
}

static int testapp_xdp_prog_cleanup(struct test_spec *test)
{
	test->total_steps = 2;
	test->nb_sockets = 2;
	if (testapp_validate_traffic(test))
		return TEST_FAILURE;

	if (swap_xsk_resources(test))
		return TEST_FAILURE;
	return testapp_validate_traffic(test);
}

static int testapp_headroom(struct test_spec *test)
{
	test->ifobj_rx->umem->frame_headroom = UMEM_HEADROOM_TEST_SIZE;
	return testapp_validate_traffic(test);
}

static int testapp_stats_rx_dropped(struct test_spec *test)
{
	if (test->mode == TEST_MODE_ZC) {
		ksft_test_result_skip("Can not run RX_DROPPED test for ZC mode\n");
		return TEST_SKIP;
	}

	pkt_stream_replace_half(test, MIN_PKT_SIZE * 4, 0);
	test->ifobj_rx->umem->frame_headroom = test->ifobj_rx->umem->frame_size -
		XDP_PACKET_HEADROOM - MIN_PKT_SIZE * 3;
	pkt_stream_receive_half(test);
	test->ifobj_rx->validation_func = validate_rx_dropped;
	return testapp_validate_traffic(test);
}

static int testapp_stats_tx_invalid_descs(struct test_spec *test)
{
	pkt_stream_replace_half(test, XSK_UMEM__INVALID_FRAME_SIZE, 0);
	test->ifobj_tx->validation_func = validate_tx_invalid_descs;
	return testapp_validate_traffic(test);
}

static int testapp_stats_rx_full(struct test_spec *test)
{
	pkt_stream_replace(test, DEFAULT_UMEM_BUFFERS + DEFAULT_UMEM_BUFFERS / 2, MIN_PKT_SIZE);
	test->ifobj_rx->xsk->pkt_stream = pkt_stream_generate(DEFAULT_UMEM_BUFFERS, MIN_PKT_SIZE);

	test->ifobj_rx->xsk->rxqsize = DEFAULT_UMEM_BUFFERS;
	test->ifobj_rx->release_rx = false;
	test->ifobj_rx->validation_func = validate_rx_full;
	return testapp_validate_traffic(test);
}

static int testapp_stats_fill_empty(struct test_spec *test)
{
	pkt_stream_replace(test, DEFAULT_UMEM_BUFFERS + DEFAULT_UMEM_BUFFERS / 2, MIN_PKT_SIZE);
	test->ifobj_rx->xsk->pkt_stream = pkt_stream_generate(DEFAULT_UMEM_BUFFERS, MIN_PKT_SIZE);

	test->ifobj_rx->use_fill_ring = false;
	test->ifobj_rx->validation_func = validate_fill_empty;
	return testapp_validate_traffic(test);
}

static int testapp_send_receive_unaligned(struct test_spec *test)
{
	test->ifobj_tx->umem->unaligned_mode = true;
	test->ifobj_rx->umem->unaligned_mode = true;
	/* Let half of the packets straddle a 4K buffer boundary */
	pkt_stream_replace_half(test, MIN_PKT_SIZE, -MIN_PKT_SIZE / 2);

	return testapp_validate_traffic(test);
}

static int testapp_send_receive_unaligned_mb(struct test_spec *test)
{
	test->mtu = MAX_ETH_JUMBO_SIZE;
	test->ifobj_tx->umem->unaligned_mode = true;
	test->ifobj_rx->umem->unaligned_mode = true;
	pkt_stream_replace(test, DEFAULT_PKT_CNT, MAX_ETH_JUMBO_SIZE);
	return testapp_validate_traffic(test);
}

static int testapp_single_pkt(struct test_spec *test)
{
	struct pkt pkts[] = {{0, MIN_PKT_SIZE, 0, true}};

	pkt_stream_generate_custom(test, pkts, ARRAY_SIZE(pkts));
	return testapp_validate_traffic(test);
}

static int testapp_send_receive_mb(struct test_spec *test)
{
	test->mtu = MAX_ETH_JUMBO_SIZE;
	pkt_stream_replace(test, DEFAULT_PKT_CNT, MAX_ETH_JUMBO_SIZE);

	return testapp_validate_traffic(test);
}

static int testapp_invalid_desc_mb(struct test_spec *test)
{
	struct xsk_umem_info *umem = test->ifobj_tx->umem;
	u64 umem_size = umem->num_frames * umem->frame_size;
	struct pkt pkts[] = {
		/* Valid packet for synch to start with */
		{0, MIN_PKT_SIZE, 0, true, 0},
		/* Zero frame len is not legal */
		{0, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		{0, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		{0, 0, 0, false, 0},
		/* Invalid address in the second frame */
		{0, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		{umem_size, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		/* Invalid len in the middle */
		{0, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		{0, XSK_UMEM__INVALID_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		/* Invalid options in the middle */
		{0, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		{0, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XSK_DESC__INVALID_OPTION},
		/* Transmit 2 frags, receive 3 */
		{0, XSK_UMEM__MAX_FRAME_SIZE, 0, true, XDP_PKT_CONTD},
		{0, XSK_UMEM__MAX_FRAME_SIZE, 0, true, 0},
		/* Middle frame crosses chunk boundary with small length */
		{0, XSK_UMEM__LARGE_FRAME_SIZE, 0, false, XDP_PKT_CONTD},
		{-MIN_PKT_SIZE / 2, MIN_PKT_SIZE, 0, false, 0},
		/* Valid packet for synch so that something is received */
		{0, MIN_PKT_SIZE, 0, true, 0}};

	if (umem->unaligned_mode) {
		/* Crossing a chunk boundary allowed */
		pkts[12].valid = true;
		pkts[13].valid = true;
	}

	test->mtu = MAX_ETH_JUMBO_SIZE;
	pkt_stream_generate_custom(test, pkts, ARRAY_SIZE(pkts));
	return testapp_validate_traffic(test);
}

static int testapp_invalid_desc(struct test_spec *test)
{
	struct xsk_umem_info *umem = test->ifobj_tx->umem;
	u64 umem_size = umem->num_frames * umem->frame_size;
	struct pkt pkts[] = {
		/* Zero packet address allowed */
		{0, MIN_PKT_SIZE, 0, true},
		/* Allowed packet */
		{0, MIN_PKT_SIZE, 0, true},
		/* Straddling the start of umem */
		{-2, MIN_PKT_SIZE, 0, false},
		/* Packet too large */
		{0, XSK_UMEM__INVALID_FRAME_SIZE, 0, false},
		/* Up to end of umem allowed */
		{umem_size - MIN_PKT_SIZE - 2 * umem->frame_size, MIN_PKT_SIZE, 0, true},
		/* After umem ends */
		{umem_size, MIN_PKT_SIZE, 0, false},
		/* Straddle the end of umem */
		{umem_size - MIN_PKT_SIZE / 2, MIN_PKT_SIZE, 0, false},
		/* Straddle a 4K boundary */
		{0x1000 - MIN_PKT_SIZE / 2, MIN_PKT_SIZE, 0, false},
		/* Straddle a 2K boundary */
		{0x800 - MIN_PKT_SIZE / 2, MIN_PKT_SIZE, 0, true},
		/* Valid packet for synch so that something is received */
		{0, MIN_PKT_SIZE, 0, true}};

	if (umem->unaligned_mode) {
		/* Crossing a page boundary allowed */
		pkts[7].valid = true;
	}
	if (umem->frame_size == XSK_UMEM__DEFAULT_FRAME_SIZE / 2) {
		/* Crossing a 2K frame size boundary not allowed */
		pkts[8].valid = false;
	}

	if (test->ifobj_tx->shared_umem) {
		pkts[4].offset += umem_size;
		pkts[5].offset += umem_size;
		pkts[6].offset += umem_size;
	}

	pkt_stream_generate_custom(test, pkts, ARRAY_SIZE(pkts));
	return testapp_validate_traffic(test);
}

static int testapp_xdp_drop(struct test_spec *test)
{
	struct xsk_xdp_progs *skel_rx = test->ifobj_rx->xdp_progs;
	struct xsk_xdp_progs *skel_tx = test->ifobj_tx->xdp_progs;

	test_spec_set_xdp_prog(test, skel_rx->progs.xsk_xdp_drop, skel_tx->progs.xsk_xdp_drop,
			       skel_rx->maps.xsk, skel_tx->maps.xsk);

	pkt_stream_receive_half(test);
	return testapp_validate_traffic(test);
}

static int testapp_xdp_metadata_copy(struct test_spec *test)
{
	struct xsk_xdp_progs *skel_rx = test->ifobj_rx->xdp_progs;
	struct xsk_xdp_progs *skel_tx = test->ifobj_tx->xdp_progs;

	test_spec_set_xdp_prog(test, skel_rx->progs.xsk_xdp_populate_metadata,
			       skel_tx->progs.xsk_xdp_populate_metadata,
			       skel_rx->maps.xsk, skel_tx->maps.xsk);
	test->ifobj_rx->use_metadata = true;

	skel_rx->bss->count = 0;

	return testapp_validate_traffic(test);
}

static int testapp_xdp_shared_umem(struct test_spec *test)
{
	struct xsk_xdp_progs *skel_rx = test->ifobj_rx->xdp_progs;
	struct xsk_xdp_progs *skel_tx = test->ifobj_tx->xdp_progs;

	test->total_steps = 1;
	test->nb_sockets = 2;

	test_spec_set_xdp_prog(test, skel_rx->progs.xsk_xdp_shared_umem,
			       skel_tx->progs.xsk_xdp_shared_umem,
			       skel_rx->maps.xsk, skel_tx->maps.xsk);

	pkt_stream_even_odd_sequence(test);

	return testapp_validate_traffic(test);
}

static int testapp_poll_txq_tmout(struct test_spec *test)
{
	test->ifobj_tx->use_poll = true;
	/* create invalid frame by set umem frame_size and pkt length equal to 2048 */
	test->ifobj_tx->umem->frame_size = 2048;
	pkt_stream_replace(test, 2 * DEFAULT_PKT_CNT, 2048);
	return testapp_validate_traffic_single_thread(test, test->ifobj_tx);
}

static int testapp_poll_rxq_tmout(struct test_spec *test)
{
	test->ifobj_rx->use_poll = true;
	return testapp_validate_traffic_single_thread(test, test->ifobj_rx);
}

static int testapp_too_many_frags(struct test_spec *test)
{
	struct pkt *pkts;
	u32 max_frags, i;
	int ret;

	if (test->mode == TEST_MODE_ZC) {
		max_frags = test->ifobj_tx->xdp_zc_max_segs;
	} else {
		max_frags = get_max_skb_frags();
		if (!max_frags) {
			ksft_print_msg("Couldn't retrieve MAX_SKB_FRAGS from system, using default (17) value\n");
			max_frags = 17;
		}
		max_frags += 1;
	}

	pkts = calloc(2 * max_frags + 2, sizeof(struct pkt));
	if (!pkts)
		return TEST_FAILURE;

	test->mtu = MAX_ETH_JUMBO_SIZE;

	/* Valid packet for synch */
	pkts[0].len = MIN_PKT_SIZE;
	pkts[0].valid = true;

	/* One valid packet with the max amount of frags */
	for (i = 1; i < max_frags + 1; i++) {
		pkts[i].len = MIN_PKT_SIZE;
		pkts[i].options = XDP_PKT_CONTD;
		pkts[i].valid = true;
	}
	pkts[max_frags].options = 0;

	/* An invalid packet with the max amount of frags but signals packet
	 * continues on the last frag
	 */
	for (i = max_frags + 1; i < 2 * max_frags + 1; i++) {
		pkts[i].len = MIN_PKT_SIZE;
		pkts[i].options = XDP_PKT_CONTD;
		pkts[i].valid = false;
	}

	/* Valid packet for synch */
	pkts[2 * max_frags + 1].len = MIN_PKT_SIZE;
	pkts[2 * max_frags + 1].valid = true;

	pkt_stream_generate_custom(test, pkts, 2 * max_frags + 2);
	ret = testapp_validate_traffic(test);

	free(pkts);
	return ret;
}

static int xsk_load_xdp_programs(struct ifobject *ifobj)
{
	ifobj->xdp_progs = xsk_xdp_progs__open_and_load();
	if (libbpf_get_error(ifobj->xdp_progs))
		return libbpf_get_error(ifobj->xdp_progs);

	return 0;
}

static void xsk_unload_xdp_programs(struct ifobject *ifobj)
{
	xsk_xdp_progs__destroy(ifobj->xdp_progs);
}

/* Simple test */
static bool hugepages_present(void)
{
	size_t mmap_sz = 2 * DEFAULT_UMEM_BUFFERS * XSK_UMEM__DEFAULT_FRAME_SIZE;
	void *bufs;

	bufs = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, MAP_HUGE_2MB);
	if (bufs == MAP_FAILED)
		return false;

	mmap_sz = ceil_u64(mmap_sz, HUGEPAGE_SIZE) * HUGEPAGE_SIZE;
	munmap(bufs, mmap_sz);
	return true;
}

static void init_iface(struct ifobject *ifobj, thread_func_t func_ptr)
{
	LIBBPF_OPTS(bpf_xdp_query_opts, query_opts);
	int err;

	ifobj->func_ptr = func_ptr;

	err = xsk_load_xdp_programs(ifobj);
	if (err) {
		ksft_print_msg("Error loading XDP program\n");
		exit_with_error(err);
	}

	if (hugepages_present())
		ifobj->unaligned_supp = true;

	err = bpf_xdp_query(ifobj->ifindex, XDP_FLAGS_DRV_MODE, &query_opts);
	if (err) {
		ksft_print_msg("Error querying XDP capabilities\n");
		exit_with_error(-err);
	}
	if (query_opts.feature_flags & NETDEV_XDP_ACT_RX_SG)
		ifobj->multi_buff_supp = true;
	if (query_opts.feature_flags & NETDEV_XDP_ACT_XSK_ZEROCOPY) {
		if (query_opts.xdp_zc_max_segs > 1) {
			ifobj->multi_buff_zc_supp = true;
			ifobj->xdp_zc_max_segs = query_opts.xdp_zc_max_segs;
		} else {
			ifobj->xdp_zc_max_segs = 0;
		}
	}
}

static int testapp_send_receive(struct test_spec *test)
{
	return testapp_validate_traffic(test);
}

static int testapp_send_receive_2k_frame(struct test_spec *test)
{
	test->ifobj_tx->umem->frame_size = 2048;
	test->ifobj_rx->umem->frame_size = 2048;
	pkt_stream_replace(test, DEFAULT_PKT_CNT, MIN_PKT_SIZE);
	return testapp_validate_traffic(test);
}

static int testapp_poll_rx(struct test_spec *test)
{
	test->ifobj_rx->use_poll = true;
	return testapp_validate_traffic(test);
}

static int testapp_poll_tx(struct test_spec *test)
{
	test->ifobj_tx->use_poll = true;
	return testapp_validate_traffic(test);
}

static int testapp_aligned_inv_desc(struct test_spec *test)
{
	return testapp_invalid_desc(test);
}

static int testapp_aligned_inv_desc_2k_frame(struct test_spec *test)
{
	test->ifobj_tx->umem->frame_size = 2048;
	test->ifobj_rx->umem->frame_size = 2048;
	return testapp_invalid_desc(test);
}

static int testapp_unaligned_inv_desc(struct test_spec *test)
{
	test->ifobj_tx->umem->unaligned_mode = true;
	test->ifobj_rx->umem->unaligned_mode = true;
	return testapp_invalid_desc(test);
}

static int testapp_unaligned_inv_desc_4001_frame(struct test_spec *test)
{
	u64 page_size, umem_size;

	/* Odd frame size so the UMEM doesn't end near a page boundary. */
	test->ifobj_tx->umem->frame_size = 4001;
	test->ifobj_rx->umem->frame_size = 4001;
	test->ifobj_tx->umem->unaligned_mode = true;
	test->ifobj_rx->umem->unaligned_mode = true;
	/* This test exists to test descriptors that staddle the end of
	 * the UMEM but not a page.
	 */
	page_size = sysconf(_SC_PAGESIZE);
	umem_size = test->ifobj_tx->umem->num_frames * test->ifobj_tx->umem->frame_size;
	assert(umem_size % page_size > MIN_PKT_SIZE);
	assert(umem_size % page_size < page_size - MIN_PKT_SIZE);

	return testapp_invalid_desc(test);
}

static int testapp_aligned_inv_desc_mb(struct test_spec *test)
{
	return testapp_invalid_desc_mb(test);
}

static int testapp_unaligned_inv_desc_mb(struct test_spec *test)
{
	test->ifobj_tx->umem->unaligned_mode = true;
	test->ifobj_rx->umem->unaligned_mode = true;
	return testapp_invalid_desc_mb(test);
}

static int testapp_xdp_metadata(struct test_spec *test)
{
	return testapp_xdp_metadata_copy(test);
}

static int testapp_xdp_metadata_mb(struct test_spec *test)
{
	test->mtu = MAX_ETH_JUMBO_SIZE;
	return testapp_xdp_metadata_copy(test);
}

static int testapp_hw_sw_min_ring_size(struct test_spec *test)
{
	int ret;

	test->set_ring = true;
	test->total_steps = 2;
	test->ifobj_tx->ring.tx_pending = DEFAULT_BATCH_SIZE;
	test->ifobj_tx->ring.rx_pending = DEFAULT_BATCH_SIZE * 2;
	test->ifobj_tx->xsk->batch_size = 1;
	test->ifobj_rx->xsk->batch_size = 1;
	ret = testapp_validate_traffic(test);
	if (ret)
		return ret;

	/* Set batch size to hw_ring_size - 1 */
	test->ifobj_tx->xsk->batch_size = DEFAULT_BATCH_SIZE - 1;
	test->ifobj_rx->xsk->batch_size = DEFAULT_BATCH_SIZE - 1;
	return testapp_validate_traffic(test);
}

static int testapp_hw_sw_max_ring_size(struct test_spec *test)
{
	u32 max_descs = XSK_RING_PROD__DEFAULT_NUM_DESCS * 4;
	int ret;

	test->set_ring = true;
	test->total_steps = 2;
	test->ifobj_tx->ring.tx_pending = test->ifobj_tx->ring.tx_max_pending;
	test->ifobj_tx->ring.rx_pending  = test->ifobj_tx->ring.rx_max_pending;
	test->ifobj_rx->umem->num_frames = max_descs;
	test->ifobj_rx->umem->fill_size = max_descs;
	test->ifobj_rx->umem->comp_size = max_descs;
	test->ifobj_tx->xsk->batch_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
	test->ifobj_rx->xsk->batch_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;

	ret = testapp_validate_traffic(test);
	if (ret)
		return ret;

	/* Set batch_size to 8152 for testing, as the ice HW ignores the 3 lowest bits when
	 * updating the Rx HW tail register.
	 */
	test->ifobj_tx->xsk->batch_size = test->ifobj_tx->ring.tx_max_pending - 8;
	test->ifobj_rx->xsk->batch_size = test->ifobj_tx->ring.tx_max_pending - 8;
	pkt_stream_replace(test, max_descs, MIN_PKT_SIZE);
	return testapp_validate_traffic(test);
}

static int testapp_xdp_adjust_tail(struct test_spec *test, int adjust_value)
{
	struct xsk_xdp_progs *skel_rx = test->ifobj_rx->xdp_progs;
	struct xsk_xdp_progs *skel_tx = test->ifobj_tx->xdp_progs;

	test_spec_set_xdp_prog(test, skel_rx->progs.xsk_xdp_adjust_tail,
			       skel_tx->progs.xsk_xdp_adjust_tail,
			       skel_rx->maps.xsk, skel_tx->maps.xsk);

	skel_rx->bss->adjust_value = adjust_value;

	return testapp_validate_traffic(test);
}

static int testapp_adjust_tail(struct test_spec *test, u32 value, u32 pkt_len)
{
	int ret;

	test->adjust_tail_support = true;
	test->adjust_tail = true;
	test->total_steps = 1;

	pkt_stream_replace_ifobject(test->ifobj_tx, DEFAULT_BATCH_SIZE, pkt_len);
	pkt_stream_replace_ifobject(test->ifobj_rx, DEFAULT_BATCH_SIZE, pkt_len + value);

	ret = testapp_xdp_adjust_tail(test, value);
	if (ret)
		return ret;

	if (!test->adjust_tail_support) {
		ksft_test_result_skip("%s %sResize pkt with bpf_xdp_adjust_tail() not supported\n",
				      mode_string(test), busy_poll_string(test));
		return TEST_SKIP;
	}

	return 0;
}

static int testapp_adjust_tail_shrink(struct test_spec *test)
{
	/* Shrink by 4 bytes for testing purpose */
	return testapp_adjust_tail(test, -4, MIN_PKT_SIZE * 2);
}

static int testapp_adjust_tail_shrink_mb(struct test_spec *test)
{
	test->mtu = MAX_ETH_JUMBO_SIZE;
	/* Shrink by the frag size */
	return testapp_adjust_tail(test, -XSK_UMEM__MAX_FRAME_SIZE, XSK_UMEM__LARGE_FRAME_SIZE * 2);
}

static int testapp_adjust_tail_grow(struct test_spec *test)
{
	/* Grow by 4 bytes for testing purpose */
	return testapp_adjust_tail(test, 4, MIN_PKT_SIZE * 2);
}

static int testapp_adjust_tail_grow_mb(struct test_spec *test)
{
	test->mtu = MAX_ETH_JUMBO_SIZE;
	/* Grow by (frag_size - last_frag_Size) - 1 to stay inside the last fragment */
	return testapp_adjust_tail(test, (XSK_UMEM__MAX_FRAME_SIZE / 2) - 1,
				   XSK_UMEM__LARGE_FRAME_SIZE * 2);
}

static int testapp_tx_queue_consumer(struct test_spec *test)
{
	int nr_packets;

	if (test->mode == TEST_MODE_ZC) {
		ksft_test_result_skip("Can not run TX_QUEUE_CONSUMER test for ZC mode\n");
		return TEST_SKIP;
	}

	nr_packets = MAX_TX_BUDGET_DEFAULT + 1;
	pkt_stream_replace(test, nr_packets, MIN_PKT_SIZE);
	test->ifobj_tx->xsk->batch_size = nr_packets;
	test->ifobj_tx->xsk->check_consumer = true;

	return testapp_validate_traffic(test);
}

static void run_pkt_test(struct test_spec *test)
{
	int ret;

	ret = test->test_func(test);

	if (ret == TEST_PASS)
		ksft_test_result_pass("PASS: %s %s%s\n", mode_string(test), busy_poll_string(test),
				      test->name);
	pkt_stream_restore_default(test);
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

	ifobj->umem = calloc(1, sizeof(*ifobj->umem));
	if (!ifobj->umem)
		goto out_umem;

	return ifobj;

out_umem:
	free(ifobj->xsk_arr);
out_xsk_arr:
	free(ifobj);
	return NULL;
}

static void ifobject_delete(struct ifobject *ifobj)
{
	free(ifobj->umem);
	free(ifobj->xsk_arr);
	free(ifobj);
}

static bool is_xdp_supported(int ifindex)
{
	int flags = XDP_FLAGS_DRV_MODE;

	LIBBPF_OPTS(bpf_link_create_opts, opts, .flags = flags);
	struct bpf_insn insns[2] = {
		BPF_MOV64_IMM(BPF_REG_0, XDP_PASS),
		BPF_EXIT_INSN()
	};
	int prog_fd, insn_cnt = ARRAY_SIZE(insns);
	int err;

	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, NULL, "GPL", insns, insn_cnt, NULL);
	if (prog_fd < 0)
		return false;

	err = bpf_xdp_attach(ifindex, prog_fd, flags, NULL);
	if (err) {
		close(prog_fd);
		return false;
	}

	bpf_xdp_detach(ifindex, flags, NULL);
	close(prog_fd);

	return true;
}

static const struct test_spec tests[] = {
	{.name = "SEND_RECEIVE", .test_func = testapp_send_receive},
	{.name = "SEND_RECEIVE_2K_FRAME", .test_func = testapp_send_receive_2k_frame},
	{.name = "SEND_RECEIVE_SINGLE_PKT", .test_func = testapp_single_pkt},
	{.name = "POLL_RX", .test_func = testapp_poll_rx},
	{.name = "POLL_TX", .test_func = testapp_poll_tx},
	{.name = "POLL_RXQ_FULL", .test_func = testapp_poll_rxq_tmout},
	{.name = "POLL_TXQ_FULL", .test_func = testapp_poll_txq_tmout},
	{.name = "SEND_RECEIVE_UNALIGNED", .test_func = testapp_send_receive_unaligned},
	{.name = "ALIGNED_INV_DESC", .test_func = testapp_aligned_inv_desc},
	{.name = "ALIGNED_INV_DESC_2K_FRAME_SIZE", .test_func = testapp_aligned_inv_desc_2k_frame},
	{.name = "UNALIGNED_INV_DESC", .test_func = testapp_unaligned_inv_desc},
	{.name = "UNALIGNED_INV_DESC_4001_FRAME_SIZE",
	 .test_func = testapp_unaligned_inv_desc_4001_frame},
	{.name = "UMEM_HEADROOM", .test_func = testapp_headroom},
	{.name = "TEARDOWN", .test_func = testapp_teardown},
	{.name = "BIDIRECTIONAL", .test_func = testapp_bidirectional},
	{.name = "STAT_RX_DROPPED", .test_func = testapp_stats_rx_dropped},
	{.name = "STAT_TX_INVALID", .test_func = testapp_stats_tx_invalid_descs},
	{.name = "STAT_RX_FULL", .test_func = testapp_stats_rx_full},
	{.name = "STAT_FILL_EMPTY", .test_func = testapp_stats_fill_empty},
	{.name = "XDP_PROG_CLEANUP", .test_func = testapp_xdp_prog_cleanup},
	{.name = "XDP_DROP_HALF", .test_func = testapp_xdp_drop},
	{.name = "XDP_SHARED_UMEM", .test_func = testapp_xdp_shared_umem},
	{.name = "XDP_METADATA_COPY", .test_func = testapp_xdp_metadata},
	{.name = "XDP_METADATA_COPY_MULTI_BUFF", .test_func = testapp_xdp_metadata_mb},
	{.name = "SEND_RECEIVE_9K_PACKETS", .test_func = testapp_send_receive_mb},
	{.name = "SEND_RECEIVE_UNALIGNED_9K_PACKETS",
	 .test_func = testapp_send_receive_unaligned_mb},
	{.name = "ALIGNED_INV_DESC_MULTI_BUFF", .test_func = testapp_aligned_inv_desc_mb},
	{.name = "UNALIGNED_INV_DESC_MULTI_BUFF", .test_func = testapp_unaligned_inv_desc_mb},
	{.name = "TOO_MANY_FRAGS", .test_func = testapp_too_many_frags},
	{.name = "HW_SW_MIN_RING_SIZE", .test_func = testapp_hw_sw_min_ring_size},
	{.name = "HW_SW_MAX_RING_SIZE", .test_func = testapp_hw_sw_max_ring_size},
	{.name = "XDP_ADJUST_TAIL_SHRINK", .test_func = testapp_adjust_tail_shrink},
	{.name = "XDP_ADJUST_TAIL_SHRINK_MULTI_BUFF", .test_func = testapp_adjust_tail_shrink_mb},
	{.name = "XDP_ADJUST_TAIL_GROW", .test_func = testapp_adjust_tail_grow},
	{.name = "XDP_ADJUST_TAIL_GROW_MULTI_BUFF", .test_func = testapp_adjust_tail_grow_mb},
	{.name = "TX_QUEUE_CONSUMER", .test_func = testapp_tx_queue_consumer},
	};

static void print_tests(void)
{
	u32 i;

	printf("Tests:\n");
	for (i = 0; i < ARRAY_SIZE(tests); i++)
		printf("%u: %s\n", i, tests[i].name);
}

int main(int argc, char **argv)
{
	struct pkt_stream *rx_pkt_stream_default;
	struct pkt_stream *tx_pkt_stream_default;
	struct ifobject *ifobj_tx, *ifobj_rx;
	u32 i, j, failed_tests = 0, nb_tests;
	int modes = TEST_MODE_SKB + 1;
	struct test_spec test;
	bool shared_netdev;
	int ret;

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	ifobj_tx = ifobject_create();
	if (!ifobj_tx)
		exit_with_error(ENOMEM);
	ifobj_rx = ifobject_create();
	if (!ifobj_rx)
		exit_with_error(ENOMEM);

	setlocale(LC_ALL, "");

	parse_command_line(ifobj_tx, ifobj_rx, argc, argv);

	if (opt_print_tests) {
		print_tests();
		ksft_exit_xpass();
	}
	if (opt_run_test != RUN_ALL_TESTS && opt_run_test >= ARRAY_SIZE(tests)) {
		ksft_print_msg("Error: test %u does not exist.\n", opt_run_test);
		ksft_exit_xfail();
	}

	shared_netdev = (ifobj_tx->ifindex == ifobj_rx->ifindex);
	ifobj_tx->shared_umem = shared_netdev;
	ifobj_rx->shared_umem = shared_netdev;

	if (!validate_interface(ifobj_tx) || !validate_interface(ifobj_rx))
		print_usage(argv);

	if (is_xdp_supported(ifobj_tx->ifindex)) {
		modes++;
		if (ifobj_zc_avail(ifobj_tx))
			modes++;
	}

	ret = get_hw_ring_size(ifobj_tx->ifname, &ifobj_tx->ring);
	if (!ret) {
		ifobj_tx->hw_ring_size_supp = true;
		ifobj_tx->set_ring.default_tx = ifobj_tx->ring.tx_pending;
		ifobj_tx->set_ring.default_rx = ifobj_tx->ring.rx_pending;
	}

	init_iface(ifobj_rx, worker_testapp_validate_rx);
	init_iface(ifobj_tx, worker_testapp_validate_tx);

	test_spec_init(&test, ifobj_tx, ifobj_rx, 0, &tests[0]);
	tx_pkt_stream_default = pkt_stream_generate(DEFAULT_PKT_CNT, MIN_PKT_SIZE);
	rx_pkt_stream_default = pkt_stream_generate(DEFAULT_PKT_CNT, MIN_PKT_SIZE);
	if (!tx_pkt_stream_default || !rx_pkt_stream_default)
		exit_with_error(ENOMEM);
	test.tx_pkt_stream_default = tx_pkt_stream_default;
	test.rx_pkt_stream_default = rx_pkt_stream_default;

	if (opt_run_test == RUN_ALL_TESTS)
		nb_tests = ARRAY_SIZE(tests);
	else
		nb_tests = 1;
	if (opt_mode == TEST_MODE_ALL) {
		ksft_set_plan(modes * nb_tests);
	} else {
		if (opt_mode == TEST_MODE_DRV && modes <= TEST_MODE_DRV) {
			ksft_print_msg("Error: XDP_DRV mode not supported.\n");
			ksft_exit_xfail();
		}
		if (opt_mode == TEST_MODE_ZC && modes <= TEST_MODE_ZC) {
			ksft_print_msg("Error: zero-copy mode not supported.\n");
			ksft_exit_xfail();
		}

		ksft_set_plan(nb_tests);
	}

	for (i = 0; i < modes; i++) {
		if (opt_mode != TEST_MODE_ALL && i != opt_mode)
			continue;

		for (j = 0; j < ARRAY_SIZE(tests); j++) {
			if (opt_run_test != RUN_ALL_TESTS && j != opt_run_test)
				continue;

			test_spec_init(&test, ifobj_tx, ifobj_rx, i, &tests[j]);
			run_pkt_test(&test);
			usleep(USLEEP_MAX);

			if (test.fail)
				failed_tests++;
		}
	}

	if (ifobj_tx->hw_ring_size_supp)
		hw_ring_size_reset(ifobj_tx);

	pkt_stream_delete(tx_pkt_stream_default);
	pkt_stream_delete(rx_pkt_stream_default);
	xsk_unload_xdp_programs(ifobj_tx);
	xsk_unload_xdp_programs(ifobj_rx);
	ifobject_delete(ifobj_tx);
	ifobject_delete(ifobj_rx);

	if (failed_tests)
		ksft_exit_fail();
	else
		ksft_exit_pass();
}
