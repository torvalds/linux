// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <stdio.h>
#include <check.h>

#define RTLA_ALLOW_CLI_P_H
#include "../../src/cli_p.h"
#include "cli_params_assert.h"

#define TEST_CALLBACK(value, cb) OPT_CALLBACK('t', "test", value, "test value", "test help", cb)
#define TEST_LLONG_RANGE(value, lo, hi) \
	RTLA_OPT_CALLBACK_DATA('t', "test", value, "test value", "test help", \
	opt_llong_callback, LLONG_RANGE(lo, hi))
#define TEST_INT_RANGE(value, lo, hi) \
	RTLA_OPT_CALLBACK_DATA('t', "test", value, "test value", "test help", \
	opt_int_callback, INT_RANGE(lo, hi))

START_TEST(test_opt_llong_callback_simple)
{
	long long test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_llong_callback);

	ck_assert_int_eq(opt_llong_callback(&opt, "1234567890", 0), 0);
	ck_assert_int_eq(test_value, 1234567890);
}
END_TEST

START_TEST(test_opt_llong_callback_max)
{
	long long test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_llong_callback);

	ck_assert_int_eq(opt_llong_callback(&opt, "9223372036854775807", 0), 0);
	ck_assert_int_eq(test_value, 9223372036854775807LL);
}
END_TEST

START_TEST(test_opt_llong_callback_min)
{
	long long test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_llong_callback);

	ck_assert_int_eq(opt_llong_callback(&opt, "-9223372036854775808", 0), 0);
	ck_assert_int_eq(test_value, ~9223372036854775807LL);
}
END_TEST

START_TEST(test_opt_llong_callback_non_numeric)
{
	long long test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_llong_callback);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_llong_callback(&opt, "abc", 0), -1);
	ck_assert_int_eq(test_value, 0);
}
END_TEST

START_TEST(test_opt_llong_callback_non_numeric_suffix)
{
	long long test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_llong_callback);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_llong_callback(&opt, "1234567890abc", 0), -1);
	ck_assert_int_eq(test_value, 0);
}
END_TEST

START_TEST(test_opt_llong_callback_unset)
{
	long long test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_llong_callback);

	ck_assert_int_eq(opt_llong_callback(&opt, "1234567890", 0), 0);
	ck_assert_int_eq(opt_llong_callback(&opt, NULL, 1), 0);
	ck_assert_int_eq(test_value, 0);
}
END_TEST

START_TEST(test_opt_llong_callback_unset_defval)
{
	long long test_value = 0;
	const long long default_value = 42;
	const struct option opt = RTLA_OPT_LLONG_DEFVAL('t', "test", &test_value, "test value",
							"test help", &default_value);

	ck_assert_int_eq(opt_llong_callback(&opt, "1234567890", 0), 0);
	ck_assert_int_eq(opt_llong_callback(&opt, NULL, 1), 0);
	ck_assert_int_eq(test_value, default_value);
}
END_TEST

START_TEST(test_opt_int_callback_simple)
{
	int test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_int_callback);

	ck_assert_int_eq(opt_int_callback(&opt, "1234567890", 0), 0);
	ck_assert_int_eq(test_value, 1234567890);
}
END_TEST

START_TEST(test_opt_int_callback_max)
{
	int test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_int_callback);

	ck_assert_int_eq(opt_int_callback(&opt, "2147483647", 0), 0);
	ck_assert_int_eq(test_value, 2147483647);
}
END_TEST

START_TEST(test_opt_int_callback_min)
{
	int test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_int_callback);

	ck_assert_int_eq(opt_int_callback(&opt, "-2147483648", 0), 0);
	ck_assert_int_eq(test_value, -2147483648);
}
END_TEST

START_TEST(test_opt_int_callback_non_numeric)
{
	int test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_int_callback);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_int_callback(&opt, "abc", 0), -1);
	ck_assert_int_eq(test_value, 0);
}
END_TEST

