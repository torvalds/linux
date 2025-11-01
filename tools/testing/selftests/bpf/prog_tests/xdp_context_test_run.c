// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include "test_xdp_context_test_run.skel.h"
#include "test_xdp_meta.skel.h"

#define RX_NAME "veth0"
#define TX_NAME "veth1"
#define TX_NETNS "xdp_context_tx"
#define RX_NETNS "xdp_context_rx"
#define TAP_NAME "tap0"
#define DUMMY_NAME "dum0"
#define TAP_NETNS "xdp_context_tuntap"

#define TEST_PAYLOAD_LEN 32
static const __u8 test_payload[TEST_PAYLOAD_LEN] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
};

void test_xdp_context_error(int prog_fd, struct bpf_test_run_opts opts,
			    __u32 data_meta, __u32 data, __u32 data_end,
			    __u32 ingress_ifindex, __u32 rx_queue_index,
			    __u32 egress_ifindex)
{
	struct xdp_md ctx = {
		.data = data,
		.data_end = data_end,
		.data_meta = data_meta,
		.ingress_ifindex = ingress_ifindex,
		.rx_queue_index = rx_queue_index,
		.egress_ifindex = egress_ifindex,
	};
	int err;

	opts.ctx_in = &ctx;
	opts.ctx_size_in = sizeof(ctx);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_EQ(errno, EINVAL, "errno-EINVAL");
	ASSERT_ERR(err, "bpf_prog_test_run");
}

void test_xdp_context_test_run(void)
{
	struct test_xdp_context_test_run *skel = NULL;
	char data[sizeof(pkt_v4) + sizeof(__u32)];
	char bad_ctx[sizeof(struct xdp_md) + 1];
	struct xdp_md ctx_in, ctx_out;
	DECLARE_LIBBPF_OPTS(bpf_test_run_opts, opts,
			    .data_in = &data,
			    .data_size_in = sizeof(data),
			    .ctx_out = &ctx_out,
			    .ctx_size_out = sizeof(ctx_out),
			    .repeat = 1,
		);
	int err, prog_fd;

	skel = test_xdp_context_test_run__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;
	prog_fd = bpf_program__fd(skel->progs.xdp_context);

	/* Data past the end of the kernel's struct xdp_md must be 0 */
	bad_ctx[sizeof(bad_ctx) - 1] = 1;
	opts.ctx_in = bad_ctx;
	opts.ctx_size_in = sizeof(bad_ctx);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_EQ(errno, E2BIG, "extradata-errno");
	ASSERT_ERR(err, "bpf_prog_test_run(extradata)");

	*(__u32 *)data = XDP_PASS;
	*(struct ipv4_packet *)(data + sizeof(__u32)) = pkt_v4;
	opts.ctx_in = &ctx_in;
	opts.ctx_size_in = sizeof(ctx_in);
	memset(&ctx_in, 0, sizeof(ctx_in));
	ctx_in.data_meta = 0;
	ctx_in.data = sizeof(__u32);
	ctx_in.data_end = ctx_in.data + sizeof(pkt_v4);
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(err, "bpf_prog_test_run(valid)");
	ASSERT_EQ(opts.retval, XDP_PASS, "valid-retval");
	ASSERT_EQ(opts.data_size_out, sizeof(pkt_v4), "valid-datasize");
	ASSERT_EQ(opts.ctx_size_out, opts.ctx_size_in, "valid-ctxsize");
	ASSERT_EQ(ctx_out.data_meta, 0, "valid-datameta");
	ASSERT_EQ(ctx_out.data, 0, "valid-data");
	ASSERT_EQ(ctx_out.data_end, sizeof(pkt_v4), "valid-dataend");

	/* Meta data's size must be a multiple of 4 */
	test_xdp_context_error(prog_fd, opts, 0, 1, sizeof(data), 0, 0, 0);

	/* data_meta must reference the start of data */
	test_xdp_context_error(prog_fd, opts, 4, sizeof(__u32), sizeof(data),
			       0, 0, 0);

	/* Meta data must be 255 bytes or smaller */
	test_xdp_context_error(prog_fd, opts, 0, 256, sizeof(data), 0, 0, 0);

	/* Total size of data must be data_end - data_meta or larger */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32),
			       sizeof(data) + 1, 0, 0, 0);

	/* RX queue cannot be specified without specifying an ingress */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32), sizeof(data),
			       0, 1, 0);

	/* Interface 1 is always the loopback interface which always has only
	 * one RX queue (index 0). This makes index 1 an invalid rx queue index
	 * for interface 1.
	 */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32), sizeof(data),
			       1, 1, 0);

	/* The egress cannot be specified */
	test_xdp_context_error(prog_fd, opts, 0, sizeof(__u32), sizeof(data),
			       0, 0, 1);

	test_xdp_context_test_run__destroy(skel);
}

