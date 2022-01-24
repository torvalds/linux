// SPDX-License-Identifier: LGPL-2.1
#define _GNU_SOURCE
#include <assert.h>
#include <linux/membarrier.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <stddef.h>

static inline pid_t rseq_gettid(void)
{
	return syscall(__NR_gettid);
}

#define NR_INJECT	9
static int loop_cnt[NR_INJECT + 1];

static int loop_cnt_1 asm("asm_loop_cnt_1") __attribute__((used));
static int loop_cnt_2 asm("asm_loop_cnt_2") __attribute__((used));
static int loop_cnt_3 asm("asm_loop_cnt_3") __attribute__((used));
static int loop_cnt_4 asm("asm_loop_cnt_4") __attribute__((used));
static int loop_cnt_5 asm("asm_loop_cnt_5") __attribute__((used));
static int loop_cnt_6 asm("asm_loop_cnt_6") __attribute__((used));

static int opt_modulo, verbose;

static int opt_yield, opt_signal, opt_sleep,
		opt_disable_rseq, opt_threads = 200,
		opt_disable_mod = 0, opt_test = 's', opt_mb = 0;

#ifndef RSEQ_SKIP_FASTPATH
static long long opt_reps = 5000;
#else
static long long opt_reps = 100;
#endif

static __thread __attribute__((tls_model("initial-exec")))
unsigned int signals_delivered;

#ifndef BENCHMARK

static __thread __attribute__((tls_model("initial-exec"), unused))
unsigned int yield_mod_cnt, nr_abort;

#define printf_verbose(fmt, ...)			\
	do {						\
		if (verbose)				\
			printf(fmt, ## __VA_ARGS__);	\
	} while (0)

#ifdef __i386__

#define INJECT_ASM_REG	"eax"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"mov asm_loop_cnt_" #n ", %%" INJECT_ASM_REG "\n\t" \
	"test %%" INJECT_ASM_REG ",%%" INJECT_ASM_REG "\n\t" \
	"jz 333f\n\t" \
	"222:\n\t" \
	"dec %%" INJECT_ASM_REG "\n\t" \
	"jnz 222b\n\t" \
	"333:\n\t"

#elif defined(__x86_64__)

#define INJECT_ASM_REG_P	"rax"
#define INJECT_ASM_REG		"eax"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG_P \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"lea asm_loop_cnt_" #n "(%%rip), %%" INJECT_ASM_REG_P "\n\t" \
	"mov (%%" INJECT_ASM_REG_P "), %%" INJECT_ASM_REG "\n\t" \
	"test %%" INJECT_ASM_REG ",%%" INJECT_ASM_REG "\n\t" \
	"jz 333f\n\t" \
	"222:\n\t" \
	"dec %%" INJECT_ASM_REG "\n\t" \
	"jnz 222b\n\t" \
	"333:\n\t"

#elif defined(__s390__)

#define RSEQ_INJECT_INPUT \
	, [loop_cnt_1]"m"(loop_cnt[1]) \
	, [loop_cnt_2]"m"(loop_cnt[2]) \
	, [loop_cnt_3]"m"(loop_cnt[3]) \
	, [loop_cnt_4]"m"(loop_cnt[4]) \
	, [loop_cnt_5]"m"(loop_cnt[5]) \
	, [loop_cnt_6]"m"(loop_cnt[6])

#define INJECT_ASM_REG	"r12"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"l %%" INJECT_ASM_REG ", %[loop_cnt_" #n "]\n\t" \
	"ltr %%" INJECT_ASM_REG ", %%" INJECT_ASM_REG "\n\t" \
	"je 333f\n\t" \
	"222:\n\t" \
	"ahi %%" INJECT_ASM_REG ", -1\n\t" \
	"jnz 222b\n\t" \
	"333:\n\t"

#elif defined(__ARMEL__)

#define RSEQ_INJECT_INPUT \
	, [loop_cnt_1]"m"(loop_cnt[1]) \
	, [loop_cnt_2]"m"(loop_cnt[2]) \
	, [loop_cnt_3]"m"(loop_cnt[3]) \
	, [loop_cnt_4]"m"(loop_cnt[4]) \
	, [loop_cnt_5]"m"(loop_cnt[5]) \
	, [loop_cnt_6]"m"(loop_cnt[6])

#define INJECT_ASM_REG	"r4"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"ldr " INJECT_ASM_REG ", %[loop_cnt_" #n "]\n\t" \
	"cmp " INJECT_ASM_REG ", #0\n\t" \
	"beq 333f\n\t" \
	"222:\n\t" \
	"subs " INJECT_ASM_REG ", #1\n\t" \
	"bne 222b\n\t" \
	"333:\n\t"

#elif defined(__AARCH64EL__)

#define RSEQ_INJECT_INPUT \
	, [loop_cnt_1] "Qo" (loop_cnt[1]) \
	, [loop_cnt_2] "Qo" (loop_cnt[2]) \
	, [loop_cnt_3] "Qo" (loop_cnt[3]) \
	, [loop_cnt_4] "Qo" (loop_cnt[4]) \
	, [loop_cnt_5] "Qo" (loop_cnt[5]) \
	, [loop_cnt_6] "Qo" (loop_cnt[6])

#define INJECT_ASM_REG	RSEQ_ASM_TMP_REG32

#define RSEQ_INJECT_ASM(n) \
	"	ldr	" INJECT_ASM_REG ", %[loop_cnt_" #n "]\n"	\
	"	cbz	" INJECT_ASM_REG ", 333f\n"			\
	"222:\n"							\
	"	sub	" INJECT_ASM_REG ", " INJECT_ASM_REG ", #1\n"	\
	"	cbnz	" INJECT_ASM_REG ", 222b\n"			\
	"333:\n"

#elif defined(__PPC__)

#define RSEQ_INJECT_INPUT \
	, [loop_cnt_1]"m"(loop_cnt[1]) \
	, [loop_cnt_2]"m"(loop_cnt[2]) \
	, [loop_cnt_3]"m"(loop_cnt[3]) \
	, [loop_cnt_4]"m"(loop_cnt[4]) \
	, [loop_cnt_5]"m"(loop_cnt[5]) \
	, [loop_cnt_6]"m"(loop_cnt[6])

#define INJECT_ASM_REG	"r18"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"lwz %%" INJECT_ASM_REG ", %[loop_cnt_" #n "]\n\t" \
	"cmpwi %%" INJECT_ASM_REG ", 0\n\t" \
	"beq 333f\n\t" \
	"222:\n\t" \
	"subic. %%" INJECT_ASM_REG ", %%" INJECT_ASM_REG ", 1\n\t" \
	"bne 222b\n\t" \
	"333:\n\t"

#elif defined(__mips__)

#define RSEQ_INJECT_INPUT \
	, [loop_cnt_1]"m"(loop_cnt[1]) \
	, [loop_cnt_2]"m"(loop_cnt[2]) \
	, [loop_cnt_3]"m"(loop_cnt[3]) \
	, [loop_cnt_4]"m"(loop_cnt[4]) \
	, [loop_cnt_5]"m"(loop_cnt[5]) \
	, [loop_cnt_6]"m"(loop_cnt[6])

#define INJECT_ASM_REG	"$5"

#define RSEQ_INJECT_CLOBBER \
	, INJECT_ASM_REG

#define RSEQ_INJECT_ASM(n) \
	"lw " INJECT_ASM_REG ", %[loop_cnt_" #n "]\n\t" \
	"beqz " INJECT_ASM_REG ", 333f\n\t" \
	"222:\n\t" \
	"addiu " INJECT_ASM_REG ", -1\n\t" \
	"bnez " INJECT_ASM_REG ", 222b\n\t" \
	"333:\n\t"

#else
#error unsupported target
#endif

#define RSEQ_INJECT_FAILED \
	nr_abort++;

#define RSEQ_INJECT_C(n) \
{ \
	int loc_i, loc_nr_loops = loop_cnt[n]; \
	\
	for (loc_i = 0; loc_i < loc_nr_loops; loc_i++) { \
		rseq_barrier(); \
	} \
	if (loc_nr_loops == -1 && opt_modulo) { \
		if (yield_mod_cnt == opt_modulo - 1) { \
			if (opt_sleep > 0) \
				poll(NULL, 0, opt_sleep); \
			if (opt_yield) \
				sched_yield(); \
			if (opt_signal) \
				raise(SIGUSR1); \
			yield_mod_cnt = 0; \
		} else { \
			yield_mod_cnt++; \
		} \
	} \
}

#else

#define printf_verbose(fmt, ...)

#endif /* BENCHMARK */

#include "rseq.h"

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
};

