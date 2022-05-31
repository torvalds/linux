// SPDX-License-Identifier: GPL-2.0
/*
 * selftest for sparc64's privileged ADI driver
 *
 * Author: Tom Hromatka <tom.hromatka@oracle.com>
 */
#include <linux/kernel.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../kselftest.h"

#define DEBUG_LEVEL_1_BIT	(0x0001)
#define DEBUG_LEVEL_2_BIT	(0x0002)
#define DEBUG_LEVEL_3_BIT	(0x0004)
#define DEBUG_LEVEL_4_BIT	(0x0008)
#define DEBUG_TIMING_BIT	(0x1000)

/* bit mask of enabled bits to print */
#define DEBUG 0x0001

#define DEBUG_PRINT_L1(...)	debug_print(DEBUG_LEVEL_1_BIT, __VA_ARGS__)
#define DEBUG_PRINT_L2(...)	debug_print(DEBUG_LEVEL_2_BIT, __VA_ARGS__)
#define DEBUG_PRINT_L3(...)	debug_print(DEBUG_LEVEL_3_BIT, __VA_ARGS__)
#define DEBUG_PRINT_L4(...)	debug_print(DEBUG_LEVEL_4_BIT, __VA_ARGS__)
#define DEBUG_PRINT_T(...)	debug_print(DEBUG_TIMING_BIT, __VA_ARGS__)

static void debug_print(int level, const char *s, ...)
{
	va_list args;

	va_start(args, s);

	if (DEBUG & level)
		vfprintf(stdout, s, args);
	va_end(args);
}

#ifndef min
#define min(x, y) ((x) < (y) ? x : y)
#endif

#define RETURN_FROM_TEST(_ret) \
	do { \
		DEBUG_PRINT_L1( \
			"\tTest %s returned %d\n", __func__, _ret); \
		return _ret; \
	} while (0)

#define ADI_BLKSZ	64
#define ADI_MAX_VERSION	15

#define TEST_STEP_FAILURE(_ret) \
	do { \
		fprintf(stderr, "\tTest step failure: %d at %s:%d\n", \
			_ret, __func__, __LINE__); \
		goto out; \
	} while (0)

#define RDTICK(_x) \
	asm volatile(" rd %%tick, %0\n" : "=r" (_x))

static int random_version(void)
{
	long tick;

	RDTICK(tick);

	return tick % (ADI_MAX_VERSION + 1);
}

#define MAX_RANGES_SUPPORTED	5
static const char system_ram_str[] = "System RAM\n";
static int range_count;
static unsigned long long int start_addr[MAX_RANGES_SUPPORTED];
static unsigned long long int   end_addr[MAX_RANGES_SUPPORTED];

struct stats {
	char		name[16];
	unsigned long	total;
	unsigned long	count;
	unsigned long	bytes;
};

static struct stats read_stats = {
	.name = "read", .total = 0, .count = 0, .bytes = 0};
static struct stats pread_stats = {
	.name = "pread", .total = 0, .count = 0, .bytes = 0};
static struct stats write_stats = {
	.name = "write", .total = 0, .count = 0, .bytes = 0};
static struct stats pwrite_stats = {
	.name = "pwrite", .total = 0, .count = 0, .bytes = 0};
static struct stats seek_stats = {
	.name = "seek", .total = 0, .count = 0, .bytes = 0};

static void update_stats(struct stats * const ustats,
			 unsigned long measurement, unsigned long bytes)
{
	ustats->total += measurement;
	ustats->bytes += bytes;
	ustats->count++;
}

static void print_ustats(const struct stats * const ustats)
{
	DEBUG_PRINT_L1("%s\t%7d\t%7.0f\t%7.0f\n",
		       ustats->name, ustats->count,
		       (float)ustats->total / (float)ustats->count,
		       (float)ustats->bytes / (float)ustats->count);
}

static void print_stats(void)
{
	DEBUG_PRINT_L1("\nSyscall\tCall\tAvgTime\tAvgSize\n"
		       "\tCount\t(ticks)\t(bytes)\n"
		       "-------------------------------\n");

	print_ustats(&read_stats);
	print_ustats(&pread_stats);
	print_ustats(&write_stats);
	print_ustats(&pwrite_stats);
	print_ustats(&seek_stats);
}

