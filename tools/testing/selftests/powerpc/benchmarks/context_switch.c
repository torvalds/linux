// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Context switch microbenchmark.
 *
 * Copyright (C) 2015 Anton Blanchard <anton@au.ibm.com>, IBM
 */

#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <linux/futex.h>
#ifdef __powerpc__
#include <altivec.h>
#endif
#include "utils.h"

static unsigned int timeout = 30;

static int touch_vdso;
struct timeval tv;

static int touch_fp = 1;
double fp;

static int touch_vector = 1;
vector int a, b, c;

#ifdef __powerpc__
static int touch_altivec = 1;

/*
 * Note: LTO (Link Time Optimisation) doesn't play well with this function
 * attribute. Be very careful enabling LTO for this test.
 */
static void __attribute__((__target__("no-vsx"))) altivec_touch_fn(void)
{
	c = a + b;
}
#endif

static void touch(void)
{
	if (touch_vdso)
		gettimeofday(&tv, NULL);

	if (touch_fp)
		fp += 0.1;

#ifdef __powerpc__
	if (touch_altivec)
		altivec_touch_fn();
#endif

	if (touch_vector)
		c = a + b;

	asm volatile("# %0 %1 %2": : "r"(&tv), "r"(&fp), "r"(&c));
}

static void start_thread_on(void *(*fn)(void *), void *arg, unsigned long cpu)
{
	int rc;
	pthread_t tid;
	cpu_set_t cpuset;
	pthread_attr_t attr;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	rc = pthread_attr_init(&attr);
	if (rc) {
		errno = rc;
		perror("pthread_attr_init");
		exit(1);
	}

	rc = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
	if (rc)	{
		errno = rc;
		perror("pthread_attr_setaffinity_np");
		exit(1);
	}

	rc = pthread_create(&tid, &attr, fn, arg);
	if (rc) {
		errno = rc;
		perror("pthread_create");
		exit(1);
	}
}

static void start_process_on(void *(*fn)(void *), void *arg, unsigned long cpu)
{
	int pid;
	cpu_set_t cpuset;

	pid = fork();
	if (pid == -1) {
		perror("fork");
		exit(1);
	}

	if (pid)
		return;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	if (sched_setaffinity(0, sizeof(cpuset), &cpuset)) {
		perror("sched_setaffinity");
		exit(1);
	}

	fn(arg);

	exit(0);
}

static unsigned long iterations;
static unsigned long iterations_prev;

static void sigalrm_handler(int junk)
{
	unsigned long i = iterations;

	printf("%ld\n", i - iterations_prev);
	iterations_prev = i;

	if (--timeout == 0)
		kill(0, SIGUSR1);

	alarm(1);
}

static void sigusr1_handler(int junk)
{
	exit(0);
}

struct actions {
	void (*setup)(int, int);
	void *(*thread1)(void *);
	void *(*thread2)(void *);
};

#define READ 0
#define WRITE 1

static int pipe_fd1[2];
static int pipe_fd2[2];

static void pipe_setup(int cpu1, int cpu2)
{
	if (pipe(pipe_fd1) || pipe(pipe_fd2))
		exit(1);
}

static void *pipe_thread1(void *arg)
{
	signal(SIGALRM, sigalrm_handler);
	alarm(1);

	while (1) {
		assert(read(pipe_fd1[READ], &c, 1) == 1);
		touch();

		assert(write(pipe_fd2[WRITE], &c, 1) == 1);
		touch();

		iterations += 2;
	}

	return NULL;
}

static void *pipe_thread2(void *arg)
{
	while (1) {
		assert(write(pipe_fd1[WRITE], &c, 1) == 1);
		touch();

		assert(read(pipe_fd2[READ], &c, 1) == 1);
		touch();
	}

	return NULL;
}

static struct actions pipe_actions = {
	.setup = pipe_setup,
	.thread1 = pipe_thread1,
	.thread2 = pipe_thread2,
};

static void yield_setup(int cpu1, int cpu2)
{
	if (cpu1 != cpu2) {
		fprintf(stderr, "Both threads must be on the same CPU for yield test\n");
		exit(1);
	}
}

static void *yield_thread1(void *arg)
{
	signal(SIGALRM, sigalrm_handler);
	alarm(1);

	while (1) {
		sched_yield();
		touch();

		iterations += 2;
	}

	return NULL;
}

static void *yield_thread2(void *arg)
{
	while (1) {
		sched_yield();
		touch();
	}

	return NULL;
}

static struct actions yield_actions = {
	.setup = yield_setup,
	.thread1 = yield_thread1,
	.thread2 = yield_thread2,
};

