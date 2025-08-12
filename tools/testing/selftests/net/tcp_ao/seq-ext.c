// SPDX-License-Identifier: GPL-2.0
/* Check that after SEQ number wrap-around:
 * 1. SEQ-extension has upper bytes set
 * 2. TCP connection is alive and no TCPAOBad segments
 * In order to test (2), the test doesn't just adjust seq number for a queue
 * on a connected socket, but migrates it to another sk+port number, so
 * that there won't be any delayed packets that will fail to verify
 * with the new SEQ numbers.
 */
#include <inttypes.h>
#include "aolib.h"

const unsigned int nr_packets = 1000;
const unsigned int msg_len = 1000;
const unsigned int quota = nr_packets * msg_len;
unsigned int client_new_port;

/* Move them closer to roll-over */
static void test_adjust_seqs(struct tcp_sock_state *img,
			     struct tcp_ao_repair *ao_img,
			     bool server)
{
	uint32_t new_seq1, new_seq2;

	/* make them roll-over during quota, but on different segments */
	if (server) {
		new_seq1 = ((uint32_t)-1) - msg_len;
		new_seq2 = ((uint32_t)-1) - (quota - 2 * msg_len);
	} else {
		new_seq1 = ((uint32_t)-1) - (quota - 2 * msg_len);
		new_seq2 = ((uint32_t)-1) - msg_len;
	}

	img->in.seq = new_seq1;
	img->trw.snd_wl1 = img->in.seq - msg_len;
	img->out.seq = new_seq2;
	img->trw.rcv_wup = img->in.seq;
}

static int test_sk_restore(struct tcp_sock_state *img,
			   struct tcp_ao_repair *ao_img, sockaddr_af *saddr,
			   const union tcp_addr daddr, unsigned int dport,
			   struct tcp_counters *cnt)
{
	int sk;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	test_enable_repair(sk);
	test_sock_restore(sk, img, saddr, daddr, dport);
	if (test_add_repaired_key(sk, DEFAULT_TEST_PASSWORD, 0, daddr, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");
	test_ao_restore(sk, ao_img);

	if (test_get_tcp_counters(sk, cnt))
		test_error("test_get_tcp_counters()");

	test_disable_repair(sk);
	test_sock_state_free(img);
	return sk;
}

static void *server_fn(void *arg)
{
	uint64_t before_good, after_good, after_bad;
	struct tcp_counters cnt1, cnt2;
	struct tcp_sock_state img;
	struct tcp_ao_repair ao_img;
	sockaddr_af saddr;
	ssize_t bytes;
	int sk, lsk;

	lsk = test_listen_socket(this_ip_addr, test_server_port, 1);

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
		if (bytes > 0)
			test_fail("server served: %zd", bytes);
		else
			test_fail("server returned: %zd", bytes);
		goto out;
	}

	before_good = netstat_get_one("TCPAOGood", NULL);

	synchronize_threads(); /* 3: restore the connection on another port */

	test_enable_repair(sk);
	test_sock_checkpoint(sk, &img, &saddr);
	test_ao_checkpoint(sk, &ao_img);
	test_kill_sk(sk);
#ifdef IPV6_TEST
	saddr.sin6_port = htons(ntohs(saddr.sin6_port) + 1);
#else
	saddr.sin_port = htons(ntohs(saddr.sin_port) + 1);
#endif
	test_adjust_seqs(&img, &ao_img, true);
	synchronize_threads(); /* 4: dump finished */
	sk = test_sk_restore(&img, &ao_img, &saddr, this_ip_dest,
			     client_new_port, &cnt1);

	trace_ao_event_sne_expect(TCP_AO_SND_SNE_UPDATE, this_ip_addr,
			this_ip_dest, test_server_port + 1, client_new_port, 1);
	trace_ao_event_sne_expect(TCP_AO_SND_SNE_UPDATE, this_ip_dest,
			this_ip_addr, client_new_port, test_server_port + 1, 1);
	trace_ao_event_sne_expect(TCP_AO_RCV_SNE_UPDATE, this_ip_addr,
			this_ip_dest, test_server_port + 1, client_new_port, 1);
	trace_ao_event_sne_expect(TCP_AO_RCV_SNE_UPDATE, this_ip_dest,
			this_ip_addr, client_new_port, test_server_port + 1, 1);
	synchronize_threads(); /* 5: verify the connection during SEQ-number rollover */
	bytes = test_server_run(sk, quota, TEST_TIMEOUT_SEC);
	if (bytes != quota) {
		if (bytes > 0)
			test_fail("server served: %zd", bytes);
		else
			test_fail("server returned: %zd", bytes);
	} else {
		test_ok("server alive");
	}

	synchronize_threads(); /* 6: verify counters after SEQ-number rollover */
	if (test_get_tcp_counters(sk, &cnt2))
		test_error("test_get_tcp_counters()");
	after_good = netstat_get_one("TCPAOGood", NULL);

	test_assert_counters(NULL, &cnt1, &cnt2, TEST_CNT_GOOD);

	if (after_good <= before_good) {
		test_fail("TCPAOGood counter did not increase: %" PRIu64 " <= %" PRIu64,
			  after_good, before_good);
	} else {
		test_ok("TCPAOGood counter increased %" PRIu64 " => %" PRIu64,
			before_good, after_good);
	}
	after_bad = netstat_get_one("TCPAOBad", NULL);
	if (after_bad)
		test_fail("TCPAOBad counter is non-zero: %" PRIu64, after_bad);
	else
		test_ok("TCPAOBad counter didn't increase");
	test_enable_repair(sk);
	test_ao_checkpoint(sk, &ao_img);
	if (ao_img.snd_sne && ao_img.rcv_sne) {
		test_ok("SEQ extension incremented: %u/%u",
			ao_img.snd_sne, ao_img.rcv_sne);
	} else {
		test_fail("SEQ extension was not incremented: %u/%u",
			  ao_img.snd_sne, ao_img.rcv_sne);
	}

	synchronize_threads(); /* 6: verified => closed */
out:
	close(sk);
	return NULL;
}

static void *client_fn(void *arg)
{
	uint64_t before_good, after_good, after_bad;
	struct tcp_counters cnt1, cnt2;
	struct tcp_sock_state img;
	struct tcp_ao_repair ao_img;
	sockaddr_af saddr;
	int sk;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, -1, 100, 100))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	synchronize_threads(); /* 1: MKT added => connect() */
	if (test_connect_socket(sk, this_ip_dest, test_server_port) <= 0)
		test_error("failed to connect()");

	synchronize_threads(); /* 2: accepted => send data */
	if (test_client_verify(sk, msg_len, nr_packets)) {
		test_fail("pre-migrate verify failed");
		return NULL;
	}

	before_good = netstat_get_one("TCPAOGood", NULL);

	synchronize_threads(); /* 3: restore the connection on another port */
	test_enable_repair(sk);
	test_sock_checkpoint(sk, &img, &saddr);
	test_ao_checkpoint(sk, &ao_img);
	test_kill_sk(sk);
