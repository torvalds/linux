// SPDX-License-Identifier: GPL-2.0
/* Author: Dmitry Safonov <dima@arista.com> */
#include <inttypes.h>
#include "../../../../include/linux/kernel.h"
#include "aolib.h"

static union tcp_addr tcp_md5_client;

static int test_port = 7788;
static void make_listen(int sk)
{
	sockaddr_af addr;

	tcp_addr_to_sockaddr_in(&addr, &this_ip_addr, htons(test_port++));
	if (bind(sk, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		test_error("bind()");
	if (listen(sk, 1))
		test_error("listen()");
}

static void test_vefify_ao_info(int sk, struct tcp_ao_info_opt *info,
				const char *tst)
{
	struct tcp_ao_info_opt tmp = {};
	socklen_t len = sizeof(tmp);

	if (getsockopt(sk, IPPROTO_TCP, TCP_AO_INFO, &tmp, &len))
		test_error("getsockopt(TCP_AO_INFO) failed");

#define __cmp_ao(member)							\
do {										\
	if (info->member != tmp.member) {					\
		test_fail("%s: getsockopt(): " __stringify(member) " %zu != %zu",	\
			  tst, (size_t)info->member, (size_t)tmp.member);	\
		return;								\
	}									\
} while(0)
	if (info->set_current)
		__cmp_ao(current_key);
	if (info->set_rnext)
		__cmp_ao(rnext);
	if (info->set_counters) {
		__cmp_ao(pkt_good);
		__cmp_ao(pkt_bad);
		__cmp_ao(pkt_key_not_found);
		__cmp_ao(pkt_ao_required);
		__cmp_ao(pkt_dropped_icmp);
	}
	__cmp_ao(ao_required);
	__cmp_ao(accept_icmps);

	test_ok("AO info get: %s", tst);
#undef __cmp_ao
}

static void __setsockopt_checked(int sk, int optname, bool get,
				 void *optval, socklen_t *len,
				 int err, const char *tst, const char *tst2)
{
	int ret;

	if (!tst)
		tst = "";
	if (!tst2)
		tst2 = "";

	errno = 0;
	if (get)
		ret = getsockopt(sk, IPPROTO_TCP, optname, optval, len);
	else
		ret = setsockopt(sk, IPPROTO_TCP, optname, optval, *len);
	if (ret == -1) {
		if (errno == err)
			test_ok("%s%s", tst ?: "", tst2 ?: "");
		else
			test_fail("%s%s: %setsockopt() failed",
				  tst, tst2, get ? "g" : "s");
		close(sk);
		return;
	}

	if (err) {
		test_fail("%s%s: %setsockopt() was expected to fail with %d",
			  tst, tst2, get ? "g" : "s", err);
	} else {
		test_ok("%s%s", tst ?: "", tst2 ?: "");
		if (optname == TCP_AO_ADD_KEY) {
			test_verify_socket_key(sk, optval);
		} else if (optname == TCP_AO_INFO && !get) {
			test_vefify_ao_info(sk, optval, tst2);
		} else if (optname == TCP_AO_GET_KEYS) {
			if (*len != sizeof(struct tcp_ao_getsockopt))
				test_fail("%s%s: get keys returned wrong tcp_ao_getsockopt size",
					  tst, tst2);
		}
	}
	close(sk);
}

static void setsockopt_checked(int sk, int optname, void *optval,
			       int err, const char *tst)
{
	const char *cmd = NULL;
	socklen_t len;

	switch (optname) {
	case TCP_AO_ADD_KEY:
		cmd = "key add: ";
		len = sizeof(struct tcp_ao_add);
		break;
	case TCP_AO_DEL_KEY:
		cmd = "key del: ";
		len = sizeof(struct tcp_ao_del);
		break;
	case TCP_AO_INFO:
		cmd = "AO info set: ";
		len = sizeof(struct tcp_ao_info_opt);
		break;
	default:
		break;
	}

	__setsockopt_checked(sk, optname, false, optval, &len, err, cmd, tst);
}

static int prepare_defs(int cmd, void *optval)
{
	int sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);

	if (sk < 0)
		test_error("socket()");

	switch (cmd) {
	case TCP_AO_ADD_KEY: {
		struct tcp_ao_add *add = optval;

		if (test_prepare_def_key(add, DEFAULT_TEST_PASSWORD, 0, this_ip_dest,
					-1, 0, 100, 100))
			test_error("prepare default tcp_ao_add");
		break;
		}
	case TCP_AO_DEL_KEY: {
		struct tcp_ao_del *del = optval;

		if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest,
				 DEFAULT_TEST_PREFIX, 100, 100))
			test_error("add default key");
		memset(del, 0, sizeof(struct tcp_ao_del));
		del->sndid = 100;
		del->rcvid = 100;
		del->prefix = DEFAULT_TEST_PREFIX;
		tcp_addr_to_sockaddr_in(&del->addr, &this_ip_dest, 0);
		break;
		}
	case TCP_AO_INFO: {
		struct tcp_ao_info_opt *info = optval;

		if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest,
				 DEFAULT_TEST_PREFIX, 100, 100))
			test_error("add default key");
		memset(info, 0, sizeof(struct tcp_ao_info_opt));
		break;
		}
	case TCP_AO_GET_KEYS: {
		struct tcp_ao_getsockopt *get = optval;

		if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest,
				 DEFAULT_TEST_PREFIX, 100, 100))
			test_error("add default key");
		memset(get, 0, sizeof(struct tcp_ao_getsockopt));
		get->nkeys = 1;
		get->get_all = 1;
		break;
		}
	default:
		test_error("unknown cmd");
	}

	return sk;
}

