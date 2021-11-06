// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

#define nr_iters 2

void serial_test_bpf_obj_id(void)
{
	const __u64 array_magic_value = 0xfaceb00c;
	const __u32 array_key = 0;
	const char *file = "./test_obj_id.o";
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
	__u32 i, next_id, info_len, nr_id_found, duration = 0;
	struct timespec real_time_ts, boot_time_ts;
	int err = 0;
	__u64 array_value;
	uid_t my_uid = getuid();
	time_t now, load_time;

	err = bpf_prog_get_fd_by_id(0);
	CHECK(err >= 0 || errno != ENOENT,
	      "get-fd-by-notexist-prog-id", "err %d errno %d\n", err, errno);

	err = bpf_map_get_fd_by_id(0);
	CHECK(err >= 0 || errno != ENOENT,
	      "get-fd-by-notexist-map-id", "err %d errno %d\n", err, errno);

	err = bpf_link_get_fd_by_id(0);
	CHECK(err >= 0 || errno != ENOENT,
	      "get-fd-by-notexist-link-id", "err %d errno %d\n", err, errno);

	/* Check bpf_obj_get_info_by_fd() */
	bzero(zeros, sizeof(zeros));
	for (i = 0; i < nr_iters; i++) {
		now = time(NULL);
		err = bpf_prog_load(file, BPF_PROG_TYPE_RAW_TRACEPOINT,
				    &objs[i], &prog_fds[i]);
		/* test_obj_id.o is a dumb prog. It should never fail
		 * to load.
		 */
		if (CHECK_FAIL(err))
			continue;

		/* Insert a magic value to the map */
		map_fds[i] = bpf_find_map(__func__, objs[i], "test_map_id");
		if (CHECK_FAIL(map_fds[i] < 0))
			goto done;
		err = bpf_map_update_elem(map_fds[i], &array_key,
					  &array_magic_value, 0);
		if (CHECK_FAIL(err))
			goto done;

		prog = bpf_object__find_program_by_title(objs[i],
							 "raw_tp/sys_enter");
		if (CHECK_FAIL(!prog))
			goto done;
		links[i] = bpf_program__attach(prog);
		err = libbpf_get_error(links[i]);
		if (CHECK(err, "prog_attach", "prog #%d, err %d\n", i, err)) {
			links[i] = NULL;
			goto done;
		}

		/* Check getting map info */
		info_len = sizeof(struct bpf_map_info) * 2;
		bzero(&map_infos[i], info_len);
		err = bpf_obj_get_info_by_fd(map_fds[i], &map_infos[i],
					     &info_len);
		if (CHECK(err ||
			  map_infos[i].type != BPF_MAP_TYPE_ARRAY ||
			  map_infos[i].key_size != sizeof(__u32) ||
			  map_infos[i].value_size != sizeof(__u64) ||
			  map_infos[i].max_entries != 1 ||
			  map_infos[i].map_flags != 0 ||
			  info_len != sizeof(struct bpf_map_info) ||
			  strcmp((char *)map_infos[i].name, expected_map_name),
			  "get-map-info(fd)",
			  "err %d errno %d type %d(%d) info_len %u(%zu) key_size %u value_size %u max_entries %u map_flags %X name %s(%s)\n",
			  err, errno,
			  map_infos[i].type, BPF_MAP_TYPE_ARRAY,
			  info_len, sizeof(struct bpf_map_info),
			  map_infos[i].key_size,
			  map_infos[i].value_size,
			  map_infos[i].max_entries,
			  map_infos[i].map_flags,
			  map_infos[i].name, expected_map_name))
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
		if (CHECK_FAIL(err))
			goto done;
		err = clock_gettime(CLOCK_BOOTTIME, &boot_time_ts);
		if (CHECK_FAIL(err))
			goto done;
		err = bpf_obj_get_info_by_fd(prog_fds[i], &prog_infos[i],
					     &info_len);
		load_time = (real_time_ts.tv_sec - boot_time_ts.tv_sec)
			+ (prog_infos[i].load_time / nsec_per_sec);
		if (CHECK(err ||
			  prog_infos[i].type != BPF_PROG_TYPE_RAW_TRACEPOINT ||
			  info_len != sizeof(struct bpf_prog_info) ||
			  (env.jit_enabled && !prog_infos[i].jited_prog_len) ||
			  (env.jit_enabled &&
			   !memcmp(jited_insns, zeros, sizeof(zeros))) ||
			  !prog_infos[i].xlated_prog_len ||
			  !memcmp(xlated_insns, zeros, sizeof(zeros)) ||
			  load_time < now - 60 || load_time > now + 60 ||
			  prog_infos[i].created_by_uid != my_uid ||
			  prog_infos[i].nr_map_ids != 1 ||
			  *(int *)(long)prog_infos[i].map_ids != map_infos[i].id ||
			  strcmp((char *)prog_infos[i].name, expected_prog_name),
			  "get-prog-info(fd)",
			  "err %d errno %d i %d type %d(%d) info_len %u(%zu) "
			  "jit_enabled %d jited_prog_len %u xlated_prog_len %u "
			  "jited_prog %d xlated_prog %d load_time %lu(%lu) "
			  "uid %u(%u) nr_map_ids %u(%u) map_id %u(%u) "
			  "name %s(%s)\n",
			  err, errno, i,
			  prog_infos[i].type, BPF_PROG_TYPE_SOCKET_FILTER,
			  info_len, sizeof(struct bpf_prog_info),
			  env.jit_enabled,
			  prog_infos[i].jited_prog_len,
			  prog_infos[i].xlated_prog_len,
			  !!memcmp(jited_insns, zeros, sizeof(zeros)),
			  !!memcmp(xlated_insns, zeros, sizeof(zeros)),
			  load_time, now,
			  prog_infos[i].created_by_uid, my_uid,
			  prog_infos[i].nr_map_ids, 1,
			  *(int *)(long)prog_infos[i].map_ids, map_infos[i].id,
			  prog_infos[i].name, expected_prog_name))
			goto done;

		/* Check getting link info */
		info_len = sizeof(struct bpf_link_info) * 2;
		bzero(&link_infos[i], info_len);
		link_infos[i].raw_tracepoint.tp_name = ptr_to_u64(&tp_name);
		link_infos[i].raw_tracepoint.tp_name_len = sizeof(tp_name);
		err = bpf_obj_get_info_by_fd(bpf_link__fd(links[i]),
					     &link_infos[i], &info_len);
		if (CHECK(err ||
			  link_infos[i].type != BPF_LINK_TYPE_RAW_TRACEPOINT ||
			  link_infos[i].prog_id != prog_infos[i].id ||
			  link_infos[i].raw_tracepoint.tp_name != ptr_to_u64(&tp_name) ||
			  strcmp(u64_to_ptr(link_infos[i].raw_tracepoint.tp_name),
				 "sys_enter") ||
			  info_len != sizeof(struct bpf_link_info),
			  "get-link-info(fd)",
			  "err %d errno %d info_len %u(%zu) type %d(%d) id %d "
			  "prog_id %d (%d) tp_name %s(%s)\n",
			  err, errno,
			  info_len, sizeof(struct bpf_link_info),
			  link_infos[i].type, BPF_LINK_TYPE_RAW_TRACEPOINT,
			  link_infos[i].id,
			  link_infos[i].prog_id, prog_infos[i].id,
			  (const char *)u64_to_ptr(link_infos[i].raw_tracepoint.tp_name),
			  "sys_enter"))
			goto done;

	}

	/* Check bpf_prog_get_next_id() */
	nr_id_found = 0;
	next_id = 0;
	while (!bpf_prog_get_next_id(next_id, &next_id)) {
		struct bpf_prog_info prog_info = {};
		__u32 saved_map_id;
		int prog_fd;

		info_len = sizeof(prog_info);

		prog_fd = bpf_prog_get_fd_by_id(next_id);
		if (prog_fd < 0 && errno == ENOENT)
			/* The bpf_prog is in the dead row */
			continue;
		if (CHECK(prog_fd < 0, "get-prog-fd(next_id)",
			  "prog_fd %d next_id %d errno %d\n",
			  prog_fd, next_id, errno))
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
		err = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &info_len);
		if (CHECK(!err || errno != EFAULT,
			  "get-prog-fd-bad-nr-map-ids", "err %d errno %d(%d)",
			  err, errno, EFAULT))
			break;
		bzero(&prog_info, sizeof(prog_info));
		info_len = sizeof(prog_info);

		saved_map_id = *(int *)((long)prog_infos[i].map_ids);
		prog_info.map_ids = prog_infos[i].map_ids;
		prog_info.nr_map_ids = 2;
		err = bpf_obj_get_info_by_fd(prog_fd, &prog_info, &info_len);
		prog_infos[i].jited_prog_insns = 0;
		prog_infos[i].xlated_prog_insns = 0;
		CHECK(err || info_len != sizeof(struct bpf_prog_info) ||
		      memcmp(&prog_info, &prog_infos[i], info_len) ||
		      *(int *)(long)prog_info.map_ids != saved_map_id,
		      "get-prog-info(next_id->fd)",
		      "err %d errno %d info_len %u(%zu) memcmp %d map_id %u(%u)\n",
		      err, errno, info_len, sizeof(struct bpf_prog_info),
		      memcmp(&prog_info, &prog_infos[i], info_len),
		      *(int *)(long)prog_info.map_ids, saved_map_id);
		close(prog_fd);
	}
	CHECK(nr_id_found != nr_iters,
	      "check total prog id found by get_next_id",
	      "nr_id_found %u(%u)\n",
	      nr_id_found, nr_iters);

	/* Check bpf_map_get_next_id() */
	nr_id_found = 0;
	next_id = 0;
	while (!bpf_map_get_next_id(next_id, &next_id)) {
		struct bpf_map_info map_info = {};
		int map_fd;

		info_len = sizeof(map_info);

		map_fd = bpf_map_get_fd_by_id(next_id);
		if (map_fd < 0 && errno == ENOENT)
			/* The bpf_map is in the dead row */
			continue;
		if (CHECK(map_fd < 0, "get-map-fd(next_id)",
			  "map_fd %d next_id %u errno %d\n",
			  map_fd, next_id, errno))
			break;

		for (i = 0; i < nr_iters; i++)
			if (map_infos[i].id == next_id)
				break;

		if (i == nr_iters)
			continue;

		nr_id_found++;

		err = bpf_map_lookup_elem(map_fd, &array_key, &array_value);
		if (CHECK_FAIL(err))
			goto done;

		err = bpf_obj_get_info_by_fd(map_fd, &map_info, &info_len);
		CHECK(err || info_len != sizeof(struct bpf_map_info) ||
		      memcmp(&map_info, &map_infos[i], info_len) ||
		      array_value != array_magic_value,
		      "check get-map-info(next_id->fd)",
		      "err %d errno %d info_len %u(%zu) memcmp %d array_value %llu(%llu)\n",
		      err, errno, info_len, sizeof(struct bpf_map_info),
		      memcmp(&map_info, &map_infos[i], info_len),
		      array_value, array_magic_value);

		close(map_fd);
	}
	CHECK(nr_id_found != nr_iters,
	      "check total map id found by get_next_id",
	      "nr_id_found %u(%u)\n",
	      nr_id_found, nr_iters);

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
		if (CHECK(link_fd < 0, "get-link-fd(next_id)",
			  "link_fd %d next_id %u errno %d\n",
			  link_fd, next_id, errno))
			break;

		for (i = 0; i < nr_iters; i++)
			if (link_infos[i].id == next_id)
				break;

		if (i == nr_iters)
			continue;

		nr_id_found++;

		err = bpf_obj_get_info_by_fd(link_fd, &link_info, &info_len);
		cmp_res = memcmp(&link_info, &link_infos[i],
				offsetof(struct bpf_link_info, raw_tracepoint));
		CHECK(err || info_len != sizeof(link_info) || cmp_res,
		      "check get-link-info(next_id->fd)",
		      "err %d errno %d info_len %u(%zu) memcmp %d\n",
		      err, errno, info_len, sizeof(struct bpf_link_info),
		      cmp_res);

		close(link_fd);
	}
	CHECK(nr_id_found != nr_iters,
	      "check total link id found by get_next_id",
	      "nr_id_found %u(%u)\n", nr_id_found, nr_iters);

done:
	for (i = 0; i < nr_iters; i++) {
		bpf_link__destroy(links[i]);
		bpf_object__close(objs[i]);
	}
}
