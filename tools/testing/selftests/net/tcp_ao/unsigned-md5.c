// SPDX-License-Identifier: GPL-2.0
/* Author: Dmitry Safonov <dima@arista.com> */
#include <inttypes.h>
#include "aolib.h"

#define fault(type)	(inj == FAULT_ ## type)
static const char *md5_password = "Some evil genius, enemy to mankind, must have been the first contriver.";
static const char *ao_password = DEFAULT_TEST_PASSWORD;

static union tcp_addr client2;
static union tcp_addr client3;

static const int test_vrf_ifindex = 200;
static const uint8_t test_vrf_tabid = 42;
static void setup_vrfs(void)
{
	int err;

	if (!kernel_config_has(KCONFIG_NET_VRF))
		return;

	err = add_vrf("ksft-vrf", test_vrf_tabid, test_vrf_ifindex, -1);
	if (err)
		test_error("Failed to add a VRF: %d", err);

	err = link_set_up("ksft-vrf");
	if (err)
		test_error("Failed to bring up a VRF");

	err = ip_route_add_vrf(veth_name, TEST_FAMILY,
			       this_ip_addr, this_ip_dest, test_vrf_tabid);
	if (err)
		test_error("Failed to add a route to VRF: %d", err);
}

static void try_accept(const char *tst_name, unsigned int port,
		       union tcp_addr *md5_addr, uint8_t md5_prefix,
		       union tcp_addr *ao_addr, uint8_t ao_prefix,
		       bool set_ao_required,
		       uint8_t sndid, uint8_t rcvid, uint8_t vrf,
		       const char *cnt_name, test_cnt cnt_expected,
		       int needs_tcp_md5, fault_t inj)
{
	struct tcp_ao_counters ao_cnt1, ao_cnt2;
	uint64_t before_cnt = 0, after_cnt = 0; /* silence GCC */
	int lsk, err, sk = 0;
	time_t timeout;

	if (needs_tcp_md5 && should_skip_test(tst_name, KCONFIG_TCP_MD5))
		return;

	lsk = test_listen_socket(this_ip_addr, port, 1);

	if (md5_addr && test_set_md5(lsk, *md5_addr, md5_prefix, -1, md5_password))
		test_error("setsockopt(TCP_MD5SIG_EXT)");

	if (ao_addr && test_add_key(lsk, ao_password,
				    *ao_addr, ao_prefix, sndid, rcvid))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	if (set_ao_required && test_set_ao_flags(lsk, true, false))
		test_error("setsockopt(TCP_AO_INFO)");

	if (cnt_name)
		before_cnt = netstat_get_one(cnt_name, NULL);
	if (ao_addr && test_get_tcp_ao_counters(lsk, &ao_cnt1))
		test_error("test_get_tcp_ao_counters()");

	synchronize_threads(); /* preparations done */

	timeout = fault(TIMEOUT) ? TEST_RETRANSMIT_SEC : TEST_TIMEOUT_SEC;
	err = test_wait_fd(lsk, timeout, 0);
	synchronize_threads(); /* connect()/accept() timeouts */
	if (err == -ETIMEDOUT) {
		if (!fault(TIMEOUT))
			test_fail("timed out for accept()");
	} else if (err < 0) {
		test_error("test_wait_fd()");
	} else {
		if (fault(TIMEOUT))
			test_fail("ready to accept");

		sk = accept(lsk, NULL, NULL);
		if (sk < 0) {
			test_error("accept()");
		} else {
			if (fault(TIMEOUT))
				test_fail("%s: accepted", tst_name);
		}
	}

	if (ao_addr && test_get_tcp_ao_counters(lsk, &ao_cnt2))
		test_error("test_get_tcp_ao_counters()");
	close(lsk);

	if (!cnt_name) {
		test_ok("%s: no counter checks", tst_name);
		goto out;
	}

	after_cnt = netstat_get_one(cnt_name, NULL);

	if (after_cnt <= before_cnt) {
		test_fail("%s: %s counter did not increase: %" PRIu64 " <= %" PRIu64,
				tst_name, cnt_name, after_cnt, before_cnt);
	} else {
		test_ok("%s: counter %s increased %" PRIu64 " => %" PRIu64,
			tst_name, cnt_name, before_cnt, after_cnt);
	}
	if (ao_addr)
		test_tcp_ao_counters_cmp(tst_name, &ao_cnt1, &ao_cnt2, cnt_expected);

out:
	synchronize_threads(); /* test_kill_sk() */
	if (sk > 0)
		test_kill_sk(sk);
}

