/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (C) 2020 Facebook, Inc. */

#ifndef __TESTING_HELPERS_H
#define __TESTING_HELPERS_H

#include <stdbool.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <time.h>

int parse_num_list(const char *s, bool **set, int *set_len);
__u32 link_info_prog_id(const struct bpf_link *link, struct bpf_link_info *info);
int bpf_prog_test_load(const char *file, enum bpf_prog_type type,
		       struct bpf_object **pobj, int *prog_fd);
int bpf_test_load_program(enum bpf_prog_type type, const struct bpf_insn *insns,
			  size_t insns_cnt, const char *license,
			  __u32 kern_version, char *log_buf,
			  size_t log_buf_sz);

/*
 * below function is exported for testing in prog_test test
 */
struct test_filter_set;
int parse_test_list(const char *s,
		    struct test_filter_set *test_set,
		    bool is_glob_pattern);
int parse_test_list_file(const char *path,
			 struct test_filter_set *test_set,
			 bool is_glob_pattern);

__u64 read_perf_max_sample_freq(void);
int load_bpf_testmod(bool verbose);
int unload_bpf_testmod(bool verbose);
int kern_sync_rcu(void);

static inline __u64 get_time_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);

	return (u64)t.tv_sec * 1000000000 + t.tv_nsec;
}

#endif /* __TESTING_HELPERS_H */