static void test_extend(int cmd, bool get, const char *tst, socklen_t under_size)
{
	struct {
		union {
			struct tcp_ao_add add;
			struct tcp_ao_del del;
			struct tcp_ao_getsockopt get;
			struct tcp_ao_info_opt info;
		};
		char *extend[100];
	} tmp_opt;
	socklen_t extended_size = sizeof(tmp_opt);
	int sk;

	memset(&tmp_opt, 0, sizeof(tmp_opt));
	sk = prepare_defs(cmd, &tmp_opt);
	__setsockopt_checked(sk, cmd, get, &tmp_opt, &under_size,
			     EINVAL, tst, ": minimum size");

	memset(&tmp_opt, 0, sizeof(tmp_opt));
	sk = prepare_defs(cmd, &tmp_opt);
	__setsockopt_checked(sk, cmd, get, &tmp_opt, &extended_size,
			     0, tst, ": extended size");

	memset(&tmp_opt, 0, sizeof(tmp_opt));
	sk = prepare_defs(cmd, &tmp_opt);
	__setsockopt_checked(sk, cmd, get, NULL, &extended_size,
			     EFAULT, tst, ": null optval");

	if (get) {
		memset(&tmp_opt, 0, sizeof(tmp_opt));
		sk = prepare_defs(cmd, &tmp_opt);
		__setsockopt_checked(sk, cmd, get, &tmp_opt, NULL,
				     EFAULT, tst, ": null optlen");
	}
}

static void extend_tests(void)
{
	test_extend(TCP_AO_ADD_KEY, false, "AO add",
		    offsetof(struct tcp_ao_add, key));
	test_extend(TCP_AO_DEL_KEY, false, "AO del",
		    offsetof(struct tcp_ao_del, keyflags));
	test_extend(TCP_AO_INFO, false, "AO set info",
		    offsetof(struct tcp_ao_info_opt, pkt_dropped_icmp));
	test_extend(TCP_AO_INFO, true, "AO get info", -1);
	test_extend(TCP_AO_GET_KEYS, true, "AO get keys", -1);
}

