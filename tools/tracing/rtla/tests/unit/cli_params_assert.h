/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

#include "../../src/timerlat.h"

/* Tracing Options */

#define CLI_ASSERT_SINGLE_EVENT(_system, _event) do {\
	ck_assert_ptr_nonnull(params->events);\
	ck_assert_str_eq(params->events->system, _system);\
	ck_assert_str_eq(params->events->event, _event);\
	ck_assert_ptr_null(params->events->next);\
} while (0)

#define CLI_ASSERT_SINGLE_FILTER(_filter) do {\
	ck_assert_ptr_nonnull(params->events);\
	ck_assert_str_eq(params->events->filter, _filter);\
	ck_assert_ptr_null(params->events->next);\
} while (0)

#define CLI_ASSERT_SINGLE_TRIGGER(_trigger) do {\
	ck_assert_ptr_nonnull(params->events);\
	ck_assert_str_eq(params->events->trigger, _trigger);\
	ck_assert_ptr_null(params->events->next);\
} while (0)

/* CPU Configuration */

#define CLI_ASSERT_CPUSET(_field, ...) do {\
	int n;\
	int cpus[] = { __VA_ARGS__ };\
	for (n = 0; n < sizeof(cpus) / sizeof(int); n++)\
		ck_assert(CPU_ISSET(cpus[n], &params->_field));\
	ck_assert_int_eq(CPU_COUNT(&params->_field), n);\
} while (0)

/* Auto Analysis and Actions */

#define CLI_OSNOISE_ASSERT_AUTO(_stop) do {\
	ck_assert_int_eq(params->stop_us, _stop);\
	ck_assert_int_eq(osn_params->threshold, 1);\
	ck_assert_int_eq(params->threshold_actions.len, 1);\
	ck_assert_int_eq(params->threshold_actions.list[0].type, ACTION_TRACE_OUTPUT);\
	ck_assert_str_eq(params->threshold_actions.list[0].trace_output, "osnoise_trace.txt");\
} while (0)

#define CLI_TIMERLAT_ASSERT_AUTO(_threshold) do {\
	ck_assert_int_eq(params->stop_us, _threshold);\
	ck_assert_int_eq(params->stop_total_us, _threshold);\
	ck_assert_int_eq(tlat_params->print_stack, _threshold);\
	ck_assert_int_eq(params->threshold_actions.len, 1);\
	ck_assert_int_eq(params->threshold_actions.list[0].type, ACTION_TRACE_OUTPUT);\
	ck_assert_str_eq(params->threshold_actions.list[0].trace_output, "timerlat_trace.txt");\
} while (0)

#define CLI_TIMERLAT_ASSERT_AA_ONLY(_threshold) do {\
	ck_assert_int_eq(params->stop_us, _threshold);\
	ck_assert_int_eq(params->stop_total_us, _threshold);\
	ck_assert_int_eq(tlat_params->print_stack, _threshold);\
	ck_assert_int_eq(params->threshold_actions.len, 0);\
	ck_assert(params->aa_only);\
} while (0)

#define CLI_ASSERT_SINGLE_ACTION(_actions, _type, _arg, _valtype, _value) do {\
	ck_assert_int_eq(params->_actions.len, 1);\
	ck_assert_int_eq(params->_actions.list[0].type, _type);\
	ck_assert_##_valtype##_eq(params->_actions.list[0]._arg, _value);\
} while (0)
