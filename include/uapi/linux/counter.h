/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace ABI for Counter character devices
 * Copyright (C) 2020 William Breathitt Gray
 */
#ifndef _UAPI_COUNTER_H_
#define _UAPI_COUNTER_H_

/* Component scope definitions */
enum counter_scope {
	COUNTER_SCOPE_DEVICE,
	COUNTER_SCOPE_SIGNAL,
	COUNTER_SCOPE_COUNT,
};

/* Count direction values */
enum counter_count_direction {
	COUNTER_COUNT_DIRECTION_FORWARD,
	COUNTER_COUNT_DIRECTION_BACKWARD,
};

/* Count mode values */
enum counter_count_mode {
	COUNTER_COUNT_MODE_NORMAL,
	COUNTER_COUNT_MODE_RANGE_LIMIT,
	COUNTER_COUNT_MODE_NON_RECYCLE,
	COUNTER_COUNT_MODE_MODULO_N,
};

/* Count function values */
enum counter_function {
	COUNTER_FUNCTION_INCREASE,
	COUNTER_FUNCTION_DECREASE,
	COUNTER_FUNCTION_PULSE_DIRECTION,
	COUNTER_FUNCTION_QUADRATURE_X1_A,
	COUNTER_FUNCTION_QUADRATURE_X1_B,
	COUNTER_FUNCTION_QUADRATURE_X2_A,
	COUNTER_FUNCTION_QUADRATURE_X2_B,
	COUNTER_FUNCTION_QUADRATURE_X4,
};

/* Signal values */
enum counter_signal_level {
	COUNTER_SIGNAL_LEVEL_LOW,
	COUNTER_SIGNAL_LEVEL_HIGH,
};

/* Action mode values */
enum counter_synapse_action {
	COUNTER_SYNAPSE_ACTION_NONE,
	COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

#endif /* _UAPI_COUNTER_H_ */
