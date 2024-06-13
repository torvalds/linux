// SPDX-License-Identifier: GPL-2.0-only
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "tests.h"
#include <api/io.h>
#include <linux/kernel.h>

#define TEMPL "/tmp/perf-test-XXXXXX"

#define EXPECT_EQUAL(val, expected)                             \
do {								\
	if (val != expected) {					\
		pr_debug("%s:%d: %d != %d\n",			\
			__FILE__, __LINE__, val, expected);	\
		ret = -1;					\
	}							\
} while (0)

#define EXPECT_EQUAL64(val, expected)                           \
do {								\
	if (val != expected) {					\
		pr_debug("%s:%d: %lld != %lld\n",		\
			__FILE__, __LINE__, val, expected);	\
		ret = -1;					\
	}							\
} while (0)

static int make_test_file(char path[PATH_MAX], const char *contents)
{
	ssize_t contents_len = strlen(contents);
	int fd;

	strcpy(path, TEMPL);
	fd = mkstemp(path);
	if (fd < 0) {
		pr_debug("mkstemp failed");
		return -1;
	}
	if (write(fd, contents, contents_len) < contents_len) {
		pr_debug("short write");
		close(fd);
		unlink(path);
		return -1;
	}
	close(fd);
	return 0;
}

static int setup_test(char path[PATH_MAX], const char *contents,
		      size_t buf_size, struct io *io)
{
	if (make_test_file(path, contents))
		return -1;

	io->fd = open(path, O_RDONLY);
	if (io->fd < 0) {
		pr_debug("Failed to open '%s'\n", path);
		unlink(path);
		return -1;
	}
	io->buf = malloc(buf_size);
	if (io->buf == NULL) {
		pr_debug("Failed to allocate memory");
		close(io->fd);
		unlink(path);
		return -1;
	}
	io__init(io, io->fd, io->buf, buf_size);
	return 0;
}

static void cleanup_test(char path[PATH_MAX], struct io *io)
{
	free(io->buf);
	close(io->fd);
	unlink(path);
}

static int do_test_get_char(const char *test_string, size_t buf_size)
{
	char path[PATH_MAX];
	struct io io;
	int ch, ret = 0;
	size_t i;

	if (setup_test(path, test_string, buf_size, &io))
		return -1;

	for (i = 0; i < strlen(test_string); i++) {
		ch = io__get_char(&io);

		EXPECT_EQUAL(ch, test_string[i]);
		EXPECT_EQUAL(io.eof, false);
	}
	ch = io__get_char(&io);
	EXPECT_EQUAL(ch, -1);
	EXPECT_EQUAL(io.eof, true);

	cleanup_test(path, &io);
	return ret;
}

static int test_get_char(void)
{
	int i, ret = 0;
	size_t j;

	static const char *const test_strings[] = {
		"12345678abcdef90",
		"a\nb\nc\nd\n",
		"\a\b\t\v\f\r",
	};
	for (i = 0; i <= 10; i++) {
		for (j = 0; j < ARRAY_SIZE(test_strings); j++) {
			if (do_test_get_char(test_strings[j], 1 << i))
				ret = -1;
		}
	}
	return ret;
}

static int do_test_get_hex(const char *test_string,
			__u64 val1, int ch1,
			__u64 val2, int ch2,
			__u64 val3, int ch3,
			bool end_eof)
{
	char path[PATH_MAX];
	struct io io;
	int ch, ret = 0;
	__u64 hex;

	if (setup_test(path, test_string, 4, &io))
		return -1;

	ch = io__get_hex(&io, &hex);
	EXPECT_EQUAL64(hex, val1);
	EXPECT_EQUAL(ch, ch1);

	ch = io__get_hex(&io, &hex);
	EXPECT_EQUAL64(hex, val2);
	EXPECT_EQUAL(ch, ch2);

	ch = io__get_hex(&io, &hex);
	EXPECT_EQUAL64(hex, val3);
	EXPECT_EQUAL(ch, ch3);

	EXPECT_EQUAL(io.eof, end_eof);

	cleanup_test(path, &io);
	return ret;
}

static int test_get_hex(void)
{
	int ret = 0;

	if (do_test_get_hex("12345678abcdef90",
				0x12345678abcdef90, -1,
				0, -1,
				0, -1,
				true))
		ret = -1;

	if (do_test_get_hex("1\n2\n3\n",
				1, '\n',
				2, '\n',
				3, '\n',
				false))
		ret = -1;

	if (do_test_get_hex("12345678ABCDEF90;a;b",
				0x12345678abcdef90, ';',
				0xa, ';',
				0xb, -1,
				true))
		ret = -1;

	if (do_test_get_hex("0x1x2x",
				0, 'x',
				1, 'x',
				2, 'x',
				false))
		ret = -1;

	if (do_test_get_hex("x1x",
				0, -2,
				1, 'x',
				0, -1,
				true))
		ret = -1;

	if (do_test_get_hex("10000000000000000000000000000abcdefgh99i",
				0xabcdef, 'g',
				0, -2,
				0x99, 'i',
				false))
		ret = -1;

	return ret;
}

static int do_test_get_dec(const char *test_string,
			__u64 val1, int ch1,
			__u64 val2, int ch2,
			__u64 val3, int ch3,
			bool end_eof)
{
	char path[PATH_MAX];
	struct io io;
	int ch, ret = 0;
	__u64 dec;

	if (setup_test(path, test_string, 4, &io))
		return -1;

	ch = io__get_dec(&io, &dec);
	EXPECT_EQUAL64(dec, val1);
	EXPECT_EQUAL(ch, ch1);

	ch = io__get_dec(&io, &dec);
	EXPECT_EQUAL64(dec, val2);
	EXPECT_EQUAL(ch, ch2);

	ch = io__get_dec(&io, &dec);
	EXPECT_EQUAL64(dec, val3);
	EXPECT_EQUAL(ch, ch3);

	EXPECT_EQUAL(io.eof, end_eof);

	cleanup_test(path, &io);
	return ret;
}

static int test_get_dec(void)
{
	int ret = 0;

	if (do_test_get_dec("12345678abcdef90",
				12345678, 'a',
				0, -2,
				0, -2,
				false))
		ret = -1;

	if (do_test_get_dec("1\n2\n3\n",
				1, '\n',
				2, '\n',
				3, '\n',
				false))
		ret = -1;

	if (do_test_get_dec("12345678;1;2",
				12345678, ';',
				1, ';',
				2, -1,
				true))
		ret = -1;

	if (do_test_get_dec("0x1x2x",
				0, 'x',
				1, 'x',
				2, 'x',
				false))
		ret = -1;

	if (do_test_get_dec("x1x",
				0, -2,
				1, 'x',
				0, -1,
				true))
		ret = -1;

	if (do_test_get_dec("10000000000000000000000000000000000000000000000000000000000123456789ab99c",
				123456789, 'a',
				0, -2,
				99, 'c',
				false))
		ret = -1;

	return ret;
}

static int test__api_io(struct test_suite *test __maybe_unused,
			int subtest __maybe_unused)
{
	int ret = 0;

	if (test_get_char())
		ret = TEST_FAIL;
	if (test_get_hex())
		ret = TEST_FAIL;
	if (test_get_dec())
		ret = TEST_FAIL;
	return ret;
}

DEFINE_SUITE("Test api io", api_io);
