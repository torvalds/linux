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
#include <sys/wait.h>

#include "synctest.h"

static int run_test(int (*test)(void), char *name)
{
	int result;
	pid_t childpid;

	fflush(stdout);
	childpid = fork();

	if (childpid) {
		waitpid(childpid, &result, 0);
		if (WIFEXITED(result))
			return WEXITSTATUS(result);
		return 1;
	}

	printf("[RUN]\tExecuting %s\n", name);
	exit(test());
}

int main(void)
{
	int err = 0;

	printf("[RUN]\tTesting sync framework\n");

	err += RUN_TEST(test_alloc_timeline);
	err += RUN_TEST(test_alloc_fence);
	err += RUN_TEST(test_alloc_fence_negative);

	err += RUN_TEST(test_fence_one_timeline_wait);
	err += RUN_TEST(test_fence_one_timeline_merge);
	err += RUN_TEST(test_fence_merge_same_fence);
	err += RUN_TEST(test_fence_multi_timeline_wait);
	err += RUN_TEST(test_stress_two_threads_shared_timeline);
	err += RUN_TEST(test_consumer_stress_multi_producer_single_consumer);
	err += RUN_TEST(test_merge_stress_random_merge);

	if (err)
		printf("[FAIL]\tsync errors: %d\n", err);
	else
		printf("[OK]\tsync\n");

	return !!err;
}
