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

static void test_sockmap_ktls_enable_fails_when_in_sockmap(int family, int map)
{
	struct tls12_crypto_info_aes_gcm_128 crypto = {
		.info = {
			.version     = TLS_1_2_VERSION,
			.cipher_type = TLS_CIPHER_AES_GCM_128,
		},
	};
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

	/* Add the socket to the sockmap, attaching a psock. */
	err = bpf_map_update_elem(map, &zero, &s, BPF_ANY);
	if (!ASSERT_OK(err, "sockmap update elem"))
		goto close;

	/* Installing the TLS ULP is allowed, it does not touch the datapath. */
	err = setsockopt(s, IPPROTO_TCP, TCP_ULP, "tls", strlen("tls"));
	if (!ASSERT_OK(err, "setsockopt(TCP_ULP)"))
		goto close;

	/* Enabling the TLS crypto datapath must be rejected. */
	err = setsockopt(s, SOL_TLS, TLS_TX, &crypto, sizeof(crypto));
	ASSERT_ERR(err, "setsockopt(TLS_TX)");

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

static void run_tests(int family, enum bpf_map_type map_type)
{
	int map;

	map = bpf_map_create(map_type, NULL, sizeof(int), sizeof(int), 1, NULL);
	if (!ASSERT_GE(map, 0, "bpf_map_create"))
		return;

	if (test__start_subtest(fmt_test_name("update_fails_when_sock_has_ulp", family, map_type)))
		test_sockmap_ktls_update_fails_when_sock_has_ulp(family, map);

	if (test__start_subtest(fmt_test_name("enable_fails_when_in_sockmap", family, map_type)))
		test_sockmap_ktls_enable_fails_when_in_sockmap(family, map);

	close(map);
}

static void run_ktls_test(int family, int sotype)
{
	if (test__start_subtest("tls simple offload"))
		test_sockmap_ktls_offload(family, sotype);
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
