// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (C) 2019 Netronome Systems, Inc. */
/* Copyright (C) 2020 Facebook, Inc. */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "test_progs.h"
#include "testing_helpers.h"

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

int parse_test_list(const char *s,
		    struct test_filter_set *set,
		    bool is_glob_pattern)
{
	char *input, *state = NULL, *next;
	struct test_filter *tmp, *tests = NULL;
	int i, j, cnt = 0;

	input = strdup(s);
	if (!input)
		return -ENOMEM;

	while ((next = strtok_r(state ? NULL : input, ",", &state))) {
		char *subtest_str = strchr(next, '/');
		char *pattern = NULL;
		int glob_chars = 0;

		tmp = realloc(tests, sizeof(*tests) * (cnt + 1));
		if (!tmp)
			goto err;
		tests = tmp;

		tests[cnt].subtest_cnt = 0;
		tests[cnt].subtests = NULL;

		if (is_glob_pattern) {
			pattern = "%s";
		} else {
			pattern = "*%s*";
			glob_chars = 2;
		}

		if (subtest_str) {
			char **tmp_subtests = NULL;
			int subtest_cnt = tests[cnt].subtest_cnt;

			*subtest_str = '\0';
			subtest_str += 1;
			tmp_subtests = realloc(tests[cnt].subtests,
					       sizeof(*tmp_subtests) *
					       (subtest_cnt + 1));
			if (!tmp_subtests)
				goto err;
			tests[cnt].subtests = tmp_subtests;

			tests[cnt].subtests[subtest_cnt] =
				malloc(strlen(subtest_str) + glob_chars + 1);
			if (!tests[cnt].subtests[subtest_cnt])
				goto err;
			sprintf(tests[cnt].subtests[subtest_cnt],
				pattern,
				subtest_str);

			tests[cnt].subtest_cnt++;
		}

		tests[cnt].name = malloc(strlen(next) + glob_chars + 1);
		if (!tests[cnt].name)
			goto err;
		sprintf(tests[cnt].name, pattern, next);

		cnt++;
	}

	tmp = realloc(set->tests, sizeof(*tests) * (cnt + set->cnt));
	if (!tmp)
		goto err;

	memcpy(tmp +  set->cnt, tests, sizeof(*tests) * cnt);
	set->tests = tmp;
	set->cnt += cnt;

	free(tests);
	free(input);
	return 0;

err:
	for (i = 0; i < cnt; i++) {
		for (j = 0; j < tests[i].subtest_cnt; j++)
			free(tests[i].subtests[j]);

		free(tests[i].name);
	}
	free(tests);
	free(input);
	return -ENOMEM;
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
