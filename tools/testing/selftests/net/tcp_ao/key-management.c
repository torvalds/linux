// SPDX-License-Identifier: GPL-2.0
/* Author: Dmitry Safonov <dima@arista.com> */
#include <inttypes.h>
#include "../../../../include/linux/kernel.h"
#include "aolib.h"

const size_t nr_packets = 20;
const size_t msg_len = 100;
const size_t quota = nr_packets * msg_len;
union tcp_addr wrong_addr;
#define SECOND_PASSWORD	"at all times sincere friends of freedom have been rare"
#define fault(type)	(inj == FAULT_ ## type)

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
		test_error("Failed to add a route to VRF");
}


static int prepare_sk(union tcp_addr *addr, uint8_t sndid, uint8_t rcvid)
{
	int sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);

	if (sk < 0)
		test_error("socket()");

	if (test_add_key(sk, DEFAULT_TEST_PASSWORD, this_ip_dest,
			 DEFAULT_TEST_PREFIX, 100, 100))
		test_error("test_add_key()");

	if (addr && test_add_key(sk, SECOND_PASSWORD, *addr,
				 DEFAULT_TEST_PREFIX, sndid, rcvid))
		test_error("test_add_key()");

	return sk;
}

static int prepare_lsk(union tcp_addr *addr, uint8_t sndid, uint8_t rcvid)
{
	int sk = prepare_sk(addr, sndid, rcvid);

	if (listen(sk, 10))
		test_error("listen()");

	return sk;
}

static int test_del_key(int sk, uint8_t sndid, uint8_t rcvid, bool async,
			int current_key, int rnext_key)
{
	struct tcp_ao_info_opt ao_info = {};
	struct tcp_ao_getsockopt key = {};
	struct tcp_ao_del del = {};
	sockaddr_af sockaddr;
	int err;

	tcp_addr_to_sockaddr_in(&del.addr, &this_ip_dest, 0);
	del.prefix = DEFAULT_TEST_PREFIX;
	del.sndid = sndid;
	del.rcvid = rcvid;

	if (current_key >= 0) {
		del.set_current = 1;
		del.current_key = (uint8_t)current_key;
	}
	if (rnext_key >= 0) {
		del.set_rnext = 1;
		del.rnext = (uint8_t)rnext_key;
	}

	err = setsockopt(sk, IPPROTO_TCP, TCP_AO_DEL_KEY, &del, sizeof(del));
	if (err < 0)
		return -errno;

	if (async)
		return 0;

	tcp_addr_to_sockaddr_in(&sockaddr, &this_ip_dest, 0);
	err = test_get_one_ao(sk, &key, &sockaddr, sizeof(sockaddr),
			      DEFAULT_TEST_PREFIX, sndid, rcvid);
	if (!err)
		return -EEXIST;
	if (err != -E2BIG)
		test_error("getsockopt()");
	if (current_key < 0 && rnext_key < 0)
		return 0;
	if (test_get_ao_info(sk, &ao_info))
		test_error("getsockopt(TCP_AO_INFO) failed");
	if (current_key >= 0 && ao_info.current_key != (uint8_t)current_key)
		return -ENOTRECOVERABLE;
	if (rnext_key >= 0 && ao_info.rnext != (uint8_t)rnext_key)
		return -ENOTRECOVERABLE;
	return 0;
}

static void try_delete_key(char *tst_name, int sk, uint8_t sndid, uint8_t rcvid,
			   bool async, int current_key, int rnext_key,
			   fault_t inj)
{
	int err;

	err = test_del_key(sk, sndid, rcvid, async, current_key, rnext_key);
	if ((err == -EBUSY && fault(BUSY)) || (err == -EINVAL && fault(CURRNEXT))) {
		test_ok("%s: key deletion was prevented", tst_name);
		return;
	}
	if (err && fault(FIXME)) {
		test_xfail("%s: failed to delete the key %u:%u %d",
			   tst_name, sndid, rcvid, err);
		return;
	}
	if (!err) {
		if (fault(BUSY) || fault(CURRNEXT)) {
			test_fail("%s: the key was deleted %u:%u %d", tst_name,
				  sndid, rcvid, err);
		} else {
			test_ok("%s: the key was deleted", tst_name);
		}
		return;
	}
	test_fail("%s: can't delete the key %u:%u %d", tst_name, sndid, rcvid, err);
}

static int test_set_key(int sk, int current_keyid, int rnext_keyid)
{
	struct tcp_ao_info_opt ao_info = {};
	int err;

	if (current_keyid >= 0) {
		ao_info.set_current = 1;
		ao_info.current_key = (uint8_t)current_keyid;
	}
	if (rnext_keyid >= 0) {
		ao_info.set_rnext = 1;
		ao_info.rnext = (uint8_t)rnext_keyid;
	}

	err = test_set_ao_info(sk, &ao_info);
	if (err)
		return err;
	if (test_get_ao_info(sk, &ao_info))
		test_error("getsockopt(TCP_AO_INFO) failed");
	if (current_keyid >= 0 && ao_info.current_key != (uint8_t)current_keyid)
		return -ENOTRECOVERABLE;
	if (rnext_keyid >= 0 && ao_info.rnext != (uint8_t)rnext_keyid)
		return -ENOTRECOVERABLE;
	return 0;
}

