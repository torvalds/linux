// SPDX-License-Identifier: GPL-2.0
/* Author: Dmitry Safonov <dima@arista.com> */
#include <inttypes.h>
#include "aolib.h"

#define fault(type)	(inj == FAULT_ ## type)
static volatile int sk_pair;

static inline int test_add_key_maclen(int sk, const char *key, uint8_t maclen,
				      union tcp_addr in_addr, uint8_t prefix,
				      uint8_t sndid, uint8_t rcvid)
{
	struct tcp_ao_add tmp = {};
	int err;

	if (prefix > DEFAULT_TEST_PREFIX)
		prefix = DEFAULT_TEST_PREFIX;

	err = test_prepare_key(&tmp, DEFAULT_TEST_ALGO, in_addr, false, false,
			       prefix, 0, sndid, rcvid, maclen,
			       0, strlen(key), key);
	if (err)
		return err;

	err = setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &tmp, sizeof(tmp));
	if (err < 0)
		return -errno;

	return test_verify_socket_key(sk, &tmp);
}

static void try_accept(const char *tst_name, unsigned int port, const char *pwd,
		       union tcp_addr addr, uint8_t prefix,
		       uint8_t sndid, uint8_t rcvid, uint8_t maclen,
		       const char *cnt_name, test_cnt cnt_expected,
		       fault_t inj)
{
	struct tcp_counters cnt1, cnt2;
	uint64_t before_cnt = 0, after_cnt = 0; /* silence GCC */
	test_cnt poll_cnt = (cnt_expected == TEST_CNT_GOOD) ? 0 : cnt_expected;
	int lsk, err, sk = 0;

	lsk = test_listen_socket(this_ip_addr, port, 1);

	if (pwd && test_add_key_maclen(lsk, pwd, maclen, addr, prefix, sndid, rcvid))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	if (cnt_name)
		before_cnt = netstat_get_one(cnt_name, NULL);
	if (pwd && test_get_tcp_counters(lsk, &cnt1))
		test_error("test_get_tcp_counters()");

	synchronize_threads(); /* preparations done */

	err = test_skpair_wait_poll(lsk, 0, poll_cnt, &sk_pair);
	if (err == -ETIMEDOUT) {
		sk_pair = err;
		if (!fault(TIMEOUT))
			test_fail("%s: timed out for accept()", tst_name);
	} else if (err == -EKEYREJECTED) {
		if (!fault(KEYREJECT))
			test_fail("%s: key was rejected", tst_name);
	} else if (err < 0) {
		test_error("test_skpair_wait_poll()");
	} else {
		if (fault(TIMEOUT))
			test_fail("%s: ready to accept", tst_name);

		sk = accept(lsk, NULL, NULL);
		if (sk < 0) {
			test_error("accept()");
		} else {
			if (fault(TIMEOUT))
				test_fail("%s: accepted", tst_name);
		}
	}

	synchronize_threads(); /* before counter checks */
	if (pwd && test_get_tcp_counters(lsk, &cnt2))
		test_error("test_get_tcp_counters()");

	close(lsk);

	if (pwd)
		test_assert_counters(tst_name, &cnt1, &cnt2, cnt_expected);

	if (!cnt_name)
		goto out;

	after_cnt = netstat_get_one(cnt_name, NULL);

	if (after_cnt <= before_cnt) {
		test_fail("%s: %s counter did not increase: %" PRIu64 " <= %" PRIu64,
				tst_name, cnt_name, after_cnt, before_cnt);
	} else {
		test_ok("%s: counter %s increased %" PRIu64  " => %" PRIu64,
			tst_name, cnt_name, before_cnt, after_cnt);
	}

out:
	synchronize_threads(); /* close() */
	if (sk > 0)
		close(sk);
}

static void *server_fn(void *arg)
{
	union tcp_addr wrong_addr, network_addr;
	unsigned int port = test_server_port;

	if (inet_pton(TEST_FAMILY, TEST_WRONG_IP, &wrong_addr) != 1)
		test_error("Can't convert ip address %s", TEST_WRONG_IP);

	try_accept("Non-AO server + AO client", port++, NULL,
		   this_ip_dest, -1, 100, 100, 0,
		   "TCPAOKeyNotFound", TEST_CNT_NS_KEY_NOT_FOUND, FAULT_TIMEOUT);

	try_accept("AO server + Non-AO client", port++, DEFAULT_TEST_PASSWORD,
		   this_ip_dest, -1, 100, 100, 0,
		   "TCPAORequired", TEST_CNT_AO_REQUIRED, FAULT_TIMEOUT);

	try_accept("Wrong password", port++, "something that is not DEFAULT_TEST_PASSWORD",
		   this_ip_dest, -1, 100, 100, 0,
		   "TCPAOBad", TEST_CNT_BAD, FAULT_TIMEOUT);

	try_accept("Wrong rcv id", port++, DEFAULT_TEST_PASSWORD,
		   this_ip_dest, -1, 100, 101, 0,
		   "TCPAOKeyNotFound", TEST_CNT_AO_KEY_NOT_FOUND, FAULT_TIMEOUT);

	try_accept("Wrong snd id", port++, DEFAULT_TEST_PASSWORD,
		   this_ip_dest, -1, 101, 100, 0,
		   "TCPAOGood", TEST_CNT_GOOD, FAULT_TIMEOUT);

	try_accept("Different maclen", port++, DEFAULT_TEST_PASSWORD,
		   this_ip_dest, -1, 100, 100, 8,
		   "TCPAOBad", TEST_CNT_BAD, FAULT_TIMEOUT);

	try_accept("Server: Wrong addr", port++, DEFAULT_TEST_PASSWORD,
		   wrong_addr, -1, 100, 100, 0,
		   "TCPAOKeyNotFound", TEST_CNT_AO_KEY_NOT_FOUND, FAULT_TIMEOUT);

	/* Key rejected by the other side, failing short through skpair */
	try_accept("Client: Wrong addr", port++, NULL,
		   this_ip_dest, -1, 100, 100, 0, NULL, 0, FAULT_KEYREJECT);

	try_accept("rcv id != snd id", port++, DEFAULT_TEST_PASSWORD,
		   this_ip_dest, -1, 200, 100, 0,
		   "TCPAOGood", TEST_CNT_GOOD, 0);

	if (inet_pton(TEST_FAMILY, TEST_NETWORK, &network_addr) != 1)
		test_error("Can't convert ip address %s", TEST_NETWORK);

	try_accept("Server: prefix match", port++, DEFAULT_TEST_PASSWORD,
		   network_addr, 16, 100, 100, 0,
		   "TCPAOGood", TEST_CNT_GOOD, 0);

	try_accept("Client: prefix match", port++, DEFAULT_TEST_PASSWORD,
		   this_ip_dest, -1, 100, 100, 0,
		   "TCPAOGood", TEST_CNT_GOOD, 0);

	/* client exits */
	synchronize_threads();
	return NULL;
}