static void server_add_routes(void)
{
	int family = TEST_FAMILY;

	synchronize_threads(); /* client_add_ips() */

	if (ip_route_add(veth_name, family, this_ip_addr, client2))
		test_error("Failed to add route");
	if (ip_route_add(veth_name, family, this_ip_addr, client3))
		test_error("Failed to add route");
}

static void server_add_fail_tests(unsigned int *port)
{
	union tcp_addr addr_any = {};

	try_accept("TCP-AO established: add TCP-MD5 key", (*port)++, NULL, 0,
		   &addr_any, 0, 0, 100, 100, 0, "TCPAOGood", TEST_CNT_GOOD,
		   1, 0);
	try_accept("TCP-MD5 established: add TCP-AO key", (*port)++, &addr_any,
		   0, NULL, 0, 0, 0, 0, 0, NULL, 0, 1, 0);
	try_accept("non-signed established: add TCP-AO key", (*port)++, NULL, 0,
		   NULL, 0, 0, 0, 0, 0, "CurrEstab", 0, 0, 0);
}

static void server_vrf_tests(unsigned int *port)
{
	setup_vrfs();
}

static void *server_fn(void *arg)
{
	unsigned int port = test_server_port;
	union tcp_addr addr_any = {};

	server_add_routes();

	try_accept("AO server (INADDR_ANY): AO client", port++, NULL, 0,
		   &addr_any, 0, 0, 100, 100, 0, "TCPAOGood",
		   TEST_CNT_GOOD, 0, 0);
	try_accept("AO server (INADDR_ANY): MD5 client", port++, NULL, 0,
		   &addr_any, 0, 0, 100, 100, 0, "TCPMD5Unexpected",
		   0, 1, FAULT_TIMEOUT);
	try_accept("AO server (INADDR_ANY): no sign client", port++, NULL, 0,
		   &addr_any, 0, 0, 100, 100, 0, "TCPAORequired",
		   TEST_CNT_AO_REQUIRED, 0, FAULT_TIMEOUT);
	try_accept("AO server (AO_REQUIRED): AO client", port++, NULL, 0,
		   &this_ip_dest, TEST_PREFIX, true,
		   100, 100, 0, "TCPAOGood", TEST_CNT_GOOD, 0, 0);
	try_accept("AO server (AO_REQUIRED): unsigned client", port++, NULL, 0,
		   &this_ip_dest, TEST_PREFIX, true,
		   100, 100, 0, "TCPAORequired",
		   TEST_CNT_AO_REQUIRED, 0, FAULT_TIMEOUT);

	try_accept("MD5 server (INADDR_ANY): AO client", port++, &addr_any, 0,
		   NULL, 0, 0, 0, 0, 0, "TCPAOKeyNotFound",
		   0, 1, FAULT_TIMEOUT);
	try_accept("MD5 server (INADDR_ANY): MD5 client", port++, &addr_any, 0,
		   NULL, 0, 0, 0, 0, 0, NULL, 0, 1, 0);
	try_accept("MD5 server (INADDR_ANY): no sign client", port++, &addr_any,
		   0, NULL, 0, 0, 0, 0, 0, "TCPMD5NotFound",
		   0, 1, FAULT_TIMEOUT);

	try_accept("no sign server: AO client", port++, NULL, 0,
		   NULL, 0, 0, 0, 0, 0, "TCPAOKeyNotFound",
		   TEST_CNT_AO_KEY_NOT_FOUND, 0, FAULT_TIMEOUT);
	try_accept("no sign server: MD5 client", port++, NULL, 0,
		   NULL, 0, 0, 0, 0, 0, "TCPMD5Unexpected",
		   0, 1, FAULT_TIMEOUT);
	try_accept("no sign server: no sign client", port++, NULL, 0,
		   NULL, 0, 0, 0, 0, 0, "CurrEstab", 0, 0, 0);

	try_accept("AO+MD5 server: AO client (matching)", port++,
		   &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "TCPAOGood", TEST_CNT_GOOD, 1, 0);
	try_accept("AO+MD5 server: AO client (misconfig, matching MD5)", port++,
		   &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "TCPAOKeyNotFound", TEST_CNT_AO_KEY_NOT_FOUND,
		   1, FAULT_TIMEOUT);
	try_accept("AO+MD5 server: AO client (misconfig, non-matching)", port++,
		   &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "TCPAOKeyNotFound", TEST_CNT_AO_KEY_NOT_FOUND,
		   1, FAULT_TIMEOUT);
	try_accept("AO+MD5 server: MD5 client (matching)", port++,
		   &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, NULL, 0, 1, 0);
	try_accept("AO+MD5 server: MD5 client (misconfig, matching AO)", port++,
		   &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "TCPMD5Unexpected", 0, 1, FAULT_TIMEOUT);
	try_accept("AO+MD5 server: MD5 client (misconfig, non-matching)", port++,
		   &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "TCPMD5Unexpected", 0, 1, FAULT_TIMEOUT);
	try_accept("AO+MD5 server: no sign client (unmatched)", port++,
		   &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "CurrEstab", 0, 1, 0);
	try_accept("AO+MD5 server: no sign client (misconfig, matching AO)",
		   port++, &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "TCPAORequired",
		   TEST_CNT_AO_REQUIRED, 1, FAULT_TIMEOUT);
	try_accept("AO+MD5 server: no sign client (misconfig, matching MD5)",
		   port++, &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, "TCPMD5NotFound", 0, 1, FAULT_TIMEOUT);

	try_accept("AO+MD5 server: client with both [TCP-MD5] and TCP-AO keys",
		   port++, &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, NULL, 0, 1, FAULT_TIMEOUT);
	try_accept("AO+MD5 server: client with both TCP-MD5 and [TCP-AO] keys",
		   port++, &this_ip_dest, TEST_PREFIX, &client2, TEST_PREFIX, 0,
		   100, 100, 0, NULL, 0, 1, FAULT_TIMEOUT);

	server_add_fail_tests(&port);

	server_vrf_tests(&port);

	/* client exits */
	synchronize_threads();
	return NULL;
}