static int test_add_current_rnext_key(int sk, const char *key, uint8_t keyflags,
				      union tcp_addr in_addr, uint8_t prefix,
				      bool set_current, bool set_rnext,
				      uint8_t sndid, uint8_t rcvid)
{
	struct tcp_ao_add tmp = {};
	int err;

	err = test_prepare_key(&tmp, DEFAULT_TEST_ALGO, in_addr,
			       set_current, set_rnext,
			       prefix, 0, sndid, rcvid, 0, keyflags,
			       strlen(key), key);
	if (err)
		return err;


	err = setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &tmp, sizeof(tmp));
	if (err < 0)
		return -errno;

	return test_verify_socket_key(sk, &tmp);
}

static int __try_add_current_rnext_key(int sk, const char *key, uint8_t keyflags,
				       union tcp_addr in_addr, uint8_t prefix,
				       bool set_current, bool set_rnext,
				       uint8_t sndid, uint8_t rcvid)
{
	struct tcp_ao_info_opt ao_info = {};
	int err;

	err = test_add_current_rnext_key(sk, key, keyflags, in_addr, prefix,
					 set_current, set_rnext, sndid, rcvid);
	if (err)
		return err;

	if (test_get_ao_info(sk, &ao_info))
		test_error("getsockopt(TCP_AO_INFO) failed");
	if (set_current && ao_info.current_key != sndid)
		return -ENOTRECOVERABLE;
	if (set_rnext && ao_info.rnext != rcvid)
		return -ENOTRECOVERABLE;
	return 0;
}

static void try_add_current_rnext_key(char *tst_name, int sk, const char *key,
				     uint8_t keyflags,
				     union tcp_addr in_addr, uint8_t prefix,
				     bool set_current, bool set_rnext,
				     uint8_t sndid, uint8_t rcvid, fault_t inj)
{
	int err;

	err = __try_add_current_rnext_key(sk, key, keyflags, in_addr, prefix,
					  set_current, set_rnext, sndid, rcvid);
	if (!err && !fault(CURRNEXT)) {
		test_ok("%s", tst_name);
		return;
	}
	if (err == -EINVAL && fault(CURRNEXT)) {
		test_ok("%s", tst_name);
		return;
	}
	test_fail("%s", tst_name);
}

static void check_closed_socket(void)
{
	int sk;

	sk = prepare_sk(&this_ip_dest, 200, 200);
	try_delete_key("closed socket, delete a key", sk, 200, 200, 0, -1, -1, 0);
	try_delete_key("closed socket, delete all keys", sk, 100, 100, 0, -1, -1, 0);
	close(sk);

	sk = prepare_sk(&this_ip_dest, 200, 200);
	if (test_set_key(sk, 100, 200))
		test_error("failed to set current/rnext keys");
	try_delete_key("closed socket, delete current key", sk, 100, 100, 0, -1, -1, FAULT_BUSY);
	try_delete_key("closed socket, delete rnext key", sk, 200, 200, 0, -1, -1, FAULT_BUSY);
	close(sk);

	sk = prepare_sk(&this_ip_dest, 200, 200);
	if (test_add_key(sk, "Glory to heros!", this_ip_dest,
			 DEFAULT_TEST_PREFIX, 10, 11))
		test_error("test_add_key()");
	if (test_add_key(sk, "Glory to Ukraine!", this_ip_dest,
			 DEFAULT_TEST_PREFIX, 12, 13))
		test_error("test_add_key()");
	try_delete_key("closed socket, delete a key + set current/rnext", sk, 100, 100, 0, 10, 13, 0);
	try_delete_key("closed socket, force-delete current key", sk, 10, 11, 0, 200, -1, 0);
	try_delete_key("closed socket, force-delete rnext key", sk, 12, 13, 0, -1, 200, 0);
	try_delete_key("closed socket, delete current+rnext key", sk, 200, 200, 0, -1, -1, FAULT_BUSY);
	close(sk);

	sk = prepare_sk(&this_ip_dest, 200, 200);
	if (test_set_key(sk, 100, 200))
		test_error("failed to set current/rnext keys");
	try_add_current_rnext_key("closed socket, add + change current key",
				  sk, "Laaaa! Lalala-la-la-lalala...", 0,
				  this_ip_dest, DEFAULT_TEST_PREFIX,
				  true, false, 10, 20, 0);
	try_add_current_rnext_key("closed socket, add + change rnext key",
				  sk, "Laaaa! Lalala-la-la-lalala...", 0,
				  this_ip_dest, DEFAULT_TEST_PREFIX,
				  false, true, 20, 10, 0);
	close(sk);
}

static void assert_no_current_rnext(const char *tst_msg, int sk)
{
	struct tcp_ao_info_opt ao_info = {};

	if (test_get_ao_info(sk, &ao_info))
		test_error("getsockopt(TCP_AO_INFO) failed");

	errno = 0;
	if (ao_info.set_current || ao_info.set_rnext) {
		test_xfail("%s: the socket has current/rnext keys: %d:%d",
			   tst_msg,
			   (ao_info.set_current) ? ao_info.current_key : -1,
			   (ao_info.set_rnext) ? ao_info.rnext : -1);
	} else {
		test_ok("%s: the socket has no current/rnext keys", tst_msg);
	}
}