struct spinlock_thread_test_data {
	struct spinlock_test_data *data;
	long long reps;
	int reg;
};

struct inc_test_data {
	struct test_data_entry c[CPU_SETSIZE];
};

struct inc_thread_test_data {
	struct inc_test_data *data;
	long long reps;
	int reg;
};

struct percpu_list_node {
	intptr_t data;
	struct percpu_list_node *next;
};

struct percpu_list_entry {
	struct percpu_list_node *head;
} __attribute__((aligned(128)));

struct percpu_list {
	struct percpu_list_entry c[CPU_SETSIZE];
};

#define BUFFER_ITEM_PER_CPU	100

struct percpu_buffer_node {
	intptr_t data;
};

struct percpu_buffer_entry {
	intptr_t offset;
	intptr_t buflen;
	struct percpu_buffer_node **array;
} __attribute__((aligned(128)));

struct percpu_buffer {
	struct percpu_buffer_entry c[CPU_SETSIZE];
};

#define MEMCPY_BUFFER_ITEM_PER_CPU	100

struct percpu_memcpy_buffer_node {
	intptr_t data1;
	uint64_t data2;
};

struct percpu_memcpy_buffer_entry {
	intptr_t offset;
	intptr_t buflen;
	struct percpu_memcpy_buffer_node *array;
} __attribute__((aligned(128)));

struct percpu_memcpy_buffer {
	struct percpu_memcpy_buffer_entry c[CPU_SETSIZE];
};

/* A simple percpu spinlock. Grabs lock on current cpu. */
static int rseq_this_cpu_lock(struct percpu_lock *lock)
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

static void rseq_percpu_unlock(struct percpu_lock *lock, int cpu)
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
	struct spinlock_thread_test_data *thread_data = arg;
	struct spinlock_test_data *data = thread_data->data;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg &&
	    rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		int cpu = rseq_this_cpu_lock(&data->lock);
		data->c[cpu].count++;
		rseq_percpu_unlock(&data->lock, cpu);
