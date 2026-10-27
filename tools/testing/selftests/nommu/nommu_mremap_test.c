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

static long get_fs_type(const char *path)
{
	struct statfs fs;

	if (statfs(path, &fs) == 0)
		return fs.f_type;

	return 0;
}

static void munmap_shrink_test(void)
{
	void *addr;
	int ret;

	/* munmap shrink test */
	for (int i = 0; i < 4; i++) {
		addr = mmap(NULL, ps * 4, PROT_READ | PROT_WRITE,
			    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (addr == MAP_FAILED) {
			ksft_print_msg("mmap failed: %s(%d)\n", strerror(errno), errno);
			ksft_test_result_fail("munmap shrink\n");
			return;
		}
		ret = munmap((char *)addr + ps * i, ps);
		if (ret != 0) {
			ksft_print_msg("memory %p isn't unmapped at %p\n",
				addr, (char *)addr + ps * i);
			goto error;
		}

		if (i == 0) {
			if (munmap(addr +  ps, ps * 3))
				goto error;
		} else if (i == 1) {
			if (munmap(addr, ps) || munmap(addr + (ps * 2), ps * 2))
				goto error;
		} else if (i == 2) {
			if (munmap(addr, ps * 2) || munmap(addr + (ps * 3), ps))
				goto error;
		} else if (i == 3) {
			if (munmap(addr, ps * 3))
				goto error;
		}
	}

	ksft_test_result_pass("munmap shrink\n");
	return;
error:
	for (int j = 0; j < 4; j++) {
		if (munmap((char *)addr + j * ps, ps))
			ksft_print_msg("cleanup munmap failed: %s\n", strerror(errno));
	}
	ksft_print_msg("clean up failures\n");
	ksft_test_result_fail("munmap shrink\n");
}

static size_t page_align(size_t len)
{
	return (len + ps - 1) / ps * ps;
}

static void mremap_shrink_test(void)
{
	void *addr, *addr2;
	size_t current_len;
	size_t old_len, new_len;
	struct param {
		size_t old;
		size_t new;
	} params[] = {
		/* should not happen any shrink */
		{ .old = ps * 4 - 1, .new = ps * 4 - 2 },
		/* should not happen any shrink */
		{ .old = ps * 4 - 1, .new = ps * 4 },
		{ .old = ps * 4, .new = ps * 2 },
		/* should not happen any shrink */
		{ .old = ps * 2, .new = ps * 2 - 2 },
		{ .old = ps * 2 - 2, .new = ps * 1 },
	};

	/* mremap shrink test */
	current_len = page_align(ps * 4 - 1);
	addr = mmap(NULL, ps * 4 - 1, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (addr == MAP_FAILED) {
		ksft_print_msg("mmap failed: %s(%d)\n", strerror(errno), errno);
		ksft_test_result_fail("mremap shrink\n");
		return;
	}

	for (int i = 0; i < ARRAY_SIZE(params); i++) {
		old_len = params[i].old;
		new_len = params[i].new;
		current_len = page_align(new_len);
		addr2 = mremap(addr, old_len, new_len, MREMAP_MAYMOVE);
		if (addr2 == MAP_FAILED) {
			ksft_print_msg("memory %p isn't remapped at %p\n", addr, addr2);
			ksft_test_result_fail("mremap shrink\n");
			munmap(addr, page_align(old_len));
			return;
		}

		addr = addr2;
	}

	if (munmap(addr, current_len)) {
		ksft_print_msg("cleanup failed: %s\n", strerror(errno));
		ksft_test_result_fail("mremap shrink\n");
		return;
	}
	ksft_test_result_pass("mremap shrink\n");
}

static int get_shared_writable_file_expected_error(const char *path)
{
	if (nommu && get_fs_type(path) == RAMFS_MAGIC)
		return EPERM; /* ramfs failed */

	return 0;
}

struct mremap_case_t {
	const char *name;
	const char *pathname;
	int open_flags;
	int mmap_prot;
	int mmap_flags;
	int exp_err;
	int nommu_exp_err;
	int (*resolve_exp_err)(const char *path);
	unsigned int old_pages;
	unsigned int new_pages;
};

static struct mremap_case_t mremap_cases[] = {
	{
		.name = "anonymous shrink (r--)",
		.pathname = NULL,
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ,
		.mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE,
		.exp_err = 0,
		.resolve_exp_err = 0,
	},
	{
		.name = "shared file shrink (r--)",
		.pathname = "/tmp/ksft.nommu-remap-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ,
		.mmap_flags = MAP_SHARED,
		.exp_err = 0,
		.resolve_exp_err = get_shared_writable_file_expected_error,
	},
	{
		.name = "private file unchanged length (r-)",
		.pathname = "/tmp/ksft.nommu-remap-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ,
		.mmap_flags = MAP_PRIVATE,
		.exp_err = 0,
		.nommu_exp_err = EPERM,
		.resolve_exp_err = 0,
		.old_pages = 4,
		.new_pages = 4,
	},
	{
		.name = "private file unchanged length (rw-)",
		.pathname = "/tmp/ksft.nommu-remap-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ | PROT_WRITE,
		.mmap_flags = MAP_PRIVATE,
		.exp_err = 0,
		.resolve_exp_err = 0,
		.old_pages = 4,
		.new_pages = 4,
	},
	{
		.name = "private file growth (r-)",
		.pathname = "/tmp/ksft.nommu-remap-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ,
		.mmap_flags = MAP_PRIVATE,
		.exp_err = 0,
		.nommu_exp_err = EPERM,
		.resolve_exp_err = 0,
		.old_pages = 4,
		.new_pages = 8,
	},
	{
		.name = "private file growth (rw-)",
		.pathname = "/tmp/ksft.nommu-remap-XXXXXX",
		.open_flags = O_CREAT | O_RDWR | O_EXCL,
		.mmap_prot = PROT_READ | PROT_WRITE,
		.mmap_flags = MAP_PRIVATE,
		.exp_err = 0,
		.nommu_exp_err = ENOMEM,
		.resolve_exp_err = 0,
		.old_pages = 4,
		.new_pages = 8,
	},
};

static int run_mremap_test(struct mremap_case_t *tcase)
{
	int fd = -1;
	void *addr, *addr2;
	char pb[PATH_MAX];
	const char *path = tcase->pathname;
	int rc = KSFT_PASS;
	int expected_error;
	unsigned int old_pages = tcase->old_pages ?: 4;
	unsigned int new_pages = tcase->new_pages ?: 2;
	unsigned int file_pages = old_pages > new_pages ?
				  old_pages : new_pages;

	ksft_print_msg("[RUN] Testing mremap: %s\n", tcase->name);

	if (tcase->pathname && strstr(tcase->pathname, "XXXXXX")) {
		strncpy(pb, tcase->pathname, sizeof(pb) - 1);
		pb[sizeof(pb) - 1] = '\0';
		fd = mkstemp(pb);
		if (fd < 0) {
			ksft_print_msg("Failed to setup file backing\n");
			ksft_test_result_skip("%s\n", tcase->name);
			return KSFT_SKIP;
		}
		if (ftruncate(fd, ps * file_pages) != 0) {
			ksft_print_msg("Failed to setup file backing\n");
			rc = KSFT_FAIL;
			goto out;
		}

		if (nommu && (tcase->mmap_flags & MAP_SHARED) &&
		    get_fs_type(pb) != RAMFS_MAGIC) {
			ksft_print_msg("Skip the test under non-ramfs filesystem (%s)\n",
				pb);
			rc = KSFT_SKIP;
			goto out;
		}
		path = pb;
	} else if (tcase->pathname) {
		fd = open(tcase->pathname, tcase->open_flags, 0600);
		if (fd < 0) {
			ksft_print_msg("Backing node not accessible\n");
			ksft_test_result_skip("%s\n", tcase->name);
			return KSFT_SKIP;
		}

		if (nommu && (tcase->mmap_flags & MAP_SHARED) &&
		    get_fs_type(tcase->pathname) != RAMFS_MAGIC) {
			ksft_print_msg("Skip the test under non-ramfs filesystem (%s)\n",
					      tcase->pathname);
			rc = KSFT_SKIP;
			goto out;
		}
	}

	addr = mmap(NULL, ps * old_pages, tcase->mmap_prot,
		    tcase->mmap_flags, fd, 0);
	if (addr == MAP_FAILED) {
		ksft_print_msg("mmap mapping failed %s(%d)\n", strerror(errno), errno);
		rc = KSFT_FAIL;
		goto out;
	}

	expected_error = nommu ? tcase->nommu_exp_err : tcase->exp_err;
	if (tcase->resolve_exp_err && fd >= 0)
		expected_error = tcase->resolve_exp_err(path);

	addr2 = mremap(addr, ps * old_pages, ps * new_pages,
		       MREMAP_MAYMOVE);

	if (expected_error != 0) {
		if (addr2 != MAP_FAILED) {
			ksft_print_msg("Expected error %d, but mremap unexpectedly succeeded\n",
				       expected_error);
			rc = KSFT_FAIL;
		} else if (errno != expected_error) {
			ksft_print_msg("Expected error %d, got %s(%d)\n",
				       expected_error, strerror(errno), errno);
			rc = KSFT_FAIL;
		} else {
			ksft_print_msg("%s: Handled expected error path (errno=%d)\n",
				       tcase->name, expected_error);
		}
	} else if (addr2 == MAP_FAILED) {
		ksft_print_msg("mremap shrink failed unexpectedly: %s\n",
			       strerror(errno));
		rc = KSFT_FAIL;
	} else {
		ksft_print_msg("%s step successful\n", tcase->name);
	}

	/* clean up */
	if (munmap(addr2 == MAP_FAILED ? addr : addr2,
		   addr2 == MAP_FAILED ? ps * old_pages : ps * new_pages)) {
		ksft_print_msg("munmap failed: %s\n", strerror(errno));
		rc = KSFT_FAIL;
	}

out:
	if (fd >= 0) {
		close(fd);
		if (tcase->pathname && strstr(tcase->pathname, "XXXXXX") &&
		    unlink(pb)) {
			ksft_print_msg("unlink failed for %s: %s\n", pb, strerror(errno));
			rc = KSFT_FAIL;
		}
	}

	ksft_test_result_report(rc, "%s\n", tcase->name);
	return rc;
}

int main(int argc, char **argv)
{
	int i;

	nommu = ksft_is_nommu();
	if (nommu < 0)
		ksft_exit_skip("Cannot read /proc/meminfo; mount procfs at /proc\n");

	ps = sysconf(_SC_PAGESIZE);
	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(mremap_cases) + 2);

	munmap_shrink_test();
	mremap_shrink_test();

	for (i = 0; i < (int)ARRAY_SIZE(mremap_cases); i++)
		run_mremap_test(&mremap_cases[i]);

	ksft_finished();
}
