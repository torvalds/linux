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
#include <sys/time.h>
#include <unistd.h>

#define DBGFS_TARGET_IDS "/sys/kernel/debug/damon/target_ids"

static void write_targetid_exit(void)
{
	int target_ids_fd = open(DBGFS_TARGET_IDS, O_RDWR);
	char pid_str[128];

	snprintf(pid_str, sizeof(pid_str), "%d", getpid());
	write(target_ids_fd, pid_str, sizeof(pid_str));
	close(target_ids_fd);
	exit(0);
}

unsigned long msec_timestamp(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000UL + tv.tv_usec / 1000;
}

int main(int argc, char *argv[])
{
	unsigned long start_ms;
	int time_to_run, nr_forks = 0;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <msecs to run>\n", argv[0]);
		exit(1);
	}
	time_to_run = atoi(argv[1]);

	start_ms = msec_timestamp();
	while (true) {
		int pid = fork();

		if (pid < 0) {
			fprintf(stderr, "fork() failed\n");
			exit(1);
		}
		if (pid == 0)
			write_targetid_exit();
		wait(NULL);
		nr_forks++;

		if (msec_timestamp() - start_ms > time_to_run)
			break;
	}
	printf("%d\n", nr_forks);
	return 0;
}
