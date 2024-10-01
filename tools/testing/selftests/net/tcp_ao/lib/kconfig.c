// SPDX-License-Identifier: GPL-2.0
/* Check what features does the kernel support (where the selftest is running).
 * Somewhat inspired by CRIU kerndat/kdat kernel features detector.
 */
#include <pthread.h>
#include "aolib.h"

struct kconfig_t {
	int _error;		/* negative errno if not supported */
	int (*check_kconfig)(int *error);
};

static int has_net_ns(int *err)
{
	if (access("/proc/self/ns/net", F_OK) < 0) {
		*err = errno;
		if (errno == ENOENT)
			return 0;
		test_print("Unable to access /proc/self/ns/net: %m");
		return -errno;
	}
	return *err = errno = 0;
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
		return -errno;
	}

	tmp.sndid = 100;
	tmp.rcvid = 100;
	tmp.keylen = strlen(password);
	memcpy(tmp.key, password, strlen(password));
	strcpy(tmp.alg_name, "hmac(sha1)");
	memcpy(&tmp.addr, &addr, sizeof(addr));
	*err = 0;
	if (setsockopt(sk, IPPROTO_TCP, TCP_AO_ADD_KEY, &tmp, sizeof(tmp)) < 0) {
		*err = -errno;
		if (errno != ENOPROTOOPT)
			ret = -errno;
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
		return -errno;
	}

	/*
	 * Under CONFIG_CRYPTO_FIPS=y it fails with ENOMEM, rather with
	 * anything more descriptive. Oh well.
	 */
	*err = 0;
	if (test_set_md5(sk, addr_any, 0, -1, DEFAULT_TEST_PASSWORD)) {
		*err = -errno;
		if (errno != ENOPROTOOPT && errno == ENOMEM) {
			test_print("setsockopt(TCP_MD5SIG_EXT): %m");
			ret = -errno;
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
	if (*err && *err != -EOPNOTSUPP) {
		test_print("Failed to add a VRF: %d", *err);
		ret = *err;
	}

	switch_ns(orig_netns);
	close(orig_netns);
	close(ns_test);
	return ret;
}

static int has_ftrace(int *err)
{
	*err = test_setup_tracing();
	return 0;
}

#define KCONFIG_UNKNOWN			1
static pthread_mutex_t kconfig_lock = PTHREAD_MUTEX_INITIALIZER;
static struct kconfig_t kconfig[__KCONFIG_LAST__] = {
	{ KCONFIG_UNKNOWN, has_net_ns },
	{ KCONFIG_UNKNOWN, has_veth },
	{ KCONFIG_UNKNOWN, has_tcp_ao },
	{ KCONFIG_UNKNOWN, has_tcp_md5 },
	{ KCONFIG_UNKNOWN, has_vrfs },
	{ KCONFIG_UNKNOWN, has_ftrace },
};

const char *tests_skip_reason[__KCONFIG_LAST__] = {
	"Tests require network namespaces support (CONFIG_NET_NS)",
	"Tests require veth support (CONFIG_VETH)",
	"Tests require TCP-AO support (CONFIG_TCP_AO)",
	"setsockopt(TCP_MD5SIG_EXT) is not supported (CONFIG_TCP_MD5)",
	"VRFs are not supported (CONFIG_NET_VRF)",
	"Ftrace points are not supported (CONFIG_TRACEPOINTS)",
};

bool kernel_config_has(enum test_needs_kconfig k)
{
	bool ret;

	pthread_mutex_lock(&kconfig_lock);
	if (kconfig[k]._error == KCONFIG_UNKNOWN) {
		if (kconfig[k].check_kconfig(&kconfig[k]._error))
			test_error("Failed to initialize kconfig %u", k);
	}
	ret = kconfig[k]._error == 0;
	pthread_mutex_unlock(&kconfig_lock);
	return ret;
}
