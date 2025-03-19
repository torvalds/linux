// SPDX-License-Identifier: GPL-2.0
#include <alloca.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include "../../../../../include/linux/kernel.h"
#include "../../../../../include/linux/stringify.h"
#include "aolib.h"

const unsigned int test_server_port = 7010;
int __test_listen_socket(int backlog, void *addr, size_t addr_sz)
{
	int err, sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	long flags;

	if (sk < 0)
		test_error("socket()");

	err = setsockopt(sk, SOL_SOCKET, SO_BINDTODEVICE, veth_name,
			 strlen(veth_name) + 1);
	if (err < 0)
		test_error("setsockopt(SO_BINDTODEVICE)");

	if (bind(sk, (struct sockaddr *)addr, addr_sz) < 0)
		test_error("bind()");

	flags = fcntl(sk, F_GETFL);
	if ((flags < 0) || (fcntl(sk, F_SETFL, flags | O_NONBLOCK) < 0))
		test_error("fcntl()");

	if (listen(sk, backlog))
		test_error("listen()");

	return sk;
}

static int __test_wait_fd(int sk, struct timeval *tv, bool write)
{
	fd_set fds, efds;
	int ret;
	socklen_t slen = sizeof(ret);

	FD_ZERO(&fds);
	FD_SET(sk, &fds);
	FD_ZERO(&efds);
	FD_SET(sk, &efds);

	errno = 0;
	if (write)
		ret = select(sk + 1, NULL, &fds, &efds, tv);
	else
		ret = select(sk + 1, &fds, NULL, &efds, tv);
	if (ret < 0)
		return -errno;
	if (ret == 0) {
		errno = ETIMEDOUT;
		return -ETIMEDOUT;
	}

	if (getsockopt(sk, SOL_SOCKET, SO_ERROR, &ret, &slen))
		return -errno;
	if (ret)
		return -ret;
	return 0;
}

int test_wait_fd(int sk, time_t sec, bool write)
{
	struct timeval tv = { .tv_sec = sec, };

	return __test_wait_fd(sk, sec ? &tv : NULL, write);
}

static bool __skpair_poll_should_stop(int sk, struct tcp_counters *c,
				      test_cnt condition)
{
	struct tcp_counters c2;
	test_cnt diff;

	if (test_get_tcp_counters(sk, &c2))
		test_error("test_get_tcp_counters()");

	diff = test_cmp_counters(c, &c2);
	test_tcp_counters_free(&c2);
	return (diff & condition) == condition;
}

/* How often wake up and check netns counters & paired (*err) */
#define POLL_USEC	150
static int __test_skpair_poll(int sk, bool write, uint64_t timeout,
			      struct tcp_counters *c, test_cnt cond,
			      volatile int *err)
{
	uint64_t t;

	for (t = 0; t <= timeout * 1000000; t += POLL_USEC) {
		struct timeval tv = { .tv_usec = POLL_USEC, };
		int ret;

		ret = __test_wait_fd(sk, &tv, write);
		if (ret != -ETIMEDOUT)
			return ret;
		if (c && cond && __skpair_poll_should_stop(sk, c, cond))
			break;
		if (err && *err)
			return *err;
	}
	if (err)
		*err = -ETIMEDOUT;
	return -ETIMEDOUT;
}

int __test_connect_socket(int sk, const char *device,
			  void *addr, size_t addr_sz, bool async)
{
	long flags;
	int err;

	if (device != NULL) {
		err = setsockopt(sk, SOL_SOCKET, SO_BINDTODEVICE, device,
				 strlen(device) + 1);
		if (err < 0)
			test_error("setsockopt(SO_BINDTODEVICE, %s)", device);
	}

	flags = fcntl(sk, F_GETFL);
	if ((flags < 0) || (fcntl(sk, F_SETFL, flags | O_NONBLOCK) < 0))
		test_error("fcntl()");

	if (connect(sk, addr, addr_sz) < 0) {
		if (errno != EINPROGRESS) {
			err = -errno;
			goto out;
		}
		if (async)
			return sk;
		err = test_wait_fd(sk, TEST_TIMEOUT_SEC, 1);
		if (err)
			goto out;
	}
	return sk;

out:
	close(sk);
	return err;
}

