// SPDX-License-Identifier: GPL-2.0
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "../kselftest.h"

static void test_limit(void)
{
	struct rlimit lims;
	void *map;

	if (getrlimit(RLIMIT_MEMLOCK, &lims))
		ksft_exit_fail_msg("getrlimit: %s\n", strerror(errno));

	if (mlockall(MCL_ONFAULT | MCL_FUTURE))
		ksft_exit_fail_msg("mlockall: %s\n", strerror(errno));

	map = mmap(NULL, 2 * lims.rlim_max, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);

	ksft_test_result(map == MAP_FAILED, "The map failed respecting mlock limits\n");

	if (map != MAP_FAILED)
		munmap(map, 2 * lims.rlim_max);
	munlockall();
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(1);

	if (!getuid())
		ksft_test_result_skip("The test must be run from a normal user\n");
	else
		test_limit();

	ksft_finished();
}
