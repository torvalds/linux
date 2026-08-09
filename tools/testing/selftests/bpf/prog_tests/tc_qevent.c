// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <network_helpers.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>

#include "test_tc_qevent.skel.h"

#define NS_TX		"tc_qevent_tx"
#define NS_RX		"tc_qevent_rx"
#define IP_TX		"10.255.0.1"
#define IP_RX		"10.255.0.2"
#define PIN_PATH	"/sys/fs/bpf/tc_qevent_redirect"

static void blast_udp(void)
{
	struct sockaddr_in dst = {};
	char buf[1400] = {};
	int fd, i;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (!ASSERT_GE(fd, 0, "udp socket"))
		return;

	dst.sin_family = AF_INET;
	dst.sin_port = htons(12345);
	inet_pton(AF_INET, IP_RX, &dst.sin_addr);

	/*
	 * Push far more than the RED queue can hold. Once qavg crosses qth_min
	 * every further packet hits the congestion_drop / early_drop qevent.
	 */
	for (i = 0; i < 50000; i++)
		sendto(fd, buf, sizeof(buf), MSG_DONTWAIT,
		       (struct sockaddr *)&dst, sizeof(dst));

	close(fd);
}

static void run_qevent_redirect(struct bpf_program *prog, __u64 *counter)
{
	struct nstoken *tok = NULL;
	int err;

	SYS_NOFAIL("ip netns del %s", NS_TX);
	SYS_NOFAIL("ip netns del %s", NS_RX);
	unlink(PIN_PATH);

	err = bpf_program__pin(prog, PIN_PATH);
	if (!ASSERT_OK(err, "pin prog"))
		return;

	SYS(unpin,  "ip netns add %s", NS_TX);
	SYS(del_tx, "ip netns add %s", NS_RX);
	SYS(del_rx, "ip -n %s link add veth0 type veth peer name veth1 netns %s", NS_TX, NS_RX);
	SYS(del_rx, "ip -n %s addr add %s/24 dev veth0", NS_TX, IP_TX);
	SYS(del_rx, "ip -n %s link set veth0 up", NS_TX);
	SYS(del_rx, "ip -n %s addr add %s/24 dev veth1", NS_RX, IP_RX);
	SYS(del_rx, "ip -n %s link set veth1 up", NS_RX);

	tok = open_netns(NS_TX);
	if (!ASSERT_OK_PTR(tok, "open_netns"))
		goto del_rx;

	SYS(close_ns, "tc qdisc add dev veth0 root handle 1: htb default 1");
	SYS(close_ns, "tc class add dev veth0 parent 1: classid 1:1 htb rate 1mbit ceil 1mbit");

	if (system("tc qdisc add dev veth0 parent 1:1 handle 11: red "
		   "limit 500000 avpkt 1000 probability 1 min 5000 max 6000 "
		   "burst 6 qevent early_drop block 10 2>/dev/null")) {
		test__skip();
		goto close_ns;
	}

	if (system("tc filter add block 10 bpf da object-pinned "
		   PIN_PATH " 2>/dev/null")) {
		test__skip();
		goto close_ns;
	}

	blast_udp();
	ASSERT_GT(*counter, 0, "qevent classifier ran");
close_ns:
	close_netns(tok);
del_rx:
	SYS_NOFAIL("ip netns del %s", NS_RX);
del_tx:
	SYS_NOFAIL("ip netns del %s", NS_TX);
unpin:
	bpf_program__unpin(prog, PIN_PATH);
}

void test_tc_qevent(void)
{
	struct test_tc_qevent *skel;

	skel = test_tc_qevent__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;

	if (test__start_subtest("redirect_verdict"))
		run_qevent_redirect(skel->progs.qevent_redirect_verdict,
				    &skel->bss->verdict_calls);
	if (test__start_subtest("redirect_helper"))
		run_qevent_redirect(skel->progs.qevent_redirect_helper,
				    &skel->bss->helper_calls);

	test_tc_qevent__destroy(skel);
}