int test_skpair_wait_poll(int sk, bool write,
			  test_cnt cond, volatile int *err)
{
	struct tcp_counters c;
	int ret;

	*err = 0;
	if (test_get_tcp_counters(sk, &c))
		test_error("test_get_tcp_counters()");
	synchronize_threads(); /* 1: init skpair & read nscounters */

	ret = __test_skpair_poll(sk, write, TEST_TIMEOUT_SEC, &c, cond, err);
	test_tcp_counters_free(&c);
	return ret;
}

int _test_skpair_connect_poll(int sk, const char *device,
			      void *addr, size_t addr_sz,
			      test_cnt condition, volatile int *err)
{
	struct tcp_counters c;
	int ret;

	*err = 0;
	if (test_get_tcp_counters(sk, &c))
		test_error("test_get_tcp_counters()");
	synchronize_threads(); /* 1: init skpair & read nscounters */
	ret = __test_connect_socket(sk, device, addr, addr_sz, true);
	if (ret < 0) {
		test_tcp_counters_free(&c);
		return (*err = ret);
	}
	ret = __test_skpair_poll(sk, 1, TEST_TIMEOUT_SEC, &c, condition, err);
	if (ret < 0)
		close(sk);
	test_tcp_counters_free(&c);
	return ret;
}

int __test_set_md5(int sk, void *addr, size_t addr_sz, uint8_t prefix,
		   int vrf, const char *password)
{
	size_t pwd_len = strlen(password);
	struct tcp_md5sig md5sig = {};

	md5sig.tcpm_keylen = pwd_len;
	memcpy(md5sig.tcpm_key, password, pwd_len);
	md5sig.tcpm_flags = TCP_MD5SIG_FLAG_PREFIX;
	md5sig.tcpm_prefixlen = prefix;
	if (vrf >= 0) {
		md5sig.tcpm_flags |= TCP_MD5SIG_FLAG_IFINDEX;
		md5sig.tcpm_ifindex = (uint8_t)vrf;
	}
	memcpy(&md5sig.tcpm_addr, addr, addr_sz);

	errno = 0;
	return setsockopt(sk, IPPROTO_TCP, TCP_MD5SIG_EXT,
			&md5sig, sizeof(md5sig));
}


int test_prepare_key_sockaddr(struct tcp_ao_add *ao, const char *alg,
		void *addr, size_t addr_sz, bool set_current, bool set_rnext,
		uint8_t prefix, uint8_t vrf, uint8_t sndid, uint8_t rcvid,
		uint8_t maclen, uint8_t keyflags,
		uint8_t keylen, const char *key)
{
	memset(ao, 0, sizeof(struct tcp_ao_add));

	ao->set_current	= !!set_current;
	ao->set_rnext	= !!set_rnext;
	ao->prefix	= prefix;
	ao->sndid	= sndid;
	ao->rcvid	= rcvid;
	ao->maclen	= maclen;
	ao->keyflags	= keyflags;
	ao->keylen	= keylen;
	ao->ifindex	= vrf;

	memcpy(&ao->addr, addr, addr_sz);

	if (strlen(alg) > 64)
		return -ENOBUFS;
	strncpy(ao->alg_name, alg, 64);

	memcpy(ao->key, key,
	       (keylen > TCP_AO_MAXKEYLEN) ? TCP_AO_MAXKEYLEN : keylen);
	return 0;
}

static int test_get_ao_keys_nr(int sk)
{
	struct tcp_ao_getsockopt tmp = {};
	socklen_t tmp_sz = sizeof(tmp);
	int ret;

	tmp.nkeys  = 1;
	tmp.get_all = 1;

	ret = getsockopt(sk, IPPROTO_TCP, TCP_AO_GET_KEYS, &tmp, &tmp_sz);
	if (ret)
		return -errno;
	return (int)tmp.nkeys;
}

