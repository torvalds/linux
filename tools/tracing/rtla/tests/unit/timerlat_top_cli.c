// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <limits.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#include <linux/container_of.h>

#include "cli_params_assert.h"
#include "../../src/cli.h"

#define PARSE_ARGS(...) char *argv[] = { __VA_ARGS__, NULL  };\
			int argc = sizeof(argv) / sizeof(char *) - 1;\
			struct common_params *params =\
				timerlat_top_parse_args(argc, argv);\
			struct timerlat_params *tlat_params __maybe_unused =\
				to_timerlat_params(params)

/* Tracing Options */

START_TEST(test_irq_short)
{
	PARSE_ARGS("timerlat", "top", "-i", "20");

	ck_assert_int_eq(params->stop_us, 20);
}
END_TEST

START_TEST(test_irq_long)
{
	PARSE_ARGS("timerlat", "top", "--irq", "20");

	ck_assert_int_eq(params->stop_us, 20);
}
END_TEST

START_TEST(test_period_short)
{
	PARSE_ARGS("timerlat", "top", "-p", "200");

	ck_assert_int_eq(tlat_params->timerlat_period_us, 200);
}
END_TEST

START_TEST(test_period_long)
{
	PARSE_ARGS("timerlat", "top", "--period", "200");

	ck_assert_int_eq(tlat_params->timerlat_period_us, 200);
}
END_TEST

START_TEST(test_stack_short)
{
	PARSE_ARGS("timerlat", "top", "-s", "20");

	ck_assert_int_eq(tlat_params->print_stack, 20);
}
END_TEST

START_TEST(test_stack_long)
{
	PARSE_ARGS("timerlat", "top", "--stack", "20");

	ck_assert_int_eq(tlat_params->print_stack, 20);
}
END_TEST

START_TEST(test_thread_short)
{
	PARSE_ARGS("timerlat", "top", "-T", "20");

	ck_assert_int_eq(params->stop_total_us, 20);
}
END_TEST

START_TEST(test_thread_long)
{
	PARSE_ARGS("timerlat", "top", "--thread", "20");

	ck_assert_int_eq(params->stop_total_us, 20);
}
END_TEST

/* Event Configuration */

START_TEST(test_event_short)
{
	PARSE_ARGS("timerlat", "top", "-e", "system:event");

	CLI_ASSERT_SINGLE_EVENT("system", "event");
}
END_TEST

START_TEST(test_event_long)
{
	PARSE_ARGS("timerlat", "top", "--event", "system:event");

	CLI_ASSERT_SINGLE_EVENT("system", "event");
}
END_TEST

START_TEST(test_filter)
{
	PARSE_ARGS("timerlat", "top", "-e", "system:event", "--filter", "filter");

	CLI_ASSERT_SINGLE_FILTER("filter");
}
END_TEST

START_TEST(test_trigger)
{
	PARSE_ARGS("timerlat", "top", "-e", "system:event", "--trigger", "trigger");

	CLI_ASSERT_SINGLE_TRIGGER("trigger");
}
END_TEST

START_TEST(test_trace_short_noarg)
{
	PARSE_ARGS("timerlat", "top", "-t");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "timerlat_trace.txt");
}
END_TEST

START_TEST(test_trace_short_followarg)
{
	PARSE_ARGS("timerlat", "top", "-t", "-d", "20");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "timerlat_trace.txt");
	ck_assert_int_eq(params->duration, 20); /* check if next argument is read correctly */
}
END_TEST