static void test_optmem_limit(void)
{
	size_t i, keys_limit, current_optmem = test_get_optmem();
	struct tcp_ao_add ao;
	union tcp_addr net = {};
	int sk;

	if (inet_pton(TEST_FAMILY, TEST_NETWORK, &net) != 1)
		test_error("Can't convert ip address %s", TEST_NETWORK);

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	keys_limit = current_optmem / KERNEL_TCP_AO_KEY_SZ_ROUND_UP;
	for (i = 0;; i++) {
		union tcp_addr key_peer;
		int err;

		key_peer = gen_tcp_addr(net, i + 1);
		tcp_addr_to_sockaddr_in(&ao.addr, &key_peer, 0);
		err = setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY,
				 &ao, sizeof(ao));
		if (!err) {
			/*
			 * TCP_AO_ADD_KEY should be the same order as the real
			 * sizeof(struct tcp_ao_key) in kernel.
			 */
			if (i <= keys_limit * 10)
				continue;
			test_fail("optmem limit test failed: added %zu key", i);
			break;
		}
		if (i < keys_limit) {
			test_fail("optmem limit test failed: couldn't add %zu key", i);
			break;
		}
		test_ok("optmem limit was hit on adding %zu key", i);
		break;
	}
	close(sk);
}

static void test_einval_add_key(void)
{
	struct tcp_ao_add ao;
	int sk;

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.keylen = TCP_AO_MAXKEYLEN + 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "too big keylen");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.reserved = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "using reserved padding");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.reserved2 = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "using reserved2 padding");

	/* tcp_ao_verify_ipv{4,6}() checks */
	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.addr.ss_family = AF_UNIX;
	memcpy(&ao.addr, &SOCKADDR_ANY, sizeof(SOCKADDR_ANY));
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "wrong address family");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	tcp_addr_to_sockaddr_in(&ao.addr, &this_ip_dest, 1234);
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "port (unsupported)");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.prefix = 0;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "no prefix, addr");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.prefix = 0;
	memcpy(&ao.addr, &SOCKADDR_ANY, sizeof(SOCKADDR_ANY));
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, 0, "no prefix, any addr");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.prefix = 32;
	memcpy(&ao.addr, &SOCKADDR_ANY, sizeof(SOCKADDR_ANY));
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "prefix, any addr");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.prefix = 129;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "too big prefix");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.prefix = 2;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "too short prefix");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.keyflags = (uint8_t)(-1);
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "bad key flags");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	make_listen(sk);
	ao.set_current = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "add current key on a listen socket");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	make_listen(sk);
	ao.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "add rnext key on a listen socket");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	make_listen(sk);
	ao.set_current = 1;
	ao.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "add current+rnext key on a listen socket");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.set_current = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, 0, "add key and set as current");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, 0, "add key and set as rnext");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.set_current = 1;
	ao.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, 0, "add key and set as current+rnext");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.ifindex = 42;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL,
			   "ifindex without TCP_AO_KEYF_IFNINDEX");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.keyflags |= TCP_AO_KEYF_IFINDEX;
	ao.ifindex = 42;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EINVAL, "non-existent VRF");
	/*
	 * tcp_md5_do_lookup{,_any_l3index}() are checked in unsigned-md5
	 * see client_vrf_tests().
	 */

	test_optmem_limit();

	/* tcp_ao_parse_crypto() */
	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao.maclen = 100;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EMSGSIZE, "maclen bigger than TCP hdr");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	strcpy(ao.alg_name, "imaginary hash algo");
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, ENOENT, "bad algo");
}

