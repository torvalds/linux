// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Oracle and/or its affiliates. */

#include <test_progs.h>
#include <bpf/btf.h>

#include "test_unpriv_bpf_disabled.skel.h"

#include "cap_helpers.h"
#include "bpf_util.h"

/* Using CAP_LAST_CAP is risky here, since it can get pulled in from
 * an old /usr/include/linux/capability.h and be < CAP_BPF; as a result
 * CAP_BPF would not be included in ALL_CAPS.  Instead use CAP_BPF as
 * we know its value is correct since it is explicitly defined in
 * cap_helpers.h.
 */
#define ALL_CAPS	((2ULL << CAP_BPF) - 1)

#define PINPATH		"/sys/fs/bpf/unpriv_bpf_disabled_"
#define NUM_MAPS	7

static __u32 got_perfbuf_val;
static __u32 got_ringbuf_val;

static int process_ringbuf(void *ctx, void *data, size_t len)
{
	if (ASSERT_EQ(len, sizeof(__u32), "ringbuf_size_valid"))
		got_ringbuf_val = *(__u32 *)data;
	return 0;
}

static void process_perfbuf(void *ctx, int cpu, void *data, __u32 len)
{
	if (ASSERT_EQ(len, sizeof(__u32), "perfbuf_size_valid"))
		got_perfbuf_val = *(__u32 *)data;
}

static int sysctl_set(const char *sysctl_path, char *old_val, const char *new_val)
{
	int ret = 0;
	FILE *fp;

	fp = fopen(sysctl_path, "r+");
	if (!fp)
		return -errno;
	if (old_val && fscanf(fp, "%s", old_val) <= 0) {
		ret = -ENOENT;
	} else if (!old_val || strcmp(old_val, new_val) != 0) {
		fseek(fp, 0, SEEK_SET);
		if (fprintf(fp, "%s", new_val) < 0)
			ret = -errno;
	}
	fclose(fp);

	return ret;
}

static void test_unpriv_bpf_disabled_positive(struct test_unpriv_bpf_disabled *skel,
					      __u32 prog_id, int prog_fd, int perf_fd,
					      char **map_paths, int *map_fds)
{
	struct perf_buffer *perfbuf = NULL;
	struct ring_buffer *ringbuf = NULL;
	int i, nr_cpus, link_fd = -1;

	nr_cpus = bpf_num_possible_cpus();

	skel->bss->perfbuf_val = 1;
	skel->bss->ringbuf_val = 2;

	/* Positive tests for unprivileged BPF disabled. Verify we can
	 * - retrieve and interact with pinned maps;
	 * - set up and interact with perf buffer;
	 * - set up and interact with ring buffer;
	 * - create a link
	 */
	perfbuf = perf_buffer__new(bpf_map__fd(skel->maps.perfbuf), 8, process_perfbuf, NULL, NULL,
				   NULL);
	if (!ASSERT_OK_PTR(perfbuf, "perf_buffer__new"))
		goto cleanup;

	ringbuf = ring_buffer__new(bpf_map__fd(skel->maps.ringbuf), process_ringbuf, NULL, NULL);
	if (!ASSERT_OK_PTR(ringbuf, "ring_buffer__new"))
		goto cleanup;

	/* trigger & validate perf event, ringbuf output */
	usleep(1);

	ASSERT_GT(perf_buffer__poll(perfbuf, 100), -1, "perf_buffer__poll");
	ASSERT_EQ(got_perfbuf_val, skel->bss->perfbuf_val, "check_perfbuf_val");
	ASSERT_EQ(ring_buffer__consume(ringbuf), 1, "ring_buffer__consume");
	ASSERT_EQ(got_ringbuf_val, skel->bss->ringbuf_val, "check_ringbuf_val");

	for (i = 0; i < NUM_MAPS; i++) {
		map_fds[i] = bpf_obj_get(map_paths[i]);
		if (!ASSERT_GT(map_fds[i], -1, "obj_get"))
			goto cleanup;
	}

	for (i = 0; i < NUM_MAPS; i++) {
		bool prog_array = strstr(map_paths[i], "prog_array") != NULL;
		bool array = strstr(map_paths[i], "array") != NULL;
		bool buf = strstr(map_paths[i], "buf") != NULL;
		__u32 key = 0, vals[nr_cpus], lookup_vals[nr_cpus];
		__u32 expected_val = 1;
		int j;

		/* skip ringbuf, perfbuf */
		if (buf)
			continue;

		for (j = 0; j < nr_cpus; j++)
			vals[j] = expected_val;

		if (prog_array) {
			/* need valid prog array value */
			vals[0] = prog_fd;
			/* prog array lookup returns prog id, not fd */
			expected_val = prog_id;
		}
		ASSERT_OK(bpf_map_update_elem(map_fds[i], &key, vals, 0), "map_update_elem");
		ASSERT_OK(bpf_map_lookup_elem(map_fds[i], &key, &lookup_vals), "map_lookup_elem");
		ASSERT_EQ(lookup_vals[0], expected_val, "map_lookup_elem_values");
		if (!array)
			ASSERT_OK(bpf_map_delete_elem(map_fds[i], &key), "map_delete_elem");
	}

	link_fd = bpf_link_create(bpf_program__fd(skel->progs.handle_perf_event), perf_fd,
				  BPF_PERF_EVENT, NULL);
	ASSERT_GT(link_fd, 0, "link_create");

cleanup:
	if (link_fd)
		close(link_fd);
	if (perfbuf)
		perf_buffer__free(perfbuf);
	if (ringbuf)
		ring_buffer__free(ringbuf);
}

