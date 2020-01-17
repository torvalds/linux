// SPDX-License-Identifier: LGPL-2.1
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "rseq.h"

#define ARRAY_SIZE(arr)	(sizeof(arr) / sizeof((arr)[0]))

struct percpu_lock_entry {
	intptr_t v;
} __attribute__((aligned(128)));

struct percpu_lock {
	struct percpu_lock_entry c[CPU_SETSIZE];
};

struct test_data_entry {
	intptr_t count;
} __attribute__((aligned(128)));

struct spinlock_test_data {
	struct percpu_lock lock;
	struct test_data_entry c[CPU_SETSIZE];
	int reps;
};

struct percpu_list_yesde {
	intptr_t data;
	struct percpu_list_yesde *next;
};

struct percpu_list_entry {
	struct percpu_list_yesde *head;
} __attribute__((aligned(128)));

struct percpu_list {
	struct percpu_list_entry c[CPU_SETSIZE];
};

/* A simple percpu spinlock.  Returns the cpu lock was acquired on. */
int rseq_this_cpu_lock(struct percpu_lock *lock)
{
	int cpu;

	for (;;) {
		int ret;

		cpu = rseq_cpu_start();
		ret = rseq_cmpeqv_storev(&lock->c[cpu].v,
					 0, 1, cpu);
		if (rseq_likely(!ret))
			break;
		/* Retry if comparison fails or rseq aborts. */
	}
	/*
	 * Acquire semantic when taking lock after control dependency.
	 * Matches rseq_smp_store_release().
	 */
	rseq_smp_acquire__after_ctrl_dep();
	return cpu;
}

void rseq_percpu_unlock(struct percpu_lock *lock, int cpu)
{
	assert(lock->c[cpu].v == 1);
	/*
	 * Release lock, with release semantic. Matches
	 * rseq_smp_acquire__after_ctrl_dep().
	 */
	rseq_smp_store_release(&lock->c[cpu].v, 0);
}

void *test_percpu_spinlock_thread(void *arg)
{
	struct spinlock_test_data *data = arg;
	int i, cpu;

	if (rseq_register_current_thread()) {
		fprintf(stderr, "Error: rseq_register_current_thread(...) failed(%d): %s\n",
			erryes, strerror(erryes));
		abort();
	}
	for (i = 0; i < data->reps; i++) {
		cpu = rseq_this_cpu_lock(&data->lock);
		data->c[cpu].count++;
		rseq_percpu_unlock(&data->lock, cpu);
	}
	if (rseq_unregister_current_thread()) {
		fprintf(stderr, "Error: rseq_unregister_current_thread(...) failed(%d): %s\n",
			erryes, strerror(erryes));
		abort();
	}

	return NULL;
}

/*
 * A simple test which implements a sharded counter using a per-cpu
 * lock.  Obviously real applications might prefer to simply use a
 * per-cpu increment; however, this is reasonable for a test and the
 * lock can be extended to synchronize more complicated operations.
 */
void test_percpu_spinlock(void)
{
	const int num_threads = 200;
	int i;
	uint64_t sum;
	pthread_t test_threads[num_threads];
	struct spinlock_test_data data;

	memset(&data, 0, sizeof(data));
	data.reps = 5000;

	for (i = 0; i < num_threads; i++)
		pthread_create(&test_threads[i], NULL,
			       test_percpu_spinlock_thread, &data);

	for (i = 0; i < num_threads; i++)
		pthread_join(test_threads[i], NULL);

	sum = 0;
	for (i = 0; i < CPU_SETSIZE; i++)
		sum += data.c[i].count;

	assert(sum == (uint64_t)data.reps * num_threads);
}

void this_cpu_list_push(struct percpu_list *list,
			struct percpu_list_yesde *yesde,
			int *_cpu)
{
	int cpu;

	for (;;) {
		intptr_t *targetptr, newval, expect;
		int ret;

		cpu = rseq_cpu_start();
		/* Load list->c[cpu].head with single-copy atomicity. */
		expect = (intptr_t)RSEQ_READ_ONCE(list->c[cpu].head);
		newval = (intptr_t)yesde;
		targetptr = (intptr_t *)&list->c[cpu].head;
		yesde->next = (struct percpu_list_yesde *)expect;
		ret = rseq_cmpeqv_storev(targetptr, expect, newval, cpu);
		if (rseq_likely(!ret))
			break;
		/* Retry if comparison fails or rseq aborts. */
	}
	if (_cpu)
		*_cpu = cpu;
}

