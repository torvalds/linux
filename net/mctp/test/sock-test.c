// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include "utils.h"

static struct kunit_case mctp_test_cases[] = {
	{}
};

static struct kunit_suite mctp_test_suite = {
	.name = "mctp-sock",
	.test_cases = mctp_test_cases,
};

kunit_test_suite(mctp_test_suite);
