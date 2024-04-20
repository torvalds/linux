// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Benjamin Tissoires */
#include <test_progs.h>
#include "wq.skel.h"
#include "wq_failures.skel.h"

void serial_test_wq(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	RUN_TESTS(wq);
}

void serial_test_failures_wq(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, topts);

	RUN_TESTS(wq_failures);
}