static long sys_futex(void *addr1, int op, int val1, struct timespec *timeout,
		      void *addr2, int val3)
{
	return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

static unsigned long cmpxchg(unsigned long *p, unsigned long expected,
			     unsigned long desired)
{
	unsigned long exp = expected;

	__atomic_compare_exchange_n(p, &exp, desired, 0,
				    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return exp;
}

static unsigned long xchg(unsigned long *p, unsigned long val)
{
	return __atomic_exchange_n(p, val, __ATOMIC_SEQ_CST);
}

static int processes;

static int mutex_lock(unsigned long *m)
{
	int c;
	int flags = FUTEX_WAIT;
	if (!processes)
		flags |= FUTEX_PRIVATE_FLAG;

	c = cmpxchg(m, 0, 1);
	if (!c)
		return 0;

	if (c == 1)
		c = xchg(m, 2);

	while (c) {
		sys_futex(m, flags, 2, NULL, NULL, 0);
		c = xchg(m, 2);
	}

	return 0;
}

static int mutex_unlock(unsigned long *m)
{
	int flags = FUTEX_WAKE;
	if (!processes)
		flags |= FUTEX_PRIVATE_FLAG;

	if (*m == 2)
		*m = 0;
	else if (xchg(m, 0) == 1)
		return 0;

	sys_futex(m, flags, 1, NULL, NULL, 0);

	return 0;
}

static unsigned long *m1, *m2;

static void futex_setup(int cpu1, int cpu2)
{
	if (!processes) {
		static unsigned long _m1, _m2;
		m1 = &_m1;
		m2 = &_m2;
	} else {
		int shmid;
		void *shmaddr;

		shmid = shmget(IPC_PRIVATE, getpagesize(), SHM_R | SHM_W);
		if (shmid < 0) {
			perror("shmget");
			exit(1);
		}

		shmaddr = shmat(shmid, NULL, 0);
		if (shmaddr == (char *)-1) {
			perror("shmat");
			shmctl(shmid, IPC_RMID, NULL);
			exit(1);
		}

		shmctl(shmid, IPC_RMID, NULL);

		m1 = shmaddr;
		m2 = shmaddr + sizeof(*m1);
	}

	*m1 = 0;
	*m2 = 0;

	mutex_lock(m1);
	mutex_lock(m2);
}

static void *futex_thread1(void *arg)
{
	signal(SIGALRM, sigalrm_handler);
	alarm(1);

	while (1) {
		mutex_lock(m2);
		mutex_unlock(m1);

		iterations += 2;
	}

	return NULL;
}

static void *futex_thread2(void *arg)
{
	while (1) {
		mutex_unlock(m2);
		mutex_lock(m1);
	}

	return NULL;
}

static struct actions futex_actions = {
	.setup = futex_setup,
	.thread1 = futex_thread1,
	.thread2 = futex_thread2,
};

static struct option options[] = {
	{ "test", required_argument, 0, 't' },
	{ "process", no_argument, &processes, 1 },
	{ "timeout", required_argument, 0, 's' },
	{ "vdso", no_argument, &touch_vdso, 1 },
	{ "no-fp", no_argument, &touch_fp, 0 },
#ifdef __powerpc__
	{ "no-altivec", no_argument, &touch_altivec, 0 },
#endif
	{ "no-vector", no_argument, &touch_vector, 0 },
	{ 0, },
};

static void usage(void)
{
	fprintf(stderr, "Usage: context_switch2 <options> CPU1 CPU2\n\n");
	fprintf(stderr, "\t\t--test=X\tpipe, futex or yield (default)\n");
	fprintf(stderr, "\t\t--process\tUse processes (default threads)\n");
	fprintf(stderr, "\t\t--timeout=X\tDuration in seconds to run (default 30)\n");
	fprintf(stderr, "\t\t--vdso\t\ttouch VDSO\n");
	fprintf(stderr, "\t\t--no-fp\t\tDon't touch FP\n");
#ifdef __powerpc__
	fprintf(stderr, "\t\t--no-altivec\tDon't touch altivec\n");
#endif
	fprintf(stderr, "\t\t--no-vector\tDon't touch vector\n");
}

int main(int argc, char *argv[])
{
	signed char c;
	struct actions *actions = &yield_actions;
	int cpu1;
	int cpu2;
	static void (*start_fn)(void *(*fn)(void *), void *arg, unsigned long cpu);

	while (1) {
		int option_index = 0;

		c = getopt_long(argc, argv, "", options, &option_index);

		if (c == -1)
			break;

		switch (c) {
		case 0:
			if (options[option_index].flag != 0)
				break;

			usage();
			exit(1);
			break;

		case 't':
			if (!strcmp(optarg, "pipe")) {
				actions = &pipe_actions;
			} else if (!strcmp(optarg, "yield")) {
				actions = &yield_actions;
			} else if (!strcmp(optarg, "futex")) {
				actions = &futex_actions;
			} else {
				usage();
				exit(1);
			}
			break;

		case 's':
			timeout = atoi(optarg);
			break;

		default:
			usage();
			exit(1);
		}
	}

	if (processes)
		start_fn = start_process_on;
	else
		start_fn = start_thread_on;

	if (((argc - optind) != 2)) {
		cpu1 = cpu2 = pick_online_cpu();
	} else {
		cpu1 = atoi(argv[optind++]);
		cpu2 = atoi(argv[optind++]);
	}

	printf("Using %s with ", processes ? "processes" : "threads");

	if (actions == &pipe_actions)
		printf("pipe");
	else if (actions == &yield_actions)
		printf("yield");
	else
		printf("futex");

	printf(" on cpus %d/%d touching FP:%s altivec:%s vector:%s vdso:%s\n",
	       cpu1, cpu2, touch_fp ?  "yes" : "no", touch_altivec ? "yes" : "no",
	       touch_vector ? "yes" : "no", touch_vdso ? "yes" : "no");

	/* Create a new process group so we can signal everyone for exit */
	setpgid(getpid(), getpid());

	signal(SIGUSR1, sigusr1_handler);

	actions->setup(cpu1, cpu2);

	start_fn(actions->thread1, NULL, cpu1);
	start_fn(actions->thread2, NULL, cpu2);

	while (1)
		sleep(3600);

	return 0;
}