static int client_bind(int sk, union tcp_addr bind_addr)
{
#ifdef IPV6_TEST
	struct sockaddr_in6 addr = {
		.sin6_family	= AF_INET6,
		.sin6_port	= 0,
		.sin6_addr	= bind_addr.a6,
	};
#else
	struct sockaddr_in addr = {
		.sin_family	= AF_INET,
		.sin_port	= 0,
		.sin_addr	= bind_addr.a4,
	};
#endif
	return bind(sk, &addr, sizeof(addr));
}

static void try_connect(const char *tst_name, unsigned int port,
		       union tcp_addr *md5_addr, uint8_t md5_prefix,
		       union tcp_addr *ao_addr, uint8_t ao_prefix,
		       uint8_t sndid, uint8_t rcvid, uint8_t vrf,
		       fault_t inj, int needs_tcp_md5, union tcp_addr *bind_addr)
{
	time_t timeout;
	int sk, ret;

	if (needs_tcp_md5 && should_skip_test(tst_name, KCONFIG_TCP_MD5))
		return;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	if (bind_addr && client_bind(sk, *bind_addr))
		test_error("bind()");

	if (md5_addr && test_set_md5(sk, *md5_addr, md5_prefix, -1, md5_password))
		test_error("setsockopt(TCP_MD5SIG_EXT)");

	if (ao_addr && test_add_key(sk, ao_password, *ao_addr,
				    ao_prefix, sndid, rcvid))
		test_error("setsockopt(TCP_AO_ADD_KEY)");

	synchronize_threads(); /* preparations done */

	timeout = fault(TIMEOUT) ? TEST_RETRANSMIT_SEC : TEST_TIMEOUT_SEC;
	ret = _test_connect_socket(sk, this_ip_dest, port, timeout);

	synchronize_threads(); /* connect()/accept() timeouts */
	if (ret < 0) {
		if (fault(KEYREJECT) && ret == -EKEYREJECTED)
			test_ok("%s: connect() was prevented", tst_name);
		else if (ret == -ETIMEDOUT && fault(TIMEOUT))
			test_ok("%s", tst_name);
		else if (ret == -ECONNREFUSED &&
				(fault(TIMEOUT) || fault(KEYREJECT)))
			test_ok("%s: refused to connect", tst_name);
		else
			test_error("%s: connect() returned %d", tst_name, ret);
		goto out;
	}

	if (fault(TIMEOUT) || fault(KEYREJECT))
		test_fail("%s: connected", tst_name);
	else
		test_ok("%s: connected", tst_name);

out:
	synchronize_threads(); /* test_kill_sk() */
	/* _test_connect_socket() cleans up on failure */
	if (ret > 0)
		test_kill_sk(sk);
}

