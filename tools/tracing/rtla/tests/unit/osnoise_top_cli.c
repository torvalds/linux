// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <limits.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#include "cli_params_assert.h"
#include "../../src/cli.h"

#define PARSE_ARGS(...) char *argv[] = { __VA_ARGS__, NULL  };\
			int argc = sizeof(argv) / sizeof(char *) - 1;\
			struct common_params *params =\
				osnoise_top_parse_args(argc, argv);\
			struct osnoise_params *osn_params __maybe_unused =\
				to_osnoise_params(params)

/* Tracing Options */

START_TEST(test_period_short)
{
	PARSE_ARGS("osnoise", "top", "-p", "100000");

	ck_assert_int_eq(osn_params->period, 100000);
}
END_TEST

START_TEST(test_period_long)
{
	PARSE_ARGS("osnoise", "top", "--period", "100000");

	ck_assert_int_eq(osn_params->period, 100000);
}
END_TEST

START_TEST(test_runtime_short)
{
	PARSE_ARGS("osnoise", "top", "-r", "95000");

	ck_assert_int_eq(osn_params->runtime, 95000);
}
END_TEST

START_TEST(test_runtime_long)
{
	PARSE_ARGS("osnoise", "top", "--runtime", "95000");

	ck_assert_int_eq(osn_params->runtime, 95000);
}
END_TEST

START_TEST(test_stop_short)
{
	PARSE_ARGS("osnoise", "top", "-s", "20");

	ck_assert_int_eq(params->stop_us, 20);
}
END_TEST

START_TEST(test_stop_long)
{
	PARSE_ARGS("osnoise", "top", "--stop", "20");

	ck_assert_int_eq(params->stop_us, 20);
}
END_TEST

START_TEST(test_stop_total_short)
{
	PARSE_ARGS("osnoise", "top", "-S", "20");

	ck_assert_int_eq(params->stop_total_us, 20);
}
END_TEST

START_TEST(test_stop_total_long)
{
	PARSE_ARGS("osnoise", "top", "--stop-total", "20");

	ck_assert_int_eq(params->stop_total_us, 20);
}
END_TEST

START_TEST(test_threshold_short)
{
	PARSE_ARGS("osnoise", "top", "-T", "5");

	ck_assert_int_eq(osn_params->threshold, 5);
}
END_TEST

START_TEST(test_threshold_long)
{
	PARSE_ARGS("osnoise", "top", "--threshold", "5");

	ck_assert_int_eq(osn_params->threshold, 5);
}
END_TEST

START_TEST(test_trace_short_noarg)
{
	PARSE_ARGS("osnoise", "top", "-t");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "osnoise_trace.txt");
}
END_TEST

START_TEST(test_trace_short_followarg)
{
	PARSE_ARGS("osnoise", "top", "-t", "-d", "20");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "osnoise_trace.txt");
	ck_assert_int_eq(params->duration, 20); /* check if next argument is read correctly */
}
END_TEST