#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf_verbose("tid %d: count %lld\n",
				       (int) rseq_gettid(), i);
#endif
	}
	printf_verbose("tid %d: number of rseq abort: %d, signals delivered: %u\n",
		       (int) rseq_gettid(), nr_abort, signals_delivered);
	if (!opt_disable_rseq && thread_data->reg &&
	    rseq_unregister_current_thread())
		abort();
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
	const int num_threads = opt_threads;
	int i, ret;
	uint64_t sum;
	pthread_t test_threads[num_threads];
	struct spinlock_test_data data;
	struct spinlock_thread_test_data thread_data[num_threads];

	memset(&data, 0, sizeof(data));
	for (i = 0; i < num_threads; i++) {
		thread_data[i].reps = opt_reps;
		if (opt_disable_mod <= 0 || (i % opt_disable_mod))
			thread_data[i].reg = 1;
		else
			thread_data[i].reg = 0;
		thread_data[i].data = &data;
		ret = pthread_create(&test_threads[i], NULL,
				     test_percpu_spinlock_thread,
				     &thread_data[i]);
		if (ret) {
			errno = ret;
			perror("pthread_create");
			abort();
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_join(test_threads[i], NULL);
		if (ret) {
			errno = ret;
			perror("pthread_join");
			abort();
		}
	}

	sum = 0;
	for (i = 0; i < CPU_SETSIZE; i++)
		sum += data.c[i].count;

	assert(sum == (uint64_t)opt_reps * num_threads);
}

void *test_percpu_inc_thread(void *arg)
{
	struct inc_thread_test_data *thread_data = arg;
	struct inc_test_data *data = thread_data->data;
	long long i, reps;

	if (!opt_disable_rseq && thread_data->reg &&
	    rseq_register_current_thread())
		abort();
	reps = thread_data->reps;
	for (i = 0; i < reps; i++) {
		int ret;

		do {
			int cpu;

			cpu = rseq_cpu_start();
			ret = rseq_addv(&data->c[cpu].count, 1, cpu);
		} while (rseq_unlikely(ret));
#ifndef BENCHMARK
		if (i != 0 && !(i % (reps / 10)))
			printf_verbose("tid %d: count %lld\n",
				       (int) rseq_gettid(), i);
#endif
	}
	printf_verbose("tid %d: number of rseq abort: %d, signals delivered: %u\n",
		       (int) rseq_gettid(), nr_abort, signals_delivered);
	if (!opt_disable_rseq && thread_data->reg &&
	    rseq_unregister_current_thread())
		abort();
	return NULL;
}

