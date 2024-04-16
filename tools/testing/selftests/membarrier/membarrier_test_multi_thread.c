// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <linux/membarrier.h>
#include <syscall.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include "membarrier_test_impl.h"

static int thread_ready, thread_quit;
static pthread_mutex_t test_membarrier_thread_mutex =
	PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t test_membarrier_thread_cond =
	PTHREAD_COND_INITIALIZER;

void *test_membarrier_thread(void *arg)
{
	pthread_mutex_lock(&test_membarrier_thread_mutex);
	thread_ready = 1;
	pthread_cond_broadcast(&test_membarrier_thread_cond);
	pthread_mutex_unlock(&test_membarrier_thread_mutex);

	pthread_mutex_lock(&test_membarrier_thread_mutex);
	while (!thread_quit)
		pthread_cond_wait(&test_membarrier_thread_cond,
				  &test_membarrier_thread_mutex);
	pthread_mutex_unlock(&test_membarrier_thread_mutex);

	return NULL;
}

static int test_mt_membarrier(void)
{
	int i;
	pthread_t test_thread;

	pthread_create(&test_thread, NULL,
		       test_membarrier_thread, NULL);

	pthread_mutex_lock(&test_membarrier_thread_mutex);
	while (!thread_ready)
		pthread_cond_wait(&test_membarrier_thread_cond,
				  &test_membarrier_thread_mutex);
	pthread_mutex_unlock(&test_membarrier_thread_mutex);

	test_membarrier_fail();

	test_membarrier_success();

	pthread_mutex_lock(&test_membarrier_thread_mutex);
	thread_quit = 1;
	pthread_cond_broadcast(&test_membarrier_thread_cond);
	pthread_mutex_unlock(&test_membarrier_thread_mutex);

	pthread_join(test_thread, NULL);

	return 0;
}

int main(int argc, char **argv)
{
	ksft_print_header();
	ksft_set_plan(13);

	test_membarrier_query();

	/* Multi-threaded */
	test_mt_membarrier();

	return ksft_exit_pass();
}