static int send_test_packet(int ifindex)
{
	int n, sock = -1;
	__u8 packet[sizeof(struct ethhdr) + TEST_PAYLOAD_LEN];

	/* The ethernet header is not relevant for this test and doesn't need to
	 * be meaningful.
	 */
	struct ethhdr eth = { 0 };

	memcpy(packet, &eth, sizeof(eth));
	memcpy(packet + sizeof(eth), test_payload, TEST_PAYLOAD_LEN);

	sock = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW);
	if (!ASSERT_GE(sock, 0, "socket"))
		goto err;

	struct sockaddr_ll saddr = {
		.sll_family = PF_PACKET,
		.sll_ifindex = ifindex,
		.sll_halen = ETH_ALEN
	};
	n = sendto(sock, packet, sizeof(packet), 0, (struct sockaddr *)&saddr,
		   sizeof(saddr));
	if (!ASSERT_EQ(n, sizeof(packet), "sendto"))
		goto err;

	close(sock);
	return 0;

err:
	if (sock >= 0)
		close(sock);
	return -1;
}

static int write_test_packet(int tap_fd)
{
	__u8 packet[sizeof(struct ethhdr) + TEST_PAYLOAD_LEN];
	int n;

	/* The ethernet header doesn't need to be valid for this test */
	memset(packet, 0, sizeof(struct ethhdr));
	memcpy(packet + sizeof(struct ethhdr), test_payload, TEST_PAYLOAD_LEN);

	n = write(tap_fd, packet, sizeof(packet));
	if (!ASSERT_EQ(n, sizeof(packet), "write packet"))
		return -1;

	return 0;
}

static void assert_test_result(const struct bpf_map *result_map)
{
	int err;
	__u32 map_key = 0;
	__u8 map_value[TEST_PAYLOAD_LEN];

	err = bpf_map__lookup_elem(result_map, &map_key, sizeof(map_key),
				   &map_value, TEST_PAYLOAD_LEN, BPF_ANY);
	if (!ASSERT_OK(err, "lookup test_result"))
		return;

	ASSERT_MEMEQ(&map_value, &test_payload, TEST_PAYLOAD_LEN,
		     "test_result map contains test payload");
}

static bool clear_test_result(struct bpf_map *result_map)
{
	const __u8 v[sizeof(test_payload)] = {};
	const __u32 k = 0;
	int err;

	err = bpf_map__update_elem(result_map, &k, sizeof(k), v, sizeof(v), BPF_ANY);
	ASSERT_OK(err, "update test_result");

	return err == 0;
}

void test_xdp_context_veth(void)
{
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .attach_point = BPF_TC_INGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 1);
	struct netns_obj *rx_ns = NULL, *tx_ns = NULL;
	struct bpf_program *tc_prog, *xdp_prog;
	struct test_xdp_meta *skel = NULL;
	struct nstoken *nstoken = NULL;
	int rx_ifindex, tx_ifindex;
	int ret;

	tx_ns = netns_new(TX_NETNS, false);
	if (!ASSERT_OK_PTR(tx_ns, "create tx_ns"))
		return;

	rx_ns = netns_new(RX_NETNS, false);
	if (!ASSERT_OK_PTR(rx_ns, "create rx_ns"))
		goto close;

	SYS(close, "ip link add " RX_NAME " netns " RX_NETNS
	    " type veth peer name " TX_NAME " netns " TX_NETNS);

	nstoken = open_netns(RX_NETNS);
	if (!ASSERT_OK_PTR(nstoken, "setns rx_ns"))
		goto close;

	SYS(close, "ip link set dev " RX_NAME " up");

	skel = test_xdp_meta__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open and load skeleton"))
		goto close;

	rx_ifindex = if_nametoindex(RX_NAME);
	if (!ASSERT_GE(rx_ifindex, 0, "if_nametoindex rx"))
		goto close;

	tc_hook.ifindex = rx_ifindex;
	ret = bpf_tc_hook_create(&tc_hook);
	if (!ASSERT_OK(ret, "bpf_tc_hook_create"))
		goto close;

	tc_prog = bpf_object__find_program_by_name(skel->obj, "ing_cls");
	if (!ASSERT_OK_PTR(tc_prog, "open ing_cls prog"))
		goto close;

	tc_opts.prog_fd = bpf_program__fd(tc_prog);
	ret = bpf_tc_attach(&tc_hook, &tc_opts);
	if (!ASSERT_OK(ret, "bpf_tc_attach"))
		goto close;

	xdp_prog = bpf_object__find_program_by_name(skel->obj, "ing_xdp");
	if (!ASSERT_OK_PTR(xdp_prog, "open ing_xdp prog"))
		goto close;

	ret = bpf_xdp_attach(rx_ifindex,
			     bpf_program__fd(xdp_prog),
			     0, NULL);
	if (!ASSERT_GE(ret, 0, "bpf_xdp_attach"))
		goto close;

	close_netns(nstoken);

	nstoken = open_netns(TX_NETNS);
	if (!ASSERT_OK_PTR(nstoken, "setns tx_ns"))
		goto close;

	SYS(close, "ip link set dev " TX_NAME " up");

	tx_ifindex = if_nametoindex(TX_NAME);
	if (!ASSERT_GE(tx_ifindex, 0, "if_nametoindex tx"))
		goto close;

	ret = send_test_packet(tx_ifindex);
	if (!ASSERT_OK(ret, "send_test_packet"))
		goto close;

	assert_test_result(skel->maps.test_result);

