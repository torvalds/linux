// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

struct test {
	int a;
	int b;
} __attribute__((preserve_access_index));

volatile struct test global_value_for_test = {};
