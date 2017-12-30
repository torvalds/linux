/*
 *  sync tests
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

#ifndef SELFTESTS_SYNCTEST_H
#define SELFTESTS_SYNCTEST_H

#include <stdio.h>
#include "../kselftest.h"

#define ASSERT(cond, msg) do { \
	if (!(cond)) { \
		ksft_print_msg("[ERROR]\t%s", (msg)); \
		return 1; \
	} \
} while (0)

#define RUN_TEST(x) run_test((x), #x)

/* Allocation tests */
int test_alloc_timeline(void);
int test_alloc_fence(void);
int test_alloc_fence_negative(void);

/* Fence tests with one timeline */
int test_fence_one_timeline_wait(void);
int test_fence_one_timeline_merge(void);

/* Fence merge tests */
int test_fence_merge_same_fence(void);

/* Fence wait tests */
int test_fence_multi_timeline_wait(void);

/* Stress test - parallelism */
int test_stress_two_threads_shared_timeline(void);

/* Stress test - consumer */
int test_consumer_stress_multi_producer_single_consumer(void);

/* Stress test - merging */
int test_merge_stress_random_merge(void);

#endif
