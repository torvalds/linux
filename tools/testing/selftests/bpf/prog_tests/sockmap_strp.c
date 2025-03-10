// SPDX-License-Identifier: GPL-2.0
#include <error.h>
#include <netinet/tcp.h>
#include <test_progs.h>
#include "sockmap_helpers.h"
#include "test_skmsg_load_helpers.skel.h"
#include "test_sockmap_strp.skel.h"

#define STRP_PKT_HEAD_LEN 4
#define STRP_PKT_BODY_LEN 6
#define STRP_PKT_FULL_LEN (STRP_PKT_HEAD_LEN + STRP_PKT_BODY_LEN)

static const char packet[STRP_PKT_FULL_LEN] = "head+body\0";
static const int test_packet_num = 100;

/* Current implementation of tcp_bpf_recvmsg_parser() invokes data_ready
 * with sk held if an skb exists in sk_receive_queue. Then for the
 * data_ready implementation of strparser, it will delay the read
 * operation if sk is held and EAGAIN is returned.
 */
static int sockmap_strp_consume_pre_data(int p)
{
	int recvd;
	bool retried = false;
	char rcv[10];

retry:
	errno = 0;
	recvd = recv_timeout(p, rcv, sizeof(rcv), 0, 1);
	if (recvd < 0 && errno == EAGAIN && retried == false) {
		/* On the first call, EAGAIN will certainly be returned.
		 * A 1-second wait is enough for the workqueue to finish.
		 */
		sleep(1);
		retried = true;
		goto retry;
	}

	if (!ASSERT_EQ(recvd, STRP_PKT_FULL_LEN, "recv error or truncated data") ||
	    !ASSERT_OK(memcmp(packet, rcv, STRP_PKT_FULL_LEN),
				"data mismatch"))
		return -1;
	return 0;
}

