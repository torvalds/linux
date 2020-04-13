// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Carlos Neira cneirabustos@gmail.com */
#include <test_progs.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

struct bss {
	__u64 dev;
	__u64 ino;
	__u64 pid_tgid;
	__u64 user_pid_tgid;
};

void test_ns_current_pid_tgid(void)
{
	const char *probe_name = "raw_tracepoint/sys_enter";
	const char *file = "test_ns_current_pid_tgid.o";
	int err, key = 0, duration = 0;
	struct bpf_link *link = NULL;
	struct bpf_program *prog;
	struct bpf_map *bss_map;
	struct bpf_object *obj;
	struct bss bss;
	struct stat st;
	__u64 id;

	obj = bpf_object__open_file(file, NULL);
	if (CHECK(IS_ERR(obj), "obj_open", "err %ld\n", PTR_ERR(obj)))
		return;

	err = bpf_object__load(obj);
	if (CHECK(err, "obj_load", "err %d errno %d\n", err, errno))
		goto cleanup;

	bss_map = bpf_object__find_map_by_name(obj, "test_ns_.bss");
	if (CHECK(!bss_map, "find_bss_map", "failed\n"))
		goto cleanup;

	prog = bpf_object__find_program_by_title(obj, probe_name);
	if (CHECK(!prog, "find_prog", "prog '%s' not found\n",
		  probe_name))
		goto cleanup;

	memset(&bss, 0, sizeof(bss));
	pid_t tid = syscall(SYS_gettid);
	pid_t pid = getpid();

	id = (__u64) tid << 32 | pid;
	bss.user_pid_tgid = id;

	if (CHECK_FAIL(stat("/proc/self/ns/pid", &st))) {
		perror("Failed to stat /proc/self/ns/pid");
		goto cleanup;
	}

	bss.dev = st.st_dev;
	bss.ino = st.st_ino;

	err = bpf_map_update_elem(bpf_map__fd(bss_map), &key, &bss, 0);
	if (CHECK(err, "setting_bss", "failed to set bss : %d\n", err))
		goto cleanup;

	link = bpf_program__attach_raw_tracepoint(prog, "sys_enter");
	if (CHECK(IS_ERR(link), "attach_raw_tp", "err %ld\n",
		  PTR_ERR(link))) {
		link = NULL;
		goto cleanup;
	}

	/* trigger some syscalls */
	usleep(1);

	err = bpf_map_lookup_elem(bpf_map__fd(bss_map), &key, &bss);
	if (CHECK(err, "set_bss", "failed to get bss : %d\n", err))
		goto cleanup;

	if (CHECK(id != bss.pid_tgid, "Compare user pid/tgid vs. bpf pid/tgid",
		  "User pid/tgid %llu BPF pid/tgid %llu\n", id, bss.pid_tgid))
		goto cleanup;
cleanup:
	if (!link) {
		bpf_link__destroy(link);
		link = NULL;
	}
	bpf_object__close(obj);
}
