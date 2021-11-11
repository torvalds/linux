/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tools/testing/selftests/kvm/include/test_util.h
 *
 * Copyright (C) 2018, Google LLC.
 */

#ifndef SELFTEST_KVM_TEST_UTIL_H
#define SELFTEST_KVM_TEST_UTIL_H

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "kselftest.h"

static inline int _no_printf(const char *format, ...) { return 0; }

#ifdef DEBUG
#define pr_debug(...) printf(__VA_ARGS__)
#else
#define pr_debug(...) _no_printf(__VA_ARGS__)
#endif
#ifndef QUIET
#define pr_info(...) printf(__VA_ARGS__)
#else
#define pr_info(...) _no_printf(__VA_ARGS__)
#endif

void print_skip(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

ssize_t test_write(int fd, const void *buf, size_t count);
ssize_t test_read(int fd, void *buf, size_t count);
int test_seq_read(const char *path, char **bufp, size_t *sizep);

void test_assert(bool exp, const char *exp_str,
		 const char *file, unsigned int line, const char *fmt, ...)
		__attribute__((format(printf, 5, 6)));

#define TEST_ASSERT(e, fmt, ...) \
	test_assert((e), #e, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define ASSERT_EQ(a, b) do { \
	typeof(a) __a = (a); \
	typeof(b) __b = (b); \
	TEST_ASSERT(__a == __b, \
		    "ASSERT_EQ(%s, %s) failed.\n" \
		    "\t%s is %#lx\n" \
		    "\t%s is %#lx", \
		    #a, #b, #a, (unsigned long) __a, #b, (unsigned long) __b); \
} while (0)

#define TEST_FAIL(fmt, ...) \
	TEST_ASSERT(false, fmt, ##__VA_ARGS__)

size_t parse_size(const char *size);

int64_t timespec_to_ns(struct timespec ts);
struct timespec timespec_add_ns(struct timespec ts, int64_t ns);
struct timespec timespec_add(struct timespec ts1, struct timespec ts2);
struct timespec timespec_sub(struct timespec ts1, struct timespec ts2);
struct timespec timespec_elapsed(struct timespec start);
struct timespec timespec_div(struct timespec ts, int divisor);

enum vm_mem_backing_src_type {
	VM_MEM_SRC_ANONYMOUS,
	VM_MEM_SRC_ANONYMOUS_THP,
	VM_MEM_SRC_ANONYMOUS_HUGETLB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_16KB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_64KB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_512KB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_1MB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_2MB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_8MB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_16MB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_32MB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_256MB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_512MB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_1GB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_2GB,
	VM_MEM_SRC_ANONYMOUS_HUGETLB_16GB,
	VM_MEM_SRC_SHMEM,
	VM_MEM_SRC_SHARED_HUGETLB,
	NUM_SRC_TYPES,
};

#define DEFAULT_VM_MEM_SRC VM_MEM_SRC_ANONYMOUS

struct vm_mem_backing_src_alias {
	const char *name;
	uint32_t flag;
};

#define MIN_RUN_DELAY_NS	200000UL

bool thp_configured(void);
size_t get_trans_hugepagesz(void);
size_t get_def_hugetlb_pagesz(void);
const struct vm_mem_backing_src_alias *vm_mem_backing_src_alias(uint32_t i);
size_t get_backing_src_pagesz(uint32_t i);
bool is_backing_src_hugetlb(uint32_t i);
void backing_src_help(const char *flag);
enum vm_mem_backing_src_type parse_backing_src_type(const char *type_name);
long get_run_delay(void);

/*
 * Whether or not the given source type is shared memory (as opposed to
 * anonymous).
 */
static inline bool backing_src_is_shared(enum vm_mem_backing_src_type t)
{
	return vm_mem_backing_src_alias(t)->flag & MAP_SHARED;
}

/* Aligns x up to the next multiple of size. Size must be a power of 2. */
static inline uint64_t align_up(uint64_t x, uint64_t size)
{
	uint64_t mask = size - 1;

	TEST_ASSERT(size != 0 && !(size & (size - 1)),
		    "size not a power of 2: %lu", size);
	return ((x + mask) & ~mask);
}

static inline uint64_t align_down(uint64_t x, uint64_t size)
{
	uint64_t x_aligned_up = align_up(x, size);

	if (x == x_aligned_up)
		return x;
	else
		return x_aligned_up - size;
}

static inline void *align_ptr_up(void *x, size_t size)
{
	return (void *)align_up((unsigned long)x, size);
}

#endif /* SELFTEST_KVM_TEST_UTIL_H */
