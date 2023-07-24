// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <linux/kernel.h>
#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "../kselftest.h"

static const char * const dev_files[] = {
	"/dev/zero", "/dev/null", "/dev/urandom",
	"/proc/version", "/proc"
};
static const int cachestat_nr = 451;

void print_cachestat(struct cachestat *cs)
{
	ksft_print_msg(
	"Using cachestat: Cached: %lu, Dirty: %lu, Writeback: %lu, Evicted: %lu, Recently Evicted: %lu\n",
	cs->nr_cache, cs->nr_dirty, cs->nr_writeback,
	cs->nr_evicted, cs->nr_recently_evicted);
}

bool write_exactly(int fd, size_t filesize)
{
	int random_fd = open("/dev/urandom", O_RDONLY);
	char *cursor, *data;
	int remained;
	bool ret;

	if (random_fd < 0) {
		ksft_print_msg("Unable to access urandom.\n");
		ret = false;
		goto out;
	}

	data = malloc(filesize);
	if (!data) {
		ksft_print_msg("Unable to allocate data.\n");
		ret = false;
		goto close_random_fd;
	}

	remained = filesize;
	cursor = data;

	while (remained) {
		ssize_t read_len = read(random_fd, cursor, remained);

		if (read_len <= 0) {
			ksft_print_msg("Unable to read from urandom.\n");
			ret = false;
			goto out_free_data;
		}

		remained -= read_len;
		cursor += read_len;
	}

	/* write random data to fd */
	remained = filesize;
	cursor = data;
	while (remained) {
		ssize_t write_len = write(fd, cursor, remained);

		if (write_len <= 0) {
			ksft_print_msg("Unable write random data to file.\n");
			ret = false;
			goto out_free_data;
		}

		remained -= write_len;
		cursor += write_len;
	}

	ret = true;
out_free_data:
	free(data);
close_random_fd:
	close(random_fd);
out:
	return ret;
}

/*
 * Open/create the file at filename, (optionally) write random data to it
 * (exactly num_pages), then test the cachestat syscall on this file.
 *
 * If test_fsync == true, fsync the file, then check the number of dirty
 * pages.
 */
bool test_cachestat(const char *filename, bool write_random, bool create,
		bool test_fsync, unsigned long num_pages, int open_flags,
		mode_t open_mode)
{
	size_t PS = sysconf(_SC_PAGESIZE);
	int filesize = num_pages * PS;
	bool ret = true;
	long syscall_ret;
	struct cachestat cs;
	struct cachestat_range cs_range = { 0, filesize };

	int fd = open(filename, open_flags, open_mode);

	if (fd == -1) {
		ksft_print_msg("Unable to create/open file.\n");
		ret = false;
		goto out;
	} else {
		ksft_print_msg("Create/open %s\n", filename);
	}

	if (write_random) {
		if (!write_exactly(fd, filesize)) {
			ksft_print_msg("Unable to access urandom.\n");
			ret = false;
			goto out1;
		}
	}

	syscall_ret = syscall(cachestat_nr, fd, &cs_range, &cs, 0);

	ksft_print_msg("Cachestat call returned %ld\n", syscall_ret);

	if (syscall_ret) {
		ksft_print_msg("Cachestat returned non-zero.\n");
		ret = false;
		goto out1;

	} else {
		print_cachestat(&cs);

		if (write_random) {
			if (cs.nr_cache + cs.nr_evicted != num_pages) {
				ksft_print_msg(
					"Total number of cached and evicted pages is off.\n");
				ret = false;
			}
		}
	}

	if (test_fsync) {
		if (fsync(fd)) {
			ksft_print_msg("fsync fails.\n");
			ret = false;
		} else {
			syscall_ret = syscall(cachestat_nr, fd, &cs_range, &cs, 0);

			ksft_print_msg("Cachestat call (after fsync) returned %ld\n",
				syscall_ret);

			if (!syscall_ret) {
				print_cachestat(&cs);

				if (cs.nr_dirty) {
					ret = false;
					ksft_print_msg(
						"Number of dirty should be zero after fsync.\n");
				}
			} else {
				ksft_print_msg("Cachestat (after fsync) returned non-zero.\n");
				ret = false;
				goto out1;
			}
		}
	}

out1:
	close(fd);

	if (create)
		remove(filename);
out:
	return ret;
}

bool test_cachestat_shmem(void)
{
	size_t PS = sysconf(_SC_PAGESIZE);
	size_t filesize = PS * 512 * 2; /* 2 2MB huge pages */
	int syscall_ret;
	size_t compute_len = PS * 512;
	struct cachestat_range cs_range = { PS, compute_len };
	char *filename = "tmpshmcstat";
	struct cachestat cs;
	bool ret = true;
	unsigned long num_pages = compute_len / PS;
	int fd = shm_open(filename, O_CREAT | O_RDWR, 0600);

	if (fd < 0) {
		ksft_print_msg("Unable to create shmem file.\n");
		ret = false;
		goto out;
	}

	if (ftruncate(fd, filesize)) {
		ksft_print_msg("Unable to truncate shmem file.\n");
		ret = false;
		goto close_fd;
	}

	if (!write_exactly(fd, filesize)) {
		ksft_print_msg("Unable to write to shmem file.\n");
		ret = false;
		goto close_fd;
	}

	syscall_ret = syscall(cachestat_nr, fd, &cs_range, &cs, 0);

	if (syscall_ret) {
		ksft_print_msg("Cachestat returned non-zero.\n");
		ret = false;
		goto close_fd;
	} else {
		print_cachestat(&cs);
		if (cs.nr_cache + cs.nr_evicted != num_pages) {
			ksft_print_msg(
				"Total number of cached and evicted pages is off.\n");
			ret = false;
		}
	}

close_fd:
	shm_unlink(filename);
out:
	return ret;
}

int main(void)
{
	int ret = 0;

	for (int i = 0; i < 5; i++) {
		const char *dev_filename = dev_files[i];

		if (test_cachestat(dev_filename, false, false, false,
			4, O_RDONLY, 0400))
			ksft_test_result_pass("cachestat works with %s\n", dev_filename);
		else {
			ksft_test_result_fail("cachestat fails with %s\n", dev_filename);
			ret = 1;
		}
	}

	if (test_cachestat("tmpfilecachestat", true, true,
		true, 4, O_CREAT | O_RDWR, 0400 | 0600))
		ksft_test_result_pass("cachestat works with a normal file\n");
	else {
		ksft_test_result_fail("cachestat fails with normal file\n");
		ret = 1;
	}

	if (test_cachestat_shmem())
		ksft_test_result_pass("cachestat works with a shmem file\n");
	else {
		ksft_test_result_fail("cachestat fails with a shmem file\n");
		ret = 1;
	}

	return ret;
}