START_TEST(test_opt_int_callback_non_numeric_suffix)
{
	int test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_int_callback);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_int_callback(&opt, "1234567890abc", 0), -1);
	ck_assert_int_eq(test_value, 0);
}
END_TEST

START_TEST(test_opt_int_callback_unset)
{
	int test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_int_callback);

	ck_assert_int_eq(opt_int_callback(&opt, "1234567890", 0), 0);
	ck_assert_int_eq(opt_int_callback(&opt, NULL, 1), 0);
	ck_assert_int_eq(test_value, 0);
}
END_TEST

START_TEST(test_opt_int_callback_unset_defval)
{
	int test_value = 0;
	const struct option opt = RTLA_OPT_INT_DEFVAL('t', "test", &test_value, "test value",
						      "test help", 42);

	ck_assert_int_eq(opt_int_callback(&opt, "1234567890", 0), 0);
	ck_assert_int_eq(opt_int_callback(&opt, NULL, 1), 0);
	ck_assert_int_eq(test_value, 42);
}
END_TEST

START_TEST(test_opt_llong_callback_range_in)
{
	long long test_value = 0;
	const struct option opt = TEST_LLONG_RANGE(&test_value, 10, 100);

	ck_assert_int_eq(opt_llong_callback(&opt, "50", 0), 0);
	ck_assert_int_eq(test_value, 50);
}
END_TEST

START_TEST(test_opt_llong_callback_range_below)
{
	long long test_value = 0;
	const struct option opt = TEST_LLONG_RANGE(&test_value, 10, 100);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_llong_callback(&opt, "9", 0), -1);
}
END_TEST

START_TEST(test_opt_llong_callback_range_above)
{
	long long test_value = 0;
	const struct option opt = TEST_LLONG_RANGE(&test_value, 10, 100);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_llong_callback(&opt, "101", 0), -1);
}
END_TEST

START_TEST(test_opt_llong_callback_range_boundary)
{
	long long test_value = 0;
	const struct option opt = TEST_LLONG_RANGE(&test_value, 10, 100);

	ck_assert_int_eq(opt_llong_callback(&opt, "10", 0), 0);
	ck_assert_int_eq(test_value, 10);
	ck_assert_int_eq(opt_llong_callback(&opt, "100", 0), 0);
	ck_assert_int_eq(test_value, 100);
}
END_TEST

START_TEST(test_opt_int_callback_range_in)
{
	int test_value = 0;
	const struct option opt = TEST_INT_RANGE(&test_value, 0, 10000);

	ck_assert_int_eq(opt_int_callback(&opt, "5000", 0), 0);
	ck_assert_int_eq(test_value, 5000);
}
END_TEST

START_TEST(test_opt_int_callback_range_below)
{
	int test_value = 0;
	const struct option opt = TEST_INT_RANGE(&test_value, 0, 10000);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_int_callback(&opt, "-1", 0), -1);
}
END_TEST

START_TEST(test_opt_int_callback_range_above)
{
	int test_value = 0;
	const struct option opt = TEST_INT_RANGE(&test_value, 0, 10000);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_int_callback(&opt, "10001", 0), -1);
}
END_TEST

START_TEST(test_opt_int_callback_range_boundary)
{
	int test_value = 0;
	const struct option opt = TEST_INT_RANGE(&test_value, 0, 10000);

	ck_assert_int_eq(opt_int_callback(&opt, "0", 0), 0);
	ck_assert_int_eq(test_value, 0);
	ck_assert_int_eq(opt_int_callback(&opt, "10000", 0), 0);
	ck_assert_int_eq(test_value, 10000);
}
END_TEST

START_TEST(test_opt_cpus_cb)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_cpus_cb);

	nr_cpus = 4;
	ck_assert_int_eq(opt_cpus_cb(&opt, "0-3", 0), 0);
	ck_assert_str_eq(params.cpus, "0-3");
}
END_TEST

START_TEST(test_opt_cpus_cb_invalid)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_cpus_cb);

	nr_cpus = 4;
	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_cpus_cb(&opt, "0-3,5", 0), -1);
}
END_TEST

