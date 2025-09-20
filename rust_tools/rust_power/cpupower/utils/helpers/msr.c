// SPDX-License-Identifier: GPL-2.0
#if defined(__i386__) || defined(__x86_64__)

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include "helpers/helpers.h"

/* Intel specific MSRs */
#define MSR_IA32_PERF_STATUS		0x198
#define MSR_IA32_MISC_ENABLES		0x1a0
#define MSR_NEHALEM_TURBO_RATIO_LIMIT	0x1ad

/*
 * read_msr
 *
 * Will return 0 on success and -1 on failure.
 * Possible errno values could be:
 * EFAULT -If the read/write did not fully complete
 * EIO    -If the CPU does not support MSRs
 * ENXIO  -If the CPU does not exist
 */

int read_msr(int cpu, unsigned int idx, unsigned long long *val)
{
	int fd;
	char msr_file_name[64];

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_RDONLY);
	if (fd < 0)
		return -1;
	if (lseek(fd, idx, SEEK_CUR) == -1)
		goto err;
	if (read(fd, val, sizeof *val) != sizeof *val)
		goto err;
	close(fd);
	return 0;
 err:
	close(fd);
	return -1;
}

/*
 * write_msr
 *
 * Will return 0 on success and -1 on failure.
 * Possible errno values could be:
 * EFAULT -If the read/write did not fully complete
 * EIO    -If the CPU does not support MSRs
 * ENXIO  -If the CPU does not exist
 */
int write_msr(int cpu, unsigned int idx, unsigned long long val)
{
	int fd;
	char msr_file_name[64];

	sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
	fd = open(msr_file_name, O_WRONLY);
	if (fd < 0)
		return -1;
	if (lseek(fd, idx, SEEK_CUR) == -1)
		goto err;
	if (write(fd, &val, sizeof val) != sizeof val)
		goto err;
	close(fd);
	return 0;
 err:
	close(fd);
	return -1;
}

unsigned long long msr_intel_get_turbo_ratio(unsigned int cpu)
{
	unsigned long long val;
	int ret;

	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_HAS_TURBO_RATIO))
		return -1;

	ret = read_msr(cpu, MSR_NEHALEM_TURBO_RATIO_LIMIT, &val);
	if (ret)
		return ret;
	return val;
}
#endif
