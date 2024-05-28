// SPDX-License-Identifier: GPL-2.0
/* Author: Dmitry Safonov <dima@arista.com> */
/* This is over-simplified TCP_REPAIR for TCP_ESTABLISHED sockets
 * It tests that TCP-AO enabled connection can be restored.
 * For the proper socket repair see:
 * https://github.com/checkpoint-restore/criu/blob/criu-dev/soccr/soccr.h
 */
#include <inttypes.h>
#include "aolib.h"

const size_t nr_packets = 20;
const size_t msg_len = 100;
const size_t quota = nr_packets * msg_len;
#define fault(type)	(inj == FAULT_ ## type)

static void try_server_run(const char *tst_name, unsigned int port,
			   fault_t inj, test_cnt cnt_expected)
{
	const char *cnt_name = "TCPAOGood";
	struct tcp_ao_counters ao1, ao2;
	uint64_t before_cnt, after_cnt;
	int sk, lsk;
	time_t timeout;
	ssize_t bytes;

	if (fault(TIMEOUT))
		cnt_name = "TCPAOBad";
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

	bytes = test_server_run(sk, quota, TEST_TIMEOUT_SEC);
	if (bytes != quota) {
		test_fail("%s: server served: %zd", tst_name, bytes);
		goto out;
	}

	before_cnt = netstat_get_one(cnt_name, NULL);
	if (test_get_tcp_ao_counters(sk, &ao1))
		test_error("test_get_tcp_ao_counters()");

	timeout = fault(TIMEOUT) ? TEST_RETRANSMIT_SEC : TEST_TIMEOUT_SEC;
	bytes = test_server_run(sk, quota, timeout);
	if (fault(TIMEOUT)) {
		if (bytes > 0)
			test_fail("%s: server served: %zd", tst_name, bytes);
		else
			test_ok("%s: server couldn't serve", tst_name);
	} else {
		if (bytes != quota)
			test_fail("%s: server served: %zd", tst_name, bytes);
		else
			test_ok("%s: server alive", tst_name);
	}
	if (test_get_tcp_ao_counters(sk, &ao2))
		test_error("test_get_tcp_ao_counters()");
	after_cnt = netstat_get_one(cnt_name, NULL);

	test_tcp_ao_counters_cmp(tst_name, &ao1, &ao2, cnt_expected);

	if (after_cnt <= before_cnt) {
		test_fail("%s: %s counter did not increase: %zu <= %zu",
				tst_name, cnt_name, after_cnt, before_cnt);
	} else {
		test_ok("%s: counter %s increased %zu => %zu",
			tst_name, cnt_name, before_cnt, after_cnt);
	}

	/*
	 * Before close() as that will send FIN and move the peer in TCP_CLOSE
	 * and that will prevent reading AO counters from the peer's socket.
	 */
	synchronize_threads(); /* 3: verified => closed */
out:
	close(sk);
}

static void *server_fn(void *arg)
{
	unsigned int port = test_server_port;

	try_server_run("TCP-AO migrate to another socket", port++,
		       0, TEST_CNT_GOOD);
	try_server_run("TCP-AO with wrong send ISN", port++,
		       FAULT_TIMEOUT, TEST_CNT_BAD);
	try_server_run("TCP-AO with wrong receive ISN", port++,
		       FAULT_TIMEOUT, TEST_CNT_BAD);
	try_server_run("TCP-AO with wrong send SEQ ext number", port++,
		       FAULT_TIMEOUT, TEST_CNT_BAD);
	try_server_run("TCP-AO with wrong receive SEQ ext number", port++,
		       FAULT_TIMEOUT, TEST_CNT_NS_BAD | TEST_CNT_GOOD);

	synchronize_threads(); /* don't race to exit: client exits */
	return NULL;
}

static void test_get_sk_checkpoint(unsigned int server_port, sockaddr_af *saddr,
				   struct tcp_sock_state *img,
				   struct tcp_ao_repair *ao_img)
{
	int sk;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	synchronize_threads(); /* 1: MKT added => connect() */
	if (test_connect_socket(sk, this_ip_dest, server_port) <= 0)
		test_error("failed to connect()");

	synchronize_threads(); /* 2: accepted => send data */
	if (test_client_verify(sk, msg_len, nr_packets, TEST_TIMEOUT_SEC))
		test_fail("pre-migrate verify failed");