int test_get_one_ao(int sk, struct tcp_ao_getsockopt *out,
		void *addr, size_t addr_sz, uint8_t prefix,
		uint8_t sndid, uint8_t rcvid)
{
	struct tcp_ao_getsockopt tmp = {};
	socklen_t tmp_sz = sizeof(tmp);
	int ret;

	memcpy(&tmp.addr, addr, addr_sz);
	tmp.prefix = prefix;
	tmp.sndid  = sndid;
	tmp.rcvid  = rcvid;
	tmp.nkeys  = 1;

	ret = getsockopt(sk, IPPROTO_TCP, TCP_AO_GET_KEYS, &tmp, &tmp_sz);
	if (ret)
		return ret;
	if (tmp.nkeys != 1)
		return -E2BIG;
	*out = tmp;
	return 0;
}

int test_get_ao_info(int sk, struct tcp_ao_info_opt *out)
{
	socklen_t sz = sizeof(*out);

	out->reserved = 0;
	out->reserved2 = 0;
	if (getsockopt(sk, IPPROTO_TCP, TCP_AO_INFO, out, &sz))
		return -errno;
	if (sz != sizeof(*out))
		return -EMSGSIZE;
	return 0;
}

int test_set_ao_info(int sk, struct tcp_ao_info_opt *in)
{
	socklen_t sz = sizeof(*in);

	in->reserved = 0;
	in->reserved2 = 0;
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_INFO, in, sz))
		return -errno;
	return 0;
}

int test_cmp_getsockopt_setsockopt(const struct tcp_ao_add *a,
				   const struct tcp_ao_getsockopt *b)
{
	bool is_kdf_aes_128_cmac = false;
	bool is_cmac_aes = false;

	if (!strcmp("cmac(aes128)", a->alg_name)) {
		is_kdf_aes_128_cmac = (a->keylen != 16);
		is_cmac_aes = true;
	}

#define __cmp_ao(member)						\
do {									\
	if (b->member != a->member) {					\
		test_fail("getsockopt(): " __stringify(member) " %u != %u",	\
				b->member, a->member);			\
		return -1;						\
	}								\
} while(0)
	__cmp_ao(sndid);
	__cmp_ao(rcvid);
	__cmp_ao(prefix);
	__cmp_ao(keyflags);
	__cmp_ao(ifindex);
	if (a->maclen) {
		__cmp_ao(maclen);
	} else if (b->maclen != 12) {
		test_fail("getsockopt(): expected default maclen 12, but it's %u",
				b->maclen);
		return -1;
	}
	if (!is_kdf_aes_128_cmac) {
		__cmp_ao(keylen);
	} else if (b->keylen != 16) {
		test_fail("getsockopt(): expected keylen 16 for cmac(aes128), but it's %u",
				b->keylen);
		return -1;
	}
#undef __cmp_ao
	if (!is_kdf_aes_128_cmac && memcmp(b->key, a->key, a->keylen)) {
		test_fail("getsockopt(): returned key is different `%s' != `%s'",
				b->key, a->key);
		return -1;
	}
	if (memcmp(&b->addr, &a->addr, sizeof(b->addr))) {
		test_fail("getsockopt(): returned address is different");
		return -1;
	}
	if (!is_cmac_aes && strcmp(b->alg_name, a->alg_name)) {
		test_fail("getsockopt(): returned algorithm %s is different than %s", b->alg_name, a->alg_name);
		return -1;
	}
	if (is_cmac_aes && strcmp(b->alg_name, "cmac(aes)")) {
		test_fail("getsockopt(): returned algorithm %s is different than cmac(aes)", b->alg_name);
		return -1;
	}
	/* For a established key rotation test don't add a key with
	 * set_current = 1, as it's likely to change by peer's request;
	 * rather use setsockopt(TCP_AO_INFO)
	 */
	if (a->set_current != b->is_current) {
		test_fail("getsockopt(): returned key is not Current_key");
		return -1;
	}
	if (a->set_rnext != b->is_rnext) {
		test_fail("getsockopt(): returned key is not RNext_key");
		return -1;
	}

	return 0;
}