START_TEST(test_opt_cgroup_cb)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_cgroup_cb);

	ck_assert_int_eq(opt_cgroup_cb(&opt, "cgroup", 0), 0);
	ck_assert_int_eq(params.cgroup, 1);
	ck_assert_str_eq(params.cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_opt_cgroup_cb_equals)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_cgroup_cb);

	ck_assert_int_eq(opt_cgroup_cb(&opt, "=cgroup", 0), 0);
	ck_assert_int_eq(params.cgroup, 1);
	ck_assert_str_eq(params.cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_opt_cgroup_cb_unset)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_cgroup_cb);

	ck_assert_int_eq(opt_cgroup_cb(&opt, "cgroup", 0), 0);
	ck_assert_int_eq(opt_cgroup_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.cgroup, 0);
	ck_assert_ptr_null(params.cgroup_name);
}
END_TEST

START_TEST(test_opt_duration_cb)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_duration_cb);

	ck_assert_int_eq(opt_duration_cb(&opt, "1m", 0), 0);
	ck_assert_int_eq(params.duration, 60);
}
END_TEST

START_TEST(test_opt_duration_cb_invalid)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_duration_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_duration_cb(&opt, "abc", 0), -1);
}
END_TEST

START_TEST(test_opt_duration_cb_unset)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_duration_cb);

	ck_assert_int_eq(opt_duration_cb(&opt, "1m", 0), 0);
	ck_assert_int_eq(opt_duration_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.duration, 0);
}
END_TEST

START_TEST(test_opt_event_cb)
{
	struct trace_events *events = NULL;
	const struct option opt = TEST_CALLBACK(&events, opt_event_cb);

	ck_assert_int_eq(opt_event_cb(&opt, "sched:sched_switch", 0), 0);
	ck_assert_str_eq(events->system, "sched");
	ck_assert_str_eq(events->event, "sched_switch");
	ck_assert_ptr_eq(events->next, NULL);
}
END_TEST

START_TEST(test_opt_event_cb_multiple)
{
	struct trace_events *events = NULL;
	const struct option opt = TEST_CALLBACK(&events, opt_event_cb);

	ck_assert_int_eq(opt_event_cb(&opt, "sched:sched_switch", 0), 0);
	ck_assert_int_eq(opt_event_cb(&opt, "sched:sched_wakeup", 0), 0);
	ck_assert_str_eq(events->system, "sched");
	ck_assert_str_eq(events->event, "sched_wakeup");
	ck_assert_str_eq(events->next->system, "sched");
	ck_assert_str_eq(events->next->event, "sched_switch");
	ck_assert_ptr_eq(events->next->next, NULL);
}
END_TEST

START_TEST(test_opt_housekeeping_cb)
{
	struct common_params __params = {0};
	struct common_params *params = &__params;
	const struct option opt = TEST_CALLBACK(params, opt_housekeeping_cb);

	nr_cpus = 4;
	ck_assert_int_eq(opt_housekeeping_cb(&opt, "0-3", 0), 0);
	ck_assert_int_eq(params->hk_cpus, 1);
	CLI_ASSERT_CPUSET(hk_cpu_set, 0, 1, 2, 3);
}
END_TEST

START_TEST(test_opt_housekeeping_cb_invalid)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_housekeeping_cb);

	nr_cpus = 4;
	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_housekeeping_cb(&opt, "0-3,5", 0), -1);
}
END_TEST

START_TEST(test_opt_housekeeping_cb_unset)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_housekeeping_cb);

	nr_cpus = 4;
	ck_assert_int_eq(opt_housekeeping_cb(&opt, "0-3", 0), 0);
	ck_assert_int_eq(opt_housekeeping_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.hk_cpus, 0);
	ck_assert_int_eq(CPU_COUNT(&params.hk_cpu_set), 0);
}
END_TEST

START_TEST(test_opt_priority_cb)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_priority_cb);

	ck_assert_int_eq(opt_priority_cb(&opt, "f:95", 0), 0);
	ck_assert_int_eq(params.sched_param.sched_policy, SCHED_FIFO);
	ck_assert_int_eq(params.sched_param.sched_priority, 95);
}
END_TEST

