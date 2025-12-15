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
#include <getopt.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/mman.h>
#include <linux/netdev.h>
#include <linux/ethtool.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "prog_tests/test_xsk.h"
#include "xsk_xdp_progs.skel.h"
#include "xsk.h"
#include "xskxceiver.h"
#include <bpf/bpf.h>
#include <linux/filter.h>
#include "kselftest.h"
#include "xsk_xdp_common.h"

#include <network_helpers.h>

static bool opt_print_tests;
static enum test_mode opt_mode = TEST_MODE_ALL;
static u32 opt_run_test = RUN_ALL_TESTS;

void test__fail(void) { /* for network_helpers.c */ }

static void __exit_with_error(int error, const char *file, const char *func, int line)
{
	ksft_test_result_fail("[%s:%s:%i]: ERROR: %d/\"%s\"\n", file, func, line,
			      error, strerror(error));
	ksft_exit_xfail();
}

#define exit_with_error(error) __exit_with_error(error, __FILE__, __func__, __LINE__)

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
	ret = xsk_configure_socket(xsk, umem, ifobject, false);
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

static void xsk_unload_xdp_programs(struct ifobject *ifobj)
{
	xsk_xdp_progs__destroy(ifobj->xdp_progs);
}

static void run_pkt_test(struct test_spec *test)
{
	int ret;

	ret = test->test_func(test);

	switch (ret) {
	case TEST_PASS:
		ksft_test_result_pass("PASS: %s %s%s\n", mode_string(test), busy_poll_string(test),
				      test->name);
		break;
	case TEST_SKIP:
		ksft_test_result_skip("SKIP: %s %s%s\n", mode_string(test), busy_poll_string(test),
				      test->name);
		break;
	case TEST_FAILURE:
		ksft_test_result_fail("FAIL: %s %s%s\n", mode_string(test), busy_poll_string(test),
				      test->name);
		break;
	default:
		ksft_test_result_fail("FAIL: %s %s%s -- Unexpected returned value (%d)\n",
				      mode_string(test), busy_poll_string(test), test->name, ret);
	}

	pkt_stream_restore_default(test);
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

static void print_tests(void)
{
	u32 i;

	printf("Tests:\n");
	for (i = 0; i < ARRAY_SIZE(tests); i++)
		printf("%u: %s\n", i, tests[i].name);
	for (i = ARRAY_SIZE(tests); i < ARRAY_SIZE(tests) + ARRAY_SIZE(ci_skip_tests); i++)
		printf("%u: %s\n", i, ci_skip_tests[i - ARRAY_SIZE(tests)].name);
}

int main(int argc, char **argv)
{
	const size_t total_tests = ARRAY_SIZE(tests) + ARRAY_SIZE(ci_skip_tests);
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
	if (opt_run_test != RUN_ALL_TESTS && opt_run_test >= total_tests) {
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

	if (init_iface(ifobj_rx, worker_testapp_validate_rx) ||
	    init_iface(ifobj_tx, worker_testapp_validate_tx)) {
		ksft_print_msg("Error : can't initialize interfaces\n");
		ksft_exit_xfail();
	}

	test_init(&test, ifobj_tx, ifobj_rx, 0, &tests[0]);
	tx_pkt_stream_default = pkt_stream_generate(DEFAULT_PKT_CNT, MIN_PKT_SIZE);
	rx_pkt_stream_default = pkt_stream_generate(DEFAULT_PKT_CNT, MIN_PKT_SIZE);
	if (!tx_pkt_stream_default || !rx_pkt_stream_default)
		exit_with_error(ENOMEM);
	test.tx_pkt_stream_default = tx_pkt_stream_default;
	test.rx_pkt_stream_default = rx_pkt_stream_default;

	if (opt_run_test == RUN_ALL_TESTS)
		nb_tests = total_tests;
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

		for (j = 0; j < total_tests; j++) {
			if (opt_run_test != RUN_ALL_TESTS && j != opt_run_test)
				continue;

			if (j < ARRAY_SIZE(tests))
				test_init(&test, ifobj_tx, ifobj_rx, i, &tests[j]);
			else
				test_init(&test, ifobj_tx, ifobj_rx, i,
					  &ci_skip_tests[j - ARRAY_SIZE(tests)]);
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
