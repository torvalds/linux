// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <limits.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#include "../../src/utils.h"

extern int nr_cpus;

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

struct cpu_list_iterate_cb_data {
	int index;
	int *values;
};

static int cpu_list_iterate_callback(int cpu, void *data)
{
	struct cpu_list_iterate_cb_data *cb_data = data;

	ck_assert_int_eq(cpu, cb_data->values[cb_data->index++]);

	return 0;
}

static int cpu_list_iterate_callback_error(int cpu, void *data)
{
	struct cpu_list_iterate_cb_data *cb_data = data;

	if (cpu > 10)
		return -42;

	ck_assert_int_eq(cpu, cb_data->values[cb_data->index++]);

	return 0;
}

START_TEST(test_cpu_list_iterate)
{
	struct cpu_list_iterate_cb_data cb_data;
	int test_data_1[] = {1, 2, 3, 4};
	int test_data_2[] = {1, 2, 10, 11, 12};

	cb_data.index = 0;

	cb_data.values = test_data_1;
	ck_assert_int_eq(cpu_list_iterate("1,2,3,4", cpu_list_iterate_callback, &cb_data), 4);
	ck_assert_int_eq(cb_data.index, 4);
	cb_data.index = 0;
	ck_assert_int_eq(cpu_list_iterate("1-4", cpu_list_iterate_callback, &cb_data), 4);
	ck_assert_int_eq(cb_data.index, 4);
	cb_data.index = 0;
	ck_assert_int_eq(cpu_list_iterate("1,2-3,4", cpu_list_iterate_callback, &cb_data), 4);
	ck_assert_int_eq(cb_data.index, 4);
	cb_data.index = 0;
	ck_assert_int_eq(cpu_list_iterate("1-3,4", cpu_list_iterate_callback, &cb_data), 4);
	ck_assert_int_eq(cb_data.index, 4);
	cb_data.index = 0;
	ck_assert_int_eq(cpu_list_iterate("1,2-4", cpu_list_iterate_callback, &cb_data), 4);
	ck_assert_int_eq(cb_data.index, 4);

	cb_data.index = 0;
	ck_assert_int_eq(cpu_list_iterate("1,2-4", cpu_list_iterate_callback_error, &cb_data), 4);
	ck_assert_int_eq(cb_data.index, 4);
	cb_data.index = 0;
	cb_data.values = test_data_2;
	ck_assert_int_eq(cpu_list_iterate("1,2,10-12", cpu_list_iterate_callback_error, &cb_data),
			 -42);
	ck_assert_int_eq(cb_data.index, 3);
}
END_TEST

START_TEST(test_parse_cpu_set)
{
	cpu_set_t set;

	nr_cpus = 8;
	ck_assert_int_eq(parse_cpu_set("0", &set), 0);
	ck_assert(CPU_ISSET(0, &set));
	ck_assert(!CPU_ISSET(1, &set));

	ck_assert_int_eq(parse_cpu_set("0,2", &set), 0);
	ck_assert(CPU_ISSET(0, &set));
	ck_assert(CPU_ISSET(2, &set));

	ck_assert_int_eq(parse_cpu_set("0-3", &set), 0);
	ck_assert(CPU_ISSET(0, &set));
	ck_assert(CPU_ISSET(1, &set));
	ck_assert(CPU_ISSET(2, &set));
	ck_assert(CPU_ISSET(3, &set));

	ck_assert_int_eq(parse_cpu_set("1-3,5", &set), 0);
	ck_assert(!CPU_ISSET(0, &set));
	ck_assert(CPU_ISSET(1, &set));
	ck_assert(CPU_ISSET(2, &set));
	ck_assert(CPU_ISSET(3, &set));
	ck_assert(!CPU_ISSET(4, &set));
	ck_assert(CPU_ISSET(5, &set));

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
	tcase_add_test(tc, test_cpu_list_iterate);
	tcase_add_test(tc, test_parse_cpu_set);
	tcase_add_test(tc, test_parse_prio);

	suite_add_tcase(s, tc);
	return s;
}
