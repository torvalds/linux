// SPDX-License-Identifier: GPL-2.0
/*
 * The test checks that both active and passive reset have correct TCP-AO
 * signature. An "active" reset (abort) here is procured from closing
 * listen() socket with non-accepted connections in the queue:
 * inet_csk_listen_stop() => inet_child_forget() =>
 *                        => tcp_disconnect() => tcp_send_active_reset()
 *
 * The passive reset is quite hard to get on established TCP connections.
 * It could be procured from non-established states, but the synchronization
 * part from userspace in order to reliably get RST seems uneasy.
 * So, instead it's procured by corrupting SEQ number on TIMED-WAIT state.
 *
 * It's important to test both passive and active RST as they go through
 * different code-paths:
 * - tcp_send_active_reset() makes no-data skb, sends it with tcp_transmit_skb()
 * - tcp_v*_send_reset() create their reply skbs and send them with
 *   ip_send_unicast_reply()
 *
 * In both cases TCP-AO signatures have to be correct, which is verified by
 * (1) checking that the TCP-AO connection was reset and (2) TCP-AO counters.
 *
 * Author: Dmitry Safonov <dima@arista.com>
 */
#include <inttypes.h>
#include "../../../../include/linux/kernel.h"
#include "aolib.h"

const size_t quota = 1000;
const size_t packet_sz = 100;
/*
 * Backlog == 0 means 1 connection in queue, see:
 * commit 64a146513f8f ("[NET]: Revert incorrect accept queue...")
 */
const unsigned int backlog;

static void netstats_check(struct netstat *before, struct netstat *after,
			   char *msg)
{
	uint64_t before_cnt, after_cnt;

	before_cnt = netstat_get(before, "TCPAORequired", NULL);
	after_cnt = netstat_get(after, "TCPAORequired", NULL);
	if (after_cnt > before_cnt)
		test_fail("Segments without AO sign (%s): %" PRIu64 " => %" PRIu64,
			  msg, before_cnt, after_cnt);
	else
		test_ok("No segments without AO sign (%s)", msg);

	before_cnt = netstat_get(before, "TCPAOGood", NULL);
	after_cnt = netstat_get(after, "TCPAOGood", NULL);
	if (after_cnt <= before_cnt)
		test_fail("Signed AO segments (%s): %" PRIu64 " => %" PRIu64,
			  msg, before_cnt, after_cnt);
	else
		test_ok("Signed AO segments (%s): %" PRIu64 " => %" PRIu64,
			  msg, before_cnt, after_cnt);

	before_cnt = netstat_get(before, "TCPAOBad", NULL);
	after_cnt = netstat_get(after, "TCPAOBad", NULL);
	if (after_cnt > before_cnt)
		test_fail("Segments with bad AO sign (%s): %" PRIu64 " => %" PRIu64,
			  msg, before_cnt, after_cnt);
	else
		test_ok("No segments with bad AO sign (%s)", msg);
}

/*
 * Another way to send RST, but not through tcp_v{4,6}_send_reset()
 * is tcp_send_active_reset(), that is not in reply to inbound segment,
 * but rather active send. It uses tcp_transmit_skb(), so that should
 * work, but as it also sends RST - nice that it can be covered as well.
 */
static void close_forced(int sk)
{
	struct linger sl;

	sl.l_onoff = 1;
	sl.l_linger = 0;
	if (setsockopt(sk, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)))
		test_error("setsockopt(SO_LINGER)");
	close(sk);
}