static void assert_no_tcp_repair(void)
{
	struct tcp_ao_repair ao_img = {};
	socklen_t len = sizeof(ao_img);
	int sk, err;

	sk = prepare_sk(&this_ip_dest, 200, 200);
	test_enable_repair(sk);
	if (listen(sk, 10))
		test_error("listen()");
	errno = 0;
	err = getsockopt(sk, SOL_TCP, TCP_AO_REPAIR, &ao_img, &len);
	if (err && errno == EPERM)
		test_ok("listen socket, getsockopt(TCP_AO_REPAIR) is restricted");
	else
		test_fail("listen socket, getsockopt(TCP_AO_REPAIR) works");
	errno = 0;
	err = setsockopt(sk, SOL_TCP, TCP_AO_REPAIR, &ao_img, sizeof(ao_img));
	if (err && errno == EPERM)
		test_ok("listen socket, setsockopt(TCP_AO_REPAIR) is restricted");
	else
		test_fail("listen socket, setsockopt(TCP_AO_REPAIR) works");
	close(sk);
}

static void check_listen_socket(void)
{
	int sk, err;

	sk = prepare_lsk(&this_ip_dest, 200, 200);
	try_delete_key("listen socket, delete a key", sk, 200, 200, 0, -1, -1, 0);
	try_delete_key("listen socket, delete all keys", sk, 100, 100, 0, -1, -1, 0);
	close(sk);

	sk = prepare_lsk(&this_ip_dest, 200, 200);
	err = test_set_key(sk, 100, -1);
	if (err == -EINVAL)
		test_ok("listen socket, setting current key not allowed");
	else
		test_fail("listen socket, set current key");
	err = test_set_key(sk, -1, 200);
	if (err == -EINVAL)
		test_ok("listen socket, setting rnext key not allowed");
	else
		test_fail("listen socket, set rnext key");
	close(sk);

	sk = prepare_sk(&this_ip_dest, 200, 200);
	if (test_set_key(sk, 100, 200))
		test_error("failed to set current/rnext keys");
	if (listen(sk, 10))
		test_error("listen()");
	assert_no_current_rnext("listen() after current/rnext keys set", sk);
	try_delete_key("listen socket, delete current key from before listen()", sk, 100, 100, 0, -1, -1, FAULT_FIXME);
	try_delete_key("listen socket, delete rnext key from before listen()", sk, 200, 200, 0, -1, -1, FAULT_FIXME);
	close(sk);

	assert_no_tcp_repair();

	sk = prepare_lsk(&this_ip_dest, 200, 200);
	if (test_add_key(sk, "Glory to heros!", this_ip_dest,
			 DEFAULT_TEST_PREFIX, 10, 11))
		test_error("test_add_key()");
	if (test_add_key(sk, "Glory to Ukraine!", this_ip_dest,
			 DEFAULT_TEST_PREFIX, 12, 13))
		test_error("test_add_key()");
	try_delete_key("listen socket, delete a key + set current/rnext", sk,
		       100, 100, 0, 10, 13, FAULT_CURRNEXT);
	try_delete_key("listen socket, force-delete current key", sk,
		       10, 11, 0, 200, -1, FAULT_CURRNEXT);
	try_delete_key("listen socket, force-delete rnext key", sk,
		       12, 13, 0, -1, 200, FAULT_CURRNEXT);
	try_delete_key("listen socket, delete a key", sk,
		       200, 200, 0, -1, -1, 0);
	close(sk);

	sk = prepare_lsk(&this_ip_dest, 200, 200);
	try_add_current_rnext_key("listen socket, add + change current key",
				  sk, "Laaaa! Lalala-la-la-lalala...", 0,
				  this_ip_dest, DEFAULT_TEST_PREFIX,
				  true, false, 10, 20, FAULT_CURRNEXT);
	try_add_current_rnext_key("listen socket, add + change rnext key",
				  sk, "Laaaa! Lalala-la-la-lalala...", 0,
				  this_ip_dest, DEFAULT_TEST_PREFIX,
				  false, true, 20, 10, FAULT_CURRNEXT);
	close(sk);
}

static const char *fips_fpath = "/proc/sys/crypto/fips_enabled";
static bool is_fips_enabled(void)
{
	static int fips_checked = -1;
	FILE *fenabled;
	int enabled;

	if (fips_checked >= 0)
		return !!fips_checked;
	if (access(fips_fpath, R_OK)) {
		if (errno != ENOENT)
			test_error("Can't open %s", fips_fpath);
		fips_checked = 0;
		return false;
	}
	fenabled = fopen(fips_fpath, "r");
	if (!fenabled)
		test_error("Can't open %s", fips_fpath);
	if (fscanf(fenabled, "%d", &enabled) != 1)
		test_error("Can't read from %s", fips_fpath);
	fclose(fenabled);
	fips_checked = !!enabled;
	return !!fips_checked;
}

struct test_key {
	char password[TCP_AO_MAXKEYLEN];
	const char *alg;
	unsigned int len;
	uint8_t client_keyid;
	uint8_t server_keyid;
	uint8_t maclen;
	uint8_t matches_client		: 1,
		matches_server		: 1,
		matches_vrf		: 1,
		is_current		: 1,
		is_rnext		: 1,
		used_on_handshake	: 1,
		used_after_accept	: 1,
		used_on_client		: 1;
};