START_TEST(test_opt_priority_cb_invalid)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_priority_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_priority_cb(&opt, "abc", 0), -1);
}
END_TEST

START_TEST(test_opt_priority_cb_unset)
{
	struct common_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_priority_cb);

	ck_assert_int_eq(opt_priority_cb(&opt, "f:95", 0), 0);
	ck_assert_int_eq(opt_priority_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.sched_param.sched_policy, 0);
	ck_assert_int_eq(params.sched_param.sched_priority, 0);
}
END_TEST

START_TEST(test_opt_trigger_cb)
{
	struct trace_events *events = trace_event_alloc("sched:sched_switch");
	const struct option opt = TEST_CALLBACK(&events, opt_trigger_cb);

	ck_assert_int_eq(opt_trigger_cb(&opt, "stacktrace", 0), 0);
	ck_assert_str_eq(events->trigger, "stacktrace");
}
END_TEST

START_TEST(test_opt_trigger_cb_no_event)
{
	struct trace_events *events = NULL;
	const struct option opt = TEST_CALLBACK(&events, opt_trigger_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_trigger_cb(&opt, "stacktrace", 0), -1);
}
END_TEST

START_TEST(test_opt_filter_cb)
{
	struct trace_events *events = trace_event_alloc("sched:sched_switch");
	const struct option opt = TEST_CALLBACK(&events, opt_filter_cb);

	ck_assert_int_eq(opt_filter_cb(&opt, "comm ~ \"rtla\"", 0), 0);
	ck_assert_str_eq(events->filter, "comm ~ \"rtla\"");
}
END_TEST

START_TEST(test_opt_filter_cb_no_event)
{
	struct trace_events *events = NULL;
	const struct option opt = TEST_CALLBACK(&events, opt_filter_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_filter_cb(&opt, "comm ~ \"rtla\"", 0), -1);
}
END_TEST

START_TEST(test_opt_osnoise_auto_cb)
{
	struct osnoise_params params = {0};
	struct osnoise_cb_data cb_data = {&params};
	const struct option opt = TEST_CALLBACK(&cb_data, opt_osnoise_auto_cb);

	ck_assert_int_eq(opt_osnoise_auto_cb(&opt, "10", 0), 0);
	ck_assert_int_eq(params.common.stop_us, 10);
	ck_assert_int_eq(params.threshold, 1);
	ck_assert_str_eq(cb_data.trace_output, "osnoise_trace.txt");
}
END_TEST

START_TEST(test_opt_osnoise_auto_cb_unset)
{
	struct osnoise_params params = {0};
	struct osnoise_cb_data cb_data = {&params};
	const struct option opt = TEST_CALLBACK(&cb_data, opt_osnoise_auto_cb);

	ck_assert_int_eq(opt_osnoise_auto_cb(&opt, "10", 0), 0);
	ck_assert_int_eq(opt_osnoise_auto_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.common.stop_us, 0);
	ck_assert_int_eq(params.threshold, 0);
	ck_assert_ptr_null(cb_data.trace_output);
}
END_TEST


START_TEST(test_opt_osnoise_trace_output_cb)
{
	const char *trace_output = NULL;
	const struct option opt = TEST_CALLBACK(&trace_output, opt_osnoise_trace_output_cb);

	ck_assert_int_eq(opt_osnoise_trace_output_cb(&opt, "trace.txt", 0), 0);
	ck_assert_str_eq(trace_output, "trace.txt");
}
END_TEST

START_TEST(test_opt_osnoise_trace_output_cb_noarg)
{
	const char *trace_output = NULL;
	const struct option opt = TEST_CALLBACK(&trace_output, opt_osnoise_trace_output_cb);

	ck_assert_int_eq(opt_osnoise_trace_output_cb(&opt, NULL, 0), 0);
	ck_assert_str_eq(trace_output, "osnoise_trace.txt");
}
END_TEST