close:
	close_netns(nstoken);
	test_xdp_meta__destroy(skel);
	netns_free(rx_ns);
	netns_free(tx_ns);
}

static void test_tuntap(struct bpf_program *xdp_prog,
			struct bpf_program *tc_prio_1_prog,
			struct bpf_program *tc_prio_2_prog,
			struct bpf_map *result_map)
{
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .attach_point = BPF_TC_INGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 1);
	struct netns_obj *ns = NULL;
	int tap_fd = -1;
	int tap_ifindex;
	int ret;

	if (!clear_test_result(result_map))
		return;

	ns = netns_new(TAP_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "create and open ns"))
		return;

	tap_fd = open_tuntap(TAP_NAME, true);
	if (!ASSERT_GE(tap_fd, 0, "open_tuntap"))
		goto close;

	SYS(close, "ip link set dev " TAP_NAME " up");

	tap_ifindex = if_nametoindex(TAP_NAME);
	if (!ASSERT_GE(tap_ifindex, 0, "if_nametoindex"))
		goto close;

	tc_hook.ifindex = tap_ifindex;
	ret = bpf_tc_hook_create(&tc_hook);
	if (!ASSERT_OK(ret, "bpf_tc_hook_create"))
		goto close;

	tc_opts.prog_fd = bpf_program__fd(tc_prio_1_prog);
	ret = bpf_tc_attach(&tc_hook, &tc_opts);
	if (!ASSERT_OK(ret, "bpf_tc_attach"))
		goto close;

	if (tc_prio_2_prog) {
		LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 2,
			    .prog_fd = bpf_program__fd(tc_prio_2_prog));

		ret = bpf_tc_attach(&tc_hook, &tc_opts);
		if (!ASSERT_OK(ret, "bpf_tc_attach"))
			goto close;
	}

	ret = bpf_xdp_attach(tap_ifindex, bpf_program__fd(xdp_prog),
			     0, NULL);
	if (!ASSERT_GE(ret, 0, "bpf_xdp_attach"))
		goto close;

	ret = write_test_packet(tap_fd);
	if (!ASSERT_OK(ret, "write_test_packet"))
		goto close;

	assert_test_result(result_map);

close:
	if (tap_fd >= 0)
		close(tap_fd);
	netns_free(ns);
}

