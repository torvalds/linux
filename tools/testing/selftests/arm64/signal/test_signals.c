// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Generic test wrapper for arm64 signal tests.
 *
 * Each test provides its own tde struct tdescr descriptor to link with
 * this wrapper. Framework provides common helpers.
 */
#include <kselftest.h>

#include "test_signals.h"
#include "test_signals_utils.h"

struct tdescr *current = &tde;

int main(int argc, char *argv[])
{
	ksft_print_msg("%s :: %s\n", current->name, current->descr);
	if (test_setup(current) && test_init(current)) {
		test_run(current);
		test_cleanup(current);
	}
	test_result(current);

	return current->result;
}
