// SPDX-License-Identifier: GPL-2.0-only
/*
 * tools/testing/selftests/kvm/lib/test_util.c
 *
 * Copyright (C) 2020, Google LLC.
 */
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/mman.h>
#include "linux/kernel.h"

#include "test_util.h"

/*
 * Random number generator that is usable from guest code. This is the
 * Park-Miller LCG using standard constants.
 */

struct guest_random_state new_guest_random_state(uint32_t seed)
{
	struct guest_random_state s = {.seed = seed};
	return s;
}

uint32_t guest_random_u32(struct guest_random_state *state)
{
	state->seed = (uint64_t)state->seed * 48271 % ((uint32_t)(1 << 31) - 1);
	return state->seed;
}

/*
 * Parses "[0-9]+[kmgt]?".
 */
size_t parse_size(const char *size)
{
	size_t base;
	char *scale;
	int shift = 0;

	TEST_ASSERT(size && isdigit(size[0]), "Need at least one digit in '%s'", size);

	base = strtoull(size, &scale, 0);

	TEST_ASSERT(base != ULLONG_MAX, "Overflow parsing size!");

	switch (tolower(*scale)) {
	case 't':
		shift = 40;
		break;
	case 'g':
		shift = 30;
		break;
	case 'm':
		shift = 20;
		break;
	case 'k':
		shift = 10;
		break;
	case 'b':
	case '\0':
		shift = 0;
		break;
	default:
		TEST_ASSERT(false, "Unknown size letter %c", *scale);
	}

	TEST_ASSERT((base << shift) >> shift == base, "Overflow scaling size!");

	return base << shift;
}

int64_t timespec_to_ns(struct timespec ts)
{
	return (int64_t)ts.tv_nsec + 1000000000LL * (int64_t)ts.tv_sec;
}

struct timespec timespec_add_ns(struct timespec ts, int64_t ns)
{
	struct timespec res;

	res.tv_nsec = ts.tv_nsec + ns;
	res.tv_sec = ts.tv_sec + res.tv_nsec / 1000000000LL;
	res.tv_nsec %= 1000000000LL;

	return res;
}

struct timespec timespec_add(struct timespec ts1, struct timespec ts2)
{
	int64_t ns1 = timespec_to_ns(ts1);
	int64_t ns2 = timespec_to_ns(ts2);
	return timespec_add_ns((struct timespec){0}, ns1 + ns2);
}

struct timespec timespec_sub(struct timespec ts1, struct timespec ts2)
{
	int64_t ns1 = timespec_to_ns(ts1);
	int64_t ns2 = timespec_to_ns(ts2);
	return timespec_add_ns((struct timespec){0}, ns1 - ns2);
}

struct timespec timespec_elapsed(struct timespec start)
{
	struct timespec end;

	clock_gettime(CLOCK_MONOTONIC, &end);
	return timespec_sub(end, start);
}

struct timespec timespec_div(struct timespec ts, int divisor)
{
	int64_t ns = timespec_to_ns(ts) / divisor;

	return timespec_add_ns((struct timespec){0}, ns);
}

void print_skip(const char *fmt, ...)
{
	va_list ap;

	assert(fmt);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	puts(", skipping test");
}

bool thp_configured(void)
{
	int ret;
	struct stat statbuf;

	ret = stat("/sys/kernel/mm/transparent_hugepage", &statbuf);
	TEST_ASSERT(ret == 0 || (ret == -1 && errno == ENOENT),
		    "Error in stating /sys/kernel/mm/transparent_hugepage");

	return ret == 0;
}

size_t get_trans_hugepagesz(void)
{
	size_t size;
	FILE *f;
	int ret;

	TEST_ASSERT(thp_configured(), "THP is not configured in host kernel");

	f = fopen("/sys/kernel/mm/transparent_hugepage/hpage_pmd_size", "r");
	TEST_ASSERT(f != NULL, "Error in opening transparent_hugepage/hpage_pmd_size");

	ret = fscanf(f, "%ld", &size);
	ret = fscanf(f, "%ld", &size);
	TEST_ASSERT(ret < 1, "Error reading transparent_hugepage/hpage_pmd_size");
	fclose(f);

	return size;
}

