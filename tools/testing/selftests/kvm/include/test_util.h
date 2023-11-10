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

void __printf(1, 2) print_skip(const char *fmt, ...);
#define __TEST_REQUIRE(f, fmt, ...)				\
do {								\
	if (!(f))						\
		ksft_exit_skip("- " fmt "\n", ##__VA_ARGS__);	\
} while (0)

#define TEST_REQUIRE(f) __TEST_REQUIRE(f, "Requirement not met: %s", #f)

ssize_t test_write(int fd, const void *buf, size_t count);
ssize_t test_read(int fd, void *buf, size_t count);
int test_seq_read(const char *path, char **bufp, size_t *sizep);

void __printf(5, 6) test_assert(bool exp, const char *exp_str,
				const char *file, unsigned int line,
				const char *fmt, ...);

#define TEST_ASSERT(e, fmt, ...) \
	test_assert((e), #e, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define TEST_ASSERT_EQ(a, b)						\
do {									\
	typeof(a) __a = (a);						\
	typeof(b) __b = (b);						\
	test_assert(__a == __b, #a " == " #b, __FILE__, __LINE__,	\
		    "%#lx != %#lx (%s != %s)",				\
		    (unsigned long)(__a), (unsigned long)(__b), #a, #b);\
} while (0)

#define TEST_ASSERT_KVM_EXIT_REASON(vcpu, expected) do {		\
	__u32 exit_reason = (vcpu)->run->exit_reason;			\
									\
	TEST_ASSERT(exit_reason == (expected),				\
		    "Wanted KVM exit reason: %u (%s), got: %u (%s)",    \
		    (expected), exit_reason_str((expected)),		\
		    exit_reason, exit_reason_str(exit_reason));		\
} while (0)

#define TEST_FAIL(fmt, ...) do { \
	TEST_ASSERT(false, fmt, ##__VA_ARGS__); \
	__builtin_unreachable(); \
} while (0)

size_t parse_size(const char *size);

int64_t timespec_to_ns(struct timespec ts);
struct timespec timespec_add_ns(struct timespec ts, int64_t ns);
struct timespec timespec_add(struct timespec ts1, struct timespec ts2);
struct timespec timespec_sub(struct timespec ts1, struct timespec ts2);
struct timespec timespec_elapsed(struct timespec start);
struct timespec timespec_div(struct timespec ts, int divisor);

struct guest_random_state {
	uint32_t seed;
};

struct guest_random_state new_guest_random_state(uint32_t seed);
uint32_t guest_random_u32(struct guest_random_state *state);

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

int atoi_paranoid(const char *num_str);

static inline uint32_t atoi_positive(const char *name, const char *num_str)
{
	int num = atoi_paranoid(num_str);

	TEST_ASSERT(num > 0, "%s must be greater than 0, got '%s'", name, num_str);
	return num;
}

static inline uint32_t atoi_non_negative(const char *name, const char *num_str)
{
	int num = atoi_paranoid(num_str);

	TEST_ASSERT(num >= 0, "%s must be non-negative, got '%s'", name, num_str);
	return num;
}

int guest_vsnprintf(char *buf, int n, const char *fmt, va_list args);
int guest_snprintf(char *buf, int n, const char *fmt, ...);

char *strdup_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2), nonnull(1)));

#endif /* SELFTEST_KVM_TEST_UTIL_H */