#define PREINSTALL_MD5_FIRST	BIT(0)
#define PREINSTALL_AO		BIT(1)
#define POSTINSTALL_AO		BIT(2)
#define PREINSTALL_MD5		BIT(3)
#define POSTINSTALL_MD5		BIT(4)

static int try_add_key_vrf(int sk, union tcp_addr in_addr, uint8_t prefix,
			   int vrf, uint8_t sndid, uint8_t rcvid,
			   bool set_ao_required)
{
	uint8_t keyflags = 0;

	if (vrf >= 0)
		keyflags |= TCP_AO_KEYF_IFINDEX;
	else
		vrf = 0;
	if (set_ao_required) {
		int err = test_set_ao_flags(sk, true, 0);

		if (err)
			return err;
	}
	return test_add_key_vrf(sk, ao_password, keyflags, in_addr, prefix,
				(uint8_t)vrf, sndid, rcvid);
}

static bool test_continue(const char *tst_name, int err,
			  fault_t inj, bool added_ao)
{
	bool expected_to_fail;

	expected_to_fail = fault(PREINSTALL_AO) && added_ao;
	expected_to_fail |= fault(PREINSTALL_MD5) && !added_ao;

	if (!err) {
		if (!expected_to_fail)
			return true;
		test_fail("%s: setsockopt()s were expected to fail", tst_name);
		return false;
	}
	if (err != -EKEYREJECTED || !expected_to_fail) {
		test_error("%s: setsockopt(%s) = %d", tst_name,
			   added_ao ? "TCP_AO_ADD_KEY" : "TCP_MD5SIG_EXT", err);
		return false;
	}
	test_ok("%s: prefailed as expected: %m", tst_name);
	return false;
}

static int open_add(const char *tst_name, unsigned int port,
		    unsigned int strategy,
		    union tcp_addr md5_addr, uint8_t md5_prefix, int md5_vrf,
		    union tcp_addr ao_addr, uint8_t ao_prefix,
		    int ao_vrf, bool set_ao_required,
		    uint8_t sndid, uint8_t rcvid,
		    fault_t inj)
{
	int sk;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	if (client_bind(sk, this_ip_addr))
		test_error("bind()");

	if (strategy & PREINSTALL_MD5_FIRST) {
		if (test_set_md5(sk, md5_addr, md5_prefix, md5_vrf, md5_password))
			test_error("setsockopt(TCP_MD5SIG_EXT)");
	}

	if (strategy & PREINSTALL_AO) {
		int err = try_add_key_vrf(sk, ao_addr, ao_prefix, ao_vrf,
					  sndid, rcvid, set_ao_required);

		if (!test_continue(tst_name, err, inj, true)) {
			close(sk);
			return -1;
		}
	}

	if (strategy & PREINSTALL_MD5) {
		errno = 0;
		test_set_md5(sk, md5_addr, md5_prefix, md5_vrf, md5_password);
		if (!test_continue(tst_name, -errno, inj, false)) {
			close(sk);
			return -1;
		}
	}

	return sk;
}