START_TEST(test_opt_osnoise_trace_output_cb_unset)
{
	const char *trace_output = NULL;
	const struct option opt = TEST_CALLBACK(&trace_output, opt_osnoise_trace_output_cb);

	ck_assert_int_eq(opt_osnoise_trace_output_cb(&opt, "trace.txt", 0), 0);
	ck_assert_int_eq(opt_osnoise_trace_output_cb(&opt, NULL, 1), 0);
	ck_assert_ptr_null(trace_output);
}
END_TEST

START_TEST(test_opt_osnoise_on_threshold_cb)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_osnoise_on_threshold_cb);

	ck_assert_int_eq(opt_osnoise_on_threshold_cb(&opt, "trace", 0), 0);
	ck_assert_int_eq(actions.len, 1);
	ck_assert_int_eq(actions.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions.list[0].trace_output, "osnoise_trace.txt");
}
END_TEST

START_TEST(test_opt_osnoise_on_threshold_cb_invalid)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_osnoise_on_threshold_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_osnoise_on_threshold_cb(&opt, "abc", 0), -1);
}
END_TEST

START_TEST(test_opt_osnoise_on_end_cb)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_osnoise_on_end_cb);

	ck_assert_int_eq(opt_osnoise_on_end_cb(&opt, "trace", 0), 0);
	ck_assert_int_eq(actions.len, 1);
	ck_assert_int_eq(actions.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions.list[0].trace_output, "osnoise_trace.txt");
}
END_TEST

START_TEST(test_opt_osnoise_on_end_cb_invalid)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_osnoise_on_end_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_osnoise_on_end_cb(&opt, "abc", 0), -1);
}
END_TEST

START_TEST(test_opt_timerlat_auto_cb)
{
	struct timerlat_params params = {0};
	struct timerlat_cb_data cb_data = {&params};
	const struct option opt = TEST_CALLBACK(&cb_data, opt_timerlat_auto_cb);

	ck_assert_int_eq(opt_timerlat_auto_cb(&opt, "10", 0), 0);
	ck_assert_int_eq(params.common.stop_us, 10);
	ck_assert_int_eq(params.common.stop_total_us, 10);
	ck_assert_int_eq(params.print_stack, 10);
	ck_assert_str_eq(cb_data.trace_output, "timerlat_trace.txt");
}
END_TEST

START_TEST(test_opt_timerlat_auto_cb_unset)
{
	struct timerlat_params params = {0};
	struct timerlat_cb_data cb_data = {&params};
	const struct option opt = TEST_CALLBACK(&cb_data, opt_timerlat_auto_cb);

	ck_assert_int_eq(opt_timerlat_auto_cb(&opt, "10", 0), 0);
	ck_assert_int_eq(opt_timerlat_auto_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.common.stop_us, 0);
	ck_assert_int_eq(params.common.stop_total_us, 0);
	ck_assert_int_eq(params.print_stack, 0);
	ck_assert_ptr_null(cb_data.trace_output);
}
END_TEST


START_TEST(test_opt_aa_only_cb)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_aa_only_cb);

	ck_assert_int_eq(opt_aa_only_cb(&opt, "10", 0), 0);
	ck_assert_int_eq(params.common.stop_us, 10);
	ck_assert_int_eq(params.common.stop_total_us, 10);
	ck_assert_int_eq(params.print_stack, 10);
	ck_assert_int_eq(params.common.aa_only, 1);
}
END_TEST

START_TEST(test_opt_aa_only_cb_unset)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_aa_only_cb);

	ck_assert_int_eq(opt_aa_only_cb(&opt, "10", 0), 0);
	ck_assert_int_eq(opt_aa_only_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.common.stop_us, 0);
	ck_assert_int_eq(params.common.stop_total_us, 0);
	ck_assert_int_eq(params.print_stack, 0);
	ck_assert_int_eq(params.common.aa_only, 0);
}
END_TEST