/* Write a packet to a tap dev and copy it to ingress of a dummy dev */
static void test_tuntap_mirred(struct bpf_program *xdp_prog,
			       struct bpf_program *tc_prog,
			       bool *test_pass)
{
	LIBBPF_OPTS(bpf_tc_hook, tc_hook, .attach_point = BPF_TC_INGRESS);
	LIBBPF_OPTS(bpf_tc_opts, tc_opts, .handle = 1, .priority = 1);
	struct netns_obj *ns = NULL;
	int dummy_ifindex;
	int tap_fd = -1;
	int tap_ifindex;
	int ret;

	*test_pass = false;

	ns = netns_new(TAP_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		return;

	/* Setup dummy interface */
	SYS(close, "ip link add name " DUMMY_NAME " type dummy");
	SYS(close, "ip link set dev " DUMMY_NAME " up");

	dummy_ifindex = if_nametoindex(DUMMY_NAME);
	if (!ASSERT_GE(dummy_ifindex, 0, "if_nametoindex"))
		goto close;

	tc_hook.ifindex = dummy_ifindex;
	ret = bpf_tc_hook_create(&tc_hook);
	if (!ASSERT_OK(ret, "bpf_tc_hook_create"))
		goto close;

	tc_opts.prog_fd = bpf_program__fd(tc_prog);
	ret = bpf_tc_attach(&tc_hook, &tc_opts);
	if (!ASSERT_OK(ret, "bpf_tc_attach"))
		goto close;

	/* Setup TAP interface */
	tap_fd = open_tuntap(TAP_NAME, true);
	if (!ASSERT_GE(tap_fd, 0, "open_tuntap"))
		goto close;

	SYS(close, "ip link set dev " TAP_NAME " up");

	tap_ifindex = if_nametoindex(TAP_NAME);
	if (!ASSERT_GE(tap_ifindex, 0, "if_nametoindex"))
		goto close;

	ret = bpf_xdp_attach(tap_ifindex, bpf_program__fd(xdp_prog), 0, NULL);
	if (!ASSERT_GE(ret, 0, "bpf_xdp_attach"))
		goto close;

	/* Copy all packets received from TAP to dummy ingress */
	SYS(close, "tc qdisc add dev " TAP_NAME " clsact");
	SYS(close, "tc filter add dev " TAP_NAME " ingress "
		   "protocol all matchall "
		   "action mirred ingress mirror dev " DUMMY_NAME);

	/* Receive a packet on TAP */
	ret = write_test_packet(tap_fd);
	if (!ASSERT_OK(ret, "write_test_packet"))
		goto close;

	ASSERT_TRUE(*test_pass, "test_pass");

close:
	if (tap_fd >= 0)
		close(tap_fd);
	netns_free(ns);
}

void test_xdp_context_tuntap(void)
{
	struct test_xdp_meta *skel = NULL;

	skel = test_xdp_meta__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open and load skeleton"))
		return;

	if (test__start_subtest("data_meta"))
		test_tuntap(skel->progs.ing_xdp,
			    skel->progs.ing_cls,
			    NULL, /* tc prio 2 */
			    skel->maps.test_result);
	if (test__start_subtest("dynptr_read"))
		test_tuntap(skel->progs.ing_xdp,
			    skel->progs.ing_cls_dynptr_read,
			    NULL, /* tc prio 2 */
			    skel->maps.test_result);
	if (test__start_subtest("dynptr_slice"))
		test_tuntap(skel->progs.ing_xdp,
			    skel->progs.ing_cls_dynptr_slice,
			    NULL, /* tc prio 2 */
			    skel->maps.test_result);
	if (test__start_subtest("dynptr_write"))
		test_tuntap(skel->progs.ing_xdp_zalloc_meta,
			    skel->progs.ing_cls_dynptr_write,
			    skel->progs.ing_cls_dynptr_read,
			    skel->maps.test_result);
	if (test__start_subtest("dynptr_slice_rdwr"))
		test_tuntap(skel->progs.ing_xdp_zalloc_meta,
			    skel->progs.ing_cls_dynptr_slice_rdwr,
			    skel->progs.ing_cls_dynptr_slice,
			    skel->maps.test_result);
	if (test__start_subtest("dynptr_offset"))
		test_tuntap(skel->progs.ing_xdp_zalloc_meta,
			    skel->progs.ing_cls_dynptr_offset_wr,
			    skel->progs.ing_cls_dynptr_offset_rd,
			    skel->maps.test_result);
	if (test__start_subtest("dynptr_offset_oob"))
		test_tuntap(skel->progs.ing_xdp,
			    skel->progs.ing_cls_dynptr_offset_oob,
			    skel->progs.ing_cls,
			    skel->maps.test_result);
	if (test__start_subtest("clone_data_meta_empty_on_data_write"))
		test_tuntap_mirred(skel->progs.ing_xdp,
				   skel->progs.clone_data_meta_empty_on_data_write,
				   &skel->bss->test_pass);
	if (test__start_subtest("clone_data_meta_empty_on_meta_write"))
		test_tuntap_mirred(skel->progs.ing_xdp,
				   skel->progs.clone_data_meta_empty_on_meta_write,
				   &skel->bss->test_pass);
	if (test__start_subtest("clone_dynptr_empty_on_data_slice_write"))
		test_tuntap_mirred(skel->progs.ing_xdp,
				   skel->progs.clone_dynptr_empty_on_data_slice_write,
				   &skel->bss->test_pass);
	if (test__start_subtest("clone_dynptr_empty_on_meta_slice_write"))
		test_tuntap_mirred(skel->progs.ing_xdp,
				   skel->progs.clone_dynptr_empty_on_meta_slice_write,
				   &skel->bss->test_pass);
	if (test__start_subtest("clone_dynptr_rdonly_before_data_dynptr_write"))
		test_tuntap_mirred(skel->progs.ing_xdp,
				   skel->progs.clone_dynptr_rdonly_before_data_dynptr_write,
				   &skel->bss->test_pass);
	if (test__start_subtest("clone_dynptr_rdonly_before_meta_dynptr_write"))
		test_tuntap_mirred(skel->progs.ing_xdp,
				   skel->progs.clone_dynptr_rdonly_before_meta_dynptr_write,
				   &skel->bss->test_pass);

	test_xdp_meta__destroy(skel);
}