struct key_collection {
	unsigned int nr_keys;
	struct test_key *keys;
};

static struct key_collection collection;

#define TEST_MAX_MACLEN		16
const char *test_algos[] = {
	"cmac(aes128)",
	"hmac(sha1)", "hmac(sha512)", "hmac(sha384)", "hmac(sha256)",
	"hmac(sha224)", "hmac(sha3-512)",
	/* only if !CONFIG_FIPS */
#define TEST_NON_FIPS_ALGOS	2
	"hmac(rmd160)", "hmac(md5)"
};
const unsigned int test_maclens[] = { 1, 4, 12, 16 };
#define MACLEN_SHIFT		2
#define ALGOS_SHIFT		4

static unsigned int make_mask(unsigned int shift, unsigned int prev_shift)
{
	unsigned int ret = BIT(shift) - 1;

	return ret << prev_shift;
}

static void init_key_in_collection(unsigned int index, bool randomized)
{
	struct test_key *key = &collection.keys[index];
	unsigned int algos_nr, algos_index;

	/* Same for randomized and non-randomized test flows */
	key->client_keyid = index;
	key->server_keyid = 127 + index;
	key->matches_client = 1;
	key->matches_server = 1;
	key->matches_vrf = 1;
	/* not really even random, but good enough for a test */
	key->len = rand() % (TCP_AO_MAXKEYLEN - TEST_TCP_AO_MINKEYLEN);
	key->len += TEST_TCP_AO_MINKEYLEN;
	randomize_buffer(key->password, key->len);

	if (randomized) {
		key->maclen = (rand() % TEST_MAX_MACLEN) + 1;
		algos_index = rand();
	} else {
		unsigned int shift = MACLEN_SHIFT;

		key->maclen = test_maclens[index & make_mask(shift, 0)];
		algos_index = index & make_mask(ALGOS_SHIFT, shift);
	}
	algos_nr = ARRAY_SIZE(test_algos);
	if (is_fips_enabled())
		algos_nr -= TEST_NON_FIPS_ALGOS;
	key->alg = test_algos[algos_index % algos_nr];
}

static int init_default_key_collection(unsigned int nr_keys, bool randomized)
{
	size_t key_sz = sizeof(collection.keys[0]);

	if (!nr_keys) {
		free(collection.keys);
		collection.keys = NULL;
		return 0;
	}

	/*
	 * All keys have uniq sndid/rcvid and sndid != rcvid in order to
	 * check for any bugs/issues for different keyids, visible to both
	 * peers. Keyid == 254 is unused.
	 */
	if (nr_keys > 127)
		test_error("Test requires too many keys, correct the source");

	collection.keys = reallocarray(collection.keys, nr_keys, key_sz);
	if (!collection.keys)
		return -ENOMEM;

	memset(collection.keys, 0, nr_keys * key_sz);
	collection.nr_keys = nr_keys;
	while (nr_keys--)
		init_key_in_collection(nr_keys, randomized);

	return 0;
}

static void test_key_error(const char *msg, struct test_key *key)
{
	test_error("%s: key: { %s, %u:%u, %u, %u:%u:%u:%u:%u (%u)}",
		   msg, key->alg, key->client_keyid, key->server_keyid,
		   key->maclen, key->matches_client, key->matches_server,
		   key->matches_vrf, key->is_current, key->is_rnext, key->len);
}

static int test_add_key_cr(int sk, const char *pwd, unsigned int pwd_len,
			   union tcp_addr addr, uint8_t vrf,
			   uint8_t sndid, uint8_t rcvid,
			   uint8_t maclen, const char *alg,
			   bool set_current, bool set_rnext)
{
	struct tcp_ao_add tmp = {};
	uint8_t keyflags = 0;
	int err;

	if (!alg)
		alg = DEFAULT_TEST_ALGO;

	if (vrf)
		keyflags |= TCP_AO_KEYF_IFINDEX;
	err = test_prepare_key(&tmp, alg, addr, set_current, set_rnext,
			       DEFAULT_TEST_PREFIX, vrf, sndid, rcvid, maclen,
			       keyflags, pwd_len, pwd);
	if (err)
		return err;

	err = setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &tmp, sizeof(tmp));
	if (err < 0)
		return -errno;

	return test_verify_socket_key(sk, &tmp);
}

static void verify_current_rnext(const char *tst, int sk,
				 int current_keyid, int rnext_keyid)
{
	struct tcp_ao_info_opt ao_info = {};

	if (test_get_ao_info(sk, &ao_info))
		test_error("getsockopt(TCP_AO_INFO) failed");

	errno = 0;
	if (current_keyid >= 0) {
		if (!ao_info.set_current)
			test_fail("%s: the socket doesn't have current key", tst);
		else if (ao_info.current_key != current_keyid)
			test_fail("%s: current key is not the expected one %d != %u",
				  tst, current_keyid, ao_info.current_key);
		else
			test_ok("%s: current key %u as expected",
				tst, ao_info.current_key);
	}
	if (rnext_keyid >= 0) {
		if (!ao_info.set_rnext)
			test_fail("%s: the socket doesn't have rnext key", tst);
		else if (ao_info.rnext != rnext_keyid)
			test_fail("%s: rnext key is not the expected one %d != %u",
				  tst, rnext_keyid, ao_info.rnext);
		else
			test_ok("%s: rnext key %u as expected", tst, ao_info.rnext);
	}
}