void test_percpu_inc(void)
{
	const int num_threads = opt_threads;
	int i, ret;
	uint64_t sum;
	pthread_t test_threads[num_threads];
	struct inc_test_data data;
	struct inc_thread_test_data thread_data[num_threads];

	memset(&data, 0, sizeof(data));
	for (i = 0; i < num_threads; i++) {
		thread_data[i].reps = opt_reps;
		if (opt_disable_mod <= 0 || (i % opt_disable_mod))
			thread_data[i].reg = 1;
		else
			thread_data[i].reg = 0;
		thread_data[i].data = &data;
		ret = pthread_create(&test_threads[i], NULL,
				     test_percpu_inc_thread,
				     &thread_data[i]);
		if (ret) {
			errno = ret;
			perror("pthread_create");
			abort();
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_join(test_threads[i], NULL);
		if (ret) {
			errno = ret;
			perror("pthread_join");
			abort();
		}
	}

	sum = 0;
	for (i = 0; i < CPU_SETSIZE; i++)
		sum += data.c[i].count;

	assert(sum == (uint64_t)opt_reps * num_threads);
}

void this_cpu_list_push(struct percpu_list *list,
			struct percpu_list_node *node,
			int *_cpu)
{
	int cpu;

	for (;;) {
		intptr_t *targetptr, newval, expect;
		int ret;

		cpu = rseq_cpu_start();
		/* Load list->c[cpu].head with single-copy atomicity. */
		expect = (intptr_t)RSEQ_READ_ONCE(list->c[cpu].head);
		newval = (intptr_t)node;
		targetptr = (intptr_t *)&list->c[cpu].head;
		node->next = (struct percpu_list_node *)expect;
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
struct percpu_list_node *this_cpu_list_pop(struct percpu_list *list,
					   int *_cpu)
{
	struct percpu_list_node *node = NULL;
	int cpu;

	for (;;) {
		struct percpu_list_node *head;
		intptr_t *targetptr, expectnot, *load;
		long offset;
		int ret;

		cpu = rseq_cpu_start();
		targetptr = (intptr_t *)&list->c[cpu].head;
		expectnot = (intptr_t)NULL;
		offset = offsetof(struct percpu_list_node, next);
		load = (intptr_t *)&head;
		ret = rseq_cmpnev_storeoffp_load(targetptr, expectnot,
						   offset, load, cpu);
		if (rseq_likely(!ret)) {
			node = head;
			break;
		}
		if (ret > 0)
			break;
		/* Retry if rseq aborts. */
	}
	if (_cpu)
		*_cpu = cpu;
	return node;
}

/*
 * __percpu_list_pop is not safe against concurrent accesses. Should
 * only be used on lists that are not concurrently modified.
 */
struct percpu_list_node *__percpu_list_pop(struct percpu_list *list, int cpu)
{
	struct percpu_list_node *node;

	node = list->c[cpu].head;
	if (!node)
		return NULL;
	list->c[cpu].head = node->next;
	return node;
}

void *test_percpu_list_thread(void *arg)
{
	long long i, reps;
	struct percpu_list *list = (struct percpu_list *)arg;

	if (!opt_disable_rseq && rseq_register_current_thread())
		abort();

	reps = opt_reps;
	for (i = 0; i < reps; i++) {
		struct percpu_list_node *node;

		node = this_cpu_list_pop(list, NULL);
		if (opt_yield)
			sched_yield();  /* encourage shuffling */
		if (node)
			this_cpu_list_push(list, node, NULL);
	}

	printf_verbose("tid %d: number of rseq abort: %d, signals delivered: %u\n",
		       (int) rseq_gettid(), nr_abort, signals_delivered);
	if (!opt_disable_rseq && rseq_unregister_current_thread())
		abort();

	return NULL;
}

/* Simultaneous modification to a per-cpu linked list from many threads.  */
void test_percpu_list(void)
{
	const int num_threads = opt_threads;
	int i, j, ret;
	uint64_t sum = 0, expected_sum = 0;
	struct percpu_list list;
	pthread_t test_threads[num_threads];
	cpu_set_t allowed_cpus;

	memset(&list, 0, sizeof(list));

	/* Generate list entries for every usable cpu. */
	sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus);
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &allowed_cpus))
			continue;
		for (j = 1; j <= 100; j++) {
			struct percpu_list_node *node;

			expected_sum += j;

			node = malloc(sizeof(*node));
			assert(node);
			node->data = j;
			node->next = list.c[i].head;
			list.c[i].head = node;
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_create(&test_threads[i], NULL,
				     test_percpu_list_thread, &list);
		if (ret) {
			errno = ret;
			perror("pthread_create");
			abort();
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_join(test_threads[i], NULL);
		if (ret) {
			errno = ret;
			perror("pthread_join");
			abort();
		}
	}

	for (i = 0; i < CPU_SETSIZE; i++) {
		struct percpu_list_node *node;

		if (!CPU_ISSET(i, &allowed_cpus))
			continue;

		while ((node = __percpu_list_pop(&list, i))) {
			sum += node->data;
			free(node);
		}
	}

	/*
	 * All entries should now be accounted for (unless some external
	 * actor is interfering with our allowed affinity while this
	 * test is running).
	 */
	assert(sum == expected_sum);
}

bool this_cpu_buffer_push(struct percpu_buffer *buffer,
			  struct percpu_buffer_node *node,
			  int *_cpu)
{
	bool result = false;
	int cpu;

	for (;;) {
		intptr_t *targetptr_spec, newval_spec;
		intptr_t *targetptr_final, newval_final;
		intptr_t offset;
		int ret;

		cpu = rseq_cpu_start();
		offset = RSEQ_READ_ONCE(buffer->c[cpu].offset);
		if (offset == buffer->c[cpu].buflen)
			break;
		newval_spec = (intptr_t)node;
		targetptr_spec = (intptr_t *)&buffer->c[cpu].array[offset];
		newval_final = offset + 1;
		targetptr_final = &buffer->c[cpu].offset;
		if (opt_mb)
			ret = rseq_cmpeqv_trystorev_storev_release(
				targetptr_final, offset, targetptr_spec,
				newval_spec, newval_final, cpu);
		else
			ret = rseq_cmpeqv_trystorev_storev(targetptr_final,
				offset, targetptr_spec, newval_spec,
				newval_final, cpu);
		if (rseq_likely(!ret)) {
			result = true;
			break;
		}
		/* Retry if comparison fails or rseq aborts. */
	}
	if (_cpu)
		*_cpu = cpu;
	return result;
}

struct percpu_buffer_node *this_cpu_buffer_pop(struct percpu_buffer *buffer,
					       int *_cpu)
{
	struct percpu_buffer_node *head;
	int cpu;

	for (;;) {
		intptr_t *targetptr, newval;
		intptr_t offset;
		int ret;

		cpu = rseq_cpu_start();
		/* Load offset with single-copy atomicity. */
		offset = RSEQ_READ_ONCE(buffer->c[cpu].offset);
		if (offset == 0) {
			head = NULL;
			break;
		}
		head = RSEQ_READ_ONCE(buffer->c[cpu].array[offset - 1]);
		newval = offset - 1;
		targetptr = (intptr_t *)&buffer->c[cpu].offset;
		ret = rseq_cmpeqv_cmpeqv_storev(targetptr, offset,
			(intptr_t *)&buffer->c[cpu].array[offset - 1],
			(intptr_t)head, newval, cpu);
		if (rseq_likely(!ret))
			break;
		/* Retry if comparison fails or rseq aborts. */
	}
	if (_cpu)
		*_cpu = cpu;
	return head;
}

/*
 * __percpu_buffer_pop is not safe against concurrent accesses. Should
 * only be used on buffers that are not concurrently modified.
 */
struct percpu_buffer_node *__percpu_buffer_pop(struct percpu_buffer *buffer,
					       int cpu)
{
	struct percpu_buffer_node *head;
	intptr_t offset;

	offset = buffer->c[cpu].offset;
	if (offset == 0)
		return NULL;
	head = buffer->c[cpu].array[offset - 1];
	buffer->c[cpu].offset = offset - 1;
	return head;
}

void *test_percpu_buffer_thread(void *arg)
{
	long long i, reps;
	struct percpu_buffer *buffer = (struct percpu_buffer *)arg;

	if (!opt_disable_rseq && rseq_register_current_thread())
		abort();

	reps = opt_reps;
	for (i = 0; i < reps; i++) {
		struct percpu_buffer_node *node;

		node = this_cpu_buffer_pop(buffer, NULL);
		if (opt_yield)
			sched_yield();  /* encourage shuffling */
		if (node) {
			if (!this_cpu_buffer_push(buffer, node, NULL)) {
				/* Should increase buffer size. */
				abort();
			}
		}
	}

	printf_verbose("tid %d: number of rseq abort: %d, signals delivered: %u\n",
		       (int) rseq_gettid(), nr_abort, signals_delivered);
	if (!opt_disable_rseq && rseq_unregister_current_thread())
		abort();

	return NULL;
}

/* Simultaneous modification to a per-cpu buffer from many threads.  */
void test_percpu_buffer(void)
{
	const int num_threads = opt_threads;
	int i, j, ret;
	uint64_t sum = 0, expected_sum = 0;
	struct percpu_buffer buffer;
	pthread_t test_threads[num_threads];
	cpu_set_t allowed_cpus;

	memset(&buffer, 0, sizeof(buffer));

	/* Generate list entries for every usable cpu. */
	sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus);
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &allowed_cpus))
			continue;
		/* Worse-case is every item in same CPU. */
		buffer.c[i].array =
			malloc(sizeof(*buffer.c[i].array) * CPU_SETSIZE *
			       BUFFER_ITEM_PER_CPU);
		assert(buffer.c[i].array);
		buffer.c[i].buflen = CPU_SETSIZE * BUFFER_ITEM_PER_CPU;
		for (j = 1; j <= BUFFER_ITEM_PER_CPU; j++) {
			struct percpu_buffer_node *node;

			expected_sum += j;

			/*
			 * We could theoretically put the word-sized
			 * "data" directly in the buffer. However, we
			 * want to model objects that would not fit
			 * within a single word, so allocate an object
			 * for each node.
			 */
			node = malloc(sizeof(*node));
			assert(node);
			node->data = j;
			buffer.c[i].array[j - 1] = node;
			buffer.c[i].offset++;
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_create(&test_threads[i], NULL,
				     test_percpu_buffer_thread, &buffer);
		if (ret) {
			errno = ret;
			perror("pthread_create");
			abort();
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_join(test_threads[i], NULL);
		if (ret) {
			errno = ret;
			perror("pthread_join");
			abort();
		}
	}

	for (i = 0; i < CPU_SETSIZE; i++) {
		struct percpu_buffer_node *node;

		if (!CPU_ISSET(i, &allowed_cpus))
			continue;

		while ((node = __percpu_buffer_pop(&buffer, i))) {
			sum += node->data;
			free(node);
		}
		free(buffer.c[i].array);
	}

	/*
	 * All entries should now be accounted for (unless some external
	 * actor is interfering with our allowed affinity while this
	 * test is running).
	 */
	assert(sum == expected_sum);
}

