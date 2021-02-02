/* SPDX-License-Identifier: GPL-2.0 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cgroup_util.h"
#include "../clone3/clone3_selftests.h"

static ssize_t read_text(const char *path, char *buf, size_t max_len)
{
	ssize_t len;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;

	len = read(fd, buf, max_len - 1);
	if (len < 0)
		goto out;

	buf[len] = 0;
out:
	close(fd);
	return len;
}

static ssize_t write_text(const char *path, char *buf, ssize_t len)
{
	int fd;

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd < 0)
		return fd;

	len = write(fd, buf, len);
	if (len < 0) {
		close(fd);
		return len;
	}

	close(fd);

	return len;
}

char *cg_name(const char *root, const char *name)
{
	size_t len = strlen(root) + strlen(name) + 2;
	char *ret = malloc(len);

	snprintf(ret, len, "%s/%s", root, name);

	return ret;
}

char *cg_name_indexed(const char *root, const char *name, int index)
{
	size_t len = strlen(root) + strlen(name) + 10;
	char *ret = malloc(len);

	snprintf(ret, len, "%s/%s_%d", root, name, index);

	return ret;
}

char *cg_control(const char *cgroup, const char *control)
{
	size_t len = strlen(cgroup) + strlen(control) + 2;
	char *ret = malloc(len);

	snprintf(ret, len, "%s/%s", cgroup, control);

	return ret;
}

int cg_read(const char *cgroup, const char *control, char *buf, size_t len)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", cgroup, control);

	if (read_text(path, buf, len) >= 0)
		return 0;

	return -1;
}

int cg_read_strcmp(const char *cgroup, const char *control,
		   const char *expected)
{
	size_t size;
	char *buf;
	int ret;

	/* Handle the case of comparing against empty string */
	if (!expected)
		return -1;
	else
		size = strlen(expected) + 1;

	buf = malloc(size);
	if (!buf)
		return -1;

	if (cg_read(cgroup, control, buf, size)) {
		free(buf);
		return -1;
	}

	ret = strcmp(expected, buf);
	free(buf);
	return ret;
}

int cg_read_strstr(const char *cgroup, const char *control, const char *needle)
{
	char buf[PAGE_SIZE];

	if (cg_read(cgroup, control, buf, sizeof(buf)))
		return -1;

	return strstr(buf, needle) ? 0 : -1;
}

long cg_read_long(const char *cgroup, const char *control)
{
	char buf[128];

	if (cg_read(cgroup, control, buf, sizeof(buf)))
		return -1;

	return atol(buf);
}

long cg_read_key_long(const char *cgroup, const char *control, const char *key)
{
	char buf[PAGE_SIZE];
	char *ptr;

	if (cg_read(cgroup, control, buf, sizeof(buf)))
		return -1;

	ptr = strstr(buf, key);
	if (!ptr)
		return -1;

	return atol(ptr + strlen(key));
}

long cg_read_lc(const char *cgroup, const char *control)
{
	char buf[PAGE_SIZE];
	const char delim[] = "\n";
	char *line;
	long cnt = 0;

	if (cg_read(cgroup, control, buf, sizeof(buf)))
		return -1;

	for (line = strtok(buf, delim); line; line = strtok(NULL, delim))
		cnt++;

	return cnt;
}

int cg_write(const char *cgroup, const char *control, char *buf)
{
	char path[PATH_MAX];
	ssize_t len = strlen(buf);

	snprintf(path, sizeof(path), "%s/%s", cgroup, control);

	if (write_text(path, buf, len) == len)
		return 0;

	return -1;
}

int cg_find_unified_root(char *root, size_t len)
{
	char buf[10 * PAGE_SIZE];
	char *fs, *mount, *type;
	const char delim[] = "\n\t ";

	if (read_text("/proc/self/mounts", buf, sizeof(buf)) <= 0)
		return -1;

	/*
	 * Example:
	 * cgroup /sys/fs/cgroup cgroup2 rw,seclabel,noexec,relatime 0 0
	 */
	for (fs = strtok(buf, delim); fs; fs = strtok(NULL, delim)) {
		mount = strtok(NULL, delim);
		type = strtok(NULL, delim);
		strtok(NULL, delim);
		strtok(NULL, delim);
		strtok(NULL, delim);

		if (strcmp(type, "cgroup2") == 0) {
			strncpy(root, mount, len);
			return 0;
		}
	}

	return -1;
}