static int key_collection_socket(bool server, unsigned int port)
{
	unsigned int i;
	int sk;

	if (server)
		sk = test_listen_socket(this_ip_addr, port, 1);
	else
		sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0)
		test_error("socket()");

	for (i = 0; i < collection.nr_keys; i++) {
		struct test_key *key = &collection.keys[i];
		union tcp_addr *addr = &wrong_addr;
		uint8_t sndid, rcvid, vrf;
		bool set_current = false, set_rnext = false;

		if (key->matches_vrf)
			vrf = 0;
		else
			vrf = test_vrf_ifindex;
		if (server) {
			if (key->matches_client)
				addr = &this_ip_dest;
			sndid = key->server_keyid;
			rcvid = key->client_keyid;
		} else {
			if (key->matches_server)
				addr = &this_ip_dest;
			sndid = key->client_keyid;
			rcvid = key->server_keyid;
			set_current = key->is_current;
			set_rnext = key->is_rnext;
		}

		if (test_add_key_cr(sk, key->password, key->len,
				    *addr, vrf, sndid, rcvid, key->maclen,
				    key->alg, set_current, set_rnext))
			test_key_error("setsockopt(TCP_AO_ADD_KEY)", key);
		if (set_current || set_rnext)
			key->used_on_handshake = 1;
#ifdef DEBUG
		test_print("%s [%u/%u] key: { %s, %u:%u, %u, %u:%u:%u:%u (%u)}",
			   server ? "server" : "client", i, collection.nr_keys,
			   key->alg, rcvid, sndid, key->maclen,
			   key->matches_client, key->matches_server,
			   key->is_current, key->is_rnext, key->len);
#endif
	}
	return sk;
}

static void verify_counters(const char *tst_name, bool is_listen_sk, bool server,
			    struct tcp_ao_counters *a, struct tcp_ao_counters *b)
{
	unsigned int i;

	__test_tcp_ao_counters_cmp(tst_name, a, b, TEST_CNT_GOOD);

	for (i = 0; i < collection.nr_keys; i++) {
		struct test_key *key = &collection.keys[i];
		uint8_t sndid, rcvid;
		bool was_used;

		if (server) {
			sndid = key->server_keyid;
			rcvid = key->client_keyid;
			if (is_listen_sk)
				was_used = key->used_on_handshake;
			else
				was_used = key->used_after_accept;
		} else {
			sndid = key->client_keyid;
			rcvid = key->server_keyid;
			was_used = key->used_on_client;
		}

		test_tcp_ao_key_counters_cmp(tst_name, a, b, was_used,
					     sndid, rcvid);
	}
	test_tcp_ao_counters_free(a);
	test_tcp_ao_counters_free(b);
	test_ok("%s: passed counters checks", tst_name);
}

static struct tcp_ao_getsockopt *lookup_key(struct tcp_ao_getsockopt *buf,
					    size_t len, int sndid, int rcvid)
{
	size_t i;

	for (i = 0; i < len; i++) {
		if (sndid >= 0 && buf[i].sndid != sndid)
			continue;
		if (rcvid >= 0 && buf[i].rcvid != rcvid)
			continue;
		return &buf[i];
	}
	return NULL;
}

