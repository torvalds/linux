// SPDX-License-Identifier: GPL-2.0-only
/*
 * Userfaultfd tests common header
 *
 * Copyright (C) 2015-2023  Red Hat, Inc.
 */
#ifndef __UFFD_COMMON_H__
#define __UFFD_COMMON_H__

#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <linux/mman.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pthread.h>
#include <linux/userfaultfd.h>
#include <setjmp.h>
#include <stdbool.h>
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <sys/random.h>

#include "../kselftest.h"
#include "vm_util.h"

#define UFFD_FLAGS	(O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY)

#define _err(fmt, ...)						\
	do {							\
		int ret = errno;				\
		fprintf(stderr, "ERROR: " fmt, ##__VA_ARGS__);	\
		fprintf(stderr, " (errno=%d, @%s:%d)\n",	\
			ret, __FILE__, __LINE__);		\
	} while (0)

#define errexit(exitcode, fmt, ...)		\
	do {					\
		_err(fmt, ##__VA_ARGS__);	\
		exit(exitcode);			\
	} while (0)

#define err(fmt, ...) errexit(1, fmt, ##__VA_ARGS__)

/* pthread_mutex_t starts at page offset 0 */
#define area_mutex(___area, ___nr)					\
	((pthread_mutex_t *) ((___area) + (___nr)*page_size))
/*
 * count is placed in the page after pthread_mutex_t naturally aligned
 * to avoid non alignment faults on non-x86 archs.
 */
#define area_count(___area, ___nr)					\
	((volatile unsigned long long *) ((unsigned long)		\
				 ((___area) + (___nr)*page_size +	\
				  sizeof(pthread_mutex_t) +		\
				  sizeof(unsigned long long) - 1) &	\
				 ~(unsigned long)(sizeof(unsigned long long) \
						  -  1)))

/* Userfaultfd test statistics */
struct uffd_args {
	int cpu;
	/* Whether apply wr-protects when installing pages */
	bool apply_wp;
	unsigned long missing_faults;
	unsigned long wp_faults;
	unsigned long minor_faults;

	/* A custom fault handler; defaults to uffd_handle_page_fault. */
	void (*handle_fault)(struct uffd_msg *msg, struct uffd_args *args);
};

struct uffd_test_ops {
	int (*allocate_area)(void **alloc_area, bool is_src);
	void (*release_pages)(char *rel_area);
	void (*alias_mapping)(__u64 *start, size_t len, unsigned long offset);
	void (*check_pmd_mapping)(void *p, int expect_nr_hpages);
};
typedef struct uffd_test_ops uffd_test_ops_t;

extern unsigned long nr_cpus, nr_pages, nr_pages_per_cpu, page_size;
extern char *area_src, *area_src_alias, *area_dst, *area_dst_alias, *area_remap;
extern int uffd, uffd_flags, finished, *pipefd, test_type;
extern bool map_shared;
extern bool test_uffdio_wp;
extern unsigned long long *count_verify;
extern volatile bool test_uffdio_copy_eexist;

extern uffd_test_ops_t anon_uffd_test_ops;
extern uffd_test_ops_t shmem_uffd_test_ops;
extern uffd_test_ops_t hugetlb_uffd_test_ops;
extern uffd_test_ops_t *uffd_test_ops;

void uffd_stats_report(struct uffd_args *args, int n_cpus);
int uffd_test_ctx_init(uint64_t features, const char **errmsg);
int userfaultfd_open(uint64_t *features);
int uffd_read_msg(int ufd, struct uffd_msg *msg);
void wp_range(int ufd, __u64 start, __u64 len, bool wp);
void uffd_handle_page_fault(struct uffd_msg *msg, struct uffd_args *args);
int __copy_page(int ufd, unsigned long offset, bool retry, bool wp);
int copy_page(int ufd, unsigned long offset, bool wp);
void *uffd_poll_thread(void *arg);

int uffd_open_dev(unsigned int flags);
int uffd_open_sys(unsigned int flags);
int uffd_open(unsigned int flags);
int uffd_get_features(uint64_t *features);

#define TEST_ANON	1
#define TEST_HUGETLB	2
#define TEST_SHMEM	3

#endif