bool this_cpu_memcpy_buffer_push(struct percpu_memcpy_buffer *buffer,
				 struct percpu_memcpy_buffer_node item,
				 int *_cpu)
{
	bool result = false;
	int cpu;

	for (;;) {
		intptr_t *targetptr_final, newval_final, offset;
		char *destptr, *srcptr;
		size_t copylen;
		int ret;

		cpu = rseq_cpu_start();
		/* Load offset with single-copy atomicity. */
		offset = RSEQ_READ_ONCE(buffer->c[cpu].offset);
		if (offset == buffer->c[cpu].buflen)
			break;
		destptr = (char *)&buffer->c[cpu].array[offset];
		srcptr = (char *)&item;
		/* copylen must be <= 4kB. */
		copylen = sizeof(item);
		newval_final = offset + 1;
		targetptr_final = &buffer->c[cpu].offset;
		if (opt_mb)
			ret = rseq_cmpeqv_trymemcpy_storev_release(
				targetptr_final, offset,
				destptr, srcptr, copylen,
				newval_final, cpu);
		else
			ret = rseq_cmpeqv_trymemcpy_storev(targetptr_final,
				offset, destptr, srcptr, copylen,
				newval_final, cpu);
		if (rseq_likely(!ret)) {
			result = true;
			break;
		}
		/* Retry if comparison fails or rseq aborts. */
	}
	if (_cpu)
		*_cpu = cpu;
	return result;
}

bool this_cpu_memcpy_buffer_pop(struct percpu_memcpy_buffer *buffer,
				struct percpu_memcpy_buffer_node *item,
				int *_cpu)
{
	bool result = false;
	int cpu;

	for (;;) {
		intptr_t *targetptr_final, newval_final, offset;
		char *destptr, *srcptr;
		size_t copylen;
		int ret;

		cpu = rseq_cpu_start();
		/* Load offset with single-copy atomicity. */
		offset = RSEQ_READ_ONCE(buffer->c[cpu].offset);
		if (offset == 0)
			break;
		destptr = (char *)item;
		srcptr = (char *)&buffer->c[cpu].array[offset - 1];
		/* copylen must be <= 4kB. */
		copylen = sizeof(*item);
		newval_final = offset - 1;
		targetptr_final = &buffer->c[cpu].offset;
		ret = rseq_cmpeqv_trymemcpy_storev(targetptr_final,
			offset, destptr, srcptr, copylen,
			newval_final, cpu);
		if (rseq_likely(!ret)) {
			result = true;
			break;
		}
		/* Retry if comparison fails or rseq aborts. */
	}
	if (_cpu)
		*_cpu = cpu;
	return result;
}