static int build_memory_map(void)
{
	char line[256];
	FILE *fp;
	int i;

	range_count = 0;

	fp = fopen("/proc/iomem", "r");
	if (!fp) {
		fprintf(stderr, "/proc/iomem: error %d: %s\n",
			errno, strerror(errno));
		return -errno;
	}

	while (fgets(line, sizeof(line), fp) != 0) {
		if (strstr(line, system_ram_str)) {
			char *dash, *end_ptr;

			/* Given a line like this:
			 * d0400000-10ffaffff : System RAM
			 * replace the "-" with a space
			 */
			dash = strstr(line, "-");
			dash[0] = 0x20;

			start_addr[range_count] = strtoull(line, &end_ptr, 16);
			end_addr[range_count] = strtoull(end_ptr, NULL, 16);
			range_count++;
		}
	}

	fclose(fp);

	DEBUG_PRINT_L1("RAM Ranges\n");
	for (i = 0; i < range_count; i++)
		DEBUG_PRINT_L1("\trange %d: 0x%llx\t- 0x%llx\n",
			       i, start_addr[i], end_addr[i]);

	if (range_count == 0) {
		fprintf(stderr, "No valid address ranges found.  Error.\n");
		return -1;
	}

	return 0;
}

static int read_adi(int fd, unsigned char *buf, int buf_sz)
{
	int ret, bytes_read = 0;
	long start, end, elapsed_time = 0;

	do {
		RDTICK(start);
		ret = read(fd, buf + bytes_read, buf_sz - bytes_read);
		RDTICK(end);
		if (ret < 0)
			return -errno;

		elapsed_time += end - start;
		update_stats(&read_stats, elapsed_time, buf_sz);
		bytes_read += ret;

	} while (bytes_read < buf_sz);

	DEBUG_PRINT_T("\tread elapsed timed = %ld\n", elapsed_time);
	DEBUG_PRINT_L3("\tRead  %d bytes\n", bytes_read);

	return bytes_read;
}

static int pread_adi(int fd, unsigned char *buf,
		     int buf_sz, unsigned long offset)
{
	int ret, i, bytes_read = 0;
	unsigned long cur_offset;
	long start, end, elapsed_time = 0;

	cur_offset = offset;
	do {
		RDTICK(start);
		ret = pread(fd, buf + bytes_read, buf_sz - bytes_read,
			    cur_offset);
		RDTICK(end);
		if (ret < 0)
			return -errno;

		elapsed_time += end - start;
		update_stats(&pread_stats, elapsed_time, buf_sz);
		bytes_read += ret;
		cur_offset += ret;

	} while (bytes_read < buf_sz);

	DEBUG_PRINT_T("\tpread elapsed timed = %ld\n", elapsed_time);
	DEBUG_PRINT_L3("\tRead  %d bytes starting at offset 0x%lx\n",
		       bytes_read, offset);
	for (i = 0; i < bytes_read; i++)
		DEBUG_PRINT_L4("\t\t0x%lx\t%d\n", offset + i, buf[i]);

	return bytes_read;
}

static int write_adi(int fd, const unsigned char * const buf, int buf_sz)
{
	int ret, bytes_written = 0;
	long start, end, elapsed_time = 0;

	do {
		RDTICK(start);
		ret = write(fd, buf + bytes_written, buf_sz - bytes_written);
		RDTICK(end);
		if (ret < 0)
			return -errno;

		elapsed_time += (end - start);
		update_stats(&write_stats, elapsed_time, buf_sz);
		bytes_written += ret;
	} while (bytes_written < buf_sz);

	DEBUG_PRINT_T("\twrite elapsed timed = %ld\n", elapsed_time);
	DEBUG_PRINT_L3("\tWrote %d of %d bytes\n", bytes_written, buf_sz);

	return bytes_written;
}