START_TEST(test_trace_short_space)
{
	PARSE_ARGS("timerlat", "top", "-t", "tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

START_TEST(test_trace_short_equals)
{
	PARSE_ARGS("timerlat", "top", "-t=tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

START_TEST(test_trace_long_noarg)
{
	PARSE_ARGS("timerlat", "top", "--trace");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "timerlat_trace.txt");
}
END_TEST

START_TEST(test_trace_long_followarg)
{
	PARSE_ARGS("timerlat", "top", "--trace", "-d", "20");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "timerlat_trace.txt");
	ck_assert_int_eq(params->duration, 20); /* check if next argument is read correctly */
}
END_TEST

START_TEST(test_trace_long_space)
{
	PARSE_ARGS("timerlat", "top", "--trace", "tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

START_TEST(test_trace_long_equals)
{
	PARSE_ARGS("timerlat", "top", "--trace=tracefile");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "tracefile");
}
END_TEST

/* CPU Configuration */

START_TEST(test_cpus_short)
{
	nr_cpus = 4;

	PARSE_ARGS("timerlat", "top", "-c", "0-1,3");

	ck_assert_str_eq(params->cpus, "0-1,3");
	CLI_ASSERT_CPUSET(monitored_cpus, 0, 1, 3);
}
END_TEST

START_TEST(test_cpus_long)
{
	nr_cpus = 4;

	PARSE_ARGS("timerlat", "top", "--cpus", "0-1,3");

	ck_assert_str_eq(params->cpus, "0-1,3");
	CLI_ASSERT_CPUSET(monitored_cpus, 0, 1, 3);
}
END_TEST

START_TEST(test_housekeeping_short)
{
	nr_cpus = 4;

	PARSE_ARGS("timerlat", "top", "-H", "0-1,3");

	CLI_ASSERT_CPUSET(hk_cpu_set, 0, 1, 3);
}
END_TEST

START_TEST(test_housekeeping_long)
{
	nr_cpus = 4;

	PARSE_ARGS("timerlat", "top", "--house-keeping", "0-1,3");

	CLI_ASSERT_CPUSET(hk_cpu_set, 0, 1, 3);
}
END_TEST

/* Thread Configuration */

START_TEST(test_cgroup_short_noarg)
{
	PARSE_ARGS("timerlat", "top", "-C");

	ck_assert(params->cgroup);
	ck_assert_ptr_null(params->cgroup_name);
}
END_TEST

START_TEST(test_cgroup_short_space)
{
	PARSE_ARGS("timerlat", "top", "-C", "cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_cgroup_short_equals)
{
	PARSE_ARGS("timerlat", "top", "-C=cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_cgroup_long_noarg)
{
	PARSE_ARGS("timerlat", "top", "--cgroup");

	ck_assert(params->cgroup);
	ck_assert_ptr_null(params->cgroup_name);
}
END_TEST

START_TEST(test_cgroup_long_space)
{
	PARSE_ARGS("timerlat", "top", "--cgroup", "cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_cgroup_long_equals)
{
	PARSE_ARGS("timerlat", "top", "--cgroup=cgroup");

	ck_assert(params->cgroup);
	ck_assert_str_eq(params->cgroup_name, "cgroup");
}
END_TEST

START_TEST(test_kernel_threads_short)
{
	PARSE_ARGS("timerlat", "top", "-k");

	ck_assert(params->kernel_workload);
	ck_assert(!params->user_workload);
	ck_assert(!params->user_data);
}
END_TEST

START_TEST(test_kernel_threads_long)
{
	PARSE_ARGS("timerlat", "top", "--kernel-threads");

	ck_assert(params->kernel_workload);
	ck_assert(!params->user_workload);
	ck_assert(!params->user_data);
}
END_TEST

START_TEST(test_priority_short)
{
	PARSE_ARGS("timerlat", "top", "-P", "f:95");

	ck_assert_int_eq(params->sched_param.sched_policy, SCHED_FIFO);
	ck_assert_int_eq(params->sched_param.sched_priority, 95);
}
END_TEST

START_TEST(test_priority_long)
{
	PARSE_ARGS("timerlat", "top", "--priority", "f:95");

	ck_assert_int_eq(params->sched_param.sched_policy, SCHED_FIFO);
	ck_assert_int_eq(params->sched_param.sched_priority, 95);
}
END_TEST

START_TEST(test_user_load_short)
{
	PARSE_ARGS("timerlat", "top", "-U");

	ck_assert(!params->kernel_workload);
	ck_assert(!params->user_workload);
	ck_assert(params->user_data);
}
END_TEST

START_TEST(test_user_load_long)
{
	PARSE_ARGS("timerlat", "top", "--user-load");

	ck_assert(!params->kernel_workload);
	ck_assert(!params->user_workload);
	ck_assert(params->user_data);
}
END_TEST

START_TEST(test_user_threads_short)
{
	PARSE_ARGS("timerlat", "top", "-u");

	ck_assert(!params->kernel_workload);
	ck_assert(params->user_workload);
	ck_assert(params->user_data);
}
END_TEST

START_TEST(test_user_threads_long)
{
	PARSE_ARGS("timerlat", "top", "--user-threads");

	ck_assert(!params->kernel_workload);
	ck_assert(params->user_workload);
	ck_assert(params->user_data);
}
END_TEST

START_TEST(test_aligned_short)
{
	PARSE_ARGS("timerlat", "top", "-A", "500");

	ck_assert(tlat_params->timerlat_align);
	ck_assert_int_eq(tlat_params->timerlat_align_us, 500);
}
END_TEST

START_TEST(test_aligned_long)
{
	PARSE_ARGS("timerlat", "top", "--aligned", "500");

	ck_assert(tlat_params->timerlat_align);
	ck_assert_int_eq(tlat_params->timerlat_align_us, 500);
}
END_TEST

/* Output */

START_TEST(test_nano_short)
{
	PARSE_ARGS("timerlat", "top", "-n");

	ck_assert_int_eq(params->output_divisor, 1);
}
END_TEST

START_TEST(test_nano_long)
{
	PARSE_ARGS("timerlat", "top", "--nano");

	ck_assert_int_eq(params->output_divisor, 1);
}
END_TEST

START_TEST(test_quiet_short)
{
	PARSE_ARGS("timerlat", "top", "-q");

	ck_assert(params->quiet);
}
END_TEST

START_TEST(test_quiet_long)
{
	PARSE_ARGS("timerlat", "top", "--quiet");

	ck_assert(params->quiet);
}
END_TEST

/* System Tuning */

START_TEST(test_deepest_idle_state)
{
	PARSE_ARGS("timerlat", "top", "--deepest-idle-state", "1");

	ck_assert_int_eq(tlat_params->deepest_idle_state, 1);
}
END_TEST

START_TEST(test_dma_latency)
{
	PARSE_ARGS("timerlat", "top", "--dma-latency", "10");

	ck_assert_int_eq(tlat_params->dma_latency, 10);
}
END_TEST

START_TEST(test_trace_buffer_size)
{
	PARSE_ARGS("timerlat", "top", "--trace-buffer-size", "200");

	ck_assert_int_eq(params->buffer_size, 200);
}
END_TEST

START_TEST(test_warm_up)
{
	PARSE_ARGS("timerlat", "top", "--warm-up", "5");

	ck_assert_int_eq(params->warmup, 5);
}
END_TEST

/* Auto Analysis and Actions */

START_TEST(test_auto)
{
	PARSE_ARGS("timerlat", "top", "-a", "20");

	CLI_TIMERLAT_ASSERT_AUTO(20);
}
END_TEST

START_TEST(test_aa_only)
{
	PARSE_ARGS("timerlat", "top", "--aa-only", "20");

	CLI_TIMERLAT_ASSERT_AA_ONLY(20);
}
END_TEST

START_TEST(test_bpf_action)
{
	PARSE_ARGS("timerlat", "top", "--bpf-action", "program");

	ck_assert_str_eq(tlat_params->bpf_action_program, "program");
}
END_TEST

START_TEST(test_dump_tasks)
{
	PARSE_ARGS("timerlat", "top", "--dump-tasks");

	ck_assert(tlat_params->dump_tasks);
}
END_TEST

START_TEST(test_no_aa)
{
	PARSE_ARGS("timerlat", "top", "--no-aa");

	ck_assert(tlat_params->no_aa);
}
END_TEST

START_TEST(test_on_end)
{
	PARSE_ARGS("timerlat", "top", "--on-end", "trace");

	CLI_ASSERT_SINGLE_ACTION(end_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "timerlat_trace.txt");
}
END_TEST

START_TEST(test_on_threshold)
{
	PARSE_ARGS("timerlat", "top", "--on-threshold", "trace");

	CLI_ASSERT_SINGLE_ACTION(threshold_actions, ACTION_TRACE_OUTPUT, trace_output, str,
				 "timerlat_trace.txt");
}
END_TEST

START_TEST(test_stack_format)
{
	PARSE_ARGS("timerlat", "top", "--stack-format", "truncate");

	ck_assert_int_eq(tlat_params->stack_format, STACK_FORMAT_TRUNCATE);
}
END_TEST

/* General */

START_TEST(test_debug_short)
{
	PARSE_ARGS("timerlat", "top", "-D");

	ck_assert(config_debug);
}
END_TEST

START_TEST(test_debug_long)
{
	PARSE_ARGS("timerlat", "top", "--debug");

	ck_assert(config_debug);
}
END_TEST

START_TEST(test_duration_short)
{
	PARSE_ARGS("timerlat", "top", "-d", "1m");

	ck_assert_int_eq(params->duration, 60);
}
END_TEST

START_TEST(test_duration_long)
{
	PARSE_ARGS("timerlat", "top", "--duration", "1m");

	ck_assert_int_eq(params->duration, 60);
}
END_TEST

Suite *timerlat_top_cli_suite(void)
{
	Suite *s = suite_create("timerlat_top_cli");
	TCase *tc;

	tc = tcase_create("tracing_options");
	tcase_add_test(tc, test_irq_short);
	tcase_add_test(tc, test_irq_long);
	tcase_add_test(tc, test_period_short);
	tcase_add_test(tc, test_period_long);
	tcase_add_test(tc, test_stack_short);
	tcase_add_test(tc, test_stack_long);
	tcase_add_test(tc, test_thread_short);
	tcase_add_test(tc, test_thread_long);
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
	tcase_add_test(tc, test_kernel_threads_short);
	tcase_add_test(tc, test_kernel_threads_long);
	tcase_add_test(tc, test_priority_short);
	tcase_add_test(tc, test_priority_long);
	tcase_add_test(tc, test_user_load_short);
	tcase_add_test(tc, test_user_load_long);
	tcase_add_test(tc, test_user_threads_short);
	tcase_add_test(tc, test_user_threads_long);
	tcase_add_test(tc, test_aligned_short);
	tcase_add_test(tc, test_aligned_long);
	suite_add_tcase(s, tc);

	tc = tcase_create("output");
	tcase_add_test(tc, test_nano_short);
	tcase_add_test(tc, test_nano_long);
	tcase_add_test(tc, test_quiet_short);
	tcase_add_test(tc, test_quiet_long);
	suite_add_tcase(s, tc);

	tc = tcase_create("system_tuning");
	tcase_add_test(tc, test_deepest_idle_state);
	tcase_add_test(tc, test_dma_latency);
	tcase_add_test(tc, test_trace_buffer_size);
	tcase_add_test(tc, test_warm_up);
	suite_add_tcase(s, tc);

	tc = tcase_create("aa_actions");
	tcase_add_test(tc, test_auto);
	tcase_add_test(tc, test_aa_only);
	tcase_add_test(tc, test_bpf_action);
	tcase_add_test(tc, test_dump_tasks);
	tcase_add_test(tc, test_no_aa);
	tcase_add_test(tc, test_on_end);
	tcase_add_test(tc, test_on_threshold);
	tcase_add_test(tc, test_stack_format);
	suite_add_tcase(s, tc);

	tc = tcase_create("general");
	tcase_add_test(tc, test_debug_short);
	tcase_add_test(tc, test_debug_long);
	tcase_add_test(tc, test_duration_short);
	tcase_add_test(tc, test_duration_long);
	suite_add_tcase(s, tc);

	return s;
}