size_t get_def_hugetlb_pagesz(void)
{
	char buf[64];
	const char *hugepagesize = "Hugepagesize:";
	const char *hugepages_total = "HugePages_Total:";
	FILE *f;

	f = fopen("/proc/meminfo", "r");
	TEST_ASSERT(f != NULL, "Error in opening /proc/meminfo");

	while (fgets(buf, sizeof(buf), f) != NULL) {
		if (strstr(buf, hugepages_total) == buf) {
			unsigned long long total = strtoull(buf + strlen(hugepages_total), NULL, 10);
			if (!total) {
				fprintf(stderr, "HUGETLB is not enabled in /proc/sys/vm/nr_hugepages\n");
				exit(KSFT_SKIP);
			}
		}
		if (strstr(buf, hugepagesize) == buf) {
			fclose(f);
			return strtoull(buf + strlen(hugepagesize), NULL, 10) << 10;
		}
	}

	if (feof(f)) {
		fprintf(stderr, "HUGETLB is not configured in host kernel");
		exit(KSFT_SKIP);
	}

	TEST_FAIL("Error in reading /proc/meminfo");
}

#define ANON_FLAGS	(MAP_PRIVATE | MAP_ANONYMOUS)
#define ANON_HUGE_FLAGS	(ANON_FLAGS | MAP_HUGETLB)

const struct vm_mem_backing_src_alias *vm_mem_backing_src_alias(uint32_t i)
{
	static const struct vm_mem_backing_src_alias aliases[] = {
		[VM_MEM_SRC_ANONYMOUS] = {
			.name = "anonymous",
			.flag = ANON_FLAGS,
		},
		[VM_MEM_SRC_ANONYMOUS_THP] = {
			.name = "anonymous_thp",
			.flag = ANON_FLAGS,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB] = {
			.name = "anonymous_hugetlb",
			.flag = ANON_HUGE_FLAGS,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_16KB] = {
			.name = "anonymous_hugetlb_16kb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_16KB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_64KB] = {
			.name = "anonymous_hugetlb_64kb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_64KB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_512KB] = {
			.name = "anonymous_hugetlb_512kb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_512KB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_1MB] = {
			.name = "anonymous_hugetlb_1mb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_1MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_2MB] = {
			.name = "anonymous_hugetlb_2mb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_2MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_8MB] = {
			.name = "anonymous_hugetlb_8mb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_8MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_16MB] = {
			.name = "anonymous_hugetlb_16mb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_16MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_32MB] = {
			.name = "anonymous_hugetlb_32mb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_32MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_256MB] = {
			.name = "anonymous_hugetlb_256mb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_256MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_512MB] = {
			.name = "anonymous_hugetlb_512mb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_512MB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_1GB] = {
			.name = "anonymous_hugetlb_1gb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_1GB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_2GB] = {
			.name = "anonymous_hugetlb_2gb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_2GB,
		},
		[VM_MEM_SRC_ANONYMOUS_HUGETLB_16GB] = {
			.name = "anonymous_hugetlb_16gb",
			.flag = ANON_HUGE_FLAGS | MAP_HUGE_16GB,
		},
		[VM_MEM_SRC_SHMEM] = {
			.name = "shmem",
			.flag = MAP_SHARED,
		},
		[VM_MEM_SRC_SHARED_HUGETLB] = {
			.name = "shared_hugetlb",
			/*
			 * No MAP_HUGETLB, we use MFD_HUGETLB instead. Since
			 * we're using "file backed" memory, we need to specify
			 * this when the FD is created, not when the area is
			 * mapped.
			 */
			.flag = MAP_SHARED,
		},
	};
	_Static_assert(ARRAY_SIZE(aliases) == NUM_SRC_TYPES,
		       "Missing new backing src types?");

	TEST_ASSERT(i < NUM_SRC_TYPES, "Backing src type ID %d too big", i);

	return &aliases[i];
}

#define MAP_HUGE_PAGE_SIZE(x) (1ULL << ((x >> MAP_HUGE_SHIFT) & MAP_HUGE_MASK))

