// SPDX-License-Identifier: GPL-2.0+

/*
 * Context switch microbenchmark.
 *
 * Copyright 2018, Anton Blanchard, IBM Corp.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned int timeout = 30;

static void set_cpu(int cpu)
{
	cpu_set_t cpuset;

	if (cpu == -1)
		return;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	if (sched_setaffinity(0, sizeof(cpuset), &cpuset)) {
		perror("sched_setaffinity");
		exit(1);
	}
}

static void start_process_on(void *(*fn)(void *), void *arg, int cpu)
{
	int pid;

	pid = fork();
	if (pid == -1) {
		perror("fork");
		exit(1);
	}

	if (pid)
		return;

	set_cpu(cpu);

	fn(arg);

	exit(0);
}

static int cpu;
static int do_fork = 0;
static int do_vfork = 0;
static int do_exec = 0;
static char *exec_file;
static int exec_target = 0;
static unsigned long iterations;
static unsigned long iterations_prev;

static void run_exec(void)
{
	char *const argv[] = { "./exec_target", NULL };

	if (execve("./exec_target", argv, NULL) == -1) {
		perror("execve");
		exit(1);
	}
}

static void bench_fork(void)
{
	while (1) {
		pid_t pid = fork();
		if (pid == -1) {
			perror("fork");
			exit(1);
		}
		if (pid == 0) {
			if (do_exec)
				run_exec();
			_exit(0);
		}
		pid = waitpid(pid, NULL, 0);
		if (pid == -1) {
			perror("waitpid");
			exit(1);
		}
		iterations++;
	}
}

static void bench_vfork(void)
{
	while (1) {
		pid_t pid = vfork();
		if (pid == -1) {
			perror("fork");
			exit(1);
		}
		if (pid == 0) {
			if (do_exec)
				run_exec();
			_exit(0);
		}
		pid = waitpid(pid, NULL, 0);
		if (pid == -1) {
			perror("waitpid");
			exit(1);
		}
		iterations++;
	}
}

static void *null_fn(void *arg)
{
	pthread_exit(NULL);
}

static void bench_thread(void)
{
	pthread_t tid;
	cpu_set_t cpuset;
	pthread_attr_t attr;
	int rc;

	rc = pthread_attr_init(&attr);
	if (rc) {
		errno = rc;
		perror("pthread_attr_init");
		exit(1);
	}

	if (cpu != -1) {
		CPU_ZERO(&cpuset);
		CPU_SET(cpu, &cpuset);

		rc = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
		if (rc) {
			errno = rc;
			perror("pthread_attr_setaffinity_np");
			exit(1);
		}
	}

	while (1) {
		rc = pthread_create(&tid, &attr, null_fn, NULL);
		if (rc) {
			errno = rc;
			perror("pthread_create");
			exit(1);
		}
		rc = pthread_join(tid, NULL);
		if (rc) {
			errno = rc;
			perror("pthread_join");
			exit(1);
		}
		iterations++;
	}
}

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

static void *bench_proc(void *arg)
{
	signal(SIGALRM, sigalrm_handler);
	alarm(1);

	if (do_fork)
		bench_fork();
	else if (do_vfork)
		bench_vfork();
	else
		bench_thread();

	return NULL;
}

static struct option options[] = {
	{ "fork", no_argument, &do_fork, 1 },
	{ "vfork", no_argument, &do_vfork, 1 },
	{ "exec", no_argument, &do_exec, 1 },
	{ "timeout", required_argument, 0, 's' },
	{ "exec-target", no_argument, &exec_target, 1 },
	{ NULL },
};

static void usage(void)
{
	fprintf(stderr, "Usage: fork <options> CPU\n\n");
	fprintf(stderr, "\t\t--fork\tUse fork() (default threads)\n");
	fprintf(stderr, "\t\t--vfork\tUse vfork() (default threads)\n");
	fprintf(stderr, "\t\t--exec\tAlso exec() (default no exec)\n");
	fprintf(stderr, "\t\t--timeout=X\tDuration in seconds to run (default 30)\n");
	fprintf(stderr, "\t\t--exec-target\tInternal option for exec workload\n");
}

int main(int argc, char *argv[])
{
	signed char c;

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

		case 's':
			timeout = atoi(optarg);
			break;

		default:
			usage();
			exit(1);
		}
	}

	if (do_fork && do_vfork) {
		usage();
		exit(1);
	}
	if (do_exec && !do_fork && !do_vfork) {
		usage();
		exit(1);
	}

	if (do_exec) {
		char *dirname = strdup(argv[0]);
		int i;
		i = strlen(dirname) - 1;
		while (i) {
			if (dirname[i] == '/') {
				dirname[i] = '\0';
				if (chdir(dirname) == -1) {
					perror("chdir");
					exit(1);
				}
				break;
			}
			i--;
		}
	}

	if (exec_target) {
		exit(0);
	}

	if (((argc - optind) != 1)) {
		cpu = -1;
	} else {
		cpu = atoi(argv[optind++]);
	}

	if (do_exec)
		exec_file = argv[0];

	set_cpu(cpu);

	printf("Using ");
	if (do_fork)
		printf("fork");
	else if (do_vfork)
		printf("vfork");
	else
		printf("clone");

	if (do_exec)
		printf(" + exec");

	printf(" on cpu %d\n", cpu);

	/* Create a new process group so we can signal everyone for exit */
	setpgid(getpid(), getpid());

	signal(SIGUSR1, sigusr1_handler);

	start_process_on(bench_proc, NULL, cpu);

	while (1)
		sleep(3600);

	return 0;
}
