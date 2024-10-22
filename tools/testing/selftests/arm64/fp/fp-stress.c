// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Limited.
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 199309L

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <asm/hwcap.h>

#include "../../kselftest.h"

#define MAX_VLS 16

struct child_data {
	char *name, *output;
	pid_t pid;
	int stdout;
	bool output_seen;
	bool exited;
	int exit_status;
};

static int epoll_fd;
static struct child_data *children;
static struct epoll_event *evs;
static int tests;
static int num_children;
static bool terminate;

static int startup_pipe[2];

static int num_processors(void)
{
	long nproc = sysconf(_SC_NPROCESSORS_CONF);
	if (nproc < 0) {
		perror("Unable to read number of processors\n");
		exit(EXIT_FAILURE);
	}

	return nproc;
}

static void child_start(struct child_data *child, const char *program)
{
	int ret, pipefd[2], i;
	struct epoll_event ev;

	ret = pipe(pipefd);
	if (ret != 0)
		ksft_exit_fail_msg("Failed to create stdout pipe: %s (%d)\n",
				   strerror(errno), errno);

	child->pid = fork();
	if (child->pid == -1)
		ksft_exit_fail_msg("fork() failed: %s (%d)\n",
				   strerror(errno), errno);

	if (!child->pid) {
		/*
		 * In child, replace stdout with the pipe, errors to
		 * stderr from here as kselftest prints to stdout.
		 */
		ret = dup2(pipefd[1], 1);
		if (ret == -1) {
			printf("dup2() %d\n", errno);
			exit(EXIT_FAILURE);
		}

		/*
		 * Duplicate the read side of the startup pipe to
		 * FD 3 so we can close everything else.
		 */
		ret = dup2(startup_pipe[0], 3);
		if (ret == -1) {
			printf("dup2() %d\n", errno);
			exit(EXIT_FAILURE);
		}

		/*
		 * Very dumb mechanism to clean open FDs other than
		 * stdio. We don't want O_CLOEXEC for the pipes...
		 */
		for (i = 4; i < 8192; i++)
			close(i);

		/*
		 * Read from the startup pipe, there should be no data
		 * and we should block until it is closed.  We just
		 * carry on on error since this isn't super critical.
		 */
		ret = read(3, &i, sizeof(i));
		if (ret < 0)
			printf("read(startp pipe) failed: %s (%d)\n",
			       strerror(errno), errno);
		if (ret > 0)
			printf("%d bytes of data on startup pipe\n", ret);
		close(3);

		ret = execl(program, program, NULL);
		printf("execl(%s) failed: %d (%s)\n",
		       program, errno, strerror(errno));

		exit(EXIT_FAILURE);
	} else {
		/*
		 * In parent, remember the child and close our copy of the
		 * write side of stdout.
		 */
		close(pipefd[1]);
		child->stdout = pipefd[0];
		child->output = NULL;
		child->exited = false;
		child->output_seen = false;

		ev.events = EPOLLIN | EPOLLHUP;
		ev.data.ptr = child;

		ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, child->stdout, &ev);
		if (ret < 0) {
			ksft_exit_fail_msg("%s EPOLL_CTL_ADD failed: %s (%d)\n",
					   child->name, strerror(errno), errno);
		}
	}
}

static bool child_output_read(struct child_data *child)
{
	char read_data[1024];
	char work[1024];
	int ret, len, cur_work, cur_read;

	ret = read(child->stdout, read_data, sizeof(read_data));
	if (ret < 0) {
		if (errno == EINTR)
			return true;

		ksft_print_msg("%s: read() failed: %s (%d)\n",
			       child->name, strerror(errno),
			       errno);
		return false;
	}
	len = ret;

	child->output_seen = true;

	/* Pick up any partial read */
	if (child->output) {
		strncpy(work, child->output, sizeof(work) - 1);
		cur_work = strnlen(work, sizeof(work));
		free(child->output);
		child->output = NULL;
	} else {
		cur_work = 0;
	}

	cur_read = 0;
	while (cur_read < len) {
		work[cur_work] = read_data[cur_read++];

		if (work[cur_work] == '\n') {
			work[cur_work] = '\0';
			ksft_print_msg("%s: %s\n", child->name, work);
			cur_work = 0;
		} else {
			cur_work++;
		}
	}

	if (cur_work) {
		work[cur_work] = '\0';
		ret = asprintf(&child->output, "%s", work);
		if (ret == -1)
			ksft_exit_fail_msg("Out of memory\n");
	}

	return false;
}