int test_cmp_getsockopt_setsockopt_ao(const struct tcp_ao_info_opt *a,
				      const struct tcp_ao_info_opt *b)
{
	/* No check for ::current_key, as it may change by the peer */
	if (a->ao_required != b->ao_required) {
		test_fail("getsockopt(): returned ao doesn't have ao_required");
		return -1;
	}
	if (a->accept_icmps != b->accept_icmps) {
		test_fail("getsockopt(): returned ao doesn't accept ICMPs");
		return -1;
	}
	if (a->set_rnext && a->rnext != b->rnext) {
		test_fail("getsockopt(): RNext KeyID has changed");
		return -1;
	}
#define __cmp_cnt(member)						\
do {									\
	if (b->member != a->member) {					\
		test_fail("getsockopt(): " __stringify(member) " %llu != %llu",	\
				b->member, a->member);			\
		return -1;						\
	}								\
} while(0)
	if (a->set_counters) {
		__cmp_cnt(pkt_good);
		__cmp_cnt(pkt_bad);
		__cmp_cnt(pkt_key_not_found);
		__cmp_cnt(pkt_ao_required);
		__cmp_cnt(pkt_dropped_icmp);
	}
#undef __cmp_cnt
	return 0;
}

int test_get_tcp_counters(int sk, struct tcp_counters *out)
{
	struct tcp_ao_getsockopt *key_dump;
	socklen_t key_dump_sz = sizeof(*key_dump);
	struct tcp_ao_info_opt info = {};
	bool c1, c2, c3, c4, c5, c6, c7, c8;
	struct netstat *ns;
	int err, nr_keys;

	memset(out, 0, sizeof(*out));

	/* per-netns */
	ns = netstat_read();
	out->ao.netns_ao_good = netstat_get(ns, "TCPAOGood", &c1);
	out->ao.netns_ao_bad = netstat_get(ns, "TCPAOBad", &c2);
	out->ao.netns_ao_key_not_found = netstat_get(ns, "TCPAOKeyNotFound", &c3);
	out->ao.netns_ao_required = netstat_get(ns, "TCPAORequired", &c4);
	out->ao.netns_ao_dropped_icmp = netstat_get(ns, "TCPAODroppedIcmps", &c5);
	out->netns_md5_notfound = netstat_get(ns, "TCPMD5NotFound", &c6);
	out->netns_md5_unexpected = netstat_get(ns, "TCPMD5Unexpected", &c7);
	out->netns_md5_failure = netstat_get(ns, "TCPMD5Failure", &c8);
	netstat_free(ns);
	if (c1 || c2 || c3 || c4 || c5 || c6 || c7 || c8)
		return -EOPNOTSUPP;

	err = test_get_ao_info(sk, &info);
	if (err == -ENOENT)
		return 0;
	if (err)
		return err;

	/* per-socket */
	out->ao.ao_info_pkt_good = info.pkt_good;
	out->ao.ao_info_pkt_bad = info.pkt_bad;
	out->ao.ao_info_pkt_key_not_found = info.pkt_key_not_found;
	out->ao.ao_info_pkt_ao_required = info.pkt_ao_required;
	out->ao.ao_info_pkt_dropped_icmp = info.pkt_dropped_icmp;

	/* per-key */
	nr_keys = test_get_ao_keys_nr(sk);
	if (nr_keys < 0)
		return nr_keys;
	if (nr_keys == 0)
		test_error("test_get_ao_keys_nr() == 0");
	out->ao.nr_keys = (size_t)nr_keys;
	key_dump = calloc(nr_keys, key_dump_sz);
	if (!key_dump)
		return -errno;

	key_dump[0].nkeys = nr_keys;
	key_dump[0].get_all = 1;
	err = getsockopt(sk, IPPROTO_TCP, TCP_AO_GET_KEYS,
			 key_dump, &key_dump_sz);
	if (err) {
		free(key_dump);
		return -errno;
	}

	out->ao.key_cnts = calloc(nr_keys, sizeof(out->ao.key_cnts[0]));
	if (!out->ao.key_cnts) {
		free(key_dump);
		return -errno;
	}

	while (nr_keys--) {
		out->ao.key_cnts[nr_keys].sndid = key_dump[nr_keys].sndid;
		out->ao.key_cnts[nr_keys].rcvid = key_dump[nr_keys].rcvid;
		out->ao.key_cnts[nr_keys].pkt_good = key_dump[nr_keys].pkt_good;
		out->ao.key_cnts[nr_keys].pkt_bad = key_dump[nr_keys].pkt_bad;
	}
	free(key_dump);

	return 0;
}