/*
 * __percpu_memcpy_buffer_pop is not safe against concurrent accesses. Should
 * only be used on buffers that are not concurrently modified.
 */
bool __percpu_memcpy_buffer_pop(struct percpu_memcpy_buffer *buffer,
				struct percpu_memcpy_buffer_node *item,
				int cpu)
{
	intptr_t offset;

	offset = buffer->c[cpu].offset;
	if (offset == 0)
		return false;
	memcpy(item, &buffer->c[cpu].array[offset - 1], sizeof(*item));
	buffer->c[cpu].offset = offset - 1;
	return true;
}

void *test_percpu_memcpy_buffer_thread(void *arg)
{
	long long i, reps;
	struct percpu_memcpy_buffer *buffer = (struct percpu_memcpy_buffer *)arg;

	if (!opt_disable_rseq && rseq_register_current_thread())
		abort();

	reps = opt_reps;
	for (i = 0; i < reps; i++) {
		struct percpu_memcpy_buffer_node item;
		bool result;

		result = this_cpu_memcpy_buffer_pop(buffer, &item, NULL);
		if (opt_yield)
			sched_yield();  /* encourage shuffling */
		if (result) {
			if (!this_cpu_memcpy_buffer_push(buffer, item, NULL)) {
				/* Should increase buffer size. */
				abort();
			}
		}
	}

	printf_verbose("tid %d: number of rseq abort: %d, signals delivered: %u\n",
		       (int) rseq_gettid(), nr_abort, signals_delivered);
	if (!opt_disable_rseq && rseq_unregister_current_thread())
		abort();

	return NULL;
}

