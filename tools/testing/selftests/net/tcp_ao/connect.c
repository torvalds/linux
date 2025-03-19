// SPDX-License-Identifier: GPL-2.0
/* Author: Dmitry Safonov <dima@arista.com> */
#include <inttypes.h>
#include "aolib.h"

static void *server_fn(void *arg)
{
	int sk, lsk;
	ssize_t bytes;

	lsk = test_listen_socket(this_ip_addr, test_server_port, 1);

	if (test_add_key(lsk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");
	synchronize_threads();

	if (test_wait_fd(lsk, TEST_TIMEOUT_SEC, 0))
		test_error("test_wait_fd()");

	sk = accept(lsk, NULL, NULL);
	if (sk < 0)
		test_error("accept()");

	synchronize_threads();

	bytes = test_server_run(sk, 0, 0);

	test_fail("server served: %zd", bytes);
	return NULL;
}

static void *client_fn(void *arg)
{
	int sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	uint64_t before_aogood, after_aogood;
	const size_t nr_packets = 20;
	struct netstat *ns_before, *ns_after;
	struct tcp_counters ao1, ao2;

	if (sk < 0)
		test_error("socket()");

	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	synchronize_threads();
	if (test_connect_socket(sk, this_ip_dest, test_server_port) <= 0)
		test_error("failed to connect()");
	synchronize_threads();

	ns_before = netstat_read();
	before_aogood = netstat_get(ns_before, "TCPAOGood", NULL);
	if (test_get_tcp_counters(sk, &ao1))
		test_error("test_get_tcp_counters()");

	if (test_client_verify(sk, 100, nr_packets)) {
		test_fail("verify failed");
		return NULL;
	}

	ns_after = netstat_read();
	after_aogood = netstat_get(ns_after, "TCPAOGood", NULL);
	if (test_get_tcp_counters(sk, &ao2))
		test_error("test_get_tcp_counters()");
	netstat_print_diff(ns_before, ns_after);
	netstat_free(ns_before);
	netstat_free(ns_after);

	if (nr_packets > (after_aogood - before_aogood)) {
		test_fail("TCPAOGood counter mismatch: %zu > (%" PRIu64 " - %" PRIu64 ")",
				nr_packets, after_aogood, before_aogood);
		return NULL;
	}
	if (test_assert_counters("connect", &ao1, &ao2, TEST_CNT_GOOD))
		return NULL;

	test_ok("connect TCPAOGood %" PRIu64 "/%" PRIu64 "/%" PRIu64 " => %" PRIu64 "/%" PRIu64 "/%" PRIu64 ", sent %zu",
			before_aogood, ao1.ao.ao_info_pkt_good,
			ao1.ao.key_cnts[0].pkt_good,
			after_aogood, ao2.ao.ao_info_pkt_good,
			ao2.ao.key_cnts[0].pkt_good,
			nr_packets);
	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(2, server_fn, client_fn);
	return 0;
}