static void child_output(struct child_data *child, uint32_t events,
			 bool flush)
{
	bool read_more;

	if (events & EPOLLIN) {
		do {
			read_more = child_output_read(child);
		} while (read_more);
	}

	if (events & EPOLLHUP) {
		close(child->stdout);
		child->stdout = -1;
		flush = true;
	}

	if (flush && child->output) {
		ksft_print_msg("%s: %s<EOF>\n", child->name, child->output);
		free(child->output);
		child->output = NULL;
	}
}

static void child_tickle(struct child_data *child)
{
	if (child->output_seen && !child->exited)
		kill(child->pid, SIGUSR2);
}

static void child_stop(struct child_data *child)
{
	if (!child->exited)
		kill(child->pid, SIGTERM);
}

static void child_cleanup(struct child_data *child)
{
	pid_t ret;
	int status;
	bool fail = false;

	if (!child->exited) {
		do {
			ret = waitpid(child->pid, &status, 0);
			if (ret == -1 && errno == EINTR)
				continue;

			if (ret == -1) {
				ksft_print_msg("waitpid(%d) failed: %s (%d)\n",
					       child->pid, strerror(errno),
					       errno);
				fail = true;
				break;
			}
		} while (!WIFEXITED(status));
		child->exit_status = WEXITSTATUS(status);
	}

	if (!child->output_seen) {
		ksft_print_msg("%s no output seen\n", child->name);
		fail = true;
	}

	if (child->exit_status != 0) {
		ksft_print_msg("%s exited with error code %d\n",
			       child->name, child->exit_status);
		fail = true;
	}

	ksft_test_result(!fail, "%s\n", child->name);
}

static void handle_child_signal(int sig, siginfo_t *info, void *context)
{
	int i;
	bool found = false;

	for (i = 0; i < num_children; i++) {
		if (children[i].pid == info->si_pid) {
			children[i].exited = true;
			children[i].exit_status = info->si_status;
			found = true;
			break;
		}
	}

	if (!found)
		ksft_print_msg("SIGCHLD for unknown PID %d with status %d\n",
			       info->si_pid, info->si_status);
}

static void handle_exit_signal(int sig, siginfo_t *info, void *context)
{
	int i;

	/* If we're already exiting then don't signal again */
	if (terminate)
		return;

	ksft_print_msg("Got signal, exiting...\n");

	terminate = true;

	/*
	 * This should be redundant, the main loop should clean up
	 * after us, but for safety stop everything we can here.
	 */
	for (i = 0; i < num_children; i++)
		child_stop(&children[i]);
}

static void start_fpsimd(struct child_data *child, int cpu, int copy)
{
	int ret;

	ret = asprintf(&child->name, "FPSIMD-%d-%d", cpu, copy);
	if (ret == -1)
		ksft_exit_fail_msg("asprintf() failed\n");

	child_start(child, "./fpsimd-test");

	ksft_print_msg("Started %s\n", child->name);
}

static void start_kernel(struct child_data *child, int cpu, int copy)
{
	int ret;

	ret = asprintf(&child->name, "KERNEL-%d-%d", cpu, copy);
	if (ret == -1)
		ksft_exit_fail_msg("asprintf() failed\n");

	child_start(child, "./kernel-test");

	ksft_print_msg("Started %s\n", child->name);
}

static void start_sve(struct child_data *child, int vl, int cpu)
{
	int ret;

	ret = prctl(PR_SVE_SET_VL, vl | PR_SVE_VL_INHERIT);
	if (ret < 0)
		ksft_exit_fail_msg("Failed to set SVE VL %d\n", vl);

	ret = asprintf(&child->name, "SVE-VL-%d-%d", vl, cpu);
	if (ret == -1)
		ksft_exit_fail_msg("asprintf() failed\n");

	child_start(child, "./sve-test");

	ksft_print_msg("Started %s\n", child->name);
}

