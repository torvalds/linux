/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#ifndef __EXIT_TEST_H__
#define __EXIT_TEST_H__

enum exit_test_case {
	EXIT_SELECT_CPU,
	EXIT_ENQUEUE,
	EXIT_DISPATCH,
	EXIT_ENABLE,
	EXIT_INIT_TASK,
	EXIT_INIT,
	NUM_EXITS,
};

#endif  // # __EXIT_TEST_H__