static void test_unpriv_bpf_disabled_negative(struct test_unpriv_bpf_disabled *skel,
					      __u32 prog_id, int prog_fd, int perf_fd,
					      char **map_paths, int *map_fds)
{
	const struct bpf_insn prog_insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	const size_t prog_insn_cnt = ARRAY_SIZE(prog_insns);
	LIBBPF_OPTS(bpf_prog_load_opts, load_opts);
	struct bpf_map_info map_info = {};
	__u32 map_info_len = sizeof(map_info);
	struct bpf_link_info link_info = {};
	__u32 link_info_len = sizeof(link_info);
	struct btf *btf = NULL;
	__u32 attach_flags = 0;
	__u32 prog_ids[3] = {};
	__u32 prog_cnt = 3;
	__u32 next;
	int i;

	/* Negative tests for unprivileged BPF disabled.  Verify we cannot
	 * - load BPF programs;
	 * - create BPF maps;
	 * - get a prog/map/link fd by id;
	 * - get next prog/map/link id
	 * - query prog
	 * - BTF load
	 */
	ASSERT_EQ(bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, "simple_prog", "GPL",
				prog_insns, prog_insn_cnt, &load_opts),
		  -EPERM, "prog_load_fails");

	/* some map types require particular correct parameters which could be
	 * sanity-checked before enforcing -EPERM, so only validate that
	 * the simple ARRAY and HASH maps are failing with -EPERM
	 */
	for (i = BPF_MAP_TYPE_HASH; i <= BPF_MAP_TYPE_ARRAY; i++)
		ASSERT_EQ(bpf_map_create(i, NULL, sizeof(int), sizeof(int), 1, NULL),
			  -EPERM, "map_create_fails");

	ASSERT_EQ(bpf_prog_get_fd_by_id(prog_id), -EPERM, "prog_get_fd_by_id_fails");
	ASSERT_EQ(bpf_prog_get_next_id(prog_id, &next), -EPERM, "prog_get_next_id_fails");
	ASSERT_EQ(bpf_prog_get_next_id(0, &next), -EPERM, "prog_get_next_id_fails");

	if (ASSERT_OK(bpf_map_get_info_by_fd(map_fds[0], &map_info, &map_info_len),
		      "obj_get_info_by_fd")) {
		ASSERT_EQ(bpf_map_get_fd_by_id(map_info.id), -EPERM, "map_get_fd_by_id_fails");
		ASSERT_EQ(bpf_map_get_next_id(map_info.id, &next), -EPERM,
			  "map_get_next_id_fails");
	}
	ASSERT_EQ(bpf_map_get_next_id(0, &next), -EPERM, "map_get_next_id_fails");

	if (ASSERT_OK(bpf_link_get_info_by_fd(bpf_link__fd(skel->links.sys_nanosleep_enter),
					      &link_info, &link_info_len),
		      "obj_get_info_by_fd")) {
		ASSERT_EQ(bpf_link_get_fd_by_id(link_info.id), -EPERM, "link_get_fd_by_id_fails");
		ASSERT_EQ(bpf_link_get_next_id(link_info.id, &next), -EPERM,
			  "link_get_next_id_fails");
	}
	ASSERT_EQ(bpf_link_get_next_id(0, &next), -EPERM, "link_get_next_id_fails");

	ASSERT_EQ(bpf_prog_query(prog_fd, BPF_TRACE_FENTRY, 0, &attach_flags, prog_ids,
				 &prog_cnt), -EPERM, "prog_query_fails");

	btf = btf__new_empty();
	if (ASSERT_OK_PTR(btf, "empty_btf") &&
	    ASSERT_GT(btf__add_int(btf, "int", 4, 0), 0, "unpriv_int_type")) {
		const void *raw_btf_data;
		__u32 raw_btf_size;

		raw_btf_data = btf__raw_data(btf, &raw_btf_size);
		if (ASSERT_OK_PTR(raw_btf_data, "raw_btf_data_good"))
			ASSERT_EQ(bpf_btf_load(raw_btf_data, raw_btf_size, NULL), -EPERM,
				  "bpf_btf_load_fails");
	}
	btf__free(btf);
}

