/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 */

#ifndef _SELFTESTS_POWERPC_UTILS_H
#define _SELFTESTS_POWERPC_UTILS_H

#define __cacheline_aligned __attribute__((aligned(128)))

#include <stdint.h>
#include <stdbool.h>
#include <linux/auxvec.h>
#include <linux/perf_event.h>
#include <asm/cputable.h>
#include "reg.h"

/* Avoid headaches with PRI?64 - just use %ll? always */
typedef unsigned long long u64;
typedef   signed long long s64;

/* Just for familiarity */
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

void test_harness_set_timeout(uint64_t time);
int test_harness(int (test_function)(void), char *name);

int read_auxv(char *buf, ssize_t buf_size);
void *find_auxv_entry(int type, char *auxv);
void *get_auxv_entry(int type);

int pick_online_cpu(void);

int parse_intmax(const char *buffer, size_t count, intmax_t *result, int base);
int parse_uintmax(const char *buffer, size_t count, uintmax_t *result, int base);
int parse_int(const char *buffer, size_t count, int *result, int base);
int parse_uint(const char *buffer, size_t count, unsigned int *result, int base);
int parse_long(const char *buffer, size_t count, long *result, int base);
int parse_ulong(const char *buffer, size_t count, unsigned long *result, int base);

int read_file(const char *path, char *buf, size_t count, size_t *len);
int write_file(const char *path, const char *buf, size_t count);
int read_file_alloc(const char *path, char **buf, size_t *len);
int read_long(const char *path, long *result, int base);
int write_long(const char *path, long result, int base);
int read_ulong(const char *path, unsigned long *result, int base);
int write_ulong(const char *path, unsigned long result, int base);
int read_debugfs_file(const char *debugfs_file, char *buf, size_t count);
int write_debugfs_file(const char *debugfs_file, const char *buf, size_t count);
int read_debugfs_int(const char *debugfs_file, int *result);
int write_debugfs_int(const char *debugfs_file, int result);
int read_sysfs_file(char *debugfs_file, char *result, size_t result_size);
int perf_event_open_counter(unsigned int type,
			    unsigned long config, int group_fd);
int perf_event_enable(int fd);
int perf_event_disable(int fd);
int perf_event_reset(int fd);

struct perf_event_read {
	__u64 nr;
	__u64 l1d_misses;
};

#if !defined(__GLIBC_PREREQ) || !__GLIBC_PREREQ(2, 30)
#include <unistd.h>
#include <sys/syscall.h>

static inline pid_t gettid(void)
{
	return syscall(SYS_gettid);
}
#endif

static inline bool have_hwcap(unsigned long ftr)
{
	return ((unsigned long)get_auxv_entry(AT_HWCAP) & ftr) == ftr;
}

#ifdef AT_HWCAP2
static inline bool have_hwcap2(unsigned long ftr2)
{
	return ((unsigned long)get_auxv_entry(AT_HWCAP2) & ftr2) == ftr2;
}
#else
static inline bool have_hwcap2(unsigned long ftr2)
{
	return false;
}
#endif

static inline char *auxv_base_platform(void)
{
	return ((char *)get_auxv_entry(AT_BASE_PLATFORM));
}

static inline char *auxv_platform(void)
{
	return ((char *)get_auxv_entry(AT_PLATFORM));
}

bool is_ppc64le(void);
int using_hash_mmu(bool *using_hash);

/* Yes, this is evil */
#define FAIL_IF(x)						\
do {								\
	if ((x)) {						\
		fprintf(stderr,					\
		"[FAIL] Test FAILED on line %d\n", __LINE__);	\
		return 1;					\
	}							\
} while (0)

#define FAIL_IF_EXIT(x)						\
do {								\
	if ((x)) {						\
		fprintf(stderr,					\
		"[FAIL] Test FAILED on line %d\n", __LINE__);	\
		_exit(1);					\
	}							\
} while (0)

/* The test harness uses this, yes it's gross */
#define MAGIC_SKIP_RETURN_VALUE	99

#define SKIP_IF(x)						\
do {								\
	if ((x)) {						\
		fprintf(stderr,					\
		"[SKIP] Test skipped on line %d\n", __LINE__);	\
		return MAGIC_SKIP_RETURN_VALUE;			\
	}							\
} while (0)

#define SKIP_IF_MSG(x, msg)					\
do {								\
	if ((x)) {						\
		fprintf(stderr,					\
		"[SKIP] Test skipped on line %d: %s\n",		\
		 __LINE__, msg);				\
		return MAGIC_SKIP_RETURN_VALUE;			\
	}							\
} while (0)

#define _str(s) #s
#define str(s) _str(s)

#define sigsafe_err(msg)	({ \
		ssize_t nbytes __attribute__((unused)); \
		nbytes = write(STDERR_FILENO, msg, strlen(msg)); })

/* POWER9 feature */
#ifndef PPC_FEATURE2_ARCH_3_00
#define PPC_FEATURE2_ARCH_3_00 0x00800000
#endif

/* POWER10 feature */
#ifndef PPC_FEATURE2_ARCH_3_1
#define PPC_FEATURE2_ARCH_3_1 0x00040000
#endif

/* POWER10 features */
#ifndef PPC_FEATURE2_MMA
#define PPC_FEATURE2_MMA 0x00020000
#endif

#if defined(__powerpc64__)
#define UCONTEXT_NIA(UC)	(UC)->uc_mcontext.gp_regs[PT_NIP]
#define UCONTEXT_MSR(UC)	(UC)->uc_mcontext.gp_regs[PT_MSR]
#elif defined(__powerpc__)
#define UCONTEXT_NIA(UC)	(UC)->uc_mcontext.uc_regs->gregs[PT_NIP]
#define UCONTEXT_MSR(UC)	(UC)->uc_mcontext.uc_regs->gregs[PT_MSR]
#else
#error implement UCONTEXT_NIA
#endif

#endif /* _SELFTESTS_POWERPC_UTILS_H */