static void verify_keys(const char *tst_name, int sk,
			bool is_listen_sk, bool server)
{
	socklen_t len = sizeof(struct tcp_ao_getsockopt);
	struct tcp_ao_getsockopt *keys;
	bool passed_test = true;
	unsigned int i;

	keys = calloc(collection.nr_keys, len);
	if (!keys)
		test_error("calloc()");

	keys->nkeys = collection.nr_keys;
	keys->get_all = 1;

	if (getsockopt(sk, IPPROTO_TCP, TCP_AO_GET_KEYS, keys, &len)) {
		free(keys);
		test_error("getsockopt(TCP_AO_GET_KEYS)");
	}

	for (i = 0; i < collection.nr_keys; i++) {
		struct test_key *key = &collection.keys[i];
		struct tcp_ao_getsockopt *dump_key;
		bool is_kdf_aes_128_cmac = false;
		bool is_cmac_aes = false;
		uint8_t sndid, rcvid;
		bool matches = false;

		if (server) {
			if (key->matches_client)
				matches = true;
			sndid = key->server_keyid;
			rcvid = key->client_keyid;
		} else {
			if (key->matches_server)
				matches = true;
			sndid = key->client_keyid;
			rcvid = key->server_keyid;
		}
		if (!key->matches_vrf)
			matches = false;
		/* no keys get removed on the original listener socket */
		if (is_listen_sk)
			matches = true;

		dump_key = lookup_key(keys, keys->nkeys, sndid, rcvid);
		if (matches != !!dump_key) {
			test_fail("%s: key %u:%u %s%s on the socket",
				  tst_name, sndid, rcvid,
				  key->matches_vrf ? "" : "[vrf] ",
				  matches ? "disappeared" : "yet present");
			passed_test = false;
			goto out;
		}
		if (!dump_key)
			continue;

		if (!strcmp("cmac(aes128)", key->alg)) {
			is_kdf_aes_128_cmac = (key->len != 16);
			is_cmac_aes = true;
		}

		if (is_cmac_aes) {
			if (strcmp(dump_key->alg_name, "cmac(aes)")) {
				test_fail("%s: key %u:%u cmac(aes) has unexpected alg %s",
					  tst_name, sndid, rcvid,
					  dump_key->alg_name);
				passed_test = false;
				continue;
			}
		} else if (strcmp(dump_key->alg_name, key->alg)) {
			test_fail("%s: key %u:%u has unexpected alg %s != %s",
				  tst_name, sndid, rcvid,
				  dump_key->alg_name, key->alg);
			passed_test = false;
			continue;
		}
		if (is_kdf_aes_128_cmac) {
			if (dump_key->keylen != 16) {
				test_fail("%s: key %u:%u cmac(aes128) has unexpected len %u",
					  tst_name, sndid, rcvid,
					  dump_key->keylen);
				continue;
			}
		} else if (dump_key->keylen != key->len) {
			test_fail("%s: key %u:%u changed password len %u != %u",
				  tst_name, sndid, rcvid,
				  dump_key->keylen, key->len);
			passed_test = false;
			continue;
		}
		if (!is_kdf_aes_128_cmac &&
		    memcmp(dump_key->key, key->password, key->len)) {
			test_fail("%s: key %u:%u has different password",
				  tst_name, sndid, rcvid);
			passed_test = false;
			continue;
		}
		if (dump_key->maclen != key->maclen) {
			test_fail("%s: key %u:%u changed maclen %u != %u",
				  tst_name, sndid, rcvid,
				  dump_key->maclen, key->maclen);
			passed_test = false;
			continue;
		}
	}

	if (passed_test)
		test_ok("%s: The socket keys are consistent with the expectations",
			tst_name);
out:
	free(keys);
}

static int start_server(const char *tst_name, unsigned int port, size_t quota,
			struct tcp_ao_counters *begin,
			unsigned int current_index, unsigned int rnext_index)
{
	struct tcp_ao_counters lsk_c1, lsk_c2;
	ssize_t bytes;
	int sk, lsk;

	synchronize_threads(); /* 1: key collection initialized */
	lsk = key_collection_socket(true, port);
	if (test_get_tcp_ao_counters(lsk, &lsk_c1))
		test_error("test_get_tcp_ao_counters()");
	synchronize_threads(); /* 2: MKTs added => connect() */
	if (test_wait_fd(lsk, TEST_TIMEOUT_SEC, 0))
		test_error("test_wait_fd()");

	sk = accept(lsk, NULL, NULL);
	if (sk < 0)
		test_error("accept()");
	if (test_get_tcp_ao_counters(sk, begin))
		test_error("test_get_tcp_ao_counters()");

	synchronize_threads(); /* 3: accepted => send data */
	if (test_get_tcp_ao_counters(lsk, &lsk_c2))
		test_error("test_get_tcp_ao_counters()");
	verify_keys(tst_name, lsk, true, true);
	close(lsk);

	bytes = test_server_run(sk, quota, TEST_TIMEOUT_SEC);
	if (bytes != quota)
		test_fail("%s: server served: %zd", tst_name, bytes);
	else
		test_ok("%s: server alive", tst_name);

	verify_counters(tst_name, true, true, &lsk_c1, &lsk_c2);

	return sk;
}

static void end_server(const char *tst_name, int sk,
		       struct tcp_ao_counters *begin)
{
	struct tcp_ao_counters end;

	if (test_get_tcp_ao_counters(sk, &end))
		test_error("test_get_tcp_ao_counters()");
	verify_keys(tst_name, sk, false, true);

	synchronize_threads(); /* 4: verified => closed */
	close(sk);

	verify_counters(tst_name, true, false, begin, &end);
	synchronize_threads(); /* 5: counters */
}

static void try_server_run(const char *tst_name, unsigned int port, size_t quota,
			   unsigned int current_index, unsigned int rnext_index)
{
	struct tcp_ao_counters tmp;
	int sk;

	sk = start_server(tst_name, port, quota, &tmp,
			  current_index, rnext_index);
	end_server(tst_name, sk, &tmp);
}

static void server_rotations(const char *tst_name, unsigned int port,
			     size_t quota, unsigned int rotations,
			     unsigned int current_index, unsigned int rnext_index)
{
	struct tcp_ao_counters tmp;
	unsigned int i;
	int sk;

	sk = start_server(tst_name, port, quota, &tmp,
			  current_index, rnext_index);

	for (i = current_index + 1; rotations > 0; i++, rotations--) {
		ssize_t bytes;

		if (i >= collection.nr_keys)
			i = 0;
		bytes = test_server_run(sk, quota, TEST_TIMEOUT_SEC);
		if (bytes != quota) {
			test_fail("%s: server served: %zd", tst_name, bytes);
			return;
		}
		verify_current_rnext(tst_name, sk,
				     collection.keys[i].server_keyid, -1);
		synchronize_threads(); /* verify current/rnext */
	}
	end_server(tst_name, sk, &tmp);
}

