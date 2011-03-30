#if defined(__i386__) || defined(__x86_64__)

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include "helpers/helpers.h"

/* Intel specific MSRs */
#define MSR_IA32_PERF_STATUS		0x198
#define MSR_IA32_MISC_ENABLES		0x1a0
#define MSR_IA32_ENERGY_PERF_BIAS	0x1b0

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

int msr_intel_has_boost_support(unsigned int cpu)
{
	unsigned long long misc_enables;
	int ret;

	ret = read_msr(cpu, MSR_IA32_MISC_ENABLES, &misc_enables);
	if (ret)
		return ret;
	return (misc_enables >> 38) & 0x1;
}

int msr_intel_boost_is_active(unsigned int cpu)
{
	unsigned long long perf_status;
	int ret;

	ret = read_msr(cpu, MSR_IA32_PERF_STATUS, &perf_status);
	if (ret)
		return ret;
	return (perf_status >> 32) & 0x1;
}

int msr_intel_get_perf_bias(unsigned int cpu)
{
	unsigned long long val;
	int ret;

	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_PERF_BIAS))
		return -1;

	ret = read_msr(cpu, MSR_IA32_ENERGY_PERF_BIAS, &val);
	if (ret)
		return ret;
	return val;
}

int msr_intel_set_perf_bias(unsigned int cpu, unsigned int val)
{
	int ret;

	if (!(cpupower_cpu_info.caps & CPUPOWER_CAP_PERF_BIAS))
		return -1;

	ret = write_msr(cpu, MSR_IA32_ENERGY_PERF_BIAS, val);
	if (ret)
		return ret;
	return 0;
}
#endif
