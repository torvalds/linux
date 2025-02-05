// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/*
 * Test suite of lwt_xmit BPF programs that redirect packets
 *   The file tests focus not only if these programs work as expected normally,
 *   but also if they can handle abnormal situations gracefully.
 *
 * WARNING
 * -------
 *  This test suite may crash the kernel, thus should be run in a VM.
 *
 * Setup:
 * ---------
 *  All tests are performed in a single netns. Two lwt encap routes are setup for
 *  each subtest:
 *
 *    ip route add 10.0.0.0/24 encap bpf xmit <obj> sec "<ingress_sec>" dev link_err
 *    ip route add 20.0.0.0/24 encap bpf xmit <obj> sec "<egress_sec>" dev link_err
 *
 *  Here <obj> is statically defined to test_lwt_redirect.bpf.o, and each section
 *  of this object holds a program entry to test. The BPF object is built from
 *  progs/test_lwt_redirect.c. We didn't use generated BPF skeleton since the
 *  attachment for lwt programs are not supported by libbpf yet.
 *
 *  For testing, ping commands are run in the test netns:
 *
 *    ping 10.0.0.<ifindex> -c 1 -w 1 -s 100
 *    ping 20.0.0.<ifindex> -c 1 -w 1 -s 100
 *
 * Scenarios:
 * --------------------------------
 *  1. Redirect to a running tap/tun device
 *  2. Redirect to a down tap/tun device
 *  3. Redirect to a vlan device with lower layer down
 *
 *  Case 1, ping packets should be received by packet socket on target device
 *  when redirected to ingress, and by tun/tap fd when redirected to egress.
 *
 *  Case 2,3 are considered successful as long as they do not crash the kernel
 *  as a regression.
 *
 *  Case 1,2 use tap device to test redirect to device that requires MAC
 *  header, and tun device to test the case with no MAC header added.
 */
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/if_tun.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#define NETNS "ns_lwt_redirect"
#include "lwt_helpers.h"
#include "test_progs.h"
#include "network_helpers.h"

#define BPF_OBJECT            "test_lwt_redirect.bpf.o"
#define INGRESS_SEC(need_mac) ((need_mac) ? "redir_ingress" : "redir_ingress_nomac")
#define EGRESS_SEC(need_mac)  ((need_mac) ? "redir_egress" : "redir_egress_nomac")
#define LOCAL_SRC             "10.0.0.1"
#define CIDR_TO_INGRESS       "10.0.0.0/24"
#define CIDR_TO_EGRESS        "20.0.0.0/24"

/* ping to redirect toward given dev, with last byte of dest IP being the target
 * device index.
 *
 * Note: ping command inside BPF-CI is busybox version, so it does not have certain
 * function, such like -m option to set packet mark.
 */
static void ping_dev(const char *dev, bool is_ingress)
{
	int link_index = if_nametoindex(dev);
	char ip[256];

	if (!ASSERT_GE(link_index, 0, "if_nametoindex"))
		return;

	if (is_ingress)
		snprintf(ip, sizeof(ip), "10.0.0.%d", link_index);
	else
		snprintf(ip, sizeof(ip), "20.0.0.%d", link_index);

	/* We won't get a reply. Don't fail here */
	SYS_NOFAIL("ping %s -c1 -W1 -s %d",
		   ip, ICMP_PAYLOAD_SIZE);
}

static int new_packet_sock(const char *ifname)
{
	int err = 0;
	int ignore_outgoing = 1;
	int ifindex = -1;
	int s = -1;

	s = socket(AF_PACKET, SOCK_RAW, 0);
	if (!ASSERT_GE(s, 0, "socket(AF_PACKET)"))
		return -1;

	ifindex = if_nametoindex(ifname);
	if (!ASSERT_GE(ifindex, 0, "if_nametoindex")) {
		close(s);
		return -1;
	}

	struct sockaddr_ll addr = {
		.sll_family = AF_PACKET,
		.sll_protocol = htons(ETH_P_IP),
		.sll_ifindex = ifindex,
	};

	err = bind(s, (struct sockaddr *)&addr, sizeof(addr));
	if (!ASSERT_OK(err, "bind(AF_PACKET)")) {
		close(s);
		return -1;
	}

	/* Use packet socket to capture only the ingress, so we can distinguish
	 * the case where a regression that actually redirects the packet to
	 * the egress.
	 */
	err = setsockopt(s, SOL_PACKET, PACKET_IGNORE_OUTGOING,
			 &ignore_outgoing, sizeof(ignore_outgoing));
	if (!ASSERT_OK(err, "setsockopt(PACKET_IGNORE_OUTGOING)")) {
		close(s);
		return -1;
	}

	err = fcntl(s, F_SETFL, O_NONBLOCK);
	if (!ASSERT_OK(err, "fcntl(O_NONBLOCK)")) {
		close(s);
		return -1;
	}

	return s;
}

static int expect_icmp(char *buf, ssize_t len)
{
	struct ethhdr *eth = (struct ethhdr *)buf;

	if (len < (ssize_t)sizeof(*eth))
		return -1;

	if (eth->h_proto == htons(ETH_P_IP))
		return __expect_icmp_ipv4((char *)(eth + 1), len - sizeof(*eth));

	return -1;
}

static int expect_icmp_nomac(char *buf, ssize_t len)
{
	return __expect_icmp_ipv4(buf, len);
}