static void try_to_preadd(const char *tst_name, unsigned int port,
			  unsigned int strategy,
			  union tcp_addr md5_addr, uint8_t md5_prefix,
			  int md5_vrf,
			  union tcp_addr ao_addr, uint8_t ao_prefix,
			  int ao_vrf, bool set_ao_required,
			  uint8_t sndid, uint8_t rcvid,
			  int needs_tcp_md5, int needs_vrf, fault_t inj)
{
	int sk;

	if (needs_tcp_md5 && should_skip_test(tst_name, KCONFIG_TCP_MD5))
		return;
	if (needs_vrf && should_skip_test(tst_name, KCONFIG_NET_VRF))
		return;

	sk = open_add(tst_name, port, strategy, md5_addr, md5_prefix, md5_vrf,
		      ao_addr, ao_prefix, ao_vrf, set_ao_required,
		      sndid, rcvid, inj);
	if (sk < 0)
		return;

	test_ok("%s", tst_name);
	close(sk);
}

static void try_to_add(const char *tst_name, unsigned int port,
		       unsigned int strategy,
		       union tcp_addr md5_addr, uint8_t md5_prefix,
		       int md5_vrf,
		       union tcp_addr ao_addr, uint8_t ao_prefix,
		       int ao_vrf, uint8_t sndid, uint8_t rcvid,
		       int needs_tcp_md5, fault_t inj)
{
	time_t timeout;
	int sk, ret;

	if (needs_tcp_md5 && should_skip_test(tst_name, KCONFIG_TCP_MD5))
		return;

	sk = open_add(tst_name, port, strategy, md5_addr, md5_prefix, md5_vrf,
		      ao_addr, ao_prefix, ao_vrf, 0, sndid, rcvid, inj);
	if (sk < 0)
		return;

	synchronize_threads(); /* preparations done */

	timeout = fault(TIMEOUT) ? TEST_RETRANSMIT_SEC : TEST_TIMEOUT_SEC;
	ret = _test_connect_socket(sk, this_ip_dest, port, timeout);

	synchronize_threads(); /* connect()/accept() timeouts */
	if (ret <= 0) {
		test_error("%s: connect() returned %d", tst_name, ret);
		goto out;
	}

	if (strategy & POSTINSTALL_MD5) {
		if (test_set_md5(sk, md5_addr, md5_prefix, md5_vrf, md5_password)) {
			if (fault(POSTINSTALL)) {
				test_ok("%s: postfailed as expected", tst_name);
				goto out;
			} else {
				test_error("setsockopt(TCP_MD5SIG_EXT)");
			}
		} else if (fault(POSTINSTALL)) {
			test_fail("%s: post setsockopt() was expected to fail", tst_name);
			goto out;
		}
	}

	if (strategy & POSTINSTALL_AO) {
		if (try_add_key_vrf(sk, ao_addr, ao_prefix, ao_vrf,
				   sndid, rcvid, 0)) {
			if (fault(POSTINSTALL)) {
				test_ok("%s: postfailed as expected", tst_name);
				goto out;
			} else {
				test_error("setsockopt(TCP_AO_ADD_KEY)");
			}
		} else if (fault(POSTINSTALL)) {
			test_fail("%s: post setsockopt() was expected to fail", tst_name);
			goto out;
		}
	}

out:
	synchronize_threads(); /* test_kill_sk() */
	/* _test_connect_socket() cleans up on failure */
	if (ret > 0)
		test_kill_sk(sk);
}

static void client_add_ip(union tcp_addr *client, const char *ip)
{
	int err, family = TEST_FAMILY;

	if (inet_pton(family, ip, client) != 1)
		test_error("Can't convert ip address %s", ip);

	err = ip_addr_add(veth_name, family, *client, TEST_PREFIX);
	if (err)
		test_error("Failed to add ip address: %d", err);
}

static void client_add_ips(void)
{
	client_add_ip(&client2, __TEST_CLIENT_IP(2));
	client_add_ip(&client3, __TEST_CLIENT_IP(3));
	synchronize_threads(); /* server_add_routes() */
}

