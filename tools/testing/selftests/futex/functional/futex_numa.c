// SPDX-License-Identifier: GPL-2.0

#include <pthread.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "futextest.h"
#include "futex2test.h"

typedef u_int32_t u32;
typedef int32_t   s32;
typedef u_int64_t u64;

static unsigned int fflags = (FUTEX2_SIZE_U32 | FUTEX2_PRIVATE);
static int fnode = FUTEX_NO_NODE;

/* fairly stupid test-and-set lock with a waiter flag */

#define N_LOCK		0x0000001
#define N_WAITERS	0x0001000

struct futex_numa_32 {
	union {
		u64 full;
		struct {
			u32 val;
			u32 node;
		};
	};
};

void futex_numa_32_lock(struct futex_numa_32 *lock)
{
	for (;;) {
		struct futex_numa_32 new, old = {
			.full = __atomic_load_n(&lock->full, __ATOMIC_RELAXED),
		};

		for (;;) {
			new = old;
			if (old.val == 0) {
				/* no waiter, no lock -> first lock, set no-node */
				new.node = fnode;
			}
			if (old.val & N_LOCK) {
				/* contention, set waiter */
				new.val |= N_WAITERS;
			}
			new.val |= N_LOCK;

			/* nothing changed, ready to block */
			if (old.full == new.full)
				break;

			/*
			 * Use u64 cmpxchg to set the futex value and node in a
			 * consistent manner.
			 */
			if (__atomic_compare_exchange_n(&lock->full,
							&old.full, new.full,
							/* .weak */ false,
							__ATOMIC_ACQUIRE,
							__ATOMIC_RELAXED)) {

				/* if we just set N_LOCK, we own it */
				if (!(old.val & N_LOCK))
					return;

				/* go block */
				break;
			}
		}

		futex2_wait(lock, new.val, fflags, NULL, 0);
	}
}

void futex_numa_32_unlock(struct futex_numa_32 *lock)
{
	u32 val = __atomic_sub_fetch(&lock->val, N_LOCK, __ATOMIC_RELEASE);
	assert((s32)val >= 0);
	if (val & N_WAITERS) {
		int woken = futex2_wake(lock, 1, fflags);
		assert(val == N_WAITERS);
		if (!woken) {
			__atomic_compare_exchange_n(&lock->val, &val, 0U,
						    false, __ATOMIC_RELAXED,
						    __ATOMIC_RELAXED);
		}
	}
}

static long nanos = 50000;

struct thread_args {
	pthread_t tid;
	volatile int * done;
	struct futex_numa_32 *lock;
	int val;
	int *val1, *val2;
	int node;
};

static void *threadfn(void *_arg)
{
	struct thread_args *args = _arg;
	struct timespec ts = {
		.tv_nsec = nanos,
	};
	int node;

	while (!*args->done) {

		futex_numa_32_lock(args->lock);
		args->val++;

		assert(*args->val1 == *args->val2);
		(*args->val1)++;
		nanosleep(&ts, NULL);
		(*args->val2)++;

		node = args->lock->node;
		futex_numa_32_unlock(args->lock);

		if (node != args->node) {
			args->node = node;
			printf("node: %d\n", node);
		}

		nanosleep(&ts, NULL);
	}

	return NULL;
}

static void *contendfn(void *_arg)
{
	struct thread_args *args = _arg;

	while (!*args->done) {
		/*
		 * futex2_wait() will take hb-lock, verify *var == val and
		 * queue/abort.  By knowingly setting val 'wrong' this will
		 * abort and thereby generate hb-lock contention.
		 */
		futex2_wait(&args->lock->val, ~0U, fflags, NULL, 0);
		args->val++;
	}

	return NULL;
}

static volatile int done = 0;
static struct futex_numa_32 lock = { .val = 0, };
static int val1, val2;

int main(int argc, char *argv[])
{
	struct thread_args *tas[512], *cas[512];
	int c, t, threads = 2, contenders = 0;
	int sleeps = 10;
	int total = 0;

	while ((c = getopt(argc, argv, "c:t:s:n:N::")) != -1) {
		switch (c) {
		case 'c':
			contenders = atoi(optarg);
			break;
		case 't':
			threads = atoi(optarg);
			break;
		case 's':
			sleeps = atoi(optarg);
			break;
		case 'n':
			nanos = atoi(optarg);
			break;
		case 'N':
			fflags |= FUTEX2_NUMA;
			if (optarg)
				fnode = atoi(optarg);
			break;
		default:
			exit(1);
			break;
		}
	}

	for (t = 0; t < contenders; t++) {
		struct thread_args *args = calloc(1, sizeof(*args));
		if (!args) {
			perror("thread_args");
			exit(-1);
		}

		args->done = &done;
		args->lock = &lock;
		args->val1 = &val1;
		args->val2 = &val2;
		args->node = -1;

		if (pthread_create(&args->tid, NULL, contendfn, args)) {
			perror("pthread_create");
			exit(-1);
		}

		cas[t] = args;
	}

	for (t = 0; t < threads; t++) {
		struct thread_args *args = calloc(1, sizeof(*args));
		if (!args) {
			perror("thread_args");
			exit(-1);
		}

		args->done = &done;
		args->lock = &lock;
		args->val1 = &val1;
		args->val2 = &val2;
		args->node = -1;

		if (pthread_create(&args->tid, NULL, threadfn, args)) {
			perror("pthread_create");
			exit(-1);
		}

		tas[t] = args;
	}

	sleep(sleeps);

	done = true;

	for (t = 0; t < threads; t++) {
		struct thread_args *args = tas[t];

		pthread_join(args->tid, NULL);
		total += args->val;
//		printf("tval: %d\n", args->val);
	}
	printf("total: %d\n", total);

	if (contenders) {
		total = 0;
		for (t = 0; t < contenders; t++) {
			struct thread_args *args = cas[t];

			pthread_join(args->tid, NULL);
			total += args->val;
			//		printf("tval: %d\n", args->val);
		}
		printf("contenders: %d\n", total);
	}

	return 0;
}

