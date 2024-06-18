// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/*
 * Test suite of lwt BPF programs that reroutes packets
 *   The file tests focus not only if these programs work as expected normally,
 *   but also if they can handle abnormal situations gracefully. This test
 *   suite currently only covers lwt_xmit hook. lwt_in tests have not been
 *   implemented.
 *
 * WARNING
 * -------
 *  This test suite can crash the kernel, thus should be run in a VM.
 *
 * Setup:
 * ---------
 *  all tests are performed in a single netns. A lwt encap route is setup for
 *  each subtest:
 *
 *    ip route add 10.0.0.0/24 encap bpf xmit <obj> sec "<section_N>" dev link_err
 *
 *  Here <obj> is statically defined to test_lwt_reroute.bpf.o, and it contains
 *  a single test program entry. This program sets packet mark by last byte of
 *  the IPv4 daddr. For example, a packet going to 1.2.3.4 will receive a skb
 *  mark 4. A packet will only be marked once, and IP x.x.x.0 will be skipped
 *  to avoid route loop. We didn't use generated BPF skeleton since the
 *  attachment for lwt programs are not supported by libbpf yet.
 *
 *  The test program will bring up a tun device, and sets up the following
 *  routes:
 *
 *    ip rule add pref 100 from all fwmark <tun_index> lookup 100
 *    ip route add table 100 default dev tun0
 *
 *  For normal testing, a ping command is running in the test netns:
 *
 *    ping 10.0.0.<tun_index> -c 1 -w 1 -s 100
 *
 *  For abnormal testing, fq is used as the qdisc of the tun device. Then a UDP
 *  socket will try to overflow the fq queue and trigger qdisc drop error.
 *
 * Scenarios:
 * --------------------------------
 *  1. Reroute to a running tun device
 *  2. Reroute to a device where qdisc drop
 *
 *  For case 1, ping packets should be received by the tun device.
 *
 *  For case 2, force UDP packets to overflow fq limit. As long as kernel
 *  is not crashed, it is considered successful.
 */
#define NETNS "ns_lwt_reroute"
#include "lwt_helpers.h"
#include "network_helpers.h"
#include <linux/net_tstamp.h>

#define BPF_OBJECT            "test_lwt_reroute.bpf.o"
#define LOCAL_SRC             "10.0.0.1"
#define TEST_CIDR             "10.0.0.0/24"
#define XMIT_HOOK             "xmit"
#define XMIT_SECTION          "lwt_xmit"
#define NSEC_PER_SEC          1000000000ULL

/* send a ping to be rerouted to the target device */
static void ping_once(const char *ip)
{
	/* We won't get a reply. Don't fail here */
	SYS_NOFAIL("ping %s -c1 -W1 -s %d",
		   ip, ICMP_PAYLOAD_SIZE);
}

/* Send snd_target UDP packets to overflow the fq queue and trigger qdisc drop
 * error. This is done via TX tstamp to force buffering delayed packets.
 */
static int overflow_fq(int snd_target, const char *target_ip)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(1234),
	};

	char data_buf[8]; /* only #pkts matter, so use a random small buffer */
	char control_buf[CMSG_SPACE(sizeof(uint64_t))];
	struct iovec iov = {
		.iov_base = data_buf,
		.iov_len = sizeof(data_buf),
	};
	int err = -1;
	int s = -1;
	struct sock_txtime txtime_on = {
		.clockid = CLOCK_MONOTONIC,
		.flags = 0,
	};
	struct msghdr msg = {
		.msg_name = &addr,
		.msg_namelen = sizeof(addr),
		.msg_control = control_buf,
		.msg_controllen = sizeof(control_buf),
		.msg_iovlen = 1,
		.msg_iov = &iov,
	};
	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);

	memset(data_buf, 0, sizeof(data_buf));

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (!ASSERT_GE(s, 0, "socket"))
		goto out;

	err = setsockopt(s, SOL_SOCKET, SO_TXTIME, &txtime_on, sizeof(txtime_on));
	if (!ASSERT_OK(err, "setsockopt(SO_TXTIME)"))
		goto out;

	err = inet_pton(AF_INET, target_ip, &addr.sin_addr);
	if (!ASSERT_EQ(err, 1, "inet_pton"))
		goto out;

	while (snd_target > 0) {
		struct timespec now;

		memset(control_buf, 0, sizeof(control_buf));
		cmsg->cmsg_type = SCM_TXTIME;
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_len = CMSG_LEN(sizeof(uint64_t));

		err = clock_gettime(CLOCK_MONOTONIC, &now);
		if (!ASSERT_OK(err, "clock_gettime(CLOCK_MONOTONIC)")) {
			err = -1;
			goto out;
		}

		*(uint64_t *)CMSG_DATA(cmsg) = (now.tv_nsec + 1) * NSEC_PER_SEC +
					       now.tv_nsec;

		/* we will intentionally send more than fq limit, so ignore
		 * the error here.
		 */
		sendmsg(s, &msg, MSG_NOSIGNAL);
		snd_target--;
	}

	/* no kernel crash so far is considered success */
	err = 0;