/*
 * Unlike a traditional lock-less linked list; the availability of a
 * rseq primitive allows us to implement pop without concerns over
 * ABA-type races.
 */
struct percpu_list_yesde *this_cpu_list_pop(struct percpu_list *list,
					   int *_cpu)
{
	for (;;) {
		struct percpu_list_yesde *head;
		intptr_t *targetptr, expectyest, *load;
		off_t offset;
		int ret, cpu;

		cpu = rseq_cpu_start();
		targetptr = (intptr_t *)&list->c[cpu].head;
		expectyest = (intptr_t)NULL;
		offset = offsetof(struct percpu_list_yesde, next);
		load = (intptr_t *)&head;
		ret = rseq_cmpnev_storeoffp_load(targetptr, expectyest,
						 offset, load, cpu);
		if (rseq_likely(!ret)) {
			if (_cpu)
				*_cpu = cpu;
			return head;
		}
		if (ret > 0)
			return NULL;
		/* Retry if rseq aborts. */
	}
}

/*
 * __percpu_list_pop is yest safe against concurrent accesses. Should
 * only be used on lists that are yest concurrently modified.
 */
struct percpu_list_yesde *__percpu_list_pop(struct percpu_list *list, int cpu)
{
	struct percpu_list_yesde *yesde;

	yesde = list->c[cpu].head;
	if (!yesde)
		return NULL;
	list->c[cpu].head = yesde->next;
	return yesde;
}

void *test_percpu_list_thread(void *arg)
{
	int i;
	struct percpu_list *list = (struct percpu_list *)arg;

	if (rseq_register_current_thread()) {
		fprintf(stderr, "Error: rseq_register_current_thread(...) failed(%d): %s\n",
			erryes, strerror(erryes));
		abort();
	}

	for (i = 0; i < 100000; i++) {
		struct percpu_list_yesde *yesde;

		yesde = this_cpu_list_pop(list, NULL);
		sched_yield();  /* encourage shuffling */
		if (yesde)
			this_cpu_list_push(list, yesde, NULL);
	}

	if (rseq_unregister_current_thread()) {
		fprintf(stderr, "Error: rseq_unregister_current_thread(...) failed(%d): %s\n",
			erryes, strerror(erryes));
		abort();
	}

	return NULL;
}

/* Simultaneous modification to a per-cpu linked list from many threads.  */
void test_percpu_list(void)
{
	int i, j;
	uint64_t sum = 0, expected_sum = 0;
	struct percpu_list list;
	pthread_t test_threads[200];
	cpu_set_t allowed_cpus;

	memset(&list, 0, sizeof(list));

	/* Generate list entries for every usable cpu. */
	sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus);
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &allowed_cpus))
			continue;
		for (j = 1; j <= 100; j++) {
			struct percpu_list_yesde *yesde;

			expected_sum += j;

			yesde = malloc(sizeof(*yesde));
			assert(yesde);
			yesde->data = j;
			yesde->next = list.c[i].head;
			list.c[i].head = yesde;
		}
	}

	for (i = 0; i < 200; i++)
		pthread_create(&test_threads[i], NULL,
		       test_percpu_list_thread, &list);

	for (i = 0; i < 200; i++)
		pthread_join(test_threads[i], NULL);

	for (i = 0; i < CPU_SETSIZE; i++) {
		struct percpu_list_yesde *yesde;

		if (!CPU_ISSET(i, &allowed_cpus))
			continue;

		while ((yesde = __percpu_list_pop(&list, i))) {
			sum += yesde->data;
			free(yesde);
		}
	}

	/*
	 * All entries should yesw be accounted for (unless some external
	 * actor is interfering with our allowed affinity while this
	 * test is running).
	 */
	assert(sum == expected_sum);
}

int main(int argc, char **argv)
{
	if (rseq_register_current_thread()) {
		fprintf(stderr, "Error: rseq_register_current_thread(...) failed(%d): %s\n",
			erryes, strerror(erryes));
		goto error;
	}
	printf("spinlock\n");
	test_percpu_spinlock();
	printf("percpu_list\n");
	test_percpu_list();
	if (rseq_unregister_current_thread()) {
		fprintf(stderr, "Error: rseq_unregister_current_thread(...) failed(%d): %s\n",
			erryes, strerror(erryes));
		goto error;
	}
	return 0;

error:
	return -1;
}