void test_unpriv_bpf_disabled(void)
{
	char *map_paths[NUM_MAPS] = {	PINPATH	"array",
					PINPATH "percpu_array",
					PINPATH "hash",
					PINPATH "percpu_hash",
					PINPATH "perfbuf",
					PINPATH "ringbuf",
					PINPATH "prog_array" };
	int map_fds[NUM_MAPS];
	struct test_unpriv_bpf_disabled *skel;
	char unprivileged_bpf_disabled_orig[32] = {};
	char perf_event_paranoid_orig[32] = {};
	struct bpf_prog_info prog_info = {};
	__u32 prog_info_len = sizeof(prog_info);
	struct perf_event_attr attr = {};
	int prog_fd, perf_fd = -1, i, ret;
	__u64 save_caps = 0;
	__u32 prog_id;

	skel = test_unpriv_bpf_disabled__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->test_pid = getpid();

	map_fds[0] = bpf_map__fd(skel->maps.array);
	map_fds[1] = bpf_map__fd(skel->maps.percpu_array);
	map_fds[2] = bpf_map__fd(skel->maps.hash);
	map_fds[3] = bpf_map__fd(skel->maps.percpu_hash);
	map_fds[4] = bpf_map__fd(skel->maps.perfbuf);
	map_fds[5] = bpf_map__fd(skel->maps.ringbuf);
	map_fds[6] = bpf_map__fd(skel->maps.prog_array);

	for (i = 0; i < NUM_MAPS; i++)
		ASSERT_OK(bpf_obj_pin(map_fds[i], map_paths[i]), "pin map_fd");

	/* allow user without caps to use perf events */
	if (!ASSERT_OK(sysctl_set("/proc/sys/kernel/perf_event_paranoid", perf_event_paranoid_orig,
				  "-1"),
		       "set_perf_event_paranoid"))
		goto cleanup;
	/* ensure unprivileged bpf disabled is set */
	ret = sysctl_set("/proc/sys/kernel/unprivileged_bpf_disabled",
			 unprivileged_bpf_disabled_orig, "2");
	if (ret == -EPERM) {
		/* if unprivileged_bpf_disabled=1, we get -EPERM back; that's okay. */
		if (!ASSERT_OK(strcmp(unprivileged_bpf_disabled_orig, "1"),
			       "unprivileged_bpf_disabled_on"))
			goto cleanup;
	} else {
		if (!ASSERT_OK(ret, "set unprivileged_bpf_disabled"))
			goto cleanup;
	}

	prog_fd = bpf_program__fd(skel->progs.sys_nanosleep_enter);
	ASSERT_OK(bpf_prog_get_info_by_fd(prog_fd, &prog_info, &prog_info_len),
		  "obj_get_info_by_fd");
	prog_id = prog_info.id;
	ASSERT_GT(prog_id, 0, "valid_prog_id");

	attr.size = sizeof(attr);
	attr.type = PERF_TYPE_SOFTWARE;
	attr.config = PERF_COUNT_SW_CPU_CLOCK;
	attr.freq = 1;
	attr.sample_freq = 1000;
	perf_fd = syscall(__NR_perf_event_open, &attr, -1, 0, -1, PERF_FLAG_FD_CLOEXEC);
	if (!ASSERT_GE(perf_fd, 0, "perf_fd"))
		goto cleanup;

	if (!ASSERT_OK(test_unpriv_bpf_disabled__attach(skel), "skel_attach"))
		goto cleanup;

	if (!ASSERT_OK(cap_disable_effective(ALL_CAPS, &save_caps), "disable caps"))
		goto cleanup;

	if (test__start_subtest("unpriv_bpf_disabled_positive"))
		test_unpriv_bpf_disabled_positive(skel, prog_id, prog_fd, perf_fd, map_paths,
						  map_fds);

	if (test__start_subtest("unpriv_bpf_disabled_negative"))
		test_unpriv_bpf_disabled_negative(skel, prog_id, prog_fd, perf_fd, map_paths,
						  map_fds);

cleanup:
	close(perf_fd);
	if (save_caps)
		cap_enable_effective(save_caps, NULL);
	if (strlen(perf_event_paranoid_orig) > 0)
		sysctl_set("/proc/sys/kernel/perf_event_paranoid", NULL, perf_event_paranoid_orig);
	if (strlen(unprivileged_bpf_disabled_orig) > 0)
		sysctl_set("/proc/sys/kernel/unprivileged_bpf_disabled", NULL,
			   unprivileged_bpf_disabled_orig);
	for (i = 0; i < NUM_MAPS; i++)
		unlink(map_paths[i]);
	test_unpriv_bpf_disabled__destroy(skel);
}