static int run_client(const char *tst_name, unsigned int port,
		      unsigned int nr_keys, int current_index, int rnext_index,
		      struct tcp_ao_counters *before,
		      const size_t msg_sz, const size_t msg_nr)
{
	int sk;

	synchronize_threads(); /* 1: key collection initialized */
	sk = key_collection_socket(false, port);

	if (current_index >= 0 || rnext_index >= 0) {
		int sndid = -1, rcvid = -1;

		if (current_index >= 0)
			sndid = collection.keys[current_index].client_keyid;
		if (rnext_index >= 0)
			rcvid = collection.keys[rnext_index].server_keyid;
		if (test_set_key(sk, sndid, rcvid))
			test_error("failed to set current/rnext keys");
	}
	if (before && test_get_tcp_ao_counters(sk, before))
		test_error("test_get_tcp_ao_counters()");

	synchronize_threads(); /* 2: MKTs added => connect() */
	if (test_connect_socket(sk, this_ip_dest, port++) <= 0)
		test_error("failed to connect()");
	if (current_index < 0)
		current_index = nr_keys - 1;
	if (rnext_index < 0)
		rnext_index = nr_keys - 1;
	collection.keys[current_index].used_on_handshake = 1;
	collection.keys[rnext_index].used_after_accept = 1;
	collection.keys[rnext_index].used_on_client = 1;

	synchronize_threads(); /* 3: accepted => send data */
	if (test_client_verify(sk, msg_sz, msg_nr, TEST_TIMEOUT_SEC)) {
		test_fail("verify failed");
		close(sk);
		if (before)
			test_tcp_ao_counters_free(before);
		return -1;
	}

	return sk;
}

static int start_client(const char *tst_name, unsigned int port,
			unsigned int nr_keys, int current_index, int rnext_index,
			struct tcp_ao_counters *before,
			const size_t msg_sz, const size_t msg_nr)
{
	if (init_default_key_collection(nr_keys, true))
		test_error("Failed to init the key collection");

	return run_client(tst_name, port, nr_keys, current_index,
			  rnext_index, before, msg_sz, msg_nr);
}

static void end_client(const char *tst_name, int sk, unsigned int nr_keys,
		       int current_index, int rnext_index,
		       struct tcp_ao_counters *start)
{
	struct tcp_ao_counters end;

	/* Some application may become dependent on this kernel choice */
	if (current_index < 0)
		current_index = nr_keys - 1;
	if (rnext_index < 0)
		rnext_index = nr_keys - 1;
	verify_current_rnext(tst_name, sk,
			     collection.keys[current_index].client_keyid,
			     collection.keys[rnext_index].server_keyid);
	if (start && test_get_tcp_ao_counters(sk, &end))
		test_error("test_get_tcp_ao_counters()");
	verify_keys(tst_name, sk, false, false);
	synchronize_threads(); /* 4: verify => closed */
	close(sk);
	if (start)
		verify_counters(tst_name, false, false, start, &end);
	synchronize_threads(); /* 5: counters */
}

static void try_unmatched_keys(int sk, int *rnext_index)
{
	struct test_key *key;
	unsigned int i = 0;
	int err;

	do {
		key = &collection.keys[i];
		if (!key->matches_server)
			break;
	} while (++i < collection.nr_keys);
	if (key->matches_server)
		test_error("all keys on client match the server");

	err = test_add_key_cr(sk, key->password, key->len, wrong_addr,
			      0, key->client_keyid, key->server_keyid,
			      key->maclen, key->alg, 0, 0);
	if (!err) {
		test_fail("Added a key with non-matching ip-address for established sk");
		return;
	}
	if (err == -EINVAL)
		test_ok("Can't add a key with non-matching ip-address for established sk");
	else
		test_error("Failed to add a key");

	err = test_add_key_cr(sk, key->password, key->len, this_ip_dest,
			      test_vrf_ifindex,
			      key->client_keyid, key->server_keyid,
			      key->maclen, key->alg, 0, 0);
	if (!err) {
		test_fail("Added a key with non-matching VRF for established sk");
		return;
	}
	if (err == -EINVAL)
		test_ok("Can't add a key with non-matching VRF for established sk");
	else
		test_error("Failed to add a key");

	for (i = 0; i < collection.nr_keys; i++) {
		key = &collection.keys[i];
		if (!key->matches_client)
			break;
	}
	if (key->matches_client)
		test_error("all keys on server match the client");
	if (test_set_key(sk, -1, key->server_keyid))
		test_error("Can't change the current key");
	if (test_client_verify(sk, msg_len, nr_packets, TEST_TIMEOUT_SEC))
		test_fail("verify failed");
	*rnext_index = i;
}

static int client_non_matching(const char *tst_name, unsigned int port,
			       unsigned int nr_keys,
			       int current_index, int rnext_index,
			       const size_t msg_sz, const size_t msg_nr)
{
	unsigned int i;

	if (init_default_key_collection(nr_keys, true))
		test_error("Failed to init the key collection");

	for (i = 0; i < nr_keys; i++) {
		/* key (0, 0) matches */
		collection.keys[i].matches_client = !!((i + 3) % 4);
		collection.keys[i].matches_server = !!((i + 2) % 4);
		if (kernel_config_has(KCONFIG_NET_VRF))
			collection.keys[i].matches_vrf = !!((i + 1) % 4);
	}

	return run_client(tst_name, port, nr_keys, current_index,
			  rnext_index, NULL, msg_sz, msg_nr);
}