static int pwrite_adi(int fd, const unsigned char * const buf,
		      int buf_sz, unsigned long offset)
{
	int ret, bytes_written = 0;
	unsigned long cur_offset;
	long start, end, elapsed_time = 0;

	cur_offset = offset;

	do {
		RDTICK(start);
		ret = pwrite(fd, buf + bytes_written,
			     buf_sz - bytes_written, cur_offset);
		RDTICK(end);
		if (ret < 0) {
			fprintf(stderr, "pwrite(): error %d: %s\n",
				errno, strerror(errno));
			return -errno;
		}

		elapsed_time += (end - start);
		update_stats(&pwrite_stats, elapsed_time, buf_sz);
		bytes_written += ret;
		cur_offset += ret;

	} while (bytes_written < buf_sz);

	DEBUG_PRINT_T("\tpwrite elapsed timed = %ld\n", elapsed_time);
	DEBUG_PRINT_L3("\tWrote %d of %d bytes starting at address 0x%lx\n",
		       bytes_written, buf_sz, offset);

	return bytes_written;
}

static off_t seek_adi(int fd, off_t offset, int whence)
{
	long start, end;
	off_t ret;

	RDTICK(start);
	ret = lseek(fd, offset, whence);
	RDTICK(end);
	DEBUG_PRINT_L2("\tlseek ret = 0x%llx\n", ret);
	if (ret < 0)
		goto out;

	DEBUG_PRINT_T("\tlseek elapsed timed = %ld\n", end - start);
	update_stats(&seek_stats, end - start, 0);

out:
	(void)lseek(fd, 0, SEEK_END);
	return ret;
}

