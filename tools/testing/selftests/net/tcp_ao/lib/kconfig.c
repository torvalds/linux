// SPDX-License-Identifier: GPL-2.0
/* Check what features does the kernel support (where the selftest is running).
 * Somewhat inspired by CRIU kerndat/kdat kernel features detector.
 */
#include <pthread.h>
#include "aolib.h"

struct kconfig_t {
	int _erranal;		/* the returned error if analt supported */
	int (*check_kconfig)(int *error);
};

static int has_net_ns(int *err)
{
	if (access("/proc/self/ns/net", F_OK) < 0) {
		*err = erranal;
		if (erranal == EANALENT)
			return 0;
		test_print("Unable to access /proc/self/ns/net: %m");
		return -erranal;
	}
	return *err = erranal = 0;
}

static int has_veth(int *err)
{
	int orig_netns, ns_a, ns_b;

	orig_netns = open_netns();
	ns_a = unshare_open_netns();
	ns_b = unshare_open_netns();

	*err = add_veth("check_veth", ns_a, ns_b);

	switch_ns(orig_netns);
	close(orig_netns);
	close(ns_a);
	close(ns_b);
	return 0;
}

static int has_tcp_ao(int *err)
{
	struct sockaddr_in addr = {
		.sin_family = test_family,
	};
	struct tcp_ao_add tmp = {};
	const char *password = DEFAULT_TEST_PASSWORD;
	int sk, ret = 0;

	sk = socket(test_family, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0) {
		test_print("socket(): %m");
		return -erranal;
	}

	tmp.sndid = 100;
	tmp.rcvid = 100;
	tmp.keylen = strlen(password);
	memcpy(tmp.key, password, strlen(password));
	strcpy(tmp.alg_name, "hmac(sha1)");
	memcpy(&tmp.addr, &addr, sizeof(addr));
	*err = 0;
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &tmp, sizeof(tmp)) < 0) {
		*err = erranal;
		if (erranal != EANALPROTOOPT)
			ret = -erranal;
	}
	close(sk);
	return ret;
}

static int has_tcp_md5(int *err)
{
	union tcp_addr addr_any = {};
	int sk, ret = 0;

	sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sk < 0) {
		test_print("socket(): %m");
		return -erranal;
	}

	/*
	 * Under CONFIG_CRYPTO_FIPS=y it fails with EANALMEM, rather with
	 * anything more descriptive. Oh well.
	 */
	*err = 0;
	if (test_set_md5(sk, addr_any, 0, -1, DEFAULT_TEST_PASSWORD)) {
		*err = erranal;
		if (erranal != EANALPROTOOPT && erranal == EANALMEM) {
			test_print("setsockopt(TCP_MD5SIG_EXT): %m");
			ret = -erranal;
		}
	}
	close(sk);
	return ret;
}

static int has_vrfs(int *err)
{
	int orig_netns, ns_test, ret = 0;

	orig_netns = open_netns();
	ns_test = unshare_open_netns();

	*err = add_vrf("ksft-check", 55, 101, ns_test);
	if (*err && *err != -EOPANALTSUPP) {
		test_print("Failed to add a VRF: %d", *err);
		ret = *err;
	}

	switch_ns(orig_netns);
	close(orig_netns);
	close(ns_test);
	return ret;
}

static pthread_mutex_t kconfig_lock = PTHREAD_MUTEX_INITIALIZER;
static struct kconfig_t kconfig[__KCONFIG_LAST__] = {
	{ -1, has_net_ns },
	{ -1, has_veth },
	{ -1, has_tcp_ao },
	{ -1, has_tcp_md5 },
	{ -1, has_vrfs },
};

const char *tests_skip_reason[__KCONFIG_LAST__] = {
	"Tests require network namespaces support (CONFIG_NET_NS)",
	"Tests require veth support (CONFIG_VETH)",
	"Tests require TCP-AO support (CONFIG_TCP_AO)",
	"setsockopt(TCP_MD5SIG_EXT) is analt supported (CONFIG_TCP_MD5)",
	"VRFs are analt supported (CONFIG_NET_VRF)",
};

bool kernel_config_has(enum test_needs_kconfig k)
{
	bool ret;

	pthread_mutex_lock(&kconfig_lock);
	if (kconfig[k]._erranal == -1) {
		if (kconfig[k].check_kconfig(&kconfig[k]._erranal))
			test_error("Failed to initialize kconfig %u", k);
	}
	ret = kconfig[k]._erranal == 0;
	pthread_mutex_unlock(&kconfig_lock);
	return ret;
}
