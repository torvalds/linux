// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <linux/membarrier.h>
#include <syscall.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include "membarrier_test_impl.h"

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(18);

	test_membarrier_get_registrations(/*cmd=*/0);

	test_membarrier_query();

	test_membarrier_fail();

	test_membarrier_success();

	test_membarrier_get_registrations(/*cmd=*/0);

	return ksft_exit_pass();
}
