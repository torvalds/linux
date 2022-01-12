// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <test_progs.h>
#include <sys/stat.h>
#include <linux/sched.h>
#include <sys/syscall.h>

#define MAX_PATH_LEN		128
#define MAX_FILES		7

#include "test_d_path.skel.h"
#include "test_d_path_check_rdonly_mem.skel.h"

static int duration;

static struct {
	__u32 cnt;
	char paths[MAX_FILES][MAX_PATH_LEN];
} src;

static int set_pathname(int fd, pid_t pid)
{
	char buf[MAX_PATH_LEN];

	snprintf(buf, MAX_PATH_LEN, "/proc/%d/fd/%d", pid, fd);
	return readlink(buf, src.paths[src.cnt++], MAX_PATH_LEN);
}

static int trigger_fstat_events(pid_t pid)
{
	int sockfd = -1, procfd = -1, devfd = -1;
	int localfd = -1, indicatorfd = -1;
	int pipefd[2] = { -1, -1 };
	struct stat fileStat;
	int ret = -1;

	/* unmountable pseudo-filesystems */
	if (CHECK(pipe(pipefd) < 0, "trigger", "pipe failed\n"))
		return ret;
	/* unmountable pseudo-filesystems */
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (CHECK(sockfd < 0, "trigger", "socket failed\n"))
		goto out_close;
	/* mountable pseudo-filesystems */
	procfd = open("/proc/self/comm", O_RDONLY);
	if (CHECK(procfd < 0, "trigger", "open /proc/self/comm failed\n"))
		goto out_close;
	devfd = open("/dev/urandom", O_RDONLY);
	if (CHECK(devfd < 0, "trigger", "open /dev/urandom failed\n"))
		goto out_close;
	localfd = open("/tmp/d_path_loadgen.txt", O_CREAT | O_RDONLY, 0644);
	if (CHECK(localfd < 0, "trigger", "open /tmp/d_path_loadgen.txt failed\n"))
		goto out_close;
	/* bpf_d_path will return path with (deleted) */
	remove("/tmp/d_path_loadgen.txt");
	indicatorfd = open("/tmp/", O_PATH);
	if (CHECK(indicatorfd < 0, "trigger", "open /tmp/ failed\n"))
		goto out_close;

	ret = set_pathname(pipefd[0], pid);
	if (CHECK(ret < 0, "trigger", "set_pathname failed for pipe[0]\n"))
		goto out_close;
	ret = set_pathname(pipefd[1], pid);
	if (CHECK(ret < 0, "trigger", "set_pathname failed for pipe[1]\n"))
		goto out_close;
	ret = set_pathname(sockfd, pid);
	if (CHECK(ret < 0, "trigger", "set_pathname failed for socket\n"))
		goto out_close;
	ret = set_pathname(procfd, pid);
	if (CHECK(ret < 0, "trigger", "set_pathname failed for proc\n"))
		goto out_close;
	ret = set_pathname(devfd, pid);
	if (CHECK(ret < 0, "trigger", "set_pathname failed for dev\n"))
		goto out_close;
	ret = set_pathname(localfd, pid);
	if (CHECK(ret < 0, "trigger", "set_pathname failed for file\n"))
		goto out_close;
	ret = set_pathname(indicatorfd, pid);
	if (CHECK(ret < 0, "trigger", "set_pathname failed for dir\n"))
		goto out_close;

	/* triggers vfs_getattr */
	fstat(pipefd[0], &fileStat);
	fstat(pipefd[1], &fileStat);
	fstat(sockfd, &fileStat);
	fstat(procfd, &fileStat);
	fstat(devfd, &fileStat);
	fstat(localfd, &fileStat);
	fstat(indicatorfd, &fileStat);

out_close:
	/* triggers filp_close */
	close(pipefd[0]);
	close(pipefd[1]);
	close(sockfd);
	close(procfd);
	close(devfd);
	close(localfd);
	close(indicatorfd);
	return ret;
}

static void test_d_path_basic(void)
{
	struct test_d_path__bss *bss;
	struct test_d_path *skel;
	int err;

	skel = test_d_path__open_and_load();
	if (CHECK(!skel, "setup", "d_path skeleton failed\n"))
		goto cleanup;

	err = test_d_path__attach(skel);
	if (CHECK(err, "setup", "attach failed: %d\n", err))
		goto cleanup;

	bss = skel->bss;
	bss->my_pid = getpid();

	err = trigger_fstat_events(bss->my_pid);
	if (err < 0)
		goto cleanup;

	if (CHECK(!bss->called_stat,
		  "stat",
		  "trampoline for security_inode_getattr was not called\n"))
		goto cleanup;

	if (CHECK(!bss->called_close,
		  "close",
		  "trampoline for filp_close was not called\n"))
		goto cleanup;

	for (int i = 0; i < MAX_FILES; i++) {
		CHECK(strncmp(src.paths[i], bss->paths_stat[i], MAX_PATH_LEN),
		      "check",
		      "failed to get stat path[%d]: %s vs %s\n",
		      i, src.paths[i], bss->paths_stat[i]);
		CHECK(strncmp(src.paths[i], bss->paths_close[i], MAX_PATH_LEN),
		      "check",
		      "failed to get close path[%d]: %s vs %s\n",
		      i, src.paths[i], bss->paths_close[i]);
		/* The d_path helper returns size plus NUL char, hence + 1 */
		CHECK(bss->rets_stat[i] != strlen(bss->paths_stat[i]) + 1,
		      "check",
		      "failed to match stat return [%d]: %d vs %zd [%s]\n",
		      i, bss->rets_stat[i], strlen(bss->paths_stat[i]) + 1,
		      bss->paths_stat[i]);
		CHECK(bss->rets_close[i] != strlen(bss->paths_stat[i]) + 1,
		      "check",
		      "failed to match stat return [%d]: %d vs %zd [%s]\n",
		      i, bss->rets_close[i], strlen(bss->paths_close[i]) + 1,
		      bss->paths_stat[i]);
	}

cleanup:
	test_d_path__destroy(skel);
}

static void test_d_path_check_rdonly_mem(void)
{
	struct test_d_path_check_rdonly_mem *skel;

	skel = test_d_path_check_rdonly_mem__open_and_load();
	ASSERT_ERR_PTR(skel, "unexpected_load_overwriting_rdonly_mem");

	test_d_path_check_rdonly_mem__destroy(skel);
}

void test_d_path(void)
{
	if (test__start_subtest("basic"))
		test_d_path_basic();

	if (test__start_subtest("check_rdonly_mem"))
		test_d_path_check_rdonly_mem();
}
