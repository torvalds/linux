// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "test_send_signal_kern.skel.h"

static void sigusr1_handler(int signum)
{
}

#define THREAD_COUNT 100

static void *worker(void *p)
{
	int i;

	for ( i = 0; i < 1000; i++)
		usleep(1);

	return NULL;
}

/* NOTE: cause events loss */
void serial_test_send_signal_sched_switch(void)
{
	struct test_send_signal_kern *skel;
	pthread_t threads[THREAD_COUNT];
	u32 duration = 0;
	int i, err;

	signal(SIGUSR1, sigusr1_handler);

	skel = test_send_signal_kern__open_and_load();
	if (CHECK(!skel, "skel_open_and_load", "skeleton open_and_load failed\n"))
		return;

	skel->bss->pid = getpid();
	skel->bss->sig = SIGUSR1;

	err = test_send_signal_kern__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed\n"))
		goto destroy_skel;

	for (i = 0; i < THREAD_COUNT; i++) {
		err = pthread_create(threads + i, NULL, worker, NULL);
		if (CHECK(err, "pthread_create", "Error creating thread, %s\n",
			  strerror(errno)))
			goto destroy_skel;
	}

	for (i = 0; i < THREAD_COUNT; i++)
		pthread_join(threads[i], NULL);

destroy_skel:
	test_send_signal_kern__destroy(skel);
}