static void start_ssve(struct child_data *child, int vl, int cpu)
{
	int ret;

	ret = asprintf(&child->name, "SSVE-VL-%d-%d", vl, cpu);
	if (ret == -1)
		ksft_exit_fail_msg("asprintf() failed\n");

	ret = prctl(PR_SME_SET_VL, vl | PR_SME_VL_INHERIT);
	if (ret < 0)
		ksft_exit_fail_msg("Failed to set SME VL %d\n", ret);

	child_start(child, "./ssve-test");

	ksft_print_msg("Started %s\n", child->name);
}

static void start_za(struct child_data *child, int vl, int cpu)
{
	int ret;

	ret = prctl(PR_SME_SET_VL, vl | PR_SVE_VL_INHERIT);
	if (ret < 0)
		ksft_exit_fail_msg("Failed to set SME VL %d\n", ret);

	ret = asprintf(&child->name, "ZA-VL-%d-%d", vl, cpu);
	if (ret == -1)
		ksft_exit_fail_msg("asprintf() failed\n");

	child_start(child, "./za-test");

	ksft_print_msg("Started %s\n", child->name);
}

static void start_zt(struct child_data *child, int cpu)
{
	int ret;

	ret = asprintf(&child->name, "ZT-%d", cpu);
	if (ret == -1)
		ksft_exit_fail_msg("asprintf() failed\n");

	child_start(child, "./zt-test");

	ksft_print_msg("Started %s\n", child->name);
}

static void probe_vls(int vls[], int *vl_count, int set_vl)
{
	unsigned int vq;
	int vl;

	*vl_count = 0;

	for (vq = SVE_VQ_MAX; vq > 0; vq /= 2) {
		vl = prctl(set_vl, vq * 16);
		if (vl == -1)
			ksft_exit_fail_msg("SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		vl &= PR_SVE_VL_LEN_MASK;

		if (*vl_count && (vl == vls[*vl_count - 1]))
			break;

		vq = sve_vq_from_vl(vl);

		vls[*vl_count] = vl;
		*vl_count += 1;
	}
}

/* Handle any pending output without blocking */
static void drain_output(bool flush)
{
	int ret = 1;
	int i;

	while (ret > 0) {
		ret = epoll_wait(epoll_fd, evs, tests, 0);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			ksft_print_msg("epoll_wait() failed: %s (%d)\n",
				       strerror(errno), errno);
		}

		for (i = 0; i < ret; i++)
			child_output(evs[i].data.ptr, evs[i].events, flush);
	}
}

static const struct option options[] = {
	{ "timeout",	required_argument, NULL, 't' },
	{ }
};

