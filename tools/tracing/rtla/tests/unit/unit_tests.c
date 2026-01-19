// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <limits.h>
#include <unistd.h>

#include "../../src/utils.h"

START_TEST(test_strtoi)
{
	int result;
	char buf[64];

	ck_assert_int_eq(strtoi("123", &result), 0);
	ck_assert_int_eq(result, 123);
	ck_assert_int_eq(strtoi(" -456", &result), 0);
	ck_assert_int_eq(result, -456);

	snprintf(buf, sizeof(buf), "%d", INT_MAX);
	ck_assert_int_eq(strtoi(buf, &result), 0);
	snprintf(buf, sizeof(buf), "%ld", (long)INT_MAX + 1);
	ck_assert_int_eq(strtoi(buf, &result), -1);

	ck_assert_int_eq(strtoi("", &result), -1);
	ck_assert_int_eq(strtoi("123abc", &result), -1);
	ck_assert_int_eq(strtoi("123 ", &result), -1);
}
END_TEST

START_TEST(test_parse_cpu_set)
{
	cpu_set_t set;
	int nr_cpus = sysconf(_SC_NPROCESSORS_CONF);

	ck_assert_int_eq(parse_cpu_set("0", &set), 0);
	ck_assert(CPU_ISSET(0, &set));
	ck_assert(!CPU_ISSET(1, &set));

	if (nr_cpus > 2) {
		ck_assert_int_eq(parse_cpu_set("0,2", &set), 0);
		ck_assert(CPU_ISSET(0, &set));
		ck_assert(CPU_ISSET(2, &set));
	}

	if (nr_cpus > 3) {
		ck_assert_int_eq(parse_cpu_set("0-3", &set), 0);
		ck_assert(CPU_ISSET(0, &set));
		ck_assert(CPU_ISSET(1, &set));
		ck_assert(CPU_ISSET(2, &set));
		ck_assert(CPU_ISSET(3, &set));
	}

	if (nr_cpus > 5) {
		ck_assert_int_eq(parse_cpu_set("1-3,5", &set), 0);
		ck_assert(!CPU_ISSET(0, &set));
		ck_assert(CPU_ISSET(1, &set));
		ck_assert(CPU_ISSET(2, &set));
		ck_assert(CPU_ISSET(3, &set));
		ck_assert(!CPU_ISSET(4, &set));
		ck_assert(CPU_ISSET(5, &set));
	}

	ck_assert_int_eq(parse_cpu_set("-1", &set), 1);
	ck_assert_int_eq(parse_cpu_set("abc", &set), 1);
	ck_assert_int_eq(parse_cpu_set("9999", &set), 1);
}
END_TEST

START_TEST(test_parse_prio)
{
	struct sched_attr attr;

	ck_assert_int_eq(parse_prio("f:50", &attr), 0);
	ck_assert_uint_eq(attr.sched_policy, SCHED_FIFO);
	ck_assert_uint_eq(attr.sched_priority, 50U);

	ck_assert_int_eq(parse_prio("r:30", &attr), 0);
	ck_assert_uint_eq(attr.sched_policy, SCHED_RR);

	ck_assert_int_eq(parse_prio("o:0", &attr), 0);
	ck_assert_uint_eq(attr.sched_policy, SCHED_OTHER);
	ck_assert_int_eq(attr.sched_nice, 0);

	ck_assert_int_eq(parse_prio("d:10ms:100ms", &attr), 0);
	ck_assert_uint_eq(attr.sched_policy, 6U);

	ck_assert_int_eq(parse_prio("f:999", &attr), -1);
	ck_assert_int_eq(parse_prio("o:-20", &attr), -1);
	ck_assert_int_eq(parse_prio("d:100ms:10ms", &attr), -1);
	ck_assert_int_eq(parse_prio("x:50", &attr), -1);
}
END_TEST

Suite *utils_suite(void)
{
	Suite *s = suite_create("utils");
	TCase *tc = tcase_create("core");

	tcase_add_test(tc, test_strtoi);
	tcase_add_test(tc, test_parse_cpu_set);
	tcase_add_test(tc, test_parse_prio);

	suite_add_tcase(s, tc);
	return s;
}

int main(void)
{
	int num_failed;
	SRunner *sr;

	sr = srunner_create(utils_suite());
	srunner_run_all(sr, CK_NORMAL);
	num_failed = srunner_ntests_failed(sr);

	srunner_free(sr);

	return (num_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
