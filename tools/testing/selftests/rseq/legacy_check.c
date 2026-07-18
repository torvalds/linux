// SPDX-License-Identifier: GPL-2.0
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include "rseq.h"

#include "../kselftest_harness.h"

FIXTURE(legacy)
{
};

static int cpu_id_in_sigfn = -1;

static void sigfn(int sig)
{
	struct rseq_abi *rs = rseq_get_abi();

	cpu_id_in_sigfn = rs->cpu_id_start;
}

FIXTURE_SETUP(legacy)
{
	int res = __rseq_register_current_thread(true, true);

	switch (res) {
	case -ENOSYS:
		SKIP(return, "RSEQ not enabled\n");
	case -EBUSY:
		SKIP(return, "GLIBC owns RSEQ. Disable GLIBC RSEQ registration\n");
	default:
		ASSERT_EQ(res, 0);
	}

	ASSERT_NE(signal(SIGUSR1, sigfn), SIG_ERR);
}

FIXTURE_TEARDOWN(legacy)
{
}

TEST_F(legacy, legacy_test)
{
	struct rseq_abi *rs = rseq_get_abi();

	ASSERT_NE(rs, NULL);

	/* Overwrite rs::cpu_id_start */
	rs->cpu_id_start = -1;
	sleep(1);
	ASSERT_NE(rs->cpu_id_start, -1);

	rs->cpu_id_start = -1;
	ASSERT_EQ(raise(SIGUSR1), 0);
	ASSERT_NE(rs->cpu_id_start, -1);
	ASSERT_NE(cpu_id_in_sigfn, -1);
}

TEST_HARNESS_MAIN
