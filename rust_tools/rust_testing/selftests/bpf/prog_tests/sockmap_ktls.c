// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare
/*
 * Tests for sockmap/sockhash holding kTLS sockets.
 */
#include <error.h>
#include <netinet/tcp.h>
#include <linux/tls.h>
#include "test_progs.h"
#include "sockmap_helpers.h"
#include "test_skmsg_load_helpers.skel.h"
#include "test_sockmap_ktls.skel.h"

#define MAX_TEST_NAME 80
#define TCP_ULP 31

static int init_ktls_pairs(int c, int p)
{
	int err;
	struct tls12_crypto_info_aes_gcm_128 crypto_rx;
	struct tls12_crypto_info_aes_gcm_128 crypto_tx;

	err = setsockopt(c, IPPROTO_TCP, TCP_ULP, "tls", strlen("tls"));
	if (!ASSERT_OK(err, "setsockopt(TCP_ULP)"))
		goto out;

	err = setsockopt(p, IPPROTO_TCP, TCP_ULP, "tls", strlen("tls"));
	if (!ASSERT_OK(err, "setsockopt(TCP_ULP)"))
		goto out;

	memset(&crypto_rx, 0, sizeof(crypto_rx));
	memset(&crypto_tx, 0, sizeof(crypto_tx));
	crypto_rx.info.version = TLS_1_2_VERSION;
	crypto_tx.info.version = TLS_1_2_VERSION;
	crypto_rx.info.cipher_type = TLS_CIPHER_AES_GCM_128;
	crypto_tx.info.cipher_type = TLS_CIPHER_AES_GCM_128;

	err = setsockopt(c, SOL_TLS, TLS_TX, &crypto_tx, sizeof(crypto_tx));
	if (!ASSERT_OK(err, "setsockopt(TLS_TX)"))
		goto out;

	err = setsockopt(p, SOL_TLS, TLS_RX, &crypto_rx, sizeof(crypto_rx));
	if (!ASSERT_OK(err, "setsockopt(TLS_RX)"))
		goto out;
	return 0;
out:
	return -1;
}

static int create_ktls_pairs(int family, int sotype, int *c, int *p)
{
	int err;

	err = create_pair(family, sotype, c, p);
	if (!ASSERT_OK(err, "create_pair()"))
		return -1;

	err = init_ktls_pairs(*c, *p);
	if (!ASSERT_OK(err, "init_ktls_pairs(c, p)"))
		return -1;
	return 0;
}

static void test_sockmap_ktls_update_fails_when_sock_has_ulp(int family, int map)
{
	struct sockaddr_storage addr = {};
	socklen_t len = sizeof(addr);
	struct sockaddr_in6 *v6;
	struct sockaddr_in *v4;
	int err, s, zero = 0;

	switch (family) {
	case AF_INET:
		v4 = (struct sockaddr_in *)&addr;
		v4->sin_family = AF_INET;
		break;
	case AF_INET6:
		v6 = (struct sockaddr_in6 *)&addr;
		v6->sin6_family = AF_INET6;
		break;
	default:
		PRINT_FAIL("unsupported socket family %d", family);
		return;
	}

	s = socket(family, SOCK_STREAM, 0);
	if (!ASSERT_GE(s, 0, "socket"))
		return;

	err = bind(s, (struct sockaddr *)&addr, len);
	if (!ASSERT_OK(err, "bind"))
		goto close;

	err = getsockname(s, (struct sockaddr *)&addr, &len);
	if (!ASSERT_OK(err, "getsockname"))
		goto close;

	err = connect(s, (struct sockaddr *)&addr, len);
	if (!ASSERT_OK(err, "connect"))
		goto close;

	/* save sk->sk_prot and set it to tls_prots */
	err = setsockopt(s, IPPROTO_TCP, TCP_ULP, "tls", strlen("tls"));
	if (!ASSERT_OK(err, "setsockopt(TCP_ULP)"))
		goto close;

	/* sockmap update should not affect saved sk_prot */
	err = bpf_map_update_elem(map, &zero, &s, BPF_ANY);
	if (!ASSERT_ERR(err, "sockmap update elem"))
		goto close;

	/* call sk->sk_prot->setsockopt to dispatch to saved sk_prot */
	err = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &zero, sizeof(zero));
	ASSERT_OK(err, "setsockopt(TCP_NODELAY)");

close:
	close(s);
}

static const char *fmt_test_name(const char *subtest_name, int family,
				 enum bpf_map_type map_type)
{
	const char *map_type_str = BPF_MAP_TYPE_SOCKMAP ? "SOCKMAP" : "SOCKHASH";
	const char *family_str = AF_INET ? "IPv4" : "IPv6";
	static char test_name[MAX_TEST_NAME];

	snprintf(test_name, MAX_TEST_NAME,
		 "sockmap_ktls %s %s %s",
		 subtest_name, family_str, map_type_str);

	return test_name;
}