static int test0_prpw_aligned_1byte(int fd)
{
	/* somewhat arbitrarily chosen address */
	unsigned long paddr =
		(end_addr[range_count - 1] - 0x1000) & ~(ADI_BLKSZ - 1);
	unsigned char version[1], expected_version;
	loff_t offset;
	int ret;

	version[0] = random_version();
	expected_version = version[0];

	offset = paddr / ADI_BLKSZ;

	ret = pwrite_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	ret = pread_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	if (expected_version != version[0]) {
		DEBUG_PRINT_L2("\tExpected version %d but read version %d\n",
			       expected_version, version[0]);
		TEST_STEP_FAILURE(-expected_version);
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

#define TEST1_VERSION_SZ	4096
static int test1_prpw_aligned_4096bytes(int fd)
{
	/* somewhat arbitrarily chosen address */
	unsigned long paddr =
		(end_addr[range_count - 1] - 0x6000) & ~(ADI_BLKSZ - 1);
	unsigned char version[TEST1_VERSION_SZ],
		expected_version[TEST1_VERSION_SZ];
	loff_t offset;
	int ret, i;

	for (i = 0; i < TEST1_VERSION_SZ; i++) {
		version[i] = random_version();
		expected_version[i] = version[i];
	}

	offset = paddr / ADI_BLKSZ;

	ret = pwrite_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	ret = pread_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	for (i = 0; i < TEST1_VERSION_SZ; i++) {
		if (expected_version[i] != version[i]) {
			DEBUG_PRINT_L2(
				"\tExpected version %d but read version %d\n",
				expected_version, version[0]);
			TEST_STEP_FAILURE(-expected_version[i]);
		}
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

#define TEST2_VERSION_SZ	10327
static int test2_prpw_aligned_10327bytes(int fd)
{
	/* somewhat arbitrarily chosen address */
	unsigned long paddr =
		(start_addr[0] + 0x6000) & ~(ADI_BLKSZ - 1);
	unsigned char version[TEST2_VERSION_SZ],
		expected_version[TEST2_VERSION_SZ];
	loff_t offset;
	int ret, i;

	for (i = 0; i < TEST2_VERSION_SZ; i++) {
		version[i] = random_version();
		expected_version[i] = version[i];
	}

	offset = paddr / ADI_BLKSZ;

	ret = pwrite_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	ret = pread_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	for (i = 0; i < TEST2_VERSION_SZ; i++) {
		if (expected_version[i] != version[i]) {
			DEBUG_PRINT_L2(
				"\tExpected version %d but read version %d\n",
				expected_version, version[0]);
			TEST_STEP_FAILURE(-expected_version[i]);
		}
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

#define TEST3_VERSION_SZ	12541
static int test3_prpw_unaligned_12541bytes(int fd)
{
	/* somewhat arbitrarily chosen address */
	unsigned long paddr =
		((start_addr[0] + 0xC000) & ~(ADI_BLKSZ - 1)) + 17;
	unsigned char version[TEST3_VERSION_SZ],
		expected_version[TEST3_VERSION_SZ];
	loff_t offset;
	int ret, i;

	for (i = 0; i < TEST3_VERSION_SZ; i++) {
		version[i] = random_version();
		expected_version[i] = version[i];
	}

	offset = paddr / ADI_BLKSZ;

	ret = pwrite_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	ret = pread_adi(fd, version, sizeof(version), offset);
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	for (i = 0; i < TEST3_VERSION_SZ; i++) {
		if (expected_version[i] != version[i]) {
			DEBUG_PRINT_L2(
				"\tExpected version %d but read version %d\n",
				expected_version, version[0]);
			TEST_STEP_FAILURE(-expected_version[i]);
		}
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

static int test4_lseek(int fd)
{
#define	OFFSET_ADD	(0x100)
#define OFFSET_SUBTRACT	(0xFFFFFFF000000000)

	off_t offset_out, offset_in;
	int ret;


	offset_in = 0x123456789abcdef0;
	offset_out = seek_adi(fd, offset_in, SEEK_SET);
	if (offset_out != offset_in) {
		ret = -1;
		TEST_STEP_FAILURE(ret);
	}

	/* seek to the current offset.  this should return EINVAL */
	offset_out = seek_adi(fd, offset_in, SEEK_SET);
	if (offset_out < 0 && errno == EINVAL)
		DEBUG_PRINT_L2(
			"\tSEEK_SET failed as designed. Not an error\n");
	else {
		ret = -2;
		TEST_STEP_FAILURE(ret);
	}

	offset_out = seek_adi(fd, 0, SEEK_CUR);
	if (offset_out != offset_in) {
		ret = -3;
		TEST_STEP_FAILURE(ret);
	}

	offset_out = seek_adi(fd, OFFSET_ADD, SEEK_CUR);
	if (offset_out != (offset_in + OFFSET_ADD)) {
		ret = -4;
		TEST_STEP_FAILURE(ret);
	}

	offset_out = seek_adi(fd, OFFSET_SUBTRACT, SEEK_CUR);
	if (offset_out != (offset_in + OFFSET_ADD + OFFSET_SUBTRACT)) {
		ret = -5;
		TEST_STEP_FAILURE(ret);
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

static int test5_rw_aligned_1byte(int fd)
{
	/* somewhat arbitrarily chosen address */
	unsigned long paddr =
		(end_addr[range_count - 1] - 0xF000) & ~(ADI_BLKSZ - 1);
	unsigned char version, expected_version;
	loff_t offset;
	off_t oret;
	int ret;

	offset = paddr / ADI_BLKSZ;
	version = expected_version = random_version();

	oret = seek_adi(fd, offset, SEEK_SET);
	if (oret != offset) {
		ret = -1;
		TEST_STEP_FAILURE(ret);
	}

	ret = write_adi(fd, &version, sizeof(version));
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	oret = seek_adi(fd, offset, SEEK_SET);
	if (oret != offset) {
		ret = -1;
		TEST_STEP_FAILURE(ret);
	}

	ret = read_adi(fd, &version, sizeof(version));
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	if (expected_version != version) {
		DEBUG_PRINT_L2("\tExpected version %d but read version %d\n",
			       expected_version, version);
		TEST_STEP_FAILURE(-expected_version);
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

#define TEST6_VERSION_SZ        9434
static int test6_rw_aligned_9434bytes(int fd)
{
	/* somewhat arbitrarily chosen address */
	unsigned long paddr =
		(end_addr[range_count - 1] - 0x5F000) & ~(ADI_BLKSZ - 1);
	unsigned char version[TEST6_VERSION_SZ],
		      expected_version[TEST6_VERSION_SZ];
	loff_t offset;
	off_t oret;
	int ret, i;

	offset = paddr / ADI_BLKSZ;
	for (i = 0; i < TEST6_VERSION_SZ; i++)
		version[i] = expected_version[i] = random_version();

	oret = seek_adi(fd, offset, SEEK_SET);
	if (oret != offset) {
		ret = -1;
		TEST_STEP_FAILURE(ret);
	}

	ret = write_adi(fd, version, sizeof(version));
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	memset(version, 0, TEST6_VERSION_SZ);

	oret = seek_adi(fd, offset, SEEK_SET);
	if (oret != offset) {
		ret = -1;
		TEST_STEP_FAILURE(ret);
	}

	ret = read_adi(fd, version, sizeof(version));
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	for (i = 0; i < TEST6_VERSION_SZ; i++) {
		if (expected_version[i] != version[i]) {
			DEBUG_PRINT_L2(
				"\tExpected version %d but read version %d\n",
				expected_version[i], version[i]);
			TEST_STEP_FAILURE(-expected_version[i]);
		}
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

#define TEST7_VERSION_SZ        14963
static int test7_rw_aligned_14963bytes(int fd)
{
	/* somewhat arbitrarily chosen address */
	unsigned long paddr =
	  ((start_addr[range_count - 1] + 0xF000) & ~(ADI_BLKSZ - 1)) + 39;
	unsigned char version[TEST7_VERSION_SZ],
		      expected_version[TEST7_VERSION_SZ];
	loff_t offset;
	off_t oret;
	int ret, i;

	offset = paddr / ADI_BLKSZ;
	for (i = 0; i < TEST7_VERSION_SZ; i++) {
		version[i] = random_version();
		expected_version[i] = version[i];
	}

	oret = seek_adi(fd, offset, SEEK_SET);
	if (oret != offset) {
		ret = -1;
		TEST_STEP_FAILURE(ret);
	}

	ret = write_adi(fd, version, sizeof(version));
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	memset(version, 0, TEST7_VERSION_SZ);

	oret = seek_adi(fd, offset, SEEK_SET);
	if (oret != offset) {
		ret = -1;
		TEST_STEP_FAILURE(ret);
	}

	ret = read_adi(fd, version, sizeof(version));
	if (ret != sizeof(version))
		TEST_STEP_FAILURE(ret);

	for (i = 0; i < TEST7_VERSION_SZ; i++) {
		if (expected_version[i] != version[i]) {
			DEBUG_PRINT_L2(
				"\tExpected version %d but read version %d\n",
				expected_version[i], version[i]);
			TEST_STEP_FAILURE(-expected_version[i]);
		}

		paddr += ADI_BLKSZ;
	}

	ret = 0;
out:
	RETURN_FROM_TEST(ret);
}

static int (*tests[])(int fd) = {
	test0_prpw_aligned_1byte,
	test1_prpw_aligned_4096bytes,
	test2_prpw_aligned_10327bytes,
	test3_prpw_unaligned_12541bytes,
	test4_lseek,
	test5_rw_aligned_1byte,
	test6_rw_aligned_9434bytes,
	test7_rw_aligned_14963bytes,
};
#define TEST_COUNT	ARRAY_SIZE(tests)

int main(int argc, char *argv[])
{
	int fd, ret, test;

	ret = build_memory_map();
	if (ret < 0)
		return ret;

	fd = open("/dev/adi", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open: error %d: %s\n",
			errno, strerror(errno));
		return -errno;
	}

	for (test = 0; test < TEST_COUNT; test++) {
		DEBUG_PRINT_L1("Running test #%d\n", test);

		ret = (*tests[test])(fd);
		if (ret != 0)
			ksft_test_result_fail("Test #%d failed: error %d\n",
					      test, ret);
		else
			ksft_test_result_pass("Test #%d passed\n", test);
	}

	print_stats();
	close(fd);

	if (ksft_get_fail_cnt() > 0)
		ksft_exit_fail();
	else
		ksft_exit_pass();

	/* it's impossible to get here, but the compiler throws a warning
	 * about control reaching the end of non-void function.  bah.
	 */
	return 0;
}
