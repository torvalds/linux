// SPDX-License-Identifier: GPL-2.0-only
/*
 * Use the core scheduling prctl() to test core scheduling cookies control.
 *
 * Copyright (c) 2021 Oracle and/or its affiliates.
 * Author: Chris Hyser <chris.hyser@oracle.com>
 *
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses>.
 */

#define _GNU_SOURCE
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sched.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if __GLIBC_PREREQ(2, 30) == 0
#include <sys/syscall.h>
static pid_t gettid(void)
{
	return syscall(SYS_gettid);
}
#endif

#ifndef PR_SCHED_CORE
#define PR_SCHED_CORE			62
# define PR_SCHED_CORE_GET		0
# define PR_SCHED_CORE_CREATE		1 /* create unique core_sched cookie */
# define PR_SCHED_CORE_SHARE_TO		2 /* push core_sched cookie to pid */
# define PR_SCHED_CORE_SHARE_FROM	3 /* pull core_sched cookie to pid */
# define PR_SCHED_CORE_MAX		4
#endif

#define MAX_PROCESSES 128
#define MAX_THREADS   128

static const char USAGE[] = "cs_prctl_test [options]\n"
"    options:\n"
"	-P  : number of processes to create.\n"
"	-T  : number of threads per process to create.\n"
"	-d  : delay time to keep tasks alive.\n"
"	-k  : keep tasks alive until keypress.\n";

enum pid_type {PIDTYPE_PID = 0, PIDTYPE_TGID, PIDTYPE_PGID};

const int THREAD_CLONE_FLAGS = CLONE_THREAD | CLONE_SIGHAND | CLONE_FS | CLONE_VM | CLONE_FILES;

struct child_args {
	int num_threads;
	int pfd[2];
	int cpid;
	int thr_tids[MAX_THREADS];
};

static struct child_args procs[MAX_PROCESSES];
static int num_processes = 2;
static int need_cleanup = 0;

static int _prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4,
		  unsigned long arg5)
{
	int res;

	res = prctl(option, arg2, arg3, arg4, arg5);
	printf("%d = prctl(%d, %ld, %ld, %ld, %lx)\n", res, option, (long)arg2, (long)arg3,
	       (long)arg4, arg5);
	return res;
}

#define STACK_SIZE (1024 * 1024)

#define handle_error(msg) __handle_error(__FILE__, __LINE__, msg)
static void __handle_error(char *fn, int ln, char *msg)
{
	int pidx;
	printf("(%s:%d) - ", fn, ln);
	perror(msg);
	if (need_cleanup) {
		for (pidx = 0; pidx < num_processes; ++pidx)
			kill(procs[pidx].cpid, 15);
		need_cleanup = 0;
	}
	exit(EXIT_FAILURE);
}

static void handle_usage(int rc, char *msg)
{
	puts(USAGE);
	puts(msg);
	putchar('\n');
	exit(rc);
}

static unsigned long get_cs_cookie(int pid)
{
	unsigned long long cookie;
	int ret;

	ret = prctl(PR_SCHED_CORE, PR_SCHED_CORE_GET, pid, PIDTYPE_PID,
		    (unsigned long)&cookie);
	if (ret) {
		printf("Not a core sched system\n");
		return -1UL;
	}

	return cookie;
}

static int child_func_thread(void __attribute__((unused))*arg)
{
	while (1)
		usleep(20000);
	return 0;
}

static void create_threads(int num_threads, int thr_tids[])
{
	void *child_stack;
	pid_t tid;
	int i;

	for (i = 0; i < num_threads; ++i) {
		child_stack = malloc(STACK_SIZE);
		if (!child_stack)
			handle_error("child stack allocate");

		tid = clone(child_func_thread, child_stack + STACK_SIZE, THREAD_CLONE_FLAGS, NULL);
		if (tid == -1)
			handle_error("clone thread");
		thr_tids[i] = tid;
	}
}

static int child_func_process(void *arg)
{
	struct child_args *ca = (struct child_args *)arg;

	close(ca->pfd[0]);

	create_threads(ca->num_threads, ca->thr_tids);

	write(ca->pfd[1], &ca->thr_tids, sizeof(int) * ca->num_threads);
	close(ca->pfd[1]);

	while (1)
		usleep(20000);
	return 0;
}

static unsigned char child_func_process_stack[STACK_SIZE];

void create_processes(int num_processes, int num_threads, struct child_args proc[])
{
	pid_t cpid;
	int i;

	for (i = 0; i < num_processes; ++i) {
		proc[i].num_threads = num_threads;

		if (pipe(proc[i].pfd) == -1)
			handle_error("pipe() failed");

		cpid = clone(child_func_process, child_func_process_stack + STACK_SIZE,
			     SIGCHLD, &proc[i]);
		proc[i].cpid = cpid;
		close(proc[i].pfd[1]);
	}

	for (i = 0; i < num_processes; ++i) {
		read(proc[i].pfd[0], &proc[i].thr_tids, sizeof(int) * proc[i].num_threads);
		close(proc[i].pfd[0]);
	}
}