test_cnt test_cmp_counters(struct tcp_counters *before,
			   struct tcp_counters *after)
{
#define __cmp(cnt, e_cnt)						\
do {									\
	if (before->cnt > after->cnt)					\
		test_error("counter " __stringify(cnt) " decreased");	\
	if (before->cnt != after->cnt)					\
		ret |= e_cnt;						\
} while (0)

	test_cnt ret = 0;
	size_t i;

	if (before->ao.nr_keys != after->ao.nr_keys)
		test_error("the number of keys has changed");

	_for_each_counter(__cmp);

	i = before->ao.nr_keys;
	while (i--) {
		__cmp(ao.key_cnts[i].pkt_good, TEST_CNT_KEY_GOOD);
		__cmp(ao.key_cnts[i].pkt_bad, TEST_CNT_KEY_BAD);
	}
#undef __cmp
	return ret;
}

int test_assert_counters_sk(const char *tst_name,
			    struct tcp_counters *before,
			    struct tcp_counters *after,
			    test_cnt expected)
{
#define __cmp_ao(cnt, e_cnt)						\
do {									\
	if (before->cnt > after->cnt) {					\
		test_fail("%s: Decreased counter " __stringify(cnt) " %" PRIu64 " > %" PRIu64, \
			  tst_name ?: "", before->cnt, after->cnt);	\
		return -1;						\
	}								\
	if ((before->cnt != after->cnt) != !!(expected & e_cnt)) {	\
		test_fail("%s: Counter " __stringify(cnt) " was %sexpected to increase %" PRIu64 " => %" PRIu64, \
			  tst_name ?: "", (expected & e_cnt) ? "" : "not ",	\
			  before->cnt, after->cnt);			\
		return -1;						\
	}								\
} while (0)

	errno = 0;
	_for_each_counter(__cmp_ao);
	return 0;
#undef __cmp_ao
}

int test_assert_counters_key(const char *tst_name,
			     struct tcp_ao_counters *before,
			     struct tcp_ao_counters *after,
			     test_cnt expected, int sndid, int rcvid)
{
	size_t i;
#define __cmp_ao(i, cnt, e_cnt)					\
do {									\
	if (before->key_cnts[i].cnt > after->key_cnts[i].cnt) {		\
		test_fail("%s: Decreased counter " __stringify(cnt) " %" PRIu64 " > %" PRIu64 " for key %u:%u", \
			  tst_name ?: "", before->key_cnts[i].cnt,	\
			  after->key_cnts[i].cnt,			\
			  before->key_cnts[i].sndid,			\
			  before->key_cnts[i].rcvid);			\
		return -1;						\
	}								\
	if ((before->key_cnts[i].cnt != after->key_cnts[i].cnt) != !!(expected & e_cnt)) {		\
		test_fail("%s: Counter " __stringify(cnt) " was %sexpected to increase %" PRIu64 " => %" PRIu64 " for key %u:%u", \
			  tst_name ?: "", (expected & e_cnt) ? "" : "not ",\
			  before->key_cnts[i].cnt,			\
			  after->key_cnts[i].cnt,			\
			  before->key_cnts[i].sndid,			\
			  before->key_cnts[i].rcvid);			\
		return -1;						\
	}								\
} while (0)

	if (before->nr_keys != after->nr_keys) {
		test_fail("%s: Keys changed on the socket %zu != %zu",
			  tst_name, before->nr_keys, after->nr_keys);
		return -1;
	}

	/* per-key */
	i = before->nr_keys;
	while (i--) {
		if (sndid >= 0 && before->key_cnts[i].sndid != sndid)
			continue;
		if (rcvid >= 0 && before->key_cnts[i].rcvid != rcvid)
			continue;
		__cmp_ao(i, pkt_good, TEST_CNT_KEY_GOOD);
		__cmp_ao(i, pkt_bad, TEST_CNT_KEY_BAD);
	}
	return 0;
#undef __cmp_ao
}

void test_tcp_counters_free(struct tcp_counters *cnts)
{
	free(cnts->ao.key_cnts);
}

