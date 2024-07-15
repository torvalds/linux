// SPDX-License-Identifier: GPL-2.0
/*
 * Author: SeongJae Park <sj@kernel.org>
 */
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DBGFS_MONITOR_ON "/sys/kernel/debug/damon/monitor_on_DEPRECATED"
#define DBGFS_TARGET_IDS "/sys/kernel/debug/damon/target_ids"

static void turn_damon_on_exit(void)
{
	int target_ids_fd = open(DBGFS_TARGET_IDS, O_RDWR);
	int monitor_on_fd = open(DBGFS_MONITOR_ON, O_RDWR);
	char pid_str[128];

	snprintf(pid_str, sizeof(pid_str), "%d", getpid());
	write(target_ids_fd, pid_str, sizeof(pid_str));
	write(monitor_on_fd, "on\n", 3);
	close(target_ids_fd);
	close(monitor_on_fd);
	usleep(1000);
	exit(0);
}

static void try_race(void)
{
	int target_ids_fd = open(DBGFS_TARGET_IDS, O_RDWR);
	int pid = fork();
	int buf[256];

	if (pid < 0) {
		fprintf(stderr, "fork() failed\n");
		exit(1);
	}
	if (pid == 0)
		turn_damon_on_exit();
	while (true) {
		int status;

		read(target_ids_fd, buf, sizeof(buf));
		if (waitpid(-1, &status, WNOHANG) == pid)
			break;
	}
	close(target_ids_fd);
}

static inline uint64_t ts_to_ms(struct timespec *ts)
{
	return (uint64_t)ts->tv_sec * 1000 + (uint64_t)ts->tv_nsec / 1000000;
}

int main(int argc, char *argv[])
{
	struct timespec start_time, now;
	int runtime_ms;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <runtime in ms>\n", argv[0]);
		exit(1);
	}
	runtime_ms = atoi(argv[1]);
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	while (true) {
		try_race();
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (ts_to_ms(&now) - ts_to_ms(&start_time) > runtime_ms)
			break;
	}
	return 0;
}
