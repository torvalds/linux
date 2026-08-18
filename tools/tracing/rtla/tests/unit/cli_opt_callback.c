// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <stdio.h>
#include <check.h>

#define RTLA_ALLOW_CLI_P_H
#include "../../src/cli_p.h"
#include "cli_params_assert.h"

#define TEST_CALLBACK(value, cb) OPT_CALLBACK('t', "test", value, "test value", "test help", cb)

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

	ck_assert_int_eq(opt_int_callback(&opt, "abc", 0), -1);
	ck_assert_int_eq(test_value, 0);
}
END_TEST

START_TEST(test_opt_int_callback_non_numeric_suffix)
{
	int test_value = 0;
	const struct option opt = TEST_CALLBACK(&test_value, opt_int_callback);

	ck_assert_int_eq(opt_int_callback(&opt, "1234567890abc", 0), -1);
	ck_assert_int_eq(test_value, 0);
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
	opt_cpus_cb(&opt, "0-3,5", 0);
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
	opt_duration_cb(&opt, "abc", 0);
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
	opt_housekeeping_cb(&opt, "0-3,5", 0);
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
	opt_priority_cb(&opt, "abc", 0);
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
	opt_trigger_cb(&opt, "stacktrace", 0);
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
	opt_filter_cb(&opt, "comm ~ \"rtla\"", 0);
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

START_TEST(test_opt_osnoise_period_cb)
{
	unsigned long long period = 0;
	const struct option opt = TEST_CALLBACK(&period, opt_osnoise_period_cb);

	ck_assert_int_eq(opt_osnoise_period_cb(&opt, "1000000", 0), 0);
	ck_assert_int_eq(period, 1000000);
}
END_TEST

START_TEST(test_opt_osnoise_period_cb_invalid)
{
	unsigned long long period = 0;
	const struct option opt = TEST_CALLBACK(&period, opt_osnoise_period_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_osnoise_period_cb(&opt, "10000001", 0);
}
END_TEST

START_TEST(test_opt_osnoise_runtime_cb)
{
	unsigned long long runtime = 0;
	const struct option opt = TEST_CALLBACK(&runtime, opt_osnoise_runtime_cb);

	ck_assert_int_eq(opt_osnoise_runtime_cb(&opt, "900000", 0), 0);
	ck_assert_int_eq(runtime, 900000);
}
END_TEST

START_TEST(test_opt_osnoise_runtime_cb_invalid)
{
	unsigned long long runtime = 0;
	const struct option opt = TEST_CALLBACK(&runtime, opt_osnoise_runtime_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_osnoise_runtime_cb(&opt, "99", 0);
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
	opt_osnoise_on_threshold_cb(&opt, "abc", 0);
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
	opt_osnoise_on_end_cb(&opt, "abc", 0);
}
END_TEST

START_TEST(test_opt_timerlat_period_cb)
{
	long long period = 0;
	const struct option opt = TEST_CALLBACK(&period, opt_timerlat_period_cb);

	ck_assert_int_eq(opt_timerlat_period_cb(&opt, "1000", 0), 0);
	ck_assert_int_eq(period, 1000);
}
END_TEST

START_TEST(test_opt_timerlat_period_cb_invalid)
{
	long long period = 0;
	const struct option opt = TEST_CALLBACK(&period, opt_timerlat_period_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_timerlat_period_cb(&opt, "1000001", 0);
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

START_TEST(test_opt_dma_latency_cb)
{
	int dma_latency = 0;
	const struct option opt = TEST_CALLBACK(&dma_latency, opt_dma_latency_cb);

	ck_assert_int_eq(opt_dma_latency_cb(&opt, "1000", 0), 0);
	ck_assert_int_eq(dma_latency, 1000);
}
END_TEST

START_TEST(test_opt_dma_latency_cb_min)
{
	int dma_latency = 0;
	const struct option opt = TEST_CALLBACK(&dma_latency, opt_dma_latency_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_dma_latency_cb(&opt, "-1", 0);
}
END_TEST

START_TEST(test_opt_dma_latency_cb_max)
{
	int dma_latency = 0;
	const struct option opt = TEST_CALLBACK(&dma_latency, opt_dma_latency_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_dma_latency_cb(&opt, "10001", 0);
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
	opt_timerlat_on_threshold_cb(&opt, "abc", 0);
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
	opt_timerlat_on_end_cb(&opt, "abc", 0);
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

START_TEST(test_opt_nano_cb)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_nano_cb);

	ck_assert_int_eq(opt_nano_cb(&opt, NULL, 0), 0);
	ck_assert_int_eq(params.common.output_divisor, 1);
}
END_TEST

START_TEST(test_opt_timerlat_align_cb)
{
	struct timerlat_params params = {0};
	const struct option opt = TEST_CALLBACK(&params, opt_timerlat_align_cb);

	ck_assert_int_eq(opt_timerlat_align_cb(&opt, "500", 0), 0);
	ck_assert(params.timerlat_align);
	ck_assert_int_eq(params.timerlat_align_us, 500);
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
	opt_stack_format_cb(&opt, "abc", 0);
}
END_TEST

START_TEST(test_opt_bucket_size_cb)
{
	int bucket_size = 0;
	const struct option opt = TEST_CALLBACK(&bucket_size, opt_bucket_size_cb);

	ck_assert_int_eq(opt_bucket_size_cb(&opt, "100", 0), 0);
	ck_assert_int_eq(bucket_size, 100);
}
END_TEST

START_TEST(test_opt_bucket_size_min)
{
	int bucket_size = 0;
	const struct option opt = TEST_CALLBACK(&bucket_size, opt_bucket_size_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_bucket_size_cb(&opt, "0", 0);
}
END_TEST

START_TEST(test_opt_bucket_size_max)
{
	int bucket_size = 0;
	const struct option opt = TEST_CALLBACK(&bucket_size, opt_bucket_size_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_bucket_size_cb(&opt, "1000001", 0);
}
END_TEST

START_TEST(test_opt_entries_cb)
{
	int entries = 0;
	const struct option opt = TEST_CALLBACK(&entries, opt_entries_cb);

	ck_assert_int_eq(opt_entries_cb(&opt, "100", 0), 0);
	ck_assert_int_eq(entries, 100);
}
END_TEST

START_TEST(test_opt_entries_min)
{
	int entries = 0;
	const struct option opt = TEST_CALLBACK(&entries, opt_entries_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_entries_cb(&opt, "9", 0);
}
END_TEST

START_TEST(test_opt_entries_max)
{
	int entries = 0;
	const struct option opt = TEST_CALLBACK(&entries, opt_entries_cb);

	assert(freopen("/dev/null", "w", stderr));
	opt_entries_cb(&opt, "10000000", 0);
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
	tcase_add_test(tc, test_opt_int_callback_simple);
	tcase_add_test(tc, test_opt_int_callback_max);
	tcase_add_test(tc, test_opt_int_callback_min);
	tcase_add_test(tc, test_opt_int_callback_non_numeric);
	tcase_add_test(tc, test_opt_int_callback_non_numeric_suffix);
	tcase_add_test(tc, test_opt_cpus_cb);
	tcase_add_exit_test(tc, test_opt_cpus_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_cgroup_cb);
	tcase_add_test(tc, test_opt_cgroup_cb_equals);
	tcase_add_test(tc, test_opt_duration_cb);
	tcase_add_exit_test(tc, test_opt_duration_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_event_cb);
	tcase_add_test(tc, test_opt_event_cb_multiple);
	tcase_add_test(tc, test_opt_housekeeping_cb);
	tcase_add_exit_test(tc, test_opt_housekeeping_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_priority_cb);
	tcase_add_exit_test(tc, test_opt_priority_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_trigger_cb);
	tcase_add_exit_test(tc, test_opt_trigger_cb_no_event, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_filter_cb);
	tcase_add_exit_test(tc, test_opt_filter_cb_no_event, EXIT_FAILURE);
	suite_add_tcase(s, tc);

	tc = tcase_create("osnoise");
	tcase_add_test(tc, test_opt_osnoise_auto_cb);
	tcase_add_test(tc, test_opt_osnoise_period_cb);
	tcase_add_exit_test(tc, test_opt_osnoise_period_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_osnoise_runtime_cb);
	tcase_add_exit_test(tc, test_opt_osnoise_runtime_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_osnoise_trace_output_cb);
	tcase_add_test(tc, test_opt_osnoise_trace_output_cb_noarg);
	tcase_add_test(tc, test_opt_osnoise_on_threshold_cb);
	tcase_add_exit_test(tc, test_opt_osnoise_on_threshold_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_osnoise_on_end_cb);
	tcase_add_exit_test(tc, test_opt_osnoise_on_end_cb_invalid, EXIT_FAILURE);
	suite_add_tcase(s, tc);

	tc = tcase_create("timerlat");
	tcase_add_test(tc, test_opt_timerlat_period_cb);
	tcase_add_exit_test(tc, test_opt_timerlat_period_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_timerlat_auto_cb);
	tcase_add_test(tc, test_opt_dma_latency_cb);
	tcase_add_exit_test(tc, test_opt_dma_latency_cb_min, EXIT_FAILURE);
	tcase_add_exit_test(tc, test_opt_dma_latency_cb_max, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_aa_only_cb);
	tcase_add_test(tc, test_opt_timerlat_trace_output_cb);
	tcase_add_test(tc, test_opt_timerlat_trace_output_cb_noarg);
	tcase_add_test(tc, test_opt_timerlat_on_threshold_cb);
	tcase_add_exit_test(tc, test_opt_timerlat_on_threshold_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_timerlat_on_end_cb);
	tcase_add_exit_test(tc, test_opt_timerlat_on_end_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_user_threads_cb);
	tcase_add_test(tc, test_opt_nano_cb);
	tcase_add_test(tc, test_opt_stack_format_cb);
	tcase_add_exit_test(tc, test_opt_stack_format_cb_invalid, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_timerlat_align_cb);
	suite_add_tcase(s, tc);

	tc = tcase_create("histogram");
	tcase_add_test(tc, test_opt_bucket_size_cb);
	tcase_add_exit_test(tc, test_opt_bucket_size_min, EXIT_FAILURE);
	tcase_add_exit_test(tc, test_opt_bucket_size_max, EXIT_FAILURE);
	tcase_add_test(tc, test_opt_entries_cb);
	tcase_add_exit_test(tc, test_opt_entries_min, EXIT_FAILURE);
	tcase_add_exit_test(tc, test_opt_entries_max, EXIT_FAILURE);
	suite_add_tcase(s, tc);

	return s;
}
