/*
 * POWER Data Stream Control Register (DSCR) sysfs thread test
 *
 * This test updates the system wide DSCR default value through
 * sysfs interface which should then update all the CPU specific
 * DSCR default values which must also be then visible to threads
 * executing on individual CPUs on the system.
 *
 * Copyright 2015, Anshuman Khandual, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#define _GNU_SOURCE
#include "dscr.h"

static int test_thread_dscr(unsigned long val)
{
	unsigned long cur_dscr, cur_dscr_usr;

	cur_dscr = get_dscr();
	cur_dscr_usr = get_dscr_usr();

	if (val != cur_dscr) {
		printf("[cpu %d] Kernel DSCR should be %ld but is %ld\n",
					sched_getcpu(), val, cur_dscr);
		return 1;
	}

	if (val != cur_dscr_usr) {
		printf("[cpu %d] User DSCR should be %ld but is %ld\n",
					sched_getcpu(), val, cur_dscr_usr);
		return 1;
	}
	return 0;
}

static int check_cpu_dscr_thread(unsigned long val)
{
	cpu_set_t mask;
	int cpu;

	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (sched_setaffinity(0, sizeof(mask), &mask))
			continue;

		if (test_thread_dscr(val))
			return 1;
	}
	return 0;

}

int dscr_sysfs_thread(void)
{
	unsigned long orig_dscr_default;
	int i, j;

	orig_dscr_default = get_default_dscr();
	for (i = 0; i < COUNT; i++) {
		for (j = 0; j < DSCR_MAX; j++) {
			set_default_dscr(j);
			if (check_cpu_dscr_thread(j))
				goto fail;
		}
	}
	set_default_dscr(orig_dscr_default);
	return 0;
fail:
	set_default_dscr(orig_dscr_default);
	return 1;
}

int main(int argc, char *argv[])
{
	return test_harness(dscr_sysfs_thread, "dscr_sysfs_thread_test");
}