int main(int argc, char **argv)
{
	int ret;
	int timeout = 10;
	int cpus, i, j, c;
	int sve_vl_count, sme_vl_count;
	bool all_children_started = false;
	int seen_children;
	int sve_vls[MAX_VLS], sme_vls[MAX_VLS];
	bool have_sme2;
	struct sigaction sa;

	while ((c = getopt_long(argc, argv, "t:", options, NULL)) != -1) {
		switch (c) {
		case 't':
			ret = sscanf(optarg, "%d", &timeout);
			if (ret != 1)
				ksft_exit_fail_msg("Failed to parse timeout %s\n",
						   optarg);
			break;
		default:
			ksft_exit_fail_msg("Unknown argument\n");
		}
	}

	cpus = num_processors();
	tests = 0;

	if (getauxval(AT_HWCAP) & HWCAP_SVE) {
		probe_vls(sve_vls, &sve_vl_count, PR_SVE_SET_VL);
		tests += sve_vl_count * cpus;
	} else {
		sve_vl_count = 0;
	}

	if (getauxval(AT_HWCAP2) & HWCAP2_SME) {
		probe_vls(sme_vls, &sme_vl_count, PR_SME_SET_VL);
		tests += sme_vl_count * cpus * 2;
	} else {
		sme_vl_count = 0;
	}

	if (getauxval(AT_HWCAP2) & HWCAP2_SME2) {
		tests += cpus;
		have_sme2 = true;
	} else {
		have_sme2 = false;
	}

	tests += cpus * 2;

	ksft_print_header();
	ksft_set_plan(tests);

	ksft_print_msg("%d CPUs, %d SVE VLs, %d SME VLs, SME2 %s\n",
		       cpus, sve_vl_count, sme_vl_count,
		       have_sme2 ? "present" : "absent");

	if (timeout > 0)
		ksft_print_msg("Will run for %ds\n", timeout);
	else
		ksft_print_msg("Will run until terminated\n");

	children = calloc(sizeof(*children), tests);
	if (!children)
		ksft_exit_fail_msg("Unable to allocate child data\n");

	ret = epoll_create1(EPOLL_CLOEXEC);
	if (ret < 0)
		ksft_exit_fail_msg("epoll_create1() failed: %s (%d)\n",
				   strerror(errno), ret);
	epoll_fd = ret;

	/* Create a pipe which children will block on before execing */
	ret = pipe(startup_pipe);
	if (ret != 0)
		ksft_exit_fail_msg("Failed to create startup pipe: %s (%d)\n",
				   strerror(errno), errno);

	/* Get signal handers ready before we start any children */
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handle_exit_signal;
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	ret = sigaction(SIGINT, &sa, NULL);
	if (ret < 0)
		ksft_print_msg("Failed to install SIGINT handler: %s (%d)\n",
			       strerror(errno), errno);
	ret = sigaction(SIGTERM, &sa, NULL);
	if (ret < 0)
		ksft_print_msg("Failed to install SIGTERM handler: %s (%d)\n",
			       strerror(errno), errno);
	sa.sa_sigaction = handle_child_signal;
	ret = sigaction(SIGCHLD, &sa, NULL);
	if (ret < 0)
		ksft_print_msg("Failed to install SIGCHLD handler: %s (%d)\n",
			       strerror(errno), errno);

	evs = calloc(tests, sizeof(*evs));
	if (!evs)
		ksft_exit_fail_msg("Failed to allocated %d epoll events\n",
				   tests);

	for (i = 0; i < cpus; i++) {
		start_fpsimd(&children[num_children++], i, 0);
		start_kernel(&children[num_children++], i, 0);

		for (j = 0; j < sve_vl_count; j++)
			start_sve(&children[num_children++], sve_vls[j], i);

		for (j = 0; j < sme_vl_count; j++) {
			start_ssve(&children[num_children++], sme_vls[j], i);
			start_za(&children[num_children++], sme_vls[j], i);
		}

		if (have_sme2)
			start_zt(&children[num_children++], i);
	}

	/*
	 * All children started, close the startup pipe and let them
	 * run.
	 */
	close(startup_pipe[0]);
	close(startup_pipe[1]);

	for (;;) {
		/* Did we get a signal asking us to exit? */
		if (terminate)
			break;

		/*
		 * Timeout is counted in seconds with no output, the
		 * tests print during startup then are silent when
		 * running so this should ensure they all ran enough
		 * to install the signal handler, this is especially
		 * useful in emulation where we will both be slow and
		 * likely to have a large set of VLs.
		 */
		ret = epoll_wait(epoll_fd, evs, tests, 1000);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			ksft_exit_fail_msg("epoll_wait() failed: %s (%d)\n",
					   strerror(errno), errno);
		}

		/* Output? */
		if (ret > 0) {
			for (i = 0; i < ret; i++) {
				child_output(evs[i].data.ptr, evs[i].events,
					     false);
			}
			continue;
		}

		/* Otherwise epoll_wait() timed out */

		/*
		 * If the child processes have not produced output they
		 * aren't actually running the tests yet .
		 */
		if (!all_children_started) {
			seen_children = 0;

			for (i = 0; i < num_children; i++)
				if (children[i].output_seen ||
				    children[i].exited)
					seen_children++;

			if (seen_children != num_children) {
				ksft_print_msg("Waiting for %d children\n",
					       num_children - seen_children);
				continue;
			}

			all_children_started = true;
		}

		ksft_print_msg("Sending signals, timeout remaining: %d\n",
			       timeout);

		for (i = 0; i < num_children; i++)
			child_tickle(&children[i]);

		/* Negative timeout means run indefinitely */
		if (timeout < 0)
			continue;
		if (--timeout == 0)
			break;
	}

	ksft_print_msg("Finishing up...\n");
	terminate = true;

	for (i = 0; i < tests; i++)
		child_stop(&children[i]);

	drain_output(false);

	for (i = 0; i < tests; i++)
		child_cleanup(&children[i]);

	drain_output(true);

	ksft_print_cnts();

	return 0;
}