static void check_current_back(const char *tst_name, unsigned int port,
			       unsigned int nr_keys,
			       unsigned int current_index, unsigned int rnext_index,
			       unsigned int rotate_to_index)
{
	struct tcp_ao_counters tmp;
	int sk;

	sk = start_client(tst_name, port, nr_keys, current_index, rnext_index,
			  &tmp, msg_len, nr_packets);
	if (sk < 0)
		return;
	if (test_set_key(sk, collection.keys[rotate_to_index].client_keyid, -1))
		test_error("Can't change the current key");
	if (test_client_verify(sk, msg_len, nr_packets, TEST_TIMEOUT_SEC))
		test_fail("verify failed");
	collection.keys[rotate_to_index].used_after_accept = 1;

	end_client(tst_name, sk, nr_keys, current_index, rnext_index, &tmp);
}

static void roll_over_keys(const char *tst_name, unsigned int port,
			   unsigned int nr_keys, unsigned int rotations,
			   unsigned int current_index, unsigned int rnext_index)
{
	struct tcp_ao_counters tmp;
	unsigned int i;
	int sk;

	sk = start_client(tst_name, port, nr_keys, current_index, rnext_index,
			  &tmp, msg_len, nr_packets);
	if (sk < 0)
		return;
	for (i = rnext_index + 1; rotations > 0; i++, rotations--) {
		if (i >= collection.nr_keys)
			i = 0;
		if (test_set_key(sk, -1, collection.keys[i].server_keyid))
			test_error("Can't change the Rnext key");
		if (test_client_verify(sk, msg_len, nr_packets, TEST_TIMEOUT_SEC)) {
			test_fail("verify failed");
			close(sk);
			test_tcp_ao_counters_free(&tmp);
			return;
		}
		verify_current_rnext(tst_name, sk, -1,
				     collection.keys[i].server_keyid);
		collection.keys[i].used_on_client = 1;
		synchronize_threads(); /* verify current/rnext */
	}
	end_client(tst_name, sk, nr_keys, current_index, rnext_index, &tmp);
}

static void try_client_run(const char *tst_name, unsigned int port,
			   unsigned int nr_keys, int current_index, int rnext_index)
{
	struct tcp_ao_counters tmp;
	int sk;

	sk = start_client(tst_name, port, nr_keys, current_index, rnext_index,
			  &tmp, msg_len, nr_packets);
	if (sk < 0)
		return;
	end_client(tst_name, sk, nr_keys, current_index, rnext_index, &tmp);
}

static void try_client_match(const char *tst_name, unsigned int port,
			     unsigned int nr_keys,
			     int current_index, int rnext_index)
{
	int sk;

	sk = client_non_matching(tst_name, port, nr_keys, current_index,
				 rnext_index, msg_len, nr_packets);
	if (sk < 0)
		return;
	try_unmatched_keys(sk, &rnext_index);
	end_client(tst_name, sk, nr_keys, current_index, rnext_index, NULL);
}

static void *server_fn(void *arg)
{
	unsigned int port = test_server_port;

	setup_vrfs();
	try_server_run("server: Check current/rnext keys unset before connect()",
		       port++, quota, 19, 19);
	try_server_run("server: Check current/rnext keys set before connect()",
		       port++, quota, 10, 10);
	try_server_run("server: Check current != rnext keys set before connect()",
		       port++, quota, 5, 10);
	try_server_run("server: Check current flapping back on peer's RnextKey request",
		       port++, quota * 2, 5, 10);
	server_rotations("server: Rotate over all different keys", port++,
			 quota, 20, 0, 0);
	try_server_run("server: Check accept() => established key matching",
		       port++, quota * 2, 0, 0);

	synchronize_threads(); /* don't race to exit: client exits */
	return NULL;
}

static void check_established_socket(void)
{
	unsigned int port = test_server_port;

	setup_vrfs();
	try_client_run("client: Check current/rnext keys unset before connect()",
		       port++, 20, -1, -1);
	try_client_run("client: Check current/rnext keys set before connect()",
		       port++, 20, 10, 10);
	try_client_run("client: Check current != rnext keys set before connect()",
		       port++, 20, 10, 5);
	check_current_back("client: Check current flapping back on peer's RnextKey request",
			   port++, 20, 10, 5, 2);
	roll_over_keys("client: Rotate over all different keys", port++,
		       20, 20, 0, 0);
	try_client_match("client: Check connect() => established key matching",
			 port++, 20, 0, 0);
}

static void *client_fn(void *arg)
{
	if (inet_pton(TEST_FAMILY, TEST_WRONG_IP, &wrong_addr) != 1)
		test_error("Can't convert ip address %s", TEST_WRONG_IP);
	check_closed_socket();
	check_listen_socket();
	check_established_socket();
	return NULL;
}

int main(int argc, char *argv[])
{
	test_init(120, server_fn, client_fn);
	return 0;
}