START_TEST(test_opt_timerlat_trace_output_cb)
{
	const char *trace_output = NULL;
	const struct option opt = TEST_CALLBACK(&trace_output, opt_timerlat_trace_output_cb);

	ck_assert_int_eq(opt_timerlat_trace_output_cb(&opt, "trace.txt", 0), 0);
	ck_assert_str_eq(trace_output, "trace.txt");
}
END_TEST

START_TEST(test_opt_timerlat_trace_output_cb_noarg)
{
	const char *trace_output = NULL;
	const struct option opt = TEST_CALLBACK(&trace_output, opt_timerlat_trace_output_cb);

	ck_assert_int_eq(opt_timerlat_trace_output_cb(&opt, NULL, 0), 0);
	ck_assert_str_eq(trace_output, "timerlat_trace.txt");
}
END_TEST

START_TEST(test_opt_timerlat_trace_output_cb_unset)
{
	const char *trace_output = NULL;
	const struct option opt = TEST_CALLBACK(&trace_output, opt_timerlat_trace_output_cb);

	ck_assert_int_eq(opt_timerlat_trace_output_cb(&opt, "trace.txt", 0), 0);
	ck_assert_int_eq(opt_timerlat_trace_output_cb(&opt, NULL, 1), 0);
	ck_assert_ptr_null(trace_output);
}
END_TEST

START_TEST(test_opt_timerlat_on_threshold_cb)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_timerlat_on_threshold_cb);

	ck_assert_int_eq(opt_timerlat_on_threshold_cb(&opt, "trace", 0), 0);
	ck_assert_int_eq(actions.len, 1);
	ck_assert_int_eq(actions.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions.list[0].trace_output, "timerlat_trace.txt");
}
END_TEST

START_TEST(test_opt_timerlat_on_threshold_cb_invalid)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_timerlat_on_threshold_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_timerlat_on_threshold_cb(&opt, "abc", 0), -1);
}
END_TEST

START_TEST(test_opt_timerlat_on_end_cb)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_timerlat_on_end_cb);

	ck_assert_int_eq(opt_timerlat_on_end_cb(&opt, "trace", 0), 0);
	ck_assert_int_eq(actions.len, 1);
	ck_assert_int_eq(actions.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions.list[0].trace_output, "timerlat_trace.txt");
}
END_TEST

START_TEST(test_opt_timerlat_on_end_cb_invalid)
{
	struct actions actions = {0};
	const struct option opt = TEST_CALLBACK(&actions, opt_timerlat_on_end_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_timerlat_on_end_cb(&opt, "abc", 0), -1);
}
END_TEST

START_TEST(test_opt_user_threads_cb)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_user_threads_cb);

	ck_assert_int_eq(opt_user_threads_cb(&opt, NULL, 0), 0);
	ck_assert_int_eq(params.common.user_workload, 1);
	ck_assert_int_eq(params.common.user_data, 1);
}
END_TEST

START_TEST(test_opt_user_threads_cb_unset)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_user_threads_cb);

	ck_assert_int_eq(opt_user_threads_cb(&opt, NULL, 0), 0);
	ck_assert_int_eq(opt_user_threads_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.common.user_workload, 0);
	ck_assert_int_eq(params.common.user_data, 0);
}
END_TEST

START_TEST(test_opt_nano_cb)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_nano_cb);

	ck_assert_int_eq(opt_nano_cb(&opt, NULL, 0), 0);
	ck_assert_int_eq(params.common.output_divisor, 1);
}
END_TEST

START_TEST(test_opt_nano_cb_unset)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_nano_cb);

	ck_assert_int_eq(opt_nano_cb(&opt, NULL, 0), 0);
	ck_assert_int_eq(opt_nano_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.common.output_divisor, default_output_divisor);
}
END_TEST

START_TEST(test_opt_timerlat_align_cb)
{
	struct timerlat_params params = {0};
	const struct option opt = RTLA_OPT_CALLBACK_DATA('A', "aligned", &params, "us",
		"test", opt_timerlat_align_cb, LLONG_RANGE(0, LLONG_MAX));

	ck_assert_int_eq(opt_timerlat_align_cb(&opt, "500", 0), 0);
	ck_assert(params.timerlat_align);
	ck_assert_int_eq(params.timerlat_align_us, 500);
}
END_TEST

