// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

#define nr_iters 2

void serial_test_bpf_obj_id(void)
{
	const __u64 array_magic_value = 0xfaceb00c;
	const __u32 array_key = 0;
	const char *file = "./test_obj_id.bpf.o";
	const char *expected_prog_name = "test_obj_id";
	const char *expected_map_name = "test_map_id";
	const __u64 nsec_per_sec = 1000000000;

	struct bpf_object *objs[nr_iters] = {};
	struct bpf_link *links[nr_iters] = {};
	struct bpf_program *prog;
	int prog_fds[nr_iters], map_fds[nr_iters];
	/* +1 to test for the info_len returned by kernel */
	struct bpf_prog_info prog_infos[nr_iters + 1];
	struct bpf_map_info map_infos[nr_iters + 1];
	struct bpf_link_info link_infos[nr_iters + 1];
	/* Each prog only uses one map. +1 to test nr_map_ids
	 * returned by kernel.
	 */
	__u32 map_ids[nr_iters + 1];
	char jited_insns[128], xlated_insns[128], zeros[128], tp_name[128];
	__u32 i, next_id, info_len, nr_id_found;
	struct timespec real_time_ts, boot_time_ts;
	int err = 0;
	__u64 array_value;
	uid_t my_uid = getuid();
	time_t now, load_time;

	err = bpf_prog_get_fd_by_id(0);
	ASSERT_LT(err, 0, "bpf_prog_get_fd_by_id");
	ASSERT_EQ(errno, ENOENT, "bpf_prog_get_fd_by_id");

	err = bpf_map_get_fd_by_id(0);
	ASSERT_LT(err, 0, "bpf_map_get_fd_by_id");
	ASSERT_EQ(errno, ENOENT, "bpf_map_get_fd_by_id");

	err = bpf_link_get_fd_by_id(0);
	ASSERT_LT(err, 0, "bpf_map_get_fd_by_id");
	ASSERT_EQ(errno, ENOENT, "bpf_map_get_fd_by_id");

	/* Check bpf_map_get_info_by_fd() */
	bzero(zeros, sizeof(zeros));
	for (i = 0; i < nr_iters; i++) {
		now = time(NULL);
		err = bpf_prog_test_load(file, BPF_PROG_TYPE_RAW_TRACEPOINT,
				    &objs[i], &prog_fds[i]);
		/* test_obj_id.o is a dumb prog. It should never fail
		 * to load.
		 */
		if (!ASSERT_OK(err, "bpf_prog_test_load"))
			continue;

		/* Insert a magic value to the map */
		map_fds[i] = bpf_find_map(__func__, objs[i], "test_map_id");
		if (!ASSERT_GE(map_fds[i], 0, "bpf_find_map"))
			goto done;

		err = bpf_map_update_elem(map_fds[i], &array_key,
					  &array_magic_value, 0);
		if (!ASSERT_OK(err, "bpf_map_update_elem"))
			goto done;

		prog = bpf_object__find_program_by_name(objs[i], "test_obj_id");
		if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
			goto done;

		links[i] = bpf_program__attach(prog);
		err = libbpf_get_error(links[i]);
		if (!ASSERT_OK(err, "bpf_program__attach")) {
			links[i] = NULL;
			goto done;
		}

		/* Check getting map info */
		info_len = sizeof(struct bpf_map_info) * 2;
		bzero(&map_infos[i], info_len);
		err = bpf_map_get_info_by_fd(map_fds[i], &map_infos[i],
					     &info_len);
		if (!ASSERT_OK(err, "bpf_map_get_info_by_fd") ||
				!ASSERT_EQ(map_infos[i].type, BPF_MAP_TYPE_ARRAY, "map_type") ||
				!ASSERT_EQ(map_infos[i].key_size, sizeof(__u32), "key_size") ||
				!ASSERT_EQ(map_infos[i].value_size, sizeof(__u64), "value_size") ||
				!ASSERT_EQ(map_infos[i].max_entries, 1, "max_entries") ||
				!ASSERT_EQ(map_infos[i].map_flags, 0, "map_flags") ||
				!ASSERT_EQ(info_len, sizeof(struct bpf_map_info), "map_info_len") ||
				!ASSERT_STREQ((char *)map_infos[i].name, expected_map_name, "map_name"))
			goto done;

		/* Check getting prog info */
		info_len = sizeof(struct bpf_prog_info) * 2;
		bzero(&prog_infos[i], info_len);
		bzero(jited_insns, sizeof(jited_insns));
		bzero(xlated_insns, sizeof(xlated_insns));
		prog_infos[i].jited_prog_insns = ptr_to_u64(jited_insns);
		prog_infos[i].jited_prog_len = sizeof(jited_insns);
		prog_infos[i].xlated_prog_insns = ptr_to_u64(xlated_insns);
		prog_infos[i].xlated_prog_len = sizeof(xlated_insns);
		prog_infos[i].map_ids = ptr_to_u64(map_ids + i);
		prog_infos[i].nr_map_ids = 2;

		err = clock_gettime(CLOCK_REALTIME, &real_time_ts);
		if (!ASSERT_OK(err, "clock_gettime"))
			goto done;

		err = clock_gettime(CLOCK_BOOTTIME, &boot_time_ts);
		if (!ASSERT_OK(err, "clock_gettime"))
			goto done;

		err = bpf_prog_get_info_by_fd(prog_fds[i], &prog_infos[i],
					      &info_len);
		load_time = (real_time_ts.tv_sec - boot_time_ts.tv_sec)
			+ (prog_infos[i].load_time / nsec_per_sec);

		if (!ASSERT_OK(err, "bpf_prog_get_info_by_fd") ||
				!ASSERT_EQ(prog_infos[i].type, BPF_PROG_TYPE_RAW_TRACEPOINT, "prog_type") ||
				!ASSERT_EQ(info_len, sizeof(struct bpf_prog_info), "prog_info_len") ||
				!ASSERT_FALSE((env.jit_enabled && !prog_infos[i].jited_prog_len), "jited_prog_len") ||
				!ASSERT_FALSE((env.jit_enabled && !memcmp(jited_insns, zeros, sizeof(zeros))),
					"jited_insns") ||
				!ASSERT_NEQ(prog_infos[i].xlated_prog_len, 0, "xlated_prog_len") ||
				!ASSERT_NEQ(memcmp(xlated_insns, zeros, sizeof(zeros)), 0, "xlated_insns") ||
				!ASSERT_GE(load_time, (now - 60), "load_time") ||
				!ASSERT_LE(load_time, (now + 60), "load_time") ||
				!ASSERT_EQ(prog_infos[i].created_by_uid, my_uid, "created_by_uid") ||
				!ASSERT_EQ(prog_infos[i].nr_map_ids, 1, "nr_map_ids") ||
				!ASSERT_EQ(*(int *)(long)prog_infos[i].map_ids, map_infos[i].id, "map_ids") ||
				!ASSERT_STREQ((char *)prog_infos[i].name, expected_prog_name, "prog_name"))
			goto done;

		/* Check getting link info */
		info_len = sizeof(struct bpf_link_info) * 2;
		bzero(&link_infos[i], info_len);
		link_infos[i].raw_tracepoint.tp_name = ptr_to_u64(&tp_name);
		link_infos[i].raw_tracepoint.tp_name_len = sizeof(tp_name);
		err = bpf_link_get_info_by_fd(bpf_link__fd(links[i]),
					      &link_infos[i], &info_len);
		if (!ASSERT_OK(err, "bpf_link_get_info_by_fd") ||
				!ASSERT_EQ(link_infos[i].type, BPF_LINK_TYPE_RAW_TRACEPOINT, "link_type") ||
				!ASSERT_EQ(link_infos[i].prog_id, prog_infos[i].id, "prog_id") ||
				!ASSERT_EQ(link_infos[i].raw_tracepoint.tp_name, ptr_to_u64(&tp_name), "&tp_name") ||
				!ASSERT_STREQ(u64_to_ptr(link_infos[i].raw_tracepoint.tp_name), "sys_enter", "tp_name"))
			goto done;
	}

	/* Check bpf_prog_get_next_id() */
	nr_id_found = 0;
	next_id = 0;
	while (!bpf_prog_get_next_id(next_id, &next_id)) {
		struct bpf_prog_info prog_info = {};
		__u32 saved_map_id;
		int prog_fd, cmp_res;

		info_len = sizeof(prog_info);

		prog_fd = bpf_prog_get_fd_by_id(next_id);
		if (prog_fd < 0 && errno == ENOENT)
			/* The bpf_prog is in the dead row */
			continue;
		if (!ASSERT_GE(prog_fd, 0, "bpf_prog_get_fd_by_id"))
			break;

		for (i = 0; i < nr_iters; i++)
			if (prog_infos[i].id == next_id)
				break;

		if (i == nr_iters)
			continue;

		nr_id_found++;

		/* Negative test:
		 * prog_info.nr_map_ids = 1
		 * prog_info.map_ids = NULL
		 */
		prog_info.nr_map_ids = 1;
		err = bpf_prog_get_info_by_fd(prog_fd, &prog_info, &info_len);
		if (!ASSERT_ERR(err, "bpf_prog_get_info_by_fd") ||
				!ASSERT_EQ(errno, EFAULT, "bpf_prog_get_info_by_fd"))
			break;
		bzero(&prog_info, sizeof(prog_info));
		info_len = sizeof(prog_info);

		saved_map_id = *(int *)((long)prog_infos[i].map_ids);
		prog_info.map_ids = prog_infos[i].map_ids;
		prog_info.nr_map_ids = 2;
		err = bpf_prog_get_info_by_fd(prog_fd, &prog_info, &info_len);
		prog_infos[i].jited_prog_insns = 0;
		prog_infos[i].xlated_prog_insns = 0;
		cmp_res = memcmp(&prog_info, &prog_infos[i], info_len);

		ASSERT_OK(err, "bpf_prog_get_info_by_fd");
		ASSERT_EQ(info_len, sizeof(struct bpf_prog_info), "prog_info_len");
		ASSERT_OK(cmp_res, "memcmp");
		ASSERT_EQ(*(int *)(long)prog_info.map_ids, saved_map_id, "map_id");
		close(prog_fd);
	}
	ASSERT_EQ(nr_id_found, nr_iters, "prog_nr_id_found");

	/* Check bpf_map_get_next_id() */
	nr_id_found = 0;
	next_id = 0;
	while (!bpf_map_get_next_id(next_id, &next_id)) {
		struct bpf_map_info map_info = {};
		int map_fd, cmp_res;

		info_len = sizeof(map_info);

		map_fd = bpf_map_get_fd_by_id(next_id);
		if (map_fd < 0 && errno == ENOENT)
			/* The bpf_map is in the dead row */
			continue;
		if (!ASSERT_GE(map_fd, 0, "bpf_map_get_fd_by_id"))
			break;

		for (i = 0; i < nr_iters; i++)
			if (map_infos[i].id == next_id)
				break;

		if (i == nr_iters)
			continue;

		nr_id_found++;

		err = bpf_map_lookup_elem(map_fd, &array_key, &array_value);
		if (!ASSERT_OK(err, "bpf_map_lookup_elem"))
			goto done;

		err = bpf_map_get_info_by_fd(map_fd, &map_info, &info_len);
		cmp_res = memcmp(&map_info, &map_infos[i], info_len);
		ASSERT_OK(err, "bpf_map_get_info_by_fd");
		ASSERT_EQ(info_len, sizeof(struct bpf_map_info), "info_len");
		ASSERT_OK(cmp_res, "memcmp");
		ASSERT_EQ(array_value, array_magic_value, "array_value");

		close(map_fd);
	}
	ASSERT_EQ(nr_id_found, nr_iters, "map_nr_id_found");

	/* Check bpf_link_get_next_id() */
	nr_id_found = 0;
	next_id = 0;
	while (!bpf_link_get_next_id(next_id, &next_id)) {
		struct bpf_link_info link_info;
		int link_fd, cmp_res;

		info_len = sizeof(link_info);
		memset(&link_info, 0, info_len);

		link_fd = bpf_link_get_fd_by_id(next_id);
		if (link_fd < 0 && errno == ENOENT)
			/* The bpf_link is in the dead row */
			continue;
		if (!ASSERT_GE(link_fd, 0, "bpf_link_get_fd_by_id"))
			break;

		for (i = 0; i < nr_iters; i++)
			if (link_infos[i].id == next_id)
				break;

		if (i == nr_iters)
			continue;

		nr_id_found++;

		err = bpf_link_get_info_by_fd(link_fd, &link_info, &info_len);
		cmp_res = memcmp(&link_info, &link_infos[i],
				offsetof(struct bpf_link_info, raw_tracepoint));
		ASSERT_OK(err, "bpf_link_get_info_by_fd");
		ASSERT_EQ(info_len, sizeof(link_info), "info_len");
		ASSERT_OK(cmp_res, "memcmp");

		close(link_fd);
	}
	ASSERT_EQ(nr_id_found, nr_iters, "link_nr_id_found");

done:
	for (i = 0; i < nr_iters; i++) {
		bpf_link__destroy(links[i]);
		bpf_object__close(objs[i]);
	}
}