#ifdef IPV6_TEST
	client_new_port = ntohs(saddr.sin6_port) + 1;
	saddr.sin6_port = htons(ntohs(saddr.sin6_port) + 1);
#else
	client_new_port = ntohs(saddr.sin_port) + 1;
	saddr.sin_port = htons(ntohs(saddr.sin_port) + 1);
#endif
	test_adjust_seqs(&img, &ao_img, false);
	synchronize_threads(); /* 4: dump finished */
	sk = test_sk_restore(&img, &ao_img, &saddr, this_ip_dest,
			     test_server_port + 1, &cnt1);

	synchronize_threads(); /* 5: verify the connection during SEQ-number rollover */
	if (test_client_verify(sk, msg_len, nr_packets))
		test_fail("post-migrate verify failed");
	else
		test_ok("post-migrate connection alive");

	synchronize_threads(); /* 5: verify counters after SEQ-number rollover */
	if (test_get_tcp_counters(sk, &cnt2))
		test_error("test_get_tcp_counters()");
	after_good = netstat_get_one("TCPAOGood", NULL);

	test_assert_counters(NULL, &cnt1, &cnt2, TEST_CNT_GOOD);

	if (after_good <= before_good) {
		test_fail("TCPAOGood counter did not increase: %" PRIu64 " <= %" PRIu64,
			  after_good, before_good);
	} else {
		test_ok("TCPAOGood counter increased %" PRIu64 " => %" PRIu64,
			before_good, after_good);
	}
	after_bad = netstat_get_one("TCPAOBad", NULL);
	if (after_bad)
		test_fail("TCPAOBad counter is non-zero: %" PRIu64, after_bad);
	else
		test_ok("TCPAOBad counter didn't increase");

	synchronize_threads(); /* 6: verified => closed */
	close(sk);

	synchronize_threads(); /* don't race to exit: let server exit() */
	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(8, server_fn, client_fn);
	return 0;
}