static void client_add_fail_tests(unsigned int *port)
{
	try_to_add("TCP-AO established: add TCP-MD5 key",
		   (*port)++, POSTINSTALL_MD5 | PREINSTALL_AO,
		   this_ip_dest, TEST_PREFIX, -1, this_ip_dest, TEST_PREFIX, 0,
		   100, 100, 1, FAULT_POSTINSTALL);
	try_to_add("TCP-MD5 established: add TCP-AO key",
		   (*port)++, PREINSTALL_MD5 | POSTINSTALL_AO,
		   this_ip_dest, TEST_PREFIX, -1, this_ip_dest, TEST_PREFIX, 0,
		   100, 100, 1, FAULT_POSTINSTALL);
	try_to_add("non-signed established: add TCP-AO key",
		   (*port)++, POSTINSTALL_AO,
		   this_ip_dest, TEST_PREFIX, -1, this_ip_dest, TEST_PREFIX, 0,
		   100, 100, 0, FAULT_POSTINSTALL);

	try_to_add("TCP-AO key intersects with existing TCP-MD5 key",
		   (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		   this_ip_addr, TEST_PREFIX, -1, this_ip_addr, TEST_PREFIX, -1,
		   100, 100, 1, FAULT_PREINSTALL_AO);
	try_to_add("TCP-MD5 key intersects with existing TCP-AO key",
		   (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		   this_ip_addr, TEST_PREFIX, -1, this_ip_addr, TEST_PREFIX, -1,
		   100, 100, 1, FAULT_PREINSTALL_MD5);

	try_to_preadd("TCP-MD5 key + TCP-AO required",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, -1,
		      this_ip_addr, TEST_PREFIX, -1, true,
		      100, 100, 1, 0, FAULT_PREINSTALL_AO);
	try_to_preadd("TCP-AO required on socket + TCP-MD5 key",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, -1,
		      this_ip_addr, TEST_PREFIX, -1, true,
		      100, 100, 1, 0, FAULT_PREINSTALL_MD5);
}

static void client_vrf_tests(unsigned int *port)
{
	setup_vrfs();

	/* The following restrictions for setsockopt()s are expected:
	 *
	 * |--------------|-----------------|-------------|-------------|
	 * |              | MD5 key without |   MD5 key   |   MD5 key   |
	 * |              |     l3index     |  l3index=0  |  l3index=N  |
	 * |--------------|-----------------|-------------|-------------|
	 * |  TCP-AO key  |                 |             |             |
	 * |  without     |     reject      |    reject   |    reject   |
	 * |  l3index     |                 |             |             |
	 * |--------------|-----------------|-------------|-------------|
	 * |  TCP-AO key  |                 |             |             |
	 * |  l3index=0   |     reject      |    reject   |    allow    |
	 * |--------------|-----------------|-------------|-------------|
	 * |  TCP-AO key  |                 |             |             |
	 * |  l3index=N   |     reject      |    allow    |    reject   |
	 * |--------------|-----------------|-------------|-------------|
	 */
	try_to_preadd("VRF: TCP-AO key (no l3index) + TCP-MD5 key (no l3index)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, -1,
		      this_ip_addr, TEST_PREFIX, -1, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_MD5);
	try_to_preadd("VRF: TCP-MD5 key (no l3index) + TCP-AO key (no l3index)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, -1,
		      this_ip_addr, TEST_PREFIX, -1, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_AO);
	try_to_preadd("VRF: TCP-AO key (no l3index) + TCP-MD5 key (l3index=0)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, 0,
		      this_ip_addr, TEST_PREFIX, -1, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_MD5);
	try_to_preadd("VRF: TCP-MD5 key (l3index=0) + TCP-AO key (no l3index)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, 0,
		      this_ip_addr, TEST_PREFIX, -1, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_AO);
	try_to_preadd("VRF: TCP-AO key (no l3index) + TCP-MD5 key (l3index=N)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex,
		      this_ip_addr, TEST_PREFIX, -1, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_MD5);
	try_to_preadd("VRF: TCP-MD5 key (l3index=N) + TCP-AO key (no l3index)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex,
		      this_ip_addr, TEST_PREFIX, -1, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_AO);

	try_to_preadd("VRF: TCP-AO key (l3index=0) + TCP-MD5 key (no l3index)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, -1,
		      this_ip_addr, TEST_PREFIX, 0, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_MD5);
	try_to_preadd("VRF: TCP-MD5 key (no l3index) + TCP-AO key (l3index=0)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, -1,
		      this_ip_addr, TEST_PREFIX, 0, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_AO);
	try_to_preadd("VRF: TCP-AO key (l3index=0) + TCP-MD5 key (l3index=0)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, 0,
		      this_ip_addr, TEST_PREFIX, 0, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_MD5);
	try_to_preadd("VRF: TCP-MD5 key (l3index=0) + TCP-AO key (l3index=0)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, 0,
		      this_ip_addr, TEST_PREFIX, 0, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_AO);
	try_to_preadd("VRF: TCP-AO key (l3index=0) + TCP-MD5 key (l3index=N)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex,
		      this_ip_addr, TEST_PREFIX, 0, 0, 100, 100,
		      1, 1, 0);
	try_to_preadd("VRF: TCP-MD5 key (l3index=N) + TCP-AO key (l3index=0)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex,
		      this_ip_addr, TEST_PREFIX, 0, 0, 100, 100,
		      1, 1, 0);

	try_to_preadd("VRF: TCP-AO key (l3index=N) + TCP-MD5 key (no l3index)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex,
		      this_ip_addr, TEST_PREFIX, -1, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_MD5);
	try_to_preadd("VRF: TCP-MD5 key (no l3index) + TCP-AO key (l3index=N)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, -1,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_AO);
	try_to_preadd("VRF: TCP-AO key (l3index=N) + TCP-MD5 key (l3index=0)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, 0,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex, 0, 100, 100,
		      1, 1, 0);
	try_to_preadd("VRF: TCP-MD5 key (l3index=0) + TCP-AO key (l3index=N)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, 0,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex, 0, 100, 100,
		      1, 1, 0);
	try_to_preadd("VRF: TCP-AO key (l3index=N) + TCP-MD5 key (l3index=N)",
		      (*port)++, PREINSTALL_MD5 | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_MD5);
	try_to_preadd("VRF: TCP-MD5 key (l3index=N) + TCP-AO key (l3index=N)",
		      (*port)++, PREINSTALL_MD5_FIRST | PREINSTALL_AO,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex,
		      this_ip_addr, TEST_PREFIX, test_vrf_ifindex, 0, 100, 100,
		      1, 1, FAULT_PREINSTALL_AO);
}

static void *client_fn(void *arg)
{
	unsigned int port = test_server_port;
	union tcp_addr addr_any = {};

	client_add_ips();

	try_connect("AO server (INADDR_ANY): AO client", port++, NULL, 0,
		    &addr_any, 0, 100, 100, 0, 0, 0, &this_ip_addr);
	trace_hash_event_expect(TCP_HASH_MD5_UNEXPECTED, this_ip_addr,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO server (INADDR_ANY): MD5 client", port++, &addr_any, 0,
		    NULL, 0, 100, 100, 0, FAULT_TIMEOUT, 1, &this_ip_addr);
	trace_hash_event_expect(TCP_HASH_AO_REQUIRED, this_ip_addr,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO server (INADDR_ANY): unsigned client", port++, NULL, 0,
		    NULL, 0, 100, 100, 0, FAULT_TIMEOUT, 0, &this_ip_addr);
	try_connect("AO server (AO_REQUIRED): AO client", port++, NULL, 0,
		    &addr_any, 0, 100, 100, 0, 0, 0, &this_ip_addr);
	trace_hash_event_expect(TCP_HASH_AO_REQUIRED, client2,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO server (AO_REQUIRED): unsigned client", port++, NULL, 0,
		    NULL, 0, 100, 100, 0, FAULT_TIMEOUT, 0, &client2);

	trace_ao_event_expect(TCP_AO_KEY_NOT_FOUND, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("MD5 server (INADDR_ANY): AO client", port++, NULL, 0,
		   &addr_any, 0, 100, 100, 0, FAULT_TIMEOUT, 1, &this_ip_addr);
	try_connect("MD5 server (INADDR_ANY): MD5 client", port++, &addr_any, 0,
		   NULL, 0, 100, 100, 0, 0, 1, &this_ip_addr);
	trace_hash_event_expect(TCP_HASH_MD5_REQUIRED, this_ip_addr,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("MD5 server (INADDR_ANY): no sign client", port++, NULL, 0,
		   NULL, 0, 100, 100, 0, FAULT_TIMEOUT, 1, &this_ip_addr);

	trace_ao_event_expect(TCP_AO_KEY_NOT_FOUND, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("no sign server: AO client", port++, NULL, 0,
		   &addr_any, 0, 100, 100, 0, FAULT_TIMEOUT, 0, &this_ip_addr);
	trace_hash_event_expect(TCP_HASH_MD5_UNEXPECTED, this_ip_addr,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("no sign server: MD5 client", port++, &addr_any, 0,
		   NULL, 0, 100, 100, 0, FAULT_TIMEOUT, 1, &this_ip_addr);
	try_connect("no sign server: no sign client", port++, NULL, 0,
		   NULL, 0, 100, 100, 0, 0, 0, &this_ip_addr);

	try_connect("AO+MD5 server: AO client (matching)", port++, NULL, 0,
		   &addr_any, 0, 100, 100, 0, 0, 1, &client2);
	trace_ao_event_expect(TCP_AO_KEY_NOT_FOUND, this_ip_addr, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("AO+MD5 server: AO client (misconfig, matching MD5)",
		   port++, NULL, 0, &addr_any, 0, 100, 100, 0,
		   FAULT_TIMEOUT, 1, &this_ip_addr);
	trace_ao_event_expect(TCP_AO_KEY_NOT_FOUND, client3, this_ip_dest,
			      -1, port, 0, 0, 1, 0, 0, 0, 100, 100, -1);
	try_connect("AO+MD5 server: AO client (misconfig, non-matching)",
		   port++, NULL, 0, &addr_any, 0, 100, 100, 0,
		   FAULT_TIMEOUT, 1, &client3);
	try_connect("AO+MD5 server: MD5 client (matching)", port++, &addr_any, 0,
		   NULL, 0, 100, 100, 0, 0, 1, &this_ip_addr);
	trace_hash_event_expect(TCP_HASH_MD5_UNEXPECTED, client2,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO+MD5 server: MD5 client (misconfig, matching AO)",
		   port++, &addr_any, 0, NULL, 0, 100, 100, 0, FAULT_TIMEOUT,
		   1, &client2);
	trace_hash_event_expect(TCP_HASH_MD5_UNEXPECTED, client3,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO+MD5 server: MD5 client (misconfig, non-matching)",
		   port++, &addr_any, 0, NULL, 0, 100, 100, 0, FAULT_TIMEOUT,
		   1, &client3);
	try_connect("AO+MD5 server: no sign client (unmatched)",
		   port++, NULL, 0, NULL, 0, 100, 100, 0, 0, 1, &client3);
	trace_hash_event_expect(TCP_HASH_AO_REQUIRED, client2,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO+MD5 server: no sign client (misconfig, matching AO)",
		   port++, NULL, 0, NULL, 0, 100, 100, 0, FAULT_TIMEOUT,
		   1, &client2);
	trace_hash_event_expect(TCP_HASH_MD5_REQUIRED, this_ip_addr,
				this_ip_dest, -1, port, 0, 0, 1, 0, 0, 0);
	try_connect("AO+MD5 server: no sign client (misconfig, matching MD5)",
		   port++, NULL, 0, NULL, 0, 100, 100, 0, FAULT_TIMEOUT,
		   1, &this_ip_addr);

	try_connect("AO+MD5 server: client with both [TCP-MD5] and TCP-AO keys",
		   port++, &this_ip_addr, TEST_PREFIX,
		   &client2, TEST_PREFIX, 100, 100, 0, FAULT_KEYREJECT,
		   1, &this_ip_addr);
	try_connect("AO+MD5 server: client with both TCP-MD5 and [TCP-AO] keys",
		   port++, &this_ip_addr, TEST_PREFIX,
		   &client2, TEST_PREFIX, 100, 100, 0, FAULT_KEYREJECT,
		   1, &client2);

	client_add_fail_tests(&port);
	client_vrf_tests(&port);

	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(73, server_fn, client_fn);
	return 0;
}
