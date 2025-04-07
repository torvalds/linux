// SPDX-License-Identifier: GPL-2.0
/* Author: Dmitry Safonov <dima@arista.com> */
#include <inttypes.h>
#include "aolib.h"

static union tcp_addr local_addr;

static void __setup_lo_intf(const char *lo_intf,
			    const char *addr_str, uint8_t prefix)
{
	if (inet_pton(TEST_FAMILY, addr_str, &local_addr) != 1)
		test_error("Can't convert local ip address");

	if (ip_addr_add(lo_intf, TEST_FAMILY, local_addr, prefix))
		test_error("Failed to add %s ip address", lo_intf);

	if (link_set_up(lo_intf))
		test_error("Failed to bring %s up", lo_intf);

	if (ip_route_add(lo_intf, TEST_FAMILY, local_addr, local_addr))
		test_error("Failed to add a local route %s", lo_intf);
}

static void setup_lo_intf(const char *lo_intf)
{
#ifdef IPV6_TEST
	__setup_lo_intf(lo_intf, "::1", 128);
#else
	__setup_lo_intf(lo_intf, "127.0.0.1", 8);
#endif
}

static void tcp_self_connect(const char *tst, unsigned int port,
			     bool different_keyids, bool check_restore)
{
	struct tcp_counters before, after;
	uint64_t before_aogood, after_aogood;
	struct netstat *ns_before, *ns_after;
	const size_t nr_packets = 20;
	struct tcp_ao_repair ao_img;
	struct tcp_sock_state img;
	sockaddr_af addr;
	int sk;

	tcp_addr_to_sockaddr_in(&addr, &local_addr, htons(port));

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	if (different_keyids) {
		if (test_add_key(sk, DEFAULT_TEST_PASSWORD, local_addr, -1, 5, 7))
			test_error("setsockopt(TCP_AO_ADD_KEY)");
		if (test_add_key(sk, DEFAULT_TEST_PASSWORD, local_addr, -1, 7, 5))
			test_error("setsockopt(TCP_AO_ADD_KEY)");
	} else {
		if (test_add_key(sk, DEFAULT_TEST_PASSWORD, local_addr, -1, 100, 100))
			test_error("setsockopt(TCP_AO_ADD_KEY)");
	}

	if (bind(sk, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		test_error("bind()");

	ns_before = netstat_read();
	before_aogood = netstat_get(ns_before, "TCPAOGood", NULL);
	if (test_get_tcp_counters(sk, &before))
		test_error("test_get_tcp_counters()");

	if (__test_connect_socket(sk, "lo", (struct sockaddr *)&addr,
				  sizeof(addr), 0) < 0) {
		ns_after = netstat_read();
		netstat_print_diff(ns_before, ns_after);
		test_error("failed to connect()");
	}

	if (test_client_verify(sk, 100, nr_packets)) {
		test_fail("%s: tcp connection verify failed", tst);
		close(sk);
		return;
	}

	ns_after = netstat_read();
	after_aogood = netstat_get(ns_after, "TCPAOGood", NULL);
	if (test_get_tcp_counters(sk, &after))
		test_error("test_get_tcp_counters()");
	if (!check_restore) {
		/* to debug: netstat_print_diff(ns_before, ns_after); */
		netstat_free(ns_before);
	}
	netstat_free(ns_after);

	if (after_aogood <= before_aogood) {
		test_fail("%s: TCPAOGood counter mismatch: %" PRIu64 " <= %" PRIu64,
			  tst, after_aogood, before_aogood);
		close(sk);
		return;
	}

	if (test_assert_counters(tst, &before, &after, TEST_CNT_GOOD)) {
		close(sk);
		return;
	}

	if (!check_restore) {
		test_ok("%s: connect TCPAOGood %" PRIu64 " => %" PRIu64,
				tst, before_aogood, after_aogood);
		close(sk);
		return;
	}

	test_enable_repair(sk);
	test_sock_checkpoint(sk, &img, &addr);
#ifdef IPV6_TEST
	addr.sin6_port = htons(port + 1);
#else
	addr.sin_port = htons(port + 1);
#endif
	test_ao_checkpoint(sk, &ao_img);
	test_kill_sk(sk);

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	test_enable_repair(sk);
	__test_sock_restore(sk, "lo", &img, &addr, &addr, sizeof(addr));
	if (different_keyids) {
		if (test_add_repaired_key(sk, DEFAULT_TEST_PASSWORD, 0,
					  local_addr, -1, 7, 5))
			test_error("setsockopt(TCP_AO_ADD_KEY)");
		if (test_add_repaired_key(sk, DEFAULT_TEST_PASSWORD, 0,
					  local_addr, -1, 5, 7))
			test_error("setsockopt(TCP_AO_ADD_KEY)");
	} else {
		if (test_add_repaired_key(sk, DEFAULT_TEST_PASSWORD, 0,
					  local_addr, -1, 100, 100))
			test_error("setsockopt(TCP_AO_ADD_KEY)");
	}
	test_ao_restore(sk, &ao_img);
	test_disable_repair(sk);
	test_sock_state_free(&img);
	if (test_client_verify(sk, 100, nr_packets)) {
		test_fail("%s: tcp connection verify failed", tst);
		close(sk);
		return;
	}
	ns_after = netstat_read();
	after_aogood = netstat_get(ns_after, "TCPAOGood", NULL);
	/* to debug: netstat_print_diff(ns_before, ns_after); */
	netstat_free(ns_before);
	netstat_free(ns_after);
	close(sk);
	if (after_aogood <= before_aogood) {
		test_fail("%s: TCPAOGood counter mismatch: %" PRIu64 " <= %" PRIu64,
			  tst, after_aogood, before_aogood);
		return;
	}
	test_ok("%s: connect TCPAOGood %" PRIu64 " => %" PRIu64,
			tst, before_aogood, after_aogood);
}

static void *client_fn(void *arg)
{
	unsigned int port = test_server_port;

	setup_lo_intf("lo");

	tcp_self_connect("self-connect(same keyids)", port++, false, false);

	/* expecting rnext to change based on the first segment RNext != Current */
	trace_ao_event_expect(TCP_AO_RNEXT_REQUEST, local_addr, local_addr,
			      port, port, 0, -1, -1, -1, -1, -1, 7, 5, -1);
	tcp_self_connect("self-connect(different keyids)", port++, true, false);
	tcp_self_connect("self-connect(restore)", port, false, true);
	port += 2; /* restore test restores over different port */
	trace_ao_event_expect(TCP_AO_RNEXT_REQUEST, local_addr, local_addr,
			      port, port, 0, -1, -1, -1, -1, -1, 7, 5, -1);
	/* intentionally on restore they are added to the socket in different order */
	trace_ao_event_expect(TCP_AO_RNEXT_REQUEST, local_addr, local_addr,
			      port + 1, port + 1, 0, -1, -1, -1, -1, -1, 5, 7, -1);
	tcp_self_connect("self-connect(restore, different keyids)", port, true, true);
	port += 2; /* restore test restores over different port */

	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(5, client_fn, NULL);
	return 0;
}
