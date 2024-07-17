// SPDX-License-Identifier: GPL-2.0
#include <dirent.h>
#include <stdlib.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <api/fs/fs.h>
#include "dso.h"
#include "dsos.h"
#include "machine.h"
#include "symbol.h"
#include "tests.h"
#include "debug.h"

static char *test_file(int size)
{
#define TEMPL "/tmp/perf-test-XXXXXX"
	static char buf_templ[sizeof(TEMPL)];
	char *templ = buf_templ;
	int fd, i;
	unsigned char *buf;

	strcpy(buf_templ, TEMPL);
#undef TEMPL

	fd = mkstemp(templ);
	if (fd < 0) {
		perror("mkstemp failed");
		return NULL;
	}

	buf = malloc(size);
	if (!buf) {
		close(fd);
		return NULL;
	}

	for (i = 0; i < size; i++)
		buf[i] = (unsigned char) ((int) i % 10);

	if (size != write(fd, buf, size))
		templ = NULL;

	free(buf);
	close(fd);
	return templ;
}

#define TEST_FILE_SIZE (DSO__DATA_CACHE_SIZE * 20)

struct test_data_offset {
	off_t offset;
	u8 data[10];
	int size;
};

struct test_data_offset offsets[] = {
	/* Fill first cache page. */
	{
		.offset = 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read first cache page. */
	{
		.offset = 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Fill cache boundary pages. */
	{
		.offset = DSO__DATA_CACHE_SIZE - DSO__DATA_CACHE_SIZE % 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read cache boundary pages. */
	{
		.offset = DSO__DATA_CACHE_SIZE - DSO__DATA_CACHE_SIZE % 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Fill final cache page. */
	{
		.offset = TEST_FILE_SIZE - 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read final cache page. */
	{
		.offset = TEST_FILE_SIZE - 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read final cache page. */
	{
		.offset = TEST_FILE_SIZE - 3,
		.data   = { 7, 8, 9, 0, 0, 0, 0, 0, 0, 0 },
		.size   = 3,
	},
};

/* move it from util/dso.c for compatibility */
static int dso__data_fd(struct dso *dso, struct machine *machine)
{
	int fd = dso__data_get_fd(dso, machine);

	if (fd >= 0)
		dso__data_put_fd(dso);

	return fd;
}

static int test__dso_data(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct machine machine;
	struct dso *dso;
	char *file = test_file(TEST_FILE_SIZE);
	size_t i;

	TEST_ASSERT_VAL("No test file", file);

	memset(&machine, 0, sizeof(machine));
	dsos__init(&machine.dsos);

	dso = dso__new(file);
	TEST_ASSERT_VAL("Failed to add dso", !dsos__add(&machine.dsos, dso));
	TEST_ASSERT_VAL("Failed to access to dso",
			dso__data_fd(dso, &machine) >= 0);

	/* Basic 10 bytes tests. */
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		struct test_data_offset *data = &offsets[i];
		ssize_t size;
		u8 buf[10];

		memset(buf, 0, 10);
		size = dso__data_read_offset(dso, &machine, data->offset,
				     buf, 10);

		TEST_ASSERT_VAL("Wrong size", size == data->size);
		TEST_ASSERT_VAL("Wrong data", !memcmp(buf, data->data, 10));
	}

	/* Read cross multiple cache pages. */
	{
		ssize_t size;
		int c;
		u8 *buf;

		buf = malloc(TEST_FILE_SIZE);
		TEST_ASSERT_VAL("ENOMEM\n", buf);

		/* First iteration to fill caches, second one to read them. */
		for (c = 0; c < 2; c++) {
			memset(buf, 0, TEST_FILE_SIZE);
			size = dso__data_read_offset(dso, &machine, 10,
						     buf, TEST_FILE_SIZE);

			TEST_ASSERT_VAL("Wrong size",
				size == (TEST_FILE_SIZE - 10));

			for (i = 0; i < (size_t)size; i++)
				TEST_ASSERT_VAL("Wrong data",
					buf[i] == (i % 10));
		}

		free(buf);
	}

	dso__put(dso);
	dsos__exit(&machine.dsos);
	unlink(file);
	return 0;
}

static long open_files_cnt(void)
{
	char path[PATH_MAX];
	struct dirent *dent;
	DIR *dir;
	long nr = 0;

	scnprintf(path, PATH_MAX, "%s/self/fd", procfs__mountpoint());
	pr_debug("fd path: %s\n", path);

	dir = opendir(path);
	TEST_ASSERT_VAL("failed to open fd directory", dir);

	while ((dent = readdir(dir)) != NULL) {
		if (!strcmp(dent->d_name, ".") ||
		    !strcmp(dent->d_name, ".."))
			continue;

		nr++;
	}

	closedir(dir);
	return nr - 1;
}

static int dsos__create(int cnt, int size, struct dsos *dsos)
{
	int i;

	dsos__init(dsos);

	for (i = 0; i < cnt; i++) {
		struct dso *dso;
		char *file = test_file(size);

		TEST_ASSERT_VAL("failed to get dso file", file);
		dso = dso__new(file);
		TEST_ASSERT_VAL("failed to get dso", dso);
		TEST_ASSERT_VAL("failed to add dso", !dsos__add(dsos, dso));
		dso__put(dso);
	}

	return 0;
}

static void dsos__delete(struct dsos *dsos)
{
	for (unsigned int i = 0; i < dsos->cnt; i++) {
		struct dso *dso = dsos->dsos[i];

		dso__data_close(dso);
		unlink(dso__name(dso));
	}
	dsos__exit(dsos);
}

static int set_fd_limit(int n)
{
	struct rlimit rlim;

	if (getrlimit(RLIMIT_NOFILE, &rlim))
		return -1;

	pr_debug("file limit %ld, new %d\n", (long) rlim.rlim_cur, n);

	rlim.rlim_cur = n;
	return setrlimit(RLIMIT_NOFILE, &rlim);
}

static int test__dso_data_cache(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct machine machine;
	long nr_end, nr = open_files_cnt();
	int dso_cnt, limit, i, fd;

	/* Rest the internal dso open counter limit. */
	reset_fd_limit();

	memset(&machine, 0, sizeof(machine));

	/* set as system limit */
	limit = nr * 4;
	TEST_ASSERT_VAL("failed to set file limit", !set_fd_limit(limit));

	/* and this is now our dso open FDs limit */
	dso_cnt = limit / 2;
	TEST_ASSERT_VAL("failed to create dsos\n",
			!dsos__create(dso_cnt, TEST_FILE_SIZE, &machine.dsos));

	for (i = 0; i < (dso_cnt - 1); i++) {
		struct dso *dso = machine.dsos.dsos[i];

		/*
		 * Open dsos via dso__data_fd(), it opens the data
		 * file and keep it open (unless open file limit).
		 */
		fd = dso__data_fd(dso, &machine);
		TEST_ASSERT_VAL("failed to get fd", fd > 0);

		if (i % 2) {
			#define BUFSIZE 10
			u8 buf[BUFSIZE];
			ssize_t n;

			n = dso__data_read_offset(dso, &machine, 0, buf, BUFSIZE);
			TEST_ASSERT_VAL("failed to read dso", n == BUFSIZE);
		}
	}

	/* verify the first one is already open */
	TEST_ASSERT_VAL("dsos[0] is not open", dso__data(machine.dsos.dsos[0])->fd != -1);

	/* open +1 dso to reach the allowed limit */
	fd = dso__data_fd(machine.dsos.dsos[i], &machine);
	TEST_ASSERT_VAL("failed to get fd", fd > 0);

	/* should force the first one to be closed */
	TEST_ASSERT_VAL("failed to close dsos[0]", dso__data(machine.dsos.dsos[0])->fd == -1);

	/* cleanup everything */
	dsos__delete(&machine.dsos);

	/* Make sure we did not leak any file descriptor. */
	nr_end = open_files_cnt();
	pr_debug("nr start %ld, nr stop %ld\n", nr, nr_end);
	TEST_ASSERT_VAL("failed leaking files", nr == nr_end);
	return 0;
}

static long new_limit(int count)
{
	int fd = open("/dev/null", O_RDONLY);
	long ret = fd;
	if (count > 0)
		ret = new_limit(--count);
	close(fd);
	return ret;
}

static int test__dso_data_reopen(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct machine machine;
	long nr_end, nr = open_files_cnt(), lim = new_limit(3);
	int fd, fd_extra;

#define dso_0 (machine.dsos.dsos[0])
#define dso_1 (machine.dsos.dsos[1])
#define dso_2 (machine.dsos.dsos[2])

	/* Rest the internal dso open counter limit. */
	reset_fd_limit();

	memset(&machine, 0, sizeof(machine));

	/*
	 * Test scenario:
	 * - create 3 dso objects
	 * - set process file descriptor limit to current
	 *   files count + 3
	 * - test that the first dso gets closed when we
	 *   reach the files count limit
	 */

	/* Make sure we are able to open 3 fds anyway */
	TEST_ASSERT_VAL("failed to set file limit",
			!set_fd_limit((lim)));

	TEST_ASSERT_VAL("failed to create dsos\n",
			!dsos__create(3, TEST_FILE_SIZE, &machine.dsos));

	/* open dso_0 */
	fd = dso__data_fd(dso_0, &machine);
	TEST_ASSERT_VAL("failed to get fd", fd > 0);

	/* open dso_1 */
	fd = dso__data_fd(dso_1, &machine);
	TEST_ASSERT_VAL("failed to get fd", fd > 0);

	/*
	 * open extra file descriptor and we just
	 * reached the files count limit
	 */
	fd_extra = open("/dev/null", O_RDONLY);
	TEST_ASSERT_VAL("failed to open extra fd", fd_extra > 0);

	/* open dso_2 */
	fd = dso__data_fd(dso_2, &machine);
	TEST_ASSERT_VAL("failed to get fd", fd > 0);

	/*
	 * dso_0 should get closed, because we reached
	 * the file descriptor limit
	 */
	TEST_ASSERT_VAL("failed to close dso_0", dso__data(dso_0)->fd == -1);

	/* open dso_0 */
	fd = dso__data_fd(dso_0, &machine);
	TEST_ASSERT_VAL("failed to get fd", fd > 0);

	/*
	 * dso_1 should get closed, because we reached
	 * the file descriptor limit
	 */
	TEST_ASSERT_VAL("failed to close dso_1", dso__data(dso_1)->fd == -1);

	/* cleanup everything */
	close(fd_extra);
	dsos__delete(&machine.dsos);

	/* Make sure we did not leak any file descriptor. */
	nr_end = open_files_cnt();
	pr_debug("nr start %ld, nr stop %ld\n", nr, nr_end);
	TEST_ASSERT_VAL("failed leaking files", nr == nr_end);
	return 0;
}


static struct test_case tests__dso_data[] = {
	TEST_CASE("read", dso_data),
	TEST_CASE("cache", dso_data_cache),
	TEST_CASE("reopen", dso_data_reopen),
	{	.name = NULL, }
};

struct test_suite suite__dso_data = {
	.desc = "DSO data tests",
	.test_cases = tests__dso_data,
};