static void try_connect(const char *tst_name, unsigned int port,
			const char *pwd, union tcp_addr addr, uint8_t prefix,
			uint8_t sndid, uint8_t rcvid,
			test_cnt cnt_expected, fault_t inj)
{
	struct tcp_counters cnt1, cnt2;
	int sk, ret;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	if (pwd && test_add_key(sk, pwd, addr, prefix, sndid, rcvid))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	if (pwd && test_get_tcp_counters(sk, &cnt1))
		test_error("test_get_tcp_counters()");

	synchronize_threads(); /* preparations done */

	ret = test_skpair_connect_poll(sk, this_ip_dest, port, cnt_expected, &sk_pair);
	synchronize_threads(); /* before counter checks */
	if (ret < 0) {
		sk_pair = ret;
		if (fault(KEYREJECT) && ret == -EKEYREJECTED) {
			test_ok("%s: connect() was prevented", tst_name);
		} else if (ret == -ETIMEDOUT && fault(TIMEOUT)) {
			test_ok("%s", tst_name);
		} else if (ret == -ECONNREFUSED &&
				(fault(TIMEOUT) || fault(KEYREJECT))) {
			test_ok("%s: refused to connect", tst_name);
		} else {
			test_error("%s: connect() returned %d", tst_name, ret);
		}
		goto out;
	}

	if (fault(TIMEOUT) || fault(KEYREJECT))
		test_fail("%s: connected", tst_name);
	else
		test_ok("%s: connected", tst_name);
	if (pwd && ret > 0) {
		if (test_get_tcp_counters(sk, &cnt2))
			test_error("test_get_tcp_counters()");
		test_assert_counters(tst_name, &cnt1, &cnt2, cnt_expected);
	} else if (pwd) {
		test_tcp_counters_free(&cnt1);
	}
out:
	synchronize_threads(); /* close() */

	if (ret > 0)
		close(sk);
}

static void *client_fn(void *arg)
{
	union tcp_addr wrong_addr, network_addr, addr_any = {};
	unsigned int port = test_server_port;

	if (inet_pton(TEST_FAMILY, TEST_WRONG_IP, &wrong_addr) != 1)
		test_error("Can't convert ip address %s", TEST_WRONG_IP);

	trace_ao_event_expect(TCP_AO_KEY_NOT_FOUND, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("Non-AO server + AO client", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 100, 0, FAULT_TIMEOUT);

	trace_hash_event_expect(TCP_HASH_AO_REQUIRED, this_ip_addr, this_ip_dest,
				-1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO server + Non-AO client", port++, NULL,
			this_ip_dest, -1, 100, 100, 0, FAULT_TIMEOUT);

	trace_ao_event_expect(TCP_AO_MISMATCH, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("Wrong password", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 100, 0, FAULT_TIMEOUT);

	trace_ao_event_expect(TCP_AO_KEY_NOT_FOUND, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("Wrong rcv id", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 100, 0, FAULT_TIMEOUT);

	/*
	 * XXX: The test doesn't increase any counters, see tcp_make_synack().
	 * Potentially, it can be speed up by setting sk_pair = -ETIMEDOUT
	 * but the price would be increased complexity of the tracer thread.
	 */
	trace_ao_event_sk_expect(TCP_AO_SYNACK_NO_KEY, this_ip_dest, addr_any,
				 port, 0, 100, 100);
	try_connect("Wrong snd id", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 100, 0, FAULT_TIMEOUT);

	trace_ao_event_expect(TCP_AO_WRONG_MACLEN, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("Different maclen", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 100, 0, FAULT_TIMEOUT);

	trace_ao_event_expect(TCP_AO_KEY_NOT_FOUND, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("Server: Wrong addr", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 100, 0, FAULT_TIMEOUT);

	try_connect("Client: Wrong addr", port++, DEFAULT_TEST_PASSWORD,
			wrong_addr, -1, 100, 100, 0, FAULT_KEYREJECT);

	try_connect("rcv id != snd id", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 200, TEST_CNT_GOOD, 0);

	if (inet_pton(TEST_FAMILY, TEST_NETWORK, &network_addr) != 1)
		test_error("Can't convert ip address %s", TEST_NETWORK);

	try_connect("Server: prefix match", port++, DEFAULT_TEST_PASSWORD,
			this_ip_dest, -1, 100, 100, TEST_CNT_GOOD, 0);

	try_connect("Client: prefix match", port++, DEFAULT_TEST_PASSWORD,
			network_addr, 16, 100, 100, TEST_CNT_GOOD, 0);

	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(22, server_fn, client_fn);
	return 0;
}