static void test_einval_del_key(void)
{
	struct tcp_ao_del del;
	int sk;

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.reserved = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL, "using reserved padding");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.reserved2 = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL, "using reserved2 padding");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	make_listen(sk);
	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, DEFAULT_TEST_PREFIX, 0, 0))
		test_error("add key");
	del.set_current = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL, "del and set current key on a listen socket");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	make_listen(sk);
	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, DEFAULT_TEST_PREFIX, 0, 0))
		test_error("add key");
	del.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL, "del and set rnext key on a listen socket");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	make_listen(sk);
	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, DEFAULT_TEST_PREFIX, 0, 0))
		test_error("add key");
	del.set_current = 1;
	del.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL, "del and set current+rnext key on a listen socket");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.keyflags = (uint8_t)(-1);
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL, "bad key flags");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.ifindex = 42;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL,
			   "ifindex without TCP_AO_KEYF_IFNINDEX");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.keyflags |= TCP_AO_KEYF_IFINDEX;
	del.ifindex = 42;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "non-existent VRF");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.set_current = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "set non-existing current key");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "set non-existing rnext key");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.set_current = 1;
	del.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "set non-existing current+rnext key");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, DEFAULT_TEST_PREFIX, 0, 0))
		test_error("add key");
	del.set_current = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, 0, "set current key");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, DEFAULT_TEST_PREFIX, 0, 0))
		test_error("add key");
	del.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, 0, "set rnext key");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest, DEFAULT_TEST_PREFIX, 0, 0))
		test_error("add key");
	del.set_current = 1;
	del.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, 0, "set current+rnext key");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.set_current = 1;
	del.current_key = 100;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "set as current key to be removed");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.set_rnext = 1;
	del.rnext = 100;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "set as rnext key to be removed");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.set_current = 1;
	del.current_key = 100;
	del.set_rnext = 1;
	del.rnext = 100;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "set as current+rnext key to be removed");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.del_async = 1;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, EINVAL, "async on non-listen");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.sndid = 101;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "non-existing sndid");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	del.rcvid = 101;
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "non-existing rcvid");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	tcp_addr_to_sockaddr_in(&del.addr, &this_ip_addr, 0);
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, ENOENT, "incorrect addr");

	sk = prepare_defs(TCP_AO_DEL_KEY, &del);
	setsockopt_checked(sk, TCP_AO_DEL_KEY, &del, 0, "correct key delete");
}

static void test_einval_ao_info(void)
{
	struct tcp_ao_info_opt info;
	int sk;

	sk = prepare_defs(TCP_AO_INFO, &info);
	make_listen(sk);
	info.set_current = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, EINVAL, "set current key on a listen socket");

	sk = prepare_defs(TCP_AO_INFO, &info);
	make_listen(sk);
	info.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, EINVAL, "set rnext key on a listen socket");

	sk = prepare_defs(TCP_AO_INFO, &info);
	make_listen(sk);
	info.set_current = 1;
	info.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, EINVAL, "set current+rnext key on a listen socket");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.reserved = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, EINVAL, "using reserved padding");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.reserved2 = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, EINVAL, "using reserved2 padding");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.accept_icmps = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, 0, "accept_icmps");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.ao_required = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, 0, "ao required");

	if (!should_skip_test("ao required with MD5 key", KCONFIG_TCP_MD5)) {
		sk = prepare_defs(TCP_AO_INFO, &info);
		info.ao_required = 1;
		if (test_set_md5(sk, tcp_md5_client, TEST_PREFIX, -1,
				 "long long secret")) {
			test_error("setsockopt(TCP_MD5SIG_EXT)");
			close(sk);
		} else {
			setsockopt_checked(sk, TCP_AO_INFO, &info, EKEYREJECTED,
					   "ao required with MD5 key");
		}
	}

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.set_current = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, ENOENT, "set non-existing current key");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, ENOENT, "set non-existing rnext key");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.set_current = 1;
	info.set_rnext = 1;
	setsockopt_checked(sk, TCP_AO_INFO, &info, ENOENT, "set non-existing current+rnext key");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.set_current = 1;
	info.current_key = 100;
	setsockopt_checked(sk, TCP_AO_INFO, &info, 0, "set current key");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.set_rnext = 1;
	info.rnext = 100;
	setsockopt_checked(sk, TCP_AO_INFO, &info, 0, "set rnext key");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.set_current = 1;
	info.set_rnext = 1;
	info.current_key = 100;
	info.rnext = 100;
	setsockopt_checked(sk, TCP_AO_INFO, &info, 0, "set current+rnext key");

	sk = prepare_defs(TCP_AO_INFO, &info);
	info.set_counters = 1;
	info.pkt_good = 321;
	info.pkt_bad = 888;
	info.pkt_key_not_found = 654;
	info.pkt_ao_required = 987654;
	info.pkt_dropped_icmp = 10000;
	setsockopt_checked(sk, TCP_AO_INFO, &info, 0, "set counters");

	sk = prepare_defs(TCP_AO_INFO, &info);
	setsockopt_checked(sk, TCP_AO_INFO, &info, 0, "no-op");
}