START_TEST(test_opt_timerlat_align_cb_invalid)
{
	struct timerlat_params params = {0};
	const struct option opt = RTLA_OPT_CALLBACK_DATA('A', "aligned", &params, "us",
		"test", opt_timerlat_align_cb, LLONG_RANGE(0, LLONG_MAX));

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_timerlat_align_cb(&opt, "-1", 0), -1);
}
END_TEST

START_TEST(test_opt_timerlat_align_cb_unset)
{
	struct timerlat_params params = {0};
	const struct option opt = RTLA_OPT_CALLBACK_DATA('A', "aligned", &params, "us",
		"test", opt_timerlat_align_cb, LLONG_RANGE(0, LLONG_MAX));

	ck_assert_int_eq(opt_timerlat_align_cb(&opt, "500", 0), 0);
	ck_assert_int_eq(opt_timerlat_align_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(params.timerlat_align, 0);
	ck_assert_int_eq(params.timerlat_align_us, 0);
}
END_TEST

START_TEST(test_opt_stack_format_cb)
{
	int stack_format = 0;
	const struct option opt = TEST_CALLBACK(&stack_format, opt_stack_format_cb);

	ck_assert_int_eq(opt_stack_format_cb(&opt, "full", 0), 0);
	ck_assert_int_eq(stack_format, STACK_FORMAT_FULL);
}
END_TEST

START_TEST(test_opt_stack_format_cb_invalid)
{
	int stack_format = 0;
	const struct option opt = TEST_CALLBACK(&stack_format, opt_stack_format_cb);

	assert(freopen("/dev/null", "w", stderr));
	ck_assert_int_eq(opt_stack_format_cb(&opt, "abc", 0), -1);
}
END_TEST

START_TEST(test_opt_stack_format_cb_unset)
{
	int stack_format = 0;
	const struct option opt = TEST_CALLBACK(&stack_format, opt_stack_format_cb);

	ck_assert_int_eq(opt_stack_format_cb(&opt, "full", 0), 0);
	ck_assert_int_eq(opt_stack_format_cb(&opt, NULL, 1), 0);
	ck_assert_int_eq(stack_format, default_stack_format);
}
END_TEST


Suite *cli_opt_callback_suite(void)
{
	Suite *s = suite_create("cli_opt_callback");
	TCase *tc;

	tc = tcase_create("common");
	tcase_add_test(tc, test_opt_llong_callback_simple);
	tcase_add_test(tc, test_opt_llong_callback_max);
	tcase_add_test(tc, test_opt_llong_callback_min);
	tcase_add_test(tc, test_opt_llong_callback_non_numeric);
	tcase_add_test(tc, test_opt_llong_callback_non_numeric_suffix);
	tcase_add_test(tc, test_opt_llong_callback_unset);
	tcase_add_test(tc, test_opt_llong_callback_unset_defval);
	tcase_add_test(tc, test_opt_llong_callback_range_in);
	tcase_add_test(tc, test_opt_llong_callback_range_below);
	tcase_add_test(tc, test_opt_llong_callback_range_above);
	tcase_add_test(tc, test_opt_llong_callback_range_boundary);
	tcase_add_test(tc, test_opt_int_callback_simple);
	tcase_add_test(tc, test_opt_int_callback_max);
	tcase_add_test(tc, test_opt_int_callback_min);
	tcase_add_test(tc, test_opt_int_callback_non_numeric);
	tcase_add_test(tc, test_opt_int_callback_non_numeric_suffix);
	tcase_add_test(tc, test_opt_int_callback_unset);
	tcase_add_test(tc, test_opt_int_callback_unset_defval);
	tcase_add_test(tc, test_opt_int_callback_range_in);
	tcase_add_test(tc, test_opt_int_callback_range_below);
	tcase_add_test(tc, test_opt_int_callback_range_above);
	tcase_add_test(tc, test_opt_int_callback_range_boundary);
	tcase_add_test(tc, test_opt_cpus_cb);
	tcase_add_test(tc, test_opt_cpus_cb_invalid);
	tcase_add_test(tc, test_opt_cgroup_cb);
	tcase_add_test(tc, test_opt_cgroup_cb_equals);
	tcase_add_test(tc, test_opt_cgroup_cb_unset);
	tcase_add_test(tc, test_opt_duration_cb);
	tcase_add_test(tc, test_opt_duration_cb_unset);
	tcase_add_test(tc, test_opt_duration_cb_invalid);
	tcase_add_test(tc, test_opt_event_cb);
	tcase_add_test(tc, test_opt_event_cb_multiple);
	tcase_add_test(tc, test_opt_housekeeping_cb);
	tcase_add_test(tc, test_opt_housekeeping_cb_invalid);
	tcase_add_test(tc, test_opt_housekeeping_cb_unset);
	tcase_add_test(tc, test_opt_priority_cb);
	tcase_add_test(tc, test_opt_priority_cb_invalid);
	tcase_add_test(tc, test_opt_priority_cb_unset);
	tcase_add_test(tc, test_opt_trigger_cb);
	tcase_add_test(tc, test_opt_trigger_cb_no_event);
	tcase_add_test(tc, test_opt_filter_cb);
	tcase_add_test(tc, test_opt_filter_cb_no_event);
	suite_add_tcase(s, tc);

	tc = tcase_create("osnoise");
	tcase_add_test(tc, test_opt_osnoise_auto_cb);
	tcase_add_test(tc, test_opt_osnoise_auto_cb_unset);
	tcase_add_test(tc, test_opt_osnoise_trace_output_cb);
	tcase_add_test(tc, test_opt_osnoise_trace_output_cb_noarg);
	tcase_add_test(tc, test_opt_osnoise_trace_output_cb_unset);
	tcase_add_test(tc, test_opt_osnoise_on_threshold_cb);
	tcase_add_test(tc, test_opt_osnoise_on_threshold_cb_invalid);
	tcase_add_test(tc, test_opt_osnoise_on_end_cb);
	tcase_add_test(tc, test_opt_osnoise_on_end_cb_invalid);
	suite_add_tcase(s, tc);

	tc = tcase_create("timerlat");
	tcase_add_test(tc, test_opt_timerlat_auto_cb);
	tcase_add_test(tc, test_opt_timerlat_auto_cb_unset);
	tcase_add_test(tc, test_opt_aa_only_cb);
	tcase_add_test(tc, test_opt_aa_only_cb_unset);
	tcase_add_test(tc, test_opt_timerlat_trace_output_cb);
	tcase_add_test(tc, test_opt_timerlat_trace_output_cb_noarg);
	tcase_add_test(tc, test_opt_timerlat_trace_output_cb_unset);
	tcase_add_test(tc, test_opt_timerlat_on_threshold_cb);
	tcase_add_test(tc, test_opt_timerlat_on_threshold_cb_invalid);
	tcase_add_test(tc, test_opt_timerlat_on_end_cb);
	tcase_add_test(tc, test_opt_timerlat_on_end_cb_invalid);
	tcase_add_test(tc, test_opt_user_threads_cb);
	tcase_add_test(tc, test_opt_user_threads_cb_unset);
	tcase_add_test(tc, test_opt_nano_cb);
	tcase_add_test(tc, test_opt_nano_cb_unset);
	tcase_add_test(tc, test_opt_stack_format_cb);
	tcase_add_test(tc, test_opt_stack_format_cb_invalid);
	tcase_add_test(tc, test_opt_stack_format_cb_unset);
	tcase_add_test(tc, test_opt_timerlat_align_cb);
	tcase_add_test(tc, test_opt_timerlat_align_cb_invalid);
	tcase_add_test(tc, test_opt_timerlat_align_cb_unset);
	suite_add_tcase(s, tc);

	return s;
}
