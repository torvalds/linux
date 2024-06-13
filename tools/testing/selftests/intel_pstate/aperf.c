// SPDX-License-Identifier: GPL-2.0
#include <math.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/timeb.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include "../kselftest.h"

#define MSEC_PER_SEC	1000L
#define NSEC_PER_MSEC	1000000L

void usage(char *name) {
	printf ("Usage: %s cpunum\n", name);
}

int main(int argc, char **argv) {
	unsigned int i, cpu, fd;
	char msr_file_name[64];
	long long tsc, old_tsc, new_tsc;
	long long aperf, old_aperf, new_aperf;
	long long mperf, old_mperf, new_mperf;
	struct timespec before, after;
	long long int start, finish, total;
	cpu_set_t cpuset;

	if (argc != 2) {
		usage(argv[0]);
		return 1;
	}

	errno = 0;
	cpu = strtol(argv[1], (char **) NULL, 10);

	if (errno) {
		usage(argv[0]);
		return 1;
	}

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDONLY);

	if (fd == -1) {
		printf("/dev/cpu/%d/msr: %s\n", cpu, strerror(errno));
		return KSFT_SKIP;
	}

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset)) {
		perror("Failed to set cpu affinity");
		return 1;
	}

	if (clock_gettime(CLOCK_MONOTONIC, &before) < 0) {
		perror("clock_gettime");
		return 1;
	}
	pread(fd, &old_tsc,  sizeof(old_tsc), 0x10);
	pread(fd, &old_aperf,  sizeof(old_mperf), 0xe7);
	pread(fd, &old_mperf,  sizeof(old_aperf), 0xe8);

	for (i=0; i<0x8fffffff; i++) {
		sqrt(i);
	}

	if (clock_gettime(CLOCK_MONOTONIC, &after) < 0) {
		perror("clock_gettime");
		return 1;
	}
	pread(fd, &new_tsc,  sizeof(new_tsc), 0x10);
	pread(fd, &new_aperf,  sizeof(new_mperf), 0xe7);
	pread(fd, &new_mperf,  sizeof(new_aperf), 0xe8);

	tsc = new_tsc-old_tsc;
	aperf = new_aperf-old_aperf;
	mperf = new_mperf-old_mperf;

	start = before.tv_sec*MSEC_PER_SEC + before.tv_nsec/NSEC_PER_MSEC;
	finish = after.tv_sec*MSEC_PER_SEC + after.tv_nsec/NSEC_PER_MSEC;
	total = finish - start;

	printf("runTime: %4.2f\n", 1.0*total/MSEC_PER_SEC);
	printf("freq: %7.0f\n", tsc / (1.0*aperf / (1.0 * mperf)) / total);
	return 0;
}