	test_enable_repair(sk);
	test_sock_checkpoint(sk, img, saddr);
	test_ao_checkpoint(sk, ao_img);
	test_kill_sk(sk);
}

static void test_sk_restore(const char *tst_name, unsigned int server_port,
			    sockaddr_af *saddr, struct tcp_sock_state *img,
			    struct tcp_ao_repair *ao_img,
			    fault_t inj, test_cnt cnt_expected)
{
	const char *cnt_name = "TCPAOGood";
	struct tcp_ao_counters ao1, ao2;
	uint64_t before_cnt, after_cnt;
	time_t timeout;
	int sk;

	if (fault(TIMEOUT))
		cnt_name = "TCPAOBad";

	before_cnt = netstat_get_one(cnt_name, NULL);
	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	test_enable_repair(sk);
	test_sock_restore(sk, img, saddr, this_ip_dest, server_port);
	if (test_add_repaired_key(sk, DEFAULT_TEST_PASSWORD, 0, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");
	test_ao_restore(sk, ao_img);

	if (test_get_tcp_ao_counters(sk, &ao1))
		test_error("test_get_tcp_ao_counters()");

	test_disable_repair(sk);
	test_sock_state_free(img);

	timeout = fault(TIMEOUT) ? TEST_RETRANSMIT_SEC : TEST_TIMEOUT_SEC;
	if (test_client_verify(sk, msg_len, nr_packets, timeout)) {
		if (fault(TIMEOUT))
			test_ok("%s: post-migrate connection is broken", tst_name);
		else
			test_fail("%s: post-migrate connection is working", tst_name);
	} else {
		if (fault(TIMEOUT))
			test_fail("%s: post-migrate connection still working", tst_name);
		else
			test_ok("%s: post-migrate connection is alive", tst_name);
	}
	if (test_get_tcp_ao_counters(sk, &ao2))
		test_error("test_get_tcp_ao_counters()");
	after_cnt = netstat_get_one(cnt_name, NULL);

	test_tcp_ao_counters_cmp(tst_name, &ao1, &ao2, cnt_expected);

	if (after_cnt <= before_cnt) {
		test_fail("%s: %s counter did not increase: %zu <= %zu",
				tst_name, cnt_name, after_cnt, before_cnt);
	} else {
		test_ok("%s: counter %s increased %zu => %zu",
			tst_name, cnt_name, before_cnt, after_cnt);
	}
	synchronize_threads(); /* 3: verified => closed */
	close(sk);
}

static void *client_fn(void *arg)
{
	unsigned int port = test_server_port;
	struct tcp_sock_state tcp_img;
	struct tcp_ao_repair ao_img;
	sockaddr_af saddr;

	test_get_sk_checkpoint(port, &saddr, &tcp_img, &ao_img);
	test_sk_restore("TCP-AO migrate to another socket", port++,
			&saddr, &tcp_img, &ao_img, 0, TEST_CNT_GOOD);

	test_get_sk_checkpoint(port, &saddr, &tcp_img, &ao_img);
	ao_img.snt_isn += 1;
	test_sk_restore("TCP-AO with wrong send ISN", port++,
			&saddr, &tcp_img, &ao_img, FAULT_TIMEOUT, TEST_CNT_BAD);

	test_get_sk_checkpoint(port, &saddr, &tcp_img, &ao_img);
	ao_img.rcv_isn += 1;
	test_sk_restore("TCP-AO with wrong receive ISN", port++,
			&saddr, &tcp_img, &ao_img, FAULT_TIMEOUT, TEST_CNT_BAD);

	test_get_sk_checkpoint(port, &saddr, &tcp_img, &ao_img);
	ao_img.snd_sne += 1;
	test_sk_restore("TCP-AO with wrong send SEQ ext number", port++,
			&saddr, &tcp_img, &ao_img, FAULT_TIMEOUT,
			TEST_CNT_NS_BAD | TEST_CNT_GOOD);

	test_get_sk_checkpoint(port, &saddr, &tcp_img, &ao_img);
	ao_img.rcv_sne += 1;
	test_sk_restore("TCP-AO with wrong receive SEQ ext number", port++,
			&saddr, &tcp_img, &ao_img, FAULT_TIMEOUT,
			TEST_CNT_NS_GOOD | TEST_CNT_BAD);

	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(20, server_fn, client_fn);
	return 0;
}
