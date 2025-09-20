/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#ifndef __HOTPLUG_TEST_H__
#define __HOTPLUG_TEST_H__

enum hotplug_test_flags {
	HOTPLUG_EXIT_RSN = 1LLU << 0,
	HOTPLUG_ONLINING = 1LLU << 1,
};

#endif  // # __HOTPLUG_TEST_H__
