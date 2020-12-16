// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Carlos Neira cneirabustos@gmail.com */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include "test_progs.h"

#define CHECK_NEWNS(condition, tag, format...) ({		\
	int __ret = !!(condition);			\
	if (__ret) {					\
		printf("%s:FAIL:%s ", __func__, tag);	\
		printf(format);				\
	} else {					\
		printf("%s:PASS:%s\n", __func__, tag);	\
	}						\
	__ret;						\
})

struct bss {
	__u64 dev;
	__u64 ino;
	__u64 pid_tgid;
	__u64 user_pid_tgid;
};

int main(int argc, char **argv)
{
	pid_t pid;
	int exit_code = 1;
	struct stat st;

	printf("Testing bpf_get_ns_current_pid_tgid helper in new ns\n");

	if (stat("/proc/self/ns/pid", &st)) {
		perror("stat failed on /proc/self/ns/pid ns\n");
		printf("%s:FAILED\n", argv[0]);
		return exit_code;
	}

	if (CHECK_NEWNS(unshare(CLONE_NEWPID | CLONE_NEWNS),
			"unshare CLONE_NEWPID | CLONE_NEWNS", "error errno=%d\n", errno))
		return exit_code;

	pid = fork();
	if (pid == -1) {
		perror("Fork() failed\n");
		printf("%s:FAILED\n", argv[0]);
		return exit_code;
	}

	if (pid > 0) {
		int status;

		usleep(5);
		waitpid(pid, &status, 0);
		return 0;
	} else {

		pid = fork();
		if (pid == -1) {
			perror("Fork() failed\n");
			printf("%s:FAILED\n", argv[0]);
			return exit_code;
		}

		if (pid > 0) {
			int status;
			waitpid(pid, &status, 0);
			return 0;
		} else {
			if (CHECK_NEWNS(mount("none", "/proc", NULL, MS_PRIVATE|MS_REC, NULL),
				"Unmounting proc", "Cannot umount proc! errno=%d\n", errno))
				return exit_code;

			if (CHECK_NEWNS(mount("proc", "/proc", "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL),
				"Mounting proc", "Cannot mount proc! errno=%d\n", errno))
				return exit_code;

			const char *probe_name = "raw_tracepoint/sys_enter";
			const char *file = "test_ns_current_pid_tgid.o";
			struct bpf_link *link = NULL;
			struct bpf_program *prog;
			struct bpf_map *bss_map;
			struct bpf_object *obj;
			int exit_code = 1;
			int err, key = 0;
			struct bss bss;
			struct stat st;
			__u64 id;

			obj = bpf_object__open_file(file, NULL);
			if (CHECK_NEWNS(IS_ERR(obj), "obj_open", "err %ld\n", PTR_ERR(obj)))
				return exit_code;

			err = bpf_object__load(obj);
			if (CHECK_NEWNS(err, "obj_load", "err %d errno %d\n", err, errno))
				goto cleanup;

			bss_map = bpf_object__find_map_by_name(obj, "test_ns_.bss");
			if (CHECK_NEWNS(!bss_map, "find_bss_map", "failed\n"))
				goto cleanup;

			prog = bpf_object__find_program_by_title(obj, probe_name);
			if (CHECK_NEWNS(!prog, "find_prog", "prog '%s' not found\n",
						probe_name))
				goto cleanup;

			memset(&bss, 0, sizeof(bss));
			pid_t tid = syscall(SYS_gettid);
			pid_t pid = getpid();

			id = (__u64) tid << 32 | pid;
			bss.user_pid_tgid = id;

			if (CHECK_NEWNS(stat("/proc/self/ns/pid", &st),
				"stat new ns", "Failed to stat /proc/self/ns/pid errno=%d\n", errno))
				goto cleanup;

			bss.dev = st.st_dev;
			bss.ino = st.st_ino;

			err = bpf_map_update_elem(bpf_map__fd(bss_map), &key, &bss, 0);
			if (CHECK_NEWNS(err, "setting_bss", "failed to set bss : %d\n", err))
				goto cleanup;

			link = bpf_program__attach_raw_tracepoint(prog, "sys_enter");
			if (CHECK_NEWNS(IS_ERR(link), "attach_raw_tp", "err %ld\n",
						PTR_ERR(link))) {
				link = NULL;
				goto cleanup;
			}

			/* trigger some syscalls */
			usleep(1);

			err = bpf_map_lookup_elem(bpf_map__fd(bss_map), &key, &bss);
			if (CHECK_NEWNS(err, "set_bss", "failed to get bss : %d\n", err))
				goto cleanup;

			if (CHECK_NEWNS(id != bss.pid_tgid, "Compare user pid/tgid vs. bpf pid/tgid",
						"User pid/tgid %llu BPF pid/tgid %llu\n", id, bss.pid_tgid))
				goto cleanup;

			exit_code = 0;
			printf("%s:PASS\n", argv[0]);
cleanup:
			if (!link) {
				bpf_link__destroy(link);
				link = NULL;
			}
			bpf_object__close(obj);
		}
	}
	return 0;
}