static void getsockopt_checked(int sk, struct tcp_ao_getsockopt *optval,
			       int err, const char *tst)
{
	socklen_t len = sizeof(struct tcp_ao_getsockopt);

	__setsockopt_checked(sk, TCP_AO_GET_KEYS, true, optval, &len, err,
			     "get keys: ", tst);
}

static void test_einval_get_keys(void)
{
	struct tcp_ao_getsockopt out;
	int sk;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");
	getsockopt_checked(sk, &out, ENOENT, "no ao_info");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	getsockopt_checked(sk, &out, 0, "proper tcp_ao_get_mkts()");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.pkt_good = 643;
	getsockopt_checked(sk, &out, EINVAL, "set out-only pkt_good counter");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.pkt_bad = 94;
	getsockopt_checked(sk, &out, EINVAL, "set out-only pkt_bad counter");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.keyflags = (uint8_t)(-1);
	getsockopt_checked(sk, &out, EINVAL, "bad keyflags");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.ifindex = 42;
	getsockopt_checked(sk, &out, EINVAL,
			   "ifindex without TCP_AO_KEYF_IFNINDEX");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.reserved = 1;
	getsockopt_checked(sk, &out, EINVAL, "using reserved field");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.prefix = 0;
	tcp_addr_to_sockaddr_in(&out.addr, &this_ip_dest, 0);
	getsockopt_checked(sk, &out, EINVAL, "no prefix, addr");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.prefix = 0;
	memcpy(&out.addr, &SOCKADDR_ANY, sizeof(SOCKADDR_ANY));
	getsockopt_checked(sk, &out, 0, "no prefix, any addr");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.prefix = 32;
	memcpy(&out.addr, &SOCKADDR_ANY, sizeof(SOCKADDR_ANY));
	getsockopt_checked(sk, &out, EINVAL, "prefix, any addr");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.prefix = 129;
	tcp_addr_to_sockaddr_in(&out.addr, &this_ip_dest, 0);
	getsockopt_checked(sk, &out, EINVAL, "too big prefix");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.prefix = 2;
	tcp_addr_to_sockaddr_in(&out.addr, &this_ip_dest, 0);
	getsockopt_checked(sk, &out, EINVAL, "too short prefix");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.prefix = DEFAULT_TEST_PREFIX;
	tcp_addr_to_sockaddr_in(&out.addr, &this_ip_dest, 0);
	getsockopt_checked(sk, &out, 0, "prefix + addr");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 1;
	out.prefix = DEFAULT_TEST_PREFIX;
	getsockopt_checked(sk, &out, EINVAL, "get_all + prefix");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 1;
	tcp_addr_to_sockaddr_in(&out.addr, &this_ip_dest, 0);
	getsockopt_checked(sk, &out, EINVAL, "get_all + addr");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 1;
	out.sndid = 1;
	getsockopt_checked(sk, &out, EINVAL, "get_all + sndid");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 1;
	out.rcvid = 1;
	getsockopt_checked(sk, &out, EINVAL, "get_all + rcvid");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_current = 1;
	out.prefix = DEFAULT_TEST_PREFIX;
	getsockopt_checked(sk, &out, EINVAL, "current + prefix");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_current = 1;
	tcp_addr_to_sockaddr_in(&out.addr, &this_ip_dest, 0);
	getsockopt_checked(sk, &out, EINVAL, "current + addr");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_current = 1;
	out.sndid = 1;
	getsockopt_checked(sk, &out, EINVAL, "current + sndid");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_current = 1;
	out.rcvid = 1;
	getsockopt_checked(sk, &out, EINVAL, "current + rcvid");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_rnext = 1;
	out.prefix = DEFAULT_TEST_PREFIX;
	getsockopt_checked(sk, &out, EINVAL, "rnext + prefix");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_rnext = 1;
	tcp_addr_to_sockaddr_in(&out.addr, &this_ip_dest, 0);
	getsockopt_checked(sk, &out, EINVAL, "rnext + addr");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_rnext = 1;
	out.sndid = 1;
	getsockopt_checked(sk, &out, EINVAL, "rnext + sndid");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_rnext = 1;
	out.rcvid = 1;
	getsockopt_checked(sk, &out, EINVAL, "rnext + rcvid");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 1;
	out.is_current = 1;
	getsockopt_checked(sk, &out, EINVAL, "get_all + current");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 1;
	out.is_rnext = 1;
	getsockopt_checked(sk, &out, EINVAL, "get_all + rnext");

	sk = prepare_defs(TCP_AO_GET_KEYS, &out);
	out.get_all = 0;
	out.is_current = 1;
	out.is_rnext = 1;
	getsockopt_checked(sk, &out, 0, "current + rnext");
}

