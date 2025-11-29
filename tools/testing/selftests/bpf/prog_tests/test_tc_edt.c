// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/*
 * BPF-based flow shaping
 *
 * The test brings up two veth in two isolated namespaces, attach some flow
 * shaping program onto it, and ensures that a manual speedtest maximum
 * value matches the rate set in the BPF shapers.
 */

#include <asm-generic/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <bpf/libbpf.h>
#include <pthread.h>
#include "test_progs.h"
#include "network_helpers.h"
#include "test_tc_edt.skel.h"

#define SERVER_NS "tc-edt-server-ns"
#define CLIENT_NS "tc-edt-client-ns"
#define IP4_ADDR_VETH1 "192.168.1.1"
#define IP4_ADDR_VETH2 "192.168.1.2"
#define IP4_ADDR_VETH2_HEX 0xC0A80102

#define TIMEOUT_MS		2000
#define TEST_PORT		9000
#define TARGET_RATE_MBPS	5.0
#define TX_BYTES_COUNT		(1 * 1000 * 1000)
#define RATE_ERROR_PERCENT	2.0

struct connection {
	int server_listen_fd;
	int server_conn_fd;
	int client_conn_fd;
};

static int setup(struct test_tc_edt *skel)
{
	struct nstoken *nstoken_client, *nstoken_server;
	int ret;

	if (!ASSERT_OK(make_netns(CLIENT_NS), "create client ns"))
		goto fail;
	if (!ASSERT_OK(make_netns(SERVER_NS), "create server ns"))
		goto fail_delete_client_ns;

	nstoken_client = open_netns(CLIENT_NS);
	if (!ASSERT_OK_PTR(nstoken_client, "open client ns"))
		goto fail_delete_server_ns;
	SYS(fail_close_client_ns, "ip link add veth1 type veth peer name %s",
	    "veth2 netns " SERVER_NS);
	SYS(fail_close_client_ns, "ip -4 addr add " IP4_ADDR_VETH1 "/24 dev veth1");
	SYS(fail_close_client_ns, "ip link set veth1 up");

	nstoken_server = open_netns(SERVER_NS);
	if (!ASSERT_OK_PTR(nstoken_server, "enter server ns"))
		goto fail_close_client_ns;
	SYS(fail_close_server_ns, "ip -4 addr add " IP4_ADDR_VETH2 "/24 dev veth2");
	SYS(fail_close_server_ns, "ip link set veth2 up");
	SYS(fail_close_server_ns, "tc qdisc add dev veth2 root fq");
	ret = tc_prog_attach("veth2", -1, bpf_program__fd(skel->progs.tc_prog));
	if (!ASSERT_OK(ret, "attach bpf prog"))
		goto fail_close_server_ns;
	skel->bss->target_rate = TARGET_RATE_MBPS * 1000 * 1000;
	close_netns(nstoken_server);
	close_netns(nstoken_client);

	return 0;

fail_close_server_ns:
	close_netns(nstoken_server);
fail_close_client_ns:
	close_netns(nstoken_client);
fail_delete_server_ns:
	remove_netns(SERVER_NS);
fail_delete_client_ns:
	remove_netns(CLIENT_NS);
fail:
	return -1;
}

static void cleanup(void)
{
	remove_netns(CLIENT_NS);
	remove_netns(SERVER_NS);
}

static void run_test(void)
{
	int server_fd, client_fd, err;
	double rate_mbps, rate_error;
	struct nstoken *nstoken;
	__u64 ts_start, ts_end;

	nstoken = open_netns(SERVER_NS);
	if (!ASSERT_OK_PTR(nstoken, "open server ns"))
		return;
	server_fd = start_server(AF_INET, SOCK_STREAM, IP4_ADDR_VETH2,
			TEST_PORT, TIMEOUT_MS);
	if (!ASSERT_OK_FD(server_fd, "start server"))
		return;

	close_netns(nstoken);
	nstoken = open_netns(CLIENT_NS);
	if (!ASSERT_OK_PTR(nstoken, "open client ns"))
		return;
	client_fd = connect_to_fd(server_fd, 0);
	if (!ASSERT_OK_FD(client_fd, "connect client"))
		return;

	ts_start = get_time_ns();
	err = send_recv_data(server_fd, client_fd, TX_BYTES_COUNT);
	ts_end = get_time_ns();
	close_netns(nstoken);
	ASSERT_OK(err, "send_recv_data");

	rate_mbps = TX_BYTES_COUNT / ((ts_end - ts_start) / 1000.0);
	rate_error =
		fabs((rate_mbps - TARGET_RATE_MBPS) * 100.0 / TARGET_RATE_MBPS);

	ASSERT_LE(rate_error, RATE_ERROR_PERCENT,
		  "rate error is lower than threshold");
}

void test_tc_edt(void)
{
	struct test_tc_edt *skel;

	skel = test_tc_edt__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open and load"))
		return;

	if (!ASSERT_OK(setup(skel), "global setup"))
		return;

	run_test();

	cleanup();
	test_tc_edt__destroy(skel);
}
