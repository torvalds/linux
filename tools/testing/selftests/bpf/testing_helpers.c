// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */
/* Copyright (C) 2020 Facebook, Inc. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "test_progs.h"
#include "testing_helpers.h"
#include <linux/membarrier.h>

int parse_num_list(const char *s, bool **num_set, int *num_set_len)
{
	int i, set_len = 0, new_len, num, start = 0, end = -1;
	bool *set = NULL, *tmp, parsing_end = false;
	char *next;

	while (s[0]) {
		errno = 0;
		num = strtol(s, &next, 10);
		if (errno)
			return -errno;

		if (parsing_end)
			end = num;
		else
			start = num;

		if (!parsing_end && *next == '-') {
			s = next + 1;
			parsing_end = true;
			continue;
		} else if (*next == ',') {
			parsing_end = false;
			s = next + 1;
			end = num;
		} else if (*next == '\0') {
			parsing_end = false;
			s = next;
			end = num;
		} else {
			return -EINVAL;
		}

		if (start > end)
			return -EINVAL;

		if (end + 1 > set_len) {
			new_len = end + 1;
			tmp = realloc(set, new_len);
			if (!tmp) {
				free(set);
				return -ENOMEM;
			}
			for (i = set_len; i < start; i++)
				tmp[i] = false;
			set = tmp;
			set_len = new_len;
		}
		for (i = start; i <= end; i++)
			set[i] = true;
	}

	if (!set || parsing_end)
		return -EINVAL;

	*num_set = set;
	*num_set_len = set_len;

	return 0;
}

static int do_insert_test(struct test_filter_set *set,
			  char *test_str,
			  char *subtest_str)
{
	struct test_filter *tmp, *test;
	char **ctmp;
	int i;

	for (i = 0; i < set->cnt; i++) {
		test = &set->tests[i];

		if (strcmp(test_str, test->name) == 0) {
			free(test_str);
			goto subtest;
		}
	}

	tmp = realloc(set->tests, sizeof(*test) * (set->cnt + 1));
	if (!tmp)
		return -ENOMEM;

	set->tests = tmp;
	test = &set->tests[set->cnt];

	test->name = test_str;
	test->subtests = NULL;
	test->subtest_cnt = 0;

	set->cnt++;

subtest:
	if (!subtest_str)
		return 0;

	for (i = 0; i < test->subtest_cnt; i++) {
		if (strcmp(subtest_str, test->subtests[i]) == 0) {
			free(subtest_str);
			return 0;
		}
	}

	ctmp = realloc(test->subtests,
		       sizeof(*test->subtests) * (test->subtest_cnt + 1));
	if (!ctmp)
		return -ENOMEM;

	test->subtests = ctmp;
	test->subtests[test->subtest_cnt] = subtest_str;

	test->subtest_cnt++;

	return 0;
}

static int insert_test(struct test_filter_set *set,
		       char *test_spec,
		       bool is_glob_pattern)
{
	char *pattern, *subtest_str, *ext_test_str, *ext_subtest_str = NULL;
	int glob_chars = 0;

	if (is_glob_pattern) {
		pattern = "%s";
	} else {
		pattern = "*%s*";
		glob_chars = 2;
	}

	subtest_str = strchr(test_spec, '/');
	if (subtest_str) {
		*subtest_str = '\0';
		subtest_str += 1;
	}

	ext_test_str = malloc(strlen(test_spec) + glob_chars + 1);
	if (!ext_test_str)
		goto err;

	sprintf(ext_test_str, pattern, test_spec);

	if (subtest_str) {
		ext_subtest_str = malloc(strlen(subtest_str) + glob_chars + 1);
		if (!ext_subtest_str)
			goto err;

		sprintf(ext_subtest_str, pattern, subtest_str);
	}

	return do_insert_test(set, ext_test_str, ext_subtest_str);

err:
	free(ext_test_str);
	free(ext_subtest_str);

	return -ENOMEM;
}

int parse_test_list_file(const char *path,
			 struct test_filter_set *set,
			 bool is_glob_pattern)
{
	char *buf = NULL, *capture_start, *capture_end, *scan_end;
	size_t buflen = 0;
	int err = 0;
	FILE *f;

	f = fopen(path, "r");
	if (!f) {
		err = -errno;
		fprintf(stderr, "Failed to open '%s': %d\n", path, err);
		return err;
	}

	while (getline(&buf, &buflen, f) != -1) {
		capture_start = buf;

		while (isspace(*capture_start))
			++capture_start;

		capture_end = capture_start;
		scan_end = capture_start;

		while (*scan_end && *scan_end != '#') {
			if (!isspace(*scan_end))
				capture_end = scan_end;

			++scan_end;
		}

		if (capture_end == capture_start)
			continue;

		*(++capture_end) = '\0';

		err = insert_test(set, capture_start, is_glob_pattern);
		if (err)
			break;
	}

	fclose(f);
	return err;
}

int parse_test_list(const char *s,
		    struct test_filter_set *set,
		    bool is_glob_pattern)
{
	char *input, *state = NULL, *test_spec;
	int err = 0;

	input = strdup(s);
	if (!input)
		return -ENOMEM;

	while ((test_spec = strtok_r(state ? NULL : input, ",", &state))) {
		err = insert_test(set, test_spec, is_glob_pattern);
		if (err)
			break;
	}

	free(input);
	return err;
}

__u32 link_info_prog_id(const struct bpf_link *link, struct bpf_link_info *info)
{
	__u32 info_len = sizeof(*info);
	int err;

	memset(info, 0, sizeof(*info));
	err = bpf_link_get_info_by_fd(bpf_link__fd(link), info, &info_len);
	if (err) {
		printf("failed to get link info: %d\n", -errno);
		return 0;
	}
	return info->prog_id;
}

int extra_prog_load_log_flags = 0;

int bpf_prog_test_load(const char *file, enum bpf_prog_type type,
		       struct bpf_object **pobj, int *prog_fd)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts,
		.kernel_log_level = extra_prog_load_log_flags,
	);
	struct bpf_object *obj;
	struct bpf_program *prog;
	__u32 flags;
	int err;

	obj = bpf_object__open_file(file, &opts);
	if (!obj)
		return -errno;

	prog = bpf_object__next_program(obj, NULL);
	if (!prog) {
		err = -ENOENT;
		goto err_out;
	}

	if (type != BPF_PROG_TYPE_UNSPEC && bpf_program__type(prog) != type)
		bpf_program__set_type(prog, type);

	flags = bpf_program__flags(prog) | BPF_F_TEST_RND_HI32;
	bpf_program__set_flags(prog, flags);

	err = bpf_object__load(obj);
	if (err)
		goto err_out;

	*pobj = obj;
	*prog_fd = bpf_program__fd(prog);

	return 0;
err_out:
	bpf_object__close(obj);
	return err;
}

int bpf_test_load_program(enum bpf_prog_type type, const struct bpf_insn *insns,
			  size_t insns_cnt, const char *license,
			  __u32 kern_version, char *log_buf,
			  size_t log_buf_sz)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.kern_version = kern_version,
		.prog_flags = BPF_F_TEST_RND_HI32,
		.log_level = extra_prog_load_log_flags,
		.log_buf = log_buf,
		.log_size = log_buf_sz,
	);

	return bpf_prog_load(type, NULL, license, insns, insns_cnt, &opts);
}

__u64 read_perf_max_sample_freq(void)
{
	__u64 sample_freq = 5000; /* fallback to 5000 on error */
	FILE *f;

	f = fopen("/proc/sys/kernel/perf_event_max_sample_rate", "r");
	if (f == NULL) {
		printf("Failed to open /proc/sys/kernel/perf_event_max_sample_rate: err %d\n"
		       "return default value: 5000\n", -errno);
		return sample_freq;
	}
	if (fscanf(f, "%llu", &sample_freq) != 1) {
		printf("Failed to parse /proc/sys/kernel/perf_event_max_sample_rate: err %d\n"
		       "return default value: 5000\n", -errno);
	}

	fclose(f);
	return sample_freq;
}