#define TEST_BUF_SIZE 4096
static ssize_t _test_server_run(int sk, ssize_t quota, struct tcp_counters *c,
				test_cnt cond, volatile int *err,
				time_t timeout_sec)
{
	ssize_t total = 0;

	do {
		char buf[TEST_BUF_SIZE];
		ssize_t bytes, sent;
		int ret;

		ret = __test_skpair_poll(sk, 0, timeout_sec, c, cond, err);
		if (ret)
			return ret;

		bytes = recv(sk, buf, sizeof(buf), 0);

		if (bytes < 0)
			test_error("recv(): %zd", bytes);
		if (bytes == 0)
			break;

		ret = __test_skpair_poll(sk, 1, timeout_sec, c, cond, err);
		if (ret)
			return ret;

		sent = send(sk, buf, bytes, 0);
		if (sent == 0)
			break;
		if (sent != bytes)
			test_error("send()");
		total += bytes;
	} while (!quota || total < quota);

	return total;
}

ssize_t test_server_run(int sk, ssize_t quota, time_t timeout_sec)
{
	return _test_server_run(sk, quota, NULL, 0, NULL,
				timeout_sec ?: TEST_TIMEOUT_SEC);
}

int test_skpair_server(int sk, ssize_t quota, test_cnt cond, volatile int *err)
{
	struct tcp_counters c;
	ssize_t ret;

	*err = 0;
	if (test_get_tcp_counters(sk, &c))
		test_error("test_get_tcp_counters()");
	synchronize_threads(); /* 1: init skpair & read nscounters */

	ret = _test_server_run(sk, quota, &c, cond, err, TEST_TIMEOUT_SEC);
	test_tcp_counters_free(&c);
	return ret;
}

static ssize_t test_client_loop(int sk, size_t buf_sz, const size_t msg_len,
				struct tcp_counters *c, test_cnt cond,
				volatile int *err)
{
	char msg[msg_len];
	int nodelay = 1;
	char *buf;
	size_t i;

	buf = alloca(buf_sz);
	if (!buf)
		return -ENOMEM;
	randomize_buffer(buf, buf_sz);

	if (setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)))
		test_error("setsockopt(TCP_NODELAY)");

	for (i = 0; i < buf_sz; i += min(msg_len, buf_sz - i)) {
		size_t sent, bytes = min(msg_len, buf_sz - i);
		int ret;

		ret = __test_skpair_poll(sk, 1, TEST_TIMEOUT_SEC, c, cond, err);
		if (ret)
			return ret;

		sent = send(sk, buf + i, bytes, 0);
		if (sent == 0)
			break;
		if (sent != bytes)
			test_error("send()");

		bytes = 0;
		do {
			ssize_t got;

			ret = __test_skpair_poll(sk, 0, TEST_TIMEOUT_SEC,
						 c, cond, err);
			if (ret)
				return ret;

			got = recv(sk, msg + bytes, sizeof(msg) - bytes, 0);
			if (got <= 0)
				return i;
			bytes += got;
		} while (bytes < sent);
		if (bytes > sent)
			test_error("recv(): %zd > %zd", bytes, sent);
		if (memcmp(buf + i, msg, bytes) != 0) {
			test_fail("received message differs");
			return -1;
		}
	}
	return i;
}

int test_client_verify(int sk, const size_t msg_len, const size_t nr)
{
	size_t buf_sz = msg_len * nr;
	ssize_t ret;

	ret = test_client_loop(sk, buf_sz, msg_len, NULL, 0, NULL);
	if (ret < 0)
		return (int)ret;
	return ret != buf_sz ? -1 : 0;
}

int test_skpair_client(int sk, const size_t msg_len, const size_t nr,
		       test_cnt cond, volatile int *err)
{
	struct tcp_counters c;
	size_t buf_sz = msg_len * nr;
	ssize_t ret;

	*err = 0;
	if (test_get_tcp_counters(sk, &c))
		test_error("test_get_tcp_counters()");
	synchronize_threads(); /* 1: init skpair & read nscounters */

	ret = test_client_loop(sk, buf_sz, msg_len, &c, cond, err);
	test_tcp_counters_free(&c);
	if (ret < 0)
		return (int)ret;
	return ret != buf_sz ? -1 : 0;
}
