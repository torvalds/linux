// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (C) 2003 - 2004  Dominik Brodowski <linux@dominikbrodowski.de>
 *
 * Based on code found in
 * linux/arch/i386/kernel/cpu/cpufreq/speedstep-centrino.c
 * and originally developed by Jeremy Fitzhardinge.
 *
 * USAGE: simply run it to decode the current settings on CPU 0,
 *	  or pass the CPU number as argument, or pass the MSR content
 *	  as argument.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#define MCPU	32

#define MSR_IA32_PERF_STATUS	0x198

static int rdmsr(unsigned int cpu, unsigned int msr,
		 unsigned int *lo, unsigned int *hi)
{
	int fd;
	char file[20];
	unsigned long long val;
	int retval = -1;

	*lo = *hi = 0;

	if (cpu > MCPU)
		goto err1;

	sprintf(file, "/dev/cpu/%d/msr", cpu);
	fd = open(file, O_RDONLY);

	if (fd < 0)
		goto err1;

	if (lseek(fd, msr, SEEK_CUR) == -1)
		goto err2;

	if (read(fd, &val, 8) != 8)
		goto err2;

	*lo = (uint32_t )(val & 0xffffffffull);
	*hi = (uint32_t )(val>>32 & 0xffffffffull);

	retval = 0;
err2:
	close(fd);
err1:
	return retval;
}

static void decode (unsigned int msr)
{
	unsigned int multiplier;
	unsigned int mv;

	multiplier = ((msr >> 8) & 0xFF);

	mv = (((msr & 0xFF) * 16) + 700);

	printf("0x%x means multiplier %d @ %d mV\n", msr, multiplier, mv);
}

static int decode_live(unsigned int cpu)
{
	unsigned int lo, hi;
	int err;

	err = rdmsr(cpu, MSR_IA32_PERF_STATUS, &lo, &hi);

	if (err) {
		printf("can't get MSR_IA32_PERF_STATUS for cpu %d\n", cpu);
		printf("Possible trouble: you don't run an Enhanced SpeedStep capable cpu\n");
		printf("or you are not root, or the msr driver is not present\n");
		return 1;
	}

	decode(lo);

	return 0;
}

int main (int argc, char **argv)
{
	unsigned int cpu, mode = 0;

	if (argc < 2)
		cpu = 0;
	else {
		cpu = strtoul(argv[1], NULL, 0);
		if (cpu >= MCPU)
			mode = 1;
	}

	if (mode)
		decode(cpu);
	else
		decode_live(cpu);

	return 0;
}
