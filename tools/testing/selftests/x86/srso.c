// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <cpuid.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void)
{
	struct perf_event_attr ret_attr, mret_attr;
	long long count_rets, count_rets_mispred;
	int rrets_fd, mrrets_fd;
	unsigned int cpuid1_eax, b, c, d;

	__cpuid(1, cpuid1_eax, b, c, d);

	if (cpuid1_eax < 0x00800f00 ||
	    cpuid1_eax > 0x00afffff) {
		fprintf(stderr, "This needs to run on a Zen[1-4] machine (CPUID(1).EAX: 0x%x). Exiting...\n", cpuid1_eax);
		exit(EXIT_FAILURE);
	}

	memset(&ret_attr, 0, sizeof(struct perf_event_attr));
	memset(&mret_attr, 0, sizeof(struct perf_event_attr));

	ret_attr.type = mret_attr.type = PERF_TYPE_RAW;
	ret_attr.size = mret_attr.size = sizeof(struct perf_event_attr);
	ret_attr.config = 0xc8;
	mret_attr.config = 0xc9;
	ret_attr.disabled = mret_attr.disabled = 1;
	ret_attr.exclude_user = mret_attr.exclude_user = 1;
	ret_attr.exclude_hv = mret_attr.exclude_hv = 1;

	rrets_fd = syscall(SYS_perf_event_open, &ret_attr, 0, -1, -1, 0);
	if (rrets_fd == -1) {
		perror("opening retired RETs fd");
		exit(EXIT_FAILURE);
	}

	mrrets_fd = syscall(SYS_perf_event_open, &mret_attr, 0, -1, -1, 0);
	if (mrrets_fd == -1) {
		perror("opening retired mispredicted RETs fd");
		exit(EXIT_FAILURE);
	}

	ioctl(rrets_fd, PERF_EVENT_IOC_RESET, 0);
	ioctl(mrrets_fd, PERF_EVENT_IOC_RESET, 0);

	ioctl(rrets_fd, PERF_EVENT_IOC_ENABLE, 0);
	ioctl(mrrets_fd, PERF_EVENT_IOC_ENABLE, 0);

	printf("Sleeping for 10 seconds\n");
	sleep(10);

	ioctl(rrets_fd, PERF_EVENT_IOC_DISABLE, 0);
	ioctl(mrrets_fd, PERF_EVENT_IOC_DISABLE, 0);

	read(rrets_fd, &count_rets, sizeof(long long));
	read(mrrets_fd, &count_rets_mispred, sizeof(long long));

	printf("RETs: (%lld retired <-> %lld mispredicted)\n",
		count_rets, count_rets_mispred);
	printf("SRSO Safe-RET mitigation works correctly if both counts are almost equal.\n");

	return 0;
}