int cg_create(const char *cgroup)
{
	return mkdir(cgroup, 0644);
}

int cg_wait_for_proc_count(const char *cgroup, int count)
{
	char buf[10 * PAGE_SIZE] = {0};
	int attempts;
	char *ptr;

	for (attempts = 10; attempts >= 0; attempts--) {
		int nr = 0;

		if (cg_read(cgroup, "cgroup.procs", buf, sizeof(buf)))
			break;

		for (ptr = buf; *ptr; ptr++)
			if (*ptr == '\n')
				nr++;

		if (nr >= count)
			return 0;

		usleep(100000);
	}

	return -1;
}

int cg_killall(const char *cgroup)
{
	char buf[PAGE_SIZE];
	char *ptr = buf;

	if (cg_read(cgroup, "cgroup.procs", buf, sizeof(buf)))
		return -1;

	while (ptr < buf + sizeof(buf)) {
		int pid = strtol(ptr, &ptr, 10);

		if (pid == 0)
			break;
		if (*ptr)
			ptr++;
		else
			break;
		if (kill(pid, SIGKILL))
			return -1;
	}

	return 0;
}

int cg_destroy(const char *cgroup)
{
	int ret;

retry:
	ret = rmdir(cgroup);
	if (ret && errno == EBUSY) {
		cg_killall(cgroup);
		usleep(100);
		goto retry;
	}

	if (ret && errno == ENOENT)
		ret = 0;

	return ret;
}

int cg_enter(const char *cgroup, int pid)
{
	char pidbuf[64];

	snprintf(pidbuf, sizeof(pidbuf), "%d", pid);
	return cg_write(cgroup, "cgroup.procs", pidbuf);
}

int cg_enter_current(const char *cgroup)
{
	return cg_write(cgroup, "cgroup.procs", "0");
}

int cg_enter_current_thread(const char *cgroup)
{
	return cg_write(cgroup, "cgroup.threads", "0");
}

int cg_run(const char *cgroup,
	   int (*fn)(const char *cgroup, void *arg),
	   void *arg)
{
	int pid, retcode;

	pid = fork();
	if (pid < 0) {
		return pid;
	} else if (pid == 0) {
		char buf[64];

		snprintf(buf, sizeof(buf), "%d", getpid());
		if (cg_write(cgroup, "cgroup.procs", buf))
			exit(EXIT_FAILURE);
		exit(fn(cgroup, arg));
	} else {
		waitpid(pid, &retcode, 0);
		if (WIFEXITED(retcode))
			return WEXITSTATUS(retcode);
		else
			return -1;
	}
}

pid_t clone_into_cgroup(int cgroup_fd)
{
#ifdef CLONE_ARGS_SIZE_VER2
	pid_t pid;

	struct __clone_args args = {
		.flags = CLONE_INTO_CGROUP,
		.exit_signal = SIGCHLD,
		.cgroup = cgroup_fd,
	};

	pid = sys_clone3(&args, sizeof(struct __clone_args));
	/*
	 * Verify that this is a genuine test failure:
	 * ENOSYS -> clone3() not available
	 * E2BIG  -> CLONE_INTO_CGROUP not available
	 */
	if (pid < 0 && (errno == ENOSYS || errno == E2BIG))
		goto pretend_enosys;

	return pid;

pretend_enosys:
#endif
	errno = ENOSYS;
	return -ENOSYS;
}

int clone_reap(pid_t pid, int options)
{
	int ret;
	siginfo_t info = {
		.si_signo = 0,
	};

again:
	ret = waitid(P_PID, pid, &info, options | __WALL | __WNOTHREAD);
	if (ret < 0) {
		if (errno == EINTR)
			goto again;
		return -1;
	}

	if (options & WEXITED) {
		if (WIFEXITED(info.si_status))
			return WEXITSTATUS(info.si_status);
	}

	if (options & WSTOPPED) {
		if (WIFSTOPPED(info.si_status))
			return WSTOPSIG(info.si_status);
	}

	if (options & WCONTINUED) {
		if (WIFCONTINUED(info.si_status))
			return 0;
	}

	return -1;
}

int dirfd_open_opath(const char *dir)
{
	return open(dir, O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW | O_PATH);
}