static void send_and_capture_test_packets(const char *test_name, int tap_fd,
					  const char *target_dev, bool need_mac)
{
	int psock = -1;
	struct timeval timeo = {
		.tv_sec = 0,
		.tv_usec = 250000,
	};
	int ret = -1;

	filter_t filter = need_mac ? expect_icmp : expect_icmp_nomac;

	ping_dev(target_dev, false);

	ret = wait_for_packet(tap_fd, filter, &timeo);
	if (!ASSERT_EQ(ret, 1, "wait_for_epacket")) {
		log_err("%s egress test fails", test_name);
		goto out;
	}

	psock = new_packet_sock(target_dev);
	ping_dev(target_dev, true);

	ret = wait_for_packet(psock, filter, &timeo);
	if (!ASSERT_EQ(ret, 1, "wait_for_ipacket")) {
		log_err("%s ingress test fails", test_name);
		goto out;
	}

out:
	if (psock >= 0)
		close(psock);
}

static int setup_redirect_target(const char *target_dev, bool need_mac)
{
	int target_index = -1;
	int tap_fd = -1;

	tap_fd = open_tuntap(target_dev, need_mac);
	if (!ASSERT_GE(tap_fd, 0, "open_tuntap"))
		goto fail;

	target_index = if_nametoindex(target_dev);
	if (!ASSERT_GE(target_index, 0, "if_nametoindex"))
		goto fail;

	SYS(fail, "sysctl -w net.ipv6.conf.all.disable_ipv6=1");
	SYS(fail, "ip link add link_err type dummy");
	SYS(fail, "ip link set lo up");
	SYS(fail, "ip addr add dev lo " LOCAL_SRC "/32");
	SYS(fail, "ip link set link_err up");
	SYS(fail, "ip link set %s up", target_dev);

	SYS(fail, "ip route add %s dev link_err encap bpf xmit obj %s sec %s",
	    CIDR_TO_INGRESS, BPF_OBJECT, INGRESS_SEC(need_mac));

	SYS(fail, "ip route add %s dev link_err encap bpf xmit obj %s sec %s",
	    CIDR_TO_EGRESS, BPF_OBJECT, EGRESS_SEC(need_mac));

	return tap_fd;

fail:
	if (tap_fd >= 0)
		close(tap_fd);
	return -1;
}

static void test_lwt_redirect_normal(void)
{
	const char *target_dev = "tap0";
	int tap_fd = -1;
	bool need_mac = true;

	tap_fd = setup_redirect_target(target_dev, need_mac);
	if (!ASSERT_GE(tap_fd, 0, "setup_redirect_target"))
		return;

	send_and_capture_test_packets(__func__, tap_fd, target_dev, need_mac);
	close(tap_fd);
}

static void test_lwt_redirect_normal_nomac(void)
{
	const char *target_dev = "tun0";
	int tap_fd = -1;
	bool need_mac = false;

	tap_fd = setup_redirect_target(target_dev, need_mac);
	if (!ASSERT_GE(tap_fd, 0, "setup_redirect_target"))
		return;

	send_and_capture_test_packets(__func__, tap_fd, target_dev, need_mac);
	close(tap_fd);
}

/* This test aims to prevent regression of future. As long as the kernel does
 * not panic, it is considered as success.
 */
static void __test_lwt_redirect_dev_down(bool need_mac)
{
	const char *target_dev = "tap0";
	int tap_fd = -1;

	tap_fd = setup_redirect_target(target_dev, need_mac);
	if (!ASSERT_GE(tap_fd, 0, "setup_redirect_target"))
		return;

	SYS(out, "ip link set %s down", target_dev);
	ping_dev(target_dev, true);
	ping_dev(target_dev, false);

out:
	close(tap_fd);
}

static void test_lwt_redirect_dev_down(void)
{
	__test_lwt_redirect_dev_down(true);
}

static void test_lwt_redirect_dev_down_nomac(void)
{
	__test_lwt_redirect_dev_down(false);
}

/* This test aims to prevent regression of future. As long as the kernel does
 * not panic, it is considered as success.
 */
static void test_lwt_redirect_dev_carrier_down(void)
{
	const char *lower_dev = "tap0";
	const char *vlan_dev = "vlan100";
	int tap_fd = -1;

	tap_fd = setup_redirect_target(lower_dev, true);
	if (!ASSERT_GE(tap_fd, 0, "setup_redirect_target"))
		return;

	SYS(out, "ip link add vlan100 link %s type vlan id 100", lower_dev);
	SYS(out, "ip link set %s up", vlan_dev);
	SYS(out, "ip link set %s down", lower_dev);
	ping_dev(vlan_dev, true);
	ping_dev(vlan_dev, false);

out:
	close(tap_fd);
}

static void *test_lwt_redirect_run(void *arg)
{
	netns_delete();
	RUN_TEST(lwt_redirect_normal);
	RUN_TEST(lwt_redirect_normal_nomac);
	RUN_TEST(lwt_redirect_dev_down);
	RUN_TEST(lwt_redirect_dev_down_nomac);
	RUN_TEST(lwt_redirect_dev_carrier_down);
	return NULL;
}

void test_lwt_redirect(void)
{
	pthread_t test_thread;
	int err;

	/* Run the tests in their own thread to isolate the namespace changes
	 * so they do not affect the environment of other tests.
	 * (specifically needed because of unshare(CLONE_NEWNS) in open_netns())
	 */
	err = pthread_create(&test_thread, NULL, &test_lwt_redirect_run, NULL);
	if (ASSERT_OK(err, "pthread_create"))
		ASSERT_OK(pthread_join(test_thread, NULL), "pthread_join");
}