/* Simultaneous modification to a per-cpu buffer from many threads.  */
void test_percpu_memcpy_buffer(void)
{
	const int num_threads = opt_threads;
	int i, j, ret;
	uint64_t sum = 0, expected_sum = 0;
	struct percpu_memcpy_buffer buffer;
	pthread_t test_threads[num_threads];
	cpu_set_t allowed_cpus;

	memset(&buffer, 0, sizeof(buffer));

	/* Generate list entries for every usable cpu. */
	sched_getaffinity(0, sizeof(allowed_cpus), &allowed_cpus);
	for (i = 0; i < CPU_SETSIZE; i++) {
		if (!CPU_ISSET(i, &allowed_cpus))
			continue;
		/* Worse-case is every item in same CPU. */
		buffer.c[i].array =
			malloc(sizeof(*buffer.c[i].array) * CPU_SETSIZE *
			       MEMCPY_BUFFER_ITEM_PER_CPU);
		assert(buffer.c[i].array);
		buffer.c[i].buflen = CPU_SETSIZE * MEMCPY_BUFFER_ITEM_PER_CPU;
		for (j = 1; j <= MEMCPY_BUFFER_ITEM_PER_CPU; j++) {
			expected_sum += 2 * j + 1;

			/*
			 * We could theoretically put the word-sized
			 * "data" directly in the buffer. However, we
			 * want to model objects that would not fit
			 * within a single word, so allocate an object
			 * for each node.
			 */
			buffer.c[i].array[j - 1].data1 = j;
			buffer.c[i].array[j - 1].data2 = j + 1;
			buffer.c[i].offset++;
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_create(&test_threads[i], NULL,
				     test_percpu_memcpy_buffer_thread,
				     &buffer);
		if (ret) {
			errno = ret;
			perror("pthread_create");
			abort();
		}
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_join(test_threads[i], NULL);
		if (ret) {
			errno = ret;
			perror("pthread_join");
			abort();
		}
	}

	for (i = 0; i < CPU_SETSIZE; i++) {
		struct percpu_memcpy_buffer_node item;

		if (!CPU_ISSET(i, &allowed_cpus))
			continue;

		while (__percpu_memcpy_buffer_pop(&buffer, &item, i)) {
			sum += item.data1;
			sum += item.data2;
		}
		free(buffer.c[i].array);
	}

	/*
	 * All entries should now be accounted for (unless some external
	 * actor is interfering with our allowed affinity while this
	 * test is running).
	 */
	assert(sum == expected_sum);
}

static void test_signal_interrupt_handler(int signo)
{
	signals_delivered++;
}

static int set_signal_handler(void)
{
	int ret = 0;
	struct sigaction sa;
	sigset_t sigset;

	ret = sigemptyset(&sigset);
	if (ret < 0) {
		perror("sigemptyset");
		return ret;
	}

	sa.sa_handler = test_signal_interrupt_handler;
	sa.sa_mask = sigset;
	sa.sa_flags = 0;
	ret = sigaction(SIGUSR1, &sa, NULL);
	if (ret < 0) {
		perror("sigaction");
		return ret;
	}

	printf_verbose("Signal handler set for SIGUSR1\n");

	return ret;
}

struct test_membarrier_thread_args {
	int stop;
	intptr_t percpu_list_ptr;
};

/* Worker threads modify data in their "active" percpu lists. */
void *test_membarrier_worker_thread(void *arg)
{
	struct test_membarrier_thread_args *args =
		(struct test_membarrier_thread_args *)arg;
	const int iters = opt_reps;
	int i;

	if (rseq_register_current_thread()) {
		fprintf(stderr, "Error: rseq_register_current_thread(...) failed(%d): %s\n",
			errno, strerror(errno));
		abort();
	}

	/* Wait for initialization. */
	while (!atomic_load(&args->percpu_list_ptr)) {}

	for (i = 0; i < iters; ++i) {
		int ret;

		do {
			int cpu = rseq_cpu_start();

			ret = rseq_offset_deref_addv(&args->percpu_list_ptr,
				sizeof(struct percpu_list_entry) * cpu, 1, cpu);
		} while (rseq_unlikely(ret));
	}

	if (rseq_unregister_current_thread()) {
		fprintf(stderr, "Error: rseq_unregister_current_thread(...) failed(%d): %s\n",
			errno, strerror(errno));
		abort();
	}
	return NULL;
}

void test_membarrier_init_percpu_list(struct percpu_list *list)
{
	int i;

	memset(list, 0, sizeof(*list));
	for (i = 0; i < CPU_SETSIZE; i++) {
		struct percpu_list_node *node;

		node = malloc(sizeof(*node));
		assert(node);
		node->data = 0;
		node->next = NULL;
		list->c[i].head = node;
	}
}

void test_membarrier_free_percpu_list(struct percpu_list *list)
{
	int i;

	for (i = 0; i < CPU_SETSIZE; i++)
		free(list->c[i].head);
}

static int sys_membarrier(int cmd, int flags, int cpu_id)
{
	return syscall(__NR_membarrier, cmd, flags, cpu_id);
}

/*
 * The manager thread swaps per-cpu lists that worker threads see,
 * and validates that there are no unexpected modifications.
 */
void *test_membarrier_manager_thread(void *arg)
{
	struct test_membarrier_thread_args *args =
		(struct test_membarrier_thread_args *)arg;
	struct percpu_list list_a, list_b;
	intptr_t expect_a = 0, expect_b = 0;
	int cpu_a = 0, cpu_b = 0;

	if (rseq_register_current_thread()) {
		fprintf(stderr, "Error: rseq_register_current_thread(...) failed(%d): %s\n",
			errno, strerror(errno));
		abort();
	}

	/* Init lists. */
	test_membarrier_init_percpu_list(&list_a);
	test_membarrier_init_percpu_list(&list_b);

	atomic_store(&args->percpu_list_ptr, (intptr_t)&list_a);

	while (!atomic_load(&args->stop)) {
		/* list_a is "active". */
		cpu_a = rand() % CPU_SETSIZE;
		/*
		 * As list_b is "inactive", we should never see changes
		 * to list_b.
		 */
		if (expect_b != atomic_load(&list_b.c[cpu_b].head->data)) {
			fprintf(stderr, "Membarrier test failed\n");
			abort();
		}

		/* Make list_b "active". */
		atomic_store(&args->percpu_list_ptr, (intptr_t)&list_b);
		if (sys_membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ,
					MEMBARRIER_CMD_FLAG_CPU, cpu_a) &&
				errno != ENXIO /* missing CPU */) {
			perror("sys_membarrier");
			abort();
		}
		/*
		 * Cpu A should now only modify list_b, so the values
		 * in list_a should be stable.
		 */
		expect_a = atomic_load(&list_a.c[cpu_a].head->data);

		cpu_b = rand() % CPU_SETSIZE;
		/*
		 * As list_a is "inactive", we should never see changes
		 * to list_a.
		 */
		if (expect_a != atomic_load(&list_a.c[cpu_a].head->data)) {
			fprintf(stderr, "Membarrier test failed\n");
			abort();
		}

		/* Make list_a "active". */
		atomic_store(&args->percpu_list_ptr, (intptr_t)&list_a);
		if (sys_membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ,
					MEMBARRIER_CMD_FLAG_CPU, cpu_b) &&
				errno != ENXIO /* missing CPU*/) {
			perror("sys_membarrier");
			abort();
		}
		/* Remember a value from list_b. */
		expect_b = atomic_load(&list_b.c[cpu_b].head->data);
	}

	test_membarrier_free_percpu_list(&list_a);
	test_membarrier_free_percpu_list(&list_b);

	if (rseq_unregister_current_thread()) {
		fprintf(stderr, "Error: rseq_unregister_current_thread(...) failed(%d): %s\n",
			errno, strerror(errno));
		abort();
	}
	return NULL;
}