out:
	if (s >= 0)
		close(s);

	return err;
}

static int setup(const char *tun_dev)
{
	int target_index = -1;
	int tap_fd = -1;

	tap_fd = open_tuntap(tun_dev, false);
	if (!ASSERT_GE(tap_fd, 0, "open_tun"))
		return -1;

	target_index = if_nametoindex(tun_dev);
	if (!ASSERT_GE(target_index, 0, "if_nametoindex"))
		return -1;

	SYS(fail, "ip link add link_err type dummy");
	SYS(fail, "ip link set lo up");
	SYS(fail, "ip addr add dev lo " LOCAL_SRC "/32");
	SYS(fail, "ip link set link_err up");
	SYS(fail, "ip link set %s up", tun_dev);

	SYS(fail, "ip route add %s dev link_err encap bpf xmit obj %s sec lwt_xmit",
	    TEST_CIDR, BPF_OBJECT);

	SYS(fail, "ip rule add pref 100 from all fwmark %d lookup 100",
	    target_index);
	SYS(fail, "ip route add t 100 default dev %s", tun_dev);

	return tap_fd;

fail:
	if (tap_fd >= 0)
		close(tap_fd);
	return -1;
}

static void test_lwt_reroute_normal_xmit(void)
{
	const char *tun_dev = "tun0";
	int tun_fd = -1;
	int ifindex = -1;
	char ip[256];
	struct timeval timeo = {
		.tv_sec = 0,
		.tv_usec = 250000,
	};

	tun_fd = setup(tun_dev);
	if (!ASSERT_GE(tun_fd, 0, "setup_reroute"))
		return;

	ifindex = if_nametoindex(tun_dev);
	if (!ASSERT_GE(ifindex, 0, "if_nametoindex"))
		return;

	snprintf(ip, 256, "10.0.0.%d", ifindex);

	/* ping packets should be received by the tun device */
	ping_once(ip);

	if (!ASSERT_EQ(wait_for_packet(tun_fd, __expect_icmp_ipv4, &timeo), 1,
		       "wait_for_packet"))
		log_err("%s xmit", __func__);
}

/*
 * Test the failure case when the skb is dropped at the qdisc. This is a
 * regression prevention at the xmit hook only.
 */
static void test_lwt_reroute_qdisc_dropped(void)
{
	const char *tun_dev = "tun0";
	int tun_fd = -1;
	int ifindex = -1;
	char ip[256];

	tun_fd = setup(tun_dev);
	if (!ASSERT_GE(tun_fd, 0, "setup_reroute"))
		goto fail;

	SYS(fail, "tc qdisc replace dev %s root fq limit 5 flow_limit 5", tun_dev);

	ifindex = if_nametoindex(tun_dev);
	if (!ASSERT_GE(ifindex, 0, "if_nametoindex"))
		return;

	snprintf(ip, 256, "10.0.0.%d", ifindex);
	ASSERT_EQ(overflow_fq(10, ip), 0, "overflow_fq");

fail:
	if (tun_fd >= 0)
		close(tun_fd);
}

static void *test_lwt_reroute_run(void *arg)
{
	netns_delete();
	RUN_TEST(lwt_reroute_normal_xmit);
	RUN_TEST(lwt_reroute_qdisc_dropped);
	return NULL;
}

void test_lwt_reroute(void)
{
	pthread_t test_thread;
	int err;

	/* Run the tests in their own thread to isolate the namespace changes
	 * so they do not affect the environment of other tests.
	 * (specifically needed because of unshare(CLONE_NEWNS) in open_netns())
	 */
	err = pthread_create(&test_thread, NULL, &test_lwt_reroute_run, NULL);
	if (ASSERT_OK(err, "pthread_create"))
		ASSERT_OK(pthread_join(test_thread, NULL), "pthread_join");
}