size_t get_backing_src_pagesz(uint32_t i)
{
	uint32_t flag = vm_mem_backing_src_alias(i)->flag;

	switch (i) {
	case VM_MEM_SRC_ANONYMOUS:
	case VM_MEM_SRC_SHMEM:
		return getpagesize();
	case VM_MEM_SRC_ANONYMOUS_THP:
		return get_trans_hugepagesz();
	case VM_MEM_SRC_ANONYMOUS_HUGETLB:
	case VM_MEM_SRC_SHARED_HUGETLB:
		return get_def_hugetlb_pagesz();
	default:
		return MAP_HUGE_PAGE_SIZE(flag);
	}
}

bool is_backing_src_hugetlb(uint32_t i)
{
	return !!(vm_mem_backing_src_alias(i)->flag & MAP_HUGETLB);
}

static void print_available_backing_src_types(const char *prefix)
{
	int i;

	printf("%sAvailable backing src types:\n", prefix);

	for (i = 0; i < NUM_SRC_TYPES; i++)
		printf("%s    %s\n", prefix, vm_mem_backing_src_alias(i)->name);
}

void backing_src_help(const char *flag)
{
	printf(" %s: specify the type of memory that should be used to\n"
	       "     back the guest data region. (default: %s)\n",
	       flag, vm_mem_backing_src_alias(DEFAULT_VM_MEM_SRC)->name);
	print_available_backing_src_types("     ");
}

enum vm_mem_backing_src_type parse_backing_src_type(const char *type_name)
{
	int i;

	for (i = 0; i < NUM_SRC_TYPES; i++)
		if (!strcmp(type_name, vm_mem_backing_src_alias(i)->name))
			return i;

	print_available_backing_src_types("");
	TEST_FAIL("Unknown backing src type: %s", type_name);
	return -1;
}

long get_run_delay(void)
{
	char path[64];
	long val[2];
	FILE *fp;

	sprintf(path, "/proc/%ld/schedstat", syscall(SYS_gettid));
	fp = fopen(path, "r");
	/* Return MIN_RUN_DELAY_NS upon failure just to be safe */
	if (fscanf(fp, "%ld %ld ", &val[0], &val[1]) < 2)
		val[1] = MIN_RUN_DELAY_NS;
	fclose(fp);

	return val[1];
}

int atoi_paranoid(const char *num_str)
{
	char *end_ptr;
	long num;

	errno = 0;
	num = strtol(num_str, &end_ptr, 0);
	TEST_ASSERT(!errno, "strtol(\"%s\") failed", num_str);
	TEST_ASSERT(num_str != end_ptr,
		    "strtol(\"%s\") didn't find a valid integer.", num_str);
	TEST_ASSERT(*end_ptr == '\0',
		    "strtol(\"%s\") failed to parse trailing characters \"%s\".",
		    num_str, end_ptr);
	TEST_ASSERT(num >= INT_MIN && num <= INT_MAX,
		    "%ld not in range of [%d, %d]", num, INT_MIN, INT_MAX);

	return num;
}

char *strdup_printf(const char *fmt, ...)
{
	va_list ap;
	char *str;

	va_start(ap, fmt);
	TEST_ASSERT(vasprintf(&str, fmt, ap) >= 0, "vasprintf() failed");
	va_end(ap);

	return str;
}

#define CLOCKSOURCE_PATH "/sys/devices/system/clocksource/clocksource0/current_clocksource"

char *sys_get_cur_clocksource(void)
{
	char *clk_name;
	struct stat st;
	FILE *fp;

	fp = fopen(CLOCKSOURCE_PATH, "r");
	TEST_ASSERT(fp, "failed to open clocksource file, errno: %d", errno);

	TEST_ASSERT(!fstat(fileno(fp), &st), "failed to stat clocksource file, errno: %d",
		    errno);

	clk_name = malloc(st.st_size);
	TEST_ASSERT(clk_name, "failed to allocate buffer to read file");

	TEST_ASSERT(fgets(clk_name, st.st_size, fp), "failed to read clocksource file: %d",
		    ferror(fp));

	fclose(fp);

	return clk_name;
}