static void einval_tests(void)
{
	test_einval_add_key();
	test_einval_del_key();
	test_einval_ao_info();
	test_einval_get_keys();
}

static void duplicate_tests(void)
{
	union tcp_addr network_dup;
	struct tcp_ao_add ao, ao2;
	int sk;

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &ao, sizeof(ao)))
		test_error("setsockopt()");
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EEXIST, "duplicate: full copy");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	ao2 = ao;
	memcpy(&ao2.addr, &SOCKADDR_ANY, sizeof(SOCKADDR_ANY));
	ao2.prefix = 0;
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &ao2, sizeof(ao)))
		test_error("setsockopt()");
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EEXIST, "duplicate: any addr key on the socket");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &ao, sizeof(ao)))
		test_error("setsockopt()");
	memcpy(&ao.addr, &SOCKADDR_ANY, sizeof(SOCKADDR_ANY));
	ao.prefix = 0;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EEXIST, "duplicate: add any addr key");

	if (inet_pton(TEST_FAMILY, TEST_NETWORK, &network_dup) != 1)
		test_error("Can't convert ip address %s", TEST_NETWORK);
	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &ao, sizeof(ao)))
		test_error("setsockopt()");
	if (test_prepare_def_key(&ao, "password", 0, network_dup,
				 16, 0, 100, 100))
		test_error("prepare default tcp_ao_add");
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EEXIST, "duplicate: add any addr for the same subnet");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &ao, sizeof(ao)))
		test_error("setsockopt()");
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EEXIST, "duplicate: full copy of a key");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &ao, sizeof(ao)))
		test_error("setsockopt()");
	ao.rcvid = 101;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EEXIST, "duplicate: RecvID differs");

	sk = prepare_defs(TCP_AO_ADD_KEY, &ao);
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &ao, sizeof(ao)))
		test_error("setsockopt()");
	ao.sndid = 101;
	setsockopt_checked(sk, TCP_AO_ADD_KEY, &ao, EEXIST, "duplicate: SendID differs");
}

static void *client_fn(void *arg)
{
	if (inet_pton(TEST_FAMILY, __TEST_CLIENT_IP(2), &tcp_md5_client) != 1)
		test_error("Can't convert ip address");
	extend_tests();
	einval_tests();
	duplicate_tests();
	/*
	 * TODO: check getsockopt(TCP_AO_GET_KEYS) with different filters
	 * returning proper nr & keys;
	 */

	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(120, client_fn, NULL);
	return 0;
}