START_TEST(test_trace_short_space)
{
	PARSE_ARGS("osnoise", "top", "-t", "tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

START_TEST(test_trace_short_equals)
{
	PARSE_ARGS("osnoise", "top", "-t=tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

START_TEST(test_trace_long_noarg)
{
	PARSE_ARGS("osnoise", "top", "--trace");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "osnoise_trace.txt");
}
END_TEST

START_TEST(test_trace_long_followarg)
{
	PARSE_ARGS("osnoise", "top", "--trace", "-d", "20");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "osnoise_trace.txt");
	ck_assert_int_eq(params->duration, 20); /* check if next argument is read correctly */
}
END_TEST

START_TEST(test_trace_long_space)
{
	PARSE_ARGS("osnoise", "top", "--trace", "tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

START_TEST(test_trace_long_equals)
{
	PARSE_ARGS("osnoise", "top", "--trace=tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

/* Event Configuration */

START_TEST(test_event_short)
{
	PARSE_ARGS("osnoise", "top", "-e", "system:event");

	CLI_ASSERT_SINGLE_EVENT("system", "event");
}
END_TEST

START_TEST(test_event_long)
{
	PARSE_ARGS("osnoise", "top", "--event", "system:event");

	CLI_ASSERT_SINGLE_EVENT("system", "event");
}
END_TEST

START_TEST(test_filter)
{
	PARSE_ARGS("osnoise", "top", "-e", "system:event", "--filter", "filter");

	CLI_ASSERT_SINGLE_FILTER("filter");
}
END_TEST

START_TEST(test_trigger)
{
	PARSE_ARGS("osnoise", "top", "-e", "system:event", "--trigger", "trigger");

	CLI_ASSERT_SINGLE_TRIGGER("trigger");
}
END_TEST

/* CPU Configuration */

START_TEST(test_cpus_short)
{
	nr_cpus = 4;

	PARSE_ARGS("osnoise", "top", "-c", "0-1,3");

	ck_assert_str_eq(params->cpus, "0-1,3");
	CLI_ASSERT_CPUSET(monitored_cpus, 0, 1, 3);
}
END_TEST

START_TEST(test_cpus_long)
{
	nr_cpus = 4;

	PARSE_ARGS("osnoise", "top", "--cpus", "0-1,3");

	ck_assert_str_eq(params->cpus, "0-1,3");
	CLI_ASSERT_CPUSET(monitored_cpus, 0, 1, 3);
}
END_TEST

START_TEST(test_housekeeping_short)
{
	nr_cpus = 4;

	PARSE_ARGS("osnoise", "top", "-H", "0-1,3");

	CLI_ASSERT_CPUSET(hk_cpu_set, 0, 1, 3);
}
END_TEST

START_TEST(test_housekeeping_long)
{
	nr_cpus = 4;

	PARSE_ARGS("osnoise", "top", "--house-keeping", "0-1,3");

	CLI_ASSERT_CPUSET(hk_cpu_set, 0, 1, 3);
}
END_TEST

/* Thread Configuration */

START_TEST(test_cgroup_short_noarg)
{
	PARSE_ARGS("osnoise", "top", "-C");

	ck_assert(params->cgroup);
	ck_assert_ptr_null(params->cgroup_name);
}
END_TEST

START_TEST(test_cgroup_short_space)
{
	PARSE_ARGS("osnoise", "top", "-C", "cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_cgroup_short_equals)
{
	PARSE_ARGS("osnoise", "top", "-C=cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_cgroup_long_noarg)
{
	PARSE_ARGS("osnoise", "top", "--cgroup");

	ck_assert(params->cgroup);
	ck_assert_ptr_null(params->cgroup_name);
}
END_TEST

START_TEST(test_cgroup_long_space)
{
	PARSE_ARGS("osnoise", "top", "--cgroup", "cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_cgroup_long_equals)
{
	PARSE_ARGS("osnoise", "top", "--cgroup=cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_priority_short)
{
	PARSE_ARGS("osnoise", "top", "-P", "f:95");

	ck_assert_int_eq(params->sched_param.sched_policy, SCHED_FIFO);
	ck_assert_int_eq(params->sched_param.sched_priority, 95);
}
END_TEST

START_TEST(test_priority_long)
{
	PARSE_ARGS("osnoise", "top", "--priority", "f:95");

	ck_assert_int_eq(params->sched_param.sched_policy, SCHED_FIFO);
	ck_assert_int_eq(params->sched_param.sched_priority, 95);
}
END_TEST

/* Output */

START_TEST(test_quiet_short)
{
	PARSE_ARGS("osnoise", "top", "-q");

	ck_assert(params->quiet);
}
END_TEST

START_TEST(test_quiet_long)
{
	PARSE_ARGS("osnoise", "top", "--quiet");

	ck_assert(params->quiet);
}
END_TEST

/* Auto Analysis and Actions */

START_TEST(test_auto)
{
	PARSE_ARGS("osnoise", "top", "-a", "20");

	CLI_OSNOISE_ASSERT_AUTO(20);
}
END_TEST

START_TEST(test_on_end)
{
	PARSE_ARGS("osnoise", "top", "--on-end", "trace");

	CLI_ASSERT_SINGLE_ACTION(end_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "osnoise_trace.txt");
}
END_TEST

START_TEST(test_on_threshold)
{
	PARSE_ARGS("osnoise", "top", "--on-threshold", "trace");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "osnoise_trace.txt");
}
END_TEST

/* System Tuning */

START_TEST(test_trace_buffer_size)
{
	PARSE_ARGS("osnoise", "top", "--trace-buffer-size", "200");

	ck_assert_int_eq(params->buffer_size, 200);
}
END_TEST

START_TEST(test_warm_up)
{
	PARSE_ARGS("osnoise", "top", "--warm-up", "5");

	ck_assert_int_eq(params->warmup, 5);
}
END_TEST

/* General */

START_TEST(test_debug_short)
{
	PARSE_ARGS("osnoise", "top", "-D");

	ck_assert(config_debug);
}
END_TEST

START_TEST(test_debug_long)
{
	PARSE_ARGS("osnoise", "top", "--debug");

	ck_assert(config_debug);
}
END_TEST

START_TEST(test_duration_short)
{
	PARSE_ARGS("osnoise", "top", "-d", "1m");

	ck_assert_int_eq(params->duration, 60);
}
END_TEST

START_TEST(test_duration_long)
{
	PARSE_ARGS("osnoise", "top", "--duration", "1m");

	ck_assert_int_eq(params->duration, 60);
}
END_TEST

Suite *osnoise_top_cli_suite(void)
{
	Suite *s = suite_create("osnoise_top_cli");
	TCase *tc;

	tc = tcase_create("tracing_options");
	tcase_add_test(tc, test_period_short);
	tcase_add_test(tc, test_period_long);
	tcase_add_test(tc, test_runtime_short);
	tcase_add_test(tc, test_runtime_long);
	tcase_add_test(tc, test_stop_short);
	tcase_add_test(tc, test_stop_long);
	tcase_add_test(tc, test_stop_total_short);
	tcase_add_test(tc, test_stop_total_long);
	tcase_add_test(tc, test_threshold_short);
	tcase_add_test(tc, test_threshold_long);
	tcase_add_test(tc, test_trace_short_noarg);
	tcase_add_test(tc, test_trace_short_followarg);
	tcase_add_test(tc, test_trace_short_space);
	tcase_add_test(tc, test_trace_short_equals);
	tcase_add_test(tc, test_trace_long_noarg);
	tcase_add_test(tc, test_trace_long_followarg);
	tcase_add_test(tc, test_trace_long_space);
	tcase_add_test(tc, test_trace_long_equals);
	suite_add_tcase(s, tc);

	tc = tcase_create("event_configuration");
	tcase_add_test(tc, test_event_short);
	tcase_add_test(tc, test_event_long);
	tcase_add_test(tc, test_filter);
	tcase_add_test(tc, test_trigger);
	suite_add_tcase(s, tc);

	tc = tcase_create("cpu_configuration");
	tcase_add_test(tc, test_cpus_short);
	tcase_add_test(tc, test_cpus_long);
	tcase_add_test(tc, test_housekeeping_short);
	tcase_add_test(tc, test_housekeeping_long);
	suite_add_tcase(s, tc);

	tc = tcase_create("thread_configuration");
	tcase_add_test(tc, test_cgroup_short_noarg);
	tcase_add_test(tc, test_cgroup_short_space);
	tcase_add_test(tc, test_cgroup_short_equals);
	tcase_add_test(tc, test_cgroup_long_noarg);
	tcase_add_test(tc, test_cgroup_long_space);
	tcase_add_test(tc, test_cgroup_long_equals);
	tcase_add_test(tc, test_priority_short);
	tcase_add_test(tc, test_priority_long);
	suite_add_tcase(s, tc);

	tc = tcase_create("output");
	tcase_add_test(tc, test_quiet_short);
	tcase_add_test(tc, test_quiet_long);
	suite_add_tcase(s, tc);

	tc = tcase_create("system_tuning");
	tcase_add_test(tc, test_trace_buffer_size);
	tcase_add_test(tc, test_warm_up);
	suite_add_tcase(s, tc);

	tc = tcase_create("aa_actions");
	tcase_add_test(tc, test_auto);
	tcase_add_test(tc, test_on_end);
	tcase_add_test(tc, test_on_threshold);
	suite_add_tcase(s, tc);

	tc = tcase_create("general");
	tcase_add_test(tc, test_debug_short);
	tcase_add_test(tc, test_debug_long);
	tcase_add_test(tc, test_duration_short);
	tcase_add_test(tc, test_duration_long);
	suite_add_tcase(s, tc);

	return s;
}
