// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2018 IBM Corporation.
 * Copyright 2020 Canonical Ltd.
 */

#define __SANE_USERSPACE_TYPES__

#include <sys/types.h>
#include <stdint.h>
#include <malloc.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "utils.h"
#include "flush_utils.h"

int uaccess_flush_test(void)
{
	char *p;
	int repetitions = 10;
	int fd, passes = 0, iter, rc = 0;
	struct perf_event_read v;
	__u64 l1d_misses_total = 0;
	unsigned long iterations = 100000, zero_size = 24 * 1024;
	unsigned long l1d_misses_expected;
	int rfi_flush_orig;
	int entry_flush_orig;
	int uaccess_flush, uaccess_flush_orig;

	SKIP_IF(geteuid() != 0);

	// The PMU event we use only works on Power7 or later
	SKIP_IF(!have_hwcap(PPC_FEATURE_ARCH_2_06));

	if (read_debugfs_file("powerpc/rfi_flush", &rfi_flush_orig) < 0) {
		perror("Unable to read powerpc/rfi_flush debugfs file");
		SKIP_IF(1);
	}

	if (read_debugfs_file("powerpc/entry_flush", &entry_flush_orig) < 0) {
		perror("Unable to read powerpc/entry_flush debugfs file");
		SKIP_IF(1);
	}

	if (read_debugfs_file("powerpc/uaccess_flush", &uaccess_flush_orig) < 0) {
		perror("Unable to read powerpc/entry_flush debugfs file");
		SKIP_IF(1);
	}

	if (rfi_flush_orig != 0) {
		if (write_debugfs_file("powerpc/rfi_flush", 0) < 0) {
			perror("error writing to powerpc/rfi_flush debugfs file");
			FAIL_IF(1);
		}
	}

	if (entry_flush_orig != 0) {
		if (write_debugfs_file("powerpc/entry_flush", 0) < 0) {
			perror("error writing to powerpc/entry_flush debugfs file");
			FAIL_IF(1);
		}
	}

	uaccess_flush = uaccess_flush_orig;

	fd = perf_event_open_counter(PERF_TYPE_HW_CACHE, PERF_L1D_READ_MISS_CONFIG, -1);
	FAIL_IF(fd < 0);

	p = (char *)memalign(zero_size, CACHELINE_SIZE);

	FAIL_IF(perf_event_enable(fd));

	// disable L1 prefetching
	set_dscr(1);

	iter = repetitions;

	/*
	 * We expect to see l1d miss for each cacheline access when entry_flush
	 * is set. Allow a small variation on this.
	 */
	l1d_misses_expected = iterations * (zero_size / CACHELINE_SIZE - 2);

again:
	FAIL_IF(perf_event_reset(fd));

	syscall_loop_uaccess(p, iterations, zero_size);

	FAIL_IF(read(fd, &v, sizeof(v)) != sizeof(v));

	if (uaccess_flush && v.l1d_misses >= l1d_misses_expected)
		passes++;
	else if (!uaccess_flush && v.l1d_misses < (l1d_misses_expected / 2))
		passes++;

	l1d_misses_total += v.l1d_misses;

	while (--iter)
		goto again;

	if (passes < repetitions) {
		printf("FAIL (L1D misses with uaccess_flush=%d: %llu %c %lu) [%d/%d failures]\n",
		       uaccess_flush, l1d_misses_total, uaccess_flush ? '<' : '>',
		       uaccess_flush ? repetitions * l1d_misses_expected :
		       repetitions * l1d_misses_expected / 2,
		       repetitions - passes, repetitions);
		rc = 1;
	} else {
		printf("PASS (L1D misses with uaccess_flush=%d: %llu %c %lu) [%d/%d pass]\n",
		       uaccess_flush, l1d_misses_total, uaccess_flush ? '>' : '<',
		       uaccess_flush ? repetitions * l1d_misses_expected :
		       repetitions * l1d_misses_expected / 2,
		       passes, repetitions);
	}

	if (uaccess_flush == uaccess_flush_orig) {
		uaccess_flush = !uaccess_flush_orig;
		if (write_debugfs_file("powerpc/uaccess_flush", uaccess_flush) < 0) {
			perror("error writing to powerpc/uaccess_flush debugfs file");
			return 1;
		}
		iter = repetitions;
		l1d_misses_total = 0;
		passes = 0;
		goto again;
	}

	perf_event_disable(fd);
	close(fd);

	set_dscr(0);

	if (write_debugfs_file("powerpc/rfi_flush", rfi_flush_orig) < 0) {
		perror("unable to restore original value of powerpc/rfi_flush debugfs file");
		return 1;
	}

	if (write_debugfs_file("powerpc/entry_flush", entry_flush_orig) < 0) {
		perror("unable to restore original value of powerpc/entry_flush debugfs file");
		return 1;
	}

	if (write_debugfs_file("powerpc/uaccess_flush", uaccess_flush_orig) < 0) {
		perror("unable to restore original value of powerpc/uaccess_flush debugfs file");
		return 1;
	}

	return rc;
}

int main(int argc, char *argv[])
{
	return test_harness(uaccess_flush_test, "uaccess_flush_test");
}