#define close_prot_errno(fd)                                                   \
	if (fd >= 0) {                                                         \
		int _e_ = errno;                                               \
		close(fd);                                                     \
		errno = _e_;                                                   \
	}

static int clone_into_cgroup_run_nowait(const char *cgroup,
					int (*fn)(const char *cgroup, void *arg),
					void *arg)
{
	int cgroup_fd;
	pid_t pid;

	cgroup_fd =  dirfd_open_opath(cgroup);
	if (cgroup_fd < 0)
		return -1;

	pid = clone_into_cgroup(cgroup_fd);
	close_prot_errno(cgroup_fd);
	if (pid == 0)
		exit(fn(cgroup, arg));

	return pid;
}

int cg_run_nowait(const char *cgroup,
		  int (*fn)(const char *cgroup, void *arg),
		  void *arg)
{
	int pid;

	pid = clone_into_cgroup_run_nowait(cgroup, fn, arg);
	if (pid > 0)
		return pid;

	/* Genuine test failure. */
	if (pid < 0 && errno != ENOSYS)
		return -1;

	pid = fork();
	if (pid == 0) {
		char buf[64];

		snprintf(buf, sizeof(buf), "%d", getpid());
		if (cg_write(cgroup, "cgroup.procs", buf))
			exit(EXIT_FAILURE);
		exit(fn(cgroup, arg));
	}

	return pid;
}

int get_temp_fd(void)
{
	return open(".", O_TMPFILE | O_RDWR | O_EXCL);
}

int alloc_pagecache(int fd, size_t size)
{
	char buf[PAGE_SIZE];
	struct stat st;
	int i;

	if (fstat(fd, &st))
		goto cleanup;

	size += st.st_size;

	if (ftruncate(fd, size))
		goto cleanup;

	for (i = 0; i < size; i += sizeof(buf))
		read(fd, buf, sizeof(buf));

	return 0;

cleanup:
	return -1;
}

int alloc_anon(const char *cgroup, void *arg)
{
	size_t size = (unsigned long)arg;
	char *buf, *ptr;

	buf = malloc(size);
	for (ptr = buf; ptr < buf + size; ptr += PAGE_SIZE)
		*ptr = 0;

	free(buf);
	return 0;
}

int is_swap_enabled(void)
{
	char buf[PAGE_SIZE];
	const char delim[] = "\n";
	int cnt = 0;
	char *line;

	if (read_text("/proc/swaps", buf, sizeof(buf)) <= 0)
		return -1;

	for (line = strtok(buf, delim); line; line = strtok(NULL, delim))
		cnt++;

	return cnt > 1;
}

int set_oom_adj_score(int pid, int score)
{
	char path[PATH_MAX];
	int fd, len;

	sprintf(path, "/proc/%d/oom_score_adj", pid);

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd < 0)
		return fd;

	len = dprintf(fd, "%d", score);
	if (len < 0) {
		close(fd);
		return len;
	}

	close(fd);
	return 0;
}

ssize_t proc_read_text(int pid, bool thread, const char *item, char *buf, size_t size)
{
	char path[PATH_MAX];

	if (!pid)
		snprintf(path, sizeof(path), "/proc/%s/%s",
			 thread ? "thread-self" : "self", item);
	else
		snprintf(path, sizeof(path), "/proc/%d/%s", pid, item);

	return read_text(path, buf, size);
}

int proc_read_strstr(int pid, bool thread, const char *item, const char *needle)
{
	char buf[PAGE_SIZE];

	if (proc_read_text(pid, thread, item, buf, sizeof(buf)) < 0)
		return -1;

	return strstr(buf, needle) ? 0 : -1;
}

int clone_into_cgroup_run_wait(const char *cgroup)
{
	int cgroup_fd;
	pid_t pid;

	cgroup_fd =  dirfd_open_opath(cgroup);
	if (cgroup_fd < 0)
		return -1;

	pid = clone_into_cgroup(cgroup_fd);
	close_prot_errno(cgroup_fd);
	if (pid < 0)
		return -1;

	if (pid == 0)
		exit(EXIT_SUCCESS);

	/*
	 * We don't care whether this fails. We only care whether the initial
	 * clone succeeded.
	 */
	(void)clone_reap(pid, WEXITED);
	return 0;
}
