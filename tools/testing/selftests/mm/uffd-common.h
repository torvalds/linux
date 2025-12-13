// SPDX-License-Identifier: GPL-2.0-only
/*
 * Userfaultfd tests common header
 *
 * Copyright (C) 2015-2023  Red Hat, Inc.
 */
#ifndef __UFFD_COMMON_H__
#define __UFFD_COMMON_H__

#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__ // Use ll64
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
#include <stdatomic.h>

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

struct uffd_global_test_opts {
	unsigned long nr_parallel, nr_pages, nr_pages_per_cpu, page_size;
	char *area_src, *area_src_alias, *area_dst, *area_dst_alias, *area_remap;
	int uffd, uffd_flags, finished, *pipefd, test_type;
	bool map_shared;
	bool test_uffdio_wp;
	unsigned long long *count_verify;
	volatile bool test_uffdio_copy_eexist;
	atomic_bool ready_for_fork;
};
typedef struct uffd_global_test_opts uffd_global_test_opts_t;

/* Userfaultfd test statistics */
struct uffd_args {
	int cpu;
	/* Whether apply wr-protects when installing pages */
	bool apply_wp;
	unsigned long missing_faults;
	unsigned long wp_faults;
	unsigned long minor_faults;
	struct uffd_global_test_opts *gopts;

	/* A custom fault handler; defaults to uffd_handle_page_fault. */
	void (*handle_fault)(struct uffd_global_test_opts *gopts,
			     struct uffd_msg *msg,
			     struct uffd_args *args);
};

struct uffd_test_ops {
	int (*allocate_area)(uffd_global_test_opts_t *gopts, void **alloc_area, bool is_src);
	void (*release_pages)(uffd_global_test_opts_t *gopts, char *rel_area);
	void (*alias_mapping)(uffd_global_test_opts_t *gopts,
			      __u64 *start,
			      size_t len,
			      unsigned long offset);
	void (*check_pmd_mapping)(uffd_global_test_opts_t *gopts, void *p, int expect_nr_hpages);
};
typedef struct uffd_test_ops uffd_test_ops_t;

struct uffd_test_case_ops {
	int (*pre_alloc)(uffd_global_test_opts_t *gopts, const char **errmsg);
	int (*post_alloc)(uffd_global_test_opts_t *gopts, const char **errmsg);
};
typedef struct uffd_test_case_ops uffd_test_case_ops_t;

extern uffd_global_test_opts_t *uffd_gtest_opts;
extern uffd_test_ops_t anon_uffd_test_ops;
extern uffd_test_ops_t shmem_uffd_test_ops;
extern uffd_test_ops_t hugetlb_uffd_test_ops;
extern uffd_test_ops_t *uffd_test_ops;
extern uffd_test_case_ops_t *uffd_test_case_ops;

pthread_mutex_t *area_mutex(char *area, unsigned long nr, uffd_global_test_opts_t *gopts);
volatile unsigned long long *area_count(char *area,
					unsigned long nr,
					uffd_global_test_opts_t *gopts);

void uffd_stats_report(struct uffd_args *args, int n_cpus);
int uffd_test_ctx_init(uffd_global_test_opts_t *gopts, uint64_t features, const char **errmsg);
void uffd_test_ctx_clear(uffd_global_test_opts_t *gopts);
int userfaultfd_open(uffd_global_test_opts_t *gopts, uint64_t *features);
int uffd_read_msg(uffd_global_test_opts_t *gopts, struct uffd_msg *msg);
void wp_range(int ufd, __u64 start, __u64 len, bool wp);
void uffd_handle_page_fault(uffd_global_test_opts_t *gopts,
			    struct uffd_msg *msg,
			    struct uffd_args *args);
int __copy_page(uffd_global_test_opts_t *gopts, unsigned long offset, bool retry, bool wp);
int copy_page(uffd_global_test_opts_t *gopts, unsigned long offset, bool wp);
int move_page(uffd_global_test_opts_t *gopts, unsigned long offset, unsigned long len);
void *uffd_poll_thread(void *arg);

int uffd_open_dev(unsigned int flags);
int uffd_open_sys(unsigned int flags);
int uffd_open(unsigned int flags);
int uffd_get_features(uint64_t *features);

#define TEST_ANON	1
#define TEST_HUGETLB	2
#define TEST_SHMEM	3

#endif
