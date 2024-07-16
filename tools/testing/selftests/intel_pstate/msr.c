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


int main(int argc, char **argv) {
	int cpu, fd;
	long long msr;
	char msr_file_name[64];

	if (argc != 2)
		return 1;

	errno = 0;
	cpu = strtol(argv[1], (char **) NULL, 10);

	if (errno)
		return 1;

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDONLY);

	if (fd == -1) {
		perror("Failed to open");
		return 1;
	}

	pread(fd, &msr,  sizeof(msr), 0x199);

	printf("msr 0x199: 0x%llx\n", msr);
	return 0;
}
