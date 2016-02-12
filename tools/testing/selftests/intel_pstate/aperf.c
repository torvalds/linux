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

void usage(char *name) {
	printf ("Usage: %s cpunum\n", name);
}

int main(int argc, char **argv) {
	int i, cpu, fd;
	char msr_file_name[64];
	long long tsc, old_tsc, new_tsc;
	long long aperf, old_aperf, new_aperf;
	long long mperf, old_mperf, new_mperf;
	struct timeb before, after;
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
		perror("Failed to open");
		return 1;
	}

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset)) {
		perror("Failed to set cpu affinity");
		return 1;
	}

	ftime(&before);
	pread(fd, &old_tsc,  sizeof(old_tsc), 0x10);
	pread(fd, &old_aperf,  sizeof(old_mperf), 0xe7);
	pread(fd, &old_mperf,  sizeof(old_aperf), 0xe8);

	for (i=0; i<0x8fffffff; i++) {
		sqrt(i);
	}

	ftime(&after);
	pread(fd, &new_tsc,  sizeof(new_tsc), 0x10);
	pread(fd, &new_aperf,  sizeof(new_mperf), 0xe7);
	pread(fd, &new_mperf,  sizeof(new_aperf), 0xe8);

	tsc = new_tsc-old_tsc;
	aperf = new_aperf-old_aperf;
	mperf = new_mperf-old_mperf;

	start = before.time*1000 + before.millitm;
	finish = after.time*1000 + after.millitm;
	total = finish - start;

	printf("runTime: %4.2f\n", 1.0*total/1000);
	printf("freq: %7.0f\n", tsc / (1.0*aperf / (1.0 * mperf)) / total);
	return 0;
}
