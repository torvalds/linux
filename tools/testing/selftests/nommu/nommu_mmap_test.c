// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include "kselftest.h"
#include "nommu.h"

#include <sys/vfs.h>
#ifndef RAMFS_MAGIC
#define RAMFS_MAGIC 0x858458f6
#endif

static size_t ps;
static int nommu;

struct test_case_t {
	const char *name;
	const char *pathname;
	int open_flags;
	int mmap_prot;
	int mmap_flags;
	int exp_err;
	int (*resolve_exp_err)(const char *path);
};

static int get_shm_expected_error(const char *path)
{
	struct statfs fs;

	if (!nommu)
		return 0;

	if (statfs(path, &fs) == 0) {
		if (fs.f_type == RAMFS_MAGIC)
			return 0; /* ramfs succeed with contiguous memory */
	}
	 /* hostfs, etc returns ENODEV due to lack of contiguous allocation */
	return ENODEV;
}

static struct test_case_t test_cases[] = {
	{
		.name = "anonymous private allocation",
		.pathname = NULL,
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ | PROT_WRITE,
		.mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE,
		.exp_err = 0,
		.resolve_exp_err = NULL,
	},
	{
		.name = "non-anonymous private file mapping (rw-)",
		.pathname = "/tmp/ksft.nommu-reg-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ | PROT_WRITE,
		.mmap_flags = MAP_PRIVATE,
		.exp_err = 0,
		.resolve_exp_err = NULL,
	},
	{
		.name = "non-anonymous private file mapping (r--)",
		.pathname = "/tmp/ksft.nommu-reg-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ,
		.mmap_flags = MAP_PRIVATE,
		.exp_err = 0,
		.resolve_exp_err = NULL,
	},
	{
		.name = "non-anonymous shared file mapping (rw-)",
		.pathname = "/tmp/ksft.nommu-shm-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ | PROT_WRITE,
		.mmap_flags = MAP_SHARED,
		.exp_err = 0,
		.resolve_exp_err = get_shm_expected_error,
	},
	{
		.name = "non-anonymous shared file mapping (r--)",
		.pathname = "/tmp/ksft.nommu-shm-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ,
		.mmap_flags = MAP_SHARED,
		.exp_err = 0,
		.resolve_exp_err = get_shm_expected_error,
	},
};

static int run_mapping_matrix_test(struct test_case_t *tcase)
{
	int fd;
	void *ptr = MAP_FAILED;
	char path_buf[PATH_MAX];
	const char *path = tcase->pathname;
	int rc = KSFT_PASS;
	int expected_error;

	ksft_print_msg("[RUN] %s\n", tcase->name);

	if (tcase->pathname == NULL) {
		fd = -1;
	} else if (strstr(tcase->pathname, "XXXXXX")) {
		strncpy(path_buf, tcase->pathname, sizeof(path_buf) - 1);
		path_buf[sizeof(path_buf) - 1] = '\0';
		fd = mkstemp(path_buf);
		if (fd < 0) {
			ksft_print_msg("Failed to setup temp node: %s\n",
				tcase->pathname);
			ksft_test_result_skip("%s\n", tcase->name);
			return KSFT_SKIP;
		}
		if (ftruncate(fd, ps) != 0) {
			ksft_print_msg("ftruncate failed for: %s\n", tcase->pathname);
			rc = KSFT_FAIL;
			goto cleanup;
		}
		path = path_buf;
	} else {
		fd = open(tcase->pathname, tcase->open_flags, 0600);
		if (fd < 0) {
			ksft_print_msg("Device node not accessible: %s\n",
				       tcase->pathname);
			ksft_test_result_skip("%s\n", tcase->name);
			return KSFT_SKIP;
		}
	}

	expected_error = tcase->exp_err;
	if (tcase->resolve_exp_err && fd >= 0)
		expected_error = tcase->resolve_exp_err(path);

	ptr = mmap(NULL, ps, tcase->mmap_prot, tcase->mmap_flags, fd, 0);

	if (expected_error != 0) {
		if (ptr != MAP_FAILED) {
			ksft_print_msg("mmap unexpectedly succeeded (exp error %d)\n",
					      expected_error);
			rc = KSFT_FAIL;
			goto cleanup;
		}
		if (errno != expected_error) {
			ksft_print_msg("mmap failed with %d (%s), but expected %d\n",
				errno, strerror(errno), expected_error);
			rc = KSFT_FAIL;
			goto cleanup;
		}
		ksft_print_msg("Correctly rejected with expected error %s(%d)\n",
			strerror(expected_error), expected_error);
		rc = KSFT_PASS;
		goto cleanup;
	}

	if (ptr == MAP_FAILED) {
		ksft_print_msg("mmap failed unexpectedly: %s\n", strerror(errno));
		rc = KSFT_FAIL;
		goto cleanup;
	}

cleanup:
	if (ptr != MAP_FAILED && munmap(ptr, ps)) {
		ksft_print_msg("munmap failed: %s\n", strerror(errno));
		rc = KSFT_FAIL;
	}
	if (fd >= 0) {
		close(fd);
		if (tcase->pathname && strstr(tcase->pathname, "XXXXXX") &&
		    unlink(path_buf)) {
			ksft_print_msg("unlink failed for %s: %s\n",
				       path_buf, strerror(errno));
			rc = KSFT_FAIL;
		}
	}
	ksft_test_result_report(rc, "%s\n", tcase->name);
	return rc;
}

