// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <lkl.h>
#include <lkl_host.h>
#include <lkl_config.h>

#include "test.h"

static const char *config_json =
"{\n"
"	\"gateway\":\"192.168.113.1\",\n"
"	\"gateway6\":\"fc03::1\",\n"
"	\"nameserver\":\"2001:4860:4860::8888\",\n"
"	\"debug\":\"1\",\n"
"	\"interfaces\": [\n"
"		{\n"
"			\"type\":\"tap\",\n"
"			\"param\":\"lkl_test_tap0\",\n"
"			\"ip\":\"192.168.113.2\",\n"
"			\"masklen\":\"24\",\n"
"			\"ipv6\":\"fc03::2\",\n"
"			\"masklen6\":\"64\",\n"
"			\"mac\": \"aa:bb:cc:dd:ee:ff\"\n"
"		}\n"
"	]\n"
"}\n";

int lkl_test_config_load_json(void)
{
	struct lkl_config *cfg = alloca(sizeof(*cfg));

#ifndef LKL_HOST_CONFIG_JSMN
	lkl_test_logf("no json support\n");
	return TEST_SKIP;
#endif

	memset(cfg, 0, sizeof(*cfg));

	if (lkl_load_config_json(cfg, config_json) < 0) {
		lkl_test_logf("failed to load config\n");
		return TEST_FAILURE;
	}

	if (cfg->ifnum != 1) {
		lkl_test_logf("bad ifnum\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->gateway, "192.168.113.1") != 0) {
		lkl_test_logf("bad gateway\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->gateway6, "fc03::1") != 0) {
		lkl_test_logf("bad gateway6\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->nameserver, "2001:4860:4860::8888") != 0) {
		lkl_test_logf("bad nameserver\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->debug, "1") != 0) {
		lkl_test_logf("bad debug\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->ifaces->ifparams, "lkl_test_tap0") != 0) {
		lkl_test_logf("bad iface params\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->ifaces->ifip, "192.168.113.2") != 0) {
		lkl_test_logf("bad iface ip\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->ifaces->ifnetmask_len, "24") != 0) {
		lkl_test_logf("bad iface masklen\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->ifaces->ifipv6, "fc03::2") != 0) {
		lkl_test_logf("bad iface ipv6\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->ifaces->ifnetmask6_len, "64") != 0) {
		lkl_test_logf("bad iface masklen6\n");
		return TEST_FAILURE;
	}

	if (strcmp(cfg->ifaces->ifmac_str, "aa:bb:cc:dd:ee:ff") != 0) {
		lkl_test_logf("bad iface mac\n");
		return TEST_FAILURE;
	}

	return TEST_SUCCESS;
}

struct lkl_test tests[] = {
	LKL_TEST(config_load_json),
};

int main(int argc, const char **argv)
{
	return lkl_test_run(tests, sizeof(tests)/sizeof(struct lkl_test),
			    "config");
}
