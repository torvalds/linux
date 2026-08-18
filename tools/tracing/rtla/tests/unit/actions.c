// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <check.h>
#include <signal.h>

#include "../../src/actions.h"

static struct actions actions_fixture;

static void actions_fixture_setup(void)
{
	actions_init(&actions_fixture);
}

static void actions_fixture_teardown(void)
{
	actions_destroy(&actions_fixture);
}

START_TEST(test_actions_init)
{
	struct actions actions;

	actions_init(&actions);

	ck_assert_int_eq(actions.len, 0);
	ck_assert_int_eq(actions.size, action_default_size);
	ck_assert(!actions.continue_flag);
	ck_assert_ptr_eq(actions.trace_output_inst, NULL);
}
END_TEST

START_TEST(test_actions_destroy)
{
	struct actions actions;

	actions_init(&actions);
	actions_destroy(&actions);
}
END_TEST

START_TEST(test_actions_reallocate)
{
	struct actions actions;
	int i;

	actions_init(&actions);

	ck_assert_int_eq(actions.len, 0);
	ck_assert_int_eq(actions.size, action_default_size);

	/* Fill size of actions array */
	for (i = 0; i < action_default_size; i++)
		actions_add_continue(&actions);

	ck_assert_int_eq(actions.len, action_default_size);
	ck_assert_int_eq(actions.size, action_default_size);

	/* Add one more action to trigger reallocation */
	actions_add_continue(&actions);

	ck_assert_int_eq(actions.len, action_default_size + 1);
	ck_assert_int_eq(actions.size, action_default_size * 2);

	actions_destroy(&actions);
}
END_TEST

START_TEST(test_actions_add_trace_output)
{
	actions_add_trace_output(&actions_fixture, "trace_output.txt");

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions_fixture.list[0].trace_output, "trace_output.txt");
	ck_assert(actions_fixture.present[ACTION_TRACE_OUTPUT]);
}
END_TEST

START_TEST(test_actions_add_signal)
{
	actions_add_signal(&actions_fixture, SIGINT, 1234);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_SIGNAL);
	ck_assert_int_eq(actions_fixture.list[0].signal, SIGINT);
	ck_assert_int_eq(actions_fixture.list[0].pid, 1234);
	ck_assert(actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_add_shell)
{
	actions_add_shell(&actions_fixture, "echo Hello");

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_SHELL);
	ck_assert_str_eq(actions_fixture.list[0].command, "echo Hello");
	ck_assert(actions_fixture.present[ACTION_SHELL]);
}
END_TEST

START_TEST(test_actions_add_continue)
{
	actions_add_continue(&actions_fixture);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_CONTINUE);
	ck_assert(actions_fixture.present[ACTION_CONTINUE]);
}
END_TEST

START_TEST(test_actions_add_multiple_same_action)
{
	actions_add_trace_output(&actions_fixture, "trace1.txt");
	actions_add_trace_output(&actions_fixture, "trace2.txt");

	ck_assert_int_eq(actions_fixture.len, 2);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions_fixture.list[0].trace_output, "trace1.txt");
	ck_assert_int_eq(actions_fixture.list[1].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions_fixture.list[1].trace_output, "trace2.txt");
	ck_assert(actions_fixture.present[ACTION_TRACE_OUTPUT]);
}
END_TEST

START_TEST(test_actions_add_multiple_different_action)
{
	actions_add_trace_output(&actions_fixture, "trace_output.txt");
	actions_add_signal(&actions_fixture, SIGINT, 1234);

	ck_assert_int_eq(actions_fixture.len, 2);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions_fixture.list[0].trace_output, "trace_output.txt");
	ck_assert(actions_fixture.present[ACTION_TRACE_OUTPUT]);
	ck_assert_int_eq(actions_fixture.list[1].type, ACTION_SIGNAL);
	ck_assert_int_eq(actions_fixture.list[1].signal, SIGINT);
	ck_assert_int_eq(actions_fixture.list[1].pid, 1234);
	ck_assert(actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_trace_output)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "trace", "trace.txt"), 0);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions_fixture.list[0].trace_output, "trace.txt");
	ck_assert(actions_fixture.present[ACTION_TRACE_OUTPUT]);
}
END_TEST

START_TEST(test_actions_parse_trace_output_arg)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "trace,file=trace2.txt", "trace1.txt"), 0);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_TRACE_OUTPUT);
	ck_assert_str_eq(actions_fixture.list[0].trace_output, "trace2.txt");
	ck_assert(actions_fixture.present[ACTION_TRACE_OUTPUT]);
}
END_TEST

START_TEST(test_actions_parse_trace_output_arg_bad)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "trace,foo=bar", "trace_output.txt"), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_TRACE_OUTPUT]);
}
END_TEST

START_TEST(test_actions_parse_signal)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "signal,num=1,pid=1234", NULL), 0);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_SIGNAL);
	ck_assert_int_eq(actions_fixture.list[0].signal, 1);
	ck_assert_int_eq(actions_fixture.list[0].pid, 1234);
	ck_assert(actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_signal_swapped)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "signal,pid=1234,num=1", NULL), 0);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_SIGNAL);
	ck_assert_int_eq(actions_fixture.list[0].signal, 1);
	ck_assert_int_eq(actions_fixture.list[0].pid, 1234);
	ck_assert(actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_signal_parent)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "signal,pid=parent,num=1", NULL), 0);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_SIGNAL);
	ck_assert_int_eq(actions_fixture.list[0].signal, 1);
	ck_assert_int_eq(actions_fixture.list[0].pid, -1);
	ck_assert(actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_signal_no_arg)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "signal", NULL), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_signal_no_pid)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "signal,num=1", NULL), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_signal_no_num)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "signal,pid=1234", NULL), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_signal_arg_bad)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "signal,foo=bar", NULL), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_SIGNAL]);
}
END_TEST

