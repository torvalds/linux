/*
 *  sync test runner
 *  Copyright 2015-2016 Collabora Ltd.
 *
 *  Based on the implementation from the Android Open Source Project,
 *
 *  Copyright 2012 Google, Inc
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *  OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#include "../kselftest.h"
#include "synctest.h"

static int run_test(int (*test)(void), char *name)
{
	int result;
	pid_t childpid;
	int ret;

	fflush(stdout);
	childpid = fork();

	if (childpid) {
		waitpid(childpid, &result, 0);
		if (WIFEXITED(result)) {
			ret = WEXITSTATUS(result);
			if (!ret)
				ksft_test_result_pass("[RUN]\t%s\n", name);
			else
				ksft_test_result_fail("[RUN]\t%s\n", name);
			return ret;
		}
		return 1;
	}

	exit(test());
}

static void sync_api_supported(void)
{
	struct stat sbuf;
	int ret;

	ret = stat("/sys/kernel/debug/sync/sw_sync", &sbuf);
	if (!ret)
		return;

	if (errno == ENOENT)
		ksft_exit_skip("Sync framework not supported by kernel\n");

	if (errno == EACCES)
		ksft_exit_skip("Run Sync test as root.\n");

	ksft_exit_fail_msg("stat failed on /sys/kernel/debug/sync/sw_sync: %s",
				strerror(errno));
}

int main(void)
{
	int err;

	ksft_print_header();
	ksft_set_plan(3 + 7);

	sync_api_supported();

	ksft_print_msg("[RUN]\tTesting sync framework\n");

	RUN_TEST(test_alloc_timeline);
	RUN_TEST(test_alloc_fence);
	RUN_TEST(test_alloc_fence_negative);

	RUN_TEST(test_fence_one_timeline_wait);
	RUN_TEST(test_fence_one_timeline_merge);
	RUN_TEST(test_fence_merge_same_fence);
	RUN_TEST(test_fence_multi_timeline_wait);
	RUN_TEST(test_stress_two_threads_shared_timeline);
	RUN_TEST(test_consumer_stress_multi_producer_single_consumer);
	RUN_TEST(test_merge_stress_random_merge);

	err = ksft_get_fail_cnt();
	if (err)
		ksft_exit_fail_msg("%d out of %d sync tests failed\n",
					err, ksft_test_num());

	/* need this return to keep gcc happy */
	return ksft_exit_pass();
}