static void test_sockmap_ktls_offload(int family, int sotype)
{
	int err;
	int c = 0, p = 0, sent, recvd;
	char msg[12] = "hello world\0";
	char rcv[13];

	err = create_ktls_pairs(family, sotype, &c, &p);
	if (!ASSERT_OK(err, "create_ktls_pairs()"))
		goto out;

	sent = send(c, msg, sizeof(msg), 0);
	if (!ASSERT_OK(err, "send(msg)"))
		goto out;

	recvd = recv(p, rcv, sizeof(rcv), 0);
	if (!ASSERT_OK(err, "recv(msg)") ||
	    !ASSERT_EQ(recvd, sent, "length mismatch"))
		goto out;

	ASSERT_OK(memcmp(msg, rcv, sizeof(msg)), "data mismatch");

out:
	if (c)
		close(c);
	if (p)
		close(p);
}

static void test_sockmap_ktls_tx_cork(int family, int sotype, bool push)
{
	int err, off;
	int i, j;
	int start_push = 0, push_len = 0;
	int c = 0, p = 0, one = 1, sent, recvd;
	int prog_fd, map_fd;
	char msg[12] = "hello world\0";
	char rcv[20] = {0};
	struct test_sockmap_ktls *skel;

	skel = test_sockmap_ktls__open_and_load();
	if (!ASSERT_TRUE(skel, "open ktls skel"))
		return;

	err = create_pair(family, sotype, &c, &p);
	if (!ASSERT_OK(err, "create_pair()"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.prog_sk_policy);
	map_fd = bpf_map__fd(skel->maps.sock_map);

	err = bpf_prog_attach(prog_fd, map_fd, BPF_SK_MSG_VERDICT, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach sk msg"))
		goto out;

	err = bpf_map_update_elem(map_fd, &one, &c, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(c)"))
		goto out;

	err = init_ktls_pairs(c, p);
	if (!ASSERT_OK(err, "init_ktls_pairs(c, p)"))
		goto out;

	skel->bss->cork_byte = sizeof(msg);
	if (push) {
		start_push = 1;
		push_len = 2;
	}
	skel->bss->push_start = start_push;
	skel->bss->push_end = push_len;

	off = sizeof(msg) / 2;
	sent = send(c, msg, off, 0);
	if (!ASSERT_EQ(sent, off, "send(msg)"))
		goto out;

	recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT, 1);
	if (!ASSERT_EQ(-1, recvd, "expected no data"))
		goto out;

	/* send remaining msg */
	sent = send(c, msg + off, sizeof(msg) - off, 0);
	if (!ASSERT_EQ(sent, sizeof(msg) - off, "send remaining data"))
		goto out;

	recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT, 1);
	if (!ASSERT_OK(err, "recv(msg)") ||
	    !ASSERT_EQ(recvd, sizeof(msg) + push_len, "check length mismatch"))
		goto out;

	for (i = 0, j = 0; i < recvd;) {
		/* skip checking the data that has been pushed in */
		if (i >= start_push && i <= start_push + push_len - 1) {
			i++;
			continue;
		}
		if (!ASSERT_EQ(rcv[i], msg[j], "data mismatch"))
			goto out;
		i++;
		j++;
	}
out:
	if (c)
		close(c);
	if (p)
		close(p);
	test_sockmap_ktls__destroy(skel);
}

static void test_sockmap_ktls_tx_no_buf(int family, int sotype, bool push)
{
	int c = -1, p = -1, one = 1, two = 2;
	struct test_sockmap_ktls *skel;
	unsigned char *data = NULL;
	struct msghdr msg = {0};
	struct iovec iov[2];
	int prog_fd, map_fd;
	int txrx_buf = 1024;
	int iov_length = 8192;
	int err;

	skel = test_sockmap_ktls__open_and_load();
	if (!ASSERT_TRUE(skel, "open ktls skel"))
		return;

	err = create_pair(family, sotype, &c, &p);
	if (!ASSERT_OK(err, "create_pair()"))
		goto out;

	err = setsockopt(c, SOL_SOCKET, SO_RCVBUFFORCE, &txrx_buf, sizeof(int));
	err |= setsockopt(p, SOL_SOCKET, SO_SNDBUFFORCE, &txrx_buf, sizeof(int));
	if (!ASSERT_OK(err, "set buf limit"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.prog_sk_policy_redir);
	map_fd = bpf_map__fd(skel->maps.sock_map);

	err = bpf_prog_attach(prog_fd, map_fd, BPF_SK_MSG_VERDICT, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach sk msg"))
		goto out;

	err = bpf_map_update_elem(map_fd, &one, &c, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(c)"))
		goto out;

	err = bpf_map_update_elem(map_fd, &two, &p, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(p)"))
		goto out;

	skel->bss->apply_bytes = 1024;

	err = init_ktls_pairs(c, p);
	if (!ASSERT_OK(err, "init_ktls_pairs(c, p)"))
		goto out;

	data = calloc(iov_length, sizeof(char));
	if (!data)
		goto out;

	iov[0].iov_base = data;
	iov[0].iov_len = iov_length;
	iov[1].iov_base = data;
	iov[1].iov_len = iov_length;
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	for (;;) {
		err = sendmsg(c, &msg, MSG_DONTWAIT);
		if (err <= 0)
			break;
	}

out:
	if (data)
		free(data);
	if (c != -1)
		close(c);
	if (p != -1)
		close(p);

	test_sockmap_ktls__destroy(skel);
}

static void test_sockmap_ktls_tx_pop(int family, int sotype)
{
	char msg[37] = "0123456789abcdefghijklmnopqrstuvwxyz\0";
	int c = 0, p = 0, one = 1, sent, recvd;
	struct test_sockmap_ktls *skel;
	int prog_fd, map_fd;
	char rcv[50] = {0};
	int err;
	int i, m, r;

	skel = test_sockmap_ktls__open_and_load();
	if (!ASSERT_TRUE(skel, "open ktls skel"))
		return;

	err = create_pair(family, sotype, &c, &p);
	if (!ASSERT_OK(err, "create_pair()"))
		goto out;

	prog_fd = bpf_program__fd(skel->progs.prog_sk_policy);
	map_fd = bpf_map__fd(skel->maps.sock_map);

	err = bpf_prog_attach(prog_fd, map_fd, BPF_SK_MSG_VERDICT, 0);
	if (!ASSERT_OK(err, "bpf_prog_attach sk msg"))
		goto out;

	err = bpf_map_update_elem(map_fd, &one, &c, BPF_NOEXIST);
	if (!ASSERT_OK(err, "bpf_map_update_elem(c)"))
		goto out;

	err = init_ktls_pairs(c, p);
	if (!ASSERT_OK(err, "init_ktls_pairs(c, p)"))
		goto out;

	struct {
		int	pop_start;
		int	pop_len;
	} pop_policy[] = {
		/* trim the start */
		{0, 2},
		{0, 10},
		{1, 2},
		{1, 10},
		/* trim the end */
		{35, 2},
		/* New entries should be added before this line */
		{-1, -1},
	};

	i = 0;
	while (pop_policy[i].pop_start >= 0) {
		skel->bss->pop_start = pop_policy[i].pop_start;
		skel->bss->pop_end =  pop_policy[i].pop_len;

		sent = send(c, msg, sizeof(msg), 0);
		if (!ASSERT_EQ(sent, sizeof(msg), "send(msg)"))
			goto out;

		recvd = recv_timeout(p, rcv, sizeof(rcv), MSG_DONTWAIT, 1);
		if (!ASSERT_EQ(recvd, sizeof(msg) - pop_policy[i].pop_len, "pop len mismatch"))
			goto out;

		/* verify the data
		 * msg: 0123456789a bcdefghij klmnopqrstuvwxyz
		 *                  |       |
		 *                  popped data
		 */
		for (m = 0, r = 0; m < sizeof(msg);) {
			/* skip checking the data that has been popped */
			if (m >= pop_policy[i].pop_start &&
			    m <= pop_policy[i].pop_start + pop_policy[i].pop_len - 1) {
				m++;
				continue;
			}

			if (!ASSERT_EQ(msg[m], rcv[r], "data mismatch"))
				goto out;
			m++;
			r++;
		}
		i++;
	}
out:
	if (c)
		close(c);
	if (p)
		close(p);
	test_sockmap_ktls__destroy(skel);
}

static void run_tests(int family, enum bpf_map_type map_type)
{
	int map;

	map = bpf_map_create(map_type, NULL, sizeof(int), sizeof(int), 1, NULL);
	if (!ASSERT_GE(map, 0, "bpf_map_create"))
		return;

	if (test__start_subtest(fmt_test_name("update_fails_when_sock_has_ulp", family, map_type)))
		test_sockmap_ktls_update_fails_when_sock_has_ulp(family, map);

	close(map);
}

static void run_ktls_test(int family, int sotype)
{
	if (test__start_subtest("tls simple offload"))
		test_sockmap_ktls_offload(family, sotype);
	if (test__start_subtest("tls tx cork"))
		test_sockmap_ktls_tx_cork(family, sotype, false);
	if (test__start_subtest("tls tx cork with push"))
		test_sockmap_ktls_tx_cork(family, sotype, true);
	if (test__start_subtest("tls tx egress with no buf"))
		test_sockmap_ktls_tx_no_buf(family, sotype, true);
	if (test__start_subtest("tls tx with pop"))
		test_sockmap_ktls_tx_pop(family, sotype);
}

void test_sockmap_ktls(void)
{
	run_tests(AF_INET, BPF_MAP_TYPE_SOCKMAP);
	run_tests(AF_INET, BPF_MAP_TYPE_SOCKHASH);
	run_tests(AF_INET6, BPF_MAP_TYPE_SOCKMAP);
	run_tests(AF_INET6, BPF_MAP_TYPE_SOCKHASH);
	run_ktls_test(AF_INET, SOCK_STREAM);
	run_ktls_test(AF_INET6, SOCK_STREAM);
}