static int finit_module(int fd, const char *param_values, int flags)
{
	return syscall(__NR_finit_module, fd, param_values, flags);
}

static int delete_module(const char *name, int flags)
{
	return syscall(__NR_delete_module, name, flags);
}

int unload_bpf_testmod(bool verbose)
{
	if (kern_sync_rcu())
		fprintf(stdout, "Failed to trigger kernel-side RCU sync!\n");
	if (delete_module("bpf_testmod", 0)) {
		if (errno == ENOENT) {
			if (verbose)
				fprintf(stdout, "bpf_testmod.ko is already unloaded.\n");
			return -1;
		}
		fprintf(stdout, "Failed to unload bpf_testmod.ko from kernel: %d\n", -errno);
		return -1;
	}
	if (verbose)
		fprintf(stdout, "Successfully unloaded bpf_testmod.ko.\n");
	return 0;
}

int load_bpf_testmod(bool verbose)
{
	int fd;

	if (verbose)
		fprintf(stdout, "Loading bpf_testmod.ko...\n");

	fd = open("bpf_testmod.ko", O_RDONLY);
	if (fd < 0) {
		fprintf(stdout, "Can't find bpf_testmod.ko kernel module: %d\n", -errno);
		return -ENOENT;
	}
	if (finit_module(fd, "", 0)) {
		fprintf(stdout, "Failed to load bpf_testmod.ko into the kernel: %d\n", -errno);
		close(fd);
		return -EINVAL;
	}
	close(fd);

	if (verbose)
		fprintf(stdout, "Successfully loaded bpf_testmod.ko.\n");
	return 0;
}

/*
 * Trigger synchronize_rcu() in kernel.
 */
int kern_sync_rcu(void)
{
	return syscall(__NR_membarrier, MEMBARRIER_CMD_SHARED, 0, 0);
}