void disp_processes(int num_processes, struct child_args proc[])
{
	int i, j;

	printf("tid=%d, / tgid=%d / pgid=%d: %lx\n", gettid(), getpid(), getpgid(0),
	       get_cs_cookie(getpid()));

	for (i = 0; i < num_processes; ++i) {
		printf("    tid=%d, / tgid=%d / pgid=%d: %lx\n", proc[i].cpid, proc[i].cpid,
		       getpgid(proc[i].cpid), get_cs_cookie(proc[i].cpid));
		for (j = 0; j < proc[i].num_threads; ++j) {
			printf("        tid=%d, / tgid=%d / pgid=%d: %lx\n", proc[i].thr_tids[j],
			       proc[i].cpid, getpgid(0), get_cs_cookie(proc[i].thr_tids[j]));
		}
	}
	puts("\n");
}

static int errors;

#define validate(v) _validate(__LINE__, v, #v)
void _validate(int line, int val, char *msg)
{
	if (!val) {
		++errors;
		printf("(%d) FAILED: %s\n", line, msg);
	} else {
		printf("(%d) PASSED: %s\n", line, msg);
	}
}

int main(int argc, char *argv[])
{
	int keypress = 0;
	int num_threads = 3;
	int delay = 0;
	int res = 0;
	int pidx;
	int pid;
	int opt;

	while ((opt = getopt(argc, argv, ":hkT:P:d:")) != -1) {
		switch (opt) {
		case 'P':
			num_processes = (int)strtol(optarg, NULL, 10);
			break;
		case 'T':
			num_threads = (int)strtoul(optarg, NULL, 10);
			break;
		case 'd':
			delay = (int)strtol(optarg, NULL, 10);
			break;
		case 'k':
			keypress = 1;
			break;
		case 'h':
			printf(USAGE);
			exit(EXIT_SUCCESS);
		default:
			handle_usage(20, "unknown option");
		}
	}

	if (num_processes < 1 || num_processes > MAX_PROCESSES)
		handle_usage(1, "Bad processes value");

	if (num_threads < 1 || num_threads > MAX_THREADS)
		handle_usage(2, "Bad thread value");

	if (keypress)
		delay = -1;

	srand(time(NULL));

	/* put into separate process group */
	if (setpgid(0, 0) != 0)
		handle_error("process group");

	printf("\n## Create a thread/process/process group hiearchy\n");
	create_processes(num_processes, num_threads, procs);
	need_cleanup = 1;
	disp_processes(num_processes, procs);
	validate(get_cs_cookie(0) == 0);

	printf("\n## Set a cookie on entire process group\n");
	if (_prctl(PR_SCHED_CORE, PR_SCHED_CORE_CREATE, 0, PIDTYPE_PGID, 0) < 0)
		handle_error("core_sched create failed -- PGID");
	disp_processes(num_processes, procs);

	validate(get_cs_cookie(0) != 0);

	/* get a random process pid */
	pidx = rand() % num_processes;
	pid = procs[pidx].cpid;

	validate(get_cs_cookie(0) == get_cs_cookie(pid));
	validate(get_cs_cookie(0) == get_cs_cookie(procs[pidx].thr_tids[0]));

	printf("\n## Set a new cookie on entire process/TGID [%d]\n", pid);
	if (_prctl(PR_SCHED_CORE, PR_SCHED_CORE_CREATE, pid, PIDTYPE_TGID, 0) < 0)
		handle_error("core_sched create failed -- TGID");
	disp_processes(num_processes, procs);

	validate(get_cs_cookie(0) != get_cs_cookie(pid));
	validate(get_cs_cookie(pid) != 0);
	validate(get_cs_cookie(pid) == get_cs_cookie(procs[pidx].thr_tids[0]));

	printf("\n## Copy the cookie of current/PGID[%d], to pid [%d] as PIDTYPE_PID\n",
	       getpid(), pid);
	if (_prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_TO, pid, PIDTYPE_PID, 0) < 0)
		handle_error("core_sched share to itself failed -- PID");
	disp_processes(num_processes, procs);

	validate(get_cs_cookie(0) == get_cs_cookie(pid));
	validate(get_cs_cookie(pid) != 0);
	validate(get_cs_cookie(pid) != get_cs_cookie(procs[pidx].thr_tids[0]));

	printf("\n## Copy cookie from a thread [%d] to current/PGID [%d] as PIDTYPE_PID\n",
	       procs[pidx].thr_tids[0], getpid());
	if (_prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_FROM, procs[pidx].thr_tids[0],
		   PIDTYPE_PID, 0) < 0)
		handle_error("core_sched share from thread failed -- PID");
	disp_processes(num_processes, procs);

	validate(get_cs_cookie(0) == get_cs_cookie(procs[pidx].thr_tids[0]));
	validate(get_cs_cookie(pid) != get_cs_cookie(procs[pidx].thr_tids[0]));

	printf("\n## Copy cookie from current [%d] to current as pidtype PGID\n", getpid());
	if (_prctl(PR_SCHED_CORE, PR_SCHED_CORE_SHARE_TO, 0, PIDTYPE_PGID, 0) < 0)
		handle_error("core_sched share to self failed -- PGID");
	disp_processes(num_processes, procs);

	validate(get_cs_cookie(0) == get_cs_cookie(pid));
	validate(get_cs_cookie(pid) != 0);
	validate(get_cs_cookie(pid) == get_cs_cookie(procs[pidx].thr_tids[0]));

	if (errors) {
		printf("TESTS FAILED. errors: %d\n", errors);
		res = 10;
	} else {
		printf("SUCCESS !!!\n");
	}

	if (keypress)
		getchar();
	else
		sleep(delay);

	for (pidx = 0; pidx < num_processes; ++pidx)
		kill(procs[pidx].cpid, 15);

	return res;
}