/* Test MEMBARRIER_CMD_PRIVATE_RESTART_RSEQ_ON_CPU membarrier command. */
#ifdef RSEQ_ARCH_HAS_OFFSET_DEREF_ADDV
void test_membarrier(void)
{
	const int num_threads = opt_threads;
	struct test_membarrier_thread_args thread_args;
	pthread_t worker_threads[num_threads];
	pthread_t manager_thread;
	int i, ret;

	if (sys_membarrier(MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED_RSEQ, 0, 0)) {
		perror("sys_membarrier");
		abort();
	}

	thread_args.stop = 0;
	thread_args.percpu_list_ptr = 0;
	ret = pthread_create(&manager_thread, NULL,
			test_membarrier_manager_thread, &thread_args);
	if (ret) {
		errno = ret;
		perror("pthread_create");
		abort();
	}

	for (i = 0; i < num_threads; i++) {
		ret = pthread_create(&worker_threads[i], NULL,
				test_membarrier_worker_thread, &thread_args);
		if (ret) {
			errno = ret;
			perror("pthread_create");
			abort();
		}
	}


	for (i = 0; i < num_threads; i++) {
		ret = pthread_join(worker_threads[i], NULL);
		if (ret) {
			errno = ret;
			perror("pthread_join");
			abort();
		}
	}

	atomic_store(&thread_args.stop, 1);
	ret = pthread_join(manager_thread, NULL);
	if (ret) {
		errno = ret;
		perror("pthread_join");
		abort();
	}
}
#else /* RSEQ_ARCH_HAS_OFFSET_DEREF_ADDV */
void test_membarrier(void)
{
	fprintf(stderr, "rseq_offset_deref_addv is not implemented on this architecture. "
			"Skipping membarrier test.\n");
}
#endif

static void show_usage(int argc, char **argv)
{
	printf("Usage : %s <OPTIONS>\n",
		argv[0]);
	printf("OPTIONS:\n");
	printf("	[-1 loops] Number of loops for delay injection 1\n");
	printf("	[-2 loops] Number of loops for delay injection 2\n");
	printf("	[-3 loops] Number of loops for delay injection 3\n");
	printf("	[-4 loops] Number of loops for delay injection 4\n");
	printf("	[-5 loops] Number of loops for delay injection 5\n");
	printf("	[-6 loops] Number of loops for delay injection 6\n");
	printf("	[-7 loops] Number of loops for delay injection 7 (-1 to enable -m)\n");
	printf("	[-8 loops] Number of loops for delay injection 8 (-1 to enable -m)\n");
	printf("	[-9 loops] Number of loops for delay injection 9 (-1 to enable -m)\n");
	printf("	[-m N] Yield/sleep/kill every modulo N (default 0: disabled) (>= 0)\n");
	printf("	[-y] Yield\n");
	printf("	[-k] Kill thread with signal\n");
	printf("	[-s S] S: =0: disabled (default), >0: sleep time (ms)\n");
	printf("	[-t N] Number of threads (default 200)\n");
	printf("	[-r N] Number of repetitions per thread (default 5000)\n");
	printf("	[-d] Disable rseq system call (no initialization)\n");
	printf("	[-D M] Disable rseq for each M threads\n");
	printf("	[-T test] Choose test: (s)pinlock, (l)ist, (b)uffer, (m)emcpy, (i)ncrement, membarrie(r)\n");
	printf("	[-M] Push into buffer and memcpy buffer with memory barriers.\n");
	printf("	[-v] Verbose output.\n");
	printf("	[-h] Show this help.\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			continue;
		switch (argv[i][1]) {
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			loop_cnt[argv[i][1] - '0'] = atol(argv[i + 1]);
			i++;
			break;
		case 'm':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_modulo = atol(argv[i + 1]);
			if (opt_modulo < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 's':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_sleep = atol(argv[i + 1]);
			if (opt_sleep < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'y':
			opt_yield = 1;
			break;
		case 'k':
			opt_signal = 1;
			break;
		case 'd':
			opt_disable_rseq = 1;
			break;
		case 'D':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_disable_mod = atol(argv[i + 1]);
			if (opt_disable_mod < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 't':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_threads = atol(argv[i + 1]);
			if (opt_threads < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'r':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_reps = atoll(argv[i + 1]);
			if (opt_reps < 0) {
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'h':
			show_usage(argc, argv);
			goto end;
		case 'T':
			if (argc < i + 2) {
				show_usage(argc, argv);
				goto error;
			}
			opt_test = *argv[i + 1];
			switch (opt_test) {
			case 's':
			case 'l':
			case 'i':
			case 'b':
			case 'm':
			case 'r':
				break;
			default:
				show_usage(argc, argv);
				goto error;
			}
			i++;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'M':
			opt_mb = 1;
			break;
		default:
			show_usage(argc, argv);
			goto error;
		}
	}

	loop_cnt_1 = loop_cnt[1];
	loop_cnt_2 = loop_cnt[2];
	loop_cnt_3 = loop_cnt[3];
	loop_cnt_4 = loop_cnt[4];
	loop_cnt_5 = loop_cnt[5];
	loop_cnt_6 = loop_cnt[6];

	if (set_signal_handler())
		goto error;

	if (!opt_disable_rseq && rseq_register_current_thread())
		goto error;
	switch (opt_test) {
	case 's':
		printf_verbose("spinlock\n");
		test_percpu_spinlock();
		break;
	case 'l':
		printf_verbose("linked list\n");
		test_percpu_list();
		break;
	case 'b':
		printf_verbose("buffer\n");
		test_percpu_buffer();
		break;
	case 'm':
		printf_verbose("memcpy buffer\n");
		test_percpu_memcpy_buffer();
		break;
	case 'i':
		printf_verbose("counter increment\n");
		test_percpu_inc();
		break;
	case 'r':
		printf_verbose("membarrier\n");
		test_membarrier();
		break;
	}
	if (!opt_disable_rseq && rseq_unregister_current_thread())
		abort();
end:
	return 0;

error:
	return -1;
}