static struct test_sockmap_strp *sockmap_strp_init(int *out_map, bool pass,
						   bool need_parser)
{
	struct test_sockmap_strp *strp = NULL;
	int verdict, parser;
	int err;

	strp = test_sockmap_strp__open_and_load();
	*out_map = bpf_map__fd(strp->maps.sock_map);

	if (need_parser)
		parser = bpf_program__fd(strp->progs.prog_skb_parser_partial);
	else
		parser = bpf_program__fd(strp->progs.prog_skb_parser);

	if (pass)
		verdict = bpf_program__fd(strp->progs.prog_skb_verdict_pass);
	else
		verdict = bpf_program__fd(strp->progs.prog_skb_verdict);

	err = bpf_prog_attach(parser, *out_map, BPF_SK_SKB_STREAM_PARSER, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach stream parser"))
		goto err;

	err = bpf_prog_attach(verdict, *out_map, BPF_SK_SKB_STREAM_VERDICT, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach stream verdict"))
		goto err;

	return strp;
err:
	test_sockmap_strp__destroy(strp);
	return NULL;
}

/* Dispatch packets to different socket by packet size:
 *
 *                      ------  ------
 *                     | pkt4 || pkt1 |... > remote socket
 *  ------ ------     / ------  ------
 * | pkt8 | pkt7 |...
 *  ------ ------     \ ------  ------
 *                     | pkt3 || pkt2 |... > local socket
 *                      ------  ------
 */
static void test_sockmap_strp_dispatch_pkt(int family, int sotype)
{
	int i, j, zero = 0, one = 1, recvd;
	int err, map;
	int c0 = -1, p0 = -1, c1 = -1, p1 = -1;
	struct test_sockmap_strp *strp = NULL;
	int test_cnt = 6;
	char rcv[10];
	struct {
		char	data[7];
		int	data_len;
		int	send_cnt;
		int	*receiver;
	} send_dir[2] = {
		/* data expected to deliver to local */
		{"llllll", 6, 0, &p0},
		/* data expected to deliver to remote */
		{"rrrrr",  5, 0, &c1}
	};

	strp = sockmap_strp_init(&map, false, false);
	if (!ASSERT_TRUE(strp, "sockmap_strp_init"))
		return;

	err = create_socket_pairs(family, sotype, &c0, &c1, &p0, &p1);
	if (!ASSERT_OK(err, "create_socket_pairs()"))
		goto out;

	err = bpf_map_update_elem(map, &zero, &p0, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(p0)"))
		goto out_close;

	err = bpf_map_update_elem(map, &one, &p1, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(p1)"))
		goto out_close;

	err = setsockopt(c1, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
	if (!ASSERT_OK(err, "setsockopt(TCP_NODELAY)"))
		goto out_close;

	/* deliver data with data size greater than 5 to local */
	strp->data->verdict_max_size = 5;

	for (i = 0; i < test_cnt; i++) {
		int d = i % 2;

		xsend(c0, send_dir[d].data, send_dir[d].data_len, 0);
		send_dir[d].send_cnt++;
	}

	for (i = 0; i < 2; i++) {
		for (j = 0; j < send_dir[i].send_cnt; j++) {
			int expected = send_dir[i].data_len;

			recvd = recv_timeout(*send_dir[i].receiver, rcv,
					     expected, MSG_DONTWAIT,
					     IO_TIMEOUT_SEC);
			if (!ASSERT_EQ(recvd, expected, "recv_timeout()"))
				goto out_close;
			if (!ASSERT_OK(memcmp(send_dir[i].data, rcv, recvd),
				       "data mismatch"))
				goto out_close;
		}
	}
out_close:
	close(c0);
	close(c1);
	close(p0);
	close(p1);
out:
	test_sockmap_strp__destroy(strp);
}

/* We have multiple packets in one skb
 * ------------ ------------ ------------
 * |  packet1  |   packet2  |  ...
 * ------------ ------------ ------------
 */
static void test_sockmap_strp_multiple_pkt(int family, int sotype)
{
	int i, zero = 0;
	int sent, recvd, total;
	int err, map;
	int c = -1, p = -1;
	struct test_sockmap_strp *strp = NULL;
	char *snd = NULL, *rcv = NULL;

	strp = sockmap_strp_init(&map, true, true);
	if (!ASSERT_TRUE(strp, "sockmap_strp_init"))
		return;

	err = create_pair(family, sotype, &c, &p);
	if (err)
		goto out;

	err = bpf_map_update_elem(map, &zero, &p, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(zero, p)"))
		goto out_close;

	/* construct multiple packets in one buffer */
	total = test_packet_num * STRP_PKT_FULL_LEN;
	snd = malloc(total);
	rcv = malloc(total + 1);
	if (!ASSERT_TRUE(snd, "malloc(snd)") ||
	    !ASSERT_TRUE(rcv, "malloc(rcv)"))
		goto out_close;

	for (i = 0; i < test_packet_num; i++) {
		memcpy(snd + i * STRP_PKT_FULL_LEN,
		       packet, STRP_PKT_FULL_LEN);
	}

	sent = xsend(c, snd, total, 0);
	if (!ASSERT_EQ(sent, total, "xsend(c)"))
		goto out_close;

	/* try to recv one more byte to avoid truncation check */
	recvd = recv_timeout(p, rcv, total + 1, MSG_DONTWAIT, IO_TIMEOUT_SEC);
	if (!ASSERT_EQ(recvd, total, "recv(rcv)"))
		goto out_close;

	/* we sent TCP segment with multiple encapsulation
	 * then check whether packets are handled correctly
	 */
	if (!ASSERT_OK(memcmp(snd, rcv, total), "data mismatch"))
		goto out_close;

out_close:
	close(c);
	close(p);
	if (snd)
		free(snd);
	if (rcv)
		free(rcv);
out:
	test_sockmap_strp__destroy(strp);
}

/* Test strparser with partial read */
static void test_sockmap_strp_partial_read(int family, int sotype)
{
	int zero = 0, recvd, off;
	int err, map;
	int c = -1, p = -1;
	struct test_sockmap_strp *strp = NULL;
	char rcv[STRP_PKT_FULL_LEN + 1] = "0";

	strp = sockmap_strp_init(&map, true, true);
	if (!ASSERT_TRUE(strp, "sockmap_strp_init"))
		return;

	err = create_pair(family, sotype, &c, &p);
	if (err)
		goto out;

	/* sk_data_ready of 'p' will be replaced by strparser handler */
	err = bpf_map_update_elem(map, &zero, &p, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(zero, p)"))
		goto out_close;

	/* 1.1 send partial head, 1 byte header left */
	off = STRP_PKT_HEAD_LEN - 1;
	xsend(c, packet, off, 0);
	recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT, 1);
	if (!ASSERT_EQ(-1, recvd, "partial head sent, expected no data"))
		goto out_close;

	/* 1.2 send remaining head and body */
	xsend(c, packet + off, STRP_PKT_FULL_LEN - off, 0);
	recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT, IO_TIMEOUT_SEC);
	if (!ASSERT_EQ(recvd, STRP_PKT_FULL_LEN, "expected full data"))
		goto out_close;

	/* 2.1 send partial head, 1 byte header left */
	off = STRP_PKT_HEAD_LEN - 1;
	xsend(c, packet, off, 0);

	/* 2.2 send remaining head and partial body, 1 byte body left */
	xsend(c, packet + off, STRP_PKT_FULL_LEN - off - 1, 0);
	off = STRP_PKT_FULL_LEN - 1;
	recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT, 1);
	if (!ASSERT_EQ(-1, recvd, "partial body sent, expected no data"))
		goto out_close;

	/* 2.3 send remaining body */
	xsend(c, packet + off, STRP_PKT_FULL_LEN - off, 0);
	recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT, IO_TIMEOUT_SEC);
	if (!ASSERT_EQ(recvd, STRP_PKT_FULL_LEN, "expected full data"))
		goto out_close;

out_close:
	close(c);
	close(p);

out:
	test_sockmap_strp__destroy(strp);
}

/* Test simple socket read/write with strparser + FIONREAD */
static void test_sockmap_strp_pass(int family, int sotype, bool fionread)
{
	int zero = 0, pkt_size = STRP_PKT_FULL_LEN, sent, recvd, avail;
	int err, map;
	int c = -1, p = -1;
	int test_cnt = 10, i;
	struct test_sockmap_strp *strp = NULL;
	char rcv[STRP_PKT_FULL_LEN + 1] = "0";

	strp = sockmap_strp_init(&map, true, true);
	if (!ASSERT_TRUE(strp, "sockmap_strp_init"))
		return;

	err = create_pair(family, sotype, &c, &p);
	if (err)
		goto out;

	/* inject some data before bpf process, it should be read
	 * correctly because we check sk_receive_queue in
	 * tcp_bpf_recvmsg_parser().
	 */
	sent = xsend(c, packet, pkt_size, 0);
	if (!ASSERT_EQ(sent, pkt_size, "xsend(pre-data)"))
		goto out_close;

	/* sk_data_ready of 'p' will be replaced by strparser handler */
	err = bpf_map_update_elem(map, &zero, &p, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(p)"))
		goto out_close;

	/* consume previous data we injected */
	if (sockmap_strp_consume_pre_data(p))
		goto out_close;

	/* Previously, we encountered issues such as deadlocks and
	 * sequence errors that resulted in the inability to read
	 * continuously. Therefore, we perform multiple iterations
	 * of testing here.
	 */
	for (i = 0; i < test_cnt; i++) {
		sent = xsend(c, packet, pkt_size, 0);
		if (!ASSERT_EQ(sent, pkt_size, "xsend(c)"))
			goto out_close;

		recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT,
				     IO_TIMEOUT_SEC);
		if (!ASSERT_EQ(recvd, pkt_size, "recv_timeout(p)") ||
		    !ASSERT_OK(memcmp(packet, rcv, pkt_size),
				  "memcmp, data mismatch"))
			goto out_close;
	}

	if (fionread) {
		sent = xsend(c, packet, pkt_size, 0);
		if (!ASSERT_EQ(sent, pkt_size, "second xsend(c)"))
			goto out_close;

		err = ioctl(p, FIONREAD, &avail);
		if (!ASSERT_OK(err, "ioctl(FIONREAD) error") ||
		    !ASSERT_EQ(avail, pkt_size, "ioctl(FIONREAD)"))
			goto out_close;

		recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT,
				     IO_TIMEOUT_SEC);
		if (!ASSERT_EQ(recvd, pkt_size, "second recv_timeout(p)") ||
		    !ASSERT_OK(memcmp(packet, rcv, pkt_size),
			      "second memcmp, data mismatch"))
			goto out_close;
	}

out_close:
	close(c);
	close(p);

out:
	test_sockmap_strp__destroy(strp);
}

/* Test strparser with verdict mode */
static void test_sockmap_strp_verdict(int family, int sotype)
{
	int zero = 0, one = 1, sent, recvd, off;
	int err, map;
	int c0 = -1, p0 = -1, c1 = -1, p1 = -1;
	struct test_sockmap_strp *strp = NULL;
	char rcv[STRP_PKT_FULL_LEN + 1] = "0";

	strp = sockmap_strp_init(&map, false, true);
	if (!ASSERT_TRUE(strp, "sockmap_strp_init"))
		return;

	/* We simulate a reverse proxy server.
	 * When p0 receives data from c0, we forward it to c1.
	 * From c1's perspective, it will consider this data
	 * as being sent by p1.
	 */
	err = create_socket_pairs(family, sotype, &c0, &c1, &p0, &p1);
	if (!ASSERT_OK(err, "create_socket_pairs()"))
		goto out;

	err = bpf_map_update_elem(map, &zero, &p0, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(p0)"))
		goto out_close;

	err = bpf_map_update_elem(map, &one, &p1, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(p1)"))
		goto out_close;

	sent = xsend(c0, packet, STRP_PKT_FULL_LEN, 0);
	if (!ASSERT_EQ(sent, STRP_PKT_FULL_LEN, "xsend(c0)"))
		goto out_close;

	recvd = recv_timeout(c1, rcv, sizeof(rcv), MSG_DONTWAIT,
			     IO_TIMEOUT_SEC);
	if (!ASSERT_EQ(recvd, STRP_PKT_FULL_LEN, "recv_timeout(c1)") ||
	    !ASSERT_OK(memcmp(packet, rcv, STRP_PKT_FULL_LEN),
			  "received data does not match the sent data"))
		goto out_close;

	/* send again to ensure the stream is functioning correctly. */
	sent = xsend(c0, packet, STRP_PKT_FULL_LEN, 0);
	if (!ASSERT_EQ(sent, STRP_PKT_FULL_LEN, "second xsend(c0)"))
		goto out_close;

	/* partial read */
	off = STRP_PKT_FULL_LEN / 2;
	recvd = recv_timeout(c1, rcv, off, MSG_DONTWAIT,
			     IO_TIMEOUT_SEC);
	recvd += recv_timeout(c1, rcv + off, sizeof(rcv) - off, MSG_DONTWAIT,
			      IO_TIMEOUT_SEC);

	if (!ASSERT_EQ(recvd, STRP_PKT_FULL_LEN, "partial recv_timeout(c1)") ||
	    !ASSERT_OK(memcmp(packet, rcv, STRP_PKT_FULL_LEN),
			  "partial received data does not match the sent data"))
		goto out_close;

out_close:
	close(c0);
	close(c1);
	close(p0);
	close(p1);
out:
	test_sockmap_strp__destroy(strp);
}

void test_sockmap_strp(void)
{
	if (test__start_subtest("sockmap strp tcp pass"))
		test_sockmap_strp_pass(AF_INET, SOCK_STREAM, false);
	if (test__start_subtest("sockmap strp tcp v6 pass"))
		test_sockmap_strp_pass(AF_INET6, SOCK_STREAM, false);
	if (test__start_subtest("sockmap strp tcp pass fionread"))
		test_sockmap_strp_pass(AF_INET, SOCK_STREAM, true);
	if (test__start_subtest("sockmap strp tcp v6 pass fionread"))
		test_sockmap_strp_pass(AF_INET6, SOCK_STREAM, true);
	if (test__start_subtest("sockmap strp tcp verdict"))
		test_sockmap_strp_verdict(AF_INET, SOCK_STREAM);
	if (test__start_subtest("sockmap strp tcp v6 verdict"))
		test_sockmap_strp_verdict(AF_INET6, SOCK_STREAM);
	if (test__start_subtest("sockmap strp tcp partial read"))
		test_sockmap_strp_partial_read(AF_INET, SOCK_STREAM);
	if (test__start_subtest("sockmap strp tcp multiple packets"))
		test_sockmap_strp_multiple_pkt(AF_INET, SOCK_STREAM);
	if (test__start_subtest("sockmap strp tcp dispatch"))
		test_sockmap_strp_dispatch_pkt(AF_INET, SOCK_STREAM);
}
