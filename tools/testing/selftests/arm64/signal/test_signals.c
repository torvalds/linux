// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 ARM Limited
 *
 * Generic test wrapper for arm64 signal tests.
 *
 * Each test provides its own tde struct tdescr descriptor to link with
 * this wrapper. Framework provides common helpers.
 */

#include <sys/auxv.h>
#include <sys/prctl.h>

#include <kselftest.h>

#include "test_signals.h"
#include "test_signals_utils.h"

struct tdescr *current = &tde;

int main(int argc, char *argv[])
{
	/*
	 * Ensure GCS is at least enabled throughout the tests if
	 * supported, otherwise the inability to return from the
	 * function that enabled GCS makes it very inconvenient to set
	 * up test cases.  The prctl() may fail if GCS was locked by
	 * libc setup code.
	 */
	if (getauxval(AT_HWCAP) & HWCAP_GCS)
		gcs_set_state(PR_SHADOW_STACK_ENABLE);

	ksft_print_msg("%s :: %s\n", current->name, current->descr);
	if (test_setup(current) && test_init(current)) {
		test_run(current);
		test_cleanup(current);
	}
	test_result(current);

	/* Do not return in case GCS was enabled */
	exit(current->result);
}