START_TEST(test_actions_parse_shell)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "shell,command=echo Hello", NULL), 0);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_SHELL);
	ck_assert_str_eq(actions_fixture.list[0].command, "echo Hello");
	ck_assert(actions_fixture.present[ACTION_SHELL]);
}
END_TEST

START_TEST(test_actions_parse_shell_no_arg)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "shell", NULL), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_SHELL]);
}
END_TEST

START_TEST(test_actions_parse_shell_arg_bad)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "shell,foo=bar", NULL), -1);
	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_SHELL]);
}
END_TEST

START_TEST(test_actions_parse_continue)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "continue", NULL), 0);

	ck_assert_int_eq(actions_fixture.len, 1);
	ck_assert_int_eq(actions_fixture.list[0].type, ACTION_CONTINUE);
	ck_assert(actions_fixture.present[ACTION_CONTINUE]);
}
END_TEST

START_TEST(test_actions_parse_continue_arg_bad)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "continue,foo=bar", NULL), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
	ck_assert(!actions_fixture.present[ACTION_CONTINUE]);
}
END_TEST

START_TEST(test_actions_parse_invalid)
{
	ck_assert_int_eq(actions_parse(&actions_fixture, "foobar", NULL), -1);

	ck_assert_int_eq(actions_fixture.len, 0);
}
END_TEST

START_TEST(test_actions_perform_continue)
{
	actions_add_continue(&actions_fixture);
	ck_assert_int_eq(actions_perform(&actions_fixture), 0);

	ck_assert(actions_fixture.continue_flag);
}
END_TEST

START_TEST(test_actions_perform_continue_after_successful_shell_command)
{
	actions_add_shell(&actions_fixture, "exit 0");
	actions_add_continue(&actions_fixture);
	ck_assert_int_eq(actions_perform(&actions_fixture), 0 << 8);

	ck_assert(actions_fixture.continue_flag);
}
END_TEST

START_TEST(test_actions_perform_continue_after_failed_shell_command)
{
	actions_add_shell(&actions_fixture, "exit 1");
	actions_add_continue(&actions_fixture);
	ck_assert_int_eq(actions_perform(&actions_fixture), 1 << 8);

	ck_assert(!actions_fixture.continue_flag);
}
END_TEST

START_TEST(test_actions_perform_continue_unset_flag)
{
	actions_fixture.continue_flag = true;

	actions_add_shell(&actions_fixture, "exit 1");
	actions_add_continue(&actions_fixture);
	ck_assert_int_eq(actions_perform(&actions_fixture), 1 << 8);

	ck_assert(!actions_fixture.continue_flag);
}
END_TEST

Suite *actions_suite(void)
{
	Suite *s = suite_create("actions");
	TCase *tc;

	tc = tcase_create("alloc");
	tcase_add_test(tc, test_actions_init);
	tcase_add_test(tc, test_actions_destroy);
	tcase_add_test(tc, test_actions_reallocate);
	suite_add_tcase(s, tc);

	tc = tcase_create("add");
	tcase_add_checked_fixture(tc, actions_fixture_setup, actions_fixture_teardown);
	tcase_add_test(tc, test_actions_add_trace_output);
	tcase_add_test(tc, test_actions_add_signal);
	tcase_add_test(tc, test_actions_add_shell);
	tcase_add_test(tc, test_actions_add_continue);
	tcase_add_test(tc, test_actions_add_multiple_same_action);
	tcase_add_test(tc, test_actions_add_multiple_different_action);
	suite_add_tcase(s, tc);

	tc = tcase_create("parse");
	tcase_add_checked_fixture(tc, actions_fixture_setup, actions_fixture_teardown);
	tcase_add_test(tc, test_actions_parse_trace_output);
	tcase_add_test(tc, test_actions_parse_trace_output_arg);
	tcase_add_test(tc, test_actions_parse_trace_output_arg_bad);
	tcase_add_test(tc, test_actions_parse_signal);
	tcase_add_test(tc, test_actions_parse_signal_swapped);
	tcase_add_test(tc, test_actions_parse_signal_parent);
	tcase_add_test(tc, test_actions_parse_signal_no_arg);
	tcase_add_test(tc, test_actions_parse_signal_no_pid);
	tcase_add_test(tc, test_actions_parse_signal_no_num);
	tcase_add_test(tc, test_actions_parse_signal_arg_bad);
	tcase_add_test(tc, test_actions_parse_shell);
	tcase_add_test(tc, test_actions_parse_shell_no_arg);
	tcase_add_test(tc, test_actions_parse_shell_arg_bad);
	tcase_add_test(tc, test_actions_parse_continue);
	tcase_add_test(tc, test_actions_parse_continue_arg_bad);
	tcase_add_test(tc, test_actions_parse_invalid);
	suite_add_tcase(s, tc);

	tc = tcase_create("perform");
	tcase_add_checked_fixture(tc, actions_fixture_setup, actions_fixture_teardown);
	tcase_add_test(tc, test_actions_perform_continue);
	tcase_add_test(tc, test_actions_perform_continue_after_successful_shell_command);
	tcase_add_test(tc, test_actions_perform_continue_after_failed_shell_command);
	tcase_add_test(tc, test_actions_perform_continue_unset_flag);
	suite_add_tcase(s, tc);

	return s;
}
