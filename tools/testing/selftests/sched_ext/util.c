/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include "util.h"

#ifndef SCHED_EXT
#define SCHED_EXT 7
#endif

/* Returns read len on success, or -errno on failure. */
static ssize_t read_text(const char *path, char *buf, size_t max_len)
{
	ssize_t len;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	len = read(fd, buf, max_len - 1);

	if (len >= 0)
		buf[len] = 0;

	close(fd);
	return len < 0 ? -errno : len;
}

/* Returns written len on success, or -errno on failure. */
static ssize_t write_text(const char *path, char *buf, ssize_t len)
{
	int fd;
	ssize_t written;

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd < 0)
		return -errno;

	written = write(fd, buf, len);
	close(fd);
	return written < 0 ? -errno : written;
}

long file_read_long(const char *path)
{
	char buf[128];


	if (read_text(path, buf, sizeof(buf)) <= 0)
		return -1;

	return atol(buf);
}

int file_write_long(const char *path, long val)
{
	char buf[64];
	int ret;

	ret = sprintf(buf, "%ld", val);
	if (ret < 0)
		return ret;

	if (write_text(path, buf, ret) <= 0)
		return -1;

	return 0;
}

struct scx_test_gated_worker scx_test_spawn_gated_worker(int cpu, bool set_sched_ext)
{
	struct scx_test_gated_worker worker = { .pid = -1, .start_fd = -1 };
	int ready[2], start[2];
	pid_t parent = getpid();
	char byte = 1;

	if (pipe(ready))
		return worker;
	if (pipe(start)) {
		close(ready[0]);
		close(ready[1]);
		return worker;
	}

	worker.pid = fork();
	if (!worker.pid) {
		struct sched_param param = {};
		cpu_set_t mask;

		close(ready[0]);
		close(start[1]);
		if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent)
			_exit(1);
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (sched_setaffinity(0, sizeof(mask), &mask))
			_exit(1);
		if (set_sched_ext && sched_setscheduler(0, SCHED_EXT, &param))
			_exit(1);
		if (write(ready[1], &byte, 1) != 1)
			_exit(1);
		close(ready[1]);
		if (read(start[0], &byte, 1) != 1)
			_exit(1);
		close(start[0]);
		for (;;)
			asm volatile("" ::: "memory");
	}
	if (worker.pid < 0) {
		close(ready[0]);
		close(ready[1]);
		close(start[0]);
		close(start[1]);
		return worker;
	}

	close(ready[1]);
	close(start[0]);
	if (read(ready[0], &byte, 1) != 1) {
		close(ready[0]);
		close(start[1]);
		kill(worker.pid, SIGKILL);
		waitpid(worker.pid, NULL, 0);
		worker.pid = -1;
		return worker;
	}
	close(ready[0]);
	worker.start_fd = start[1];
	return worker;
}

bool scx_test_start_gated_worker(struct scx_test_gated_worker *worker)
{
	char byte = 1;

	if (write(worker->start_fd, &byte, 1) != 1)
		return false;
	close(worker->start_fd);
	worker->start_fd = -1;
	return true;
}

void scx_test_stop_gated_worker(struct scx_test_gated_worker *worker)
{
	if (worker->start_fd >= 0)
		close(worker->start_fd);
	if (worker->pid > 0) {
		kill(worker->pid, SIGKILL);
		waitpid(worker->pid, NULL, 0);
	}
	worker->pid = -1;
	worker->start_fd = -1;
}

static int config_preempt_lazy_mode(void)
{
	bool dynamic = false, lazy = false, immediate = false;
	char buf[128];
	gzFile file;

	file = gzopen("/proc/config.gz", "r");
	if (!file)
		return -1;

	while (gzgets(file, buf, sizeof(buf))) {
		if (!strcmp(buf, "CONFIG_PREEMPT_DYNAMIC=y\n"))
			dynamic = true;
		else if (!strcmp(buf, "CONFIG_PREEMPT_LAZY=y\n"))
			lazy = true;
		else if (!strcmp(buf, "CONFIG_PREEMPT_NONE=y\n") ||
			 !strcmp(buf, "CONFIG_PREEMPT_VOLUNTARY=y\n") ||
			 !strcmp(buf, "CONFIG_PREEMPT=y\n") ||
			 !strcmp(buf, "CONFIG_PREEMPT_RT=y\n"))
			immediate = true;
	}
	gzclose(file);

	if (dynamic)
		return -1;
	if (lazy)
		return 1;
	if (immediate)
		return 0;
	return -1;
}

int scx_test_preempt_lazy_mode(void)
{
	char buf[128];

	if (read_text("/sys/kernel/debug/sched/preempt", buf, sizeof(buf)) > 0) {
		if (strstr(buf, "(lazy)"))
			return 1;
		if (strstr(buf, "(none)") || strstr(buf, "(voluntary)") ||
		    strstr(buf, "(full)"))
			return 0;
	}

	return config_preempt_lazy_mode();
}