static void test_server_active_rst(unsigned int port)
{
	struct tcp_ao_counters cnt1, cnt2;
	ssize_t bytes;
	int sk, lsk;

	lsk = test_listen_socket(this_ip_addr, port, backlog);
	if (test_add_key(lsk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");
	if (test_get_tcp_ao_counters(lsk, &cnt1))
		test_error("test_get_tcp_ao_counters()");

	synchronize_threads(); /* 1: MKT added */
	if (test_wait_fd(lsk, TEST_TIMEOUT_SEC, 0))
		test_error("test_wait_fd()");

	sk = accept(lsk, NULL, NULL);
	if (sk < 0)
		test_error("accept()");

	synchronize_threads(); /* 2: connection accept()ed, another queued */
	if (test_get_tcp_ao_counters(lsk, &cnt2))
		test_error("test_get_tcp_ao_counters()");

	synchronize_threads(); /* 3: close listen socket */
	close(lsk);
	bytes = test_server_run(sk, quota, 0);
	if (bytes != quota)
		test_error("servered only %zd bytes", bytes);
	else
		test_ok("servered %zd bytes", bytes);

	synchronize_threads(); /* 4: finishing up */
	close_forced(sk);

	synchronize_threads(); /* 5: closed active sk */

	synchronize_threads(); /* 6: counters checks */
	if (test_tcp_ao_counters_cmp("active RST server", &cnt1, &cnt2, TEST_CNT_GOOD))
		test_fail("MKT counters (server) have not only good packets");
	else
		test_ok("MKT counters are good on server");
}

static void test_server_passive_rst(unsigned int port)
{
	struct tcp_ao_counters ao1, ao2;
	int sk, lsk;
	ssize_t bytes;

	lsk = test_listen_socket(this_ip_addr, port, 1);

	if (test_add_key(lsk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	synchronize_threads(); /* 1: MKT added => connect() */
	if (test_wait_fd(lsk, TEST_TIMEOUT_SEC, 0))
		test_error("test_wait_fd()");

	sk = accept(lsk, NULL, NULL);
	if (sk < 0)
		test_error("accept()");

	synchronize_threads(); /* 2: accepted => send data */
	close(lsk);
	if (test_get_tcp_ao_counters(sk, &ao1))
		test_error("test_get_tcp_ao_counters()");

	bytes = test_server_run(sk, quota, TEST_TIMEOUT_SEC);
	if (bytes != quota) {
		if (bytes > 0)
			test_fail("server served: %zd", bytes);
		else
			test_fail("server returned %zd", bytes);
	}

	synchronize_threads(); /* 3: checkpoint the client */
	synchronize_threads(); /* 4: close the server, creating twsk */
	if (test_get_tcp_ao_counters(sk, &ao2))
		test_error("test_get_tcp_ao_counters()");
	close(sk);

	synchronize_threads(); /* 5: restore the socket, send more data */
	test_tcp_ao_counters_cmp("passive RST server", &ao1, &ao2, TEST_CNT_GOOD);

	synchronize_threads(); /* 6: server exits */
}

static void *server_fn(void *arg)
{
	struct netstat *ns_before, *ns_after;
	unsigned int port = test_server_port;

	ns_before = netstat_read();

	test_server_active_rst(port++);
	test_server_passive_rst(port++);

	ns_after = netstat_read();
	netstats_check(ns_before, ns_after, "server");
	netstat_free(ns_after);
	netstat_free(ns_before);
	synchronize_threads(); /* exit */

	synchronize_threads(); /* don't race to exit() - client exits */
	return NULL;
}

static int test_wait_fds(int sk[], size_t nr, bool is_writable[],
			 ssize_t wait_for, time_t sec)
{
	struct timeval tv = { .tv_sec = sec };
	struct timeval *ptv = NULL;
	fd_set left;
	size_t i;
	int ret;

	FD_ZERO(&left);
	for (i = 0; i < nr; i++) {
		FD_SET(sk[i], &left);
		if (is_writable)
			is_writable[i] = false;
	}

	if (sec)
		ptv = &tv;

	do {
		bool is_empty = true;
		fd_set fds, efds;
		int nfd = 0;

		FD_ZERO(&fds);
		FD_ZERO(&efds);
		for (i = 0; i < nr; i++) {
			if (!FD_ISSET(sk[i], &left))
				continue;

			if (sk[i] > nfd)
				nfd = sk[i];

			FD_SET(sk[i], &fds);
			FD_SET(sk[i], &efds);
			is_empty = false;
		}
		if (is_empty)
			return -ENOENT;

		errno = 0;
		ret = select(nfd + 1, NULL, &fds, &efds, ptv);
		if (ret < 0)
			return -errno;
		if (!ret)
			return -ETIMEDOUT;
		for (i = 0; i < nr; i++) {
			if (FD_ISSET(sk[i], &fds)) {
				if (is_writable)
					is_writable[i] = true;
				FD_CLR(sk[i], &left);
				wait_for--;
				continue;
			}
			if (FD_ISSET(sk[i], &efds)) {
				FD_CLR(sk[i], &left);
				wait_for--;
			}
		}
	} while (wait_for > 0);

	return 0;
}

static void test_client_active_rst(unsigned int port)
{
	int i, sk[3], err;
	bool is_writable[ARRAY_SIZE(sk)] = {false};
	unsigned int last = ARRAY_SIZE(sk) - 1;

	for (i = 0; i < ARRAY_SIZE(sk); i++) {
		sk[i] = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
		if (sk[i] < 0)
			test_error("socket()");
		if (test_add_key(sk[i], DEFAULT_TEST_PASSWORD,
				 this_ip_dest, -1, 100, 100))
			test_error("setsockopt(TCP_AO_ADD_KEY)");
	}

	synchronize_threads(); /* 1: MKT added */
	for (i = 0; i < last; i++) {
		err = _test_connect_socket(sk[i], this_ip_dest, port,
					       (i == 0) ? TEST_TIMEOUT_SEC : -1);
		if (err < 0)
			test_error("failed to connect()");
	}

	synchronize_threads(); /* 2: two connections: one accept()ed, another queued */
	err = test_wait_fds(sk, last, is_writable, last, TEST_TIMEOUT_SEC);
	if (err < 0)
		test_error("test_wait_fds(): %d", err);

	/* async connect() with third sk to get into request_sock_queue */
	err = _test_connect_socket(sk[last], this_ip_dest, port, -1);
	if (err < 0)
		test_error("failed to connect()");

	synchronize_threads(); /* 3: close listen socket */
	if (test_client_verify(sk[0], packet_sz, quota / packet_sz, TEST_TIMEOUT_SEC))
		test_fail("Failed to send data on connected socket");
	else
		test_ok("Verified established tcp connection");

	synchronize_threads(); /* 4: finishing up */

	synchronize_threads(); /* 5: closed active sk */
	/*
	 * Wait for 2 connections: one accepted, another in the accept queue,
	 * the one in request_sock_queue won't get fully established, so
	 * doesn't receive an active RST, see inet_csk_listen_stop().
	 */
	err = test_wait_fds(sk, last, NULL, last, TEST_TIMEOUT_SEC);
	if (err < 0)
		test_error("select(): %d", err);

	for (i = 0; i < ARRAY_SIZE(sk); i++) {
		socklen_t slen = sizeof(err);

		if (getsockopt(sk[i], SOL_SOCKET, SO_ERROR, &err, &slen))
			test_error("getsockopt()");
		if (is_writable[i] && err != ECONNRESET) {
			test_fail("sk[%d] = %d, err = %d, connection wasn't reset",
				  i, sk[i], err);
		} else {
			test_ok("sk[%d] = %d%s", i, sk[i],
				is_writable[i] ? ", connection was reset" : "");
		}
	}
	synchronize_threads(); /* 6: counters checks */
}

static void test_client_passive_rst(unsigned int port)
{
	struct tcp_ao_counters ao1, ao2;
	struct tcp_ao_repair ao_img;
	struct tcp_sock_state img;
	sockaddr_af saddr;
	int sk, err;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	synchronize_threads(); /* 1: MKT added => connect() */
	if (test_connect_socket(sk, this_ip_dest, port) <= 0)
		test_error("failed to connect()");

	synchronize_threads(); /* 2: accepted => send data */
	if (test_client_verify(sk, packet_sz, quota / packet_sz, TEST_TIMEOUT_SEC))
		test_fail("Failed to send data on connected socket");
	else
		test_ok("Verified established tcp connection");

	synchronize_threads(); /* 3: checkpoint the client */
	test_enable_repair(sk);
	test_sock_checkpoint(sk, &img, &saddr);
	test_ao_checkpoint(sk, &ao_img);
	test_disable_repair(sk);

	synchronize_threads(); /* 4: close the server, creating twsk */

	/*
	 * The "corruption" in SEQ has to be small enough to fit into TCP
	 * window, see tcp_timewait_state_process() for out-of-window
	 * segments.
	 */
	img.out.seq += 5; /* 5 is more noticeable in tcpdump than 1 */

	/*
	 * FIXME: This is kind-of ugly and dirty, but it works.
	 *
	 * At this moment, the server has close'ed(sk).
	 * The passive RST that is being targeted here is new data after
	 * half-duplex close, see tcp_timewait_state_process() => TCP_TW_RST
	 *
	 * What is needed here is:
	 * (1) wait for FIN from the server
	 * (2) make sure that the ACK from the client went out
	 * (3) make sure that the ACK was received and processed by the server
	 *
	 * Otherwise, the data that will be sent from "repaired" socket
	 * post SEQ corruption may get to the server before it's in
	 * TCP_FIN_WAIT2.
	 *
	 * (1) is easy with select()/poll()
	 * (2) is possible by polling tcpi_state from TCP_INFO
	 * (3) is quite complex: as server's socket was already closed,
	 *     probably the way to do it would be tcp-diag.
	 */
	sleep(TEST_RETRANSMIT_SEC);

	synchronize_threads(); /* 5: restore the socket, send more data */
	test_kill_sk(sk);

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	test_enable_repair(sk);
	test_sock_restore(sk, &img, &saddr, this_ip_dest, port);
	if (test_add_repaired_key(sk, DEFAULT_TEST_PASSWORD, 0, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");
	test_ao_restore(sk, &ao_img);

	if (test_get_tcp_ao_counters(sk, &ao1))
		test_error("test_get_tcp_ao_counters()");

	test_disable_repair(sk);
	test_sock_state_free(&img);

	/*
	 * This is how "passive reset" is acquired in this test from TCP_TW_RST:
	 *
	 * IP 10.0.254.1.7011 > 10.0.1.1.59772: Flags [P.], seq 901:1001, ack 1001, win 249,
	 *    options [tcp-ao keyid 100 rnextkeyid 100 mac 0x10217d6c36a22379086ef3b1], length 100
	 * IP 10.0.254.1.7011 > 10.0.1.1.59772: Flags [F.], seq 1001, ack 1001, win 249,
	 *    options [tcp-ao keyid 100 rnextkeyid 100 mac 0x104ffc99b98c10a5298cc268], length 0
	 * IP 10.0.1.1.59772 > 10.0.254.1.7011: Flags [.], ack 1002, win 251,
	 *    options [tcp-ao keyid 100 rnextkeyid 100 mac 0xe496dd4f7f5a8a66873c6f93,nop,nop,sack 1 {1001:1002}], length 0
	 * IP 10.0.1.1.59772 > 10.0.254.1.7011: Flags [P.], seq 1006:1106, ack 1001, win 251,
	 *    options [tcp-ao keyid 100 rnextkeyid 100 mac 0x1b5f3330fb23fbcd0c77d0ca], length 100
	 * IP 10.0.254.1.7011 > 10.0.1.1.59772: Flags [R], seq 3215596252, win 0,
	 *    options [tcp-ao keyid 100 rnextkeyid 100 mac 0x0bcfbbf497bce844312304b2], length 0
	 */
	err = test_client_verify(sk, packet_sz, quota / packet_sz, 2 * TEST_TIMEOUT_SEC);
	/* Make sure that the connection was reset, not timeouted */
	if (err && err == -ECONNRESET)
		test_ok("client sock was passively reset post-seq-adjust");
	else if (err)
		test_fail("client sock was not reset post-seq-adjust: %d", err);
	else
		test_fail("client sock is yet connected post-seq-adjust");

	if (test_get_tcp_ao_counters(sk, &ao2))
		test_error("test_get_tcp_ao_counters()");

	synchronize_threads(); /* 6: server exits */
	close(sk);
	test_tcp_ao_counters_cmp("client passive RST", &ao1, &ao2, TEST_CNT_GOOD);
}

static void *client_fn(void *arg)
{
	struct netstat *ns_before, *ns_after;
	unsigned int port = test_server_port;

	ns_before = netstat_read();

	test_client_active_rst(port++);
	test_client_passive_rst(port++);

	ns_after = netstat_read();
	netstats_check(ns_before, ns_after, "client");
	netstat_free(ns_after);
	netstat_free(ns_before);

	synchronize_threads(); /* exit */
	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(14, server_fn, client_fn);
	return 0;
}