static int test_map_fixed(void)
{
	void *fixed_addr;
	void *ptr;

	ksft_print_msg("[RUN] %s\n", __func__);

	fixed_addr = mmap(NULL, ps, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (fixed_addr == MAP_FAILED) {
		ksft_print_msg("Unable to reserve test address: %s\n",
				strerror(errno));
		ksft_test_result_skip("MAP_FIXED behavior\n");
		return KSFT_SKIP;
	}

	if (munmap(fixed_addr, ps)) {
		ksft_print_msg("Unable to release test address: %s\n",
			strerror(errno));
		ksft_test_result_fail("MAP_FIXED behavior\n");
		return KSFT_FAIL;
	}

	ptr = mmap(fixed_addr, ps, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

	if (nommu) {
		if (ptr == MAP_FAILED && (errno == ENODEV || errno == EINVAL)) {
			ksft_print_msg("MAP_FIXED correctly rejected under nommu\n");
			ksft_test_result_pass("MAP_FIXED behavior\n");
			return KSFT_PASS;
		}
		if (ptr != MAP_FAILED) {
			ksft_print_msg("MAP_FIXED unexpectedly allowed under nommu\n");
			ksft_test_result_fail("MAP_FIXED behavior\n");
			if (munmap(ptr, ps))
				ksft_print_msg("munmap failed: %s\n", strerror(errno));
			return KSFT_FAIL;
		}
		ksft_print_msg("MAP_FIXED failed under NOMMU: %s\n",
				      strerror(errno));
		ksft_test_result_fail("MAP_FIXED behavior\n");
		return KSFT_FAIL;
	}
	if (ptr != MAP_FAILED) {
		ksft_print_msg("MAP_FIXED successfully allocated under MMU\n");
		if (munmap(ptr, ps)) {
			ksft_print_msg("munmap failed: %s\n", strerror(errno));
			ksft_test_result_fail("MAP_FIXED behavior\n");
			return KSFT_FAIL;
		}
		ksft_test_result_pass("MAP_FIXED behavior\n");
		return KSFT_PASS;
	}
	ksft_print_msg("MAP_FIXED failed allocation under MMU\n");
	ksft_test_result_fail("MAP_FIXED behavior\n");
	return KSFT_FAIL;
}

int main(int argc, char **argv)
{
	int i;

	nommu = ksft_is_nommu();
	if (nommu < 0)
		ksft_exit_skip("Cannot read /proc/meminfo; mount procfs at /proc\n");

	ps = sysconf(_SC_PAGESIZE);
	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(test_cases) + 1);

	if (nommu)
		ksft_print_msg("Running strict MMAP test criteria under nommu architecture\n");
	else
		ksft_print_msg("Running MMAP test criteria under MMU architecture\n");

	test_map_fixed();
	for (i = 0; i < (int)ARRAY_SIZE(test_cases); i++)
		run_mapping_matrix_test(&test_cases[i]);

	ksft_finished();
}
